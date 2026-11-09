# Unreal Editor Python (headless). TAG copies of the Game Animation Sample's Dense loop databases, tuned for football:
#   PSD_TAG_Sprint_Loops: the sprint loops without the walk/run-to-sprint transitions (offered all the time, the
#     transitions kept a top-speed runner in an acceleration lean) plus the gentle 20 deg curve loops.
#   PSD_TAG_Run_Loops: the run loops plus the run arc clips (small / wide / tight curves), so a curving runner gets a
#     curving stride instead of a straight loop rotated by the actor.
# Read by FAthleteAssets::LoadMotionMatching (falls back to Epic's databases when these are missing).
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
SRC = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/Dense/"
ANIM = "/Game/Characters/UEFN_Mannequin/Animations/"
DST = "/Game/TAG/Anim/MM/"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "mm_build_dbs.json")
rep = {"dbs": {}, "errors": []}


def build(src, dst, drop, add):
    if EAL.does_asset_exist(dst):
        EAL.delete_asset(dst)
    db = EAL.duplicate_asset(src, dst)
    removed = sum(LIB.database_remove_anims_containing(db, d) for d in drop)
    added = []
    for a in add:
        anim = unreal.load_asset(ANIM + a)
        if anim and LIB.database_add_anim(db, anim):
            added.append(a.split("/")[-1])
    EAL.save_asset(dst, only_if_is_dirty=False)
    rep["dbs"][dst.split("/")[-1]] = {"removed": removed, "added": added, "anims": list(LIB.database_anim_names(db))}


try:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([SRC, ANIM], True)
    build(SRC + "PSD_Dense_Stand_Sprint_Loops", DST + "PSD_TAG_Sprint_Loops", ["Transition"],
          ["Sprint/M_Neutral_Sprint_Loop_F_L_20", "Sprint/M_Neutral_Sprint_Loop_F_R_20"])
    build(SRC + "PSD_Dense_Stand_Run_Loops", DST + "PSD_TAG_Run_Loops", [],
          ["Run/M_Neutral_Run_Arc_%s_%s" % (k, s) for k in ("Small", "Wide", "Tight") for s in ("L", "R")])
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_MM_DBS " + json.dumps(rep)[:2000])
