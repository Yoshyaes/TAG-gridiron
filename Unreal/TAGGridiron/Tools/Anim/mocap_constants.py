"""The game constants the mocap clips define, read from the built jsons and printed as the C++ lines to paste:
  Render/Athlete.cpp   Release[] (frame of each throw's release = the right hand's peak speed)
  Render/Athlete.h     ThrowKeyedStart (7 frames before the set throw's release), Kick*Contact (the kicking foot's
                       peak speed), CatchDiveTravel (the dive's pelvis travel), Drop*Speed (trajectory speed while
                       the drop moves)
  Game/MatchDirector.cpp  GPairs (see mocap_pairs.py)
    python Tools/Anim/mocap_constants.py [clips_dir]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
CLIPS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PROJ, "RawArt", "Anim", "Clips")
FPS = 30.0


def load(name):
    p = os.path.join(CLIPS, "A_TAG_%s.json" % name)
    return json.load(open(p)) if os.path.exists(p) else None


def main():
    throws = ["QB_Throw", "QB_Throw_Quick", "QB_Throw_Deep", "QB_Throw_Fade", "QB_Throw_Sidearm", "QB_Throw_Touch", "QB_Throw_RunR", "QB_Throw_RunL"]
    rel = []
    for t in throws:
        r = load(t)
        rel.append(r["events"]["hand_r_peak_speed"]["frame"] if r else -1)
    print("// Render/Athlete.cpp")
    print("const int32 Release[ThrowCount] = {%s}; // Tools/Anim/mocap_constants.py: each clip's right-hand peak speed" % ", ".join(str(v) for v in rel))
    print("// Render/Athlete.h")
    print("static constexpr float ThrowKeyedStart = %d.f / 30.f; // 7 frames before the set throw's release (frame %d)" % (max(0, rel[0] - 7), rel[0]))
    kicks = {"Kick_Kickoff": "KickoffContact", "Kick_FieldGoal": "FieldGoalContact", "Kick_Punt": "PuntContact"}
    parts = []
    for k, name in kicks.items():
        r = load(k)
        if r:
            parts.append("%s = %d.f / 30.f" % (name, r["events"]["foot_r_peak_speed"]["frame"]))
    print("static constexpr float %s;" % ", ".join(parts))
    d = load("Catch_Dive")
    if d:
        print("static constexpr float CatchDiveTravel = %.0f.f; // cm the dive carries the body forward" % (-d["pelvis_travel_xy_m"][1] * 100.0))
    sp = []
    for k, name in (("QB_Drop_Shotgun", "DropShotgunSpeed"), ("QB_Drop_UnderCenter", "DropUnderSpeed")):
        r = load(k)
        if r and r["traj_moving_frames"]:
            v = r["traj_travel_m"] / (r["traj_moving_frames"] / FPS) * 100.0
            sp.append("%s = %.0f.f" % (name, v))
            print("// %s: %.2f m over %d moving frames (%.2f m/s), %.2f s clip" % (k, r["traj_travel_m"], r["traj_moving_frames"], v / 100.0, r["seconds"]))
    print("static constexpr float %s; // cm/s the drops were captured at" % ", ".join(sp))
    print("// Tools/Anim/check_timing.py SPEEDS: travel m, moving frames")
    for k in ("QB_Drop_Shotgun", "QB_Drop_UnderCenter"):
        r = load(k)
        if r:
            print("  %s: (%.2f, %d)" % (k, r["traj_travel_m"], r["traj_moving_frames"]))


main()
