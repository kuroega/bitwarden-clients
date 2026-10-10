param([int]$Seconds = 20)
$ErrorActionPreference = 'Stop'
$exe = Join-Path (Split-Path $PSScriptRoot) 'bin\vault-backup.exe'
$timer = [Diagnostics.Stopwatch]::StartNew()
$process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru
try {
    if (-not $process.WaitForInputIdle(10000)) { throw 'The GUI did not become input-idle within 10 seconds.' }
    $readyMs = $timer.Elapsed.TotalMilliseconds
    Start-Sleep -Milliseconds 1000
    $process.Refresh()
    $startCpu = $process.TotalProcessorTime.TotalSeconds
    $startTime = [Diagnostics.Stopwatch]::StartNew()
    $samples = @()
    for ($i = 0; $i -lt $Seconds; $i++) {
        Start-Sleep -Milliseconds 1000
        $process.Refresh()
        if ($process.HasExited) { throw 'The GUI exited unexpectedly.' }
        $samples += [pscustomobject]@{
            working_set_mib = $process.WorkingSet64 / 1MB
            private_commit_mib = $process.PrivateMemorySize64 / 1MB
            handles = $process.HandleCount
            threads = $process.Threads.Count
        }
    }
    $elapsed = $startTime.Elapsed.TotalSeconds
    $cpuSeconds = $process.TotalProcessorTime.TotalSeconds - $startCpu
    [pscustomobject]@{
        mode = 'hidden_gui_idle_no_interaction'
        input_idle_ms = [math]::Round($readyMs,2)
        observed_seconds = [math]::Round($elapsed,3)
        idle_cpu_seconds = [math]::Round($cpuSeconds,6)
        idle_cpu_percent_one_core = [math]::Round(100 * $cpuSeconds / $elapsed,4)
        logical_processors = [Environment]::ProcessorCount
        working_set_mib_min = [math]::Round(($samples.working_set_mib | Measure-Object -Minimum).Minimum,3)
        working_set_mib_max = [math]::Round(($samples.working_set_mib | Measure-Object -Maximum).Maximum,3)
        private_commit_mib_min = [math]::Round(($samples.private_commit_mib | Measure-Object -Minimum).Minimum,3)
        private_commit_mib_max = [math]::Round(($samples.private_commit_mib | Measure-Object -Maximum).Maximum,3)
        handles_min = ($samples.handles | Measure-Object -Minimum).Minimum
        handles_max = ($samples.handles | Measure-Object -Maximum).Maximum
        threads_min = ($samples.threads | Measure-Object -Minimum).Minimum
        threads_max = ($samples.threads | Measure-Object -Maximum).Maximum
    } | ConvertTo-Json
} finally {
    # This script owns this newly launched idle process; it never starts a backup.
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    $process.Dispose()
}
