param(
    [string]$Iso = 'S:\PS3\Haze (USA) (En,Fr,Es).iso',
    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not (Test-Path -LiteralPath $Iso -PathType Leaf)) { throw "ISO missing: $Iso" }
if (-not (Test-Path -LiteralPath $SevenZip -PathType Leaf)) { throw "7-Zip missing: $SevenZip" }
$destination = Join-Path $root 'game\disc'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
# Force ISO9660: auto-detecting this disc as UDF fails in 7-Zip. Skip existing
# files so rerunning extraction cannot replace a locally decrypted executable.
& $SevenZip x -tIso $Iso 'PS3_GAME\*' "-o$destination" -aos -bsp0 -bso0
if ($LASTEXITCODE -ne 0) { throw 'Disc extraction failed.' }
$sfo = Join-Path $destination 'PS3_GAME\PARAM.SFO'
if (-not (Test-Path -LiteralPath $sfo)) { throw 'Extraction did not produce PARAM.SFO.' }
$data = [System.IO.File]::ReadAllBytes($sfo)
if ($data.Length -lt 20 -or [BitConverter]::ToString($data[0..3]) -ne '00-50-53-46') {
    throw 'PARAM.SFO is unreadable. This script requires a decrypted disc image.'
}
Write-Output "Game files ready at $destination"
