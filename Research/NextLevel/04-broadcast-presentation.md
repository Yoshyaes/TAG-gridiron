# 04 - Broadcast presentation, cameras, and the time between plays

Researcher 4 of 7. Written 2026-11-23 against branch `look-and-feel` (working tree, uncommitted edits from other sessions included). Read-only: no code, assets, or config were changed, nothing was built or launched.

**How the sources were gathered.** Web search and page fetches both worked. Several sites refused automated fetches (HTTP 403): Operation Sports (articles and forums), the EA forums, and some SVG pages. For those, the claim comes from the search engine's result text, not from reading the page. Each such claim is marked *(snippet)*. Claims from my own background knowledge, with no source found, are marked *(memory)*. Everything about TAG Gridiron comes from reading the code. Line numbers refer to the working tree on 2026-11-23.

---

## 1. What the best studios and real broadcasts do

### 1.1 Real NFL production

**Camera complement.**
- NBC's Sunday Night Football runs 50+ cameras in 2025: a drone, SkyCam, a 4x super slow-motion 4K camera, goal-line and line-to-gain pylon systems, and RF/robotic units. Source: [SVG 2025](https://www.sportsvideo.org/2025/09/04/nfl-kickoff-2025-nbc-sports-embarks-on-the-road-to-super-bowl-lx-as-snf-celebrates-20th-anniversary/). Earlier counts were 39 in 2018 with dual SkyCams ([SVG 2018](https://www.sportsvideo.org/2018/09/06/nfl-kickoff-2018-from-green-zone-to-dual-skycams-nbc-sports-has-big-plans-for-sunday-night-football/)), plus pylon and line-to-gain "MarkerCams" added for the playoffs ([SVG 2020](https://www.sportsvideo.org/2020/01/10/nbc-sports-adds-pyloncams-line-to-gain-markercams-for-nfc-divisional-playoff-matchup/)). Confidence: high.
- A director actively uses far fewer cameras than the truck has. The 2011 DGA piece gives 8-14 cameras for a regular game, up to 20 on Sunday night, and 52 at Super Bowl XLIII. "High cameras" above the sideline still follow each play ([DGA Quarterly, Television Football](https://www.dga.org/craft/dgaq/issues/1103-fall-2011/television-football)). Confidence: high.
- The minimum useful set, from a school-production camera map: "C1 Hi 50 Game, C2 Hi 50 Tight, 3-High End Zone, 4-Low End Zone" ([ESPN3 school production manual, PDF](https://obo.uni.edu/sites/default/files/rfq/attachment_b_-_espn3_school_productions_broadcast_manual.pdf), *(snippet)*). The pattern: one wide game camera and one tight camera at the 50, then one high and one low behind the play. Confidence: medium. It is a school manual, used here as the skeleton of the pro plan.
- Pylon cameras are "meant as a first or second look" on goal-line plays. ESPN fed the two goal-line pylon cameras live and recorded the others for replay ([SVG 2015](https://www.sportsvideo.org/2015/09/11/nfl-kickoff-espn-pursues-definitive-goal-line-shot-with-pylon-cam-for-monday-night-football/)). Confidence: high.
- SkyCam as the primary game camera is a special-event look. NBC used it that way for some Thursday games. The SVG snippet says that on third downs and in the red zone they would still "likely use the traditional high Camera 1 position" ([SVG 2017](https://www.sportsvideo.org/2017/11/16/nbc-sports-confident-with-skycam-as-primary-game-camera-for-tnf-tonight/), *(snippet)*). Confidence: medium.

**Operator behaviour.**
- Broadcast operators frame "tight to medium before the snap, then wider as the play progresses". On a pass they get "wide enough so you won't lose it, and once the receiver catches the ball, zoom back in" ([Videomaker](https://www.videomaker.com/article/f2/7043-sports-videography-tips-from-a-tv-sportscaster) and similar guides, *(snippet)*). Confidence: medium. These are general broadcast guides, not an NFL network rulebook.
- An operator isolating a receiver keeps the cornerback in the frame (Russo, Fox, in [DGA](https://www.dga.org/craft/dgaq/issues/1103-fall-2011/television-football)). Confidence: high.
- I found no published source on how pros lose and reacquire the ball, or on reaction delay. Anything below on "human error" is inference.

**Director's cut between plays.**
- After a key play, Esocoff (NBC) cut from the live shot to the sideline and end-zone angles, then to the referee at the monitor, then to reactions from both benches. "We try to pay attention to both sides of every story." Russo (Fox): hold strong reaction shots, "All that matters is the moment". Esocoff again: "sitting on a shot for a little longer may be better" ([DGA](https://www.dga.org/craft/dgaq/issues/1103-fall-2011/television-football)). Confidence: high.
- Fox splits the work. The director (Russo) picks live shots, and the replay producer (Zyontz) picks replays and must "have a plan within ... three seconds". Reaction close-ups ("bummage") are the "little capper to the mini-story we just told in 10 seconds", and slow motion stretches a half-second reaction to 4-5 s ([The Ringer](https://www.theringer.com/2023/02/09/nfl/fox-nfl-super-bowl-broadcast-bummage)). Confidence: high. This is the clearest statement of the post-play sequence: replay(s), then a reaction capper, then back to live.
- Showing full huddles "gets dull" (Aceti), and downtime between plays was often up to 30 s ([DGA](https://www.dga.org/craft/dgaq/issues/1103-fall-2011/television-football)). Confidence: high.

**How the broadcast time splits.** A WSJ study of four broadcasts ([Fox Sports reprint](https://www.foxsports.com/stories/nfl/football-games-have-11-minutes-of-action)) found:
- about 11 minutes of action (10:43);
- "as many as 75 minutes" of players huddling, lining up or milling about;
- replays taking "56% more time" than live action;
- coaches and referees about 7% of the show, cheerleaders about 3 s.

FiveThirtyEight's later sample found 18 minutes of action ([FiveThirtyEight](https://fivethirtyeight.com/features/how-much-football-is-even-in-a-football-broadcast/), *(snippet)*). Confidence: medium-high, given the small samples. Takeaway: TV fills the gap between snaps with replays first, faces second and the huddle last.

**Replay conventions.**
- A key play gets several angles in a row: overhead, both end zones, a high-speed sideline cart and a handheld "defining shot" ([DGA](https://www.dga.org/craft/dgaq/issues/1103-fall-2011/television-football)).
- ESPN's replay lead aims to "present the very best look first" (search snippet attributed to ESPN Front Row; the fetched page was wrong).
- I found no published standard order of angles. Confidence: medium.
- Wipes in and out of a replay are a standard broadcast-package element. Madden 27 lists a "logo wipe" and a "replay wipe" among the seven parts of each seasonal package ([EA, Madden 27 Sights and Sounds](https://www.ea.com/games/madden-nfl/news/madden-27-sights-and-sounds)). Confidence: high.
- Speed ramps (real time easing into slow motion at the key moment) are common on TV *(memory)*. Confidence: medium.

**First-down line.**
- The line "obeys the visual rules of foreground objects occluding background objects". Some broadcasts add a blue line of scrimmage, and some turn the yellow line red on fourth down ([Wikipedia, 1st & Ten](https://en.wikipedia.org/wiki/1st_%26_Ten_(graphics_system))). Confidence: high.
- Down and distance drawn on the turf before the snap is common on current network broadcasts *(memory)*. Confidence: medium.

**Score bug design.** An SVG 2026 piece interviewing network graphics leads ([SVG](https://www.sportsvideo.org/2026/06/09/designing-the-modern-scorebug-how-broadcast-graphics-teams-are-rethinking-the-most-important-element-on-screen/)) gives these principles:
- Build the hierarchy first: score and clock are primary, "Protect that at all costs".
- Keep the base state minimal.
- Let typography carry the brand.
- Use motion for temporary hierarchy: the bug can "shrink, slide, or reconfigure itself for a few seconds", then return.
- Automate only what the data can detect.
- Test legibility "in a bar".

Confidence: high.

Two related findings:
- Timeouts remaining are a known weak spot that Fox's 2025 bug fixed ([Daring Fireball](https://daringfireball.net/2025/02/fox_new_scorebug_graphic_design), *(snippet)*).
- ESPN pulled a yellow down-and-distance box mid-game because yellow is the penalty-flag colour ([PFT](https://www.nbcsports.com/nfl/profootballtalk/rumor-mill/news/espn-quickly-changes-yellow-down-and-distance-graphic), *(snippet)*). Confidence: medium-high.

**Tracking-stat graphics (Next Gen Stats).** Completion probability was the first broadcast-facing model, from 2018. It is built from 10+ in-play measures such as pass distance, velocity and nearest-defender separation. Example on air: a one-handed catch with "28.8% completion probability" ([AWS](https://aws.amazon.com/sports/nfl/going-deep), [InfoQ](https://infoq.com/news/2021/10/aws-nfl-nextgenstats), *(snippet)*). Some stats become real-time graphics, others become talking points for the booth. Confidence: medium-high.

### 1.2 How games reproduce it

**NBA 2K.**
- Visual Concepts says its camera system is "built on the foundation of authentic broadcast TV configurations". They studied "the creative core of the broadcast TV Director" and added "variation to pan, tilts and zooms" to match a nightly broadcast ([NBA 2K12 Developer Insight, Operation Sports](https://forums.operationsports.com/features/1347/nba-2k12-developer-insight-12-presentation/), *(snippet)*).
- 2K12 also varied camera position and lighting slightly per arena for a "sense of displacement" on the road ([Kotaku](https://kotaku.com/if-this-is-the-only-nba-on-your-tv-all-year-nba-2k12-s-5853730)).
- 2K26 adds a "matrix cam", a 360-degree orbit replay ([NBA 2K26 Courtside Report](https://nba.2k.com/2k26/courtside-report/presentation/)).

Confidence: medium. 2K's camera internals are not published; I found no GDC talk.

**MLB The Show.**
- MLB 12 rebuilt its presentations around "the timing of the camera cuts, the framing" and "who we cut the camera to". It refused cuts that would reveal "that an actor ... had seemingly teleported across the field" ([PlayStation Blog 2012](https://blog.playstation.com/2012/03/15/mlb-12-the-show-how-trubroadcast-presentations-put-you-in-the-game/)).
- MLB 25 has "75 unique mid-game presentations" when an inning returns (standings, award races, game summaries, pitcher comparisons, trends) and "on-screen milestone alerts" ([theshow.com](https://theshow.com/news/presentation-commentary-updates-25/)).

Confidence: high. The lesson is a large pool of data-driven between-inning graphics, not a large pool of cutscenes.

**Madden.**
- Madden 26 has three primetime packages (Thursday, Sunday, Monday), each with its own theme and elements ([EA, Madden 26 presentation deep dive](https://www.ea.com/games/madden-nfl/madden-nfl-26/news/madden-26-gridiron-notes-presentation-deep-dive)).
- Madden 27 ([EA](https://www.ea.com/games/madden-nfl/news/madden-27-sights-and-sounds)) adds:
  - seasonal packages of seven elements (theme, commentary, intro graphic, score bug, end-quarter banner, logo wipe, replay wipe);
  - "Drive Starters now include replays";
  - new pre-snap cameras that depend on field position;
  - halftime segments (Impact Player, Playoff Hunt, first-half coverage);
  - a "Quick Presentation" where "cameras stay on the field when the play-call UI appears".

  Confidence: high.
- Player complaints:
  - "every game starts nearly identically" ([Bleacher Report](https://bleacherreport.com/articles/1092391-7-things-madden-nfl-still-hasnt-mastered), *(snippet)*);
  - "replays blurred and hidden under play selection art" ([EA forum, Madden 26](https://forums.ea.com/discussions/madden-nfl-26-general-discussion-en/broadcast-presentation-please/12475106), *(snippet)*);
  - users praising a quick-presentation option because it lets them "play a game of football" ([Operation Sports forum](https://forums.operationsports.com/forums/madden-nfl-football/855669-quick-presentation.html), *(snippet)*).

  Confidence: medium. These are forum voices, but they are consistent.

**College Football 25/26.**
- Runouts are tiered by context: Epic (primetime, light and drone shows), Standard, Quick (default in one career mode and for unranked CPU home teams), and a player-camera runout. They "adapt to the game's context".
- New broadcast banners run "from the first time you enter the field to between quarters and halftime stat updates".
- Pregame views compare top players or units ([EA, CFB 26 presentation](https://www.ea.com/games/ea-sports-college-football/college-football-26/news/cfb26-campus-huddle-presentation)).

Confidence: high. The lesson: one sequence in short, normal and long versions, picked by how big the game is.

**ESPN NFL 2K5** (still cited as the bar). A virtual Chris Berman "hosts halftime and postgame shows with video highlights" of your own game on Xbox; PS2 and GameCube got freeze-frames instead. The criticism was "inexplicably poor lip synching" ([NBC News 2004 review](https://www.nbcnews.com/id/wbna5801225)). Confidence: high. What fans remember is your own game's highlights at the half, not the studio set.

**Gameplay cameras.**
- EA's football games offer Standard, Wide, All-22, Broadcast and Zoom. Wide pulls the camera back without raising it; Broadcast raises it ([Sportskeeda](https://sportskeeda.com/college-football/news-how-change-camera-angle-college-football-25-road-glory-a-beginner-s-guide), [VideoGamer](https://www.videogamer.com/?p=1094269), *(snippet)*).
- A critic calls EA's broadcast cam "not quite a true broadcast camera" and "not really a playable broadcast camera" ([Operation Sports](https://operationsports.com/why-do-camera-angles-in-ea-football-games-continue-to-stink), *(snippet)*).

Confidence: medium. The lesson: playable cameras and TV cameras are different tools. Offer both and don't force one to be the other.

### 1.3 Techniques worth borrowing

- **Film idioms as small state machines.** In "The Virtual Cinematographer" (He, Cohen, Salesin, SIGGRAPH 96), each state is a canonical shot and each transition is a cut, organised as a hierarchical FSM ([Microsoft Research](https://www.microsoft.com/en-us/research/?p=150986), *(snippet)*). Confidence: high. It maps directly onto "after a sack: replay, rusher close-up, QB reaction, wide reset".
- **Pick by salience with memory.** Valve's response system (Ruskin, GDC 2012) fires the most specific rule that matches the world state and remembers what it has fired ([slides PDF](https://cdn.fastly.steamstatic.com/apps/valve/2012/GDC2012_Ruskin_Elan_DynamicDialog.pdf), *(snippet)*). Confidence: high. TAG's booth already works this way (cooldowns, `rot()`); shot and graphic choice can reuse the pattern.
- **Score plays for replay or highlight interest from game-state value.** A Sloan paper turns any state-value model into a continuous "interest" score ([Sloan](https://www.sloansportsconference.com/research-papers/data-driven-lowlight-and-highlight-reel-creation-based-on-explainable-temporal-game-models), *(snippet)*). A Google patent tags events during play, then builds replays from several camera views ([US8515253](https://patents.google.com/patent/US8515253), *(snippet)*). Confidence: medium. Methods only, no shipped detail.
- **Camera design pitfalls.** "50 Camera Mistakes" by John Nesky, GDC 2014 ([GDC Vault](https://gdcvault.com/play/1020460/50-Camera), *(snippet)*). Useful as a checklist for the gameplay cameras.

### 1.4 UE5 tooling for a solo developer

- **Gameplay Cameras plugin** (camera rigs, transitions, blend stacks): [Epic docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-camera-system-overview). Still flagged Experimental as of 5.7 according to a third-party guide, and "highly experimental and quite in flux" per an Epic staffer *(snippet)*. Confidence: medium.
- **`ULevelSequencePlayer::CreateLevelSequencePlayer`** plays a Sequencer asset from C++ (needs the `LevelSequence` and `MovieScene` modules): [API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/LevelSequence/ULevelSequencePlayer). Binding runtime-spawned actors into tracks is the hard part; I found no clean source for it.
- **Cine Camera** filmback, lens and focus: [docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematic-cameras-in-unreal-engine). TAG already uses this.
- **Take Recorder**: [docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/take-recorder-in-unreal-engine).
- **UMG:** I did not research UMG animation. TAG's UI is code-built Slate, and Slate can animate through lambdas, as `GridironOverlay` already does for fades.

---

## 2. Where TAG Gridiron stands

Paths below are relative to `Unreal/TAGGridiron/Source/TAGGridiron/`.

**Correction to the brief: the tackle cam and the catch cam are not in-game shots.** `-TagTackleCam` (`MatchDirector.h:610`, `TickTackleCam`), `-TagCatchCam` (`MatchDirector.cpp:7473-7508`) and `-TagLineCam` (7462-7471) are filming and measuring flags for look-dev. A player never sees them. The highlight reel is an offline tool (`Tools/Art/build_highlights.py`) that cuts the loading-screen movie.

### What it does well

**Cameras** (`Game/MatchDirector.cpp` `UpdateCamera`, 7065-7532):
- Four modes: `Broadcast`, `Sideline`, `Skycam` and `Player` (enum in `MatchDirector.h:32`). Note that `Broadcast` is the behind-the-offense gameplay camera and `Sideline` is the TV camera.
- The TV camera has three fixed stations at the 22, 50 and 78, 22 yd back and 13 yd up. It only pans and zooms, the station is picked before the snap, and a station change is a cut.
- The lens zooms to the men near the ball and widens with the ball in the air (30 yd) or on a kick (44 yd).
- Motion uses critically damped springs, a feed-forward lead on the carrier's velocity, and a safe-frame guard that keeps the carrier inside 72% of the half frame.
- Close shots run at f/1.2 and the game camera at f/5.6.
- Gameplay cameras are cycled by C or by holding VIEW for 0.5 s.

This is a solid operator for a solo build.

**Post-play cut-in** (`StartCutIn` 3850; trigger 3927-3935; shot 7287-7370):
- An 85 mm close shot of the player who made the play, on a TD, takeaway, sack or 20+ yard gain, for 2.8-3.5 s.
- It is placed once on the game camera's side, with one recut if blocked, and steadiness is logged.

**Instant replay** (`RecordReplayFrame` 4013, `StartReplay` 4063, `TickReplay` 4078-4147):
- Every visible man's bone pose and the ball are recorded at 30 Hz from snap to whistle+1.3 s, capped at 40 s.
- Big plays (TD, INT, lost fumble, sack, 20+ yards, 40+ on returns, blocked kicks) replay the last 5 s at 0.55x, about 9.1 s on screen.
- The replay plays the game's own recorded poses back, so it costs no extra animation work. The sound replays the play's own hits slowed down (`Audio/TagAudio.cpp:1137-1143`), with a whoosh cue (983).

**Breaks** (`BeginBreak` 5410, `BreakCamera` 5423):
- Quarter ends and the two-minute warning get a slow crane over the bowl with a banner, 3.4 s and 3.0 s.
- At a user game's halftime the box score appears (`Next` 3241); a CPU game gets a 6 s banner.

**Between-play life:**
- Men jog after the whistle (`TickPostJog`).
- A help-up on one ordinary tackle in four (3875-3907).
- TD group celebrations and takeaway celebrations (3945-4005).
- The umpire carries and spots the ball (`TickUmpire`), and the crew walks its mechanics (`TickCrew`).
- Benches, coaches, assistants, trainers and chain crew stand on the sidelines (`SpawnSidelines` 2548). The benches cheer on scores (`TickSidelines` 2658).

**Booth** (`Booth/`): a deep two-man system with storylines (`Booth.cpp storyLine` 2849, `quarterStory`, `halftimeStory`). It can hold the snap in watched games (`wantsHold` 333).

**Field:** a blue line of scrimmage and a yellow first-down line (`Render/Stadium.cpp:579-580`, `SetLines` 1582-1615), a down box and chain crew, and video boards showing the score.

### Where it falls short

1. **Replays have one angle, one speed and no wipe.**
   - The only angle is a field-level reverse, 5.6 m up, on a 50 mm lens at a constant 0.55x (`TickReplay` 4107-4126). Every replay in a game looks the same.
   - There is no speed ramp around the catch or hit, no second angle on a touchdown, and no visual transition. The cut in and out is hard (4072, 4142).
   - Selection is a yes/no test (3937-3938), so every sack replays and a third-down conversion or a big hit never does.
2. **The time between plays has no shots.** The only post-play shot is the one cut-in. There are no reaction cappers (bench, coach, QB), no crowd shots, and no wide reset.
   - In a user game: whistle, 1.8 s Post (`Tick` 8278), the optional replay, then the call screen over the game camera.
   - In a watched game: 1.4 s, the optional replay, a 1.1 s pre-snap plus the walk-in (`StartPresnap` 3681-3683), and any booth hold.
   - Nothing chooses shots by context, and nothing remembers what was shown.
3. **The score bug is a prototype** (`UI/GridironOverlay.cpp MakeScoreBug` 574-627).
   - It uses the engine default font (`TagTheme::Font` wraps `FCoreStyle::GetDefaultFontStyle`, `UI/TagTheme.cpp:14-19`) and team abbreviations, though logos exist (`RosterUI::Logo`).
   - It shows no timeouts, although the sim tracks them (`Sim/Game.h:141`), and no red-zone or two-minute states.
   - The flag state changes the down box from gold (1, 0.83, 0) to amber (1, 0.69, 0) (`GridironOverlay.cpp:43, 609`). The two are hard to tell apart, and the base colour is the yellow ESPN dropped because it reads as a flag.
   - Banners are 64 pt default-font text with a fade (75-93).
4. **The pregame open plays over an empty field.** I inferred this from the code; it has not been checked on frames.
   - `StartMatch` (2766-2774) puts the TV or behind-offense camera on `IdleFocus` (midfield, `MatchDirector.h:519`) and enters `Wait(0.5)`. The booth holds the kickoff for its open for up to 45 s (`Booth::wantsHold`, 339).
   - At that point no men are shown: `PrespawnAthletes` leaves everyone hidden and dormant.
   - The loading screen before it does have a matchup card and the reel (`UI/LoadingScreen`).
5. **No graphic follows the booth's storylines.** Story lines exist ("X is 14 of 20 for 180 yards"), captions are off by default (`MatchDirector.h:381`), and nothing on screen shows what the voice is reading. The only stat graphic is the lower third for a TD or 20+ yard play (3908-3922).
6. **The sim's tracking numbers are thrown away on screen.**
   - `PlayResult` holds `airYards`, `yac`, `catchSep`, `arriveSep`, `flightT`, `timeToThrow`, `pressured` and `brokenTackles` (`Sim/PlaySim.h:433-452`).
   - The ball holds the odds the catch was rolled with, `fateP` (376).
   - These are exactly the numbers broadcasts show from Next Gen Stats. Only the booth reads some of them.
7. **Substitutions pop.** `ShowOnly` (5261-5279) hides and shows men instantly. In `StartPresnap`, a man who was not on the field last play has no "from" spot and stands straight onto his formation spot (3630-3666). Personnel changes, which the 300-500-play books make common, appear as men blinking in.
8. **There is no presentation-pace setting, and skips are scattered.** SPACE or Cross skips the last 1.2 s of Post and the replay (8321, 4137); a confirm press skips the booth hold (8150-8163); hurry-up drops the replay. There is no Full / Standard / Quick choice.
9. **Halftime and postgame are menus, not shows.** A box score (`ShowBoxScore`) and `halftimeStory` in the booth. There are no highlights, although the replay buffer machinery to play them back already exists.
10. **There are no gameplay-camera options beyond the four modes.** No zoom or height setting, and no Wide, Zoom or All-22 presets like EA's.
11. **Measurement gap.** Per the memory note `tv-fidelity-goal.md`, the scorecard's presentation category "read 10 while post-play shots were filmed from behind the bench". Every action below therefore verifies with frames or contact sheets, not only a number.

---

## 3. Actions (ranked by player-visible impact per unit of effort)

Effort: S = under a day, M = days, L = weeks.

**Where the code goes.** `MatchDirector.cpp` is 8,363 lines and is edited by several sessions at once. New behaviour goes in new files in the director's existing split pattern (`MatchKicking.cpp`, `MatchPresnap.cpp`):
- `Game/MatchBroadcast.cpp` for shots, replays and pace;
- `UI/BroadcastGfx.cpp` for the bug, lower thirds and wipes.

Only small hooks then touch `MatchDirector.cpp`.

### A1. Score bug v2: hierarchy, logos, timeouts, state slides (Impact H, Effort S-M)
- **Build:**
  - Rebuild `MakeScoreBug` as one compact bar: logo plus abbreviation plus score per team, three timeout pips each (`Game->timeouts`), quarter and clock, and a down-and-distance cell kept neutral (dark or white, not gold).
  - Show the play clock only in its last 10 s.
  - Add a temporary-state slot that slides out of the bug for 2-4 s and returns. States: FLAG (flag yellow, used only for flags), TIMEOUT <team> (pip drains), RED ZONE, 2-MIN WARNING, TOUCHDOWN, and a one-line FIRST DOWN (replacing the centre-screen toast).
  - Use one OFL display face for the bug and banners, as LOOK-AND-FEEL-PLAN Phase 5 already calls for. Load it through `TagTheme::Font` so every screen changes together.
- **Visible result:** the always-on element reads as a TV package, and timeouts, a flag or the red zone can be read at a glance.
- **Where:** `UI/GridironOverlay.cpp` 574-650 (`MakeScoreBug`, `DownText`), `UI/TagTheme.cpp:14`, and the flag and timeout hooks already in `MatchDirector` (`SetFlag`, `Next`).
- **Dependencies:** a font file (OFL). Logos already exist.
- **Verify:**
  - (a) A scripted capture of each state (extend `-TagUiShots`).
  - (b) Over a `-TagHlFull` game, log every timeout and check the pips equal `Game->timeouts` after each one (0 mismatches).
  - (c) Downscale 1080p captures to 640x360 and check that score, clock and down are readable (the SVG "bar test").

### A2. Replay package: angle by play type, two angles on the biggest plays, speed ramp, wipe (Impact H, Effort M)
- **Build:**
  - Replace the single rig in `TickReplay` with three to four code-defined replay cameras, all reading the existing 30 Hz buffer:
    - **High end zone** behind the offense: wide, real speed. Best for runs and line play.
    - **Tight iso** from the near sideline on the key man (receiver at the catch, rusher on a sack, carrier on a break), 100-135 mm, slow.
    - **Low end zone / pylon** on goal-line touchdowns.
    - The existing **field-level reverse**.
  - Choose the angle by play type and keep variety. A TD or takeaway gets two angles back to back: first the "best look" (high end zone or pylon), then the tight iso.
  - Speed ramp: about 1.0x until 0.6 s before the key event, ease to 0.35x through it, back to 0.7x. The key time comes from the sim's play events, which the director already scans (`SeenEvents`).
  - A 0.35 s TAG logo wipe (Slate, full screen) in and out of each replay. It hides the cut and the return of live poses (`EndReplay`).
  - Hide the field lines during a replay (inference: they mark the next play, not the replayed one).
  - Stretch goal: freeze and orbit 180 degrees on a touchdown, as in 2K26's "matrix cam" or DGA's "freeze and spin".
- **Visible result:** replays look edited rather than looped, and touchdowns get a proper treatment.
- **Where:** `Game/MatchDirector.cpp` `StartReplay`/`TickReplay` 4063-4147 (moved into `MatchBroadcast.cpp`), plus a wipe widget in `UI/BroadcastGfx.cpp`.
- **Dependencies:** A4 for the choice of plays. Reuse the cut-in's `Clear()` test (7294-7306) so no replay camera sits behind a body.
- **Verify:**
  - In one `-TagHlFull` game, log "TAG replay angle=<a> ramp=<t0,t1> secs=<s>" per replay.
  - No first angle is used on more than 2 replays in a row.
  - Run the reel tool's flatness check over the replay frames: 0 frames above 0.5, the body-on-lens test already in `build_highlights.py`.
  - Frame-difference spikes, which mark cuts, fall only inside wipe frames.
  - Contact sheet of every replay's first, key and last frame.

### A3. Between-play shot director with memory (Impact H, Effort M)
- **Build:**
  - A small rule table of post-whistle "beats", each a code-defined shot that reuses the cut-in's placement, clear and blocked logic. Types:
    - hero close-up (exists);
    - opponent reaction (the beaten defender or the sacked QB, the "bummage" capper);
    - bench reaction (scoring team's sideline; `Sideline` actors exist);
    - coach (head coaches exist, `SpawnSidelines` 2595-2598);
    - crowd section (pack fans in the lower rows);
    - official signalling (penalties);
    - wide reset (TV camera).
  - Each rule has a context test (score, turnover, sack, 3rd/4th down result, big hit from `MaxJoltThisPlay`, late and close, home or away), a weight, a per-shot cooldown and a "same subject" cooldown.
  - The director picks 1-3 beats per play within a time budget set by the pace setting (A8), as an idiom chain in the He/Cohen/Salesin sense. Example: sack leads to replay, rusher close-up, QB reaction.
  - Save recent picks to `GameUserSettings.ini` so game 10 does not open its post-play beats the way game 9 did. The booth's per-match `rot()` could use the same store.
- **Visible result:** after the whistle the picture cuts like TV (play, replay, faces, back to the line), and the order differs from play to play and game to game.
- **Where:**
  - New `Game/MatchBroadcast.cpp`.
  - Hook in `OnPlayDead` (3927-3935, replacing the fixed `StartCutIn` call).
  - Generalise the `CutInTarget` branch of `UpdateCamera` (7287-7370) into "current shot".
  - The `Post`/`Wait` state timers in `Tick` (8131-8166, 8284-8327).
- **Dependencies:** A4 (replay score feeds the beat budget). Coach and bench close-ups need reaction clips beyond Idle/Cheer/Clap/Kneel for a "bad" reaction; see the open questions.
- **Verify:**
  - Over a full `-TagHlFull -TagHuddleSecs=4` game (that flag films the between-play time), log "TAG shot <type> <subject> <secs>".
  - Count per type.
  - Max run of the same type ≤ 2.
  - No 3-shot sequence repeated within 20 plays.
  - 0 flatness-blocked frames.
  - A contact sheet of the last 45 frames of every play (the method the tv-fidelity note calls for).
  - Across two games with the same seed: different opening shot sequences in each game's first 5 big plays.

### A4. Replay-worthiness score and budget (Impact M, Effort S)
- **Build:**
  - Replace the boolean at 3937-3938 with a score. Parts:
    - yards over a down-dependent expectation;
    - TD, takeaway, sack;
    - a third- or fourth-down conversion or stop;
    - red zone;
    - hit jolt (`MaxJoltThisPlay`);
    - spectacular or diving catch (`fateDive`);
    - a low-probability catch (`fateP` < 0.4);
    - late and close.
  - Keep a budget: about one replay per 4-6 scrimmage plays in Standard pace, every TD and takeaway always, and no two routine sacks back to back.
  - The score also ranks plays for A9 (halftime highlights).
- **Visible result:** the plays you want to see again are the ones replayed, and routine sacks stop wearing the feature out.
- **Where:** `MatchDirector.cpp` `OnPlayDead` 3936-3938.
- **Dependencies:** none.
- **Verify:**
  - Log the score's parts per play.
  - Over a full game: replays per game, share by reason, and longest gap.
  - Target bands are my inference, to tune with Fred: 10-18 replays in a 15-minute-quarter user game; every TD and takeaway replayed; no reason above 40% of replays.

### A5. Pregame open that is not an empty field (Impact H, Effort M)
- **Build:** a code-driven sequence under the booth's open (it already holds the kickoff):
  1. Establishing crane (`BreakCamera` exists, 5423).
  2. A full-width matchup card: logos, team colours, each side's top X-Factor or Superstar from the abilities system, weather.
  3. Both teams visible. Cheapest is the kickoff units standing at their 25s and benches populated. A tunnel run-out is the expensive version.
  4. A cut to the TV wide shot for the kickoff.

  Make it tiered like CFB 26: a short version by default and a long one for watched games. One press skips it (the existing confirm-skip at 8150).
- **Visible result:** the first 30-45 s of every game looks like a broadcast open instead of a held shot of turf.
- **Where:** `StartMatch` (2766-2774), a `Wait`-state hook, and `UI/BroadcastGfx.cpp` for the card.
- **Dependencies:** A1's font and theme.
- **Verify:**
  - First confirm the problem: `-TagPlayNow` offscreen capture of the first 60 s.
  - After: a contact sheet of the open, with no stretch over 2 s showing an empty field.
  - The booth open ends before kickoff in ≥ 9 of 10 runs (log).
  - A single press reaches the kickoff setup in ≤ 0.3 s.

### A6. Sim-truth stat tags on replays and lower thirds (Impact M, Effort S)
- **Build:** a one-line tag during the replay's key moment, Next Gen Stats style, only for numbers the sim actually has:
  - catch probability (`Ball::fateP`, shown when < 50%);
  - separation at the catch (`catchSep`);
  - air yards and YAC;
  - time to throw (`timeToThrow`);
  - carrier top speed (track the max of `|vel|` on the carrier during the play; new, trivial);
  - broken tackles.

  Pick the one most remarkable against league norms. This is honest data, not an ML model.
- **Visible result:** "CATCH PROBABILITY 24%" under a diving grab, a stat no competitor has to estimate.
- **Where:** `UI/BroadcastGfx.cpp`, fed in `StartReplay`. Data comes from `Sim->result` (`Sim/PlaySim.h:425-452`) and `Sim->ball` (376).
- **Dependencies:** A2 helps but is not required (the tag can ride the current replay).
- **Verify:**
  - Log each tag with its source field and value. A script checks every shown number against the play's telemetry or event row (0 mismatches).
  - Coverage: a tag on ≥ 80% of pass-play replays, and none shown from a field that was unset (-1).

### A7. Field overlays to broadcast convention (Impact M, Effort S)
- **Build:**
  - Make the first-down line red on 4th down (Wikipedia, 1st & Ten).
  - Use an unlit or emissive material so the line reads the same day and night. It is a lit cube today (`Stadium.cpp:579-580`).
  - Draw a down-and-distance mark ("3RD & 7") on the turf beside the line of scrimmage before the snap, faded out at the snap. *(memory-based convention)*
  - Hide both lines during cut-ins, the A3 shots and replays.
- **Visible result:** the field graphics match what a viewer expects, and fourth down feels different.
- **Where:** `Render/Stadium.cpp` `SetLines` 1582-1615 (add the down and a hide flag); calls in `CallPlays` 3450 and `StartPresnap` 3675.
- **Dependencies:** none.
- **Verify:**
  - Captures before the snap on 1st, 3rd and 4th down, day and night.
  - The line is visible at the TV station distance at 720p.
  - Not visible in any replay or cut-in frame (sheet).

### A8. Presentation pace setting and one skip rule (Impact M, Effort S)
- **Build:**
  - Settings > Presentation with three levels:
    - **Full:** all beats; the default in Watch a Game.
    - **Standard:** about 6-10 s of beats on a big play, 0-2 s on a routine one.
    - **Quick:** the replay only on TD or takeaway, no beats, the camera stays on the field under the call screen (Madden 27's "Quick Presentation").
  - One rule: any beat or replay skips on a single confirm press, and the next one does not start for 0.3 s (avoids double skips).
  - Saved in `GameUserSettings.ini` `[TAG.Settings]` like captions.
- **Visible result:** players who want TV get it; players who want to play aren't held up. This answers the main Madden complaint found in the forums.
- **Where:**
  - `UI/GridironOverlay` `ShowSettings`.
  - `MatchDirector` Post/Replay/Wait timers (8278, 8321, 4137).
  - The A3 budget.
- **Dependencies:** A3 for most of the value. On its own it gates the replay and cut-in.
- **Verify:** over a full game per setting, time from the whistle to the call screen opening on routine plays.
  - Targets (inference): Quick median ≤ 2.5 s; Standard median ≤ 4 s with p90 ≤ 12 s; Full unconstrained.
  - Skip latency ≤ 1 frame after the press.

### A9. Booth-synced graphics (Impact M-H, Effort M)
- **Build:**
  - Give `tag::booth::Line` an optional graphic payload (template id plus fields).
  - Set it where Story, Score and Break lines are made. The topic keys are already structured, for example `"qb:<pid>"` and `"rb:<pid>"` in `storyLine` (`Booth.cpp:2849-2890`).
  - Show the graphic when the line starts (`started()` through `ATagBooth`) and hide it when the line finishes. On TV the voice reads the graphic.
  - Templates: QB line, rusher line, receiver line, team comparison, drive summary (plays / yards / time, at the end of a drive), third-down conversions, X-Factor zone status.
  - The same templates fire from game state without the booth (drive summary after every score or punt; "3RD & 8" plus the team's conversion rate before the snap), so captions-off and booth-off games get them too.
- **Visible result:** what the analyst says appears on screen at the moment he says it, the way MLB The Show's "75 mid-game presentations" and CFB's banners work.
- **Where:**
  - `Booth/Booth.h:40-54` (`Line`), `Booth/Booth.cpp` storyLine / quarterStory / halftimeStory.
  - `Booth/TagBooth.cpp` (forward on start).
  - `UI/BroadcastGfx.cpp`.
- **Dependencies:** A1's look.
- **Verify:**
  - `BoothScript.exe` (headless, `Tools/build_booth.bat`) prints the share of Story lines with a graphic (target ≥ 70%).
  - Extend its FACT CHECK so graphic numbers equal the stats (0 contradictions over 6 seeds, as today for calls).
  - In game, log the start time of each graphic against its line (|Δ| ≤ 0.3 s).

### A10. Halftime and postgame show with your game's highlights (Impact H, Effort L; M without new art)
- **Build:**
  - Keep the frames of the top 6-8 plays by the A4 score, copying the replay buffer out at `ResetReplayBuf`.
  - At halftime and the final whistle, play them back through the existing replay path: A2 angles, a title card per clip, and booth voice from `halftimeStory`.
  - Follow with the box score and two or three A9 graphics (leaders, team comparison).
  - The sides stay off the field during it; the crane shot frames each clip.
  - This is the 2K5 halftime show without its expensive part, the virtual host.
- **Visible result:** a halftime that is about the game you just played.
- **Where:** `MatchDirector.cpp` `ResetReplayBuf` 4055 and `Next` 3231-3243 (halftime); `ShowFinal` 4149; `MatchBroadcast.cpp`.
- **Dependencies:** A2 and A4. Memory needs checking: each recorded frame holds every visible man's local bone transforms, and I could not establish the skeleton's bone count (see open questions).
- **Verify:**
  - `-TagQuarterLen=90` game to halftime: the log lists the kept clips with their scores.
  - Contact sheet of the show.
  - Process memory before and after keeping the clips (log), within a budget Fred sets.
  - 0 pose pops at clip boundaries (wipe-covered).

### A11. Substitutions jog on and off (Impact M, Effort M)
- **Build:**
  - At `StartPresnap`, a man entering without a "from" spot starts at his team's sideline, level with the ball, and walks in with `Sim->walkFrom`.
  - A man leaving is kept visible and jogs to the bench before being hidden.
  - The CPU's set timer already waits for walkers (3683), so this only lengthens pre-snap when personnel changes.
- **Visible result:** no one blinks into the formation. Personnel changes look like football.
- **Where:** `StartPresnap` 3630-3673, `ShowOnly` 5261-5279; the post jog for leavers.
- **Dependencies:** none.
- **Verify:** per pre-snap telemetry, count men who become visible inside the camera frustum within 2 m of their formation spot. Target 0, against a current count I expect to be high (run first to get the baseline).

### A12. Gameplay camera presets and the TV operator's framing (Impact M, Effort S)
- **Build:**
  - (a) Add Wide (back, not up) and Zoom presets of the behind-the-offense camera, plus height and zoom sliders in Settings, mirroring EA's Standard / Wide / Zoom / All-22 / Broadcast set.
  - (b) For the TV camera, follow the operator rules found:
    - a tighter formation frame before the snap (about 18-20 yd instead of the fixed 25 at 7239);
    - widen at the snap and drop-back;
    - re-tighten on the catch, not only when the ball lands.
  - (c) Optionally, a 0.1-0.2 s reaction lag on reversals in Watch mode only. This is inferred; no source.
- **Visible result:** a playable camera for every taste, and a TV camera that breathes with the play.
- **Where:** `UpdateCamera` 7076-7115 and 7224-7245; Settings screen.
- **Dependencies:** none.
- **Verify:**
  - Captures per preset at own 5, midfield and opponent 10.
  - Carrier-in-safe-frame share in `-TagHlFull` telemetry stays ≥ 98%, the level the realism plan reports.
  - Width-over-time plot for 10 pass plays shows the widen and re-tighten.

**Order to build:** A1, A4, A2, A3, A8 (one presentation batch); then A5, A6, A7; then A9; then A10 and A11. A12 can go anywhere.

---

## 4. Not worth doing (now)

- **Sequencer-authored between-play cutscenes with runtime bindings.** Every shot here aims at men the sim places differently each play. Code-defined shot rigs reuse the cut-in's placement and clearance logic and stay in C++. Binding runtime actors into Sequencer tracks is fiddly, and I found no clean source on it. Sequencer could still suit fixed stadium flyovers if art wants hand-keyed moves; `BreakCamera` already does that in code.
- **Moving to the Gameplay Cameras plugin.** It is still experimental and changing (Epic staff, *(snippet)*), and the existing spring camera works and is measured.
- **A virtual studio host or sideline reporter on camera.** It is heavy on art and animation, and lip sync was the one criticism of 2K5's shows even with ESPN's budget ([NBC News](https://www.nbcnews.com/id/wbna5801225)). Voice plus highlights plus graphics gets most of the value.
- **Replaying every play like TV does.** TV spends more time on replays than on action (WSJ), but the forums complain about the opposite: presentation in the way of playing. A budget (A4) and a pace setting (A8) fit a game.
- **Simulated operator mistakes (losing the ball) in the gameplay camera.** They hurt play. At most a hint of lag in Watch mode (A12c).
- **Skycam as the default.** Networks treat it as a special-event primary camera ([SVG 2017](https://www.sportsvideo.org/2017/11/16/nbc-sports-confident-with-skycam-as-primary-game-camera-for-tnf-tonight/), *(snippet)*). Keep it as an option.
- **Seasonal or primetime packages, mascots, CFB-scale traditions.** These are asset-heavy and need a franchise or calendar mode to matter. One night-game variant of the A1 bug and wipe is cheap once A1 exists, but later.
- **A language model choosing shots or graphics live.** The booth writer was already paused over cost (`broadcast-booth.md`), and shot choice must happen within a frame or two. Rules with memory are enough.
- **Live pylon or line-to-gain cameras as gameplay shots.** On TV they are replay angles. One pylon replay angle in A2 covers it.

---

## 5. Open questions

1. **Is the pregame open really an empty field?** Inferred from `StartMatch`, `Wait` and the hidden prespawn; not seen on frames. A 60 s `-TagPlayNow` capture settles it before A5 is scheduled.
2. **How heavy is a kept replay?** I could not establish the skeleton's bone count. A frame stores every visible man's local bone transforms (`AAthlete::CaptureReplayPose`, `Render/Athlete.cpp:4224`). The cost of keeping 6-8 plays for A10 needs a measured number.
3. **Are there negative reaction clips for coaches and benches?** Sideline people have Idle/Cheer/Clap/Kneel (`model-pack-characters.md`). I did not check whether the mocap pack has dejected coach or bench reactions for the A3 cappers. Without them, the "losing side" beats will use QB or defender captures only.
4. **Does `ShowOnly`'s `ResetPose` at the play call snap a man still on the turf?** It runs on everyone at `CallPlays` (5275). I did not trace whether it is visible behind the call screen.
5. **No source on how real operators lose and reacquire the ball or how fast they react**, and none on a standard replay-angle order. A12c and A2's angle order are inference.
6. **2K's camera internals.** Operation Sports blocked the fetch of the 2K12 developer insight, so I read only the search snippet. No GDC talk was found.
7. **Pace targets.** The numbers in A4 and A8 are my starting points. Fred prefers playtesting to scorecards (`playtest-over-scorecard.md`), so he should set the Standard pace by playing it.
8. **Possible conflict with the plan's ground rule (outside my domain).** `LOOK-AND-FEEL-PLAN.md` treats MaddenRef as reference only. The memory notes say the sideline, coach and fan models come from a personal Madden 19 model pack. Close-up presentation shots (A3) put those models in the spotlight. Worth confirming before building the coach and bench shots.
