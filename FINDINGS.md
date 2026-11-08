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

## Stage 3. Animation feasibility. NO GO (cut list taken at 01:20, inside the 2 hour box)
- **Container.** Every AntStateAsset EBX is a stub (StreamingGuid, ChunkSize). The data is an `AssetBank` RES in EA GenericData format, big endian, with `GD.REFL` (type reflection), `GD.STRM` and one `GD.DATA` record per asset.
- **Main gameplay bank.** `animations/antanimations/levels/maddengame/maddengame_win32_antstate`, 634,890,752 bytes raw, 471,492 `GD.DATA` records. Reflection declares `VbrAnimationAsset` and `FrameAnimationAsset` codecs, `ChannelToDofAsset`, 29 `DofSetList`, 34 BlendSpace, `ClipControllerAsset` and `LayoutHierarchyAsset`.
- **Clip names.** Animation assets are keyed by 64 bit hex hashes (`b9da56b1f152dd2a` style). Some controller and state names are readable (`pt_standard_stance_loop`, `run2stance`, `org_tackle`, `slight_jogging`, `p_celebrate`), so a future decoder could map names to clips through the state graph.
- **Why no go.** Nothing available decodes this today. Frosty 1.0.6.3 has no animation decoding. The only mature VBR decoder is the FIFA 17 edition FrostyAnimationEditor fork, which targets the FIFA 17 profile. Running it on Madden data means adapting its GenericData and VBR readers and the DOF to skeleton mapping, which is a decoder port, explicitly out of scope.
- **Evidence kept.** `extracted\stage3\raw\maddengame.assetbank` and one small NIS bank, reproducible from `tools\madden\jobs\stage3_probe.json`.
- **Options for Fred.**
  1. Port the FIFA 17 edition decoder (GenericData reflection reader, VBR and Frame codecs, ChannelToDof mapping onto `MaddenFB_Hero_Skeleton`). 2 to 6 weeks, uncertain, because Madden's codec version may differ from FIFA 17.
  2. Ask the Madden Modding Community whether their closed animation plugin can export clips. Unknown effort, depends on them.
  3. Keep GASP plus the procedural layers in `docs\ANIMATION_SPEC.md` (current plan). Close to zero new effort. Recommended.
- **Effect on later stages.** Stage 4 and Stage 6 drop animations. Skeleton, meshes and textures stay as visual and proportion reference.

## Stage 4. Prototype set batch (meshes only, animations cut)
- **Kit.** One skeleton, three body types (t, m, h) each with arms, legs, jersey, pants, socks, tight sleeves, shoes and gloves, two helmets (Xenith shell with Prism facemask, Riddell SpeedFlex shell with 2 bar facemask), one head. 35 meshes and 36 textures, driven by `tools\madden\prototype.py` (resolves the kit from the inventory, dumps each mesh's MeshVariationDb, then writes `jobs\prototype.json`, `jobs\ue_prototype.json` and `jobs\mat_prototype.json`).
- **Manifest.** `extracted\manifest.json` lists every exported file with source asset, bundles, superbundles, size, SHA256, export params, texture format and, for meshes, the MeshVariationDb path and per slot texture and vector parameters. 265 entries after Stage 4, matching the disk.
- **Uniform pieces carry only template textures in their MeshVariationDbs** (`flatGrey_color`, invisible number and patch slots, wrinkle and pad normal arrays). Team textures are chosen at runtime, so the 49ers 2018 set is applied as an explicit override.
- **MeshVariationDb slots index materials across all LODs** (a one section mesh with three LODs has three slots). The binding uses LOD0 section `material_id`.
- **Known issue 5.** Frosty's FBX exporter has `EXP_FBX_MATERIAL` hard coded off, so every imported mesh has one UE material slot and multi section meshes share it (the eyes take the head material). Listed, not fixed. A fix needs per section material nodes written into the FBX.
- **Spot check (five random).** `SK_fullarms_h`, `SK_glovel_t`, `SK_sleeve_tight_h`, `SK_pants_t`, `SK_socks_mid_t`. UE bounds equal FBX bounds within 0.2 cm (Y and Z swapped by the Y up to Z up conversion), 268 bones on the shared skeleton, at least one UV set. All pass. Saved in `logs\spotcheck_stage4.json`.
- Preview level `/Game/MaddenRef/Maps/L_MaddenRef_Prototype` shows thin, muscular and heavy players side by side at x = -120, 0 and 120 cm (Xenith, SpeedFlex, Xenith).
- **failures.csv review.** 15 rows from `stage2probe` (importer debugging, all fixed before the Stage 2 gate: deprecated property, registry not scanned, delete then rename collision). 16 rows from the first Stage 4 material run (MeshVariationDb slot index past the single UE slot, fixed by the LOD0 section mapping and the one slot rule). No open failures.

## Stage 5. Textures and materials
- **Normal maps are OpenGL style (green up), not DirectX.** Curl test on clean maps: jersey 0.95 confidence, pants 0.81, Longshot legs 0.42, shoes 0.45. Low detail maps (arm, vent hole) are inconclusive. The head is inconclusive (0.13) because its atlas has many UV islands. After conversion the jersey tests DirectX at 0.95.
- **Some normal maps pack other data in blue.** The head normal stores XY in RG, duplicates G into A and keeps a mask in B. Socks store 0 in B. Jersey, pants, shoes and legs carry a real Z.
- **Fix lives in one place.** `tools\madden\texfix.py` inverts green and rebuilds Z from XY for every normal map, writes `extracted\prototype\textures_ue\<name>_ue.png`, keeps the head's blue mask as `_bmask.png`, and records each output in the manifest with its source hash. Unreal imports normals only from `textures_ue`.
- **Normal maps are BC7, not BC5.** The kickoff assumed two channel BC5. Only the Z rebuild matters, and it is applied uniformly.
- **RSM channels.** Cloth `RSMTexture` (jersey, pants, socks) has B near 0 everywhere, consistent with R roughness, G specular, B metallic. Skin uses `R_S_SSR_T_Texture` (arm B 0.37, A 0.26), read as R roughness, G specular, B subsurface, A translucency, so metallic falls back to a scalar for skin (`RSMIsSkin` switch). Whether R is roughness or smoothness is not proven. The name says roughness and the jersey's mean of 0.31 is plausible but smooth. Flagged for Fred's visual check.
- **Per mesh material description JSON.** `extracted\prototype\materials\<mesh>.material.json` holds the source mesh, MeshVariationDb path, every Madden material slot with its shader parameter names (`colorTexture`, `normalClampTexture`, `normalWrapTexture`, `R_S_SSR_T_Texture`, `RSMTexture`, `sweatNormalTexture`, wrinkle, pad and number slots), the conventions above, and the Unreal bindings. `maddenref_materials.py` builds material instances from these files only.
- **Master material** `/Game/MaddenRef/Materials/M_MaddenRef_Ref` takes BaseColor, Normal and RSM textures plus `UseRSM` and `RSMIsSkin` switches. Stage 2's preview keeps the earlier `M_MaddenRef_Basic` with unconverted normals. The prototype set is the current reference.
- Not modelled yet. Detail normal tiling (`normalWrapTexture`), sweat, dirt and grass masks, team colour palettes, jersey numbers and names (number and font slice textures exist per team). These need Madden shader graph work and were left out of the reference materials.

## Stage 6. UE import automation
- **One command per kit.** `python -I tools\madden\madden.py kit <spec>` runs plan, export, `texfix.py`, material JSON, Interchange import, material instances and preview level, manifest verify, install guard and audit, logging each step to `logs\kit_<name>_result.json`. A second run skips everything (host `todo=0`, UE `todo=0`).
- **Kit specs** (`tools\madden\kits\*.json`) replace the hard coded prototype. They select meshes by explicit path with body type expansion, by regex (first or all matches), or by seeded random sample. They add team textures and overrides, and choose a preview layout. Specs without a skeleton import as static meshes (`SM_`).
- **Skeleton bound check per kit.** Any mesh whose highest vertex bone index reaches the skeleton's bone count is excluded and listed in the kit summary instead of exported with garbage weights.
- **UE import skip state.** `logs\ue_state.json` records each imported asset's source hash. An import is skipped when its `.uasset` exists and the hash matches.
- **Disk projection.** 1 MB per mesh and 8 MB per texture (twice the Stage 4 averages). Each kit checks the projection against the 50 GB floor on D and is skipped, not partly run, when it would break it.
- Scale lives in one place (FBX meters at export). Naming and Content layout are documented in `tools\madden\README.md`.
- Animations are not hooked up because Stage 3 was a no go.

## Stage 7. Capped scale out
| Chunk (kit) | Selection | Meshes | Textures | Material instances | Result |
| --- | --- | --- | --- | --- | --- |
| `s7_bodies` | every player body mesh | 16 | 8 | 16 | pass |
| `s7_helmets` | every helmet mesh (shells, facemasks, visors, clips, accessories, green dots) | 185 | 4 | 28 | pass |
| `s7_heads` | random sample of 50 player head folder meshes (seed 19) | 50 | 78 | 50 | pass |
| `s7_coaches` | 8 random coaches, all their meshes (seed 19) | 28 | 72 | 28 | pass |
| `s7_stadium_niners` | 49ers stadium components plus shared sideline objects (static) | 193 | 137 | 168 | pass |
- Every kit's second run skips all export and import work. No meshes were excluded by the skeleton bound check. Projected sizes stayed far under the 50 GB floor (largest 1.3 GB, D had 451 GB free).
- **Coaches use the player skeleton.** Coach indices fit both `MaddenFB_Hero_Skeleton` (268) and the actor `Male_Skeleton_6ft` (298, same first 60 bones), but only Hero gives sensible names (body max index 250 is `RightToeBase` on Hero, a pec helper on the actor skeleton).
- **Head folders hold more than heads.** The 50 sample includes hair caps (`*_haircap_mesh`), because hair lives in the same folder.
- **Variation DB matching bug, fixed.** A prefix match picked a sibling part's DB (`<base>_hair_playerhead_brt` sorts before `<base>_playerhead_brt`), leaving 14 of 50 heads untextured. Ranking candidates by fewest extra name tokens fixed all 50.
- **World meshes keep materials in the mesh EBX.** Stadium meshes have no MeshVariationDb. Each `MeshMaterial` points at a `SurfaceShaderPreset` (`colorTexture` colour plus AO, `nsmTexture` RG normal XY, B smoothness, A metal, plus `colorTile` tiling). The kit runner now dumps presets in a second plan pass. `texfix.py` splits NSM into a DirectX normal and an RSM style map (roughness = 1 minus smoothness). `M_MaddenRef_World` adds a Tiling scalar.
- **One normal map rule for the whole game.** Across 53 stadium NSM maps the vote was mixed (36 OpenGL, 17 DirectX, mostly low confidence), but the strongest structured maps (chainlink fence 0.75, stand cloth 0.61) test OpenGL like the character maps, while the DirectX votes come from isotropic noise surfaces. Green is inverted for every Madden normal.
- **Nanite off for reference statics.** The first spot check failed on two small stadium props (UE bounds up to 7 cm larger than the FBX). Nanite was padding the bounds. With `build_nanite` off, all five spot checks match within 0.2 cm.
- Stadium pieces are authored in local space and laid out on a 50 m grid in `L_MaddenRef_Stage7StadiumNiners`, not assembled into a stadium. Placement lives in level data that was not extracted.
- 25 of 193 stadium meshes have no usable colour, normal or NSM texture (glass, decals and special shaders) and keep the default material.
- Spot check (five random across Stage 7, report only Unreal run). `SK_novaknick_19965_haircap`, `SM_gatoradestand`, `SM_niners_bgnis_highways`, `SM_gatoradedrinkholder`, `SM_anvilwheels`. All pass (`logs\spotcheck_stage7.json`).
- `failures.csv` has 31 rows in total, all from Stage 2 and Stage 4 debugging and all resolved (see the Stage 4 review). No failures were logged in Stage 5, 6 or 7.
- Frosty's `editor_config.json` hash, the hand export `Test` folder and `Caches\madden19.cache` are unchanged by the whole run.


# Animation decoder port (started 2026-11-07, approved by Fred)
Tooling is in `tools\madden\antbank\`. It is Python and reads the AssetBank RES that FrostyHost exports through the Frosty SDK, so it reads the bank the same way the rest of the pipeline does. `gd.py` GenericData reader, `vbr.py` VBR codec, `antdb.py` cross bank database and rig index, `pose.py` evaluation on MaddenFB_Hero_Skeleton, `inventory.py` Checkpoint A, `checkb.py` Checkpoint B. The FIFA 17 edition FrostyAnimationEditor has no licence file, so it was read as format reference and the code here is an independent reimplementation, private like everything else.

## Checkpoint A. Bank header and clip index. PASS
- **Container layout matches FIFA 17.** Big endian header, root key table T, export table C, import tables A and B, then GD streams. Only the version differs. The gameplay bank `levels/maddengame` is version 3 with 471,492 root keys and empty import tables. The small NIS banks are version 5.
- **Madden's GD streams are little endian.** Tags end in `l` (`GD.STRMl`, `GD.REFLl`, `GD.DATAl`) where FIFA 17's end in `b`. Every size, offset and value inside them is little endian.
- **The object header moves with byte order.** Marker `0x030E6205` at +0x00 and type id at +0x10 (FIFA 17 big endian has +0x04 and +0x14). Data offset u16 at +0x1C in both.
- The type table parses unchanged (468 types in the gameplay bank). `AnimationAsset`, `VbrAnimationAsset`, `FrameAnimationAsset`, `ClipControllerAsset`, `ChannelToDofAsset`, `RigAsset`, `SkeletonAsset` and `LayoutHierarchyAsset` carry the field names the FIFA 17 decoder reads.
- **VBR data layout and bitstream match.** On 400 random clips the layout arithmetic (key times, constants, channel map, widths, ramps, blocks) equals the Data length exactly, and block 0's bitstream ends within 0 to 7 spare bits (byte padding).
- **Clip names come from controllers, as in FIFA 17.** A clip's own name is the hex key of its `ClipControllerAsset` (sometimes with `_1`). The controller name is readable (`MG_Loco_Run_Average__P01`). Controllers usually play two clips, a body clip (`Anim`) and a finger clip (`LayerAnim`).
- **Inventory.** `extracted\animation\animation_inventory.csv` lists 25,349 clips (25,026 `VBR `, 323 `FRAM`), 25,166 named by a controller, with frames, fps, duration, codec id, trim, flags, channel counts and channel map keys. Rates are 60 fps (14,048 clips) and 30 fps (11,245), plus a few at 24, 59.94 and 100. Median length 142 frames, longest 4,448.
- **Two banks are needed.** Clips are in the gameplay bank, but their `ChannelToDofAsset`s, the main `DofSetList` and the player rigs are in the launch bank (`content/common/configs/bundles/launch_sba`, 137 MB, 145,037 objects). Both are exported by `tools\madden\jobs\anim_banks.json`.
- VBR `Flags` use bits FIFA 17 does not document (2, 3, 7 and 8, for example 0x10C). They did not affect decoding in Checkpoint B.

## Checkpoint B. Three clips decoded onto MaddenFB_Hero_Skeleton. PASS (one criterion interpreted, see below)
- **Channels.** Body clips use channel maps `6a6eb813061beab6` (39 DOFs) or `e5b9c7b4b01de68d` (37 DOFs). They hold 29 to 31 joint rotations (`Hips` to `Head`, arms, legs, `AITrajectory`, ball and ankle effectors), 6 translations (`AITrajectory.t`, `Hips.t`, `Left/RightFootball.t`, `Left/RightAnkleEffectorAux.t`) and 2 floats (`Left/RightToeBase.vel`). Finger clips use `0183f7cfb285b0d9` (38 finger rotations). DOF names resolve through the launch bank's rigs (8,808 named DOFs) and equal MaddenFB_Hero_Skeleton bone names. `MaddenRigamatePreview` (266 joints) is a strict subset of the Hero skeleton.
- **Units are yards.** Every `MaddenRig` default translation is 1.0936 times the EBX bind translation, so meters = rig x 0.9144. FIFA 17 used feet.
- **Rotation convention.** Standard quaternion (x, y, z, w) to matrix. With it, 227 of 266 shared joints' rig defaults equal the EBX bind within 1 degree. The rest are RM_ deformation helpers, IK effectors and `Hips`, which differs by exactly 90 degrees about the vertical axis. Children are relative to Hips, so that offset only changes heading. Decoded as is, the run faces and moves along +Z, the mesh's forward axis, so no correction is applied.
- **Clips.** Idle `HB_Blocking_Idle_Loop__P01` (key `f7f8174082a094de`), run `MG_Loco_Run_Average__P01` (`3baf401e5a1208cb`), tackle `2man_Catch_Def_In_Over_ShoveTackle__P01` (`5e9d6a52c48f75cb`). Full numbers are in `extracted\animation\checkpoint_b.json`.

| Check | Idle | Run | Tackle |
| --- | --- | --- | --- |
| No NaN or infinity | yes | yes | yes |
| Bone length relative deviation (real bones) | 4e-15 | 1e-14 | 1e-14 |
| Root speed (AITrajectory, horizontal) | 0.10 m/s | **7.06 m/s** | 2.43 m/s |
| Facing against direction of travel (cos) | stationary | 0.99 | 0.77 |
| Knees bend forward (left, right) | 100%, 100% | 100%, 100% | 100%, 100% |
| Knee angle range | 100 to 111 deg | 50 to 166 deg | 53 to 161 deg |
| Head height | 1.27 to 1.35 m (blocking crouch) | 1.50 to 1.57 m | 1.14 to 1.69 m |
| Lowest foot above ground | 0.045 m | 0.014 m | 0.028 m |
| First to last frame (median) | 0.81 deg (loop closes) | 4.8 deg | 22.8 deg (not a loop) |
| Frame 0 against bind, median spine and head, legs, arms | 12, 51, 27 deg | 9, 26, 25 deg | 14, 22, 29 deg |

- **Frame 0 against the bind pose, interpreted.** The Hero bind pose is a T pose, and these clips start in a crouch, a run stride and a ready stance, so frame 0 cannot equal it. A scrambled decode would leave joints at random rotations, median about 126 degrees from the bind. The decoded medians are 9 to 51 degrees, bone lengths equal the bind exactly, and the joint frames follow the bind convention (227 rig defaults within 1 degree). No T pose clip exists in the bank to test it literally. Flagged for Fred.
- The largest frame to frame steps are on `LeftAnkleEffectorAux` and `RightAnkleEffectorAux` (IK targets, not skinned bones), up to 54 degrees per frame in the run. Real joints stay smooth (idle maximum 7 degrees per frame at 60 fps, on Head).

## Checkpoint C. FBX export and Unreal import of the three clips. PASS
- **Exporter.** `tools\madden\antbank\fbxanim.py` writes ASCII FBX 7.4 that mirrors Frosty's skeleton FBX. Y up, meters (UnitScaleFactor 100), the same 268 bone names and parents, bind = EBX LocalPose, Euler XYZ (R = Rz Ry Rx), and a BindPose with the EBX global matrices. Every bone is keyed on every frame. Joints a clip does not drive keep the EBX bind, so RM_ deformation helpers and the face stay at the mesh bind. The finger layer clip (`LayerAnim`) is merged into the body clip.
- **Convention check.** The Euler extraction reproduces Frosty's own Lcl Rotation and Lcl Translation values exactly (for example Hips 90, 0, 90 and LeftArm 3.19, -29.03, 32.46), so the animation FBX and the mesh FBX describe the same skeleton.
- **Two Interchange findings, both fixed.**
  1. Interchange ignores the FBX `CustomFrameRate` and guessed 12 fps, then refused clips whose length is not frame aligned. The exporter now writes the standard FBX time mode (6 for 30 fps, 3 for 60 fps, and so on) and the importer bakes at the clip's own rate (`custom_bone_animation_sample_rate`).
  2. Without a BindPose node Interchange warned about invalid bind poses. The exporter now writes one and the warning is gone.
- **Result.** `/Game/MaddenRef/Animations/Checkpoint/A_HB_Blocking_Idle_Loop__P01`, `A_MG_Loco_Run_Average__P01` and `A_2man_Catch_Def_In_Over_ShoveTackle__P01`, all on `MaddenFB_Hero_Skeleton`. The import log ends `Success - 0 error(s), 0 warning(s)`. Every clip has 268 bone tracks and none is missing from the skeleton.

| Clip | Length | Frames | Head height at t0 (UE, Python) | Root travel (UE, Python) |
| --- | --- | --- | --- | --- |
| Idle | 1.317 s | 80 | 127.5 cm, 126.7 to 134.5 cm | 0.5 cm, 0.4 cm |
| Run | 1.733 s | 53 | 156.3 cm, 150.3 to 156.7 cm | 11.33 m, 11.327 m |
| Tackle | 2.433 s | 147 | 142.5 cm, 113.6 to 168.8 cm | 5.59 m, 5.59 m |

- Preview level `/Game/MaddenRef/Maps/L_MaddenRef_AnimCheckpoint` puts the dressed Stage 2 player in each clip side by side (every part plays the same sequence). Visual sign off is left for Fred.
- Install guard unchanged, manifest 3,099 files matching the disk, MaddenRef audit passes (1,283 packages).

## Checkpoint D. Prototype batch of 26 core clips through the kit system. PASS
- **Kit integration.** Specs with `"kind": "anim"` run through `tools\madden\antbank\animkit.py` from `madden.py kit`. Steps are plan (controller names to body and finger clips), disk floor, decode with a plausibility gate, FBX export with manifest entries, Unreal import of the clips that passed, preview level, verify, install guard and audit. Spec `tools\madden\kits\anim_prototype.json`.
- **Plausibility gate per clip.** A clip is held out of the Unreal import, and logged to `failures.csv` with a reason, when any value is not finite, a bone length varies by more than 1e-4, the head goes above 2.6 m or never above 0.2 m, a foot goes 0.3 m below ground, or the root moves faster than 15 m/s.
- **Result.** 26 of 26 clips decoded, passed the gate, exported and imported. The Unreal log ends `Success - 0 error(s), 0 warning(s)`. Every clip has 268 bone tracks, none missing from the skeleton. A second run skipped all 26 (export `skipped=26`, Unreal `todo=0`). Manifest 3,151 files matching the disk, install guard unchanged, audit passes (1,310 packages).

| Category | Clips (controller names) | Numbers |
| --- | --- | --- |
| Stance | `Idle_Preplay_Crouch_Stance_var3__P01`, `MADNAUG04_0054_1_1_PrePlay_3ptStance_quick_LookAround__P01`, `FATIGUED_STANCE_P_QB_shotgun_looping__P01`, `FATIGUED_STANCE_P_WR_2pt_armsDN_looping__P01` | root still, head 1.32 m crouch, 0.68 to 0.74 m in the three point stance |
| Run | `MG_Loco_Run_Average__P01`, `MG_Loco_Sprint_Normal__P01`, `MG_Loco_Standard_Sprint_Burn__P01`, `Loco_NonBC_15_Jog_Loop__P01`, `MG_Loco_QB_Run_ShortStride__P01` | 7.06, 9.61, 11.20, 2.23 and 7.31 m/s |
| Cut | `BC_JUKE_AGILE_LEFT__P01`, `BC_JUKE_AGILE_RIGHT__P01`, `BC_Agile_Juke_Left_Precision_v01__P01`, `BC_Cut_Test_D1_to_D7__P01` | 3.9 to 5.5 m/s, head dips to 1.08 m in the jukes |
| Tackle | `2man_Catch_Def_In_Over_ShoveTackle__P01` and `__P02` (both players), `2man_Catch_Def_In_Under_SwatTackle_v6__P01`, `2man_Catch_Jump_Def_In_Under_JumpCatchTackle_v5__P01` | head down to 0.18 m when a player goes to ground |
| Throw | `MADNMAY04_0022_2._QB_deepPass_D1_standardRelease__P01`, `MADNMAY04_0041_2_QB_backFootShortPass_D1_standard__P01`, `MADNMAY04_0093_QB_rollout_R_shortPass_D7_standard__P01` | 0.8 to 3.2 m/s, head peaks 1.6 to 1.76 m |
| Catch | `Catch_Stand_0_Waist_RunFwd_16f_V1__P01`, `2man_BS_Catch_Under_GoodTiming_v1__P01`, `EffortCatch_AboveHead_Overthrow__P01` | head up to 1.93 m in the above head catch |
| Celebrate | `2014_PC_Generic_SpikeBall__P01`, `2012_NCAA_Celebration_MidTensity_V7__P01`, `2014_PI_Celebration_Pointtohelmet_Var1__P01` | head up to 2.15 m in the jump |

- Unreal assets in `/Game/MaddenRef/Animations/Prototype`. Preview level `/Game/MaddenRef/Maps/L_MaddenRef_AnimPrototype` has the dressed Stage 2 player in each of the 26 clips on a grid.
- **Sizes measured.** About 42 KB of ASCII FBX and 9.7 KB of Unreal asset per frame with every bone keyed.
- **Sparse keying for Checkpoint E.** `fbxanim.py --sparse` keys only the bones a clip drives (about 70, body plus fingers). The rest keep the EBX bind in their Lcl values, which is also the reference pose Unreal falls back to. On the three Checkpoint C clips the FBX shrinks 3.4 times and Unreal gives identical results (head height, root travel and length, 0 errors, 0 warnings). Unreal still bakes a track for every bone, so Unreal assets stay at about 10 KB per frame.
- **Scope of E.** 12,187 player body clips with 2,376,939 frames. That is about 30 GB of sparse FBX on D and about 23 GB of Unreal assets on C. About 400 clips use other rigs (camera, props, quadruped, sideline) and are out of scope for MaddenFB_Hero_Skeleton.
- **Extra safety floor on C.** C is 95 percent full and holds the Unreal project, so each animation chunk also checks for 40 GB free on C after its projected Unreal size, on top of the 50 GB floor on D. A per chunk quick verify hashes that chunk's files and size checks the rest, with a full verify at the end.

## Checkpoint E. Full clip list in chunks. PASS
- **Scope.** Every player body clip on MaddenFB_Hero_Skeleton. That is 12,187 controller names (body channel maps `6a6eb813061beab6` and `e5b9c7b4b01de68d`), each merged with its finger layer. About 400 clips on other rigs (camera, props, quadruped, sideline actors) are out of scope.
- **Chunks.** 83 kits `tools\madden\kits\anim_full_000.json` to `anim_full_082.json`, cut at 30,000 frames each. Three chunks with more than 290 clips (71, 75 and 77) were split in half into 80, 81 and 82, because Unreal import time grows with clip count. `tools\madden\anim_driver.py` runs pending chunks one 10 minute window at a time and stops everything on an install guard change. Wall time about 6 hours 15 minutes (09:21 to 15:36), under the 12 hour cap.
- **Result.**
  - 12,187 clips decoded, 12,182 passed the gate and are in `/Game/MaddenRef/Animations/Full/C000` to `C082` (2,372,513 frames).
  - Every chunk's final Unreal log ends `Success - 0 error(s), 0 warning(s)`.
  - The full verify hashed all 27,521 manifest files with no mismatch.
  - Install guard unchanged, audit passes (13,495 packages).
- **Held out, with reasons in `failures.csv`.**
  1. `Pose_Accel_Anim__P01`, `Pose_Fatigue_Anim__P01`, `Pose_Fatigue_Idle_Anim__P01` and `Torso_Twist_Left_4_HeadTracking__P01` are the only four additive body clips. They store deltas for layering (fatigue posture, acceleration lean, head tracking twist), so played as absolute poses they collapse to the ground. Two of them had passed the gate and been imported before the additive rule existed. They were removed (FBX, metadata, manifest entries and Unreal assets) and the chunk was rerun.
  2. `PostPlay_Group_TD_Celeb_Vikings_Drive_the_Car_1man5__P02` has one bad key in the source data. At frame 406 of 634 `Hips.t` jumps about 5 m and returns on the next key, inside one 8 key VBR block whose other keys decode cleanly. A decoder fault in that block would smear over all 8 keys, so this reads as a capture pop in the data. It was left unaltered and held out.
- **Real fixes made during E.**
  1. **Gate too strict for players lying down.** Injury clips keep the head at 0.16 m all clip long. The "head never rises" limit went from 0.2 m to 0.05 m.
  2. **Frame codec layers.** Some finger layers are `FrameAnimationAsset` (FRAM), one stored pose. 323 are empty placeholders and 40 hold a pose. Layout is QuatCount x 4 floats, then Vec3Count x 4 floats, then the float block padded to whole groups of 4 (quaternion norms within 2.6e-7 of 1). Empty layers now add nothing.
  3. **Additive clips** are held out (above).
  4. **NTSC rates.** Eight clips run at 29.97 or 59.94 fps. Unreal bakes at integer rates and refused their lengths, so they are written at 30 or 60 fps (duration changes by 0.1 percent, source rate kept in each clip's JSON).
  5. **Path length.** One clip name made a 126 character Unreal asset name that broke the Windows path limit. Unreal names over 120 characters are now shortened to 100 characters plus 8 hex of a hash of the full name.
- **Sizes.** Sparse FBX `extracted\animation\full` 25 GB on D (426 GB free afterwards). Unreal animations 20 GB on C (70 GB free afterwards, above the 40 GB floor this job set for C).
- **What is not done.** No retargeting to the UEFN mannequin or MetaHumans. No root motion setup (root motion sits on `AITrajectory` and `Hips`, under `Reference`). No additive clips. No clips for non player rigs. No state machines, blend spaces or tags from the bank (they decode but were not exported).

## Preview levels. In place playback and staging (2026-11-07)
- **Problem.** Clips carry real root travel (the run moves 11.3 m in 1.7 s), so preview actors ran off and snapped back every loop.
- **In place copies, preview only.** `fbxanim.make_in_place` writes a copy of each clip where every frame's world transform on `AITrajectory` (whose parent `Reference` is the identity) removes the Hips' horizontal position and turns the clip so frame 0 faces the mesh forward axis. Vertical motion, every rotation and every other joint are unchanged. Checked on idle, run and tackle: Hips horizontal position within 4e-16 m of the origin on every frame, head height unchanged, joint to Hips distances unchanged within 2e-15 m.
  - Copies are in `extracted\animation\preview_inplace\<kit>\` (manifest stage `<kit>_preview`) and `/Game/MaddenRef/Animations/PreviewInPlace/AnimCheckpoint` (3) and `/AnimPrototype` (26), named `A_<clip>_InPlace`.
  - The real sequences in `Animations/Checkpoint`, `Animations/Prototype` and `Animations/Full` were not touched (no file newer than before the change).
- **Staging (all nine L_MaddenRef levels).**
  - World Settings GameMode override is `GameModeBase`. The saved levels had no override before (their World Settings were empty, so the project default `GridironGameMode` would have run). Any override set in an open editor session was unsaved.
  - Lights. `SkyAtmosphere`, a movable `DirectionalLight` (atmosphere sun, pitch -40 degrees from the camera side), and a movable `SkyLight` with real time capture. No lighting build is needed.
  - Ground. `/Engine/BasicShapes/Plane` with `WorldGridMaterial`, sized to the actors plus a margin, at z = 0. The grid makes foot sliding easy to see.
  - Camera. A `CameraActor` (90 degree horizontal field of view, 16:9) set to auto activate for player 0, and a `PlayerStart` at the same spot. It looks at the row of subjects nearest to it, three quarters onto their front, at distance = subject height x 3 / (2 x tan(vertical half FOV)). That makes the subject fill a third of the view height, 5.06 m for a 189.7 cm dressed player (heads 1.14 m, helmets 0.82 m). Press Play to see that view. In the editor viewport, pilot `PreviewCamera`.
- **Level rebuild fix.** Rebuilding a level by deleting it and calling `new_level` on the same path failed silently inside one commandlet session (the deleted package stays in memory), so the first attempt left the old levels on disk. `maddenref_materials.py` now loads an existing level, destroys its actors, rebuilds, checks that the current world is that level and that the save succeeded. A probe of all nine saved levels confirms the override, camera, ground, lights and in place animations.
- Anim kits run the preview steps with `madden.py kit` (in place export, import, level). `tools\madden\kits\anim_checkpoint.json` is a preview only kit (`skip_main`) for the checkpoint level.
