# The game's automation gates, one after the other, each a -game run that exits by itself; prints each run's error
# count and its TAG summary lines. -LiveCoding=0 keeps a running gate from holding the build lock.
#   powershell -File Tools/Art/gates.ps1 [-Only autowatch,gaptest,movetest,pausetest]
param([string[]]$Only = @("autowatch", "gaptest", "movetest", "pausetest"))
$Only = @($Only | ForEach-Object { $_ -split "," })  # -File hands the list over as one string
$Proj = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$exe = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$up = Join-Path $Proj "TAGGridiron.uproject"
$runs = @{
    autowatch = @("-TagAutoWatch");
    gaptest   = @("-TagGapTest");
    movetest  = @("-TagMoveTest");
    pausetest = @("-TagPauseTest", "-TagLoadSecs=3");
}
foreach ($k in $Only) {
    $log = "tag_gate_$k.log"
    $args = @("`"$up`"") + $runs[$k] + @("-game", "-windowed", "-ResX=1600", "-ResY=900", "-nosplash", "-LiveCoding=0", "-log=$log")
    Write-Output "== $k"
    $p = Start-Process -FilePath $exe -ArgumentList $args -PassThru -Wait
    $path = Join-Path $Proj "Saved\Logs\$log"
    $errs = (Select-String -Path $path -Pattern "Error:" | Where-Object { $_.Line -notmatch "LogWindows: Error: begin: stack|CrashReport|LogShaderCompilers" }).Count
    Write-Output "errors: $errs (exit $($p.ExitCode))"
    Select-String -Path $path -Pattern "TAG perf|TAG slide|TAG gait|TAG pad test|TAG pause test|TAG kickoff catch|TAG loading screen|TAG: pack library in play" | ForEach-Object { $_.Line -replace "^\[[^\]]*\]\[\s*\d+\]LogTemp: Display: ", "" } | Select-Object -First 14
}
