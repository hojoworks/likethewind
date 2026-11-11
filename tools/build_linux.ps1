#requires -Version 5.1
<#
  Cross-build an x86_64 Linux executable from Windows.

  SDL2 is loaded from a shared library shipped next to the executable. The
  executable targets glibc 2.34, which lets SDL's X11/Wayland backends load the
  desktop's native client libraries. A musl-static build cannot reliably load
  those glibc-based libraries, so SDL falls back to its headless offscreen
  driver on a normal desktop.

  The package still needs the normal ROM files (or an extracted work\roms
  directory) at runtime.

  Usage:
    powershell -ExecutionPolicy Bypass -File tools\build_linux.ps1
    powershell -ExecutionPolicy Bypass -File tools\build_linux.ps1 -SkipDownload
#>
param(
    [switch]$SkipDownload
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Info($m) { Write-Host "[linux] $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "[linux] error: $m" -ForegroundColor Red; exit 1 }

$Zig = Join-Path $Root 'refs\zig\zig-x86_64-windows-0.14.1\zig.exe'
if (-not (Test-Path $Zig)) {
    $candidate = Get-ChildItem (Join-Path $Root 'refs\zig') -Filter zig.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($candidate) { $Zig = $candidate } else { Fail 'Zig not found; run tools\build.ps1 once' }
}
Info "zig: $(& $Zig version)"

# Debian's SDL2 packages are built against glibc. The runtime package gives us
# the shared library to ship; the dev package supplies the matching headers.
# Debian 12's SDL2 2.26.5 has SDL_GetDefaultAudioInfo and requires glibc 2.34.
$SdlRoot = Join-Path $Root 'vendor\SDL2-linux-gnu'
$Sdl = Join-Path $SdlRoot 'usr'
$SdlSo = Join-Path $Sdl 'lib\x86_64-linux-gnu\libSDL2-2.0.so.0'
if (-not (Test-Path $SdlSo)) {
    if ($SkipDownload) { Fail 'Linux SDL2 not found; rerun without -SkipDownload' }
    $sdlVersion = '2.26.5+dfsg-1'
    $sdlBase = 'https://deb.debian.org/debian/pool/main/libs/libsdl2'
    $devPkg = "libsdl2-dev_${sdlVersion}_amd64.deb"
    $runPkg = "libsdl2-2.0-0_${sdlVersion}_amd64.deb"
    $tmp = Join-Path $env:TEMP 'likethewind-sdl2-glibc'
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null

    foreach ($item in @(
        @{ Name = $devPkg; Stage = 'dev' },
        @{ Name = $runPkg; Stage = 'run' }
    )) {
        $deb = Join-Path $tmp $item.Name
        if (-not (Test-Path $deb)) {
            Info "downloading Debian SDL2 package $($item.Name)"
            Invoke-WebRequest -Uri "$sdlBase/$($item.Name)" -OutFile $deb -UseBasicParsing
        }
        $stage = Join-Path $tmp $item.Stage
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        & tar -xf $deb -C $stage data.tar.xz
        if ($LASTEXITCODE -ne 0) { Fail "SDL2 package extraction failed: $($item.Name)" }
        # tar on Windows reports errors for a few absolute symlinks; the
        # headers and the shared library still extract. Validate below.
        $oldEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & tar -xf (Join-Path $stage 'data.tar.xz') -C $stage 2>$null
        $ErrorActionPreference = $oldEap
    }

    $devUsr = Join-Path $tmp 'dev\usr'
    $runSo = Join-Path $tmp 'run\usr\lib\x86_64-linux-gnu\libSDL2-2.0.so.0.2600.5'
    if (-not (Test-Path (Join-Path $devUsr 'include\SDL2\SDL.h')) -or
        -not (Test-Path $runSo)) {
        Fail 'Debian SDL2 package contents were not extracted'
    }
    New-Item -ItemType Directory -Force -Path $SdlRoot | Out-Null
    Copy-Item $devUsr $SdlRoot -Recurse -Force
    $soDir = Split-Path $SdlSo -Parent
    New-Item -ItemType Directory -Force -Path $soDir | Out-Null
    Copy-Item $runSo $SdlSo -Force
}
Info "SDL2: $SdlSo"

if (-not (Test-Path 'work\generated\sources.txt')) {
    Fail 'generated game sources missing; run tools\build.ps1 -EntriesOnly first'
}
if (-not (Test-Path 'work\generated_test')) {
    Fail 'generated test sources missing; run tools\build.ps1 once first'
}
if (-not (Test-Path 'build\musashi_gen\m68kops.c')) {
    Fail 'Musashi opcode table missing; run tools\build.ps1 once first'
}
if (-not (Test-Path 'work\generated\rom_table.h')) {
    Info 'generating ROM table'
    python tools\gen_rom_table.py --out work\generated\rom_table.h
    if ($LASTEXITCODE -ne 0) { Fail 'ROM table generation failed' }
}

$Build = Join-Path $Root 'build'
$Obj = Join-Path $Build 'linux_obj'
Remove-Item -Recurse -Force $Obj -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Obj | Out-Null

$Target = 'x86_64-linux-gnu.2.34'
$Common = @(
    '-target', $Target, '-O2', '-mcpu=baseline',
    '-I', 'runtime\include',
    '-I', 'tools\musashi',
    '-I', 'build\musashi_gen',
    '-I', 'vendor\ymfm',
    '-I', 'vendor\z80',
    '-I', 'vendor\miniz',
    '-I', 'work\generated',
    '-I', (Join-Path $Sdl 'include\SDL2'),
    '-I', (Join-Path $Sdl 'include'),
    '-I', (Join-Path $Sdl 'include\x86_64-linux-gnu'),
    '-DLIKETHEWIND_HAVE_SDL'
)

Info 'compiling runtime'
foreach ($f in Get-ChildItem runtime\src\*.cpp) {
    $o = Join-Path $Obj ($f.BaseName + '.o')
    & $Zig c++ @Common -std=c++20 -c $f.FullName -o $o
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $($f.Name)" }
}

Info 'compiling Musashi / Z80 / YM2151 / miniz'
foreach ($f in @(
    'tools\musashi\m68kcpu.c',
    'tools\musashi\softfloat\softfloat.c',
    'tools\musashi\musashi_glue.c',
    'build\musashi_gen\m68kops.c'
)) {
    $o = Join-Path $Obj ('musashi_' + [IO.Path]::GetFileNameWithoutExtension($f) + '.o')
    & $Zig cc @Common -w -c $f -o $o
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $f" }
}
foreach ($pair in @(
    @('vendor\z80\z80.c', 'z80.o'),
    @('vendor\miniz\miniz.c', 'miniz.o'),
    @('vendor\ymfm\ymfm_opm.cpp', 'ymfm_opm.o')
)) {
    $o = Join-Path $Obj $pair[1]
    if ($pair[0].EndsWith('.cpp')) {
        & $Zig c++ @Common -std=c++20 -c $pair[0] -o $o
    } else {
        & $Zig cc @Common -w -c $pair[0] -o $o
    }
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $($pair[0])" }
}

Info 'compiling generated test code'
foreach ($f in Get-ChildItem work\generated_test -Filter *.c) {
    $o = Join-Path $Obj ('test_' + $f.BaseName + '.o')
    & $Zig cc @Common -w -c $f.FullName -o $o
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $($f.Name)" }
}

Info 'compiling generated game code'
foreach ($f in Get-ChildItem work\generated -Filter *.c) {
    $o = Join-Path $Obj ('game_' + $f.BaseName + '.o')
    & $Zig cc @Common -w -c $f.FullName -o $o
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $($f.Name)" }
}

$Exe = Join-Path $Build 'LikeTheWind-linux'
$Objects = Get-ChildItem $Obj -Filter *.o | ForEach-Object { $_.FullName }
$Rpath = '-Wl,-rpath,$ORIGIN'
Info 'linking glibc Linux executable'
& $Zig c++ -target $Target -mcpu=baseline -s @Objects $SdlSo $Rpath -ldl -lpthread -lm -o $Exe
if ($LASTEXITCODE -ne 0) { Fail 'Linux link failed' }

# Keep the bundled SDL2 next to the executable; the RUNPATH above makes the
# package relocatable.
$BundledSdl = Join-Path $Build 'libSDL2-2.0.so.0'
Copy-Item $SdlSo $BundledSdl -Force

Info "done: $Exe"
Info "bundled: $BundledSdl"
Info 'The target needs glibc 2.34+ and a desktop X11/Wayland/audio stack.'
exit 0
