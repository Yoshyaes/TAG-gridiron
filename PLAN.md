# Madden 19 reference extraction plan (approved 2026-11-07)

## Context
Fred wants reference assets from Madden NFL 19 (skeletons, player meshes, textures, animations, then coaches and stadiums) pulled into Unreal for the TAG Gridiron project. One head export was proven by hand in Frosty 1.0.6.3. Step 0 is evaluate and plan. On approval this file becomes `PLAN.md` in the repo, the recon facts go into `FINDINGS.md`, and Stage 0b starts.

## Recon results (verified this session, read only)

### Machine
- UE 5.8.4 at `C:\Program Files\Epic Games\UE_5.8`. Project `Unreal\TAGGridiron` already enables PythonScriptPlugin, EditorScriptingUtilities, IKRig, MetaHuman. Interchange is on by engine default.
- No .NET (Core) SDK. .NET Framework 4.8 SDK and targeting pack are installed. VS 2019 and 2022 Build Tools are installed, with Roslyn `csc.exe` and MSBuild. So a modern C# net48 x64 build needs no new installs.
- Frosty binaries are x64 .NET Framework 4.8 (MADDEN19SDK.dll is 4.7). No Zone.Identifier block on the DLLs.
- Python 3.14 default, 3.11 to 3.13 also installed. Blender is not installed.
- Madden 19 lives at `D:\Games\Madden NFL 19` (83 GB), taken from `%LOCALAPPDATA%\Frosty\editor_config.json`.
- Disk. C has 108 GB free (95 percent full). D has 453 GB free.
- Repo is on master, clean, with remote `github.com/Yoshyaes/TAG-gridiron`. Root `tools\` holds TypeScript sim tools. `Assets/` and `Content/TAG/` are already gitignored.

### Install integrity (known issue 4)
- `D:\Games\Madden NFL 19\ModData\Editor\` exists, dated 2026-11-07 00:07. It has 129 files (1.4 MB) and a `mods.json` naming `EditorMod_4087.fbmod`, which came from the Frosty Launch.
- `CryptBase.dll` was copied into the game root. Its hash matches `FrostyEditor\ThirdParty\CryptBase.dll`. This is the Frosty mod loader hook, and it makes the game read ModData on its next start.
- No other file outside ModData changed after 2026-11-06.
- The install also contains `codex.cfg` and `OrangeEmu64.dll`, which a retail EA or Origin install does not include. The licensing note assumes a legitimately owned copy, so Fred should confirm where this copy came from.

### Frosty and data
- Superbundles really exist. `playercontent_sb`, `playercontentlaunch_sb`, `coachcontent_sb`, `stadiums_sb` and also `refereecontent_sb`, `propcontent_sb` and `longshotsb`. Whether the per-player assets sit inside them is still unconfirmed.
- Scale root cause, from the v1.0.6.3 source of `MeshSetPlugin/FrostyMeshSetEditor.cs` (`FBXExporter.ExportFBX`). The Scale option only sets the FBX system unit. It never rescales vertices or bones, and Frostbite data is in meters. The hand export says cm (UnitScaleFactor 1.0, confirmed in the file) but holds meter values, hence 0.28 cm. Fix at export time with `units = "Meters"`. That gives correct metadata for UE and Blender alike, with no per-import setting to forget.
- Skinning. Bones are written only when a skeleton EBX path is passed. Nothing is picked automatically. For Madden 19 the exporter ignores per-section bone lists and maps vertex bone indices straight onto the chosen skeleton's bone order. A wrong skeleton gives garbage weights, and an index past the bone count throws. Skeletons can be listed with `am.EnumerateEbx("SkeletonAsset")`.
- Textures. `TexturePlugin/TextureExporter.cs` writes DDS in managed code (BC5 as DX10 BC5_UNORM, NormalDXN as ATI2). PNG and TGA go through native `dxtex.dll`. Normal maps are two channel. Z must be rebuilt as sqrt(1 - x² - y²). The green convention is untested.
- Animation. Frosty 1.0.6.3 has no animation decode or export (only a developer preview stub). FrostySdk has an `AntResource` type, and the Madden 19 profile includes the full ANT type schema. Madden uses EA ANT AssetBanks. The only mature public decoder is the FIFA 17 edition FrostyAnimationEditor (open source, FBX and BVH out, VBR codec). Nothing public covers Madden 19, 20 or FIFA 18 to 20. **Animation export is unproven and probably needs custom decoding.**
- The project's own `docs\ANIMATION_SPEC.md:3-6` says nothing was extracted from any game and that the game uses freely licensed assets. Extracted material must therefore never reach a build or the git history (see decision 1 for the guardrails).

## Headless approach (pick)
- **Pick. A small C# console host, `FrostyHost.exe`, net48 x64**, built with VS 2022 Build Tools Roslyn csc. It references the shipped Frosty DLLs in place and runs with the Frosty folder as its current directory. No Frosty rebuild.
  - Init order taken from `FrostyEditor/Windows/SplashWindow.xaml.cs`. First an AssemblyResolve hook (`EbxClasses` resolves to `Profiles\MADDEN19SDK.dll`, plus ThirdParty SharpDX and Newtonsoft). Then `ProfilesLibrary.Initialize("Madden19")`, then `FileSystem` with the sources Patch, Update and Data, then `ResourceManager`, then `TypeLibrary.Initialize`, then `AssetManager.Initialize`. Last, set the statics `Frosty.Core.App.AssetManager`, `FileSystem`, `ResourceManager` and `Logger`.
  - Madden 19 needs no key file. The cache is written to `Caches\` under the Frosty folder, never into the game folder. The existing `madden19.cache` is reused.
  - It calls `FBXExporter.ExportFBX` and `TextureExporter.Export` directly. It skips `AssetDefinition.Export`, which opens a dialog and writes Config. It passes an uninitialized `FrostyTaskWindow` as a stub, which is unverified and is the first thing Stage 0b tests.
  - It writes its own logger. FrostyCmd's logger throws on warnings.
- Fallback 1 is a Frosty menu extension plugin (`RegisterMenuExtension`) that runs the same batch code inside the GUI. Fallback 2 is GUI automation, only as a last resort.
- Rejected options. PowerShell hosting fails because `GetEntryAssembly` is null and delegates break on worker threads. Building Frosty from source needs toolsets that are missing and gains nothing.

## Layout and safety
- `tools\madden\`. `FrostyHost\` (C# source plus `build.ps1`), `madden.py` (the Python 3.11+ orchestrator, which runs the host and writes manifests), `install_guard.py`, and `jobs\*.json` (asset lists per stage).
- `extracted\`, a junction to `D:\MaddenRef\extracted\`. Raw outputs only, with `manifest.json`, `failures.csv` and the licensing `README.md`. Gitignored.
- `logs\`, `PLAN.md`, `FINDINGS.md` at the repo root.
- **Install guard.** Before and after every run, snapshot the path, size and mtime of every install file, plus SHA256 of the exe, `.toc`, `.sb` and `.cat` files. Any difference aborts the run and is reported. The host opens the game read only and never calls mod or launch code.
- **Idempotent.** Every job is keyed by asset path and export settings. A job is skipped when its outputs exist and their hashes match the manifest. Failures are appended to `failures.csv` (asset, stage, error, timestamp) and the run continues.
- Work on branch `madden-extraction`, small commits, never push without Fred's go. Only tooling and docs get committed, never anything extracted.

## Stages (each ends at a gate with a short report)

### Stage 0b. Host bring up (about 0.5 day)
Record, then remove, ModData and CryptBase.dll (decision 2), then take the install baseline. Add the gitignore, NeverCook and audit guardrails (decision 1). Build FrostyHost, init headless, export `faulkmarshall_11413_mesh` twice (Centimeters and Meters) and the `_color` texture. Diff against `Test\` (node names, vertex and index counts, bounding box, UnitScaleFactor, PNG pixel hash). Write `PLAN.md`, `FINDINGS.md`, `.gitignore` entries and `extracted\README.md`. Run the install guard.

### Stage 1. Inventory and risk probe (about 1 day)
Write `inventory.csv` with type, path, bundles, superbundle and res or chunk size for every EBX, plus `inventory-summary.md` with counts by type and top level path. Find the naming patterns for heads, bodies, uniforms, helmets, gear, skeletons, coaches and stadiums. **Also list every SkeletonAsset and every Ant, AssetBank or antstate asset with sizes**, so the animation risk is visible at the first gate.

### Stage 2. Skeleton and one complete player (1 to 2 days)
Pick the skeleton by checking each candidate's bone count against the maximum vertex bone index in the meshes, then confirm by bone names. Follow ObjectBlueprint and EBX references from `faulkMarshall_11413` to its body, uniform, helmet and gear. Export with the skeleton in Meters and import into `/Game/MaddenRef/` in TAGGridiron as Skeletal Meshes on one Skeleton. Check textures, rig, scale (about 1.9 m tall) and orientation.

### Stage 3. Animation feasibility (hard 2 hour box, defaults to no go)
Locate the ANT AssetBank behind the player animations and dump its header and clip codec tags. Try idle, run and tackle onto the Stage 2 skeleton using whatever already decodes. If they are not clean inside 2 hours, record the evidence and take the cut list. No decoder port. The morning report lists the options for Fred with effort.
- Port the FIFA 17 edition FrostyAnimationEditor decoder. 2 to 6 weeks, uncertain.
- Ask the Madden Modding Community for their closed tools. Unknown effort.
- Use GASP plus the procedural layers already in `ANIMATION_SPEC.md`. Close to zero new effort.

### Stage 4. Prototype set batch (1 to 2 days)
One skeleton, three body types, two helmets, matching uniform pieces, and about 25 animations if Stage 3 is a go. Driven by `jobs\prototype.json`. Output `manifest.json` (file, source asset path, bundle, textures, material parameters, hash).

### Stage 5. Textures and materials (2 to 3 days)
Export textures as DDS (lossless) plus PNG. Rebuild normal map Z in one place in Python. Verify green orientation against the hand export render. Dump material parameters from the MeshVariationDb and shader EBX into per mesh JSON. Build reference master materials and instances that read that JSON.

### Stage 6. UE import automation (2 to 3 days)
`maddenref_import.py`, run through `UnrealEditor-Cmd -run=pythonscript`. Reuse the `AssetImportTask` pattern in `Unreal\TAGGridiron\Tools\Art\ue_build_materials.py` (`import_tex`) and the retargeter setup in `mh_retarget.py`. Name assets by source asset path, not by FBX material node. Create Skeleton, Skeletal Meshes, material instances and animations, all under `/Game/MaddenRef/` only.

### Stage 7. Capped scale out (chunked)
All body types, all helmets, a sample of 50 heads, a sample of coaches, then stadium pieces only if everything else passed. Report counts, failures and projected size per chunk, and skip any chunk that would break the 50 GB floor on D.

## Cut list if animations are blocked
Drop animation extraction completely. Keep skeleton, meshes and textures as visual and proportion reference. Animation follows `ANIMATION_SPEC.md` (GASP plus procedural). Stage 4 shrinks to meshes only.

## Risks
1. Animation decode (high). Time boxed in Stage 3.
2. The headless host hits a GUI dependency (medium). The plugin fallback is ready.
3. Wrong skeleton gives bad weights silently (medium). Check bone index bounds and render a pose in UE.
4. The shipped MADDEN19SDK.dll may not match this install's build (low, since the editor already loads it).
5. Disk (low). Extracts on D with a 50 GB floor. Only tooling and logs on C.
6. Reference assets leaking into a build or the GitHub repo (high impact, since they share the TAGGridiron project). Mitigated by the gitignore, NeverCook and audit guardrails.

## Gate tests (every stage)
Run twice and confirm the second run skips everything. Confirm `manifest.json` counts match the disk. Spot check five random exports in UE. Give every row in `failures.csv` a reason. Install guard shows no change.

## Known issues (listed, not fixed now)
1. Export with no skeleton gives Static Meshes (confirmed cause. No skeleton path was passed).
2. FBX nodes are named by material. The naming rule uses the source asset path instead.
3. 100x scale (confirmed cause. Unit label only). The fix is Meters at export.
4. ModData and CryptBase.dll in the install from the earlier Launch. Fred authorized a one time removal in Stage 0b.

## Decisions from Fred (2026-11-07)
1. **UE target is inside TAGGridiron.** All reference imports go to `/Game/MaddenRef/` (on disk `Unreal\TAGGridiron\Content\MaddenRef\`). Guardrails, set up in Stage 0b and checked at every gate.
   - `.gitignore` gets `Unreal/TAGGridiron/Content/MaddenRef/` and `extracted/`.
   - `Config\DefaultGame.ini` gets `+DirectoriesToNeverCook=(Path="/Game/MaddenRef")` under `[/Script/UnrealEd.ProjectPackagingSettings]`, so nothing in MaddenRef cooks into a build.
   - New `Unreal\TAGGridiron\Tools\Art\maddenref_audit.py` runs through `UnrealEditor-Cmd -run=pythonscript`. It fails if any asset outside `/Game/MaddenRef` references one inside it, and if any file under MaddenRef is tracked by git. It runs at every gate.
   - `Athlete.cpp` and the other runtime code never hardcode a MaddenRef path.
   - FINDINGS.md records that this choice is in tension with the clean-room statement in `ANIMATION_SPEC.md:3-6`. The spec stays true only while the guardrails hold.
2. **ModData cleanup is authorized once.** At the start of Stage 0b I record the path, size and SHA256 of every file in `D:\Games\Madden NFL 19\ModData\` and of the root `CryptBase.dll` in FINDINGS.md. Then I delete exactly those two items and take the clean baseline snapshot for the install guard. No other write to the install, ever.
3. **Extracts live on D at `D:\MaddenRef\extracted\`**. The repo's `extracted\` is a gitignored directory junction (`mklink /J`) to it. The free space floor is 50 GB on D. Before each chunk the orchestrator writes the projected size to the log and MORNING-REPORT.md and skips any chunk that would break the floor.
4. **Stage 7 is capped.** All body types, all helmets, a sample of 50 heads and a sample of coaches. Stadium pieces only if everything else passed.
5. **Stage 3 defaults to no go.** A hard 2 hour box. If idle, run and tackle do not decode cleanly onto the Stage 2 skeleton in that time, take the cut list and move on. Never start a decoder port.
6. **Open item.** The install contains `codex.cfg` and `OrangeEmu64.dll` and the provenance of this copy is unconfirmed. It goes in FINDINGS.md and MORNING-REPORT.md as an open item. Those two files are never copied anywhere and never enter git. The inventory and export tooling excludes them explicitly.

## Overnight run rules (Fred asleep, Frosty and UE closed)
- A passed gate goes straight on to the next stage with no wait.
- A failed gate gets up to three real fixes. If it still fails, stop that stage, log it in FINDINGS.md and continue with any stage that does not depend on it. Dependencies are 0b before everything, 1 before 2, 2 before 3, 4 and 5, 4 and 5 before 6, 6 before 7. Stage 3 failing only removes animations from 4 and 6.
- Any install guard change stops everything. Write the report and end the run.
- Never push. Never write to the install except the two authorized removals. Never launch or apply mods.
- Small commits on `madden-extraction`, tooling and docs only.
- Every command gets a timeout of 10 minutes or less. Long batches run in chunks that each finish inside that limit. No polling or waiting beyond 10 minutes.
- Unreal runs headless only, through `UnrealEditor-Cmd`.
- The five random UE spot checks happen headless. `maddenref_import.py` writes per asset bounds, bone count, material slots, UV channel count and texture bindings to a JSON report, and a headless render of a thumbnail per checked asset when the commandlet allows it. Visual sign off is left for Fred in the morning.

## Morning report
`MORNING-REPORT.md` at the repo root is updated after every gate and on any stop. It stays under one page and covers stages passed and failed, what is on disk with sizes, what is in Unreal under `/Game/MaddenRef/`, the open items (including provenance), and exactly what Fred needs to do next.

## Files created or changed (all phases)
- New. `PLAN.md`, `FINDINGS.md`, `MORNING-REPORT.md`, junction `extracted\` to `D:\MaddenRef\extracted\`, `tools\madden\FrostyHost\*.cs`, `tools\madden\FrostyHost\build.ps1`, `tools\madden\madden.py`, `tools\madden\install_guard.py`, `tools\madden\jobs\*.json`, `extracted\README.md` (written by the script, ignored), `Unreal\TAGGridiron\Tools\Art\maddenref_import.py`, `Unreal\TAGGridiron\Tools\Art\maddenref_audit.py`.
- Changed. `.gitignore`, `Unreal\TAGGridiron\Config\DefaultGame.ini`.
- Reused patterns. `import_tex()` in `Unreal\TAGGridiron\Tools\Art\ue_build_materials.py`, IK Rig and retargeter setup in `Unreal\TAGGridiron\Tools\Art\mh_retarget.py`, headless launch line in `Unreal\TAGGridiron\Tools\setup_assets.bat`.

## Verification
- Stage 0b passes when FrostyHost exports the test head headless, the Meters FBX imports into UE at about 28 x 25 x 41 cm with no offset scale, vertex counts match the hand export, and the install guard shows no change after the run.
- Every gate runs the gate tests above plus `maddenref_audit.py`, and `git status` shows nothing under `extracted\` or `Content\MaddenRef\`.
