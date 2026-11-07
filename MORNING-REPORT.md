# Morning report. Madden 19 reference pipeline

Updated after every gate. Branch `madden-extraction`, nothing pushed.

## Stages
| Stage | Result |
| --- | --- |
| 0b Host bring up | PASS |
| 1 Inventory | running |

## On disk
- `D:\MaddenRef\extracted` (junction `extracted\`). Stage 0b test head only, a few MB.

## In Unreal under /Game/MaddenRef
- `Stage0b/SM_faulkmarshall_11413_head__head_lod0_mat` and `__eyes_lod0_mat` (static, 25.5 x 22.8 x 37.4 cm).

## Open items
1. Provenance of the Madden 19 install (`codex.cfg`, `OrangeEmu64.dll` present). Please confirm where this copy came from.
2. Clean room statement in `docs\ANIMATION_SPEC.md` holds only while the MaddenRef guardrails hold.

## What I need from you
Nothing yet.
