# Morning report. Madden 19 reference pipeline

Branch `madden-extraction`, 9 commits, nothing pushed. Install guard clean at every gate (480 files unchanged since the authorized ModData and CryptBase.dll removal).

## Stages
| Stage | Result |
| --- | --- |
| 0b Host bring up | PASS. Headless Frosty host, scale fixed at export (Meters) |
| 1 Inventory | PASS. 523,630 rows, 19 skeletons, 2,374 heads |
| 2 Skeleton and one player | PASS. `MaddenFB_Hero_Skeleton` (268 bones), 13 parts, 189.7 cm |
| 3 Animation probe | NO GO at 01:20, cut list taken. VBR AssetBank, no decoder without a port |
| 4 Prototype batch | PASS. 35 meshes, 3 body types, 2 helmets, 49ers uniform |
| 5 Textures and materials | PASS. Normals OpenGL to DirectX in `texfix.py`, per mesh material JSON |
| 6 UE import automation | PASS. `madden.py kit <spec>` runs everything, second run skips |
| 7 Capped scale out | PASS. Bodies 16, helmets 185, heads 50, coaches 8 (28 meshes), 49ers stadium 193 |

## On disk
`D:\MaddenRef\extracted` (junction `extracted\`) 1.8 GB. Inventory 188 MB, animation banks 617 MB (evidence only), Stage 2 107 MB, prototype 177 MB, Stage 7 670 MB. Manifest 3,083 files, all hashes match. D has 451 GB free.

## In Unreal under /Game/MaddenRef (499 MB, gitignored, never cooked)
Skeleton, Players (Stage2, Prototype, Bodies, Helmets, Heads), Coaches/Sample, Stadiums/Niners, Materials (`M_MaddenRef_Ref`, `M_MaddenRef_World`). Preview levels in `Maps/` are `L_MaddenRef_Prototype` (start here), `Stage2Player`, `Stage7Bodies`, `Stage7Helmets`, `Stage7Heads`, `Stage7Coaches` and `Stage7StadiumNiners`. Audit passes. Nothing outside MaddenRef references it.

## Open items
1. **Provenance.** The install contains `codex.cfg` and `OrangeEmu64.dll`. Please confirm where this copy came from. Both are excluded from all tooling and git.
2. **Clean room.** `docs\ANIMATION_SPEC.md` says nothing was extracted. That holds for shipped content only while the MaddenRef guardrails hold.
3. **Animation.** Options are a decoder port (2 to 6 weeks, uncertain), asking the Madden modding community, or the current GASP plus procedural plan (recommended).
4. **Known issue 5.** Frosty writes no FBX materials, so each mesh gets one UE slot (eyes share the head material).

## What I need from you
1. Open `L_MaddenRef_Prototype` in the editor and eyeball it (I could only run `-nullrhi`). Check normal map direction on jersey folds, whether RSM red reads as roughness (not smoothness), and overall look.
2. Answer the provenance question.
3. Pick an animation option.
4. Say whether to push `madden-extraction` (tooling and docs only) or keep it local.
