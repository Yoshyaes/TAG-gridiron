# Madden 19 reference pipeline. Findings

Every surprise, with evidence. Newest stage last. Private reference work, see `extracted\README.md`.

## Open items for Fred
1. **Provenance of the install.** `D:\Games\Madden NFL 19` contains `codex.cfg` and `OrangeEmu64.dll`, which a retail EA or Origin install does not include. Not confirmed where this copy came from. The licensing note assumes a legitimately owned copy. Both files are excluded from all tooling, never copied and never committed (`.gitignore` lists them, `madden.py verify` flags them).
2. **Clean room tension.** `docs\ANIMATION_SPEC.md:3-6` says nothing was extracted from any game. Reference imports now live inside the TAGGridiron project under `/Game/MaddenRef`. That statement stays true for shipped content only while the guardrails hold (gitignore, `DirectoriesToNeverCook`, `maddenref_audit.py` at every gate, no runtime code path to MaddenRef).
3. **SDK version mismatch.** Frosty's MADDEN19SDK.dll reports SDK version 599397. The install's file system head is 568969. The editor would offer an SDK update dialog. Everything tested so far reads correctly, so this is noted, not acted on.

## Step 0 recon (2026-11-07)
- UE 5.8.4 at `C:\Program Files\Epic Games\UE_5.8`. No .NET (Core) SDK. .NET Framework 4.8 SDK and targeting pack plus VS 2019 and 2022 Build Tools (Roslyn csc) are installed. Python 3.14 default, 3.11 to 3.13 present. No Blender.
- Frosty 1.0.6.3 binaries are x64 .NET Framework 4.8. MADDEN19SDK.dll targets 4.7.
- The kickoff guessed superbundle names. They are real files in `Data\Win32`. `playercontent_sb`, `playercontentlaunch_sb`, `coachcontent_sb`, `coachcontentlaunch_sb`, `stadiums_sb`, `stadiumslaunch_sb`, plus `refereecontent_sb`, `propcontent_sb`, `longshotsb`, `teamwipe_sb` and others.
- Disk. C had 108 GB free, so extracts live on D (`D:\MaddenRef\extracted`, junctioned at repo `extracted\`).

## The earlier Frosty Launch (known issue 4)
- `ModData\Editor\` held 14 real files (1.5 MB, including `mods.json` naming `EditorMod_4087.fbmod`), 18 folders and **117 symbolic links pointing back at real game files and folders** (`ModData\Editor\Data` linked to the real `Data` folder, and one link per patch `.sb` and `.toc`). A recursive delete that followed links would have destroyed game data.
- `CryptBase.dll` in the game root matched Frosty's `ThirdParty\CryptBase.dll` (SHA256 `3E8768...D454`). It is the Frosty mod loader hook.
- Removal (authorized once by Fred). Every entry was recorded first in `logs\moddata_removed.csv` (path, kind, size, SHA256 or link target). Then links were unlinked as links, files unlinked one by one and folders removed only when empty. Nothing recursive. A snapshot of the real install taken before removal and one taken after are identical (480 entries). `logs\guard_baseline.json` is the clean baseline every later run is compared against.

## Stage 0b. Headless host
- **Headless works.** `tools\madden\FrostyHost` (C# net48 x64, built with Roslyn csc against the shipped DLLs) initializes the Madden 19 profile from Frosty's existing cache in about 2 s and sees 320,890 EBX assets.
- Three GUI dependencies surfaced and were each resolved without touching Frosty.
  1. The FBX exporter unpacks tangent space with a D3D11 compute shader through `FrostyDeviceManager`, which reads Frosty `Config`. The host calls `Config.Load(path)` on `%LOCALAPPDATA%\Frosty\editor_config.json` read only. `Config.Save` is never called. The file's SHA256 was checked unchanged after runs.
  2. The shader lookup goes through `App.PluginManager`. The host constructs a `PluginManager` (loads plugin assemblies, runs no startup actions). `PluginManager.Initialize` is not called because it builds GUI asset definitions.
  3. The first lazy D3D11 device creation throws a NullReferenceException outside the editor and the second succeeds. The host warms the device up at init.
- **Scale root cause confirmed in code and on disk.** Frosty's Scale option only sets the FBX unit label. Vertices stay in meters. The Centimeters export is geometry identical to the hand export (3,252 head and 74 eye vertices, 5,974 and 120 polygons, UnitScaleFactor 1). The Meters export writes UnitScaleFactor 100 and imports into UE at **25.49 x 22.76 x 37.44 cm with no offset scale**. Fix lives in one place, `units` defaults to `Meters` in FrostyHost and in every job file.
- Texture export is byte identical to the hand export PNG (SHA256 `36ef70...0868`).
- UE 5.8 imports FBX through Interchange from `AssetImportTask`, headless with `-nullrhi`, in about 5 s per run. Multi node FBX files give one asset per material node (known issue 2). `maddenref_import.py` imports into a staging folder and renames to `<name>` or `<name>__<node>`, so re-runs replace cleanly.
- Under `-nullrhi`, the static mesh UV channel query returns 0 even though the FBX has one UV set. UV checks use `tools\madden\fbxinfo.py` on the FBX instead.
- The head mesh's LOD0 references 87 skeleton bones with indices up to 220. Its skeleton must therefore have at least 221 bones.

## Stage 1. Inventory
- Full inventory in 9 s through FrostyHost. 320,890 EBX, 202,740 RES and chunk rows (523,630 rows total) across 81,649 bundles. Outputs `extracted\inventory\inventory.csv`, `bundles.csv`, `inventory-summary.md` and `families.json`.
- **Skeletons.** 19 SkeletonAssets. The player candidate is `content/characters/rig/skeleton/player/MaddenFB_Hero_Skeleton`. Others are sideline player, actor (male, female, child), crowd and Longshot props.
- **Body types.** Every player body and apparel part comes in four variants, `_t` thin, `_m` muscular, `_h` heavy and `_d` default. Bodies are `content/characters/player/bodies/{fullarms,nohands,legs}_{t,m,h,d}_mesh` (12, plus old fallbacks).
- **Parts.** Helmets 185 skinned meshes under `parts/helmets/<model>/` split into shell, facemask, visor, visor clips, accessories and green dot. Footwear 936, armgear 336, torso apparel 132 (sleeves, shoulder pad variants), legs apparel 100 (socks), torso gear 16 (towel, neck roll).
- **Uniforms have no meshes.** `parts/uniforms/<team>/<year>/` holds 32,011 TextureAssets (`_COL`, `_NORM`, `_RSM`) and 2,696 AppearanceShaderPresets. Jersey and pants geometry must come from the body meshes. To be confirmed in Stage 2.
- **Heads.** 2,374 player heads at `content/characters/player/players/<letter>/<name_id>/<name_id>_mesh`.
- **Coaches.** 936 skinned meshes under `content/characters/coach/` (per coach head, headset, warm and cold bodies, plus 123 shared bodies).
- **Stadiums.** 3,792 ObjectBlueprints under `content/worlds/components/stadiums/` and 4,053 world RigidMeshAssets.
- **Animation risk is now visible.** 1,383 AntStateAssets and 1,338 AssetBank resources. Gameplay animation lives in one bank, `animations/antanimations/levels/maddengame/maddengame_win32_antstate` (635 MB raw, 352 MB compressed). Another 137 MB bank serves launch content. This matches the FIFA style single bank layout reported by the community.
- The inventory csv has fields over 131,072 characters (bundle lists). Readers must raise `csv.field_size_limit`.

## Stage 2. Skeleton and one complete player
- **Skeleton found by bound check.** `content/characters/rig/skeleton/player/MaddenFB_Hero_Skeleton` has 268 bones (Reference, AITrajectory, Hips, Spine to Head and Face, a full face rig, and RM_ deformation helpers for pads, pecs, scapula and knees). Across head, arms, legs, jersey, pants, socks, sleeves, helmet, facemask, shoes and gloves, the highest vertex bone index is 255. The sideline skeleton (76 bones) is ruled out. Names at the referenced indices make sense (98 `helmetPlacement`, 252 `RM_RightKnee`).
- Frosty's Madden path ignores per section bone lists, so the bound check plus name check is the safety net against silently wrong weights.
- **A player is assembled at runtime.** The player ObjectBlueprint references only the head mesh (plus face poser and accessory joint offset data). Body, uniform and gear come from shared part meshes chosen by game code, so the reference "player" is built by picking parts.
- **Parts used.** Head `faulkmarshall_11413_mesh`, `bodies/fullarms_d_mesh` (bare arms only), `bodies/legs_d_mesh` (lower legs only), `torsoapparel/jersey/jersey_d_mesh`, `legsapparel/pants/pants_d_mesh`, `socks_mid_d`, `sleeve_tight_d`, Xenith helmet shell and Prism facemask, Nike low shoes left and right, gloves left and right. Uniform textures from `uniforms/49ers/2018` (WHIALT jersey and pants, GOLALT helmet).
- **Assembled size.** Every part is authored in place on the shared bind pose. Assembled bounds are 189.7 cm tall from sole to helmet top (head top 185.8 cm), 153 cm across the bind pose arms. FBX is Y up and the player faces +Z. In UE the Head bone sits at 165.7 cm.
- **Skin weights exported.** Each FBX has the full 267 limb hierarchy, one Skin deformer and one Cluster per used bone (head 87, arms 83, helmet 1).
- **Texture formats.** Colour maps are BC7_SRGB (jersey BC1A_SRGB at 2048 x 4096). Normal maps are BC7_UNORM, not two channel BC5, so the Z rebuild may not be needed. Checked in Stage 5. RSM maps are BC7_UNORM.
- **Material parameters live in AppearanceShaderPresets and MeshVariationDatabases.** The player skin preset binds `arm_RSM_skintone6`, `arm_normal`, `arm-sweat-packed`, `arm_skintone7` and `SkinNoise` to `player_skin_shader`. The head MeshVariationDb binds colour, normal, `face_skinTile`, `commonRSM` and `playerHead_Sweat` per material slot.
- **UE 5.8 Interchange lessons.**
  1. `AssetImportTask.options` is ignored by Interchange. Skeletal imports go through `InterchangeManager.import_asset` with a saved pipeline asset (`/Game/MaddenRef/_Pipelines/P_MaddenRef_mesh`) as the override.
  2. The commandlet's asset registry has not scanned /Game when the script starts. Call `search_all_assets(True)` first.
  3. `delete_asset` returns True but the object stays in memory for the session, so delete then rename onto the same name fails. Renames also leave redirectors. The importer therefore copies each FBX to `<asset name>.fbx` and imports straight into the destination with replace existing. A bootstrap job named `MaddenFB_Hero` makes Interchange create `/Game/MaddenRef/Skeleton/MaddenFB_Hero_Skeleton` itself.
  4. The deprecated `combine_static_meshes` property raises in Python. Use `combine_static_meshes_behavior`.
- Preview level `/Game/MaddenRef/Maps/L_MaddenRef_Stage2Player` places all 12 parts at the origin with basic material instances (`M_MaddenRef_Basic`, RSM wired behind a `UseRSM` switch until Stage 5 confirms channel meaning). Visual sign off is left for Fred, since `-nullrhi` cannot render.
