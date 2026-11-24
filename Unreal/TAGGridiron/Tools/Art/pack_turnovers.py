# Unreal Editor Python (headless). The pack library's turnover captures as data the game can place and time:
#   strips  - two-man strip tackles (Tackles / BrokenTackles, "Strip" in the name): the contact (first frame the pelvises
#             come within 0.9 m), the moment the ball comes out (the stripper's nearest hand at its closest to the
#             carrier's ball socket - the pack rig's RightFootball / LeftFootball joint of the carrying arm), which arm
#             carries, the punch's direction there (where the ball goes), and the pair's placement at the contact the way
#             pack_pairs.py records a tackle (Ox / Oy / Yaw, the carrier in the tackler's frame, x left, y back, m)
#   ints    - two-man interceptions (Catching, "INT" / "Int"): which man comes down with the ball (the one carrying it
#             after: a ball socket tucked to his chest), the catch (that man's hands closest together above 0.7 m while the
#             two are within 2.5 m), the receiver in the defender's frame there, both men's speeds into it and the ball's
#             place relative to the defender
#   dives   - captures of a man going down onto the turf with his hands low (ball-carrier dives, low diving catches): the
#             first frame both hands are under 30 cm with the pelvis under 70 cm (on the ball), and how far ahead of where
#             he started that is - a fumble recovery falls on the ball with one
# Output: Content/TAG/AnimH/Pack/pack_turnovers.json for FAthleteAssets.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
RAW = os.path.join(PROJ, "RawArt", "Anim", "PackHero")
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH", "Pack", "pack_turnovers.json")
LIB = "/Game/TAG/AnimH/Pack/"
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw
BONES = ("root", "pelvis", "hand_l", "hand_r", "spine_05", "RightFootball", "LeftFootball")


def world_of(anchor, heading_deg, x_left, y_back):
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


def rel(p_from, h_from, p_to):
    """p_to in the frame at p_from heading h_from (x left, y back)."""
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


def wrap180(a):
    return math.degrees(math.atan2(math.sin(math.radians(a)), math.cos(math.radians(a))))


def man(anim, j):
    """Per frame (30 Hz): each bone's capture-world position (x, y, z m) and the root's heading (deg, + left)."""
    anc, h0 = j["source"]["anchor"], j["source"]["anchor_heading_deg"]
    L = anim.get_play_length()
    n = int(round(L * 30)) + 1
    rows = []
    for i in range(n):
        t = min(L, i / 30.0)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
        row = {}
        for b in BONES:
            try:
                tr = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
            except Exception:
                continue
            x, y = tr.translation.x / 100.0, -tr.translation.y / 100.0
            wx, wy = world_of(anc, h0, x, y)
            row[b] = (wx, wy, tr.translation.z / 100.0)
            if b == "root":
                row["yaw"] = h0 - (tr.rotation.rotator().yaw - ROOT_REST_YAW)
        rows.append(row)
    return rows, L


def d2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def d3(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def mid(a, b):
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2)


def groups_in(cat, pred):
    g = {}
    for jpath in glob.glob(os.path.join(RAW, cat, "A_TAG_Pack_*.json")):
        name = os.path.splitext(os.path.basename(jpath))[0]
        m = re.match(r"^(.*)__P(\d+)$", name)
        if m and pred(name):
            g.setdefault((cat, m.group(1)), {})[m.group(2)] = jpath
    return g


def load_pair(cat, parts):
    js = {k: json.load(open(parts[k])) for k in parts}
    ns = {k: os.path.splitext(os.path.basename(parts[k]))[0] for k in parts}
    an = {k: unreal.load_asset(LIB + cat + "/" + ns[k]) for k in ns}
    return js, ns, an


def carry_hand(M, i0, i1):
    """Which ball socket is tucked to the chest over frames i0..i1 (share of frames each is within 32 cm)."""
    nr = nl = 0
    for i in range(i0, i1):
        r = M[i]
        if "RightFootball" not in r or "spine_05" not in r:
            continue
        if d3(r["RightFootball"], r["spine_05"]) < 0.32:
            nr += 1
        if d3(r["LeftFootball"], r["spine_05"]) < 0.32:
            nl += 1
    return nr, nl


def carry_score(M, i0, i1):
    """A ball held still at the chest (m): min over hands of the mean + 1.5 std of the socket's distance from spine_05
    over frames i0..i1 (carriers ~0.30-0.40, a man pumping his arms or reaching 0.5-0.8)."""
    best = 9.0
    for s in ("RightFootball", "LeftFootball"):
        d = [d3(M[i][s], M[i]["spine_05"]) for i in range(i0, i1) if s in M[i] and "spine_05" in M[i]]
        if len(d) < 4:
            continue
        mu = sum(d) / len(d)
        best = min(best, mu + 1.5 * math.sqrt(sum((x - mu) ** 2 for x in d) / len(d)))
    return best


# ------------------------------------------------------------------ strips
strips = []
for cat in ("Tackles", "BrokenTackles"):
    for (c, g), parts in sorted(groups_in(cat, lambda n: "strip" in n.lower()).items()):
        if sorted(parts) != ["01", "02"]:
            continue
        try:
            js, ns, an = load_pair(c, parts)
            if not all(an.values()):
                continue
            A, LA = man(an["01"], js["01"])
            B, LB = man(an["02"], js["02"])
            n = min(len(A), len(B))
            # the carrier is the man with a ball tucked at the start: held still at his chest (carry_score, the test of
            # pack_pairs.py / Tools/Anim/pack_role_check.py) when one man clearly is; else the share of frames tucked
            # (a sacked quarterback holds it in both hands in front: neither score is a carry there). The share alone
            # took the stripper for the carrier in 7 of 61 captures (a pumping arm passes the chest every stride).
            ra, la = carry_hand(A, 0, min(n, 12))
            rb, lb = carry_hand(B, 0, min(n, 12))
            sa, sb = carry_score(A, 0, min(n, 12)), carry_score(B, 0, min(n, 12))
            a_carries = sa < sb - 0.12 if min(sa, sb) < 0.50 and abs(sa - sb) > 0.12 else max(ra, la) >= max(rb, lb)
            if a_carries:
                C, T, kc, kt, LC, LT, cr, cl = A, B, "01", "02", LA, LB, ra, la
            else:
                C, T, kc, kt, LC, LT, cr, cl = B, A, "02", "01", LB, LA, rb, lb
            sock = "RightFootball" if cr >= cl else "LeftFootball"
            dist = [d2(C[i]["pelvis"], T[i]["pelvis"]) for i in range(n)]
            contact = next((i for i in range(n) if dist[i] < 0.9), None)
            if contact is None:
                contact = min(range(n), key=lambda i: dist[i])
            # the punch: the stripper's nearest hand at its closest to the ball socket
            best = (9.0, contact, "hand_r")
            for i in range(max(0, contact - 20), min(n, contact + 40)):
                for h in ("hand_l", "hand_r"):
                    dd = d3(T[i][h], C[i][sock])
                    if dd < best[0]:
                        best = (dd, i, h)
            dmin, iout, hand = best
            # the ball's way out: the punching hand's motion over the 3 frames after, in the carrier's frame at the punch
            j = min(n - 1, iout + 3)
            v = ((T[j][hand][0] - T[iout][hand][0]) * 10.0, (T[j][hand][1] - T[iout][hand][1]) * 10.0, (T[j][hand][2] - T[iout][hand][2]) * 10.0)
            ch = C[iout]["yaw"]
            pvx, pvy = rel((0.0, 0.0), ch, (v[0], v[1]))
            # the pair at the contact (as pack_pairs.py)
            ox, oy = rel(T[contact]["root"], T[contact]["yaw"], C[contact]["root"])
            yaw = wrap180(C[contact]["yaw"] - T[contact]["yaw"])
            trc = rel(C[contact]["root"], C[contact]["yaw"], C[-1]["root"])
            low = min(r["pelvis"][2] for r in C[contact:])
            outcome = "loses" if re.search(r"lose", g, re.I) else "keeps" if re.search(r"keep", g, re.I) else \
                "breaks" if re.search(r"break", g, re.I) else "misses" if re.search(r"miss|whiff", g, re.I) else "tackled"
            strips.append({"Name": g.replace("A_TAG_Pack_", ""), "Cat": c, "C": ns[kc], "T": ns[kt], "Sack": "Sack" in g, "Outcome": outcome,
                           "Start": round(contact / 30.0, 2), "Out": round(iout / 30.0, 2), "D": round(dmin, 2), "Hand": "R" if sock == "RightFootball" else "L",
                           "Punch": hand, "PopX": round(pvx, 2), "PopY": round(pvy, 2), "PopZ": round(v[2], 2),
                           "Ox": round(ox, 2), "Oy": round(oy, 2), "Yaw": round(yaw, 1), "TravelC": [round(trc[0], 2), round(trc[1], 2)],
                           "CMinZ": round(low, 3), "LenC": round(LC, 2), "LenT": round(LT, 2)})
        except Exception as e:
            unreal.log_warning("TAG_TURNOVERS strip %s: %s" % (g, str(e)[:160]))

# ------------------------------------------------------------------ interceptions
ints = []
for (c, g), parts in sorted(groups_in("Catching", lambda n: re.search(r"(_INT_|_Int_|INT_|Interception|_Int_Var)", n) is not None).items()):
    if sorted(parts) != ["01", "02"]:
        continue
    try:
        js, ns, an = load_pair(c, parts)
        if not all(an.values()):
            continue
        A, LA = man(an["01"], js["01"])
        B, LB = man(an["02"], js["02"])
        n = min(len(A), len(B))

        def catch_frame(M, O):
            best = (9.0, None)
            for i in range(int(n * 0.08), n):
                r = M[i]
                hz = (r["hand_l"][2] + r["hand_r"][2]) / 2
                if hz < 0.7 or d2(r["pelvis"], O[i]["pelvis"]) > 2.5:
                    continue
                gap = d3(r["hand_l"], r["hand_r"])
                if gap < best[0]:
                    best = (gap, i)
            return best

        ga, ia = catch_frame(A, B)
        gb, ib = catch_frame(B, A)
        if ia is None and ib is None:
            continue
        # the interceptor carries it after his catch
        def carry_after(M, i):
            if i is None:
                return -1
            r, l = carry_hand(M, min(n - 1, i + 8), n)
            return max(r, l) / max(1, n - i - 8)
        fa, fb = carry_after(A, ia), carry_after(B, ib)
        # (the share of frames tucked within 32 cm took the receiver for the interceptor in 2man_INT_Over_FaceQB_Got_em_v2
        # and others: a ball held still at the chest over 0.2-1.2 s after each man's catch decides when one man clearly
        # holds it - Tools/Anim/pack_role_check.py - else the share as before)
        def held_after(M, i):
            if i is None or min(n, i + 36) - (i + 6) < 8:
                return 9.0
            return carry_score(M, i + 6, min(n, i + 36))
        ha, hb = held_after(A, ia), held_after(B, ib)
        if min(ha, hb) < 0.50 and abs(ha - hb) > 0.12:
            fa, fb = (1.0, 0.0) if ha < hb else (0.0, 1.0)
        if fa >= fb:
            D, R, kd, kr, LD, LR, ic, gap, frac = A, B, "01", "02", LA, LB, ia, ga, fa
        else:
            D, R, kd, kr, LD, LR, ic, gap, frac = B, A, "02", "01", LB, LA, ib, gb, fb
        if ic is None:
            continue
        rx, ry = rel(D[ic]["root"], D[ic]["yaw"], R[ic]["root"])
        ball = mid(D[ic]["hand_l"], D[ic]["hand_r"])
        bx, by = rel(D[ic]["root"], D[ic]["yaw"], ball)
        i0, i1 = max(0, ic - 6), min(n - 1, ic + 1)
        dt = max(1, i1 - i0) / 30.0
        dv = d2(D[i1]["root"], D[i0]["root"]) / dt
        rv = d2(R[i1]["root"], R[i0]["root"]) / dt
        # the defender's direction of travel into the catch, relative to his facing (deg, + left)
        mx, my = rel(D[i0]["root"], D[ic]["yaw"], D[i1]["root"])
        ints.append({"Name": g.replace("A_TAG_Pack_", ""), "D": ns[kd], "R": ns[kr], "T": round(ic / 30.0, 2), "Gap": round(gap, 2), "Carry": round(frac, 2),
                     "Rx": round(rx, 2), "Ry": round(ry, 2), "RYaw": round(wrap180(R[ic]["yaw"] - D[ic]["yaw"]), 1),
                     "Bx": round(bx, 2), "By": round(by, 2), "Bz": round(ball[2], 2), "DSpeed": round(dv, 2), "RSpeed": round(rv, 2),
                     "DMoveDeg": round(math.degrees(math.atan2(mx, -my)) if (mx or my) else 0.0, 1), "LenD": round(LD, 2), "LenR": round(LR, 2)})
    except Exception as e:
        unreal.log_warning("TAG_TURNOVERS int %s: %s" % (g, str(e)[:160]))

# ------------------------------------------------------------------ dives onto the ball
dives = []
DIVE_RX = re.compile(r"(BC_Dive_Low|BC_Dive_Tap|BC_Dive_Forward|Catch_Run_0_Ankles_Fall|Dive45_.*_Knees|Dive45_.*_Waist|Catch_Dive_.*Low|RunDiveLow)")
for cat in ("BallCarrier", "Catching"):
    for jpath in sorted(glob.glob(os.path.join(RAW, cat, "A_TAG_Pack_*.json"))):
        name = os.path.splitext(os.path.basename(jpath))[0]
        if not DIVE_RX.search(name) or "2man" in name:
            continue
        try:
            anim = unreal.load_asset(LIB + cat + "/" + name)
            if not anim:
                continue
            M, L = man(anim, json.load(open(jpath)))
            n = len(M)
            touch = next((i for i in range(2, n) if max(M[i]["hand_l"][2], M[i]["hand_r"][2]) < 0.30 and M[i]["pelvis"][2] < 0.70), None)
            if touch is None:
                continue
            hands = mid(M[touch]["hand_l"], M[touch]["hand_r"])
            hx, hy = rel(M[0]["root"], M[0]["yaw"], hands)
            v0 = d2(M[min(n - 1, 3)]["root"], M[0]["root"]) * 10.0
            rest = next((i for i in range(touch, n) if M[i]["pelvis"][2] < 0.35), touch)
            dives.append({"Name": name, "Cat": cat, "Touch": round(touch / 30.0, 2), "Reach": round(-hy, 2), "Side": round(hx, 2), "V0": round(v0, 2),
                          "Down": round(rest / 30.0, 2), "Len": round(L, 2), "EndZ": round(M[-1]["pelvis"][2], 2)})
        except Exception as e:
            unreal.log_warning("TAG_TURNOVERS dive %s: %s" % (name, str(e)[:160]))

json.dump({"strips": strips, "ints": ints, "dives": dives}, open(OUT, "w"), indent=0)
unreal.log("TAG_TURNOVERS %d strips (%d lose the ball), %d interceptions, %d dives -> %s" % (
    len(strips), sum(1 for s in strips if s["Outcome"] == "loses"), len(ints), len(dives), OUT))
