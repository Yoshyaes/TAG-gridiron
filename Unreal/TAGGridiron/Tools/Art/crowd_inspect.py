# Unreal Editor Python (headless, read only). Inspects the MetaHuman Crowd Sample plugin content to plan the
# stadium hero crowd: actor blueprints, their components and editable properties, MetaHuman instance assets.
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_inspect.json")
ROOT = "/MetaHumanCrowdContent"
rep = {"assets": {}, "blueprints": {}, "errors": []}


def props(obj, limit=80):
    out = {}
    for name in sorted(dir(obj))[:2000]:
        if name.startswith("_") or name in ("static_class", "get_class"):
            continue
        try:
            v = obj.get_editor_property(name)
        except Exception:
            continue
        s = str(v)
        out[name] = s[:160]
        if len(out) >= limit:
            break
    return out


try:
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.scan_paths_synchronous([ROOT], True)   # commandlets do not scan plugin content on their own
    for folder in ("Actor_BPs", "StarterKit_MigrateMe/Blueprint", "Instances", "Animations", "AI", "Characters/Assembled"):
        assets = ar.get_assets_by_path(ROOT + "/" + folder, recursive=True)
        classes = {}
        for a in assets:
            c = str(a.asset_class_path.asset_name)
            classes[c] = classes.get(c, 0) + 1
        rep["assets"][folder] = {"count": len(assets), "classes": classes,
                                 "sample": [str(a.package_name) for a in assets[:6]]}
    sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    for bp_path in (ROOT + "/StarterKit_MigrateMe/Blueprint/BP_StarterKitActor", ROOT + "/Actor_BPs/BP_MassHighRes",
                    ROOT + "/Actor_BPs/BP_MassHighRes_Unique"):
        bp = unreal.load_asset(bp_path)
        if not bp:
            rep["errors"].append("missing " + bp_path)
            continue
        info = {}
        gen = unreal.BlueprintEditorLibrary.generated_class(bp)
        cdo = unreal.get_default_object(gen)
        info["class"] = str(gen.get_name())
        info["cdo_props"] = props(cdo)
        comps = []
        try:
            handles = sub.k2_gather_subobject_data_for_blueprint(bp)
            for h in handles:
                data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
                obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
                comps.append({"name": str(unreal.SubobjectDataBlueprintFunctionLibrary.get_display_name(data)),
                              "class": obj.get_class().get_name() if obj else None,
                              "props": props(obj, 40) if obj and obj.get_class().get_name() != info["class"] else {}})
        except Exception as e:
            comps.append({"error": str(e)})
        info["components"] = comps
        rep["blueprints"][bp_path] = info
    asm = [a for a in ar.get_assets_by_path(ROOT + "/Characters/Assembled", recursive=True)
           if str(a.asset_class_path.asset_name) in ("Blueprint", "SkeletalMesh", "AnimBlueprint", "Skeleton")]
    rep["assembled"] = sorted("%s %s" % (a.asset_class_path.asset_name, a.package_name) for a in asm)
    bps = [a for a in asm if str(a.asset_class_path.asset_name) == "Blueprint"][:2]
    for a in bps:
        bp = unreal.load_asset(str(a.package_name))
        gen = unreal.BlueprintEditorLibrary.generated_class(bp)
        info = {"class": gen.get_name(), "parent": str(unreal.get_default_object(gen).get_class().get_super_class().get_name()) if hasattr(unreal.get_default_object(gen).get_class(), "get_super_class") else None, "components": []}
        try:
            for h in sub.k2_gather_subobject_data_for_blueprint(bp):
                data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
                obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
                if obj is None:
                    continue
                c = {"name": str(unreal.SubobjectDataBlueprintFunctionLibrary.get_display_name(data)), "class": obj.get_class().get_name()}
                if isinstance(obj, unreal.SkeletalMeshComponent):
                    m = obj.get_editor_property("skeletal_mesh_asset") if hasattr(obj, "get_editor_property") else None
                    c["mesh"] = m.get_path_name() if m else None
                    try:
                        c["anim_class"] = str(obj.get_editor_property("anim_class"))
                    except Exception:
                        pass
                info["components"].append(c)
        except Exception as e:
            info["components"].append({"error": str(e)})
        rep["blueprints"][str(a.package_name)] = info
    mhi = ar.get_assets_by_path(ROOT + "/Instances", recursive=True)
    if mhi:
        o = unreal.load_asset(str(mhi[0].package_name))
        rep["mhi_example"] = {"path": str(mhi[0].package_name), "class": o.get_class().get_name(), "props": props(o, 60)}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc())
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_CROWD_INSPECT done errors=%d" % len(rep["errors"]))
