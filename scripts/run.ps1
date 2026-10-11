param(
    [ValidateRange(1,600)][int]$Seconds = 60,
    [ValidateRange(0,10000)][int]$TraceCalls = 0,
    [ValidateSet(0,2,4,8,16)][int]$CaptureFrames = 0,
    [ValidateRange(0,1000000)][int]$FrameSkip = 0,
    [ValidateRange(1,1000000)][int]$FrameEvery = 1,
    [ValidateSet('Live','Legacy')][string]$Renderer = 'Live'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'build\haze\HazeRecomp.exe'
$elf = Join-Path $root 'game\disc\PS3_GAME\USRDIR\EBOOT.ELF'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Build the native executable first.' }
if (-not (Test-Path -LiteralPath $elf)) { throw 'The decrypted Haze ELF is missing.' }
$runDir = Join-Path $root ('local\runs\' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
# This is a bounded diagnostic runner, not a claim of working gameplay.
# Keep missing imports visible rather than relying on fabricated success.
$settings = @{
    PS3_VFS_ROOT = (Join-Path $root 'game\disc')
    PS3_HLE_TRACE = [string]$TraceCalls
    PS3_HLE_UNRESOLVED = 'fail'
    HAZE_BOOT_REPORT = '1'
    RSX_LIVE_DRAW = $(if ($Renderer -eq 'Live') { '1' } else { '0' })
    PS3_MILESTONE_OUT = (Join-Path $runDir 'boot-milestones.log')
    CELLMARK_DUMP = $(if ($CaptureFrames -gt 0) { [string]$CaptureFrames } else { $null })
    CELLMARK_DUMP_MINDRAWS = '1'
    CELLMARK_DUMP_DIR = (Join-Path $runDir 'frames')
    CELLMARK_DUMP_SKIP = [string]$FrameSkip
    CELLMARK_DUMP_EVERY = [string]$FrameEvery
    LD_FRAME_DUMP = $(if ($CaptureFrames -gt 0 -and $Renderer -eq 'Live') { Join-Path $runDir 'frames' } else { $null })
    LD_FRAME_DUMP_FROM = [string]$FrameSkip
    LD_FRAME_DUMP_EVERY = [string]$FrameEvery
    LD_FRAME_DUMP_COUNT = [string]$CaptureFrames
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
    # Shader dumps from the SDK use relative paths. Keep them inside this
    # ignored run directory, along with the logs and captured game frames.
    $p = Start-Process -FilePath $exe -ArgumentList ('"{0}"' -f $elf) -WorkingDirectory $runDir -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runDir 'boot.stdout.log') -RedirectStandardError (Join-Path $runDir 'boot.stderr.log')
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
        output_directory = $runDir
        capture_skip = $FrameSkip
        capture_every = $FrameEvery
        renderer = $Renderer
    }
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'local\boot-result.json') -Encoding UTF8
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDir 'boot-result.json') -Encoding UTF8
    $result | ConvertTo-Json
} finally {
    foreach ($key in $settings.Keys) { [Environment]::SetEnvironmentVariable($key, $old[$key], 'Process') }
}
