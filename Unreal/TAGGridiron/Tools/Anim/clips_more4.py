"""TAG football clips, batch 6 (hand keyed, original): paired tackles that go all the way to the ground.

Each tackle is authored as a PAIR in one shared space so the contact is real: the tackler's hands are keyed onto the
ball carrier's body (waist, back, thighs) frame by frame, both men are driven by the same momentum and they land on
and against each other. Both clips keep their travel (NOT in place): the renderer anchors both actors at the hit
(the carrier's actor at the hit spot facing his run, the tackler's actor placed from PAIRS below) and lets the clips
carry the bodies to the turf. Every fall ends in one of the keyed get-ups' first poses (face down with the head
forward, face up with the head back), shifted by the travel, so the get-up chains out of the landing with no ragdoll.

Conventions as clips.py: each clip in its own actor frame, facing -Y, left +X, up +Z, meters, 30 fps.

PAIRS (the carrier's actor relative to the tackler's, in the tackler's frame): offset (x, y) and the carrier's yaw
relative to the tackler (degrees, positive turns left). A point p in the carrier's frame is, in the tackler's frame,
offset + Rz(yaw) p. The renderer mirrors these numbers (AMatchDirector::StartPairedTackle).

  Drive      head on, no yards after contact: the tackler drives through, the carrier goes down on his back, the
             tackler lands on top of him                                              carrier ahead, facing the tackler
  Over       head on, the carrier still falling forward: the tackler goes low at the thighs, the carrier pitches
             forward over his back                                                   carrier ahead, facing the tackler
  Behind     from behind: wrapped at the waist, the carrier pitches forward onto his face, the tackler lands on his
             legs                                                                    carrier ahead, same facing
  Side(_M)   from the carrier's right (left): knocked off his feet sideways, turned as he goes down, face down;
             the tackler lands across his back                                       carrier ahead, crossing
  Dive       shoestring: the tackler dives full length at the ankles, the carrier trips and falls forward
  PileOn     a second man arriving: launches and lands on top of the pile
"""
import math

import clips as C
import clips_more as M

ANKLE = M.ANKLE
_base, _tuck = M._base, M._tuck

PAIRS = {
    "Drive": {"offset": (0.0, -0.58), "yaw": 180.0},
    "Over": {"offset": (-0.20, -0.70), "yaw": 180.0},  # head across, beside his hip
    "Behind": {"offset": (0.0, -0.55), "yaw": 0.0},
    "Side": {"offset": (0.0, -0.55), "yaw": -90.0},
    "Dive": {"offset": (0.0, -1.35), "yaw": 0.0},
}


# ------------------------------------------------------------------------------------------------ geometry
def _rz(p, deg):
    a = math.radians(deg)
    x, y = p[0], p[1]
    out = (x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a))
    return out + tuple(p[2:])


def _pyl(v, pitch, yaw, lean):
    """tag_anim.pyl applied to a vector: lean about +Y, then pitch about +X (positive tips the top toward -Y), then yaw."""
    x, y, z = v
    a = math.radians(lean)
    x, z = x * math.cos(a) + z * math.sin(a), -x * math.sin(a) + z * math.cos(a)
    a = math.radians(pitch)
    y, z = y * math.cos(a) - z * math.sin(a), y * math.sin(a) + z * math.cos(a)
    return _rz((x, y, z), yaw)


def body(key, local):
    """A point on a body given relative to its pelvis in the pelvis frame (x left, y back, z up the spine)."""
    px, py, pz = key["pelvis"]
    p, y, l = key.get("pelvis_rot", (0, 0, 0))
    dx, dy, dz = _pyl(local, p, y, l)
    return (px + dx, py + dy, pz + dz)


def to_tackler(pair, p):
    ox, oy = PAIRS[pair]["offset"]
    x, y, z = _rz(p, PAIRS[pair]["yaw"])
    return (ox + x, oy + y, z)


def _lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def _interp(keys, f):
    """Linear interpolation of the numeric tuples in a sparse key list [(frame, dict)] at frame f (for contact)."""
    for (f0, k0), (f1, k1) in zip(keys, keys[1:]):
        if f0 <= f <= f1:
            t = (f - f0) / float(f1 - f0)
            out = {}
            for n, v in k1.items():
                a = k0.get(n, v)
                out[n] = _lerp(a, v, t) if isinstance(v, tuple) else (a + (v - a) * t if isinstance(v, (int, float)) else v)
            return out
    return dict(keys[-1][1] if f > keys[-1][0] else keys[0][1])


def _k(**kw):
    d = {"knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0), "elbow_l": (1, 0.3, -0.5), "elbow_r": (-1, 0.3, -0.5)}
    d.update(kw)
    return d


# lying poses: the first keys of the keyed get-ups (clips.getup_face_down / getup_face_up), relative to the pelvis
def face_down(px, py, yaw=0.0, lift=0.0, hands=True, feet=None):
    """Face down, head toward the actor's forward turned by yaw, pelvis at (px, py) (+lift when lying on someone)."""
    def at(dx, dy, z):
        x, y = _rz((dx, dy), yaw)
        return (px + x, py + y, z)
    fb = feet if feet is not None else (0.78 if lift < 0.05 else 0.66)
    d = _k(pelvis=(px, py, 0.15 + lift), pelvis_rot=(92, yaw, 0), spine=(-6, 0, 0),
           foot_l=at(0.13, fb, 0.06 + lift * 0.3), foot_r=at(-0.13, fb, 0.06 + lift * 0.3),
           foot_pitch_l=70, foot_pitch_r=70, foot_yaw_l=yaw, foot_yaw_r=yaw,
           elbow_l=(1, 0.6, 0), elbow_r=(-1, 0.6, 0), knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1),
           look=at(0.0, -4.0, 0.3))
    if hands:
        d["hand_l"] = at(0.26, -0.40, 0.07 + lift)
        d["hand_r"] = at(-0.26, -0.40, 0.07 + lift)
    return d


def face_up(px, py, yaw=0.0):
    def at(dx, dy, z):
        x, y = _rz((dx, dy), yaw)
        return (px + x, py + y, z)
    return _k(pelvis=(px, py, 0.14), pelvis_rot=(-86, yaw, 0), spine=(4, 0, 0),
              hand_l=at(0.40, 0.30, 0.07), hand_r=at(-0.40, 0.30, 0.07),
              foot_l=at(0.14, -0.78, 0.07), foot_r=at(-0.14, -0.78, 0.07), foot_yaw_l=yaw, foot_yaw_r=yaw,
              elbow_l=(1, 0.4, -1), elbow_r=(-1, 0.4, -1), knee_l=(0.2, -0.2, 1), knee_r=(-0.2, -0.2, 1),
              look=at(0.0, 2.0, 2.0))


def _running(py=0.0, ball=True):
    """The ball carrier at speed, in stride, ball high and tight in the right arm."""
    d = _k(pelvis=(0.0, py, 0.90), pelvis_rot=(16, 0, 0), spine=(6, 0, 0), look=(0.0, py - 6.0, 1.4),
           hand_l=(0.24, py - 0.14, 1.02), elbow_l=(1, 0.6, -0.4),
           foot_l=(0.12, py - 0.24, ANKLE), foot_r=(-0.12, py + 0.26, 0.16), foot_pitch_r=45)
    if ball:
        d.update(_tuck(py))
    return d


def _key(rig, keys, mirror=False):
    for f, k in keys:
        rig.key(f, C.mirror(k) if mirror else k)
    return keys[0][0], keys[-1][0]


def _hold(keys, until):
    f, k = keys[-1]
    if until > f:
        keys.append((until, dict(k)))
    return keys


def _ball(keys, upto):
    """Ball arm locked to the chest, following the body down (frames 3 .. upto-1)."""
    for f, k in keys:
        if 3 <= f < upto:
            k["hand_r"] = body(k, (-0.12, -0.12, 0.32))
            k["elbow_r"] = (-1, 0.8, -0.2)
    return keys


# ============================================================================================== DRIVE (head on)
DRIVE_C = [
    (0, _running()),
    (3, _k(pelvis=(0.0, 0.05, 0.88), pelvis_rot=(2, 0, 0), spine=(-14, 0, 0), look=(0.0, -2.0, 2.4),
           hand_l=(0.34, -0.02, 1.12), elbow_l=(1, 0.3, -0.5), **_tuck(0.05),
           foot_l=(0.12, -0.12, ANKLE), foot_r=(-0.12, 0.20, 0.12), foot_pitch_r=30)),
    (8, _k(pelvis=(0.0, 0.32, 0.80), pelvis_rot=(-20, 0, 0), spine=(-10, 0, 0), look=(0.0, -1.0, 2.6),
           hand_l=(0.40, 0.20, 1.14), elbow_l=(1, 0.2, -0.3), **_tuck(0.32),
           foot_l=(0.13, 0.02, 0.12), foot_r=(-0.13, 0.42, ANKLE), foot_pitch_l=20, knee_l=(0.2, -1, 0.3))),
    (14, _k(pelvis=(0.0, 0.62, 0.60), pelvis_rot=(-46, 0, 0), spine=(-4, 0, 0), look=(0.0, -1.0, 2.6),
            hand_l=(0.46, 0.70, 0.86), elbow_l=(1, 0.0, -0.3), **_tuck(0.62),
            foot_l=(0.14, 0.18, 0.24), foot_r=(-0.13, 0.46, ANKLE), foot_pitch_l=10, knee_l=(0.2, -1, 0.6))),
    (19, _k(pelvis=(0.0, 0.86, 0.34), pelvis_rot=(-70, 0, 0), spine=(4, 0, 0), look=(0.0, -0.5, 2.4),
            hand_l=(0.46, 1.18, 0.30), elbow_l=(1, 0.2, 0.2), hand_r=(-0.12, 1.22, 0.52), elbow_r=(-1, 0.4, -0.6),
            foot_l=(0.14, 0.22, 0.34), foot_r=(-0.14, 0.24, 0.26), knee_l=(0.2, -0.3, 1), knee_r=(-0.2, -0.3, 1))),
    (23, _k(pelvis=(0.0, 1.00, 0.17), pelvis_rot=(-84, 0, 0), spine=(8, 0, 0), look=(0.0, 0.0, 2.6),
            hand_l=(0.44, 1.28, 0.09), elbow_l=(1, 0.4, -1), hand_r=(-0.12, 1.36, 0.26), elbow_r=(-1, 0.4, -0.6),
            foot_l=(0.14, 0.16, 0.16), foot_r=(-0.14, 0.14, 0.14), knee_l=(0.2, -0.2, 1), knee_r=(-0.2, -0.2, 1))),
    (26, _k(pelvis=(0.0, 1.00, 0.19), pelvis_rot=(-80, 0, 0), spine=(14, 0, 0), look=(0.0, 0.0, 2.6),
            hand_l=(0.44, 1.28, 0.08), elbow_l=(1, 0.4, -1), hand_r=(-0.12, 1.34, 0.30), elbow_r=(-1, 0.4, -0.6),
            foot_l=(0.14, 0.14, 0.10), foot_r=(-0.14, 0.12, 0.10), knee_l=(0.2, -0.2, 1), knee_r=(-0.2, -0.2, 1))),
    (34, face_up(0.0, 1.00)),
]
_ball(DRIVE_C, 19)


def _drive_t():
    """The tackler: sink, strike through the carrier's chest with the head across, wrap behind his back, run the feet
    through him, ride him down and land on top, chest on his stomach, arms still around him."""
    c = lambda f, loc: to_tackler("Drive", body(_interp(DRIVE_C, f), loc))
    back = lambda f, s: c(f, (s * 0.16, 0.13, 0.12) if f < 20 else (s * 0.21, 0.02, 0.10))  # behind his waist, then his sides
    look = lambda f: c(f, (0.0, 0.0, 0.5))
    keys = [
        (0, _k(pelvis=(0.0, 0.0, 0.86), pelvis_rot=(30, 0, 0), spine=(8, 0, 0), look=look(0),
               hand_l=(0.26, -0.18, 0.92), hand_r=(-0.26, -0.18, 0.92), elbow_l=(1, 0.6, -0.2), elbow_r=(-1, 0.6, -0.2),
               foot_l=(0.15, -0.22, ANKLE), foot_r=(-0.15, 0.24, 0.12), foot_pitch_r=35)),
        (3, _k(pelvis=(0.0, -0.10, 0.80), pelvis_rot=(42, 0, 0), spine=(12, 0, 0), look=look(3), clav_l=(4, 14), clav_r=(4, 14),
               hand_l=c(3, (0.22, 0.02, 0.10)), hand_r=c(3, (-0.22, 0.02, 0.10)), elbow_l=(1, 0.2, -0.3), elbow_r=(-1, 0.2, -0.3),
               foot_l=(0.15, -0.36, ANKLE), foot_r=(-0.15, 0.10, 0.10), foot_pitch_r=45)),
        (8, _k(pelvis=(0.0, -0.36, 0.76), pelvis_rot=(54, 0, 0), spine=(14, 0, 0), look=look(8), clav_l=(2, 20), clav_r=(2, 20),
               hand_l=back(8, 1), hand_r=back(8, -1), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
               foot_l=(0.15, -0.50, ANKLE), foot_r=(-0.15, -0.14, 0.14), foot_pitch_r=55, foot_pitch_l=20)),
        (14, _k(pelvis=(0.0, -0.66, 0.62), pelvis_rot=(66, 0, 0), spine=(14, 0, 0), look=look(14), clav_l=(2, 20), clav_r=(2, 20),
                hand_l=back(14, 1), hand_r=back(14, -1), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
                foot_l=(0.15, -0.58, 0.12), foot_r=(-0.15, -0.40, ANKLE), foot_pitch_r=60, foot_pitch_l=30)),
        (19, _k(pelvis=(0.0, -0.92, 0.46), pelvis_rot=(78, 0, 0), spine=(10, 0, 0), look=look(19), clav_l=(2, 18), clav_r=(2, 18),
                hand_l=back(19, 1), hand_r=back(19, -1), elbow_l=(1, 0, 0.4), elbow_r=(-1, 0, 0.4),
                foot_l=(0.15, -0.50, 0.16), foot_r=(-0.15, -0.30, 0.12), foot_pitch_l=50, foot_pitch_r=60,
                knee_l=(0.2, -0.4, -1), knee_r=(-0.2, -0.4, -1))),
        (23, _k(pelvis=(0.0, -1.16, 0.37), pelvis_rot=(84, 0, 0), spine=(4, 0, 0), look=look(23), clav_l=(2, 16), clav_r=(2, 16),
                hand_l=back(23, 1), hand_r=back(23, -1), elbow_l=(1, 0.2, 0.6), elbow_r=(-1, 0.2, 0.6),
                foot_l=(0.15, -0.48, 0.08), foot_r=(-0.15, -0.44, 0.08), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (26, _k(pelvis=(0.0, -1.16, 0.40), pelvis_rot=(82, 0, 0), spine=(8, 0, 0), look=look(26), clav_l=(2, 16), clav_r=(2, 16),
                hand_l=back(26, 1), hand_r=back(26, -1), elbow_l=(1, 0.2, 0.6), elbow_r=(-1, 0.2, 0.6),
                foot_l=(0.15, -0.48, 0.07), foot_r=(-0.15, -0.44, 0.07), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (34, _k(pelvis=(0.0, -1.16, 0.36), pelvis_rot=(86, 0, 0), spine=(2, 0, 0), look=(0.0, -4.0, 0.2), clav_l=(2, 10), clav_r=(2, 10),
                hand_l=back(34, 1), hand_r=back(34, -1), elbow_l=(1, 0.4, 0.4), elbow_r=(-1, 0.4, 0.4),
                foot_l=(0.15, -0.48, 0.07), foot_r=(-0.15, -0.44, 0.07), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
    ]
    return keys


# ============================================================================================== OVER (head on, forward)
OVER_C = [
    (0, _running()),
    (3, _k(pelvis=(0.0, -0.06, 0.88), pelvis_rot=(30, 0, 0), spine=(10, 0, 0), look=(0.0, -3.0, 0.8),
           hand_l=(0.30, -0.30, 1.00), elbow_l=(1, 0.4, -0.4), **_tuck(-0.06),
           foot_l=(0.12, -0.16, ANKLE), foot_r=(-0.12, 0.16, 0.16), foot_pitch_r=40)),
    (8, _k(pelvis=(0.0, -0.30, 0.82), pelvis_rot=(58, 0, 0), spine=(14, 0, 0), look=(0.0, -2.0, 0.0),
           hand_l=(0.32, -0.86, 0.66), elbow_l=(1, 0.2, 0.0), **_tuck(-0.30),
           foot_l=(0.12, -0.04, 0.20), foot_r=(-0.12, 0.14, 0.26), foot_pitch_l=50, foot_pitch_r=50,
           knee_l=(0.2, 0.3, -1), knee_r=(-0.2, 0.3, -1))),
    (13, _k(pelvis=(0.0, -0.58, 0.68), pelvis_rot=(92, 0, 0), spine=(10, 0, 0), look=(0.0, -2.0, -1.0),
            hand_l=(0.30, -1.22, 0.24), elbow_l=(1, 0.2, 0.2), hand_r=(-0.26, -1.18, 0.30), elbow_r=(-1, 0.2, 0.2),
            foot_l=(0.13, 0.04, 0.56), foot_r=(-0.13, 0.10, 0.50), foot_pitch_l=60, foot_pitch_r=60,
            knee_l=(0.2, 0.3, -1), knee_r=(-0.2, 0.3, -1))),
    (18, _k(pelvis=(0.0, -0.86, 0.42), pelvis_rot=(102, 0, 0), spine=(4, 0, 0), look=(0.0, -3.0, 0.0),
            hand_l=(0.30, -1.32, 0.07), elbow_l=(1, 0.6, 0), hand_r=(-0.28, -1.30, 0.08), elbow_r=(-1, 0.6, 0),
            foot_l=(0.13, -0.06, 0.48), foot_r=(-0.13, -0.02, 0.44), foot_pitch_l=70, foot_pitch_r=70,
            knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
    (22, face_down(0.0, -1.00, lift=0.10)),
    (25, face_down(0.0, -1.00, lift=0.13)),
    (32, face_down(0.0, -1.00, lift=0.08)),
]
_ball(OVER_C, 13)


def _over_t():
    """The tackler: drops his hips and shoots at the thighs, head across, arms wrap behind the knees; the carrier's
    momentum takes him over the top and the tackler is driven face down underneath, arms letting go as he lands."""
    c = lambda f, loc: to_tackler("Over", body(_interp(OVER_C, f), loc))
    thigh = lambda f, s: c(f, (s * 0.13, 0.10, -0.30))
    look = lambda f: c(f, (0.0, 0.0, -0.1))
    keys = [
        (0, _k(pelvis=(0.0, 0.0, 0.80), pelvis_rot=(40, 0, 0), spine=(8, 0, 0), look=look(0),
               hand_l=(0.26, -0.18, 0.84), hand_r=(-0.26, -0.18, 0.84), elbow_l=(1, 0.6, -0.2), elbow_r=(-1, 0.6, -0.2),
               foot_l=(0.16, -0.24, ANKLE), foot_r=(-0.16, 0.24, 0.12), foot_pitch_r=35)),
        (3, _k(pelvis=(0.0, -0.08, 0.64), pelvis_rot=(60, 0, 0), spine=(14, 0, 0), look=look(3), clav_l=(4, 16), clav_r=(4, 16),
               hand_l=thigh(3, -1), hand_r=thigh(3, 1), elbow_l=(1, 0.2, -0.2), elbow_r=(-1, 0.2, -0.2),
               foot_l=(0.16, -0.34, ANKLE), foot_r=(-0.16, 0.20, 0.10), foot_pitch_r=50,
               knee_l=(0.3, -1, 0), knee_r=(-0.3, -1, 0))),
        (8, _k(pelvis=(0.0, -0.18, 0.50), pelvis_rot=(74, 0, 0), spine=(12, 0, 0), look=look(8), clav_l=(2, 20), clav_r=(2, 20),
               hand_l=thigh(8, -1), hand_r=thigh(8, 1), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
               foot_l=(0.16, -0.30, ANKLE), foot_r=(-0.16, 0.22, ANKLE), foot_pitch_l=40, foot_pitch_r=60,
               knee_l=(0.3, -1, -0.4), knee_r=(-0.3, -1, -0.4))),
        (13, _k(pelvis=(0.0, -0.22, 0.32), pelvis_rot=(86, 0, 0), spine=(6, 0, 0), look=(0.0, -3.0, 0.0), clav_l=(2, 16), clav_r=(2, 16),
                hand_l=(0.22, -0.72, 0.30), hand_r=(-0.22, -0.72, 0.30), elbow_l=(1, 0.4, 0.2), elbow_r=(-1, 0.4, 0.2),
                foot_l=(0.15, 0.40, 0.10), foot_r=(-0.15, 0.42, 0.10), foot_pitch_l=60, foot_pitch_r=60,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (18, face_down(0.0, -0.20)),
        (21, face_down(0.0, -0.20, lift=-0.01)),
        (32, face_down(0.0, -0.20)),
    ]
    return keys


# ============================================================================================== BEHIND
BEHIND_C = [
    (0, _running()),
    (3, _k(pelvis=(0.0, -0.14, 0.86), pelvis_rot=(8, 0, 0), spine=(-10, 0, 0), look=(0.0, -4.0, 2.0),
           hand_l=(0.30, -0.24, 1.10), elbow_l=(1, 0.4, -0.5), **_tuck(-0.14),
           foot_l=(0.12, -0.30, ANKLE), foot_r=(-0.12, 0.10, 0.18), foot_pitch_r=40)),
    (8, _k(pelvis=(0.0, -0.42, 0.76), pelvis_rot=(40, 0, 0), spine=(14, 0, 0), look=(0.0, -3.0, 0.6),
           hand_l=(0.30, -0.80, 0.72), elbow_l=(1, 0.2, 0.0), **_tuck(-0.42),
           foot_l=(0.12, -0.36, ANKLE), foot_r=(-0.12, -0.02, 0.24), foot_pitch_l=40, foot_pitch_r=50)),
    (14, _k(pelvis=(0.0, -0.72, 0.54), pelvis_rot=(66, 0, 0), spine=(12, 0, 0), look=(0.0, -3.0, 0.0),
            hand_l=(0.30, -1.18, 0.34), elbow_l=(1, 0.3, 0.2), **_tuck(-0.72),
            foot_l=(0.12, -0.26, 0.16), foot_r=(-0.12, -0.10, 0.22), foot_pitch_l=60, foot_pitch_r=60,
            knee_l=(0.2, 0.2, -1), knee_r=(-0.2, 0.2, -1))),
    (19, _k(pelvis=(0.0, -0.94, 0.24), pelvis_rot=(88, 0, 0), spine=(2, 0, 0), look=(0.0, -4.0, 0.3),
            hand_l=(0.30, -1.38, 0.07), elbow_l=(1, 0.6, 0), hand_r=(-0.20, -1.36, 0.12), elbow_r=(-1, 0.6, 0),
            foot_l=(0.13, -0.10, 0.12), foot_r=(-0.13, -0.08, 0.12), foot_pitch_l=70, foot_pitch_r=70,
            knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
    (22, face_down(0.0, -1.00, lift=0.03)),
    (32, face_down(0.0, -1.00)),
]
_ball(BEHIND_C, 19)


def _behind_t():
    """The tackler from behind: closes, wraps both arms around the waist with the head to the side, his weight
    tears the carrier's hips back and down; they fall together and he lands on the carrier's legs, arms still locked."""
    c = lambda f, loc: to_tackler("Behind", body(_interp(BEHIND_C, f), loc))
    front = lambda f, s: c(f, (s * 0.15, -0.12, 0.10) if f < 16 else (s * 0.20, 0.02, 0.0))  # around his waist, then his hips
    look = lambda f: c(f, (0.5, 0.0, 0.5))
    keys = [
        (0, _k(pelvis=(0.0, 0.0, 0.88), pelvis_rot=(28, 0, 0), spine=(8, 0, 0), look=look(0),
               hand_l=(0.24, -0.30, 1.0), hand_r=(-0.24, -0.30, 1.0), elbow_l=(1, 0.4, -0.3), elbow_r=(-1, 0.4, -0.3),
               foot_l=(0.13, -0.24, ANKLE), foot_r=(-0.13, 0.26, 0.14), foot_pitch_r=40)),
        (3, _k(pelvis=(0.0, -0.14, 0.82), pelvis_rot=(44, 0, 0), spine=(10, 0, 0), look=look(3), clav_l=(4, 16), clav_r=(4, 16),
               hand_l=c(3, (0.22, 0.0, 0.12)), hand_r=c(3, (-0.22, 0.0, 0.12)), elbow_l=(1, 0.2, -0.2), elbow_r=(-1, 0.2, -0.2),
               foot_l=(0.14, -0.38, ANKLE), foot_r=(-0.14, 0.12, 0.14), foot_pitch_r=50)),
        (8, _k(pelvis=(0.0, -0.40, 0.72), pelvis_rot=(58, 0, 0), spine=(12, 0, 0), look=look(8), clav_l=(2, 20), clav_r=(2, 20),
               hand_l=front(8, 1), hand_r=front(8, -1), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
               foot_l=(0.14, -0.46, ANKLE), foot_r=(-0.14, -0.10, 0.16), foot_pitch_l=30, foot_pitch_r=55)),
        (14, _k(pelvis=(0.0, -0.56, 0.54), pelvis_rot=(72, 0, 0), spine=(10, 0, 0), look=look(14), clav_l=(2, 20), clav_r=(2, 20),
                hand_l=front(14, 1), hand_r=front(14, -1), elbow_l=(1, 0, 0.3), elbow_r=(-1, 0, 0.3),
                foot_l=(0.14, -0.36, 0.14), foot_r=(-0.14, -0.20, 0.12), foot_pitch_l=50, foot_pitch_r=60,
                knee_l=(0.2, -0.4, -1), knee_r=(-0.2, -0.4, -1))),
        (19, _k(pelvis=(0.0, -0.80, 0.36), pelvis_rot=(84, 0, 0), spine=(6, 0, 0), look=look(19), clav_l=(2, 16), clav_r=(2, 16),
                hand_l=front(19, 1), hand_r=front(19, -1), elbow_l=(1, 0.2, 0.5), elbow_r=(-1, 0.2, 0.5),
                foot_l=(0.15, -0.12, 0.08), foot_r=(-0.15, -0.10, 0.08), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (22, _k(pelvis=(0.0, -0.84, 0.35), pelvis_rot=(86, 0, 0), spine=(4, 0, 0), look=look(22), clav_l=(2, 14), clav_r=(2, 14),
                hand_l=front(22, 1), hand_r=front(22, -1), elbow_l=(1, 0.2, 0.5), elbow_r=(-1, 0.2, 0.5),
                foot_l=(0.15, -0.16, 0.07), foot_r=(-0.15, -0.14, 0.07), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (32, _k(pelvis=(0.0, -0.84, 0.33), pelvis_rot=(88, 0, 0), spine=(2, 0, 0), look=(0.0, -4.0, 0.2), clav_l=(2, 10), clav_r=(2, 10),
                hand_l=front(32, 1), hand_r=front(32, -1), elbow_l=(1, 0.4, 0.5), elbow_r=(-1, 0.4, 0.5),
                foot_l=(0.15, -0.16, 0.07), foot_r=(-0.15, -0.14, 0.07), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
    ]
    return keys


# ============================================================================================== SIDE (hit on his right)
SIDE_C = [
    (0, _running()),
    (3, _k(pelvis=(0.06, -0.06, 0.86), pelvis_rot=(14, 10, 12), spine=(-6, 10, -12), look=(-1.5, -3.0, 1.8),
           hand_l=(0.40, -0.10, 1.12), elbow_l=(1, 0.3, -0.5), **_tuck(-0.06),
           foot_l=(0.14, -0.22, ANKLE), foot_r=(-0.06, 0.18, 0.16), foot_pitch_r=40)),
    (8, _k(pelvis=(0.30, -0.26, 0.74), pelvis_rot=(26, 25, 30), spine=(8, 6, 4), look=(1.0, -2.0, 0.8),
           hand_l=(0.56, -0.36, 0.68), elbow_l=(1, 0.2, 0.2), **_tuck(-0.26),
           foot_l=(0.32, -0.20, ANKLE), foot_r=(0.06, 0.02, 0.22), foot_pitch_l=40, foot_pitch_r=50,
           knee_l=(0.6, -1, 0), knee_r=(0.2, -1, 0))),
    (14, _k(pelvis=(0.58, -0.44, 0.50), pelvis_rot=(56, 45, 34), spine=(8, 0, 0), look=(1.5, -2.0, 0.0),
            hand_l=(0.78, -0.64, 0.38), elbow_l=(1, 0.2, 0.2),
            foot_l=(0.30, -0.02, 0.16), foot_r=(0.12, 0.10, 0.26), foot_pitch_l=60, foot_pitch_r=60,
            knee_l=(0.4, -0.3, -1), knee_r=(0.2, -0.3, -1))),
    (19, _k(pelvis=(0.80, -0.56, 0.22), pelvis_rot=(86, 58, 12), spine=(4, 0, 0), look=(2.5, -2.0, 0.3),
            hand_l=(1.12, -0.88, 0.07), elbow_l=(1, 0.6, 0), hand_r=(0.76, -0.98, 0.14), elbow_r=(-1, 0.6, 0),
            foot_l=(0.30, -0.18, 0.14), foot_r=(0.20, -0.08, 0.14), foot_pitch_l=70, foot_pitch_r=70,
            knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
    (22, face_down(0.84, -0.58, yaw=60, lift=0.03)),
    (32, face_down(0.84, -0.58, yaw=60)),
]
_ball(SIDE_C, 19)


def _side_t():
    """The tackler from the carrier's right: hits him at the near hip with the shoulder, head in front, arms wrap
    his waist, drives him off his line to the far side; they go down together, the tackler across his back."""
    c = lambda f, loc: to_tackler("Side", body(_interp(SIDE_C, f), loc))
    far = lambda f, s: c(f, (-0.20, -0.10 if s > 0 else 0.10, 0.10) if f < 6 else
                         (0.20 if s > 0 else 0.0, -0.12 if s > 0 else 0.14, 0.10))  # near hip, then far hip / small of the back
    look = lambda f: c(f, (0.0, -0.4, 0.5))

    def p(f, x, y, z, pitch, yaw, **kw):
        return _k(pelvis=(x, y, z), pelvis_rot=(pitch, yaw, 0), look=look(f), hand_l=far(f, 1), hand_r=far(f, -1), **kw)
    keys = [
        (0, _k(pelvis=(0.0, 0.0, 0.86), pelvis_rot=(30, 0, 0), spine=(8, 0, 0), look=look(0),
               hand_l=(0.26, -0.20, 0.94), hand_r=(-0.26, -0.20, 0.94), elbow_l=(1, 0.6, -0.2), elbow_r=(-1, 0.6, -0.2),
               foot_l=(0.15, -0.22, ANKLE), foot_r=(-0.15, 0.24, 0.12), foot_pitch_r=35)),
        (3, p(3, 0.0, -0.10, 0.80, 44, 0, spine=(10, 0, 0), clav_l=(4, 16), clav_r=(4, 16), elbow_l=(1, 0.2, -0.2),
              elbow_r=(-1, 0.2, -0.2), foot_l=(0.15, -0.36, ANKLE), foot_r=(-0.15, 0.10, 0.12), foot_pitch_r=45)),
        (8, p(8, -0.08, -0.40, 0.70, 56, -10, spine=(12, 0, 0), clav_l=(2, 20), clav_r=(2, 20), elbow_l=(1, 0, 0.2),
              elbow_r=(-1, 0, 0.2), foot_l=(0.12, -0.50, ANKLE), foot_r=(-0.18, -0.12, 0.14), foot_pitch_l=30, foot_pitch_r=55)),
        (14, p(14, -0.20, -0.66, 0.52, 70, -16, spine=(10, 0, 0), clav_l=(2, 20), clav_r=(2, 20), elbow_l=(1, 0, 0.3),
               elbow_r=(-1, 0, 0.3), foot_l=(0.04, -0.46, 0.14), foot_r=(-0.24, -0.28, 0.12), foot_pitch_l=50,
               foot_pitch_r=60, foot_yaw_l=-16, foot_yaw_r=-16, knee_l=(0.2, -0.4, -1), knee_r=(-0.2, -0.4, -1))),
        (19, p(19, -0.30, -0.84, 0.34, 84, -22, spine=(6, 0, 0), clav_l=(2, 16), clav_r=(2, 16), elbow_l=(1, 0.2, 0.5),
               elbow_r=(-1, 0.2, 0.5), foot_l=(0.10, -0.26, 0.08), foot_r=(-0.20, -0.16, 0.08), foot_pitch_l=70,
               foot_pitch_r=70, foot_yaw_l=-22, foot_yaw_r=-22, knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (22, p(22, -0.32, -0.88, 0.33, 86, -22, spine=(4, 0, 0), clav_l=(2, 14), clav_r=(2, 14), elbow_l=(1, 0.2, 0.5),
               elbow_r=(-1, 0.2, 0.5), foot_l=(0.10, -0.26, 0.07), foot_r=(-0.20, -0.16, 0.07), foot_pitch_l=70,
               foot_pitch_r=70, foot_yaw_l=-22, foot_yaw_r=-22, knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (32, p(32, -0.32, -0.88, 0.31, 88, -22, spine=(2, 0, 0), clav_l=(2, 10), clav_r=(2, 10), elbow_l=(1, 0.4, 0.5),
               elbow_r=(-1, 0.4, 0.5), foot_l=(0.10, -0.26, 0.07), foot_r=(-0.20, -0.16, 0.07), foot_pitch_l=70,
               foot_pitch_r=70, foot_yaw_l=-22, foot_yaw_r=-22, knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
    ]
    return keys


# ============================================================================================== DIVE (shoestring)
DIVE_C = [
    (0, _running()),
    (3, _running(-0.30)),
    (6, _k(pelvis=(0.0, -0.58, 0.88), pelvis_rot=(26, 0, 0), spine=(8, 0, 0), look=(0.0, -5.0, 1.0),
           hand_l=(0.30, -0.80, 0.96), elbow_l=(1, 0.4, -0.4), **_tuck(-0.58),
           foot_l=(0.12, -0.80, ANKLE), foot_r=(-0.12, -0.20, 0.20), foot_pitch_r=45)),
    (11, _k(pelvis=(0.0, -1.02, 0.74), pelvis_rot=(54, 0, 0), spine=(12, 0, 0), look=(0.0, -4.0, 0.2),
            hand_l=(0.34, -1.56, 0.56), elbow_l=(1, 0.2, 0.0), **_tuck(-1.02),
            foot_l=(0.12, -0.70, 0.18), foot_r=(-0.12, -0.36, 0.30), foot_pitch_l=50, foot_pitch_r=60)),
    (16, _k(pelvis=(0.0, -1.42, 0.46), pelvis_rot=(78, 0, 0), spine=(8, 0, 0), look=(0.0, -4.0, 0.0),
            hand_l=(0.32, -1.96, 0.16), elbow_l=(1, 0.4, 0.2), **_tuck(-1.42),
            foot_l=(0.12, -0.70, 0.36), foot_r=(-0.12, -0.56, 0.40), foot_pitch_l=60, foot_pitch_r=60)),
    (20, _k(pelvis=(0.0, -1.70, 0.22), pelvis_rot=(90, 0, 0), spine=(2, 0, 0), look=(0.0, -5.0, 0.3),
            hand_l=(0.30, -2.12, 0.07), elbow_l=(1, 0.6, 0), hand_r=(-0.22, -2.10, 0.12), elbow_r=(-1, 0.6, 0),
            foot_l=(0.13, -0.86, 0.14), foot_r=(-0.13, -0.84, 0.12), foot_pitch_l=70, foot_pitch_r=70,
            knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
    (23, face_down(0.0, -1.76, lift=0.03)),
    (32, face_down(0.0, -1.76)),
]
_ball(DIVE_C, 20)


def _dive_t():
    """Shoestring: a last push off the ground, full extension with the feet off the turf, both hands at the ankles,
    he clips the trailing foot, lands on his chest and slides, arms still out."""
    c = lambda f, loc: to_tackler("Dive", body(_interp(DIVE_C, f), loc))
    ankle = lambda f: _interp(DIVE_C, f)["foot_r"]
    at = lambda f, dx: (lambda q: (q[0] + dx, q[1], max(0.07, q[2])))(to_tackler("Dive", ankle(f)))
    keys = [
        (0, _k(pelvis=(0.0, 0.0, 0.88), pelvis_rot=(24, 0, 0), spine=(6, 0, 0), look=(0.0, -4.0, 0.4),
               hand_l=(0.24, -0.24, 0.98), hand_r=(-0.24, 0.10, 0.98), elbow_l=(1, 0.5, -0.4), elbow_r=(-1, 0.5, -0.4),
               foot_l=(0.13, -0.24, ANKLE), foot_r=(-0.13, 0.26, 0.16), foot_pitch_r=40)),
        (3, _k(pelvis=(0.0, -0.30, 0.76), pelvis_rot=(46, 0, 0), spine=(8, 0, 0), look=(0.0, -4.0, 0.2),
               hand_l=(0.20, -0.76, 0.74), hand_r=(-0.20, -0.76, 0.74), elbow_l=(1, 0, -0.2), elbow_r=(-1, 0, -0.2),
               foot_l=(0.13, -0.32, ANKLE), foot_r=(-0.13, 0.20, 0.20), foot_pitch_l=40, foot_pitch_r=50)),
        (7, _k(pelvis=(0.0, -1.00, 0.52), pelvis_rot=(74, 0, 0), spine=(8, 0, 0), look=(0.0, -4.0, 0.0),
               hand_l=at(7, 0.10), hand_r=at(7, -0.08), elbow_l=(1, 0, 0), elbow_r=(-1, 0, 0),
               foot_l=(0.13, -0.32, 0.30), foot_r=(-0.13, -0.26, 0.34), foot_pitch_l=60, foot_pitch_r=60,
               knee_l=(0.2, 0.3, -1), knee_r=(-0.2, 0.3, -1))),
        (11, _k(pelvis=(0.0, -1.30, 0.32), pelvis_rot=(86, 0, 0), spine=(4, 0, 0), look=(0.0, -4.0, 0.0),
                hand_l=at(11, 0.10), hand_r=at(11, -0.08), elbow_l=(1, 0, 0), elbow_r=(-1, 0, 0),
                foot_l=(0.13, -0.58, 0.24), foot_r=(-0.13, -0.54, 0.22), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (15, face_down(0.0, -1.50, hands=False)),
        (32, face_down(0.0, -1.62, hands=False)),
    ]
    for f, k in keys:
        if "hand_l" not in k:
            k["hand_l"], k["hand_r"] = (0.18, -2.10, 0.08), (-0.18, -2.08, 0.08)
            k["elbow_l"], k["elbow_r"] = (1, 0, 0), (-1, 0, 0)
    return keys


# ============================================================================================== PILE ON
def _pile_on():
    """A second tackler arriving: two hard steps, launches over the pile and lands across it, arms over the carrier."""
    return [
        (0, _k(pelvis=(0.0, 0.0, 0.90), pelvis_rot=(22, 0, 0), spine=(6, 0, 0), look=(0.0, -3.0, 0.4),
               hand_l=(0.24, -0.14, 1.0), hand_r=(-0.24, 0.10, 1.0), elbow_l=(1, 0.5, -0.4), elbow_r=(-1, 0.5, -0.4),
               foot_l=(0.13, -0.24, ANKLE), foot_r=(-0.13, 0.26, 0.16), foot_pitch_r=40)),
        (4, _k(pelvis=(0.0, -0.36, 0.84), pelvis_rot=(36, 0, 0), spine=(8, 0, 0), look=(0.0, -3.0, 0.3),
               hand_l=(0.26, -0.70, 0.96), hand_r=(-0.26, -0.70, 0.96), elbow_l=(1, 0.2, -0.3), elbow_r=(-1, 0.2, -0.3),
               foot_l=(0.13, -0.40, ANKLE), foot_r=(-0.13, 0.0, 0.22), foot_pitch_l=30, foot_pitch_r=55)),
        (9, _k(pelvis=(0.0, -0.80, 0.78), pelvis_rot=(64, 0, 0), spine=(8, 0, 0), look=(0.0, -3.0, 0.2),
               hand_l=(0.30, -1.30, 0.56), hand_r=(-0.30, -1.30, 0.56), elbow_l=(1, 0, 0), elbow_r=(-1, 0, 0),
               foot_l=(0.13, -0.40, 0.26), foot_r=(-0.13, -0.30, 0.34), foot_pitch_l=60, foot_pitch_r=60)),
        (14, _k(pelvis=(0.0, -1.10, 0.52), pelvis_rot=(82, 0, 0), spine=(6, 0, 0), look=(0.0, -3.0, 0.2),
                hand_l=(0.30, -1.56, 0.36), hand_r=(-0.30, -1.56, 0.36), elbow_l=(1, 0.3, 0.3), elbow_r=(-1, 0.3, 0.3),
                foot_l=(0.13, -0.42, 0.18), foot_r=(-0.13, -0.38, 0.16), foot_pitch_l=70, foot_pitch_r=70,
                knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (17, face_down(0.0, -1.20, lift=0.32, hands=False)),
        (20, face_down(0.0, -1.20, lift=0.36, hands=False)),
        (30, face_down(0.0, -1.20, lift=0.32, hands=False)),
    ]


def _pile_hands(keys):
    for f, k in keys:
        if "hand_l" not in k:
            k["hand_l"], k["hand_r"] = (0.30, -1.66, 0.36), (-0.30, -1.66, 0.36)
            k["elbow_l"], k["elbow_r"] = (1, 0.4, 0.4), (-1, 0.4, 0.4)
    return keys


def _clip(keys, mirror=False):
    return lambda rig: _key(rig, keys, mirror)


TACKLES = {
    "PT_Drive_T": _clip(_drive_t()), "PT_Drive_C": _clip(DRIVE_C),
    "PT_Over_T": _clip(_over_t()), "PT_Over_C": _clip(OVER_C),
    "PT_Behind_T": _clip(_behind_t()), "PT_Behind_C": _clip(BEHIND_C),
    "PT_Side_T": _clip(_side_t()), "PT_Side_C": _clip(SIDE_C),
    "PT_Side_T_M": _clip(_side_t(), True), "PT_Side_C_M": _clip(SIDE_C, True),
    "PT_Dive_T": _clip(_dive_t()), "PT_Dive_C": _clip(DIVE_C),
    "PT_PileOn": _clip(_pile_hands(_pile_on())),
}


if __name__ == "__main__":
    # the numbers the renderer needs: where each carrier lies at the end (pelvis, his frame) and where each tackler's
    # pelvis ends, and the contact distance at the strike (tackler hands to carrier waist)
    for n, keys in (("Drive", DRIVE_C), ("Over", OVER_C), ("Behind", BEHIND_C), ("Side", SIDE_C), ("Dive", DIVE_C)):
        print(n, "carrier end pelvis", keys[-1][1]["pelvis"])
    for n, fn in (("Drive", _drive_t), ("Over", _over_t), ("Behind", _behind_t), ("Side", _side_t), ("Dive", _dive_t)):
        k = fn()
        print(n, "tackler end pelvis", k[-1][1]["pelvis"], "hands f3", k[1][1]["hand_l"])
