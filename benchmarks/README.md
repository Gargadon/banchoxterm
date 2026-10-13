# Internal startup measurements

This is an internal measurement tool, not an application feature. Use a Release
build and a visible desktop session. The script rejects Debug builds, creates a
fresh isolated profile for each run, and closes only the process it launched.
It does not modify the user's saved sessions or settings.

```powershell
# If local policy blocks scripts, allow this PowerShell process only:
Set-ExecutionPolicy -Scope Process Bypass
./scripts/benchmark-startup.ps1 `
  -Executable build/benchmark-release/banchoxterm.exe `
  -QtBin C:/Qt/6.12.0/msvc2022_64/bin `
  -Runs 3 -DurationSeconds 20 -WarmupSeconds 5 `
  -OutputDirectory build/benchmarks/release-startup
```

Outputs: `startup.json`, `startup.md`, and `samples.csv`. Report the application's
memory and its process tree separately, so console-host and shell costs remain
visible. CPU is a percentage of the entire machine; resident memory and private
committed memory are distinct metrics. A sum of process working sets can count
shared pages more than once.

For website publication, retain the date, build, hardware, scenario, run count,
and methodology alongside the figures. Startup means time to a visible window,
not cold boot or time to an interactive shell. File-system caches are not reset.
The tool measures default startup, not SSH traffic, large terminal output, RDP,
or long-running workloads. Comparing another application requires measuring
that application on the same machine under an equivalent scenario.
