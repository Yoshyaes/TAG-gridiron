# 07 - Polish pipeline, QA automation and performance

Researcher 7 of 7. Written 2026-11-23 against branch `look-and-feel` (HEAD cd18e0e plus about 11.7k uncommitted lines).
Web search and page fetch were available. Two pages refused automated fetches (Epic's shader-stutter tech blog and the
GameSpot Madden 16 patch article), so claims taken from those come from search-result summaries and are marked that way.
Claims about the engine marked "engine source" were read in the local UE 5.8 install
(`C:\Program Files\Epic Games\UE_5.8\Engine\Source`). Claims about the project cite the file I read.

How confident I am in each claim: **High** means a primary source or code I read says it. **Medium** means a secondary
source, or a primary source that only partly covers the claim. **Inferred** means my own reasoning from the evidence.

---

## 1. What the best studios do

### 1.1 Automated tests are part of the build, and a failing build stops commits (Rare, Sea of Thieves)
- What Rare had: C++ unit tests, integration tests built on test maps (including tests for AI behaviour trees),
  multiplayer integration tests that put a client on the network, and performance, latency and platform tests. TeamCity
  ran them on a build farm. Small tests ran "roughly every twenty minutes" and big multiplayer and perf tests overnight.
  A red build meant "no further commits are permitted" until the change was fixed or rolled back. QA still did the
  polish work and found what the tests missed.
  [Game Developer, part 4 of 4](https://gamedeveloper.com/design/how-rare-automates-testing-for-ai-and-more-in-sea-of-thieves-part-4-of-4-);
  [GDC 2019 talk listing](https://gdcvault.com/play/1026366/Automated-Testing-of-Gameplay-Features);
  [Unreal Fest Europe 2019](https://www.unrealengine.com/events/unreal-fest-europe-2019/automated-testing-at-scale-in-sea-of-thieves).
  Confidence: **Medium**. The details come from a secondary write-up, and I could not get the talk's own numbers.
- What applies to TAG (**Inferred**): the part that matters is the gate, a short suite that has to pass before code
  lands. The size of Rare's suite does not carry over to one developer.

### 1.2 A deterministic sim makes replay regression tests possible (Riot, Factorio)
- Riot made the League of Legends server deterministic so it could replay a recorded game. The work was driven by "a
  desire to create fast, repeatable tests driven from a set of recordable inputs". Riot found that "only the first
  divergence really matters", because state runs into chaos after it. The work took about a year of engineering.
  [Riot: Introduction](https://technology.riotgames.com/node/67),
  [Fixing Divergences](https://technology.riotgames.com/node/84). Confidence: **High**.
- Factorio uses integration tests to catch determinism regressions. Its "heavy" mode saves and reloads the game every
  tick and compares a CRC of the map, and tests run on each commit.
  [FFF #60](https://factorio.com/blog/post/fff-60), [FFF #315](https://cf-www.factorio.com/blog/post/fff-315).
  Confidence: **High**.
- What applies to TAG (**Inferred**): TAG already has the expensive part, a deterministic C++ sim (`SimTests`
  "determinism: same seed => identical game"). Golden digests and first-divergence reports cost very little on top of it.

### 1.3 Scripted bots come first, and learning agents are added on top (EA SEED)
- SEED added an experimental RL system to an existing scripted-bot test pipeline to widen coverage in Battlefield 2042
  and Dead Space (2023). SEED also cites Battlefield V: 601 features would take about 0.5M hours to test by hand.
  [CoG 2023 paper](https://arxiv.org/abs/2307.11105),
  [SEED overview](https://www.ea.com/seed/news/seed-ml-research-aaa-game-testing),
  [DRL testing](https://www.ea.com/seed/news/automated-game-testing-deep-reinforcement-learning),
  [curiosity agents](https://arxiv.org/pdf/2103.13798.pdf). Confidence: **High** on what the work was. The pages I could
  read do not list the deployment costs in detail.
- What applies to TAG (**Inferred**): in a football sim, CPU-vs-CPU games are the scripted bot, and they already cover
  most of what an exploration agent finds in an open map. RL agents make sense later, if at all, for hunting user
  exploits ("money plays").

### 1.4 Machine-learning visual detectors work best as a second layer
- SEED's CNN for texture glitches (stretched, low-res, missing, placeholder) reports 86.8 % accuracy, 88 % of glitches
  caught and an 8.7 % false-positive rate. It was trained on synthetic glitches.
  [AIIDE 2020 / arXiv 2406.08231](https://arxiv.org/abs/2406.08231). Confidence: **Medium**. The numbers come from the
  abstract snippet.
- Vision-language models: on GlitchBench the best model (GPT-4V) scored 43.4 %
  ([arXiv 2312.05291](https://arxiv.org/abs/2312.05291)). On VideoGameQA-Bench the best image glitch detection was
  82.8 % (GPT-4o) and the best video glitch detection 78.1 % (Gemini 2.5 Pro). The models are weak on "body configuration"
  (distorted poses), "intricate object clipping" (props through hands, limbs through geometry) and common-sense glitches,
  and their most common false positive is clipping that is not there.
  [arXiv 2505.15952](https://arxiv.org/html/2505.15952v2). Confidence: **High**.
- What applies to TAG (**Inferred**): the defects TAG cares most about are body pose, limb interpenetration and the ball
  through a hand, which is exactly where VLMs are weakest. Numeric detectors on telemetry must be the primary gate. A VLM
  can rank and describe frames those detectors flag, and it can review presentation (camera, HUD, UI), where it does
  better.

### 1.5 Numeric motion-quality metrics (motion-synthesis research)
- Foot skate is commonly measured as the share of contact frames where the foot's horizontal speed passes a threshold.
  NVIDIA's Kimodo benchmark counts a toe below 5 cm as in contact and over 0.2 m/s as skating, and warns that the number
  is unreliable when contact detection is poor. Penetration is reported as both a frequency and a depth, and one paper
  takes contact labels "from the source reference rather than the evaluated motion".
  [Kimodo metrics](https://research.nvidia.com/labs/sil/projects/kimodo/docs/benchmark/metrics.html),
  [motion generation survey](https://arxiv.org/pdf/2307.10894). Confidence: **High**.
- What applies to TAG (**Inferred**): TAG already does the better variant. `plant_truth.py` and the `plf` telemetry
  column take "planted" from the capture, and the memory notes record that the height-only test gave wrong numbers. Keep
  that design, and apply the same idea (ground truth from the capture) to overlap and pops.

### 1.6 Epic's test and perf tooling
- The Automation Test Framework covers functional tests and screenshot comparison against ground-truth images
  ([Screenshot Comparison](https://dev.epicgames.com/documentation/en-us/unreal-engine/screenshot-comparison-tool-in-unreal-engine),
  [Automation framework](https://dev.epicgames.com/documentation/unreal-engine/automation-test-framework-in-unreal-engine)).
  Gauntlet runs sessions from RunUAT and "does not require any specific game-side automation code"
  ([Gauntlet](https://dev.epicgames.com/documentation/en-us/unreal-engine/gauntlet-automation-framework-overview-in-unreal-engine)).
  The Automated Performance Testing plugin ties together CSV Profiler, Insights and Gauntlet. Epic staff call it
  experimental and say there is no easy upgrade path from 5.5
  ([forum](https://forums.unrealengine.com/t/automated-performance-testing-plugin-guidance/2666136)). Confidence:
  **High** (docs) / **Medium** (forum). Both plugins ship in the local 5.8 install (`Engine/Plugins/Performance/AutomatedPerfTesting`,
  `Engine/Plugins/Runtime/Gauntlet`; engine source).

### 1.7 Frame pacing in UE5
- **PSO precaching** is the main defence against shader-pipeline hitches and is on by default. Misses log
  "PSO PRECACHING MISS". `stat PSOPrecache` and `r.PSOPrecache.Validation=1/2` track misses. Runtime creations slower than
  `r.PSO.RuntimeCreationHitchThreshold` (20 ms) are counted. Every smoothness test should run with `-clearPSODriverCache`,
  and the loading screen should stay up until `FShaderPipelineCache::NumPrecompilesRemaining()` reaches 0.
  [PSO precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine). **High**.
  Epic's blog says precaching did not cover global shaders as of 5.5
  ([tech blog](https://www.unrealengine.com/en-US/tech-blog/game-engines-and-shader-stuttering-unreal-engines-solution-to-the-problem),
  search summary only). **Medium**.
- **Engine source fact that matters here:** `PipelineStateCache::IsPSOPrecachingEnabled()` returns `false` under
  `WITH_EDITOR` ("Disables in the editor for now by default untill more testing is done",
  `Runtime/RHI/Private/PipelineStateCache.cpp:4951-4958`), and the component precache paths also check `!GIsEditor`
  (`Runtime/Engine/Private/PSOPrecache.cpp:136-166`). **High**. An editor binary running `-game` therefore never precaches.
- **Hitch definition**: `t.HitchFrameTimeThreshold` defaults to 60 ms, and a frame must also be 1.5x slower than the
  previous one (`Runtime/Engine/Private/Performance/EnginePerformanceTargets.cpp:28-48`; engine source). **High**. At a
  120 fps target, 60 ms is far too loose to use as TAG's own bar.
- **Incremental GC** (`gc.AllowIncrementalReachability`) spreads reachability analysis over frames. It requires
  `TObjectPtr` on every UPROPERTY, Epic recommends it for single-threaded builds, and forums report streaming freezes and
  crashes. [Incremental GC](https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine).
  **High** (docs) / **Medium** (forum reports).
- **Animation Budget Allocator**: it holds game-thread animation to `a.Budget.BudgetMs` by throttling and interpolating
  `USkeletalMeshComponentBudgeted` components, and turns URO off on the components it registers.
  [Docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine). **High**.
- **Camera cuts**: `APlayerCameraManager::SetGameCameraCutThisFrame()` is meant "for game code to call, e.g., when
  performing a teleport that should be seamless" (`Engine/Classes/Camera/PlayerCameraManager.h:881-883`; engine source).
  **High**. A November 2026 forum report describes TSR showing a ghost of an old frame on camera-cut frames in 5.8
  ([forum](https://forums.unrealengine.com/t/tsr-output-srv-exposes-unwritten-mip-1-on-camera-cut-frames-tonemap-samples-stale-data-5-8-main/2854936)).
  **Medium** (one user report). Another thread says texture streaming takes the view's location into account but not its
  direction, so a cut to a new angle can pop
  ([forum](https://forums.unrealengine.com/t/texture-pop-between-camera-cuts-using-sequencer/2663207)). **Low/Medium**.

### 1.8 Sports titles tune from data and keep shipping rules and clock fixes
- EA FC 27's dev notes tuned from "a month's worth of in-game data". Their examples: win rates by formation were evenly
  spread, and successful tackles rose as players learned the defensive controls.
  [FC 27 October notes](https://www.ea.com/en/games/ea-sports-fc/fc-27/news/fc27-october-gameplay-developer-notes). **High**.
- Madden keeps shipping fixes for the rules and clock long tail:
  - Madden 25 (October 2024): no clock runoff in ranked head-to-head with Accelerated Clock on
    ([TU notes](https://careers.ea.com/games/madden-nfl/madden-nfl-25/news/title-update-october-16-2024)).
  - Madden 26: CPU timeouts called right before punting while losing big
    ([TU notes](https://www.ea.com/games/madden-nfl/madden-nfl-26/news/title-update-september-17-2025)).
  - Madden 16, as reported: a declined penalty after an incompletion restarted the clock; Play Now used regular-season
    overtime rules; a timeout on a safety punt led to a regular kickoff
    ([GameSpot](https://www.gamespot.com/articles/madden-nfl-16-update-released-fixes-100-bugs-and-m/1100-6431039/),
    search summary only; the page refused the fetch).
  Confidence: **High** for Madden 25/26, **Medium** for Madden 16.
- What applies to TAG (**Inferred**): even a studio with dedicated QA ships these. A deterministic headless sim can
  enumerate them far more cheaply than playtesting can.

### 1.9 Crash reporting and triage
- Sentry's Unreal SDK uses Crashpad on Windows, captures crashes natively from UE 5.2, and cannot run alongside UE's
  own Crash Reporter on Windows. [Sentry Unreal docs](https://docs.sentry.io/platforms/unreal). **High**.
- Triage practice: severity and priority are separate scales; P0 "ship blockers" are crashes, data loss and progression
  blockers; a weekly 30-minute triage; and a zero bug bounce, where each incoming bug is held against the milestone's
  release criteria.
  [Microsoft ZBB](https://learn.microsoft.com/en-my/archive/blogs/victor.pearson/zbb-zero-bug-bounce),
  [weekly triage, small team](https://bugnet.io/blog/weekly-bug-triage-small-team),
  [severity vs priority](https://bugnet.io/blog/how-to-organize-bug-reports-by-severity-and-priority). **Medium**. These
  are practice guides, not studio postmortems.

### 1.10 A protected polish phase
- Finishing in stages: feature/code lock, then content lock, then polish, then stabilisation. Demiurge's Brothers in
  Arms Wii postmortem argued that a strict feature lockdown is essential.
  [Demiurge](https://gamedeveloper.com/game-platforms/demiurge-feature-lockdown-vital-to-completing-i-brothers-in-arms-i-wii),
  [Art of game polish](https://gamedeveloper.com/design/the-art-of-game-polish-developers-speak). **Medium/Low**. I found
  no first-party AAA source with numbers.

### 1.11 Parallel AI coding sessions
- Claude Code documents running parallel sessions in separate git worktrees (`--worktree`) so their file edits do not
  collide. [Claude Code worktrees](https://code.claude.com/docs/en/worktrees). **High**.

---

## 2. Where TAG Gridiron stands

### 2.1 Inventory (186 distinct `-Tag` flags found by grepping `TEXT("Tag...")` in `Source/`)

| Kind | Entry points | Files |
|---|---|---|
| Headless sim tests | `SimTests.exe`, 49 named tests: determinism, 100 full games, every play vs every defence, presnap, playbooks, place kicks, abilities, depth chart, run exchange, kneel, TD to PAT, two-minute warning | `Tools/SimTests.cpp`, `Tools/build_calibrate.bat` |
| Headless calibration | `SimCalibrate` (200 games, NFL bands, `--json --set`), `ReturnCal`, `ConceptCal`, `MoveCal`, `AbilityCal`, `PlaybookDump`, `fit.py`, `nfl_reference.py`, `score.py` + `rubric.json` (63 metrics) | `Tools/`, `Tools/Realism/` |
| In-engine tests that print pass/fail | `-TagPauseTest`, `-TagMultiTest`, `-TagPadPlayTest`, `-TagArtTest -TagPadTest`, `-TagControlsTest`, `-TagChartTest`, `-TagInputTest` | `Game/MatchDirector.cpp:4306`, `MatchMulti.cpp:593`, `MatchPadPlay.cpp:34`, `MatchPresnap.cpp:1211`, `UI/ControlsScreen.cpp:1299`, `UI/CreatePlayer.cpp:283`, `UI/InputTest.cpp:189` |
| In-engine measurement | `-TagAutoWatch`, `-TagSoak=`, `-TagBench=`, `-TagMoveTest`, `-TagGapTest`, `-TagLineTest`, `-TagTackleTest/Cam/Check`, `-TagCatchCheck`, `-TagHandoffCheck`, `-TagMarkerCheck`, `-TagStallDbg`, `-TagMMWhy`, `-TagPairLog`, float check | `Game/MatchDirector.cpp:233-341, 599, 819-838, 2969-3006, 5677, 6664`; `TAGGridiron.cpp:22-197` |
| Full-game film + telemetry | `-TagHighlights -TagHlFull` with `-TagHlDir/-TagHlMaxClips/-TagHlFromQ/-TagHuddleSecs/-TagHlBroadcast/-TagHlKeepTurnovers` | `MatchDirector.cpp:2822-3054` |
| Scenario control | `-TagForcePlay=`, `-TagForceDef=`, `-TagTune=`, `-TagHome/Away=`, `-TagWeather(Seed)=`, `-TagQuarterLen=`, `-TagFakePad=`, `-TagDay` | `MatchDirector.cpp:149, 3578-3590` |
| A/B kill switches | about 40 `-TagNo*` flags (`-TagNoFootLock`, `-TagNoKeepOut`, `-TagNoLean`, `-TagHeightLock` ...) | various |
| Screenshot modes | `-TagUiShots`, `-TagTeamSelect`, `-TagRosterShots`, `-TagAbilityShots`, `-TagZoneShots`, `-TagCallShot`, `-TagMoveShot`, `-TagLineup`, `-TagHeroGallery`, `-TagShotAt=` | |
| Analysis scripts | `analyze.py` (actor teleports, body pops, pelvis pops, ball in hand, body overlap, framing, foot truth), `capture_fidelity.py`, `plant_truth.py`, `lean_check.py`, `Anim/tackle_check.py`, `tackle_truth_*.py`, `Art/compare_shots.py`, `build_highlights.py` (lens-blocked frame filter) | `Tools/Realism`, `Tools/Anim`, `Tools/Art` |
| Gate and perf scripts | `Tools/Art/gates.ps1` (autowatch, gaptest, movetest, pausetest), `Tools/Perf/bench.ps1`, `ab.ps1`, `trace.ps1` | |

This is a strong base for one developer. The measurement side, especially capture-grounded foot and tackle truth, goes
beyond what the public sources describe for most studios. What is missing is the wiring around it.

### 2.2 Gaps, with evidence

1. **No verdict, so nothing can gate.** The 29 `FPlatformMisc::RequestExit(...)` calls in `Source/` all exit normally,
   and `RequestExitWithStatus` is never used (grep). `gates.ps1` counts "Error:" lines and prints summary lines; it does
   not read the pass/fail lines or set its own exit code. The seven self-checking modes are not run together anywhere.
   There are no git hooks (`.git/hooks` has only samples).
2. **The game crashes in normal test runs, and nobody tracks it.** `Unreal/TAGGridiron/Saved/Crashes` holds 78 crash
   folders from 11-06 to 11-23.
   - Of the newest 40, 11 are `-game` processes and 9 of those are since 11-18: two access violations today in
     `-TagHlFull` runs, one from a launch with no flags on 11-22 (possibly a playtest), an access violation in
     `ATagAudio::Cue()` (`TagAudio.cpp:896` at the time) during `-TagPlayNow -TagBench` on 11-21, two on 11-19 (`-TagUiShots`,
     `-TagHlFull`), and on 11-18 an array index assert in `-TagMultiTest` plus two out-of-memory fatals in `-TagSoak`
     (`GenericPlatformMemory.cpp:300` is the OOM handler; engine source).
   - Editor crashes come mostly from the PoseSearch compressed-data asserts and MTAccessDetector ensures.
   - Most stacks have no symbols (only `ntdll` frames), which matches the memory note that engine PDBs are not installed.
3. **The headless sim checks game totals, not each play.** "100 full games" checks that games finish, totals are sane
   and no team scores exactly 1 (`SimTests.cpp:86-116`). Of the rules long tail, only kneel, TD to PAT and the two-minute
   warning have tests. Safety, onside, overtime (`Game.cpp:306-315, 356-360`), the untimed down (`Game.cpp:255, 800`),
   timeouts (`Game.cpp:266-278`) and penalty enforcement have none. The determinism test compares
   `score + log.size()` of one game within one build (`SimTests.cpp:72-84`), so there is no golden reference across
   builds. When one session's render or UI change alters a sim outcome, nothing notices.
4. **Defect telemetry exists only inside the full-game film.** `HlTelemetry` runs only for filmed frames
   (`MatchDirector.cpp:2846`), and filming writes a PNG every frame (`:2907`). The memory notes put a full game at about
   70 GB and 60 minutes, and 40 clips at about 12 minutes. So detectors run on one or two games, not hundreds. `analyze.py`
   produces aggregates scored against bands, not a list of individual defects with clip and frame to look at.
5. **Perf is measured on the editor binary, and hitches are not counted.**
   - The launcher runs `UnrealEditor.exe ... -game` (`Play TAG Gridiron (Unreal).bat`), so PSO precaching is compiled
     out (see 1.7).
   - The bench logs average, median and 1 % low per 2 s window, but no maximum frame, no hitch count and no game state
     at the hitch (`TAGGridiron.cpp:186-197`).
   - The 25-30 s warm-up (`bench.ps1`) leaves out the loading-to-first-snap stretch the user sits through.
   - The memory notes have 1 % lows of about 61-64 fps against an average of about 123 in the user game, and a bench
     drop from 99 to 93 with the "cause not found". Nothing keeps a history to bisect against.
6. **Camera cuts are not signalled to the renderer.** `UpdateCamera` resets its own springs on a cut
   (`MatchDirector.cpp:7160-7168`), but `Source/` has no call to `SetGameCameraCutThisFrame`. Meanwhile TSR (AA method 4)
   and motion blur are on (`Config/DefaultEngine.ini:9,14`). **Inferred:** the first frame after a replay, cut-in or
   station change reuses temporal history and motion vectors from the old shot, which risks a smeared or ghosted frame.
   Telemetry also has no camera position, so "camera inside a body" can only be found by eye. The memory notes record
   that the lens-blocked filter in `build_highlights.py` found such frames.
7. **Coverage is narrow.** Nearly every mode calls `StartMatch(3, 7, ...)` (OKC vs MEM; `MatchDirector.cpp:239-340`).
   Soak and highlights accept `-TagHome/-TagAway`, but no script sweeps the 32 teams, four weather types and day/night.
8. **Parallel sessions share one working tree.** `git worktree list` shows one tree. 92 paths are changed, +11,713 / -1,822
   lines across 68 files since the 11-18 commit. `parallel-sessions.md` describes the manual hunk-splitting and side
   builds needed to commit safely. A side build of the staged state is the only real check, and it is manual.
9. **Smaller items:**
   - `HandoffCal.cpp` exists but `build_calibrate.bat` does not build it. The memory warns "never trust a Tools/build
     exe without its source".
   - Save data is ini via `GConfig->Flush` (`TagBindings.cpp:44`, `CreatePlayer.cpp:303-331`). That is fine at today's
     scope.

---

## 3. Actions (ranked by defects prevented per unit of effort)

Each action lists what to build, the visible failure it catches, where it goes, effort (S under a day, M days, L weeks),
dependencies, and how to verify it. For a detector, verifying means a known-bad case it must flag.

### A1. Crash register plus symbols (S)
- **Build:** install the engine debug symbols (Epic Games Launcher > UE 5.8 > Options > "Editor symbols for debugging";
  check D: space first). Add `Tools/Gate/crashes.py`, which:
  - parses every `Saved/Crashes/*/CrashContext.runtime-xml`;
  - records date, game or editor (from `<CommandLine>`), the `-Tag` flags, the error message, the first `TAGGridiron`
    frame and the log tail;
  - dedupes by signature and writes a register of new and known crashes;
  - exits non-zero on any new game-process crash since a given time.
- **Catches:** crashes and hangs, the most damaging failure a player can see. Nine game crashes in the last five days
  went untriaged.
- **Where:** `Tools/Gate/` (new); reads `Saved/Crashes`.
- **Dependencies:** none.
- **Verify:** the register must list the 11-21 `ATagAudio::Cue()` crash with file and line, put the two 11-23
  `-TagHlFull` access violations in one group, and mark the 11-18 soak fatals as out-of-memory. After the symbols are
  installed, a fresh crash must show engine function names instead of `ntdll + offset`.

### A2. One local gate with real exit codes (S to M)
- **Build:**
  - Change the self-checking modes to exit with `FPlatformMisc::RequestExitWithStatus(false, FailCount ? 1 : 0)`, and
    give every automation mode a wall-clock timeout that logs `TAG timeout` and exits 2.
  - Add `Tools/Gate/gate.ps1`. Quick tier: `build_calibrate.bat` + `SimTests.exe` + the golden digest (A3). Full tier
    adds `Build.bat`, then each in-engine self-test (`-TagPauseTest`, `-TagInputTest`, `-TagControlsTest`,
    `-TagMultiTest`, `-TagChartTest`, `-TagArtTest -TagPadTest`), a short soak to Final (`-TagSoak` with
    `-TagQuarterLen=60`), and A1.
  - Every engine run gets `-LiveCoding=0 -RenderOffscreen -unattended`.
  - The gate prints one PASS/FAIL table and writes `Saved/Gate/<stamp>.json`.
- **Catches:** crashes, softlocks, broken menus and input, sim regressions, all before a commit instead of during
  Fred's play time.
- **Where:** `Tools/Gate/gate.ps1` (it replaces `Tools/Art/gates.ps1`); exit calls in `MatchDirector.cpp`,
  `MatchMulti.cpp`, `MatchPadPlay.cpp`, `UI/InputTest.cpp`, `UI/ControlsScreen.cpp`, `UI/CreatePlayer.cpp`.
- **Dependencies:** A1. For a commit-time gate, it should run on an exported copy of the index (the side-build method
  in `parallel-sessions.md`).
- **Verify:**
  - Force a failing check (for example, run `-TagPauseTest` with the pause key unbound through `-TagFakePad`). The gate
    must go red with the mode named.
  - Kill a run part-way. The gate must report a timeout or crash, not PASS.

### A3. Golden sim digest with a first-divergence report (S)
- **Build:**
  - A `SimGolden` mode in `SimTests` (or a small tool) plays 64 fixed seeds over 32 home/away pairings with two quarter
    lengths, and hashes every `PlayLog` line, the score and the clock after each play.
  - It writes `Tools/Realism/golden_sim.txt` (committed).
  - On a mismatch it prints the seed and the first play that differs, with both descriptions (Riot's "only the first
    divergence matters").
  - The gate fails when the digest changes and the diff does not touch `Sim/` or `Tune`. When it does, the change needs
    an explicit `-Bless` with a one-line reason.
- **Catches:** unintended changes to football outcomes from unrelated work, non-determinism (run twice in one process),
  and compiler or flag drift.
- **Where:** `Tools/SimTests.cpp` or a new `Tools/SimGolden.cpp`, plus `build_calibrate.bat`.
- **Dependencies:** none.
- **Verify:** `--set` one Tune constant 1 % off, and the gate must name the first diverging play. Two unchanged runs
  must match exactly.

### A4. Per-play rules invariants and enumerated edge-case scenarios (S to M)
- **Build:**
  - `Sim/GameInvariants.h`, checked after every play when on (always on in `SimTests` and `SimGolden`). It checks:
    - score changes only by legal amounts that match the log tag;
    - down between 1 and 4, `toGo` greater than 0 and no larger than the distance to the goal, `ballOn` between 0 and 100;
    - the clock never rises within a quarter;
    - timeouts between 0 and 3, reset at the half, 2 in overtime;
    - possession changes only with a reason (turnover, punt, downs, score, safety kick);
    - PAT or 2-point try only after a TD;
    - a free kick after a safety;
    - one two-minute warning per half;
    - an untimed down only after a defensive foul at 0:00;
    - overtime ends only when its rules allow it;
    - no snap after Final.
  - A scenario table that starts games from crafted states (q, clock, score, ballOn, down/distance, timeouts). Each
    state's possible outcomes run through the sim, and the result must fall into the expected class. Cases: backed up at
    own 1 (safety); trailing by 1-8 late (onside, 2-point decision); end of half with a defensive foul; OT first
    possession FG / TD / safety / turnover; fumble out of bounds forward and backward; intentional grounding from the end
    zone; kneel-out with timeouts left; a penalty on a try; a punt muffed into the end zone.
- **Catches:** the rules, clock and game-flow long tail that Madden still patches (1.8). A wrong rule is a "broken
  moment" no animation work can hide.
- **Where:** `Sim/` (header only, no engine dependency), `Tools/SimTests.cpp`.
- **Dependencies:** A3 for the seed harness.
- **Verify:** temporarily skip the halftime timeout reset (`Game.cpp:322`) or the safety free-kick setup
  (`Game.cpp:929-933`) in a scratch copy. The invariants must fire on the first affected play.

### A5. Signal camera cuts, and record the camera in telemetry (S)
- **Build:**
  - Call `PC->PlayerCameraManager->SetGameCameraCutThisFrame()` wherever `bCutNext` or a mode change fires
    (`MatchDirector.cpp:7160`) and on replay start and end.
  - Add telemetry columns for camera position, nearest body to the lens (minimum over every athlete's head and pelvis),
    camera angular speed, and whether the ball holder is on screen.
  - Detectors:
    - lens within 60 cm of a body (camera in a helmet or body);
    - ball holder off screen for more than 0.5 s while live;
    - angular-velocity spikes over 3x their neighbours (jitter);
    - the cut frame's mean frame difference against the frames either side (smear).
- **Catches:** the camera inside a helmet or body, losing the ball, camera jitter, and a ghosted or smeared frame on
  every cut (replay, cut-in, station change, quarter break).
- **Where:** `Game/MatchDirector.cpp` (UpdateCamera, HlTelemetry), `Tools/Realism/analyze.py` or `defects.py` (A6).
- **Dependencies:** none for the cut call; A6 for detection at scale.
- **Verify:**
  - Film a few clips with replays before and after the change, and compare the frame right after each cut. Any smear or
    ghost must be gone.
  - Run a debug offset that parks the camera inside the nearest athlete for one shot. The lens detector must flag every
    such frame.

### A6. Telemetry without frames, and a list of individual defects (M)
- **Build:**
  - A `-TagHlNoFrames` switch: `HlTelemetry` writes every frame with no screenshot request, so a full game costs
    telemetry only.
  - `Tools/Realism/defects.py` turns the CSVs into individual events (clip, frame, pid, type, size). Event types:
    - pose pop: pelvis, head, hand or foot speed over a threshold, or more than 1.8x both neighbours (reuse `body_pops`);
    - actor teleport;
    - planted-foot slide by `plf`;
    - upright overlap deeper than the captures' own p99 (`tackle-realism.md`: about 10 cm is normal at capture level);
    - ball more than 25 cm from both hands while held;
    - feet under the turf;
    - reference pose: hands at shoulder height and spread wide for 2+ frames, or an empty `anim` column;
    - facing more than 120° off the direction of travel above 3 m/s, outside backpedal and drop states;
    - NaN bones;
    - an official within 1 m.
  - The top N events per class link to a re-film command. Because the sim is deterministic, `-TagHlFromQ` plus the clip
    number reproduces any clip with frames.
- **Catches:** sliding, pops, T-poses, the ball through a hand, bodies through bodies, players facing backwards, at the
  scale of many games instead of one. It points Fred, or a session, at specific frames.
- **Where:** `Game/MatchDirector.cpp` (`TickHighlights`, `HlTelemetry`), `Tools/Realism/defects.py`.
- **Dependencies:** none. A5 adds the camera classes.
- **Verify:** the existing A/B switches make good known-bad cases:
  - `-TagHeightLock` (the old foot lock) must raise the slide count compared with the default;
  - `-TagNoKeepOut` must raise limb-in-pile events;
  - `-TagNoFootLock` must raise planted-foot travel.
  Also, a run with `-TagHlNoFrames` must give the same defect counts as a filmed run of the same clips, within
  tolerance (see open question 2).

### A7. Count hitches, attribute them, and keep a perf history (S)
- **Build:**
  - Extend the bench in `TAGGridiron.cpp` to log the maximum frame, frames over 2x the median, and frames over 25 ms.
    Each hitch gets a `TAG hitch` line with director state, clip, camera mode, cut-this-frame and pack streaming.
  - Add a cold mode (warm-up 0, starting from the menu: `-TagPlayNow` or `-TagPadPlayTest`) so loading, kickoff, the
    first replay and halftime are measured.
  - `bench.ps1` appends one line per run (commit, flags, avg, 1 % low, hitches/min, max) to `Saved/Gate/perf_history.csv`.
    The gate warns on a drop of more than 5 % or a rise in hitches.
  - `trace.ps1` on the worst window for root cause.
- **Catches:** hitches on cuts, replays, kickoffs and loads that an average hides, and silent perf regressions such as
  the unexplained 99 to 93 fps drop.
- **Where:** `Source/TAGGridiron/TAGGridiron.cpp`, `Tools/Perf/bench.ps1`.
- **Dependencies:** none.
- **Verify:** run with `-ExecCmds="obj gc"` scheduled mid-bench, or set a large `-TagPackMB=` to force pack streaming
  mid-run. The bench must log a hitch at that time with the right state.

### A8. Overnight coverage matrix (S, after A2 and A6)
- **Build:** `Tools/Gate/soak_matrix.ps1`, started by Windows Task Scheduler at night. It runs 16 games, so each of the
  32 teams appears once, across weather 1-4 and day/night, with short quarters, `-TagHlNoFrames` telemetry, A1's crash
  scan and A6's defect list. The morning summary lists crashes, stalls and defects per team and weather.
- **Catches:** team-, kit-, roster- and weather-specific failures that a fixed OKC-vs-MEM run never sees, plus slow
  leaks (the 11-18 soak OOMs).
- **Where:** `Tools/Gate/`.
- **Dependencies:** A1, A2, A6. Fred's PC must be idle (RAM notes in `perf-100fps.md`).
- **Verify:** the summary must show 32 distinct teams and every weather. A deliberately broken team asset (a scratch
  copy) must appear under that team only.

### A9. A cooked Development build for perf and playtests (M, may grow to L)
- **Build:** package a Windows Development (or Test) build, then:
  - run A7 on it with `-clearPSODriverCache`;
  - check `stat PSOPrecache` and `r.PSOPrecache.Validation=1` for misses;
  - keep the loading screen up until `FShaderPipelineCache::NumPrecompilesRemaining()==0`;
  - add a small bundled PSO cache only if misses remain;
  - point Fred's desktop shortcut at the cooked build once it passes A2.
- **Catches:** first-use shader and PSO hitches (impossible to fix in an editor binary, where precaching is compiled
  out), editor-only memory overhead (the 12-15 GB process in `perf-100fps.md`), and perf numbers that mean nothing for
  the shipped game.
- **Where:** packaging config (`Config/`), launcher bat, `UI/LoadingScreen`.
- **Dependencies:** cooking may fail on editor-only paths (`Render/TagEditorLib.cpp`, MaddenRef never-cook content, the
  D: junction for AnimH). Expect a few days of fixing.
- **Verify:** on the same seed, the cooked build must show zero "PSO PRECACHING MISS" lines on the second run and fewer
  cold-run hitches than the editor build.

### A10. Commit hygiene for parallel sessions (S for the hook, M for worktrees)
- **Build:**
  - A `pre-commit` hook that refuses a commit unless `gate.ps1 -Quick` passed for the exact tree hash of the index. The
    gate records the hash.
  - Commit small and often: the current tree carries 11.7k uncommitted lines from several sessions.
  - Move sessions whose work does not touch `Content/` (sim, tools, UI code) to their own git worktrees
    ([Claude Code worktrees](https://code.claude.com/docs/en/worktrees)), using the sparse Source/Config/Plugins checkout
    already proven in `parallel-sessions.md`.
- **Catches:** half-written files from another session landing in a commit, and commits that do not compile. The memory
  notes record both (a "'Role' hides class member" error, and an LNK2019 from a file edited between the mtime check and
  `git add`).
- **Where:** `.git/hooks/pre-commit` (a local hook, not versioned), `Tools/Gate/`.
- **Dependencies:** A2 and A3.
- **Verify:** stage a file containing a deliberate syntax error, or with the golden digest changed by a non-sim file.
  The hook must refuse the commit.

### A11. Bug register, triage bar and definition of done (S)
- **Build:**
  - One bug list: P0 = crash, hang, softlock, wrong rule or score, lost settings or roster; P1 = visible in most games
    (pop, T-pose, camera in a body, ball through a hand, frozen frame); P2 = polish.
  - Weekly zero bug bounce on P0 and P1 before new feature work. A1, A6 and A8 add entries automatically.
  - Definition of done per feature:
    - a `SimTests` case or invariant;
    - the gate green;
    - an A/B flag;
    - the bench within 2 %;
    - no new crash signature;
    - a filmed check of what the user sits through: call screen, huddle, his own camera, post-play. The memory notes
      show that these were missed when only snap-to-whistle was filmed.
- **Catches:** regressions of things already fixed, and features that land without a way to check them.
- **Where:** a tracked file chosen by Fred (none exists today), `Tools/Gate/`.
- **Dependencies:** A1 and A2 make it cheap.
- **Verify:** after two weeks, every P0 crash signature in A1's register is either fixed or listed with an owner.

### A12. VLM triage of flagged frames (S to M)
- **Build:** for each event A5 and A6 flag, plus a fixed contact sheet of post-play and huddle frames, re-film the clip
  (deterministic) and send a 6-frame strip to a vision model, asking which of a fixed defect list it sees. Use the answer
  to rank and describe events. It is never a gate, and never the only check for pose or clipping (1.4).
- **Catches:** presentation and camera problems numbers miss (a shot from behind the bench, text over a face, painted
  crowd blocks, a missing texture). It also triages numeric flags faster.
- **Where:** `Tools/Realism/vlm_review.py`.
- **Dependencies:** A6, plus Fred's call on API cost and privacy.
- **Verify:** build a 40-sheet set from past captures: known-bad (the post-play shots from behind the bench and the
  orange-disc crowd close-ups in the memory notes) and known-good. Measure precision and recall before using it, and drop
  it if precision is under about 70 %.

---

## 4. Not worth doing (now)

- **RL or imitation-learning test agents (EA SEED style):** CPU-vs-CPU games plus the scripted user modes
  (`-TagPadPlayTest`, `-TagMultiTest`) already supply the play volume. RL needs a fast reset loop, reward design and
  upkeep a solo developer cannot carry. Revisit only to hunt user exploits.
- **Moving to UE Automation tests, functional test actors or Gauntlet:** the `-Tag` modes are already functional tests.
  Gauntlet adds a C#/UAT layer for little gain on one PC, and the Automated Perf Testing plugin is experimental with no
  upgrade path from 5.5 (1.6). Exit codes plus a gate script give the same verdict. The exception: Gauntlet's
  `UE.BootTest` could be worth trying once a cooked build exists.
- **Pixel-diff golden screenshots of gameplay:** Lumen, TSR, weather, crowd variation and constant art changes would
  flood it with false failures. Telemetry detectors measure the actual defects. If wanted at all, keep image diffs for
  static UI screens (`-TagUiShots`).
- **Sentry or another cloud crash service:** with no outside players, the local `Saved/Crashes` folder plus symbols and
  A1 gives the same triage. Revisit when builds go to outside testers (note it cannot run alongside UE's Crash Reporter on
  Windows).
- **Training a custom CNN glitch detector:** it needs labelled data, and the defects TAG cares about can be measured
  directly from bones and the sim with capture ground truth.
- **Turning on incremental GC blindly:** it is experimental, needs `TObjectPtr` everywhere, and has forum reports of
  crashes and freezes. Measure GC hitches with A7 first.
- **Animation Budget Allocator on field players:** throttling and interpolating the 22 men on a TV camera would add the
  very stutter being removed. The crowd already ticks at its own 30 Hz (`-TagFanHz`). The allocator might fit the
  sideline and bench later, if A7 shows animation game-thread cost still matters.
- **A CI server (Jenkins, TeamCity):** one PC, one developer. Task Scheduler plus `gate.ps1` does the same job.
- **Chasing more scorecard tenths:** Fred has said he prefers playtesting (`playtest-over-scorecard.md`). The actions
  above find defects for him to judge; they are not a new score to maximise.

---

## 5. Open questions

1. Does an in-engine CPU game (`-TagSoak`) play the same plays as headless `SimTests` for the same seed and matchup?
   If so, A3's digest can also catch presentation code that mutates sim state (like the stale `Ent*` maps in
   `game-flow-and-presentation.md`). I did not establish how the engine seeds `Game`.
2. Are the telemetry columns valid without screenshots and without `-UseFixedTimeStep`? Variable timestep would change
   per-frame speeds and pop thresholds. A6 needs a calibration run comparing the two.
3. Is the Live Coding build lock global to the engine install or per project directory? This decides whether a
   worktree session can build while another session's game is open (A10).
4. What caused today's two `-TagHlFull` access violations and the no-flag game crash on 11-22? The stacks have no
   symbols, so A1 comes first.
5. How hard is cooking: editor-only code, MaddenRef content, the D: junction? I did not try it (read-only brief).
6. Is a smeared or ghosted frame on cuts actually visible in TAG today? That is inferred from the missing cut call and
   the TSR and motion blur settings, not seen on frames.
7. Are the 1 % lows (about 62 fps against an average of about 123) caused by PSO and streaming hitches or by steady
   render-thread cost? Only A7 plus A9 can separate the two. A cooked build may not move them.
8. Is there disk space for the engine symbols and nightly telemetry? The memory notes D: as nearly full at times, and
   C: had about 14 GB free.
