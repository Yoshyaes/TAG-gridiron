# Unreal Editor Python (headless). Ground truth for the gang tackles (pack_gangs.json): each group's carrier-to-tackler and
# carrier-to-second-man pelvis distances (m) every frame from Start -> Saved/gang_truth.json. Helpers are pack_gangs.py's.
# --- pack_gangs.py header: the pack library's three-man gang tackles as groups the game can place: a carrier
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



rows = json.load(open(OUT))
out = {}
for r in rows:
    try:
        A = {}
        for k in ("C", "T", "A"):
            a = unreal.load_asset(LIB + r[k]); j = json.load(open(os.path.join(SRC, r[k] + ".json")))
            A[k], _ = man(a, j)
        s = int(round(r["Start"] * 30))
        n = min(len(A["C"]), len(A["T"]), len(A["A"]))
        d = lambda X, i: round(math.hypot(A["C"][i][0][0] - A[X][i][0][0], A["C"][i][0][1] - A[X][i][0][1]), 3)
        out[r["C"]] = {"T": [d("T", i) for i in range(s, n)], "A": [d("A", i) for i in range(s, n)]}
    except Exception as e:
        unreal.log_warning("TAG_GANG_TRUTH %s %s" % (r["C"], e))
json.dump(out, open(os.path.join(PROJ, "Saved", "gang_truth.json"), "w"))
