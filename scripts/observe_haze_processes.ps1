param(
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(1,1800)][int]$Seconds = 300,
    [ValidateRange(1,10)][int]$IntervalSeconds = 2
)
$ErrorActionPreference = 'Stop'
$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
if (Test-Path -LiteralPath $destination) { throw 'Choose a new process-observation output path.' }
$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
do {
    $processes = @(Get-CimInstance Win32_Process -Filter "name='HazeRecomp.exe'" |
        Select-Object ProcessId, ExecutablePath, CreationDate)
    [ordered]@{
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        qpc = [Diagnostics.Stopwatch]::GetTimestamp()
        qpc_frequency = [Diagnostics.Stopwatch]::Frequency
        processes = $processes
    } | ConvertTo-Json -Depth 4 -Compress | Add-Content -LiteralPath $destination -Encoding UTF8
    Start-Sleep -Seconds $IntervalSeconds
} while ([DateTime]::UtcNow -lt $deadline)
