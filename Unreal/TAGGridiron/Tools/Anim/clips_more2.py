"""TAG football clips, batch 4 (hand keyed, original): the throwing family, more catches, and collision reactions
(direction-aware tackled reactions, missed tackles, broken-tackle stumbles). Conventions as clips.py: facing -Y,
left +X, up +Z, meters, 30 fps, ankles at z 0.094, positive yaw turns left. Right-handed quarterback.

Throws keep the sim's timing contract: the clip starts THROW_START[name] seconds in, so its release frame
(THROW_RELEASE) comes ~0.23 s after the sim lets the ball go, like the original A_TAG_QB_Throw.
"""
import math

import clips as C
import clips_more as M

ANKLE = M.ANKLE
_base, _tuck, _run, _key_all, _rot = M._base, M._tuck, M._run, M._key_all, M._rot


# ================================================================================================ throws
def _throw_keys(stride=0.45, rz=1.80, slot=0.0, load_back=0.10, follow=1.0, fade=0.0, tscale=1.0, lean=0.0):
    """The right-handed throwing motion, parameterized from the set-feet pass (clips.qb_throw): set, load with the
    ball at the ear and the lead arm at the target, stride, plant with the hips opening ahead of the shoulders,
    release (high elbow, or a lower slot), follow through across the body, settle.
      stride: lead foot plant distance toward the target (negative = steps back, off the back foot)
      rz / slot: release height and arm slot (slot < 0 drops the arm out to the side)
      load_back: how far the ball goes back on the load (deep balls load longer)
      follow: follow-through length; fade: pelvis drifts back (fading away); tscale: tempo; lean: torso pitch extra"""
    s = stride / 0.45
    look = (0.0, -15.0, 1.7)

    def k(f, **kw):
        d = {"foot_l": (0.16, -0.10, ANKLE), "foot_r": (-0.20, 0.18, ANKLE), "foot_yaw_l": -25, "foot_yaw_r": -70,
             "knee_l": (0.3, -1, 0), "knee_r": (-0.6, -1, 0), "look": look}
        d.update(kw)
        return (int(round(f * tscale)), d)

    plant = (0.12, -stride, ANKLE)
    fy = lambda y: y * s + fade          # pelvis travel toward the target, minus the fade
    hz = lambda z: z + (rz - 1.80)
    keys = [
        k(0, pelvis=(0.0, 0.03, 0.90), pelvis_rot=(8, -45, 0), spine=(6, -20, 0),
          hand_r=(-0.12, -0.18, 1.22), elbow_r=(-1, 0.3, -1), hand_l=(-0.02, -0.24, 1.20), elbow_l=(1, -0.2, -1)),
        k(8, pelvis=(-0.02, fy(0.06), 0.88), pelvis_rot=(6, -55, 0), spine=(4, -35, 0), clav_r=(6, -4),
          hand_r=(-0.20, 0.06 + load_back - 0.10, 1.62), elbow_r=(-1, 0.4, 0.2), hand_l=(0.10, -0.42, 1.38),
          elbow_l=(1, 0, -0.5), foot_pitch_l=8),
        k(14, pelvis=(0.0, fy(0.0), 0.87), pelvis_rot=(6, -45, 0), spine=(4, -38, 0), clav_r=(8, -6),
          hand_r=(-0.22, 0.10 + load_back - 0.10, 1.65), elbow_r=(-1, 0.4, 0.25), hand_l=(0.12, -0.45, 1.36),
          elbow_l=(1, 0, -0.5), foot_l=(0.14, -stride * 0.62, 0.17), foot_yaw_l=-22),
        k(18, pelvis=(0.02, fy(-0.10), 0.86), pelvis_rot=(10 + lean * 0.5, -15, 0), spine=(6, -40, 0), clav_r=(10, -8),
          hand_r=(-0.24, 0.16 + load_back - 0.10, 1.66), elbow_r=(-1, 0.3, 0.3), hand_l=(0.20, -0.35, 1.25),
          elbow_l=(1, 0.2, -0.6), foot_l=plant, foot_yaw_l=-20),
        k(21, pelvis=(0.03, fy(-0.13), 0.855), pelvis_rot=(14 + lean, 5, 0), spine=(12 + lean, 0, -12 * slot),
          clav_r=(12, 4), hand_r=(-0.20 + slot * 0.25, -0.10, hz(1.80) + slot * 0.35), elbow_r=(-1, -0.2, 0.6 + slot),
          hand_l=(0.25, -0.10, 1.10), elbow_l=(1, 0.5, -0.4), foot_l=plant, foot_yaw_l=-20, foot_pitch_r=12),
        k(23, pelvis=(0.03, fy(-0.16), 0.85), pelvis_rot=(18 + lean, 20, 0), spine=(22 + lean, 20, -5 - 10 * slot),
          clav_r=(8, 14), hand_r=(-0.10 + slot * 0.2, -0.45 * min(1.0, follow), hz(1.56) + slot * 0.2),
          elbow_r=(-1, -0.5, 0.3), hand_l=(0.26, 0.0, 1.05), elbow_l=(1, 0.6, -0.2), foot_l=plant, foot_yaw_l=-20,
          foot_pitch_r=25),
        k(30, pelvis=(0.04, fy(-0.22), 0.83), pelvis_rot=(28 * follow + lean, 30, 0), spine=(28 * follow + lean, 25, 0),
          clav_r=(-2, 20), hand_r=(0.18 * follow, -0.38, 0.92 + (1 - follow) * 0.45), elbow_r=(-0.5, -0.3, 1.0),
          hand_l=(0.30, 0.05, 1.0), elbow_l=(1, 0.6, -0.4), foot_l=plant, foot_yaw_l=-20, foot_pitch_r=45, foot_yaw_r=-40),
        k(38, pelvis=(0.02, fy(-0.18), 0.88), pelvis_rot=(12, 15, 0), spine=(8, 10, 0),
          hand_r=(-0.22, -0.12, 1.0), elbow_r=(-1, 0.3, -1), hand_l=(0.24, -0.08, 1.0), elbow_l=(1, 0.3, -1),
          foot_l=plant, foot_yaw_l=-20, foot_r=(-0.12, -stride * 0.45 + 0.0, ANKLE), foot_yaw_r=-15),
        k(48, pelvis=(0.0, fy(-0.16), 0.90), pelvis_rot=(6, 5, 0), spine=(4, 0, 0),
          hand_r=(-0.25, -0.08, 1.0), elbow_r=(-1, 0.3, -1), hand_l=(0.25, -0.10, 1.0), elbow_l=(1, 0.3, -1),
          foot_l=plant, foot_yaw_l=-15, foot_r=(-0.12, -stride * 0.45, ANKLE), foot_yaw_r=-12),
    ]
    base_py = [0.03, 0.06, 0.0, -0.10, -0.13, -0.16, -0.22, -0.18, -0.16]
    for (f, d), b in zip(keys, base_py):
        dy = d["pelvis"][1] - b
        for h in ("hand_l", "hand_r"):
            x, y, z = d[h]
            d[h] = (x, y + dy, min(z, 1.80))  # never above the reach of the throwing arm
    return keys


def _throw(rig, **kw):
    keys = _throw_keys(**kw)
    for f, d in keys:
        rig.key(f, d)
    return keys[0][0], keys[-1][0]


def qb_throw_quick(rig):
    """Quick game (slant, hitch): no real stride, compact three-quarter release, fast tempo."""
    return _throw(rig, stride=0.22, rz=1.72, load_back=0.02, follow=0.7, tscale=0.76)


def qb_throw_deep(rig):
    """Deep ball: longer load, full stride, high release, the whole body follows through."""
    return _throw(rig, stride=0.52, rz=1.80, load_back=0.12, follow=1.0, tscale=1.12, lean=6)


def qb_throw_fade(rig):
    """Off the back foot with a rusher in his face: the front foot steps back, the hips drift away, arm only."""
    return _throw(rig, stride=-0.12, fade=0.22, rz=1.80, load_back=0.06, follow=0.6, lean=-4)


def qb_throw_sidearm(rig):
    """Sidearm around an arm in the lane: the throwing arm drops out to the side, the release comes at shoulder height."""
    return _throw(rig, stride=0.32, rz=1.80, slot=-1.0, load_back=0.06, follow=0.8)


def qb_throw_touch(rig):
    """Touch (lofted over the linebacker): high release, soft wrist, short follow through."""
    return _throw(rig, stride=0.38, rz=1.78, load_back=0.04, follow=0.6, tscale=1.05)


def _throw_on_run(rig, turn):
    """Throw on the run: the legs keep running (in place), the shoulders turn to the target and the arm comes through
    on the move. turn > 0: running to his left (the harder one for a righty: more torso turn); < 0: to his right."""
    def ov(f, t):
        py = -4.0 * t
        d = {"pelvis_rot": (14, turn * 30, 0), "look": (0.0, py - 15.0, 1.7)}
        if f <= 10:          # load: ball to the ear, lead arm out at the target
            u = f / 10.0
            d.update(spine=(4, -turn * 30 - 30 * u, 0), hand_r=(-0.16, py + 0.04 * u, 1.24 + 0.40 * u),
                     elbow_r=(-1, 0.4, 0.2), hand_l=(0.06 + 0.06 * u, py - 0.30 - 0.12 * u, 1.24 + 0.12 * u),
                     elbow_l=(1, 0, -0.5))
        elif f <= 16:        # through to the release
            u = (f - 10) / 6.0
            d.update(spine=(4 + 8 * u, -turn * 30 - 30 + 30 * u, 0), hand_r=(-0.18, py + 0.04 - 0.16 * u, 1.64 + 0.14 * u),
                     elbow_r=(-1, -0.2 * u, 0.6 * u), hand_l=(0.20, py - 0.30, 1.16), elbow_l=(1, 0.5, -0.4))
        else:                # follow through, then back into the carry
            u = min(1.0, (f - 16) / 10.0)
            d.update(spine=(12 + 10 * (1 - abs(2 * u - 1)), -turn * 30 + 20 * u, 0),
                     hand_r=(-0.18 + 0.30 * u, py - 0.12 - 0.20 * (1 - u), 1.78 - 0.70 * u), elbow_r=(-0.6, -0.3, 0.8),
                     hand_l=(0.22, py - 0.10, 1.02), elbow_l=(1, 0.5, -0.4))
        return d
    return _key_all(rig, _run(36, speed=4.0, cadence=4.4, pelvis_z=0.88, lean=12, arms=False, overrides=ov))


def qb_throw_run_r(rig):
    return _throw_on_run(rig, -1)


def qb_throw_run_l(rig):
    return _throw_on_run(rig, 1)


# release frame of each throw and where the clip starts (release - 7 frames), seconds
THROW_RELEASE = {"QB_Throw_Quick": 16, "QB_Throw_Deep": 23, "QB_Throw_Fade": 21, "QB_Throw_Sidearm": 21,
                 "QB_Throw_Touch": 22, "QB_Throw_RunR": 16, "QB_Throw_RunL": 16}


# =============================================================================================== catches
def catch_in_stride(rig):
    """The everyday catch at full speed: hands go out in front at the face (thumbs together), catch it away from the
    body, pull it in and tuck without breaking stride."""
    def ov(f, t):
        py = -7.0 * t
        if f < 5:
            return {}
        if f <= 10:
            u = (f - 5) / 5.0
            return {"hand_l": (0.06 + 0.10 * (1 - u), py - 0.10 - 0.36 * u, 1.10 + 0.30 * u),
                    "hand_r": (-0.06 - 0.10 * (1 - u), py - 0.10 - 0.36 * u, 1.10 + 0.30 * u),
                    "elbow_l": (1, 0.2, -0.6), "elbow_r": (-1, 0.2, -0.6), "look": (0.0, py - 2.0, 1.4)}
        if f <= 15:
            u = (f - 10) / 5.0
            return {"hand_l": (0.06, py - 0.46 + 0.26 * u, 1.40 - 0.16 * u), "hand_r": (-0.06, py - 0.46 + 0.28 * u, 1.40 - 0.14 * u),
                    "elbow_l": (1, 0.4, -0.5), "elbow_r": (-1, 0.4, -0.5), "look": (0.0, py - 3.0, 1.3)}
        d = {"hand_l": (0.16, py - 0.16, 1.06), "look": (0.0, py - 8.0, 1.5)}
        d.update(_tuck(py))
        return d
    return _key_all(rig, _run(24, speed=7.0, overrides=ov))


def catch_high_point(rig):
    """Contested jump ball: gather, explode up, both arms fully extended to catch it at its highest point, ball
    pulled down hard on the way down, land balanced."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.90), pelvis_rot=(14, 0, 0), spine=(4, 0, 0), look=(0, -3, 2.4),
                  hand_l=(0.22, -0.10, 1.00), hand_r=(-0.22, -0.10, 1.00), foot_l=(0.14, -0.10, ANKLE),
                  foot_r=(-0.14, 0.10, ANKLE))),
        (5, _base(pelvis=(0, 0, 0.76), pelvis_rot=(26, 0, 0), spine=(10, 0, 0), look=(0, -2, 2.4),
                  hand_l=(0.24, 0.10, 0.90), hand_r=(-0.24, 0.10, 0.90), foot_l=(0.15, -0.10, ANKLE),
                  foot_r=(-0.15, 0.10, ANKLE))),
        (11, _base(pelvis=(0, -0.05, 1.28), pelvis_rot=(-2, 0, 0), spine=(-10, 0, 0), look=(0, -1.5, 2.6),
                   hand_l=(0.06, -0.14, 2.14), hand_r=(-0.06, -0.14, 2.14), elbow_l=(1, 0, -0.2), elbow_r=(-1, 0, -0.2),
                   foot_l=(0.13, -0.05, 0.42), foot_r=(-0.13, 0.08, 0.30), foot_pitch_l=50, foot_pitch_r=50)),
        (15, _base(pelvis=(0, -0.06, 1.18), pelvis_rot=(4, 0, 0), spine=(-2, 0, 0), look=(0, -3, 1.6),
                   hand_l=(0.06, -0.20, 1.86), hand_r=(-0.08, -0.20, 1.88), elbow_l=(1, 0.3, -0.6), elbow_r=(-1, 0.3, -0.6),
                   foot_l=(0.13, -0.06, 0.28), foot_r=(-0.13, 0.08, 0.22), foot_pitch_l=40, foot_pitch_r=40)),
        (20, _base(pelvis=(0, -0.06, 0.82), pelvis_rot=(22, 0, 0), spine=(10, 0, 0), look=(0, -5, 1.2),
                   hand_l=(0.10, -0.20, 1.10), **_tuck(0.0), foot_l=(0.15, -0.08, ANKLE), foot_r=(-0.15, 0.10, ANKLE),
                   knee_l=(0.25, -1, 0.2), knee_r=(-0.25, -1, 0.2))),
        (28, _base(pelvis=(0, -0.06, 0.90), pelvis_rot=(14, 0, 0), spine=(6, 0, 0), look=(0, -8, 1.5),
                   hand_l=(0.16, -0.16, 1.04), **_tuck(0.0), foot_l=(0.15, -0.10, ANKLE), foot_r=(-0.15, 0.10, ANKLE))),
    ]
    return _key_all(rig, keys)


def catch_one_hand(rig):
    """One-handed stab at a ball thrown wide (to his left): lunge step, the left arm reaches out full length, the ball
    sticks to the palm and is pulled into the body, the right hand covers it."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.92), pelvis_rot=(12, 0, 0), spine=(4, 0, 0), look=(0.8, -4, 1.4),
                  hand_l=(0.22, -0.10, 1.00), hand_r=(-0.22, -0.10, 1.00), foot_l=(0.13, -0.14, ANKLE),
                  foot_r=(-0.13, 0.14, ANKLE), foot_pitch_r=20)),
        (6, _base(pelvis=(0.16, -0.10, 0.86), pelvis_rot=(16, 18, -14), spine=(6, 10, -18), look=(1.6, -2.5, 1.5),
                  hand_l=(0.60, -0.30, 1.42), elbow_l=(1, 0, -0.2), hand_r=(0.02, -0.20, 1.10),
                  foot_l=(0.40, -0.24, ANKLE), foot_r=(-0.08, 0.14, 0.12), foot_pitch_r=40)),
        (9, _base(pelvis=(0.20, -0.12, 0.84), pelvis_rot=(16, 20, -16), spine=(6, 12, -20), look=(1.6, -2.0, 1.4),
                  hand_l=(0.62, -0.26, 1.38), elbow_l=(1, 0.1, -0.2), hand_r=(0.04, -0.20, 1.12),
                  foot_l=(0.42, -0.24, ANKLE), foot_r=(-0.06, 0.12, 0.12), foot_pitch_r=40)),
        (14, _base(pelvis=(0.16, -0.10, 0.86), pelvis_rot=(14, 10, -6), spine=(8, 4, -6), look=(0.6, -4, 1.3),
                   hand_l=(0.24, -0.22, 1.20), hand_r=(0.08, -0.20, 1.18), elbow_l=(1, 0.4, -0.4),
                   foot_l=(0.40, -0.24, ANKLE), foot_r=(0.0, 0.10, ANKLE))),
        (22, _base(pelvis=(0.14, -0.10, 0.90), pelvis_rot=(12, 0, 0), spine=(6, 0, 0), look=(0, -8, 1.5),
                   hand_l=(0.30, -0.18, 1.06), **_tuck(0.0), foot_l=(0.30, -0.22, ANKLE), foot_r=(0.0, 0.10, ANKLE))),
    ]
    return _key_all(rig, C._in_place(keys))


def catch_back_shoulder(rig):
    """Back-shoulder fade: running a go route, he reads the ball thrown behind him to the outside (left) shoulder,
    turns his head and shoulders back, the hands come up and back to it, he plants, secures it and turns upfield."""
    def ov(f, t):
        py = -6.0 * t
        if f < 6:
            return {}
        if f <= 14:
            u = (f - 6) / 8.0
            return {"spine": (2, 50 * u, 0), "look": (2.0 * u, py + 3.0 * u, 1.6),
                    "hand_l": (0.30, py + 0.20 * u, 1.10 + 0.40 * u), "hand_r": (0.12, py + 0.16 * u, 1.10 + 0.42 * u),
                    "elbow_l": (1, 0.4, 0.2), "elbow_r": (0.2, 0.6, -0.6)}
        if f <= 20:
            u = (f - 14) / 6.0
            return {"spine": (4, 50 - 30 * u, 0), "look": (1.0, py, 1.3),
                    "hand_l": (0.24, py + 0.20 - 0.30 * u, 1.50 - 0.30 * u), "hand_r": (0.06, py + 0.16 - 0.30 * u, 1.52 - 0.30 * u)}
        d = {"spine": (6, 0, 0), "look": (0.0, py - 6.0, 1.5), "hand_l": (0.16, py - 0.16, 1.06)}
        d.update(_tuck(py))
        return d
    return _key_all(rig, _run(28, speed=6.0, overrides=ov))


def catch_comeback(rig):
    """Comeback route: break down from the stem, plant, come back toward the quarterback to meet the ball at the
    chest, catch, and spin back upfield to the outside. In place."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.90), pelvis_rot=(16, 0, 0), spine=(6, 0, 0), look=(0, -6, 1.4),
                  hand_l=(0.22, -0.10, 1.0), hand_r=(-0.22, -0.10, 1.0), foot_l=(0.12, -0.24, ANKLE),
                  foot_r=(-0.12, 0.20, 0.14), foot_pitch_r=40)),
        (6, _base(pelvis=(0, -0.20, 0.78), pelvis_rot=(6, 0, 0), spine=(-4, 0, 0), look=(0.0, 4, 1.4),
                  hand_l=(0.22, -0.20, 1.0), hand_r=(-0.22, -0.20, 1.0), foot_l=(0.14, -0.40, ANKLE),
                  foot_r=(-0.14, -0.10, ANKLE), foot_pitch_l=0, knee_l=(0.25, -1, 0.3), knee_r=(-0.25, -1, 0.3))),
        (12, _base(pelvis=(0, -0.10, 0.84), pelvis_rot=(10, 170, 0), spine=(6, 0, 0), look=(0.0, 6, 1.4),
                   hand_l=(-0.06, 0.24, 1.32), hand_r=(0.06, 0.24, 1.32), elbow_l=(-1, 0.2, -0.6), elbow_r=(1, 0.2, -0.6),
                   foot_l=(-0.10, -0.20, ANKLE), foot_r=(0.12, -0.02, ANKLE), foot_yaw_l=170, foot_yaw_r=170)),
        (16, _base(pelvis=(0, -0.04, 0.86), pelvis_rot=(12, 175, 0), spine=(8, 0, 0), look=(0.0, 6, 1.3),
                   hand_l=(-0.06, 0.18, 1.24), hand_r=(0.06, 0.20, 1.24), foot_l=(-0.10, -0.16, ANKLE),
                   foot_r=(0.12, 0.04, ANKLE), foot_yaw_l=170, foot_yaw_r=170)),
        (24, _base(pelvis=(0, -0.04, 0.88), pelvis_rot=(14, 280, 0), spine=(6, 0, 0), look=(-6.0, 0, 1.4),
                   hand_l=(-0.06, 0.16, 1.06), hand_r=(-0.14, -0.10, 1.18), elbow_r=(0, -0.8, -0.2),
                   foot_l=(0.02, -0.18, ANKLE), foot_r=(-0.02, 0.06, 0.12), foot_yaw_l=280, foot_yaw_r=280)),
        (30, _base(pelvis=(0, -0.04, 0.90), pelvis_rot=(16, 360, 0), spine=(6, 0, 0), look=(0, -8, 1.5),
                   hand_l=(0.16, -0.16, 1.04), **_tuck(-0.04), foot_l=(0.13, -0.22, ANKLE), foot_r=(-0.13, 0.12, 0.12),
                   foot_yaw_l=360, foot_yaw_r=360, foot_pitch_r=30)),
    ]
    return _key_all(rig, keys)


def catch_scoop(rig):
    """Low ball at speed: he sinks his hips in stride, pinkies together, scoops it off his shoe tops, and comes up
    running with it tucked."""
    def ov(f, t):
        py = -6.5 * t
        if f < 4:
            return {}
        if f <= 12:
            u = math.sin(math.pi * (f - 4) / 8.0)
            return {"pelvis_rot": (16 + 34 * u, 0, 0), "spine": (6 + 22 * u, 0, 0), "look": (0, py - 1.5, 0.3),
                    "hand_l": (0.07, py - 0.30 - 0.20 * u, 1.00 - 0.40 * u), "hand_r": (-0.07, py - 0.30 - 0.20 * u, 1.00 - 0.40 * u),
                    "elbow_l": (1, 0.3, 0), "elbow_r": (-1, 0.3, 0)}
        d = {"look": (0.0, py - 8.0, 1.5), "hand_l": (0.16, py - 0.16, 1.04)}
        d.update(_tuck(py))
        return d
    return _key_all(rig, _run(22, speed=6.5, pelvis_z=0.86, overrides=ov))


# ============================================================================================ collisions
def tackled_from_behind(rig):
    """Hit from behind: the hips are driven forward out from under him, the chest pitches toward the turf, the free
    arm reaches out to break the fall, the ball stays locked in. In place; the ragdoll takes the landing."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.90), pelvis_rot=(16, 0, 0), spine=(6, 0, 0), look=(0, -6, 1.4),
                  hand_l=(0.24, -0.12, 1.02), **_tuck(0.0), foot_l=(0.12, -0.22, ANKLE), foot_r=(-0.12, 0.22, 0.14),
                  foot_pitch_r=40)),
        (4, _base(pelvis=(0, -0.12, 0.88), pelvis_rot=(30, 0, 0), spine=(18, 0, 0), look=(0, -3, 0.8),
                  hand_l=(0.30, -0.36, 1.00), **_tuck(-0.12), foot_l=(0.12, -0.22, ANKLE), foot_r=(-0.12, 0.10, 0.20),
                  foot_pitch_r=60)),
        (8, _base(pelvis=(0, -0.34, 0.76), pelvis_rot=(52, 0, 0), spine=(22, 0, 0), look=(0, -2, 0.2),
                  hand_l=(0.28, -0.80, 0.70), elbow_l=(1, 0, -0.3), **_tuck(-0.34),
                  foot_l=(0.12, -0.24, 0.12), foot_r=(-0.12, 0.04, 0.26), foot_pitch_l=50, foot_pitch_r=70)),
        (11, _base(pelvis=(0, -0.52, 0.62), pelvis_rot=(66, 0, 0), spine=(22, 0, 0), look=(0, -1.5, 0.0),
                   hand_l=(0.26, -1.00, 0.36), elbow_l=(1, 0, -0.3), **_tuck(-0.52),
                   foot_l=(0.12, -0.20, 0.20), foot_r=(-0.12, 0.0, 0.34), foot_pitch_l=60, foot_pitch_r=75)),
    ]
    return _key_all(rig, C._in_place(keys))


def _tuck_at(px, py):
    return {"hand_r": (px - 0.12, py - 0.16, 1.18), "elbow_r": (-1, 0.8, -0.2)}


def tackled_from_side(rig):
    """Hit from his right: the hips are knocked out to his left, the upper body whips back toward the hit, the legs
    cross and he starts to go down sideways, ball protected. In place."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.90), pelvis_rot=(16, 0, 0), spine=(6, 0, 0), look=(0, -6, 1.4),
                  hand_l=(0.24, -0.12, 1.02), **_tuck(0.0), foot_l=(0.12, -0.22, ANKLE), foot_r=(-0.12, 0.22, 0.14),
                  foot_pitch_r=40)),
        (4, _base(pelvis=(0.14, -0.04, 0.86), pelvis_rot=(12, 12, 16), spine=(2, -10, -16), look=(-1.5, -3, 1.6),
                  hand_l=(0.40, -0.04, 1.20), elbow_l=(1, 0.2, -0.5), **_tuck_at(0.14, -0.04),
                  foot_l=(0.12, -0.22, ANKLE), foot_r=(-0.06, 0.12, 0.14), foot_pitch_r=30)),
        (8, _base(pelvis=(0.30, -0.08, 0.74), pelvis_rot=(18, 20, 34), spine=(4, -14, -20), look=(-1.0, -2, 1.0),
                  hand_l=(0.62, -0.10, 0.90), elbow_l=(1, 0.2, -0.4), **_tuck_at(0.30, -0.08),
                  foot_l=(0.20, -0.20, ANKLE), foot_r=(0.08, 0.04, 0.16), foot_pitch_r=40)),
        (11, _base(pelvis=(0.44, -0.10, 0.60), pelvis_rot=(22, 24, 52), spine=(6, -16, -22), look=(-0.5, -1.5, 0.5),
                   hand_l=(0.80, -0.14, 0.50), elbow_l=(1, 0.2, -0.3), **_tuck_at(0.44, -0.10),
                   foot_l=(0.24, -0.18, 0.12), foot_r=(0.16, 0.0, 0.24), foot_pitch_l=40, foot_pitch_r=50)),
    ]
    return _key_all(rig, C._in_place(keys))


def tackle_miss(rig):
    """Whiffed tackle: the defender lunges, his arms close on air as the carrier slips by to his left, he stumbles
    onto a hand and a knee, then pushes back up and turns to chase. In place."""
    keys = [
        (0, _base(pelvis=(0, 0, 0.90), pelvis_rot=(22, 0, 0), spine=(8, 0, 0), look=(0, -4, 1.0),
                  hand_l=(0.24, -0.20, 1.02), hand_r=(-0.24, -0.02, 1.0),
                  foot_l=(0.14, -0.20, ANKLE), foot_r=(-0.14, 0.22, ANKLE), foot_pitch_r=25)),
        (6, _base(pelvis=(0, -0.36, 0.80), pelvis_rot=(50, 0, 0), spine=(10, 0, 0), look=(0.4, -3, 0.8),
                  hand_l=(0.30, -0.86, 1.00), hand_r=(-0.30, -0.86, 1.00), elbow_l=(1, 0, -0.3), elbow_r=(-1, 0, -0.3),
                  foot_l=(0.14, -0.40, ANKLE), foot_r=(-0.14, 0.0, 0.14), foot_pitch_r=55)),
        (11, _base(pelvis=(0.06, -0.62, 0.66), pelvis_rot=(64, 0, 6), spine=(14, 0, 0), look=(1.5, -2, 0.6),
                   hand_l=(0.10, -1.00, 0.70), hand_r=(-0.06, -1.00, 0.72), elbow_l=(1, 0.3, 0.2), elbow_r=(-1, 0.3, 0.2),
                   foot_l=(0.14, -0.50, ANKLE), foot_r=(-0.14, -0.20, 0.16), foot_pitch_r=60)),
        (17, _base(pelvis=(0.04, -0.70, 0.52), pelvis_rot=(60, 0, 8), spine=(20, 0, 0), look=(2.0, -1, 0.4),
                   hand_l=(0.24, -1.02, 0.11), hand_r=(-0.10, -0.90, 0.40), foot_l=(0.14, -0.56, ANKLE),
                   foot_r=(-0.14, -0.30, 0.12), foot_pitch_r=80, knee_r=(-0.2, -1, -0.4))),
        (26, _base(pelvis=(0.04, -0.66, 0.78), pelvis_rot=(30, 20, 0), spine=(10, 14, 0), look=(4.0, 0, 1.2),
                   hand_l=(0.30, -0.80, 0.90), hand_r=(-0.20, -0.70, 0.92), foot_l=(0.14, -0.62, ANKLE),
                   foot_r=(-0.14, -0.40, ANKLE), foot_yaw_l=20, foot_yaw_r=20)),
        (32, _base(pelvis=(0.04, -0.66, 0.90), pelvis_rot=(18, 40, 0), spine=(6, 20, 0), look=(5.0, 1.0, 1.4),
                   hand_l=(0.26, -0.60, 1.0), hand_r=(-0.18, -0.70, 1.0), foot_l=(0.18, -0.62, ANKLE),
                   foot_r=(-0.10, -0.46, 0.12), foot_yaw_l=40, foot_yaw_r=40, foot_pitch_r=30)),
    ]
    return _key_all(rig, C._in_place(keys))


def broken_stumble(rig):
    """Bounces off a tackle: contact on his right knocks him sideways in stride, the free hand drops to touch the turf
    to keep him up, the ball stays tucked, he regains the stride and runs on."""
    def ov(f, t):
        py = -5.5 * t
        d = dict(_tuck(py))
        if f <= 14:
            u = math.sin(math.pi * f / 14.0)
            d.update({"pelvis": (0.10 * u, py, 0.86 - 0.18 * u), "pelvis_rot": (16 + 30 * u, 0, 18 * u),
                      "spine": (6 + 18 * u, 0, 14 * u),
                      "hand_l": (0.30 + 0.18 * u, py - 0.24 - 0.12 * u, 1.00 - 0.46 * u), "elbow_l": (1, 0.2, -0.3),
                      "look": (0.4, py - 3.0, 1.0 - 0.6 * u)})
        else:
            d.update({"hand_l": (0.24, py - 0.12, 1.02), "look": (0.0, py - 6.0, 1.4)})
        return d
    return _key_all(rig, _run(26, speed=5.5, pelvis_z=0.86, overrides=ov))


MORE2 = {
    "QB_Throw_Quick": qb_throw_quick, "QB_Throw_Deep": qb_throw_deep, "QB_Throw_Fade": qb_throw_fade,
    "QB_Throw_Sidearm": qb_throw_sidearm, "QB_Throw_Touch": qb_throw_touch, "QB_Throw_RunR": qb_throw_run_r,
    "QB_Throw_RunL": qb_throw_run_l,
    "Catch_InStride": catch_in_stride, "Catch_HighPoint": catch_high_point, "Catch_OneHand": catch_one_hand,
    "Catch_BackShoulder": catch_back_shoulder, "Catch_Comeback": catch_comeback, "Catch_Scoop": catch_scoop,
    "Tackled_FromBehind": tackled_from_behind, "Tackled_FromSide": tackled_from_side, "Tackle_Miss": tackle_miss,
    "Broken_Stumble": broken_stumble,
}
# the other side's version of the asymmetric ones
for _n in ("Catch_OneHand", "Catch_BackShoulder", "Catch_Comeback", "Tackled_FromSide", "Tackle_Miss", "Broken_Stumble"):
    MORE2[_n + "_M"] = M._mirrored(MORE2[_n])
