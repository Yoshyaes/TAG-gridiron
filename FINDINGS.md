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
