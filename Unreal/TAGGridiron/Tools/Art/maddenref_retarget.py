# Unreal Editor Python (headless). Retargets Madden reference clips from MaddenFB_Hero_Skeleton onto the
# UEFN mannequin, the skeleton the Athlete plays its animation on (the MetaHuman bodies follow it at runtime).
# Follows the mh_retarget.py pattern. Auto generated IK Rig, fuzzy chain mapping, target auto aligned to the
# source rest pose, FK only (the Run IK Rig op is off).
#
# Editor preview only. Everything this writes lives under /Game/MaddenRef/Retargeted. Nothing outside
# /Game/MaddenRef is saved, and no game code may reference these assets (maddenref_audit.py checks it).
#
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Env MADDENREF_RETARGET names a JSON {"clips": ["/Game/MaddenRef/Animations/Prototype/A_x", ...]}.
# Env MADDENREF_DRY=1 builds and samples everything in memory and saves nothing.
# Env MADDENREF_REPORT names the report. Each clip is sampled at 0, 25, 50, 75 and 100 percent and the world
# direction of every mapped limb segment is compared between source and target (angle in degrees), along with
# pelvis height and root travel.
import json
import math
import os
import traceback

import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
OUT = "/Game/MaddenRef/Retargeted"
RIGS = OUT + "/Rigs"
SRC_MESH = "/Game/MaddenRef/Skeleton/MaddenFB_Hero"
TGT_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"
TGT_RIG = "/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin"
SUFFIX = "_UEFN"
SRC = unreal.RetargetSourceOrTarget.SOURCE
TGT = unreal.RetargetSourceOrTarget.TARGET
# Limb segments compared after retargeting (Madden bone pair, UEFN bone pair).
SEGMENTS = [
    ("spine", ("Hips", "Neck"), ("pelvis", "neck_01")),
    ("neck", ("Neck", "Head"), ("neck_01", "head")),
    ("upperarm_l", ("LeftArm", "LeftForeArm"), ("upperarm_l", "lowerarm_l")),
    ("forearm_l", ("LeftForeArm", "LeftHand"), ("lowerarm_l", "hand_l")),
    ("upperarm_r", ("RightArm", "RightForeArm"), ("upperarm_r", "lowerarm_r")),
    ("forearm_r", ("RightForeArm", "RightHand"), ("lowerarm_r", "hand_r")),
    ("thigh_l", ("LeftUpLeg", "LeftLeg"), ("thigh_l", "calf_l")),
    ("shin_l", ("LeftLeg", "LeftFoot"), ("calf_l", "foot_l")),
    ("thigh_r", ("RightUpLeg", "RightLeg"), ("thigh_r", "calf_r")),
    ("shin_r", ("RightLeg", "RightFoot"), ("calf_r", "foot_r")),
]
DRY = os.environ.get("MADDENREF_DRY") == "1"
lines = []


def w(*a):
    lines.append(" ".join(str(x) for x in a))


def get_or_create(path, cls, factory):
    """Reuse in place on re-runs (delete then create in one session returns None)."""
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    pkg, name = path.rsplit("/", 1)
    return tools.create_asset(name, pkg, cls, factory)


def save(path):
    if DRY:
        return
    if not EAL.save_asset(path, only_if_is_dirty=False):
        raise RuntimeError("save failed for %s (file locked, is the Unreal Editor open?)" % path)


def build_rigs():
    src_mesh = unreal.load_asset(SRC_MESH)
    tgt_mesh = unreal.load_asset(TGT_MESH)
    tgt_rig = unreal.load_asset(TGT_RIG)
    if not (src_mesh and tgt_mesh and tgt_rig):
        raise RuntimeError("missing input %s" % [bool(src_mesh), bool(tgt_mesh), bool(tgt_rig)])
    rig = get_or_create(RIGS + "/IK_MaddenFB_Hero", unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    rc = unreal.IKRigController.get_controller(rig)
    rc.set_skeletal_mesh(src_mesh)
    if not rc.apply_auto_generated_retarget_definition():
        raise RuntimeError("auto retarget definition failed on MaddenFB_Hero_Skeleton")
    # The auto definition makes Neck a one bone chain and leaves Neck1 in no chain, so the upper neck would
    # never move. Neck1 joins the Neck chain (two bones, like neck_01 and neck_02 on the UEFN mannequin).
    rc.set_retarget_chain_end_bone("Neck", "Neck1")
    # Madden root motion lives on AITrajectory (Reference > AITrajectory > Hips).
    rc.set_root_motion_bone("AITrajectory")
    chains = [(str(c.get_editor_property("chain_name")), str(rc.get_retarget_chain_start_bone(c.get_editor_property("chain_name"))),
               str(rc.get_retarget_chain_end_bone(c.get_editor_property("chain_name")))) for c in rc.get_retarget_chains()]
    w("source root", rc.get_retarget_root(), "root motion", rc.get_root_motion_bone(), "chains", chains)
    save(rig.get_path_name().split(".")[0])

    rtg = get_or_create(RIGS + "/RTG_MaddenFB_to_UEFN", unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.remove_all_ops()
    c.add_default_ops()
    c.set_ik_rig(SRC, rig)
    c.set_ik_rig(TGT, tgt_rig)
    c.set_preview_mesh(SRC, src_mesh)
    c.set_preview_mesh(TGT, tgt_mesh)
    c.assign_ik_rig_to_all_ops(SRC, rig)
    c.assign_ik_rig_to_all_ops(TGT, tgt_rig)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Fuzzy mapping drives the UEFN Root chain from a toe chain. Root motion comes from the Root Motion op instead.
    w("set_source_chain doc", (c.set_source_chain.__doc__ or "").splitlines()[:1])
    c.set_source_chain("None", "Root")
    one_to_one(c)
    mapping = []
    for ch in unreal.IKRigController.get_controller(tgt_rig).get_retarget_chains():
        n = ch.get_editor_property("chain_name")
        try:
            mapping.append((str(n), str(c.get_source_chain(n))))
        except Exception as e:
            mapping.append((str(n), "n/a " + str(e)[:80]))
    w("chain map (target, source)", mapping)
    c.auto_align_all_bones(TGT)
    for i in range(c.get_num_retarget_ops()):
        if "IK Rig" in str(c.get_op_name(i)):
            c.set_retarget_op_enabled(i, False)
    w("ops", [(str(c.get_op_name(i)), c.get_retarget_op_enabled(i)) for i in range(c.get_num_retarget_ops())])
    save(rtg.get_path_name().split(".")[0])
    return src_mesh, tgt_mesh, rtg, mapping


# Chains with the same bone count on both skeletons copy rotations bone for bone instead of interpolating
# along the chain (3 bone arms and legs, 2 bone neck).
ONE_TO_ONE = ("LeftArm", "RightArm", "LeftLeg", "RightLeg", "Neck")


def one_to_one(c):
    i = c.get_index_of_op_by_name("FK Chains")
    ctrl = c.get_op_controller(i)
    st = ctrl.get_settings()
    w("fk settings fields", [m for m in dir(st) if not m.startswith("_")][:60])
    chains = list(st.get_editor_property("chains_to_retarget"))
    changed = []
    for ch in chains:
        if str(ch.get_editor_property("target_chain_name")) in ONE_TO_ONE:
            ch.set_editor_property("rotation_mode", unreal.FKChainRotationMode.ONE_TO_ONE)
            changed.append(str(ch.get_editor_property("target_chain_name")))
    st.set_editor_property("chains_to_retarget", chains)
    ctrl.set_settings(st)
    w("fk one to one", changed)


def pose_dirs(seq, t, pairs):
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t, opts)
    out = {}
    for name, (a, b) in pairs:
        pa = unreal.AnimPoseExtensions.get_bone_pose(pose, a, unreal.AnimPoseSpaces.WORLD).translation
        pb = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD).translation
        out[name] = pb - pa
    return pose, out


def angle(u, v):

    nu, nv = u.length(), v.length()
    if nu < 1e-6 or nv < 1e-6:
        return None
    d = max(-1.0, min(1.0, u.dot(v) / (nu * nv)))
    return math.degrees(math.acos(d))


def compare(src_seq, tgt_seq):
    rows = []
    length = src_seq.get_play_length()
    s_root0 = t_root0 = None
    sq0 = tq0 = None
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        t = length * frac
        sp, sd = pose_dirs(src_seq, t, [(n, s) for n, s, _ in SEGMENTS])
        tp, td = pose_dirs(tgt_seq, min(t, tgt_seq.get_play_length()), [(n, g) for n, _, g in SEGMENTS])
        s_hips = unreal.AnimPoseExtensions.get_bone_pose(sp, "Hips", unreal.AnimPoseSpaces.WORLD).translation
        t_pelvis = unreal.AnimPoseExtensions.get_bone_pose(tp, "pelvis", unreal.AnimPoseSpaces.WORLD).translation
        if s_root0 is None:
            s_root0, t_root0 = s_hips, t_pelvis
        ang = {n: angle(sd[n], td[n]) for n, _, _ in SEGMENTS}
        sq = unreal.AnimPoseExtensions.get_bone_pose(sp, "Head", unreal.AnimPoseSpaces.WORLD).rotation
        tq = unreal.AnimPoseExtensions.get_bone_pose(tp, "head", unreal.AnimPoseSpaces.WORLD).rotation
        if frac == 0.0:
            sq0, tq0 = sq, tq
        # head turn since the first sample, source against target (degrees between the two delta rotations)
        ds = sq * sq0.inversed()
        dt = tq * tq0.inversed()
        a = math.degrees((ds.inversed() * dt).get_angle())
        ang["head_turn"] = min(a, 360.0 - a)
        rows.append({"t": round(t, 3), "max_deg": round(max(a for a in ang.values() if a is not None), 2),
                     "deg": {k: (round(v, 2) if v is not None else None) for k, v in ang.items()},
                     "src_hips_z": round(s_hips.z, 2), "tgt_pelvis_z": round(t_pelvis.z, 2),
                     "src_travel_cm": round((s_hips - s_root0).length(), 2), "tgt_travel_cm": round((t_pelvis - t_root0).length(), 2)})
    return rows


def run():
    spec = json.load(open(os.environ["MADDENREF_RETARGET"]))
    report = {"out": OUT, "dry": DRY, "clips": []}
    src_mesh, tgt_mesh, rtg, mapping = build_rigs()
    report["chain_map"] = mapping
    assets = []
    for p in spec["clips"]:
        ad = unreal.EditorAssetLibrary.find_asset_data(p)
        if not ad.is_valid():
            raise RuntimeError("missing clip " + p)
        assets.append(ad)
    made = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        assets, src_mesh, tgt_mesh, rtg, "", "", "", SUFFIX, OUT, False, False, True)
    paths = sorted(str(a.package_name) for a in made)
    w("retargeted", len(paths))
    for p in paths:
        if not p.startswith(OUT + "/"):
            raise RuntimeError("retarget wrote outside %s: %s" % (OUT, p))
        save(p)
    for p in spec["clips"]:
        name = p.rsplit("/", 1)[1]
        tp = OUT + "/" + name + SUFFIX
        src_seq = unreal.load_asset(p)
        tgt_seq = unreal.load_asset(tp)
        entry = {"source": p, "target": tp, "exists": tgt_seq is not None}
        if tgt_seq is not None:
            entry["skeleton"] = tgt_seq.get_editor_property("skeleton").get_path_name()
            entry["src_len"] = round(src_seq.get_play_length(), 3)
            entry["tgt_len"] = round(tgt_seq.get_play_length(), 3)
            entry["samples"] = compare(src_seq, tgt_seq)
            entry["max_deg"] = max(r["max_deg"] for r in entry["samples"])
        report["clips"].append(entry)
    report["written"] = paths
    return report


try:
    rep = run()
    rep["status"] = "ok"
except Exception:
    rep = {"status": "fail", "error": traceback.format_exc()}
rep["log"] = lines
json.dump(rep, open(os.environ["MADDENREF_REPORT"], "w"), indent=1)
