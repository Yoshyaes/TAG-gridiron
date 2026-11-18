# Unreal Editor Python (headless). Every two-man blocking capture of the pack library as a pair the game can place: the
# line play counterpart of pack_pairs.py (tackles). For each Blocking group with exactly a __P01 and a __P02, both men are
# followed through the capture's shared world (anchor + root motion, as in pack_pairs.py) and the pair is described from
# its first frame:
#   Ox, Oy   B (P02) in A's (P01's) frame (m; x left, y back); Yaw B's heading relative to A's (deg, + turns left)
#   FaceA/B  how squarely each man faces the other at the start (deg off the line between them; 0 = square)
#   D0, DMin, DMean, DMax   pelvis to pelvis distance through the capture (m)
#   TravelA/B  each man's root travel from start to end in his own start frame (x left, y back, m); PathA/B the length of
#              the root path (m); StepA/B the largest root step in a frame (cm; a tracking jump shows here)
#   EndOx, EndOy, EndYaw   the relation at the end (who got past whom)
#   ZMinA/B  lowest pelvis (m): who ends on the turf (a pancake, a cut block)
#   LenA/B   lengths (s)
# Output: Content/TAG/AnimH/Pack/pack_blocks.json (hero skeleton library only).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "PackHero", "Blocking")
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH", "Pack", "pack_blocks.json")
LIB = "/Game/TAG/AnimH/Pack/Blocking/"
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw


def bone(anim, t, b):
    return unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), b, unreal.AnimPoseSpaces.WORLD)


def clip_xy(tr):
    return tr.translation.x / 100.0, -tr.translation.y / 100.0


def world_of(anchor, heading_deg, x_left, y_back):
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


def man(anim, j):
    anc, h0 = j["source"]["anchor"], j["source"]["anchor_heading_deg"]
    L = anim.get_play_length()
    n = int(round(L * 30)) + 1
    rows = []
    for i in range(n):
        t = min(L, i / 30.0)
        r = bone(anim, t, "root")
        p = bone(anim, t, "pelvis")
        rx, ry = clip_xy(r)
        px, py = clip_xy(p)
        yaw = -(r.rotation.rotator().yaw - ROOT_REST_YAW)
        rows.append((world_of(anc, h0, px, py), world_of(anc, h0, rx, ry), h0 + yaw))
    return rows, L


def rel(p_from, h_from, p_to):
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


def wrap(a):
    return math.degrees(math.atan2(math.sin(math.radians(a)), math.cos(math.radians(a))))


def off_line(p_from, h_from, p_to):
    """Degrees between the man's heading and the line to the other man (0 = facing him squarely)."""
    x, y = rel(p_from, h_from, p_to)  # x left, y back: straight ahead is y < 0
    return abs(math.degrees(math.atan2(x, -y)))


groups = {}
for jpath in glob.glob(os.path.join(SRC, "A_TAG_Pack_*.json")):
    name = os.path.splitext(os.path.basename(jpath))[0]
    m = re.match(r"^(.*)__P(\d+)$", name)
    if m:
        groups.setdefault(m.group(1), {})[m.group(2)] = jpath
rows = []
for g, parts in sorted(groups.items()):
    if sorted(parts.keys()) != ["01", "02"]:
        continue
    try:
        ja, jb = json.load(open(parts["01"])), json.load(open(parts["02"]))
        na, nb = os.path.splitext(os.path.basename(parts["01"]))[0], os.path.splitext(os.path.basename(parts["02"]))[0]
        aa, ab = unreal.load_asset(LIB + na), unreal.load_asset(LIB + nb)
        if not aa or not ab:
            continue
        A, LA = man(aa, ja)
        B, LB = man(ab, jb)
        n = min(len(A), len(B))
        dist = [math.hypot(A[i][0][0] - B[i][0][0], A[i][0][1] - B[i][0][1]) for i in range(n)]
        (ap, ar, ah), (bp, br, bh) = A[0], B[0]
        ox, oy = rel(ar, ah, br)
        eox, eoy = rel(A[n - 1][1], A[n - 1][2], B[n - 1][1])

        def path(M):
            return sum(math.hypot(M[i][1][0] - M[i - 1][1][0], M[i][1][1] - M[i - 1][1][1]) for i in range(1, len(M)))

        def step(M):
            return max([math.hypot(M[i][1][0] - M[i - 1][1][0], M[i][1][1] - M[i - 1][1][1]) for i in range(1, len(M))] or [0.0]) * 100

        ta, tb = rel(ar, ah, A[-1][1]), rel(br, bh, B[-1][1])
        rows.append({"Name": g[len("A_TAG_Pack_"):], "A": na, "B": nb, "LenA": round(LA, 2), "LenB": round(LB, 2),
                     "Ox": round(ox, 2), "Oy": round(oy, 2), "Yaw": round(wrap(bh - ah), 1),
                     "FaceA": round(off_line(ap, ah, bp), 0), "FaceB": round(off_line(bp, bh, ap), 0),
                     "D0": round(dist[0], 2), "DMin": round(min(dist), 2), "DMean": round(sum(dist) / n, 2), "DMax": round(max(dist), 2),
                     "TravelA": [round(ta[0], 2), round(ta[1], 2)], "TravelB": [round(tb[0], 2), round(tb[1], 2)],
                     "PathA": round(path(A), 2), "PathB": round(path(B), 2), "StepA": round(step(A), 0), "StepB": round(step(B), 0),
                     "EndOx": round(eox, 2), "EndOy": round(eoy, 2), "EndYaw": round(wrap(B[n - 1][2] - A[n - 1][2]), 1),
                     "ZMinA": round(ja["pelvis_z"]["min"], 2), "ZMinB": round(jb["pelvis_z"]["min"], 2)})
    except Exception as e:
        unreal.log_warning("TAG_PACK_BLOCKS %s: %s" % (g, str(e)[:160]))
json.dump(rows, open(OUT, "w"), indent=0)
unreal.log("TAG_PACK_BLOCKS %d two-man blocking captures -> %s" % (len(rows), OUT))
