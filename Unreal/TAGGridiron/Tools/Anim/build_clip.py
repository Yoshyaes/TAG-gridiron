"""Blender runner: author one TAG clip, bake, measure, preview and export FBX.
    blender --background --factory-startup --python Tools/Anim/build_clip.py -- <rig.fbx> <clip> <out_dir> [--render]
Writes <out_dir>/A_TAG_<clip>.fbx, <out_dir>/A_TAG_<clip>.json (measurements) and preview PNGs.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clips  # noqa: E402
import clips_more  # noqa: E402
import tag_anim  # noqa: E402

import clips_more2  # noqa: E402
import clips_more3  # noqa: E402

clips.CLIPS.update(clips_more.MORE)
clips.CLIPS.update(clips_more2.MORE2)
clips.CLIPS.update(clips_more3.LINE)

argv = sys.argv[sys.argv.index("--") + 1:]
rig_fbx, name, out_dir = argv[0], argv[1], argv[2]
render = "--render" in argv

rig = tag_anim.Rig(rig_fbx)
f0, f1 = clips.CLIPS[name](rig)
rig.commit()
import bpy  # noqa: E402
ik_miss = {}
ik_frame = {}
for f in range(f0, f1 + 1, max(1, (f1 - f0) // 6)):
    bpy.context.scene.frame_set(f)
    for key, (mid, end, c, tgt, pole, cr) in rig.chains.items():
        m = round((rig.head(end) - tgt.matrix_world.translation).length, 4)
        if m > ik_miss.get(key, -1.0):
            ik_miss[key] = m
            ik_frame[key] = f
rig.bake(f0, f1)
rows = rig.measure(f0, f1)


def stat(key, i):
    v = [r[key][i] for r in rows]
    return {"min": round(min(v), 3), "max": round(max(v), 3), "start": round(v[0], 3), "end": round(v[-1], 3)}


slide = {}
for side in ("l", "r"):
    pts = [r["foot_" + side] for r in rows]
    # horizontal travel between consecutive frames where the ankle is down on both (a plant), so steps don't count
    slide[side] = round(max((math.hypot(a[0] - b[0], a[1] - b[1]) for a, b in zip(pts, pts[1:])
                             if a[2] < 0.105 and b[2] < 0.105), default=0.0), 4)
report = {"clip": name, "frames": [f0, f1], "seconds": round((f1 - f0) / 30.0, 3), "pole_angles": rig.pole_angles,
          "pelvis_z": stat("pelvis", 2), "head_z": stat("head", 2), "hand_r_z": stat("hand_r", 2), "shoulder_r": [round(v, 3) for v in rows[0]["upperarm_r"]], "hand_r": [round(v, 3) for v in rows[0]["hand_r"]],
          "max_planted_foot_step_m": slide, "ik_miss_m": ik_miss, "ik_miss_frame": ik_frame, "track_align": rig.track_align}
os.makedirs(out_dir, exist_ok=True)
fbx = os.path.join(out_dir, "A_TAG_%s.fbx" % name)
rig.export(fbx, f0, f1)
if render:
    rig.build_display()
    frames = sorted({f0, (f0 + f1) // 2, f1} | set(json.loads(os.environ.get("TAG_RENDER_FRAMES", "[]"))))
    report["previews"] = rig.render(frames, os.path.join(out_dir, "preview_" + name))
json.dump(report, open(os.path.join(out_dir, "A_TAG_%s.json" % name), "w"), indent=1)
print("TAG_CLIP", json.dumps(report)[:1500])
