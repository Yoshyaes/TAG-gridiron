# Unreal Editor Python (headless). Motion matching databases for the man with the ball: the pack's own ball-carrier
# locomotion (Madden's MG_BC_* motion graph loops: walk / jog / run / sprint in eight directions, the ball tucked in
# the capture), on the pack bodies' skeleton. Every other man searches PSD_TAG_<gait>_Loops (non-carrier captures: an
# empty hand); a carrier ran on those with a procedural tuck on top and the ball looked fused to his hand (Fred, 16 Nov).
#   PSD_TAG_BC_Walk_Loops   MG_BC_*Walk*
#   PSD_TAG_BC_Run_Loops    MG_BC_*Jog* + MG_BC_*Run*
#   PSD_TAG_BC_Sprint_Loops MG_BC_*Sprint*
# Each is a copy of the hero PSD_TAG_<gait>_Loops (its schema and settings), emptied and refilled. Read by
# FAthleteAssets::LoadMotionMatching (MMBallV), used by AAthlete::DriveMotionMatching while he has the ball.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Report: Saved/mm_build_bc_dbs.json
import json
import os
import re

import unreal

EAL = unreal.EditorAssetLibrary
LIB = unreal.TagEditorLib
MM = "/Game/TAG/AnimH/MM/"
BC = "/Game/TAG/AnimH/Pack/BallCarrier"
REPORT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "mm_build_bc_dbs.json")
# not a free run with the ball: pushing a pile, squared up to a tackler, both arms over the ball (traffic)
SKIP = ("PushPile", "SquaredUp", "Coverball", "CoverBall", "Celeb", "Dive", "Stationary", "Hole")
rep = {"dbs": {}, "errors": [], "skipped": []}

try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    reg.scan_paths_synchronous([MM, BC], True)
    clips = sorted(str(d.package_name) for d in reg.get_assets(unreal.ARFilter(package_paths=[BC], recursive_paths=True))
                   if str(d.asset_name).startswith("A_TAG_Pack_MG_BC_"))
    rep["mg_bc"] = len(clips)
    gaits = {"Walk": [], "Run": [], "Sprint": []}
    for p in clips:
        n = p.rsplit("/", 1)[1]
        if any(s in n for s in SKIP):
            rep["skipped"].append(n)
            continue
        if re.search(r"Sprint", n):
            gaits["Sprint"].append(p)
        elif re.search(r"Jog|Run", n):
            gaits["Run"].append(p)
        elif re.search(r"Walk", n):
            gaits["Walk"].append(p)
        else:
            rep["skipped"].append(n)
    for g, paths in gaits.items():
        src, dst = MM + "PSD_TAG_%s_Loops" % g, MM + "PSD_TAG_BC_%s_Loops" % g
        db = unreal.load_asset(dst) if EAL.does_asset_exist(dst) else EAL.duplicate_asset(src, dst)
        if not db:
            rep["errors"].append("no database " + dst)
            continue
        LIB.database_remove_anims_containing(db, "_")
        added = []
        for p in paths:
            anim = unreal.load_asset(p)
            if anim and LIB.database_add_anim(db, anim):
                added.append(p.rsplit("/", 1)[1])
        # (the captures are not cyclic: indexed only up to half a second before their end, as the other loops)
        trimmed = LIB.database_trim_ends(db, "A_TAG_Pack_", 0.5)
        EAL.save_asset(dst, only_if_is_dirty=False)
        rep["dbs"][dst.rsplit("/", 1)[1]] = {"added": len(added), "trimmed": trimmed, "anims": added}
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
json.dump(rep, open(REPORT, "w"), indent=1)
unreal.log("TAG_MM_BC_DBS " + json.dumps({k: (v if k != "dbs" else {n: d["added"] for n, d in v.items()}) for k, v in rep.items() if k != "skipped"})[:2000])
