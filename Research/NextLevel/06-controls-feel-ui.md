# 06: Controls, game feel, and UI/UX

Researcher 6 of 7. Scope: the path from the player's thumbs to the athlete on screen, and every screen the player touches.
Written 2026-11-24 against branch `look-and-feel` (uncommitted working tree). Nothing in the project was changed. The only
other file written is a throwaway script, `steer.py` in the session scratchpad, which copies the sim's steering law to time cuts.

**How the research was done, and its limits.** Web search and fetch were available. Three research sub-agents
and I used up the session's shared web-search budget (200 searches) partway through. After that I could only fetch URLs I already
had. Several questions were left part-answered, and they are listed in section 5. EA's older Gridiron Notes, Pitch Notes and
CFB 25/26 pages now redirect to the current-year titles, so for older games the evidence is EA's accessibility pages
(ea.com/able) and community guides. I opened the key EA pages myself: the Madden 26 deep dive, the Madden 26 accessibility page, the FC 26
Pitch Notes and the Madden 27 "fastest Madden" page. Where a claim rests on a search-engine summary rather than a page that was
read, it says so. Claims marked **[memory]** come from my own knowledge with no source fetched this session.

Code claims cite files under `Unreal/TAGGridiron/Source/TAGGridiron/`. I read them this session. I also read the engine source
in `C:\Program Files\Epic Games\UE_5.8\Engine` for the dead-zone and rumble paths.

---

## 1. What the best studios do

### 1.1 Responsiveness vs. animation fidelity

| # | Technique / finding | Source | Confidence |
|---|---|---|---|
| R1 | **EA FC 26 split gameplay into two presets.** "Competitive Gameplay is tuned for added responsiveness and higher player control", while "Authentic Gameplay is tuned for higher realism and true-to-life match tempo/speed in offline modes". The same notes say animation transitions and branching were "re-evaluated and retuned… to diminish the feeling of being 'locked in' to animations". Also: "a player should always feel and play well when you are controlling them." | https://www.ea.com/games/ea-sports-fc/fc-26/news/pitch-notes-fc26-gameplay-deep-dive (official, 22 Jul 2025; fetched) | High |
| R2 | **Madden 26 movement tuning** is "built on giving players ultimate control and responsiveness in change of direction". Top-end speed stays true to real athletes: a 4.2 forty is a 4.2. In short, they keep speed real and lean toward control on change of direction. | https://www.ea.com/games/madden-nfl/madden-nfl-26/news/madden-26-gridiron-notes-gameplay-deep-dive (official, 17 Jun 2025; fetched) | High (on what is stated; no numbers given) |
| R3 | **NBA 2K18 moved off "animation-driven" movement.** 2K said it brought "improvements to feel and responsiveness" but lost "some of the control… over animation selection, sacrificing some of the authenticity". NBA 2K17 patched out long dribble-pickup animations "to improve responsiveness". | https://bleedingcool.com/games/nba-2k20-releases-new-info-on-system-improvements (press quoting the 2K blog); https://www.gameinformer.com/b/news/archive/2016/11/22/latest-nba-2k17-patch-cleans-up-dribbling-animations.aspx | Medium-high |
| R4 | **For Honor's motion matching (Clavet, GDC 2016).** "The goal is not to be as responsive as possible, the goal is to be as predictable as possible." The simulation drives the character, and animation is "a cosmetic detail on top". The mesh's centre of mass is clamped about 15 cm from the simulated point, and clips are time-scaled at most +10% / -20%. | https://www.gameanim.com/2016/05/03/motion-matching-ubisofts-honor/ (community summary of the talk); talk page https://www.gdcvault.com/play/1023280/Motion-Matching-and-The-Road | Medium (second-hand summary) |
| R5 | **Daniel Holden on code-driven vs data-driven displacement.** Pulling the simulation toward the animation makes the character "less responsive than we would like". It is better to clamp the visual deviation in distance and angle, because "a little bit of foot sliding can often look much less weird than breaking the posing". Future trajectory is predicted with a critically damped spring on the desired velocity. A normal blend is about 0.2 s. | https://theorangeduck.com/page/code-vs-data-driven-displacement ; https://theorangeduck.com/page/spring-roll-call ; https://theorangeduck.com/page/dead-blending | High (author's own blog) |
| R6 | **Epic's motion matching docs and GASP.** GASP uses a capsule-driven model with Offset Root Bone, so the mesh drifts a little from the capsule during starts and pivots. Search Throttle Time, Blend Time and the continuing-pose bias are the tunable knobs. Neither page gives response-time targets. | https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine ; https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine | High |
| R7 | **Response-time targets.** None of the motion-matching sources gives one in ms or frames. O3DE's example searches about 5 times a second. | https://docs.o3de.org/blog/posts/blog-motionmatching/ | Absence confirmed for the sources read |
| R8 | **Madden 23 FieldSENSE.** Animation branching drives the ball-carrier moves, and "360° Cuts provides exact control to trigger hard cuts". Madden 25 BOOM Tech picks the fall, stumble or recovery from the physics of the collision. | https://news.xbox.com/en-us/2022/06/27/introducing-fieldsense-in-madden-nfl-23/ ; https://www.ea.com/inside-ea/news/boom-tech-ea-sports-madden-nfl-25 | High (what they claim) |
| R9 | **Input buffering and cancel windows.** Fighting and action games keep a button press alive for a few frames, so a press made just before an action becomes legal still fires. They also open defined cancel windows inside animations. | **[memory]**. The sources I tried (Infil glossary) rendered by JavaScript and could not be read. | Medium (standard practice; no source this session) |

### 1.2 Input latency

| # | Finding | Source | Confidence |
|---|---|---|---|
| L1 | **Mick West's camera measurements.** PS3 games were filmed at 60 fps with TV lag subtracted. Best was about 3 frames (50 ms; Guitar Hero III, the PS3 menu); many games were 4 frames (67 ms); Halo 3 was 133-167 ms; GTA IV was 166-200 ms. He also notes "1/5th of a second (200ms) is too long to wait for a gun to fire". His "Programming Responsiveness" says it takes "up to three frames" from input to visible feedback in a typical engine loop. | https://www.gamedeveloper.com/design/measuring-responsiveness-in-video-games ; https://www.gamedeveloper.com/programming/programming-responsiveness | High |
| L2 | **Fighting-game end-to-end lag** (camera + LED method): Tekken 7 4.8 frames; SFV 5.3-6.3 frames. SF6 in the demo was under 5 frames, about 3 with its input-delay reduction setting, and under 2 at 120 Hz. | https://displaylag.com/video-game-input-lag-database/ ; https://wccftech.com/street-fighter-6-input-delay-on-playstation-can-get-under-3-frames-training-mode-discovered-in-the-demo/ | Medium-high |
| L3 | **Sports-game latency.** No lab measurement was found for Madden or any other sports game. EA forum users measured FC 24/25 at 32-124 ms, depending on where they stopped the clock. | https://forums.ea.com/discussions/fc-24-general-discussion-en/ive-measured-the-input-delay-and-here-are-the-results/12118721 | Low |
| L4 | **UE pipelining adds latency.** The game, render and RHI threads can run ahead of the GPU, and that queued work adds input latency. Settings: `r.GTSyncType` 0 (game thread syncs with the render thread; the default), 1 (syncs with the RHI thread), 2 (syncs with swap-chain present; needs vsync). `r.OneFrameThreadLag` and `rhi.SyncSlackMS` ("decreasing this value will reduce input latency"). Epic's 30 Hz example goes from up to 130 ms worst case to about 66 ms with mode 2. | https://dev.epicgames.com/documentation/en-us/unreal-engine/low-latency-frame-syncing-in-unreal-engine | High |
| L5 | **Epic staff advice.** Try `r.GTSyncType 2` and a lower `rhi.SyncSlackMS`, enable NVIDIA Reflex, and A/B `r.OneFrameThreadLag` with Insights. One developer measured about 115 ms in an empty level. | https://forums.unrealengine.com/t/high-input-latency-at-runtime/2630610 | Medium-high |
| L6 | **NVIDIA Reflex ships with UE5 as a plugin.** Its PC Latency (PCL) stats measure input-to-display latency per frame without hardware, and FrameView reads them. LDAT and OpenLDAT measure click-to-photon with a light sensor. One third-party test found little gain from Reflex in a UE5 title. | https://developer.nvidia.com/unrealengine ; https://developer.nvidia.com/blog/understanding-and-measuring-pc-latency ; https://developer.nvidia.com/nvidia-latency-display-analysis-tool ; https://primagames.com/gaming/nvidia-reflex-low-latency-tested (search summary only) | High / low for the last |
| L7 | **Madden 27 (Nov 2026)** says "pushing past 60 FPS reduces input-to-display latency". It targets 100-120 fps on PS5 Pro and up to 90 on base consoles, and offers 30/60/120/uncapped on PC. | https://www.ea.com/games/madden-nfl/news/madden-27-the-fastest-madden-ever (fetched) | High |

### 1.3 Control schemes

| # | Finding | Source | Confidence |
|---|---|---|---|
| C1 | **Madden passing types.** Madden 26 and CFB 25/26 offer Placement, Placement & Accuracy, Classic and Revamped. Madden 23 added "Reticle Visibility" and "Meter Visibility" toggles and a free-form reticle speed setting (0-20, default 7). | https://www.ea.com/able/resources/madden-nfl/madden-nfl-26 (fetched); https://www.ea.com/able/resources/madden-nfl/madden-nfl-23/ps5/game-options | High |
| C2 | **How the Madden 26 passing modes behave.** In Placement & Accuracy, "the left stick directs the target… meter timing affects accuracy". Placement is the same lead without meter timing. In Classic, the receiver button with lob, touch or bullet plus "directional influence". "Pass Lead Increase" can be None, Small, Medium or Large. | https://www.operationsports.com/madden-25-and-college-football-25-passing-settings-and-revamped-vs-placement-and-accuracy/ | Medium (community guide) |
| C3 | **Madden pass inputs.** Tap the receiver button for a lob; tap and hold for a bullet; hold L1 for high, L2 for low; double-tap the icon for a pump fake; R3 to throw it away. I found no source saying whether the throw starts on press or on release, or whether a lob waits to rule out a double tap. | https://www.ea.com/able/resources/madden-nfl/madden-nfl-23/ps5/controls ; https://realsport101.com/article/madden-26-passing-guide-how-to-throw-lasers-and-avoid-gettting-sacked | High for the mapping; open for the timing |
| C4 | **Madden switching and assists.** Defense switches with CIRCLE in coverage, pursuit and ball in the air. Madden 23 assists: "Defensive Switch Assist" (on by default) helps your movement right after a switch so you don't run the new man out of the play. "Heat Seeker" steers the user toward the carrier. "Ball Hawk" auto-moves you to play a catch. | https://www.ea.com/able/resources/madden-nfl/madden-nfl-23/ps5/controls ; https://www.ea.com/able/resources/madden-nfl/madden-nfl-23/ps5/game-options | High |
| C5 | **EA FC 24 "Auto Switching"** lets the player choose which situations switch automatically: Manual, Only on Loose Balls, Only on Air Balls, Automatic, or Air and Loose Balls. There is also a "Player Indicator Size" option. | https://www.ea.com/able/resources/ea-sports-fc/fc-24 | High |
| C6 | **Madden 26 pre-snap and line play.** The full route tree is available for hot routes. "Protection on the Fly" changes protection with an RS flick, without opening a menu. Route Commit is a high-risk defensive guess. Disguised safety rotation. Each pass-rush move costs "a dynamic amount of stamina" by rating, replacing the old per-play move cap. | Madden 26 deep dive (fetched, URL in R2) | High |
| C7 | **Madden 27 (Aug 2026)** advertises "streamlined controls… up to twice as fast" for pre-play adjustments, preset adjustments, a timing-based catching meter, and a QB sneak meter. | https://www.ea.com/games/madden-nfl/news/madden-27-launch-ea-sports (fetched by a sub-agent) | Medium-high |
| C8 | **Kicking.** Madden 26 and CFB 25/26 let you pick "tap and hold" or "tap and tap" for the kick meter. Madden 26 has home-field kick-meter "wobble" (it had to be tuned down for Buffalo), and weather costs kick and throw power and accuracy. | Madden 26 accessibility page (fetched); https://www.ea.com/games/madden-nfl/madden-nfl-26/news/title-update-september-17-2025 | High |
| C9 | **Madden 06 QB Vision cone.** Throws outside a cone sized by Awareness were weak and inaccurate. It "was so unpopular that it disappeared in Madden NFL 09." | https://en.wikipedia.org/wiki/Madden_NFL_06 | Medium (Wikipedia) |
| C10 | **ESPN NFL 2K5.** VIP opponent profiles (IGN: "probably the coolest innovation"), first-person football, and the Crib. I could not source how VIP worked; my recollection is that it recorded a human opponent's tendencies so the CPU could play like that person. | https://en.wikipedia.org/wiki/ESPN_NFL_2K5 ; mechanics **[memory]** | Medium / low for the mechanics |
| C11 | **Play calling in Madden 26.** Coach Suggestions come from "the data-driven model", a per-team model of the real coach's tendencies. Adaptive Counters are tagged in the menu, and "Coach Speak" explains why a play is suggested. Madden 11's GameFlow (a situational game plan) was sold as cutting game time by up to half. | Madden 26 deep dive (fetched); https://en.wikipedia.org/wiki/Madden_NFL_11 | High / medium |

### 1.4 Assistance, difficulty, onboarding, accessibility

| # | Finding | Source | Confidence |
|---|---|---|---|
| A1 | **Madden 26 assists and difficulty.** "On Field Trainer can be turned to On, Off, or to Dynamic to provide hints" (Dynamic adapts to the player's skill). Defensive Assists give automatic movement guidance. Auto Pass. Player and CPU sliders across five difficulty levels. Madden 23 game styles: Arcade, Simulation (default) and Competitive. | https://www.ea.com/able/resources/madden-nfl/madden-nfl-26 (fetched); Madden 23 game-options URL in C1 | High |
| A2 | **Madden 26 accessibility.** Colorblind filters for protanomaly, deuteranomaly and tritanomaly; vibration Off / Normal / Assist; "Enlarge On Field Graphics"; menu narration. "Remapping options are not present." EA FC 24, by contrast, remaps classic controls and offers colorblind filters and a radar colour option. | Madden 26 accessibility page (fetched); FC 24 URL in C5 | High |
| A3 | **Xbox Accessibility Guidelines.** XAG 110: players should be able to "turn off the feedback" and "adjust the strength of the feedback"; haptics "shouldn't be the only method of conveying information". XAG 103: pair haptics with at least one other cue. | https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/110 ; …/103 | High |

### 1.5 Feedback: haptics, hit-stop, shake, HUD

| # | Finding | Source | Confidence |
|---|---|---|---|
| F1 | **NBA 2K21 on DualSense.** Trigger resistance on the sprint trigger rises as the player's energy drains. Strong post players feel little L2 resistance when backing down weaker men. Haptics accent the collision systems. | https://blog.playstation.com/?p=342188 (developer post, Mike Wang) | High |
| F2 | **Rumble design.** No public rumble guidelines with durations or intensities were found. Sony's DualSense guidance is likely under NDA. | — | Absence confirmed |
| F3 | **Camera shake.** Eiserloh (GDC 2016, "Juicing Your Cameras With Math") recommends a decaying "trauma" value, with shake = trauma² or ³ driving smooth Perlin noise offsets and rotations rather than per-frame random jitter. The talk page was confirmed; the slides could not be fetched, so the details are from memory. | https://www.gdcvault.com/play/1023557/Math-for-Game-Programmers-Juicing (page confirmed); details **[memory]** | Medium |
| F4 | **Hit-stop.** Brawlers and fighting games freeze both characters for a few frames on a heavy hit (Sakurai's columns and videos). Vlambeer's "Art of Screenshake" covers the same family of techniques. Neither talk's content could be fetched. | https://www.youtube.com/watch?v=AJdEqssNZ-U (title confirmed); content **[memory]** | Medium |

### 1.6 Menus and front end

I could not fetch dedicated sources this session for Persona 5, Forza Horizon, Destiny or MLB The Show menu design (the
search budget was gone). What follows is **[memory]**, and it is the widely repeated craft consensus rather than any one studio's claim:
- Premium front ends respond within the frame and never show a frozen frame. Animated transitions run concurrently with input, so a fast player is never made to wait.
- They keep one motion language: the same easing curves and durations everywhere, usually 150-250 ms.
- They use one type family with strict hierarchy, and a persistent button-legend bar.
- They hide loading behind something live (a 3D showcase, a reel, a tip), never behind a stalled image.

Confidence: medium. This is consensus practice, not sourced here.

### 1.7 UE5 tooling

| # | Finding | Source | Confidence |
|---|---|---|---|
| U1 | **Thumbstick dead zones.** Axial dead zones make sweeping input "snap" to the cardinal directions. A scaled radial dead zone, (\|v\|-dz)/(1-dz) along the stick's own direction, has "no longer a visible edge". Typical values are 0.1-0.2. Microsoft's XInput sample uses a circular dead zone (7849/32767, about 0.24) with rescaling. Enhanced Input's `Axial` type "matches the original UE4 deadzone logic"; `Radial` gives "circular/spherical coverage". | https://www.gamedeveloper.com/disciplines/doing-thumbstick-dead-zones-right ; https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput ; https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/DeadZoneType | High |
| U2 | **Legacy UE dead zone is per axis (engine source checked).** `UPlayerInput::MassageAxisInput` (`Engine/Source/Runtime/Engine/Private/UserInterface/PlayerInput.cpp:2305`) applies `DeadZone` to each axis key separately, and `APlayerController::GetInputAnalogKeyState` returns that value. | UE 5.8 engine source, read locally | High |
| U3 | **GameInput rumble (engine source checked).** The plugin sends rumble (low/high motors plus the two impulse triggers) to **every device mapped to the platform user** (`Engine/Plugins/Runtime/GameInput/Source/GameInputBase/Private/IGameInputDeviceInterface.cpp:235-313`). I found no Windows path for DualSense adaptive triggers. Epic's force-feedback page lists trigger effects as having "platform-specific implementations", and audio-based vibration as PS5 DualSense only. | engine source; https://dev.epicgames.com/documentation/en-us/unreal-engine/force-feedback-in-unreal-engine | High for the routing; whether DualSense rumbles through GameInput on Windows is **unknown** |
| U4 | **CommonUI.** It routes input to the top-most widget, handles gamepad navigation and per-platform glyphs, and needs the CommonGameViewportClient plus data assets. It is set up through settings and assets, and no C++ or Slate-only route is documented. Slate performance: invalidation boxes and global invalidation, and avoid adding or removing children every frame. | https://dev.epicgames.com/documentation/en-us/unreal-engine/common-ui-quickstart-guide-for-unreal-engine ; https://dev.epicgames.com/documentation/en-us/unreal-engine/invalidation-in-slate-and-umg-for-unreal-engine | High |
| U5 | **Enhanced Input.** Hold, Tap, Pulse, Chord and Combo triggers; `UEnhancedInputUserSettings` for remapping. No input-buffer trigger is documented. | Epic Python API pages (e.g. https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EnhancedInputUserSettings) | High |

---

## 2. Where TAG Gridiron stands

### Already strong (keep and build on)
- **Per-device input that works for couch play.** `Game/TagInput.*` (`FTagInputHub`, `FTagReader`) reads each device on its own in front of Slate. It removes the duplicate Xbox press that arrives through both XInput and GameInput (`SecondCopy`, 80 ms) and lets one player own a menu (`SetMenuOwners`).
- **Remapping every context.** `Game/TagBindings.*` covers seven contexts, pad and keyboard, with prompts translated to the player's layout. `UI/ControlsScreen.cpp` draws the pad the player actually holds. **Madden 26 has no remapping at all** (A2), so this is a real lead.
- **A control set close to Madden 26.** `ReadUserInputSlot` (`Game/MatchDirector.cpp:7556-7784`) has pass types by press length plus L1/L2 high/low, R3 throw away, R2 sprint with stamina, R1 cover-up or strip, L2 strafe, and the highlight stick (truck, juke, stiff arm, spin, hurdle, hit, cut). Rush moves use energy (`MatchTrenches.cpp`). Pre-snap menus cover audibles, hot routes, protection, D-line and LB shifts, shells and defensive assignments (`Game/MatchPresnap.cpp:365-440`). On defense, holding the switch button and pushing the left stick picks the man that way pre-snap (`PlaySim::switchUserToward`).
- **Kick meter.** Two schemes (`Game/MatchKicking.cpp:90-236`): a stick swing with lean as accuracy, and a three-press keyboard scheme. It shows wind and the kicker's ratings, and the result stays on screen for 1.6 s.
- **On-field indicators.** Receiver icons coloured to match the play art, a user ring with a facing notch, a highlight cue (a ring on the tackler and a move pill), trench bars and a stamina bar (`UI/GridironOverlay.h:37-100`).
- **One UI look.** `UI/TagTheme.*` has one palette, one type family (Roboto Black / BoldCondensed), one selection look, a 0.22 s eased modal entrance and a 0.2 s card fade. Pause restores the screen it opened over, and the loading screen plays the highlight reel.
- **Glyphs** follow the last device used and the pad type detected (`TrackInputDevice`, `SetPadKind`). Camera changes need a 0.5 s hold.

### Falls short: verified in code

1. **The stick dead zone is axial, which snaps steering toward straight lines and slows diagonals.**
   - Where: `Config/DefaultInput.ini` sets `DeadZone=0.25` per axis on all four stick axes, which UE applies one axis at a time (U2). `FTagInputHub::Axis` repeats the same per-axis 0.25 rule for multiplayer pads (`Game/TagInput.cpp:217-226`). `ReadUserInputSlot` then builds the run direction from these values (`MatchDirector.cpp:7572-7581`). Speed scales with the stick's length (`PlaySim::applyUser`, `Sim/PlaySim.cpp:3095`).
   - Steering, computed from those formulas: at full deflection, any stick angle within ±14.5° of up, down, left or right reads as exactly that direction. That is 116° of the 360° circle collapsed onto four headings. A stick 20° off upfield steers 7.6°; 30° steers 22°.
   - Speed, inferred: a full diagonal on a circular stick gate reads about 0.86 in length, so a diagonal run gets about 86% of `vmax`.
   - Why it matters: in the behind-the-offense camera, "upfield" is stick-up, so a runner's small angle adjustments fall inside the snap.
   - Caveat: how much this bites depends on the raw diagonal values of Fred's pads, which I could not log without running the game.
2. **No rumble or haptics anywhere.** No `ForceFeedback` or `Rumble` calls exist in `Source/` (grep). A tackle, a sack, a big hit, a catch in traffic and a kick strike are only seen and heard.
3. **A tap-throw waits 220 ms before anything happens, and a bullet waits up to 300 ms** (`MatchDirector.cpp:7612-7629`).
   - A tap is held back 0.22 s to see whether a second tap (pump fake) is coming. A press becomes a bullet only on release, or once held 0.3 s.
   - The windup (`anim = Throw`) starts only when the sim receives `throwTo` (`PlaySim::applyUser`, `PlaySim.cpp:3041-3053`). The ball leaves `releaseTime x 0.8` later, which is 0.13-0.27 s (`PlaySim.cpp:3751`).
   - Net effect: about 0.35-0.5 s from pressing to the ball leaving, and the first 150-300 ms show no visible reaction at all.
   - Madden uses the same press-length and double-tap scheme (C3). Whether Madden also holds the throw back is not known.
4. **Moves pressed a moment too early are dropped silently.** `PlaySim::startMove` refuses a carrier move until `animT > 0.6` s into the current animation. A catch or a juke resets that clock, so a flick in the first 0.6 s after a catch does nothing. It also refuses a hit stick while `moveT < 0.9` (`PlaySim.cpp:3106-3116`). The director clears `stickMove` after every sim step (`MatchDirector.cpp:8236-8240`), so there is no buffer and nothing on screen says the move was refused.
5. **Live defensive switching ignores where the ball is going.**
   - `PlaySim::switchUser` (`PlaySim.cpp:2124-2144`) picks the nearest man to `ball.pos`, which is the ball's current position. While a pass is in the air that is near the quarterback, not the catch point.
   - Engaged linemen and men behind the carrier are not excluded.
   - The stick-directed pick exists only before the snap.
   - When a switch happens, the stick's current direction applies to the new man at once. There is no grace period like Madden's Defensive Switch Assist (C4).
6. **Cuts are slow at speed: heavy by design.** The user's man moves under the same physical steering law as the CPU's (`PlaySim::steerVel`, `PlaySim.cpp:4940-4973`). Timed with a copy of that law (session scratchpad `steer.py`, 60 Hz):

   | Player (speed / accel / agility) | Start speed | Stick turned 90°: heading reaches 45° | Stick reversed: comes to a stop |
   |---|---|---|---|
   | 80 / 80 / 80 back | full | 700 ms | 900 ms |
   | 80 / 80 / 80 back | half | 367 ms | 450 ms |
   | 95 / 95 / 95 elite | full | 750 ms | 950 ms |
   | 60 / 60 / 60 lineman | full | 650 ms | 850 ms |

   Rest to 90% of top speed takes 1.5-2.1 s.
   - These numbers are physically plausible. The sim was fitted to tracking data (see `sim-nfl-calibration`).
   - But Madden and FC say they deliberately give the user more change of direction than reality (R1, R2). In TAG the sharp cut lives only in the juke impulse (`startMove`).
   - One side effect: at full speed agility adds only 0.03 yd/s² per point of lateral grip, so an elite back turns no quicker in degrees than a lineman.
7. **The user's man animates like the other 21.** Motion matching searches at most every 66 ms. A new pick holds 0.3 s unless the path turns at least 30° or the speed changes by 2.5 m/s, and blends take 0.3 s (`Render/TagMotionMatchInstance.cpp:265-286`, `.h:124`). The predicted trajectory extrapolates measured acceleration, not the stick's intent, except on a start from rest (`Render/Athlete.cpp:2683-2749`). Nothing is tuned specially for the user-controlled athlete (grep of `Render/` finds no user flag).
8. **Latency was never measured, and nothing is tuned for it.**
   - Nothing is set for it: no `r.GTSyncType`, `r.OneFrameThreadLag`, `t.MaxFPS` or vsync settings (`Config/DefaultEngine.ini`). The Reflex plugin is in the engine (`Engine/Plugins/Runtime/Nvidia/Reflex`) but not enabled in `TAGGridiron.uproject`.
   - The launcher runs uncooked `UnrealEditor.exe -game -windowed` at 1080p on a 1440p 165 Hz monitor (`Play TAG Gridiron (Unreal).bat`).
   - The sim adds its own delay. It steps at 60 Hz (`STEP`, `MatchDirector.cpp:58`) and the renderer interpolates between the previous and current step (`SyncPlay(..., Acc/STEP)`), so about 8-17 ms passes between a press and the start of its effect. I worked this out from the code; it was not measured.
9. **No difficulty levels, assists, colorblind or HUD options, or practice mode.**
   - Settings (`UI/GridironOverlay.cpp:2132-2154`) cover only controls, captions, right-stick inversion and three volumes. Team select adds weather, day/night and quarter length (`UI/TeamSelect.cpp:691-695`).
   - No difficulty or assist code exists (grep), and there is no practice or tutorial mode.
   - The receiver colours (`ButtonColor`: blue, red, pink, green, grey) carry meaning that protan and deutan players will confuse. The icon shapes do help.
10. **The same long text hints come back on every play.** For example "R2 sprint R-STICK up truck … TRIANGLE hurdle" (`MatchDirector.cpp:7757`). `bCarrierHint` resets at each snap (`MatchDirector.cpp:3685`), and nothing fades a hint out once the player has learned the move.
11. **The play-call screen can be browsed but gives no help choosing.**
    - It shows three plays per page (`CallPerPage = 3`, `UI/GridironOverlay.h:353`) out of a 320-500 play book. You browse by formation family, set, and type tab; R-stick jumps five pages, R3 repeats the last play, and TRIANGLE is a blind "Coach's pick" (`UI/GridironOverlay.cpp:1206-1320, 1960-1990`).
    - Nothing suggests plays for the situation, although `Game::cpuOffense` already weighs down, distance, score and tempo (`Sim/Game.cpp:426-455`).
    - Nobody has measured how long a call takes.
12. **No controls for the ball in the air.** The defender has no play-the-ball or swat button, and the receiver's "aggressive catch" (TRIANGLE held) changes the animation only (`UserInput`, `Sim/PlaySim.h:463-496`; `MatchDirector.cpp:7761-7766`). Madden has a Ball Hawk assist and catch controls (C4, C7). Here, user skill cannot change a contested ball.
13. **The camera shake is per-frame white noise** (`FRandRange` on every axis each frame, `MatchDirector.cpp:7260-7263`). It fires only on touchdowns (25 cm) and sacks (12 cm), after the whistle. A big hit, a truck or a hit-stick hit gets no hit-stop, no shake and no rumble.
14. **A player-facing string names a rival brand:** "Madden-style play call…" (`UI/GridironOverlay.cpp:688`).
15. **Rumble cannot reach one specific pad in multiplayer, as the input is set up today.** Every device is mapped to platform user 0 (`Config/Windows/WindowsInput.ini`). GameInput rumble then goes to every device of that user (U3), and the PlayerController's force feedback has no idea which pad belongs to which player.

---

## 3. Actions (ranked by player-felt impact per unit of effort)

Effort: S = under a day, M = days, L = weeks. "Sim-neutral" means CPU-vs-CPU results stay bit-identical: SimTests and
SimCalibrate with the same seeds give the same output. That matters because the sim is calibrated to NFL data, and every
action below is meant to keep it.

### 1. Scaled radial stick dead zone and full-length diagonals. **S**
- **Build:**
  - One helper, `TagInput::Stick(FVector2D raw, float dz = 0.15, float outer = 0.95)`. It applies a radial dead zone, rescales the length to (\|v\|-dz)/(outer-dz) clamped to 1, and keeps the direction.
  - Set the legacy stick `DeadZone` to 0 in `Config/DefaultInput.ini`, so the raw value reaches the helper.
  - Use the helper in `ReadUserInputSlot` (`MatchDirector.cpp:7572-7574`), `FTagInputHub::Axis`, and the stick reads in `MatchKicking.cpp:106-110` and the pre-snap walk.
  - Leave the right-stick flick detection on length; it already uses magnitude.
- **Player feels:** the runner goes where the thumb points at every angle, and a diagonal sprint is as fast as a straight one.
- **Dependencies:** none. Slate's left-stick navigation keys come from the platform layer, but check that the menus still navigate (`-TagInputTest`).
- **Verify:**
  - (a) Log raw and processed stick values at 10°, 20°, 30° and 45° (the `-TagPadPlayTest` path can inject axis values): the heading should equal the stick angle within 1°.
  - (b) Telemetry: a full-diagonal sprint should reach the same top speed as a full-up sprint (the same `vmax` within 1%).
  - (c) Fred's thumb test: a 10° drift toward the sideline from the hash.
  - Sim-neutral, because only user input changes.

### 2. Rumble layer with an Off / Low / Normal setting. **S** single player, **M** per-pad in multiplayer
- **Build:**
  - Add `Game/TagHaptics.*`, a table from sim and director events to effects. Strength would scale with the event's measured jolt; `MaxJoltThisPlay` and the contact tracking already exist.
    - tackle or contact: low motor, 60-150 ms
    - big hit or trucked: both motors, 180-250 ms
    - sack: both motors, about 200 ms
    - catch in traffic: high motor, about 60 ms
    - fumble: two pulses
    - kick strike: short high-motor tick
    - snap: faint tick
    - touchdown: long low roll
  - Play effects through `APlayerController::PlayDynamicForceFeedback`, or `ClientPlayForceFeedback` with an asset made in C++.
  - Add a strength setting (Off / Low / Normal) to Settings (XAG 110), saved in GameUserSettings.ini like `[TAG.Controls]`.
- **Multiplayer:** the engine routes rumble to all pads of user 0 (U3). Either turn rumble off in multiplayer at first, or call `XInputSetState` per XInput slot from the hub. The second needs the hub to map an `FInputDeviceId` to an XInput slot.
- **Player feels:** contact in the hands. Rumble is the cheapest feel upgrade available.
- **Dependencies:** none.
- **Verify:**
  - Log "TAG haptic <event> <ms> <strength>". A scripted `-TagPadPlayTest` run should fire one per tackle.
  - Check by hand that an Xbox pad rumbles.
  - **Test whether a DualSense rumbles through GameInput on Windows** (open question 1).
  - With the setting at Off, nothing should be sent.

### 3. Start the throw on the press. **S-M**
- **Build:** in `ReadUserInputSlot`, send `throwTo` with a provisional style as soon as the receiver button goes down, so the windup starts on that frame. The pass type then stays open until the release point (`pendingThrow` time):
  - A release before 0.15 s, with no modifier held, makes it a touch or lob.
  - A button still held at the release point makes it a bullet.
  - A second tap during the windup turns it into a pump fake. This needs a small sim change: `PendingThrow` must accept a later style or a cancel, with the "pump" event replacing the throw.
  - Remove the 0.22 s wait.
- **Player feels:** the quarterback reacts the moment the button goes down, and a lob no longer feels late.
- **Dependencies:** sim change confined to the user path (`userish`), so it is sim-neutral.
- **Verify:**
  - Log press-to-windup time in frames. It is about 9-18 frames at 60 Hz today (0.15-0.3 s); the target is 0-1 sim step.
  - Log press-to-release time, then a lob, a bullet and a pump fake through `-TagPadPlayTest`.
  - Run `-TagTune=throwStyle=N` before and after: the flights should be unchanged.

### 4. A 150 ms input buffer for moves, plus a cue when one is refused. **S**
- **Build:**
  - In the director, keep `stickMove`, `rushMove`, `dive`, `strip` and `switchReq` alive for 150 ms (9 steps), and re-offer them each step until `startMove` accepts. That requires `startMove` to return its result to the director: it already returns `bool`.
  - Clear the buffer on success, on expiry, or on a new press.
  - When a move expires unused, flash the highlight cue's pill grey, so the player learns the timing instead of thinking the pad failed.
- **Player feels:** "I pressed it and nothing happened" goes away, especially right after a catch.
- **Verify:**
  - Count "move asked / accepted / expired" per play in the log.
  - A scripted flick at `animT` 0.5 s after a catch should fire at 0.6 s. Today it is dropped.
  - Sim-neutral (user only).

### 5. Switching that follows the ball, with a short grace period. **S-M**
- **Build:** in `PlaySim::switchUser`:
  - Ball in the air: rank by time to reach `ballLanding()` (the function already exists).
  - Ball carried: rank by intercept time against the carrier's predicted path (`predictPos`). Skip engaged linemen and men already beaten.
  - Before the catch: keep "nearest to the ball".
- Live directional switching: hold CIRCLE and push the left stick, reusing `switchUserToward` and the pre-snap hold logic. This keeps one rule before and after the snap.
- **Switch grace:** for 0.25 s after a switch, or until the stick moves more than 45° from where it was, ignore the stick and let the AI keep driving the new man. That is Madden's Defensive Switch Assist principle (C4). Add a setting (On/Off).
- **Player feels:** CIRCLE on a deep ball lands on the defender who can make the play, and the new man does not take a step the wrong way.
- **Verify:**
  - Scripted scenario: 50 deep passes with an automated switch at the throw. Log the switched man's distance to the catch point against the best possible defender's; the aim is the best man in at least 90% of throws.
  - Count "switched man ran away from the ball in the first 0.3 s" before and after.
  - Sim-neutral (user only).

### 6. Make the user's man the most responsive animator on the field. **S**
- **Build:** give the user-controlled athlete its own motion-matching settings. `MinDwell`, `DwellTurnDeg` and `BaseThrottle` are already per-instance members (`TagMotionMatchInstance.h:124`). The director knows `userControlled` and can set a flag on the athlete in `SyncPlay`.
  - search throttle: 0-16 ms
  - `MinDwell`: 0.12 s
  - `DwellTurnDeg`: 15°
  - blend: 0.2 s, Holden's "normal" (R5)
  - For the trajectory, predict with the sim's own `steerVel` toward the user's `want`. The sim uses the same law, so the prediction is exact, and the search sees the cut about 100 ms earlier (my estimate).
- **Player feels:** the body turns when the thumb turns; less "on rails" lag behind the stick.
- **Cost:** 1 of 22 athletes searching every frame. The perf note says all 22 at full rate cost about 25 fps, so one should cost about 1 fps. Check it with `Tools/Perf/ab.ps1`.
- **Verify:**
  - Use `-TagMMTrace=<number>` or telemetry to measure the lag between the sim's heading change and the mesh's (pelvis) heading change on a scripted 90° cut. Target: 6 frames or fewer (100 ms) for the user's man. This is my target; no source gives one (R7).
  - Foot-slide metric unchanged.
  - Bench within 2 fps.

### 7. Measure latency, then tune it. **S**
- **Build:**
  - Enable the engine's Reflex plugin in `TAGGridiron.uproject`. Fred has an RTX 4080 SUPER, which suits it.
  - Add `-TagLatencyLog` to print PC Latency, or read it with FrameView.
  - A/B with the same seed and scene:
    - `r.GTSyncType` 0 against 2 (mode 2 with vsync on)
    - `r.OneFrameThreadLag` 1 against 0
    - `t.MaxFPS` at about 158 on the 165 Hz monitor, against uncapped
    - the launcher's windowed mode against borderless fullscreen at native resolution
  - Keep whatever gives the lowest PCL without losing more than 5% fps. Put it in DefaultEngine.ini and the launcher.
- **Player feels:** a snappier stick and a snappier snap. Possibly small.
- **Verify:**
  - Average PCL in ms before and after.
  - Sanity reference: about 50 ms end to end was "very good" on 60 Hz consoles (L1), and fighting games ship at 3-5 frames (L2).
  - If Reflex is not wanted, a 240 fps phone video of an LED wired to a pad button gives the same answer (L2 method).

### 8. A user-only movement option ("Authentic" / "Responsive"). **S** to build, **M** to tune
- **Build:**
  - A setting that scales `latHigh`, `aBrake` and `grip` for **user-controlled men only**, in `steerVel` or `applyUser`.
  - Start points: lateral +20%, braking +10%, and more of the agility rating counting at speed (0.06 instead of 0.03 per point).
  - Default "Authentic", the current law. Top speed stays untouched.
  - This is the FC 26 Competitive/Authentic split (R1) and Madden 26's "speed true, change of direction favoured" stance (R2), applied only where the player's hands are.
- **Player feels:** cuts land sooner, at the cost of some realism. Let Fred choose by feel.
- **Dependencies:** do actions 1 and 6 first, or the stick and animation lag will mask the change.
- **Verify:**
  - Re-run the `steer.py` table against the new constants: heading to 45° at full speed should be about 550-600 ms instead of 700-750.
  - SimCalibrate unchanged (no user in CPU games).
  - Fred A/B playtest.

### 9. Coach suggestions plus a call timer on the play-call screen. **M**
- **Build:**
  - A "SUGGESTED" first page of three cards with a one-line reason each, for example "3RD & 7 - beats single-high" or "HURRY-UP - sideline route".
  - Rank them with the same situational weights as `Game::cpuOffense` / the CPU defense caller, but **without touching the game RNG**: use a hash of (drive, play number, team), or a separate `Rng`. Otherwise suggestions would change the sim.
  - Make TRIANGLE ("Coach's pick") choose the top suggestion visibly, not blindly.
  - Add a log line with the time from `ShowPlayCall` to `CallPick`.
- **Player feels:** a good call in a few seconds, with the big book still there to browse.
- **Verify:**
  - Median time to call over 20 user calls before and after. Aim for under 8 s; the 40 s play clock leaves room, but faster calls make the flow feel quicker.
  - `-TagCallTour` shots.
  - SimTests bit-identical.

### 10. Hit-stop, a proper trauma shake, and rumble on big hits. **S**, after action 2
- **Build:**
  - **Hit-stop:** on a user-involved "bighit" or "trucked" event, hold the sim for 3-5 steps (50-80 ms). Do not advance `Acc` in the `EMatchState::Live` loop while the athletes' animation is also held. The sim is stepped at a fixed rate, so pausing steps changes nothing it computes. The game clock follows `Sim->t`, so it is unaffected too.
  - **Shake:** replace the `FRandRange` jitter (`MatchDirector.cpp:7260-7263`) with Eiserloh-style trauma: shake = trauma², driving Perlin offsets on position and rotation, with trauma added by jolt (F3). Drop the shake on touchdowns, or make it a slow sway.
  - A setting to turn hit-stop and shake off.
- **Player feels:** big hits land with weight, the way TV slow motion sells them.
- **Verify:**
  - With and without `-TagNoHitStop` on the same seed, the play results (yards, tackles, events) must match exactly.
  - Film a hit-stick hit with `-TagMoveShot` and check the frame count of the hold.

### Further actions (below the top ten, still worthwhile)

**11. Dynamic on-field trainer. S**
- Each hint line in `ReadUserInputSlot`, `MatchKicking` and the pre-snap panel gets a counter, saved to the ini. A hint shows until its action has been used successfully three times, then hides; it comes back after 10 plays without it being used.
- Settings: Hints On / Dynamic / Off.
- Verify: hint counters in the log, and a new player sees the carrier hint at most about five times.
- Principle: Madden 26's "Dynamic" trainer (A1).

**12. Ball-in-air controls. M**
- Defense: play the ball (an interception try) or swat it.
- Offense: possession or aggressive catch, which changes the sim's catch odds.
- Only on the user's man, and the sim's catch roll keeps its expected value for CPU men.
- Gives skill expression on the most important moment of a pass.
- Verify: catch calibration for a CPU QB throwing to CPU receivers unchanged; scripted user swats change outcomes in the expected direction.

**13. Difficulty and assists without stat cheating. M**
- Three levels change only CPU *decision* knobs, and only when a human is in the game:
  - `tune().aiMoveK` (CPU move strength)
  - QB read speed
  - coverage reaction (the `stun` / `playRec` terms)
  - pass-rush shed
- Ratings never change.
- Assists: switch grace (action 5), catch assist (auto-choose the catch type), heat seeker on/off.
- Verify: SimCalibrate on CPU-vs-CPU bit-identical at every level (no human means no change), and win rates over `-TagSoak` user-bot games step with level.

**14. Pass lead with the left stick. M-L**
- Left stick held at the release point moves the aim up to about 2.5 yd in that direction, screen-relative.
- The accuracy error widens with the lead length and shrinks with `accShort/Mid/Deep`.
- No meter by default (broadcast look). A meter is an option.
- Verify: telemetry of the aimed spot against the receiver; ConceptCal unchanged for CPU throws.
- Depends on action 3.

**15. Colour and size options. S**
- A colourblind-safe receiver palette (blue, orange, yellow, white, plus shapes), chosen in Settings.
- An "Enlarge on-field icons" option (A2).
- Verify: `-TagUiShots` with each palette, and a simulated deuteranopia filter on the shots.

**16. Brand-clean strings. S**
- Remove "Madden-style" from `GridironOverlay.cpp:688` and grep for any other player-facing rival names. This keeps TAG's own identity, and it is trivial.

**17. Menu input-to-visible check. S**
- Log the frame of each play-call key press against the frame its rebuilt page first paints (`BuildPlayCall` rebuilds the screen on every press).
- Target: the same or next frame.
- If a page turn takes more than 16 ms, cache the built card widgets per page.

---

## 4. Not worth doing (considered and rejected)

- **Raise the sim rate from 60 Hz to 120 Hz to cut input delay.** It would save about 8 ms on average, but it changes every integration result and with it the NFL calibration (`STEP` feeds every step). Actions 6 and 7 buy more feel for far less.
- **Migrate the UI to CommonUI or UMG.** The hand-built Slate system already does input routing per owner, glyph switching, focus, and one theme. CommonUI is set up through assets and the editor (U4), which fits a C++-only project poorly, and a rewrite would cost weeks with nothing the player would notice.
- **Move gameplay input to Enhanced Input.** `TagBind` already gives per-context remapping and the multiplayer hub. Enhanced Input's triggers do not include a buffer (U5). The only real gain, its radial dead zone, takes one function (action 1).
- **DualSense adaptive triggers and advanced haptics on PC now.** There is no documented Windows path in UE outside Sony's SDK (U3; unverified for third-party plugins). Revisit if a console port happens. The NBA 2K21 fatigue-trigger idea (F1) is good and maps directly onto `SprintStamina` when it does.
- **A QB vision cone.** Madden tried it, and players disliked it enough that it was removed in Madden 09 (C9).
- **An ML or "real coach" suggestion model, or a voiced "Coach Speak".** A ranked situational list (action 9) delivers the player-facing value. A model needs data TAG does not have.
- **A placement-and-accuracy meter on every throw by default.** It fights the broadcast look and moves the skill test into a meter. Offer it later as an option on top of action 14 if Fred wants a competitive mode.
- **A 3D player showcase on every menu.** Create-a-Player already has a turntable. The loading screen and the front end gain more from not building the stadium twice (a perf item in `perf-100fps.md`) than from more 3D.
- **A full Skills Trainer suite.** It costs weeks. A practice mode that replays a chosen play is cheap, because `-TagForcePlay` and the hurry-up "last play" path exist. Do that and the dynamic trainer (action 11) first.

---

## 5. Open questions

1. **Does a DualSense rumble through GameInput on Windows?** The engine sends `SetRumbleState` to all of user 0's devices, but I could not confirm that a DualSense on PC honours it. Test it on Fred's pad.
2. **What do Fred's pads report at a full diagonal?** The diagonal speed loss in section 2, item 1 assumes a circular gate (about 0.707 per axis). Log raw values once; Xbox and DualSense may differ.
3. **Does Madden hold back a lob to wait for a pump-fake double tap?** And does it start the windup on press? I found no source (C3). It does not change action 3, but it decides whether TAG can claim to be ahead here.
4. **What are Madden's or CFB's live switch rules?** Is the ball in the air ranked by the ball or by the catch point, and is there a right-stick switch? Not found. The FC 24 options (C5) and Madden's Switch Assist (C4) are the only official evidence.
5. **Madden's actual end-to-end latency.** Not found, so sports-game latency targets in this report lean on Mick West and fighting games.
6. **How the play-call screens of Madden 26/27 and CFB 26 lay out plays** (per page, search, recents). The pages were unreachable this session.
7. **The current PCL and pipeline latency of TAG itself.** Unknown until action 7 runs. It may already be fine at 120+ fps.
8. **Whether the 0.6 s / 0.9 s move lockouts in `startMove` exist for animation reasons** (clip length) or for balance. If for animation, the buffer in action 4 is the fix. If for balance, they may also need shortening, which needs MoveCal.
9. **Whether Fred wants the "Responsive" movement option at all.** It knowingly trades realism for control. It is his call by feel.
10. **Menu research.** The menu and front-end references (Persona 5, Forza Horizon, Destiny, MLB The Show) were not fetched this session. Section 1.6 is craft consensus from memory.
