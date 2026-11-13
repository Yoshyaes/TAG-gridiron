"""Blender check: how exactly does a built TAG clip reproduce the capture it came from?
    blender -b --factory-startup --python Tools/Anim/mocap_fidelity.py -- <rig.fbx> <out.json> <clip> [<clip> ...]

For each pack clip (manifest name) the clip is built onto <rig.fbx> the way the library builds it (mode "root": the
whole capture with its travel), then every output frame of the result is evaluated on the rig (root motion included)
and its joints are compared with the capture's own joints at the same source time, in the same frame of reference
(the anchor trajectory frame, actor axes). Reported per clip and overall: mean / p95 / max joint position error (mm)
over the main joints, and the same for the hands and feet alone (what contact reads from).

On the hero rig (RawArt/Hero/Rig/SKM_TAG_HeroRig.fbx, the pack's own skeleton) the error should be ~0. On the UEFN
rig (RawArt/Anim/UEFN_Rig_Idle.fbx) it is the first of the two retargets the game used to put between the capture and
the pack bodies (pack -> UEFN here, UEFN -> pack body by the IK retargeter at runtime).
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402

import mocap_retarget as MR  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
rig_fbx, out_json, clips = argv[0], argv[1], argv[2:]

# source joint -> target joint on either rig
JOINTS = {"Hips": "pelvis", "Spine3": "spine_05", "Head": "head", "LeftArm": "upperarm_l", "RightArm": "upperarm_r",
          "LeftForeArm": "lowerarm_l", "RightForeArm": "lowerarm_r", "LeftHand": "hand_l", "RightHand": "hand_r",
          "LeftUpLeg": "thigh_l", "RightUpLeg": "thigh_r", "LeftLeg": "calf_l", "RightLeg": "calf_r",
          "LeftFoot": "foot_l", "RightFoot": "foot_r", "LeftToeBase": "ball_l", "RightToeBase": "ball_r"}
CONTACT = ("LeftHand", "RightHand", "LeftFoot", "RightFoot", "LeftToeBase", "RightToeBase")


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(round(p * (len(v) - 1))))] if v else 0.0


bpy.ops.wm.read_factory_settings(use_empty=True)
target = MR.Target(rig_fbx)
rt = MR.Retargeter(target)
report = {"rig": os.path.basename(rig_fbx), "identity": rt.identity, "clips": {}}
all_err, all_contact = [], []
for clip in clips:
    spec = {"src": clip, "mode": "root"}
    n_out, rows, info = MR.build(target, rt, spec)
    a = target.arm
    # the result, evaluated on the rig: world positions (metres) per output frame
    got = []
    for i in range(n_out):
        bpy.context.scene.frame_set(i)
        W = a.matrix_world
        got.append({t: (W @ a.pose.bones[t].matrix).to_translation() for t in JOINTS.values() if t in a.pose.bones})
    # the capture, in the anchor frame: T0 = the anchor trajectory heading at the first frame, placed where build put it
    src = MR.Source(MR.source_path(clip))
    f_start = info["src_frames"][0]
    src.seek(f_start)
    T0 = MR.traj_frame(src.world("AITrajectory"))
    T0.translation = MR.Vector(info["anchor"])
    T0i = T0.inverted()
    errs, contact = [], []
    worst = (0.0, None, None)
    for i in range(n_out):
        fs = min(f_start + i * info["time_scale"] * src.fps / MR.FPS_OUT, info["src_frames"][1])
        src.seek(fs)
        for s, t in JOINTS.items():
            if t not in got[i]:
                continue
            want = (MR.TRAJ_TO_ACTOR @ T0i @ src.world(s)).to_translation()
            e = (got[i][t] - want).length * 1000.0
            errs.append(e)
            if s in CONTACT:
                contact.append(e)
            if e > worst[0]:
                worst = (e, i, t)
    bpy.data.objects.remove(src.arm)
    r = {"frames": n_out, "mean_mm": round(sum(errs) / max(1, len(errs)), 2), "p95_mm": round(pct(errs, 0.95), 2),
         "max_mm": round(worst[0], 2), "worst_at": [worst[1], worst[2]],
         "contact_mean_mm": round(sum(contact) / max(1, len(contact)), 2), "contact_p95_mm": round(pct(contact, 0.95), 2)}
    report["clips"][clip] = r
    all_err += errs
    all_contact += contact
    print("TAG_FIDELITY %s %s" % (clip, json.dumps(r)), flush=True)
    a.animation_data_clear()
    for act in list(bpy.data.actions):
        bpy.data.actions.remove(act)
    for o in list(bpy.data.objects):
        if o != a:
            bpy.data.objects.remove(o)
    a.location = (0.0, 0.0, 0.0)
    a.rotation_euler = (0.0, 0.0, 0.0)
    for pb in a.pose.bones:
        pb.matrix_basis = MR.Matrix.Identity(4)
report["overall"] = {"mean_mm": round(sum(all_err) / max(1, len(all_err)), 2), "p95_mm": round(pct(all_err, 0.95), 2),
                     "max_mm": round(max(all_err, default=0.0), 2),
                     "contact_mean_mm": round(sum(all_contact) / max(1, len(all_contact)), 2),
                     "contact_p95_mm": round(pct(all_contact, 0.95), 2)}
os.makedirs(os.path.dirname(os.path.abspath(out_json)), exist_ok=True)
json.dump(report, open(out_json, "w"), indent=1)
print("TAG_FIDELITY_OVERALL %s %s" % (report["rig"], json.dumps(report["overall"])), flush=True)
