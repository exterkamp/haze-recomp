param(
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(1,1800)][int]$Seconds = 300,
    [ValidateRange(1,10)][int]$IntervalSeconds = 2
)
$ErrorActionPreference = 'Stop'
# Read-only desktop/host context. Samples can reveal a change between polls;
# they cannot exclude short interactions that begin and end between samples.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HazeObservation {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct FileTime { public uint low, high; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("kernel32.dll")] public static extern bool GetSystemTimes(out FileTime idle, out FileTime kernel, out FileTime user);
    public static ulong Ticks(FileTime time) { return ((ulong)time.high << 32) | time.low; }
}
'@
$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
if (Test-Path -LiteralPath $destination) { throw 'Choose a new process-observation output path.' }
$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
do {
    $processes = @(Get-CimInstance Win32_Process -Filter "name='HazeRecomp.exe'" |
        Select-Object ProcessId, ExecutablePath, CreationDate)
    foreach ($observed in $processes) {
        $windowState = $null
        $hostProcess = $null
        try {
            $hostProcess = Get-Process -Id $observed.ProcessId -ErrorAction Stop
            $window = $hostProcess.MainWindowHandle
            $rect = New-Object HazeObservation+Rect
            if ($window -ne [IntPtr]::Zero -and [HazeObservation]::GetWindowRect($window, [ref]$rect)) {
                $windowState = [ordered]@{
                    hwnd = $window.ToInt64()
                    left = $rect.left; top = $rect.top; right = $rect.right; bottom = $rect.bottom
                    minimized = [HazeObservation]::IsIconic($window)
                    priority_class = $hostProcess.PriorityClass.ToString()
                }
            }
        } catch {
            # Process/window may disappear during observation; retain null.
        } finally {
            if ($null -ne $hostProcess) { $hostProcess.Dispose() }
        }
        $observed | Add-Member -NotePropertyName window -NotePropertyValue $windowState
    }
    $foreground = [HazeObservation]::GetForegroundWindow()
    [uint32]$foregroundPid = 0
    [void][HazeObservation]::GetWindowThreadProcessId($foreground, [ref]$foregroundPid)
    $idle = New-Object HazeObservation+FileTime
    $kernelTime = New-Object HazeObservation+FileTime
    $userTime = New-Object HazeObservation+FileTime
    $systemCpu = $null
    if ([HazeObservation]::GetSystemTimes([ref]$idle, [ref]$kernelTime, [ref]$userTime)) {
        # Windows kernel time includes idle; use deltas, never uptime ratios.
        $systemCpu = [ordered]@{
            idle_100ns = [HazeObservation]::Ticks($idle)
            kernel_100ns = [HazeObservation]::Ticks($kernelTime)
            user_100ns = [HazeObservation]::Ticks($userTime)
        }
    }
    [ordered]@{
        timestamp_utc = [DateTime]::UtcNow.ToString('o')
        qpc = [Diagnostics.Stopwatch]::GetTimestamp()
        qpc_frequency = [Diagnostics.Stopwatch]::Frequency
        processes = $processes
        foreground_pid = $foregroundPid
        system_cpu = $systemCpu
    } | ConvertTo-Json -Depth 5 -Compress | Add-Content -LiteralPath $destination -Encoding UTF8
    Start-Sleep -Seconds $IntervalSeconds
} while ([DateTime]::UtcNow -lt $deadline)
