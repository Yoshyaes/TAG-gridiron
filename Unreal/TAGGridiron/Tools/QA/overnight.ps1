# TAG Gridiron overnight matrix (Research/NextLevel/07 A8): runs one game process at a time over seeds x weather x
# day/night x pace x modes, survives a crash or a hang of any cell, and collects every cell's TAG RESULT lines, new crash
# reports, defect list (Tools/QA/defects.py on -TagHlNoFrames telemetry) and perf numbers into one summary:
# Saved\Overnight\<stamp>\summary.md + summary.json (+ one folder per cell with its log, defects.md, defects.json).
#
#   powershell -ExecutionPolicy Bypass -File Tools\QA\overnight.ps1 -DryRun            print the cells, run nothing
#   ... -Seeds 7,11,29 -Weather 1,3,4 -Light night,day -Pace full,quick -Modes cpu,padplay,feel
#   ... -RotateTeams                 cpu / soak cells walk the league in pairs (cell i: teams 2i and 2i+1), so 16 cpu
#                                    cells field all 32 teams once
#   ... -QuarterLen 120 -MaxClips 0  short quarters (the default); -MaxClips caps a cpu game's plays
#   ... -StopBake                    create Saved\BoothVoice\_bake_stop first and wait for the bake's chunk to end
#   ... -MaxCells 4                  only the first n cells (a smoke run)
#   ... -Budget Tools\QA\defect_budget.json   the defect budget (default; "" = report only)
#
# Modes: cpu (a CPU-vs-CPU game, -TagHighlights -TagHlFull -TagHlNoFrames: telemetry, no frames), soak (-TagSoak, a
# watched game with its own TAG RESULT), bench (frame time, TAG bench summary), and the self-checking user modes padplay,
# feel, multi, pause, controls, hitstop, input. Only cpu cells use all four axes: soak and bench always play at night
# (MatchDirector hard-codes it), and the user modes fix their own weather and light, so they run once per seed x pace.
#
# Each cell gets -TagTimeout=<s> (the game ends itself with status 2) and a wall-clock backstop 120 s later that stops
# the process this script started (and its own children), never anything else. A cell does not start while another
# UnrealEditor.exe runs: it waits up to -WaitIdleMin minutes, then is BLOCKED. Results per cell: PASS, FAIL, TIMEOUT,
# CRASH, BLOCKED, SKIPPED. Exit 1 if any cell is not PASS or a defect list is over budget.
param(
    [string]$Seeds = "7,11",
    [string]$Weather = "1,3,4",
    [string]$Light = "night,day",
    [string]$Pace = "",
    [string]$Modes = "cpu",
    [switch]$RotateTeams,
    [int]$HomeTeam = 3,
    [int]$AwayTeam = 7,
    [double]$QuarterLen = 120,
    [int]$MaxClips = 0,
    [int]$CellTimeout = 0,
    [int]$MaxCells = 0,
    [string]$Extra = "",
    [string]$Budget = "",
    [switch]$NoBudget,
    [switch]$StopBake,
    [int]$WaitIdleMin = 30,
    [string]$TeleRoot = "",
    [switch]$DropTelemetry,
    [switch]$DryRun
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

# ------------------------------------------------------------------------------------------------ configuration
$Proj     = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path          # Unreal\TAGGridiron
$UProject = Join-Path $Proj "TAGGridiron.uproject"
$Editor   = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$GameDll  = Join-Path $Proj "Binaries\Win64\UnrealEditor-TAGGridiron.dll"
$Defects  = Join-Path $PSScriptRoot "defects.py"
$Crashes  = Join-Path $PSScriptRoot "crashes.py"
$BakeStop = Join-Path $Proj "Saved\BoothVoice\_bake_stop"
if (-not $Budget -and -not $NoBudget) { $Budget = Join-Path $PSScriptRoot "defect_budget.json" }
if (-not $TeleRoot) { $TeleRoot = Join-Path $env:LOCALAPPDATA "TAGOvernight" }   # (no space: -TagHlDir goes to the engine as is)

$Common   = "-game -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 -LiveCoding=0 -unattended -nosplash -AsyncLoadingThread -TagNoBooth -TagAudioSilent"
$BenchRes = "-ResX=1920 -ResY=1080"
$WxName   = @{ 1 = "dry"; 2 = "wind"; 3 = "rain"; 4 = "snow" }
$NTeams   = 32
# GB free needed on the telemetry drive before a cpu cell (a short-quarter game's telemetry is ~0.2-0.5 GB)
$NeedGB   = 3

# Per mode: what it varies over, its flags ({seed} {wx} {light} {pace} {home} {away} {ql} {clips} {tele} filled in),
# its default timeout (s), and how it reports (hl = TAG hl done + telemetry, result = TAG RESULT line, bench = TAG bench summary)
$ModeTable = [ordered]@{
    cpu      = @{ Axes = "seed,wx,light,pace,teams"; Report = "hl"; Timeout = 3600;
                  Flags = "-TagHighlights -TagHlFull -TagHlNoFrames -UseFixedTimeStep -FPS=30 -TagWeatherSeed={seed} -TagPackSeed={seed} -TagWeather={wx} {light} {pace} -TagHome={home} -TagAway={away} -TagQuarterLen={ql} {clips} -TagHlDir={tele}/frames" }
    soak     = @{ Axes = "seed,wx,pace,teams"; Report = "result"; Timeout = 900;
                  Flags = "-TagSoak=600 -TagQuarterLen={ql} -TagNoLoad -TagWeatherSeed={seed} -TagWeather={wx} {pace} -TagHome={home} -TagAway={away}" }
    bench    = @{ Axes = "wx"; Report = "bench"; Timeout = 900;
                  Flags = "-TagSoak=100000 -TagNoLoad -TagBench=60 -TagBenchWarm=30 -TagWeatherSeed={seed} -TagWeather={wx}" }
    padplay  = @{ Axes = "seed,pace"; Report = "result"; Timeout = 480;  Flags = "-TagPadPlayTest -TagWeatherSeed={seed} {pace}" }
    feel     = @{ Axes = "seed,pace"; Report = "result"; Timeout = 900;  Flags = "-TagFeelTest -TagWeatherSeed={seed} {pace}" }
    multi    = @{ Axes = "seed,pace"; Report = "result"; Timeout = 480;  Flags = "-TagMultiTest -TagWeatherSeed={seed} {pace}" }
    pause    = @{ Axes = "seed,pace"; Report = "result"; Timeout = 300;  Flags = "-TagPauseTest -TagLoadSecs=3 -TagWeatherSeed={seed} {pace}" }
    controls = @{ Axes = "seed,pace"; Report = "result"; Timeout = 300;  Flags = "-TagControlsTest -TagUserKicks -TagNoLoad -TagWeatherSeed={seed} {pace}" }
    hitstop  = @{ Axes = "seed,pace"; Report = "result"; Timeout = 900;  Flags = "-TagHitStopTest -TagWeatherSeed={seed} -TagHitStopPlays=16 -TagQuarterLen=120 -TagNoLoad {pace}" }
    input    = @{ Axes = "seed"; Report = "result"; Timeout = 300;       Flags = "-TagInputTest -TagNoLoad -TagWeatherSeed={seed}" }
}
$ResultRx  = 'TAG RESULT\s+(\S+)\s+pass=(\d+)\s+fail=(\d+)'
$Protected = @("zenserver", "CrashReportClient", "CrashReportClientEditor", "UnrealTraceServer")

# ------------------------------------------------------------------------------------------------ helpers
function Split-List([string]$s) { @($s -split "[,\s]+" | Where-Object { $_ }) }

function Find-Python {
    foreach ($c in @("python", "py")) { $cmd = Get-Command $c -ErrorAction SilentlyContinue; if ($cmd) { return $cmd.Source } }
    $null
}

function Get-TeamAbbr {
    # league order from Sim/League.cpp (kTeamSeeds): index -> abbreviation, for the summary
    $map = @{}
    $f = Join-Path $Proj "Source\TAGGridiron\Sim\League.cpp"
    if (-not (Test-Path $f)) { return $map }
    $i = 0; $on = $false
    foreach ($ln in (Get-Content $f)) {
        if ($ln -match 'kTeamSeeds\s*=') { $on = $true; continue }
        if (-not $on) { continue }
        if ($ln -match '^\s*\};') { break }
        $m = [regex]::Match($ln, '^\s*\{\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"([A-Z]{2,4})"')
        if ($m.Success) { $map[$i] = $m.Groups[1].Value; $i++ }
    }
    $map
}

function Get-Editors { @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction SilentlyContinue) }

function Get-OwnDescendants([int]$RootPid, [datetime]$RootStart) {
    $all = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    $found = New-Object System.Collections.ArrayList
    $seen = @{}
    $queue = New-Object System.Collections.Queue
    $queue.Enqueue($RootPid)
    while ($queue.Count -gt 0) {
        $parent = $queue.Dequeue()
        foreach ($c in $all) {
            # (a child is ours only if it started after our root did: Windows reuses process ids)
            if ($c.ParentProcessId -eq $parent -and $c.ProcessId -ne $RootPid -and $c.CreationDate -ge $RootStart -and -not $seen.ContainsKey([int]$c.ProcessId)) {
                $seen[[int]$c.ProcessId] = $true
                [void]$found.Add($c); $queue.Enqueue([int]$c.ProcessId)
            }
        }
    }
    , $found
}

function Stop-Own($Proc) {
    # the game this script started and its own children (shader workers), never another process
    if (-not $Proc -or $Proc.HasExited) { return }
    foreach ($c in (Get-OwnDescendants $Proc.Id $Proc.StartTime)) {
        $n = [IO.Path]::GetFileNameWithoutExtension($c.Name)
        if ($Protected -contains $n) { continue }
        try { Stop-Process -Id $c.ProcessId -Force -ErrorAction Stop } catch {}
    }
    try { $Proc.Kill() } catch {}
    try { [void]$Proc.WaitForExit(30000) } catch {}
}

function Get-CrashFolders {
    $d = Join-Path $Proj "Saved\Crashes"
    if (Test-Path $d) { @(Get-ChildItem $d -Directory | ForEach-Object { $_.Name }) } else { @() }
}

function Get-FreeRamGB {
    try { [math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB, 1) } catch { $null }
}

function Wait-Idle([int]$Minutes) {
    # true once no UnrealEditor.exe runs (polled every 30 s for up to $Minutes)
    $until = (Get-Date).AddMinutes($Minutes)
    $said = $false
    while ($true) {
        $o = @(Get-Editors)
        if (-not $o.Count) { return $true }
        if ((Get-Date) -ge $until) { return $false }
        if (-not $said) { Write-Output ("  waiting: UnrealEditor.exe running (pid {0}), up to {1} min" -f (($o | ForEach-Object { $_.ProcessId }) -join ", "), $Minutes); $said = $true }
        Start-Sleep -Seconds 30
    }
}

function Stop-VoiceBake {
    # the idle voice bake (Tools/Booth/bake_when_idle.ps1) ends after its current chunk once the stop file exists; wait for
    # that chunk's booth_tts.py to finish (up to 6 min). Never kills it. Delete the file before restarting the bake.
    New-Item -ItemType File -Force -Path $BakeStop | Out-Null
    Write-Host "voice bake: stop file written ($BakeStop)"
    $until = (Get-Date).AddMinutes(6)
    while ((Get-Date) -lt $until) {
        $tts = @(Get-CimInstance Win32_Process -Filter "Name='python.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine -match 'booth_tts' })
        if (-not $tts.Count) { Write-Host "voice bake: no chunk running"; return "stopped" }
        Start-Sleep -Seconds 15
    }
    Write-Host "voice bake: a chunk is still running after 6 min (left alone)"
    "stop requested, chunk still running"
}

function Fill([string]$Tpl, $Cell, [string]$Tele) {
    $light = if ($Cell.light -eq "day") { "-TagDay" } else { "" }
    $pace = if ($Cell.pace) { "-TagPace=" + $Cell.pace } else { "" }
    $clips = if ($MaxClips -gt 0) { "-TagHlMaxClips=$MaxClips" } else { "" }
    $s = $Tpl.Replace("{seed}", [string]$Cell.seed).Replace("{wx}", [string]$Cell.wx).Replace("{light}", $light).Replace("{pace}", $pace)
    $s = $s.Replace("{home}", [string]$Cell.home).Replace("{away}", [string]$Cell.away).Replace("{ql}", [string]$QuarterLen).Replace("{clips}", $clips)
    $s = $s.Replace("{tele}", ($Tele -replace '\\', '/'))
    ($s -replace '\s+', ' ').Trim()
}

# ------------------------------------------------------------------------------------------------ the cells
$SeedL = @(Split-List $Seeds | ForEach-Object { [int]$_ })
$WxL = @(Split-List $Weather | ForEach-Object { [int]$_ })
$LightL = @(Split-List $Light)
$PaceL = @(Split-List $Pace); if (-not $PaceL.Count) { $PaceL = @("") }
$ModeL = @(Split-List $Modes)
foreach ($m in $ModeL) { if (-not $ModeTable.Contains($m)) { Write-Output "unknown mode '$m' (modes: $(@($ModeTable.Keys) -join ', '))"; exit 2 } }
foreach ($w in $WxL) { if (-not $WxName.ContainsKey($w)) { Write-Output "unknown weather $w (1 dry, 2 wind, 3 rain, 4 snow)"; exit 2 } }
foreach ($l in $LightL) { if ($l -notin @("day", "night")) { Write-Output "unknown light '$l' (day, night)"; exit 2 } }
foreach ($p in $PaceL) { if ($p -and $p -notin @("full", "standard", "quick")) { Write-Output "unknown pace '$p' (full, standard, quick)"; exit 2 } }
if (-not $SeedL.Count -or -not $WxL.Count -or -not $LightL.Count) { Write-Output "empty -Seeds, -Weather or -Light"; exit 2 }
if ($TeleRoot -match ' ') { Write-Output "-TeleRoot has a space in it ($TeleRoot): the engine takes -TagHlDir as is"; exit 2 }

$Abbr = Get-TeamAbbr
$cells = New-Object System.Collections.ArrayList
$teamI = 0
foreach ($mode in $ModeL) {
    $axes = $ModeTable[$mode].Axes
    $sL = if ($axes -match 'seed') { $SeedL } else { @($SeedL[0]) }
    $wL = if ($axes -match 'wx') { $WxL } else { @(1) }
    $lL = if ($axes -match 'light') { $LightL } else { @("night") }
    $pL = if ($axes -match 'pace') { $PaceL } else { @("") }
    foreach ($s in $sL) { foreach ($w in $wL) { foreach ($l in $lL) { foreach ($p in $pL) {
        $h = $HomeTeam; $a = $AwayTeam
        if ($axes -match 'teams' -and $RotateTeams) { $h = (2 * $teamI) % $NTeams; $a = (2 * $teamI + 1) % $NTeams; $teamI++ }
        if ($axes -notmatch 'teams') { $h = 3; $a = 7 }     # (the user modes and the bench field OKC v MEM)
        $c = [ordered]@{ n = $cells.Count + 1; mode = $mode; seed = $s; wx = $w; weather = $WxName[$w]; light = $l; pace = $p;
                         home = $h; away = $a; teams = ("{0}@{1}" -f $(if ($Abbr.ContainsKey($a)) { $Abbr[$a] } else { $a }), $(if ($Abbr.ContainsKey($h)) { $Abbr[$h] } else { $h })) }
        $c.id = "{0:D2}_{1}_s{2}{3}{4}{5}{6}" -f $c.n, $mode, $s, $(if ($axes -match 'wx') { "_" + $c.weather } else { "" }),
            $(if ($axes -match 'light') { "_" + $l } else { "" }), $(if ($p) { "_" + $p } else { "" }), $(if ($axes -match 'teams') { "_" + $c.teams } else { "" })
        [void]$cells.Add($c)
    } } } }
}
if ($MaxCells -gt 0 -and $cells.Count -gt $MaxCells) { $cells = [System.Collections.ArrayList]@($cells | Select-Object -First $MaxCells) }

$Stamp = Get-Date -Format "yyyyMMdd_HHmm"
$OutDir = Join-Path $Proj "Saved\Overnight\$Stamp"
$TeleDir = Join-Path $TeleRoot $Stamp
$commit = ""; $dirty = $false
try { $commit = (& git -C $Proj rev-parse --short HEAD 2>$null); $dirty = [bool](& git -C $Proj status --porcelain -- Source Config 2>$null) } catch {}

$estMin = 0
foreach ($c in $cells) { $t = if ($CellTimeout -gt 0) { $CellTimeout } else { $ModeTable[$c.mode].Timeout }; $estMin += $t / 60 }
Write-Output ("TAG overnight {0}: {1} cells ({2}), at most ~{3:N0} min (every cell at its timeout)  commit {4}{5}" -f $Stamp, $cells.Count,
    (($cells | Group-Object { $_.mode } | ForEach-Object { "$($_.Count) $($_.Name)" }) -join ", "), $estMin, $commit, $(if ($dirty) { " + uncommitted Source/Config" } else { "" }))
if ($RotateTeams) { Write-Output ("teams: {0} distinct over the cpu / soak cells" -f @($cells | Where-Object { $ModeTable[$_.mode].Axes -match 'teams' } | ForEach-Object { $_.home; $_.away } | Sort-Object -Unique).Count) }
Write-Output ("out: {0}   telemetry: {1}   budget: {2}" -f $OutDir, $TeleDir, $(if ($Budget) { $Budget } else { "none" }))
Write-Output ""

if ($DryRun) {
    foreach ($c in $cells) {
        $md = $ModeTable[$c.mode]
        $tele = Join-Path $TeleDir $c.id
        $to = if ($CellTimeout -gt 0) { $CellTimeout } else { $md.Timeout }
        $log = Join-Path $OutDir "$($c.id)\run.log"
        $res = if ($c.mode -eq "bench") { $BenchRes } else { "" }
        $flags = Fill $md.Flags $c $tele
        Write-Output ("[{0}] UnrealEditor.exe `"{1}`" {2} {3} {4} -TagTimeout={5} -abslog=`"{6}`" {7}" -f $c.id, $UProject, $Common, $res, $flags, $to, $log, $Extra).Replace("  ", " ").Trim()
        if ($md.Report -eq "hl") { Write-Output ("      then: python Tools\QA\defects.py `"{0}`" --log `"{1}`" --out-dir `"{2}`"{3}" -f $tele, $log, (Split-Path $log), $(if ($Budget) { " --budget `"$Budget`"" } else { " --no-budget" })) }
    }
    Write-Output ""
    Write-Output ("DRYRUN: {0} cells; nothing started{1}" -f $cells.Count, $(if ($StopBake) { "; -StopBake would write $BakeStop" } else { "" }))
    if (@(Get-Editors).Count) { Write-Output "(note: UnrealEditor.exe is running now: a real run would wait up to $WaitIdleMin min per cell for it)" }
    exit 0
}

# ------------------------------------------------------------------------------------------------ run
foreach ($need in @($Editor, $UProject)) { if (-not (Test-Path $need)) { Write-Output "missing $need"; exit 2 } }
if (-not (Test-Path $GameDll)) { Write-Output "missing $GameDll (build the editor target)"; exit 2 }
$Py = Find-Python
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Start = Get-Date
$bake = "left alone"
if ($StopBake) { $bake = Stop-VoiceBake }
$Rows = New-Object System.Collections.ArrayList
$script:Running = $null
$stopAll = ""
$waitMin = $WaitIdleMin

function Write-Summary {
    $done = @($Rows)
    $byRes = @{}
    foreach ($r in $done) { $byRes[$r.result] = 1 + $(if ($byRes.ContainsKey($r.result)) { $byRes[$r.result] } else { 0 }) }
    $over = @($done | Where-Object { $_.defects_verdict -eq "OVER" })
    $sum = [ordered]@{ stamp = $Stamp; commit = $commit; uncommitted_source = $dirty; started = $Start.ToString("s");
        seconds = [math]::Round(((Get-Date) - $Start).TotalSeconds); cells_planned = $cells.Count; cells_run = $done.Count;
        results = $byRes; defects_over_budget = $over.Count; voice_bake = $bake; stopped_early = $stopAll; budget = $Budget;
        args = [ordered]@{ seeds = $Seeds; weather = $Weather; light = $Light; pace = $Pace; modes = $Modes; rotate_teams = [bool]$RotateTeams;
                           quarter_len = $QuarterLen; max_clips = $MaxClips; extra = $Extra }; cells = $done }
    $sum | ConvertTo-Json -Depth 7 | Out-File -FilePath (Join-Path $OutDir "summary.json") -Encoding utf8

    $L = New-Object System.Collections.ArrayList
    [void]$L.Add("# TAG overnight $Stamp")
    [void]$L.Add("")
    [void]$L.Add(("Commit {0}{1}. {2} of {3} cells run in {4:N0} min. Results: {5}. Defect lists over budget: {6}. Voice bake: {7}.{8}" -f $commit,
        $(if ($dirty) { " (+ uncommitted Source/Config)" } else { "" }), $done.Count, $cells.Count, (((Get-Date) - $Start).TotalMinutes),
        (($byRes.Keys | Sort-Object | ForEach-Object { "$_ $($byRes[$_])" }) -join ", "), $over.Count, $bake, $(if ($stopAll) { " Stopped early: $stopAll." } else { "" })))
    [void]$L.Add("")
    [void]$L.Add("| cell | mode | seed | weather | light | pace | teams | result | min | detail | defects (high) | budget | peak GB | fps |")
    [void]$L.Add("|---|---|---:|---|---|---|---|---|---:|---|---:|---|---:|---:|")
    foreach ($r in $done) {
        [void]$L.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | **{7}** | {8:N1} | {9} | {10} | {11} | {12} | {13} |" -f $r.id, $r.mode, $r.seed,
            $(if ($r.axes -match 'wx') { $r.weather } else { "-" }), $(if ($r.axes -match 'light') { $r.light } else { "-" }), $(if ($r.pace) { $r.pace } else { "-" }),
            $(if ($r.axes -match 'teams') { $r.teams } else { "-" }), $r.result, ($r.seconds / 60.0), ($r.detail -replace '\|', '/'),
            $(if ($null -ne $r.defects) { "$($r.defects) ($($r.defects_high))" } else { "-" }), $r.defects_verdict,
            $(if ($r.peak_gb) { $r.peak_gb } else { "-" }), $(if ($r.fps) { $r.fps } else { "-" })))
    }
    [void]$L.Add("")
    # by team, weather and light: failures, stalls, defects
    foreach ($axis in @("teams", "weather", "light")) {
        $grp = @($done | Where-Object { $_.axes -match $(if ($axis -eq "weather") { "wx" } else { $axis }) } | Group-Object { $_.$axis })
        if (-not $grp.Count) { continue }
        [void]$L.Add("## By $axis")
        [void]$L.Add("")
        [void]$L.Add("| $axis | cells | not PASS | crashes | timeouts | stalls | defects | high |")
        [void]$L.Add("|---|---:|---:|---:|---:|---:|---:|---:|")
        foreach ($g in $grp) {
            $v = @($g.Group)
            $sumOf = { param($k) ($v | ForEach-Object { if ($null -ne $_.$k) { [int]$_.$k } else { 0 } } | Measure-Object -Sum).Sum }
            [void]$L.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |" -f $g.Name, $v.Count, @($v | Where-Object { $_.result -ne "PASS" }).Count,
                @($v | Where-Object { $_.result -eq "CRASH" }).Count, @($v | Where-Object { $_.result -eq "TIMEOUT" }).Count,
                (& $sumOf "stalls"), (& $sumOf "defects"), (& $sumOf "defects_high")))
        }
        [void]$L.Add("")
    }
    $top = @($done | ForEach-Object { $cell = $_; if ($_.top) { $_.top | ForEach-Object { $_ | Add-Member -NotePropertyName cell -NotePropertyValue $cell.id -Force -PassThru } } } |
        Sort-Object -Property @{ Expression = { $_.score }; Descending = $true } | Select-Object -First 15)
    if ($top.Count) {
        [void]$L.Add("## Worst defects")
        [void]$L.Add("")
        foreach ($e in $top) {
            [void]$L.Add(("- **{0}** sev {1}, {2}: clip {3} frame {4}, {5}: {6}{7}" -f $e.cls, $e.sev, $e.cell, $e.clip, $e.frame, $e.game_time, $e.detail,
                $(if ($e.players) { " -- " + ($e.players -join ", ") } else { "" })))
            [void]$L.Add("  - replay: ``$($e.replay)``")
        }
        [void]$L.Add("")
    }
    $crashFile = Join-Path $OutDir "crashes.log"
    if (Test-Path $crashFile) {
        [void]$L.Add("## Crash register")
        [void]$L.Add("")
        foreach ($ln in (Get-Content $crashFile | Where-Object { $_ -match 'CRASH CHECK' })) { [void]$L.Add("- $ln") }
        [void]$L.Add("")
    }
    ($L -join "`n") | Out-File -FilePath (Join-Path $OutDir "summary.md") -Encoding utf8
}

try {
    foreach ($c in $cells) {
        $md = $ModeTable[$c.mode]
        $cellDir = Join-Path $OutDir $c.id
        $tele = Join-Path $TeleDir $c.id
        $log = Join-Path $cellDir "run.log"
        $to = if ($CellTimeout -gt 0) { $CellTimeout } else { $md.Timeout }
        $row = [ordered]@{ n = $c.n; id = $c.id; mode = $c.mode; axes = $md.Axes; seed = $c.seed; weather = $c.weather; light = $c.light; pace = $c.pace;
                           teams = $c.teams; home = $c.home; away = $c.away; result = "SKIPPED"; exit = $null; seconds = 0; detail = ""; results = @();
                           crash_reports = @(); clips = $null; final_score = $null; defects = $null; defects_high = $null; stalls = $null;
                           defects_verdict = "-"; top = @(); peak_gb = $null; free_ram_gb = $null; fps = $null; bench = $null; log = $log; telemetry = $null }
        if ($stopAll) { $row.detail = $stopAll; [void]$Rows.Add([pscustomobject]$row); continue }
        New-Item -ItemType Directory -Force -Path $cellDir | Out-Null
        if ($md.Report -eq "hl") {
            New-Item -ItemType Directory -Force -Path $tele | Out-Null
            $drive = (Get-Item $tele).PSDrive.Name
            $free = [math]::Round((Get-PSDrive $drive).Free / 1GB, 1)
            if ($free -lt $NeedGB) { $stopAll = "only $free GB free on ${drive}: (need $NeedGB GB a cpu cell)"; $row.detail = $stopAll; [void]$Rows.Add([pscustomobject]$row); Write-Summary; continue }
            $row.telemetry = $tele
        }
        Write-Output ("[{0}] {1}" -f $c.id, (Get-Date -Format "HH:mm:ss"))
        if (-not (Wait-Idle $waitMin)) {
            $row.result = "BLOCKED"; $row.detail = "another UnrealEditor.exe kept running"
            $waitMin = 1      # (the next cells try again only briefly)
            [void]$Rows.Add([pscustomobject]$row); Write-Summary
            Write-Output "  BLOCKED"
            continue
        }
        $waitMin = $WaitIdleMin
        $res = if ($c.mode -eq "bench") { $BenchRes } else { "" }
        $argLine = ("`"$UProject`" $Common $res " + (Fill $md.Flags $c $tele) + " -TagTimeout=$to -abslog=`"$log`" $Extra") -replace '\s+', ' '
        $row.free_ram_gb = Get-FreeRamGB
        $before = @(Get-CrashFolders)
        $t0 = Get-Date
        $p = Start-Process -FilePath $Editor -ArgumentList $argLine.Trim() -PassThru -WorkingDirectory $Proj
        $null = $p.Handle
        $script:Running = $p
        $peak = 0L; $killed = ""
        $deadline = $t0.AddSeconds($to + 120)
        while (-not $p.WaitForExit(5000)) {
            try { $p.Refresh(); if ($p.PeakWorkingSet64 -gt $peak) { $peak = $p.PeakWorkingSet64 } } catch {}
            if ((Get-Date) -ge $deadline) { $killed = "hung past -TagTimeout + 120 s: stopped"; break }
            # an old binary does not know -TagHlNoFrames and films every frame (~70 GB a game): stop it
            if ($md.Report -eq "hl" -and (Test-Path (Join-Path $tele "frames")) -and @(Get-ChildItem (Join-Path $tele "frames") -Filter *.png -ErrorAction SilentlyContinue | Select-Object -First 1).Count) {
                $killed = "frames being written: the game binary predates -TagHlNoFrames (rebuild)"; break
            }
        }
        if ($killed) { Stop-Own $p } else { $p.WaitForExit() }
        $script:Running = $null
        $code = $null; try { $code = $p.ExitCode } catch {}
        $row.exit = $code
        $row.seconds = [math]::Round(((Get-Date) - $t0).TotalSeconds)
        if ($peak -gt 0) { $row.peak_gb = [math]::Round($peak / 1GB, 1) }
        $newCrash = @(Get-CrashFolders | Where-Object { $before -notcontains $_ })
        $row.crash_reports = $newCrash
        $hasLog = Test-Path $log
        $results = @(); $tagTimeout = $false; $critical = $false; $done = $false
        if ($hasLog) {
            $results = @(Select-String -Path $log -Pattern $ResultRx | ForEach-Object { $_.Matches[0].Value })
            $tagTimeout = [bool](Select-String -Path $log -Pattern "TAG timeout" -Quiet)
            $critical = [bool](Select-String -Path $log -Pattern "=== Critical error: ===|Fatal error!" -Quiet)
            $done = [bool](Select-String -Path $log -Pattern "TAG hl done" -Quiet)
        }
        $row.results = $results
        $why = @()
        if ($killed) { $why += $killed }
        if ($tagTimeout) { $why += "game logged TAG timeout ($to s)" }
        if ($newCrash.Count) { $why += "crash report " + ($newCrash -join ", ") }
        elseif ($critical) { $why += "critical error in the log" }
        if (-not $hasLog) { $why += "no log" }
        # what the cell reports
        if ($md.Report -eq "result") {
            $pass = 0; $fail = 0
            foreach ($x in $results) { $mm = [regex]::Match($x, $ResultRx); $pass += [int]$mm.Groups[2].Value; $fail += [int]$mm.Groups[3].Value }
            if ($hasLog -and -not $results.Count) { $why += "no 'TAG RESULT' line" }
            elseif ($fail -gt 0) { $why += "fail=$fail" }
            elseif ($results.Count -and $pass -eq 0) { $why += "pass=0" }
            if (-not $killed -and $null -ne $code -and $code -ne 0) { $why += "exit $code" }
            $row.detail = ("pass=$pass fail=$fail " + ($why -join "; ")).Trim()
        }
        elseif ($md.Report -eq "bench") {
            $b = if ($hasLog) { Select-String -Path $log -Pattern 'TAG bench summary.*' | Select-Object -Last 1 } else { $null }
            if ($b) {
                $line = $b.Matches[0].Value
                $row.bench = $line
                $fm = [regex]::Match($line, 'fps=([\d.]+).*low1=([\d.]+)fps')
                if ($fm.Success) { $row.fps = [double]$fm.Groups[1].Value; $row.detail = ("avg {0} fps, 1% low {1} fps " -f $fm.Groups[1].Value, $fm.Groups[2].Value) + ($why -join "; ") }
            } else { $why += "no 'TAG bench summary'"; $row.detail = $why -join "; " }
        }
        else {
            # a cpu game: the log's clips, frames (gf) and final score, then the defect list
            $clipN = 0; $gf0 = $null; $gf1 = $null; $frames = 0
            if ($hasLog) {
                foreach ($m in (Select-String -Path $log -Pattern 'TAG hl clip (\d+) (start cam \d+ gf (\d+)|end frames (\d+)|game q\d+ clock \S+ score \d+-\d+ gf (\d+))')) {
                    $g = $m.Matches[0].Groups
                    $clipN = [math]::Max($clipN, [int]$g[1].Value)
                    if ($g[3].Success -and $null -eq $gf0) { $gf0 = [long]$g[3].Value }
                    if ($g[4].Success) { $frames += [int]$g[4].Value }
                    if ($g[5].Success) { $gf1 = [long]$g[5].Value }
                }
                $fs = Select-String -Path $log -Pattern 'TAG hl final score (\d+-\d+)' | Select-Object -Last 1
                if ($fs) { $row.final_score = $fs.Matches[0].Groups[1].Value }
            }
            $row.clips = $clipN
            $gameFrames = if ($null -ne $gf0 -and $null -ne $gf1 -and $gf1 -gt $gf0) { $gf1 - $gf0 } else { $frames }
            if ($row.seconds -gt 0 -and $gameFrames -gt 0) { $row.fps = [math]::Round($gameFrames / $row.seconds, 1) }   # (fixed step, uncapped: frames per wall second)
            if (-not $done) { $why += "no 'TAG hl done'" }
            if (-not $killed -and $null -ne $code -and $code -ne 0) { $why += "exit $code" }
            $row.detail = ("{0} plays{1} {2}" -f $clipN, $(if ($row.final_score) { ", final " + $row.final_score } else { "" }), ($why -join "; ")).Trim()
            if ($Py -and (Test-Path $Defects) -and (Test-Path (Join-Path $tele "telemetry"))) {
                $dargs = @($Defects, $tele, "--log", $log, "--out-dir", $cellDir, "--quiet")
                if ($Budget) { $dargs += @("--budget", $Budget) } else { $dargs += "--no-budget" }
                $dout = & $Py @dargs 2>&1
                $dcode = $LASTEXITCODE
                $dout | Out-File -FilePath (Join-Path $cellDir "defects.log") -Encoding utf8
                $row.defects_verdict = switch ($dcode) { 0 { if ($Budget) { "ok" } else { "-" } } 1 { "OVER" } default { "ERROR" } }
                $dj = Join-Path $cellDir "defects.json"
                if (Test-Path $dj) {
                    $D = Get-Content $dj -Raw | ConvertFrom-Json
                    $row.defects = @($D.events).Count
                    $row.defects_high = @($D.events | Where-Object { $_.sev -ge 3 }).Count
                    $row.stalls = @($D.events | Where-Object { $_.cls -eq "stall" }).Count
                    $row.top = @($D.events | Select-Object -First 3 | ForEach-Object { [pscustomobject]@{ cls = $_.cls; sev = $_.sev; score = $_.score; clip = $_.clip; frame = $_.frame;
                        game_time = $_.game_time; detail = $_.detail; players = @($_.players); replay = $_.replay } })
                    if ($row.defects_verdict -eq "OVER") { $row.detail += "; defects over budget: " + (($D.budget_rows | Where-Object { $_.verdict -eq "OVER" } | ForEach-Object { $_.cls }) -join ", ") }
                }
            } else { $row.defects_verdict = "no telemetry" }
            if ($DropTelemetry -and (Test-Path $tele)) { Remove-Item -Recurse -Force $tele -ErrorAction SilentlyContinue }
        }
        $row.result = if ($killed -match "hung" -or $tagTimeout) { "TIMEOUT" } elseif ($newCrash.Count -or $critical) { "CRASH" } elseif ($why.Count) { "FAIL" } else { "PASS" }
        if ($killed -match "predates") { $stopAll = $killed }
        [void]$Rows.Add([pscustomobject]$row)
        Write-Output ("  {0} {1:N1} min  {2}{3}" -f $row.result, ($row.seconds / 60.0), $row.detail, $(if ($null -ne $row.defects) { "  defects $($row.defects) ($($row.defects_high) high) $($row.defects_verdict)" } else { "" }))
        Write-Summary
    }
}
finally {
    if ($script:Running -and -not $script:Running.HasExited) {
        Write-Output "interrupted: stopping the game this script started (pid $($script:Running.Id))"
        Stop-Own $script:Running
    }
}

# ------------------------------------------------------------------------------------------------ crash register, summary
if ($Py -and (Test-Path $Crashes)) {
    $cl = Join-Path $OutDir "crashes.log"
    & $Py $Crashes --quiet --fail-since $Start.ToString("yyyy-MM-dd HH:mm:ss") --json (Join-Path $OutDir "crashes.json") 2>&1 | Out-File -FilePath $cl -Encoding utf8
}
Write-Summary
$bad = @($Rows | Where-Object { $_.result -ne "PASS" -or $_.defects_verdict -eq "OVER" })
Write-Output ""
Write-Output ("OVERNIGHT {0}: {1} cells, {2} not clean -> {3}" -f $(if ($bad.Count) { "FAIL" } else { "PASS" }), $Rows.Count, $bad.Count, (Join-Path $OutDir "summary.md"))
if ($bake -ne "left alone") { Write-Output "voice bake: delete $BakeStop before restarting Tools\Booth\bake_when_idle.ps1" }
if ($bad.Count) { exit 1 }
exit 0
