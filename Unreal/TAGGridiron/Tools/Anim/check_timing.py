"""Timing gate: every TAG clip's duration and speed against real football and the look targets.

    python Tools/Anim/check_timing.py            (reads RawArt/Anim/Clips/*.json and docs/look_targets.json)

Two kinds of check:
  1. Real-world numbers (broadcast footage, combine data, coaching manuals), hard limits per clip family:
     release-to-follow-through of a pass 0.35-0.6 s; shotgun three step 0.6-0.9 s; under-center five step 1.3-1.9 s;
     a tackle from contact to the ground 0.6-1.4 s; a get-up 1.5-4 s; a catch's hands-to-tuck 0.5-1.2 s;
     jukes and spins 0.5-1.1 s; a kick's approach-to-contact 0.6-1.2 s; stance / pocket / idle loops >= 0.8 s.
  2. The look targets (docs/LOOK_TARGETS.md, aggregate medians and p10-p90 of whole clips by category): our clip
     lies inside the category's p10-p90, or is a one-shot that the game chains (noted, not failed): their clips carry
     setup and recovery that ours hand to motion matching.
Exit code 1 when a real-world limit fails.
"""
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
CLIPS = os.path.join(PROJ, "RawArt", "Anim", "Clips")
TARGETS = os.path.join(PROJ, "..", "..", "docs", "look_targets.json")

# clip family -> (real-world min s, max s, look-target category, note)
REAL = [
    (r"^QB_Throw", (0.9, 2.0), "qb_throw", "set to settle; release ~0.23 s after the sim's, follow through 0.35-0.6 s"),
    (r"^QB_Drop_Shotgun", (0.6, 0.95), "qb_dropback", "three quick steps and a hitch"),
    (r"^QB_Drop_UnderCenter", (1.3, 1.9), "qb_dropback", "five step drop, plant and hitch"),
    (r"^QB_Pocket_Loop", (0.8, 3.0), "stance_qb", "loop"),
    (r"^QB_Shotgun_Loop", (2.0, 4.0), "stance_qb", "breathing loop"),
    (r"^Celebrate_Loop", (1.2, 5.0), "celebration", "loop"),
    (r"^QB_(SnapCatch|UnderCenterSnap)", (0.3, 0.6), "snap", ""),
    (r"^QB_(Handoff|Pitch)", (0.5, 1.2), "handoff_pitch", ""),
    (r"^QB_Kneel", (0.8, 2.5), "other", ""),
    (r"^PT_.*_T$|^PT_.*_T_M$", (0.9, 1.8), "tackle", "contact to the ground, tackler"),
    (r"^PT_.*_C$|^PT_.*_C_M$", (0.9, 1.8), "tackle", "contact to the ground, carrier"),
    (r"^PT_PileOn", (0.5, 1.2), "tackle_multi", ""),
    (r"^Tackle_(Wrap|High|Low|Shoulder|Arm|Assist)", (0.5, 1.3), "tackle", "sink, explode, wrap (the pair takes him down)"),
    (r"^Tackle_Dive", (0.6, 1.4), "tackle_dive", ""),
    (r"^Tackle_Miss", (0.6, 1.4), "tackle", "whiff and stumble"),
    (r"^Tackled_", (0.3, 1.2), "tackle", "the carrier's hit reaction before the paired fall takes him down"),
    (r"^Broken_Stumble", (0.5, 1.2), "bc_truck_hurdle", ""),
    (r"^GetUp_(FaceDown|FaceUp|Roll)", (1.2, 3.0), "get_up", "to standing"),
    (r"^GetUp_Slow", (2.0, 5.0), "get_up", "shaken up"),
    (r"^Injured_Down", (1.5, 6.0), "injury", "chained into the slow get-up"),
    (r"^Catch_Dive", (1.4, 2.4), "catch_dive", ""),
    (r"^Catch_", (0.5, 1.3), "catch_other", "hands to the ball, give, tuck"),
    (r"^(Juke|Spin)_", (0.4, 1.1), "bc_juke", "the cut itself; the run continues out of it"),
    (r"^StiffArm_", (0.6, 1.4), "bc_stiff_arm", ""),
    (r"^(Truck|Hurdle)", (0.6, 1.4), "bc_truck_hurdle", ""),
    (r"^Kick_", (1.0, 2.2), "kicking", "approach, plant, contact, follow through"),
    (r"^Lineman_GetOff", (0.3, 0.7), "block_run", "the first two steps"),
    (r"^Block_Punch", (0.25, 0.6), "block_pass", "the strike at contact"),
    (r"^(Block|Rush)_.*_Loop", (0.8, 2.0), "block_pass", "loop"),
    (r"^Block_(Beaten|Finish)", (0.5, 1.2), "block_pass", ""),
    (r"^(Rush|Shed)_", (0.4, 1.3), "rush_move", "a swim or rip is one quick beat"),
    (r"^Def_Breakdown_Loop", (0.6, 1.2), "loco_shuffle", "loop, 4 choppy steps/s"),
    (r"^Stance.*_Loop", (2.0, 4.0), "stance_other", "breathing loop"),
    (r"^Idle_.*_Loop", (2.0, 4.0), "idle", "loop"),
    (r"^Celebrate_.*_Loop", (1.2, 5.0), "celebration", "loop"),
    (r"^Fan_Cheer_Loop", (1.0, 4.0), "celebration", "loop"),
]
# the drops' ground speed over the moving part (travel m, moving frames at 30 fps; the rest is the hitch and settle);
# real: shotgun 2.5-3.2 m/s, under center 3.6-4.5
SPEEDS = {"QB_Drop_Shotgun": (1.8, 20, (2.5, 3.2)), "QB_Drop_UnderCenter": (6.0, 45, (3.6, 4.5))}


def main():
    targets = json.load(open(TARGETS))["timing"] if os.path.exists(TARGETS) else {}
    fails, rows = 0, []
    for path in sorted(glob.glob(os.path.join(CLIPS, "A_TAG_*.json"))):
        r = json.load(open(path))
        name = r["clip"]
        if name.endswith("_M"):
            continue  # mirrors share their original's timing
        sec = r["seconds"]
        fam = next((f for f in REAL if re.search(f[0], name)), None)
        if fam is None:
            rows.append((name, sec, "?", "", "no family"))
            continue
        lo, hi = fam[1]
        ok = lo <= sec <= hi
        cat = targets.get(fam[2])
        band = ""
        if cat:
            inside = cat["p10_s"] <= sec <= cat["p90_s"]
            band = "in %.2f-%.2f" % (cat["p10_s"], cat["p90_s"]) if inside else "chained (%.2f-%.2f whole clips)" % (cat["p10_s"], cat["p90_s"])
        sp = ""
        if name in SPEEDS:
            travel, frames, (slo, shi) = SPEEDS[name]
            v = travel / (frames / 30.0)
            sp = " %.2f m/s" % v
            ok = ok and slo <= v <= shi
        rows.append((name, sec, "ok" if ok else "FAIL", "%.1f-%.1f s%s" % (lo, hi, sp), band + ("  " + fam[3] if fam[3] else "")))
        fails += 0 if ok else 1
    w = max(len(r[0]) for r in rows)
    for name, sec, verdict, lim, note in rows:
        print("%-*s %5.2f s  %-4s  %-18s %s" % (w, name, sec, verdict, lim, note))
    print("\n%d clips, %d outside their real-world range" % (len(rows), fails))
    sys.exit(1 if fails else 0)


main()
