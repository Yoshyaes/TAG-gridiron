# Unreal Editor Python (headless). The pack library's two-man QB-to-back handoffs as pairs the game can time: each
# capture's two parts followed through its shared world (the sidecar's anchor + root motion, as pack_dbl.py / pack_gangs.py
# do), and the exchange found where it happens - the moment one man's hands come nearest the other man's chest (that
# man is the quarterback, the other the back).
#   Q, B          the quarterback's part and the back's
#   T             seconds into the captures of the exchange
#   Bx, By, BYaw  the back's root in the quarterback's frame at T (m; x left, y back; yaw relative)
#   Gun           a shotgun / pistol handoff (else under center)
#   D             the closest the hands come to the chest (m: a real exchange is ~0.3)
# Output: Content/TAG/AnimH/Pack/pack_handoffs.json for FAthleteAssets (handoffs).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH", "Pack", "pack_handoffs.json")
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw
SKIP = ("Toss", "Option", "Opt_", "Veer", "Wildcat", "Reverse", "3Player", "Fake", "PA_", "Pitch", "Sweep", "Read", "Jet", "Boot", "Dropback")
# The formation captures (2026-11-21): the pack's run plays filmed from the snap, two men, named for the formation, the run
# and what the quarterback does after it ("Normal_UC_HB_Inside_Zone_RH2or4_TR_Dropback_v1": under center, inside zone to
# run hole 2 or 4, he turns right, and carries out a drop; "..._BootLT": a boot left). They have no "Handoff" in the
# name, so the first pass left out all ~110 of them - and they are the ones with the quarterback's whole footwork to the
# mesh in them. Play-action fakes ("_PA_", pack_pafakes.py), option reads, pop passes, tosses and sweeps stay out.
RUNS = ("_HB_", "_FB_Dive", "Inside_Zone", "InsideZone", "Outside_Zone", "OutsideZone", "Dive_Power", "_Counter_", "_PowerLt_", "_PowerRt_")
RUN_SKIP = ("_PA_", "PA_Setup", "_PA", "Option", "Opt_", "ReadOpt", "Read_Opt", "TripleOpt", "PopPass", "Toss", "Sweep", "Flip", "Wildcat", "Reverse", "3Player",
            "Fake", "Flea", "End_Around", "Jet", "WRDB", "2man", "Tackle", "Block")
RUN_CATS = ("Quarterback", "Catching", "BallCarrier", "Locomotion", "Misc")


def kind_of(name):
    # the run it is, for the play it is matched to (PlayRepHandoffFit)
    if "Draw" in name:
        return "draw"
    if "Stretch" in name or "Outside_Zone" in name or "OutsideZone" in name:
        return "stretch"
    return "inside"


def world_of(anchor, heading_deg, x_left, y_back):
    h = math.radians(heading_deg)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    return (anchor[0] + left[0] * x_left - fwd[0] * y_back, anchor[1] + left[1] * x_left - fwd[1] * y_back)


def clip_xy(tr):
    return tr.translation.x / 100.0, -tr.translation.y / 100.0


def man(anim, j):
    anc, h0 = j["source"]["anchor"], j["source"]["anchor_heading_deg"]
    L = anim.get_play_length()
    n = int(round(L * 30)) + 1
    rows = []
    for i in range(n):
        t = min(L, i / 30.0)
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
        get = lambda b: unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
        r = get("root")
        hl, hr, ch = get("hand_l"), get("hand_r"), get("spine_05")
        hx = (hl.translation.x + hr.translation.x) / 200.0
        hy = -(hl.translation.y + hr.translation.y) / 200.0
        cx, cy = clip_xy(ch)
        rx, ry = clip_xy(r)
        yaw = -(r.rotation.rotator().yaw - ROOT_REST_YAW)
        lx, ly = clip_xy(hl)
        qx, qy = clip_xy(hr)
        rows.append({"hands": world_of(anc, h0, hx, hy), "chest": world_of(anc, h0, cx, cy), "root": world_of(anc, h0, rx, ry), "yaw": h0 + yaw,
                     "hl": world_of(anc, h0, lx, ly), "hr": world_of(anc, h0, qx, qy), "hz": (hl.translation.z + hr.translation.z) / 200.0,
                     "hlz": hl.translation.z / 100.0, "hrz": hr.translation.z / 100.0})
    return rows, L


def rel(p_from, h_from, p_to):
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


def wrap180(a):
    return math.degrees(math.atan2(math.sin(math.radians(a)), math.cos(math.radians(a))))


groups = {}
for cat in ("Quarterback", "Catching"):
    for jpath in glob.glob(os.path.join(PROJ, "RawArt", "Anim", "PackHero", cat, "A_TAG_Pack_*.json")):
        name = os.path.splitext(os.path.basename(jpath))[0]
        m = re.match(r"^(.*)__P(\d+)$", name)
        if not m or "Handoff" not in name or any(s in name for s in SKIP):
            continue
        groups.setdefault((cat, m.group(1)), {})[m.group(2)] = jpath
for cat in RUN_CATS:
    for jpath in glob.glob(os.path.join(PROJ, "RawArt", "Anim", "PackHero", cat, "A_TAG_Pack_*.json")):
        name = os.path.splitext(os.path.basename(jpath))[0]
        m = re.match(r"^(.*)__P(\d+)$", name)
        if not m or "Handoff" in name or not any(s in name for s in RUNS) or any(s in name for s in RUN_SKIP):
            continue
        groups.setdefault((cat, m.group(1)), {})[m.group(2)] = jpath
rows = []
for (cat, g), parts in sorted(groups.items()):
    if sorted(parts) != ["01", "02"]:
        continue
    try:
        js = {k: json.load(open(parts[k])) for k in parts}
        ns = {k: os.path.splitext(os.path.basename(parts[k]))[0] for k in parts}
        an = {k: unreal.load_asset("/Game/TAG/AnimH/Pack/%s/%s" % (cat, ns[k])) for k in ns}
        if not all(an.values()):
            continue
        A, LA = man(an["01"], js["01"])
        B, LB = man(an["02"], js["02"])
        n = min(len(A), len(B))
        best = (9.0, 0, "01")
        for i in range(n):
            dab = math.dist(A[i]["hands"], B[i]["chest"])
            dba = math.dist(B[i]["hands"], A[i]["chest"])
            if dab < best[0]:
                best = (dab, i, "01")
            if dba < best[0]:
                best = (dba, i, "02")
        d, i, qk = best
        if d > 0.6:
            continue
        Q, R = (A, B) if qk == "01" else (B, A)
        bk = "02" if qk == "01" else "01"
        # The contact frame (2026-11-21): where the back's hands (both hand bones' mid point) come nearest the
        # quarterback's ball hand - the one of his two hands nearer the back's chest - in 3D, within 0.5 s of the moment
        # above. The game lands this frame on the sim's exchange (the hands-to-chest moment left the back's hands 30-100 cm
        # off the ball on the gun / pistol captures as it came, and the ball flew the gap).
        i0 = i
        bestc = (9.0, i0)
        for k in range(max(0, i0 - 15), min(n, i0 + 16)):
            bh = (R[k]["hands"][0], R[k]["hands"][1], R[k]["hz"])
            ql = (Q[k]["hl"][0], Q[k]["hl"][1], Q[k]["hlz"])
            qr = (Q[k]["hr"][0], Q[k]["hr"][1], Q[k]["hrz"])
            ball = ql if math.dist(Q[k]["hl"], R[k]["chest"]) < math.dist(Q[k]["hr"], R[k]["chest"]) else qr
            dc = math.dist(bh, ball)
            if dc < bestc[0]:
                bestc = (dc, k)
        dc, i = bestc
        # the closest the two bodies come (root to root, m) within 0.4 s of the contact: a capture whose men pass
        # through each other is no use however its hands meet
        minsep = min(math.dist(Q[k]["root"], R[k]["root"]) for k in range(max(0, i - 12), min(n, i + 13)))
        bx, by = rel(Q[i]["root"], Q[i]["yaw"], R[i]["root"])
        # how the two men travel around the exchange (m/s over the 0.2 s before it): a mesh the back runs through, the
        # quarterback near still - a check on which man is which and on the moment found
        j = max(0, i - 6)
        span = max(1, i - j) / 30.0
        vq = math.dist(Q[i]["root"], Q[j]["root"]) / span
        vb = math.dist(R[i]["root"], R[j]["root"]) / span
        # where the two start, the back around the quarterback (the same frame): a from-the-snap capture fits the play
        # whose backfield it was filmed from
        sx, sy = rel(Q[0]["root"], Q[0]["yaw"], R[0]["root"])
        rows.append({"Name": g.replace("A_TAG_Pack_", ""), "Cat": cat, "Q": ns[qk], "B": ns[bk], "T": round(i / 30.0, 2), "D": round(d, 2),
                     "Bx": round(bx, 2), "By": round(by, 2), "BYaw": round(wrap180(R[i]["yaw"] - Q[i]["yaw"]), 1),
                     "Gun": ("Shotgun" in g or "Pistol" in g or "SG" in g), "Len": round(min(LA, LB), 2),
                     "Kind": kind_of(g), "Snap": "Handoff" not in g, "Vq": round(vq, 2), "Vb": round(vb, 2), "Sx": round(sx, 2), "Sy": round(sy, 2),
                     "T0": round(i0 / 30.0, 2), "Dc": round(dc, 3), "MinSep": round(minsep, 2)})
    except Exception as e:
        unreal.log_warning("TAG_PACK_HANDOFFS %s: %s" % (g, str(e)[:160]))
# (the list the game was reading is kept beside it - pack_handoffs.prev.json - until the new one has been looked at on film)
if os.path.exists(OUT) and not os.path.exists(OUT.replace(".json", ".prev.json")):
    with open(OUT) as f_old, open(OUT.replace(".json", ".prev.json"), "w") as f_keep:
        f_keep.write(f_old.read())
json.dump(rows, open(OUT, "w"), indent=0)
unreal.log("TAG_PACK_HANDOFFS %d handoffs (%d from the snap)" % (len(rows), sum(1 for r in rows if r["Snap"])))
