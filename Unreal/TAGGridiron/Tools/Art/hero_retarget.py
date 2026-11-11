# Unreal Editor Python (headless). IK Rigs for the model pack characters and the runtime retargeters that carry the
# game's animation (played on the hidden UEFN mannequin) onto them:
#   /Game/TAG/Hero/Rig/IK_TAG_Hero   + RTG_TAG_UEFN_to_Hero   (players, coaches, officials)
#   /Game/TAG/Hero/Rig/IK_TAG_Crowd  + RTG_TAG_UEFN_to_Crowd  (sideline crew and fans, 29 bone crowd rig)
# The target chains copy the UEFN rig's own chains: hero_build.py gave the pack rigs the same Unreal bone names, so a
# chain keeps its start and end bones (a missing bone moves the end up the chain or drops it). FK only, the target
# auto aligned to the source rest pose, like RTG_TAG_UEFN_to_MH.
#   UnrealEditor-Cmd.exe TAGGridiron.uproject -run=pythonscript -script="<this file>" -unattended -nosplash -nullrhi
# Log: Saved/tag_hero_retarget.txt
import os
import traceback

import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
OUT = "/Game/TAG/Hero/Rig"
LOG = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_hero_retarget.txt")
SRC = unreal.RetargetSourceOrTarget.SOURCE
TGT = unreal.RetargetSourceOrTarget.TARGET
lines = []


def w(*a):
    lines.append(" ".join(str(x) for x in a))


def fresh(name, cls, factory):
    path = OUT + "/" + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    return tools.create_asset(name, OUT, cls, factory)


def bones_of(mesh):
    sk = mesh.get_editor_property("skeleton")
    pose = unreal.AnimPoseExtensions.get_reference_pose(sk)
    names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
    return names


def parent_chain(mesh, bone):
    """Bone names from bone up to the root (reference skeleton of the mesh)."""
    sk = mesh.get_editor_property("skeleton")
    out = [bone]
    try:
        while True:
            p = str(unreal.AnimPoseExtensions.get_bone_parent_name(unreal.AnimPoseExtensions.get_reference_pose(sk), out[-1])) \
                if hasattr(unreal.AnimPoseExtensions, "get_bone_parent_name") else None
            if not p or p == "None":
                break
            out.append(p)
    except Exception:
        pass
    return out


def build(tag, mesh_path, rtg_name):
    src_mesh = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin")
    src_rig = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin")
    tgt_mesh = unreal.load_asset(mesh_path)
    w("==", tag, "loaded", bool(src_mesh), bool(src_rig), bool(tgt_mesh))
    have = set(bones_of(tgt_mesh))
    src_c = unreal.IKRigController.get_controller(src_rig)
    src_chains = [(str(c.get_editor_property("chain_name")), str(c.get_editor_property("start_bone").get_editor_property("bone_name")),
                   str(c.get_editor_property("end_bone").get_editor_property("bone_name"))) for c in src_c.get_retarget_chains()]
    w("source chains", src_chains, "root", src_c.get_retarget_root())

    rig = fresh("IK_TAG_" + tag, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    rc = unreal.IKRigController.get_controller(rig)
    rc.set_skeletal_mesh(tgt_mesh)
    for c in list(rc.get_retarget_chains()):
        rc.remove_retarget_chain(c.get_editor_property("chain_name"))
    rc.set_retarget_root("pelvis")
    # fallbacks for bones a rig lacks (the pack rigs have four spine bones, the crowd rig no toes or most fingers)
    alt = {"spine_02": "spine_03", "neck_02": "neck_01"}
    made = []
    used = set()
    for name, start, end in src_chains:
        s = start if start in have else alt.get(start)
        e = end if end in have else alt.get(end)
        if s and s in have and (not e or e not in have):
            e = s  # a one bone finger on the crowd rig (thumb_01, middle_01)
        if not s or not e or s not in have or e not in have or name.lower() == "root" or s in used or e in used:
            w("skip chain", name, start, end)
            continue
        used.update((s, e))
        rc.add_retarget_chain(name, s, e, "")
        made.append((name, s, e))
    w("target chains", made)
    EAL.save_loaded_asset(rig)

    rtg = fresh(rtg_name, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    try:
        c.remove_all_ops()
        c.add_default_ops()
    except Exception as e:
        w("add_default_ops n/a", e)
    c.set_ik_rig(SRC, src_rig)
    c.set_ik_rig(TGT, rig)
    c.set_preview_mesh(SRC, src_mesh)
    c.set_preview_mesh(TGT, tgt_mesh)
    try:
        c.assign_ik_rig_to_all_ops(SRC, src_rig)
        c.assign_ik_rig_to_all_ops(TGT, rig)
    except Exception as e:
        w("assign_ik_rig_to_all_ops n/a", e)
    c.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    c.auto_align_all_bones(TGT)
    for i in range(c.get_num_retarget_ops()):
        nm = str(c.get_op_name(i))
        w("op", i, nm)
        if "IK Rig" in nm or "Run IK" in nm:
            c.set_retarget_op_enabled(i, False)
            w("disabled op", nm)
    EAL.save_loaded_asset(rtg)
    w("saved", rtg.get_path_name())


FAN_CLIPS = {"Idle": "/Game/TAG/Anim/Authored/A_TAG_Idle_Stand_Loop",
             "Cheer": "/Game/TAG/Anim/Authored/A_TAG_Fan_Cheer_Loop",
             "Clap": "/Game/TAG/Anim/Pack/Celebrations/A_TAG_Pack_Celeb_TD_BC_SlowClap_1Man0_v1__P01",
             "Kneel": "/Game/TAG/Anim/Authored/A_TAG_QB_Kneel"}
FAN_DIR = "/Game/TAG/Hero/Crowd/Anims"
# every skeleton family: its retargeter, its bootstrap mesh and the clips baked for the people that stand around
# (spectators, sideline staff and media); A_<Prefix>_<Clip> in FAN_DIR. The crowd rig keeps the names A_Fan_*.
RIGS = [("Hero", "SKM_TAG_HeroRig", "Hero"), ("Crowd", "SKM_TAG_CrowdRig", "Fan"), ("Male", "SKM_TAG_MaleRig", "Male"),
        ("Female", "SKM_TAG_FemaleRig", "Female"), ("Child", "SKM_TAG_ChildRig", "Child"), ("SidePlayer", "SKM_TAG_SidePlayerRig", "SidePlayer")]


def fan_clips(tag, boot, prefix):
    """Idle / cheer / clap / kneel baked on one skeleton (played by the stadium's spectators, staff and media)."""
    src_mesh = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin")
    tgt_mesh = unreal.load_asset(OUT + "/" + boot)
    rtg = unreal.load_asset(OUT + "/RTG_TAG_UEFN_to_" + tag)
    if not (tgt_mesh and rtg):
        w("no mesh / retargeter for", tag)
        return
    if not EAL.does_directory_exist(FAN_DIR):
        EAL.make_directory(FAN_DIR)
    for clip, src in FAN_CLIPS.items():
        if not EAL.does_asset_exist(src):
            w("fan clip source missing", src)
            continue
        name = "A_%s_%s" % (prefix, clip)
        dst = FAN_DIR + "/" + name
        if EAL.does_asset_exist(dst):
            EAL.delete_asset(dst)
        res = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
            assets_to_retarget=[EAL.find_asset_data(src)], source_mesh=src_mesh, target_mesh=tgt_mesh, ik_retarget_asset=rtg,
            prefix="RTG_%s_" % prefix, target_path=FAN_DIR, use_source_path=False, include_referenced_assets=False, overwrite_existing_files=True)
        made = [str(r.package_name) for r in res]
        anim = next((m for m in made if isinstance(unreal.load_asset(m), unreal.AnimSequence)), None)
        if not anim:
            w("fan clip retarget produced nothing", name, made)
            continue
        if anim != dst:
            EAL.rename_asset(anim, dst)
        a = unreal.load_asset(dst)
        a.set_editor_property("enable_root_motion", False)
        EAL.save_asset(dst, only_if_is_dirty=False)
        w("fan clip", dst, round(a.get_play_length(), 2), a.get_editor_property("skeleton").get_path_name())


if os.environ.get("TAG_HERO_RTG_DELETE") == "1":
    # a rebuilt-in-place retargeter kept stale settings (child translations came out in the wrong units under the
    # pack rig's 100x pelvis): delete in one run, build fresh in the next (delete + create in one session fails)
    for n in ("RTG_TAG_UEFN_to_Hero", "IK_TAG_Hero", "RTG_TAG_UEFN_to_Crowd", "IK_TAG_Crowd"):
        if EAL.does_asset_exist(OUT + "/" + n):
            w("deleted", n, EAL.delete_asset(OUT + "/" + n))
    with open(LOG, "w", encoding="utf8") as f:
        f.write("\n".join(lines) + "\n")
    raise SystemExit(0)
try:
    for tag, boot, prefix in RIGS:
        if os.environ.get("TAG_HERO_RIGS") and tag not in os.environ["TAG_HERO_RIGS"].split(","):
            continue
        build(tag, OUT + "/" + boot, "RTG_TAG_UEFN_to_" + tag)
        fan_clips(tag, boot, prefix)
except Exception:
    w("FAIL", traceback.format_exc())
with open(LOG, "w", encoding="utf8") as f:
    f.write("\n".join(lines) + "\n")
