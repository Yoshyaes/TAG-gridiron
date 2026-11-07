# Madden 19 reference pipeline tooling

Private reference tooling for TAG Gridiron. It reads Madden NFL 19 through the Frosty 1.0.6.3 SDK and lands reference assets in Unreal under `/Game/MaddenRef`. Nothing it produces ships in the game. See `extracted\README.md` for the licensing note, `PLAN.md` for stages and `FINDINGS.md` for evidence.

## Requirements
- Frosty Editor 1.0.6.3 at `%USERPROFILE%\Downloads\FrostyEditor\FrostyEditor` (override with `FROSTY_DIR`), with its `Caches\madden19.cache` already built by the editor.
- Madden NFL 19 at `D:\Games\Madden NFL 19` (override with `MADDEN_DIR`). Read only.
- VS 2022 Build Tools (Roslyn csc) and the .NET Framework 4.8 targeting pack, for `FrostyHost`.
- Python 3.11 or newer for the orchestrator, Python 3.13 with Pillow and numpy for `texfix.py` and `texanalyze.py`.
- Unreal Engine 5.8 at `C:\Program Files\Epic Games\UE_5.8`. Unreal always runs headless through `UnrealEditor-Cmd`.

## One time setup
```
powershell -File tools\madden\FrostyHost\build.ps1
python -I tools\madden\madden.py init
python -I tools\madden\install_guard.py snapshot logs\guard_baseline.json
python -I tools\madden\madden.py inventory
```

## Everyday use
```
python -I tools\madden\madden.py kit tools\madden\kits\prototype.json
```
A kit run is plan, export, normal map conversion, material JSON, Unreal import, material instances and preview level, manifest verify, install guard, MaddenRef audit. Every step skips finished work. Exit code 3 means the install guard saw a change, so stop everything. Exit code 4 means the kit was skipped because it would break the 50 GB free space floor on D.

## Files
| Path | Role |
| --- | --- |
| `FrostyHost\Program.cs` | Headless Frosty host. `inventory` and `run <jobs> <results>` (ops `mesh`, `texture`, `ebx`, `meshinfo`, `deps`, `raw`) |
| `madden.py` | Orchestrator, manifest, failures, guard, disk floor, Unreal runs, kit runner |
| `kit.py` | Turns a kit spec into job files |
| `kits\*.json` | Kit specs |
| `texfix.py` | The one place normal maps are converted for Unreal |
| `texanalyze.py` | Normal map and RSM convention analysis |
| `fbxinfo.py` | Binary FBX inspector for checks |
| `install_guard.py` | Install snapshot and compare |
| `inventory_summary.py` | Inventory summary and families |
| `Unreal\TAGGridiron\Tools\Art\maddenref_import.py` | Headless Interchange import |
| `Unreal\TAGGridiron\Tools\Art\maddenref_materials.py` | Master material, instances, preview level |
| `Unreal\TAGGridiron\Tools\Art\maddenref_audit.py` | Guardrail audit |

## Conventions
- **Scale** is fixed at export. FBX is written in meters (UnitScaleFactor 100). No import offset scale anywhere.
- **Normal maps** are OpenGL style in Madden. `texfix.py` inverts green and rebuilds Z, and Unreal imports only from `textures_ue`.
- **Naming in Unreal.** `SK_<madden mesh leaf without _mesh>` for skeletal meshes, `SM_` for static meshes, `T_<madden texture leaf in lower case>`, `MI_<mesh>[_<section>]`, `M_MaddenRef_Ref` master, `L_MaddenRef_<Kit>` preview levels.
- **Content layout.**
  - `/Game/MaddenRef/Skeleton` shared skeleton (`MaddenFB_Hero_Skeleton`, created by a bootstrap import named `MaddenFB_Hero`)
  - `/Game/MaddenRef/Players/<Kit>` meshes, with `Textures` and `Materials` beneath
  - `/Game/MaddenRef/Coaches/<Kit>` and `/Game/MaddenRef/Stadiums/<Kit>` for later kits
  - `/Game/MaddenRef/Materials` master materials
  - `/Game/MaddenRef/Maps` preview levels
  - `/Game/MaddenRef/_Pipelines` Interchange pipeline assets
- **Guardrails.** `extracted\` and `Content\MaddenRef\` are gitignored, `/Game/MaddenRef` is in `DirectoriesToNeverCook`, and `maddenref_audit.py` fails if anything outside MaddenRef references it or if any of it is tracked by git.
