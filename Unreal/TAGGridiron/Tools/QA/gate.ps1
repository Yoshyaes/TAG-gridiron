# TAG Gridiron local gate (Research/NextLevel/07 A2): runs the checks one after the other, prints one PASS/FAIL table,
# writes Saved\Gate\<stamp>.json (+ each stage's log in Saved\Gate\<stamp>\) and exits 1 if any stage failed.
#
#   powershell -ExecutionPolicy Bypass -File Tools\QA\gate.ps1                  quick: sim build, SimTests, digest, crashes
#   powershell -ExecutionPolicy Bypass -File Tools\QA\gate.ps1 -Full            + editor build + every self-checking game mode
#   ... -Full -Modes pause,input            only these game modes (names from $GameModes below)
#   ... -Only simtests,crashes              only these stages (build-sim, simtests, digest, build-editor, game, crashes)
#   ... -Skip build-sim,build-editor        everything but these (e.g. binaries already built)
#   ... -DryRun                             show what each stage would run (and which would be SKIPPED); runs nothing
#   ... -Strict                             a SKIPPED stage fails the gate too
#   ... -TimeoutScale 2                     every timeout x2 (a slow day; 0.01 to test the timeout path)
#
# Results: PASS, FAIL, TIMEOUT, BLOCKED (another UnrealEditor.exe is running: a running game holds the Live Coding
# build lock, the DLLs and the GPU), SKIPPED (its tool is missing, or what it needs failed first). FAIL, TIMEOUT and
# BLOCKED fail the gate. The gate kills only processes it started itself (and, for builds, their own child processes).
param(
    [switch]$Full,
    [string]$Only = "",
    [string]$Skip = "",
    [string]$Modes = "",
    [switch]$DryRun,
    [switch]$Strict,
    [double]$TimeoutScale = 1
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

# ------------------------------------------------------------------------------------------------ configuration
$Proj      = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path          # Unreal\TAGGridiron
$Tools     = Join-Path $Proj "Tools"
$UProject  = Join-Path $Proj "TAGGridiron.uproject"
$UERoot    = "C:\Program Files\Epic Games\UE_5.8"
$Editor    = Join-Path $UERoot "Engine\Binaries\Win64\UnrealEditor.exe"
$BuildBat  = Join-Path $UERoot "Engine\Build\BatchFiles\Build.bat"
$VcVars    = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"  # (build_calibrate.bat needs it)
$GameDll   = Join-Path $Proj "Binaries\Win64\UnrealEditor-TAGGridiron.dll"

# golden sim digest (07 A3, Tools/SimDigest.cpp), built by build_calibrate.bat. With no arguments it checks this build
# against Tools/QA/golden_sim.txt (exit 0 match, 1 mismatch, 2 usage / no golden file, 3 non-deterministic).
# $DigestArgs may use {golden} (the quoted $DigestGolden) and {golden_dir} (its folder, for "--dir {golden_dir}");
# $DigestGolden is only used to report SKIPPED when the committed file is missing ("" = don't check).
$DigestExe    = Join-Path $Tools "build\SimDigest.exe"
$DigestArgs   = ""
$DigestGolden = Join-Path $Tools "QA\golden_sim.txt"

$BuildTarget  = "TAGGridironEditor Win64 Development"
$BuildExtra   = "-WaitMutex -NoHotReloadFromIDE"

# timeouts in seconds
$TBuildSim = 900; $TSimTests = 1200; $TDigest = 900; $TBuildEditor = 2700; $TCrashes = 300

# flags every engine run gets (each mode adds its own; -abslog is added per mode)
$GameCommon = "-game -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 -LiveCoding=0 -unattended -nosplash -AsyncLoadingThread -TagNoBooth -TagAudioSilent"

# The self-checking game modes (07 section 2.1 inventory + the A2 soak). Each must log "TAG RESULT <mode> pass=<n> fail=<n>"
# and exit non-zero on failure; a missing result line, pass=0, fail>0, a non-zero exit, a timeout or a crash report
# written during the run is a FAIL. On = $false leaves a mode out of -Full (it still runs when named in -Modes).
$GameModes = @(
    @{ Name = "pause";    On = $true; Timeout = 300; Flags = "-TagPauseTest -TagLoadSecs=3" },
    @{ Name = "input";    On = $true; Timeout = 300; Flags = "-TagInputTest -TagNoLoad" },
    @{ Name = "controls"; On = $true; Timeout = 300; Flags = "-TagControlsTest -TagUserKicks -TagNoLoad" },
    @{ Name = "multi";    On = $true; Timeout = 480; Flags = "-TagMultiTest" },
    @{ Name = "artpad";   On = $true; Timeout = 480; Flags = "-TagArtTest -TagPadTest" },
    @{ Name = "padplay";  On = $true; Timeout = 480; Flags = "-TagPadPlayTest" },
    @{ Name = "feel";        On = $true; Timeout = 900; Flags = "-TagFeelTest" },
    @{ Name = "hitstop-off"; On = $true; Timeout = 900; Flags = "-TagHitStopTest -TagNoHitStop -TagWeatherSeed=7 -TagHitStopPlays=16 -TagQuarterLen=120 -TagNoLoad" },
    @{ Name = "hitstop";     On = $true; Timeout = 900; Flags = "-TagHitStopTest -TagWeatherSeed=7 -TagHitStopPlays=16 -TagQuarterLen=120 -TagNoLoad" },
    @{ Name = "recent"; On = $true; Timeout = 180; Flags = "-TagRecentTest -TagNoLoad" },
    @{ Name = "replay"; On = $true; Timeout = 900; Flags = "-TagReplayTest -TagWeatherSeed=7 -TagNoLoad -TagRecentReset -TagRecentNoSave" },
    @{ Name = "shots";  On = $true; Timeout = 900; Flags = "-TagShotTest -TagWeatherSeed=7 -TagNoLoad -TagRecentReset -TagRecentNoSave" },
    @{ Name = "pace";   On = $true; Timeout = 900; Flags = "-TagPaceTest -TagWeatherSeed=7 -TagNoLoad -TagRecentReset -TagRecentNoSave" },
    @{ Name = "open";   On = $true; Timeout = 300; Flags = "-TagOpenTest -TagWeatherSeed=7 -TagNoLoad -TagRecentReset -TagRecentNoSave" },
    @{ Name = "halftime"; On = $true; Timeout = 600; Flags = "-TagHalftimeTest -TagWeatherSeed=7 -TagNoLoad -TagRecentReset -TagRecentNoSave" },
    @{ Name = "postgame"; On = $true; Timeout = 900; Flags = "-TagPostgameTest -TagWeatherSeed=7 -TagNoLoad -TagRecentReset -TagRecentNoSave" },
    @{ Name = "callcoach"; On = $true; Timeout = 400; Flags = "-TagCallCoachTest -TagNoLoad" },
    @{ Name = "gfx";        On = $true; Timeout = 420; Flags = "-TagGfxShots -TagNoLoad" },
    @{ Name = "mix";        On = $true; Timeout = 300; Flags = "-TagMixTest -TagSoak=120 -TagNoLoad -TagAudioLog" },
    @{ Name = "mix-legacy"; On = $true; Timeout = 300; Flags = "-TagMixTest -TagMixLegacy -TagSoak=120 -TagNoLoad -TagAudioLog" },
    @{ Name = "chart";    On = $true; Timeout = 900; Flags = "-TagChartTest -TagSoak=540 -TagQuarterLen=60 -TagNoLoad" },
    @{ Name = "soak";     On = $true; Timeout = 900; Flags = "-TagSoak=600 -TagQuarterLen=60 -TagNoLoad -TagWeatherSeed=7" }
)
$ResultRx = 'TAG RESULT\s+(\S+)\s+pass=(\d+)\s+fail=(\d+)'
# processes the gate never stops, even when they descend from one it started
$Protected = @("zenserver", "CrashReportClient", "CrashReportClientEditor", "UnrealTraceServer")

# ------------------------------------------------------------------------------------------------ helpers
function Split-List([string]$s) { @($s -split "[,\s]+" | Where-Object { $_ }) }   # (-File hands a list over as one string)

$Stamp   = Get-Date -Format "yyyyMMdd_HHmmss"
$GateDir = Join-Path $Proj "Saved\Gate\$Stamp"
$Start   = Get-Date
$Rows    = New-Object System.Collections.ArrayList
$script:Running = $null      # the process the gate is waiting on (stopped if the gate itself is interrupted)

function Add-Row([string]$Stage, [string]$Result, [double]$Secs, [string]$Detail, [string]$Log = "") {
    $r = [pscustomobject]@{ Stage = $Stage; Result = $Result; Secs = [math]::Round($Secs, 0); Detail = $Detail; Log = $Log }
    [void]$Rows.Add($r)
    $line = "{0,-16} {1,-8} {2,5}s  {3}" -f $Stage, $Result, $r.Secs, $Detail
    Write-Output $line
}

function Get-OtherEditors {
    @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" -ErrorAction SilentlyContinue)
}

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

function Stop-Own($Proc, [switch]$Tree) {
    # stops a process this script started (and with -Tree its own descendants), never anything else
    if (-not $Proc -or $Proc.HasExited) { return }
    if ($Tree) {
        foreach ($c in (Get-OwnDescendants $Proc.Id $Proc.StartTime)) {
            $n = [IO.Path]::GetFileNameWithoutExtension($c.Name)
            if ($Protected -contains $n) { continue }
            try { Stop-Process -Id $c.ProcessId -Force -ErrorAction Stop } catch {}
        }
    }
    try { $Proc.Kill() } catch {}
    try { [void]$Proc.WaitForExit(30000) } catch {}
}

function Invoke-Own([string]$Exe, [string]$ArgLine, [int]$Timeout, [string]$Log, [string]$WorkDir = $Proj, [switch]$Tree, [switch]$NoRedirect) {
    # runs one process with a wall-clock timeout; returns @{ Exit; TimedOut; Secs }
    $t0 = Get-Date
    $Timeout = [math]::Max(1, [int]($Timeout * $TimeoutScale))
    $sp = @{ FilePath = $Exe; PassThru = $true; WorkingDirectory = $WorkDir }
    if ($ArgLine) { $sp.ArgumentList = $ArgLine }      # (Start-Process refuses an empty argument list)
    if (-not $NoRedirect) { $sp.NoNewWindow = $true; $sp.RedirectStandardOutput = $Log; $sp.RedirectStandardError = "$Log.err" }
    $p = Start-Process @sp
    $null = $p.Handle            # (PowerShell 5.1: without the handle ExitCode reads empty after the exit)
    $script:Running = $p
    $timedOut = -not $p.WaitForExit($Timeout * 1000)
    if ($timedOut) { Stop-Own $p -Tree:$Tree } else { $p.WaitForExit() }
    $script:Running = $null
    $code = $null
    try { $code = $p.ExitCode } catch {}
    if (-not $NoRedirect -and (Test-Path "$Log.err")) {
        $err = Get-Content "$Log.err" -Raw -ErrorAction SilentlyContinue
        if ($err) { Add-Content -Path $Log -Value $err }
        Remove-Item "$Log.err" -ErrorAction SilentlyContinue
    }
    @{ Exit = $code; TimedOut = $timedOut; Secs = ((Get-Date) - $t0).TotalSeconds; Timeout = $Timeout }
}

function Get-CrashFolders {
    $d = Join-Path $Proj "Saved\Crashes"
    if (Test-Path $d) { @(Get-ChildItem $d -Directory | ForEach-Object { $_.Name }) } else { @() }
}

function Find-Python {
    foreach ($c in @("python", "py")) {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    $null
}

function First-Lines([string]$Path, [string]$Pattern, [int]$N = 3) {
    if (-not (Test-Path $Path)) { return "" }
    $m = @(Select-String -Path $Path -Pattern $Pattern -ErrorAction SilentlyContinue | Select-Object -First $N | ForEach-Object { $_.Line.Trim() })
    ($m -join " | ")
}

# ------------------------------------------------------------------------------------------------ which stages
$AllStages = @("build-sim", "simtests", "digest", "build-editor", "game", "crashes")
$Quick     = @("build-sim", "simtests", "digest", "crashes")
$Wanted = if ($Full) { $AllStages } else { $Quick }
$OnlyL = @(Split-List $Only); $SkipL = @(Split-List $Skip); $ModeL = @(Split-List $Modes)
foreach ($s in ($OnlyL + $SkipL)) { if ($AllStages -notcontains $s) { Write-Output "unknown stage '$s' (stages: $($AllStages -join ', '))"; exit 2 } }
foreach ($m in $ModeL) { if (-not ($GameModes | Where-Object { $_.Name -eq $m })) { Write-Output "unknown mode '$m' (modes: $(($GameModes | ForEach-Object { $_.Name }) -join ', '))"; exit 2 } }
if ($OnlyL.Count) { $Wanted = @($AllStages | Where-Object { $OnlyL -contains $_ }) }
if ($ModeL.Count -and $Wanted -notcontains "game") { $Wanted = @($AllStages | Where-Object { $Wanted -contains $_ -or $_ -eq "game" }) }
$Wanted = @($Wanted | Where-Object { $SkipL -notcontains $_ })
$RunModes = @($GameModes | Where-Object { if ($ModeL.Count) { $ModeL -contains $_.Name } else { $_.On } })

if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $GateDir | Out-Null }
Write-Output ("TAG gate {0} {1}  stages: {2}{3}" -f $(if ($Full) { "full" } else { "quick" }), $Stamp, ($Wanted -join ", "),
    $(if ($Wanted -contains "game") { "  modes: " + (($RunModes | ForEach-Object { $_.Name }) -join ", ") } else { "" }))
if (-not $DryRun) { Write-Output "logs: $GateDir" }
Write-Output ""

$Failed = @{}     # stage -> $true when it did not pass (later stages that need it are SKIPPED)
$GameRan = $false

try {
    # ---------------------------------------------------------------------------------------- build-sim
    if ($Wanted -contains "build-sim") {
        $bat = Join-Path $Tools "build_calibrate.bat"
        if (-not (Test-Path $bat)) { Add-Row "build-sim" "SKIPPED" 0 "no $bat" }
        elseif (-not (Test-Path $VcVars)) { Add-Row "build-sim" "SKIPPED" 0 "MSVC Build Tools not found ($VcVars)"; $Failed["build-sim"] = $true }
        elseif ($DryRun) { Add-Row "build-sim" "DRYRUN" 0 "cmd /c `"$bat`" (timeout $TBuildSim s)" }
        else {
            $log = Join-Path $GateDir "build-sim.log"
            $r = Invoke-Own "cmd.exe" "/d /c `"`"$bat`"`"" $TBuildSim $log $Tools -Tree
            if ($r.TimedOut) { Add-Row "build-sim" "TIMEOUT" $r.Secs "over $($r.Timeout) s" $log; $Failed["build-sim"] = $true }
            elseif ($r.Exit -ne 0) { Add-Row "build-sim" "FAIL" $r.Secs ("exit $($r.Exit): " + (First-Lines $log "error|not found")) $log; $Failed["build-sim"] = $true }
            else { Add-Row "build-sim" "PASS" $r.Secs "build_calibrate.bat" $log }
        }
    }

    # ---------------------------------------------------------------------------------------- simtests
    if ($Wanted -contains "simtests") {
        $exe = Join-Path $Tools "build\SimTests.exe"
        if ($Failed.ContainsKey("build-sim")) { Add-Row "simtests" "SKIPPED" 0 "sim build did not pass (the exe would be stale)" }
        elseif (-not (Test-Path $exe)) { Add-Row "simtests" "SKIPPED" 0 "no $exe (run build_calibrate.bat)" }
        elseif ($DryRun) { Add-Row "simtests" "DRYRUN" 0 "`"$exe`" (timeout $TSimTests s)" }
        else {
            $log = Join-Path $GateDir "simtests.log"
            $r = Invoke-Own $exe "" $TSimTests $log $Tools
            $sum = First-Lines $log "\d+ passed, \d+ failed" 1
            if ($r.TimedOut) { Add-Row "simtests" "TIMEOUT" $r.Secs "over $($r.Timeout) s" $log; $Failed["simtests"] = $true }
            elseif ($r.Exit -ne 0) { Add-Row "simtests" "FAIL" $r.Secs ("exit $($r.Exit) $sum " + (First-Lines $log "^\s*FAIL" 3)) $log; $Failed["simtests"] = $true }
            else { Add-Row "simtests" "PASS" $r.Secs $sum $log }
        }
    }

    # ---------------------------------------------------------------------------------------- digest
    if ($Wanted -contains "digest") {
        $dargs = $DigestArgs
        if ($DigestGolden) { $dargs = $dargs.Replace("{golden}", "`"$DigestGolden`"").Replace("{golden_dir}", "`"" + (Split-Path $DigestGolden) + "`"") }
        $dargs = $dargs.Replace("{golden}", "").Replace("{golden_dir}", "").Trim()
        if ($Failed.ContainsKey("build-sim")) { Add-Row "digest" "SKIPPED" 0 "sim build did not pass (the exe would be stale)" }
        elseif (-not (Test-Path $DigestExe)) { Add-Row "digest" "SKIPPED" 0 "no $DigestExe yet (07 A3; built by build_calibrate.bat)" }
        elseif ($DigestGolden -and -not (Test-Path $DigestGolden)) { Add-Row "digest" "SKIPPED" 0 "no golden file $DigestGolden (bless one first)" }
        elseif ($DryRun) { Add-Row "digest" "DRYRUN" 0 "`"$DigestExe`" $dargs (timeout $TDigest s)" }
        else {
            $log = Join-Path $GateDir "digest.log"
            $r = Invoke-Own $DigestExe $dargs $TDigest $log $Tools
            if ($r.TimedOut) { Add-Row "digest" "TIMEOUT" $r.Secs "over $($r.Timeout) s" $log; $Failed["digest"] = $true }
            elseif ($r.Exit -ne 0) {
                $what = switch ($r.Exit) { 1 { "mismatch" } 2 { "usage / no golden file" } 3 { "non-deterministic" } default { "exit $($r.Exit)" } }
                Add-Row "digest" "FAIL" $r.Secs ("$what (exit $($r.Exit)): " + (First-Lines $log "FAIL|NON-DETERMINISTIC|game \d+|golden|now|first" 4)) $log; $Failed["digest"] = $true
            }
            else { Add-Row "digest" "PASS" $r.Secs (First-Lines $log "SimDigest: PASS|PASS" 1) $log }
        }
    }

    # ---------------------------------------------------------------------------------------- build-editor
    if ($Wanted -contains "build-editor") {
        $others = @(Get-OtherEditors)
        if (-not (Test-Path $BuildBat)) { Add-Row "build-editor" "SKIPPED" 0 "no $BuildBat"; $Failed["build-editor"] = $true }
        elseif ($others.Count -and -not $DryRun) {
            Add-Row "build-editor" "BLOCKED" 0 ("UnrealEditor.exe running (pid " + (($others | ForEach-Object { $_.ProcessId }) -join ", ") + "): close it first")
            $Failed["build-editor"] = $true
        }
        else {
            $line = "/d /c `"`"$BuildBat`" $BuildTarget -Project=`"$UProject`" $BuildExtra`""
            if ($DryRun) { Add-Row "build-editor" "DRYRUN" 0 ("cmd $line (timeout $TBuildEditor s)" + $(if ($others.Count) { "  [would be BLOCKED now: UnrealEditor.exe running]" } else { "" })) }
            else {
                $log = Join-Path $GateDir "build-editor.log"
                $r = Invoke-Own "cmd.exe" $line $TBuildEditor $log $Proj -Tree
                if ($r.TimedOut) { Add-Row "build-editor" "TIMEOUT" $r.Secs "over $($r.Timeout) s" $log; $Failed["build-editor"] = $true }
                elseif ($r.Exit -ne 0) { Add-Row "build-editor" "FAIL" $r.Secs ("exit $($r.Exit): " + (First-Lines $log "error |Live Coding|Result:" 3)) $log; $Failed["build-editor"] = $true }
                else { Add-Row "build-editor" "PASS" $r.Secs (First-Lines $log "Result:|Total execution time" 1) $log }
            }
        }
    }

    # ---------------------------------------------------------------------------------------- game modes
    if ($Wanted -contains "game") {
        foreach ($m in $RunModes) {
            $stage = "game:" + $m.Name
            $log = Join-Path $GateDir ($m.Name + ".log")
            $argLine = "`"$UProject`" $GameCommon $($m.Flags) -abslog=`"$log`""
            if ($Failed.ContainsKey("build-editor")) { Add-Row $stage "SKIPPED" 0 "editor build did not pass"; continue }
            if (-not (Test-Path $Editor)) { Add-Row $stage "SKIPPED" 0 "no $Editor"; continue }
            if (-not (Test-Path $GameDll)) { Add-Row $stage "SKIPPED" 0 "no $GameDll (build the editor target)"; continue }
            $others = @(Get-OtherEditors)
            if ($DryRun) {
                Add-Row $stage "DRYRUN" 0 ("UnrealEditor.exe $argLine (timeout $($m.Timeout) s)" + $(if ($others.Count) { "  [would be BLOCKED now]" } else { "" }))
                continue
            }
            if ($others.Count) {
                Add-Row $stage "BLOCKED" 0 ("UnrealEditor.exe running (pid " + (($others | ForEach-Object { $_.ProcessId }) -join ", ") + "): close it first")
                continue
            }
            if (Get-Process -Name UnrealBuildTool -ErrorAction SilentlyContinue) {
                Write-Output "  (note: an UnrealBuildTool build is running; the game waits for it at TurnkeySupport and may time out)"
            }
            $before = @(Get-CrashFolders)
            $GameRan = $true
            $r = Invoke-Own $Editor $argLine $m.Timeout $log $Proj -NoRedirect
            $newCrash = @(Get-CrashFolders | Where-Object { $before -notcontains $_ })
            $res = @()
            if (Test-Path $log) { $res = @(Select-String -Path $log -Pattern $ResultRx | ForEach-Object { $_.Matches[0] }) }
            $pass = 0; $fail = 0
            foreach ($x in $res) { $pass += [int]$x.Groups[2].Value; $fail += [int]$x.Groups[3].Value }
            $tagTimeout = (Test-Path $log) -and (Select-String -Path $log -Pattern "TAG timeout" -Quiet)
            $why = @()
            if ($r.TimedOut) { $why += "gate timeout $($r.Timeout) s (stopped)" }
            if ($tagTimeout) { $why += "game logged TAG timeout" }
            if ($newCrash.Count) { $why += "crash report " + ($newCrash -join ", ") }
            if (-not $r.TimedOut -and $r.Exit -ne 0) { $why += "exit $($r.Exit)" }
            if (-not (Test-Path $log)) { $why += "no log written" }
            elseif (-not $res.Count) { $why += "no 'TAG RESULT' line" }
            elseif ($fail -gt 0) { $why += "fail=$fail " + (First-Lines $log "FAIL" 2) }
            elseif ($pass -eq 0) { $why += "pass=0 (nothing was checked)" }
            $detail = if ($res.Count) { "pass=$pass fail=$fail" } else { "" }
            if ($why.Count) {
                $verdict = if ($r.TimedOut -or $tagTimeout) { "TIMEOUT" } else { "FAIL" }
                Add-Row $stage $verdict $r.Secs (($detail, ($why -join "; ")) -join " ").Trim() $log
            } else { Add-Row $stage "PASS" $r.Secs "$detail exit 0" $log }
        }
    }

    # ---------------------------------------------------------------------------------------- crashes
    if ($Wanted -contains "crashes") {
        $py = Find-Python
        $script = Join-Path $PSScriptRoot "crashes.py"
        $cargs = "`"$script`" --quiet"
        if ($GameRan) { $cargs += " --fail-since `"" + $Start.ToString("yyyy-MM-dd HH:mm:ss") + "`"" }
        if (-not $py) { Add-Row "crashes" "SKIPPED" 0 "no python on PATH" }
        elseif (-not (Test-Path $script)) { Add-Row "crashes" "SKIPPED" 0 "no $script" }
        elseif ($DryRun) { Add-Row "crashes" "DRYRUN" 0 "$py $cargs (timeout $TCrashes s)" }
        else {
            $log = Join-Path $GateDir "crashes.log"
            $r = Invoke-Own $py $cargs $TCrashes $log $Proj
            $first = First-Lines $log "CRASH CHECK" 2
            if ($r.TimedOut) { Add-Row "crashes" "TIMEOUT" $r.Secs "over $($r.Timeout) s" $log }
            elseif ($r.Exit -eq 0) { Add-Row "crashes" "PASS" $r.Secs $first $log }
            else { Add-Row "crashes" "FAIL" $r.Secs ("exit $($r.Exit): $first") $log }
        }
    }
}
finally {
    if ($script:Running -and -not $script:Running.HasExited) {
        Write-Output "gate interrupted: stopping the process it started (pid $($script:Running.Id))"
        Stop-Own $script:Running -Tree
    }
}

# ------------------------------------------------------------------------------------------------ verdict
$bad = @($Rows | Where-Object { $_.Result -in @("FAIL", "TIMEOUT", "BLOCKED") -or ($Strict -and $_.Result -eq "SKIPPED") })
Write-Output ""
Write-Output ("{0,-16} {1,-8} {2,6}  {3}" -f "STAGE", "RESULT", "TIME", "DETAIL")
foreach ($r in $Rows) {
    $d = $r.Detail; if ($d.Length -gt 150) { $d = $d.Substring(0, 150) + "..." }
    Write-Output ("{0,-16} {1,-8} {2,5}s  {3}" -f $r.Stage, $r.Result, $r.Secs, $d)
}
$verdict = if ($DryRun) { "DRYRUN" } elseif ($bad.Count) { "FAIL" } else { "PASS" }
Write-Output ""
Write-Output ("GATE {0}: {1} stages, {2} failed, {3} skipped, {4:N0} s" -f $verdict, $Rows.Count, $bad.Count,
    @($Rows | Where-Object { $_.Result -eq "SKIPPED" }).Count, ((Get-Date) - $Start).TotalSeconds)
if (-not $DryRun) {
    $json = [pscustomobject]@{ stamp = $Stamp; tier = $(if ($Full) { "full" } else { "quick" }); verdict = $verdict;
        started = $Start.ToString("s"); seconds = [math]::Round(((Get-Date) - $Start).TotalSeconds, 0); stages = @($Rows) }
    $json | ConvertTo-Json -Depth 4 | Out-File -FilePath (Join-Path $Proj "Saved\Gate\$Stamp.json") -Encoding utf8
}
if ($bad.Count -and -not $DryRun) { exit 1 }
exit 0
