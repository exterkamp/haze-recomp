param([string]$Rpcs3 = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Rpcs3) { $Rpcs3 = Join-Path $root 'local\tools\rpcs3\rpcs3.exe' }
if (-not (Test-Path -LiteralPath $Rpcs3)) { throw "RPCS3 decryption tool missing: $Rpcs3" }
$inputFile = Join-Path $root 'game\disc\PS3_GAME\USRDIR\EBOOT.BIN'
New-Item -ItemType Directory -Path (Join-Path $root 'local') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path (Split-Path $Rpcs3 -Parent) 'log') -Force | Out-Null
Push-Location -LiteralPath (Split-Path $Rpcs3 -Parent)
try {
    $arguments = '--decrypt "{0}" --stdout --stderr' -f $inputFile
    $process = Start-Process -FilePath $Rpcs3 -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $root 'local\decrypt.stdout.log') -RedirectStandardError (Join-Path $root 'local\decrypt.stderr.log')
    if ($process.ExitCode -ne 0) { throw 'Executable decryption failed.' }
} finally { Pop-Location }
$elf = Join-Path $root 'game\disc\PS3_GAME\USRDIR\EBOOT.ELF'
if (-not (Test-Path -LiteralPath $elf)) { throw "Decryption did not produce $elf" }
$magic = [System.IO.File]::ReadAllBytes($elf)[0..3]
if ([BitConverter]::ToString($magic) -ne '7F-45-4C-46') { throw 'Decryption output is not an ELF.' }
Write-Output "Decrypted ELF: $elf"
