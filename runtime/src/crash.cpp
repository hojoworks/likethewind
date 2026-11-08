/*
 * crash.cpp — process-wide crash trap.
 *
 * Writes crash.log (and a timestamped copy) next to the executable with the
 * exception, registers, host stack, emulated CPU state and the last guest
 * routine/instruction entries. Works for unhandled SEH exceptions,
 * std::terminate and fatal signals.
 */
#include "likethewind/crash.h"

#include "likethewind/audio.h"
#include "likethewind/machine.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <exception>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace likethewind {
namespace {

#ifdef _WIN32

volatile LONG g_crashing = 0;
char g_log[262144];
size_t g_log_len = 0;

volatile LONG64 g_last_heartbeat = 0;
ULONGLONG g_start_tick = 0;
volatile LONG g_note_index = 0;
constexpr int kNoteCount = 96;
char g_notes[kNoteCount][192] = {};
DWORD g_watchdog_timeout_ms = 30000;

void append(const char *s) {
    const size_t n = std::strlen(s);
    if (g_log_len + n >= sizeof(g_log)) return;
    std::memcpy(g_log + g_log_len, s, n);
    g_log_len += n;
    g_log[g_log_len] = 0;
}

void appendf(const char *fmt, ...) {
    if (g_log_len + 1 >= sizeof(g_log)) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(g_log + g_log_len, sizeof(g_log) - g_log_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        const size_t used = std::min<size_t>((size_t)n, sizeof(g_log) - g_log_len - 1);
        g_log_len += used;
    }
}

const char *exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case 0xC0000409: return "STACK_BUFFER_OVERRUN";
    case 0xE06D7363: return "CPP_EXCEPTION";
    default: return "UNKNOWN";
    }
}

// ---- optional DbgHelp symbolization (loaded dynamically) ------------------

struct SymInfo {
    ULONG SizeOfStruct;
    ULONG TypeIndex;
    ULONG64 Reserved[2];
    ULONG Index;
    ULONG Size;
    ULONG64 ModBase;
    ULONG Flags;
    ULONG64 Value;
    ULONG64 Address;
    ULONG Register;
    ULONG Scope;
    ULONG Tag;
    ULONG NameLen;
    ULONG MaxNameLen;
    CHAR Name[1];
};

struct SymLine {
    DWORD SizeOfStruct;
    PVOID Key;
    DWORD LineNumber;
    PCHAR FileName;
    DWORD64 Address;
};

struct Addr64 {
    DWORD64 Offset;
    WORD Segment;
    DWORD Mode;
};

struct StackFrame64 {
    Addr64 AddrPC, AddrReturn, AddrFrame, AddrStack, AddrBStore;
    PVOID FuncTableEntry;
    DWORD64 Params[4];
    BOOL Far;
    BOOL Virtual;
    DWORD64 Reserved[3];
};

HMODULE g_dbghelp = nullptr;
HANDLE g_process = nullptr;
bool g_sym_ready = false;

typedef BOOL(WINAPI *SymInitFn)(HANDLE, PCSTR, BOOL);
typedef DWORD(WINAPI *SymSetOptFn)(DWORD);
typedef BOOL(WINAPI *SymFromAddrFn)(HANDLE, DWORD64, PDWORD64, SymInfo *);
typedef BOOL(WINAPI *SymGetLineFn)(HANDLE, DWORD64, PDWORD, SymLine *);
typedef PVOID(WINAPI *SymFuncTableFn)(HANDLE, DWORD64);
typedef DWORD64(WINAPI *SymModBaseFn)(HANDLE, DWORD64);
typedef BOOL(WINAPI *StackWalkFn)(DWORD, HANDLE, HANDLE, StackFrame64 *, PVOID, PVOID,
                                  PVOID, PVOID, PVOID);

SymFromAddrFn g_sym_from = nullptr;
SymGetLineFn g_sym_line = nullptr;
SymFuncTableFn g_sym_table = nullptr;
SymModBaseFn g_sym_base = nullptr;
StackWalkFn g_stack_walk = nullptr;

void init_symbols() {
    g_dbghelp = LoadLibraryA("dbghelp.dll");
    if (!g_dbghelp) return;
    auto sym_init = (SymInitFn)GetProcAddress(g_dbghelp, "SymInitialize");
    auto sym_set = (SymSetOptFn)GetProcAddress(g_dbghelp, "SymSetOptions");
    g_sym_from = (SymFromAddrFn)GetProcAddress(g_dbghelp, "SymFromAddr");
    g_sym_line = (SymGetLineFn)GetProcAddress(g_dbghelp, "SymGetLineFromAddr64");
    g_sym_table = (SymFuncTableFn)GetProcAddress(g_dbghelp, "SymFunctionTableAccess64");
    g_sym_base = (SymModBaseFn)GetProcAddress(g_dbghelp, "SymGetModuleBase64");
    g_stack_walk = (StackWalkFn)GetProcAddress(g_dbghelp, "StackWalk64");
    g_process = GetCurrentProcess();
    if (sym_set) sym_set(0x00000002 | 0x00000010);  // UNDNAME | LOAD_LINES
    // Point DbgHelp at the executable's own directory. The app is often run
    // from the repo root while LikeTheWind.pdb sits next to the exe in build\.
    char exe[MAX_PATH] = {};
    char dir[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, exe, sizeof(exe));
    if (n && n < sizeof(exe)) {
        std::snprintf(dir, sizeof(dir), "%s", exe);
        char *slash = std::strrchr(dir, '\\');
        if (slash) *slash = 0;
    }
    if (sym_init && sym_init(g_process, dir[0] ? dir : nullptr, TRUE)) g_sym_ready = true;
}

void append_symbol(DWORD64 addr, const char *prefix) {
    if (g_sym_ready && g_sym_from) {
        alignas(8) char buf[sizeof(SymInfo) + 512] = {};
        auto *si = (SymInfo *)buf;
        si->SizeOfStruct = sizeof(SymInfo);
        si->MaxNameLen = 511;
        DWORD64 disp = 0;
        if (g_sym_from(g_process, addr, &disp, si)) {
            appendf("%s%s+0x%llX", prefix, si->Name, (unsigned long long)disp);
            if (g_sym_line) {
                SymLine li{};
                li.SizeOfStruct = sizeof(SymLine);
                DWORD ld = 0;
                if (g_sym_line(g_process, addr, &ld, &li) && li.FileName)
                    appendf(" (%s:%lu)", li.FileName, (unsigned long)li.LineNumber);
            }
            append("\n");
            return;
        }
    }
    HMODULE mod = nullptr;
    char path[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)addr, &mod)) {
        GetModuleFileNameA(mod, path, sizeof(path));
        const char *base = std::strrchr(path, '\\');
        base = base ? base + 1 : path;
        appendf("%s%s+0x%llX\n", prefix, base,
                (unsigned long long)(addr - (DWORD64)(uintptr_t)mod));
    } else {
        appendf("%s0x%llX\n", prefix, (unsigned long long)addr);
    }
}

void append_stack(EXCEPTION_POINTERS *ep) {
    append("host stack:\n");
#ifdef _WIN64
    if (ep && ep->ContextRecord && g_sym_ready && g_stack_walk && g_sym_table && g_sym_base) {
        CONTEXT *ctx = ep->ContextRecord;
        StackFrame64 sf{};
        sf.AddrPC.Offset = ctx->Rip;
        sf.AddrPC.Mode = 3;  // AddrModeFlat
        sf.AddrFrame.Offset = ctx->Rbp;
        sf.AddrFrame.Mode = 3;
        sf.AddrStack.Offset = ctx->Rsp;
        sf.AddrStack.Mode = 3;
        bool any = false;
        for (int i = 0; i < 64; i++) {
            if (!g_stack_walk(IMAGE_FILE_MACHINE_AMD64, g_process, GetCurrentThread(), &sf,
                              ctx, nullptr, (PVOID)g_sym_table, (PVOID)g_sym_base, nullptr))
                break;
            if (!sf.AddrPC.Offset) break;
            char p[24];
            std::snprintf(p, sizeof(p), "  [%02d] ", i);
            append_symbol(sf.AddrPC.Offset, p);
            any = true;
        }
        if (any) return;
    }
#endif
    void *frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < n; i++) {
        char p[24];
        std::snprintf(p, sizeof(p), "  [%02d] ", (int)i);
        append_symbol((DWORD64)(uintptr_t)frames[i], p);
    }
}

void append_machine() {
    Machine *m = g_machine;
    appendf("machine: %s\n", m ? "active" : "not created");
    if (!m) return;
    appendf("  frame=%lld line=%lld active_cpu=%d view=%d x0=%d wide=%d\n",
            (long long)m->frame, (long long)m->line, m->active,
            m->view_width, m->view_x0, m->widescreen ? 1 : 0);
    appendf("  dips SWA=%02X SWB=%02X  general=%04X wheel=%02X gas=%02X brake=%02X\n",
            m->dip_a, m->dip_b, m->in_general, m->in_wheel, m->in_gas, m->in_brake);
    appendf("  start_gate=%d armed=%d pending=%d display=%d\n",
            m->start_gate ? 1 : 0, m->start_gate_armed ? 1 : 0,
            m->start_pending ? 1 : 0, m->display_enable ? 1 : 0);
    appendf("  shared mode=%d course=%d stage=%d credits=%u "
            "score=%08X time=%02X:%02X.%02X handshake=%02X cff12=%02X cf810=%02X\n",
            (m->shared[0xE09C] << 8) | m->shared[0xE09D],
            (m->shared[0xEA2A] << 8) | m->shared[0xEA2B],
            (m->shared[0xEA38] << 8) | m->shared[0xEA39],
            (unsigned)m->shared[0xE852],
            ((uint32_t)m->shared[0xFF60] << 24) |
                ((uint32_t)m->shared[0xFF61] << 16) |
                ((uint32_t)m->shared[0xFF62] << 8) |
                (uint32_t)m->shared[0xFF63],
            (unsigned)m->shared[0xE8A0], (unsigned)m->shared[0xE8A1],
            (unsigned)m->shared[0xE8A2], (unsigned)m->shared[0xFF0E],
            (unsigned)m->shared[0xFF12], (unsigned)m->shared[0xF810]);
    for (int i = 0; i < CPU_COUNT; i++) {
        const Cpu &c = m->cpus[i];
        appendf("  cpu%d pc=%06X cycles=%lld ipl=%d irq=%d reset=%d stopped=%d "
                "irq2=%llu irq4=%llu\n",
                i, c.state.pc, (long long)c.cycles, c.state.ipl, c.irq,
                c.reset_held ? 1 : 0, c.state.stopped,
                (unsigned long long)m->irqCount[i][2],
                (unsigned long long)m->irqCount[i][4]);
    }
    if (Audio *a = m->audio()) {
        appendf("  audio z80 pc=%04X cycles=%llu\n", a->z80_pc(),
                (unsigned long long)a->z80_cycles());
    }
    const uint64_t count = m->guest_trace_count;
    const uint64_t start = count > (uint64_t)Machine::kGuestTrace
                               ? count - (uint64_t)Machine::kGuestTrace
                               : 0;
    appendf("guest trace (%llu entries, showing last %llu):\n",
            (unsigned long long)count, (unsigned long long)(count - start));
    for (uint64_t i = start; i < count; i++) {
        const Machine::GuestTrace &e = m->guest_trace[i % Machine::kGuestTrace];
        appendf("  %llu f%llu l%u cpu%u pc %06X cyc %u\n",
                (unsigned long long)i, (unsigned long long)e.frame, e.line, e.cpu,
                e.pc, e.cycles);
    }
}

void write_file(const char *path) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, g_log, (DWORD)g_log_len, &written, nullptr);
    CloseHandle(h);
}

// DbgHelp's minidump entry points are loaded dynamically so a missing
// dbghelp.dll cannot prevent startup or the text report.
typedef BOOL(WINAPI *MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, DWORD,
                                          void *, void *, void *);

struct MiniDumpExceptionInfo {
    DWORD ThreadId;
    EXCEPTION_POINTERS *ExceptionPointers;
    BOOL ClientPointers;
};

const char *g_dump_stage = "";
DWORD g_dump_error = 0;

bool write_minidump(const char *path, EXCEPTION_POINTERS *ep) {
    g_dump_stage = "";
    g_dump_error = 0;
    HMODULE dbg = g_dbghelp ? g_dbghelp : LoadLibraryA("dbghelp.dll");
    if (!dbg) {
        g_dump_stage = "load dbghelp";
        g_dump_error = GetLastError();
        return false;
    }
    auto dump = (MiniDumpWriteDumpFn)GetProcAddress(dbg, "MiniDumpWriteDump");
    if (!dump) {
        g_dump_stage = "find MiniDumpWriteDump";
        g_dump_error = GetLastError();
        return false;
    }
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        g_dump_stage = "create dump file";
        g_dump_error = GetLastError();
        return false;
    }
    MiniDumpExceptionInfo info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = ep;
    info.ClientPointers = FALSE;
    // Thread info, indirectly referenced memory, unloaded modules, full
    // memory-info and tolerance for inaccessible pages.  Deliberately not
    // full-memory: the dump stays useful for support without being enormous.
    const DWORD flags = 0x00001000 | 0x00000040 | 0x00000020;
    const BOOL ok = dump(GetCurrentProcess(), GetCurrentProcessId(), h, flags,
                         nullptr, nullptr, nullptr);
    if (!ok) {
        g_dump_stage = "MiniDumpWriteDump";
        g_dump_error = GetLastError();
    }
    CloseHandle(h);
    return ok != FALSE;
}

void append_notes() {
    append("recent host notes:\n");
    const LONG end = InterlockedCompareExchange(&g_note_index, 0, 0);
    const LONG start = end > kNoteCount ? end - kNoteCount : 0;
    if (end == start) {
        append("  (none)\n");
        return;
    }
    for (LONG i = start; i < end; ++i) {
        const LONG slot = i % kNoteCount;
        if (g_notes[slot][0])
            appendf("  [%ld] %s\n", (long)i, g_notes[slot]);
    }
}

void write_log(const char *reason, const char *detail, EXCEPTION_POINTERS *ep) {
    g_log_len = 0;
    SYSTEMTIME st;
    GetLocalTime(&st);
    appendf("=== LikeTheWind crash report ===\n");
    appendf("time: %04u-%02u-%02u %02u:%02u:%02u.%03u\n", st.wYear, st.wMonth,
            st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    appendf("pid: %lu thread: %lu\n", (unsigned long)GetCurrentProcessId(),
            (unsigned long)GetCurrentThreadId());
    appendf("uptime: %llu ms\n",
            (unsigned long long)(GetTickCount64() -
                                 (g_start_tick ? g_start_tick
                                               : GetTickCount64())));
    appendf("command: %s\n", GetCommandLineA());
    appendf("reason: %s\n", reason);
    if (detail) appendf("detail: %s\n", detail);
    appendf("last error: %lu\n", (unsigned long)GetLastError());

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    appendf("system: %u processors, page %u, arch %u\n",
            (unsigned)si.dwNumberOfProcessors, (unsigned)si.dwPageSize,
            (unsigned)si.wProcessorArchitecture);
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        appendf("memory: %llu/%llu MB free/total, %lu%% load\n",
                (unsigned long long)(ms.ullAvailPhys >> 20),
                (unsigned long long)(ms.ullTotalPhys >> 20),
                (unsigned long)ms.dwMemoryLoad);
    }

    if (ep && ep->ExceptionRecord) {
        EXCEPTION_RECORD *er = ep->ExceptionRecord;
        appendf("exception: 0x%08lX (%s) at %p\n", (unsigned long)er->ExceptionCode,
                exception_name(er->ExceptionCode), er->ExceptionAddress);
        if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
            appendf("access: %s at %p\n",
                    er->ExceptionInformation[0] ? "write" : "read",
                    (void *)(uintptr_t)er->ExceptionInformation[1]);
        }
        appendf("exception flags: 0x%08lX parameters: %lu\n",
                (unsigned long)er->ExceptionFlags,
                (unsigned long)er->NumberParameters);
        for (DWORD i = 0; i < er->NumberParameters && i < 15; ++i)
            appendf("  parameter[%lu]: 0x%llX\n", (unsigned long)i,
                    (unsigned long long)er->ExceptionInformation[i]);
        for (EXCEPTION_RECORD *nested = er->ExceptionRecord; nested;
             nested = nested->ExceptionRecord)
            appendf("nested exception: 0x%08lX at %p\n",
                    (unsigned long)nested->ExceptionCode,
                    nested->ExceptionAddress);

        HMODULE fault_mod = nullptr;
        char fault_path[MAX_PATH] = {};
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)(uintptr_t)er->ExceptionAddress, &fault_mod)) {
            GetModuleFileNameA(fault_mod, fault_path, sizeof(fault_path));
            appendf("fault module: %s base=%p\n", fault_path, (void *)fault_mod);
        }

#ifdef _WIN64
        if (ep->ContextRecord) {
            CONTEXT *c = ep->ContextRecord;
            appendf("registers: RIP=%016llX RSP=%016llX RBP=%016llX\n",
                    (unsigned long long)c->Rip, (unsigned long long)c->Rsp,
                    (unsigned long long)c->Rbp);
            appendf("           RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX\n",
                    (unsigned long long)c->Rax, (unsigned long long)c->Rbx,
                    (unsigned long long)c->Rcx, (unsigned long long)c->Rdx);
            appendf("           RSI=%016llX RDI=%016llX R8=%016llX R9=%016llX\n",
                    (unsigned long long)c->Rsi, (unsigned long long)c->Rdi,
                    (unsigned long long)c->R8, (unsigned long long)c->R9);
        }
#endif
    }
    append_stack(ep);
    append("minidump: captures all process threads; host stack above is the reporting thread\n");
    append_machine();
    append_notes();

    char exe[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(nullptr, exe, sizeof(exe));
    char dir[MAX_PATH] = ".";
    if (n && n < sizeof(exe)) {
        char *slash = std::strrchr(exe, '\\');
        if (slash) {
            *slash = 0;
            std::snprintf(dir, sizeof(dir), "%s", exe);
        }
    }
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "crash-%04u%02u%02u-%02u%02u%02u-%03u.log",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                  st.wMilliseconds);
    char path1[MAX_PATH], path2[MAX_PATH];
    char dump1[MAX_PATH], dump2[MAX_PATH];
    std::snprintf(path1, sizeof(path1), "%s\\%s", dir, stamp);
    std::snprintf(path2, sizeof(path2), "%s\\crash.log", dir);
    std::snprintf(dump1, sizeof(dump1), "%s\\%s.dmp", dir, stamp);
    std::snprintf(dump2, sizeof(dump2), "%s\\crash.dmp", dir);
    if (write_minidump(dump1, ep)) {
        CopyFileA(dump1, dump2, FALSE);
        appendf("minidump: %s\n", dump1);
    } else {
        appendf("minidump: unavailable (%s, error %lu)\n",
                g_dump_stage && g_dump_stage[0] ? g_dump_stage : "unknown",
                (unsigned long)g_dump_error);
    }
    appendf("report: %s\n", path1);
    write_file(path1);
    write_file(path2);
    std::fwrite(g_log, 1, g_log_len, stderr);
    std::fflush(stderr);
}

DWORD WINAPI watchdog_thread(LPVOID) {
    for (;;) {
        Sleep(2000);
        if (g_crashing) return 0;
        const LONG64 last = InterlockedCompareExchange64(&g_last_heartbeat, 0, 0);
        if (!last) continue;
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG age = now - (ULONGLONG)last;
        if (age <= g_watchdog_timeout_ms) continue;
        if (InterlockedExchange(&g_crashing, 1) == 0) {
            char detail[128];
            std::snprintf(detail, sizeof(detail),
                          "no emulated frame for %llu ms (timeout %lu ms)",
                          (unsigned long long)age,
                          (unsigned long)g_watchdog_timeout_ms);
            write_log("hang watchdog", detail, nullptr);
        }
        TerminateProcess(GetCurrentProcess(), 4);
    }
}

LONG WINAPI on_unhandled(EXCEPTION_POINTERS *ep) {
    if (InterlockedExchange(&g_crashing, 1)) {
        TerminateProcess(GetCurrentProcess(), 3);
    }
    write_log("unhandled exception", nullptr, ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

void on_terminate() {
    if (InterlockedExchange(&g_crashing, 1)) {
        TerminateProcess(GetCurrentProcess(), 3);
    }
    static char what[512] = "unknown";
    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception &ex) {
            std::snprintf(what, sizeof(what), "%s", ex.what());
        } catch (...) {
            std::snprintf(what, sizeof(what), "non-std exception");
        }
    }
    write_log("std::terminate", what, nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
}

void on_signal(int sig) {
    if (InterlockedExchange(&g_crashing, 1)) {
        TerminateProcess(GetCurrentProcess(), 3);
    }
    char detail[64];
    std::snprintf(detail, sizeof(detail), "signal %d", sig);
    write_log("fatal signal", detail, nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
}

#else  // !_WIN32

void write_log(const char *reason, const char *detail, void *) {
    std::FILE *f = std::fopen("crash.log", "a");
    if (f) {
        std::fprintf(f, "LikeTheWind crash: %s%s%s\n", reason,
                     detail ? ": " : "", detail ? detail : "");
        std::fclose(f);
    }
    std::fprintf(stderr, "LikeTheWind crash: %s%s%s\n", reason,
                 detail ? ": " : "", detail ? detail : "");
}

void on_terminate() {
    static char what[512] = "unknown";
    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception &ex) {
            std::snprintf(what, sizeof(what), "%s", ex.what());
        } catch (...) {
            std::snprintf(what, sizeof(what), "non-std exception");
        }
    }
    write_log("std::terminate", what, nullptr);
    std::_Exit(3);
}

void on_signal(int sig) {
    char detail[64];
    std::snprintf(detail, sizeof(detail), "signal %d", sig);
    write_log("fatal signal", detail, nullptr);
    std::_Exit(3);
}

#endif

} // namespace

void crash_heartbeat() {
#ifdef _WIN32
    InterlockedExchange64(&g_last_heartbeat, (LONG64)GetTickCount64());
#endif
}

void crash_note(const char *fmt, ...) {
#ifdef _WIN32
    if (!fmt) return;
    char line[192] = {};
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    const LONG slot = (InterlockedIncrement(&g_note_index) - 1) % kNoteCount;
    std::snprintf(g_notes[slot], sizeof(g_notes[slot]), "%s", line);
#else
    (void)fmt;
#endif
}

void install_crash_handler() {
    std::set_terminate(on_terminate);
#ifdef _WIN32
    g_start_tick = GetTickCount64();
    crash_heartbeat();
    init_symbols();
    SetUnhandledExceptionFilter(on_unhandled);
    // Do not install SIGSEGV/SIGFPE/SIGILL here: the CRT's signal handlers
    // would run before SEH and we would lose the exception context. The
    // unhandled-exception filter receives the fault address and registers.
    std::signal(SIGABRT, on_signal);
    if (const char *timeout = std::getenv("LIKETHEWIND_HANG_TIMEOUT")) {
        long seconds = std::strtol(timeout, nullptr, 10);
        if (seconds < 5) seconds = 5;
        if (seconds > 600) seconds = 600;
        g_watchdog_timeout_ms = (DWORD)(seconds * 1000);
    }
    if (!std::getenv("LIKETHEWIND_NO_WATCHDOG")) {
        HANDLE thread = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
#else
    std::signal(SIGABRT, on_signal);
    std::signal(SIGSEGV, on_signal);
    std::signal(SIGFPE, on_signal);
    std::signal(SIGILL, on_signal);
#endif
}

} // namespace likethewind
