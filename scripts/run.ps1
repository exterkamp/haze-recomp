param(
    [ValidateRange(1,60)][int]$Seconds = 20,
    [ValidateRange(0,10000)][int]$TraceCalls = 0,
    [ValidateSet(0,2,4,8,16)][int]$CaptureFrames = 0
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\haze\HazeRecomp.exe'
$elf = Join-Path $root 'game\disc\PS3_GAME\USRDIR\EBOOT.ELF'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the native executable first.' }
if (-not (Test-Path -LiteralPath $elf)) { throw 'The decrypted Haze ELF is missing.' }
# This is a bounded diagnostic runner, not a claim of working gameplay.
# Keep missing imports visible rather than relying on fabricated success.
$settings = @{
    PS3_VFS_ROOT = (Join-Path $root 'game\disc')
    PS3_HLE_TRACE = [string]$TraceCalls
    PS3_HLE_UNRESOLVED = 'fail'
    HAZE_BOOT_REPORT = '1'
    PS3_MILESTONE_OUT = (Join-Path $root 'local\boot-milestones.log')
    CELLMARK_DUMP = $(if ($CaptureFrames -gt 0) { [string]$CaptureFrames } else { $null })
    CELLMARK_DUMP_MINDRAWS = '1'
    CELLMARK_DUMP_DIR = (Join-Path $root 'local\frames')
}
$old = @{}
foreach ($key in $settings.Keys) {
    $old[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    [Environment]::SetEnvironmentVariable($key, $settings[$key], 'Process')
}
New-Item -ItemType Directory -Path (Join-Path $root 'local') -Force | Out-Null
if ($CaptureFrames -gt 0) {
    New-Item -ItemType Directory -Path $settings.CELLMARK_DUMP_DIR -Force | Out-Null
}
try {
    $p = Start-Process -FilePath $exe -ArgumentList ('"{0}"' -f $elf) -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $root 'local\boot.stdout.log') -RedirectStandardError (Join-Path $root 'local\boot.stderr.log')
    $ended = $p.WaitForExit($Seconds * 1000)
    if (-not $ended) {
        Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
        if (-not $p.WaitForExit(5000)) { throw 'Timed-out native process did not terminate.' }
    }
    $result = [ordered]@{
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        timeout_seconds = $Seconds
        timed_out = -not $ended
        exit_code = $(if ($ended) { $p.Refresh(); $p.ExitCode } else { $null })
        gameplay_validated = $false
    }
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'local\boot-result.json') -Encoding UTF8
    $result | ConvertTo-Json
} finally {
    foreach ($key in $settings.Keys) { [Environment]::SetEnvironmentVariable($key, $old[$key], 'Process') }
}
