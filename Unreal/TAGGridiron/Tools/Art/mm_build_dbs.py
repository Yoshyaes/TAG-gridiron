# Unreal Editor Python (headless). TAG copies of the Game Animation Sample's Dense loop databases, tuned for football:
#   PSD_TAG_Sprint_Loops: the sprint loops without the walk/run-to-sprint transitions (offered all the time, the
#     transitions kept a top-speed runner in an acceleration lean) plus the gentle 20 deg curve loops.
#   PSD_TAG_Run_Loops: the run loops plus the run arc clips (small / wide / tight curves), so a curving runner gets a
#     curving stride instead of a straight loop rotated by the actor.
#   PSD_TAG_Walk_Loops, PSD_TAG_Crouch_Loops: copies of the walk and crouch-walk loops.
# Every database also takes the football locomotion retargeted from the mocap pack with root motion
# (Tools/Anim/mocap_map.LOCO_DB: sprints to 11 m/s, runs, backpedals, strafes, and for the crouch gait the linemen's
# kick slides, shuffles and crouched runs), imported by import_tag_anims.py as /Game/TAG/Anim/Authored/A_TAG_Loco_*.
# Read by FAthleteAssets::LoadMotionMatching (falls back to Epic's databases when these are missing).
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Anim"))
import mocap_map  # noqa: E402

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
SRC = "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/Dense/"
ANIM = "/Game/Characters/UEFN_Mannequin/Animations/"
DST = "/Game/TAG/Anim/MM/"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "mm_build_dbs.json")
rep = {"dbs": {}, "errors": []}


TAG_ANIM = "/Game/TAG/Anim/Authored/"
TRAJECTORY_WEIGHT = 3.0


def build(src, dst, drop, add, loco, always_drop=()):
    # an existing TAG database is kept (deleting a loaded asset in a commandlet fails silently) and only gains the
    # locomotion clips; a fresh one is duplicated from Epic's and gets the drops and the extra sample clips too
    fresh = not EAL.does_asset_exist(dst)
    db = EAL.duplicate_asset(src, dst) if fresh else unreal.load_asset(dst)
    if not db:
        rep["errors"].append("no database for " + dst)
        return
    removed = sum(LIB.database_remove_anims_containing(db, d) for d in drop) if fresh else 0
    removed += sum(LIB.database_remove_anims_containing(db, d) for d in always_drop)
    # locomotion clips no longer listed for this database (mocap_map.LOCO_DB, LOCO_SKATE) leave it
    have = [str(n) for n in LIB.database_anim_names(db)]
    for n in have:
        if n.startswith("A_TAG_Loco_") and n[6:] not in loco:
            removed += LIB.database_remove_anims_containing(db, n)
    added, missing = [], []
    for path in ([ANIM + a for a in add] if fresh else []) + [TAG_ANIM + "A_TAG_" + n for n in loco if "A_TAG_" + n not in have]:
        anim = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
        if anim and LIB.database_add_anim(db, anim):
            added.append(path.split("/")[-1])
        elif not anim:
            missing.append(path.split("/")[-1])
    EAL.save_asset(dst, only_if_is_dirty=False)
    rep["dbs"][dst.split("/")[-1]] = {"removed": removed, "added": added, "missing": missing, "anims": list(LIB.database_anim_names(db))}


try:
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([SRC, ANIM, TAG_ANIM], True)
    L = mocap_map.LOCO_DB
    # the sprint, run and crouch loops are the pack's alone: side by side with the sample's loops the search kept the
    # sample's on pose continuity (-TagMMTrace), and the pack covers those gaits (10-11 m/s sprints, runs, strafes,
    # backpedals; kick slides, shuffles and crouched runs for the line). The walk keeps the sample's loops as well
    # (post-play milling about), and the starts, stops, pivots and idles stay the sample's.
    PACK_ONLY = ["M_Neutral"]
    build(SRC + "PSD_Dense_Stand_Sprint_Loops", DST + "PSD_TAG_Sprint_Loops", ["Transition"],
          ["Sprint/M_Neutral_Sprint_Loop_F_L_20", "Sprint/M_Neutral_Sprint_Loop_F_R_20"], L["Sprint"], PACK_ONLY)
    build(SRC + "PSD_Dense_Stand_Run_Loops", DST + "PSD_TAG_Run_Loops", [],
          ["Run/M_Neutral_Run_Arc_%s_%s" % (k, s) for k in ("Small", "Wide", "Tight") for s in ("L", "R")], L["Run"], PACK_ONLY)
    build(SRC + "PSD_Dense_Stand_Walk_Loops", DST + "PSD_TAG_Walk_Loops", [], [], L["Walk"])
    build(SRC + "PSD_Dense_Crouch_Walk_Loops", DST + "PSD_TAG_Crouch_Loops", [], [], L["Crouch"], PACK_ONLY)
    # the sets beside the loops: the sample's starts, stops, pivots and idles plus the pack's (mixed, so every move
    # the sample covers stays covered); the crouch starts are the pack's alone (see FAthleteAssets::LoadMotionMatching)
    for g in ("Walk", "Run", "Sprint"):
        for k in ("Starts", "Stops", "Pivots"):
            build(SRC + "PSD_Dense_Stand_%s_%s" % (g, k), DST + "PSD_TAG_Stand_%s_%s" % (g, k), [], [], L["%s_%s" % (g, k)])
    build(SRC + "PSD_Dense_Stand_Idles", DST + "PSD_TAG_Stand_Idles", [], [], L["Stand_Idles"])
    build(SRC + "PSD_Dense_Crouch_Idles", DST + "PSD_TAG_Crouch_Idles", [], [], L["Crouch_Idles"])
    build(SRC + "PSD_Dense_Crouch_Walk_Starts", DST + "PSD_TAG_Crouch_Starts", [], [], L["Crouch_Starts"], PACK_ONLY)
    # the TAG schema: the sample's PSS_Default with the trajectory weighted 3x against the pose channels (the pack's
    # loops come from many men; on the sample's weights pose similarity outvoted speed and direction and the search
    # took a 5 m/s crouched run or a lateral kick slide for a 2 m/s forward drive, -TagMoveTest -TagMMTrace=67)
    SCHEMA = DST + "PSS_TAG"
    sch = unreal.load_asset(SCHEMA) if EAL.does_asset_exist(SCHEMA) else EAL.duplicate_asset(
        "/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Schemas/PSS_Default", SCHEMA)
    chans = sch.get_editor_property("channels")
    for ch in chans:
        if ch.get_class().get_name() == "PoseSearchFeatureChannel_Trajectory":
            ch.set_editor_property("weight", TRAJECTORY_WEIGHT)
    sch.set_editor_property("channels", chans)
    EAL.save_asset(SCHEMA, only_if_is_dirty=False)
    for name in rep["dbs"]:
        db = unreal.load_asset(DST + name)
        if db and db.get_editor_property("schema") != sch:
            db.set_editor_property("schema", sch)
            EAL.save_asset(DST + name, only_if_is_dirty=False)
    rep["schema"] = {"path": SCHEMA, "trajectory_weight": TRAJECTORY_WEIGHT}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_MM_DBS " + json.dumps(rep)[:2000])
