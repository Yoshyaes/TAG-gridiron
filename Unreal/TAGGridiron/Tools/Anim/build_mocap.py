"""Blender runner: retarget one mocap-pack clip onto the UEFN rig as a TAG clip and export it.
    blender --background --factory-startup --python Tools/Anim/build_mocap.py -- <rig.fbx> <clip> <out_dir> [--render]
                                                                               [--frames f0,f1,...]
<clip> is a key of mocap_map.MAP (the game's clip name without A_TAG_). Writes <out_dir>/A_TAG_<clip>.fbx and
<out_dir>/A_TAG_<clip>.json (the same measurement fields Tools/Anim/build_clip.py writes, plus the source and
detected events), and preview PNGs with --render. Prints a TAG_CLIP line on success (build_all.sh greps it).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402

import mocap_map  # noqa: E402
import mocap_retarget as MR  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
rig_fbx, name, out_dir = argv[0], argv[1], argv[2]
render = "--render" in argv
frames_arg = argv[argv.index("--frames") + 1] if "--frames" in argv else None

spec = mocap_map.resolve(name)
bpy.ops.wm.read_factory_settings(use_empty=True)
target = MR.Target(rig_fbx)
rt = MR.Retargeter(target)
n_out, rows, info = MR.build(target, rt, spec)
report = {"clip": name, "frames": [0, n_out - 1], "seconds": round((n_out - 1) / float(MR.FPS_OUT), 3), "source": info}
report.update(MR.measure(rows))
report["spec"] = {k: v for k, v in spec.items() if k != "src"}
os.makedirs(out_dir, exist_ok=True)
fbx = os.path.join(out_dir, "A_TAG_%s.fbx" % name)
MR.export(target, fbx, n_out)
if render:
    MR.build_display(target)
    if frames_arg:
        frames = sorted({int(f) for f in frames_arg.split(",") if f.strip()})
    else:
        frames = sorted({0, (n_out - 1) // 4, (n_out - 1) // 2, 3 * (n_out - 1) // 4, n_out - 1})
    frames = [max(0, min(n_out - 1, f)) for f in frames]
    report["previews"] = MR.render(target, frames, os.path.join(out_dir, "preview_" + name))
json.dump(report, open(os.path.join(out_dir, "A_TAG_%s.json" % name), "w"), indent=1)
print("TAG_CLIP", json.dumps(report)[:1800])
