# Frame-time benchmark: runs a CPU-vs-CPU match (soak watch mode by default; -Mode -TagAutoWatch for the screenshot tour) in -game mode and prints the TAG bench lines.
#   powershell -File Tools\Perf\bench.ps1 [-Name base] [-Secs 40] [-Warm 30] [-ResX 1920] [-ResY 1080] [-Extra "-TagDay"]
# Log goes to Saved\Logs\bench_<Name>.log. Close any running game first (it holds the Live Coding mutex).
param([string]$Name = "run", [int]$Secs = 40, [int]$Warm = 30, [int]$ResX = 1920, [int]$ResY = 1080, [string]$Extra = "", [string]$Mode = "-TagSoak=100000")
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$proj = Join-Path $root "TAGGridiron.uproject"
$log = Join-Path $root "Saved\Logs\bench_$Name.log"
if (Test-Path $log) { Remove-Item $log }
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$args = @("`"$proj`"", "-game", "-windowed", "-ResX=$ResX", "-ResY=$ResY", "-LiveCoding=0", "-AsyncLoadingThread", "-NoLoadingScreen", "-TagNoLoad",
  $Mode, "-TagWeatherSeed=7", "-TagBench=$Secs", "-TagBenchWarm=$Warm", "-abslog=`"$log`"", "-unattended", "-nosplash") + ($Extra -split ' ' | Where-Object { $_ })
$p = Start-Process -FilePath $ue -ArgumentList $args -PassThru
if (-not $p.WaitForExit(1000 * ($Secs + $Warm + 600))) { $p.Kill(); Write-Output "TIMEOUT" }
if (Test-Path $log) {
  Select-String -Path $log -Pattern "TAG bench" | ForEach-Object { ($_.Line -replace '^.*?TAG bench', 'TAG bench') }
} else { Write-Output "no log at $log" }
