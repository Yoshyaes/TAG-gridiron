# Unreal Editor Python. Imports private Madden 19 reference FBX and textures into /Game/MaddenRef only.
# Run headless:
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash
# Reads the job file named by env MADDENREF_JOBS (JSON list) and writes a report to env MADDENREF_REPORT.
#
# Job fields
#   kind        "mesh" (static or skeletal, decided by the FBX), "texture", or "anim"
#   file        absolute path to the FBX, PNG or TGA
#   dest        content folder, must start with /Game/MaddenRef
#   name        asset name to create
#   skeleton    optional skeleton asset path, for skeletal meshes sharing one skeleton and for anims.
#               A bootstrap job (name X, dest D, skeleton D/X_Skeleton) creates it on the first run.
#   normal      optional bool, texture is a normal map
#   srgb        optional bool, texture colour space
#
# Scale is fixed at export time (FBX written in meters with UnitScaleFactor 100). This script never applies
# an import offset scale, so a wrong scale shows up as a failed size check instead of being hidden.
import json
import os
import traceback

import unreal

ROOT = "/Game/MaddenRef"
EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def _pipeline(kind, skeleton, fps=None):
    """Saved Interchange pipeline asset (AssetImportTask.options is ignored by Interchange in 5.8)."""
    pipe_path = ROOT + "/_Pipelines/P_MaddenRef_" + kind
    pipe = unreal.load_asset(pipe_path) if EAL.does_asset_exist(pipe_path) else None
    if pipe is None:
        base = unreal.load_asset("/Interchange/Pipelines/DefaultAssetsPipeline")
        pkg, name = pipe_path.rsplit("/", 1)
        pipe = tools.duplicate_asset(name, pkg, base)
    if pipe is None:
        raise RuntimeError("could not create pipeline asset " + pipe_path)
    mesh_p = pipe.get_editor_property("mesh_pipeline")
    mesh_p.set_editor_property("create_physics_asset", False)
    # No Nanite on reference static meshes. Its bounds pad small objects and it adds nothing here.
    mesh_p.set_editor_property("build_nanite", False)
    mesh_p.set_editor_property("import_static_meshes", kind == "mesh")
    mesh_p.set_editor_property("import_skeletal_meshes", kind == "mesh")
    cur = mesh_p.get_editor_property("combine_static_meshes_behavior")
    members = [m for m in dir(type(cur)) if m.isupper()]
    unreal.log("MADDENREF combine behaviors " + ",".join(members))
    pick = next((m for m in members if "ALL" in m), None) or next((m for m in members if "COMBINE" in m and "NOT" not in m and "NEVER" not in m), None)
    if pick:
        mesh_p.set_editor_property("combine_static_meshes_behavior", getattr(type(cur), pick))
    common = pipe.get_editor_property("common_skeletal_meshes_and_animations_properties")
    common.set_editor_property("skeleton", skeleton)
    common.set_editor_property("import_only_animations", kind == "anim")
    anim_p = pipe.get_editor_property("animation_pipeline")
    anim_p.set_editor_property("import_animations", kind == "anim")
    if kind == "anim" and fps:
        # Bake at the clip's own rate so its length is frame aligned (Interchange guessed 12 fps otherwise).
        anim_p.set_editor_property("use30_hz_to_bake_bone_animation", False)
        anim_p.set_editor_property("custom_bone_animation_sample_rate", int(round(float(fps))))
    mat_p = pipe.get_editor_property("material_pipeline")
    mat_p.set_editor_property("import_materials", False)
    EAL.save_loaded_asset(pipe, only_if_is_dirty=False)
    return pipe_path + "." + pipe_path.rsplit("/", 1)[1]


def import_with_task(job):
    """Import straight into dest so Interchange names the asset after the file.

    Meshes and anims are copied to a temp file called <name>.fbx first. Interchange names a combined
    mesh after its source file, so the asset lands as dest/<name> and a re-run replaces it in place.
    No delete or rename of existing assets is needed (deleting and renaming in one commandlet session
    collides with objects that are still in memory, and renames leave redirectors). The shared skeleton
    comes from a bootstrap job whose mesh is named after the skeleton, so Interchange creates
    dest/<name>_Skeleton itself and every other job points its skeleton field at that path.
    """
    import shutil
    kind = job.get("kind")
    skel_path = job.get("skeleton")
    existing_skel = unreal.load_asset(skel_path) if skel_path and EAL.does_asset_exist(skel_path) else None
    if kind in ("mesh", "anim"):
        tmp_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "MaddenRefImport")
        os.makedirs(tmp_dir, exist_ok=True)
        tmp = os.path.join(tmp_dir, job["name"] + os.path.splitext(job["file"])[1])
        shutil.copyfile(job["file"], tmp)
        params = unreal.ImportAssetParameters()
        params.is_automated = True
        params.replace_existing = True
        params.override_pipelines = [unreal.SoftObjectPath(_pipeline(kind, existing_skel, job.get("fps")))]
        src = unreal.InterchangeManager.create_source_data(tmp)
        mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
        if not mgr.import_asset(job["dest"], src, params):
            raise RuntimeError("interchange import_asset returned false")
    else:
        task = unreal.AssetImportTask()
        task.filename = job["file"]
        task.destination_path = job["dest"]
        task.destination_name = job["name"]
        task.replace_existing = True
        task.automated = True
        task.save = False
        tools.import_asset_tasks([task])
    out = []
    for p in EAL.list_assets(job["dest"], recursive=False, include_folder=False):
        p = p.split(".")[0]
        leaf = p.rsplit("/", 1)[1]
        anim_hit = kind == "anim" and leaf.startswith(job["name"]) and isinstance(unreal.load_asset(p), unreal.AnimSequence)
        if leaf == job["name"] or leaf == job["name"] + "_Skeleton" or anim_hit:
            EAL.save_asset(p, only_if_is_dirty=False)
            out.append(p)
    return out


def _uv_channels(mesh):
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if sub is not None:
        return sub.get_num_uv_channels(mesh, 0)
    lib = getattr(unreal, "EditorStaticMeshLibrary", None)
    if lib is not None and hasattr(lib, "get_num_uv_channels"):
        return lib.get_num_uv_channels(mesh, 0)
    return None


def mesh_report(path):
    a = unreal.load_asset(path)
    r = {"path": path, "class": a.get_class().get_name() if a else None}
    if isinstance(a, unreal.StaticMesh):
        box = a.get_bounding_box()
        r["bbox_cm"] = [round(box.max.x - box.min.x, 2), round(box.max.y - box.min.y, 2), round(box.max.z - box.min.z, 2)]
        r["lods"] = a.get_num_lods()
        r["material_slots"] = [str(m.material_slot_name) for m in a.static_materials]
        r["uv_channels"] = _uv_channels(a)
    elif isinstance(a, unreal.SkeletalMesh):
        b = a.get_bounds()
        r["bbox_cm"] = [round(b.box_extent.x * 2, 2), round(b.box_extent.y * 2, 2), round(b.box_extent.z * 2, 2)]
        sk = a.skeleton
        r["skeleton"] = sk.get_path_name() if sk else None
        r["bones"] = None
        try:
            pose = unreal.AnimPoseExtensions.get_reference_pose(sk)
            names = unreal.AnimPoseExtensions.get_bone_names(pose)
            r["bones"] = len(names)
            if "Head" in [str(n) for n in names]:
                head = unreal.AnimPoseExtensions.get_bone_pose(pose, "Head", unreal.AnimPoseSpaces.WORLD)
                r["head_bone_z_cm"] = round(head.translation.z, 2)
        except Exception as e:
            r["bones_error"] = repr(e)[:200]
        r["material_slots"] = [str(m.material_slot_name) for m in a.materials]
        r["lods"] = a.get_lod_num() if hasattr(a, "get_lod_num") else None
    elif isinstance(a, unreal.AnimSequence):
        sk = a.get_editor_property("skeleton")
        r["skeleton"] = sk.get_path_name() if sk else None
        r["play_length_s"] = round(a.get_play_length(), 4)
        try:
            r["frames"] = unreal.AnimationLibrary.get_num_frames(a)
        except Exception as e:
            r["frames_error"] = repr(e)[:200]
        try:
            tracks = [str(t) for t in unreal.AnimationLibrary.get_animation_track_names(a)]
            r["bone_tracks"] = len(tracks)
            if sk is not None:
                ref = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(unreal.AnimPoseExtensions.get_reference_pose(sk))]
                ref_l = {x.lower() for x in ref}   # FNames compare case insensitively, UE returns track names in lower case
                r["tracks_not_in_skeleton"] = [t for t in tracks if t.lower() not in ref_l][:20]
                r["skeleton_bones"] = len(ref)
        except Exception as e:
            r["tracks_error"] = repr(e)[:200]
        try:
            pose0 = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, unreal.AnimPoseEvaluationOptions())
            head = unreal.AnimPoseExtensions.get_bone_pose(pose0, "Head", unreal.AnimPoseSpaces.WORLD)
            r["head_z_cm_t0"] = round(head.translation.z, 2)
            pose1 = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, a.get_play_length(), unreal.AnimPoseEvaluationOptions())
            tr0 = unreal.AnimPoseExtensions.get_bone_pose(pose0, "AITrajectory", unreal.AnimPoseSpaces.WORLD).translation
            tr1 = unreal.AnimPoseExtensions.get_bone_pose(pose1, "AITrajectory", unreal.AnimPoseSpaces.WORLD).translation
            r["traj_travel_cm"] = round(((tr1.x - tr0.x) ** 2 + (tr1.y - tr0.y) ** 2) ** 0.5, 2)
        except Exception as e:
            r["pose_error"] = repr(e)[:200]
    elif isinstance(a, unreal.Texture2D):
        r["size"] = [a.blueprint_get_size_x(), a.blueprint_get_size_y()]
        r["srgb"] = a.srgb
        r["compression"] = str(a.compression_settings)
    return r


def run():
    # The commandlet starts before the asset registry has scanned /Game, so scan synchronously first.
    unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
    jobs = json.load(open(os.environ["MADDENREF_JOBS"]))
    report = {"engine": unreal.SystemLibrary.get_engine_version(), "results": []}
    for job in jobs:
        res = {"job": job}
        try:
            if not job["dest"].startswith(ROOT):
                raise ValueError("destination outside " + ROOT)
            if job.get("kind") == "report":
                # Report only, for spot checks. Nothing is imported or saved.
                res["assets"] = [mesh_report(job["dest"] + "/" + job["name"])]
                res["status"] = "ok" if res["assets"][0]["class"] else "fail"
                report["results"].append(res)
                continue
            paths = import_with_task(job)
            res["imported"] = paths
            if job.get("kind") == "texture" and paths:
                tex = unreal.load_asset(paths[0])
                if job.get("normal"):
                    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                    tex.set_editor_property("srgb", False)
                elif "srgb" in job:
                    tex.set_editor_property("srgb", bool(job["srgb"]))
                EAL.save_loaded_asset(tex)
            res["assets"] = [mesh_report(p) for p in paths]
            res["status"] = "ok" if paths else "fail"
        except Exception as e:
            res["status"] = "fail"
            res["error"] = repr(e)
            res["trace"] = traceback.format_exc()[-1500:]
        report["results"].append(res)
        unreal.log("MADDENREF " + res["status"] + " " + job.get("name", ""))
    with open(os.environ["MADDENREF_REPORT"], "w") as fh:
        json.dump(report, fh, indent=1)


run()
