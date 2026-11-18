# Unreal Editor Python: the capture's own overlap at the game's frames (TAG_CAPDIR=<cap>, after tackle_truth_prep.py)
# -> <cap>/ovtruth.json [clip, t, game core, capture core, game gap, capture gap]
import json, math, os, sys
import unreal
sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Tools", "Anim"))
from tackle_check import pair_metrics
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "PackHero", "Tackles")
LIB = "/Game/TAG/AnimH/Pack/Tackles/"
opts = unreal.AnimPoseEvaluationOptions()
BONES = ["pelvis", "spine_03", "neck_01", "head", "upperarm_l", "lowerarm_l", "hand_l", "upperarm_r", "lowerarm_r", "hand_r",
         "thigh_l", "calf_l", "foot_l", "ball_l", "thigh_r", "calf_r", "foot_r", "ball_r"]
def world_of(anchor, h, xl, yb):
    h = math.radians(h); fwd = (math.sin(h), -math.cos(h)); left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * xl - fwd[0] * yb, anchor[1] + left[1] * xl - fwd[1] * yb)
def body(a, j, t):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t, opts)
    anc, h0 = j["source"]["anchor"], j["source"]["anchor_heading_deg"]
    b = {}
    for n in BONES:
        tr = unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD).translation
        x, y = world_of(anc, h0, tr.x / 100.0, -tr.y / 100.0)
        b[n] = (x * 100.0, y * 100.0, tr.z)
    return {"b": b}
starts = {}
for r in json.load(open(os.path.join(PROJ, "Content/TAG/AnimH/Pack/pack_pairs.json"))): starts[r["T"]] = starts[r["C"]] = r["Start"]
for r in json.load(open(os.path.join(PROJ, "Content/TAG/AnimH/Pack/pack_gangs.json"))): starts[r["T"]] = starts[r["C"]] = starts[r["A"]] = r["Start"]
CAP = os.environ["TAG_CAPDIR"]

jobs = json.load(open(os.path.join(CAP, "ovjobs.json")))
cache = {}
def get(n):
    if n not in cache: cache[n] = (unreal.load_asset(LIB + n), json.load(open(os.path.join(SRC, n + ".json"))))
    return cache[n]
out = []
for a, b, t, gc, gg in jobs:
    if a not in starts: continue
    (aa, ja), (ab, jb) = get(a), get(b)
    tt = starts[a] + t
    g, c, l = pair_metrics(body(aa, ja, tt), body(ab, jb, tt))
    out.append((a, t, gc, c, gg, g))
json.dump(out, open(os.path.join(CAP, "ovtruth.json"), "w"))
