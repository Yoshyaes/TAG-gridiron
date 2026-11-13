# Unreal Editor Python (headless). The motion matching databases for the pack bodies' own skeleton: every TAG database
# of /Game/TAG/Anim/MM (Tools/Art/mm_build_dbs.py, UEFN mannequin) is mirrored into /Game/TAG/AnimH/MM with the same
# settings and the same clips, each entry swapped for its hero-skeleton twin (the same capture built on the pack's
# skeleton: /Game/TAG/AnimH/Authored and /Game/TAG/AnimH/Pack, Tools/Art/import_tag_anims.py and import_pack_library.py
# with TAG_ANIM_RIG=hero). The schema PSS_TAGH is PSS_TAG on the hero skeleton (same channels and weights; the channels
# name bones the two rigs share: pelvis, foot_l/r, ball_l/r ...). Run mm_build_dbs.py first when the UEFN lists change.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Report: Saved/mm_build_dbs_hero.json
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
SRC = "/Game/TAG/Anim/MM/"
DST = "/Game/TAG/AnimH/MM/"
SKEL = "/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "mm_build_dbs_hero.json")
rep = {"dbs": {}, "errors": [], "missing": []}


def set_schema_skeleton(sch, skel):
    # 5.4+: an array of roled skeletons; older: a single skeleton
    try:
        roled = sch.get_editor_property("skeletons")
        out = []
        for r in roled:
            r.set_editor_property("skeleton", skel)
            try:
                r.set_editor_property("mirror_data_table", None)
            except Exception:
                pass
            out.append(r)
        sch.set_editor_property("skeletons", out)
        return "skeletons[%d]" % len(out)
    except Exception:
        sch.set_editor_property("skeleton", skel)
        return "skeleton"


try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous([SRC, "/Game/TAG/AnimH"], True)
    skel = unreal.load_asset(SKEL)
    if not EAL.does_directory_exist(DST):
        EAL.make_directory(DST)
    # schema
    sch_path = DST + "PSS_TAGH"
    sch = unreal.load_asset(sch_path) if EAL.does_asset_exist(sch_path) else EAL.duplicate_asset(SRC + "PSS_TAG", sch_path)
    rep["schema"] = {"path": sch_path, "skeleton_field": set_schema_skeleton(sch, skel)}
    EAL.save_asset(sch_path, only_if_is_dirty=False)
    # every TAG database
    names = sorted({p.split(".")[0].rsplit("/", 1)[1] for p in EAL.list_assets(SRC, recursive=False, include_folder=False)
                    if p.split(".")[0].rsplit("/", 1)[1].startswith("PSD_TAG_")})
    # the hero clips by asset name (game clips in Authored, the library under Pack/<Category>)
    by_name = {str(d.asset_name): str(d.package_name) for d in reg.get_assets(unreal.ARFilter(
        package_paths=["/Game/TAG/AnimH/Authored", "/Game/TAG/AnimH/Pack"], recursive_paths=True))}
    rep["hero_clips_indexed"] = len(by_name)
    for n in names:
        src_db = unreal.load_asset(SRC + n)
        dst = DST + n
        db = unreal.load_asset(dst) if EAL.does_asset_exist(dst) else EAL.duplicate_asset(SRC + n, dst)
        db.set_editor_property("schema", sch)
        # every entry goes (rebuilt from the UEFN list); the library's remove needs a non-empty substring and every
        # asset name has an underscore. A database that still holds entries would keep UEFN-skeleton clips, which the
        # hero mesh plays through skeleton remapping: bent, sunk bodies and a third of the frame rate (12 Nov)
        LIB.database_remove_anims_containing(db, "_")
        left = [str(x) for x in LIB.database_anim_names(db)]
        if left:
            raise RuntimeError("%s still holds %d entries after clearing: %s" % (n, len(left), left[:3]))
        want = [str(a) for a in LIB.database_anim_names(src_db)]
        added, missing = 0, []
        for a in want:
            path = by_name.get(a)
            anim = unreal.load_asset(path) if path else None
            if anim and LIB.database_add_anim(db, anim):
                added += 1
            elif not anim:
                missing.append(a)
        trimmed = LIB.database_trim_ends(db, "A_TAG_Pack_", 0.5) if "_Loops" in n else 0
        # every entry on the hero skeleton
        names_now = [str(x) for x in LIB.database_anim_names(db)]
        if len(names_now) != added:
            raise RuntimeError("%s holds %d entries, %d added" % (n, len(names_now), added))
        EAL.save_asset(dst, only_if_is_dirty=False)
        rep["dbs"][n] = {"uefn": len(want), "added": added, "missing": len(missing), "trimmed": trimmed}
        rep["missing"] += missing
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_MM_DBS_HERO " + json.dumps({k: v for k, v in rep.items() if k != "missing"})[:2500] + " missing %d" % len(rep["missing"]))
