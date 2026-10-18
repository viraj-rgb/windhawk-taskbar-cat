# Builds the harness and unit tests with Windhawk's bundled clang, then runs
# the tests. Uses the same flags Windhawk uses for mods.
#
#   .\build.ps1            build + run tests
#   .\build.ps1 -Harness 20  also run the harness for 20 seconds

param([int]$Harness = -1)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$compiler = 'C:\Program Files\Windhawk\Compiler'
$clang = Join-Path $compiler 'bin\clang++.exe'
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Force $out | Out-Null

$flags = @(
    '-std=c++23', '-target', 'x86_64-w64-mingw32',
    '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD', '-DWH_EDITING', '-DWH_STANDALONE',
    '-I', (Join-Path $compiler 'include'), '-include', 'windhawk_api.h',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers',
    '-O2', '-static', '-lgdi32', '-lshell32', '-lwtsapi32', '-ladvapi32', '-lcomctl32', '-lole32', '-luuid', '-lwinmm'
)

function Build($source, $exe, $extra) {
    & $clang @flags @extra (Join-Path $root $source) -o (Join-Path $out $exe)
    if ($LASTEXITCODE -ne 0) { throw "build failed: $source" }
    Write-Host "built $exe"
}

# The mod itself without WH_STANDALONE, the way Windhawk compiles it.
$modFlags = $flags | Where-Object { $_ -notin @('-DWH_STANDALONE', '-O2', '-static') -and $_ -notlike '-l*' }
& $clang -fsyntax-only -x c++ @modFlags (Join-Path $root 'taskbar-cat.wh.cpp')
if ($LASTEXITCODE -ne 0) { throw 'mod does not compile the Windhawk way' }
Write-Host 'checked mod (Windhawk flags)'

Build 'tests\engine_tests.cpp' 'engine_tests.exe' @()
Build 'harness\harness.cpp' 'harness.exe' @('-municode')

& (Join-Path $out 'engine_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'tests failed' }

if ($Harness -ge 0) {
    & (Join-Path $out 'harness.exe') $Harness
}
