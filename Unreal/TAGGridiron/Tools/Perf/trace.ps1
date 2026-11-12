# Runs bench.ps1 with an Insights CPU trace, then exports per-timer statistics over the bench window (region TagBench).
#   powershell -File Tools\Perf\trace.ps1 [-Name t1] [-Secs 20] [-Warm 30] [-Threads GameThread] [-Extra ""]
# Writes Saved\Profiling\<Name>.utrace and <Name>_<thread>.csv; prints the top exclusive timers.
param([string]$Name = "t1", [int]$Secs = 20, [int]$Warm = 30, [string]$Threads = "GameThread", [string]$Extra = "", [int]$Top = 45, [switch]$Reuse, [switch]$SortIncl, [switch]$Named)
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$dir = Join-Path $root "Saved\Profiling"
New-Item -ItemType Directory -Force $dir | Out-Null
$trace = Join-Path $dir "$Name.utrace"
if ((Test-Path $trace) -and -not $Reuse) { Remove-Item $trace }
if (-not $Reuse) { & (Join-Path $PSScriptRoot "bench.ps1") -Name $Name -Secs $Secs -Warm $Warm -Extra "-trace=cpu,frame,bookmark,region $(if ($Named) { '-statnamedevents' }) -tracefile=`"$trace`" $Extra" | Select-Object -Last 1 }
$ins = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealInsights.exe"
foreach ($th in ($Threads -split ',')) {
  $csv = Join-Path $env:TEMP "tag_${Name}_$($th -replace '\W','').csv"  # no spaces: the exec cmd cannot carry a quoted path
  if (Test-Path $csv) { Remove-Item $csv }
  $cmd = "TimingInsights.ExportTimerStatistics $csv -threads=$th -region=TagBench"
  $p = Start-Process $ins -ArgumentList @("-OpenTraceFile=`"$trace`"", "-AutoQuit", "-NoUI", "-ExecOnAnalysisCompleteCmd=`"$cmd`"") -PassThru
  $p.WaitForExit(600000) | Out-Null
  if (Test-Path $csv) {
    Write-Output "== $th (ms per frame over the bench window; frames from summary)"
    $rows = Import-Csv $csv
    $fr = ($rows | Where-Object { $_.Name -eq 'FEngineLoop::Tick' -or $_.Name -eq 'Frame' } | Select-Object -First 1).Count
    if (-not $fr) { $fr = 1 }
    $by = if ($SortIncl) { 'Incl' } else { 'Excl' }
    $rows | Sort-Object { -[double]$_.$by } | Select-Object -First $Top Name, @{n='PerFrame';e={[math]::Round([double]$_.Count/$fr,1)}}, @{n='InclMs';e={[math]::Round([double]$_.Incl*1000/$fr,2)}}, @{n='ExclMs';e={[math]::Round([double]$_.Excl*1000/$fr,2)}} | Format-Table -AutoSize | Out-String -Width 220
  } else { Write-Output "no csv for $th" }
}
