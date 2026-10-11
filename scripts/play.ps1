param(
    [ValidateSet('KeyboardMouse','Controller')][string]$InputMode = 'KeyboardMouse'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\haze\HazeRecomp.exe'
$elf = Join-Path $root 'game\disc\PS3_GAME\USRDIR\EBOOT.ELF'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the native executable first with scripts/build.ps1.' }
if (-not (Test-Path -LiteralPath $elf)) { throw 'The local decrypted Haze executable is missing.' }
$session = Join-Path $root ('local\sessions\' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $session -Force | Out-Null
$settings = @{
    PS3_VFS_ROOT = (Join-Path $root 'game\disc')
    PS3_DEV_FLASH = $(if ($env:PS3_DEV_FLASH) { $env:PS3_DEV_FLASH } else { Join-Path $root 'local\tools\rpcs3\dev_flash' })
    PS3_NET_ONLINE = '1'
    YDKJ_NO_CRI_CTX = '1'
    PS3_SAVEDATA_ROOT = $(if ($env:PS3_SAVEDATA_ROOT) { $env:PS3_SAVEDATA_ROOT } else { Join-Path $root 'local\state\savedata' })
    PS3_HLE_UNRESOLVED = 'fail'
    PS3_VERBOSE = '0'
    RSX_LIVE_DRAW = '1'
    HAZE_INPUT = $(if ($InputMode -eq 'Controller') { 'controller' } else { 'keyboard' })
    HAZE_INPUT_CONFIG = $(if ($env:HAZE_INPUT_CONFIG) { $env:HAZE_INPUT_CONFIG } else { Join-Path $root 'config\input.ini' })
}
$old = @{}
foreach ($key in $settings.Keys) {
    $old[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $settings[$key], 'Process')
}
try {
    Write-Host 'Intro videos skip automatically. Enter starts; Space confirms; arrows navigate.'
    Write-Host 'WASD moves. Click/F1 captures the mouse; Escape pauses and releases it.'
    Write-Host "Session logs: $session"
    # This is the interactive game launcher; the separate run.ps1 is bounded.
    $game = Start-Process -FilePath $exe -ArgumentList ('"{0}"' -f $elf) -WorkingDirectory $session -WindowStyle Normal -PassThru -RedirectStandardOutput (Join-Path $session 'game.stdout.log') -RedirectStandardError (Join-Path $session 'game.stderr.log')
    $null = $game.Handle
    $game.WaitForExit()
    $game.Refresh()
    if ($game.ExitCode -ne 0) { throw "Native game exited with code $($game.ExitCode). See $session" }
} finally {
    foreach ($key in $settings.Keys) { [Environment]::SetEnvironmentVariable($key, $old[$key], 'Process') }
}
