# Look matrix (Research/NextLevel/03 A0): films the same short game under four conditions (day dry, day rain, night
# dry, night snow) at 720p and 1440p with the existing capture flags, keeps the same picked frames of every run, and
# builds contact sheets (Tools/QA/look_sheet.py) so a before and an after run can be laid side by side.
#
#   powershell -ExecutionPolicy Bypass -File Tools\QA\look_matrix.ps1 [-Label before] [-Lineup] [-KeepFrames]
#   ... -Conditions day_dry,night_snow -Res 720       a subset
#   ... -Compare <earlier run folder>                 also the before/after sheets against that run
#   ... -SheetsOnly -Run <run folder> [-Compare <earlier run>]   rebuild sheets from frames already on disk
#   ... -DryRun                                       print the command lines, film nothing
#
# Output: <Out>\<yyyyMMdd_HHmm>[_<Label>]\<cond>_<res>\{picks\*.png, picks.tsv, run.log, telemetry\, lineup\}, sheets\,
# manifest.json. The default <Out> has no space in its path (the -TagHlDir value is handed to the engine as is).
# Every run of one condition is the same game (fixed -TagWeatherSeed and -TagPackSeed, fixed time step, the TV camera of
# -TagHlFull), so frame cNNN_FFFFF of a before run and of an after run are the same moment from the same camera.
# Different conditions are different games (weather feeds the sim), so compare down a column, not across rows.
# The whole frames of a run are deleted once its picks are copied (-KeepFrames keeps them: ~3 GB a run at 720p, ~12 GB
# at 1440p). Refuses to start while another UnrealEditor.exe runs; stops only the game it started itself.
param(
    [string]$Out = "",
    [string]$Label = "",
    [string]$Conditions = "day_dry,day_rain,night_dry,night_snow",
    [string]$Res = "720,1440",
    [int]$Seed = 7,
    [int]$Clips = 6,
    [switch]$Lineup,
    [switch]$KeepFrames,
    [string]$Compare = "",
    [switch]$SheetsOnly,
    [string]$Run = "",
    [string]$Extra = "",
    [int]$Timeout = 2400,
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

# ------------------------------------------------------------------------------------------------ configuration
$Proj     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$UProject = Join-Path $Proj "TAGGridiron.uproject"
$Editor   = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Sheet    = Join-Path $PSScriptRoot "look_sheet.py"
if (-not $Out) { $Out = Join-Path $env:LOCALAPPDATA "TAGLook" }   # (C:\Users\<you>\AppData\Local\TAGLook: no space)

# weather kind (StartMatch: 1 dry, 3 rain, 4 snow) and day/night per condition
$CondTable = [ordered]@{
    day_dry    = "-TagWeather=1 -TagDay"
    day_rain   = "-TagWeather=3 -TagDay"
    night_dry  = "-TagWeather=1"
    night_snow = "-TagWeather=4"
}
$ResTable = @{ "720" = @(1280, 720); "1080" = @(1920, 1080); "1440" = @(2560, 1440) }
# GB that must be free on the output drive before a run (whole frames are written every frame until the run ends)
$NeedGB = @{ "720" = 6; "1080" = 10; "1440" = 16 }

# the frames kept from every run: c<clip>_<frame> of -TagHlFull (clip 1 is the opening kickoff). A clip shorter than
# a pick gives its last frame instead (written to picks.tsv). Change the list and every later run follows it; keep it
# fixed between a before and an after run.
$Picks = @("c001_00090", "c002_00030", "c002_00150", "c003_00060", "c003_00200", "c004_00100", "c005_00045", "c006_00150")

$Common = "-game -TagHighlights -TagHlFull -TagHlMaxClips=$Clips -UseFixedTimeStep -FPS=30 -TagWeatherSeed=$Seed -TagPackSeed=$Seed " +
          "-RenderOffscreen -ForceRes -LiveCoding=0 -unattended -nosplash -AsyncLoadingThread -TagNoBooth -TagAudioSilent"
$LineupCommon = "-game -TagLineup -UseFixedTimeStep -FPS=30 -TagWeatherSeed=$Seed -TagPackSeed=$Seed -RenderOffscreen -ForceRes " +
                "-LiveCoding=0 -unattended -nosplash -AsyncLoadingThread -TagNoBooth -TagAudioSilent"

# ------------------------------------------------------------------------------------------------ helpers
function Split-List([string]$s) { @($s -split "[,\s]+" | Where-Object { $_ }) }

function Find-Python {
    foreach ($c in @("python", "py")) { $cmd = Get-Command $c -ErrorAction SilentlyContinue; if ($cmd) { return $cmd.Source } }
    $null
}

function Invoke-Sheets([string]$RunDir, [string]$Before) {
    $py = Find-Python
    if (-not $py) { Write-Output "no python on PATH: sheets not built (python Tools\QA\look_sheet.py `"$RunDir`")"; return }
    & $py $Sheet $RunDir
    if ($Before) { & $py $Sheet --compare $Before $RunDir }
}

$script:Game = $null
function Invoke-Game([string]$ArgLine, [int]$Secs) {
    $p = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru -WorkingDirectory $Proj
    $null = $p.Handle
    $script:Game = $p
    $done = $p.WaitForExit($Secs * 1000)
    if (-not $done) { try { $p.Kill() } catch {}; [void]$p.WaitForExit(30000) }   # (only the game this script started)
    $script:Game = $null
    $code = $null; try { $code = $p.ExitCode } catch {}
    @{ TimedOut = -not $done; Exit = $code }
}

$CondL = @(Split-List $Conditions); $ResL = @(Split-List $Res)
foreach ($c in $CondL) { if (-not $CondTable.Contains($c)) { Write-Output "unknown condition '$c' (conditions: $(@($CondTable.Keys) -join ', '))"; exit 2 } }
foreach ($r in $ResL) { if (-not $ResTable.ContainsKey($r)) { Write-Output "unknown resolution '$r' (720, 1080, 1440)"; exit 2 } }
if ($Compare -and -not (Test-Path $Compare)) { Write-Output "no run folder $Compare to compare with"; exit 2 }

if ($SheetsOnly) {
    if (-not $Run -or -not (Test-Path $Run)) { Write-Output "-SheetsOnly needs -Run <existing run folder>"; exit 2 }
    Invoke-Sheets $Run $Compare
    exit 0
}

if (-not (Test-Path $Editor)) { Write-Output "no $Editor"; exit 2 }
$stamp = Get-Date -Format "yyyyMMdd_HHmm"
$RunDir = Join-Path $Out ($stamp + $(if ($Label) { "_" + ($Label -replace '[^\w.-]', '_') } else { "" }))
if ($RunDir -match ' ') { Write-Output "the output path has a space in it ($RunDir): pick -Out without spaces (the engine takes -TagHlDir as is)"; exit 2 }

$commit = ""; $dirty = $false
try {
    $commit = (& git -C $Proj rev-parse --short HEAD 2>$null)
    $dirty = [bool](& git -C $Proj status --porcelain -- Source Config 2>$null)
} catch {}

Write-Output "look matrix -> $RunDir"
Write-Output ("conditions: {0}  res: {1}  seed {2}  clips {3}  lineup {4}  commit {5}{6}" -f ($CondL -join ","), ($ResL -join ","), $Seed, $Clips, [bool]$Lineup, $commit, $(if ($dirty) { " (+ uncommitted Source/Config changes)" } else { "" }))
$runs = @()
$stop = $false
try {
    foreach ($res in $ResL) {
        if ($stop) { break }
        foreach ($cond in $CondL) {
            $cell = Join-Path $RunDir ("{0}_{1}" -f $cond, $res)
            $frames = Join-Path $cell "frames"
            $log = Join-Path $cell "run.log"
            $wh = $ResTable[$res]
            $fwd = ($frames -replace '\\', '/')
            $argLine = "`"$UProject`" $Common $($CondTable[$cond]) -ResX=$($wh[0]) -ResY=$($wh[1]) -TagHlDir=$fwd -abslog=`"$log`" $Extra".Trim()
            $lineLog = Join-Path $cell "lineup.log"
            $lineArg = "`"$UProject`" $LineupCommon $($CondTable[$cond]) -ResX=$($wh[0]) -ResY=$($wh[1]) -abslog=`"$lineLog`" $Extra".Trim()
            if ($DryRun) {
                Write-Output ""
                Write-Output "[$cond $res] UnrealEditor.exe $argLine"
                if ($Lineup) { Write-Output "[$cond $res lineup] UnrealEditor.exe $lineArg" }
                continue
            }
            $others = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction SilentlyContinue)
            if ($others.Count) {
                Write-Output ("UnrealEditor.exe is running (pid {0}): close it, then run again. Stopping here." -f (($others | ForEach-Object { $_.ProcessId }) -join ", "))
                $stop = $true; break
            }
            New-Item -ItemType Directory -Force -Path $frames | Out-Null
            $drive = (Get-Item $RunDir).PSDrive
            $free = [math]::Round((Get-PSDrive $drive.Name).Free / 1GB, 1)
            if ($free -lt $NeedGB[$res]) { Write-Output "only $free GB free on $($drive.Name): (a $res run needs ~$($NeedGB[$res]) GB while it films). Stopping here."; $stop = $true; break }
            $t0 = Get-Date
            Write-Output ""
            Write-Output "[$cond $res] filming $Clips clips ..."
            $r = Invoke-Game $argLine $Timeout
            $secs = [math]::Round(((Get-Date) - $t0).TotalSeconds)
            # clip lengths from the log, then the picks (or the clip's last frame)
            $clipFrames = @{}
            if (Test-Path $log) {
                foreach ($m in (Select-String -Path $log -Pattern 'TAG hl clip (\d+) end frames (\d+)')) {
                    $clipFrames[$m.Matches[0].Groups[1].Value] = [int]$m.Matches[0].Groups[2].Value   # (string keys: ConvertTo-Json in 5.1 needs them)
                }
            }
            $done = (Test-Path $log) -and (Select-String -Path $log -Pattern 'TAG hl done' -Quiet)
            $picksDir = Join-Path $cell "picks"
            New-Item -ItemType Directory -Force -Path $picksDir | Out-Null
            $tsv = @("pick`tfile`tnote")
            $kept = 0
            foreach ($p in $Picks) {
                $src = Join-Path $frames "$p.png"
                $got = $p; $note = ""
                if (-not (Test-Path $src)) {
                    $clip = [int]$p.Substring(1, 3)
                    $last = @(Get-ChildItem $frames -Filter ("c{0:D3}_*.png" -f $clip) -ErrorAction SilentlyContinue | Sort-Object Name)
                    if ($last.Count) { $src = $last[-1].FullName; $got = $last[-1].BaseName; $note = "clip $clip has $($last.Count) frames: its last" }
                    else { $src = $null; $got = ""; $note = "clip $clip not filmed" }
                }
                if ($src) { Copy-Item $src (Join-Path $picksDir "$got.png") -Force; $kept++ }
                $tsv += "$p`t$got`t$note"
            }
            $tsv | Out-File -FilePath (Join-Path $cell "picks.tsv") -Encoding utf8
            $nFrames = @(Get-ChildItem $frames -Filter "c*_*.png" -ErrorAction SilentlyContinue).Count
            if (-not $KeepFrames) {
                Get-ChildItem $frames -Filter "c*_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
                if (-not @(Get-ChildItem $frames -ErrorAction SilentlyContinue).Count) { Remove-Item $frames -Force -ErrorAction SilentlyContinue }
            }
            $status = if ($r.TimedOut) { "TIMEOUT (stopped after $Timeout s)" } elseif (-not $done) { "no 'TAG hl done' (exit $($r.Exit))" } else { "ok" }
            Write-Output ("[$cond $res] $status, $secs s, clips {0}, $nFrames frames, $kept/$($Picks.Count) picks kept" -f (($clipFrames.Keys | Sort-Object { [int]$_ } | ForEach-Object { "$_=$($clipFrames[$_])" }) -join " "))
            $entry = [ordered]@{ condition = $cond; res = $res; flags = $CondTable[$cond]; status = $status; exit = $r.Exit; seconds = $secs;
                                 frames = $nFrames; picks_kept = $kept; clip_frames = $clipFrames; lineup = $null }
            if ($Lineup) {
                $lt0 = Get-Date
                $lr = Invoke-Game $lineArg 600
                $ldir = Join-Path $cell "lineup"
                New-Item -ItemType Directory -Force -Path $ldir | Out-Null
                $shots = @(Get-ChildItem (Join-Path $Proj "Saved\Screenshots") -Recurse -Filter "tag_lineup_*.png" -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $lt0 })
                foreach ($s in $shots) { Copy-Item $s.FullName (Join-Path $ldir $s.Name) -Force }
                $entry.lineup = "$($shots.Count) shots" + $(if ($lr.TimedOut) { " (TIMEOUT)" } else { "" })
                Write-Output "[$cond $res lineup] $($entry.lineup)"
            }
            $runs += [pscustomobject]$entry
        }
    }
}
finally {
    if ($script:Game -and -not $script:Game.HasExited) {
        Write-Output "interrupted: stopping the game this script started (pid $($script:Game.Id))"
        try { $script:Game.Kill() } catch {}
    }
}
if ($DryRun) { exit 0 }
if (-not (Test-Path $RunDir)) { exit 1 }
$manifest = [ordered]@{ stamp = $stamp; label = $Label; commit = $commit; uncommitted_source = $dirty; seed = $Seed; clips = $Clips;
    conditions = $CondL; resolutions = $ResL; picks = $Picks; common_flags = $Common; extra = $Extra; runs = $runs }
$manifest | ConvertTo-Json -Depth 5 | Out-File -FilePath (Join-Path $RunDir "manifest.json") -Encoding utf8
Write-Output ""
Invoke-Sheets $RunDir $Compare
Write-Output ""
Write-Output "-> $RunDir"
if (@($runs | Where-Object { $_.status -ne "ok" }).Count -or $runs.Count -lt ($CondL.Count * $ResL.Count)) { exit 1 }
exit 0
