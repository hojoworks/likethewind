/*
 * rom_prepare.cpp - built-in release ROM preparation.
 *
 * A release is just LikeTheWind.exe, SDL2.dll and a roms/ folder. On launch we
 * look for roms/pdrift.zip, verify every file by size and CRC-32, and unpack
 * the board's flat images into roms/.prepared. No Python or command line is
 * needed. The table is generated from tools/romsets.py at build time.
 */
#include "likethewind/rom_prepare.h"

#include "miniz.h"
#include "rom_table.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#endif

namespace likethewind {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

std::string lower_ascii(std::string value) {
    for (char &c : value) c = (char)std::tolower((unsigned char)c);
    return value;
}

std::string base_name(const std::string &path) {
    const size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

uint32_t crc32_of(const Bytes &data) {
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, data.data(), data.size());
}

struct Sha1 {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                     0xC3D2E1F0u};
    uint64_t total = 0;
    uint8_t block[64] = {};
    size_t used = 0;

    static uint32_t rol(uint32_t value, int bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    void process(const uint8_t *p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
                   ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

    void update(const uint8_t *p, size_t n) {
        total += n;
        while (n) {
            const size_t take = std::min(n, sizeof(block) - used);
            std::memcpy(block + used, p, take);
            used += take;
            p += take;
            n -= take;
            if (used == sizeof(block)) {
                process(block);
                used = 0;
            }
        }
    }

    std::string hex() {
        const uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (used != 56) update(&zero, 1);
        uint8_t length[8];
        for (int i = 0; i < 8; ++i)
            length[i] = (uint8_t)(bits >> (56 - i * 8));
        update(length, 8);

        char out[41];
        for (int i = 0; i < 5; ++i)
            std::snprintf(out + i * 8, 9, "%08x", h[i]);
        return std::string(out, 40);
    }
};

std::string sha1_hex(const Bytes &data) {
    Sha1 sha;
    if (!data.empty()) sha.update(data.data(), data.size());
    return sha.hex();
}

bool write_file(const fs::path &path, const Bytes &data, std::string &error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "could not create " + path.string();
        return false;
    }
    if (!data.empty()) out.write((const char *)data.data(), (std::streamsize)data.size());
    if (!out) {
        error = "could not write " + path.string();
        return false;
    }
    return true;
}

bool read_zip(const fs::path &zip_path, std::map<std::string, Bytes> &sources,
              std::string &error) {
    mz_zip_archive zip{};
    const std::string zip_name = zip_path.string();
    if (!mz_zip_reader_init_file(&zip, zip_name.c_str(), 0)) {
        error = "could not open " + zip_name;
        return false;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        const std::string name = lower_ascii(base_name(stat.m_filename));
        if (name.empty()) continue;
        Bytes data((size_t)stat.m_uncomp_size);
        if (!mz_zip_reader_extract_to_mem(&zip, i, data.data(), data.size(), 0)) {
            mz_zip_reader_end(&zip);
            error = "could not extract " + name + " from " + zip_path.string();
            return false;
        }
        sources[name] = std::move(data);
    }
    mz_zip_reader_end(&zip);
    return true;
}

const Bytes *find_source(const std::map<std::string, Bytes> &sources,
                         const RomFileSpec &spec, std::string &used) {
    auto it = sources.find(lower_ascii(spec.name));
    if (it != sources.end()) {
        used = it->first;
        return &it->second;
    }
    // Real dumps sometimes use a different EPROM label. Match the same bytes.
    for (const auto &entry : sources) {
        if (entry.second.size() == spec.size && crc32_of(entry.second) == spec.crc) {
            used = entry.first;
            return &entry.second;
        }
    }
    return nullptr;
}

bool verify_file(const RomFileSpec &spec, const Bytes &data, std::string &error) {
    if (data.size() != spec.size) {
        error = std::string(spec.name) + ": expected " + std::to_string(spec.size) +
                " bytes, got " + std::to_string(data.size());
        return false;
    }
    const uint32_t crc = crc32_of(data);
    if (crc != spec.crc) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s: CRC-32 %08X, expected %08X",
                      spec.name, crc, spec.crc);
        error = buf;
        return false;
    }
    const std::string sha = sha1_hex(data);
    if (sha != spec.sha1) {
        error = std::string(spec.name) + ": SHA-1 " + sha + ", expected " +
                spec.sha1;
        return false;
    }
    return true;
}

bool build_region(const RomRegionSpec &region,
                  const std::map<std::string, Bytes> &sources, Bytes &out,
                  std::string &error) {
    out.assign(region.size, region.fill);
    for (uint32_t i = 0; i < region.file_count; ++i) {
        const RomFileSpec &file = region.files[i];
        std::string used;
        const Bytes *data = find_source(sources, file, used);
        if (!data) {
            error = std::string(region.name) + ": missing file " + file.name;
            return false;
        }
        if (!verify_file(file, *data, error)) return false;

        switch (region.kind) {
        case RomInterleave::Flat:
            for (uint32_t copy = 0; copy < file.copies; ++copy) {
                const size_t at = (size_t)file.offset + (size_t)copy * file.size;
                if (at + file.size > out.size()) {
                    error = std::string(region.name) + ": copy past region end";
                    return false;
                }
                std::memcpy(out.data() + at, data->data(), file.size);
            }
            break;
        case RomInterleave::W16: {
            const uint32_t lane = file.offset & 1u;
            const uint32_t base = file.offset & ~1u;
            if ((size_t)base + (size_t)file.size * 2 > out.size()) {
                error = std::string(region.name) + ": file past region end";
                return false;
            }
            for (size_t j = 0; j < file.size; ++j)
                out[base + lane + j * 2] = (*data)[j];
            break;
        }
        case RomInterleave::X64: {
            const uint32_t lane = file.offset & 7u;
            const uint32_t base = file.offset & ~7u;
            if ((size_t)base + (size_t)file.size * 8 > out.size()) {
                error = std::string(region.name) + ": file past region end";
                return false;
            }
            for (size_t j = 0; j < file.size; ++j)
                out[base + lane + j * 8] = (*data)[j];
            break;
        }
        }
    }
    return true;
}

bool flat_files_valid(const fs::path &dir) {
    for (uint32_t i = 0; i < kRomRegionCount; ++i) {
        std::error_code ec;
        const fs::path path = dir / (std::string(kRomRegions[i].name) + ".bin");
        if (!fs::is_regular_file(path, ec)) return false;
        const auto size = fs::file_size(path, ec);
        if (ec || size != kRomRegions[i].size) return false;
    }
    return true;
}

std::string zip_stamp(const fs::path &zip) {
    std::error_code ec;
    const auto size = fs::file_size(zip, ec);
    const auto time = fs::last_write_time(zip, ec);
    return "romprep=1\nsize=" + std::to_string((unsigned long long)(ec ? 0 : size)) +
           "\ntime=" + std::to_string((long long)(ec ? 0 : time.time_since_epoch().count())) +
           "\n";
}

bool prepared_files_valid(const fs::path &dir) {
    return flat_files_valid(dir);
}

bool prepared_valid(const fs::path &dir, const std::string &stamp) {
    std::error_code ec;
    const fs::path marker = dir / "stamp.txt";
    if (!fs::is_regular_file(marker, ec)) return false;
    std::ifstream in(marker, std::ios::binary);
    if (!in) return false;
    std::string got((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    return got == stamp && prepared_files_valid(dir);
}

fs::path find_zip(const fs::path &roms) {
    std::error_code ec;
    fs::path fallback;
    for (const auto &entry : fs::directory_iterator(roms, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string ext = lower_ascii(entry.path().extension().string());
        if (ext != ".zip") continue;
        const std::string name = lower_ascii(entry.path().filename().string());
        if (name == "pdrift.zip") return entry.path();
        if (name.find("pdrift") != std::string::npos) fallback = entry.path();
        else if (fallback.empty()) fallback = entry.path();
    }
    return fallback;
}

void write_roms_readme(const fs::path &roms) {
    const fs::path readme = roms / "README.txt";
    std::error_code ec;
    if (fs::exists(readme, ec)) return;
    std::ofstream out(readme, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "LikeTheWind\n\n"
           "Put your MAME pdrift.zip (World, Rev A) in this folder, then run\n"
           "LikeTheWind.exe. The game checks the ROM and prepares it\n"
           "automatically on first launch; other revisions are rejected.\n";
}

bool prepare(const fs::path &zip, const fs::path &roms, const fs::path &prepared,
             std::string &error) {
    std::error_code ec;
    const fs::path tmp = roms / ".prepared.tmp";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) {
        error = "could not create " + tmp.string();
        return false;
    }

    std::map<std::string, Bytes> sources;
    if (!read_zip(zip, sources, error)) {
        fs::remove_all(tmp, ec);
        return false;
    }
    // pdrifta (World) is a different dump: its sub Y EPROMs do not have the
    // "a" suffix and do not match this recompilation.
    if (sources.count("epr-12019.54") && sources.count("epr-12018.53")) {
        error = "This looks like the MAME pdrifta (World) set, which uses "
                "different sub Y ROMs. LikeTheWind requires pdrift (World, Rev A).";
        fs::remove_all(tmp, ec);
        return false;
    }

    for (uint32_t i = 0; i < kRomRegionCount; ++i) {
        Bytes data;
        if (!build_region(kRomRegions[i], sources, data, error)) {
            fs::remove_all(tmp, ec);
            return false;
        }
        const fs::path out = tmp / (std::string(kRomRegions[i].name) + ".bin");
        if (!write_file(out, data, error)) {
            fs::remove_all(tmp, ec);
            return false;
        }
    }

    const std::string stamp = zip_stamp(zip);
    {
        std::ofstream out(tmp / "stamp.txt", std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "could not write " + (tmp / "stamp.txt").string();
            fs::remove_all(tmp, ec);
            return false;
        }
        out.write(stamp.data(), (std::streamsize)stamp.size());
    }

    fs::remove_all(prepared, ec);
    fs::rename(tmp, prepared, ec);
    if (ec) {
        error = "could not activate prepared ROMs: " + ec.message();
        fs::remove_all(tmp, ec);
        return false;
    }
    return true;
}

} // namespace

std::string executable_directory() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, path, (DWORD)sizeof(path));
    if (n > 0 && n < sizeof(path)) {
        return fs::path(path).parent_path().string();
    }
#elif defined(__linux__)
    char path[PATH_MAX] = {};
    const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = '\0';
        return fs::path(path).parent_path().string();
    }
#endif
    std::error_code ec;
    return fs::current_path(ec).string();
}

RomPrepareResult prepare_release_roms(const std::string &exe_dir) {
    RomPrepareResult result;
    std::error_code ec;
    const fs::path roms = fs::path(exe_dir) / "roms";
    fs::create_directories(roms, ec);
    if (ec) {
        result.error = "could not create " + roms.string();
        return result;
    }

    const fs::path prepared = roms / ".prepared";
    const fs::path zip = find_zip(roms);

    if (!zip.empty()) {
        const std::string stamp = zip_stamp(zip);
        if (prepared_valid(prepared, stamp)) {
            result.ok = true;
            result.rom_dir = prepared.string();
            return result;
        }
        std::string error;
        if (prepare(zip, roms, prepared, error)) {
            result.ok = true;
            result.prepared = true;
            result.rom_dir = prepared.string();
            return result;
        }
        if (flat_files_valid(roms)) {
            result.ok = true;
            result.rom_dir = roms.string();
            return result;
        }
        result.error = error.find("pdrift (World, Rev A)") != std::string::npos
                           ? error
                           : error + "\n\nLikeTheWind requires the MAME pdrift set "
                                     "(World, Rev A).";
        return result;
    }

    if (prepared_files_valid(prepared)) {
        result.ok = true;
        result.rom_dir = prepared.string();
        return result;
    }
    if (flat_files_valid(roms)) {
        result.ok = true;
        result.rom_dir = roms.string();
        return result;
    }

    write_roms_readme(roms);
    result.error = "Put your MAME pdrift.zip (World, Rev A) in:\n" +
                   roms.string() + "\n\nThen run LikeTheWind.exe again.";
    return result;
}

} // namespace likethewind
