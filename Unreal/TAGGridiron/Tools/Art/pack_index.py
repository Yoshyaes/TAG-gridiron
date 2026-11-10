# Unreal Editor Python (headless). The pack library's index: one record per imported capture with what the game needs
# to start a whole capture at the right moment (the placed clips were cut by hand to these moments):
#   len            seconds
#   release        right hand's peak-speed frame (a throw's release, a catch's reach)
#   contact        right foot's peak-speed frame (a kick's contact)
#   speed          root speed over the moving part, cm/s (drops, locomotion)
#   hit            frame the body starts going down (tackled carriers, broken tackles): the last frame before the
#                  pelvis' lowest at which it still stands above 80 % of its starting height; sampled here
#   onset          frame the root first moves sideways faster than 1.2 m/s (jukes, spins, cuts); sampled here
#   pelvis_start/end, travel: from the Blender build
# Blender measurements come from RawArt/Anim/Pack/<Category>/*.json (build_pack_library.py); hit and onset are sampled
# in Unreal for the categories that need them. Output: Content/TAG/Anim/Pack/pack_index.json (read by FPackIndex).
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
import glob
import json
import math
import os
import time

import unreal

EAL = unreal.EditorAssetLibrary
PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SRC = os.path.join(PROJ, "RawArt", "Anim", "Pack")
OUT = os.path.join(PROJ, "Content", "TAG", "Anim", "Pack", "pack_index.json")
SAMPLED = ("Tackles", "BrokenTackles", "BallCarrier", "Catching", "Blocking")
opts = unreal.AnimPoseEvaluationOptions()


def bone(anim, t, b):
    p = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts), b, unreal.AnimPoseSpaces.WORLD).translation
    return (p.x, p.y, p.z)


def sample(anim):
    """hit and onset frames (30 fps) from the pelvis height and the root's sideways speed."""
    L = anim.get_play_length()
    n = max(2, int(round(L * 30)) + 1)
    ts = [i / 30.0 for i in range(n)]
    pel = [bone(anim, t, "pelvis") for t in ts]
    root = [bone(anim, t, "root") for t in ts]
    zs = [p[2] for p in pel]
    z0 = max(zs[:5])
    imin = zs.index(min(zs))
    hit = None
    if min(zs) < 0.6 * z0:
        hit = 0
        for i in range(imin, -1, -1):
            if zs[i] > 0.8 * z0:
                hit = i
                break
    onset = None
    # the root's heading is +Y forward on these clips; sideways = x
    for i in range(1, n):
        vx = abs(root[i][0] - root[i - 1][0]) * 30.0
        if vx > 120.0:
            onset = i
            break
    return hit, onset


index = {}
t0 = time.time()
count = sampled = 0
for jpath in sorted(glob.glob(os.path.join(SRC, "*", "A_TAG_Pack_*.json"))):
    cat = os.path.basename(os.path.dirname(jpath))
    try:
        j = json.load(open(jpath))
    except Exception:
        continue
    name = os.path.splitext(os.path.basename(jpath))[0]
    ev = j.get("events", {})
    rec = {"cat": cat, "len": j.get("seconds", 0.0),
           "release": ev.get("hand_r_peak_speed", {}).get("frame"),
           "release_ms": ev.get("hand_r_peak_speed", {}).get("m_s"),
           "contact": ev.get("foot_r_peak_speed", {}).get("frame"),
           "contact_ms": ev.get("foot_r_peak_speed", {}).get("m_s"),
           "pelvis_min": ev.get("pelvis_min_frame"),
           "hands_up": ev.get("hand_r_max_z_frame"),
           "pz0": (j.get("pelvis_z") or {}).get("start"), "pz1": (j.get("pelvis_z") or {}).get("end"),
           "pzmin": (j.get("pelvis_z") or {}).get("min"),
           "travel": j.get("traj_travel_m"), "moving": j.get("traj_moving_frames")}
    if rec["travel"] and rec["moving"]:
        rec["speed"] = round(100.0 * rec["travel"] / (rec["moving"] / 30.0), 1)
    if cat in SAMPLED:
        asset = "/Game/TAG/Anim/Pack/%s/%s" % (cat, name)
        if EAL.does_asset_exist(asset):
            anim = unreal.load_asset(asset)
            try:
                hit, onset = sample(anim)
                rec["hit"], rec["onset"] = hit, onset
            except Exception as e:
                rec["sample_error"] = str(e)[:80]
            sampled += 1
            if sampled % 500 == 0:
                unreal.log("TAG_PACK_INDEX sampled %d (%.0f s)" % (sampled, time.time() - t0))
                unreal.SystemLibrary.collect_garbage()
    index[name] = {k: v for k, v in rec.items() if v is not None}
    count += 1
os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(index, open(OUT, "w"), separators=(",", ":"))
unreal.log("TAG_PACK_INDEX done %d records (%d sampled) in %.0f s -> %s" % (count, sampled, time.time() - t0, OUT))
