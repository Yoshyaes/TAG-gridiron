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
#   skeleton    optional skeleton asset path, for skeletal meshes sharing one skeleton and for anims
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


def import_with_task(job):
    """Import into a staging folder, then move each asset to dest as <name> or <name>__<node>.

    FBX files with several mesh nodes produce one asset per node named after the material node
    (known issue 2). Renaming here gives every asset a name derived from its source asset path,
    and deleting the old target first keeps re-runs idempotent.
    """
    stage = ROOT + "/_Incoming/" + job["name"]
    if EAL.does_directory_exist(stage):
        EAL.delete_directory(stage)
    task = unreal.AssetImportTask()
    task.filename = job["file"]
    task.destination_path = stage
    task.destination_name = job["name"]
    task.replace_existing = True
    task.automated = True
    task.save = False
    if job.get("skeleton") and job.get("kind") in ("mesh", "anim"):
        task.options = None
    tools.import_asset_tasks([task])
    staged = [p.split(".")[0] for p in (task.imported_object_paths or [])]
    primary = [p for p in staged if isinstance(unreal.load_asset(p), (unreal.StaticMesh, unreal.SkeletalMesh, unreal.Texture2D, unreal.AnimSequence))]
    out = []
    for p in staged:
        node = p.rsplit("/", 1)[1]
        is_primary = p in primary
        if is_primary and len(primary) == 1:
            new_name = job["name"]
        else:
            new_name = job["name"] + "__" + node
        target = job["dest"] + "/" + new_name
        if EAL.does_asset_exist(target):
            EAL.delete_asset(target)
        if not EAL.rename_asset(p, target):
            raise RuntimeError("rename failed " + p + " -> " + target)
        EAL.save_asset(target, only_if_is_dirty=False)
        out.append(target)
    if EAL.does_directory_exist(stage):
        EAL.delete_directory(stage)
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
        try:
            r["bones"] = a.get_editor_property("ref_skeleton").get_num() if hasattr(a.get_editor_property("ref_skeleton"), "get_num") else None
        except Exception:
            r["bones"] = None
        r["material_slots"] = [str(m.material_slot_name) for m in a.materials]
    elif isinstance(a, unreal.Texture2D):
        r["size"] = [a.blueprint_get_size_x(), a.blueprint_get_size_y()]
        r["srgb"] = a.srgb
        r["compression"] = str(a.compression_settings)
    return r


def run():
    jobs = json.load(open(os.environ["MADDENREF_JOBS"]))
    report = {"engine": unreal.SystemLibrary.get_engine_version(), "results": []}
    for job in jobs:
        res = {"job": job}
        try:
            if not job["dest"].startswith(ROOT):
                raise ValueError("destination outside " + ROOT)
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
