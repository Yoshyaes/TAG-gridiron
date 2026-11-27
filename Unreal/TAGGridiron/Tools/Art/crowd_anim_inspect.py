# Unreal Editor Python (headless, read only): the MetaHuman Crowd Sample's baked-animation assets (transform providers,
# sequence lists) that distant instanced fans play, so we know where a cheer has to be added.
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_anim.json")
rep = {"providers": [], "lists": [], "configs": [], "errors": []}
try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/MetaHumanCrowdContent", "/MetaHumanCrowd"], True)
    for root in ("/MetaHumanCrowdContent", "/MetaHumanCrowd"):
        for a in reg.get_assets_by_path(root, True):
            cls = str(a.asset_class_path.asset_name)
            p = str(a.package_name)
            if cls == "AnimSequenceTransformProviderSequenceList":
                o = unreal.load_asset(p)
                info = {"path": p}
                for k in ("sequences",):
                    try:
                        info[k] = [str(s)[:200] for s in o.get_editor_property(k)]
                    except Exception as e:
                        info[k] = "?" + str(e)[:150]
                rep["lists"].append(info)
            elif cls == "AnimSequenceTransformProviderData":
                rep["providers"].append(p)
            elif cls in ("MetaHumanCrowdAnimationConfig",):
                o = unreal.load_asset(p)
                info = {"path": p}
                for k in dir(o):
                    if k.startswith("_"):
                        continue
                    try:
                        v = o.get_editor_property(k)
                    except Exception:
                        continue
                    if not callable(v):
                        info[k] = str(v)[:600]
                rep["configs"].append(info)
    rep["n_providers"] = len(rep["providers"])
    rep["providers"] = rep["providers"][:15]
    # one provider in detail
    for p in rep["providers"][:3]:
        o = unreal.load_asset(p)
        d = {"path": p}
        for k in ("sequence_list", "sequences", "skinned_asset", "layer_stack", "blend_space_list"):
            try:
                d[k] = str(o.get_editor_property(k))[:400]
            except Exception as e:
                d[k] = "?" + str(e)[:120]
        rep.setdefault("detail", []).append(d)
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_CROWD_ANIM providers=%s lists=%d configs=%d" % (rep.get("n_providers"), len(rep["lists"]), len(rep["configs"])))
