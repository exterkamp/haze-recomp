param(
    [switch]$RuntimeOnly,
    [switch]$Test,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools are required.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
# Import the compiler/SDK environment into this process. No filesystem
# operations are delegated to cmd.exe.
$environment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Visual Studio environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$clang = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
$python = Join-Path $root '.venv\Scripts\python.exe'
$sdk = Join-Path $root 'external\ps3recomp'
if ($RuntimeOnly) {
    $source = $sdk
    $build = Join-Path $root 'build\runtime'
} else {
    if (-not (Test-Path -LiteralPath 'generated/ppu/ppu_recomp.h')) {
        throw 'No Haze lift exists. Run .venv\Scripts\python.exe scripts/haze.py lift first.'
    }
    $source = Join-Path $root 'port'
    $build = Join-Path $root 'build\haze'
}
$ErrorActionPreference = 'Continue' # Windows PowerShell treats native stderr warnings as errors.
& $cmake -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_C_COMPILER=$clang" "-DCMAKE_CXX_COMPILER=$clang" "-DCMAKE_BUILD_TYPE=$Configuration" "-DPython3_EXECUTABLE=$python" "-DPS3RECOMP_DIR=$sdk" "-DRECOMP_DIR=$root/generated/ppu"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $build --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
if ($Test) {
    & $python "$sdk/tools/test_spu_text_selection.py"
    if ($LASTEXITCODE -ne 0) { throw 'SPU segment-selection regression tests failed.' }
    $queueTest = Join-Path $root 'local/test_queue_size.exe'
    New-Item -ItemType Directory -Path (Split-Path $queueTest -Parent) -Force | Out-Null
    & $clang /nologo /W0 "/I$sdk/include" "/I$sdk/runtime/spu" "/Fe$queueTest" "/Fo$root/local/test_queue_size.obj" "$sdk/libs/spurs/tests/test_queue_size.c"
    if ($LASTEXITCODE -ne 0) { throw 'SPURS queue query test compilation failed.' }
    & $queueTest
    if ($LASTEXITCODE -ne 0) { throw 'SPURS queue query contract test failed.' }
    $gameDataTest = Join-Path $root 'local/test_gamedata_callback.exe'
    & $clang /nologo /W0 "/I$sdk/include" "/Fe$gameDataTest" "/Fo$root/local/test_gamedata_callback.obj" "$sdk/libs/system/tests/test_gamedata_callback.c"
    if ($LASTEXITCODE -ne 0) { throw 'Game-data callback test compilation failed.' }
    & $gameDataTest
    if ($LASTEXITCODE -ne 0) { throw 'Game-data callback ABI test failed.' }
    Write-Output 'SPU segment selection, SPURS queue query, and game-data callback tests passed.'
}

