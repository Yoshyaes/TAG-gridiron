# tackle-cam capture -> <cap>/ovjobs.json: every frame two men play the same pack capture group at the same clip time,
# with the game's overlap (tackle_check capsules). Then tackle_truth_overlap.py (UE) scores the capture at those times.
# usage: python tackle_truth_prep.py <cap>
import sys, glob, json
sys.path.insert(0, r"C:/Users/soon2/Documents/Madden TAG/Unreal/TAGGridiron/Tools/Anim")
from tackle_check import load_clip, pair_metrics
cap = sys.argv[1]; jobs = []
for f in sorted(glob.glob(cap + "/tcdata/t0*.csv")):
    fr = load_clip(f)
    for k in sorted(fr):
        men = [m for m in fr[k].values() if m["anim"].startswith("fall:A_TAG_Pack_")]
        for i in range(len(men)):
            for j in range(i + 1, len(men)):
                a, b = men[i]["anim"][5:].split("@"), men[j]["anim"][5:].split("@")
                if a[0][:-5] != b[0][:-5] or a[1] != b[1]: continue  # same capture group, same clip time
                g, c, l = pair_metrics(men[i], men[j])
                jobs.append((a[0], b[0], float(a[1]), c, g))
json.dump(jobs, open(cap + "/ovjobs.json", "w")); print(len(jobs))


# --- lying frames (pelvis under 35 cm) of captured falls -> <cap>/liejobs.json
# lying frames of captured falls in a tackle-cam run -> liejobs.json [(clip, t, game cm/s)]
import csv, math
jobs = []
for f in sorted(glob.glob(cap + "/tcdata/t0*.csv")):
    prev = {}
    for r in csv.DictReader(open(f)):
        k = r["pid"]
        try: p = (float(r["pelvis_x"]), float(r["pelvis_y"]), float(r["pelvis_z"]))
        except: continue
        a = r["anim"]
        if k in prev and a.startswith("fall:A_TAG_Pack_") and prev[k][1].startswith("fall:") and p[2] < 35 and prev[k][0][2] < 35 and int(r["frame"]) == prev[k][2] + 1:
            name, t = a[5:].split("@")
            if prev[k][1].split("@")[0] == a.split("@")[0]:
                jobs.append((name, float(t), math.hypot(p[0] - prev[k][0][0], p[1] - prev[k][0][1]) * 30.0))
        prev[k] = (p, a, int(r["frame"]))
json.dump(jobs, open(cap + "/liejobs.json", "w"))
print(len(jobs), "lying frames")
