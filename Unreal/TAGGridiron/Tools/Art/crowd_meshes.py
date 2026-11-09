# Unreal Editor Python (headless, read only): crowd mesh / animation compatibility and LOD budget.
import json, os
import unreal
ROOT = "/MetaHumanCrowdContent"
OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "crowd_meshes.json")
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.scan_paths_synchronous([ROOT], True)
rep = {"meshes": [], "anims": []}
for a in ar.get_assets_by_path(ROOT, recursive=True):
    cls = str(a.asset_class_path.asset_name)
    name = str(a.asset_name)
    pkg = str(a.package_name)
    if cls == "SkeletalMesh" and ("FullBody" in name or "Combined" in name or name == "SKM_MetaHumanCrowd"):
        m = unreal.load_asset(pkg)
        sk = m.get_editor_property("skeleton")
        lods = []
        try:
            for i in range(m.get_lod_num() if hasattr(m, "get_lod_num") else 0):
                lods.append(i)
        except Exception:
            pass
        info = {"mesh": pkg, "skeleton": sk.get_path_name() if sk else None, "lods": len(lods),
                "materials": len(m.get_editor_property("materials"))}
        try:
            sub = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
            info["lod0_verts"] = sub.get_num_verts(m, 0) if sub else None
        except Exception as e:
            info["lod_err"] = str(e)[:100]
        b = m.get_bounds()
        info["height_cm"] = round(b.box_extent.z * 2, 1)
        rep["meshes"].append(info)
    if cls in ("AnimSequence", "AnimMontage"):
        an = unreal.load_asset(pkg)
        sk = an.get_editor_property("skeleton")
        rep["anims"].append({"anim": pkg, "skeleton": sk.get_path_name() if sk else None,
                             "len": round(an.get_play_length(), 2)})
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_CROWD_MESHES done")
