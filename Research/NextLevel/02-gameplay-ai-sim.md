# 02 - Gameplay AI and football simulation

Researcher 2 of 7. Domain: how the 22 men decide what to do, and whether the outcomes match real football.
Date: 2026-11-23. Read-only pass over the code (branch `look-and-feel`, uncommitted working tree); line numbers are as
of today and will drift because other sessions are editing `PlaySim.cpp` and `Game.cpp`.

**Method and limits.** I used web search and page fetches. EA's Madden 26 and College Football 26 gameplay deep dives,
the ESPN win-rate explainer, and the nflreadr FTN and Next Gen Stats data dictionaries were fetched and read directly.
Other claims come from search-result summaries of pages I could not open (403/404); those are marked "(summary)". The
shared web-search budget ran out near the end, so I could not check two things: the licence terms for Big Data Bowl
data, and the exact Big Data Bowl 2025 column names. Both are listed under Open questions. "Inference" marks my own
reasoning, as opposed to what a source states.

---

## 1. What the best studios do

### 1.1 Coverage AI (EA, 2025)

- **Named match-coverage rules, not generic zones.** Madden 26 added a Cover 4 "Push" check, which bumps coverage to
  the trips side and forces man matches as receivers release. It also revamped the "China" call (the corner takes the
  deeper route, the flat defender the short one). Cover 3 Sky/Cloud/Hard flat play "country cover 3" with spot drops,
  and Cover 3 Match and Buzz play "Skinny" and "Skate" against 3x1 and bunch sets. Deep defenders reach their
  landmarks faster the deeper they go.
  [Madden 26 deep dive](https://www.ea.com/games/madden-nfl/madden-nfl-26/news/madden-26-gridiron-notes-gameplay-deep-dive).
  Confidence: high (official page, read directly).
- **Man coverage uses a cut-reaction formula.** Madden 26's man coverage uses "a new cut-reaction system" in which the
  ratings matchup (MCV against the receiver's short/medium/deep route running) and the defender's leverage decide the
  separation on a break. Outside leverage sharply cuts a receiver's chance to win on outside cuts. "Route Commit" locks
  a defender onto outside breakers and loses in-breakers. Same source; confidence high.
- **Eyes on the ball before a pick.** Madden 26: "AI defenders must face or look at the ball before attempting a catch,
  which reduces 'psychic INTs'." CFB 26: "When a DB doesn't have eyes on the ball, he won't make those unrealistic,
  blind leaps anymore." Interceptions there need the defender to be in phase, facing the QB and well placed; otherwise
  he swats the ball.
  [CFB 26 deep dive](https://www.ea.com/games/ea-sports-college-football/college-football-26/news/college-football-26-campus-huddle-gameplay-deep-dive).
  Confidence: high.
- **Community view.** Operation Sports (summary) says Madden 26 coverage is more realistic, but "some defenders still
  have the same psychic abilities ... they can somehow anticipate the ball coming their way before it's even been
  released."
  [OS newsletter](https://newsletter.operationsports.com/p/the-three-biggest-disappointments-with-madden-26-gameplay).
  EA forum threads say the same about zone coverage and scrambles
  ([example](https://forums.ea.com/discussions/madden-nfl-26-bug-reports-en/defensive-ai-coverage-and-run-support-issues/12470998)).
  Confidence: medium; these are anecdotes, but the theme repeats.

### 1.2 Run defense

- **Gaps by role.** Madden 26 made gap assignments "more consistent using play data." A and B gap defenders play the
  run first. C, D and E gap coverage defenders play the pass first "unless they read a block." CFB 26 says defenders
  "keep assigned gaps based on the play call and alignment, which limits blind crashing," and that linebackers and
  safeties trigger earlier into downhill fits. RPO conflict defenders read their receiver's release.
  (Both deep dives above.) Confidence: high.

### 1.3 Blocking

- **Head leverage.** Madden 26 picks block outcomes and animations from "the desired leverage, ratings, and body pose
  at engagement". "Whichever player has head leverage in that gap has a significant advantage in winning the gap."
  Protections are assigned per play (base, half slide, full slide, empty), and users can "unTarget" a defender.
  Confidence: high.
- **Scheme-aware targeting.** CFB 26 improved blocker targeting for Power, Counter, Trap, Wham, Duo, Veer and midline.
  Its pass protection picks targets from threats and overloads, and double-team and disengage decisions depend on
  "ratings, angle, play concept, and leverage." "Block Steering" lets either side steer an engaged block, using
  strength, weight, ratings and momentum. A later patch (summary) improved stunt and loop recognition and made pass
  blockers "less likely to stay in double teams unnecessarily"
  ([Destructoid](https://www.destructoid.com/college-football-26-defensive-overhaul-real-time-coaching-2-0-and-more/)).
  Confidence: high for the features; their quality is not established.
- **"Suction".** EA's collision article claimed to have removed "suction, warping, and sliding" and said "defenders
  will no longer be sucked into a block." That page now returns 404; its text came from a search summary
  (https://www.ea.com/news/collision-madden). Madden 24 and 26 players still report being "pulled back into a block"
  after getting past it ([VideoGamer](https://www.videogamer.com/news/madden-24-players-debate-blocking-system/);
  EA forums, summary). Confidence: medium.

### 1.4 Pass rush

- In Madden 26 a move's stamina cost depends on the defender's move ratings, with no fixed thresholds. Pass protection
  strength was reduced relative to the "dynamic resistance bars." CFB 26 has 12 stunt types "based on real-life
  tactics." Stunts are no longer limited to four-man rushes, and linemen who win leverage but cannot reach the QB try
  to bat the ball down. Confidence: high.
- **ESPN win rates** (built on NGS tracking).
  [Explainer](https://www.espn.co.uk/nfl/story/_/id/24892208/creating-better-nfl-pass-blocking-pass-rushing-stats-analytics-explainer-faq-how-work)
  and [2023 rankings](https://global.espn.com/nfl/story/_/id/38356170/2023-nfl-pass-rush-run-stop-blocking-win-rate-rankings-top-players-teams):
  - Definitions: a rusher wins by beating his block inside 2.5 s; a blocker wins by holding it 2.5 s. Screens are
    excluded. At team level, unblocked rushers count as rusher wins.
  - Averages: the team-level win rate on a pass play is "about 50 percent." Individual PBWR across all blocking
    positions is about 75%, and the tackle average is 79%. Individual PRWR is "close to 25 percent," with DE/OLB
    averaging 22%.
  - 2023 ranges: team PBWR 43-76%, team PRWR 31-60%. Top individual PRWR: Parsons 35% and Garrett 30% at edge;
    Donald and Jones 20% at DT.

  Confidence: high for the definitions. The 2023 ranges may be a mid-season snapshot.

### 1.5 QB AI

- **Madden 26:**
  - Scramble decisions weigh down, distance, score and clock, and the QB "predicts yards gained before contact."
  - The pocket uses an "anchor" tied to dropback depth and launch point.
  - Hot reads are rating-driven.
  - The read progression is "trained with a machine learning model based on WR openness," and its speed comes from
    Awareness.
  - "Field Vision" models the QB's sightline from his height.
  - 50 CPU-only decision traits.

  Confidence: high that these features exist. It is unknown whether the ML runs at runtime or was used offline to tune
  rules.
- **CFB 26:** high-awareness QBs "identify multiple hot read options immediately after the snap," and recognize
  double moves against the blitz better. The game produces a wider range of slightly inaccurate throws, more sacks on
  very late throws under pressure, and CPU QBs leave play-action fakes earlier. Confidence: high.

### 1.6 Play calling and adaptive AI

- **Madden 26:**
  - A predictive play-calling model trained on "decades of NFL playcall tendency data." It filters by down, distance,
    score, clock, timeouts, personnel, field position and weather, and is updated through title updates.
  - "Adaptive Counters" track opponent tendencies during a game and weight counter concepts more heavily.
  - Real-Time Coaching makes delayed safety rotations, front adjustments and timed blitzes by team archetype.

  Confidence: high on the claims; effectiveness unknown.
- **CFB 26** made "over three times as many real-time adjustments." Its "9-Box" tracks pass concepts (Flood, Mesh,
  Smash) and counters them. The CPU reads formation strength and run type, then adjusts box count, front and pressure.
  Confidence: high.
- **NFL 2K5's VIP profile** (2004) recorded a user's tendencies, such as favourite hot routes and third-and-long
  defence, and played them back as a "ghost" at higher difficulty. Reviews say it got most play-calling down but not
  specific behaviours.
  [VideoGamer review (summary)](https://www.videogamer.com/?p=164173),
  [NBC News](https://www.nbcnews.com/id/wbna5801225). Confidence: medium; no developer detail.
- **Players' fear.** "CPU reads my play call" is a recurring complaint (EA forums, summary), so any adaptation has to
  come from observed history, never from the current call. Confidence: medium.

### 1.7 Wear and Tear, momentum, home field

- **Wear and Tear.** CFB 25 introduced per-body-part accumulated damage that lowers effectiveness and rewards
  substitution
  ([Destructoid explainer](https://destructoid.com/?p=526062),
  [CBS](https://www.cbssports.com/college-football/news/watch-ea-sports-college-football-25-gameplay-trailer-goes-in-depth-on-wear-tear-homefield-advantage)).
  It "had issues in the first game." CFB 26 made it tunable: impact and recovery values per play, per timeout and at
  halftime, auto-sub modes, toughness, weather, and up to a two-tier ability penalty. Madden 26 tracks it across a game
  and, in Franchise, across a season. Confidence: high on the features. Calibration data is not public.
- **Momentum.** Madden 22's "Gameday Momentum / M-Factors" meter was dropped in Madden 25 "without much fanfare"
  ([GameSided (summary)](https://gamesided.com/posts/madden-nfl-25-axed-the-momentum-mechanic-01j54y589etp)). Players
  asked for it to be optional. An old secondhand quote from a Madden designer says there is "no such thing in Madden
  as far as momentum is concerned." Confidence: medium.
- **Home field.** CFB 26's "Stadium Pulse":
  - away QBs struggle with clock management and kicks are harder;
  - false starts and communication breakdowns increase;
  - route miscommunication scales with distance from the QB;
  - a visiting 20+ yard first down pauses the crowd effects.

  The real NFL edge has shrunk: about 1.7 points before 2020 by one estimate, and road teams won more often in 2020
  ([NFL Football Ops](https://operations.nfl.com/gameday/analytics/stats-articles/what-does-the-nfl-s-home-field-advantage-look-like-in-2020),
  [PFT](https://www.nbcsports.com/nfl/profootballtalk/rumor-mill/news/home-field-was-no-advantage-home-teams-went-127-128-1-in-2020-season)).
  Confidence: medium; I found no settled figure for 2022-2025.

### 1.8 How EA uses tracking data

- **Madden 21.** NGS drove player movement: imported route paths per receiver, and acceleration, speed and turn rates
  for ball carriers. "We are not using Next Gen Stats in our ratings system right now" and "not ... in the blocking
  space quite yet" (Oldenburg). Situational tendencies were meant to feed game plans.
  [GamesBeat](https://gamesbeat.com/how-madden-21s-ai-makes-better-use-of-nfl-next-gen-stats/).
  Confidence: high for 2020. A later NFL.com piece (summary) says route running was the only legitimate NGS impact,
  which conflicts with EA's own marketing.
- **Inference.** EA used tracking data mainly to make movement look authentic, not to validate decisions. For TAG the
  more valuable use is the second one: validating decisions against public tracking.

### 1.9 How studios and communities validate realism

- I found no public EA document on how sim statistics are targeted. The visible process is community CPU-vs-CPU slider
  tuning: simulate a week many times to get a stat range, watch games, adjust until watched stats fall inside it, and
  retest after each patch
  ([Spudbox65 sliders](https://sliders.operationsports.com/sliders/madden-26/spudbox65s-madden-26-cpu-vs-cpu-slow-sim-sliders-crhh4),
  [OS forum](https://forums.operationsports.com/forums/forum/football/madden-nfl-football/madden-nfl-football-sliders/935166-finding-the-optimal-simulation-sliders-cpu-vs-cpu-watch-mode?p=24869804)).
- One Madden 27 tester argues that user-played and CPU-played games have different stat distributions, and that
  season leaders are inflated (7-10 INTs) (EA forum, summary). Confidence: low-medium.
- **Inference.** TAG's fit loop with a held-out seed (memory `sim-nfl-calibration.md`) is already more rigorous than
  anything EA documents publicly. The gap is the level of detail checked (tracking and player level), not the rigor.

### 1.10 Other football games: what players still cite

| Game | Got right | Got wrong | Source / confidence |
|---|---|---|---|
| ESPN NFL 2K5 | Presentation; defensive play calling split between the line and the LBs/secondary; gang tackling; VIP tendency profiles | Contemporary critics were split on whether its gameplay beat Madden's | [Game Informer 2016 retrospective](https://gameinformer.com/b/features/archive/2016/11/28/the-sports-desk-is-espn-nfl-2k5-still-good-after-all-these-years), [Wikipedia](https://en.wikipedia.org/wiki/ESPN_NFL_2K5); medium |
| All-Pro Football 2K8 | Reliable catching, skill system, legends | "the AI needs a tune-up"; AI "geared to direct your players in the ball's direction" (ball-chasing) | [GameSpot review](https://gamespot.com/reviews/all-pro-football-2k8-review/1900-6175446/) (summary); low-medium |
| Backbreaker (2010) | Euphoria physics tackles "look fantastic" | "awful AI"; "if the player and coach AI is bad enough that the game is unplayable, it doesn't matter how good the physics look" | [Game Developer](https://gamedeveloper.com/design/euphoria-over-bad-ai-in-backbreaker-), [Destructoid](https://destructoid.com/review-backbreaker); medium |
| Axis Football (2015-2026) | Franchise mode, customization | "AI is pretty poor"; sluggish ("a vat of treacle"); the AI "recovers too easily" | [Xbox Hub 2026](https://www.thexboxhub.com/axis-football-2026-review/), [OS 2019](https://www.operationsports.com/axis-football-2019-review-a-solid-alternative-to-madden); medium |
| Maximum Football (2025) | Customization, frequent hotfixes | "stale, robotic gameplay" | [Metacritic](https://legacy.prod.metacritic.com/game/playstation-5/maximum-football/critic-reviews) (one review); low |

**Inference.** In every non-EA case, the AI in the 22-man decision layer, not the physics or the art, is what reviewers
blame. That supports spending TAG's next effort on decisions.

### 1.11 Published techniques that apply

- **Pitch control / time to intercept** (Spearman, Liverpool FC, 2017-18; described secondhand). A player's time to
  reach a point assumes he keeps his velocity through a reaction period, then runs at top speed. A logistic function
  turns times into control probabilities, normalized across players.
  [Pressing Intensity, arXiv 2501.04712](https://arxiv.org/html/2501.04712v1),
  [SFU pitch control paper](https://www.sfu.ca/~tswartz/papers/pitch_control.pdf). Confidence: medium; I did not read
  Spearman's own paper.
  TAG's `openness()` reach model (`PlaySim.cpp` 3800-3830) is already this idea for the QB's read (inference).
- **Big Data Bowl lineage** (public NGS tracking on Kaggle):
  - 2020: "The Zoo" CNN on handoff-moment positions, speeds and accelerations became NGS Expected Rushing Yards
    ([NFL Ops recap](https://operations.nfl.com/updates/the-game/2020-big-data-bowl-results)).
  - 2024: tackling. Missed tackle defined as a tackle probability above 75% for 0.5 s that then drops with no tackle
    in the next second. Led to NGS Tackle Probability
    ([2024 finalists](https://operations.nfl.com/gameday/analytics/big-data-bowl/2023-big-data-bowl-finalists),
    [AWS](https://aws.amazon.com/blogs/media/beyond-the-box-score-aws-and-nfl-ai-powered-tackle-analysis)).
  - 2025: pre-snap, with every frame from pre-snap to post-snap for 2022 weeks 1-9
    ([arXiv 2502.16313](https://arxiv.org/pdf/2502.16313)).
  - 2026: player movement while the ball is in the air. Input is pre-throw tracking; output is post-throw frames for
    the target and the relevant defenders. The winner evaluated defenders against "ghost" distributions
    ([NFL Ops](https://operations.nfl.com/gameday/analytics/big-data-bowl);
    [NFL Ghosts, arXiv 2406.17220](https://arxiv.org/pdf/2406.17220)).

  Confidence: high for 2020 and 2024, medium for the 2025 and 2026 data details.
- **Free charting and NGS through nflverse:**
  - FTN charting (2022 onward, CC-BY-SA 4.0): `read_thrown`, `n_pass_rushers`, `n_blitzers`, `is_play_action`,
    `is_rpo`, `is_screen_pass`, `is_motion`, `is_qb_out_of_pocket`, `is_catchable_ball`, `is_interception_worthy`,
    `is_contested_ball`, `is_throw_away`, `is_drop`, `n_defense_box`-style backfield counts
    ([dictionary](https://cloud.r-project.org/web/packages/nflreadr/vignettes/dictionary_ftn_charting.html)).
  - NGS season tables: `avg_time_to_throw`, `aggressiveness` (share of throws with a defender within 1 yd),
    `avg_separation`, `avg_cushion`, rushing `efficiency` (distance run per yard gained), `avg_time_to_los`,
    `percent_attempts_gte_eight_defenders`, expected rushing yards and RYOE, expected YAC
    ([dictionary](https://ftp.fau.de/cran/web/packages/nflreadr/vignettes/dictionary_nextgen_stats.html)).

  Confidence: high (dictionaries read directly).
- **Coverage mix.** Public usage figures differ by provider:
  - Cover 3: 29.9% (FFToday 2024), 33.8% (TruMedia via AOL), 32.9% (MatchQuarters via theScore, season unclear).
  - Cover 1: 21-22%. Quarters: ~16%. Cover 2: 11-13%. Cover 6: ~8%. Cover 0: ~4%.
  - Man coverage: 22.6% of snaps in 2025 (PFF).

  Sources: [FFToday](https://fftoday.com/articles/orth/25_cover_beaters.html),
  [theScore](https://www.thescore.com/nfl/news/3084522),
  [NFL.com](https://www.nfl.com/_amp/the-coverage-that-turned-the-seahawks-into-super-bowl-winners-and-is-taking-the-nfl-by-storm).
  Confidence: medium; use ranges.
- **Stunts.** Sports Info Solutions reports stunt rates by type of about TE 13%, ET 6% and TT 6% "per play," but the
  denominator is ambiguous
  ([SIS](https://www.sportsinfosolutions.com/?p=10090)). PFF's stunt-pressure tables are paywalled.
  Confidence: low.
- **Play action.** League YPA was 7.76 with play action against 6.72 without, and EPA/play 0.054 against -0.031 (PFF
  2025, summary). Confidence: medium; compute TAG's own target from nflverse and FTN instead.
- **Utility AI and influence maps** ([IAUS](https://gameai.com/iaus.php);
  [Game AI Pro 2, ch. 30](https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter30_Modular_Tactical_Influence_Maps.pdf)).
  Influence maps feed decisions rather than issuing commands. Confidence: medium on the sources.
  Inference: TAG already scores options with hand-weighted utilities (the carrier's 18-ray danger field, the QB's
  openness bar). A framework migration would add nothing visible.

### 1.12 "What makes it feel fake": the complaints, read as requirements

| Complaint | Requirement it implies |
|---|---|
| Psychic DBs / INTs | A defender acts only on what he can see: the receiver's body and the ball. He needs eyes on the ball to catch it. |
| Suction blocking | A block forms only where the blocker has the angle. A man past the blocker's hip is not engaged. |
| Robo-QB | Throws have a human error profile (catchable rate, INT-worthy rate) and a human read sequence. |
| CPU reads my play | Adaptation uses only history, never the current call. |
| Ratings don't matter | Player-level outcome spreads match the NFL's: stars stand out by NFL-sized margins. |
| Rubber-banding | No hidden comeback logic. TAG has none; keep it that way. |
| Warping catches | Covered by the animation researchers; TAG's `homeBall` (`PlaySim.cpp` 5789) is the sim side. |

---

## 2. Where TAG Gridiron stands

### 2.1 What it already does well (verified in code)

**Architecture.** Every man runs a per-state think function each 60 Hz tick: `PlaySim::think`, `PlaySim.cpp` 2976-3034,
dispatching to QB, Route, PassBlock, RunBlock, Rush, Man, Zone, Carrier, Pursue and Convoy. About 120 calibrated knobs
live in `tag::Tune` (`PlaySim.h` 50-191) and are fit against nflverse with held-out seeds (`Tools/Realism/fit.py`,
`nfl_reference.py`, `rubric.json` 35-96).

**Simulated perception.**
- Every defender has a reaction time: `reactT = 0.12 + (99 - max(playRec, awr)) * 0.006` (`PlaySim.cpp` 1385).
- Man and zone defenders track a delayed copy of the receiver's velocity (`delayedVel`, 4464-4469; lags at 4485 and
  4536).
- Zone defenders lean toward the receiver the QB is reading (`zoneEyes`, 4520-4524).

**QB.**
- A progression whose read time depends on decision-making, with mental noise on each read (3369-3455).
- An openness measure from forward-simulated receiver routes and defender closing at human acceleration (3800-3830).
- Hot reads and throwaways as per-second hazards (3456-3488) and a coverage scramble with a lane test (3521-3533,
  3630-3670).
- Pocket step-up away from free rushers and edge rides (3554-3572).
- The PFF pressure definition (3385-3391).

**Pass protection.**
- Line-to-rusher matching in order, tackles keeping their edge rusher, kick-slide sets on the rusher's projected path
  (4197-4206).
- Pass-offs when rushers cross (4140-4155), interior help and double teams with peel-off (4125-4133, 4210-4229), and
  backs scanning for blitzers.

**Run game.**
- Scheme jobs: pulls, kick-outs, leads, traps (`thinkRunJob`).
- Claimed-target logic and climbs to the free defender nearest the hole (4288-4326).
- Zone-read and RPO reads, play-action bite by play recognition (`fakeBite`, 4404).
- LB read step and scrape-around pursuit (4598-4609, 4676), found by a probe and fixed (memory
  `sim-nfl-calibration.md`).

**Contact.**
- Shed hazards from rating differences (`updateEngagements`, 5125-5287), early-down versus late-down rush intensity
  (5227-5230), and fouls drawn from what happened on the play.
- Tackle logit on facing, mass, speed, gang tackles and moves (6216-6349).
- Moving piles (`stepWrap`).

**CPU calling.**
- Offense: by down and distance on nflverse curves, personnel shares, `recent`-play damping (`Game.cpp` 426-531).
- Defense: by personnel, scheme lean and blitz lean (533-573).
- Important: the CPU defense sees only the offense's personnel, not its play (535). That already meets the "no play
  reading" requirement.

**Validation.** About 60 rubric metrics, including per-play shapes: first-down run gain distance, depth shares,
third-and-long causes, NGS-defined separation at arrival (`PlayResult::arriveSep`), and time to throw. Deterministic
headless runs.

### 2.2 Where it falls short (verified in code)

1. **Man coverage knows the route.**
   - What the code does: `thinkMan` aims at a blend of where the receiver is and `predictPos(r, 0.45)` (`PlaySim.cpp`
     4494-4497). `predictPos` forward-simulates the receiver's actual remaining route points (3674-3694). A 95-rated
     cover man weights that 0.75 (`routeRead`).
   - Effect: the defender sees the break 0.45 s before it happens, including a double move's real break. This is the
     "psychic DB" in code form.
2. **Picks and breakups ignore where the defender is looking.**
   - What the code does: the interception and PBU odds (`preRollCatch` 5683-5731, `preRollLane` 5778-5781,
     `resolveAir` 6016-6031 and 6100-6116) use ball skills, catching, distance and "edge". None uses the defender's
     facing.
   - After his reaction time, every defender runs to the exact computed landing spot (`ballLanding()`, 4572-4579).
   - Madden 26 and CFB 26 both shipped exactly this fix (section 1.1).
3. **Linebackers peek at the play type.**
   - What the code does: the LB read step and the fill both trigger only when `cfg.offPlay->type == PlayType::Run`
     (4586, 4598). The run read adds 0.1 s on non-runs by play type (4611).
   - Effect: on dropbacks LBs drop at once, with no read step. A draw (a run with a pass set) fools no one. Only
     plays typed PlayAction get a fake (`fakeBite` returns 0 otherwise, 4407).
4. **Engagement leverage is random.**
   - What the code does: `engage()` attaches a block whenever the bodies come within reach (4206, 4341, 5061-5084).
     It sets `engageEdge = rng.normal(0, 7)` (5080) with no geometry, and nothing stops engaging a defender who is
     already past the blocker.
   - This is the mechanism behind "suction" complaints. EA now rewards head leverage instead (section 1.3).
5. **No gap integrity or force/contain.**
   - What the code does: LB "gaps" are just each LB's own `y` at the snap (1400-1403).
   - Effect: no defender is the force or contain man. Every pursuer aims at an intercept point with a bounded lead
     (4656-4663). Only deep defenders keep a cap over the top (4665-4668).
6. **Backs read bodies, not blocks.**
   - What the code does: the carrier scores 18 rays against a Gaussian danger field and gives an engaged defender a
     weight of 0.25 whichever side of his blocker he is on (4833-4870, weight at 4846).
   - Only kick returners stay behind a lead blocker (4883-4902). `runPress` is off (1.0).
   - Memory notes say the run-gain shape is still out of band at 0-6 yards.
7. **Zones are spot drops plus nearest threat.**
   - What the code does: zone defenders go to landmarks (1315-1379), then shadow the nearest route runner within 7 yd
     underneath or the deepest threat within 17 yd laterally (4513-4550).
   - There are no carry, pass-off or MOD/MEG rules, so Cover 3 and Quarters cannot play as match coverages (the
     `Coverage` and `CoverTech` enums are in `Playbook.h` 169-176).
8. **No defensive-line stunts on scrimmage downs.** Stunts exist only for field-goal blocks (`PlaceKick.cpp` 170-174,
   597-600). The pass-off logic in pass protection is therefore never exercised by the CPU's own rush.
9. **Play calling does not adapt.**
   - Both CPU callers are weighted random choices from situation, scheme and team lean, plus a four-play repeat
     penalty (`Game.cpp` 512, 527-529).
   - No user-tendency memory and no counter tags. Team identity is only `passLean` and `blitzLean`.
   - Home field is +0.004 on road pre-snap flags (`Game.cpp` 596). This is fine given how small the real effect is.
10. **Validation stops at league averages and play shapes.**
    - There is no ESPN-style win rate, no player-level spread check ("do ratings matter?"), no read-index or
      catchable-ball profile, and no tracking-level comparison (LB depth over time, defender convergence after the
      throw).
    - FTN and NGS fields that map directly onto existing sim quantities are not used yet.

**Fatigue.** Per-player fatigue exists and drives rotation, top speed (-10%) and accuracy (`Game.cpp` 738-751,
`PlaySim.cpp` 156, 3910), so a light version of Wear and Tear's visible effect, substitution, is already there.

---

## 3. Actions (ranked by player-visible impact per unit of effort)

Effort: S = under a day, M = days, L = weeks.

Any action that changes sim behaviour ends with the existing gate:
- `SimCalibrate 200` games on seeds 11/29/41 with held-out seed 53;
- box stats within tolerance (currently about 1-3 out);
- `SimTests` green.

That gate is implied below and not repeated.

### A1. Man coverage reads the receiver's body, not his route - M

- **Build:** in `thinkMan`, replace `predictPos(r, 0.45)` with a predictor from observable cues:
  - the receiver's current velocity;
  - his deceleration, since he sinks before a break (`routeWant`, 3709-3716, already slows him);
  - a prior break direction from the defender's leverage and the receiver's stem depth.

  When the break comes, the defender reacts after a cut-reaction delay set by `manCov` against `routeRunning` and
  agility, shorter when the cut goes into his leverage (Madden 26's rule). Tag double-move routes so a defender whose
  prediction matched the fake commits a false step.
- **Visible result:** double moves and sharp breaks create space against good corners. Corners get beaten by
  route-running, not just by speed.
- **Where:** `PlaySim.cpp` `thinkMan` 4471-4503 and `delayedVel` 4464. New tunes beside `manLagK` and `routeRead`
  (`PlaySim.h` 63-73). Route tags in `Playbook.cpp` ROUTES.
- **Dependencies:** a refit of `manLagK`, `accBase` and `qbThresh` through `fit.py`.
- **Verify:**
  - Sim mean `arriveSep` (already the NGS definition) against the target-weighted league mean of NGS `avg_separation`
    from `nflreadr::load_nextgen_stats("receiving")`.
  - Sim share of targets with a defender within 1 yd against NGS `aggressiveness` (attempt-weighted).
  - Scenario: 400 reps of sluggo or out-and-up against Cover 1 press, compared with go routes of the same depth.
    Separation at the throw must be higher on the double moves (today it cannot be, because the defender anticipates
    the real break).
  - `PlayResult::manGap` in the 0.5 s window after the break should widen for routes run by receivers rated 90+ for
    route running.

### A2. Eyes on the ball for picks and breakups - S

- **Build:**
  - An interception, or a clean hands-on breakup, requires the defender to face within about 90 degrees of the
    incoming ball at the moment he plays it, or to have turned in time (reaction time plus a turn time).
  - A trailing man defender with his back to the QB plays the receiver's hands. He reacts when the receiver's hands
    come up, about 0.4 s before arrival. He then gets only a late swipe, with lower PBU odds and more DPI risk through
    `foulDpi`.
  - Replace the exact `ballLanding()` with an estimate whose error shrinks during the flight, scaled by ball skills.
- **Visible result:** no blind picks. Trailing corners swipe at the catch point instead of turning into receivers.
  Back-shoulder and deep balls against man coverage complete like the NFL's.
- **Where:** `PlaySim.cpp` 5683-5731, 5778-5781, 6016-6031, 6100-6116; `reactToBall` 4572-4579.
- **Dependencies:** refit `intBias`.
- **Verify:**
  - A new SimCalibrate count of INTs by defenders not facing the ball. Must be about 0.
  - INT rate held at the NFL's ~2.3% (box).
  - DPI per team-game held near the nflverse value (~0.5).
  - Passes defensed per attempt against nflverse `pass_defense_1_player_id` (compute).
  - Scenario: Cover 1 go routes, where trail-technique INT rate must drop from today's baseline.

### A3. Win-rate and star-spread telemetry - S (win rates) / M (season player spread)

- **Build:**
  - Add ESPN-style PRWR and PBWR (2.5 s; screens excluded; unblocked rushers count at team level) from the existing
    "shed" events and engagement times.
  - Add a season mode that sims 17 games for 32 teams and aggregates per player:
    - PRWR;
    - separation;
    - CPOE, using the sim's own pre-rolled catch odds (`Ball::fateP`) as the expected completion;
    - missed-tackle rate.
  - Add season win SD, point-differential SD and the correlation of team OVR with wins.
- **Visible result:** none by itself. It shows whether ratings matter, and the follow-up fit (the slope of rating
  differences in `updateEngagements` 5143-5154, and the tackle and accuracy slopes) makes 99s play like 99s.
- **Where:** `Tools/SimCalibrate.cpp`, `Tools/Realism/rubric.json`, `nfl_reference.py` for the NGS spreads and
  nflverse `games` for season spreads.
- **Verify targets (ESPN):**
  - League individual PRWR ~25%, edge 22%.
  - Tackle PBWR ~79%; all blockers ~75%; team-level ~50%.
  - Top edge 30-35%, top DT ~20%.
  - Team PBWR spread 43-76%, team PRWR spread 31-60%.
  - Per-player SDs of separation, CPOE and RYOE within ±25% of the NGS cross-player SDs (qualified players).

### A4. Read keys from what a defender can see - M

- **Build:** remove the `PlayType::Run` gates (4586, 4598, 4611). Each LB and box safety computes a key from things he
  can see:
  - the offensive line's first movement: run block firing out against a pass set (visible as blocker state and
    direction);
  - backfield action: mesh or flow against the QB's drop;
  - his own read time from play recognition.

  Everyone takes the read step on every snap. Play action and draws make the cues lie for the fake's length (fold
  `fakeBite` into this). The RPO conflict defender reads his receiver's release, as CFB 26 describes.
- **Visible result:** linebackers shuffle and read on every snap. Draws and play action fool the poor readers. Zone
  drops start late on runs that look like passes, and the reverse.
- **Where:** `reactToBall` 4570-4637, `fakeBite` 4404-4412, `thinkZone` 4505.
- **Dependencies:** A6 for the tracking comparison (optional); refit `lbRead`, `paBite`.
- **Verify:**
  - Play-action YPA lift against nflverse joined to FTN `is_play_action` (compute for 2023-2024; PFF's 2025 figure
    was +1.0 YPA).
  - LB depth and speed at 0.5, 1.0 and 1.5 s by play type (run, dropback, PA) against Big Data Bowl tracking (via A6).
  - Draw-play YPC must no longer trail inside zone by a wide margin (sanity; no public label).

### A5. Leverage at engagement (anti-suction) - M

- **Build:**
  - In `engage()`, refuse a block, or downgrade it to a grab with holding risk, when the defender is already past the
    blocker toward his goal (a dot-product test on the blocker-to-goal line).
  - Replace `engageEdge ~ N(0, 7)` with a geometric term plus a smaller random one. The geometric term combines the
    defender's lateral offset from the blocker on the line to his goal (head leverage), the closing speed, and how
    late the blocker set.
- **Visible result:** speed rushers win the corner when the tackle is late. Reach and down blocks win or lose by
  angle. No more defenders snapped back into a block they had already beaten.
- **Where:** `PlaySim.cpp` 5061-5084, `diff` at 5154, pass-pro set at 4197-4206, run-block contact at 4328-4341.
- **Dependencies:** A3's win rates to calibrate; refit `passProShed` and `runShed`.
- **Verify:**
  - A telemetry count of engagements formed with the defender past the blocker's hip. Must be 0.
  - Edge PRWR above DT PRWR, in the ESPN proportions (22% against well under 20% for an average DT).
  - Pressure 31-34% and sacks about 6.7% held (rubric).
  - Scenario: a 95-speed/60-strength edge against a 70-speed tackle. He wins more on the arc than a 70/95 power rusher
    does, while the power rusher shows a larger `linePush`.

### A6. Tracking parity harness (sim to Big Data Bowl schema) - M

- **Build:**
  - `SimCalibrate --bdb-out` writes 10 Hz frames in the public tracking schema (x, y, s, a, dir, o, event per
    player-frame) plus a plays table: down, distance, play type, coverage family, PA, rushers.
  - `Tools/Realism/bdb_compare.py` computes identical metrics on sim and NFL:
    - speed and acceleration by position;
    - LB depth over time by play type;
    - time to first pressure;
    - defender convergence on the catch point after the throw (2026 input/output frames);
    - separation over time by route;
    - tackle geometry (2024).
- **Visible result:** indirect. This turns the one-off probe that found the LB backpedal bug into a standing check,
  and verifies A1, A2, A4, A7 and A8.
- **Where:** `Tools/SimCalibrate.cpp` (the `onStep` observer, `PlaySim.h` 860), new Python script, `rubric.json`.
- **Dependencies:** Kaggle data download; licence check (Open questions).
- **Verify:**
  - With `--set lbRead=0 --set pursueAvoid=0` the harness reproduces the known bug (free pursuers 3-4 yd/s at 1.5-2 s).
  - The NFL side reproduces a published number (top speeds 20.5-22.6 mph, rubric `top_speed_max`).

### A7. Backs read blocks - M

- **Build:**
  - In the carrier's ray scoring, weight each engaged defender by the side of his blocker he is on (the open side is
    away from his head).
  - Add a scheme read: zone runs press the aiming point and read the first play-side lineman (bang, bounce or bend);
    gap runs follow the puller or lead blocker, extending the returner's stay-behind-the-blocker code to scrimmage
    runs.
  - Calibrate `runPress` against NGS time to the line.
- **Visible result:** backs press, set up blocks and cut off them, instead of drifting to open grass.
- **Where:** `thinkCarrier` 4778-4936 (ray loop 4833-4870; returner logic 4883-4902).
- **Dependencies:** A5 for leverage; refit `runShed`, `tackleBase`.
- **Verify:**
  - Sim time from handoff to the line, and run distance per yard gained, against the attempt-weighted league values of
    NGS `avg_time_to_los` and `efficiency`.
  - Rubric `first_run_shape` (now about 7.4) toward its 0-6 band, with YPC still 4.1-4.6.

### A8. Gap integrity and force/contain - M-L

- **Build:**
  - At the snap, give each front-seven defender a gap from the call and alignment (DL technique sets A/B/C; LBs fill
    the rest).
  - Give each side a force/contain defender by coverage: the corner in Cover 2, the strong safety or outside
    linebacker in Cover 3/1. He keeps the carrier on his inside shoulder until the carrier commits inside.
  - The backside end stays home for the cutback or bootleg.
  - Pursuit for the others keeps inside-out leverage.
- **Visible result:** outside runs get turned back inside, cutbacks meet a home defender, and edges look set rather
  than everyone flowing at the ball.
- **Where:** `setupAssignments` 1400-1403, `thinkPursue` 4639-4678, `reactToBall`, DefCall front and align data in
  `Playbook.cpp`.
- **Dependencies:** A4, since keys decide when a gap defender leaves his gap.
- **Verify:**
  - Sim runs tagged with nflverse classes `run_location` (left/middle/right) and `run_gap` (end/tackle/guard). YPC,
    share of runs ≤ 0 and share of 10+ per class within tolerance of 2023 nflverse.
  - Scenario: tosses and stretches against Cover 2. The share of carries the force defender turns inside must rise
    from about 0 today.

### A9. QB decision profile against FTN charting - S

- **Build:** log on every dropback:
  - the read index thrown on (`readIdx` at the throw, plus hot, checkdown and scramble paths);
  - catchable ball (thrown within the target's hand reach);
  - interception-worthy (pre-rolled INT, or interception odds above a threshold);
  - out of pocket;
  - throwaway.

  Compare with FTN `read_thrown`, `is_catchable_ball`, `is_interception_worthy`, `is_qb_out_of_pocket` and
  `is_throw_away`.
- **Visible result:** after tuning the read time (3394) and openness bar (3410), QBs miss throws and work
  progressions with NFL frequency instead of looking robotic.
- **Where:** `thinkQB` 3369-3455, new `PlayResult` fields, `SimCalibrate.cpp`, `nfl_reference.py`.
- **Verify:** each FTN share within ±15% relative, for example the first-read share and the catchable share (compute
  the targets; I did not download FTN).

### A10. Adaptive CPU defense and data-built coaching identities - M

- **Build:**
  - **(a) FTN tendency tables (2022-2025).** Rusher and blitzer counts by down and distance, plus PA, screen, RPO,
    motion and no-huddle rates. Use them for `cpuDefense` blitz weights and `cpuOffense` PA and screen weights,
    replacing the hand weights `paCallW` and `screenW`.
  - **(b) Archetypes.** Cluster NFL team-seasons on nflverse `pass_oe` plus the FTN rates, and give each fictional
    team one.
  - **(c) In-game memory of the human's calls.** Track run/pass by down bucket, concept family, run direction and
    target depth. Multiply the weights of DefCalls tagged as counters, like Madden 26's Adaptive Counters and CFB 26's
    9-Box. Never look at the current call.
- **Visible result:** spamming one play gets answered; teams have identities (a blitz-heavy defense, a play-action
  team).
- **Where:** `Game.cpp` `cpuOffense` 426-531 and `cpuDefense` 533-573, Team leans, DefCall tags in `Playbook.h/.cpp`.
- **Verify:**
  - CPU share of 5+ rushers by down within ±3 points of FTN.
  - Cross-team SD of sim pass rate over expected near nflverse's team SD.
  - Headless scripted-user scenario: the same concept on 1st and 10, ten times a game over 200 games. The
    counter-tagged call share is at least 1.5x baseline by the sixth call, and the concept's YPP falls from the first
    half to the second.

### A11. Match rules for Cover 3 and Quarters, and a real shell mix - L

- **Build:**
  - Cover 3 Match: the curl/flat defender carries #2 vertical to about 10-12 yd, then passes him to the hook. Add
    Skate and Skinny against 3x1, as Madden 26 names them.
  - Quarters: the safety reads #2 (vertical: man him; out or under: help on #1); the corner plays MOD/MEG.
  - Cover 2: the corner sinks against #2 vertical when the flat is empty; the Tampa Mike carries the seam.
  - Weight CPU calls to the public shell mix.
- **Visible result:** zones that look like NFL zones. Seams get carried, and concept-versus-coverage matchups behave as
  coaches expect.
- **Where:** `thinkZone` 4505-4567, `setupAssignments` 1315-1379, `Playbook.h` Coverage and CoverTech, `cpuDefense`
  weights.
- **Dependencies:** A1, so man-match defenders inherit the cue-based predictor.
- **Verify:**
  - Shell share table against the ranges in section 1.11 (Cover 3 30-34%, Cover 1 21-22%, Quarters ~16%, Cover 2
    11-13%, Cover 6 ~8%, Cover 0 ~4%).
  - Scenario: four verticals against Cover 3 Match. The hook/curl defender is within 3 yd of #2 at 1.8 s.
  - `ConceptCal --def` concept-by-coverage YPA table reviewed for known beaters, Smash against Cover 2 and Flood
    against Cover 3. Low-confidence target: no free public split exists unless the Big Data Bowl coverage labels are
    confirmed.

### A12. Defensive-line stunts on passing downs - M

- **Build:** add tackle-end (TE), end-tackle (ET) and tackle-tackle (TT) twists to DefCalls (penetrator plus looper
  with timing; the field-goal code in `PlaceKick.cpp` 170-174 and 597-600 is a template). The CPU calls them on passing
  downs.
- **Visible result:** loops at the line, and blockers who visibly pass them off or get beaten by them. This exercises
  the pass-off logic at 4140-4155.
- **Where:** `Playbook.cpp` DefCalls, `thinkRush` 4414-4462, `setupAssignments`.
- **Dependencies:** A5.
- **Verify:**
  - Stunt rate against SIS's order of magnitude (low confidence).
  - Pressure 31-34% and sacks held.
  - Pass-off event count above 0 on stunt plays, with pressure on stunts above non-stunts. Direction only: the public
    figures are paywalled.

---

## 4. Not worth doing

| Idea | Reason |
|---|---|
| An ML play-calling model like Madden 26's | TAG's teams are fictional and the data is per real team. Weighted tables from nflverse and FTN (A10) get the visible part for a fraction of the work. |
| Rewriting the AI as behaviour trees or a utility-AI framework | The per-state think functions already score options with hand utilities. A rewrite costs weeks, nobody sees it, and it breaks a calibrated, deterministic sim. |
| Pitch control computed in the sim every tick | `openness()` and the carrier's danger field already approximate time-to-intercept. Pitch control is more useful offline, as a validation metric inside A6. |
| A momentum meter or comeback logic | Madden added one in 22 and removed it in 25; players complained, and there is no NFL evidence for it. TAG has none and should keep it that way. |
| Body-part Wear and Tear | There is no public calibration data. CFB 25 needed post-launch tuning knobs. TAG's fatigue and rotation already provide the visible part, substitutions. Revisit for a franchise mode. |
| A Zoo-style CNN driving the running backs | It is a predictor of outcomes, not a policy. It is worth having only as an RYOE check on sim runs after A6 exists. |
| Euphoria-style physics tackle resolution in the sim | The Backbreaker lesson is that physics did not save bad AI. Tackle realism belongs to the animation and physics researchers. The sim's logit plus moving pile is calibrated. |
| QB field-vision obstruction by height (Madden/CFB 26) | Small visible effect; it needs an occlusion model of linemen, and the payoff is lower than A1-A9. |
| More season-average box-score fitting | It is within tolerance already. Fred prefers playtest feedback to chasing the last tenths (memory `playtest-over-scorecard.md`). |
| Bigger home-field effects | The NFL edge has shrunk to about 1-1.7 points, and TAG's crowd-noise false starts are proportionate. CFB 26's Stadium Pulse is college-flavoured. |

---

## 5. Open questions

1. **Licence for Big Data Bowl data.** Kaggle competition data often carries competition-only or non-commercial
   terms. Using it to calibrate a game TAG may sell needs a check of the specific year's rules. I ran out of search
   budget before verifying this. nflverse pbp and FTN (CC-BY-SA 4.0, attribution to FTN via nflverse) look safer; the
   licence of the NGS tables in nflverse is also unverified.
2. **Coverage labels in the Big Data Bowl.** It is unconfirmed whether the 2023 or 2025 sets include PFF-derived
   coverage fields (I recall `pff_passCoverage` and `pff_manZone`). The 2021 set had `coverages_week1.csv` (per a
   GitHub gist found in search). This decides whether A11 can have a data target.
3. **League values for the NGS and FTN targets.** I did not download nflverse, so A1, A3, A7, A9 and A10 name the
   fields to compute, not the numbers. One summary gave league separation of 3.6 yd with no season, so I did not use
   it.
4. **Reaction time of defenders after the throw.** I found no published measurement. TAG's zone defenders break
   0.04-0.12 s after release (`reactT * zoneReactK`, with `throwStart` set at launch, `PlaySim.cpp` 3924). That may be
   too fast or may be fair anticipation of the windup. The 2026 Big Data Bowl output frames could settle it via A6.
5. **How Madden uses ML in progressions.** EA's wording is "trained with a machine learning model"; whether it runs at
   runtime is not stated.
6. **Stunt frequency and stunt pressure rates.** SIS's "per play" denominator is ambiguous and PFF's numbers are
   paywalled.
7. **How EA validates sim statistics internally.** I found no public documentation, only community slider-tuning
   methods.
8. **NFL 2K5 VIP.** Whether it changed the CPU's in-game calls or only drove ghost opponents is known from reviews
   only.
