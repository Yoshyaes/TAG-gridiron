# Unreal Editor Python (headless): build an IK Rig for the TAG MetaHuman body skeleton and an
# IK Retargeter UEFN mannequin (Game Animation Sample mocap) -> TAG MetaHumans, auto-aligned to
# these exact bodies' rest pose. Output: /Game/TAG/Anim/IK_TAG_MetaHuman, RTG_TAG_UEFN_to_MH
import os
import traceback
import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
OUT = "/Game/TAG/Anim"
LOG = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_mh_retarget.txt")
lines = []


def w(*a):
    lines.append(" ".join(str(x) for x in a))


def fresh(name, cls, factory):
    """Create the asset, or reuse it in place on re-runs (delete+create in one session returns None)."""
    path = OUT + "/" + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    return tools.create_asset(name, OUT, cls, factory)


SRC = unreal.RetargetSourceOrTarget.SOURCE
TGT = unreal.RetargetSourceOrTarget.TARGET

try:
    src_mesh = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin")
    src_rig = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin")
    tgt_mesh = unreal.load_asset("/Game/TAG/MetaHumans/Unpacked/TAG_Skill/Body/SKM_TAG_Skill_BodyMesh")
    w("loaded", bool(src_mesh), bool(src_rig), bool(tgt_mesh))

    rig = fresh("IK_TAG_MetaHuman", unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    rc = unreal.IKRigController.get_controller(rig)
    w("IKRigController:", [m for m in dir(rc) if not m.startswith("_")])
    rc.set_skeletal_mesh(tgt_mesh)
    ok = rc.apply_auto_generated_retarget_definition()
    w("auto retarget definition:", ok)
    try:
        w("auto fbik:", rc.apply_auto_fbik())
    except Exception as e:
        w("auto fbik n/a", e)
    try:
        w("chains:", [(str(c.get_editor_property("chain_name")), str(c.get_editor_property("start_bone")), str(c.get_editor_property("end_bone"))) for c in rc.get_retarget_chains()])
    except Exception as e:
        w("chains n/a", e)
    try:
        w("retarget root:", rc.get_retarget_root())
    except Exception as e:
        w("root n/a", e)
    EAL.save_loaded_asset(rig)

    rtg = fresh("RTG_TAG_UEFN_to_MH", unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    w("IKRetargeterController:", [m for m in dir(c) if not m.startswith("_")])
    try:
        c.remove_all_ops()
        c.add_default_ops()
        w("default ops added")
    except Exception as e:
        w("add_default_ops n/a", e)
    c.set_ik_rig(SRC, src_rig)
    c.set_ik_rig(TGT, rig)
    try:
        c.set_preview_mesh(SRC, src_mesh)
        c.set_preview_mesh(TGT, tgt_mesh)
    except Exception as e:
        w("preview mesh err", e)
    try:
        c.assign_ik_rig_to_all_ops(SRC, src_rig)
        c.assign_ik_rig_to_all_ops(TGT, rig)
    except Exception as e:
        w("assign_ik_rig_to_all_ops n/a", e)
    try:
        c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
        w("chains auto-mapped")
    except Exception as e:
        w("auto_map_chains err", e)
    try:
        w("mapping:", c.get_chain_mapping() if hasattr(c, "get_chain_mapping") else "n/a")
    except Exception as e:
        w("mapping err", e)
    for side, nm in ((TGT, "target"),):
        try:
            c.auto_align_all_bones(side)
            w("auto-aligned", nm)
        except Exception as e:
            w("auto_align_all_bones err", nm, e)
    # FK chains copy the mocap rotations exactly; the auto full-body IK pass drags the arms back
    # toward the rest pose on these broader bodies, so it's turned off.
    for i in range(c.get_num_retarget_ops()):
        if "IK Rig" in str(c.get_op_name(i)):
            c.set_retarget_op_enabled(i, False)
            w("disabled op", c.get_op_name(i))
    EAL.save_loaded_asset(rtg)
    w("saved", rtg.get_path_name())
except Exception:
    w("FAIL", traceback.format_exc())

with open(LOG, "w", encoding="utf8") as f:
    f.write("\n".join(lines) + "\n")
