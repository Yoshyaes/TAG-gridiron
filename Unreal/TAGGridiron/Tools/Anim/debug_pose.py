"""Blender debug: where does a stance key pose get lost (pose, commit, bake)?"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
import clips  # noqa: E402
import tag_anim  # noqa: E402

rig = tag_anim.Rig(sys.argv[sys.argv.index("--") + 1])
k = None


class Cap:
    def key(self, f, kk):
        global k
        if k is None:
            k = kk


clips.stance_3pt(Cap())


def show(tag):
    print(tag, "pelvis", tuple(round(v, 3) for v in rig.head("pelvis")), "spine_05", tuple(round(v, 3) for v in rig.head("spine_05")),
          "head", tuple(round(v, 3) for v in rig.head("head")), "hand_r", tuple(round(v, 3) for v in rig.head("hand_r")),
          "pelvis_q", tuple(round(v, 3) for v in rig.arm.pose.bones["pelvis"].rotation_quaternion))


show("REST")
rig.pose(k)
show("POSED")
rig.key(0, k)
rig.key(10, k)
rig.commit()
bpy.context.scene.frame_set(0)
show("COMMITTED f0")
rig.bake(0, 10)
bpy.context.scene.frame_set(0)
show("BAKED f0")
