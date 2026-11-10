# Unreal Editor Python (headless). Every two-man tackle of the pack library as a paired fall the game can place
# (what Tools/Anim/mocap_pairs.py worked out for the five placed pairs, cut at the hit by hand). For each Tackles
# group with a carrier (__P01) and a tackler (__P02), both men are followed through the capture's shared world
# (each man's anchor = his trajectory at frame 0, plus his root motion; root x left, +Y forward in Unreal) and the
# contact is the first frame their pelvises come within 0.9 m after being apart. At the contact:
#   Ox, Oy   the carrier in the tackler's frame (m; x left, y back), Yaw his heading relative to the tackler's
#            (deg, + turns left); EndX/Y where the carrier's pelvis ends, in his frame at the contact
#   Start    seconds into both captures the fall starts (the contact)
#   Kind     Drive / Over (from the front: driven back / pitched forward), Behind, Side (from his right), SideL, Dive
#   LenT/C   the captures' lengths; TravelT/C (x left, y back, m) each man's root travel from Start to his end, the
#            fall driven by the root at runtime (AAthlete::BeginPairedFall with a start)
# Output: Content/TAG/Anim/Pack/pack_pairs.json. The five placed pairs' groups print beside GPairs for a check.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "Pack", "Tackles")
OUT = os.path.join(PROJ, "Content", "TAG", "Anim", "Pack", "pack_pairs.json")
LIB = "/Game/TAG/Anim/Pack/Tackles/"
opts = unreal.AnimPoseEvaluationOptions()
SKIP = ("Strip", "Whiff", "Miss", "Sack", "Pile", "Gang", "Sideline", "Celeb", "Fumble", "Brkn", "Broken", "Spin", "Hurdle", "Truck",
        "StiffArm", "Assist", "3Man", "Double", "Catch", "Drop", "Jump", "INT", "Int_", "Swat", "Knock", "Pitch", "on_Ground", "Juke")
CHECK = ("Tackle_D_Win_12_D1_HeadLeft", "Tackle_O_Win_12_D1_HeadLeft", "Tackle_Neu_12_D5_HeadLeft", "Tackle_Neu_12_D3_HeadLeft", "Tackle_Dive_Low_from_D5")


def bone(anim, t, b):
    tr = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), b, unreal.AnimPoseSpaces.WORLD)
    return tr


def clip_xy(tr):
    """Unreal clip space (x left, +Y forward) -> the capture's actor frame (x left, y back), metres."""
    return tr.translation.x / 100.0, -tr.translation.y / 100.0


def world_of(anchor, heading_deg, x_left, y_back):
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


def man(anim, j):
    """Per frame: pelvis world position, root world position, heading (deg, + left)."""
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
        yaw = -r.rotation.rotator().yaw  # Unreal's yaw turns right; the clip frame's + turns left
        rows.append((world_of(anc, h0, px, py), world_of(anc, h0, rx, ry), h0 + yaw))
    return rows, L


def rel(p_from, h_from, p_to):
    """p_to in the frame at p_from heading h_from (x left, y back)."""
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


pairs, checks = [], []
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
        ac, at = unreal.load_asset(LIB + nc), unreal.load_asset(LIB + nt)
        if not ac or not at:
            continue
        C, LC = man(ac, jc)
        T, LT = man(at, jt)
        n = min(len(C), len(T))
        dist = [math.hypot(C[i][0][0] - T[i][0][0], C[i][0][1] - T[i][0][1]) for i in range(n)]
        # the contact: the first frame within 0.9 m (a capture cut at the hit, like the placed ones, starts there)
        contact = None
        for i in range(n):
            if dist[i] < 0.9:
                contact = i
                break
        if contact is not None and min(dist[contact:min(n, contact + 15)]) > 0.9:
            contact = None  # a brush, not a tackle
        if contact is None:
            continue
        t0 = contact / 30.0
        if LT - t0 < 0.6 or LC - t0 < 0.6:
            continue
        (cp, cr, ch), (tp, tr_, th) = C[contact], T[contact]
        ox, oy = rel(tr_, th, cr)
        yaw = math.degrees(math.atan2(math.sin(math.radians(ch - th)), math.cos(math.radians(ch - th))))
        ex, ey = rel(cr, ch, C[-1][0])          # the carrier's pelvis at his end, in his frame at the contact
        trc = rel(cr, ch, C[-1][1])             # root travel from the contact to the end (carrier)
        trt = rel(tr_, th, T[-1][1])            # (tackler)
        d = re.search(r"_D(\d)", g)
        code = int(d.group(1)) if d else 1
        if "Dive" in g:
            kind = "Dive"
        elif code in (1, 2, 8):
            kind = "Over" if ey < -0.3 else "Drive"
        elif code in (4, 5, 6):
            kind = "Behind"
        elif code == 3:
            kind = "Side"
        else:
            kind = "SideL"
        tl, cl = (0.70, 0.90) if kind == "Dive" else (1.05, 0.45) if kind == "Over" else (0.45, 1.05)
        row = {"T": nt, "C": nc, "Kind": kind, "Ox": round(ox, 2), "Oy": round(oy, 2), "Yaw": round(yaw, 1),
               "EndX": round(ex, 2), "EndY": round(ey, 2), "TLie": tl, "CLie": cl, "Start": round(t0, 2),
               "LenT": round(LT, 2), "LenC": round(LC, 2), "TravelC": [round(trc[0], 2), round(trc[1], 2)], "TravelT": [round(trt[0], 2), round(trt[1], 2)]}
        pairs.append(row)
        if any(c in g for c in CHECK):
            checks.append(row)
    except Exception as e:
        unreal.log_warning("TAG_PACK_PAIRS %s: %s" % (g, str(e)[:120]))
json.dump(pairs, open(OUT, "w"), indent=0)
kinds = {}
for p in pairs:
    kinds[p["Kind"]] = kinds.get(p["Kind"], 0) + 1
unreal.log("TAG_PACK_PAIRS %d pairs %s -> %s" % (len(pairs), kinds, OUT))
unreal.log("TAG_PACK_PAIRS placed for comparison: Drive {-0.07,-0.62,151.3 | end -0.99,1.29} Over {0.01,-0.65,-77.7 | -1.93,-2.10} Behind {-0.05,-0.34,-9.8 | 1.93,-2.39} Side {-0.00,-0.38,-58.5 | 1.76,0.73} Dive {-0.04,-1.23,0 | 0.28,-5.08}")
for p in checks:
    unreal.log("TAG_PACK_PAIRS  %-6s %-44s ox %5.2f oy %5.2f yaw %6.1f | end %5.2f %5.2f | start %.2f travelC %s" % (p["Kind"], p["T"][11:55], p["Ox"], p["Oy"], p["Yaw"], p["EndX"], p["EndY"], p["Start"], p["TravelC"]))
