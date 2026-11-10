# Unreal Editor Python (headless, read only): how well each imported locomotion clip plants its feet, from the clip
# alone (root motion included): sampled at 60 Hz, the world speed of each ball-of-foot bone; a clean gait has the
# slower foot near rest most of the time, a clip whose capture (or retarget) drags both feet shows a high "skate"
# ratio (the slower foot's travel over the root's travel) and little planted time. Drives what mm_build_dbs.py keeps.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import math
import os

import unreal

EAL = unreal.EditorAssetLibrary
opts = unreal.AnimPoseEvaluationOptions()
only = tuple(os.environ.get("TAG_PLANT", "A_TAG_Loco_").split(","))  # name prefixes


def foot(anim, t, bone):
    p = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), bone, unreal.AnimPoseSpaces.WORLD).translation
    return (p.x, p.y, p.z)


for path in sorted(EAL.list_assets("/Game/TAG/Anim/Authored", recursive=False)):
    name = path.split("/")[-1].split(".")[0]
    if not name.startswith(only):
        continue
    anim = unreal.load_asset(path.split(".")[0])
    L = anim.get_play_length()
    dt = 1.0 / 60.0
    n = max(2, int(L / dt))
    ts = [i * L / (n - 1) for i in range(n)]
    L_, R_, root = [foot(anim, t, "ball_l") for t in ts], [foot(anim, t, "ball_r") for t in ts], [foot(anim, t, "root") for t in ts]
    d2 = lambda a, b: math.hypot(a[0] - b[0], a[1] - b[1])
    slow = root_travel = jump = jump_t = 0.0
    planted = 0
    for i in range(1, n):
        h = ts[i] - ts[i - 1]
        sl, sr = d2(L_[i], L_[i - 1]) / h, d2(R_[i], R_[i - 1]) / h
        m = min(sl, sr)
        slow += m * h
        if m < 30.0:
            planted += 1
        root_travel += d2(root[i], root[i - 1])
        if d2(root[i], root[i - 1]) > jump:
            jump, jump_t = d2(root[i], root[i - 1]), ts[i]
    zmin = min(min(p[2] for p in L_), min(p[2] for p in R_))
    zmax = max(max(p[2] for p in L_), max(p[2] for p in R_))
    unreal.log("TAG_PLANT %-36s len %.2f  root %4.1f m/s (path) %4.1f (end to end)  max step %3.0f cm at %.2f s  skate %3.0f%%  planted %3.0f%%  foot z %.0f..%.0f cm" % (
        name[6:], L, root_travel / 100.0 / max(L, 0.01), d2(root[-1], root[0]) / 100.0 / max(L, 0.01), jump, jump_t, 100.0 * slow / max(root_travel, 1.0), 100.0 * planted / (n - 1), zmin, zmax))
