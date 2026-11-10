"""TAG football clips, batch 2 (hand keyed, original choreography from coaching technique; no mocap, no reference
clips). Same conventions as clips.py: facing -Y, left +X, up +Z, meters, 30 fps, ankles rest at z 0.094, positive
yaw turns left. Coverage targets: docs/LOOK_TARGETS.md categories (catches, ball carrier moves, rush moves, tackles,
stances, kicking, celebrations, get-ups, idles).
"""
import math

import clips as C

ANKLE = 0.094


def _rot(x, y, deg):
    a = math.radians(deg)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


def _base(**kw):
    d = {"knee_l": (0.2, -1, 0), "knee_r": (-0.2, -1, 0), "elbow_l": (1, 0.3, -0.5), "elbow_r": (-1, 0.3, -0.5)}
    d.update(kw)
    return d


def _run(frames, speed=6.5, cadence=4.8, pelvis_z=0.90, lean=16, step=2, phase=0.0, width=0.11, arms=True,
         overrides=None):
    """In-place running keys every `step` frames: pelvis advances at `speed` (m/s) along -Y, feet plant under the body
    (stance ~38% of a stride), swing in an arc, pelvis bobs; arms counter-swing unless overridden. overrides(f, t)
    returns a dict merged into the key (hands, spine, look, ...). The keys go through clips._in_place."""
    T = 30.0 / cadence                      # frames per step
    cyc = 2 * T                             # frames per stride (left + right)
    stance = 0.30
    keys = []
    for f in list(range(0, frames, step)) + [frames]:
        t = f / 30.0
        py = -speed * t
        k = _base(pelvis=(0.0, py, pelvis_z - 0.02 * math.cos(2 * math.pi * (f / T + phase))),
                  pelvis_rot=(lean, 0, 0), spine=(6, 0, 0), look=(0.0, py - 6.0, 1.45))
        for side, sx, off in (("l", 1, 0.0), ("r", -1, 0.5)):
            u = ((f / cyc) + phase + off) % 1.0     # 0..stance planted, then swing
            n = math.floor((f / cyc) + phase + off)
            # plant y = pelvis y at the middle of this stance phase (foot lands slightly ahead, leaves behind)
            def plant_y(k_):
                tm = ((k_ + stance * 0.5 - phase - off) * cyc) / 30.0
                return -speed * tm
            if u < stance:
                y, z, pitch = plant_y(n), ANKLE, 10 + 40 * (u / stance)
            else:
                s = (u - stance) / (1 - stance)
                y = plant_y(n) + (plant_y(n + 1) - plant_y(n)) * s
                z, pitch = ANKLE + 0.30 * math.sin(math.pi * s), 50 - 45 * s
            k["foot_" + side] = (sx * width, y, z)
            k["foot_pitch_" + side] = pitch
            if arms:
                sw = math.sin(2 * math.pi * ((f / cyc) + phase + off))
                k["hand_" + side] = (sx * 0.22, py - 0.10 - 0.22 * sw, 1.00 + 0.12 * max(0.0, -sw))
                k["elbow_" + side] = (sx * 1, 0.6, -0.4)
        if overrides:
            k.update(overrides(f, t))
        keys.append((f, k))
    return C._in_place(keys)


def _key_all(rig, keys, mirror=False):
    for f, k in keys:
        rig.key(f, C.mirror(k) if mirror else k)
    return keys[0][0], keys[-1][0]


def _tuck(py, side="r"):
    """Ball tucked high and tight against the chest (ball arm) for a carrier at pelvis y py."""
    s = -1 if side == "r" else 1
    return {"hand_" + side: (s * 0.12, py - 0.16, 1.18), "elbow_" + side: (s * 1, 0.8, -0.2)}


# ============================================================================================== catches
def _catch(rig, high_z, reach_y, hands_gap, frames=18, hop=0.0, crouch=0.0, look_z=None):
    """Settled catch family: eyes on the ball, hands to the window (thumbs together above the chest, pinkies together
    below the waist), the ball's give into the body, then tucked high and tight. In place."""
    pz = 0.92 - crouch
    hinge = 10 + crouch * 190             # low ball: hinge at the hips so the hands reach the window
    lz = look_z if look_z is not None else high_z
    keys = [
        (0, _base(pelvis=(0, 0, 0.92), pelvis_rot=(12, 0, 0), spine=(4, 0, 0), look=(0, -6, lz),
                  hand_l=(0.20, -0.10, 1.00), hand_r=(-0.20, -0.10, 1.00),
                  foot_l=(0.14, -0.12, ANKLE), foot_r=(-0.14, 0.14, ANKLE), foot_pitch_r=18)),
        (5, _base(pelvis=(0, -0.04, pz + hop), pelvis_rot=(hinge, 0, 0), spine=(2 + crouch * 80, 0, 0), look=(0, -4, lz),
                  hand_l=(hands_gap, reach_y, high_z), hand_r=(-hands_gap, reach_y, high_z),
                  elbow_l=(1, 0.2, -0.6), elbow_r=(-1, 0.2, -0.6),
                  foot_l=(0.15, -0.14, ANKLE + hop), foot_r=(-0.15, 0.12, ANKLE + hop * 0.8), foot_pitch_l=10,
                  foot_pitch_r=14)),
        (8, _base(pelvis=(0, -0.04, pz + hop * 0.6), pelvis_rot=(hinge + 2, 0, 0), spine=(4 + crouch * 80, 0, 0),
                  look=(0, -3, lz), hand_l=(hands_gap, reach_y + 0.10, high_z - 0.04),
                  hand_r=(-hands_gap, reach_y + 0.10, high_z - 0.04), elbow_l=(1, 0.3, -0.6), elbow_r=(-1, 0.3, -0.6),
                  foot_l=(0.15, -0.14, ANKLE), foot_r=(-0.15, 0.12, ANKLE))),
        (13, _base(pelvis=(0, -0.02, 0.88), pelvis_rot=(16, 0, 0), spine=(8, 0, 0), look=(0, -6, 1.4),
                   hand_l=(0.06, -0.22, 1.24), hand_r=(-0.08, -0.22, 1.26), elbow_l=(1, 0.5, -0.4),
                   elbow_r=(-1, 0.5, -0.4), foot_l=(0.15, -0.14, ANKLE), foot_r=(-0.15, 0.12, ANKLE))),
        (frames, _base(pelvis=(0, -0.02, 0.90), pelvis_rot=(14, 0, 0), spine=(6, 0, 0), look=(0, -8, 1.45),
                       hand_l=(0.10, -0.20, 1.10), **_tuck(0.0), foot_l=(0.15, -0.14, ANKLE),
                       foot_r=(-0.15, 0.12, ANKLE), foot_pitch_r=12)),
        (frames + 14, _base(pelvis=(0, -0.06, 0.90), pelvis_rot=(16, -8, 0), spine=(6, -6, 0), look=(-0.8, -8, 1.5),
                            hand_l=(0.14, -0.16, 1.04), **_tuck(0.0), foot_l=(0.15, -0.18, ANKLE),
                            foot_r=(-0.15, 0.10, ANKLE), foot_pitch_r=18)),
    ]
    return _key_all(rig, keys)


def catch_high(rig):
    """Go up for it: small hop, hands to a diamond above the head, ball pulled down and tucked."""
    return _catch(rig, high_z=1.80, reach_y=-0.12, hands_gap=0.05, hop=0.10, look_z=1.9)


def catch_chest(rig):
    """Numbers-high ball: thumbs together, hands out in front of the chest (catch it with the hands, not the pads)."""
    return _catch(rig, high_z=1.30, reach_y=-0.40, hands_gap=0.06, frames=15, look_z=1.3)


def catch_low(rig):
    """Below the waist: bend the knees, pinkies together, scoop up into the body."""
    return _catch(rig, high_z=0.62, reach_y=-0.46, hands_gap=0.07, crouch=0.20, look_z=0.5)


def catch_over_shoulder(rig):
    """Over the shoulder (deep ball): eyes back over the left shoulder, hands up and turned back, ball in, tuck and
    turn the head upfield. Played over a run: the legs keep the stride."""
    def ov(f, t):
        py = -6.5 * t
        if f <= 14:
            s = min(1.0, f / 8.0)
            return {"look": (0.9, py + 5.0, 2.0), "spine": (2, 22 * s, 0),
                    "hand_l": (0.14, py + 0.02 * s, 1.10 + 0.62 * s), "hand_r": (0.02, py + 0.02 * s, 1.10 + 0.62 * s),
                    "elbow_l": (1, 0.4, 0.2), "elbow_r": (-1, 0.6, 0.2)}
        s = min(1.0, (f - 14) / 6.0)
        d = {"look": (0.0, py - 6.0, 1.45), "spine": (6, 22 * (1 - s), 0), "hand_l": (0.10, py - 0.18, 1.12)}
        d.update(_tuck(py))
        return d
    return _key_all(rig, _run(26, speed=6.5, overrides=ov))


def _catch_sideline(rig, mirror):
    """Sideline toe tap: high reach out toward the sideline (left), upper body leaning out of bounds, both toes
    dragged in (feet stay planted, toes down), ball secured to the chest."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.92), pelvis_rot=(12, 0, 0), spine=(4, 0, 0), look=(0.6, -4, 1.6),
                  hand_l=(0.22, -0.10, 1.0), hand_r=(-0.20, -0.10, 1.0),
                  foot_l=(0.12, -0.14, ANKLE), foot_r=(-0.14, 0.14, ANKLE))),
        (6, _base(pelvis=(0.08, -0.04, 0.94), pelvis_rot=(8, 14, -10), spine=(0, 18, -16), look=(1.2, -2.5, 1.8),
                  hand_l=(0.46, -0.16, 1.58), hand_r=(0.18, -0.24, 1.60), elbow_l=(1, 0, -0.6), elbow_r=(0, 0.2, -1),
                  foot_l=(0.10, -0.14, ANKLE), foot_r=(-0.12, 0.16, 0.12), foot_pitch_l=30, foot_pitch_r=50)),
        (10, _base(pelvis=(0.14, -0.04, 0.88), pelvis_rot=(10, 12, -14), spine=(4, 8, -16), look=(1.2, -2.0, 1.4),
                   hand_l=(0.38, -0.18, 1.38), hand_r=(0.16, -0.24, 1.40), elbow_l=(1, 0.2, -0.6),
                   elbow_r=(0, 0.3, -1), foot_l=(0.10, -0.14, ANKLE), foot_r=(-0.12, 0.18, ANKLE),
                   foot_pitch_l=45, foot_pitch_r=55)),
        (15, _base(pelvis=(0.16, -0.02, 0.86), pelvis_rot=(14, 6, -10), spine=(8, 4, -8), look=(0.2, -3.0, 0.2),
                   hand_l=(0.20, -0.20, 1.16), **_tuck(0.0), foot_l=(0.10, -0.14, ANKLE),
                   foot_r=(-0.12, 0.18, ANKLE), foot_pitch_l=40, foot_pitch_r=50)),
        (22, _base(pelvis=(0.10, 0.0, 0.92), pelvis_rot=(10, 0, 0), spine=(4, 0, 0), look=(0, -6, 1.5),
                   hand_l=(0.16, -0.16, 1.06), **_tuck(0.0), foot_l=(0.12, -0.14, ANKLE),
                   foot_r=(-0.14, 0.14, ANKLE))),
    ]
    return _key_all(rig, keys, mirror)


def catch_sideline_l(rig):
    return _catch_sideline(rig, False)


def catch_sideline_r(rig):
    return _catch_sideline(rig, True)


# ======================================================================================== ball carrier
def _spin(rig, mirror):
    """Spin move (to the left): plant the right foot, whip the head and shoulders around 360 on the balls of the
    feet, ball locked in the outside arm, come out of it running. In place; the sim keeps the actor moving."""
    keys = []
    frames = 18
    for f in range(0, frames + 1, 2):
        s = f / frames
        yaw = 360 * min(1.0, max(0.0, (s - 0.12) / 0.72))  # spin over the middle of the clip
        py = -5.0 * f / 30.0
        k = _base(pelvis=(0, py, 0.86 - 0.04 * math.sin(math.pi * s)), pelvis_rot=(20, yaw, 0), spine=(8, 0, 0))
        for side, ox in (("l", 0.14), ("r", -0.14)):
            fx, fy = _rot(ox, 0.0, yaw)
            # pivot on the right foot in the first half, the left in the second
            lift = 0.10 * math.sin(math.pi * s) if (side == "l") == (s < 0.5) else 0.0
            k["foot_" + side] = (fx, py + fy, ANKLE + lift)
            k["foot_yaw_" + side] = yaw
            k["foot_pitch_" + side] = 25
        lx, ly = _rot(0.0, -5.0, yaw)
        k["look"] = (lx, py + ly, 1.4)
        hx, hy = _rot(-0.12, -0.16, yaw)
        k["hand_r"] = (hx, py + hy, 1.16)
        gx, gy = _rot(0.30, -0.10, yaw)
        k["hand_l"] = (gx, py + gy, 1.05)
        kx, ky = _rot(-1, 0.8, yaw)
        k["elbow_r"] = (kx, ky, -0.2)
        keys.append((f, k))
    return _key_all(rig, C._in_place(keys), mirror)


def spin_l(rig):
    return _spin(rig, False)


def spin_r(rig):
    return _spin(rig, True)


def _stiff_arm(rig, mirror):
    """Stiff arm with the left (off) arm: ball tucked right, the left arm shoots out to the defender's facemask /
    shoulder pad at chest height and locks, the carrier leans into it and keeps running, then rips the arm back."""
    def ov(f, t):
        py = -6.0 * t
        d = dict(_tuck(py))
        if f < 6:
            s = f / 6.0
            d["hand_l"] = (0.24 + 0.10 * s, py - 0.10 - 0.30 * s, 1.05 + 0.20 * s)
        elif f < 18:
            d["hand_l"] = (0.36, py - 0.52, 1.28)
            d["elbow_l"] = (1, 0.2, -0.2)
            d["spine"] = (12, -10, 6)
            d["pelvis_rot"] = (20, -8, 4)
        else:
            s = (f - 18) / 6.0
            d["hand_l"] = (0.36 - 0.12 * s, py - 0.52 + 0.40 * s, 1.28 - 0.25 * s)
        d["look"] = (0.6, py - 4.0, 1.4)
        return d
    return _key_all(rig, _run(24, speed=6.0, overrides=ov), mirror)


def stiff_arm_l(rig):
    return _stiff_arm(rig, False)


def stiff_arm_r(rig):
    return _stiff_arm(rig, True)


def truck(rig):
    """Lower the boom: the carrier drops his pad level, leads with the left shoulder and forearm, ball tucked in the
    right arm, drives his legs through contact."""
    def ov(f, t):
        py = -5.5 * t
        s = min(1.0, f / 6.0) if f < 14 else max(0.0, 1 - (f - 14) / 6.0)
        d = dict(_tuck(py))
        d.update({"pelvis_rot": (16 + 26 * s, 8 * s, 0), "spine": (6 + 16 * s, 10 * s, 0),
                  "hand_l": (0.10, py - 0.30 - 0.06 * s, 1.06 + 0.10 * s), "elbow_l": (1, 0.2, 0.4),
                  "clav_l": (6 * s, 14 * s), "look": (0.0, py - 3.0, 1.0 + 0.3 * (1 - s))})
        return d
    return _key_all(rig, _run(20, speed=5.5, pelvis_z=0.86, overrides=ov))


def hurdle(rig):
    """Hurdle a diving defender: gather step, take off from the right foot, left knee drives up and over with the
    leg extended, trail leg tucks, land on the left and run on. In place."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.90), pelvis_rot=(16, 0, 0), spine=(6, 0, 0), look=(0, -6, 0.6),
                  foot_l=(0.12, -0.20, ANKLE), foot_r=(-0.12, 0.20, ANKLE), foot_pitch_r=30, **_tuck(0.0),
                  hand_l=(0.24, -0.10, 1.0))),
        (5, _base(pelvis=(0, -0.45, 0.84), pelvis_rot=(22, 0, 0), spine=(8, 0, 0), look=(0, -6, 0.5),
                  foot_l=(0.12, -0.80, 0.30), foot_r=(-0.12, -0.40, ANKLE), foot_pitch_r=40, **_tuck(-0.45),
                  hand_l=(0.30, -0.70, 1.20))),
        (11, _base(pelvis=(0, -1.30, 1.24), pelvis_rot=(26, 0, 0), spine=(10, 0, 0), look=(0, -6, 0.6),
                   foot_l=(0.12, -1.85, 0.80), foot_r=(-0.12, -1.10, 0.70), foot_pitch_l=0, foot_pitch_r=60,
                   knee_l=(0.2, -1, 0.4), knee_r=(-0.2, -0.5, -1), **_tuck(-1.30), hand_l=(0.34, -1.70, 1.40))),
        (17, _base(pelvis=(0, -2.20, 1.08), pelvis_rot=(18, 0, 0), spine=(6, 0, 0), look=(0, -8, 1.2),
                   foot_l=(0.12, -2.50, 0.30), foot_r=(-0.12, -2.05, 0.55), foot_pitch_r=50, **_tuck(-2.20),
                   hand_l=(0.30, -2.40, 1.20))),
        (22, _base(pelvis=(0, -2.85, 0.88), pelvis_rot=(18, 0, 0), spine=(6, 0, 0), look=(0, -9, 1.4),
                   foot_l=(0.12, -2.90, ANKLE), foot_r=(-0.12, -2.60, 0.20), foot_pitch_r=40, **_tuck(-2.85),
                   hand_l=(0.24, -2.95, 1.02))),
    ]
    return _key_all(rig, C._in_place(keys))


# ========================================================================================= pass rush
def rush_bull(rig):
    """Bull rush: low pad level, both hands strike inside to the blocker's chest, arms lock out, legs drive him back."""
    def ov(f, t):
        py = -3.0 * t
        s = min(1.0, f / 5.0)
        return {"pelvis_rot": (36, 0, 0), "spine": (10, 0, 0), "clav_l": (2, 16 * s), "clav_r": (2, 16 * s),
                "hand_l": (0.14, py - 0.30 - 0.32 * s, 1.08), "hand_r": (-0.14, py - 0.30 - 0.32 * s, 1.08),
                "elbow_l": (1, 0.2, -1), "elbow_r": (-1, 0.2, -1), "look": (0, py - 3.0, 1.2)}
    return _key_all(rig, _run(20, speed=3.0, cadence=5.0, pelvis_z=0.80, lean=32, arms=False, overrides=ov))


def _rush_spin(rig, mirror):
    """Pass rush spin: plant on the blocker's edge, drop the inside shoulder and spin back inside on the pivot foot,
    the near arm clubbing over, come out turned toward the quarterback."""
    keys = []
    frames = 18
    for f in range(0, frames + 1, 2):
        s = f / frames
        yaw = 360 * min(1.0, max(0.0, (s - 0.15) / 0.7))
        py = -2.5 * f / 30.0
        k = _base(pelvis=(0, py, 0.82), pelvis_rot=(30, yaw, 0), spine=(10, 0, 0))
        for side, ox in (("l", 0.15), ("r", -0.15)):
            fx, fy = _rot(ox, 0.0, yaw)
            lift = 0.10 * math.sin(math.pi * s) if (side == "r") == (s < 0.5) else 0.0
            k["foot_" + side] = (fx, py + fy, ANKLE + lift)
            k["foot_yaw_" + side] = yaw
            k["foot_pitch_" + side] = 25
        for side, (hx0, hy0, hz) in (("l", (0.22, -0.22, 1.08)), ("r", (-0.22, -0.16, 1.02))):
            hx, hy = _rot(hx0, hy0, yaw)
            k["hand_" + side] = (hx, py + hy, hz)
        lx, ly = _rot(0.0, -4.0, yaw)
        k["look"] = (lx, py + ly, 1.3)
        keys.append((f, k))
    return _key_all(rig, C._in_place(keys), mirror)


def rush_spin_l(rig):
    return _rush_spin(rig, False)


def rush_spin_r(rig):
    return _rush_spin(rig, True)


def rush_club(rig):
    """Club and go: the outside (right) forearm clubs down across the blocker's outside shoulder, the inside hand
    pushes the hip, the rusher flattens his path past him."""
    def ov(f, t):
        py = -4.0 * t
        if f < 6:
            s = f / 6.0
            hr = (-0.30, py - 0.20, 1.20 + 0.30 * s)
        elif f < 11:
            s = (f - 6) / 5.0
            hr = (-0.30 + 0.30 * s, py - 0.50, 1.50 - 0.50 * s)
        else:
            hr = (0.0, py - 0.30, 1.00)
        return {"pelvis_rot": (28, -6, 0), "spine": (8, -8, 0), "hand_r": hr, "elbow_r": (-1, 0.4, 0.4),
                "hand_l": (0.18, py - 0.45, 1.00), "elbow_l": (1, 0.3, -0.6), "look": (0.0, py - 4.0, 1.3)}
    return _key_all(rig, _run(16, speed=4.0, cadence=5.0, pelvis_z=0.82, lean=28, arms=False, overrides=ov))


# =========================================================================================== tackles
def tackle_shoulder(rig):
    """Big hit: no wrap until contact; the tackler launches up and through with the shoulder, head across the bow,
    arms swing up through contact and then close."""
    return C._tackle_variant(rig, wrap_z=1.10, sink_z=0.78, drive_pitch=48, final_pelvis_z=0.80, reach_y=-0.70)


def tackle_arm(rig):
    """Arm tackle from the side: the defender is a step late, reaches across with the near (left) arm, grabs jersey
    at the shoulder and drags, the other hand following."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.92), pelvis_rot=(20, 0, 0), spine=(8, 0, 0), look=(0.8, -3, 1.2),
                  hand_l=(0.24, -0.20, 1.02), hand_r=(-0.24, -0.02, 1.0),
                  foot_l=(0.14, -0.20, ANKLE), foot_r=(-0.14, 0.22, ANKLE), foot_pitch_r=25)),
        (5, _base(pelvis=(0.10, -0.28, 0.86), pelvis_rot=(30, 20, 0), spine=(10, 15, 0), look=(1.2, -2, 1.2),
                  hand_l=(0.62, -0.50, 1.24), elbow_l=(1, 0, -0.3), hand_r=(0.10, -0.30, 1.04),
                  foot_l=(0.30, -0.45, ANKLE), foot_r=(-0.10, 0.04, 0.14), foot_pitch_r=50)),
        (10, _base(pelvis=(0.16, -0.40, 0.82), pelvis_rot=(34, 30, 0), spine=(14, 20, 0), look=(1.0, -1.5, 0.9),
                   hand_l=(0.62, -0.30, 1.10), hand_r=(0.28, -0.44, 1.02), elbow_l=(1, 0.4, 0), elbow_r=(0, 0.4, -1),
                   foot_l=(0.30, -0.45, ANKLE), foot_r=(-0.08, -0.10, ANKLE), foot_pitch_l=20)),
        (16, _base(pelvis=(0.20, -0.50, 0.76), pelvis_rot=(40, 40, 0), spine=(16, 24, 0), look=(0.8, -1.0, 0.5),
                   hand_l=(0.52, -0.14, 0.90), hand_r=(0.26, -0.34, 0.88), elbow_l=(1, 0.5, 0.2), elbow_r=(0, 0.5, -1),
                   foot_l=(0.32, -0.45, ANKLE), foot_r=(-0.02, -0.20, ANKLE), foot_pitch_l=30, foot_pitch_r=20)),
    ]
    return _key_all(rig, C._in_place(keys))


def tackle_assist(rig):
    """Second man in (gang tackle): arrives late and high, both hands grab the carrier's shoulder pads from the side
    and ride him down on top of the first tackler."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.92), pelvis_rot=(18, 0, 0), spine=(6, 0, 0), look=(0, -3, 1.1),
                  hand_l=(0.24, -0.16, 1.02), hand_r=(-0.24, -0.02, 1.0),
                  foot_l=(0.14, -0.20, ANKLE), foot_r=(-0.14, 0.22, ANKLE), foot_pitch_r=25)),
        (5, _base(pelvis=(0, -0.34, 0.90), pelvis_rot=(28, 0, 0), spine=(10, 0, 0), look=(0, -2, 1.3),
                  hand_l=(0.22, -0.66, 1.40), hand_r=(-0.22, -0.66, 1.40), elbow_l=(1, 0, -0.4), elbow_r=(-1, 0, -0.4),
                  foot_l=(0.14, -0.50, ANKLE), foot_r=(-0.14, -0.10, 0.12), foot_pitch_r=45)),
        (10, _base(pelvis=(0, -0.56, 0.80), pelvis_rot=(44, 0, 0), spine=(16, 0, 0), look=(0, -1.5, 0.6),
                   hand_l=(0.16, -0.82, 1.10), hand_r=(-0.16, -0.82, 1.10), elbow_l=(1, 0.2, 0), elbow_r=(-1, 0.2, 0),
                   foot_l=(0.14, -0.50, ANKLE), foot_r=(-0.14, -0.30, ANKLE), foot_pitch_l=25)),
        (16, _base(pelvis=(0, -0.72, 0.66), pelvis_rot=(62, 0, 0), spine=(20, 0, 0), look=(0, -1.2, 0.2),
                   hand_l=(0.14, -0.98, 0.78), hand_r=(-0.14, -0.98, 0.78), elbow_l=(1, 0.3, 0.2), elbow_r=(-1, 0.3, 0.2),
                   foot_l=(0.14, -0.50, ANKLE), foot_r=(-0.14, -0.40, ANKLE), foot_pitch_l=40, foot_pitch_r=40)),
    ]
    return _key_all(rig, C._in_place(keys))


# =========================================================================================== stances
def _stance_loop(rig, base, **kw):
    for f, k in C.breathe(base, **kw):
        rig.key(f, k)
    return 0, 90


def stance_4pt(rig):
    """Defensive tackle four point stance (goal line / run downs): both hands down, hips slightly higher than the
    shoulders, weight forward on the hands to fire out low."""
    b = dict(C.STANCE_3PT)
    b.update({"pelvis": (0.0, 0.16, 0.72), "pelvis_rot": (92, 0, 0), "spine": (48, 0, 0), "clav_l": (-14, 24),
              "hand_l": (0.20, -0.40, 0.11), "elbow_l": (0.4, 0.3, 0.2), "foot_l": (0.25, 0.10, ANKLE),
              "foot_r": (-0.25, 0.10, ANKLE), "foot_pitch_l": 22})
    return _stance_loop(rig, b, amp_z=0.004, amp_pitch=0.8, shift=0.005)


def stance_rb(rig):
    """Running back in the backfield: two point, square, hands resting on the thigh pads, eyes on the line."""
    b = {"pelvis": (0.0, 0.04, 0.80), "pelvis_rot": (40, 0, 0), "spine": (14, 0, 0),
         "hand_l": (0.16, -0.22, 0.66), "elbow_l": (1, 0.3, 0.0), "hand_r": (-0.16, -0.22, 0.66), "elbow_r": (-1, 0.3, 0.0),
         "foot_l": (0.20, -0.04, ANKLE), "foot_r": (-0.20, 0.02, ANKLE), "foot_pitch_l": 10, "foot_pitch_r": 12,
         "foot_yaw_l": 6, "foot_yaw_r": -6, "knee_l": (0.25, -1, 0), "knee_r": (-0.25, -1, 0), "look": (0, -8, 1.0)}
    return _stance_loop(rig, b, amp_z=0.006, amp_pitch=1.0, shift=0.008)


def stance_db(rig):
    """Cornerback / safety: staggered, outside foot back, knees bent and weight on the balls, arms loose at the hips,
    eyes inside on the quarterback."""
    b = {"pelvis": (0.0, 0.04, 0.82), "pelvis_rot": (30, 12, 0), "spine": (8, 6, 0),
         "hand_l": (0.24, -0.12, 0.88), "elbow_l": (1, 0.4, -0.2), "hand_r": (-0.22, -0.10, 0.86), "elbow_r": (-1, 0.4, -0.2),
         "foot_l": (0.14, -0.10, ANKLE), "foot_r": (-0.16, 0.24, ANKLE), "foot_pitch_l": 12, "foot_pitch_r": 26,
         "foot_yaw_l": 10, "foot_yaw_r": -4, "knee_l": (0.25, -1, 0), "knee_r": (-0.25, -1, 0), "look": (2.5, -8, 1.2)}
    return _stance_loop(rig, b, amp_z=0.007, amp_pitch=1.2, shift=0.012)


def stance_lb(rig):
    """Linebacker: square, feet shoulder width, knees bent, hands up and ready to take on a block, reading the backfield."""
    b = {"pelvis": (0.0, 0.04, 0.80), "pelvis_rot": (32, 0, 0), "spine": (8, 0, 0),
         "hand_l": (0.20, -0.30, 1.02), "elbow_l": (1, 0.4, -0.4), "hand_r": (-0.20, -0.30, 1.02), "elbow_r": (-1, 0.4, -0.4),
         "foot_l": (0.22, -0.04, ANKLE), "foot_r": (-0.22, 0.02, ANKLE), "foot_pitch_l": 12, "foot_pitch_r": 12,
         "foot_yaw_l": 6, "foot_yaw_r": -6, "knee_l": (0.25, -1, 0), "knee_r": (-0.25, -1, 0), "look": (0, -8, 0.9)}
    return _stance_loop(rig, b, amp_z=0.008, amp_pitch=1.4, shift=0.014)


def stance_k(rig):
    """Kicker at his mark before the approach: angled two steps back and to the side, plant-side arm out for balance,
    leaning slightly forward, eyes on the spot."""
    b = {"pelvis": (0.0, 0.0, 0.92), "pelvis_rot": (14, -24, 0), "spine": (6, -6, 0),
         "hand_l": (0.34, -0.06, 0.98), "elbow_l": (1, 0.2, -0.4), "hand_r": (-0.20, 0.02, 0.88), "elbow_r": (-1, 0.4, 0.0),
         "foot_l": (0.12, -0.08, ANKLE), "foot_r": (-0.10, 0.16, ANKLE), "foot_pitch_r": 20,
         "foot_yaw_l": -20, "foot_yaw_r": -30, "look": (0.6, -3, 0.1)}
    return _stance_loop(rig, b, amp_z=0.006, amp_pitch=1.0, shift=0.006)


# =========================================================================================== kicking
def _kick(rig, approach, swing_z, frames, plant_y, follow):
    """Soccer-style kick family: approach steps, plant the left foot beside the ball, hips open, the right leg swings
    from the hip through the ball and up into a high follow through, arms out for balance. In place."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.92), pelvis_rot=(14, -20, 0), spine=(6, -6, 0), look=(0.3, -3, 0.1),
                  hand_l=(0.30, -0.06, 0.98), hand_r=(-0.22, 0.02, 0.90),
                  foot_l=(0.12, -0.06, ANKLE), foot_r=(-0.10, 0.16, ANKLE), foot_pitch_r=20)),
    ]
    f = 0
    y = 0.0
    for i in range(approach):
        f += 6
        y -= 0.9
        side = "r" if i % 2 == 0 else "l"
        k = _base(pelvis=(0, y, 0.90), pelvis_rot=(16, -10, 0), spine=(6, -4, 0), look=(0.2, plant_y - 1.0, 0.1),
                  hand_l=(0.30, y - 0.10, 1.00), hand_r=(-0.24, y + 0.10, 0.92))
        k["foot_" + side] = ((-0.10 if side == "r" else 0.12), y - 0.25, ANKLE)
        other = "l" if side == "r" else "r"
        k["foot_" + other] = ((0.12 if other == "l" else -0.10), y + 0.35, 0.18)
        k["foot_pitch_" + other] = 40
        keys.append((f, k))
    # plant
    f += 5
    keys.append((f, _base(pelvis=(0, plant_y + 0.20, 0.86), pelvis_rot=(10, 20, 8), spine=(-4, 16, 6),
                          look=(0.0, plant_y - 0.4, 0.1), hand_l=(0.56, plant_y + 0.20, 1.20),
                          hand_r=(-0.40, plant_y + 0.40, 1.10), elbow_l=(1, 0, -0.4), elbow_r=(-1, 0, -0.4),
                          foot_l=(0.20, plant_y, ANKLE), foot_r=(-0.10, plant_y + 0.70, 0.40),
                          foot_pitch_r=70, knee_r=(-0.2, 0.2, -1))))
    # contact
    f += 3
    keys.append((f, _base(pelvis=(0, plant_y + 0.10, 0.88), pelvis_rot=(4, 10, 6), spine=(-6, 6, 4),
                          look=(0.0, plant_y - 0.5, 0.1), hand_l=(0.60, plant_y + 0.10, 1.24),
                          hand_r=(-0.44, plant_y + 0.30, 1.14), foot_l=(0.20, plant_y, ANKLE),
                          foot_r=(-0.04, plant_y - 0.10, 0.20), foot_pitch_r=60)))
    # follow through
    f += 6
    keys.append((f, _base(pelvis=(0, plant_y - 0.10, 0.94), pelvis_rot=(-10, 0, 4), spine=(-10, 0, 0),
                          look=(0.0, plant_y - 6.0, 1.0 + follow), hand_l=(0.50, plant_y - 0.10, 1.24),
                          hand_r=(-0.40, plant_y + 0.10, 1.20), foot_l=(0.20, plant_y, ANKLE),
                          foot_r=(-0.02, plant_y - 0.60, swing_z), foot_pitch_r=10, knee_r=(-0.2, -1, 0.6))))
    # land
    f += 8
    keys.append((f, _base(pelvis=(0, plant_y - 0.55, 0.92), pelvis_rot=(12, 0, 0), spine=(4, 0, 0),
                          look=(0.0, plant_y - 8.0, 1.6), hand_l=(0.26, plant_y - 0.60, 1.00),
                          hand_r=(-0.24, plant_y - 0.50, 0.98), foot_l=(0.14, plant_y - 0.30, ANKLE),
                          foot_r=(-0.14, plant_y - 0.80, ANKLE), foot_pitch_l=20)))
    return _key_all(rig, C._in_place(keys))


def kick_kickoff(rig):
    """Kickoff: a longer approach, full swing, follow through to above the waist."""
    return _kick(rig, approach=3, swing_z=1.10, frames=40, plant_y=-3.0, follow=0.6)


def kick_field_goal(rig):
    """Field goal / extra point: two step approach from the side, compact swing, follow through high and straight."""
    return _kick(rig, approach=2, swing_z=1.25, frames=34, plant_y=-2.0, follow=0.8)


def kick_punt(rig):
    """Punt: catch the snap at the chest, two steps, drop the ball off the hands, right leg swings up to head height
    with the toe pointed, arms out."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.92), pelvis_rot=(12, 0, 0), spine=(4, 0, 0), look=(0, 3, 1.0),
                  hand_l=(0.10, -0.30, 1.16), hand_r=(-0.10, -0.30, 1.16),
                  foot_l=(0.14, 0.0, ANKLE), foot_r=(-0.14, 0.10, ANKLE))),
        (6, _base(pelvis=(0, -0.05, 0.92), pelvis_rot=(10, 0, 0), spine=(4, 0, 0), look=(0, -4, 1.4),
                  hand_l=(0.08, -0.34, 1.10), hand_r=(-0.12, -0.34, 1.10),
                  foot_l=(0.14, 0.0, ANKLE), foot_r=(-0.14, -0.40, ANKLE))),
        (12, _base(pelvis=(0, -0.65, 0.92), pelvis_rot=(8, 0, 0), spine=(2, 0, 0), look=(0, -4, 0.9),
                   hand_l=(0.10, -1.05, 1.02), hand_r=(-0.14, -1.05, 1.02),
                   foot_l=(0.14, -0.90, ANKLE), foot_r=(-0.14, -0.40, 0.14), foot_pitch_r=40)),
        (17, _base(pelvis=(0, -1.00, 0.92), pelvis_rot=(0, 0, 0), spine=(-4, 0, 0), look=(0, -3, 0.6),
                   hand_l=(0.50, -1.10, 1.20), hand_r=(-0.50, -1.10, 1.20), elbow_l=(1, 0, -0.3), elbow_r=(-1, 0, -0.3),
                   foot_l=(0.14, -0.95, ANKLE), foot_r=(-0.10, -1.30, 0.70), foot_pitch_r=0, knee_r=(-0.2, -1, 0.6))),
        (21, _base(pelvis=(0, -1.12, 0.98), pelvis_rot=(-16, 0, 0), spine=(-10, 0, 0), look=(0, -6, 3.0),
                   hand_l=(0.54, -1.10, 1.30), hand_r=(-0.54, -1.10, 1.30),
                   foot_l=(0.14, -1.00, 0.14), foot_r=(-0.06, -1.60, 1.40), foot_pitch_l=40, foot_pitch_r=0,
                   knee_r=(-0.2, -1, 0.8))),
        (30, _base(pelvis=(0, -1.55, 0.92), pelvis_rot=(12, 0, 0), spine=(4, 0, 0), look=(0, -9, 2.5),
                   hand_l=(0.26, -1.60, 1.00), hand_r=(-0.26, -1.60, 1.00),
                   foot_l=(0.14, -1.30, ANKLE), foot_r=(-0.14, -1.85, ANKLE))),
    ]
    return _key_all(rig, C._in_place(keys))


# ====================================================================================== celebrations
def _loop(rig, keys, frames):
    keys = list(keys) + [(frames, keys[0][1])]
    for f, k in keys:
        rig.key(f, k)
    return 0, frames


def celebrate_flex(rig):
    """Double biceps flex toward the crowd, pulses twice, re-flexes."""
    feet = {"foot_l": (0.20, -0.04, ANKLE), "foot_r": (-0.20, 0.04, ANKLE), "foot_yaw_l": 10, "foot_yaw_r": -10}
    up = _base(pelvis=(0, 0, 0.86), pelvis_rot=(4, 0, 0), spine=(-6, 0, 0), look=(0, -6, 2.2),
               hand_l=(0.36, -0.04, 1.52), hand_r=(-0.36, -0.04, 1.52), elbow_l=(1, 0, -0.6), elbow_r=(-1, 0, -0.6),
               clav_l=(0, 10), clav_r=(0, 10), **feet)
    squeeze = dict(up, pelvis=(0, 0, 0.82), spine=(2, 0, 0), hand_l=(0.30, -0.10, 1.44), hand_r=(-0.30, -0.10, 1.44),
                   look=(0, -6, 1.6))
    return _loop(rig, [(0, up), (12, squeeze), (24, up), (36, squeeze), (48, up)], 60)


def celebrate_point_up(rig):
    """Both arms raised, pointing to the sky, head back, then a slow turn to the crowd."""
    feet = {"foot_l": (0.16, -0.04, ANKLE), "foot_r": (-0.16, 0.06, ANKLE)}
    a = _base(pelvis=(0, 0, 0.92), pelvis_rot=(-2, 0, 0), spine=(-10, 0, 0), look=(0, -1.5, 4.0),
              hand_l=(0.20, -0.02, 1.78), hand_r=(-0.20, -0.02, 1.78), elbow_l=(1, 0, -0.2), elbow_r=(-1, 0, -0.2), **feet)
    b = dict(a, pelvis_rot=(-2, 25, 0), spine=(-10, 15, 0), look=(2.0, -1.0, 3.5))
    c = dict(a, pelvis_rot=(-2, -25, 0), spine=(-10, -15, 0), look=(-2.0, -1.0, 3.5))
    return _loop(rig, [(0, a), (30, b), (60, a), (90, c)], 120)


def celebrate_leap(rig):
    """Jump and fist pump: crouch, leap with the right fist punching up, land, repeat."""
    feet = lambda z: {"foot_l": (0.16, -0.02, z), "foot_r": (-0.16, 0.04, z)}
    crouch = _base(pelvis=(0, 0, 0.74), pelvis_rot=(26, 0, 0), spine=(10, 0, 0), look=(0, -6, 1.4),
                   hand_l=(0.22, -0.16, 0.92), hand_r=(-0.22, -0.16, 0.92), **feet(ANKLE))
    air = _base(pelvis=(0, 0, 1.22), pelvis_rot=(0, 0, 0), spine=(-8, 0, 0), look=(0, -4, 2.6),
                hand_l=(0.26, -0.12, 1.30), hand_r=(-0.18, -0.06, 2.12), elbow_l=(1, 0.2, -0.4), elbow_r=(-1, 0, -0.1),
                **feet(0.36))
    land = dict(crouch, pelvis=(0, 0, 0.80), hand_r=(-0.24, -0.12, 1.10))
    return _loop(rig, [(0, crouch), (8, air), (16, land), (30, crouch)], 40)


def celebrate_first_down(rig):
    """First down signal: arm extended pointing downfield (to his right), a couple of emphatic pumps."""
    feet = {"foot_l": (0.18, -0.02, ANKLE), "foot_r": (-0.18, 0.06, ANKLE)}
    a = _base(pelvis=(0, 0, 0.90), pelvis_rot=(6, -30, 0), spine=(0, -20, 0), look=(-3.0, -2.0, 1.5),
              hand_r=(-0.54, -0.14, 1.30), elbow_r=(-1, 0, -0.4), hand_l=(0.24, -0.04, 0.96), **feet)
    b = dict(a, hand_r=(-0.50, -0.20, 1.22), pelvis=(0, 0, 0.86))
    return _loop(rig, [(0, a), (8, b), (16, a), (24, b), (34, a)], 50)


def celebrate_shimmy(rig):
    """Side shuffle with shoulder shimmies and a hip roll (original, generic dance beat)."""
    def k(x, sh, z):
        return _base(pelvis=(x, 0, z), pelvis_rot=(8, 0, 6 * sh), spine=(4, 0, -10 * sh), look=(0, -6, 1.5),
                     hand_l=(0.30, -0.18, 1.10 + 0.06 * sh), hand_r=(-0.30, -0.18, 1.10 - 0.06 * sh),
                     elbow_l=(1, 0, -0.5), elbow_r=(-1, 0, -0.5), clav_l=(0, 8 * max(0, sh)), clav_r=(0, 8 * max(0, -sh)),
                     foot_l=(x + 0.20, -0.02, ANKLE), foot_r=(x - 0.20, 0.04, ANKLE))
    return _loop(rig, [(0, k(0, 0, 0.84)), (8, k(0.08, 1, 0.80)), (16, k(0, 0, 0.84)), (24, k(-0.08, -1, 0.80)),
                       (32, k(0, 0, 0.84)), (40, k(0.08, 1, 0.80)), (48, k(0, 0, 0.84))], 64)


# ============================================================================================ get-ups
def getup_roll(rig):
    """Roll to the side and up: lying on the right side, roll onto the knees, post a hand, rise. Ends in the idle."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.18), pelvis_rot=(90, 0, -80), spine=(0, 0, -10), look=(0.4, -1, 0.2),
                  hand_l=(0.12, -0.26, 0.30), hand_r=(-0.12, -0.24, 0.14), foot_l=(0.10, 0.80, 0.18),
                  foot_r=(-0.10, 0.80, 0.10), knee_l=(0.2, 0, -1), knee_r=(-0.2, 0, -1))),
        (12, _base(pelvis=(0, 0.10, 0.42), pelvis_rot=(70, 0, -20), spine=(30, 0, 0), look=(0, -1, 0.3),
                   hand_l=(0.22, -0.30, 0.11), hand_r=(-0.22, -0.30, 0.11), foot_l=(0.16, 0.45, 0.12),
                   foot_r=(-0.16, 0.45, 0.12), knee_l=(0.2, -1, 0), knee_r=(-0.2, -1, 0), foot_pitch_l=80, foot_pitch_r=80)),
        (22, _base(pelvis=(0, 0.06, 0.52), pelvis_rot=(50, 0, 0), spine=(24, 0, 0), look=(0, -2, 0.6),
                   hand_l=(0.20, -0.20, 0.56), hand_r=(-0.22, -0.26, 0.20), foot_l=(0.18, -0.30, ANKLE),
                   foot_r=(-0.16, 0.40, 0.12), foot_pitch_r=80)),
        (34, _base(pelvis=(0, 0.0, 0.82), pelvis_rot=(26, 0, 0), spine=(10, 0, 0), look=(0, -4, 1.2),
                   hand_l=(0.22, -0.20, 0.80), hand_r=(-0.22, -0.10, 0.84), foot_l=(0.16, -0.20, ANKLE),
                   foot_r=(-0.16, 0.16, ANKLE), foot_pitch_r=20)),
        (44, _base(pelvis=(0, 0.0, 0.94), pelvis_rot=(6, 0, 0), spine=(2, 0, 0), look=(0, -6, 1.6),
                   hand_l=(0.22, -0.04, 0.94), hand_r=(-0.22, -0.04, 0.94), foot_l=(0.15, -0.06, ANKLE),
                   foot_r=(-0.15, 0.06, ANKLE))),
    ]
    return _key_all(rig, keys)


# =============================================================================================== idles
def idle_hands_hips(rig):
    """Between plays: hands on the hips, catching his breath (chest heaves), weight shifting."""
    b = _base(pelvis=(0, 0, 0.93), pelvis_rot=(4, 0, 0), spine=(2, 0, 0), look=(0, -8, 1.5),
              hand_l=(0.20, 0.0, 0.98), hand_r=(-0.20, 0.0, 0.98), elbow_l=(1, -0.3, 0), elbow_r=(-1, -0.3, 0),
              foot_l=(0.15, -0.04, ANKLE), foot_r=(-0.15, 0.04, ANKLE), foot_yaw_l=10, foot_yaw_r=-10)
    for f, k in C.breathe(b, amp_z=0.012, amp_pitch=2.5, shift=0.03):
        rig.key(f, k)
    return 0, 90


def idle_hands_knees(rig):
    """Winded after a long drive: bent over, hands on the knees, heavy breathing."""
    b = _base(pelvis=(0, 0.06, 0.80), pelvis_rot=(56, 0, 0), spine=(24, 0, 0), look=(0, -3, 0.6),
              hand_l=(0.16, -0.26, 0.62), hand_r=(-0.16, -0.26, 0.62), elbow_l=(1, 0.3, 0.2), elbow_r=(-1, 0.3, 0.2),
              foot_l=(0.20, -0.06, ANKLE), foot_r=(-0.20, 0.02, ANKLE))
    for f, k in C.breathe(b, amp_z=0.014, amp_pitch=3.0, shift=0.01):
        rig.key(f, k)
    return 0, 90


MORE = {
    "Catch_High": catch_high, "Catch_Chest": catch_chest, "Catch_Low": catch_low,
    "Catch_OverShoulder": catch_over_shoulder, "Catch_Sideline_L": catch_sideline_l, "Catch_Sideline_R": catch_sideline_r,
    "Spin_L": spin_l, "Spin_R": spin_r, "StiffArm_L": stiff_arm_l, "StiffArm_R": stiff_arm_r, "Truck": truck,
    "Hurdle": hurdle,
    "Rush_Bull": rush_bull, "Rush_Spin_L": rush_spin_l, "Rush_Spin_R": rush_spin_r, "Rush_Club": rush_club,
    "Tackle_Shoulder": tackle_shoulder, "Tackle_Arm": tackle_arm, "Tackle_Assist": tackle_assist,
    "Stance4pt_Loop": stance_4pt, "StanceRB_Loop": stance_rb, "StanceDB_Loop": stance_db, "StanceLB_Loop": stance_lb,
    "StanceK_Loop": stance_k,
    "Kick_Kickoff": kick_kickoff, "Kick_FieldGoal": kick_field_goal, "Kick_Punt": kick_punt,
    "Celebrate_Flex_Loop": celebrate_flex, "Celebrate_PointUp_Loop": celebrate_point_up,
    "Celebrate_Leap_Loop": celebrate_leap, "Celebrate_FirstDown_Loop": celebrate_first_down,
    "Celebrate_Shimmy_Loop": celebrate_shimmy,
    "GetUp_Roll": getup_roll, "Idle_HandsHips_Loop": idle_hands_hips, "Idle_HandsKnees_Loop": idle_hands_knees,
}


# ================================================================================ coverage gaps (batch 3)
UNDER_CENTER = {"pelvis": (0.0, 0.04, 0.78), "pelvis_rot": (40, 0, 0), "spine": (20, 0, 0),
                "hand_l": (0.03, -0.36, 0.76), "elbow_l": (1, 0.3, -0.2), "hand_r": (-0.03, -0.38, 0.78),
                "elbow_r": (-1, 0.3, -0.2), "foot_l": (0.17, -0.04, ANKLE), "foot_r": (-0.17, 0.0, ANKLE),
                "foot_pitch_l": 10, "foot_pitch_r": 10, "foot_yaw_l": 6, "foot_yaw_r": -6,
                "knee_l": (0.25, -1, 0), "knee_r": (-0.25, -1, 0), "look": (0.0, -12.0, 1.4)}


def stance_under_center(rig):
    """QB under center: knees flexed, hands seated under the center (top hand pressure, bottom hand open), feet
    parallel, eyes reading the defense."""
    return _stance_loop(rig, UNDER_CENTER, amp_z=0.006, amp_pitch=1.0, shift=0.008)


def qb_under_center_snap(rig):
    """Under center snap, 14 frames: the ball slaps into the hands, comes straight to the belly, the QB pushes off the
    left foot and the right foot takes the first drop step back. In place."""
    def k(**kw):
        d = dict(UNDER_CENTER)
        d.update(kw)
        return d
    keys = [
        (0, k()),
        (3, k(hand_l=(0.03, -0.32, 0.80), hand_r=(-0.03, -0.33, 0.82), pelvis=(0.0, 0.05, 0.80))),
        (7, k(hand_l=(0.04, -0.10, 1.02), hand_r=(-0.06, -0.10, 1.06), elbow_l=(1, 0.4, -0.8), elbow_r=(-1, 0.4, -0.8),
              pelvis=(0.0, 0.12, 0.86), pelvis_rot=(18, -10, 0), spine=(8, -5, 0),
              foot_r=(-0.18, 0.30, 0.13), foot_pitch_r=20, foot_yaw_r=-25)),
        (14, k(hand_l=(0.02, 0.22, 1.12), hand_r=(-0.08, 0.24, 1.16), elbow_l=(1, 0.4, -0.8), elbow_r=(-1, 0.4, -0.8),
               pelvis=(0.0, 0.40, 0.88), pelvis_rot=(12, -25, 0), spine=(6, -10, 0), look=(0.0, -8.0, 1.6),
               foot_l=(0.16, 0.10, 0.12), foot_pitch_l=30, foot_r=(-0.20, 0.60, ANKLE), foot_yaw_r=-40)),
    ]
    return _key_all(rig, C._in_place(keys))


def qb_pitch(rig):
    """Toss to the right: reverse out, ball low in both hands, underhand two-hand pitch leading the back toward the
    sideline, follow through with the hands at the target, then carry out the fake."""
    look = (-4.0, 1.0, 0.9)
    keys = [
        (0, _base(pelvis=(0, 0.0, 0.88), pelvis_rot=(16, 0, 0), spine=(8, 0, 0), look=(0, -6, 1.4),
                  hand_l=(0.04, -0.22, 1.10), hand_r=(-0.06, -0.20, 1.12), elbow_l=(1, 0.4, -0.8), elbow_r=(-1, 0.4, -0.8),
                  foot_l=(0.18, -0.04, ANKLE), foot_r=(-0.18, 0.04, ANKLE))),
        (5, _base(pelvis=(-0.06, 0.08, 0.84), pelvis_rot=(20, -55, 0), spine=(12, -20, 0), look=look,
                  hand_l=(-0.04, -0.10, 0.84), hand_r=(-0.16, -0.04, 0.82), elbow_l=(1, 0.4, 0), elbow_r=(-1, 0.4, 0),
                  foot_l=(0.14, -0.04, ANKLE), foot_r=(-0.26, 0.24, ANKLE), foot_yaw_r=-60, foot_pitch_l=12)),
        (9, _base(pelvis=(-0.12, 0.12, 0.84), pelvis_rot=(16, -70, 0), spine=(8, -25, 0), look=look,
                  hand_l=(-0.32, 0.02, 0.92), hand_r=(-0.38, 0.10, 0.94), elbow_l=(1, 0, 0.2), elbow_r=(-1, 0, 0.2),
                  foot_l=(0.10, 0.0, ANKLE), foot_r=(-0.30, 0.26, ANKLE), foot_yaw_l=-30, foot_yaw_r=-70)),
        (13, _base(pelvis=(-0.14, 0.14, 0.86), pelvis_rot=(12, -72, 0), spine=(4, -26, 0), look=look,
                   hand_l=(-0.40, 0.14, 1.08), hand_r=(-0.44, 0.22, 1.10), elbow_l=(1, 0, 0), elbow_r=(-1, 0, 0),
                   foot_l=(0.10, 0.0, 0.12), foot_r=(-0.30, 0.26, ANKLE), foot_pitch_l=25, foot_yaw_r=-70)),
        (20, _base(pelvis=(-0.08, 0.10, 0.90), pelvis_rot=(12, -30, 0), spine=(4, -10, 0), look=(-1.0, -5.0, 1.4),
                   hand_l=(0.10, -0.10, 1.06), hand_r=(-0.12, -0.08, 1.06), elbow_l=(1, 0.3, -0.6), elbow_r=(-1, 0.3, -0.6),
                   foot_l=(0.16, -0.10, ANKLE), foot_r=(-0.26, 0.20, ANKLE))),
    ]
    return _key_all(rig, C._in_place(keys))


CATCH_DIVE_TRAVEL = 1.15  # m the body ends up ahead of where the dive started (handed to the actor afterwards)


def catch_dive(rig):
    """Layout dive for a ball just out of reach: last push step, full extension with the arms reaching out ahead, chest
    and forearms land, the body slides, then he pushes up to a knee and stands. Not in place: the travel is handed to
    the actor when the clip ends (FAthleteAssets::CatchDiveTravel)."""
    T = CATCH_DIVE_TRAVEL
    keys = [
        (0, _base(pelvis=(0, 0.0, 0.88), pelvis_rot=(20, 0, 0), spine=(8, 0, 0), look=(0, -4, 0.8),
                  hand_l=(0.24, -0.20, 1.02), hand_r=(-0.24, 0.0, 1.0),
                  foot_l=(0.13, -0.20, ANKLE), foot_r=(-0.13, 0.24, ANKLE), foot_pitch_r=30)),
        (5, _base(pelvis=(0, -0.25, 0.82), pelvis_rot=(46, 0, 0), spine=(10, 0, 0), look=(0, -3, 0.5),
                  hand_l=(0.20, -0.70, 0.95), hand_r=(-0.20, -0.70, 0.95), elbow_l=(1, 0, -0.3), elbow_r=(-1, 0, -0.3),
                  foot_l=(0.13, -0.25, ANKLE), foot_r=(-0.13, 0.15, 0.12), foot_pitch_l=30, foot_pitch_r=55)),
        (11, _base(pelvis=(0, -0.65, 0.66), pelvis_rot=(78, 0, 0), spine=(6, 0, 0), clav_l=(6, 20), clav_r=(6, 20),
                   look=(0, -2.5, 0.3), hand_l=(0.12, -1.10, 0.66), hand_r=(-0.12, -1.10, 0.66),
                   elbow_l=(1, 0, -0.3), elbow_r=(-1, 0, -0.3), foot_l=(0.13, -0.10, 0.30), foot_r=(-0.13, 0.10, 0.42),
                   foot_pitch_l=50, foot_pitch_r=60, knee_l=(0.2, -0.2, -1), knee_r=(-0.2, -0.2, -1))),
        (17, _base(pelvis=(0, -T + 0.15, 0.30), pelvis_rot=(88, 0, 0), spine=(4, 0, 0), clav_l=(6, 20), clav_r=(6, 20),
                   look=(0, -T - 2.0, 0.2), hand_l=(0.10, -T - 0.45, 0.22), hand_r=(-0.10, -T - 0.45, 0.22),
                   elbow_l=(1, 0, -0.2), elbow_r=(-1, 0, -0.2), foot_l=(0.14, -T + 0.95, 0.16),
                   foot_r=(-0.14, -T + 0.95, 0.20), foot_pitch_l=80, foot_pitch_r=80,
                   knee_l=(0.2, 0, -1), knee_r=(-0.2, 0, -1))),
        (23, _base(pelvis=(0, -T, 0.24), pelvis_rot=(90, 0, 0), spine=(2, 0, 0), look=(0, -T - 2.0, 0.2),
                   hand_l=(0.18, -T - 0.36, 0.12), hand_r=(-0.18, -T - 0.36, 0.12), elbow_l=(1, 0.3, 0), elbow_r=(-1, 0.3, 0),
                   foot_l=(0.14, -T + 0.90, 0.12), foot_r=(-0.14, -T + 0.90, 0.12), foot_pitch_l=85, foot_pitch_r=85,
                   knee_l=(0.2, 0, -1), knee_r=(-0.2, 0, -1))),
        (34, _base(pelvis=(0, -T + 0.20, 0.46), pelvis_rot=(60, 0, 0), spine=(24, 0, 0), look=(0, -T - 2.0, 0.4),
                   hand_l=(0.22, -T - 0.12, 0.11), hand_r=(-0.22, -T - 0.12, 0.11), foot_l=(0.18, -T + 0.40, 0.12),
                   foot_r=(-0.16, -T + 0.60, 0.12), foot_pitch_l=80, foot_pitch_r=80)),
        (44, _base(pelvis=(0, -T + 0.15, 0.62), pelvis_rot=(44, 0, 0), spine=(18, 0, 0), look=(0, -T - 3.0, 0.9),
                   hand_l=(0.22, -T - 0.06, 0.62), hand_r=(-0.22, -T - 0.10, 0.46), foot_l=(0.18, -T - 0.10, ANKLE),
                   foot_r=(-0.16, -T + 0.50, 0.12), foot_pitch_r=80)),
        (56, _base(pelvis=(0, -T, 0.93), pelvis_rot=(8, 0, 0), spine=(2, 0, 0), look=(0, -T - 6.0, 1.5),
                   hand_l=(0.22, -T - 0.04, 0.95), hand_r=(-0.22, -T - 0.04, 0.95), foot_l=(0.15, -T - 0.06, ANKLE),
                   foot_r=(-0.15, -T + 0.06, ANKLE))),
    ]
    return _key_all(rig, keys)


def _prone_start():
    """Face down, hands by the shoulders: where the slow get-up starts (and the injury beat ends)."""
    return _base(pelvis=(0, 0, 0.20), pelvis_rot=(90, 0, 0), spine=(0, 0, 0), look=(0, -2, 0.1),
                 hand_l=(0.22, -0.36, 0.11), hand_r=(-0.22, -0.36, 0.11), elbow_l=(1, 0.3, 0), elbow_r=(-1, 0.3, 0),
                 foot_l=(0.14, 0.82, 0.12), foot_r=(-0.14, 0.82, 0.12), foot_pitch_l=85, foot_pitch_r=85,
                 knee_l=(0.2, 0, -1), knee_r=(-0.2, 0, -1))


def injured_down(rig):
    """Hurt on the play (3 s): face down, the right knee drawn up, the right hand reaching back to clutch the back of
    the knee, head lifting and dropping, rocking with the pain, then settling flat to get up slowly."""
    s = _prone_start()
    hurt = dict(s, pelvis=(0.02, 0.02, 0.22), pelvis_rot=(86, 0, -8), spine=(-10, -14, -6), look=(0, -2, 0.6),
                hand_r=(-0.18, -0.02, 0.32), elbow_r=(-1, 0.4, 0.4), foot_r=(-0.16, 0.42, 0.40), foot_pitch_r=40,
                knee_r=(-0.2, -0.3, -1))
    rock = dict(hurt, pelvis=(0.05, 0.03, 0.24), pelvis_rot=(84, 0, -14), spine=(-16, -16, -10), look=(0.3, -2, 0.8),
                foot_r=(-0.18, 0.36, 0.46))
    keys = [(0, s), (12, hurt), (30, rock), (45, hurt), (62, rock), (76, hurt), (90, s)]
    return _key_all(rig, keys)


def getup_slow(rig):
    """Slow to get up (shaken up on the play): from face down onto one knee, a hand pressed to the right ribs, head
    hanging for a beat, then a careful rise with the hand still on the side."""
    keys = [
        (0, _prone_start()),
        (18, _base(pelvis=(0, 0.20, 0.44), pelvis_rot=(64, 0, 0), spine=(26, 0, 0), look=(0, -1.5, 0.2),
                   hand_l=(0.22, -0.16, 0.11), hand_r=(-0.22, -0.16, 0.11), foot_l=(0.16, 0.55, 0.12),
                   foot_r=(-0.16, 0.55, 0.12), foot_pitch_l=80, foot_pitch_r=80)),
        (32, _base(pelvis=(0, 0.20, 0.52), pelvis_rot=(36, 0, 6), spine=(30, 0, 8), look=(0, -1.0, 0.0),
                   hand_l=(0.22, -0.26, 0.58), hand_r=(-0.20, -0.04, 0.88), elbow_r=(-1, 0.5, 0.2),
                   foot_l=(0.18, -0.20, ANKLE), foot_r=(-0.16, 0.50, 0.12), foot_pitch_r=80)),
        (52, _base(pelvis=(0, 0.20, 0.54), pelvis_rot=(34, 0, 8), spine=(32, 0, 10), look=(0, -0.8, -0.2),
                   hand_l=(0.22, -0.24, 0.60), hand_r=(-0.20, -0.04, 0.88), elbow_r=(-1, 0.5, 0.2),
                   foot_l=(0.18, -0.20, ANKLE), foot_r=(-0.16, 0.50, 0.12), foot_pitch_r=80)),
        (70, _base(pelvis=(0, 0.08, 0.84), pelvis_rot=(18, 0, 4), spine=(10, 0, 6), look=(0, -4, 1.2),
                   hand_l=(0.24, -0.06, 0.92), hand_r=(-0.20, -0.02, 0.90), elbow_r=(-1, 0.5, 0.2),
                   foot_l=(0.16, -0.10, ANKLE), foot_r=(-0.16, 0.14, ANKLE))),
        (84, _base(pelvis=(0, 0.04, 0.93), pelvis_rot=(6, 0, 0), spine=(2, 0, 0), look=(0, -6, 1.6),
                   hand_l=(0.22, -0.04, 0.95), hand_r=(-0.20, -0.02, 0.92), foot_l=(0.15, -0.06, ANKLE),
                   foot_r=(-0.15, 0.06, ANKLE))),
    ]
    return _key_all(rig, keys)


def tackled_hit(rig):
    """The ball carrier's side of a tackle, 11 frames: running with the ball locked in, the hit jolts the torso and
    snaps the head, the hips drop and the knees buckle as he is driven forward and starts to go down (the ragdoll takes
    the fall from here). In place."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.90), pelvis_rot=(16, 0, 0), spine=(6, 0, 0), look=(0, -6, 1.4),
                  hand_l=(0.24, -0.12, 1.02), **_tuck(0.0), foot_l=(0.12, -0.22, ANKLE), foot_r=(-0.12, 0.22, 0.14),
                  foot_pitch_r=40)),
        (4, _base(pelvis=(0.04, -0.06, 0.86), pelvis_rot=(4, 8, 6), spine=(-12, 12, 10), look=(1.5, -3, 2.0),
                  hand_l=(0.36, -0.04, 1.16), elbow_l=(1, 0.2, -0.6), **_tuck(-0.06),
                  foot_l=(0.12, -0.22, ANKLE), foot_r=(-0.14, 0.06, 0.12), foot_pitch_l=20, foot_pitch_r=30)),
        (8, _base(pelvis=(0.06, -0.20, 0.76), pelvis_rot=(22, 10, 8), spine=(10, 8, 6), look=(0.5, -2, 0.6),
                  hand_l=(0.34, -0.40, 0.96), elbow_l=(1, 0.3, -0.3), **_tuck(-0.20),
                  foot_l=(0.12, -0.22, ANKLE), foot_r=(-0.12, -0.10, ANKLE), foot_pitch_l=35, foot_pitch_r=30,
                  knee_l=(0.2, -1, 0.3), knee_r=(-0.2, -1, 0.3))),
        (11, _base(pelvis=(0.06, -0.34, 0.66), pelvis_rot=(40, 10, 8), spine=(16, 6, 4), look=(0.2, -1.5, 0.2),
                   hand_l=(0.30, -0.66, 0.74), elbow_l=(1, 0.3, 0), **_tuck(-0.34),
                   foot_l=(0.12, -0.24, ANKLE), foot_r=(-0.12, -0.16, ANKLE), foot_pitch_l=50, foot_pitch_r=45,
                   knee_l=(0.2, -1, 0.5), knee_r=(-0.2, -1, 0.5))),
    ]
    return _key_all(rig, C._in_place(keys))


MORE.update({"Tackled_Hit": tackled_hit, "Injured_Down": injured_down})


# ======================================================================================= mirrored variety
class _MirrorRig:
    """Wraps a rig so every key a clip writes is mirrored left/right (clips.mirror): the other side's version of the
    same move (wrap from the other side, toss to the left, the other shoulder), not a copy of it."""
    def __init__(self, rig):
        self._rig = rig

    def key(self, f, k):
        self._rig.key(f, C.mirror(k))

    def __getattr__(self, name):
        return getattr(self._rig, name)


def _mirrored(fn):
    return lambda rig: fn(_MirrorRig(rig))


_TO_MIRROR = {
    "Tackle_Wrap": C.tackle_wrap, "Tackle_High": C.tackle_high, "Tackle_Low": C.tackle_low, "Tackle_Dive": C.tackle_dive,
    "Tackle_Shoulder": tackle_shoulder, "Tackle_Arm": tackle_arm, "Tackle_Assist": tackle_assist,
    "Tackled_Hit": tackled_hit, "Catch_High": catch_high, "Catch_Chest": catch_chest, "Catch_Low": catch_low,
    "Catch_OverShoulder": catch_over_shoulder, "Catch_Dive": catch_dive,
    "GetUp_FaceDown": C.getup_face_down, "GetUp_FaceUp": C.getup_face_up, "GetUp_Roll": getup_roll,
    "GetUp_Slow": getup_slow, "Injured_Down": injured_down, "Truck": truck, "Hurdle": hurdle,
    "QB_Pitch": qb_pitch, "QB_Handoff": C.qb_handoff,
    "Celebrate_Loop": C.celebrate, "Celebrate_Flex_Loop": celebrate_flex, "Celebrate_PointUp_Loop": celebrate_point_up,
    "Celebrate_Leap_Loop": celebrate_leap, "Celebrate_FirstDown_Loop": celebrate_first_down,
    "Celebrate_Shimmy_Loop": celebrate_shimmy,
}
MORE.update({name + "_M": _mirrored(fn) for name, fn in _TO_MIRROR.items()})
MORE.update({"StanceUnderCenter_Loop": stance_under_center, "QB_UnderCenterSnap": qb_under_center_snap,
             "QB_Pitch": qb_pitch, "Catch_Dive": catch_dive, "GetUp_Slow": getup_slow})
