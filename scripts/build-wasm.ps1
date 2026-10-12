param(
    [string]$SharedRoot = '',
    [string]$Emsdk = $env:EMSDK,
    [int]$Jobs = 4,
    [string]$Target = 'HazeRecomp'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $SharedRoot) {
    $container = Split-Path $root -Parent
    $SharedRoot = if ((Split-Path $container -Leaf) -eq '.worktrees') { Split-Path $container -Parent } else { $root }
}
$assets = (Resolve-Path -LiteralPath $SharedRoot).Path
if (-not $Emsdk) { throw 'Pass -Emsdk <emsdk directory> or set EMSDK.' }
$Emsdk = (Resolve-Path -LiteralPath $Emsdk).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath
$cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$env:EM_CONFIG = Join-Path $Emsdk '.emscripten'
$sdkPython = Get-ChildItem -LiteralPath "$Emsdk/python" -Directory | Select-Object -Last 1
$sdkNode = Get-ChildItem -LiteralPath "$Emsdk/node" -Directory | Select-Object -Last 1
$env:PATH = "$($sdkPython.FullName);$($sdkNode.FullName);$env:PATH"
# Keep generated toolchain libraries inside this worktree.
$env:EM_CACHE = Join-Path $root 'local/emscripten-cache'
$env:PATH = "$Emsdk/upstream/emscripten;$Emsdk/upstream/bin;$(Split-Path $cmake -Parent);$env:PATH"
$sdk = Join-Path $root 'external/ps3recomp'
$build = Join-Path $root 'build/wasm'
$ErrorActionPreference = 'Continue' # Native tool warnings on Windows PowerShell.
& $cmake -S "$root/port" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$Emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" `
    '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG' '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG' `
    "-DPS3RECOMP_DIR=$sdk" "-DRECOMP_DIR=$assets/generated/ppu" "-DSPU_DIR=$assets/generated/spu" `
    "-DPython3_EXECUTABLE=$assets/.venv/Scripts/python.exe"
if ($LASTEXITCODE -ne 0) { throw 'WASM configuration failed.' }
& $cmake --build $build --parallel $Jobs --target $Target
if ($LASTEXITCODE -ne 0) { throw 'WASM compilation failed.' }
