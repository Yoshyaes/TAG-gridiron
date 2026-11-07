# Morning report. Madden 19 reference pipeline

Updated after every gate. Branch `madden-extraction`, nothing pushed.

## Stages
| Stage | Result |
| --- | --- |
| 0b Host bring up | PASS |
| 1 Inventory | PASS (523,630 rows, 19 skeletons, 2,374 heads) |
| 2 Skeleton and one player | PASS (268 bone MaddenFB_Hero skeleton, 13 parts, 189.7 cm) |
| 3 Animation probe | NO GO, cut list taken (VBR AssetBank, no decoder without a port) |
| 4 Prototype batch | PASS (35 meshes, 36 textures, 3 body types, 2 helmets, manifest 265) |
| 5 Textures and materials | PASS (normals OpenGL to DirectX in texfix.py, per mesh material JSON, master M_MaddenRef_Ref) |
| 6 UE import automation | PASS (one command per kit, second run skips export and import) |
| 7 Capped scale out | running |

## On disk
- `D:\MaddenRef\extracted` (junction `extracted\`). about 1.1 GB (inventory 188 MB, Stage 3 animation banks 645 MB, Stage 2 107 MB, prototype 148 MB).

## In Unreal under /Game/MaddenRef
- `Skeleton/MaddenFB_Hero_Skeleton` (268 bones).
- `Players/Stage2/` 13 skeletal meshes, 18 textures, 9 material instances.
- `Maps/L_MaddenRef_Stage2Player` preview level with the dressed player at the origin.
- `Players/Prototype/` 35 skeletal meshes, 36 textures, 24 material instances. `Maps/L_MaddenRef_Prototype` shows thin, muscular and heavy players side by side.

## Open items
1. Provenance of the Madden 19 install (`codex.cfg`, `OrangeEmu64.dll` present). Please confirm where this copy came from.
2. Clean room statement in `docs\ANIMATION_SPEC.md` holds only while the MaddenRef guardrails hold.

## What I need from you
Nothing yet.
