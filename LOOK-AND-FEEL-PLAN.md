# TAG Gridiron look and feel plan (draft 2026-11-08)

Goal: use everything learned from D:\MaddenRef to make TAG Gridiron look and feel like a top tier football game, while every shipped asset stays ours or licensed to us.

## 1. Ground rule: MaddenRef is a measuring stick, not content

MaddenRef holds Madden NFL 19's own art: 12,182 decoded player clips, bodies, 185 helmet and gear meshes, heads, coaches, the 49ers 2018 uniform set and stadium pieces. Shipping any of it, or anything derived from it, would build TAG Gridiron out of EA's art. That rules out:
- MaddenRef meshes, textures and materials built from its presets in game content.
- Madden clips, including the retargeted `A_<clip>_UEFN` copies, driving the Athlete.
- Tracing or fitting our meshes to theirs, sampling their textures, or keying our animation from their curves.

What MaddenRef is good for, and what this plan uses it for:
- **Side by side comparison.** The same camera and pose in the reference project and in our game, so we can see where ours falls short.
- **Aggregate numbers.** Proportions, timing distributions and material value ranges, used as targets next to real world references.
- **Coverage checklist.** The clip taxonomy tells us which situations a football game has to show and how much variety it takes.

Reasons, briefly:
- The README and `docs/ANIMATION_SPEC.md` promise original, freely licensed assets.
- The install's provenance is unresolved (`codex.cfg` and `OrangeEmu64.dll`).
- TAG is a public brand, so builds labelled private will not stay private.

**Real NFL teams and players.** These need NFL and NFLPA licences, and published Madden ratings are EA's compiled data. Recommendation: ship the fictional league. Rate players from open data and our own measurables model. Add a local roster editor and import, so a player can rename teams on their own machine. Many independent sports games take this route.

## Status (2026-11-09)
- **Phase 0 done.**
  - The guardrails from HEAD are restored: `DefaultGame.ini` NeverCook, the audit failing on outside references, and the retarget and extracted README text.
  - `/Game/MaddenRef/Retargeted` is deleted, with hashes in `logs/retargeted_removal_2026-11-08.txt`. It can be regenerated from the D data.
  - Gate passed:
    - Audit: 1,369 packages, no outside referencers.
    - `git grep MaddenRef` over Source finds nothing.
    - A development cook had 0 errors, and its cooked AssetRegistry has no MaddenRef entries (`logs/cook_phase0.log`).
  - Not done: the editor-only plugin move. It would change every package path the pipeline and the D project share, and NeverCook plus the audit already guarantee nothing ships.
- **Phase 1 done.**
  - `tools/look/look_targets.py` writes `docs/LOOK_TARGETS.md` and `docs/look_targets.json`: timing by category, the interaction histogram, gait and stance aggregates. These are numbers only.
  - `Tools/Art/compare_shots.py` plus the generic `-TagShotAt` flag (`TAGGridiron.cpp`) render TAG and MaddenRef shots into `Saved/Compare/` (local only).
- **Phase 2 items 1 and 2 done (helmet and facemask).**
  - `Tools/Art/gear_build.py` builds original procedural meshes in `/Game/TAG/Gear`: a helmet with a stripe slot, ear holes, jaw flaps and a 1.2 cm wall, plus Open, Std and Cage facemasks chosen by position.
  - `Athlete.cpp` uses them, with the primitive fallback kept, and `-TagNoGear` switches to the fallback for A/B checks.
- **Bug fixed on the way.** A recurring crash on throws and falls: `FAthleteAssets` held clips in a plain static struct, so garbage collection freed unplayed clips at about 60 s. `LoadAsset` now roots them. Three soak runs passed with no crashes or ensures.
- **Not doing.** Perfect matches of Madden animations (retargeted, re-keyed or hand-keyed to their curves). That is copying EA's mocap. Phase 3 instead holds our clips to the timing ranges in `docs/LOOK_TARGETS.md` and to side by side review.
- **Fred's decisions (2026-11-09).**
  - Install Blender.
  - No bought mocap and no own capture.
  - Build a texture pack.
  - Add shoulder pads.
  - Five body types.
  - Recreate the MaddenRef animations "as closely as possible". Declined: any route to close copies of their clips copies EA's work. Phase 3 uses free licensed motion (CMU, Mixamo, GASP), keyframing in Blender, and procedural layers, held to the coverage list and timing ranges in `docs/LOOK_TARGETS.md`.
- **Done 2026-11-09.**
  - Blender 5.2.3 LTS is installed and runs headless.
  - Shoulder pads: `SM_TAG_ShoulderPads` (a yoke plus caps from `gear_build.py`) replaces the sphere pads.
  - Uniform texture pack (`Tools/Art/gen_uniform_textures.py`):
    - A digit atlas in the OFL Graduate font (`RawArt/Fonts`), plus jersey mesh knit and twill normal maps.
    - `M_TagMHUniform` projects the numbers on the chest and back and applies the fabric normals.
    - The floating text numbers are hidden on MetaHuman athletes.
  - Five body types:
    - `mh_create.py` adds TAG_DLine and TAG_Lean and builds only missing bodies. It needs the open editor with Epic sign-in, so Fred runs it.
    - `Athlete.cpp` supports five archetypes and falls back to the old three until the new bodies exist.
  - `-TagLineup` look-dev mode: six athletes, front and back shots.
- **Waiting on Fred.**
  - Run `mh_create.py` in the editor.
  - Add the MetaHuman Crowd Sample (Fab, Epic, free) to a project from the Epic Games Launcher.
- **Phase 3 started 2026-11-09: keyframe animation without mocap. Verified in game.**
  - `Tools/Anim/tag_anim.py` is a Blender library:
    - Key poses on the UEFN rig, with spine distribution, clavicles and head look-at.
    - IK through helper chains, because UE bone tails are not on the joints. Helpers are deleted before export.
    - Bake, measurement gates (heights, foot slide, IK miss) and preview renders.
  - `clips.py` has six original clips: three point stance, two point stance, shotgun stance, QB throw, lineman get-off and TD celebration.
  - `Tools/Art/import_tag_anims.py` imports them to `/Game/TAG/Anim/Authored`. The Blender to Unreal round trip is exact (0 mm).
  - `Athlete.cpp` plays them:
    - Stances before the snap, phase offset per player.
    - Get-off at the snap for OL and DL.
    - The keyed throw from its stride frame, so release meets the sim's windup.
    - The keyed celebration.
  - `TagRetargetAnimInstance` crossfades every clip change over 0.2 s, so transitions no longer pop.
  - `SM_TAG_Football` replaces the sphere ball.
  - Verified: `-TagLineup` shots (stances, celebration) and full auto watch games with 0 crashes and 0 ensures. The keyed throw and stances appear in live play.
- **Second batch (2026-11-09), verified in two full games with 0 crashes and 0 ensures.**
  - Keyed wrap tackle: sink, explode, wrap, drive, then ragdoll on "down". It replaces the GASP jump-and-roll.
  - Jukes left and right, the right a programmatic mirror, chosen from the carrier's lateral velocity.
  - QB kneel, for victory formation.
  - Ten keyed clips in total, all with an exact round trip.
  - The close-up camera orbits around any athlete blocking the shot, which fixes the catch camera.
- **Third batch (2026-11-09).**
  - Tackle variants: high (chest wrap), low (shoot the thighs) and diving (full extension for the sim's "dive", replacing the GASP jump-roll).
  - Variants rotate game wide, so simultaneous gang tacklers and consecutive plays never show the same one.
  - Jukes, kneel and tackle variants are verified in the `-TagLineup` action phase.
  - 13 keyed clips, all with an exact round trip. Full games ran with 0 crashes and 0 ensures.
- **Fourth batch (2026-11-09).**
  - Keyed block loops:
    - Engaged: hands inside on the chest plate, low hips, choppy feet.
    - Pass set: balanced and wide, hands ready, bounce and kick slide.
  - They play for near-stationary blockers, with a per-player phase. Moving blockers keep the crouch blend space.
  - Fixed the last transition that popped: switching into locomotion now crossfades too.
  - 15 keyed clips. Two full games ran with 0 crashes and 0 ensures; both block loops appear in live play.
- **Fifth batch (2026-11-09).**
  - Shotgun snap catch, which starts from the exact shotgun pose.
  - QB handoff with a carry-out fake.
  - A generic keyed one-shot on the athlete (get-off, snap catch, handoff) that crossfades back to the live state.
  - The director fires one-shots from new sim events ("handoff").
  - One full game fired 9 snap catches, 81 get-offs and 3 handoffs with 0 crashes and 0 ensures. Jukes also appear in live play.
  - 17 keyed clips.
- **Sixth batch (2026-11-09).**
  - Get ups: keyed GetUp_FaceDown and GetUp_FaceUp, 19 keyed clips in total.
  - 0.6 s after a ragdoll, the athlete reads which way the body landed (spine and shoulder line) and its heading.
  - The actor is placed where the body lies. The world-space ragdoll pose is snapshotted into the retarget instance and crossfaded (0.35 s) into the matching get up. Afterwards the offset to the sim position eases out.
  - One full game fired 8 get ups (both orientations) with 0 crashes and 0 ensures. The get-up event shot shows a player rising from his back after the play.
- **Seventh batch (2026-11-09).**
  - Pass rush sheds: swim (chop, arm over, hips through) and rip (dip, uppercut under, flatten).
  - They play when a DE, DT or LB goes from "block" straight into a run, alternating game wide.
  - One full game: 30 sheds (15 each) with 0 crashes and 0 ensures. 21 keyed clips.
- **MetaHuman Crowd Sample (2026-11-09), in progress.**
  - Installed by Fab as an engine Marketplace plugin. The build tool cannot resolve it there, so `Plugins/MetaHumanCrowdContent` is a directory junction to the engine install (git ignored, nothing copied).
  - `MetaHumanCrowd` and `MetaHumanCrowdContent` are enabled in the uproject.
  - Spectator clips on the MetaHuman skeleton (`Tools/Art/crowd_retarget.py`): our keyed `Fan_Cheer_Loop` and the GASP idle, retargeted with `RTG_TAG_UEFN_to_MH`.
  - `AStadium::BuildHeroCrowd` places fans in the three lowest sideline rows and switches them between idle and cheer with Hype.
  - Finding: the sample's merged crowd meshes are bare bodies. Clothes, heads and grooms are assembled per MetaHuman Instance by the Mass representation subsystem (`UMetaHumanCrowdAppearanceProvider`). Placed directly they render grey and undressed, and they also sat too low on the deck.
  - **Done through the Mass appearance pipeline.**
    - `DA_TAG_FanConfig` (`Tools/Art/crowd_fan_config.py`) is the sample's config without walking or orientation traits.
    - `UTagSeatSpawnGenerator` and `ATagFanSpawner` (`Render/TagFanSpawner.*`) spawn one Mass entity per seat: 485 fans in the three lowest sideline rows between the 16s, facing the field.
    - The sample's appearance pipeline dresses each fan from its MetaHuman Instances (varied bodies, heads, outfits).
    - On by default. `-TagNoHeroCrowd` turns it off.
    - Cost: full game at 1600x900 drops from 175 to about 100 fps. 0 crashes, 0 ensures, 0 Mass errors.
  - Crowd reaction:
    - At stadium distances the fans are instanced meshes drawn by `MassVisualizer` with the sample's baked idle. No per-fan actors exist, so swapping the anim blueprint does not apply.
    - Instead, `ATagFanSpawner::Animate` makes fans jump and bounce on their seats with Hype (per-fan rhythm, up to ~26 cm on a touchdown) through each entity's transform fragment, then settles them back.
    - Verified: two hyped frames 0.2 s apart differ across the crowd. About 93 fps, 0 errors.
    - Our keyed cheer is also retargeted to the crowd actor skeleton (`A_CrowdActor_Cheer`). `UpdateMassFans` swaps it onto any close-up high-res crowd actors.
  - **Distant fans cheer (2026-11-09, rebuilt).** `Tools/Art/crowd_rebuild.py`:
    - Builds `AnimConfig_TAG`: the sample's idle and walk plus our keyed cheer as baked sequence 2.
    - Builds `CrowdCollection_TAG` through Epic's crowd pipeline (`UTagEditorLib::CrowdBuildCollection`, a headless build that pumps the engine and collects garbage; ~8 min).
    - Re-points 255 instance copies at it and switches `DA_TAG_FanConfig` to them. Epic's plugin content is untouched.
    - Runtime: `UTagFanCheerProcessor` runs after the plugin's idle/walk processor. It moves a hype-scaled share of distant fans to the cheer, each on its own threshold.
    - Verified: lineup crowd shot shows rows of fans with arms up; full game 114 fps, 0 errors.
    - Memory: the build needs ~40 GB of commit. Run it with the asset-compile cap in `setup_assets.bat`.
- **Motion matching (2026-11-09, approved; built and on by default, `-TagNoMM` for the blend spaces).**
  - All GASP PoseSearch data was already in the project: Dense databases, schemas, normalization sets. Only GASP's `/Game/Blueprints` (its AnimBP and chooser enums) is missing, and we don't need it. Database choice by gait and state happens in C++ and mirrors GASP's chooser.
  - `Render/TagMotionMatchInstance`:
    - The hidden source body's anim instance subclasses `UAnimSingleNodeInstance`, so every keyed clip and blend space plays as before.
    - It adds a graph without an Anim Blueprint: PoseHistory → switch → { clip | OffsetRootBone → MotionMatching }.
    - Pose history keeps recording through keyed clips.
  - `AAthlete::DriveMotionMatching`:
    - Trajectory: 1 s of recorded history plus 1.6 s of prediction, built from the body's measured motion. The sim's velocity goes stale at dead balls.
    - Gait hysteresis switches walk/run/sprint databases. Databases are offered by state, like GASP's chooser: starts + loops for the first 0.6 s of movement, pivots + spins + loops when the predicted path turns more than ~45° within 0.5 s, loops only otherwise, stops + idles when stopping.
    - A direction-aware speed multiplier covers players faster than the capture: GASP clips run 7 m/s forward, 3.5 sideways, 3 back.
    - Play rate is capped at 1.3× (`-TagMMRateCap=`), so the 7 m/s sprint loop covers 9.1 m/s without foot slide. Stable on the course once start and pivot databases were offered by state; full game 115 fps, 0 errors.
  - Indexes build in the background through the DDC (about 40–60 s after start in uncooked runs). Players stay on blend spaces until they are ready.
  - Look-dev course: `-TagMoveTest`.
    - Three routes, each filmed by a tracking side camera with a screenshot every 0.3 s: sprint + 90° cut + stop, curve + stop, backpedal + break.
    - `Tools/Art/contact_sheet.py` tiles the shots. Saved results are in `Saved/MoveTest/`.
    - Tools: `Tools/Art/mm_inspect.py` and `mm_speeds.py`.
  - **How it got there.** The first version looked worse than the blend spaces on the course: straight sprints churned between start and turn clip fragments and read hunched. Causes, all fixed:
    - Start and pivot databases were offered on every frame. Offered all the time, their clips beat the loops on straight runs. This was the main cause; the fix is the state split above.
    - Stale sim velocity at dead balls read as "standing, about to sprint". The trajectory now uses measured motion.
    - Start clips outlived their database. Database changes now interrupt the continuing pose.
    - A game-thread read of the blend stack crashed. The clip name is now copied in `PostUpdate`.
    - Turn-in-place fed back through the root offset (left/right flip-flop). It is out of the standing set.
  - **Result on the course.**
    - Backpedal: a real athletic backpedal with bent knees, against the blend space's upright walk backwards.
    - Plant-and-break, starts, stops and the curve all read better.
    - Straight sprint: a driving acceleration stride, against the blend space's longer upright stride. About even.
    - Full game: loops and transitions dominate, starts ~15%, 115 fps (118 without), 0 crashes, ensures or Mass errors.
  - **Crouched line footwork (linemen under 2.6 m/s, moving blockers)** is on motion matching too, with GASP's Dense crouch databases (walk loops, starts, pivots, stops, idles) and the same state split.
    - On the `-TagMoveTest` lineman route (kick-slide back and out, anchor, drive, settle), the blend space sinks into a deep static squat at the anchor. Motion matching holds an athletic set with a wide base and active hands.
  - Searches are throttled to every 66 ms (`-TagMMThrottle=`). Searching every frame for all 22 players cost ~25 fps once the linemen joined. With the throttle a full game runs at 119 fps, 0 errors. The receiver's cut now plays the sprint 90° plant-turn, and the curve holds the sprint loop.
  - **Football loop databases** (`Tools/Art/mm_build_dbs.py`, `UTagEditorLib::Database*`, in `setup_assets.bat`) live in `/Game/TAG/Anim/MM/`. `LoadMotionMatching` falls back to Epic's when they're missing.
    - `PSD_TAG_Sprint_Loops`: Epic's sprint loops without the four walk/run-to-sprint transitions (they kept a top-speed runner in an acceleration lean), plus the 20° curve loops.
    - `PSD_TAG_Run_Loops`: run loops plus the six run arcs.
    - On the course: a straight sprint is pure `Sprint_Loop_F` (upright, full stride, airborne phases, arm drive). The 90° cut is one `Sprint_Turn_L_090`. The curve is `Sprint_Loop_F_R_20` (curving stride). Full game: 120 fps, 0 errors, sprint loops ~50% of all frames.
  - **Next to try:**
    - Re-run `-TagMoveTest` against `-TagMoveTest -TagNoMM` after each change.
  - Packaged builds: nodes driven from C++ read folded properties only with editor data. Shipping motion matching needs an AnimBP host for the nodes.
- **Coverage batch 2 (2026-11-09): 35 more hand-keyed clips, 57 total** (`Tools/Anim/clips_more.py`, built by `Tools/Anim/build_all.sh`, all round-trip 0 mm).
  - Catches: high (diamond), chest, low (pinkies, hip hinge), over the shoulder (in stride), sideline toe tap L/R. Triggered on the sim's catch event by ball height, receiver speed, ball direction and sideline distance.
  - Ball carrier: spin L/R, stiff arm L/R, truck, hurdle. The sim's juke picks the move from the runner's ratings: elusive backs juke and spin, power backs truck and stiff arm, agile ones occasionally hurdle.
  - Pass rush: bull, club, spin L/R join swim and rip (six-move rotation).
  - Tackles: shoulder hit, arm tackle, gang-tackle assist join wrap, high and low (six).
  - Stances by position: DT four point, RB, DB, LB, K/P. Kicks: kickoff, field goal, punt, timed so foot contact meets the ball. Celebrations: six, one per player. Get-up: roll-out joins face down / face up. Post-play idles: hands on hips, hands on knees when winded.
  - Verified in a full game: rush moves, catches and get-ups fire; 112 fps, 0 errors.
  - **Batch 3 closes the gaps (5 clips, 62 keyed total):** under-center stance and snap (QB within 1.7 yd of the center), pitch (handoff to a back more than 2.5 yd away, the sim's stretch-run toss), layout dive at an incompletion just out of reach (its forward travel handed to the actor), slow to get up holding the ribs (one in five face-down get-ups; the injury category).
  - **Coverage against MaddenRef's 37 situation categories** (aggregate counts only): all 37 now have TAG motion (keyed, GASP motion matching or ragdoll).
  - **Paired tackles:** the sim resolves a tackle in one tick and teleports the carrier forward by the yards after contact, which showed as the carrier jumping ahead and going limp while the tackler wrapped air. Now the carrier is held where the hit lands, plays a keyed hit reaction (`Tackled_Hit`), is carried with the pile onto the sim's spot, then ragdolls along the pile's direction; each tackler (gang tacklers too) eases to wrap distance of the carrier's body and squares up (`AAthlete::BeginTackleOn`, director pairing pass). Verified: tacklers close from ~120 cm to the 62 cm wrap, carriers held 30-70 cm behind the sim spot during the hit; 103 fps, 0 errors. Pileups are still each player animating alone.
  - **Paired blocks:** locked-up linemen (the sim's engagement) close to hand fighting distance, each taking half the correction so the pair meets in the middle, and square their pads to each other; the correction eases off when they disengage. Verified: engaged pairs hold 74-82 cm chest to chest (target 70); clear-day games 95-103 fps, 0 errors.
  - **Count:** 62 keyed clips plus the GASP locomotion set used by motion matching, against 12,182 MaddenRef clips. The count can't reach parity by hand keying. The target is coverage parity: every situation has motion.
- **Cameras (2026-11-09).** `AMatchDirector::UpdateCamera` is now a broadcast operator: critically damped dolly and pan (no linear-lerp drag), the frame leads the ball by its velocity, the lens zooms to the spread of players around the ball (wider when the ball is in the air), handheld drift on the field-level cameras. Post-play cut-ins (`StartCutIn`): a telephoto (85 mm) close shot of the player who made the play after a touchdown, takeaway, sack or 20+ yard gain, with the post-play beat held long enough to see it. `-TagCutIns` reviews them under automation.
- **Night lighting (2026-11-09).** Twelve 5700 K LED light banks on the stand roofs and over the end zones replace four corner spots. Six cast shadows (a fan of crisp shadows per player); helmets catch several specular glints; the sky goes near black; exposure is pinned (`-TagNightEV=`, default 4.5) so the camera never pumps. 102 fps.
- **Player faces (2026-11-09).** `Render/TagFaceAnimInstance`: each MetaHuman face copies the body's head and neck and drives the face rig's expression controls (`CTRL_expressions_*`); Epic's RigLogic post-process (`ABP_Face_PostProcess`, already on the face meshes) turns them into facial joints. Before this, leader-pose following never evaluated the face rig and every face was frozen in neutral. `AAthlete::UpdateFace`: blinks every 2-6 s (some doubles, never in unison), effort grimace from sprint speed and contact (brows down, squint, nose wrinkle, stretched mouth), breathing that deepens with exertion, pre-snap focus (brows set, lips pressed), shouting smiles in celebrations. Lineup face closeups (`tag_lineup_face_focus/celebrate.png`) verify it. Full game 101 fps (112 before), 0 errors.
- **Next.**
  - Motion matching: stride warping for the rare speeds above the 1.3× cap; football-specific motion beyond what the GASP capture covers stays keyed (Tools/Anim).

## 2. Where the game stands (reviewed 2026-11-08)

| Area | Today | Gap against MaddenRef |
| --- | --- | --- |
| Bodies | Three MetaHuman archetypes (Lineman, Skill, Big) driven by the hidden UEFN mannequin through `RTG_TAG_UEFN_to_MH` | Madden uses four body families per position plus per player morphs. Our silhouettes are generic, and padded bulk is missing. |
| Gear | Helmet is a scaled `/Engine/BasicShapes/Sphere`. Facemask is cylinders. Pads and cleats are spheres. Numbers are floating `UTextRenderComponent` (`Render/Athlete.cpp:250-336`). | Biggest single visual gap. It reads as a prototype at any distance. |
| Uniform | `M_TagMHUniform` paints flat colours on the body | No fabric, trim, stitched numbers, name bar, sweat or dirt. No garment folds over pads. |
| Animation | GASP locomotion blend spaces, crouch line play, procedural L2 layer (throw, catch, block, tackle, stance, look) in `TagRetargetAnimInstance`, ragdoll downs | No football specific mocap. Interactions (about 2,800 multi player groups in Madden) are procedural reaches only. |
| Stadium | Procedural cubes, crowd texture or instanced cubes, procedural turf, Lumen with sun, sky, fog and spot rigs (`Render/Stadium.cpp`) | No architectural bowl, animated crowd, end zone art, scoreboard or field wear. |
| Presentation | Broadcast, sideline, skycam and player cameras with shake (`Game/MatchDirector`). Slate HUD with the default font (`UI/GridironOverlay.cpp`). | No replays, cut rules, branded score bug, team identity graphics or player intros. |

## 3. Phases

Effort figures are rough, for one developer working with Claude.

### Phase 0. Restore the guardrails (half a day)
1. Revert the uncommitted 2026-11-08 direction change. That means restoring `+DirectoriesToNeverCook=(Path="/Game/MaddenRef")` in `Config/DefaultGame.ini` and restoring the "outside references fail" check in `Tools/Art/maddenref_audit.py`.
2. Make MaddenRef an editor only part of TAGGridiron, so it sits next to the game while we work but never ships:
   - Move `Content/MaddenRef` (624 MB: Players 363, Coaches 91, Animations 80, Stadiums 58, Retargeted 32, Maps, Materials, Skeleton) into a project plugin `Plugins/MaddenRefReference` with `"Type": "Editor"`, `"ExplicitlyLoaded": false` and its content folder in `DirectoriesToNeverCook`. It stays git ignored. The Animations/Full library stays in the D project.
   - Add an editor utility, `Tools/Art/ref_ghost.py`, that drops a Madden reference actor beside any TAG actor in an open level: same pose, camera and lighting, set to `bIsEditorOnlyActor`. That gives in place comparison without switching projects.
   - Delete `Retargeted` rather than move it, because it is game ready derived content.
3. Restore `README.md`, `PLAN.md`, `FINDINGS.md`, `docs/ANIMATION_SPEC.md` and `tools/madden/README.md`. They are deleted in the working tree but still in HEAD. **Fred to confirm whether that deletion was intentional.**
4. Gate:
   - The audit passes in both projects.
   - `git grep MaddenRef -- Unreal/TAGGridiron/Source` finds nothing.
   - The pak listing (`UnrealPak -List`) of a development cook contains no MaddenRef package.

### Phase 1. Reference targets (1 to 2 days)
Output: `docs/LOOK_TARGETS.md` plus `docs/look_targets.json`. These are numbers and checklists only, no Madden data files. Real world sources come first, MaddenRef second as the game feel comparison.
- **Bodies by position group** (QB, RB, WR, TE, OL, DL, LB, DB, K/P). Height and weight from NFL Combine style public measurables. Silhouette ratios (padded shoulder width to height, hip and thigh to shoulder) as aggregates measured from the Madden body families (d, h, m, t) in the reference project. These set how many MetaHuman archetypes we need and their proportions.
- **Gear silhouette.** Helmet outer size relative to head, facemask stand off, pad width beyond the shoulder, glove and cleat bulk. Sources are manufacturer specs and photos, compared against reference renders.
- **Materials.** Roughness and specular ranges per surface (jersey mesh, pants, glossy versus matte helmet shell, skin, turf). Findings already give jersey RSM R mean 0.31 and pants 0.39, with the roughness or smoothness meaning unproven. Settle that by eye in Phase 0's comparison renders.
- **Motion timing.** Medians and 10th to 90th percentiles of `duration_s` per category from `animation_inventory.csv`, aggregate only. Examples: stance hold, three, five and seven step drops, release time, tackle impact to ground, get up. Cross check against broadcast footage. These become timing targets for our own clips and for the procedural layer.
- **Coverage checklist.** Keyword taxonomy of the 12,182 clip names (rough):

  | Category | Clips |
  | --- | --- |
  | Tackle and hit | ~2,570 |
  | Catch | ~1,900 |
  | Block and line play | ~1,200 |
  | Ball carrier moves | ~1,020 |
  | QB actions | ~965 |
  | Locomotion | ~940 |
  | Celebration | ~670 |
  | Stance and pre snap | ~500 |
  | Falls and get ups | ~390 |
  | Kicking | ~75 |
  | Injury | ~50 |
  | Uncategorised (mostly post play and presentation) | ~1,850 |

  2,786 groups have two or more participants (up to 9). Lesson: contact and interactions are where variety matters, so our animation budget goes there.
- **Comparison tool.** `Tools/Art/compare_shots.py` renders a fixed set of shots: turntable at 3 m, broadcast framing, stance, line of scrimmage at 20 m. It renders the same set in both projects and writes pairs to `Saved/Compare/`, which is local only and never committed. We use it to look, never as a source.

### Phase 2. Players and gear (1 to 2 weeks). Biggest win.
1. **Helmet, shoulder pads, cleats, gloves.** These are our own meshes. Sourcing in order of preference:
   - Buy a Fab or marketplace American football gear pack whose licence allows shipping, then restyle it so it is ours.
   - Model in Blender, which is not installed yet, from real gear photos and the Phase 1 targets.
   - The helmet shell can also be parametric through Geometry Script (Modeling Tools is already enabled).
2. **Facemasks.** Procedural tube sweeps along splines in Geometry Script. Six to eight cage styles give cheap variety. Each player's cage follows their position.
3. **Uniform system.** A new master material `M_TagUniform` with:
   - A fabric base: mesh knit detail normal from CC0 sources (ambientCG, Poly Haven) or authored by us.
   - Team colour masks for body, sleeve stripes, collar, pant stripe and socks.
   - Numbers and nameplate drawn from a number atlas rendered from an OFL licensed font, replacing the floating text components.
   - Helmet decal slot for fictional team logos.
   - Sweat, grass and dirt masks that build up from sim events (spec L5).
4. **Garments.** Jersey and pants as skinned garment meshes over a pad volume on each MetaHuman body, so the padded silhouette and folds read at broadcast distance. Built in Marvelous Designer or Blender, skinned to the MetaHuman body.
5. **Bodies.** Go from 3 to 5 archetypes (OL, DL/TE, LB/RB, WR/DB, QB/K) matched to Phase 1. Each player gets a scale within its archetype from the sim's height and weight. Faces are MetaHuman presets with a stable seed per player, for a varied, fictional roster.
6. **Gate.**
   - Close up and broadcast shots from `compare_shots.py`.
   - Every silhouette ratio within 5 percent of target.
   - No `/Engine/BasicShapes` left on the Athlete.
   - Fred signs off by eye.

### Phase 3. Animation (staged, 2 to 6 weeks)
1. **Motion matching** over the full GASP database with PoseSearch (already enabled). This is the L1 "next" in the spec. It replaces the blend spaces for runners, so cuts and stops read like football.
2. **Football mocap from licensed or our own sources.** Choose one or both:
   - Buy game licensed mocap packs (sports and football packs on Fab and from mocap vendors).
   - Capture our own with markerless capture (Move.ai, Rokoko Vision) or a suit. The capture sessions double as TAG video content.

   Priority list, taken from the coverage checklist and the sim's states:
   - Three and two point stances, QB under centre and shotgun.
   - Three, five and seven step drops; throws set and on the run; handoff, pitch and fake.
   - Catches: high, low, over the shoulder, diving, sideline toe tap.
   - Block: engage, drive, shed.
   - Ball carrier: juke, spin, stiff arm, truck.
   - Tackle in three stages (start, impact, fall); get ups; a few celebrations.
3. **Interactions.**
   - Tackles and blocks are paired clips aligned to the sim's contact point with Motion Warping (already enabled).
   - A physical animation component handles stumble and recover (the L3 "next").
   - The sim decides the outcome. Animation only expresses it, as the spec requires.
4. **Gate.** The spec's acceptance checks, plus:
   - Foot slide under 5 cm per step.
   - Paired contact alignment within 10 cm.
   - The same player does not repeat the same clip within 5 reps.
   - Timing medians within the Phase 1 ranges.

### Phase 4. Stadium and field (1 to 2 weeks)
1. **Modular bowl.** Lower, club and upper decks, a video board, light towers and tunnels. Built from a licensed kit or our own blockout. Fictional stadium designs per team, with the procedural layout in `Stadium.cpp` kept as the placement driver.
2. **Animated crowd.** Instanced skeletal cards or vertex animated meshes in team colours, reacting to sim events (big play, score, third down).
3. **Field.**
   - Turf wear and divot masks that build up over the game; snow and rain wetness.
   - End zone and midfield art for the fictional league.
   - First down line and line of scrimmage drawn as a broadcast style overlay.
4. **Lighting.** Day, dusk, night and dome presets tuned to the Phase 1 material targets and broadcast footage.

### Phase 5. Presentation and UI (1 week)
1. **Broadcast camera package.**
   - Lens and height presets from TV footage.
   - Cut rules: pre snap wide, follow, end of play close up.
   - Instant replay from recorded sim state, so it is exact and costs no animation.
2. **TAG identity.**
   - Custom score bug and play call screens with an OFL font.
   - Logos and colour sets for the 32 fictional teams.
   - Player intro cards.
3. **Audio feel.** Pads, whistle, crowd swell and PA from licensed SFX libraries.

## 4. Order and dependencies
Phase 0 comes before everything. Phase 1 comes before 2, 3 and 4. Phases 2 and 4 can run in parallel. Phase 3 step 1 can start right after Phase 0. Phase 5 can run any time after Phase 0.

Suggested first two weeks: Phase 0, then Phase 1, then Phase 2 items 1 to 3 (helmet, facemask, uniform). Those remove the "prototype" read fastest.

## 5. What I need from Fred
1. Confirm the ground rule in section 1: MaddenRef stays a reference only project and the fictional league ships. That means reverting the 2026-11-08 direction change.
2. Was deleting README, PLAN, FINDINGS and ANIMATION_SPEC from the working tree intentional?
3. Asset budget: buy marketplace gear, stadium and mocap packs, or build them ourselves (install Blender)?
4. Mocap: buy packs, capture our own, or both?
