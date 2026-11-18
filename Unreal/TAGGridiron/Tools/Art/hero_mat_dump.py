# Unreal Editor Python (headless). For every item in Content/TAG/Hero/hero_catalog.json: each material slot and the
# source file of every texture its instance samples. Output Saved/hero_mat_sources.json, read by
# Tools/Art/hero_placeholder_check.py (plain Python, PIL), which flags any slot still drawing one of the engine's
# placeholders (missing-texture rainbow, default_runtime_color checker, debug_texture_norm).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi
import json
import os

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
cat = json.load(open(os.path.join(PROJ, "Content", "TAG", "Hero", "hero_catalog.json")))
out = {}
for it in cat["items"]:
    m = unreal.load_asset(it["path"].split(".")[0])
    if not m:
        out[it["name"]] = {"_missing": True}
        continue
    slots = {}
    for sm in m.get_editor_property("materials"):
        mi = sm.get_editor_property("material_interface")
        srcs = []
        if isinstance(mi, unreal.MaterialInstanceConstant):
            for p in mi.get_editor_property("texture_parameter_values"):
                t = p.get_editor_property("parameter_value")
                aid = t.get_editor_property("asset_import_data") if t else None
                srcs.append([str(p.get_editor_property("parameter_info").name), aid.get_first_filename() if aid else ""])
        slots[str(sm.get_editor_property("material_slot_name"))] = {"mi": mi.get_path_name() if mi else None, "tex": srcs}
    out[it["name"]] = {"kind": it["kind"], "slots": slots}
json.dump(out, open(os.path.join(PROJ, "Saved", "hero_mat_sources.json"), "w"), indent=0)
unreal.log("TAG_MATDUMP %d items" % len(out))
