"""Compare Unreal's imported sequences with the Python decode at the same times.

Input. logs/ue_samples.json from a headless Unreal run (component space bone positions and local rotations
at 0, 25, 50, 75 and 100 percent of each sequence, raw and compressed).
For each sequence the matching clip is decoded at the same times (fractional frames through the VBR seek),
the in place transform is applied for _InPlace copies, and positions are compared in component space.
The FBX (Y up, meters) to Unreal (Z up, cm) axis mapping is fitted once as a signed permutation on t = 0.

Usage
  py -3.13 -I compare_ue.py <ue_samples.json> <out.json>
"""
import glob
import itertools
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
import fbxanim  # noqa: E402

BONES = ["Hips", "Spine2", "Head", "LeftArm", "LeftForeArm", "LeftHand", "RightHand", "LeftUpLeg", "LeftLeg", "LeftFoot",
         "RightFoot", "RightToeBase"]


def job_index():
    idx = {}
    for p in glob.glob(os.path.join(REPO, "tools", "madden", "jobs", "*_anim.json")):
        for j in json.load(open(p)):
            idx.setdefault(j["file"], j)
    return idx


def python_positions(ex, job, times, fps, in_place):
    frames = [t * fps for t in times]
    _, _, poses = ex.ev.sample(int(job["clip_key"], 16), frames)
    if job.get("layer_key"):
        _, _, lposes = ex.ev.sample(int(job["layer_key"], 16), frames)
        for i, p in enumerate(poses if lposes else []):
            for k, v in lposes[min(i, len(lposes) - 1)].items():
                p.setdefault(k, v)
    fr = fbxanim.local_frames(ex.ev, poses)
    if in_place:
        fr = fbxanim.make_in_place(ex.ev, fr)
    h = ex.ev.hero
    return np.array([[ex.ev.fk(*f)[1][h.index[b]] for b in BONES] for f in fr]) * 100.0   # cm, FBX axes


def fit_axes(py0, ue0):
    best = None
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product((1, -1), repeat=3):
            M = np.zeros((3, 3))
            for r, (c, s) in enumerate(zip(perm, signs)):
                M[r, c] = s
            err = np.abs(py0 @ M.T - ue0).max()
            if best is None or err < best[0]:
                best = (err, M)
    return best[1], best[0]


def main(argv):
    samples = json.load(open(argv[1]))
    idx = job_index()
    ex = fbxanim.Exporter()
    report = {}
    M = None
    for path, rec in samples.items():
        if "error" in rec:
            report[path] = rec
            continue
        name = path.rsplit("/", 1)[1][2:]
        in_place = name.endswith("_InPlace")
        job = idx.get(name[:-len("_InPlace")] if in_place else name)
        if job is None:
            report[path] = {"error": "no clip job for " + name}
            continue
        fps = fbxanim.TIME_MODES and (float(round(job["fps"])) if abs(job["fps"] - round(job["fps"])) < 0.1 else job["fps"])
        out = {"clip": job["name"], "in_place": in_place, "length_s": rec["length"]}
        for mode, rows in rec["samples"].items():
            times = [r["t"] for r in rows]
            py = python_positions(ex, job, times, fps, in_place)
            ue = np.array([[r[b]["w"] for b in BONES] for r in rows])
            if M is None:
                M, e0 = fit_axes(py[0], ue[0])
                report["_axes"] = {"fbx_to_ue": M.tolist(), "fit_error_cm": round(float(e0), 3)}
            err = np.abs(py @ M.T - ue).max(axis=2)          # samples x bones, cm
            moved_py = float(np.abs(py - py[0]).max())
            moved_ue = float(np.abs(ue - ue[0]).max())
            out[mode] = {"max_err_cm_per_time": [round(float(x), 2) for x in err.max(axis=1)],
                         "worst_bone_per_time": [BONES[int(i)] for i in err.argmax(axis=1)],
                         "python_motion_cm": round(moved_py, 1), "ue_motion_cm": round(moved_ue, 1)}
        report[path] = out
        r = out.get("RAW", {})
        print("%-55s err cm %s  motion py %s ue %s" % (path.rsplit("/", 1)[1][:55], r.get("max_err_cm_per_time"), r.get("python_motion_cm"), r.get("ue_motion_cm")))
    json.dump(report, open(argv[2], "w"), indent=1)


if __name__ == "__main__":
    main(sys.argv)
