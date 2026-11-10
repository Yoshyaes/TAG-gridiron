# Unreal Editor Python (headless). Imports TAG's hand keyed clips (Tools/Anim, Blender) onto SK_UEFN_Mannequin and
# checks the Blender -> Unreal round trip against each clip's Blender measurements.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Source: RawArt/Anim/Clips/A_TAG_<clip>.fbx (+ .json). Output: /Game/TAG/Anim/Authored/A_TAG_<clip>.
# Report: Saved/tag_anim_import.json
import glob
import json
import os
import shutil

import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "Clips")
DEST = "/Game/TAG/Anim/Authored"
SKEL = "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin"
PIPE = "/Game/TAG/Anim/_Pipelines/P_TAG_Anim"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_anim_import.json")


def pipeline(skeleton):
    """Saved Interchange pipeline (AssetImportTask.options is ignored by Interchange in 5.8)."""
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


def bone_world_z(anim, bone, t):
    """Component space height (cm) of a bone at time t, via AnimPose."""
    opts = unreal.AnimPoseEvaluationOptions()
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
    tr = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
    return tr.translation.z


def main():
    skeleton = unreal.load_asset(SKEL)
    pipe = pipeline(skeleton)
    tmp_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "TAGAnimImport")
    os.makedirs(tmp_dir, exist_ok=True)
    out = {"clips": [], "errors": []}
    only = [n.strip() for n in os.environ.get("TAG_IMPORT_ONLY", "").split(",") if n.strip()]
    for fbx in sorted(glob.glob(os.path.join(SRC, "A_TAG_*.fbx"))):
        name = os.path.splitext(os.path.basename(fbx))[0]
        if only and name not in only and name[6:] not in only:
            continue
        try:
            tmp = os.path.join(tmp_dir, name + ".fbx")
            shutil.copyfile(fbx, tmp)
            params = unreal.ImportAssetParameters()
            params.is_automated = True
            params.replace_existing = True
            params.override_pipelines = [unreal.SoftObjectPath(pipe)]
            src = unreal.InterchangeManager.create_source_data(tmp)
            mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
            if not mgr.import_asset(DEST, src, params):
                raise RuntimeError("interchange import_asset returned false")
            hits = [p.split(".")[0] for p in EAL.list_assets(DEST, recursive=False, include_folder=False)
                    if p.split(".")[0].rsplit("/", 1)[1].startswith(name)]
            anims = [p for p in hits if isinstance(unreal.load_asset(p), unreal.AnimSequence)]
            if not anims:
                raise RuntimeError("no AnimSequence imported for " + name)
            path = min(anims, key=len)
            anim = unreal.load_asset(path)
            # root motion only for the locomotion clips built with mode 'root' (the trajectory on the root bone);
            # loops are flagged so PoseSearch treats them as cycles
            meta_path = os.path.splitext(fbx)[0] + ".json"
            meta0 = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
            root_motion = bool(meta0.get("source", {}).get("root_motion"))
            anim.set_editor_property("enable_root_motion", root_motion)
            # like the sample's loops: the root bone stays put in the pose (the travel is read from the root track), so
            # PoseSearch's pose features and the motion matched body do not drift with the root
            anim.set_editor_property("force_root_lock", root_motion)
            if meta0.get("spec", {}).get("loop"):
                try:
                    anim.set_editor_property("loop", True)
                except Exception:
                    pass
            if not EAL.save_asset(path, only_if_is_dirty=False):
                raise RuntimeError("save failed for %s (file locked, is the Unreal Editor open?)" % path)
            os.remove(tmp)
            rec = {"asset": path, "length_s": round(anim.get_play_length(), 3),
                   "frames": unreal.AnimationLibrary.get_num_frames(anim) if hasattr(unreal.AnimationLibrary, "get_num_frames") else None}
            # round trip: Blender measured heights (m) vs Unreal (cm) at the first frame
            meta = os.path.splitext(fbx)[0] + ".json"
            if os.path.exists(meta):
                m = json.load(open(meta))
                ue = {b: round(bone_world_z(anim, b, 0.0) / 100.0, 3) for b in ("pelvis", "head", "hand_r")}
                bl = {"pelvis": m["pelvis_z"]["start"], "head": m["head_z"]["start"], "hand_r": m["hand_r_z"]["start"]}
                rec["round_trip_z_m"] = {"blender": bl, "unreal": ue,
                                         "max_err_m": round(max(abs(ue[k] - bl[k]) for k in bl), 3)}
            out["clips"].append(rec)
        except Exception:
            import traceback
            out["errors"].append({"clip": name, "error": traceback.format_exc()[-1500:]})
    json.dump(out, open(REPORT, "w"), indent=1)
    unreal.log("TAG_ANIM_IMPORT " + ("ok" if not out["errors"] else "ERRORS") + " " + json.dumps(out)[:3000])


main()
