# Morning report. Madden 19 reference pipeline

Branch `madden-extraction`, local only, nothing pushed. Install guard clean at every gate.

## Animation decoder port. Done
| Checkpoint | Result |
| --- | --- |
| A Bank header and clip index | PASS. 25,349 clips in `extracted\animation\animation_inventory.csv` |
| B Decode idle, run, tackle | PASS. Run root speed 7.06 m/s, bone lengths constant, knees bend forward, idle loop closes to 0.8 deg. Frame 0 against the T pose bind judged by structure (see FINDINGS) |
| C FBX and UE import of the three clips | PASS. 0 errors, 0 warnings, 268 tracks, no missing bones |
| D Prototype batch | PASS. 26 core clips (stance, run, cut, tackle, throw, catch, celebrate) |
| E Full clip list | PASS. 12,182 of 12,187 player clips in Unreal, 0 UE errors, 6 h 15 min |

How it works. Madden's ANT bank is FIFA 17's format with little endian GD streams, yard units, and the player rig in a second (launch) bank. The decoder is our own Python in `tools\madden\antbank\`, reading the banks the Frosty host exports. No executable reverse engineering.

Held out (5, reasons in `failures.csv`). The four additive layer clips, which are deltas and not standalone motions, and one celebration with a single bad key (a 5 m Hips pop for one frame in the source data).

## Twitch fix (2026-11-08)
The preview twitch was in the clips, not the actors. Every animation FBX wrote key times as floats, so Unreal held one pose after the first instant. Fixed in the writer, all 12,214 FBX files repaired and all sequences re-imported. Unreal now matches the decode within 0.01 cm (raw) and about 1 cm (compressed) on 20 sampled clips.

## Where to look in Unreal
Every `L_MaddenRef` level is now lit, has a grid floor, uses the `GameModeBase` override and has a camera where one player fills about a third of the view (press Play, or pilot `PreviewCamera`). Animated players play in place from preview only copies (`Animations/PreviewInPlace`). The real clips still carry their root motion.
- `Maps/L_MaddenRef_AnimCheckpoint` has the dressed player in idle, run and tackle.
- `Maps/L_MaddenRef_AnimPrototype` has the 26 core clips on a grid.
- `Animations/Full/C000` to `C082` hold every clip, named `A_<controller name>`.

## On disk
- D. `extracted\` 27.6 GB (sparse animation FBX 25 GB), 426 GB free.
- C. Unreal animations 20 GB, 70 GB free.
- Manifest 27,521 files, all hashes match.

## Open items
1. **Provenance.** Fred will answer separately. `codex.cfg` and `OrangeEmu64.dll` stay out of the tooling and git.
2. **Frame 0 criterion.** The bind pose is a T pose, so Checkpoint B judged frame 0 by structure, not equality.
3. **C drive.** C is 97 percent full (70 GB free) after 20 GB of animations.
4. **Not done.** Retargeting to the UEFN mannequin or MetaHumans, root motion setup, additive clips, non player rigs.

## What I need from you
1. Watch `L_MaddenRef_AnimCheckpoint` and `L_MaddenRef_AnimPrototype` in the editor. They have only been checked by numbers so far.
2. Say whether you want retargeting onto the UEFN mannequin or your MetaHumans next.
