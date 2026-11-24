# 01. Animation and motion: how the best studios make players move, and what TAG Gridiron should do next

Researcher 1 of 7. Domain: locomotion, motion matching, transitions, contact animation.
Written 2026-11-23. Web search and page fetches were available. Several EA pages returned 404 or 403 to the fetcher. Where a claim comes only from a search-result summary and not from a page I read, it is marked **(snippet)**. Nothing below is from memory alone unless marked **(memory)**.

Code was read on branch `look-and-feel` as it stands today (uncommitted work included). Paths are relative to `Unreal/TAGGridiron/` unless they start at the repo root.

---

## 1. What the best studios do

Confidence key: **High** = the source states it directly. **Med** = the source states the feature and I infer the technique under it. **Low** = inference from indirect evidence.

### 1.1 EA Sports (Madden, College Football, EA FC)

| Technique / name | What the source states | What it most likely is underneath | Confidence |
|---|---|---|---|
| **BOOM Tech** (Madden 25) | Three parts: physics-based tackles, a "physics-driven ball carrier balance and recovery system", and the reloaded Hit Stick. It is built from **Driven Ragdoll** ("drives the character via physics in an effort to perform the maneuver"), **N-Teract** ("selects an appropriate animation to start playing based on the position of the player, where they're going, and the players around them"), and **PTM** ("selects new animations to play like a fall, stumble, or recovery depending on the physics-driven collision"; "the PTM reselection of animations can happen multiple times during an interaction"). Tackles run in three stages: pre-impact, impact, resolution. The goal is "preservation of momentum". [EA BOOM Tech](https://www.ea.com/news/boom-tech-ea-sports-madden-nfl-25) | N-Teract is a two-man interaction matcher: the same job as TAG's pair picker. Driven Ragdoll is a powered ragdoll tracking an animation target. PTM is feature-based re-matching of a mocap frame after physics has perturbed the pose. EA patent US9827496B1 (Fabio Zinno, priority 2015) describes exactly that loop: the ragdoll is driven toward a target pose, a collision stops it reaching the pose, features of the resulting pose are matched against the mocap library, and the matched frame becomes the next target. The same matching picks the start frame of a new clip, which avoids "the wrong foot planted" when switching gaits. [Patent US9827496](https://patents.google.com/patent/US9827496). The Madden pack TAG owns also contains `*_Physics_PoseLoop` captures (FrontSplat, BackSplat, SideSplat, Sit, TwoKnees, AllFours; found under `Content/TAG/AnimH/Pack/Misc` and `BallCarrier`). These look like pose targets for a driven ragdoll. | Stated: High. Patent as the mechanism: Med. PoseLoops as ragdoll targets: Low. |
| **FieldSENSE / Hit Everything** (Madden 23, 24) | "Fueled by animation branching technology". Hit the Pile, stand-up tackles, hitting blocks, mid-air knockouts, add-on blocks. "Branching WR releases and cuts, defensive back presses and counters". 360 Cuts for hard cuts on demand. [Xbox Wire](https://news.xbox.com/en-us/2022/06/27/introducing-fieldsense-in-madden-nfl-23/). Madden 24's "Hit Everything 2.0" added 1,700+ tackle animations and let blockers "branch in and out of double teams" **(snippet)** [EA Madden 24 gameplay](https://www.ea.com/news/madden-24-gameplay) | "Branching" most likely means interaction clips that can be left at authored or matched points into another clip, instead of playing to the end. A third man joining a tackle means re-matching a two-man interaction into a three-man one. No technical source was found. | Stated: High. Technique: Low. |
| **Madden 26 Gridiron Notes** | 500+ new animations and 40 QB "recipes". Nine ball holds. Three base arm slots (Over-Top, 3/4 High, 3/4 Low) assigned from each QB's real throw angle. Signature run cycles for three scrambling QBs; Madden 25 gave four star RBs signature run cycles. New locomotion captured for pre-snap motion, and "a complete new set of exclusive locomotion that only pass blockers use". 700+ new tackle animations. Physics-driven animation extended to more tackle types. Fewer "tackles triggering without the players making full contact". "Maintaining momentum after contact by reducing how frequently the system would look to trigger falling animations". Ball carriers had been "returning to normal locomotion too quickly", so the branch back now happens later. One shared speed tuning with College: "a 4.2 is a 4.2". [Madden 26 Gridiron Notes](https://www.ea.com/games/madden-nfl/madden-nfl-26/news/madden-26-gridiron-notes-gameplay-deep-dive) | Style is assigned per player and per role, not drawn at random: arm slot from real data, run cycles per star, and locomotion sets per job (pass blockers, players in motion). | High. |
| **College Football 25 / 26** | CFB 25 "introduced all-new specific run styles for the different archetypes" **(snippet)** [EA CFB 25 deep dive](https://www.ea.com/news/college-football-25-gameplay-deep-dive). CFB 26: "advanced animation systems ... leverage proprietary technology and machine learning" [EA CFB 26](https://www.ea.com/news/college-football-26-is-back). No locomotion technique is named. | Run style selected by player archetype. | Run styles: Med. ML claim: too vague to use. |
| **Next Gen Player Movement** (Madden 21 next-gen) | Movement "fueled by real athlete-data, route paths, and acceleration rates", with "thousands of animations" captured for it. Receivers run routes logged by NFL Next Gen Stats **(snippet)** [EA Gridiron Notes, Madden 21 NGS](https://ea.com/news/gridiron-notes-madden-nfl-21-next-gen-stats-deep-dive), [Xbox Wire, Madden 21 next gen](https://news.xbox.com/en-us/2020/12/04/next-generation-of-madden-nfl-21-on-xbox-series-xs/) | The movement model (speed and acceleration per player, route geometry) is fitted to tracking data, and the animation is chosen to follow that model. | Med. |
| **SAPIEN** (Madden 24, FC 24) | A new skeleton "built from the ground up" giving "more body definition and variation to physiques". Old animations were carried across. [EA 2024 tech roundup](https://www.ea.com/news/2024-ea-sports-latest-tech-innovations) | A rig with more bones and per-physique proportions, plus a retarget so one library plays on many bodies. | High for the claim. Internals unknown. |
| **ANT** | Historic: an animation framework shared by EA's Madden and FIFA teams, modular with genre plug-ins; Fight Night had its own branch [Copcomm](https://digital.copcomm.com/i/26447/15). Madden later moved onto Frostbite systems. | Not relevant to current technique. | Low (thin source). |
| **HyperMotion** (FIFA 22 to EA FC) | Full 11v11 matches captured with Xsens suits. 8.7M frames in FIFA 22, an "ML-Flow" algorithm that "writes new animations in real time", and stride adjustment on the approach to the ball [EA FIFA 22 HyperMotion](https://ea.com/games/fifa/fifa-22/hypermotion), [80.lv](https://80.lv/articles/ea-uses-machine-learning-for-animations-in-fifa-22). FC 24 "HyperMotion V" uses volumetric data from 180+ matches [Trusted Reviews](https://www.trustedreviews.com/explainer/what-is-hypermotion-v-4352934). | A learned model, trained on a very large capture set, that produces transitions and stride corrections between authored clips. EA research shows the family it comes from: Starke et al. *Local Motion Phases* (EA and Edinburgh, SIGGRAPH 2020; one-on-one basketball) [paper](https://www.ipab.inf.ed.ac.uk/cgvu/basketball.pdf), *DeepPhase* (SIGGRAPH 2022 best paper; applications include football dribbling and pose queries over large databases) [paper](https://i.cs.hku.hk/~taku/deepphase.pdf), and EA patent US11562523B1, "motion matching using local bone phases" (filed 2021; inventors include Starke; examples are basketball and wrestling) [patent](https://patents.google.com/patent/US11562523). | That EA uses learned and phase-based matching: Med. That a given shipped title uses exactly these: Low. |

### 1.2 Other studios

| Studio / technique | What the source states | Underneath / relevance | Confidence |
|---|---|---|---|
| **NBA 2K ProPLAY** (2K24, 2K25) | "Directly translate NBA footage into actual NBA 2K gameplay". About 9,000 animations, including 1,500 dribbles, 1,100 signature shots and 150 signature motion styles. Dribbling was "manually programmed, pre-scripted animation transitions" before and is now "one holistic, dynamic piece of motion". [Microsoft Game Dev](https://developer.microsoft.com/en-us/games/articles/2024/09/closer-look-at-nba-2k25-proplay-system/) | Video pose estimation plus cleanup feeding a motion library. No pipeline detail is public. | Claim: High. Technique: Low. |
| **NBA 2K motion styles** | Community testing: each motion style has height and attribute requirements and changes off-ball movement and speed; heavier builds lose the faster styles [Destructoid](https://www.destructoid.com/the-best-motion-styles-for-each-height-and-position-in-nba-2k25/), [2KLab](https://nba2klab.com/motion-styles) | Locomotion sets gated by body type. | Med (community sources). |
| **MLB The Show 23** | 5,000+ new gameplay animations; "players with a lower Fielding attribute will react slower to contact made off the bat" **(snippet)** [Gematsu](https://www.gematsu.com/2023/02/mlb-the-show-23-gameplay-features-detailed-game-sizzle-trailer) | Ratings choose which reaction clip family plays and when. | Med. |
| **Motion matching** (Ubisoft, For Honor) | Search the capture database for the best frame to continue from, against a pose plus a predicted trajectory [Clavet, GDC 2016](https://www.gdcvault.com/play/1022985/Motion-Matching-and-The-Road). Naughty Dog shipped it in The Last of Us Part II [Naughty Dog GDC 2021](https://www.naughtydog.com/blog/naughty_dog_at_gdc_2021) | Industry standard for locomotion. | High. |
| **Code-driven vs data-driven displacement** | The query trajectory is the simulation's future path, so prediction must use the same movement model the gameplay uses. Mismatched turn rates cause spin and slide. Foot locking is "difficult to make look good", so fix the selection first. [Holden](https://theorangeduck.com/page/code-vs-data-driven-displacement); spring prediction and inertialization formulas in [Spring-It-On](https://theorangeduck.com/page/spring-roll-call) | Directly relevant to TAG (section 2.2). | High. |
| **Learned motion matching** (La Forge 2020) | Replaces the database search with networks to cut memory; up to 30x compression per an interview [paper](https://www.theorangeduck.com/media/uploads/other_stuff/Learned_Motion_Matching.pdf), [La Forge](https://www.ubisoft.com/en-us/studio/laforge/news/3NWqwcMU9lMfumgfPAEfka/learned-motion-matching) | A memory optimisation, not a quality one. | High. |
| **PFNN / Neural State Machine** | Network weights are a function of locomotion phase [PFNN 2017](https://www.ipab.inf.ed.ac.uk/cgvu/phasefunction.pdf). NSM learns the factorisation, so it covers non-cyclic actions too [NSM 2019](https://www.ipab.inf.ed.ac.uk/cgvu/nsm.pdf) | Research. Needs training per dataset. | High. |
| **Inertialization** (Gears of War) | Transitions become a post-process: the offset at the switch decays while the velocity is kept, and the old clip is no longer evaluated. "60% cheaper" than blended transitions [Bollo, SIGGRAPH 2017](https://history.siggraph.org/wp-content/uploads/2022/09/2017-Talks-Bollo_High-Performance-Animation-in-Gears-of-War-4.pdf), [GDC 2018](https://gdcvault.com/play/1025165/Inertialization-High-Performance-Animation-Transitions) | Standard practice. | High. |
| **Dead blending** | Extrapolates the outgoing pose with per-axis decaying velocity, then crossfades. Looks better than inertialization between very different poses and damps motion slightly more. A UE node since 5.3 [Holden](https://theorangeduck.com/page/dead-blending-node-unreal-engine), [Epic API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FAnimNode_DeadBlending) | Directly relevant (actions 3 and 6). | High. |
| **Euphoria / Backbreaker** (NaturalMotion, 2010) | Tackles "simulated on the fly" with a motor-control model, with no canned tackle clips. Reviews were mixed (Metacritic mid-50s) and knocked-down players caused domino falls [Wikipedia](https://en.wikipedia.org/wiki/Backbreaker_(video_game)), [Euphoria](https://en.wikipedia.org/wiki/Euphoria_(software)) | A full active ragdoll. Physics variety did not make it read as football by itself. | High. |
| **DReCon / SuperTrack** (La Forge) | A physics character trained with reinforcement learning to track motion-matched kinematics [DReCon](https://theorangeduck.com/media/uploads/other_stuff/DReCon.pdf) | Research. Needs training infrastructure. | High. |

### 1.3 What Unreal Engine 5.8 offers, and how mature it is

Status comes from the installed engine's plugin descriptors (`C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\...\*.uplugin`, `IsBetaVersion` / `IsExperimentalVersion`) and source comments. This is first-hand.

| Feature | 5.8 status | Use for TAG |
|---|---|---|
| PoseSearch (motion matching), BlendStack, Chooser | Not flagged (production) | In use. |
| PoseSearch feature channels: Phase, Velocity, Heading, TimeToEvent, Curve, FilterCrashingLegs | Present in `PoseSearch/Public/PoseSearch/PoseSearchFeatureChannel_*.h` | Phase or foot-velocity channels could help foot-correct picks (action 9). |
| `UPoseSearchDatabase::BaseCostBias` (`PoseSearchDatabase.h:521`) | Present | Per-database preference, the basis of action 1. |
| PoseSearch interactions (`MotionMatchMulti`, `UPoseSearchInteractionAsset`) | Source comment: "Experimental, this feature might be removed without warning, not for production use" (`PoseSearchInteractionLibrary.h:41`). One frame of delay by design (line 51). | Rejected (section 4). |
| AnimationWarping (stride, orientation, foot placement nodes) | Not flagged | TAG has its own equivalents (section 2). |
| Dead Blending / Inertialization nodes | Engine runtime. Out of experimental in May 2024 per an Epic forum reply **(snippet)** [forum](https://forums.unrealengine.com/t/stability-of-the-dead-blending-node-in-animation-workflows/2670429) | Action 6. |
| MotionWarping | **Beta** | Built for montages with root motion on characters; TAG uses `AActor`s with its own follower. Not needed. |
| Physics Control | Not flagged | Possible later (section 4). |
| Mover, UAF (AnimNext), Learning Agents, MotionTrajectory | **Experimental** | Do not build on these. |
| ML Deformer | Not flagged | Not this domain; needs training data. |

The Motion Matching node can transition with its internal blend stack (MaxActiveBlends > 0) or by requesting inertialization from an Inertialization node above it [Epic API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/PoseSearch/FAnimNode_MotionMatching). The Game Animation Sample uses the blend stack, and its 5.8 update adds multi-character motion-matched interactions as a demo, "not a fully fledged interaction system" **(snippet)** [Epic blog](https://www.unrealengine.com/tech-blog/download-the-latest-game-animation-sample-project-now-updated-for-ue-5-8).

### 1.4 What this adds up to

The studios that look best share three habits:

1. **Selection follows the player.** Run styles come from archetype and body. Arm slots come from real data. Pass blockers have their own locomotion. Ratings change reaction clips. Stars get signature sets.
2. **The movement model and the animation query agree.** NGS-fitted speed and acceleration, routes as data, and a "4.2 is a 4.2" tuning shared across games.
3. **Contact is two-man and keeps momentum.** Interaction matching (N-Teract), re-selection after physics perturbs the pose (PTM), physics used sparingly, and late branching back to locomotion.

Learned and ML systems (HyperMotion, ProPLAY) are how EA and 2K *scale* these habits with capture budgets TAG does not have. The habits themselves do not need ML.

---

## 2. Where TAG Gridiron stands

### 2.1 What it already does well (verified in code)

- **The capture as captured.** A hidden source body plays the mocap on the pack's own skeleton. The visible pack body copies it bone for bone in component space (`Source/TAGGridiron/Render/TagRetargetAnimInstance.cpp:41-75`, `EvaluateCopy`). This removed a 114 mm double-retarget error (memory, `native-anim-fidelity.md`).
- **Motion matching graph in C++.** History collector → switch → OffsetRootBone → root tap → MotionMatching (`Render/TagMotionMatchInstance.cpp:89-99`). Transitions use the blend stack: BlendTime 0.3 s, no inertialization (constructor, lines 268-316). Searches are throttled to 66 ms. A 0.3 s minimum dwell holds a pick unless the move changes (lines 184-206). Continuing-pose bias is -0.3 (`Render/Athlete.cpp:1656`). A speed match scales play rate 0.75-1.25x from the clip's measured root speed (`TagMotionMatchInstance.cpp:125-154`). A misfitting clip forces a fresh search (`Athlete.cpp:2891-2915`). A pelvis-continuity patch covers the blend stack's first evaluations after a new pick (`TagMotionMatchInstance.cpp:8-46`).
- **Procedural layers in a fixed order** (`TagRetargetAnimInstance.cpp:165-180`): football layer, orientation warp (at most 40 degrees, eased), stride warp (0.7-1.35x), lean from measured acceleration, crossfade, impact springs, foot lock on the capture's own plant verdict with lifted re-steps, arm keep-out in piles, twist bones. Each layer has an A/B flag and a per-layer deviation column in telemetry.
- **Two-man contact from the pack.** Tackle pairs are picked by hit angle and by how far the pile would still have to slide (`Game/MatchDirector.cpp:910-989`). Falls follow root motion exactly. Gang tackles, strips, interceptions, play-action fakes, handoffs and FG/punt scenes all play multi-man captures.
- **Measurement.** Full-game telemetry has per-man foot, hand and head positions, `dev*`, `mm*` and `plf` columns (`Game/MatchDirector.cpp:2939-2940`). Tools exist for capture fidelity, plant truth, pops and tackles (`Tools/Realism/*.py`, `Tools/Anim/tackle_check.py`, `foot_skate.py`). The scorecard puts animation at 10.0 and movement at about 9.9 (memory, `tv-fidelity-goal.md`).

### 2.2 Where it falls short

The scorecard measures **fidelity to whichever capture plays**: slide, deviation, pops. It does not measure **whether the right capture plays** or **whether it plays on time**. Most remaining gaps are of that kind.

1. **Style is a random draw, not the player.** Each man's motion-matching set is `Player->id % 6` (`Athlete.cpp:2822`), chosen only to avoid clones. The pack's locomotion is named by body style: `MG_BIGMAN_*`, `MG_NonBC_BigGuy_*`, `MG_Loco_Heavy_*`, `MG_NonBC_Bruiser_*`, `MG_LB_Med_*`, `MG_NonBC_Agile_*`, `MG_NonBC_WR_*`, `MG_NonBC_PrePlay_DB_*` (counted in `RawArt/Anim/PackHero/Locomotion`). Every run set mixes them (`Saved/mm_run_sets_hero.json`: each of S1-S6 holds 1-2 heavy and 3-5 agile captures). So a 330 lb tackle can run on a corner's capture, and a corner on a big man's.
   - The 12 `MG_NonBC_Fatigued_MC_*` jog, run and sprint captures sit in the general databases (`Saved/mm_build_dbs.json`, `pack_loco_lists.json`), so a fresh player can pick a tired gait on the first snap.
   - The sim tracks fatigue per player (`Sim/Game.cpp:741`), but nothing in `Render/` reads it.
2. **The query's future is guessed, not known.** The 1.6 s prediction extrapolates the current smoothed acceleration for 0.5 s, then holds speed (`Athlete.cpp:2683-2749`). The sim's intent is used only for starts from rest (line 2719). The search therefore sees a route break or a user cut only once the body is already accelerating into it. The plant-and-cut capture is picked late, and orientation and stride warps cover the gap.
   - An earlier test used the sim's raw `want` all through the play. It made matched feet skate 19 against 7 cm/s, because `want` asks for more than the sim delivers (memory, `tv-fidelity-goal.md`).
   - The sim already has its own movement law: `PlaySim::predictPos` integrates `routeWant` + `steerVel` (`Sim/PlaySim.cpp:3674-3694`). It is used for QB leads, not for animation.
3. **Transitions out of single clips do not keep velocity.** `ApplyCrossfade` smoothsteps from a *frozen* copy of the last shown pose (`TagRetargetAnimInstance.cpp:295-370`; `FromPose = LastPose` at line 82). Every one-shot ↔ matched switch starts its blend with the outgoing limbs at zero velocity: stance → get-off, catch → run, juke → run, a matched run into a tackle. That is the classic "dead" transition inertialization exists to avoid. Inside motion matching, the blend stack evaluates up to four clips per man, and its new entries caused the frozen-runner stall that the pelvis patch now hides.
4. **Run blocking is one-man loops.** Pass protection plays the pack's two-man stand-off, but only **one** loop capture for every engaged pair, plus 4 wins, 2 big wins, 2 holds and 1 bull rush (`Athlete.cpp:145-154, 5276-5286`). Run blocks play keyed single-man loops (`BlockEngaged`, `DriveLoop`, `DrivenBackLoop`), rate-scaled 0.6-1.4x to the pile's speed (`Athlete.cpp:5244-5270`).
   - `Content/TAG/AnimH/Pack/pack_blocks.json` has 77 two-man block captures with offsets, distances and travel already measured. About 45 are run blocks: `RunBlock_GetEng_*` ×29, Reach, Backside cut, Seal, Impact_Force, Block_Run_*. Each is labelled by direction code and `OffWin` / `DefWin`, and none are used.
   - On run plays (about 42 % of snaps, memory) the trenches are the least captured part of the TV picture.
5. **Tackle picks ignore momentum.** The pair cost is slide residual plus the tackler's offset (`MatchDirector.cpp:942-949`), with no speed term. A 9 m/s head-on hit can get a capture filmed at 3 m/s, and the 0.2-0.4 s glide absorbs the difference. BOOM Tech names "preservation of momentum" as its goal. Pre-impact footage barely exists in the pairs: contact sits a median 0.03 s into the capture (p75 0.17 s, only 11 % ≥ 0.4 s; computed from `pack_pairs.json` `Start`). So a "pre-impact stage" cannot come from these captures.
6. **Contact without a tackle has no clip response.** `DetectContacts` feeds trunk and head springs (`ApplyImpact`, `TagRetargetAnimInstance.cpp:736-796`). Stumble clips play only when the sim calls a broken tackle. The pack's 1,265 BrokenTackles captures are mostly unused outside that path. PTM's job (re-pick after a collision) is not done.
7. **Signature is ±7 % timing.** `ArmSlot` and `StyleJitter` come from a hash of the player id (`Athlete.cpp:1936-1941`). One-shot rate is `1 + StyleJitter*0.07` (line 2585). Stars look like everyone else.
8. **Carrier loops have no style split.** `MMBallMove` is one database per gait for every ball carrier (`Athlete.cpp:1559-1570`).

---

## 3. Actions

Ranked by player-visible impact per unit of effort. Effort: S = under a day, M = days, L = weeks. Every action uses the existing mocap pack and needs no new capture. Each has an A/B flag so it can be measured like for like on the deterministic sim (`-TagWeatherSeed=<n>`).

### 1. Locomotion by body class, with fatigue gating. Effort S-M.
- **Build.**
  - Sort the pack's styled locomotion into classes: **Big** (BIGMAN, BigGuy, Heavy) for OL and DL; **Medium** (LB_Med, LB_, Bruiser) for LB, TE, FB and power RB; **Agile** (Agile, WR, DB, KickReturn) for WR, CB, S and light RB. Unstyled captures stay **General**.
  - For each gait and existing S1-S6 set, build per-class copies that keep the class's captures plus the General ones. Put the other classes' captures in a sibling database with `BaseCostBias` of about +0.1 (`PoseSearchDatabase.h:521`; search costs run 0.05-0.4 per the comment at `Athlete.cpp:1651-1653`). They then win only when nothing in class fits, and coverage holes stay filled.
  - Move `MG_NonBC_Fatigued_MC_*` out of the general sets into a Fatigued database. Search it only when `Player->fatigue` > ~0.35 and the man is not in a live play's first 2 s.
- **Visible result.** Linemen lumber and corners glide. A tight end looks like neither. Late in a long drive, the jog back to the huddle looks tired.
- **Where.** A new `Tools/Art/mm_style_sets_hero.py`, following the `mm_run_sets_hero.py` pattern: `TagEditorLib` add, trim and save, with a report in `Saved/`. In `Athlete.cpp`, `LoadMotionMatching` (the set loop at 1575-1605) gets per-class arrays. In `DriveMotionMatching` (2822), `Sv` becomes `(class, id % n)`, using `Player->pos` and `weightLb`. Add a `fatigue` telemetry column in `MatchDirector::HlTelemetry`.
- **Dependencies.** None. Run it before actions 9 and 10.
- **Verify.** In a 40-play `-TagHlFull` capture A/B'd with `-TagNoStyleSets`:
  - The share of OL/DL moving frames on Agile/WR/DB/KickReturn captures goes from today's value to < 5 %.
  - The share of WR/CB/S frames on Big captures is < 5 %.
  - Fatigued captures appear only when `fatigue` > 0.35.
  - "4+ men on one loop" stays ≤ 0.15. `plf` and live planted-foot travel do not rise. `capture_fidelity.py` dev does not rise.
  - Contact sheet: an OL and a CB jogging to the huddle side by side.

### 2. Feed motion matching the sim's own future. Effort M.
- **Build.**
  - In `AMatchDirector`, where `SetIntent` is called (`MatchDirector.cpp:5578-5579`), compute up to 1.6 s of future positions for men whose motion the sim can forecast:
    - a route runner in `EntState::Route`: step `PlaySim::predictPos` in 0.1 s samples, or a copy of its loop that returns every sample;
    - the user's man: integrate `steerVel(e, v, want, dt)` with his current stick `want` held, so the prediction obeys the sim's acceleration and turn limits;
    - a pursuer: the same, with his `want`.
  - Convert to world coordinates the way 5579 does (the `Dir` sign) and hand them to `AAthlete` (a `SetFuture(TArray<FVector>)` next to `SetIntent`).
  - In `DriveMotionMatching` (`Athlete.cpp:2720-2749`), use these samples instead of the extrapolation when they are fresh this frame. Keep the facing logic (`FaceOff`).
  - Guard: only while the man is free (not engaged, no stun, no one-shot). Fall back to today's extrapolation otherwise.
- **Visible result.** Receivers sink and plant on the break instead of after it. The user's own cuts pick the plant capture on the frame he pushes the stick. This is the "360 cuts" and NGS-route feel.
- **Where.** `Game/MatchDirector.cpp` (the sync loop around 5579), `Render/Athlete.h` and `.cpp` (`DriveMotionMatching`). The sim stays read-only: it must call `predictPos` / `steerVel` only, never mutate state.
- **Dependencies.** None. It is independent of action 1.
- **Verify.** `-TagMoveTest` routes plus a 40-play `-TagHlFull`, A/B'd with `-TagNoSimFuture`. Define a break as a sim velocity heading change > 45 degrees within 0.3 s (`svx`, `svy`). Then, over ±0.5 s around each break:
  - the lag from turn onset to the first matched pick change (from the `anim` column) falls;
  - mean |`mmang`| (clip travel vs. the man's travel) falls;
  - the orientation warp used (`mmor`) falls;
  - planted-foot travel in the window (`plant_truth.py`) does not rise. This is the check that failed when raw intent was used.
  - The user check is `-TagPadPlayTest` or a hand playtest of jukes.

### 3. Velocity-keeping transitions out of single clips (dead blending). Effort S-M.
- **Build.**
  - In `FTagRetargetProxy`, keep the pose from two evaluations back as well as `LastPose`, so each bone has a local angular velocity at the switch.
  - On a `ClipSerial` change (`PreUpdate`, lines 79-85), store `FromPose` *and* its velocity. In `ApplyCrossfade`, extrapolate `FromPose` forward with that velocity decaying (half-life about 0.1-0.15 s, clamped per bone), then smoothstep to the new pose as now. This is Holden's dead blending [ref](https://theorangeduck.com/page/dead-blending-node-unreal-engine).
  - Do not extrapolate the root or pelvis translation; `bDropFromTravel` handles them. Leave the snapshot path (get-ups) unchanged.
  - Flag `-TagFrozenFade` for A/B.
- **Visible result.** The arms and legs carry their swing through stance → get-off, catch → run, juke → run and matched run → tackle, instead of easing out of a pose that stopped dead.
- **Where.** `Render/TagRetargetAnimInstance.h` (`FromPose`, new `FromVel`) and `.cpp` (`PreUpdate`, `ApplyCrossfade`).
- **Dependencies.** None.
- **Verify.** Add a telemetry metric `xfj`: mean joint acceleration (second difference of the 11 `DevJoints`) in the 0.25 s after each `ClipSerial` bump, divided by the same man's mean in the 0.25 s before.
  - Target: p50 and p95 of `xfj` drop against `-TagFrozenFade`.
  - Body pops (`analyze.py`) stay at 0.
  - Crossfade deviation `dxf` may rise slightly; report it.
  - Contact sheet: 10 frames of the snap for a lineman and a receiver.

### 4. Two-man run blocking from `pack_blocks.json`. Effort M.
- **Build.**
  - Load the RunBlock entries the way `FLineRep` loads the pass-pro reps (`Athlete.cpp:132-172`). They need `D`/`O` parts, `Ox`/`Oy`/`Yaw`, `DMin`/`DMean`, `TravelA`/`TravelB`, plus a kind parsed from the name (GetEng Force/Gap/Interior, Reach, Backside, Seal) and the outcome (`OffWin` / `DefWin`).
  - At a run-play engagement, pick by:
    - kind from the block's geometry: the blocker's target side against his facing, and a reach vs. a down block;
    - outcome from the sign of `LinePush`;
    - the travel residual against the pile's sim motion, as the tackle cost does.
  - Play both men on one clock (the `bRepClip` / pair-clock path at 5276-5286). Steer with the follower. Return to today's loops when the capture ends.
  - Keep `RunBlock_Impact_Force_*` (both men to the turf) for pancakes only. This is the same `pzmin` < 0.45 rule as the upright families.
  - Add the mirrored stand-off (`A.Side(..., true)`) to `RepLoops`, so pass-pro pairs are not all one capture.
- **Visible result.** Down blocks, reaches and seals look like the blocks on a broadcast: hands inside, defenders shed or driven. The line no longer plays identical keyed loops.
- **Where.** `Render/Athlete.cpp` (`FAthleteAssets` line reps, the block loop in `Drive` around 5240-5320). It reads sim block data only.
- **Dependencies.** None. Action 3 helps the entries and exits.
- **Verify.** Film `-TagLineCam -TagHlFull` on a run-heavy seed. Checks:
  - the share of engaged run-block frames on two-man captures (today 0 %, target > 60 %);
  - helmet-to-helmet distance of engaged pairs inside each capture's `DMin`-`DMax` (the captures' own numbers);
  - engaged-pair overlap (`ResolveEngagedOverlap`) unchanged;
  - lineman `plf` not worse;
  - pops 0;
  - pass-pro: distinct stand-off captures per play ≥ 2.

### 5. Momentum in the tackle pick. Effort S.
- **Build.**
  - In `Tools/Art/pack_pairs.py` and `pack_gangs.py`, add both men's pelvis ground speed and heading over the first 0.15 s after `Start` to the JSON (`VT`, `VC`, `HT`, `HC`).
  - Load them into `FPackPair` and add a term `w · (|vT_cap − vT_game| + |vC_cap − vC_game|)` to the `Cost` lambda (`MatchDirector.cpp:942-949`), with w about 20-30 cm per m/s so it ranks below the slide residual.
  - Hit sticks already prefer stick captures (line 987).
- **Visible result.** Full-speed collisions play full-speed captures: bodies keep moving through the hit instead of meeting and stalling. This is BOOM Tech's "preservation of momentum".
- **Where.** `Tools/Art/pack_pairs.py`, `Render/Athlete.h` (`FPackPair`), `Game/MatchDirector.cpp` (`StartPairedTackle`).
- **Dependencies.** A pack_pairs rebuild (commandlet).
- **Verify.** `-TagTackleCam` plus `tackle_check.py`. Track:
  - median |capture speed − game speed| at contact, which should fall;
  - `hit_jump` and pile-centre jump (baseline tc13 median 197 cm/s), which should fall or hold;
  - shown-vs-scored spot, which stays at 0.5 yd or better;
  - carriers down 100 %.

### 6. A/B the matching node's transitions: blend stack vs. dead blending. Effort S to try.
- **Build.**
  - Under a flag `-TagMMDeadBlend`, set the node's MaxActiveBlends to 0 through `SetMatchSetting`.
  - Insert an `FAnimNode_DeadBlending` between `Offset` and `Tap` in the proxy constructor (`TagMotionMatchInstance.cpp:93-98`). It must sit above the matching node in the update chain so the node's inertialization request reaches it.
  - Run once with the pelvis patch (`-TagMMNoPelvisFix`) off, to see whether the stall it covers was a blend-stack artefact.
- **Visible result.** Possibly crisper matched transitions, and fewer clips evaluated per man (Bollo reports transitions 60 % cheaper).
- **Where.** `Render/TagMotionMatchInstance.h` and `.cpp`.
- **Dependencies.** None. It shares the measuring metric with action 3.
- **Verify.** `-TagStallDbg` stall count (to10: 7, or 1 with the patch). Standard bench `Tools/Perf/bench.ps1` fps. Plus `plf`, `dev`, the clone share and `xfj` at matched pick changes. Keep the flag off by default unless all of these hold or improve.

### 7. Contact reactions re-picked from the capture library ("PTM-lite"). Effort M.
- **Build.**
  - In `AMatchDirector::DetectContacts`, when a contact's mass-weighted velocity change on a free, upright, untackled man exceeds about 300 cm/s, play a stumble-and-recover one-shot.
  - Pick it from a `PackFamily` of BrokenTackles/stumble captures, filtered to upright ones (`pzmin` > 0.45). Choose by the push direction relative to his facing (the pack's D1-D8 codes) and his speed, and enter at the capture's onset (`pack_index.json`).
  - The follower steers it to the sim spot. The sim is unchanged: its `collide` already removes the speed.
  - Cooldown 1 s per man. Not for engaged linemen.
- **Visible result.** A receiver knocked off his route by a jam, or a back bounced off a pile, visibly loses balance and recovers.
- **Where.** `Game/MatchDirector.cpp` (`DetectContacts`), `Render/Athlete.cpp` (a new `PlayContactStumble`, using `PickOr` / `PackTimed`).
- **Dependencies.** Action 3 makes the entry and exit read well.
- **Verify.** The per-play "TAG contacts this play" log gets a count of contacts over the threshold and of reactions played. In a 40-play film: pops 0, the `voff` p95 for reacting men ≤ 60 cm (follower limit), and the frame step from contact to clip start ≤ 1. Then a crop sheet of 10 reactions for an eyeball check against broadcast.

### 8. Ball carriers by style. Effort S, after action 1.
- **Build.** Split `PSD_TAG_BC_*_Loops` the same way as action 1: Bruiser/Big captures for power backs and TEs, Agile for scat backs and WRs. Use the carrier's class in `MMBallMove` (`Athlete.cpp:1559-1570`, 2824-2826).
- **Visible result.** A 240 lb back carries the ball differently from a slot receiver after the catch.
- **Where.** The `Tools/Art/mm_build_bc_dbs.py` family and `Athlete.cpp`.
- **Dependencies.** Action 1, and whether the BC families hold enough captures per class (`BC_BLOS_MC_*`, `BC_SquaredUp`, `BC_Coverball`; see open questions).
- **Verify.** In telemetry, the share of carrier frames on in-class BC captures. The carrier-refresh log rate does not rise. `plf` for carriers is not worse.

### 9. Foot-aware search features (phase or foot velocity). Effort M, low confidence.
- **Build.**
  - Inspect `PSS_TAGH` with `Tools/Art/mm_schema_inspect.py`. If it lacks foot velocity, add Velocity channels on `ball_l`/`ball_r`. Otherwise try `PoseSearchFeatureChannel_Phase` on the feet: an analytic local phase, the idea behind EA's local bone phases, with no training.
  - Make this a schema copy (`PSS_TAGH_Phase`) and rebuild the hero databases into a parallel folder for A/B.
- **Visible result.** Picks that land on the matching foot. Fewer re-plants just after a matched pick change.
- **Where.** `Tools/Art/mm_build_dbs_hero.py` (a schema argument), `Athlete.cpp` `LoadMotionMatching` (`AnimRoot` / MM folder under a flag).
- **Dependencies.** Database rebuild time. Lower value if action 6 already cleans transitions.
- **Verify.** Plant-truth slide restricted to plants that span a matched clip change (p14 baseline: 70 % of those slid; re-measure today first). `-TagMMWhy` switch counts should not rise.

### 10. Signature sets for Superstars and X-Factors. Effort S, after actions 1 and 8.
- **Build.**
  - Give each Superstar or X-Factor (the abilities system) a fixed preferred sub-family: a sprint and a carry style chosen once from his ratings, and for QBs one throw-capture subset by arm angle if the pack labels them.
  - Implement it as a per-man database preference (an in-family database first, the class database with a small bias).
- **Visible result.** The star back runs the same distinctive way every carry. This is Madden's signature run cycles and QB DNA, done with the captures TAG owns.
- **Where.** `Athlete.cpp` set choice, and the abilities data in `Sim/Abilities.*` (read only).
- **Dependencies.** Actions 1 and 8.
- **Verify.** For each star, more than 70 % of his moving frames in his family across a full game, with `plf` unchanged. The scorecard clone share does not rise; stars are few.

---

## 4. Not worth doing

| Idea | Why not |
|---|---|
| Learned motion matching, PFNN, NSM, DeepPhase, or HyperMotion-style generation | Each needs a training pipeline and, for generation, far more capture than TAG has. Learned MM solves memory, which TAG handles by streaming. The cheap piece of the phase idea is the analytic Phase channel PoseSearch already ships (action 9). |
| Full active ragdoll (Euphoria / DReCon style) | It needs a powered ragdoll on 22 pack bodies (only the hidden source rig has a physics asset in the tree: `Content/TAG/Hero/Rig/SKM_TAG_HeroRig_PhysicsAsset`), RL or heavy tuning, and it gives up deterministic films. Backbreaker shows that physics variety does not read as football by itself. Fred judged the captured tackles right after the root-motion fix (memory). The spring layer already covers the jolt cheaply. |
| UE PoseSearch interactions (`MotionMatchMulti`) for tackles and blocks | Marked experimental, "not for production use", in 5.8 source, with one frame of delay by design. TAG's own pair, gang, block and turnover pickers already do the same job with measured alignment (pair pelvis error 0.023 m). |
| UE Motion Warping component | Beta, and built for montages on characters with root motion. TAG's follower, glide and `FallRootTravel` already warp to sim targets, with metrics. |
| Mover, UAF / AnimNext | Experimental in 5.8, and they would replace the sim-driven actor model that works. |
| A tackle "pre-impact stage" from the pair captures | The pairs start a median 0.03 s before contact; only 11 % have ≥ 0.4 s. There is no approach footage to show. Doing it would mean pre-rolling tackles in the sim (like `preRollCatch`) and building approach clips from single-man tackle attempts, which is L effort with calibration risk. Revisit only if playtests call tackles "sudden". |
| Video-to-animation (ProPLAY style) to grow the library | Markerless capture from video needs cleanup per clip and gives lower quality than the 12,482-clip pack TAG already has. It also breaks the "capture as captured" fidelity rule. |
| ML Deformer for jersey and muscle | Not motion. It needs simulated training data and belongs to the look researchers, if anyone. |
| Physics Control hit reactions now | Possible later for arms in piles, but `ApplyKeepOut` and `ApplyImpact` cover the visible cases. Revisit after action 7, if contact still reads stiff. |

---

## 5. Open questions

1. **Schema contents.** I did not open `PSS_TAGH` (an asset). Action 9's value depends on whether it already carries foot velocities. `mm_schema_inspect.py` answers this.
2. **Hero databases.** I read the UEFN-tree build report (`Saved/mm_build_dbs.json`) for the fatigued and style findings. `mm_build_dbs_hero.py` "mirrors every PSD_TAG_*", so the hero databases very likely hold the same mix. Confirm with `TagEditorLib.database_anim_names` before building action 1.
3. **Class coverage.** Big has about 8 run and sprint captures plus 4 Heavy; Agile has about 11. These may be too few per direction for clean matching without the General fallback, which is why action 1 uses a bias rather than exclusion. Whether the carrier families split well (action 8) is unmeasured.
4. **How much the frozen crossfade shows.** No current metric isolates it. The `xfj` metric in action 3 is the first step and should be measured before the change is judged.
5. **EA internals.** "Animation branching" (FieldSENSE) and PTM have no technical publication. The patent link is my inference. Madden 25, CFB 25 and Madden 24 Gridiron Notes pages refused the fetcher, so those claims rest on search summaries.
6. **Arm-slot labels.** I did not establish whether the pack's QB throw captures are named by arm angle (needed for action 10's QB part).
7. **Physics asset on the visible bodies.** Unknown whether the pack bodies have usable physics assets. It only matters if active-ragdoll work is ever reopened.
