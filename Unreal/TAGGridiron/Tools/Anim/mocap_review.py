"""One line per built mocap clip, with the numbers that catch a bad pick: duration, hip and head heights, where the
right hand starts, planted-foot slide, trajectory travel in the source and the detected events.

    python Tools/Anim/mocap_review.py [clips_dir]
Flags: HEAD (head below the hips + 0.25 m on a standing clip), LOW (hips under 0.35 m outside the get-ups / dives /
injury), SLIDE (planted foot steps over 12 cm per frame), SHORT/LONG against the Tools/Anim/check_timing.py families.
"""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
CLIPS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PROJ, "RawArt", "Anim", "Clips")
GROUND_OK = re.compile(r"GetUp|Injured|Catch_Dive|PT_|Tackle_Dive|Tackle_Low|Kneel|Celebrate|Idle_HandsKnees")

# duration limits from check_timing.REAL (kept in sync by hand)
REAL = [
    (r"^QB_Throw", (0.9, 2.0)), (r"^QB_Drop_Shotgun", (0.6, 0.95)), (r"^QB_Drop_UnderCenter", (1.3, 1.9)),
    (r"^QB_Pocket_Loop", (0.8, 3.0)), (r"^QB_Shotgun_Loop", (2.0, 4.0)), (r"^Celebrate_Loop", (1.2, 5.0)),
    (r"^QB_(SnapCatch|UnderCenterSnap)", (0.3, 0.6)), (r"^QB_(Handoff|Pitch)", (0.5, 1.2)), (r"^QB_Kneel", (0.8, 2.5)),
    (r"^PT_.*_[TC](_M)?$", (0.9, 1.8)), (r"^PT_PileOn", (0.5, 1.2)),
    (r"^Tackle_(Wrap|High|Low|Shoulder|Arm|Assist)", (0.5, 1.3)), (r"^Tackle_Dive", (0.6, 1.4)), (r"^Tackle_Miss", (0.6, 1.4)),
    (r"^Tackled_", (0.3, 1.2)), (r"^Broken_Stumble", (0.5, 1.2)), (r"^GetUp_(FaceDown|FaceUp|Roll)", (1.2, 3.0)),
    (r"^GetUp_Slow", (2.0, 5.0)), (r"^Injured_Down", (1.5, 6.0)), (r"^Catch_Dive", (1.4, 2.4)), (r"^Catch_", (0.5, 1.3)),
    (r"^(Juke|Spin)_", (0.4, 1.1)), (r"^StiffArm_", (0.6, 1.4)), (r"^(Truck|Hurdle)", (0.6, 1.4)), (r"^Kick_", (1.0, 2.2)),
    (r"^Lineman_GetOff", (0.3, 0.7)), (r"^Block_Punch", (0.25, 0.6)), (r"^(Block|Rush)_.*_Loop", (0.8, 2.0)),
    (r"^Block_(Beaten|Finish)", (0.5, 1.2)), (r"^(Rush|Shed)_", (0.4, 1.3)), (r"^Def_Breakdown_Loop", (0.6, 1.2)),
    (r"^Stance.*_Loop", (2.0, 4.0)), (r"^Idle_.*_Loop", (2.0, 4.0)), (r"^Celebrate_.*_Loop", (1.2, 5.0)),
]


def main():
    rows = []
    for path in sorted(glob.glob(os.path.join(CLIPS, "A_TAG_*.json"))):
        r = json.load(open(path))
        if "source" not in r:
            continue  # a hand keyed clip
        name = r["clip"]
        flags = []
        pz, hz = r["pelvis_z"], r["head_z"]
        if hz["start"] < pz["start"] + 0.25 and not GROUND_OK.search(name):
            flags.append("HEAD")
        if pz["min"] < 0.35 and not GROUND_OK.search(name):
            flags.append("LOW")
        sl = r["max_planted_foot_step_m"]
        if max(sl["l"], sl["r"]) > 0.12:
            flags.append("SLIDE")
        fam = next((lim for pat, lim in REAL if re.search(pat, name)), None)
        if fam and not name.endswith("_M"):
            if r["seconds"] < fam[0]:
                flags.append("SHORT<%.1f" % fam[0])
            elif r["seconds"] > fam[1]:
                flags.append("LONG>%.1f" % fam[1])
        ev = r["events"]
        rows.append("%-26s %5.2fs hips %.2f/%.2f head %.2f hand_r(%.2f,%.2f,%.2f) slide %.2f/%.2f traj %.1fm mv%d  hand_r peak f%d %.1fm/s foot_r f%d %.1fm/s  %s %s" % (
            name, r["seconds"], pz["start"], pz["min"], hz["start"], *r["hand_r"], sl["l"], sl["r"], r["traj_travel_m"], r["traj_moving_frames"],
            ev["hand_r_peak_speed"]["frame"], ev["hand_r_peak_speed"]["m_s"], ev["foot_r_peak_speed"]["frame"], ev["foot_r_peak_speed"]["m_s"],
            r["source"]["src"][:40], " ".join(flags)))
    print("\n".join(rows))
    print("%d clips" % len(rows))


main()
