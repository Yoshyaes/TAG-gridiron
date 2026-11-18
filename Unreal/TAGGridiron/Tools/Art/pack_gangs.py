# Unreal Editor Python (headless). The pack library's three-man gang tackles as groups the game can place: a carrier
# (__P01), the first tackler (__P02) and the man who joins him (__P03), all followed through the capture's shared world
# the way pack_pairs.py follows a pair (each man's anchor = his trajectory at frame 0, plus his root motion).
# The group's moment is the first tackler's contact (pelvises within 0.9 m). At that moment, in the carrier's frame
# (m; x left, y back; yaw deg relative to the carrier's heading, + turns left):
#   Tx, Ty, TYaw   the first tackler's root
#   Ax, Ay, AYaw   the second man's root (he is often still running in: AHit is when he reaches the carrier,
#                  seconds after Start, his pelvis within 0.9 m)
#   EndX, EndY     where the carrier's pelvis ends
#   Start          seconds into the captures of that moment
# Output: Content/TAG/AnimH/Pack/pack_gangs.json (TAG_ANIM_RIG=hero) for FAthleteAssets::LoadPackGangs.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import re

import unreal

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
HERO = os.environ.get("TAG_ANIM_RIG", "hero") == "hero"
SRC = os.path.join(PROJ, "RawArt", "Anim", "PackHero" if HERO else "Pack", "Tackles")
OUT = os.path.join(PROJ, "Content", "TAG", "AnimH" if HERO else "Anim", "Pack", "pack_gangs.json")
LIB = "/Game/TAG/AnimH/Pack/Tackles/" if HERO else "/Game/TAG/Anim/Pack/Tackles/"
opts = unreal.AnimPoseEvaluationOptions()
_skel = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_Skeleton" if HERO else "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin")
ROOT_REST_YAW = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_reference_pose(_skel), "root",
                                                        unreal.AnimPoseSpaces.LOCAL).rotation.rotator().yaw
WANT = ("Gang_Tackle", "GangTackle", "Gang_tackle")
SKIP = ("Strip", "Fumble", "Celeb", "Whiff", "Miss", "Facemask", "FaceMask", "HorseCollar", "Horsecollar")


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


def wrap180(a):
    return math.degrees(math.atan2(math.sin(math.radians(a)), math.cos(math.radians(a))))


def first_within(A, B, frm, d=0.9):
    n = min(len(A), len(B))
    for i in range(frm, n):
        if math.hypot(A[i][0][0] - B[i][0][0], A[i][0][1] - B[i][0][1]) < d:
            return i
    return None


groups = {}
for jpath in glob.glob(os.path.join(SRC, "A_TAG_Pack_*.json")):
    name = os.path.splitext(os.path.basename(jpath))[0]
    m = re.match(r"^(.*)__P(\d+)$", name)
    if not m or not any(w in name for w in WANT) or any(s in name for s in SKIP):
        continue
    groups.setdefault(m.group(1), {})[m.group(2)] = jpath
rows = []
for g, parts in sorted(groups.items()):
    if not all(k in parts for k in ("01", "02", "03")):
        continue
    try:
        js = {k: json.load(open(parts[k])) for k in ("01", "02", "03")}
        ns = {k: os.path.splitext(os.path.basename(parts[k]))[0] for k in ("01", "02", "03")}
        an = {k: unreal.load_asset(LIB + ns[k]) for k in ns}
        if not all(an.values()):
            continue
        C, LC = man(an["01"], js["01"])
        T, LT = man(an["02"], js["02"])
        A, LA = man(an["03"], js["03"])
        c0 = first_within(C, T, 0)
        if c0 is None:
            continue
        a0 = first_within(C, A, 0)
        if a0 is None:
            continue
        # the group starts at whichever man reaches the carrier first (that one is the "tackler")
        if a0 < c0:
            T, A, LT, LA, c0, a0 = A, T, LA, LT, a0, c0
            ns["02"], ns["03"] = ns["03"], ns["02"]
        t0 = c0 / 30.0
        if min(LC, LT, LA) - t0 < 0.8:
            continue
        cp, cr, ch = C[c0]
        _, tr_, th = T[c0]
        _, ar, ah = A[c0]
        tx, ty = rel(cr, ch, tr_)
        ax, ay = rel(cr, ch, ar)
        ex, ey = rel(cr, ch, C[-1][0])
        rows.append({"C": ns["01"], "T": ns["02"], "A": ns["03"], "Start": round(t0, 2), "AHit": round((a0 - c0) / 30.0, 2), "CMinZ": round(js["01"]["pelvis_z"]["min"], 3),
                     "Tx": round(tx, 2), "Ty": round(ty, 2), "TYaw": round(wrap180(th - ch), 1),
                     "Ax": round(ax, 2), "Ay": round(ay, 2), "AYaw": round(wrap180(ah - ch), 1),
                     "EndX": round(ex, 2), "EndY": round(ey, 2), "Len": round(min(LC, LT, LA), 2)})
    except Exception as e:
        unreal.log_warning("TAG_PACK_GANGS %s: %s" % (g, str(e)[:120]))
json.dump(rows, open(OUT, "w"), indent=0)
unreal.log("TAG_PACK_GANGS %d groups of %d -> %s" % (len(rows), len(groups), OUT))
for r in rows:
    unreal.log("TAG_PACK_GANGS %-58s T %5.2f %5.2f %6.1f | A %5.2f %5.2f %6.1f hit +%.2f | end %5.2f %5.2f | start %.2f" % (
        r["C"][11:69], r["Tx"], r["Ty"], r["TYaw"], r["Ax"], r["Ay"], r["AYaw"], r["AHit"], r["EndX"], r["EndY"], r["Start"]))
