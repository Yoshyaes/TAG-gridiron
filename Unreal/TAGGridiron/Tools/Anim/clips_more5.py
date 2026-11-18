"""TAG football clips, batch 6 (hand keyed, original choreography from coaching technique): the quarterback's drop and
pocket, and the defender's breakdown. Conventions as clips.py: facing -Y, left +X, up +Z, meters, 30 fps, ankles at
z 0.094, positive yaw turns left. Right-handed quarterback.

Real timing these are built to (broadcast footage, coaching manuals):
  shotgun three step drop   ~1.8 m in 0.65 s (2.8 m/s), hitch up, ball at the chest in two hands the whole way
  under center five step    ~6 m in 1.5 s (4 m/s): open the right hip, big first step, crossover, big, crossover,
                            plant on the fifth and hitch up square to the target; eyes downfield over the front shoulder
  pocket                    on the toes, a bounce every ~0.6 s, ball at the chest, the eyes scanning left to right
  breakdown                 hips sunk, chest up, arms out and bent, choppy feet (4 steps/s), eyes on the carrier's belt
"""
import math

import clips as C
import clips_more as M

ANKLE = M.ANKLE
_base, _key_all = M._base, M._key_all

# ball at the chest in two hands (right on top, laces), for a pelvis at y py
def _chest_ball(py, yaw=0.0):
    # the hands sit ~22 cm ahead of the pelvis along the body's facing (yaw turns the facing)
    fx, fy = M._rot(0.0, -0.22, yaw)
    lx, ly = M._rot(0.06, 0.0, yaw)
    return {"hand_l": (fx + lx, py + fy + ly, 1.18), "hand_r": (fx - lx * 1.3, py + fy - ly * 1.3, 1.22),
            "elbow_l": (1, 0.4, -0.8), "elbow_r": (-1, 0.4, -0.8)}


def _look_down(py):
    return (0.0, py - 15.0, 1.6)


# ==================================================================================== the quarterback's drop
def qb_drop_shotgun(rig):
    """Shotgun three step drop, 26 frames, 1.8 m: right, left, right back, gather the left foot and hitch. Shoulders
    stay nearly square to the line, ball in two hands at the chest, eyes downfield. In place."""
    def k(f, py, pz, yaw, fl, fr, pl=0.0, pr=0.0, yl=4, yr=-10, pitch=12, spine_yaw=0.0):
        d = _base(pelvis=(0.0, py, pz), pelvis_rot=(pitch, yaw, 0), spine=(4, spine_yaw, 0), look=_look_down(py),
                  foot_l=fl, foot_r=fr, foot_pitch_l=pl, foot_pitch_r=pr, foot_yaw_l=yl, foot_yaw_r=yr,
                  knee_l=(0.3, -1, 0), knee_r=(-0.3, -1, 0))
        d.update(_chest_ball(py, yaw * 0.5))
        return (f, d)
    keys = [
        k(0, 0.10, 0.86, -10, (0.18, -0.02, ANKLE), (-0.20, 0.20, 0.11), pl=0, pr=20),
        k(4, 0.38, 0.84, -14, (0.18, -0.02, ANKLE), (-0.22, 0.62, ANKLE), pl=28, pr=0),          # right plants
        k(7, 0.62, 0.85, -16, (0.16, 0.56, ANKLE + 0.14), (-0.22, 0.62, ANKLE), pl=10, pr=0),       # left swings
        k(10, 0.84, 0.84, -14, (0.16, 1.05, ANKLE), (-0.22, 0.62, ANKLE), pl=0, pr=30),             # left plants
        k(13, 1.10, 0.85, -14, (0.16, 1.05, ANKLE), (-0.22, 1.10, ANKLE + 0.14), pl=0, pr=10),      # right swings
        k(16, 1.32, 0.84, -16, (0.16, 1.05, ANKLE), (-0.22, 1.55, ANKLE), pl=30, pr=0),             # right plants
        k(20, 1.56, 0.85, -18, (0.18, 1.45, ANKLE), (-0.22, 1.55, ANKLE), pl=6, pr=0, pitch=10),    # gather, weight back
        k(26, 1.64, 0.87, -30, (0.16, 1.50, ANKLE), (-0.20, 1.78, ANKLE), pl=10, pr=8, yl=-20, yr=-60,
          pitch=8, spine_yaw=-12),                                                                 # hitched up: the pocket
    ]
    return _key_all(rig, C._in_place(keys))


def qb_drop_under_center(rig):
    """Under center five step drop, 54 frames, 6.0 m: open the right hip and reach back with the right foot, cross the
    left over, reach, cross, plant hard on the fifth step, then hitch up into the throwing stance. The chest is turned
    toward the right sideline through the drop (shoulders a little less than the hips), the head stays downfield over
    the left shoulder, the ball rides at the chest in two hands. In place."""
    def k(f, py, pz, yaw, fl, fr, pl=0.0, pr=0.0, yl=-55, yr=-60, pitch=10, spine_yaw=14):
        d = _base(pelvis=(0.0, py, pz), pelvis_rot=(pitch, yaw, 0), spine=(4, spine_yaw, 0), look=_look_down(py),
                  foot_l=fl, foot_r=fr, foot_pitch_l=pl, foot_pitch_r=pr, foot_yaw_l=yl, foot_yaw_r=yr,
                  knee_l=(0.3, -0.8, 0), knee_r=(-0.3, -0.8, 0))
        d.update(_chest_ball(py, yaw * 0.7))
        return (f, d)
    keys = [
        k(0, 0.08, 0.86, -20, (0.17, -0.02, ANKLE), (-0.17, 0.02, ANKLE), yl=6, yr=-6, spine_yaw=0),
        k(5, 0.35, 0.85, -55, (0.17, -0.02, ANKLE), (-0.08, 0.90, ANKLE + 0.25), pl=25, pr=0, yl=-20, spine_yaw=10),
        k(9, 0.75, 0.84, -65, (0.14, 0.30, ANKLE + 0.10), (-0.08, 1.15, ANKLE), pl=40, pr=0, yl=-40),       # 1 plants, left toe off
        k(14, 1.35, 0.86, -70, (0.10, 1.90, ANKLE + 0.20), (-0.08, 1.15, ANKLE), pl=5, pr=30),             # 2 crosses
        k(18, 1.95, 0.84, -70, (0.10, 2.35, ANKLE), (-0.08, 1.50, ANKLE + 0.10), pl=0, pr=45),             # 2 plants, right trails
        k(23, 2.60, 0.86, -70, (0.10, 2.35, ANKLE), (-0.08, 3.10, ANKLE + 0.22), pl=35, pr=5),             # 3 reaches
        k(27, 3.15, 0.84, -70, (0.10, 2.70, ANKLE + 0.10), (-0.08, 3.55, ANKLE), pl=45, pr=0),             # 3 plants
        k(32, 3.80, 0.86, -70, (0.10, 4.30, ANKLE + 0.20), (-0.08, 3.55, ANKLE), pl=5, pr=30),             # 4 crosses
        k(36, 4.35, 0.84, -70, (0.10, 4.75, ANKLE), (-0.08, 3.90, ANKLE + 0.10), pl=0, pr=45),             # 4 plants
        k(41, 5.00, 0.86, -60, (0.10, 4.75, ANKLE), (-0.10, 5.50, ANKLE + 0.20), pl=35, pr=5, spine_yaw=10),  # 5 reaches
        k(45, 5.55, 0.82, -45, (0.10, 5.10, ANKLE + 0.08), (-0.12, 5.95, ANKLE), pl=40, pr=0, yl=-35, yr=-55,
          pitch=14, spine_yaw=0),                                                                          # 5 plants (brake)
        k(50, 5.85, 0.86, -42, (0.16, 5.65, ANKLE), (-0.14, 5.98, ANKLE), pl=8, pr=6, yl=-25, yr=-65, pitch=10,
          spine_yaw=-14),                                                                                  # hitch up
        k(54, 5.93, 0.88, -45, (0.16, 5.83, ANKLE), (-0.20, 6.11, ANKLE), pl=4, pr=4, yl=-25, yr=-70, pitch=8,
          spine_yaw=-20),                                                                                  # the throwing stance
    ]
    return _key_all(rig, C._in_place(keys))


def qb_pocket_loop(rig):
    """In the pocket, 36 frames: the throwing stance (front shoulder at the target), bouncing on the toes twice, ball at
    the chest in two hands, the eyes scanning across the field. Starts and ends on the set throw's first pose, so the
    throw cuts in from it."""
    def k(f, dz, dy, toe, lx, bob):
        py = 0.03 + dy
        d = _base(pelvis=(0.0, py, 0.88 + dz), pelvis_rot=(8 + bob, -45, 0), spine=(6, -20, 0), look=(lx, -15.0, 1.6),
                  foot_l=(0.16, -0.10, ANKLE), foot_r=(-0.20, 0.18, ANKLE), foot_yaw_l=-25, foot_yaw_r=-70,
                  foot_pitch_l=toe, foot_pitch_r=toe + 6, knee_l=(0.3, -1, 0), knee_r=(-0.6, -1, 0),
                  hand_r=(-0.12, py - 0.21, 1.22), elbow_r=(-1, 0.3, -1), hand_l=(-0.02, py - 0.27, 1.20), elbow_l=(1, -0.2, -1))
        return (f, d)
    keys = [
        k(0, 0.0, 0.0, 10, 0.0, 0),
        k(5, 0.025, -0.02, 26, -0.8, -2),    # up on the toes
        k(9, 0.0, 0.0, 10, -1.6, 0),
        k(14, 0.025, 0.02, 26, -1.2, -2),
        k(18, 0.0, 0.0, 10, 0.4, 0),
        k(23, 0.025, -0.02, 26, 1.6, -2),    # scanning to the right
        k(27, 0.0, 0.0, 10, 1.2, 0),
        k(32, 0.025, 0.02, 26, 0.3, -2),
        k(36, 0.0, 0.0, 10, 0.0, 0),
    ]
    return _key_all(rig, keys)


# ===================================================================================== the defender's breakdown
def def_breakdown_loop(rig):
    """Breakdown in space, 24 frames: hips sunk, chest up over the knees, arms out and bent ready to wrap, choppy feet
    (4 steps/s, 6 cm lifts), eyes on the carrier's belt buckle. In place."""
    def k(f, lift_l, lift_r, dx):
        d = _base(pelvis=(dx, 0.02, 0.74 + 0.01 * (lift_l + lift_r) / 0.06), pelvis_rot=(34, 0, 0), spine=(4, 0, 0),
                  look=(0.0, -4.0, 0.9),
                  hand_l=(0.36, -0.22, 0.84), hand_r=(-0.36, -0.22, 0.84), elbow_l=(1, 0.3, -0.4), elbow_r=(-1, 0.3, -0.4),
                  foot_l=(0.24, -0.04, ANKLE + lift_l), foot_r=(-0.24, 0.04, ANKLE + lift_r),
                  foot_pitch_l=10 + 300 * lift_l, foot_pitch_r=10 + 300 * lift_r, foot_yaw_l=6, foot_yaw_r=-6,
                  knee_l=(0.3, -1, 0), knee_r=(-0.3, -1, 0))
        return (f, d)
    keys = [
        k(0, 0.0, 0.0, 0.0),
        k(3, 0.06, 0.0, -0.02),
        k(6, 0.0, 0.0, 0.0),
        k(9, 0.0, 0.06, 0.02),
        k(12, 0.0, 0.0, 0.0),
        k(15, 0.06, 0.0, -0.02),
        k(18, 0.0, 0.0, 0.0),
        k(21, 0.0, 0.06, 0.02),
        k(24, 0.0, 0.0, 0.0),
    ]
    return _key_all(rig, keys)


MORE5 = {
    "QB_Drop_Shotgun": qb_drop_shotgun, "QB_Drop_UnderCenter": qb_drop_under_center, "QB_Pocket_Loop": qb_pocket_loop,
    "Def_Breakdown_Loop": def_breakdown_loop,
}
