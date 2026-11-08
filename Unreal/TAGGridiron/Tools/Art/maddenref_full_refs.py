# Unreal Editor Python (headless, read only). Confirms nothing in the project references
# /Game/MaddenRef/Animations/Full before that folder is removed from TAGGridiron.
#   1. asset registry. No package outside Full has a hard or soft dependency on a package inside it
#   2. text. No file under Source, Config, Tools or Plugins names Animations/Full
# Env MADDENREF_REPORT names the JSON report.
import json
import os

import unreal

FULL = "/Game/MaddenRef/Animations/Full"
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                             include_searchable_names=False, include_soft_management_references=True,
                                             include_hard_management_references=True)
pkgs = sorted({str(a.package_name) for a in ar.get_assets_by_path("/Game", recursive=True)})
into_full = []
for p in pkgs:
    if p.startswith(FULL + "/"):
        continue
    hits = [str(d) for d in (ar.get_dependencies(p, opts) or []) if str(d).startswith(FULL + "/")]
    if hits:
        into_full.append([p, hits[:5]])
text = []
for sub in ("Source", "Config", "Tools", "Plugins"):
    root = os.path.join(PROJ, sub)
    for d, dirs, files in os.walk(root, followlinks=False):
        for f in files:
            if f.endswith((".py", ".ini", ".cpp", ".h", ".cs", ".json", ".bat", ".txt", ".uplugin")):
                p = os.path.join(d, f)
                try:
                    if "Animations/Full" in open(p, encoding="utf-8", errors="ignore").read() and not f == "maddenref_full_refs.py":
                        text.append(os.path.relpath(p, PROJ))
                except OSError:
                    pass
out = {"project": PROJ, "packages": len(pkgs), "full_packages": sum(1 for p in pkgs if p.startswith(FULL + "/")),
       "outside_referencing_full": into_full, "text_references": text}
json.dump(out, open(os.environ["MADDENREF_REPORT"], "w"), indent=1)
