# Morning report. Madden 19 reference pipeline

Branch `madden-extraction`, local only, nothing pushed. Install guard clean and audit passing at every step. Details in FINDINGS.md.

## Done so far
- **Animation decoder port.** Checkpoints A to E passed. 12,182 of 12,187 player clips are in Unreal. The twitch (key times written as floats) is fixed and every clip is re-imported.
- **Team textures (2026-11-08).** Every dressed preview level now wears the 49ers 2018 set. Jersey, sleeves, pants and socks follow Madden's own presets, and the facemask gets its preset colour. Stage 2 normals were unconverted (bump inverted in the animation previews) and now use the converted maps.
- **Retargeting (2026-11-08).** The Athlete animates on the UEFN mannequin. The 26 prototype clips are retargeted onto it in `/Game/MaddenRef/Retargeted`, for editor preview only. Limbs match the source within 0.2 degrees.

## Where to look in Unreal
- `Maps/L_MaddenRef_AnimCheckpoint` and `L_MaddenRef_AnimPrototype` show dressed players in motion (press Play).
- `Maps/L_MaddenRef_Stage2Player` and `L_MaddenRef_Prototype` show the dressed body types standing.
- `Retargeted/A_<clip>_UEFN` opens on the UEFN mannequin in the animation editor.

## Check by eye
1. Bump direction and roughness on jersey and pants. Roughness reads RSM red (jersey mean 0.31, pants 0.39). Whether red is roughness or smoothness is still unproven.
2. The socks. Madden's 49ers WHI preset gives a charcoal sock texture. It may need a shader tint we do not reproduce.
3. The retargeted clips on the UEFN mannequin.
4. Gloves are still untextured because the team set has no glove preset.

## C drive plan (needs your approval, nothing deleted)
C has 43.2 GB free. Two items hold 45 GB.
- `Animations/Full` is 20.6 GB. Nothing outside it references it.
- `Saved/MaddenRefImport` is 24.4 GB of leftover temporary FBX copies, all duplicates of files on D. The importer no longer leaves them.

| Option | What happens | C freed | Cost |
| --- | --- | --- | --- |
| A. Reference project on D (recommended) | New UE 5.8 project `D:\MaddenRef\MaddenRefProject` with its cache on D. `Full`, the skeleton and the pipelines are copied with the same package paths, so no reimport. Twenty random clips are re-checked against the decode there before anything is deleted. Full batch tooling then targets that project | about 45 GB | About an hour of copy and checks. Two projects to open |
| B. FBX on D, regenerate on demand | `Full` is deleted from the main project. A new pull command imports chosen clips from the verified FBX on D at about one clip per second | about 45 GB | Full library no longer browsable in Unreal |

Both options also delete `Saved/MaddenRefImport`. Neither touches the 26 prototype clips, the three checkpoint clips, the 29 in place preview copies, the retargeted clips or any preview level. Those are separate assets, and the probe found no reference from them into `Full`.

## Open items
1. **Provenance.** Fred will answer separately. `codex.cfg` and `OrangeEmu64.dll` stay out of the tooling and git.
2. **Not done.** Jersey numbers and names, gloves, retargeting beyond the prototype clips, root motion setup, additive clips, non player rigs.
3. `Unreal/TAGGridiron/Config/DefaultEditor.ini` is untracked and left out of commits.

## What I need from you
1. Look at the four dressed levels and the retargeted clips (checks above).
2. Choose option A or B for the C drive, and approve deleting `Saved/MaddenRefImport`.
