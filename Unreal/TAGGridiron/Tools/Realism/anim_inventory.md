# Animation inventory: action state -> captures, selection, procedural layers, measured

The arm sweep's map of what plays on screen (passes 1-3, 27 Nov). Measured with `Tools/Realism/arm_audit.py` over
`-TagHlFull` telemetry of four games, seeds 7 / 11 / 29 / 41 (5-minute quarters; `armset.ps1` pattern: `-TagWeatherSeed=N
-TagPackSeed=N -TagWeather=0 -TagHome/-TagAway -TagQuarterLen=300 -TagHighlights -TagHlFull -TagHlNoFrames`). Run
`python arm_audit.py OUT.json RUN_DIR...`; the tables print by sim category, by playing capture family, and per event
(throw / catch / drop / pbu / int) with the worst ten throws and catches to film (`-TagThrowCam -TagCatchCam -TagHlCamOnly`).

Abbreviations: **A** = `Source/TAGGridiron/Render/Athlete.cpp`, **R** = `Render/TagRetargetAnimInstance.cpp`,
**D** = `Game/MatchDirector.cpp`. Line numbers as of the pass-2 tree; search the function name if they have moved.

Telemetry columns used (D `HlTelemetry`): `anim` (`mm:` / `one:<clip>@<t>` / `clip:` / `fall:` / `getup:` / `scene:`),
`ahl/ahr` hand off the capture in the chest frame after all layers (cm), `afl/afr` the football layer alone (cm), `ael/aer`
elbow bend the football layer put on (deg), `awl/awr` vs `acl/acr` shown vs captured arm turn rate (deg/s; pop = shown
> captured + 600), `ajit` layer jitter, `lod`, `twl/twr` hand twist about the forearm, `tul/tur` upper arm own twist (deg),
`hws/hwc` head turn rate shown / captured (head pop = +400 deg/s). Log lines: `TAG keytime <ev> #n clip c frame f: in|none
<s> (...) hands N cm, fc F rev R` (the capture's key moment vs the ball's; pass 3: the shown hands to the sim's ball at the
event, and the forecast's time left at it), `TAG forecast play`, `TAG one-shot over before its key`, `TAG contact <clip>`
(each catch capture's measured contact), `TAG walk-up man`, `TAG toss pair`, `TAG catch fit`, `TAG pitch catch`, `TAG referee touchdown signal`, `TAG:
pack batch ...`, `TAG: anim census ...`, `TAG: pack assets ... (from the pack index | in the registry)`.

A/B: `-TagArmSweepOff` = the game before the sweep (all three passes). `-TagWalkUpAvoid` = the pass-2 walk-up avoidance profile (left off).
Pass 3: `-TagNoWalkPlan` (no walk-up lanes / waits), `-TagNoCatchSeat` (no catch seat), `-TagContactMark` (catch contacts searched
from the index's mark only), `-TagMeshCam` (film: the run exchange close), `-TagCloseCamScale=` (film: closer throw / catch / mesh shots).

## The library (where the captures come from)

| What | Where | Notes |
|---|---|---|
| Pack index (12482 captures: `cat` folder, len, `release` / `contact` / `hands_up` frames, speeds) | `Content/TAG/AnimH/Pack/pack_index.json`, `FAthleteAssets::LoadPackIndex` A:~670 | the cooked registry keeps only tagged sequences: in a cooked build `PackAssets` (A:603) lists categories from the index |
| Timed families (throws, catches, drops, kicks, tackled, tacklers, moves...) | `SelectPack` A:1070, `PackTimed` A:~680 | pass 2: every capture of a family (no Max x scale draw) |
| Tackle pairs / gangs / turnovers / doubles / handoffs / PA fakes | `pack_pairs.json`, `pack_gangs.json`, `pack_turnovers.json`, `pack_dbl.json`, `pack_handoffs.json`, `pack_pafakes.json` | pairs uncapped in pass 2 |
| Flavour families (celebrations, post-play idles, stances, get-ups, injuries, breakdowns) | `PackFamily` | a fresh draw each quarter (`ReseedPack` A:1231), skipped when there is no room beside the draw in play |
| Load order and memory | `ReseedPack` / `LoadNextPackChunk` A:1360 | plays first and whole (the essential share, loads down to 250 MB free); flavour after, to the 1500 MB floor (`-TagMemFloorMB=`) |

Measured (pass 2): editor `-game` loads the whole essential share every game (2304 captures, 863-867 MB of uncooked clips,
~0.38 MB each incl. pairs' long clips) and 2304-2304 of 2538-2664 queued; the packaged game loads 2727 of 2727 in 5 s,
96 MB (0.03 MB a capture). Before pass 1 the editor loaded 48 of ~1240 and every later quarter nothing.
Census (editor): `/Game/TAG/AnimH/Pack` 2555 sequences 641 MB; **`/Game/Characters/UEFN_Mannequin/Animations` 733
sequences 320 MB** - pulled in by every hero MM database's hard reference to the UEFN normalization set
`PSN_Dense_All`, which references the UEFN sample databases and their clips (editor only; not in the cooked census).

## Action states

| State | Captures | Selection (file:line) | Fallback when none fits | Procedural layers on top (limits) | Measured (pass 2, four games) |
|---|---|---|---|---|---|
| Stances (pre-snap) | keyed `A_TAG_Stance*`, pack `PackStances` by position | A Drive stance branch; `PackFamily` PrePlay rules A:~1090 | keyed loop | look 0.85; foot lock | presnap code hand 0.0 cm p95, head pop 0.9 % |
| Pre-snap motion / shifts / huddle break | none wired (pack has `DLShift_3pt_*`, `*break_huddle*`) | excluded from stance families (`StanceNone`) because those loop as idles and these travel | motion matching walk | AvoidOthers 66 cm / 0.8 s; pass 3: walk-up plan (D `PlanWalkUp`, applied in `SyncPlay`): each walker, line first, gets a wait (<= 1.5 s, inside the walk's own time) and a lane (his path bowed <= 90 cm / a sixth of its length to one side, nothing at the ends) chosen so no two paths are inside 1.1 m at once; render only, sim spots at the snap unchanged | see the pass-3 table |
| QB drop (UC / gun / pistol) | keyed drops, `PackDropsUC/SG` | A Drive QB branch | keyed drop; pistol uses the gun drop | none since pass 1 (QB_/InPocket/Scramble clips count as ball clips: A `bCarryClipIs`) | QB code hand 0.0 cm (was 11.7) |
| Pocket | `QBPocket` loop, `MG_QB_InPocket_Walk/Jog D1-D8`, scramble/run | A Drive pocket branch by speed and direction | MM | none (as above) | |
| Throws (set / quick / deep / roll R / roll L / touch / sidearm / fade) | `PackThrows*` (pass 2: 48/21/12/10/6), keyed `A_TAG_QB_Throw*` | windup at `pendingThrow` D:6380 -> `PlayThrowTimed` A:4118; re-pick in the windup; `AlignKey` A:2781 steers the rate 0.7-1.4 so the release lands on the sim's | keyed throw from 7 frames before release | none (IK off from the capture's first frame) | code hand 0.2 / elbow 0.4 deg (was 23 / 40); hand peak 56 ms before the ball leaves on every throw (definition) |
| Throw-away | the set throw, timed (pass 1) | D:6380 `bWindAway` | | none | |
| Shovel | `PackShovelSet/Move` | `PlayShovelTimed` A:4183 | quick throw | none | |
| Pump fake | `PackPumpFakes` | `PlayPumpFake` A:4199 | | none | |
| Handoff / play fake (incl. pistol: 22 handoffs, 15 PA in the tables) | two-man `RepHandoffs`, `RepPaFakes` | `PlayRepHandoffFit` A:4744 by geometry, `PlayRepPaFake` A:4849 | keyed handoff | pair spacing, ball handover 0.17 s | code hand 0 |
| Toss / pitch | pass 3: the pack's eight two-man tosses (`pack_tosses.json` from `Tools/Art/pack_tosses.py`: release = QB hand fastest, catch = back's hands together and slowest) | D toss block -> `AAthlete::PlayRepToss` (side of the toss from the sim's mesh direction, gun / under center, the back's place around the QB); QB keyed on the release (`Mh.T`), back on `tossT0 + tossDur` | keyed `QB_Pitch` + `PlayPitchCatch` (pass 2) when no pair fits | none | 10 of 10 forced tosses on a pair, release 17 ms off the sim's |
| Spike (clock) | none: the pack's `PrePlay_SpikeBall_QB_*` travel 7 m at 4-6 m/s (a QB running in and spiking), `Generic_SpikeBall` is a celebration | sim ends the play at the snap | snap one-shot | | not wired (no fitting capture) |
| Catches (in stride, over shoulder, back shoulder, high, low, comeback, sideline toe drag, spectacular) | `PackCatchStride/High/Low/OverShoulder/Sideline/Stand/Comeback/Spectacular` (pass 2: 64/57/31/23/3/76/21 + 14 spectacular) | pass 3: from the release forecast (`Ball::fcFate/fcBy/fcAt/fcZ/fcT/fcCont/fcDive`, D forecast block): started up to 0.9 s out (dive 0.3, INT 0.6), re-picked on an `fcRev` change; `PlayCatch` -> `BestCatchFit` by gait, approach angle, named height (+ the measured contact hand's height / reach when grossly off), run-in lead; warp (`AlignKey` 0.7-1.4) onto `fcT`, handed to the ball's own approach over the last 0.5 s by at most 0.12 s; contact frames measured per capture (`CatchContactT`: hands together and slowest at the named height) | keyed catch from its hands frame (catch event, CatchIn 0) | catch seat (pass 3, A `Drive`): the body eased <= 70 cm over the last 0.5 s so the capture's contact hands meet the ball; reach IK only over a catch capture, capped 10 cm; look 0.35 over captures; carry tuck after the tail (key + 0.35 s, held while the ball is still coming) | see the pass-3 table |
| Drops | `PackCatchDrops` | pass 3: forecast fate 2 (or the receiver on a forecast breakup / INT) -> `PlayDrop` fitted like a catch (`BestCatchFit`), counted a catch attempt (not cut for lag) | | no reach over it (pass 2) | see the pass-3 table |
| Breakups (receiver / defender) | receiver: drop capture (pass 1); defender: `PackSwat*` | forecast fate 3 -> `PlayBallDefense`, keyed on the swat's measured contact (pass 3 `SwatContactT`: reaching hand at full stretch; the index's mark was 0.1 s into some swats) | | none | see the pass-3 table |
| Interceptions | two-man `PackInts` / `PackIntCatch` | fate 4 -> `PlayIntPair` A:4391 | own INT catch timed | none | |
| Dive / layout catch | `PackCatchDive` | `PlayDiveAttempt` A:4274 (fate dive, 0.3 s) | keyed dive | | |
| Loose ball / scoop / strip | `PackLooseDives`, low catch, `PackStrips` | `PlayLooseDive` A:4299 | | | |
| Kick catch | `PackKickCatchStand/Run` | D kick catch prime + `AlignKey` | | | |
| Ball carrier | MM `_BC_` loops, cover-up `MG_BC_Coverball_*` | Drive carrier refresh | MM non-BC with carry tuck | carry tuck on non-ball clips only | code hand 1.2 / 7.7 p95 (was 2.0 / 13.4) |
| Carrier moves (juke, spin, truck, stiff arm, hurdle) | keyed + `PackJukes/Spins/Trucks/StiffArms/Hurdles` | Drive move branch | keyed | lean halved | family pops 0.7 % (was 3.1 %), slide 8.0 cm/s |
| Pass-pro stand-off / run blocks / walks | `RepLoops`, `BlockWalks`, `PackEngageOff/Def`, keyed block loops | Drive block branch | keyed loops | reach capped 10 cm | block family >tol 0.5 %, pops 1.7 % |
| Rush wins / holds / moves | two-man `RepWins/BigWins/Holds`, `PackRushWinsL/R`, `PackBeatenL/R` | shed event -> `PlayRepWin` A:4650 (pass 2: only when the shed goes left - every two-man win beats the blocker to the rusher's left), else `PlayRushMove` + `PlayBeaten` | keyed moves | none | rush family slide 1.16 cm/s; foot-slide events in rush captures 93 -> 72 |
| Tackles / falls / pile-on | `PackPairs`, `PackGangs`, `PackTacklers`, `PileOn` | D tackle events, `TickFall` A:1449 | keyed tackles | impact springs, keep-out when down | tackle family >tol 0.05 % |
| Get-ups / help-ups | keyed + `PackGetUps`, help-up scene | `StartGetUp` A:4458, D `TickHelpUp` | ragdoll -> get-up | pass 2: scenes count as captures that own the arms (no tuck over a help-up) | family over tolerance 0 % (was 19 %) |
| Celebrations | keyed `Celebrate_*`, `PackCelebrations`, turnover sets | D `CelebPick` | keyed | carry tuck on a ball-holding celebration | family >tol 4.6 % (was 7.1 %): the worst left, see gaps |
| Kicks / punts / holds / snaps | `PackKickoffs/FieldGoals/Punts`, FG / punt scenes | D kick code, `BeginScene` | keyed | scenes own their arms (pass 2) | family >tol 0.01 % (was 0.5 %) |
| Officials | pass 3: the pack's referee loops `MG_Ref_D<n>_<Jog/Run/Sprint>` (directions measured from each clip's root motion: D1 forward, D2 forward-right, D3 left, D4 back-right, D5 back; Jog D1-D3, Run D1-D5, Sprint D1/D4/D5) by speed (sprint > 6.5 m/s, run > 4.4) and the nearest direction, rate 0.7-1.4; `Ref_Touchdown_Signal` (pass 2) | A `Drive` officials branch (`bOfficialBody`) | MM below 1.5 m/s | look at the ball | |
| Sideline / bench | celebrations loops, idles, stadium pack | `SidelineCheer` A:5228 | | URO on (sideline/stadium only) | |

## Procedural layers (R `Evaluate`: Football -> Orient -> Stride -> Lean -> Crossfade -> Impact -> Feet -> KeepOut -> Twist)

| Layer | Where | Limits now |
|---|---|---|
| Carry tuck / two-hand secure | R `ApplyFootballLayer` R:775, eased A `Ease(CarryA` A:~6804 | off over ball clips (BC_, QB_, InPocket, Scramble, Coverball), over any capture from its first frame, over scenes |
| Reach (catch, wrap, block hands) | R:~840 | <= 10 cm from the capture's hands; the catch reach only over a catch capture |
| Throw arm IK | R:~790 | off whenever the keyed throw exists (always) |
| Head look | R:~880 | weight 0.85, 0.35 while a capture owns the arms |
| Lean / orient / stride | R `ApplyLean` R:975 etc. | unchanged |
| Foot lock | R `ApplyFeet` R:1123 | unchanged |
| Keep-out | R `ApplyKeepOut` R:1225 | down men only |
| Twist helpers | R `ApplyTwist` R:444 | measured: hand twist past 120 deg < 1 % of frames in any state, frame-to-frame flips 0-0.76 % (drops) - no candy-wrap pops |
| AvoidOthers | A `AvoidOthers` A:5423 | 66 cm / 0.8 s / 45 cm / 160 cm/s (the walk-up profile behind `-TagWalkUpAvoid`) |

## Known gaps (what the pack does not have, or what is not wired)

- Not in the pack: a one-man throw-away, an across-the-body throw, officials spotting / picking up the ball, a clock spike.
- Wired nowhere yet: pre-snap shifts (`Preplay_19_DLShift_*`), huddle breaks, the play-action tosses
  (`Split_Pro_3pt_Quick_Toss_*_PA_Dropback`, `*_Toss_*_PA_Setup_*`: left out of `pack_tosses.json`).
- Catch keys (pass 3): measured per capture (`CatchContactT`), at the named height over the whole clip; a few captures have
  no frame at their named height with the hands together (`Catch_Run_135_Chest_FingerTip`: found 0.33 s in, 83 cm out) and
  are keyed on a reach rather than the catch.
- Breakups: 8 of 10 `pbu` events still find the defender on locomotion - the breakups the forecast names are timed (swat
  keys 128 / 187 ms mean / p95, two measured), the rest are tips the forecast did not see coming (at-the-ball revisions).
- Interceptions (n = 3 a set) and tipped balls: a catch the forecast revised "at the ball" has no run-in left; the catcher
  is on locomotion when the ball arrives.
- Post-whistle overlaps are untouched by the walk-up plan (14-15 a set, 5-7 hard): the men jog to the ball / huddle
  (`TickPostJog`) on straight lines.
- The UEFN normalization-set chain (320 MB in the editor census, 733 sequences): pass 3 re-pointed all 169 hero `PSD_*`
  databases to a TAG set of their own (`PSN_TAGH_All`, the census then had no UEFN clips) and played the four audit games:
  locomotion got a little worse on every measure (motion-matching frames' capture deviation 8.50 -> 8.57 cm, skate median
  0.454 -> 0.481 m/s, skating over 1 m/s 26.7 -> 28.0 % of frames, travel-angle p90 71.9 -> 72.6 deg; 0.8 M frames a
  side). Reverted (the databases restored from the backup, the set deleted): the 320 MB stays an editor-only cost.
- Game flow: the clock between plays follows on-screen time (real clock), so presentation changes can move where a
  quarter ends and the plays after it (seed 7 in pass 2: one more play before the end of Q1). Per-play sim results match.

## Catches, final (pass 2 vs pass 1, four games, 90 catches)

| Measure | Before pass 1 | After pass 1 | After pass 2 |
|---|---|---|---|
| catches with no timed capture (no key) | 78 % | 11 % | 2 % |
| catch-window frames on locomotion | 35 % | 26 % | 7 % |
| hands-on-ball vs the ball, mean / p95 / max | 46 / 170 / 174 ms (keyed only) | 43 / 146 / 174 ms | 28 / 137 / 240 ms; 81 of 88 within 66 ms |
| code hand, mean of each catch's worst frame | 64 cm | 15 cm | 10.6 cm |
| code elbow, same | 72 deg | 30 deg | 25 deg |
| catches over tolerance (15 cm / 25 deg) | 86 | 53 | 50 |
| arm-pop frames in catch windows | 18.3 % | 6.3 % | 7.0 % |
| head pops in catch windows | - | - | 5.3 % |

The seven catches still 0.1-0.24 s off are one-handed / reach-behind / way-in-front / toe-drag captures whose index
`release` frame is not the hands-on-ball frame. Catches over tolerance are mostly the 10 cm reach bending the elbow
25-45 deg in the last 0.2 s: a capture whose hands are 10-30 cm off the ball. Next: contact frames measured from the
captures (hand nearest the ball's path) instead of `release`, and a wider catch draw.

## Pass 3 (forecast-timed catches, measured contacts, walk-up plan, tosses, officials)

Four games as above, 100 catches a set. Before = this tree with the pass-2 code (`p3B`, the sim's release forecast in, the
digest re-blessed); after = the final pass-3 code (`p3S`: catch seat off, which is the default). Telemetry
`%LOCALAPPDATA%/TAGAnim/p3B_*`, `p3S_*`; audits `Saved/ArmSweep/pass3/audit_before.*`, `audit_after_final_noseat.*`
(and the intermediate rounds `audit_after1..5`).

| Measure | Before (p3B) | After (p3S) | Note |
|---|---|---|---|
| catch key vs ball, mean / p95 / max | 24 / 75 / 244 ms | 21 / 46 / 201 ms | p95 under 66 ms; the max is one forecast revised at the roll 0.17 s out |
| catches with no timed capture | 8 % | 6 % | the rest: "at the ball" revisions, tipped balls |
| catches over tolerance (15 cm / 25 deg) | 46 of 100 | 21 of 100 | kick catches 1 -> 0 of 25 |
| shown hands to the sim's ball at the catch, p50 / p90 | 46 / 113 cm (p3A2) | 39 / 76 cm | 32 / 67 cm with `-TagCatchSeat` (but 34 over tolerance) |
| catch-window arm pops | 6.18 % | 4.86 % | |
| drop-window pops | 9.09 % | 2.27 % | n = 2 drops a set |
| breakup-window pops | 4.04 % | 3.54 % | |
| celebrations over tolerance | 4.89 % | 0.20 % | celebrations count as ball clips (no carry tuck over them) |
| pre-snap overlaps (all / hard) | 28 / 13 | 8 / 7 | the walk-up plan; it never ran before p3A5 (the sim counts the walk-up as live) |
| post-whistle overlaps | 15 / 5 | 15 / 5 | untouched |
| foot-slide events | 527 | 529 | |
| tosses on a two-man capture | 0 (keyed pitch + back's pitch catch) | 10 of 10 forced tosses | release 17 ms off the sim's |

Hands on the ball, filmed (seed 11 at 1920x1080, `-TagCloseCamScale=0.5 -TagMeshCam`; sheets and frames in
`Saved/ArmSweep/pass3/films`): a two-hand waist catch has the ball between both palms, fingers round it, on its frame; the
carry after it is tucked at the chest. A behind-the-body one-hander (`Catch_Run_Chest_135_TurnUp_FaceUp_BehindOneHand`)
reached up and away while the ball came in at his hip, 116 cm from the hands, and the hand gathered it four frames later
(the fit has no left / right: the pack's one-handers are not mirrored to the ball's side). At half distance the throw and
mesh shots are inside the linemen and the quarterback fills the frame - the grip at the release and the handoff could not
be judged from them (0.8 would be the scale to use). No finger poses or ball seats were changed.

What did not work and is left off: the catch seat (`-TagCatchSeat`: hands 7-9 cm nearer the ball but 34 catches over
tolerance and 152 arm pops: the body moved under the reach); fitting catches by the measured contact height alone (15 of
100 chest-high balls went to an above-the-head leap); the window from the index mark for named captures (a leap's
"catch" came out on its landing).
