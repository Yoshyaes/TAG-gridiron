# Unreal Editor Python (headless). The pack library's two-man tosses (the quarterback pitches it to a back running for
# the edge: Handoff_Shotgun_*_Toss_*, Handoff_Pistol_Toss_*, Handoff_SingleBack_I_Form_QB_Toss_*) as pairs the game can
# time (arm sweep 3). Each capture's two parts followed through their shared world (the sidecar's anchor + root motion,
# as pack_handoffs.py does):
#   Q, B          the quarterback's part (the man who travels least) and the back's
#   Tr            seconds into the captures of the release: the quarterback's ball hand at its fastest
#   Tc            seconds of the back's catch: after the release, his hands together and slowest (as the game's catch
#                 contact frames, Athlete.cpp CatchContactT)
#   Bx, By        the back's root in the quarterback's frame at the release (m; x left, y back)
#   Gun, Left     a shotgun / pistol toss (else under center); pitched to the quarterback's left
# Output: Content/TAG/AnimH/Pack/pack_tosses.json for FAthleteAssets (RepTosses).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH", "Pack", "pack_tosses.json")
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw
# (a toss with a play-action fake after it or a dropback - the quarterback's part goes on into a pass set - stays out)
SKIP = ("_PA_", "PA_Setup", "Dropback", "Celeb", "Coin", "Getup", "GetUp", "TossArms", "TossBall", "TossesBall", "LightToss")


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
        r, hl, hr = get("root"), get("hand_l"), get("hand_r")
        rx, ry = clip_xy(r)
        lx, ly = clip_xy(hl)
        qx, qy = clip_xy(hr)
        rows.append({"root": world_of(anc, h0, rx, ry), "yaw": h0 - (r.rotation.rotator().yaw - ROOT_REST_YAW),
                     "hl": world_of(anc, h0, lx, ly) + (hl.translation.z / 100.0,), "hr": world_of(anc, h0, qx, qy) + (hr.translation.z / 100.0,)})
    return rows, L


def rel(p_from, h_from, p_to):
    h = math.radians(h_from)
    fwd = (math.sin(h), -math.cos(h))
    left = (math.cos(h), math.sin(h))
    dx, dy = p_to[0] - p_from[0], p_to[1] - p_from[1]
    return dx * left[0] + dy * left[1], -(dx * fwd[0] + dy * fwd[1])


groups = {}
for cat in ("Quarterback", "Catching", "BallCarrier", "Misc"):
    for jpath in glob.glob(os.path.join(PROJ, "RawArt", "Anim", "PackHero", cat, "A_TAG_Pack_*.json")):
        name = os.path.splitext(os.path.basename(jpath))[0]
        m = re.match(r"^(.*)__P(\d+)$", name)
        if not m or "Toss" not in name or any(s in name for s in SKIP):
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
        qk = min(parts, key=lambda k: js[k].get("traj_travel_m", 0.0))
        bk = "02" if qk == "01" else "01"
        Q, LQ = man(an[qk], js[qk])
        R, LB = man(an[bk], js[bk])
        n = min(len(Q), len(R))
        sp = lambda rows_, k, h: math.dist(rows_[k][h], rows_[k - 1][h]) * 30.0
        # the release: the quarterback's faster hand at its fastest, in the first 1.6 s
        best = (0.0, 1)
        for k in range(1, min(n, 49)):
            v = max(sp(Q, k, "hl"), sp(Q, k, "hr"))
            if v > best[0]:
                best = (v, k)
        vr, ir = best
        # the catch: the back's hands closest together and slowest, 0.15-0.8 s after the release
        bc = (1e9, ir + 6)
        for k in range(min(n - 1, ir + 4), min(n, ir + 25)):
            gap = math.dist(R[k]["hl"], R[k]["hr"])
            s = gap * 100.0 + 1.5 * (sp(R, k, "hl") + sp(R, k, "hr")) * 100.0 / 30.0
            if s < bc[0]:
                bc = (s, k)
        ic = bc[1]
        bx, by = rel(Q[ir]["root"], Q[ir]["yaw"], R[ir]["root"])
        gun = "Shotgun" in g or "Pistol" in g
        left = "Toss_Left" in g or "_Lt_" in g
        rows.append({"Name": g.replace("A_TAG_Pack_", ""), "Cat": cat, "Q": ns[qk], "B": ns[bk], "Tr": round(ir / 30.0, 3), "Tc": round(ic / 30.0, 3),
                     "Vr": round(vr, 2), "Bx": round(bx, 2), "By": round(by, 2), "Gun": gun, "Left": left, "Len": round(min(LQ, LB), 2),
                     "Gap": round(math.dist(R[ic]["hl"], R[ic]["hr"]), 2), "Fly": round(math.dist(Q[ir]["root"], R[ic]["root"]), 2)})
    except Exception as e:
        unreal.log_warning("TAG_PACK_TOSSES %s: %s" % (g, str(e)[:160]))
json.dump(rows, open(OUT, "w"), indent=0)
unreal.log("TAG_PACK_TOSSES %d tosses" % len(rows))
for r in rows:
    unreal.log("TAG_PACK_TOSSES %s release %.2f catch %.2f (hand %.1f m/s, back %.1f left %.1f back at release, gap %.2f m)" %
               (r["Name"], r["Tr"], r["Tc"], r["Vr"], r["Bx"], r["By"], r["Gap"]))
