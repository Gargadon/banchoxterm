[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [string]$QtBin,
    [ValidateRange(1, 20)][int]$Runs = 3,
    [ValidateRange(5, 300)][int]$DurationSeconds = 20,
    [ValidateRange(1, 60)][int]$WarmupSeconds = 5,
    [string]$OutputDirectory = 'build/benchmarks'
)
$ErrorActionPreference = 'Stop'
if ($WarmupSeconds -ge $DurationSeconds) { throw 'Warmup must be shorter than the measurement.' }
$benchmarkExe = (Resolve-Path -LiteralPath $Executable).Path
$benchmarkBin = Split-Path -Parent $benchmarkExe
$buildInfoPath = Join-Path $benchmarkBin 'benchmark-build-info.json'
$buildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
if ($buildInfo.configuration -ne 'Release') { throw 'Only Release builds may be used for published measurements.' }
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$systemInfo = Get-CimInstance Win32_ComputerSystem
$logicalCores = [int]$systemInfo.NumberOfLogicalProcessors
$cpuModel = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
$physicalMemory = $systemInfo.TotalPhysicalMemory
$samples = [Collections.Generic.List[object]]::new()
$summaries = [Collections.Generic.List[object]]::new()

function Get-Median($Values) {
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count % 2) { return [double]$sorted[[int][Math]::Floor($sorted.Count / 2)] }
    return ([double]$sorted[$sorted.Count / 2 - 1] + [double]$sorted[$sorted.Count / 2]) / 2
}

for ($run = 1; $run -le $Runs; $run++) {
    $profile = Join-Path $outputPath ('.profiles/' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $profile -Force | Out-Null
    $startInfo = [Diagnostics.ProcessStartInfo]::new($benchmarkExe)
    $startInfo.WorkingDirectory = $benchmarkBin
    $startInfo.UseShellExecute = $false
    # Measure an ordinary visible desktop window, rather than offscreen Qt.
    $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Normal
    $startInfo.EnvironmentVariables['BANCHO_CONFIG_DIR'] = $profile
    $startInfo.EnvironmentVariables.Remove('QT_QPA_PLATFORM')
    if ($QtBin) { $startInfo.EnvironmentVariables['PATH'] = (Resolve-Path -LiteralPath $QtBin).Path + ';' + $env:PATH }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $application = [Diagnostics.Process]::Start($startInfo)
    try {
        while ($timer.Elapsed.TotalSeconds -lt 15) {
            $application.Refresh()
            if ($application.HasExited) { throw 'Application exited during startup.' }
            if ($application.MainWindowHandle -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 25
        }
        if ($application.MainWindowHandle -eq [IntPtr]::Zero) { throw 'No visible application window appeared.' }
        $windowReadyMs = $timer.Elapsed.TotalMilliseconds
        $startupCpuSeconds = $application.TotalProcessorTime.TotalSeconds
        $previousCpu = @{}
        $previousTime = $timer.Elapsed.TotalSeconds
        $runSamples = [Collections.Generic.List[object]]::new()
        $processNames = [Collections.Generic.HashSet[string]]::new()
        while ($timer.Elapsed.TotalSeconds -lt $DurationSeconds) {
            Start-Sleep -Milliseconds 500
            $application.Refresh()
            if ($application.HasExited) { throw 'Application exited during measurement.' }
            $now = $timer.Elapsed.TotalSeconds
            $interval = $now - $previousTime
            $ids = [Collections.Generic.HashSet[int]]::new()
            [void]$ids.Add($application.Id)
            $snapshot = @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId)
            do {
                $changed = $false
                foreach ($entry in $snapshot) {
                    if ($ids.Contains([int]$entry.ParentProcessId) -and $ids.Add([int]$entry.ProcessId)) { $changed = $true }
                }
            } while ($changed)
            $treeWorking = 0L; $treeCommit = 0L; $treeCpuDelta = 0.0; $appCpuDelta = 0.0
            foreach ($processId in $ids) {
                $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
                if (-not $process) { continue }
                [void]$processNames.Add($process.ProcessName)
                $treeWorking += $process.WorkingSet64
                $treeCommit += $process.PrivateMemorySize64
                $cpu = $process.TotalProcessorTime.TotalSeconds
                # The first sample establishes the CPU baseline; startup is reported separately.
                $delta = if ($previousCpu.ContainsKey($processId)) { [Math]::Max(0, $cpu - $previousCpu[$processId]) } else { 0 }
                $previousCpu[$processId] = $cpu
                $treeCpuDelta += $delta
                if ($processId -eq $application.Id) { $appCpuDelta = $delta }
            }
            $point = [pscustomobject]@{
                run = $run; seconds = [Math]::Round($now, 3)
                app_working_set_mib = [Math]::Round($application.WorkingSet64 / 1MB, 2)
                app_private_commit_mib = [Math]::Round($application.PrivateMemorySize64 / 1MB, 2)
                tree_working_set_mib = [Math]::Round($treeWorking / 1MB, 2)
                tree_private_commit_mib = [Math]::Round($treeCommit / 1MB, 2)
                app_cpu_percent = [Math]::Round(100 * $appCpuDelta / $interval / $logicalCores, 4)
                tree_cpu_percent = [Math]::Round(100 * $treeCpuDelta / $interval / $logicalCores, 4)
                process_count = $ids.Count
            }
            $samples.Add($point); $runSamples.Add($point)
            $previousTime = $now
        }
        $idle = @($runSamples | Where-Object { $_.seconds -ge $WarmupSeconds })
        $summary = [pscustomobject]@{
            run = $run; window_ready_ms = [Math]::Round($windowReadyMs, 1)
            startup_app_cpu_seconds = [Math]::Round($startupCpuSeconds, 4)
            app_peak_working_set_mib = [Math]::Round($application.PeakWorkingSet64 / 1MB, 2)
            idle_app_working_set_mib = [Math]::Round((Get-Median $idle.app_working_set_mib), 2)
            idle_app_private_commit_mib = [Math]::Round((Get-Median $idle.app_private_commit_mib), 2)
            idle_tree_working_set_mib = [Math]::Round((Get-Median $idle.tree_working_set_mib), 2)
            idle_tree_private_commit_mib = [Math]::Round((Get-Median $idle.tree_private_commit_mib), 2)
            idle_app_cpu_percent = [Math]::Round(($idle | Measure-Object app_cpu_percent -Average).Average, 4)
            idle_tree_cpu_percent = [Math]::Round(($idle | Measure-Object tree_cpu_percent -Average).Average, 4)
            process_names = @($processNames | Sort-Object)
        }
        $summaries.Add($summary)
        Write-Host "Run $run/$Runs`: app $($summary.idle_app_working_set_mib) MiB; process tree $($summary.idle_tree_working_set_mib) MiB; idle CPU $($summary.idle_tree_cpu_percent)%"
    } finally {
        if (-not $application.HasExited) {
            [void]$application.CloseMainWindow()
            if (-not $application.WaitForExit(5000)) {
                # Stop only the exact executable launched by this run.
                $owned = Get-Process -Id $application.Id -ErrorAction Stop
                if ($owned.Path -ne $benchmarkExe) { throw 'Process identity changed; refusing to stop it.' }
                Stop-Process -Id $application.Id
            }
        }
        $application.Dispose()
    }
}
$result = [ordered]@{
    schema_version = 1; measured_at_utc = [DateTime]::UtcNow.ToString('o')
    scenario = 'Fresh isolated profile; default startup; visible native desktop window; visual effects disabled'
    build = $buildInfo; executable_sha256 = (Get-FileHash -LiteralPath $benchmarkExe -Algorithm SHA256).Hash
    source = @{ base_commit = (git -C (Split-Path -Parent $PSScriptRoot) rev-parse HEAD); working_tree_modified = [bool](git -C (Split-Path -Parent $PSScriptRoot) status --porcelain) }
    machine = @{ os = [Environment]::OSVersion.VersionString; cpu = $cpuModel; logical_processors = $logicalCores; physical_ram_gib = [Math]::Round($physicalMemory / 1GB, 2) }
    method = @{ runs = $Runs; duration_seconds = $DurationSeconds; idle_after_seconds = $WarmupSeconds; sample_interval_ms = 500; cpu_counter_note = 'Zero CPU values mean the Windows counters recorded no CPU time in these samples; they do not imply zero CPU instructions or permanent zero usage.'; cpu_percent = 'Share of all logical processors; app and descendant processes reported separately'; memory = 'Working set = resident memory. Private commit is reported separately. Summed tree working sets may count shared pages more than once.'; startup = 'Time to a visible main window, not cold boot or time to an interactive shell. OS caches are not cleared.' }
    aggregate = @{
        median_window_ready_ms = [Math]::Round((Get-Median $summaries.window_ready_ms), 1)
        first_window_ready_ms = $summaries[0].window_ready_ms
        min_window_ready_ms = ($summaries | Measure-Object window_ready_ms -Minimum).Minimum
        max_window_ready_ms = ($summaries | Measure-Object window_ready_ms -Maximum).Maximum
        median_idle_app_working_set_mib = [Math]::Round((Get-Median $summaries.idle_app_working_set_mib), 2)
        median_idle_tree_working_set_mib = [Math]::Round((Get-Median $summaries.idle_tree_working_set_mib), 2)
        median_idle_app_private_commit_mib = [Math]::Round((Get-Median $summaries.idle_app_private_commit_mib), 2)
        median_idle_tree_private_commit_mib = [Math]::Round((Get-Median $summaries.idle_tree_private_commit_mib), 2)
        median_idle_app_cpu_percent = [Math]::Round((Get-Median $summaries.idle_app_cpu_percent), 4)
        median_idle_tree_cpu_percent = [Math]::Round((Get-Median $summaries.idle_tree_cpu_percent), 4)
    }
    runs = @($summaries.ToArray())
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'startup.json') -Encoding UTF8
$samples | Export-Csv -LiteralPath (Join-Path $outputPath 'samples.csv') -NoTypeInformation -Encoding UTF8
$a = $result.aggregate
@"
# BanchoXterm startup measurement

Measured: $($result.measured_at_utc)
Build: Release, BanchoXterm $($buildInfo.version), Qt $($buildInfo.qt), $($buildInfo.compiler).
Machine: $cpuModel; $logicalCores logical processors; $($result.machine.physical_ram_gib) GiB RAM; $($result.machine.os).
Scenario: $($result.scenario).

| Metric (median of $Runs runs) | Application | Application + descendants |
| --- | ---: | ---: |
| Resident memory at idle | $($a.median_idle_app_working_set_mib) MiB | $($a.median_idle_tree_working_set_mib) MiB |
| Private committed memory at idle | $($a.median_idle_app_private_commit_mib) MiB | $($a.median_idle_tree_private_commit_mib) MiB |
| CPU at idle, share of the whole machine | $($a.median_idle_app_cpu_percent)% | $($a.median_idle_tree_cpu_percent)% |

Time to a visible main window: $($a.median_window_ready_ms) ms. This does not measure cold boot or time until a shell is ready.
Observed range: $($a.min_window_ready_ms)-$($a.max_window_ready_ms) ms; first run: $($a.first_window_ready_ms) ms.

Each run lasts $DurationSeconds seconds; idle samples start after $WarmupSeconds seconds. CPU is normalized by $logicalCores logical processors. Tree resident memory may count shared pages more than once. No other product was measured. These results describe this machine and scenario, not every workload.
Zero CPU values mean the Windows counters recorded no CPU time in these samples; they do not imply zero CPU instructions or permanent zero usage.

Raw samples: samples.csv. Metadata and per-run results: startup.json.
"@ | Set-Content -LiteralPath (Join-Path $outputPath 'startup.md') -Encoding UTF8
Write-Host "Results: $outputPath"
