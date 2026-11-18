"""Timeline of a pack clip (or a whole group), to pick start / end / anchor times for mocap_map.py.
    blender --background --factory-startup --python Tools/Anim/mocap_probe.py -- <clip> [<clip> ...] [--step 0.1]
Per step prints: t, hips z, head z, right hand speed, right foot speed, hands apart (ball security < 0.3 m), and for a
clip whose group has two men the pelvis distance to the other man, his bearing in this man's hip frame (deg, 0 ahead,
+ left) and whether this man faces him. Names resolve like mocap_map (version suffix optional).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import mocap_retarget as MR  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
step = float(argv[argv.index("--step") + 1]) if "--step" in argv else 0.1
clips = [a for i, a in enumerate(argv) if not a.startswith("--") and (i == 0 or argv[i - 1] != "--step")]
bpy.ops.wm.read_factory_settings(use_empty=True)
man = MR.manifest()
for clip in clips:
    row = MR.resolve_clip(clip)
    others = [r for r in man.values() if r["group"] == row["group"] and r["clip"] != row["clip"]]
    src = MR.Source(os.path.join(MR.PACK, row["fbx"]))
    other = MR.Source(os.path.join(MR.PACK, others[0]["fbx"])) if others else None
    fps = src.fps
    print("== %s  (%d frames @ %g fps, %.2f s%s)" % (row["clip"], src.f1 - src.f0 + 1, fps, (src.f1 - src.f0) / fps,
                                                    ", with " + others[0]["clip"] if others else ""))
    prev = None
    t = 0.0
    hips0 = None
    while True:
        f = src.f0 + t * fps
        if f > src.f1 + 1e-6:
            break
        src.seek(f)
        hips = src.world("Hips").to_translation()
        if hips0 is None:
            hips0 = hips.copy()
        head = src.world("Head").to_translation()
        hr = src.world("RightHand").to_translation()
        fr = src.world("RightFoot").to_translation()
        hl = src.world("LeftHand").to_translation()
        hips_rot = src.world("Hips").to_3x3().normalized()
        fwd = hips_rot.col[1]
        left = hips_rot.col[2]
        line = "  t=%4.1f hips %.2f head %.2f disp %.2f" % (t, hips.z, head.z, Vector((hips.x - hips0.x, hips.y - hips0.y, 0)).length)
        if prev is not None:
            dt = t - prev[0]
            line += "  handR %5.1f m/s footR %5.1f m/s" % ((hr - prev[1]).length / dt, (fr - prev[2]).length / dt)
        else:
            line += "  handR   -   footR   -    "
        line += "  hands %.2f" % (hr - hl).length
        if other:
            oh = other.world("Hips").to_translation()
            d = oh - hips
            dist = Vector((d.x, d.y, 0)).length
            bearing = math.degrees(math.atan2(d.dot(left), d.dot(fwd)))
            line += "  other %.2f m at %4.0f deg, his hips %.2f" % (dist, bearing, oh.z)
        print(line)
        prev = (t, hr, fr)
        t += step
    bpy.data.objects.remove(src.arm)
    if other:
        bpy.data.objects.remove(other.arm)
