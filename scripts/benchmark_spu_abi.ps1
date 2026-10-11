# Isolated first-use ABI benchmark; no game assets or native gameplay required.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'Visual Studio C++ build tools are required.' }
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
$environment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Visual Studio environment.' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$clang = Join-Path $install 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
$sdk = Join-Path $root 'external/ps3recomp'
$output = Join-Path $root 'local/benchmark_spu_abi.exe'
New-Item -ItemType Directory -Path (Join-Path $root 'local') -Force | Out-Null
& $clang /nologo /W0 /O2 /clang:-msse4.1 "/I$sdk/runtime/spu" "/I$sdk/include" "/Fe$output" "/Fo$root/local/benchmark_spu_abi.obj" "$root/tests/benchmark_spu_abi.c"
if ($LASTEXITCODE -ne 0) { throw 'ABI benchmark compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'ABI benchmark failed.' }
