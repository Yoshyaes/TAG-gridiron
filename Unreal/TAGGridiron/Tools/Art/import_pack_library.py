# Unreal Editor Python (headless). Imports the retargeted mocap pack library (Tools/Anim/build_pack_library.sh:
# RawArt/Anim/Pack/<Category>/A_TAG_Pack_<name>.fbx + .json) onto SK_UEFN_Mannequin as /Game/TAG/Anim/Pack/<Category>/
# A_TAG_Pack_<name>, root motion on with the root locked (the same settings as the game's locomotion clips). Already
# imported assets are skipped (reruns resume); TAG_PACK_CATEGORIES=Tackles,Kicking limits a run, TAG_PACK_LIMIT=n
# caps it (one commandlet per few thousand clips keeps memory sane). Report: Saved/pack_import.json (appended per run).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import os
import shutil
import time

import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SKEL = "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin"
PIPE = "/Game/TAG/Anim/_Pipelines/P_TAG_Anim"  # the saved Interchange pipeline import_tag_anims.py builds


def pipeline(skeleton):
    pipe = unreal.load_asset(PIPE) if EAL.does_asset_exist(PIPE) else None
    if pipe is None:
        base = unreal.load_asset("/Interchange/Pipelines/DefaultAssetsPipeline")
        pkg, name = PIPE.rsplit("/", 1)
        pipe = tools.duplicate_asset(name, pkg, base)
    mesh_p = pipe.get_editor_property("mesh_pipeline")
    mesh_p.set_editor_property("import_static_meshes", False)
    mesh_p.set_editor_property("import_skeletal_meshes", False)
    mesh_p.set_editor_property("create_physics_asset", False)
    common = pipe.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", skeleton)
    common.set_editor_property("import_only_animations", True)
    anim_p = pipe.get_editor_property("animation_pipeline")
    anim_p.set_editor_property("import_animations", True)
    anim_p.set_editor_property("use30_hz_to_bake_bone_animation", False)
    anim_p.set_editor_property("custom_bone_animation_sample_rate", 30)
    pipe.get_editor_property("material_pipeline").set_editor_property("import_materials", False)
    EAL.save_loaded_asset(pipe, only_if_is_dirty=False)
    return PIPE + "." + PIPE.rsplit("/", 1)[1]

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "Pack")
DEST = "/Game/TAG/Anim/Pack"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "pack_import.json")
cats = [c for c in os.environ.get("TAG_PACK_CATEGORIES", "").split(",") if c]
limit = int(os.environ.get("TAG_PACK_LIMIT", "0") or 0)


def main():
    skeleton = unreal.load_asset(SKEL)
    pipe = pipeline(skeleton)
    tmp_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "TAGPackImport")
    os.makedirs(tmp_dir, exist_ok=True)
    out = {"imported": 0, "skipped": 0, "errors": [], "started": time.strftime("%Y-%m-%d %H:%M:%S")}
    mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
    t0 = time.time()
    n = 0
    for cat_dir in sorted(glob.glob(os.path.join(SRC, "*"))):
        cat = os.path.basename(cat_dir)
        if cats and cat not in cats:
            continue
        dest = DEST + "/" + cat
        have = set()
        if EAL.does_directory_exist(dest):
            have = {p.split(".")[0].rsplit("/", 1)[1] for p in EAL.list_assets(dest, recursive=False, include_folder=False)}
        for fbx in sorted(glob.glob(os.path.join(cat_dir, "A_TAG_Pack_*.fbx"))):
            name = os.path.splitext(os.path.basename(fbx))[0]
            if name in have:
                out["skipped"] += 1
                continue
            if limit and n >= limit:
                break
            n += 1
            try:
                tmp = os.path.join(tmp_dir, name + ".fbx")
                shutil.copyfile(fbx, tmp)
                params = unreal.ImportAssetParameters()
                params.is_automated = True
                params.replace_existing = True
                params.override_pipelines = [unreal.SoftObjectPath(pipe)]
                src = unreal.InterchangeManager.create_source_data(tmp)
                if not mgr.import_asset(dest, src, params):
                    raise RuntimeError("interchange import_asset returned false")
                path = dest + "/" + name
                anim = unreal.load_asset(path)
                if not isinstance(anim, unreal.AnimSequence):
                    hits = [p.split(".")[0] for p in EAL.list_assets(dest, recursive=False, include_folder=False)
                            if p.split(".")[0].rsplit("/", 1)[1].startswith(name)]
                    anims = [p for p in hits if isinstance(unreal.load_asset(p), unreal.AnimSequence)]
                    if not anims:
                        raise RuntimeError("no AnimSequence imported")
                    path = min(anims, key=len)
                    anim = unreal.load_asset(path)
                anim.set_editor_property("enable_root_motion", True)
                anim.set_editor_property("force_root_lock", True)
                if cat == "Additive":  # the pack's delta layers (Pose_Accel, Pose_Fatigue, head tracking)
                    anim.set_editor_property("additive_anim_type", unreal.AdditiveAnimationType.AAT_LOCAL_SPACE_BASE)
                    anim.set_editor_property("ref_pose_type", unreal.AdditiveBasePoseType.ABPT_REF_POSE)
                if not EAL.save_asset(path, only_if_is_dirty=False):
                    raise RuntimeError("save failed")
                os.remove(tmp)
                out["imported"] += 1
                if out["imported"] % 200 == 0:
                    unreal.log("TAG_PACK_IMPORT %d imported, %d skipped, %d errors, %.0f s" % (out["imported"], out["skipped"], len(out["errors"]), time.time() - t0))
                    unreal.SystemLibrary.collect_garbage()
            except Exception:
                import traceback
                out["errors"].append({"clip": name, "error": traceback.format_exc()[-600:]})
    out["seconds"] = round(time.time() - t0)
    prev = []
    if os.path.exists(REPORT):
        try:
            prev = json.load(open(REPORT))
            if isinstance(prev, dict):
                prev = [prev]
        except Exception:
            prev = []
    json.dump(prev + [out], open(REPORT, "w"), indent=1)
    unreal.log("TAG_PACK_IMPORT done imported %d skipped %d errors %d in %d s" % (out["imported"], out["skipped"], len(out["errors"]), out["seconds"]))
    for e in out["errors"][:5]:
        unreal.log("TAG_PACK_IMPORT error %s: %s" % (e["clip"], e["error"].replace("\n", " ")[-200:]))


main()
