# Unreal Editor Python (headless). The run and walk starts of the pack bodies' motion matching split three ways
# (/Game/TAG/AnimH/MM/PSD_TAG_Stand_<Run|Walk>_Starts_S1..S3: copies of the shared start databases, each given a third of
# the pack's unused starts out of a stand - forward, turn-and-go, the LB's, the big man's - and each without one of the
# keyed starts every man shared). At the snap and the whistle every man starting to run searched the same few starts:
# A_TAG_Loco_Start_Back / Start_Fwd on four or more men at once (the "4+ men on one loop" share, p83). AAthlete takes
# S(id % 3 + 1).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Report: Saved/mm_start_sets_hero.json
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
MM = "/Game/TAG/AnimH/MM/"
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
REPORT = os.path.join(SAVED, "mm_start_sets_hero.json")
NEW = {
    "Run": ["A_TAG_Pack_Accel_Jog_v2__P01", "A_TAG_Pack_Accel_Boost_FastJog_v1__P01", "A_TAG_Pack_NonBC_Agile_Start_to_Fwd_0_FB16__P01",
            "A_TAG_Pack_Turn_NonBC_Agile_Stand_to_Fwd_R_0_H2H__P01", "A_TAG_Pack_Turn_NonBC_Bruiser_Stand_to_Fwd_0_H2H__P01",
            "A_TAG_Pack_Turn_NonBC_BigGuy_Stand_to_Fwd_0_Low__P01", "A_TAG_Pack_Turn_LB_Stand_to_D1_RunFwd__P01",
            "A_TAG_Pack_Turn_NonBC_Agile_Stand_to_Fwd_R_0_L2L__P01", "A_TAG_Pack_Turn_NonBC_Bruiser_Stand_to_Fwd_0_L2L__P01",
            "A_TAG_Pack_Turn_NonBC_Agile_Stand_to_Bkpd_180_H2H__P01", "A_TAG_Pack_Turn_NonBC_Bruiser_Stand_to_Fwd_180_H2H__P01",
            "A_TAG_Pack_Turn_NonBC_BigGuy_Stand_to_Fwd_180_Hi__P01"],
    "Walk": ["A_TAG_Pack_Turn_NonBC_Stand_to_Fwd_0_Jog_v2__P01", "A_TAG_Pack_NonBC_Stand_to_Fwd_180", "A_TAG_Pack_Turn_LB_Stand_to_D2__P01"],
}
# (each set but the first loses one of the keyed starts every man shared)
DROP = {2: ["A_TAG_Loco_Start_Fwd"], 3: ["A_TAG_Loco_Start_Back"]}
rep = {"sets": {}, "errors": []}
try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/Game/TAG/AnimH"], True)
    by_name = {}
    for d in reg.get_assets(unreal.ARFilter(package_paths=["/Game/TAG/AnimH/Pack"], recursive_paths=True)):
        by_name.setdefault(str(d.asset_name), str(d.package_name))
    for gait, extra in NEW.items():
        src = MM + "PSD_TAG_Stand_%s_Starts" % gait
        for v in (1, 2, 3):
            path = src + "_S%d" % v
            if EAL.does_asset_exist(path):
                EAL.delete_asset(path)
            EAL.duplicate_asset(src, path)
            db = unreal.load_asset(path)
            removed = 0
            for s in DROP.get(v, []):
                removed += LIB.database_remove_anims_containing(db, s)
            added, missing = [], []
            for n in [x for i, x in enumerate(extra) if i % 3 == v - 1]:
                a = unreal.load_asset(by_name[n]) if n in by_name else None
                if a and LIB.database_add_anim(db, a):
                    added.append(n)
                else:
                    missing.append(n)
            EAL.save_asset(path, only_if_is_dirty=False)
            rep["sets"][path] = {"added": added, "missing": missing, "removed": removed, "anims": [str(n) for n in LIB.database_anim_names(db)]}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc())
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_START_SETS %s" % json.dumps({k.rsplit("/", 1)[1]: (len(v["anims"]), len(v["added"]), len(v["missing"])) for k, v in rep["sets"].items()}))
