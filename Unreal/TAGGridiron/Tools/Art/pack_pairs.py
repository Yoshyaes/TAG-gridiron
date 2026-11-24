# Unreal Editor Python (headless). Every two-man tackle of the pack library as a paired fall the game can place
# (what Tools/Anim/mocap_pairs.py worked out for the five placed pairs, cut at the hit by hand). For each Tackles
# group of two (__P01, __P02; the carrier is whichever holds the ball, see carry()), both men are followed through the capture's shared world
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
# TAG_ANIM_RIG=hero: the hero-skeleton library (the captures on their own bodies: contacts measured at true size)
HERO = os.environ.get("TAG_ANIM_RIG", "") == "hero"
SRC = os.path.join(PROJ, "RawArt", "Anim", "PackHero" if HERO else "Pack", "Tackles")
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH" if HERO else "Anim", "Pack", "pack_pairs.json")
LIB = "/Game/TAG/AnimH/Pack/Tackles/" if HERO else "/Game/TAG/Anim/Pack/Tackles/"
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton" if HERO else "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw
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
    """Per frame: pelvis world position, root world position, heading (deg, + left), and each ball socket's distance
    from the chest (RightFootball, LeftFootball to spine_05, m: the carry test below)."""
    anc, h0 = j["source"]["anchor"], j["source"]["anchor_heading_deg"]
    L = anim.get_play_length()
    n = int(round(L * 30)) + 1
    rows = []
    for i in range(n):
        t = min(L, i / 30.0)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
        def at(b):
            return unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
        r = at("root")
        p = at("pelvis")
        rx, ry = clip_xy(r)
        px, py = clip_xy(p)
        # Unreal's yaw turns right; the clip frame's + turns left. Relative to the root's rest yaw (the hero rig's root
        # rests turned 180 deg; with the raw yaw every hero pair came out mirrored through the tackler, Ox / Oy negated)
        yaw = -(r.rotation.rotator().yaw - ROOT_REST_YAW)
        try:
            ch = at("spine_05").translation
            sock = tuple((at(s).translation - ch).length() / 100.0 for s in ("RightFootball", "LeftFootball"))
        except Exception:
            sock = (9.0, 9.0)
        rows.append((world_of(anc, h0, px, py), world_of(anc, h0, rx, ry), h0 + yaw, sock))
    return rows, L


def carry(M, i0, i1):
    """Carry score over frames [i0, i1) (m): a carried ball is a socket held still at the chest - min over hands of the
    mean + 1.5 std of its distance from spine_05. Carriers ~0.30-0.40; a man pumping his arms or reaching 0.5-0.8."""
    i1 = min(i1, len(M))
    i0 = max(0, min(i0, i1 - 4))
    best = 9.0
    for h in (0, 1):
        d = [M[i][3][h] for i in range(i0, i1)]
        if not d:
            continue
        mu = sum(d) / len(d)
        sd = math.sqrt(sum((x - mu) ** 2 for x in d) / len(d))
        best = min(best, mu + 1.5 * sd)
    return best


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
        # who has the ball: Madden's __P01 is not always the carrier (in 84 of 381 pairs it was __P02, and the game
        # played the tackler's capture on its carrier - Tools/Anim/pack_role_check.py). The carrier is the man whose
        # ball socket is held still at his chest up to the contact; no clear carrier (two linemen, a hit stick that
        # blows the ball loose) is no tackle of a ball carrier, and a capture whose ball ends with the tackler is out.
        w0, w1 = max(contact, 12), contact + 30
        sc, st = (carry(C, 0, w0), carry(C, 0, w1)), (carry(T, 0, w0), carry(T, 0, w1))
        if st[0] < sc[0] - 0.12 and st[0] < 0.50 and st[1] <= sc[1]:
            C, T, LC, LT, jc, jt, nc, nt = T, C, LT, LC, jt, jc, nt, nc
            sc, st = st, sc
        elif min(sc[0], st[0]) > 0.50 and min(sc[1], st[1]) > 0.50:
            continue
        elif sc[0] > st[0] + 0.06 and sc[0] > 0.42:
            continue
        if sc[0] < 0.42 and carry(T, n - 20, n) < 0.38 and carry(C, n - 20, n) > 0.55:
            continue
        (cp, cr, ch, _), (tp, tr_, th, _) = C[contact], T[contact]
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
        # how far apart the two stay over the 1.2 s after the contact (pelvises, m): a tackle keeps them together;
        # a hit stick blows them apart, a shoestring trip leaves the tackler at the carrier's feet, a jam is no tackle
        sep = sum(dist[contact:min(n, contact + 36)]) / max(1, len(dist[contact:min(n, contact + 36)]))
        # CMinZ: the carrier's lowest pelvis (m); a stand-up capture (held up, never goes down) is no tackle to the ground
        row = {"T": nt, "C": nc, "Kind": kind, "Sep": round(sep, 2), "CMinZ": round(jc["pelvis_z"]["min"], 3), "Ox": round(ox, 2), "Oy": round(oy, 2), "Yaw": round(yaw, 1),
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
