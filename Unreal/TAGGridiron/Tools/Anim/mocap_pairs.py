"""Paired-tackle placement from the built PT clips: the numbers AMatchDirector::StartPairedTackle keeps in GPairs.

    python Tools/Anim/mocap_pairs.py [clips_dir]        (default RawArt/Anim/Clips)

Both men of a pair come from one pack group in a shared space (mocap_map: PT_<pair>_T is __P02, PT_<pair>_C is
__P01, mode 'anchor'). Each clip's json carries its anchor (the man's trajectory at the clip's first frame, world
position and heading) and where his pelvis ends in his own actor frame. From those:
  Ox, Oy   the carrier's actor in the tackler's frame (m; x left, y back)
  Yaw      the carrier's heading relative to the tackler's (deg, positive turns left)
  EndX/Y   the carrier's pelvis at the end of his clip, in his own frame
Writes clips_dir/mocap_pairs.json and prints the GPairs rows.
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
CLIPS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(PROJ, "RawArt", "Anim", "Clips")
PAIRS = ["Drive", "Over", "Behind", "Side", "Dive"]
LIE = {"Drive": (0.45, 1.05), "Over": (1.05, 0.45), "Behind": (0.45, 1.05), "Side": (0.45, 1.05), "Dive": (0.70, 0.90)}


def load(name):
    p = os.path.join(CLIPS, "A_TAG_%s.json" % name)
    return json.load(open(p)) if os.path.exists(p) else None


def main():
    out = {}
    for pair in PAIRS:
        t, c = load("PT_%s_T" % pair), load("PT_%s_C" % pair)
        if not t or not c:
            print("%-7s missing" % pair)
            continue
        at, ac = t["source"]["anchor"], c["source"]["anchor"]
        ht, hc = math.radians(t["source"]["anchor_heading_deg"]), math.radians(c["source"]["anchor_heading_deg"])
        # heading h: forward = (sin h, -cos h), left = (cos h, sin h)
        fwd = (math.sin(ht), -math.cos(ht))
        left = (math.cos(ht), math.sin(ht))
        dx, dy = ac[0] - at[0], ac[1] - at[1]
        ox = dx * left[0] + dy * left[1]
        oy = -(dx * fwd[0] + dy * fwd[1])
        yaw = math.degrees(math.atan2(math.sin(hc - ht), math.cos(hc - ht)))
        ex, ey = c["pelvis_end"][0], c["pelvis_end"][1]
        tl, cl = LIE[pair]
        out[pair] = {"Ox": round(ox, 2), "Oy": round(oy, 2), "Yaw": round(yaw, 1), "EndX": round(ex, 2), "EndY": round(ey, 2),
                     "TacklerLie": tl, "CarrierLie": cl, "seconds_T": t["seconds"], "seconds_C": c["seconds"],
                     "src": t["source"]["src"]}
        print("{%.2ff, %.2ff, %.1ff, %.2ff, %.2ff, %.2ff, %.2ff},  // %s: %s (T %.2fs, C %.2fs)" % (
            ox, oy, yaw, ex, ey, tl, cl, pair, t["source"]["src"], t["seconds"], c["seconds"]))
    json.dump(out, open(os.path.join(CLIPS, "mocap_pairs.json"), "w"), indent=1)


main()
