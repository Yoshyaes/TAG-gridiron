"""TAG keyframe animation library for Blender (no mocap). Original hand-authored key poses on the UEFN rig.

Conventions (Blender world, meters): character at the origin, facing -Y, left = +X, up = +Z.
Controls per key:
  pelvis   (x, y, z) position and (pitch, yaw, lean) degrees; pitch + = bend forward, yaw + = turn left, lean + = top to the left
  spine    (pitch, yaw, lean) total, distributed over spine_01..05
  clav_l / clav_r  (raise, forward) degrees
  hand_l / hand_r  IK target positions; elbow_l / elbow_r  pole directions (x, y, z)
  foot_l / foot_r  IK ankle positions; foot_pitch_l/r degrees (+ = heel up, toe planted); foot_yaw_l/r degrees
  knee_l / knee_r  pole directions
  look     world point the head (and partly the neck) aims at
Interpolation is Bezier (ease in/out) by default; holds use duplicate keys. Animator rules applied in the clip
files: anticipation before big moves, overlap (spine and head trail the pelvis by a few frames), arcs on hands.
"""
import math
import os

import bpy
from mathutils import Matrix, Vector

SPINE = ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"]
SPINE_W = [0.14, 0.18, 0.22, 0.24, 0.22]
FWD = Vector((0, -1, 0))
LEFT = Vector((1, 0, 0))
UP = Vector((0, 0, 1))
DISPLAY_SKIP = ("ik_", "weapon", "attach", "props", "poi", "twist", "metacarpal")


def rot(axis, deg):
    return Matrix.Rotation(math.radians(deg), 4, axis)


def pyl(pitch=0.0, yaw=0.0, lean=0.0):
    """World rotation: yaw about up, then pitch about the (yawed) left axis, then lean about the forward axis."""
    return rot(UP, yaw) @ rot(LEFT, pitch) @ rot(Vector((0, 1, 0)), lean)


class Rig:
    def __init__(self, fbx):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.fbx(filepath=fbx, automatic_bone_orientation=False, ignore_leaf_bones=False)
        self.arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
        a = self.arm
        # The imported take survives "action = None" in Blender 5 (NLA track / action slot) and would overwrite every
        # pose on evaluation. Drop all animation data and actions so only our keys drive the rig.
        a.animation_data_clear()
        for act in list(bpy.data.actions):
            bpy.data.actions.remove(act)
        for o in [o for o in bpy.data.objects if o.type != "ARMATURE"]:
            bpy.data.objects.remove(o)
        bpy.context.view_layer.objects.active = a
        a.select_set(True)
        bpy.ops.object.mode_set(mode="POSE")
        for pb in a.pose.bones:
            pb.rotation_mode = "QUATERNION"
            pb.matrix_basis = Matrix.Identity(4)
        self.S = a.matrix_world.to_scale()[0]          # 0.01: armature space is cm
        bpy.context.view_layer.update()
        self.rest = {pb.name: a.matrix_world @ pb.bone.matrix_local for pb in a.pose.bones}
        self.targets = {}
        self._setup_ik()
        self._setup_look()
        self._setup_ik_bones()

    # ----------------------------------------------------------------- helpers
    def head(self, name):
        """World position of a bone head in the current (evaluated) pose."""
        a = self.arm
        return a.matrix_world @ a.pose.bones[name].head

    def _empty(self, name, loc):
        e = bpy.data.objects.new(name, None)
        bpy.context.scene.collection.objects.link(e)
        e.location = loc
        e.empty_display_size = 0.05
        self.targets[name] = e
        return e

    def _setup_ik(self):
        # UE bone tails do not sit on the next joint (the forearm tail is ~27 cm off the wrist), and Blender IK solves
        # the chain tail. So each limb gets a clean helper chain fx_<upper> (joint to joint) + fx_<mid> (joint to
        # wrist / ankle) carrying the IK, and the real bones damped-track it. Their rest frames stay exactly UE's;
        # the helpers are deleted before export.
        a = self.arm
        chains = {"l_arm": ("upperarm_l", "lowerarm_l", "hand_l", Vector((0.3, 0.35, -0.2))),
                  "r_arm": ("upperarm_r", "lowerarm_r", "hand_r", Vector((-0.3, 0.35, -0.2))),
                  "l_leg": ("thigh_l", "calf_l", "foot_l", Vector((0.1, -0.6, 0.0))),
                  "r_leg": ("thigh_r", "calf_r", "foot_r", Vector((-0.1, -0.6, 0.0)))}
        bpy.ops.object.mode_set(mode="EDIT")
        eb = a.data.edit_bones
        for key, (upper, mid, end, _) in chains.items():
            fu = eb.new("fx_" + upper)
            fu.head, fu.tail, fu.roll = eb[upper].head.copy(), eb[mid].head.copy(), 0.0
            fu.parent = eb[upper].parent
            fm = eb.new("fx_" + mid)
            fm.head, fm.tail, fm.roll = eb[mid].head.copy(), eb[end].head.copy(), 0.0
            fm.parent, fm.use_connect = fu, True
            for b in (fu, fm):
                b.use_deform = False
        bpy.ops.object.mode_set(mode="POSE")
        for pb in a.pose.bones:
            if pb.name.startswith("fx_"):
                pb.rotation_mode = "QUATERNION"
                self.rest[pb.name] = a.matrix_world @ pb.bone.matrix_local
        self.chains = {}
        self.track_align = {}
        for key, (upper, mid, end, pole_dir) in chains.items():
            tgt = self._empty("T_" + end, self.rest[end].to_translation())
            tgt.rotation_mode = "QUATERNION"
            tgt.rotation_quaternion = self.rest[end].to_quaternion()
            pole = self._empty("P_" + end, self.rest[mid].to_translation() + pole_dir)
            c = a.pose.bones["fx_" + mid].constraints.new("IK")
            c.target, c.pole_target, c.chain_count, c.use_tail = tgt, pole, 2, True
            # real bones aim at the helper joints: upper -> elbow / knee, mid -> wrist / ankle
            for bone, child, head_tail in ((upper, mid, 0.0), (mid, end, 1.0)):
                d = (self.rest[child].to_translation() - self.rest[bone].to_translation()).normalized()
                axis, align = self._axis_and_align(bone, d)
                dt = a.pose.bones[bone].constraints.new("DAMPED_TRACK")
                dt.target, dt.subtarget, dt.head_tail, dt.track_axis = a, "fx_" + mid, head_tail, axis
                self.track_align[bone] = round(align, 3)
            cr = a.pose.bones[end].constraints.new("COPY_ROTATION")
            cr.target = tgt
            cr.influence = 1.0 if key.endswith("leg") else 0.0   # feet follow their target; hands follow the forearm
            self.chains[key] = (mid, end, c, tgt, pole, cr)
        self._calibrate_poles()

    def _axis_and_align(self, bone, world_dir):
        m = self.rest[bone].to_3x3().normalized()
        best = None
        for i, nm in enumerate("XYZ"):
            d = m.col[i].dot(world_dir)
            for sign, pre in ((1, "TRACK_"), (-1, "TRACK_NEGATIVE_")):
                if best is None or d * sign > best[1]:
                    best = (pre + nm, d * sign)
        return best

    def _calibrate_poles(self):
        """Pick each chain's pole_angle so the mid joint bends toward its pole (bone rolls differ per rig)."""
        a = self.arm
        for key, (mid, end, c, tgt, pole, cr) in self.chains.items():
            upper = a.pose.bones["fx_" + mid].parent.name
            root = self.rest[upper].to_translation()
            endp = self.rest[end].to_translation()
            # pull the end 25 percent toward the chain root so the limb must bend
            tgt.location = root.lerp(endp, 0.75)
            line_mid = root.lerp(tgt.location, 0.5)
            pdir = (pole.location - line_mid).normalized()
            best, best_s = 0.0, -1e9
            for deg in range(-180, 180, 5):
                c.pole_angle = math.radians(deg)
                bpy.context.view_layer.update()
                m = self.head(mid)
                s = (m - line_mid).dot(pdir) - 5.0 * (self.head(end) - tgt.location).length
                if s > best_s:
                    best, best_s = deg, s
            c.pole_angle = math.radians(best)
            tgt.location = endp
            self.pole_angles = getattr(self, "pole_angles", {})
            self.pole_angles[key] = best
        bpy.context.view_layer.update()

    def _axis_toward(self, bone, world_dir):
        m = self.rest[bone].to_3x3().normalized()
        best = None
        for i, nm in enumerate("XYZ"):
            v = m.col[i]
            d = v.dot(world_dir)
            for sign, pre in ((1, "TRACK_"), (-1, "TRACK_NEGATIVE_")):
                if best is None or d * sign > best[0]:
                    best = (d * sign, pre + nm)
        return best[1]

    def _setup_look(self):
        a = self.arm
        self.look = self._empty("T_look", Vector((0, -10, 1.7)))
        for bone, infl in (("neck_01", 0.25), ("neck_02", 0.35), ("head", 1.0)):
            c = a.pose.bones[bone].constraints.new("DAMPED_TRACK")
            c.target = self.look
            c.track_axis = self._axis_toward(bone, FWD)
            c.influence = infl
        self.look_axis = self._axis_toward("head", FWD)

    def _setup_ik_bones(self):
        a = self.arm
        for ikb, src in (("ik_foot_l", "foot_l"), ("ik_foot_r", "foot_r"), ("ik_hand_l", "hand_l"),
                         ("ik_hand_r", "hand_r"), ("ik_hand_gun", "hand_r")):
            if ikb in a.pose.bones:
                c = a.pose.bones[ikb].constraints.new("COPY_TRANSFORMS")
                c.target, c.subtarget = a, src

    # ----------------------------------------------------------------- posing
    def _rotate_bone_world(self, name, R):
        """Apply world rotation R (4x4, rotation only) to a pose bone about its head; children follow."""
        a = self.arm
        pb = a.pose.bones[name]
        Rw = a.matrix_world.inverted() @ R @ a.matrix_world   # into armature space (pure scale cancels)
        h = pb.head.copy()
        pb.matrix = Matrix.Translation(h) @ Rw @ Matrix.Translation(-h) @ pb.matrix
        bpy.context.view_layer.update()

    def pose(self, k):
        a = self.arm
        for pb in a.pose.bones:
            pb.matrix_basis = Matrix.Identity(4)
        bpy.context.view_layer.update()
        # pelvis: position then rotation (world)
        pel = a.pose.bones["pelvis"]
        if "pelvis" in k:
            x, y, z = k["pelvis"]
            pr = self.rest["pelvis"].to_translation()
            off = Vector((x, y, z)) - pr
            pel.matrix = Matrix.Translation(a.matrix_world.inverted().to_3x3() @ off) @ pel.matrix
            bpy.context.view_layer.update()
        p, y_, l = k.get("pelvis_rot", (0, 0, 0))
        self._rotate_bone_world("pelvis", pyl(p, y_, l))
        sp, sy, sl = k.get("spine", (0, 0, 0))
        for b, w in zip(SPINE, SPINE_W):
            self._rotate_bone_world(b, pyl(sp * w, sy * w, sl * w))
        for side, sgn in (("l", 1), ("r", -1)):
            raise_, fwd = k.get("clav_" + side, (0, 0))
            # raise: shoulder up (rotate about forward axis); forward: shoulder protracts (about up)
            self._rotate_bone_world("clavicle_" + side, rot(Vector((0, 1, 0)), raise_ * sgn) @ rot(UP, -fwd * sgn))
        # IK targets
        for side in ("l", "r"):
            if "hand_" + side in k:
                self.targets["T_hand_" + side].location = Vector(k["hand_" + side])
            if "elbow_" + side in k:
                sh = self.head("upperarm_" + side)
                self.targets["P_hand_" + side].location = sh + Vector(k["elbow_" + side]).normalized() * 0.6
            if "foot_" + side in k:
                fx, fy, fz = k["foot_" + side]
                pitch = k.get("foot_pitch_" + side, 0.0)
                yaw = k.get("foot_yaw_" + side, 0.0)
                t = self.targets["T_foot_" + side]
                # heel up pivots about the ball of the foot (~13 cm ahead of the ankle): raise the ankle to match
                lift = 0.13 * math.sin(math.radians(max(0.0, pitch)))
                t.location = Vector((fx, fy, fz + lift))
                base = self.rest["foot_" + side].to_quaternion()
                t.rotation_quaternion = (rot(UP, yaw) @ rot(LEFT, pitch)).to_quaternion() @ base   # + pitch = toe down
            if "knee_" + side in k:
                hip = self.head("thigh_" + side)
                self.targets["P_foot_" + side].location = hip + Vector(k["knee_" + side]).normalized() * 0.8
        if "look" in k:
            self.look.location = Vector(k["look"])
        bpy.context.view_layer.update()

    KEYED = ("pelvis", "spine", "clavicle")

    def key(self, frame, k):
        """Queue a key pose. Poses are solved first with no animation attached (the depsgraph would otherwise
        re-evaluate existing f-curves over the pose being built) and written by commit()."""
        self.pending = getattr(self, "pending", [])
        self.pending.append((frame, k))

    def commit(self):
        a = self.arm
        snaps = []
        for frame, k in self.pending:
            self.pose(k)
            snap = {}
            for pb in a.pose.bones:
                if pb.name.startswith(self.KEYED):
                    snap[("b", pb.name, "rotation_quaternion")] = tuple(pb.rotation_quaternion)
                    if pb.name == "pelvis":
                        snap[("b", pb.name, "location")] = tuple(pb.location)
            for nm, e in self.targets.items():
                snap[("e", nm, "location")] = tuple(e.location)
                if e.rotation_mode == "QUATERNION":
                    snap[("e", nm, "rotation_quaternion")] = tuple(e.rotation_quaternion)
            snaps.append((frame, snap))
        for frame, snap in snaps:
            for (kind, nm, prop), v in snap.items():
                owner = a.pose.bones[nm] if kind == "b" else self.targets[nm]
                setattr(owner, prop, v)
                owner.keyframe_insert(prop, frame=frame)
        self.pending = []

    # ----------------------------------------------------------------- output
    def bake(self, f0, f1):
        a = self.arm
        sc = bpy.context.scene
        sc.frame_start, sc.frame_end = f0, f1
        bpy.context.view_layer.objects.active = a
        bpy.ops.object.mode_set(mode="POSE")
        bpy.ops.pose.select_all(action="SELECT")
        bpy.ops.nla.bake(frame_start=f0, frame_end=f1, only_selected=False, visual_keying=True,
                         clear_constraints=True, use_current_action=True, bake_types={"POSE"})

    def measure(self, f0, f1):
        """Per frame world heights and foot slide, for the timing / height gates."""
        sc = bpy.context.scene
        rows = []
        for f in range(f0, f1 + 1):
            sc.frame_set(f)
            rows.append({n: tuple(self.head(n)) for n in ("pelvis", "head", "foot_l", "foot_r", "hand_l", "hand_r", "upperarm_r", "upperarm_l")})
        return rows

    def export(self, path, f0, f1, fps=30):
        sc = bpy.context.scene
        sc.render.fps = fps
        sc.frame_start, sc.frame_end = f0, f1
        # helper IK bones are not part of the UE skeleton
        bpy.context.view_layer.objects.active = self.arm
        bpy.ops.object.mode_set(mode="EDIT")
        for b in [b for b in self.arm.data.edit_bones if b.name.startswith("fx_")]:
            self.arm.data.edit_bones.remove(b)
        bpy.ops.object.mode_set(mode="OBJECT")
        for o in bpy.data.objects:
            o.select_set(o == self.arm)
        bpy.context.view_layer.objects.active = self.arm
        os.makedirs(os.path.dirname(path), exist_ok=True)
        bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE"}, add_leaf_bones=False,
                                 bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
                                 bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
                                 bake_anim_simplify_factor=0.0, apply_unit_scale=True, global_scale=1.0,
                                 armature_nodetype="NULL")

    # ----------------------------------------------------------------- preview
    def build_display(self):
        """Stick figure parented to bones (joint to child joint), plus a head ball, for workbench renders."""
        a = self.arm
        bpy.ops.object.mode_set(mode="OBJECT")
        a.data.pose_position = "REST"      # parent in the rest pose so the sticks sit where they were built
        bpy.context.view_layer.update()
        for pb in a.pose.bones:
            if any(s in pb.name for s in DISPLAY_SKIP) or not pb.parent or any(s in pb.parent.name for s in DISPLAY_SKIP):
                continue
            if pb.name.startswith(("index", "middle", "pinky", "ring", "thumb")):
                continue
            p0 = self.rest[pb.parent.name].to_translation()
            p1 = self.rest[pb.name].to_translation()
            v = p1 - p0
            if v.length < 0.02:
                continue
            r = 0.05 if pb.parent.name.startswith(("spine", "pelvis")) else 0.035
            bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=v.length, location=(p0 + p1) / 2)
            cyl = bpy.context.active_object
            cyl.rotation_mode = "QUATERNION"
            cyl.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(v)
            self._parent_to_bone(cyl, pb.parent.name)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.11, location=self.rest["head"].to_translation() + Vector((0, -0.02, 0.09)))
        self._parent_to_bone(bpy.context.active_object, "head")
        bpy.ops.mesh.primitive_cone_add(radius1=0.03, depth=0.08, location=self.rest["head"].to_translation() + Vector((0, -0.13, 0.08)))
        nose = bpy.context.active_object
        nose.rotation_euler = (math.radians(90), 0, 0)
        self._parent_to_bone(nose, "head")
        for side in ("l", "r"):   # feet: ankle to toe blocks
            f = self.rest["foot_" + side].to_translation()
            bpy.ops.mesh.primitive_cube_add(size=1, location=f + Vector((0, -0.08, -0.06)))
            c = bpy.context.active_object
            c.scale = (0.09, 0.24, 0.07)
            bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)   # size in the mesh, not the object
            self._parent_to_bone(c, "foot_" + side)
        bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
        a.data.pose_position = "POSE"
        bpy.context.view_layer.update()

    def _parent_to_bone(self, obj, bone):
        mw = obj.matrix_world.copy()
        obj.parent = self.arm
        obj.parent_type = "BONE"
        obj.parent_bone = bone
        bpy.context.view_layer.update()
        obj.matrix_world = mw

    def render(self, frames, out_dir, views=(("side", (1, 0, 0.05)), ("front", (0, -1, 0.05)), ("3q", (0.7, -0.7, 0.35)))):
        """views: name and unit direction from the subject; camera sits 2.6 m away, aimed at the frame's pelvis."""
        sc = bpy.context.scene
        sc.render.engine = "BLENDER_WORKBENCH"
        sc.render.resolution_x, sc.render.resolution_y = 480, 540
        sc.display.shading.light = "STUDIO"
        sc.display.shading.show_shadows = True
        cam_data = bpy.data.cameras.new("cam")
        cam = bpy.data.objects.new("cam", cam_data)
        sc.collection.objects.link(cam)
        sc.camera = cam
        os.makedirs(out_dir, exist_ok=True)
        out = []
        for vname, vdir in views:
            cam_data.lens = 45
            for f in frames:
                sc.frame_set(f)
                aim = self.head("pelvis") + Vector((0, 0, 0.05))
                cam.location = aim + Vector(vdir).normalized() * 2.6
                cam.rotation_euler = (aim - cam.location).to_track_quat("-Z", "Y").to_euler()
                p = os.path.join(out_dir, "%s_%03d.png" % (vname, f))
                sc.render.filepath = p
                bpy.ops.render.render(write_still=True)
                out.append(p)
        return out
