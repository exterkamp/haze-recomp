param([string]$SharedRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$assets=if ($SharedRoot) { (Resolve-Path -LiteralPath $SharedRoot).Path } else { $root }
$sdk=Join-Path $assets 'external/ps3recomp'
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$environment=& $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process') }
}
$clang=Join-Path $vs 'VC/Tools/Llvm/x64/bin/clang-cl.exe'
New-Item -ItemType Directory -Path "$root/local" -Force | Out-Null
$ErrorActionPreference='Continue' # Windows PowerShell promotes native stderr warnings.
foreach ($test in @('test_haze_input','test_haze_camera')) {
    & $clang /nologo /W4 /O1 /std:c++20 "/I$root/tests/fixtures/camera" "/I$sdk/include" "/I$sdk/libs/input" "/I$sdk/libs/codec" "/Fe$root/local/$test.exe" "/Fo$root/local/$test.obj" "$root/tests/$test.cpp" /link user32.lib
    if ($LASTEXITCODE -ne 0) { throw "$test compilation failed" }
    & "$root/local/$test.exe"
    if ($LASTEXITCODE -ne 0) { throw "$test failed" }
}
