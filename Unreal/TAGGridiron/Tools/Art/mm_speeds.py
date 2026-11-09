# Unreal Editor Python (headless, read only): root motion speed of the Game Animation Sample locomotion loops, used to
# set the gait bands motion matching searches (Source/TAGGridiron/Render/Athlete.cpp, DriveMotionMatching).
import json
import os

import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "mm_speeds.json")
A = "/Game/Characters/UEFN_Mannequin/Animations/"
rep = {"clips": {}, "errors": []}
for p in ("Walk/M_Neutral_Walk_Loop_F", "Run/M_Neutral_Run_Loop_F", "Sprint/M_Neutral_Sprint_Loop_F", "Run/M_Neutral_Run_Loop_B",
          "Run/M_Neutral_Run_Loop_LL", "Walk/M_Neutral_Walk_Loop_B"):
    try:
        a = unreal.load_asset(A + p)
        if not a:
            rep["errors"].append("missing " + p)
            continue
        n = a.get_play_length()
        t0 = unreal.AnimationLibrary.get_bone_pose_for_time(a, "root", 0.0, False)
        t1 = unreal.AnimationLibrary.get_bone_pose_for_time(a, "root", n, False)
        d = t1.translation - t0.translation
        rep["clips"][p] = {"len_s": round(n, 3), "dist_cm": round((d.x ** 2 + d.y ** 2) ** 0.5, 1),
                           "speed_cms": round(((d.x ** 2 + d.y ** 2) ** 0.5) / max(n, 1e-3), 1)}
    except Exception as e:
        rep["errors"].append(p + ": " + str(e)[:300])
json.dump(rep, open(OUT, "w"), indent=1)
unreal.log("TAG_MM_SPEEDS " + json.dumps(rep))
