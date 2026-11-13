# Unreal Editor Python (headless). End to end: how far from the capture did the pack bodies move on the old path?
# For each clip, the UEFN tree's version (/Game/TAG/Anim/Authored, pack -> UEFN in Blender) is retargeted onto the
# hero skeleton with the game's own runtime retargeter RTG_TAG_UEFN_to_Hero (offline, IKRetargetBatchOperation: the
# same processor the game ran), and compared joint by joint, frame by frame, with the hero tree's version
# (/Game/TAG/AnimH/Authored: the capture on its own skeleton, Tools/Anim/mocap_fidelity.py: 0.4 mm from the capture).
# Positions in each clip's own space (the frames of reference are the same), cm -> mm.
#   TAG_FIDELITY_CLIPS=QB_Throw,Tackle_Wrap,... UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>"
# Report: Saved/Fidelity/ue_old_path.json
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
TMP = "/Game/TAG/_FidelityTmp"
JOINTS = ["pelvis", "spine_05", "head", "upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r", "hand_l", "hand_r",
          "thigh_l", "thigh_r", "calf_l", "calf_r", "foot_l", "foot_r", "ball_l", "ball_r"]
CONTACT = {"hand_l", "hand_r", "foot_l", "foot_r", "ball_l", "ball_r"}
DEFAULT = ["QB_Throw", "Tackle_Wrap", "Catch_High", "Loco_Sprint_Normal", "Stance3pt_Loop", "Block_Engaged_Loop",
           "Juke_L", "Celebrate_Loop", "GetUp_FaceDown", "Kick_FieldGoal", "QB_Drop_Shotgun", "Rush_Spin_L"]
opts = unreal.AnimPoseEvaluationOptions()


def joints_at(anim, t):
    # the clip's own space (root motion included): the retargeter's Root Motion op parks the old clip's root bone at
    # pelvis height, so positions are not taken relative to the root bone (no skin rides on it)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
    return {j: unreal.AnimPoseExtensions.get_bone_pose(pose, j, unreal.AnimPoseSpaces.WORLD).translation for j in JOINTS}


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(round(p * (len(v) - 1))))] if v else 0.0


rep = {"clips": {}, "errors": []}
try:
    names = [n for n in os.environ.get("TAG_FIDELITY_CLIPS", "").split(",") if n] or DEFAULT
    src_mesh = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin")
    tgt_mesh = unreal.load_asset("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig")
    rtg = unreal.load_asset("/Game/TAG/Hero/Rig/RTG_TAG_UEFN_to_Hero")
    if not EAL.does_directory_exist(TMP):
        EAL.make_directory(TMP)
    all_e, all_c = [], []
    for n in names:
        uefn = "/Game/TAG/Anim/Authored/A_TAG_" + n
        hero = "/Game/TAG/AnimH/Authored/A_TAG_" + n
        if not (EAL.does_asset_exist(uefn) and EAL.does_asset_exist(hero)):
            rep["errors"].append("missing " + n)
            continue
        res = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            assets_to_retarget=[EAL.find_asset_data(uefn)], source_mesh=src_mesh, target_mesh=tgt_mesh, ik_retarget_asset=rtg,
            prefix="OLD_", target_path=TMP, use_source_path=False, include_referenced_assets=False, overwrite_existing_files=True)
        old = next((unreal.load_asset(str(r.package_name)) for r in res if isinstance(unreal.load_asset(str(r.package_name)), unreal.AnimSequence)), None)
        new = unreal.load_asset(hero)
        if old is None:
            rep["errors"].append("retarget produced nothing for " + n)
            continue
        L = min(old.get_play_length(), new.get_play_length())
        frames = max(2, int(L * 30))
        errs, contact = [], []
        for i in range(frames):
            t = L * i / (frames - 1)
            a, b = joints_at(old, t), joints_at(new, t)
            for j in JOINTS:
                d = (a[j] - b[j])
                e = (d.x * d.x + d.y * d.y + d.z * d.z) ** 0.5 * 10.0
                errs.append(e)
                if j in CONTACT:
                    contact.append(e)
        r = {"frames": frames, "mean_mm": round(sum(errs) / len(errs), 1), "p95_mm": round(pct(errs, 0.95), 1),
             "max_mm": round(max(errs), 1), "contact_mean_mm": round(sum(contact) / len(contact), 1),
             "contact_p95_mm": round(pct(contact, 0.95), 1)}
        rep["clips"][n] = r
        all_e += errs
        all_c += contact
        unreal.log("TAG_FIDELITY_UE %s %s" % (n, json.dumps(r)))
    rep["overall"] = {"mean_mm": round(sum(all_e) / max(1, len(all_e)), 1), "p95_mm": round(pct(all_e, 0.95), 1),
                      "contact_mean_mm": round(sum(all_c) / max(1, len(all_c)), 1), "contact_p95_mm": round(pct(all_c, 0.95), 1)}
    for p in EAL.list_assets(TMP, recursive=True, include_folder=False):
        EAL.delete_asset(p.split(".")[0])
    EAL.delete_directory(TMP)
except Exception:
    import traceback
    rep["errors"].append(traceback.format_exc()[-1500:])
os.makedirs(os.path.join(SAVED, "Fidelity"), exist_ok=True)
json.dump(rep, open(os.path.join(SAVED, "Fidelity", "ue_old_path.json"), "w"), indent=1)
unreal.log("TAG_FIDELITY_UE overall %s errors %d" % (json.dumps(rep.get("overall")), len(rep["errors"])))
