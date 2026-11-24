# TAG Gridiron: next-level master plan

Merged on 2026-11-24 from seven research reports in this folder. Each report holds the sources, the code evidence, and the full detail for every action named here.

| Report | Domain |
|---|---|
| [01](01-animation-motion.md) | Animation and motion |
| [02](02-gameplay-ai-sim.md) | Gameplay AI and sim |
| [03](03-rendering-characters.md) | Rendering, characters, environment |
| [04](04-broadcast-presentation.md) | Broadcast presentation and cameras |
| [05](05-audio-commentary.md) | Audio and commentary |
| [06](06-controls-feel-ui.md) | Controls, game feel, UI |
| [07](07-polish-qa-performance.md) | Polish pipeline, QA, performance |

Action names below use the report number and the action id inside it, for example "06 #1" or "02 A2". Effort: S = under a day, M = days, L = weeks.

## What the research says, in five lines

1. **The top studios' edge is selection and context, not secret technology.** EA's named features (FieldSENSE, BOOM Tech) are not published in detail; what is known reduces to picking the right capture for the body, the speed and the moment, and re-picking after contact. TAG plays its captures faithfully but picks them crudely.
2. **TAG's defenders know things they could not see.** Man coverage reads the true route, picks ignore facing, linebackers know run from pass, blocks ignore angle. These are the complaints players make about Madden, and fixing them is the largest realism gain in the sim.
3. **The hands come first.** The stick dead zone, a 220-300 ms wait before a throw starts, dropped move presses, and switching to the wrong defender are all felt on every snap, and most are under a day to fix.
4. **Presentation is one shot deep.** One replay angle at one speed, a score bug missing timeouts, weather and night that do not read on film, a mix with no loudness target and the crowd ducked on touchdowns.
5. **Nothing can fail.** No exit codes, no gate, 78 untracked crash reports, about 11.7k uncommitted lines. Every other item is safer after this is fixed.

## Reviewer's checks

The researchers were read-only and did not run the game. I checked a sample of their code claims myself.

| Claim | Status |
|---|---|
| Game never returns a failing exit code (`RequestExitWithStatus` absent) | Confirmed by search |
| Camera cuts never signalled to the renderer (`SetGameCameraCutThisFrame` absent) | Confirmed by search |
| 78 crash folders in `Saved/Crashes` | Confirmed |
| Locomotion set chosen by `id % 6` (`Athlete.cpp:2822`) | Confirmed |
| `ATagAudio::CrowdEnergy()` defined but never called | Confirmed |
| Pump-fake second-tap window of 0.22 s delays a tap throw (`MatchDirector.cpp:7615`) | Window confirmed; the full delay path not traced |
| `predictPos` walks the receiver's real route | Helper confirmed; the man-coverage call site not read |
| Stick dead zone 0.25 applied per axis | Value confirmed; per-axis behaviour not read |

**Confirm on a capture or a playtest before building on them** (inferred from code, never observed):
- Smear on camera cuts with TSR and motion blur (07 A5).
- Booth open playing over an empty pregame field (04 A5).
- Booth voice ignoring Master volume, and the stands cheering while the audio groans (05 A1, A2).
- Cause of the trails behind legs: TSR, motion blur, or the 30 fps film rate (03 A8).
- Tired gaits in the hero-tree databases (01 #1); the evidence is from the older tree.
- All frame-time costs in report 03 are estimates. It puts headroom above 100 fps at 1-2 ms of GPU, so measure each one.

**Standing constraints that shape the plan:**
- **Personal-use content.** The mocap and the coach, sideline and fan models come from the personal Madden 19 packs (decision of 2026-11-10/11, personal use only). Nothing here changes that, but more close-ups (04 A3, 03 A12) put those models on screen more. If the game is ever distributed, they need replacing first.
- **Big Data Bowl data licence is unchecked.** 02 A6 and part of 02 A4 calibrate against NFL tracking data. As I recall the competition terms limit use to the competition and non-commercial research, which fits a personal build but not a sold one. Verify before relying on it for anything distributed. nflverse and FTN fields are the safer targets.
- **`MatchDirector.cpp` is 8,363 lines and shared by several sessions.** New presentation work goes in new files (`Game/MatchBroadcast.cpp`, `UI/BroadcastGfx.cpp`) with small hooks in the director.

## Where reports overlap: build once

| Shared piece | Reports | Build it as |
|---|---|---|
| Sim's future path | 01 #2 wants animation to read it; 02 A1 wants man defenders to stop reading it; 06 #6 wants the user's man predicted from the stick | One change set to `predictPos` users: animation gets the true path, defenders get a perceived one. Do 02 A1 and 01 #2 together. |
| Contact package | 01 #5 and #7 (momentum, stumbles), 05 A8 (contact sounds), 06 #2 and #10 (rumble, hit-stop, shake) | One contact event carrying impulse, fed to animation, audio, rumble and camera. Hit-stop must hold the render, not change sim results. |
| Memory across games | 04 A3 (shots), 05 A6 (booth lines) | One saved "recently used" store. |
| Camera cut signal | 07 A5, 04 A2 (replays), 03 A12 (close shots) | Do 07 A5 first; the others add cuts. |
| Capture matrix | 03 A0 (day/night, dry/rain/snow), 07 A8 (overnight, all teams) | One script; 03 A0 is its first four rows. |
| Sim-truth numbers | 04 A6 (stat tags), 02 A3 and A9 (win rates, QB profile) | Same fields: log once, show on screen and check in telemetry. |
| Stored replay frames | 04 A10 (halftime show), 03 A11 (video boards) | One frame store; memory cost unmeasured. |
| Crowd state | 05 A2 (stands follow crowd energy), 03 A3 and A13 | `CrowdEnergy()` drives both. |

## The plan

Ordered so that each phase is playable and felt in a playtest. Within a phase, items are independent unless noted.

### Phase 0: safety net (about 2-3 days)

Do this before anything else; it protects the work already done.

| Do | Ref | Effort |
|---|---|---|
| Commit the uncommitted work from the parallel sessions, one topic per commit | 07 A10 | S |
| Crash register with engine symbols; triage the 9 recent game crashes | 07 A1 | S |
| One local gate with real exit codes and a PASS/FAIL table | 07 A2 | S-M |
| Golden sim digest over 64 seeds with a first-divergence report | 07 A3 | S |
| Per-play rules checks plus scenarios for safety, onside, overtime, untimed down | 07 A4 | S-M |

### Phase 1: quick wins you feel on the first snap (each under a day)

| Do | What changes | Ref |
|---|---|---|
| Round, rescaled stick dead zone | Players run where the stick points; full speed on diagonals | 06 #1 |
| 150 ms buffer for moves, cue when refused | Jukes and trucks stop being dropped | 06 #4 |
| Rumble (Off/Low/Normal) | The pad responds for the first time | 06 #2 |
| Booth on Master volume, plus a Commentary slider | Volume settings work | 05 A1 |
| Stands follow crowd energy | Visitors' fans stop cheering your touchdowns | 05 A2 |
| Picks and breakups need eyes on the ball | No more interceptions by defenders facing away | 02 A2 |
| Speed term in the tackle pick | A sprint collision no longer plays a jog tackle | 01 #5 |
| Signal camera cuts to the renderer | Removes cut smear, if the capture confirms it | 07 A5 |
| Broadcast grade: vignette and grain off, softer highlights | White jerseys keep their folds | 03 A1 |
| Night reads as night | Dark bowl, glaring light banks | 03 A3 |
| Replay-worthiness score and budget | Third-down conversions get replays; not every sack does | 04 A4 |
| Field overlays to convention | Red line on 4th down, lines hidden in replays | 04 A7 |
| Loudness harness and master limiter | The mix gets a number (-24 LUFS target) | 05 A4 |
| Look matrix capture script | Every later look change is checked in four conditions | 03 A0 |

### Phase 2: the feel core (about 2-3 weeks)

The link between thumb, athlete and opponent. This is where the game starts to play differently.

| Do | What changes | Ref | Effort |
|---|---|---|---|
| Start the throw on the press | Removes 220-300 ms from every pass | 06 #3 | S-M |
| Switching follows the catch point, with a grace period | You get the defender who can make the play | 06 #5 | S-M |
| User's man animates fastest; motion matching reads the sim's path | Body turns with the thumb; cuts and breaks picked on time | 06 #6, 01 #2 | M |
| Man defenders read speed and leverage, with reaction delay | Double moves and good routes win | 02 A1 | M |
| Locomotion by body class, fatigue gated | Linemen move like linemen; nobody is tired on snap one | 01 #1 | S-M |
| Velocity-keeping transitions | Limbs stop freezing at clip switches | 01 #3, #6 | S-M |
| Blocks need the angle | No blocks on defenders already past the blocker | 02 A5 | M |
| Linebackers and safeties read keys | Play action and draws can fool them | 02 A4 | M |
| Win-rate telemetry | Tests whether ratings matter | 02 A3 | S-M |
| Measure latency, then tune (Reflex, frame sync) | A number for input lag | 06 #7 | S |
| Contact package: stumbles, contact sounds, hit-stop, shake | Hits land in body, ears and hands | 01 #7, 05 A8, 06 #10 | M |

Decide by feel after this phase: the user-only "Authentic / Responsive" movement option (06 #8). It trades realism for control, so it is a playtest call.

### Phase 3: the broadcast (about 3-4 weeks)

| Do | What changes | Ref | Effort |
|---|---|---|---|
| Score bug v2 | Logos, timeouts, state slides, one font | 04 A1 | S-M |
| Replay package | Angle by play type, speed ramp, wipes | 04 A2 | M |
| Between-play shot director with memory | Whistle-to-snap looks like television | 04 A3 | M |
| Broadcast-style mix | Excited calls louder; the roar rises on touchdowns | 05 A3 | S |
| Voice every live call | No silent captions | 05 A5 | S-M |
| Booth memory across games | Game ten does not repeat game nine | 05 A6 | S |
| Sim-truth stat tags | Real separation, air yards, catch probability on replays | 04 A6 | S |
| Visible rain and snow | Weather reads on camera | 03 A2 | M |
| Uniforms and turf that change over the game | Dirt, stains, wetness, scuffed field | 03 A5, A6 | M each |
| Field wall and sideline dressing | No flat blue wall in the TV frame | 03 A4 | S-M |
| Pregame open | If the capture confirms the empty field | 04 A5 | M |
| Pace setting and one skip rule | Full / Standard / Quick | 04 A8 | S |
| Anti-aliasing A/B | Fixes leg and ball trails; may free 1-3 ms | 03 A8 | S-M |

### Phase 4: depth (ongoing)

| Do | Ref | Effort |
|---|---|---|
| Two-man run blocking from the 45 unused pair captures | 01 #4 | M |
| Backs read blocks; gap integrity and force/contain | 02 A7, A8 | M, M-L |
| Adaptive CPU play calling and team identities | 02 A10 | M |
| Coach suggestions and a call timer on the play-call screen | 06 #9 | M |
| Halftime and postgame show from the game's own highlights | 04 A10 | M-L |
| Booth-synced graphics; referee calls with numbers | 04 A9, 05 A11 | M, S |
| Offline-authored template banks and analyst exchanges | 05 A7 | M |
| Substitutes jog on and off | 04 A11 | M |
| Moving sun and shadow line; helmets; breath vapor | 03 A7, A9, A10 | S-M |
| Telemetry without frames and a per-clip defect list; overnight matrix | 07 A6, A8 | M, S |
| Cooked Development build for honest perf numbers | 07 A9 | M-L |
| Signature styles for ball carriers, Superstars and X-Factors | 01 #8, #10 | S |
| Animated upper-deck crowd | 03 A13 | L |
| Match coverage rules; defensive-line stunts (low confidence) | 02 A11, A12 | L, M |

## Rejected across the reports

Each report has its own list with reasons. The ones worth remembering:
- **A pre-impact tackle stage.** Tackle captures start a median 0.03 s before contact; there is no approach footage (01).
- **Learned or neural animation models.** They need capture volume and training the project does not have (01).
- **Learning-based test agents before scripted ones.** EA's own work layers them on top of scripted bots (07).
- **A virtual host for the halftime show.** New art and voice for little gain; highlights carry it (04).
- **TTS models with non-permissive licences** (05).
