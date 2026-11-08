# Morning report. Madden 19 reference pipeline

Branch `madden-extraction`, local only, nothing pushed. Install guard clean and audit passing at every step. Details in FINDINGS.md.

## Done so far
- **Animation decoder port.** Checkpoints A to E passed. 12,182 of 12,187 player clips are in Unreal, now in the reference only project on D. The twitch (key times written as floats) is fixed and every clip is re-imported.
- **Team textures (2026-11-08).** Every dressed preview level now wears the 49ers 2018 set. Jersey, sleeves, pants and socks follow Madden's own presets, and the facemask gets its preset colour. Stage 2 normals were unconverted (bump inverted in the animation previews) and now use the converted maps.
- **Retargeting (2026-11-08).** The Athlete animates on the UEFN mannequin. The 26 prototype clips are retargeted onto it in `/Game/MaddenRef/Retargeted`, for editor preview only. Limbs match the source within 0.2 degrees.

## Where to look in Unreal
- `Maps/L_MaddenRef_AnimCheckpoint` and `L_MaddenRef_AnimPrototype` show dressed players in motion (press Play).
- `Maps/L_MaddenRef_Stage2Player` and `L_MaddenRef_Prototype` show the dressed body types standing.
- `Retargeted/A_<clip>_UEFN` opens on the UEFN mannequin in the animation editor.
- The full clip library (`Animations/Full/C000` to `C082`) opens in `D:\MaddenRef\MaddenRef_ReferenceOnly\MaddenRef_ReferenceOnly.uproject`.

## Check by eye
1. Bump direction and roughness on jersey and pants. Roughness reads RSM red (jersey mean 0.31, pants 0.39). Whether red is roughness or smoothness is still unproven.
2. The socks. Madden's 49ers WHI preset gives a charcoal sock texture. It may need a shader tint we do not reproduce.
3. The retargeted clips on the UEFN mannequin.
4. Gloves are still untextured because the team set has no glove preset.

## C drive (done, option A)
C went from 40.57 GB free to 85.60 GB free (45.03 GB gained).
- The full animation batch now lives in the REFERENCE ONLY project `D:\MaddenRef\MaddenRef_ReferenceOnly`, outside the repo, git ignored, with its cache on D. Same package paths as TAGGridiron.
- Before `Animations/Full` was deleted from TAGGridiron, 20 random clips matched the decode in the new project (raw 0.02 cm, compressed 0.48 cm), both projects passed the audit, and nothing in TAGGridiron referenced `Full`.
- `Saved/MaddenRefImport` is cleared except three files with no matching manifest entry. Every removal is logged in `logs\`. Editor autosaves untouched.
- The prototype, checkpoint, in place and retargeted clips and every preview level are unchanged in TAGGridiron.

## Open items
1. **Provenance.** Fred will answer separately. `codex.cfg` and `OrangeEmu64.dll` stay out of the tooling and git.
2. **Not done.** Jersey numbers and names, gloves, retargeting beyond the prototype clips, root motion setup, additive clips, non player rigs.
3. `Unreal/TAGGridiron/Config/DefaultEditor.ini` is untracked and left out of commits.

## What I need from you
1. Look at the four dressed levels and the retargeted clips (checks above).
2. Open the reference project once to confirm the full library browses as expected. The first open builds its cache on D.
