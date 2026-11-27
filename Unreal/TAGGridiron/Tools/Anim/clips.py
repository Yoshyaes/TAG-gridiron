"""TAG football clips, hand keyed (no mocap). Each clip(rig) queues keys and returns (first, last) frame at 30 fps.

World frame: facing -Y, left +X, up +Z, meters. Ankles rest at z 0.094. Timing targets: docs/LOOK_TARGETS.md.
"""
import math


def _mix(a, b, t):
    if isinstance(a, (int, float)):
        return a + (b - a) * t
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def breathe(base, amp_z=0.006, amp_pitch=1.2, shift=0.008):
    """Three keys for a seamless 90 frame idle loop around a base pose: inhale, settle, weight shift."""
    def k(dz, dp, dx):
        d = dict(base)
        px, py, pz = base["pelvis"]
        d["pelvis"] = (px + dx, py, pz + dz)
        sp = base.get("spine", (0, 0, 0))
        d["spine"] = (sp[0] + dp, sp[1], sp[2])
        return d
    return [(0, k(0, 0, 0)), (30, k(amp_z, -amp_pitch, shift)), (60, k(-amp_z * 0.5, amp_pitch * 0.5, -shift)), (90, k(0, 0, 0))]


# ------------------------------------------------------------------------------------------- stances
def stance_3pt(rig):
    """Offensive / defensive line three point stance, right hand down. Back flat, eyes up, weight on the balls."""
    for f, k in breathe(STANCE_3PT, amp_z=0.005, amp_pitch=1.0, shift=0.006):
        rig.key(f, k)
    return 0, 90


STANCE_3PT = {
        # hips at 0.75 m, level with the shoulders (a flat back; the look targets' 12-clip band is 0.70-0.81 and line
        # coaches want the hips no lower than the shoulders). The down arm hangs straight from a shoulder ~0.62 m up
        # and ~0.45 m ahead of the hips, so the hand meets the turf a stride ahead of the feet.
        "pelvis": (0.0, 0.12, 0.76), "pelvis_rot": (78, 0, 0), "spine": (30, 0, 0),
        "clav_r": (-10, 22), "clav_l": (0, 4),
        "hand_r": (-0.20, -0.50, 0.10), "elbow_r": (-0.4, 0.3, 0.2),   # the arm hangs toward the turf; the game plants the hand
        "hand_l": (0.26, -0.12, 0.58), "elbow_l": (0.9, 0.1, -0.3),   # the off forearm rests across the thigh
        "foot_l": (0.25, -0.02, 0.094), "foot_r": (-0.25, 0.16, 0.094),
        "foot_pitch_l": 16, "foot_pitch_r": 22, "foot_yaw_l": 6, "foot_yaw_r": -6,
        "knee_l": (0.25, -1, 0), "knee_r": (-0.25, -1, 0),
        "look": (0.0, -3.0, 1.2),   # eyes up at the man across the line
}


def stance_2pt(rig):
    """Receiver / linebacker two point stance: staggered, weight forward on the front foot, back heel up."""
    base = {
        "pelvis": (0.0, 0.06, 0.80), "pelvis_rot": (34, 0, 0), "spine": (12, 0, 0),
        "hand_l": (0.20, -0.30, 0.80), "elbow_l": (1, 0.4, -0.3),
        "hand_r": (-0.20, -0.16, 0.84), "elbow_r": (-1, 0.4, -0.3),
        "foot_l": (0.13, -0.24, 0.094), "foot_r": (-0.13, 0.26, 0.094),
        "foot_pitch_l": 4, "foot_pitch_r": 32, "foot_yaw_l": 4, "foot_yaw_r": -8,
        "knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0),
        "look": (0.0, -8.0, 1.3),
    }
    for f, k in breathe(base, amp_z=0.006, amp_pitch=1.0, shift=0.008):
        rig.key(f, k)
    return 0, 90


def stance_shotgun(rig):
    """QB in shotgun: square, knees flexed, hands together at the belt ready for the snap, eyes downfield."""
    for f, k in breathe(SHOTGUN, amp_z=0.007, amp_pitch=1.2, shift=0.01):
        rig.key(f, k)
    return 0, 90


SHOTGUN = {
        "pelvis": (0.0, 0.05, 0.90), "pelvis_rot": (18, 0, 0), "spine": (8, 0, 0),
        "hand_l": (0.07, -0.30, 1.00), "elbow_l": (1, 0.3, -0.6),
        "hand_r": (-0.07, -0.30, 1.00), "elbow_r": (-1, 0.3, -0.6),
        "foot_l": (0.21, -0.02, 0.094), "foot_r": (-0.21, 0.02, 0.094),
        "foot_pitch_l": 6, "foot_pitch_r": 6, "foot_yaw_l": 8, "foot_yaw_r": -8,
        "knee_l": (0.25, -1, 0), "knee_r": (-0.25, -1, 0),
        "look": (0.0, -12.0, 1.6),
}


def qb_snap_catch(rig):
    """Shotgun snap, 12 frames from the exact shotgun pose: hands give with the ball at the belt, pull it up to
    the chest (right hand on top, laces), eyes stay downfield, right foot opens for the first drop step."""
    def k(**kw):
        d = dict(SHOTGUN)
        d.update(kw)
        return d
    keys = [
        (0, k()),
        (3, k(hand_l=(0.06, -0.36, 1.00), hand_r=(-0.06, -0.36, 1.00), pelvis=(0.0, 0.05, 0.91))),
        (6, k(hand_l=(0.05, -0.24, 1.10), hand_r=(-0.07, -0.24, 1.14), elbow_l=(1, 0.4, -0.8), elbow_r=(-1, 0.4, -0.8),
              pelvis=(0.0, 0.06, 0.90), pelvis_rot=(14, 0, 0))),
        (12, k(hand_l=(0.04, -0.20, 1.18), hand_r=(-0.08, -0.18, 1.22), elbow_l=(1, 0.4, -0.8), elbow_r=(-1, 0.4, -0.8),
               pelvis=(0.0, 0.10, 0.89), pelvis_rot=(12, -6, 0), foot_r=(-0.20, 0.20, 0.11), foot_pitch_r=20,
               foot_yaw_r=-15)),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 12


def qb_handoff(rig):
    """Handoff to the right with a carry-out fake, 20 frames: open to the back, ball extended in both hands into his
    belly, ride and release, then turn back to carry out the fake with empty hands held as if still carrying."""
    def k(**kw):
        d = {"look": (-3.0, -2.0, 1.1), "knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0),
             "elbow_l": (1, 0.3, -0.8), "elbow_r": (-1, 0.3, -0.8), "spine": (8, 0, 0)}
        d.update(kw)
        return d
    keys = [
        (0, k(pelvis=(0.0, 0.0, 0.88), pelvis_rot=(16, 0, 0), hand_l=(0.04, -0.22, 1.16), hand_r=(-0.07, -0.20, 1.20),
              foot_l=(0.18, -0.04, 0.094), foot_r=(-0.18, 0.04, 0.094))),
        (5, k(pelvis=(-0.04, 0.06, 0.86), pelvis_rot=(16, -40, 0), spine=(8, -15, 0),
              hand_l=(-0.10, -0.12, 1.10), hand_r=(-0.20, -0.06, 1.12),
              foot_l=(0.16, -0.06, 0.094), foot_r=(-0.20, 0.24, 0.094), foot_yaw_r=-50, foot_pitch_l=10)),
        (10, k(pelvis=(-0.06, 0.10, 0.84), pelvis_rot=(20, -60, 0), spine=(10, -20, 0), clav_l=(0, 12), clav_r=(0, 12),
               hand_l=(-0.30, 0.04, 1.02), hand_r=(-0.40, 0.10, 1.04), elbow_l=(1, 0.2, -1), elbow_r=(-1, 0.5, -0.6),
               foot_l=(0.12, -0.02, 0.094), foot_r=(-0.20, 0.26, 0.094), foot_yaw_l=-30, foot_yaw_r=-60)),
        (13, k(pelvis=(-0.06, 0.12, 0.85), pelvis_rot=(18, -55, 0), spine=(8, -18, 0),
               hand_l=(-0.20, -0.02, 1.04), hand_r=(-0.28, 0.06, 1.06),
               foot_l=(0.12, -0.02, 0.094), foot_r=(-0.20, 0.26, 0.094), foot_yaw_l=-30, foot_yaw_r=-60)),
        (20, k(pelvis=(-0.02, 0.18, 0.88), pelvis_rot=(14, -20, 0), spine=(6, -5, 0), look=(2.0, -4.0, 1.4),
               hand_l=(0.06, -0.06, 1.10), hand_r=(-0.10, -0.02, 1.12),
               foot_l=(0.16, 0.12, 0.11), foot_r=(-0.20, 0.30, 0.094), foot_yaw_r=-30, foot_pitch_l=20)),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 20


def qb_throw(rig):
    """Right handed set-feet pass, 1.6 s. Phases: set, load (ball to ear, lead arm at target), stride, plant with
    the front foot down 0.2 s before the release (as measured on NFL quarterbacks),
    hips opening ahead of the shoulders (separation), high elbow release over the top, follow through across the
    body with the back foot pivoting, recovery step, settle. Eyes stay on the target throughout."""
    look = (0.0, -15.0, 1.7)
    stance = {"foot_l": (0.16, -0.10, 0.094), "foot_r": (-0.20, 0.18, 0.094), "foot_yaw_l": -25, "foot_yaw_r": -70,
              "knee_l": (0.3, -1, 0), "knee_r": (-0.6, -1, 0), "look": look}

    def k(**kw):
        d = dict(stance)
        d.update(kw)
        return d

    keys = [
        (0, k(pelvis=(0.0, 0.03, 0.90), pelvis_rot=(8, -45, 0), spine=(6, -20, 0),
              hand_r=(-0.12, -0.18, 1.22), elbow_r=(-1, 0.3, -1), hand_l=(-0.02, -0.24, 1.20), elbow_l=(1, -0.2, -1))),
        (8, k(pelvis=(-0.02, 0.06, 0.88), pelvis_rot=(6, -55, 0), spine=(4, -35, 0), clav_r=(6, -4),
              hand_r=(-0.20, 0.06, 1.62), elbow_r=(-1, 0.4, 0.2), hand_l=(0.10, -0.42, 1.38), elbow_l=(1, 0, -0.5),
              foot_pitch_l=8)),
        (14, k(pelvis=(0.0, 0.0, 0.87), pelvis_rot=(6, -45, 0), spine=(4, -38, 0), clav_r=(8, -6),
               hand_r=(-0.22, 0.10, 1.65), elbow_r=(-1, 0.4, 0.25), hand_l=(0.12, -0.45, 1.36), elbow_l=(1, 0, -0.5),
               foot_l=(0.16, -0.28, 0.17), foot_yaw_l=-22)),
        (18, k(pelvis=(0.02, -0.10, 0.835), pelvis_rot=(10, -15, 0), spine=(6, -40, 0), clav_r=(10, -8),
               hand_r=(-0.24, 0.16, 1.66), elbow_r=(-1, 0.3, 0.3), hand_l=(0.20, -0.35, 1.25), elbow_l=(1, 0.2, -0.6),
               foot_l=(0.16, -0.45, 0.094), foot_yaw_l=-20)),
        (23, k(pelvis=(0.03, -0.13, 0.815), pelvis_rot=(14, 5, 0), spine=(12, 0, 0), clav_r=(12, 4),
               hand_r=(-0.16, -0.08, 1.70), elbow_r=(-1, -0.2, 0.6), hand_l=(0.25, -0.10, 1.10), elbow_l=(1, 0.5, -0.4),
               foot_l=(0.16, -0.45, 0.094), foot_yaw_l=-20, foot_pitch_r=38)),
        (25, k(pelvis=(0.03, -0.16, 0.81), pelvis_rot=(18, 20, 0), spine=(22, 20, -5), clav_r=(8, 14),
               hand_r=(-0.10, -0.40, 1.52), elbow_r=(-1, -0.5, 0.3), hand_l=(0.26, 0.0, 1.05), elbow_l=(1, 0.6, -0.2),
               foot_l=(0.16, -0.45, 0.094), foot_yaw_l=-20, foot_pitch_r=55)),
        (32, k(pelvis=(0.04, -0.22, 0.80), pelvis_rot=(28, 30, 0), spine=(28, 25, 0), clav_r=(-2, 20),
               hand_r=(0.18, -0.38, 0.92), elbow_r=(-0.5, -0.3, 1.0), hand_l=(0.30, 0.05, 1.0), elbow_l=(1, 0.6, -0.4),
               foot_l=(0.16, -0.45, 0.094), foot_yaw_l=-20, foot_pitch_r=72, foot_yaw_r=-40)),
        (38, k(pelvis=(0.02, -0.18, 0.86), pelvis_rot=(12, 15, 0), spine=(8, 10, 0),
               hand_r=(-0.22, -0.12, 1.0), elbow_r=(-1, 0.3, -1), hand_l=(0.24, -0.08, 1.0), elbow_l=(1, 0.3, -1),
               foot_l=(0.16, -0.45, 0.094), foot_yaw_l=-20, foot_r=(-0.12, -0.20, 0.094), foot_yaw_r=-15)),
        (48, k(pelvis=(0.0, -0.16, 0.90), pelvis_rot=(6, 5, 0), spine=(4, 0, 0),
               hand_r=(-0.25, -0.08, 1.0), elbow_r=(-1, 0.3, -1), hand_l=(0.25, -0.10, 1.0), elbow_l=(1, 0.3, -1),
               foot_l=(0.16, -0.45, 0.094), foot_yaw_l=-15, foot_r=(-0.12, -0.20, 0.094), foot_yaw_r=-12)),
    ]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 48


def celebrate(rig):
    """Touchdown celebration loop, 40 frames: gather, jump with both arms up, land soft, two right fist pumps."""
    look = (0.0, -6.0, 2.2)

    def k(pz, py=0.0, pitch=6.0, hl=(0.22, -0.10, 1.05), hr=(-0.22, -0.10, 1.05), lift=0.0, toe=0.0, el=(1, 0.3, -1),
          er=(-1, 0.3, -1), lk=None):
        return {"pelvis": (0.0, py, pz), "pelvis_rot": (pitch, 0, 0), "spine": (pitch * 0.4, 0, 0),
                "hand_l": hl, "hand_r": hr, "elbow_l": el, "elbow_r": er,
                "foot_l": (0.16, -0.02, 0.094 + lift), "foot_r": (-0.16, 0.02, 0.094 + lift),
                "foot_pitch_l": toe, "foot_pitch_r": toe, "knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0),
                "look": lk or look}
    keys = [
        (0, k(0.84, pitch=18, hl=(0.24, 0.05, 0.85), hr=(-0.24, 0.05, 0.85))),                       # gather
        (6, k(0.98, pitch=-4, hl=(0.30, -0.08, 1.82), hr=(-0.30, -0.08, 1.82), lift=0.16, toe=30,  # airborne
              el=(1, 0, 0.3), er=(-1, 0, 0.3))),
        (12, k(0.82, pitch=16, hl=(0.28, -0.15, 1.30), hr=(-0.28, -0.15, 1.30))),                      # land soft
        (19, k(0.88, pitch=4, hl=(0.18, -0.20, 1.20), hr=(-0.22, -0.08, 1.74), er=(-1, 0.2, 0.2))),    # pump 1
        (24, k(0.86, pitch=8, hl=(0.18, -0.20, 1.18), hr=(-0.26, -0.12, 1.45))),
        (30, k(0.88, pitch=4, hl=(0.18, -0.20, 1.20), hr=(-0.22, -0.08, 1.75), er=(-1, 0.2, 0.2))),    # pump 2
        (40, k(0.84, pitch=18, hl=(0.24, 0.05, 0.85), hr=(-0.24, 0.05, 0.85))),                       # = frame 0
    ]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 40


def _in_place(keys):
    """Remove the pelvis's forward travel from every world position so the clip plays in place (the sim moves the
    actor). Planted feet then slide back under the body exactly as fast as the actor advances."""
    x0, y0 = keys[0][1]["pelvis"][0], keys[0][1]["pelvis"][1]
    out = []
    for f, k in keys:
        dx = k["pelvis"][0] - x0
        dy = k["pelvis"][1] - y0
        d = dict(k)
        for name in ("pelvis", "hand_l", "hand_r", "foot_l", "foot_r", "look"):
            if name in d:
                x, y, z = d[name]
                d[name] = (x - dx, y - dy, z)
        out.append((f, d))
    return out


def mirror(k):
    """Left/right mirror of a key pose (x -> -x, l <-> r, yaw and lean negated)."""
    d = {}
    for name, v in k.items():
        nm = name
        if name.endswith("_l"):
            nm = name[:-2] + "_r"
        elif name.endswith("_r"):
            nm = name[:-2] + "_l"
        if name in ("pelvis_rot", "spine"):
            v = (v[0], -v[1], -v[2])
        elif name.startswith("foot_yaw"):
            v = -v
        elif name.startswith(("clav_", "foot_pitch")):
            pass
        elif isinstance(v, tuple) and len(v) == 3:
            v = (-v[0], v[1], v[2])
        d[nm] = v
    return d


def lineman_getoff(rig):
    """Snap get-off out of the three point stance, 16 frames: rise, six inch power step, drive step, hands punch
    inside to the chest plate while the pads stay low. In place."""
    stance = dict(STANCE_3PT)
    look = (0.0, -6.0, 1.1)

    def k(**kw):
        d = dict(stance)
        d.update(kw)
        d["look"] = look
        return d
    keys = [
        (0, k()),
        (3, k(pelvis=(0.0, 0.08, 0.74), pelvis_rot=(68, 0, 0), spine=(26, 0, 0), hand_r=(-0.20, -0.46, 0.30),
              foot_pitch_l=20, foot_pitch_r=26)),
        (6, k(pelvis=(0.0, -0.02, 0.74), pelvis_rot=(54, 0, 0), spine=(16, 0, 0), clav_r=(0, 10), clav_l=(0, 10),
              hand_r=(-0.16, -0.40, 0.82), hand_l=(0.16, -0.40, 0.82), elbow_r=(-1, 0.3, -0.6), elbow_l=(1, 0.3, -0.6),
              foot_l=(0.24, -0.16, 0.15), foot_pitch_l=0, foot_pitch_r=30)),
        (10, k(pelvis=(0.0, -0.14, 0.76), pelvis_rot=(46, 0, 0), spine=(14, 0, 0), clav_r=(0, 16), clav_l=(0, 16),
               hand_r=(-0.13, -0.60, 0.98), hand_l=(0.13, -0.60, 0.98), elbow_r=(-1, 0.2, -1), elbow_l=(1, 0.2, -1),
               foot_l=(0.24, -0.20, 0.094), foot_pitch_l=0, foot_r=(-0.24, -0.06, 0.16), foot_pitch_r=10)),
        (16, k(pelvis=(0.0, -0.28, 0.78), pelvis_rot=(40, 0, 0), spine=(12, 0, 0), clav_r=(0, 18), clav_l=(0, 18),
               hand_r=(-0.13, -0.72, 1.04), hand_l=(0.13, -0.72, 1.04), elbow_r=(-1, 0.2, -1), elbow_l=(1, 0.2, -1),
               foot_l=(0.24, -0.20, 0.094), foot_pitch_l=12, foot_r=(-0.24, -0.42, 0.094), foot_pitch_r=0)),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 16


def tackle_wrap(rig):
    """Form tackle, 15 frames in place: sink the hips with eyes up, explode through contact, arms shoot then close
    behind the carrier's back, legs drive. The sim's 'down' then hands the body to the ragdoll."""
    look = (0.0, -4.0, 1.0)

    def k(**kw):
        d = {"knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0), "look": look,
             "elbow_l": (1, 0.3, -0.4), "elbow_r": (-1, 0.3, -0.4)}
        d.update(kw)
        return d
    keys = [
        (0, k(pelvis=(0.0, 0.0, 0.92), pelvis_rot=(22, 0, 0), spine=(8, 0, 0),
              hand_l=(0.24, -0.20, 1.02), hand_r=(-0.24, -0.02, 1.0),
              foot_l=(0.14, -0.20, 0.094), foot_r=(-0.14, 0.22, 0.094), foot_pitch_r=25)),
        (4, k(pelvis=(0.0, -0.10, 0.78), pelvis_rot=(36, 0, 0), spine=(4, 0, 0),
              hand_l=(0.26, 0.05, 0.92), hand_r=(-0.26, 0.05, 0.92), elbow_l=(1, 0.6, -0.2), elbow_r=(-1, 0.6, -0.2),
              foot_l=(0.15, -0.30, 0.094), foot_r=(-0.15, 0.18, 0.094), foot_pitch_r=30)),
        (8, k(pelvis=(0.0, -0.32, 0.84), pelvis_rot=(46, 0, 0), spine=(10, 0, 0), clav_l=(4, 14), clav_r=(4, 14),
              hand_l=(0.28, -0.80, 1.06), hand_r=(-0.28, -0.80, 1.06), elbow_l=(1, 0, -0.3), elbow_r=(-1, 0, -0.3),
              foot_l=(0.15, -0.42, 0.094), foot_r=(-0.15, 0.02, 0.12), foot_pitch_r=45)),
        (12, k(pelvis=(0.0, -0.48, 0.80), pelvis_rot=(56, 0, 0), spine=(14, 0, 0), clav_l=(2, 20), clav_r=(2, 20),
               hand_l=(0.10, -0.92, 1.00), hand_r=(-0.10, -0.92, 1.00), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
               foot_l=(0.15, -0.42, 0.094), foot_r=(-0.15, -0.10, 0.14), foot_pitch_r=55, foot_pitch_l=20)),
        (15, k(pelvis=(0.0, -0.58, 0.74), pelvis_rot=(62, 0, 0), spine=(16, 0, 0), clav_l=(2, 22), clav_r=(2, 22),
               hand_l=(0.08, -1.02, 0.94), hand_r=(-0.08, -1.02, 0.94), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
               foot_l=(0.15, -0.42, 0.094), foot_r=(-0.15, -0.16, 0.12), foot_pitch_r=60, foot_pitch_l=30)),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 15


def _tackle_variant(rig, wrap_z, sink_z, drive_pitch, final_pelvis_z, reach_y=-0.95):
    """Tackle family: same phrasing as tackle_wrap (load, explode, wrap, drive) at a different contact height."""
    look = (0.0, -4.0, wrap_z)

    def k(**kw):
        d = {"knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0), "look": look,
             "elbow_l": (1, 0.3, -0.4), "elbow_r": (-1, 0.3, -0.4)}
        d.update(kw)
        return d
    keys = [
        (0, k(pelvis=(0.0, 0.0, 0.92), pelvis_rot=(22, 0, 0), spine=(8, 0, 0),
              hand_l=(0.24, -0.20, 1.02), hand_r=(-0.24, -0.02, 1.0),
              foot_l=(0.14, -0.20, 0.094), foot_r=(-0.14, 0.22, 0.094), foot_pitch_r=25)),
        (4, k(pelvis=(0.0, -0.10, sink_z), pelvis_rot=(drive_pitch * 0.6, 0, 0), spine=(4, 0, 0),
              hand_l=(0.26, 0.05, sink_z + 0.14), hand_r=(-0.26, 0.05, sink_z + 0.14), elbow_l=(1, 0.6, -0.2),
              elbow_r=(-1, 0.6, -0.2), foot_l=(0.15, -0.30, 0.094), foot_r=(-0.15, 0.18, 0.094), foot_pitch_r=30)),
        (8, k(pelvis=(0.0, -0.32, (sink_z + final_pelvis_z) / 2 + 0.04), pelvis_rot=(drive_pitch * 0.8, 0, 0),
              spine=(10, 0, 0), clav_l=(4, 14), clav_r=(4, 14),
              hand_l=(0.28, reach_y + 0.15, wrap_z + 0.04), hand_r=(-0.28, reach_y + 0.15, wrap_z + 0.04),
              elbow_l=(1, 0, -0.3), elbow_r=(-1, 0, -0.3),
              foot_l=(0.15, -0.42, 0.094), foot_r=(-0.15, 0.02, 0.12), foot_pitch_r=45)),
        (12, k(pelvis=(0.0, -0.48, final_pelvis_z + 0.05), pelvis_rot=(drive_pitch * 0.92, 0, 0), spine=(14, 0, 0),
               clav_l=(2, 20), clav_r=(2, 20), hand_l=(0.10, reach_y, wrap_z), hand_r=(-0.10, reach_y, wrap_z),
               elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2), foot_l=(0.15, -0.42, 0.094), foot_r=(-0.15, -0.10, 0.14),
               foot_pitch_r=55, foot_pitch_l=20)),
        (15, k(pelvis=(0.0, -0.58, final_pelvis_z), pelvis_rot=(drive_pitch, 0, 0), spine=(16, 0, 0),
               clav_l=(2, 22), clav_r=(2, 22), hand_l=(0.08, reach_y - 0.08, wrap_z - 0.06),
               hand_r=(-0.08, reach_y - 0.08, wrap_z - 0.06), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
               foot_l=(0.15, -0.42, 0.094), foot_r=(-0.15, -0.16, 0.12), foot_pitch_r=60, foot_pitch_l=30)),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 15


def tackle_high(rig):
    """Chest-level wrap, stays tall and drives the carrier back."""
    return _tackle_variant(rig, wrap_z=1.22, sink_z=0.84, drive_pitch=34, final_pelvis_z=0.86, reach_y=-0.80)


def tackle_low(rig):
    """Shoot the thighs: low hips, head across, arms wrap at the knees."""
    return _tackle_variant(rig, wrap_z=0.62, sink_z=0.70, drive_pitch=68, final_pelvis_z=0.62, reach_y=-0.85)


def tackle_dive(rig):
    """Diving tackle, 18 frames in place: last-step push, full extension with the feet leaving the ground, arms
    reaching for the legs, body near horizontal at the end (the ragdoll takes over on 'down')."""
    look = (0.0, -4.0, 0.5)

    def k(**kw):
        d = {"knee_l": (0.2, -1, 0.3), "knee_r": (-0.2, -1, 0.3), "look": look,
             "elbow_l": (1, 0, -0.3), "elbow_r": (-1, 0, -0.3)}
        d.update(kw)
        return d
    keys = [
        (0, k(pelvis=(0.0, 0.0, 0.88), pelvis_rot=(26, 0, 0), spine=(8, 0, 0), hand_l=(0.26, -0.15, 0.98),
              hand_r=(-0.26, 0.0, 0.98), foot_l=(0.14, -0.22, 0.094), foot_r=(-0.14, 0.24, 0.094), foot_pitch_r=30)),
        (5, k(pelvis=(0.0, -0.22, 0.80), pelvis_rot=(48, 0, 0), spine=(10, 0, 0), hand_l=(0.24, -0.62, 0.92),
              hand_r=(-0.24, -0.62, 0.92), foot_l=(0.14, -0.24, 0.094), foot_r=(-0.14, 0.18, 0.12), foot_pitch_l=25,
              foot_pitch_r=55)),
        (11, k(pelvis=(0.0, -0.62, 0.72), pelvis_rot=(72, 0, 0), spine=(8, 0, 0), clav_l=(6, 18), clav_r=(6, 18),
               hand_l=(0.18, -1.22, 0.62), hand_r=(-0.18, -1.22, 0.62), foot_l=(0.14, -0.18, 0.22),
               foot_r=(-0.14, 0.12, 0.34), foot_pitch_l=40, foot_pitch_r=50, knee_l=(0.2, -0.3, -1), knee_r=(-0.2, -0.3, -1))),
        (18, k(pelvis=(0.0, -0.95, 0.56), pelvis_rot=(84, 0, 0), spine=(4, 0, 0), clav_l=(6, 20), clav_r=(6, 20),
               hand_l=(0.16, -1.60, 0.40), hand_r=(-0.16, -1.60, 0.40), foot_l=(0.14, -0.20, 0.36),
               foot_r=(-0.14, 0.02, 0.46), foot_pitch_l=50, foot_pitch_r=60, knee_l=(0.2, -0.2, -1), knee_r=(-0.2, -0.2, -1))),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 18


def block_engaged(rig):
    """Engaged block loop (run or pass), 30 frames: hips low, hands inside on the chest plate, elbows tight, short
    choppy steps that keep a wide base. In place."""
    base = {"pelvis": (0.0, 0.0, 0.78), "pelvis_rot": (34, 0, 0), "spine": (8, 0, 0), "clav_l": (2, 14), "clav_r": (2, 14),
            "hand_l": (0.12, -0.50, 1.08), "hand_r": (-0.12, -0.50, 1.08), "elbow_l": (1, 0.3, -1), "elbow_r": (-1, 0.3, -1),
            "foot_l": (0.28, -0.12, 0.094), "foot_r": (-0.28, 0.06, 0.094), "foot_pitch_l": 10, "foot_pitch_r": 14,
            "knee_l": (0.35, -1, 0), "knee_r": (-0.35, -1, 0), "look": (0.0, -3.0, 1.3)}

    def k(lift_l=0.0, lift_r=0.0, dl=0.0, dr=0.0, push=0.0, sway=0.0):
        d = dict(base)
        d["pelvis"] = (sway, -push, 0.78 - abs(push) * 0.2)
        d["pelvis_rot"] = (34 + push * 40, 0, sway * 30)
        fx, fy, fz = base["foot_l"]
        d["foot_l"] = (fx, fy + dl, fz + lift_l)
        fx, fy, fz = base["foot_r"]
        d["foot_r"] = (fx, fy + dr, fz + lift_r)
        hx, hy, hz = base["hand_l"]
        d["hand_l"] = (hx, hy - push * 1.5, hz)
        hx, hy, hz = base["hand_r"]
        d["hand_r"] = (hx, hy - push * 1.5, hz)
        return d
    keys = [(0, k()), (7, k(lift_l=0.05, dl=-0.04, push=0.02, sway=-0.015)), (12, k(dl=-0.04, push=0.03)),
            (22, k(dl=-0.04, lift_r=0.05, dr=-0.04, push=0.01, sway=0.015)), (30, k())]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 30


def block_passset(rig):
    """Pass protection set loop, 36 frames: balanced wide base, inside foot up, hands up and ready (thumbs up, palms
    out), light bounce on the insteps and a short kick slide and recover. In place."""
    base = {"pelvis": (0.0, 0.04, 0.80), "pelvis_rot": (22, 0, 0), "spine": (4, 0, 0),
            "hand_l": (0.16, -0.32, 1.12), "hand_r": (-0.16, -0.32, 1.12), "elbow_l": (1, 0.4, -0.8), "elbow_r": (-1, 0.4, -0.8),
            "foot_l": (0.26, -0.08, 0.094), "foot_r": (-0.30, 0.16, 0.094), "foot_pitch_l": 8, "foot_pitch_r": 12,
            "foot_yaw_r": -10, "knee_l": (0.35, -1, 0), "knee_r": (-0.35, -1, 0), "look": (0.0, -6.0, 1.4)}

    def k(dz=0.0, slide=0.0, lift=0.0):
        d = dict(base)
        x, y, z = base["pelvis"]
        d["pelvis"] = (x - slide * 0.4, y, z + dz)
        fx, fy, fz = base["foot_r"]
        d["foot_r"] = (fx - slide, fy + slide * 0.5, fz + lift)
        return d
    keys = [(0, k()), (8, k(dz=0.012)), (14, k(slide=0.06, lift=0.035)), (19, k(slide=0.10, dz=-0.008)),
            (27, k(slide=0.03, lift=0.03)), (36, k())]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 36


def getup_face_down(rig):
    """Get up from lying face down, 46 frames: hands under the shoulders, push up, right knee under, left foot
    plants, rise and settle. Body lies along Y with the head toward -Y (actor forward)."""
    look = (0.0, -4.0, 0.3)

    def k(**kw):
        d = {"look": look, "elbow_l": (1, 0.6, 0), "elbow_r": (-1, 0.6, 0),
             "knee_l": (0.2, -0.3, -1), "knee_r": (-0.2, -0.3, -1)}
        d.update(kw)
        return d
    keys = [
        (0, k(pelvis=(0.0, 0.10, 0.15), pelvis_rot=(92, 0, 0), spine=(-6, 0, 0), hand_l=(0.26, -0.30, 0.07),
              hand_r=(-0.26, -0.30, 0.07), foot_l=(0.13, 0.95, 0.06), foot_r=(-0.13, 0.95, 0.06),
              foot_pitch_l=70, foot_pitch_r=70)),
        (10, k(pelvis=(0.0, 0.10, 0.24), pelvis_rot=(80, 0, 0), spine=(-14, 0, 0), hand_l=(0.26, -0.30, 0.07),
               hand_r=(-0.26, -0.30, 0.07), foot_l=(0.13, 0.92, 0.06), foot_r=(-0.13, 0.92, 0.06),
               foot_pitch_l=70, foot_pitch_r=70, look=(0.0, -4.0, 0.8))),
        (20, k(pelvis=(0.0, 0.12, 0.42), pelvis_rot=(62, 0, 0), spine=(4, 0, 0), hand_l=(0.26, -0.32, 0.07),
               hand_r=(-0.26, -0.32, 0.07), foot_l=(0.13, 0.80, 0.06), foot_r=(-0.15, 0.30, 0.06), foot_pitch_l=70,
               foot_pitch_r=60, knee_r=(-0.2, -1, -0.6), look=(0.0, -4.0, 1.0))),
        (30, k(pelvis=(0.0, 0.02, 0.62), pelvis_rot=(40, 0, 0), spine=(10, 0, 0), hand_l=(0.24, -0.26, 0.55),
               hand_r=(-0.24, -0.20, 0.60), elbow_l=(1, 0.3, -0.6), elbow_r=(-1, 0.3, -0.6),
               foot_l=(0.15, -0.22, 0.094), foot_r=(-0.15, 0.24, 0.07), foot_pitch_r=50,
               knee_l=(0.2, -1, 0), knee_r=(-0.2, -1, -0.3), look=(0.0, -6.0, 1.3))),
        (40, k(pelvis=(0.0, 0.0, 0.86), pelvis_rot=(14, 0, 0), spine=(4, 0, 0), hand_l=(0.24, -0.08, 0.98),
               hand_r=(-0.24, -0.04, 0.98), elbow_l=(1, 0.3, -1), elbow_r=(-1, 0.3, -1),
               foot_l=(0.15, -0.20, 0.094), foot_r=(-0.15, 0.10, 0.094), knee_l=(0.2, -1, 0), knee_r=(-0.2, -1, 0),
               look=(0.0, -8.0, 1.6))),
        (46, k(pelvis=(0.0, -0.05, 0.92), pelvis_rot=(5, 0, 0), spine=(2, 0, 0), hand_l=(0.25, -0.06, 1.0),
               hand_r=(-0.25, -0.04, 1.0), elbow_l=(1, 0.3, -1), elbow_r=(-1, 0.3, -1),
               foot_l=(0.15, -0.12, 0.094), foot_r=(-0.15, 0.02, 0.094), knee_l=(0.2, -1, 0), knee_r=(-0.2, -1, 0),
               look=(0.0, -8.0, 1.7))),
    ]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 46


def getup_face_up(rig):
    """Get up from lying on the back, 48 frames: sit up on the hands, feet tucked under, lean over the feet and
    push off the thighs, rise and settle. Body lies along Y with the head toward +Y (behind the actor)."""
    def k(**kw):
        d = {"look": (0.0, -6.0, 1.2), "elbow_l": (1, 0.4, -1), "elbow_r": (-1, 0.4, -1),
             "knee_l": (0.2, -0.2, 1), "knee_r": (-0.2, -0.2, 1)}
        d.update(kw)
        return d
    keys = [
        (0, k(pelvis=(0.0, 0.0, 0.14), pelvis_rot=(-86, 0, 0), spine=(4, 0, 0), hand_l=(0.40, 0.30, 0.07),
              hand_r=(-0.40, 0.30, 0.07), foot_l=(0.14, -0.88, 0.07), foot_r=(-0.14, -0.88, 0.07), look=(0.0, 2.0, 2.0))),
        (12, k(pelvis=(0.0, 0.0, 0.16), pelvis_rot=(-30, 0, 0), spine=(18, 0, 0), hand_l=(0.30, 0.28, 0.07),
               hand_r=(-0.30, 0.28, 0.07), elbow_l=(1, 0.6, 0), elbow_r=(-1, 0.6, 0),
               foot_l=(0.15, -0.62, 0.07), foot_r=(-0.15, -0.62, 0.07), look=(0.0, -6.0, 0.8))),
        (22, k(pelvis=(0.0, 0.0, 0.18), pelvis_rot=(-6, 0, 0), spine=(26, 0, 0), hand_l=(0.28, 0.20, 0.07),
               hand_r=(-0.28, 0.20, 0.07), elbow_l=(1, 0.6, 0), elbow_r=(-1, 0.6, 0),
               foot_l=(0.16, -0.36, 0.094), foot_r=(-0.16, -0.36, 0.094), foot_pitch_l=10, foot_pitch_r=10)),
        (32, k(pelvis=(0.0, -0.18, 0.52), pelvis_rot=(46, 0, 0), spine=(14, 0, 0), hand_l=(0.20, -0.42, 0.50),
               hand_r=(-0.20, -0.42, 0.50), elbow_l=(1, 0.4, -0.6), elbow_r=(-1, 0.4, -0.6),
               foot_l=(0.16, -0.36, 0.094), foot_r=(-0.16, -0.36, 0.094), knee_l=(0.2, -1, 0), knee_r=(-0.2, -1, 0))),
        (42, k(pelvis=(0.0, -0.30, 0.84), pelvis_rot=(14, 0, 0), spine=(4, 0, 0), hand_l=(0.24, -0.36, 0.98),
               hand_r=(-0.24, -0.34, 0.98), foot_l=(0.16, -0.36, 0.094), foot_r=(-0.16, -0.36, 0.094),
               knee_l=(0.2, -1, 0), knee_r=(-0.2, -1, 0), look=(0.0, -8.0, 1.6))),
        (48, k(pelvis=(0.0, -0.34, 0.92), pelvis_rot=(5, 0, 0), spine=(2, 0, 0), hand_l=(0.25, -0.38, 1.0),
               hand_r=(-0.25, -0.36, 1.0), foot_l=(0.15, -0.40, 0.094), foot_r=(-0.15, -0.30, 0.094),
               knee_l=(0.2, -1, 0), knee_r=(-0.2, -1, 0), look=(0.0, -8.0, 1.7))),
    ]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 48


def _shed_base(**kw):
    d = {"pelvis": (0.0, 0.0, 0.80), "pelvis_rot": (36, 0, 0), "spine": (8, 0, 0),
         "hand_l": (0.12, -0.50, 1.08), "hand_r": (-0.12, -0.50, 1.08), "elbow_l": (1, 0.3, -1), "elbow_r": (-1, 0.3, -1),
         "foot_l": (0.26, -0.10, 0.094), "foot_r": (-0.26, 0.08, 0.094), "foot_pitch_l": 10, "foot_pitch_r": 16,
         "knee_l": (0.35, -1, 0), "knee_r": (-0.35, -1, 0), "look": (0.0, -6.0, 1.2)}
    d.update(kw)
    return d


def shed_swim(rig):
    """Pass rush swim to the left, 14 frames in place: chop the blocker's hands down with the left hand, right arm
    swims over the top, hips turn through the gap, first step past him."""
    keys = [
        (0, _shed_base()),
        (3, _shed_base(hand_l=(0.10, -0.46, 0.86), hand_r=(-0.10, -0.40, 1.20), clav_r=(8, 6),
                       pelvis_rot=(36, 6, 0))),
        (7, _shed_base(pelvis=(0.06, -0.06, 0.82), pelvis_rot=(30, 22, 6), spine=(8, 18, 4),
                       hand_l=(0.20, -0.30, 0.95), hand_r=(0.02, -0.40, 1.62), elbow_r=(-0.4, 0.2, 1), clav_r=(16, 10),
                       foot_l=(0.30, -0.20, 0.13), foot_pitch_r=30)),
        (11, _shed_base(pelvis=(0.18, -0.14, 0.84), pelvis_rot=(32, 34, 8), spine=(10, 14, 4),
                        hand_l=(0.30, -0.20, 1.00), hand_r=(0.20, -0.30, 1.20), elbow_r=(-0.5, 0.5, 0.2),
                        foot_l=(0.38, -0.30, 0.094), foot_r=(-0.10, 0.06, 0.13), foot_pitch_r=40, foot_yaw_l=25)),
        (14, _shed_base(pelvis=(0.26, -0.22, 0.86), pelvis_rot=(28, 30, 4), spine=(8, 10, 2),
                        hand_l=(0.40, -0.18, 1.02), hand_r=(0.06, -0.12, 1.06), elbow_r=(-1, 0.3, -0.5),
                        foot_l=(0.38, -0.30, 0.094), foot_r=(0.10, -0.24, 0.12), foot_pitch_r=30, foot_yaw_l=25)),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 14


def shed_rip(rig):
    """Pass rush rip to the right, 14 frames in place: dip the right shoulder, the right arm uppercuts under the
    blocker's arm, lean and flatten past him."""
    keys = [
        (0, _shed_base()),
        (4, _shed_base(pelvis=(-0.03, -0.02, 0.74), pelvis_rot=(44, -10, -8), spine=(10, -10, -6),
                       hand_r=(-0.20, -0.20, 0.82), elbow_r=(-1, 0.5, -0.6), hand_l=(0.08, -0.52, 1.10),
                       clav_r=(-8, -6))),
        (8, _shed_base(pelvis=(-0.12, -0.08, 0.76), pelvis_rot=(42, -24, -12), spine=(10, -16, -8),
                       hand_r=(-0.16, -0.46, 1.34), elbow_r=(-1, 0.2, 0.2), hand_l=(0.10, -0.34, 1.02),
                       foot_r=(-0.32, -0.22, 0.13), foot_pitch_l=30)),
        (14, _shed_base(pelvis=(-0.26, -0.22, 0.82), pelvis_rot=(34, -32, -10), spine=(8, -12, -6),
                        hand_r=(-0.30, -0.24, 1.10), elbow_r=(-1, 0.3, -0.4), hand_l=(0.0, -0.30, 1.02),
                        foot_r=(-0.40, -0.32, 0.094), foot_l=(-0.06, -0.20, 0.13), foot_pitch_l=36, foot_yaw_r=-25)),
    ]
    for f, kk in _in_place(keys):
        rig.key(f, kk)
    return 0, 14


def fan_cheer(rig):
    """Spectator cheer loop for the stands, 60 frames: clap twice overhead, two right fist pumps, both arms up
    (touchdown), back to a bounce. Feet stay planted (the stands slope; people stay on their spot)."""
    feet = {"foot_l": (0.14, 0.0, 0.094), "foot_r": (-0.14, 0.02, 0.094), "knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0),
            "look": (0.0, -10.0, 1.2)}

    def k(pz=0.92, hl=(0.22, -0.10, 1.00), hr=(-0.22, -0.10, 1.00), el=(1, 0.3, -0.6), er=(-1, 0.3, -0.6), pitch=4):
        d = dict(feet)
        d.update({"pelvis": (0.0, 0.0, pz), "pelvis_rot": (pitch, 0, 0), "spine": (2, 0, 0),
                  "hand_l": hl, "hand_r": hr, "elbow_l": el, "elbow_r": er})
        return d
    up_apart = dict(hl=(0.22, -0.18, 1.70), hr=(-0.22, -0.18, 1.70), el=(1, 0.2, 0.2), er=(-1, 0.2, 0.2))
    up_clap = dict(hl=(0.03, -0.22, 1.74), hr=(-0.03, -0.22, 1.74), el=(1, 0.2, 0.2), er=(-1, 0.2, 0.2))
    keys = [
        (0, k(pz=0.90, pitch=6)),
        (6, k(pz=0.93, **up_apart)), (9, k(pz=0.92, **up_clap)), (13, k(pz=0.93, **up_apart)), (16, k(pz=0.92, **up_clap)),
        (24, k(pz=0.91, hl=(0.20, -0.20, 1.12), hr=(-0.24, -0.10, 1.72), er=(-1, 0.2, 0.2))),
        (28, k(pz=0.90, hl=(0.20, -0.20, 1.10), hr=(-0.26, -0.14, 1.38))),
        (32, k(pz=0.92, hl=(0.20, -0.20, 1.12), hr=(-0.24, -0.10, 1.74), er=(-1, 0.2, 0.2))),
        (36, k(pz=0.90, hl=(0.20, -0.20, 1.10), hr=(-0.26, -0.14, 1.38))),
        (44, k(pz=0.95, hl=(0.18, -0.06, 1.80), hr=(-0.18, -0.06, 1.80), el=(1, 0, 0.2), er=(-1, 0, 0.2), pitch=0)),
        (52, k(pz=0.93, hl=(0.18, -0.06, 1.78), hr=(-0.18, -0.06, 1.78), el=(1, 0, 0.2), er=(-1, 0, 0.2), pitch=2)),
        (60, k(pz=0.90, pitch=6)),
    ]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 60


def _juke_left_keys():
    """Ball carrier cutting left (ball tucked high and tight in the right arm), 14 frames."""
    look = (-2.0, -8.0, 1.5)

    def k(**kw):
        d = {"knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0), "look": look,
             "hand_r": None, "elbow_r": (-1, 0.2, -1), "elbow_l": (1, 0.2, -0.5)}
        d.update(kw)
        return d
    tuck = lambda px: (px - 0.10, -0.12, 1.12)
    keys = [
        (0, k(pelvis=(0.0, 0.0, 0.90), pelvis_rot=(14, 0, 0), spine=(4, 0, 0), hand_r=tuck(0.0),
              hand_l=(0.30, -0.12, 1.02), foot_l=(0.12, -0.12, 0.094), foot_r=(-0.12, 0.14, 0.11), foot_pitch_r=20)),
        (4, k(pelvis=(0.04, -0.04, 0.80), pelvis_rot=(20, 8, 14), spine=(6, 10, 6), hand_r=tuck(0.04),
              hand_l=(0.42, -0.02, 1.10), foot_l=(0.18, -0.10, 0.16), foot_r=(-0.34, -0.04, 0.094),
              foot_yaw_r=25, look=(2.0, -8.0, 1.4))),
        (8, k(pelvis=(0.18, -0.08, 0.83), pelvis_rot=(18, 18, 10), spine=(6, 12, 4), hand_r=tuck(0.18),
              hand_l=(0.55, -0.10, 1.12), foot_l=(0.42, -0.16, 0.094), foot_r=(-0.20, 0.02, 0.12), foot_pitch_r=35,
              foot_yaw_l=20, look=(4.0, -8.0, 1.5))),
        (14, k(pelvis=(0.30, -0.14, 0.90), pelvis_rot=(14, 22, 4), spine=(4, 8, 2), hand_r=tuck(0.30),
               hand_l=(0.58, -0.22, 1.02), foot_l=(0.42, -0.16, 0.11), foot_r=(0.18, -0.30, 0.094), foot_pitch_l=20,
               foot_yaw_r=18, look=(5.0, -8.0, 1.5))),
    ]
    return keys


def juke_left(rig):
    for f, kk in _in_place(_juke_left_keys()):
        rig.key(f, kk)
    return 0, 14


def juke_right(rig):
    for f, kk in _in_place([(f, mirror(k)) for f, k in _juke_left_keys()]):
        rig.key(f, kk)
    return 0, 14


def qb_kneel(rig):
    """Victory formation: take the snap, drop to the right knee with the ball held at the chest, hold."""
    hold = {"hand_l": (0.06, -0.26, 0.98), "hand_r": (-0.06, -0.26, 0.98), "elbow_l": (1, 0.3, -0.6),
            "elbow_r": (-1, 0.3, -0.6), "knee_l": (0.2, -1, 0), "knee_r": (-0.1, -1, -0.4), "look": (0.0, -10.0, 1.2)}

    def k(**kw):
        d = dict(hold)
        d.update(kw)
        return d
    keys = [
        (0, k(pelvis=(0.0, 0.05, 0.84), pelvis_rot=(22, 0, 0), spine=(8, 0, 0),
              foot_l=(0.21, -0.02, 0.094), foot_r=(-0.21, 0.02, 0.094))),
        (8, k(pelvis=(0.0, 0.12, 0.70), pelvis_rot=(12, 0, 0), spine=(6, 0, 0),
              foot_l=(0.18, -0.16, 0.094), foot_r=(-0.18, 0.30, 0.11), foot_pitch_r=50)),
        (16, k(pelvis=(0.0, 0.16, 0.56), pelvis_rot=(6, 0, 0), spine=(8, 0, 0),
               hand_l=(0.06, -0.10, 0.86), hand_r=(-0.06, -0.10, 0.86),
               foot_l=(0.18, -0.24, 0.094), foot_r=(-0.16, 0.48, 0.10), foot_pitch_r=70)),
        (30, k(pelvis=(0.0, 0.16, 0.565), pelvis_rot=(8, 0, 0), spine=(9, 0, 0),
               hand_l=(0.06, -0.10, 0.865), hand_r=(-0.06, -0.10, 0.865),
               foot_l=(0.18, -0.24, 0.094), foot_r=(-0.16, 0.48, 0.10), foot_pitch_r=70)),
    ]
    for f, kk in keys:
        rig.key(f, kk)
    return 0, 30


CLIPS = {"Fan_Cheer_Loop": fan_cheer, "Shed_Swim": shed_swim, "Shed_Rip": shed_rip, "GetUp_FaceDown": getup_face_down, "GetUp_FaceUp": getup_face_up, "QB_SnapCatch": qb_snap_catch, "QB_Handoff": qb_handoff, "Block_Engaged_Loop": block_engaged, "Block_PassSet_Loop": block_passset, "Tackle_Wrap": tackle_wrap, "Tackle_High": tackle_high, "Tackle_Low": tackle_low, "Tackle_Dive": tackle_dive, "Juke_L": juke_left, "Juke_R": juke_right, "QB_Kneel": qb_kneel,
         "Lineman_GetOff": lineman_getoff, "Celebrate_Loop": celebrate, "Stance3pt_Loop": stance_3pt, "Stance2pt_Loop": stance_2pt, "QB_Shotgun_Loop": stance_shotgun,
         "QB_Throw": qb_throw}
