"""Mocap pack -> UEFN rig retarget for TAG clips (Blender library).

Source: the personal mocap pack (D:/MaddenRef/MocapPack; FBX per performer, 67 HumanIK-style bones, Z up after import,
metres). `AITrajectory` is the root and carries the ground-plane travel and heading; `Hips` moves relative to it.
Target: the UEFN mannequin rig (RawArt/Anim/UEFN_Rig_Idle.fbx), the same rig the hand keyed clips were built on, so the
game's conventions hold unchanged: each clip in its own actor frame, facing -Y, left +X, up +Z, metres, 30 fps.

Method (the IK Retargeter's idea, done in numpy-free mathutils):
  1. Calibrate: start both skeletons in their rest poses and rotate every mapped source bone (hierarchically) so its
     direction to the next joint matches the target bone's. The source rig in this pose is the "retarget pose".
  2. Per frame: target world rotation = source world rotation * inverse(source calibrated rotation) * target rest
     rotation. Pelvis translation is the hips translation scaled by the hip height ratio (0.897), so the feet reach
     the ground of the shorter rig.
  3. Frame of reference: the trajectory. 'inplace' subtracts the trajectory's position and heading every frame (the
     sim moves and turns the actor); 'inplace_pos' subtracts only the position (turns stay in the clip); 'anchor'
     subtracts the trajectory at the clip's first frame (the clip carries the travel: falls, dives). The trajectory's
     local axes are X left, Y up, Z forward; the actor frame is X left, Y back, Z up.
  4. Mirror (left <-> right, about the actor's X = 0 plane), trim, time scale, resample to 30 fps, seamless loop blend.
"""
import csv
import math
import os

import bpy
from mathutils import Matrix, Quaternion, Vector

PACK = os.environ.get("TAG_MOCAP_PACK", "D:/MaddenRef/MocapPack")
HIP_SCALE = 0.931 / 1.038  # UEFN pelvis height / Madden hips height (rest poses)
FPS_OUT = 30

# target bone -> (source bone, source child used for the direction, target child used for the direction)
# None as a child: no direction calibration (rest-relative rotation only).
BONE_MAP = [
    ("pelvis", "Hips", "Spine", "spine_01"),
    ("spine_01", "Spine", "Spine1", "spine_02"),
    ("spine_02", "Spine1", "Spine2", "spine_03"),
    ("spine_03", "Spine2", "Spine3", "spine_04"),
    ("spine_04", "Spine3", "Neck", "spine_05"),
    ("spine_05", "Spine3", "Neck", "neck_01"),
    ("neck_01", "Neck", "Neck1", "neck_02"),
    ("neck_02", "Neck1", "Head", "head"),
    ("head", "Head", None, None),
]
for S, s in (("Left", "l"), ("Right", "r")):
    BONE_MAP += [
        ("clavicle_" + s, S + "Shoulder", S + "Arm", "upperarm_" + s),
        ("upperarm_" + s, S + "Arm", S + "ForeArm", "lowerarm_" + s),
        ("lowerarm_" + s, S + "ForeArm", S + "Hand", "hand_" + s),
        ("hand_" + s, S + "Hand", S + "HandMiddle1", "middle_01_" + s),
        ("thigh_" + s, S + "UpLeg", S + "Leg", "calf_" + s),
        ("calf_" + s, S + "Leg", S + "Foot", "foot_" + s),
        ("foot_" + s, S + "Foot", S + "ToeBase", "ball_" + s),
        ("ball_" + s, S + "ToeBase", None, None),
        ("thumb_01_" + s, S + "HandThumb1", S + "HandThumb2", "thumb_02_" + s),
        ("thumb_02_" + s, S + "HandThumb2", S + "HandThumb3", "thumb_03_" + s),
        ("thumb_03_" + s, S + "HandThumb3", None, None),
    ]
    for F, f in (("Index", "index"), ("Middle", "middle"), ("Ring", "ring"), ("Pinky", "pinky")):
        BONE_MAP += [
            ("%s_metacarpal_%s" % (f, s), "%sInHand%s" % (S, F), "%sHand%s1" % (S, F), "%s_01_%s" % (f, s)),
            ("%s_01_%s" % (f, s), "%sHand%s1" % (S, F), "%sHand%s2" % (S, F), "%s_02_%s" % (f, s)),
            ("%s_02_%s" % (f, s), "%sHand%s2" % (S, F), "%sHand%s3" % (S, F), "%s_03_%s" % (f, s)),
            ("%s_03_%s" % (f, s), "%sHand%s3" % (S, F), None, None),
        ]
# ik helper bones copy a body bone
IK_COPY = {"ik_foot_l": "foot_l", "ik_foot_r": "foot_r", "ik_hand_l": "hand_l", "ik_hand_r": "hand_r", "ik_hand_gun": "hand_r"}

# trajectory local (X forward, Y up, Z right; measured on the clips - the rest-pose file's trajectory is identity and
# says nothing) -> actor (X left, Y back, Z up)
TRAJ_TO_ACTOR = Matrix(((0, 0, -1, 0), (-1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
MIRROR = Matrix(((-1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1)))


def mirror_name(n):
    if n.startswith("Left"):
        return "Right" + n[4:]
    if n.startswith("Right"):
        return "Left" + n[5:]
    return n


# ----------------------------------------------------------------------------------------------- manifest
_MANIFEST = None


def manifest():
    global _MANIFEST
    if _MANIFEST is None:
        _MANIFEST = {}
        with open(os.path.join(PACK, "manifest.csv"), encoding="utf-8") as f:
            for r in csv.DictReader(f):
                _MANIFEST[r["clip"]] = r
    return _MANIFEST


def resolve_clip(clip):
    """Manifest row for a clip name. A name without its version suffix (Foo__P01 for Foo_v1__P01 / Foo_v01__P01 /
    Foo_Var1__P01) resolves to the lowest version."""
    import re
    m = manifest()
    if clip in m:
        return m[clip]
    mm = re.match(r"^(.*?)(__P\d+)$", clip)
    base, part = (mm.group(1), mm.group(2)) if mm else (clip, "")
    pat = re.compile("^" + re.escape(base) + r"(?:_v\d+|_V\d+|_Var\d+)?" + re.escape(part) + "$", re.I)
    hits = sorted(k for k in m if pat.match(k))
    if not hits and part:
        # a single performer filed without a suffix, or under another number (DefMove_BullRush__P02)
        pat2 = re.compile("^" + re.escape(base) + r"(?:_v\d+|_V\d+|_Var\d+)?(?:__P\d+)?$", re.I)
        hits = sorted(k for k in m if pat2.match(k))
        if len(hits) > 1:
            raise KeyError("ambiguous clip %s: %s" % (clip, hits[:6]))
    if not hits:
        raise KeyError("clip not in the pack manifest: " + clip)
    return m[hits[0]]


def source_path(clip):
    """FBX path for a clip name (manifest row) or a relative/absolute path."""
    if clip.lower().endswith(".fbx"):
        return clip if os.path.isabs(clip) else os.path.join(PACK, clip)
    return os.path.join(PACK, resolve_clip(clip)["fbx"])


# ----------------------------------------------------------------------------------------------- loading
def import_fbx(path, use_anim=True):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, automatic_bone_orientation=False, ignore_leaf_bones=False, use_anim=use_anim)
    new = [o for o in bpy.data.objects if o not in before]
    arms = [o for o in new if o.type == "ARMATURE"]
    for o in new:
        if o.type != "ARMATURE":
            bpy.data.objects.remove(o)
    return arms[0]


class Target:
    """The UEFN rig: rest matrices, hierarchy, and a writer for armature-space pose matrices."""

    def __init__(self, fbx):
        self.arm = import_fbx(fbx, use_anim=True)
        a = self.arm
        a.animation_data_clear()
        for act in list(bpy.data.actions):
            bpy.data.actions.remove(act)
        bpy.context.view_layer.objects.active = a
        a.select_set(True)
        bpy.ops.object.mode_set(mode="POSE")
        for pb in a.pose.bones:
            pb.rotation_mode = "QUATERNION"
            pb.matrix_basis = Matrix.Identity(4)
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.context.view_layer.update()
        self.W = a.matrix_world.copy()
        self.Winv = self.W.inverted()
        self.rest_arm = {b.name: b.matrix_local.copy() for b in a.data.bones}       # armature space (cm)
        self.rest_world = {n: self.W @ m for n, m in self.rest_arm.items()}          # metres
        self.parent = {b.name: (b.parent.name if b.parent else None) for b in a.data.bones}
        # hierarchy order (parents first)
        self.order = []

        def walk(b):
            self.order.append(b.name)
            for c in b.children:
                walk(c)
        for b in a.data.bones:
            if not b.parent:
                walk(b)
        self.names = set(self.order)

    def basis_from_world(self, world_by_bone):
        """world_by_bone: desired world matrices (metres) for a subset of bones. Returns {bone: basis} for every
        bone; bones without a desired matrix follow their parent (identity basis)."""
        P = {}
        basis = {}
        for n in self.order:
            p = self.parent[n]
            R = self.rest_arm[n]
            if n in world_by_bone:
                P[n] = self.Winv @ world_by_bone[n]
            else:
                P[n] = (P[p] @ self.rest_arm[p].inverted() @ R) if p else R.copy()
            if p:
                b = R.inverted() @ self.rest_arm[p] @ P[p].inverted() @ P[n]
            else:
                b = R.inverted() @ P[n]
            basis[n] = b
        return basis, P


class Source:
    """One performer clip from the pack."""

    def __init__(self, fbx):
        self.arm = import_fbx(fbx, use_anim=True)
        a = self.arm
        act = a.animation_data.action if a.animation_data else None
        self.f0, self.f1 = (int(round(act.frame_range[0])), int(round(act.frame_range[1]))) if act else (1, 1)
        self.fps = bpy.context.scene.render.fps / bpy.context.scene.render.fps_base
        self.names = {b.name for b in a.data.bones}

    def world(self, name):
        pb = self.arm.pose.bones[name]
        return self.arm.matrix_world @ pb.matrix

    def all_world(self):
        W = self.arm.matrix_world
        return {pb.name: W @ pb.matrix for pb in self.arm.pose.bones}

    def seek(self, frame):
        """Fractional source frame."""
        f = int(math.floor(frame))
        bpy.context.scene.frame_set(f, subframe=max(0.0, min(0.999, frame - f)))


def canonical_rest():
    """World matrices of the pack skeleton's rest pose (Z up, facing -Y)."""
    a = import_fbx(os.path.join(PACK, "Skeleton", "Skeleton_RestPose.fbx"), use_anim=False)
    rest = {b.name: a.matrix_world @ b.matrix_local for b in a.data.bones}
    parent = {b.name: (b.parent.name if b.parent else None) for b in a.data.bones}
    order = []

    def walk(b):
        order.append(b.name)
        for c in b.children:
            walk(c)
    for b in a.data.bones:
        if not b.parent:
            walk(b)
    bpy.data.objects.remove(a)
    return rest, parent, order


# ----------------------------------------------------------------------------------------------- retarget core
class Retargeter:
    def __init__(self, target):
        self.t = target
        self.src_rest, self.src_parent, self.src_order = canonical_rest()
        self.children = {}
        for n, p in self.src_parent.items():
            if p:
                self.children.setdefault(p, []).append(n)
        self.pairs = [(tb, sb, sc, tc) for tb, sb, sc, tc in BONE_MAP if tb in target.names and sb in self.src_rest]
        self.mirror_fix = mirror_fixes(self.src_rest)
        self.calibrate()

    def calibrate(self):
        """Rotate the source rest pose, bone by bone from the hips out, so every mapped bone points at its next joint
        the way the target's does. Stores the calibrated world rotations."""
        cal = {n: m.copy() for n, m in self.src_rest.items()}
        tgt_rest = self.t.rest_world
        by_src = {sb: (tb, sc, tc) for tb, sb, sc, tc in self.pairs}
        for sb in self.src_order:
            if sb not in by_src:
                continue
            tb, sc, tc = by_src[sb]
            if not sc or not tc or sc not in cal or tc not in tgt_rest:
                continue
            ps = cal[sb].to_translation()
            ds = cal[sc].to_translation() - ps
            dt = tgt_rest[tc].to_translation() - tgt_rest[tb].to_translation()
            if ds.length < 1e-5 or dt.length < 1e-5:
                continue
            q = ds.normalized().rotation_difference(dt.normalized())
            Rq = q.to_matrix().to_4x4()
            Tm = Matrix.Translation(ps) @ Rq @ Matrix.Translation(-ps)
            # apply to the bone and its whole subtree
            stack = [sb]
            while stack:
                n = stack.pop()
                cal[n] = Tm @ cal[n]
                stack.extend(self.children.get(n, []))
        self.cal = cal
        self.cal_rot_inv = {n: m.to_3x3().normalized().inverted().to_4x4() for n, m in cal.items()}
        self.hips_cal = cal["Hips"].to_translation()
        self.pelvis_rest = tgt_rest["pelvis"].to_translation()

    def pose(self, src_world):
        """src_world: {source bone: world matrix in the actor frame (metres)}. Returns {target bone: (world rotation
        3x3, pelvis world position or None)}."""
        out = {}
        tgt_rest = self.t.rest_world
        for tb, sb, _, _ in self.pairs:
            Ms = src_world.get(sb)
            if Ms is None:
                continue
            Rs = Ms.to_3x3().normalized().to_4x4()
            Rt = (Rs @ self.cal_rot_inv[sb] @ tgt_rest[tb].to_3x3().normalized().to_4x4()).to_3x3()
            out[tb] = (Rt, Ms.to_translation() * HIP_SCALE if tb == "pelvis" else None)
        return out

    TORSO_FIX = True

    def apply(self, src_world):
        """Full step: source world matrices (actor frame) -> target basis matrices (pose space) and the armature-space
        pose matrices P. The armature has no rotation of its own (uniform 0.01 scale), so world rotations are
        armature-space rotations; positions chain down the target hierarchy from the pelvis.
        Torso fix: the two spines are shaped differently (the pack's is an S with the spine base behind the hips, the
        UEFN rig's is straight), so with the same joint rotations a bent torso puts the head up to 10 cm lower on the
        target. The whole upper body (spine_01 and everything on it) is turned rigidly so the spine-base -> head
        direction matches the source's; the chain's internal pose is untouched."""
        want = self.pose(src_world)
        if self.TORSO_FIX and "spine_01" in want and "head" in want and "Spine" in src_world and "Head" in src_world:
            basis, P = self._chain(want)
            W = self.t.W
            vt = (W @ P["head"]).to_translation() - (W @ P["spine_01"]).to_translation()
            vs = src_world["Head"].to_translation() - src_world["Spine"].to_translation()
            if vt.length > 1e-4 and vs.length > 1e-4:
                q = vt.normalized().rotation_difference(vs.normalized())
                if q.angle > math.radians(0.5):
                    R = q.to_matrix()
                    upper = set()
                    stack = ["spine_01"]
                    while stack:
                        n = stack.pop()
                        upper.add(n)
                        stack.extend(c for c, p in self.t.parent.items() if p == n)
                    for n in upper:
                        if n in want:
                            rot, pos = want[n]
                            want[n] = (R @ rot, pos)
        return self._chain(want)

    def _chain(self, want):
        t = self.t
        P = {}
        basis = {}
        for n in t.order:
            p = t.parent[n]
            R = t.rest_arm[n]
            if n in want:
                Rw, pos_w = want[n]
                M = Rw.to_4x4()
                if pos_w is not None:
                    M.translation = t.Winv @ pos_w
                elif p:
                    off = t.rest_arm[p].inverted() @ R  # child rest relative to parent rest (armature space)
                    M.translation = (P[p] @ off).to_translation()
                else:
                    M.translation = R.to_translation()
                P[n] = M
            elif n in IK_COPY and IK_COPY[n] in P:
                P[n] = P[IK_COPY[n]].copy()
            else:
                P[n] = (P[p] @ t.rest_arm[p].inverted() @ R) if p else R.copy()
            if p:
                basis[n] = R.inverted() @ t.rest_arm[p] @ P[p].inverted() @ P[n]
            else:
                basis[n] = R.inverted() @ P[n]
        return basis, P


# ----------------------------------------------------------------------------------------------- frames of reference
def traj_frame(M):
    """Ground-plane frame of the trajectory matrix M (Z-up world): position on the ground, heading from local +X.
    Columns: X forward, Y up, Z right."""
    pos = M.to_translation()
    fwd = M.to_3x3() @ Vector((1, 0, 0))
    fwd.z = 0.0
    if fwd.length < 1e-4:
        fwd = Vector((0, -1, 0))
    fwd.normalize()
    right = fwd.cross(Vector((0, 0, 1)))  # forward x up = right
    T = Matrix.Identity(4)
    T.col[0][:3] = fwd
    T.col[1][:3] = Vector((0, 0, 1))
    T.col[2][:3] = right
    T.translation = Vector((pos.x, pos.y, 0.0))
    return T


def heading_deg(T):
    """Heading of a trajectory frame in the actor convention: 0 = facing -Y, positive turns left."""
    f = T.col[0]
    return math.degrees(math.atan2(f[0], -f[1]))


def to_actor(world, T):
    """World matrices -> actor frame given the reference trajectory frame T (local X forward, Y up, Z right)."""
    Ti = T.inverted()
    return {n: TRAJ_TO_ACTOR @ Ti @ m for n, m in world.items()}


def mirror_fixes(rest):
    """Per source bone, the local axis flips that turn the reflected left bone frame into the right bone's frame
    convention (reflection changes handedness; the rig's right-side bones keep right-handed frames, so one axis is
    negated). F = (S R_left S)^-1 R_right from the canonical rest, snapped to a +-1 diagonal."""
    fixes = {}
    for n, m in rest.items():
        mn = mirror_name(n)
        if mn not in rest:
            continue
        A = (MIRROR @ m @ MIRROR).to_3x3().normalized()
        B = rest[mn].to_3x3().normalized()
        F = A.inverted() @ B
        D = Matrix.Identity(4)
        for i in range(3):
            D[i][i] = 1.0 if F[i][i] >= 0 else -1.0
        fixes[n] = D
    return fixes


def mirror_pose(actor, fixes):
    out = {}
    for n, m in actor.items():
        out[mirror_name(n)] = MIRROR @ m @ MIRROR @ fixes.get(n, Matrix.Identity(4))
    return out


# ----------------------------------------------------------------------------------------------- clip build
def build(target, rt, spec, fps_out=FPS_OUT):
    """Retarget one performer per `spec` onto the target rig and key it at fps_out starting at frame 0.
    spec keys: src (clip name), start/end (source seconds; None = whole), fit (target seconds, time scale),
    mode ('inplace' | 'inplace_pos' | 'anchor'), mirror, loop (blend frames, 0 = none), yaw (deg, extra turn about up),
    anchor_frame (source seconds for the anchor frame; default start), lift (m added to the pelvis height).
    Returns (frames, rows, info): rows = per output frame measurements (metres, actor frame)."""
    src = Source(source_path(spec["src"]))
    sfps = src.fps
    s0 = spec.get("start")
    s1 = spec.get("end")
    f_start = src.f0 + (s0 * sfps if s0 is not None else 0.0)
    f_end = src.f0 + (s1 * sfps if s1 is not None else (src.f1 - src.f0))
    f_end = min(f_end, src.f1)
    f_start = max(f_start, src.f0)
    dur_src = (f_end - f_start) / sfps
    fit = spec.get("fit")
    scale = (dur_src / fit) if fit else 1.0  # source seconds per target second
    n_out = max(2, int(round(dur_src / scale * fps_out)) + 1)
    mode = spec.get("mode", "inplace")
    yaw = math.radians(spec.get("yaw", 0.0))
    Ryaw = Matrix.Rotation(yaw, 4, "Z")

    af = spec.get("anchor_frame")
    anchor_src_frame = src.f0 + af * sfps if af is not None else f_start
    src.seek(anchor_src_frame)
    T0 = traj_frame(src.world("AITrajectory"))
    heading_fix = None

    a = target.arm
    bpy.context.view_layer.objects.active = a
    bpy.ops.object.mode_set(mode="POSE")
    keyed = [n for n in target.order]
    rows = []
    frames_src = []
    for i in range(n_out):
        fs = f_start + i * scale * sfps / fps_out
        fs = min(fs, f_end)
        frames_src.append(fs)
        src.seek(fs)
        world = src.all_world()
        if mode == "anchor":
            T = T0
        else:
            T = traj_frame(world["AITrajectory"])
            if mode == "inplace_pos":
                T = T0.copy()
                T.translation = traj_frame(world["AITrajectory"]).translation
        actor = to_actor(world, T)
        if yaw:
            actor = {n: Ryaw @ m for n, m in actor.items()}
        if spec.get("mirror"):
            actor = mirror_pose(actor, rt.mirror_fix)
        lift = spec.get("lift", 0.0)
        if lift:
            for m in actor.values():
                m.translation = m.translation + Vector((0, 0, lift))
        basis, P = rt.apply(actor)
        for n in keyed:
            pb = a.pose.bones[n]
            b = basis[n]
            loc, rot, _ = b.decompose()
            pb.rotation_quaternion = rot
            pb.keyframe_insert("rotation_quaternion", frame=i)
            if n == "pelvis":
                pb.location = loc
                pb.keyframe_insert("location", frame=i)
        W = target.W
        def hw(n):
            return (W @ P[n]).to_translation() if n in P else Vector((0, 0, 0))
        row = {n: tuple(hw(n)) for n in ("pelvis", "head", "foot_l", "foot_r", "hand_l", "hand_r", "upperarm_r", "upperarm_l", "ball_l", "ball_r")}
        # source trajectory (actor frame of the anchor) for travel measurements
        tr = TRAJ_TO_ACTOR @ T0.inverted() @ world["AITrajectory"]
        row["traj"] = tuple(tr.to_translation())
        rows.append(row)
    loop = spec.get("loop", 0)
    if loop:
        blend_loop(a, keyed, n_out, int(loop))
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.data.objects.remove(src.arm)
    info = {"src": spec["src"], "src_fps": sfps, "src_frames": [round(f_start, 2), round(f_end, 2)], "src_seconds": round(dur_src, 3),
            "time_scale": round(scale, 4), "mode": mode, "mirror": bool(spec.get("mirror")), "loop": loop, "frames_out": n_out,
            "anchor": [round(v, 3) for v in T0.to_translation()], "anchor_heading_deg": round(heading_deg(T0), 1)}
    return n_out, rows, info


def blend_loop(arm, bones, n_out, k):
    """Make frame n_out-1 land on frame 0: over the last k frames ease every channel toward the first frame's value."""
    act = arm.animation_data.action if arm.animation_data else None
    if not act:
        return
    fcurves = []
    try:
        fcurves = list(act.fcurves)
    except AttributeError:
        pass
    if not fcurves:  # Blender 4.4+/5: layered actions
        for layer in act.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    fcurves.extend(cb.fcurves)
    for fc in fcurves:
        kps = fc.keyframe_points
        if len(kps) < 2:
            continue
        first = kps[0].co[1]
        n = len(kps)
        for j in range(max(1, n - k), n):
            w = (j - (n - 1 - k)) / float(k)
            w = w * w * (3 - 2 * w)
            kps[j].co[1] = kps[j].co[1] * (1 - w) + first * w
        fc.update()


# ----------------------------------------------------------------------------------------------- measurements
def measure(rows, fps=FPS_OUT):
    def stat(key, i):
        v = [r[key][i] for r in rows]
        return {"min": round(min(v), 3), "max": round(max(v), 3), "start": round(v[0], 3), "end": round(v[-1], 3)}

    slide = {}
    for side in ("l", "r"):
        pts = [r["foot_" + side] for r in rows]
        slide[side] = round(max((math.hypot(a[0] - b[0], a[1] - b[1]) for a, b in zip(pts, pts[1:])
                                 if a[2] < 0.105 and b[2] < 0.105), default=0.0), 4)

    def speed_peak(key):
        best, bf = -1.0, 0
        for i in range(1, len(rows)):
            a, b = rows[i - 1][key], rows[i][key]
            v = math.dist(a, b) * fps
            if v > best:
                best, bf = v, i
        return bf, round(best, 2)

    hr_f, hr_v = speed_peak("hand_r")
    fr_f, fr_v = speed_peak("foot_r")
    fl_f, fl_v = speed_peak("foot_l")
    pel = [r["pelvis"] for r in rows]
    travel = (round(pel[-1][0] - pel[0][0], 3), round(pel[-1][1] - pel[0][1], 3))
    tr = [r["traj"] for r in rows]
    traj_travel = round(math.hypot(tr[-1][0] - tr[0][0], tr[-1][1] - tr[0][1]), 3)
    # moving frames of the trajectory (speed > 0.5 m/s)
    moving = sum(1 for a, b in zip(tr, tr[1:]) if math.hypot(a[0] - b[0], a[1] - b[1]) * fps > 0.5)
    return {"pelvis_z": stat("pelvis", 2), "head_z": stat("head", 2), "hand_r_z": stat("hand_r", 2),
            "shoulder_r": [round(v, 3) for v in rows[0]["upperarm_r"]], "hand_r": [round(v, 3) for v in rows[0]["hand_r"]],
            "max_planted_foot_step_m": slide, "pelvis_travel_xy_m": travel, "pelvis_end": [round(v, 3) for v in pel[-1]],
            "pelvis_start": [round(v, 3) for v in pel[0]], "head_start": [round(v, 3) for v in rows[0]["head"]],
            "head_end": [round(v, 3) for v in rows[-1]["head"]],
            "traj_travel_m": traj_travel, "traj_moving_frames": moving,
            "events": {"hand_r_peak_speed": {"frame": hr_f, "m_s": hr_v}, "foot_r_peak_speed": {"frame": fr_f, "m_s": fr_v},
                       "foot_l_peak_speed": {"frame": fl_f, "m_s": fl_v},
                       "pelvis_min_frame": min(range(len(pel)), key=lambda i: pel[i][2]),
                       "hand_r_max_z_frame": max(range(len(rows)), key=lambda i: rows[i]["hand_r"][2])}}


# ----------------------------------------------------------------------------------------------- output
def export(target, path, n_out, fps=FPS_OUT):
    sc = bpy.context.scene
    sc.render.fps = fps
    sc.render.fps_base = 1.0
    sc.frame_start, sc.frame_end = 0, n_out - 1
    a = target.arm
    bpy.ops.object.mode_set(mode="OBJECT")
    for o in bpy.data.objects:
        o.select_set(o == a)
    bpy.context.view_layer.objects.active = a
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE"}, add_leaf_bones=False,
                             bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
                             bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
                             bake_anim_simplify_factor=0.0, apply_unit_scale=True, global_scale=1.0,
                             armature_nodetype="NULL")


DISPLAY_SKIP = ("ik_", "weapon", "attach", "props", "poi", "twist", "metacarpal")


def build_display(target):
    """Stick figure parented to bones, for workbench renders (same look as Tools/Anim/tag_anim.py)."""
    a = target.arm
    bpy.ops.object.mode_set(mode="OBJECT")
    a.data.pose_position = "REST"
    bpy.context.view_layer.update()
    rest = target.rest_world

    def parent_to_bone(obj, bone):
        mw = obj.matrix_world.copy()
        obj.parent = a
        obj.parent_type = "BONE"
        obj.parent_bone = bone
        bpy.context.view_layer.update()
        obj.matrix_world = mw

    for pb in a.pose.bones:
        if any(s in pb.name for s in DISPLAY_SKIP) or not pb.parent or any(s in pb.parent.name for s in DISPLAY_SKIP):
            continue
        if pb.name.startswith(("index", "middle", "pinky", "ring", "thumb")):
            continue
        p0 = rest[pb.parent.name].to_translation()
        p1 = rest[pb.name].to_translation()
        v = p1 - p0
        if v.length < 0.02:
            continue
        r = 0.05 if pb.parent.name.startswith(("spine", "pelvis")) else 0.035
        bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=v.length, location=(p0 + p1) / 2)
        cyl = bpy.context.active_object
        cyl.rotation_mode = "QUATERNION"
        cyl.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(v)
        parent_to_bone(cyl, pb.parent.name)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.11, location=rest["head"].to_translation() + Vector((0, -0.02, 0.09)))
    parent_to_bone(bpy.context.active_object, "head")
    bpy.ops.mesh.primitive_cone_add(radius1=0.03, depth=0.08, location=rest["head"].to_translation() + Vector((0, -0.13, 0.08)))
    nose = bpy.context.active_object
    nose.rotation_euler = (math.radians(90), 0, 0)
    parent_to_bone(nose, "head")
    for side in ("l", "r"):
        f = rest["foot_" + side].to_translation()
        bpy.ops.mesh.primitive_cube_add(size=1, location=f + Vector((0, -0.08, -0.06)))
        c = bpy.context.active_object
        c.scale = (0.09, 0.24, 0.07)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        parent_to_bone(c, "foot_" + side)
    bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
    a.data.pose_position = "POSE"
    bpy.context.view_layer.update()


def render(target, frames, out_dir, views=(("side", (1, 0, 0.05)), ("front", (0, -1, 0.05)), ("3q", (0.7, -0.7, 0.35)))):
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
    a = target.arm
    out = []
    for vname, vdir in views:
        cam_data.lens = 45
        for f in frames:
            sc.frame_set(f)
            aim = (a.matrix_world @ a.pose.bones["pelvis"].head) + Vector((0, 0, 0.05))
            cam.location = aim + Vector(vdir).normalized() * 2.6
            cam.rotation_euler = (aim - cam.location).to_track_quat("-Z", "Y").to_euler()
            p = os.path.join(out_dir, "%s_%03d.png" % (vname, f))
            sc.render.filepath = p
            bpy.ops.render.render(write_still=True)
            out.append(p)
    return out
