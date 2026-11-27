# Unreal Editor Python: the capture's own pelvis speed at the game's lying frames (TAG_CAPDIR=<cap>)
# -> <cap>/lietruth.json [clip, t, game cm/s, capture cm/s]
import json, math, os, unreal

CAP = os.environ["TAG_CAPDIR"]
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "PackHero", "Tackles"); LIB = "/Game/TAG/AnimH/Pack/Tackles/"
opts = unreal.AnimPoseEvaluationOptions()
def world_of(anchor, h, xl, yb):
    h = math.radians(h); fwd = (math.sin(h), -math.cos(h)); left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * xl - fwd[0] * yb, anchor[1] + left[1] * xl - fwd[1] * yb)
starts = {}
for r in json.load(open(os.path.join(PROJ, "Content/TAG/AnimH/Pack/pack_pairs.json"))): starts[r["T"]] = starts[r["C"]] = r["Start"]
for r in json.load(open(os.path.join(PROJ, "Content/TAG/AnimH/Pack/pack_gangs.json"))): starts[r["T"]] = starts[r["C"]] = starts[r["A"]] = r["Start"]
jobs = json.load(open(os.path.join(CAP, "liejobs.json")))
cache = {}
out = []
for name, t, game in jobs:
    if name not in starts: continue
    if name not in cache:
        a = unreal.load_asset(LIB + name); j = json.load(open(os.path.join(SRC, name + ".json")))
        cache[name] = (a, j["source"]["anchor"], j["source"]["anchor_heading_deg"])
    a, anc, h0 = cache[name]
    def pel(tt):
        p = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(a, tt, opts), "pelvis", unreal.AnimPoseSpaces.WORLD).translation
        return world_of(anc, h0, p.x / 100, -p.y / 100)
    tt = starts[name] + t
    p1, p0 = pel(tt), pel(tt - 1 / 30.0)
    out.append((name, t, game, math.hypot(p1[0] - p0[0], p1[1] - p0[1]) * 100 * 30))
json.dump(out, open(os.path.join(CAP, "lietruth.json"), "w"))
