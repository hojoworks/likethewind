#requires -Version 5.1
<#
  Build the standalone soundtrack mixer.

  It compiles only the menu, settings, audio and the SDL host, so it needs the
  sound-board ROMs (work\roms\z80.bin + pcm.bin) but no translated game code.

  Usage:
    powershell -ExecutionPolicy Bypass -File tools\build_mixer.ps1
    build\LikeTheWindMixer.exe --rom work\roms
#>
param(
    [switch]$SkipDownload
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Info($m) { Write-Host "[mixer] $m" -ForegroundColor Cyan }
function Fail($m) { Write-Host "[mixer] error: $m" -ForegroundColor Red; exit 1 }

$Zig = Join-Path $Root 'vendor\zig\zig.exe'
if (-not (Test-Path $Zig)) {
    $candidate = Get-ChildItem (Join-Path $Root 'refs\zig') -Filter zig.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if ($candidate) {
        $Zig = $candidate
    } elseif (-not $SkipDownload) {
        Info 'Zig not found; running the normal build once to bootstrap it'
        & powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Mode selftest
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Zig)) { Fail 'Zig bootstrap failed' }
    } else {
        Fail 'Zig not found; run tools\build.ps1 once'
    }
}
Info "zig: $(& $Zig version)"

$SdlRoot = Join-Path $Root 'vendor\SDL2'
$Sdl = Join-Path $SdlRoot 'x86_64-w64-mingw32'
if (-not (Test-Path (Join-Path $Sdl 'lib\libSDL2.a'))) {
    if ($SkipDownload) { Fail 'SDL2 not found; run tools\build.ps1 once' }
    Info 'SDL2 not found; running the normal build once to fetch it'
    & powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Mode selftest
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $Sdl 'lib\libSDL2.a'))) {
        Fail 'SDL2 bootstrap failed'
    }
}

$Build = Join-Path $Root 'build'
$Obj = Join-Path $Build 'mixer_obj'
New-Item -ItemType Directory -Force -Path $Build, $Obj | Out-Null

$Inc = @('-Iruntime\include', '-Ivendor\ymfm', '-Ivendor\z80',
         "-I$(Join-Path $Sdl 'include\SDL2')", '-DLIKETHEWIND_HAVE_SDL')
$Cpp = @('-std=c++20', '-O2') + $Inc

$Objects = @()
$CppSources = @(
    @('runtime\mixer\mixer_main.cpp', 'mixer_main.o'),
    @('runtime\src\menu.cpp', 'menu.o'),
    @('runtime\src\menu_draw.cpp', 'menu_draw.o'),
    @('runtime\src\settings.cpp', 'settings.o'),
    @('runtime\src\audio.cpp', 'audio.o'),
    @('vendor\ymfm\ymfm_opm.cpp', 'ymfm_opm.o')
)
foreach ($s in $CppSources) {
    $out = Join-Path $Obj $s[1]
    & $Zig c++ @Cpp -c $s[0] -o $out
    if ($LASTEXITCODE -ne 0) { Fail "compile failed: $($s[0])" }
    $Objects += $out
}

$Z80Obj = Join-Path $Obj 'z80.o'
& $Zig cc -O2 -Ivendor\z80 -c vendor\z80\z80.c -o $Z80Obj
if ($LASTEXITCODE -ne 0) { Fail 'Z80 core compile failed' }
$Objects += $Z80Obj

$Exe = Join-Path $Build 'LikeTheWindMixer.exe'
$LinkFlags = @('-L', (Join-Path $Sdl 'lib'), '-lSDL2',
               '-luser32', '-lgdi32', '-lwinmm', '-limm32', '-lole32', '-loleaut32',
               '-luuid', '-lsetupapi', '-lcfgmgr32', '-lversion', '-ldinput8', '-lhid',
               '-ladvapi32', '-lshell32')
Info 'linking mixer'
& $Zig c++ @Objects -o $Exe @LinkFlags
if ($LASTEXITCODE -ne 0) { Fail 'link failed' }
Copy-Item (Join-Path $Sdl 'bin\SDL2.dll') $Build -Force

Info "done: $Exe"

# Small PCM audition/classification app. It only needs SDL2 and pcm.bin.
$BrowserObj = Join-Path $Obj 'sample_browser.o'
& $Zig c++ @Cpp -c runtime\samples\sample_browser.cpp -o $BrowserObj
if ($LASTEXITCODE -ne 0) { Fail 'sample browser compile failed' }
$BrowserExe = Join-Path $Build 'LikeTheWindSamples.exe'
Info 'linking sample browser'
& $Zig c++ @($BrowserObj) -o $BrowserExe @LinkFlags
if ($LASTEXITCODE -ne 0) { Fail 'sample browser link failed' }
Info "done: $BrowserExe"
Info 'run: build\LikeTheWindMixer.exe --rom work\roms'
Info 'run: build\LikeTheWindSamples.exe --rom work\roms'
