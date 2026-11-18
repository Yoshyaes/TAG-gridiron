"""Blender: inspect the UEFN rig FBX (bones, rest heights, axes).
    blender --background --factory-startup --python Tools/Anim/inspect_rig.py -- <rig.fbx>
"""
import sys

import bpy

fbx = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx, automatic_bone_orientation=False, ignore_leaf_bones=False)
arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
print("OBJECTS", [(o.name, o.type) for o in bpy.data.objects])
print("ARM", arm.name, "loc", tuple(round(v, 3) for v in arm.location), "rot", tuple(round(v, 3) for v in arm.rotation_euler),
      "scale", tuple(round(v, 3) for v in arm.scale))
print("ACTION", arm.animation_data.action.name if arm.animation_data and arm.animation_data.action else None,
      "frames", bpy.context.scene.frame_start, bpy.context.scene.frame_end, "fps", bpy.context.scene.render.fps)
bpy.context.scene.frame_set(0)
mw = arm.matrix_world
names = [b.name for b in arm.data.bones]
print("NBONES", len(names))
key = ["root", "pelvis", "spine_01", "spine_03", "spine_05", "neck_01", "head", "clavicle_l", "upperarm_l", "lowerarm_l",
       "hand_l", "hand_r", "thigh_l", "calf_l", "foot_l", "ball_l", "foot_r", "ik_foot_root", "ik_hand_gun"]
for n in key:
    if n in arm.data.bones:
        b = arm.data.bones[n]
        h = mw @ b.head_local
        t = mw @ b.tail_local
        print("BONE %-14s parent=%-12s head=(%.3f %.3f %.3f) tail=(%.3f %.3f %.3f)" % (
            n, b.parent.name if b.parent else "-", h.x, h.y, h.z, t.x, t.y, t.z))
print("ALLBONES", names)
