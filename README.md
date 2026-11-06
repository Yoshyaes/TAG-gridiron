# TAG Gridiron

An original, simulation-first American football game for PC. All teams, players, logos and names are fictional.

Every snap is a full physics simulation of 22 athletes rather than a canned animation, and the whole game is calibrated against real pro-football per-game averages.

## Play

Double-click **`Play TAG Gridiron.bat`**. It installs dependencies on first run, builds the game and opens it in your browser at `http://localhost:4173`.

For development: `npm install`, then `npm run dev`.

### Modes
- **Play Now**: you vs the CPU. You call plays on both sides of the ball, control the QB or ball carrier, or play any defender.
- **Watch a Game**: CPU vs CPU in 3D at 1x, 2x or 4x speed, or sim straight to the end.
- **Quick Sim**: a full game in a fraction of a second, with box score and play-by-play.
- **Season Sim**: a 17-week season for 32 teams, with standings and league leaders.

### Controls (keyboard or gamepad)
| Key | Action |
| --- | --- |
| `Space` | Snap / juke (ball carrier) / dive tackle (defense) / continue |
| `WASD` or arrows | Move the controlled player (camera-relative) |
| `1`–`5` | Throw to that receiver; hold `Shift` for a touch pass |
| `Tab` | Switch to the defender nearest the ball |
| `C` | Cycle cameras: broadcast, sideline, skycam, player |
| `B` | Box score |
| `R` | Coach's pick on the play-call screen |
| `Esc` | Pause menu |

## What makes it realistic
- **Measurable-based athletes.** Top speed, acceleration time constants and lateral-acceleration limits come from ratings: a 99-speed player runs about a 4.28 forty and linemen run about 5.2. Mass and momentum drive collisions, yards after contact and fall-forward.
- **Line play as a contest.** Blocks are assigned at the snap: down linemen first, then the second level. Pass blockers kick-slide into a pocket. Rushers win with power or finesse over time, blocks degrade the longer they're held, double teams hold up, and a beaten blocker stays beaten.
- **Quarterback brain.** Reads a progression, predicts each receiver's route to estimate separation at the catch point, respects deep safeties, throws to the sticks on money downs, feels unblocked rushers (hot reads), has a release windup that can get hit, and loses accuracy under pressure, on the run, in rain or snow, and when fatigued.
- **Ball flight.** 3D integration with spiral drag, gravity and wind, including touch passes over underneath defenders. Kicking accounts for distance, leg strength, wind, cold and precipitation.
- **Coverage.** Cover 0/1/2/3/4, 2-Man and fire-zone pressure. Zone defenders match threats in their area, deep defenders keep a cushion based on receiver speed, and man defenders trail with rating-based reaction lag. Contested catches are decided by who is closer to the ball when it arrives.
- **Rules engine.** NFL clock rules: two-minute warning, out-of-bounds windows by half, runoff and tempo, timeouts. Penalties are accepted or declined by expected points, with the half-the-distance rule. Includes analytics-flavored 4th-down and two-point decisions, modern kickoff touchbacks, overtime where both teams possess, fatigue rotation and injuries.

## Calibration
`npm run calibrate [games]` simulates full games headless and compares per-team-game averages with NFL norms. Current results from 80 games:

| Stat | Sim | NFL |
| --- | --- | --- |
| Points | 20.2 | ~22 |
| Plays | 62.0 | ~62.5 |
| Yards / play | 5.26 | ~5.3 |
| Completion % | 65.5 | ~64.5 |
| Yards / attempt | 7.4 | ~6.9 |
| Yards / carry | 4.19 | ~4.3 |
| Sack rate | 6.8% | ~6.8% |
| Interceptions | 0.72 | ~0.8 |
| FG % | 88 | ~85 |
| Points SD | 10.3 | ~10 |

Third-down conversion by distance matches the NFL bucket by bucket: 1–2 yards ~60%, 3–4 ~50%, 5–6 ~41%, 7–9 ~35%, 10+ ~17%. Known gaps still being tuned:
- Too many 3rd-and-long situations, which holds first downs to ~16 per game versus ~19.5.
- Slightly too many 40+ yard breakaway runs.

Other tools: `npm test` (invariants and regression), `tools/playlab.ts` (one play type, many reps), `tools/depth.ts` (pass depth profile), `tools/third_game.ts` (3rd-down splits).

## Engine note
Built with TypeScript and Three.js (WebGL, hardware accelerated). Unity 6 is installed on this machine but its editor license isn't activated (batch mode reports *"No valid Unity Editor license found"*), and Unreal Engine isn't installed. The simulation core in `src/sim` has no rendering dependencies, so it can be ported to a C# Unity front end later if you want.

## Layout
```
src/sim/      physics play sim, rules engine, playbook, league, stats (engine-agnostic)
src/render/   Three.js stadium, field, procedural player rigs, cameras, weather
src/ui/       HUD, play calling, box score
src/match.ts  game-flow controller
tools/        calibration, play lab, tests
```
