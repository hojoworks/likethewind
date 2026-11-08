#requires -Version 5.1
<#
  LikeTheWind: bootstrap and build.

  Everything the build needs (Zig/Clang, SDL2, the 68000 recompiler, generated
  C) is local to this folder. No system-wide installs, no game data in the repo.

  Usage:
    powershell -ExecutionPolicy Bypass -File tools\build.ps1
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Mode selftest
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Mode game -Frames 600
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -EntriesOnly
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -SkipRecompile
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -FullDiscovery
    powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Package
#>
param(
    [ValidateSet('all','selftest','game')]
    [string]$Mode = 'all',
    [int]$Frames = 0,
    [switch]$Clean,
    [switch]$SkipDownload,
    [switch]$SkipRecompile,
    [switch]$FullDiscovery,
    [switch]$EntriesOnly,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

# Release packages must include the coverage-guided native entry seeds. A
# pointer/vector-only discovery build leaves thousands of routines on the
# interpreter fallback.
if ($Package -and -not $FullDiscovery) { $EntriesOnly = $true }

function Info($m) { Write-Host "[build] $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "[build] error: $m" -ForegroundColor Red; exit 1 }

$Build = Join-Path $Root 'build'
$Obj = Join-Path $Build 'obj'
$Gen = Join-Path $Root 'work\generated'
$GenTest = Join-Path $Root 'work\generated_test'
$ZigDir = Join-Path $Root 'vendor\zig'
$Zig = Join-Path $ZigDir 'zig.exe'

if ($Clean) {
    Info 'clean'
    Remove-Item -Recurse -Force $Build, $Gen, $GenTest -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $Build, $Obj, (Join-Path $Build 'tools') | Out-Null

# ---- 1. Zig (portable Clang + MinGW libs) ---------------------------------
if (-not (Test-Path $Zig)) {
    # A developer may already have a checkout in refs/; use it rather than
    # downloading twice.
    $candidate = Get-ChildItem (Join-Path $Root 'refs\zig') -Filter zig.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($candidate) {
        $Zig = $candidate
        Info "using $Zig"
    } elseif ($SkipDownload) {
        Fail 'Zig not found and -SkipDownload was given'
    } else {
        Info 'downloading Zig 0.14.1 (~82 MB)'
        $zip = Join-Path $env:TEMP 'likethewind-zig.zip'
        if (-not (Test-Path $zip)) {
            Invoke-WebRequest -Uri 'https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip' -OutFile $zip -UseBasicParsing
        }
        $tmp = Join-Path $env:TEMP 'likethewind-zig'
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        Expand-Archive -Path $zip -DestinationPath $tmp -Force
        New-Item -ItemType Directory -Force -Path $ZigDir | Out-Null
        Copy-Item (Join-Path $tmp 'zig-x86_64-windows-0.14.1\*') $ZigDir -Recurse -Force
        $Zig = Join-Path $ZigDir 'zig.exe'
    }
}
Info "zig: $(& $Zig version)"

# ---- 2. Musashi opcode tables (generated once) ----------------------------
$Ops = Join-Path $Build 'musashi_gen\m68kops.c'
if (-not (Test-Path $Ops)) {
    Info 'generating Musashi opcode tables'
    New-Item -ItemType Directory -Force -Path (Split-Path $Ops) | Out-Null
    & $Zig cc -O0 -o (Join-Path $Build 'tools\m68kmake.exe') tools\musashi\m68kmake.c
    if ($LASTEXITCODE -ne 0) { Fail 'm68kmake build failed' }
    & (Join-Path $Build 'tools\m68kmake.exe') (Split-Path $Ops) tools\musashi\m68k_in.c | Out-Null
    if ($LASTEXITCODE -ne 0) { Fail 'm68kmake failed' }
}

# ---- 3. The 68000 recompiler ----------------------------------------------
$Recomp = Join-Path $Build 'tools\m68krecomp.exe'
Info 'building m68krecomp'
New-Item -ItemType Directory -Force -Path (Join-Path $Build 'tools') | Out-Null
$rcSrc = @(
    'tools\musashi\m68kcpu.c', 'tools\musashi\m68kdasm.c',
    'tools\musashi\softfloat\softfloat.c', $Ops,
    'tools\musashi\musashi_glue.c', 'tools\m68krecomp\decode.c',
    'tools\m68krecomp\emit.c', 'tools\m68krecomp\recomp.c',
    'tools\m68krecomp\main.c'
)
& $Zig cc -O2 -w -Itools\musashi -Ibuild\musashi_gen -Itools\m68krecomp @rcSrc -o $Recomp
if ($LASTEXITCODE -ne 0) { Fail 'm68krecomp build failed' }

# ---- 4. Synthetic fixture (always) ----------------------------------------
Info 'building the synthetic 68000 fixture'
python tests\make_selftest.py
if ($LASTEXITCODE -ne 0) { Fail 'fixture generation failed' }
Remove-Item -Recurse -Force $GenTest -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $GenTest | Out-Null
& $Recomp --raw tests\fixtures\selftest.bin --base 0 --prefix t_ --out $GenTest --files 1
if ($LASTEXITCODE -ne 0) { Fail 'fixture translation failed' }

# ---- 5. Game translation (if the user's ROM is present) ------------------
$HaveGame = $false
if ($Mode -ne 'selftest' -and (Test-Path 'work\roms\main.bin')) {
    if ($SkipRecompile) {
        if (-not (Test-Path (Join-Path $Gen 'sources.txt'))) {
            Fail 'no existing generated code; run a normal build first'
        }
        Info 'using existing generated code'
    } else {
        Info 'translating Power Drift'
        python tools\analyse.py work\roms --out work\analysis
        if ($LASTEXITCODE -ne 0) { Fail 'analysis failed' }
        $SeedDir = Join-Path $Root 'analysis'
        if (Test-Path $SeedDir) {
            Info 'seeding checked-in entry points'
            Copy-Item (Join-Path $SeedDir '*.txt') (Join-Path $Root 'work\analysis') -Force
        }
        $RecompArgs = @('--rom', 'work\roms', '--analysis', 'work\analysis',
                        '--out', $Gen, '--recomp', $Recomp)
        if (-not $FullDiscovery) {
            # Default: vectors plus the code the recompiler discovers directly.
            # -EntriesOnly also seeds the analyser's direct-call entries and any
            # coverage-guided extra_entries files; -FullDiscovery adds the pointer
            # scan, which can produce tens of thousands of false seeds.
            $RecompArgs += '--no-pointer-scan';
            if (-not $EntriesOnly) {
                $RecompArgs += '--no-entries';
            }
        }
        python tools\recompile.py @RecompArgs
        if ($LASTEXITCODE -ne 0) { Fail 'recompilation failed' }
    }
    $HaveGame = $true
}

# ---- 6. Compile runtime + generated C ------------------------------------
Info 'compiling Musashi fallback core'
$MusashiObjs = @()
foreach ($f in @('tools\musashi\m68kcpu.c', 'tools\musashi\softfloat\softfloat.c', 'tools\musashi\musashi_glue.c', $Ops)) {
    $o = Join-Path $Obj ('musashi_' + [IO.Path]::GetFileNameWithoutExtension($f) + '.o')
    & $Zig cc -O2 -w -Itools\musashi -Ibuild\musashi_gen -c $f -o $o
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $f" }
    $MusashiObjs += $o
}

$SdlRoot = Join-Path $Root 'vendor\SDL2'
$Sdl = Join-Path $SdlRoot 'x86_64-w64-mingw32'
$HaveSdl = Test-Path (Join-Path $Sdl 'lib\libSDL2.a')
if (-not $HaveSdl -and -not $SkipDownload) {
    Info 'downloading SDL2 2.32.10 (~14 MB)'
    $tgz = Join-Path $env:TEMP 'likethewind-sdl2-2.32.10.tar.gz'
    if (-not (Test-Path $tgz)) {
        Invoke-WebRequest -Uri 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz' -OutFile $tgz -UseBasicParsing
    }
    $tmp = Join-Path $env:TEMP 'likethewind-sdl2'
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    & tar -xzf $tgz -C $tmp
    if ($LASTEXITCODE -ne 0) { Fail 'SDL2 extraction failed' }
    New-Item -ItemType Directory -Force -Path $SdlRoot | Out-Null
    Copy-Item (Join-Path $tmp 'SDL2-2.32.10\x86_64-w64-mingw32') $SdlRoot -Recurse -Force
    Copy-Item (Join-Path $tmp 'SDL2-2.32.10\LICENSE.txt') $SdlRoot -Force -ErrorAction SilentlyContinue
    $HaveSdl = Test-Path (Join-Path $Sdl 'lib\libSDL2.a')
}

Info 'generating ROM table'
python tools\gen_rom_table.py --out work\generated\rom_table.h
if ($LASTEXITCODE -ne 0) { Fail 'ROM table generation failed' }

$RuntimeFlags = @('-I', 'runtime\include', '-I', 'tools\musashi', '-I', 'build\musashi_gen',
                  '-I', 'vendor\ymfm', '-I', 'vendor\z80', '-I', 'vendor\miniz',
                  '-I', 'work\generated')
if ($HaveSdl) {
    $RuntimeFlags += @('-DLIKETHEWIND_HAVE_SDL', '-I', (Join-Path $Sdl 'include\SDL2'))
    Info 'SDL2 found: window/input enabled'
} else {
    Info 'SDL2 not found: headless build'
}

Info 'compiling runtime'
$RuntimeObjs = @()
foreach ($f in Get-ChildItem runtime\src\*.cpp) {
    $o = Join-Path $Obj ($f.BaseName + '.o')
    & $Zig c++ -std=c++20 -O2 @RuntimeFlags -c $f.FullName -o $o
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $($f.Name)" }
    $RuntimeObjs += $o
}
$RuntimeObjs += $MusashiObjs

Info 'compiling Z80 and YM2151 cores'
$Z80Obj = Join-Path $Obj 'z80.o'
& $Zig cc -O2 -I vendor\z80 -c vendor\z80\z80.c -o $Z80Obj
if ($LASTEXITCODE -ne 0) { Fail 'Z80 core compile failed' }
$YmfmObj = Join-Path $Obj 'ymfm_opm.o'
& $Zig c++ -std=c++20 -O2 -I vendor\ymfm -c vendor\ymfm\ymfm_opm.cpp -o $YmfmObj
if ($LASTEXITCODE -ne 0) { Fail 'YM2151 core compile failed' }
Info 'compiling miniz'
$MinizObj = Join-Path $Obj 'miniz.o'
& $Zig cc -O2 -w -I vendor\miniz -c vendor\miniz\miniz.c -o $MinizObj
if ($LASTEXITCODE -ne 0) { Fail 'miniz compile failed' }
$RuntimeObjs += @($Z80Obj, $YmfmObj, $MinizObj)

function Compile-Generated([string]$dir, [string]$tag) {
    $objs = @()
    foreach ($f in Get-ChildItem $dir -Filter *.c) {
        $o = Join-Path $Obj ("$tag" + $f.BaseName + '.o')
        & $Zig cc -O2 -I runtime\include -c $f.FullName -o $o
        if ($LASTEXITCODE -ne 0) { Fail "compile failed: $($f.Name)" }
        $objs += $o
    }
    return $objs
}

$TestObjs = Compile-Generated $GenTest 'test_'
$AllObjs = $RuntimeObjs + $TestObjs
if ($HaveGame) {
    $GameObjs = Compile-Generated $Gen 'game_'
    $AllObjs += $GameObjs
}

$Exe = Join-Path $Build 'LikeTheWind.exe'
Info 'linking'
$LinkFlags = @()
if ($HaveSdl) {
    $LinkFlags += @('-L', (Join-Path $Sdl 'lib'), '-lSDL2')
    $LinkFlags += @('-luser32', '-lgdi32', '-lwinmm', '-limm32', '-lole32', '-loleaut32',
                    '-luuid', '-lsetupapi', '-lcfgmgr32', '-lversion', '-ldinput8', '-lhid',
                    '-ladvapi32', '-lshell32')
}

$IconRes = $null
$IconPng = Join-Path $Root 'icon.png'
if (Test-Path $IconPng) {
    Info 'building executable icon'
    $IconIco = Join-Path $Build 'icon.ico'
    python tools\make_icon.py $IconPng $IconIco
    if ($LASTEXITCODE -ne 0) { Fail 'icon conversion failed' }
    $Rc = Join-Path $Build 'app.rc'
    Set-Content -Path $Rc -Value '1 ICON "icon.ico"' -Encoding ASCII
    Push-Location $Build
    & $Zig rc /fo app.res app.rc
    $RcExit = $LASTEXITCODE
    Pop-Location
    if ($RcExit -ne 0) { Fail 'icon resource compile failed' }
    $IconRes = Join-Path $Build 'app.res'
} else {
    Info 'icon.png not found: building without an exe icon'
}

$LinkInputs = @($AllObjs)
if ($IconRes) { $LinkInputs += $IconRes }
& $Zig c++ @LinkInputs -o $Exe @LinkFlags
if ($LASTEXITCODE -ne 0) { Fail 'link failed' }
if ($HaveSdl) {
    Copy-Item (Join-Path $Sdl 'bin\SDL2.dll') $Build -Force
}

# ---- 7. Run the self-checks ----------------------------------------------
Info 'running self-test'
& $Exe --selftest
if ($LASTEXITCODE -ne 0) { Fail 'self-test failed' }
Info 'running video test'
& $Exe --videotest
if ($LASTEXITCODE -ne 0) { Fail 'video test failed' }
Info 'running interpreter-fallback test'
& $Exe --fallbacktest
if ($LASTEXITCODE -ne 0) { Fail 'fallback test failed' }

if ($HaveGame -and $Mode -eq 'game') {
    Info "running $Frames frame(s)"
    if ($Frames -gt 0) { & $Exe --rom work\roms --frames $Frames --screenshot work\frame.ppm }
    else { & $Exe --rom work\roms --frames 60 --screenshot work\frame.ppm }
}

if ($Package) {
    if (-not $HaveGame) { Fail 'packaging requires work\roms (run prepare_rom.py first)' }
    if (-not $HaveSdl) { Fail 'packaging requires SDL2' }
    $ReleaseExe = Join-Path $Build 'LikeTheWind-release.exe'
    Info 'linking release (GUI subsystem)'
    & $Zig c++ @LinkInputs -o $ReleaseExe @LinkFlags '-Wl,--subsystem,windows'
    if ($LASTEXITCODE -ne 0) { Fail 'release link failed' }

    Info 'building portable Linux release'
    & powershell -ExecutionPolicy Bypass -File tools\build_linux.ps1
    if ($LASTEXITCODE -ne 0) { Fail 'Linux release build failed' }
    $LinuxExe = Join-Path $Build 'LikeTheWind-linux'
    if (-not (Test-Path $LinuxExe)) { Fail 'Linux executable missing' }

    $Dist = Join-Path $Root 'dist'
    # Assemble into a staging directory first. If the existing release is in
    # use (for example, a running LikeTheWind.exe), leave it intact instead of
    # deleting it and ending up with a half-copied dist folder.
    $Stage = Join-Path $Build 'dist_new'
    Remove-Item -Recurse -Force $Stage -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $Stage, (Join-Path $Stage 'roms') | Out-Null
    Copy-Item $ReleaseExe (Join-Path $Stage 'LikeTheWind.exe') -Force
    Copy-Item $LinuxExe (Join-Path $Stage 'LikeTheWind-linux') -Force
    Copy-Item (Join-Path $Build 'libSDL2-2.0.so.0') $Stage -Force
    Copy-Item (Join-Path $Build 'SDL2.dll') $Stage -Force
    Copy-Item (Join-Path $Root 'LICENSE') $Stage -Force
    Copy-Item (Join-Path $Root 'THIRD_PARTY_NOTICES.md') $Stage -Force
    if (Test-Path (Join-Path $Root 'samples')) {
        Copy-Item (Join-Path $Root 'samples') $Stage -Recurse -Force
    }
    if (Test-Path (Join-Path $Root 'mixes')) {
        Copy-Item (Join-Path $Root 'mixes') $Stage -Recurse -Force
    }
    $Licenses = Join-Path $Stage 'licenses'
    New-Item -ItemType Directory -Force -Path $Licenses | Out-Null
    Copy-Item (Join-Path $Root 'vendor\miniz\LICENSE') (Join-Path $Licenses 'miniz.txt') -Force
    Copy-Item (Join-Path $Root 'vendor\ymfm\LICENSE') (Join-Path $Licenses 'ymfm.txt') -Force
    Copy-Item (Join-Path $Root 'vendor\z80\LICENSE') (Join-Path $Licenses 'z80.txt') -Force
    Copy-Item (Join-Path $SdlRoot 'LICENSE.txt') (Join-Path $Licenses 'SDL2.txt') -Force -ErrorAction SilentlyContinue
    $ReleaseReadme = @"
LikeTheWind 1.0 - Power Drift
=============================

Windows:
    Put your MAME pdrift.zip (World, Rev A) in the roms folder.
    Run LikeTheWind.exe.

Linux x86_64:
    Put your MAME pdrift.zip (World, Rev A) in the roms folder.
    Keep libSDL2-2.0.so.0 beside LikeTheWind-linux.
    chmod +x LikeTheWind-linux
    Run ./LikeTheWind-linux

The game checks each required ROM file by size, CRC-32 and SHA-1 and prepares
the ROM automatically on first launch. Other revisions are rejected.

Controls
  Arrow keys / WASD  steer
  Up / W             accelerate
  Down / S           brake
  Space              gear
  1 / Return         start
  5 / C              coin
  F1 / F2            service / test
  F9                 reset fallback tracking
  F10                copy fallback report (also writes fallback_report.txt)
  Esc                quit
  Tab                settings menu
  Gamepad            left stick steer, RT gas, LT brake; default buttons
                     D-pad L/R, A, B, Y, Start, Back; remap in
                     Input -> Controller Bindings

Settings: Tab opens the menu. Video can hide or move the L/H gear indicator and
offers 60, 120 or 144 FPS (120/144 interpolate on a matching display);
Audio has separate music, SFX, engine and master volume sliders, a sound test
for the game's music tracks, and an AUDIO MIX selector: STOCK or HOJO'S MIX
(per-song mixes from mixes\id.cfg). The Mixer tab contains the YM/PCM faders,
chorus and plate-reverb sends and VU meters. L/R selects, Up/Down moves volume,
+/- moves the send chosen with R, and S adds/removes channels from the solo
set. CHORUS/REVERB open their settings, A/B compares the raw board mix with the
current mix, and RESET restores all trims and zeroes both sends. System has a
RUN IN BACKGROUND option. Settings are saved in likethewind-settings.cfg next
to the executable. If the game crashes, crash.log is written next to it.
Third-party licences are in the licenses folder and THIRD_PARTY_NOTICES.md.

The Linux executable targets glibc 2.34+ and uses the included
libSDL2-2.0.so.0. It needs an x86_64 Linux desktop with X11 or Wayland and an
audio device. The Windows executable uses the included SDL2.dll.
"@
    $RomsReadme = @"
Put your MAME pdrift.zip (World, Rev A) in this folder, then run
LikeTheWind.exe on Windows or:
    ./LikeTheWind-linux
on Linux. Both look for the roms folder next to the executable.

The game checks each required file by size, CRC-32 and SHA-1, then prepares
the ROM automatically on first launch; other revisions are rejected.
"@
    Set-Content -Path (Join-Path $Stage 'README.txt') -Value $ReleaseReadme -Encoding ASCII
    Set-Content -Path (Join-Path $Stage 'roms\README.txt') -Value $RomsReadme -Encoding ASCII
    if (Test-Path $Dist) { Remove-Item -Recurse -Force $Dist -ErrorAction Stop }
    Move-Item $Stage $Dist
    Info "release: $Dist"
}

Info "done: $Exe"
