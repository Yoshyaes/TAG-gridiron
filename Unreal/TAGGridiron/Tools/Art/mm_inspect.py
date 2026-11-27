# Unreal Editor Python (headless, read only): the Game Animation Sample motion matching data our locomotion searches.
# Reports each Dense database's schema, animation count and index state, and each schema's channels.
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "mm_inspect.json")
ROOT = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData"
rep = {"databases": {}, "schemas": {}, "errors": []}


def props(o, n=300):
    out = {}
    for name in dir(o):
        if name.startswith("_"):
            continue
        try:
            v = o.get_editor_property(name)
        except Exception:
            continue
        if callable(v):
            continue
        out[name] = str(v)[:n]
    return out


try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous([ROOT], True)
    for a in reg.get_assets_by_path(ROOT + "/Databases/Dense", True):
        p = str(a.package_name)
        db = unreal.load_asset(p)
        info = {}
        try:
            sch = db.get_editor_property("schema")
            info["schema"] = sch.get_path_name() if sch else None
            if sch and sch.get_path_name() not in rep["schemas"]:
                s = props(sch)
                try:
                    s["channels"] = [{"class": c.get_class().get_name(), "props": props(c, 500)} for c in sch.get_editor_property("channels")]
                except Exception as e:
                    s["channels"] = "?" + str(e)[:200]
                rep["schemas"][sch.get_path_name()] = s
        except Exception as e:
            info["schema"] = "?" + str(e)[:200]
        try:
            info["anims"] = len(db.get_editor_property("animation_assets"))
        except Exception as e:
            info["anims"] = "?" + str(e)[:100]
        info["props"] = {k: v for k, v in props(db).items() if k not in ("animation_assets",)}
        rep["databases"][p.split("/")[-1]] = info
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_MM_INSPECT dbs=%d schemas=%d errors=%d" % (len(rep["databases"]), len(rep["schemas"]), len(rep["errors"])))
