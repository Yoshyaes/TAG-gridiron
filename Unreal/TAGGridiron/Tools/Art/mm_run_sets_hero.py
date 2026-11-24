# Unreal Editor Python (headless). The run style sets of the pack bodies' motion matching (/Game/TAG/AnimH/MM/
# PSD_TAG_Run_Loops_S1..S6) given more jogs. Each of the three run sets held one or two forward jog loops, so every man
# on a set jogging back to the huddle or out to his spot found the same best capture: A_TAG_Loco_Jog_Agile and
# A_TAG_Loco_Run_Heavy on four or more men in a fifth of post-play and pre-snap frames (the "4+ men on one loop" share,
# 0.217 in p82). The pack has more constant-pace jogs nobody searched (post-play jogs, pre-play DB / LB jogs, kick-return
# jogs, the LB and heavy forward jogs): dealt across six sets (S4-S6 start as copies of S1-S3, as the walk's did;
# AAthlete takes S(id % 6 + 1)).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Report: Saved/mm_run_sets_hero.json
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
MM = "/Game/TAG/AnimH/MM/"
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
REPORT = os.path.join(SAVED, "mm_run_sets_hero.json")
# forward first (the jog back), then forward-right; each dealt to the next set in turn
JOGS = [
    "A_TAG_Pack_PTM_Loco_PostPlay_FWD_0_Constant_Jog_Slow_Loop_Gunn__P01",
    "A_TAG_Pack_PTM_Loco_PostPlay_FWD_Constant_Jog_Loop_Positive_NodHead__P01",
    "A_TAG_Pack_Loco_NonBC_15_Jog_Loop__P01",
    "A_TAG_Pack_MG_LB_Med_Jog_D1__P01",
    "A_TAG_Pack_MG_NonBC_Agile_MC_Jog_D1__P01",
    "A_TAG_Pack_MG_NonBC_KickReturn_Jog_D1__P01",
    "A_TAG_Pack_MG_NonBC_PrePlay_DB_Jog_D1__P01",
    "A_TAG_Pack_MG_NonBC_PrePlay_Slow_SlightLean_Jog_D1__P01",
    "A_TAG_Pack_MG_Loco_Heavy_Jog_LeanFwd_D1__P01",
    "A_TAG_Pack_MG_NonBC_PrePlay_DB_Jog_D2__P01",
    "A_TAG_Pack_MG_NonBC_PrePlay_Slow_SlightLean_Jog_D2__P01",
    "A_TAG_Pack_MG_NonBC_KickReturn_Jog_D2__P01",
]
rep = {"sets": {}, "errors": []}
try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous(["/Game/TAG/AnimH"], True)
    by_name = {}
    for d in reg.get_assets(unreal.ARFilter(package_paths=["/Game/TAG/AnimH/Pack"], recursive_paths=True)):
        by_name.setdefault(str(d.asset_name), str(d.package_name))
    for v in (1, 2, 3, 4, 5, 6):
        path = MM + "PSD_TAG_Run_Loops_S%d" % v
        if not EAL.does_asset_exist(path):
            EAL.duplicate_asset(MM + "PSD_TAG_Run_Loops_S%d" % (v - 3), path)
        db = unreal.load_asset(path)
        have = [str(n) for n in LIB.database_anim_names(db)]
        want = [n for i, n in enumerate(JOGS) if i % 6 == v - 1]
        added, missing = [], []
        for n in want:
            if n in have:
                continue
            a = unreal.load_asset(by_name[n]) if n in by_name else None
            if a and LIB.database_add_anim(db, a):
                added.append(n)
            else:
                missing.append(n)
        # (a long loop's first and last half second out of the search: the captures ease in and out of their pace)
        trimmed = 0
        for n in added:
            a = unreal.load_asset(by_name[n])
            if a and a.get_play_length() > 1.6:
                trimmed += LIB.database_trim_ends(db, n, 0.4)
        EAL.save_asset(path, only_if_is_dirty=False)
        rep["sets"][path] = {"added": added, "missing": missing, "trimmed": trimmed, "anims": [str(n) for n in LIB.database_anim_names(db)]}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc())
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_RUN_SETS %s" % json.dumps({k.rsplit("/", 1)[1]: (len(v["anims"]), len(v["added"]), len(v["missing"])) for k, v in rep["sets"].items()}))
