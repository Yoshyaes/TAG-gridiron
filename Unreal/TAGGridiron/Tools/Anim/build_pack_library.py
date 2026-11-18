"""Blender batch runner: retarget every clip of the mocap pack onto the UEFN rig, whole, with root motion, as a library.
    blender --background --factory-startup --python Tools/Anim/build_pack_library.py -- <rig.fbx> <out_dir> <names.txt>
                                                                                      [--shard i/n] [--limit k]
One Blender process, one rig import and one calibration for the whole list (the per-clip runner build_mocap.py starts
Blender and calibrates per clip; over 12,000 clips that is the difference between hours and days). <names.txt> holds
manifest clip names, one per line; with --shard i/n the process takes every n-th name from the i-th. Each clip becomes
<out_dir>/<Category>/A_TAG_Pack_<name>.fbx + .json (name sanitized for Unreal), skipped when the fbx already exists
(reruns resume). A clip whose skeleton the retargeter cannot read (no Hips / AITrajectory: props, cameras, additive
layers) is logged and skipped. Prints one TAG_PACK line per clip; Tools/Anim/build_pack_library.sh runs the shards.
"""
import json
import os
import re
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402

import mocap_retarget as MR  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
rig_fbx, out_dir, names_file = argv[0], argv[1], argv[2]
shard_i, shard_n = (0, 1)
if "--shard" in argv:
    shard_i, shard_n = [int(x) for x in argv[argv.index("--shard") + 1].split("/")]
limit = int(argv[argv.index("--limit") + 1]) if "--limit" in argv else None

names = [l.strip() for l in open(names_file, encoding="utf-8") if l.strip()]
all_names = list(names)
names = names[shard_i::shard_n]
if limit:
    names = names[:limit]


def asset_name(clip):
    # sanitized for Unreal; two manifest names that differ only in punctuation get the second a numbered suffix
    n = re.sub(r"[^A-Za-z0-9_]+", "_", clip).strip("_")
    if len(n) > 96:  # Unreal's package path limit: the longest capture names keep a hash of their tail
        import hashlib
        n = n[:88] + "_" + hashlib.md5(n.encode()).hexdigest()[:7]
    if n in TAKEN and TAKEN[n] != clip:
        k = 2
        while "%s_%d" % (n, k) in TAKEN and TAKEN["%s_%d" % (n, k)] != clip:
            k += 1
        n = "%s_%d" % (n, k)
    TAKEN[n] = clip
    return "A_TAG_Pack_" + n


TAKEN = {}


bpy.ops.wm.read_factory_settings(use_empty=True)
target = MR.Target(rig_fbx)
rt = MR.Retargeter(target)
man = MR.manifest()
for c in all_names:  # fixed asset names whatever the shard
    asset_name(c)
done = skipped = failed = 0
t_start = time.time()
for k, clip in enumerate(names):
    row = man.get(clip)
    cat = (row or {}).get("category", "Misc") or "Misc"
    cat = re.sub(r"[^A-Za-z0-9_]+", "_", cat)
    name = asset_name(clip)
    folder = os.path.join(out_dir, cat)
    fbx = os.path.join(folder, name + ".fbx")
    if os.path.exists(fbx):
        skipped += 1
        continue
    t0 = time.time()
    try:
        if row is None:
            raise KeyError("not in the manifest")
        # the four additive delta layers (Pose_Accel, Pose_Fatigue, head tracking) retarget like any clip; Unreal marks
        # them additive on import (import_pack_library.py)
        spec = {"src": clip, "mode": "root"}
        n_out, rows, info = MR.build(target, rt, spec)
        os.makedirs(folder, exist_ok=True)
        MR.export(target, fbx, n_out)
        meta = {"clip": clip, "asset": name, "category": cat, "frames": [0, n_out - 1],
                "seconds": round((n_out - 1) / float(MR.FPS_OUT), 3), "source": info, "spec": {"mode": "root"}}
        meta.update(MR.measure(rows))
        json.dump(meta, open(os.path.join(folder, name + ".json"), "w"), indent=1)
        done += 1
        print("TAG_PACK ok %s -> %s/%s %.2fs (%.1fs)" % (clip, cat, name, meta["seconds"], time.time() - t0), flush=True)
    except Exception as e:  # keep going: one bad capture must not stop the shard
        failed += 1
        print("TAG_PACK fail %s: %s" % (clip, str(e).replace("\n", " ")[:200]), flush=True)
        if not isinstance(e, (KeyError, ValueError)):
            print(traceback.format_exc()[-600:], flush=True)
    finally:
        # the next clip starts from a clean rig: the pose action and any leftovers of the source import
        a = target.arm
        a.animation_data_clear()
        for act in list(bpy.data.actions):
            bpy.data.actions.remove(act)
        for o in list(bpy.data.objects):
            if o != a:
                bpy.data.objects.remove(o)
        for coll in (bpy.data.armatures, bpy.data.meshes, bpy.data.materials):
            for d in list(coll):
                if d.users == 0:
                    coll.remove(d)
        bpy.context.view_layer.objects.active = a
        a.select_set(True)
        try:
            bpy.ops.object.mode_set(mode="OBJECT")
        except RuntimeError:
            pass
        a.location = (0.0, 0.0, 0.0)
        a.rotation_euler = (0.0, 0.0, 0.0)
        for pb in a.pose.bones:
            pb.matrix_basis = MR.Matrix.Identity(4)
print("TAG_PACK_DONE shard %d/%d done %d skipped %d failed %d in %.0f s" % (shard_i, shard_n, done, skipped, failed, time.time() - t_start), flush=True)
