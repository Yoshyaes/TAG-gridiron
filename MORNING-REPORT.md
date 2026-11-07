# Morning report. Madden 19 reference pipeline

Branch `madden-extraction`, local only, nothing pushed. Install guard clean at every gate.

## Current job. Animation decoder port
| Checkpoint | Result |
| --- | --- |
| A Bank header and clip index | PASS. 25,349 clips in `extracted\animation\animation_inventory.csv` |
| B Decode idle, run, tackle | PASS. Run root speed 7.06 m/s, bone lengths constant, knees bend forward, idle loop closes to 0.8 deg. Frame 0 against the T pose bind is interpreted (see FINDINGS) |
| C FBX export and UE import of the three clips | running |
| D Prototype batch (about 25 core clips) | waiting |
| E Full clip list in chunks | waiting |

Key facts so far. Madden's ANT bank is FIFA 17's format with little endian GD streams, yard units, and the player rig in a second (launch) bank.

## Earlier stages (all done)
0b host, 1 inventory, 2 one player, 4 prototype kit, 5 materials, 6 UE automation and 7 capped scale out passed. Stage 3 (the first animation probe) was a no go and is now superseded by this job.

## On disk
`D:\MaddenRef\extracted` (junction `extracted\`) about 2.6 GB including the two ANT banks (635 MB and 137 MB). Manifest matches the disk. D has about 450 GB free.

## In Unreal under /Game/MaddenRef
Unchanged from the last report. Animations will land in `/Game/MaddenRef/Animations`.

## Open items
1. **Provenance.** Fred will answer separately. `codex.cfg` and `OrangeEmu64.dll` stay out of the tooling and git.
2. **Frame 0 criterion.** The bind pose is a T pose, so Checkpoint B compares frame 0 by structure (bone lengths, joint frames, distance from a random decode), not by equality.
3. **Known issue 5.** One UE material slot per mesh.

## What I need from you
Nothing yet. Visual checks of the meshes are with you.
