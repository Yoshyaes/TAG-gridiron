# Unreal Editor Python (headless). Every two-man tackle of the pack library as a paired fall the game can place
# (what Tools/Anim/mocap_pairs.py worked out by hand for the five placed pairs): for each Tackles group with a carrier
# (__P01) and a tackler (__P02), at the carrier's hit frame (pack_index.json: the pelvis starting down):
#   Ox, Oy   the carrier in the tackler's frame (m; x left, y back), Yaw his heading relative to the tackler's (deg,
#            + turns left), from each man's anchor (his trajectory at frame 0, in the capture's shared world) and his
#            root motion up to the hit
#   EndX/Y   where the carrier's pelvis ends, in his own frame at the hit
#   Start    seconds into both captures the fall starts (the hit less 0.25 s)
#   Kind     Drive / Over (hit from the front: driven back / pitched forward), Behind, Side, SideL (from his left),
#            Dive, from the capture's direction code and where the carrier ends
# Output: Content/TAG/Anim/Pack/pack_pairs.json (read by FAthleteAssets; AMatchDirector::StartPairedTackle).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "Pack", "Tackles")
INDEX = os.path.join(PROJ, "Content", "TAG", "Anim", "Pack", "pack_index.json")
OUT = os.path.join(PROJ, "Content", "TAG", "Anim", "Pack", "pack_pairs.json")
LIB = "/Game/TAG/Anim/Pack/Tackles/"
opts = unreal.AnimPoseEvaluationOptions()
index = json.load(open(INDEX))
SKIP = ("Strip", "Whiff", "Miss", "Sack", "Pile", "Gang", "Sideline", "Celeb", "Fumble", "Brkn", "Broken", "Spin", "Hurdle", "Truck", "StiffArm", "Assist", "3Man", "Double")


def pose(anim, t, b):
    tr = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), b, unreal.AnimPoseSpaces.WORLD)
    return tr


def root_state(anim, t):
    """(x_left, y_back) in metres and yaw (deg, + left) of the root at t, in the capture's actor frame (Blender's:
    facing -Y; Unreal's import turns +Y forward, x left stays)."""
    tr = pose(anim, t, "root")
    x_left = tr.translation.x / 100.0
    y_back = -tr.translation.y / 100.0
    yaw = tr.rotation.rotator().yaw
    return x_left, y_back, -yaw  # Unreal's yaw turns right-handed about +Z looking down; the clip frame's + turns left


def world_of(anchor, heading_deg, x_left, y_back):
    """A point given in a man's frame-0 actor frame, in the capture world (mocap_pairs conventions)."""
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


pairs = []
groups = {}
for jpath in glob.glob(os.path.join(SRC, "A_TAG_Pack_*.json")):
    name = os.path.splitext(os.path.basename(jpath))[0]
    m = re.match(r"^(.*)__P(\d+)$", name)
    if not m or any(s in name for s in SKIP):
        continue
    groups.setdefault(m.group(1), {})[m.group(2)] = jpath
for g, parts in sorted(groups.items()):
    if "01" not in parts or "02" not in parts:
        continue
    try:
        jc, jt = json.load(open(parts["01"])), json.load(open(parts["02"]))
        nc, nt = os.path.splitext(os.path.basename(parts["01"]))[0], os.path.splitext(os.path.basename(parts["02"]))[0]
        rec = index.get(nc, {})
        hit = rec.get("hit")
        if hit is None:
            continue
        t_hit = hit / 30.0
        ac, at = unreal.load_asset(LIB + nc), unreal.load_asset(LIB + nt)
        if not ac or not at or at.get_play_length() < t_hit + 0.5:
            continue
        # each man at the hit, in the capture world
        cx, cy, cyaw = root_state(ac, t_hit)
        tx, ty, tyaw = root_state(at, t_hit)
        ancC, hC = jc["source"]["anchor"], jc["source"]["anchor_heading_deg"]
        ancT, hT = jt["source"]["anchor"], jt["source"]["anchor_heading_deg"]
        wc, wt = world_of(ancC, hC, cx, cy), world_of(ancT, hT, tx, ty)
        headT, headC = hT + tyaw, hC + cyaw
        h = math.radians(headT)
        fwd = (math.sin(h), -math.cos(h))
        left = (math.cos(h), math.sin(h))
        dx, dy = wc[0] - wt[0], wc[1] - wt[1]
        ox = dx * left[0] + dy * left[1]
        oy = -(dx * fwd[0] + dy * fwd[1])
        yaw = math.degrees(math.atan2(math.sin(math.radians(headC - headT)), math.cos(math.radians(headC - headT))))
        # the carrier's pelvis at his end, in his frame at the hit
        L = ac.get_play_length()
        pe = pose(ac, L, "pelvis").translation
        ex_l, ey_b = pe.x / 100.0, -pe.y / 100.0
        r = math.radians(-cyaw)
        rx = (ex_l - cx) * math.cos(r) - (ey_b - cy) * math.sin(r)
        ry = (ex_l - cx) * math.sin(r) + (ey_b - cy) * math.cos(r)
        if math.hypot(ox, oy) > 2.5:
            continue  # not in contact at the hit: a different kind of capture
        d = re.search(r"_D(\d)", g)
        code = int(d.group(1)) if d else 1
        if "Dive" in g:
            kind = "Dive"
        elif code in (1, 2, 8):
            kind = "Over" if ry < -0.3 else "Drive"
        elif code in (4, 5, 6):
            kind = "Behind"
        elif code == 3:
            kind = "Side"
        else:
            kind = "SideL"
        start = max(0.0, t_hit - 0.25)
        tl, cl = (0.70, 0.90) if kind == "Dive" else (1.05, 0.45) if kind == "Over" else (0.45, 1.05)
        pairs.append({"T": nt, "C": nc, "Kind": kind, "Ox": round(ox, 2), "Oy": round(oy, 2), "Yaw": round(yaw, 1),
                      "EndX": round(rx, 2), "EndY": round(ry, 2), "TLie": tl, "CLie": cl, "Start": round(start, 2),
                      "LenT": round(at.get_play_length(), 2), "LenC": round(L, 2)})
    except Exception as e:
        unreal.log_warning("TAG_PACK_PAIRS %s: %s" % (g, str(e)[:120]))
json.dump(pairs, open(OUT, "w"), indent=0)
kinds = {}
for p in pairs:
    kinds[p["Kind"]] = kinds.get(p["Kind"], 0) + 1
unreal.log("TAG_PACK_PAIRS %d pairs %s -> %s" % (len(pairs), kinds, OUT))
for p in pairs[:8]:
    unreal.log("TAG_PACK_PAIRS  %s %s ox %.2f oy %.2f yaw %.0f end (%.2f, %.2f) start %.2f" % (p["Kind"], p["T"][11:60], p["Ox"], p["Oy"], p["Yaw"], p["EndX"], p["EndY"], p["Start"]))
