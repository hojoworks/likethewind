#requires -Version 5.1
<#
  Run the runtime through a strict warning set.

  By default the two intentional/style-only categories are excluded:
    -Wold-style-cast   C-compatible casts are still used in low-level code.
    -Wunused-macros    clang does not see Windows headers consuming NOMINMAX.

  Pass -IncludeStyle to report those as well. Any warning fails the check.
#>
param(
    [switch]$IncludeStyle
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$Zig = Join-Path $Root 'vendor\zig\zig.exe'
if (-not (Test-Path $Zig)) {
    $candidate = Get-ChildItem (Join-Path $Root 'refs\zig') -Filter zig.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $candidate) { throw 'Zig not found; run tools\build.ps1 once' }
    $Zig = $candidate
}

$Flags = @(
    'c++', '-std=c++20', '-c',
    '-Wall', '-Wextra', '-Wpedantic', '-Wshadow', '-Wconversion',
    '-Wsign-conversion', '-Wcast-qual', '-Wcast-align', '-Wdouble-promotion',
    '-Wformat=2', '-Wformat-security', '-Wimplicit-fallthrough', '-Wundef',
    '-Wpointer-arith', '-Wwrite-strings', '-Wnon-virtual-dtor',
    '-Woverloaded-virtual', '-Wnull-dereference', '-Wvla', '-Wswitch-enum',
    '-Wunused-macros', '-Wunused-private-field',
    '-Wconditional-uninitialized', '-Wsometimes-uninitialized',
    '-Wpessimizing-move', '-Wredundant-move', '-Wrange-loop-analysis',
    '-Wreturn-std-move', '-Wself-assign', '-Wunreachable-code', '-Wcomma'
)
if ($IncludeStyle) {
    $Flags += '-Wold-style-cast'
} else {
    $Flags += @('-Wno-old-style-cast', '-Wno-unused-macros')
}

$Flags += @(
    '-I', 'runtime\include',
    '-isystem', 'tools\musashi',
    '-isystem', 'build\musashi_gen',
    '-isystem', 'vendor\ymfm',
    '-isystem', 'vendor\z80',
    '-isystem', 'vendor\miniz',
    '-isystem', 'work\generated'
)

$Sdl = Join-Path $Root 'vendor\SDL2\x86_64-w64-mingw32'
if (Test-Path (Join-Path $Sdl 'lib\libSDL2.a')) {
    $Flags += @('-DLIKETHEWIND_HAVE_SDL', '-isystem', (Join-Path $Sdl 'include\SDL2'))
}

$Out = Join-Path $env:TEMP 'likethewind-strict'
Remove-Item -Recurse -Force $Out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Out | Out-Null

$Sources = @(Get-ChildItem 'runtime\src\*.cpp' | ForEach-Object { $_.FullName })
$Sources += (Join-Path $Root 'runtime\mixer\mixer_main.cpp')

$Total = 0
foreach ($Source in $Sources) {
    $Name = [IO.Path]::GetFileNameWithoutExtension($Source)
    $Object = Join-Path $Out ($Name + '.o')
    $Errors = Join-Path $Out ($Name + '.stderr')
    $Log = Join-Path $Out ($Name + '.stdout')
    $Process = Start-Process -FilePath $Zig -ArgumentList (@($Flags) + @($Source, '-o', $Object)) `
        -NoNewWindow -Wait -PassThru -RedirectStandardError $Errors -RedirectStandardOutput $Log
    if ($Process.ExitCode -ne 0) {
        Write-Host "[strict] compile failed: $Source" -ForegroundColor Red
        Get-Content $Errors
        exit 1
    }
    $Warnings = @(Select-String -Path $Errors -Pattern 'warning:' -ErrorAction SilentlyContinue)
    if ($Warnings.Count -gt 0) {
        Write-Host "[strict] ${Source}: $($Warnings.Count) warning(s)" -ForegroundColor Yellow
        if (-not $IncludeStyle) { Get-Content $Errors }
        $Total += $Warnings.Count
    }
}

if ($Total -gt 0) {
    Write-Host "[strict] $Total warning(s)" -ForegroundColor Red
    exit 1
}
Write-Host '[strict] clean' -ForegroundColor Green
exit 0
