# Unreal Editor Python (headless). Samples AnimSequences for the comparison with the Python decode
# (tools/madden/antbank/compare_ue.py). World positions and local rotations of key bones at 0, 25, 50, 75 and
# 100 percent of each clip, raw and compressed. Env SAMPLE_ANIMS names a JSON list of asset paths, env
# SAMPLE_OUT the output JSON. Read only, saves nothing.
import unreal, json, os
unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(True)
anims = json.load(open(os.environ["SAMPLE_ANIMS"]))
bones = ["Hips", "Spine2", "Head", "LeftArm", "LeftForeArm", "LeftHand", "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot", "RightFoot", "RightToeBase", "AITrajectory"]
FR = [0.0, 0.25, 0.5, 0.75, 1.0]
out = {}
for path in anims:
    a = unreal.load_asset(path)
    if a is None:
        out[path] = {"error": "missing"}; continue
    L = a.get_play_length()
    rec = {"length": L, "frames": unreal.AnimationLibrary.get_num_frames(a), "samples": {}}
    for mode in ("RAW", "COMPRESSED"):
        opt = unreal.AnimPoseEvaluationOptions()
        try:
            opt.set_editor_property("evaluation_type", getattr(unreal.AnimDataEvalType, mode))
        except Exception as e:
            rec["opt_error"] = repr(e)
        rows = []
        for f in FR:
            t = min(f * L, L)
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t, opt)
            r = {"t": t}
            for b in bones:
                w = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
                l = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.LOCAL)
                q = l.rotation
                r[b] = {"w": [w.translation.x, w.translation.y, w.translation.z], "lq": [q.x, q.y, q.z, q.w]}
            rows.append(r)
        rec["samples"][mode] = rows
    out[path] = rec
json.dump(out, open(os.environ["SAMPLE_OUT"], "w"), indent=0)
