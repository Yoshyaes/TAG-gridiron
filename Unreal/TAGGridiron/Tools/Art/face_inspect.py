# Unreal Editor Python (headless, read only): what the TAG MetaHuman face meshes give us to animate faces with:
# post-process anim blueprint (RigLogic), skeleton and the expression control curves (blink, jaw, brows, mouth).
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "face_inspect.json")
rep = {"faces": {}, "errors": []}
try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/Game/TAG/MetaHumans"], True)
    for name in ("TAG_Skill", "TAG_Lineman"):
        p = "/Game/TAG/MetaHumans/Unpacked/%s/Face/SKM_%s_FaceMesh" % (name, name)
        m = unreal.load_asset(p)
        if not m:
            rep["errors"].append("missing " + p)
            continue
        d = {"path": p}
        for k in ("post_process_anim_blueprint", "skeleton"):
            try:
                v = m.get_editor_property(k)
                d[k] = v.get_path_name() if v else None
            except Exception as e:
                d[k] = "?" + str(e)[:150]
        try:
            sk = m.get_editor_property("skeleton")
            names = [str(n) for n in unreal.AnimationLibrary.get_animation_curve_names(None, unreal.RawCurveTrackTypes.RCT_FLOAT)] if False else []
            d["curves_note"] = "see skeleton"
        except Exception as e:
            d["curves_err"] = str(e)[:150]
        rep["faces"][name] = d
    # curve names on the face skeleton (via any face animation in the project, else the skeleton's curve metadata)
    sk = unreal.load_asset(rep["faces"]["TAG_Skill"]["skeleton"].split(".")[0]) if rep["faces"].get("TAG_Skill") else None
    if sk:
        try:
            names = [str(n) for n in sk.get_editor_property("curve_meta_data").keys()] if hasattr(sk, "get_editor_property") else []
        except Exception:
            names = []
        if not names:
            try:
                names = [str(n) for n in unreal.SkeletonLibrary.get_curve_names(sk)] if hasattr(unreal, "SkeletonLibrary") else []
            except Exception as e:
                rep["errors"].append("curves: " + str(e)[:200])
        rep["n_curves"] = len(names)
        rep["blink_like"] = [n for n in names if "lnk" in n.lower() or "blink" in n.lower()][:10]
        rep["jaw_like"] = [n for n in names if "jaw" in n.lower()][:10]
        rep["sample"] = names[:30]
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_FACE_INSPECT done")
