# Unreal Editor Python (headless, read only): the MetaHuman Crowd Sample asset chain a distant-fan cheer has to go
# through: AnimConfig entries -> crowd pipeline -> collection -> character instances used by DA_TAG_FanConfig.
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_chain.json")
rep = {"errors": []}


def props(o, n=400):
    d = {}
    for k in dir(o):
        if k.startswith("_"):
            continue
        try:
            v = o.get_editor_property(k)
        except Exception:
            continue
        if not callable(v):
            d[k] = str(v)[:n]
    return d


try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/MetaHumanCrowdContent", "/Game/TAG/Crowd"], True)
    cfg = unreal.load_asset("/MetaHumanCrowdContent/Animations/AnimConfig")
    rep["anim_entries"] = []
    for e in cfg.get_editor_property("animations_to_bake"):
        d = props(e)
        for k in ("body_anim_sequence", "face_anim_sequence", "merged_anim_sequence"):
            try:
                s = e.get_editor_property(k)
                if s:
                    d[k + "_skeleton"] = s.get_editor_property("skeleton").get_path_name()
            except Exception as ex:
                d[k + "_err"] = str(ex)[:100]
        rep["anim_entries"].append(d)
    fan = unreal.load_asset("/Game/TAG/Crowd/DA_TAG_FanConfig")
    for t in fan.get_editor_property("config").get_editor_property("traits"):
        if "Visualization" in t.get_class().get_name():
            try:
                inst = t.get_editor_property("character_instances")
                rep["n_instances"] = len(inst)
                rep["instances"] = []
                for i in list(inst)[:3]:
                    if not i:
                        continue
                    d = {"path": i.get_path_name(), "class": i.get_class().get_name()}
                    d.update(props(i, 300))
                    rep["instances"].append(d)
            except Exception as ex:
                rep["inst_err"] = str(ex)[:300]
    # collections and their pipelines in the sample
    rep["collections"] = []
    for a in reg.get_assets_by_path("/MetaHumanCrowdContent", True):
        cls = str(a.asset_class_path.asset_name)
        if "Collection" in cls and "Anim" not in cls:
            o = unreal.load_asset(str(a.package_name))
            d = {"path": str(a.package_name), "class": cls}
            try:
                pl = o.get_editor_property("pipeline")
                d["pipeline"] = pl.get_class().get_name() if pl else None
                if pl:
                    try:
                        ep = pl.get_editor_pipeline()
                        d["editor_pipeline"] = ep.get_class().get_name() if ep else None
                        if ep:
                            d["anim_config"] = str(ep.get_editor_property("animation_config"))
                    except Exception as ex:
                        d["ep_err"] = str(ex)[:200]
            except Exception as ex:
                d["pl_err"] = str(ex)[:200]
            rep["collections"].append(d)
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_CROWD_CHAIN done")
