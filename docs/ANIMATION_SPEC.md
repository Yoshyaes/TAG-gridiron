# TAG Gridiron: Animation & Look-and-Feel Study and Clean-Room Spec

**Method.** Everything here comes from public material: EA's published gridiron notes and tech
posts, GDC session descriptions, EA's published patents, and press reviews. Nothing was
decompiled, datamined, or extracted from any game. The spec in section 3 describes *goals and
behaviours* in our own terms and is implemented with our own code and freely licensed assets.

---

## 1. Technical analysis (what the leading football sim publicly says it does)

| Area | Publicly described approach | Takeaway for us |
|---|---|---|
| Skeleton & bodies | Madden 24 rebuilt its "performance skeleton" from scratch, applied body-scan data, called out wrists and ankles, and made animation respond to size/weight/strength. | Real body proportions per player and per position; good distal joints (hands, feet) matter most on screen. |
| Locomotion | EA has used **motion matching** for years (GDC "Motion Matching at EA: Five Years Later"); Madden 26 describes ML choosing "the most realistic blend in real time" for cuts and spins. | Pick poses from a large mocap database by future trajectory rather than hand-wired state machines. UE's PoseSearch + Epic's free Game Animation Sample is the open equivalent. |
| Multi-player interactions | "N-teract": align participants to a shared scene origin and assign roles (gang tackles); later talks add *synchronized multi-character motion matching* and reference points at **future intercept positions** of moving objects. | Tackles, blocks and catches are solved relative to the *other* body or the ball's predicted position, not played blind. |
| Physics tackling | Madden 25 "BOOM Tech": not raw ragdoll, but physics-informed **animation branching**. A start animation is chosen, a driven ragdoll carries the bodies into a physical collision, then a fall/stumble/recovery is reselected (possibly repeatedly). Three stages: pre-impact, impact, resolution. Inputs: weight, speed, momentum, squareness, leverage, angle, ratings. Ball carriers can stumble and rebalance. | Outcome is decided by physics + ratings; animation expresses it. Momentum must carry through contact. |
| Hit coverage | FieldSENSE "Hit Everything": hits allowed in many more contexts (mid-air, piling on, blowing up blocks); Madden 24 added/adjusted 1,700+ tackle animations and extended it to blocking. | Contact must be possible everywhere; variety prevents repetition. |
| Quarterbacks | Madden 26 "QB signatures": how each QB holds the ball in the pocket and his arm slot; 500+ QB animations, ~40 QB "recipes". | Per-player style parameters on a shared motion. |
| Catching | Madden 24 added hundreds of catches aimed at **keeping speed and momentum** (in-stride catches); Madden 27 adds a timing meter. | Hands go to where the ball *will* be; the runner keeps running. |
| Trenches | Madden 27 short-yardage refresh: pile movement, leverage, strength/weight-based push. | Linemen win by leverage (low pad level, hands inside), visible as lean and hand placement. |
| Presentation | Dynamic weather that changes mid-game, snow accumulation and surface deformation, uniforms that get dirty, home-crowd camera shake, 60–120 fps targets. | Atmosphere and wear tell the game's story; frame rate is part of feel. |

## 2. Product analysis

**What players and reviewers reward:** fluid, unscripted-looking movement; variety (no repeats);
physicality that respects size and speed; broadcast-grade presentation; responsiveness (high fps).

**Where the leader is publicly criticised:** repeated animations over long sessions; tackles that
feel triggered by an animation rather than by contact; collision/animation shortcuts and pop-ups;
yearly changes described as incremental.

**Our opening.** TAG Gridiron is simulation-first: the physics/ratings simulation already decides
every outcome. So we can make animation a pure *expression layer*: it never decides a result,
can't "warp" into a canned outcome, and procedural layers make every rep slightly different.

## 3. Clean-room spec: TAG animation & visual architecture

Layers run every frame in this order. Each is independent and can be toggled.

**L0. Simulation (authoritative).** Positions, velocities, facing, action state (run, block,
throw, catch, tackle, dive, down), engagement partners, ball state and target. Unchanged.

**L1. Base locomotion.** Mocap from Epic's free Game Animation Sample (walk/run/sprint in 8
directions, crouch line-play, idle), selected by velocity relative to facing and speed-matched so
feet don't slide. *Next:* PoseSearch motion matching over the full sample database.

**L2. Football action layer (procedural, our own).** Component-space IK and torso work on top of L1:
- **Throw:** load (torso turns away, ball cocked behind the ear), release (hips and shoulders fire,
  arm comes over the top), follow-through (hand finishes across the body). Per-QB *arm slot* and
  release height come from a stable per-player style seed.
- **Ball security:** ball carriers tuck the ball high and tight in the arm away from pursuit; QBs
  hold it two-handed at the chest in the pocket. The ball sits in the hands.
- **Catch:** hands reach to the ball's *predicted* position during the final ~0.6 s of flight; a
  high ball means hands up with thumbs together, a low ball means pinkies together. The
  receiver's legs keep running (L1 is untouched below the waist).
- **Block:** hands to the engaged opponent's chest plate, elbows in, forward drive lean scaled by
  leverage; defenders under the block lean back.
- **Tackle:** arms wrap toward the ball carrier's waist as distance closes; the head and chest
  drive across the bodyline.
- **Stance:** linemen show a three-point stance before the snap (hand down, back flat).

**L3. Contact physics.** Downed players become physical ragdolls carrying their momentum;
impulse scales with relative speed and the opponent's mass. *Next:* driven-ragdoll "stumble and
recover" blends for broken tackles (physical animation component with strength ramps).

**L4. Secondary motion.** Head and eyes track the ball (QB to target, receivers and DBs to the
ball in flight, defenders to the ball carrier) within neck limits.

**L5. Presentation.** Camera shake on big hits, broadcast cameras, weather, crowd. *Next:*
uniform dirt and grass stains accumulating per player, turf wear, snow trails.

**Variation rule.** Every procedural motion takes a small per-player style vector (arm slot,
hand height, lean, timing jitter) so no two players, and no two reps, look identical.

**Acceptance checks.** These are automated close-up screenshots plus logged pose data:
- in a run cycle, hands swing across 40 cm or more
- the ball stays within 15 cm of the carrier's hand
- the catcher's hands are within 25 cm of the ball at the catch
- linemen's hands are on an opponent's chest while engaged
- the QB's hand is behind the head at the cocked frame

## 4. Status
- L0, L1: done (mocap locomotion, crouch line-play, idle, celebrations).
- L2, L4: implemented in `Render/TagRetargetAnimInstance.*` (this pass).
- L3: ragdoll with momentum done; stumble and recover is next.
- L5: cameras, weather and crowd done; uniform dirt is next.

## Sources
- EA, *Madden NFL 27 Gameplay Deep Dive*: https://www.ea.com/games/madden-nfl/madden-nfl-27/news/madden-27-gameplay
- EA, *Madden NFL 27: Fastest Madden Ever*: https://www.ea.com/games/madden-nfl/madden-nfl-27/news/madden-27-the-fastest-madden-ever
- EA, *Madden NFL 26 Gridiron Notes*: https://www.ea.com/games/madden-nfl/madden-nfl-26/news/gridiron-notes-welcome-to-madden-26
- EA, *BOOM Tech in Madden NFL 25*: https://www.ea.com/technology/news/boom-tech-ea-sports-madden-nfl-25
- EA, *Madden 24 gameplay (SAPIEN)*: https://www.ea.com/news/madden-24-gameplay
- GDC Vault, *Environmental and Motion Matched Interactions*: https://www.gdcvault.com/play/1027465/Animation-Summit-Environmental-and-Motion
- GDC Vault, *EA Presents N-teract*: https://gdcvault.com/play/1024015/EA-Presents-N-teract-Next
- GDC Vault, *Motion Matching at EA*: https://gdcvault.com/play/1028945/Animation-Summit-Motion-Matching-at
- Reviews: https://www.shacknews.com/article/150366/madden-nfl-27-review-score, https://clutchpoints.com/gaming/madden-27-review, https://bleacherreport.com/articles/25469806-madden-nfl-27-review-gameplay-impressions-videos-and-top-features
