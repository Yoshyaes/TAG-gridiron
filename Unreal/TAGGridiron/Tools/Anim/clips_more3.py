"""TAG football clips, batch 5 (hand keyed, original choreography from line play technique): the fight in the trenches.
Same conventions as clips.py: facing -Y, left +X, up +Z, meters, 30 fps, ankles at z 0.094, positive yaw turns left.
Every clip plays in place (the sim moves the actor).

Engaged loops, picked by who is winning the rep:
  Rush_BullDrive_Loop   the rusher is driving his blocker into the pocket: pads low, arms locked out, legs churning
  Block_Anchor_Loop     the blocker giving ground against power: hips sunk, wide base, short steps back, hands inside
  Block_Drive_Loop      run block won: hands inside, pads under the defender's, legs driving him off the ball
  Block_DrivenBack_Loop the defender losing the run rep: pad level lost, upright, feet skidding back
One shots:
  Block_Punch           the strike at first contact (both sides): hands shoot from the set to the chest plate
  Rush_LongArm(_M)      power rusher: inside arm locks out on the chest, walks the blocker back, then sheds past
  Rush_DipRip(_M)       edge rusher: dips the inside shoulder, rips the inside arm up through, bends the corner
  Block_Beaten(_M)      the blocker who loses the edge: opens his hips, lunges and reaches, turns to chase
  Block_Finish          pancake finish: full extension shove and follow-through, then settle
"""
import math

import clips as C
import clips_more as M

ANKLE = M.ANKLE
_base, _run, _key_all, _rot = M._base, M._run, M._key_all, M._rot


def _lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def _path(keys):
    """Pelvis path from sparse keys [(frame, (x, y, z), pitch, yaw, roll)]: linear between keys."""
    def at(f):
        if f <= keys[0][0]:
            k = keys[0]
            return k[1], k[2], k[3], k[4]
        for (f0, p0, a0, y0, r0), (f1, p1, a1, y1, r1) in zip(keys, keys[1:]):
            if f <= f1:
                t = (f - f0) / float(f1 - f0)
                return _lerp(p0, p1, t), a0 + (a1 - a0) * t, y0 + (y1 - y0) * t, r0 + (r1 - r0) * t
        k = keys[-1]
        return k[1], k[2], k[3], k[4]
    return at


def _feet_on_path(at, f, step=5, width=0.15, lift=0.10, first="l"):
    """Footsteps that follow the pelvis path: each foot plants under the body (half a step ahead of mid-stance),
    stays for one step, then swings to its next plant. Returns the foot keys for frame f."""
    out = {}
    for side, sx, off in (("l", 1, 0 if first == "l" else step), ("r", -1, step if first == "l" else 0)):
        cyc = 2 * step
        n = math.floor((f - off) / cyc)
        p0 = off + n * cyc

        def plant(p):
            (x, y, _z), _pitch, yaw, _roll = at(p + step * 0.5)
            dx, dy = _rot(sx * width, 0.0, yaw)
            return (x + dx, y + dy, ANKLE), yaw
        a, ya = plant(p0)
        if f - p0 < step:
            out["foot_" + side] = a
            out["foot_yaw_" + side] = ya
            out["foot_pitch_" + side] = 10 + 30 * (f - p0) / step
        else:
            b, yb = plant(p0 + cyc)
            s = (f - p0 - step) / float(step)
            x, y, _ = _lerp(a, b, s)
            out["foot_" + side] = (x, y, ANKLE + lift * math.sin(math.pi * s))
            out["foot_yaw_" + side] = ya + (yb - ya) * s
            out["foot_pitch_" + side] = 40 - 30 * s
    return out


def _body(at, f, **kw):
    (x, y, z), pitch, yaw, roll = at(f)
    d = _base(pelvis=(x, y, z), pelvis_rot=(pitch, yaw, roll), knee_l=(0.35, -1, 0), knee_r=(-0.35, -1, 0))
    d.update(kw)
    return d


def _local(at, f, x, y, z):
    """A point given in the pelvis's turned frame (x left, y forward negative) at frame f, in clip space."""
    (px, py, _pz), _pitch, yaw, _roll = at(f)
    dx, dy = _rot(x, y, yaw)
    return (px + dx, py + dy, z)


# ============================================================================================== engaged loops
def _engaged_loop(rig, speed, pelvis_z, lean, width, hands, elbow, spine, look_z, clav=(2, 14), push=0.03):
    """Two strides (32 frames, 3.75 steps/s) of in-place footwork against a man, hands fixed on him; push > 0 adds
    a punch of the hands with each step (driving), < 0 a give."""
    T = 8.0

    def ov(f, t):
        py = -speed * t
        pulse = push * math.sin(2 * math.pi * f / T)
        hx, hy, hz = hands
        return {"hand_l": (hx, py + hy - pulse, hz), "hand_r": (-hx, py + hy - pulse, hz),
                "elbow_l": elbow, "elbow_r": (-elbow[0], elbow[1], elbow[2]), "clav_l": clav, "clav_r": clav,
                "spine": spine, "look": (0.0, py - 3.0, look_z), "knee_l": (0.35, -1, 0), "knee_r": (-0.35, -1, 0)}
    return _key_all(rig, _run(32, speed=speed, cadence=3.75, pelvis_z=pelvis_z, lean=lean, width=width, arms=False,
                              overrides=ov))


def rush_bull_drive(rig):
    """The rusher winning with power: pads under the blocker's, both arms locked out on his chest, short powerful
    steps driving him back toward the quarterback."""
    return _engaged_loop(rig, speed=1.2, pelvis_z=0.78, lean=40, width=0.15, hands=(0.15, -0.64, 1.10),
                         elbow=(1, 0.0, -1), spine=(12, 0, 0), look_z=1.15, clav=(2, 18), push=0.04)


def block_anchor(rig):
    """The blocker absorbing a bull rush: hips sink, base widens, torso stays up, hands inside on the chest plate,
    short choppy steps giving ground."""
    return _engaged_loop(rig, speed=-0.9, pelvis_z=0.72, lean=22, width=0.18, hands=(0.13, -0.50, 1.12),
                         elbow=(1, 0.4, -0.8), spine=(2, 0, 0), look_z=1.35, push=-0.02)


def block_drive(rig):
    """Run block won: hands inside, pads under the defender's, legs churning, driving him off the ball."""
    return _engaged_loop(rig, speed=1.6, pelvis_z=0.80, lean=36, width=0.16, hands=(0.12, -0.58, 1.14),
                         elbow=(1, 0.1, -1), spine=(10, 0, 0), look_z=1.2, clav=(2, 16), push=0.05)


def block_driven_back(rig):
    """Losing the run rep: pad level lost, chest up, hands pushing at the blocker's shoulders, feet skidding back."""
    return _engaged_loop(rig, speed=-1.4, pelvis_z=0.88, lean=12, width=0.15, hands=(0.18, -0.42, 1.30),
                         elbow=(1, 0.6, -0.4), spine=(-4, 0, 0), look_z=1.5, clav=(4, 8), push=-0.03)


# ============================================================================================== one shots
def block_punch(rig):
    """The strike at contact, 12 frames: from the set (hands up, thumbs up) both hands shoot to the chest plate with
    a short power step, hips sink, then settle into the engaged position (Block_Engaged_Loop's first pose)."""
    set_feet = {"foot_l": (0.26, -0.08, ANKLE), "foot_r": (-0.30, 0.16, ANKLE)}
    keys = [
        (0, _base(pelvis=(0.0, 0.04, 0.80), pelvis_rot=(22, 0, 0), spine=(4, 0, 0), hand_l=(0.16, -0.32, 1.12),
                  hand_r=(-0.16, -0.32, 1.12), elbow_l=(1, 0.4, -0.8), elbow_r=(-1, 0.4, -0.8),
                  knee_l=(0.35, -1, 0), knee_r=(-0.35, -1, 0), look=(0.0, -6.0, 1.4), **set_feet)),
        (3, _base(pelvis=(0.0, 0.0, 0.78), pelvis_rot=(30, 0, 0), spine=(8, 0, 0), hand_l=(0.12, -0.54, 1.10),
                  hand_r=(-0.12, -0.54, 1.10), elbow_l=(1, 0.2, -1), elbow_r=(-1, 0.2, -1), clav_l=(2, 14),
                  clav_r=(2, 14), knee_l=(0.35, -1, 0), knee_r=(-0.35, -1, 0), look=(0.0, -3.0, 1.3),
                  foot_l=(0.27, -0.14, 0.13), foot_r=(-0.30, 0.14, ANKLE), foot_pitch_l=20)),
        (5, _base(pelvis=(0.0, -0.05, 0.77), pelvis_rot=(36, 0, 0), spine=(10, 0, 0), hand_l=(0.11, -0.72, 1.10),
                  hand_r=(-0.11, -0.72, 1.10), elbow_l=(1, 0.0, -1), elbow_r=(-1, 0.0, -1), clav_l=(2, 20),
                  clav_r=(2, 20), knee_l=(0.35, -1, 0), knee_r=(-0.35, -1, 0), look=(0.0, -3.0, 1.25),
                  foot_l=(0.28, -0.18, ANKLE), foot_r=(-0.29, 0.10, ANKLE), foot_pitch_r=15)),
        (8, _base(pelvis=(0.0, -0.02, 0.78), pelvis_rot=(35, 0, 0), spine=(9, 0, 0), hand_l=(0.12, -0.56, 1.09),
                  hand_r=(-0.12, -0.56, 1.09), elbow_l=(1, 0.3, -1), elbow_r=(-1, 0.3, -1), clav_l=(2, 15),
                  clav_r=(2, 15), knee_l=(0.35, -1, 0), knee_r=(-0.35, -1, 0), look=(0.0, -3.0, 1.3),
                  foot_l=(0.28, -0.15, ANKLE), foot_r=(-0.28, 0.06, 0.12), foot_pitch_r=20)),
        (12, _base(pelvis=(0.0, 0.0, 0.78), pelvis_rot=(34, 0, 0), spine=(8, 0, 0), clav_l=(2, 14), clav_r=(2, 14),
                   hand_l=(0.12, -0.50, 1.08), hand_r=(-0.12, -0.50, 1.08), elbow_l=(1, 0.3, -1), elbow_r=(-1, 0.3, -1),
                   foot_l=(0.28, -0.12, ANKLE), foot_r=(-0.28, 0.06, ANKLE), foot_pitch_l=10, foot_pitch_r=14,
                   knee_l=(0.35, -1, 0), knee_r=(-0.35, -1, 0), look=(0.0, -3.0, 1.3))),
    ]
    return _key_all(rig, C._in_place(keys))


def rush_long_arm(rig):
    """Long arm, 26 frames: the right (inside) arm shoots and locks out on the blocker's chest with the right shoulder
    turned forward, two drive steps walk him back, then the left hand swipes his hands off the long arm and the
    rusher turns the corner past him to the left."""
    at = _path([(0, (0.0, 0.0, 0.80), 36, 0, 0), (4, (0.0, -0.10, 0.78), 38, 22, 0), (12, (0.02, -0.75, 0.78), 38, 24, 0),
                (17, (0.22, -1.10, 0.80), 34, 0, 6), (21, (0.40, -1.55, 0.84), 28, -12, 4), (26, (0.42, -2.15, 0.88), 22, -8, 0)])
    keys = []
    for f in range(0, 27):
        d = _body(at, f, look=_local(at, f, 0.0, -4.0, 1.25), spine=(10, 0, 0))
        if f <= 4:
            s = f / 4.0
            d["hand_r"] = _lerp(_local(at, f, -0.13, -0.50, 1.08), _local(at, f, -0.06, -0.76, 1.17), s)
            d["hand_l"] = _lerp(_local(at, f, 0.13, -0.50, 1.08), _local(at, f, 0.24, -0.20, 0.98), s)
            d["elbow_r"], d["clav_r"] = (-1, -0.1, -1), (2, 22 * s)
        elif f <= 13:
            d["hand_r"] = _local(at, f, -0.06, -0.76, 1.17)
            d["hand_l"] = _local(at, f, 0.24, -0.20, 0.98)
            d["elbow_r"], d["clav_r"] = (-1, -0.1, -1), (2, 22)
        elif f <= 18:
            s = (f - 13) / 5.0
            d["hand_l"] = _lerp(_local(at, f, 0.24, -0.20, 0.98), _local(at, f, -0.10, -0.62, 1.25), s)
            d["hand_r"] = _lerp(_local(at, f, -0.06, -0.76, 1.17), _local(at, f, -0.30, -0.30, 0.92), s)
            d["clav_l"] = (6, 14 * s)
        else:
            d["hand_l"] = _local(at, f, 0.22, -0.30, 1.00)
            d["hand_r"] = _local(at, f, -0.22, -0.10, 0.98)
        d.update(_feet_on_path(at, f, step=5, width=0.15, lift=0.10, first="l"))
        keys.append((f, d))
    return _key_all(rig, C._in_place(keys))


def rush_dip_rip(rig):
    """Dip and rip off the edge, 22 frames: one stride wide, the inside (right) shoulder dips under the blocker's
    hands, the right arm uppercuts up through his arm, the rusher leans and bends the corner back toward the quarterback
    (to the right)."""
    at = _path([(0, (0.0, 0.0, 0.84), 30, 0, 0), (5, (0.22, -0.55, 0.78), 40, 12, -6), (10, (0.30, -1.20, 0.72), 46, 4, -18),
                (15, (0.18, -1.90, 0.76), 40, -22, -14), (22, (-0.10, -2.75, 0.84), 30, -40, -6)])
    keys = []
    for f in range(0, 23, 1):
        d = _body(at, f, look=_local(at, f, -1.5, -4.0, 1.2), spine=(12, 0, -8 if 5 <= f <= 15 else 0))
        if f <= 5:
            s = f / 5.0
            d["hand_r"] = _lerp(_local(at, f, -0.20, -0.30, 1.00), _local(at, f, -0.16, -0.10, 0.80), s)
            d["hand_l"] = _local(at, f, 0.24, -0.40, 1.05)
        elif f <= 11:
            s = (f - 5) / 6.0
            d["hand_r"] = _lerp(_local(at, f, -0.16, -0.10, 0.80), _local(at, f, -0.08, -0.40, 1.45), s)
            d["hand_l"] = _local(at, f, 0.26, -0.20, 1.00)
            d["elbow_r"] = (-1, 0.2, 0.4)
        else:
            s = min(1.0, (f - 11) / 6.0)
            d["hand_r"] = _lerp(_local(at, f, -0.08, -0.40, 1.45), _local(at, f, -0.22, -0.20, 1.00), s)
            d["hand_l"] = _local(at, f, 0.24, -0.10, 1.00)
        d.update(_feet_on_path(at, f, step=4, width=0.14, lift=0.14, first="l"))
        keys.append((f, d))
    return _key_all(rig, C._in_place(keys))


def block_beaten(rig):
    """Beaten on the outside, 22 frames: the rusher gets past on the blocker's left; he opens his hips late, lunges and
    reaches with the left arm, overextended over his toes, then turns and gives chase."""
    at = _path([(0, (0.0, 0.0, 0.78), 34, 0, 0), (5, (0.10, -0.06, 0.76), 40, 18, 6), (10, (0.30, -0.12, 0.74), 46, 42, 10),
                (16, (0.40, -0.08, 0.80), 30, 70, 4), (22, (0.48, 0.02, 0.86), 20, 90, 0)])
    keys = []
    for f in range(0, 23, 1):
        d = _body(at, f, look=_local(at, f, 2.5, -2.0, 1.3), spine=(12, 10, 4))
        if f <= 10:
            s = f / 10.0
            d["hand_l"] = _lerp(_local(at, f, 0.12, -0.50, 1.08), _local(at, f, 0.30, -0.80, 1.00), s)
            d["hand_r"] = _lerp(_local(at, f, -0.12, -0.50, 1.08), _local(at, f, 0.05, -0.62, 1.04), s)
            d["elbow_l"], d["clav_l"] = (1, 0.0, -1), (4, 22 * s)
        else:
            s = min(1.0, (f - 10) / 8.0)
            d["hand_l"] = _lerp(_local(at, f, 0.30, -0.80, 1.00), _local(at, f, 0.22, -0.20, 1.00), s)
            d["hand_r"] = _lerp(_local(at, f, 0.05, -0.62, 1.04), _local(at, f, -0.22, -0.15, 1.00), s)
        d.update(_feet_on_path(at, f, step=5, width=0.17, lift=0.08, first="l"))
        keys.append((f, d))
    return _key_all(rig, C._in_place(keys))


def block_finish(rig):
    """Pancake finish, 20 frames: from the engaged position the blocker drives through with both hands at full
    extension and a follow-through step, pads low, then settles upright over the man he put down."""
    at = _path([(0, (0.0, 0.0, 0.78), 34, 0, 0), (6, (0.0, -0.40, 0.74), 46, 0, 0), (11, (0.0, -0.75, 0.76), 40, 0, 0),
                (20, (0.0, -0.95, 0.92), 14, 0, 0)])
    keys = []
    for f in range(0, 21, 1):
        d = _body(at, f, look=_local(at, f, 0.0, -1.5, 0.3 if f > 8 else 1.0), spine=(10, 0, 0))
        if f <= 6:
            s = f / 6.0
            for side, sx in (("l", 1), ("r", -1)):
                d["hand_" + side] = _lerp(_local(at, f, sx * 0.12, -0.50, 1.08), _local(at, f, sx * 0.12, -0.84, 0.98), s)
                d["elbow_" + side] = (sx, 0.0, -1)
                d["clav_" + side] = (2, 20 * s)
        else:
            s = min(1.0, (f - 6) / 10.0)
            for side, sx in (("l", 1), ("r", -1)):
                d["hand_" + side] = _lerp(_local(at, f, sx * 0.12, -0.84, 0.98), _local(at, f, sx * 0.24, -0.20, 1.02), s)
        d.update(_feet_on_path(at, f, step=5, width=0.16, lift=0.09, first="l"))
        keys.append((f, d))
    return _key_all(rig, C._in_place(keys))


LINE = {
    "Rush_BullDrive_Loop": rush_bull_drive, "Block_Anchor_Loop": block_anchor, "Block_Drive_Loop": block_drive,
    "Block_DrivenBack_Loop": block_driven_back, "Block_Punch": block_punch, "Rush_LongArm": rush_long_arm,
    "Rush_DipRip": rush_dip_rip, "Block_Beaten": block_beaten, "Block_Finish": block_finish,
}
for _n in ("Rush_LongArm", "Rush_DipRip", "Block_Beaten"):
    LINE[_n + "_M"] = M._mirrored(LINE[_n])
# the earlier rush moves, to the other side (a move is picked by the side the rusher beats his man on)
LINE["Shed_Swim_M"] = M._mirrored(C.shed_swim)
LINE["Shed_Rip_M"] = M._mirrored(C.shed_rip)
LINE["Rush_Club_M"] = M._mirrored(M.rush_club)
