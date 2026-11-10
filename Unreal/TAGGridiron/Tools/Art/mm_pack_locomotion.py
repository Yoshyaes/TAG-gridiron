# Unreal Editor Python (headless). The pack library's Locomotion category (/Game/TAG/Anim/Pack/Locomotion, every
# locomotion capture of the pack on the rig) sorted into the motion matching sets by rule, so the databases draw on the
# whole pack and not only the hand-listed clips of mocap_map.LOCO_DB:
#   loops (MG_*, Loco_NonBC_15_*, Cycle_*, CarTurn*, Step_Loco_Cycle_*): by root speed into Walk / Run / Sprint, the
#     line's (Crouch, Dline, OL, shuffles) into Crouch; kept only when the feet plant (skate < 65 %, mm_clip_plant's
#     measure) and the root never steps more than 30 cm between frames
#   starts (Step_Loco_Start_*, Accel_*, Step_Loco_Boost_*, NonBC_Agile_Start*): by the speed they reach;
#     Step_Loco_Crouch_to_* into Crouch_Starts
#   stops (*_to_Stop*, *Decel*, MAD_loco_speedTrans*): by the speed they come from
#   pivots (the other Step_Loco_<a>_to_<b>): by their speed
# Captures already in the game's lists (the same source clip) are left to those; two-man reactions, fidgets, stands,
# falls, kick returns and the like are not locomotion and stay out. Writes Saved/pack_loco_lists.json, which
# mm_build_dbs.py merges into the database lists.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import json
import math
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Anim"))
import mocap_map  # noqa: E402

EAL = unreal.EditorAssetLibrary
LIB = "/Game/TAG/Anim/Pack/Locomotion"
OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "pack_loco_lists.json")
opts = unreal.AnimPoseEvaluationOptions()

SKIP = ("2man", "Reaction", "MidAir", "Fidget", "Idle", "Stand", "Defeat", "Dismay", "Frustrated", "RunOut", "ReturnToLocker",
        "Endzone", "TeamSelect", "Zone", "Boot", "Option", "TEST", "FIFAMG", "FI3OMAU1", "Fall", "Knockout", "Injury",
        "Flippable", "Almost", "Pissed", "mon_Man", "Pursuit", "FE_", "Smallsteps", "GrabFaceMask", "_idle", "Celebrat",
        "Handoff", "Pitch", "KickReturn", "mirrorphase", "redux")
CROUCH = ("Crouch", "Dline", "DL_", "OL_", "motionshuffle", "2pt", "3pt", "Shuffle", "PassBlock", "PASSBLOCK")


def norm(n):
    return re.sub(r"(_v\d+|_V\d+|_Var\d+)", "", n, flags=re.I).lower()


# the game's own lists: the same capture is not taken twice
listed = {norm(spec["src"]) for spec in mocap_map.LOCO.values()}


def root_at(anim, t):
    p = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), "root", unreal.AnimPoseSpaces.WORLD).translation
    return (p.x, p.y)


def foot_at(anim, t, b):
    p = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), b, unreal.AnimPoseSpaces.WORLD).translation
    return (p.x, p.y)


def measure(anim):
    L = anim.get_play_length()
    n = max(3, int(L * 60))
    ts = [i * L / (n - 1) for i in range(n)]
    R = [root_at(anim, t) for t in ts]
    d2 = lambda a, b: math.hypot(a[0] - b[0], a[1] - b[1])
    path = sum(d2(R[i], R[i - 1]) for i in range(1, n))
    step = max(d2(R[i], R[i - 1]) for i in range(1, n))
    first = d2(R[min(n - 1, 30)], R[0]) / max(ts[min(n - 1, 30)], 0.01)
    last = d2(R[-1], R[max(0, n - 31)]) / max(L - ts[max(0, n - 31)], 0.01)
    # skate: the slower foot's travel over the root's (sampled every other step to keep it quick)
    slow = 0.0
    for i in range(2, n, 2):
        h = ts[i] - ts[i - 2]
        sl = d2(foot_at(anim, ts[i], "ball_l"), foot_at(anim, ts[i - 2], "ball_l")) / h
        sr = d2(foot_at(anim, ts[i], "ball_r"), foot_at(anim, ts[i - 2], "ball_r")) / h
        slow += min(sl, sr) * h
    return dict(len=L, speed=path / max(L, 0.01), step=step, first=first, last=last, skate=slow / max(path, 1.0))


def gait(speed):
    return "Sprint" if speed > 640 else "Run" if speed > 320 else "Walk"


lists = {}
report = []
for path in sorted(EAL.list_assets(LIB, recursive=False, include_folder=False)):
    asset = path.split(".")[0]
    name = asset.rsplit("/", 1)[1][11:]  # after A_TAG_Pack_
    if any(s in name for s in SKIP):
        continue
    if norm(name) in listed or norm(re.sub(r"__P\d+$", "", name) + "__P01") in listed:
        continue
    anim = unreal.load_asset(asset)
    if not isinstance(anim, unreal.AnimSequence):
        continue
    m = measure(anim)
    role = None
    low = name.lower()
    crouch = any(c.lower() in low for c in CROUCH)
    if m["step"] > 30:
        report.append((name, "step %.0f cm" % m["step"]))
        continue
    if "Step_Loco_Crouch_to" in name:
        role = "Crouch_Starts"
    elif re.search(r"Step_Loco_Start|^Accel_|Step_Loco_Boost|NonBC_Agile_Start", name):
        role = gait(m["last"]) + "_Starts"
    elif re.search(r"to_Stop|Decel|speedTrans", name, re.I):
        role = gait(m["first"]) + "_Stops"
    elif re.search(r"^Step_Loco_.*_to_", name):
        role = gait(max(m["first"], m["last"])) + "_Pivots"
    elif re.search(r"^MG_|^Loco_NonBC_15_|^Cycle_|^CarTurn|^Carturn|^Step_Loco_Cycle|Sprint_D1", name):
        if m["skate"] >= 0.65 or m["speed"] < 60:
            report.append((name, "skates %.0f%% at %.0f cm/s" % (100 * m["skate"], m["speed"])))
            continue
        role = "Crouch" if crouch else gait(m["speed"])
    if role is None:
        continue
    lists.setdefault(role, []).append(asset)
    report.append((name, "%s %.0f cm/s skate %.0f%%" % (role, m["speed"], 100 * m["skate"])))
json.dump(lists, open(OUT, "w"), indent=1)
unreal.log("TAG_PACK_LOCO %s" % {k: len(v) for k, v in lists.items()})
for n, r in report:
    unreal.log("TAG_PACK_LOCO  %-70s %s" % (n[:70], r))
