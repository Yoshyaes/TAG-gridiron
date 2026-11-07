# Read-only: dump TAG retargeter ops/chain settings and key bone ref positions.
import os
import unreal

OUT = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "tag_mh_retarget_inspect.txt")
L = []
rtg = unreal.load_asset("/Game/TAG/Anim/RTG_TAG_UEFN_to_MH")
c = unreal.IKRetargeterController.get_controller(rtg)
n = c.get_num_retarget_ops()
for i in range(n):
    name = c.get_op_name(i)
    oc = c.get_op_controller(i)
    L.append("op %d %s enabled=%s controller=%s" % (i, name, c.get_retarget_op_enabled(i), oc.get_class().get_name() if oc else None))
    if oc:
        L.append("   methods: " + ", ".join(m for m in dir(oc) if not m.startswith("_") and m not in dir(unreal.Object)))
        try:
            st = oc.get_settings()
            L.append("   settings: " + str(st)[:1500])
        except Exception as e:
            L.append("   settings err %s" % e)
for side in (unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET):
    L.append("pose %s: %s" % (side, c.get_current_retarget_pose_name(side)))
    for b in ("upperarm_l", "upperarm_r", "clavicle_l", "lowerarm_l", "spine_05"):
        try:
            L.append("   %s offset %s" % (b, c.get_rotation_offset_for_retarget_pose_bone(b, side).rotator()))
        except Exception as e:
            L.append("   %s err %s" % (b, e))
rig = unreal.load_asset("/Game/TAG/Anim/IK_TAG_MetaHuman")
rc = unreal.IKRigController.get_controller(rig)
for b in ("foot_l", "ball_l", "upperarm_l", "hand_l"):
    try:
        L.append("MH ref %s %s" % (b, rc.get_ref_pose_transform_of_bone(b).translation))
    except Exception as e:
        L.append("ref err %s %s" % (b, e))
src = unreal.load_asset("/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin")
sc = unreal.IKRigController.get_controller(src)
for b in ("upperarm_l", "hand_l", "foot_l", "ball_l"):
    try:
        L.append("UEFN ref %s %s" % (b, sc.get_ref_pose_transform_of_bone(b).translation))
    except Exception as e:
        L.append("ref err %s %s" % (b, e))
L.append("UEFN chains: " + str([str(x.get_editor_property("chain_name")) for x in sc.get_retarget_chains()]))
with open(OUT, "w", encoding="utf8") as f:
    f.write("\n".join(L) + "\n")
