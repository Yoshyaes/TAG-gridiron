"""Checkpoint B. Decode clips onto MaddenFB_Hero_Skeleton and test plausibility with numbers.

Tests per clip
  no NaN or infinity in any decoded channel
  bone lengths constant across frames (max relative deviation per parent child pair)
  root speed (horizontal speed of AITrajectory, also Hips) in m/s
  frame 0 local rotations against the EBX bind pose (degrees, per region)
  plausibility extras: head height, lowest foot height, quaternion norms

Usage
  py -3.13 -I checkb.py <out.json> <clip key hex> [<clip key hex> ...]
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pose  # noqa: E402

# No heading correction. The rig's Hips default differs from the EBX bind by a 90 degree yaw, but children
# are relative to Hips, so this only changes heading. Decoded as is, the run clip faces and moves along +Z,
# the mesh's forward axis (see FINDINGS, Checkpoint B).
YAW_FIX = np.eye(3)

REGIONS = {
    "spine_head": ["Spine", "Spine1", "Spine2", "Spine3", "Neck", "Neck1", "Head"],
    "legs": ["LeftUpLeg", "LeftLeg", "LeftFoot", "LeftToeBase", "RightUpLeg", "RightLeg", "RightFoot", "RightToeBase"],
    "arms": ["LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand", "RightShoulder", "RightArm", "RightForeArm", "RightHand"],
}


def evaluate(ev, key):
    conv = pose.quat_to_mat
    c, names, frames = ev.sample(key)
    h = ev.hero
    fps = None
    raw = np.concatenate([np.atleast_1d(np.asarray(v, dtype=float)).ravel() for fr in frames for v in fr.values()]) if frames else np.zeros(0)
    finite = bool(np.all(np.isfinite(raw)))
    qn = [abs(np.linalg.norm(fr[n]) - 1) for fr in frames for n in fr if n.endswith(".q")]
    G, GRs = [], []
    for fr in frames:
        R, T = ev.local(fr, conv)
        ti = h.index["AITrajectory"]
        R[ti] = YAW_FIX @ R[ti]
        T[ti] = YAW_FIX @ T[ti]
        GR, GT = ev.fk(R, T)
        G.append(GT)
        GRs.append(GR)
    G = np.array(G)                                   # frames x bones x 3 (meters)
    # bone lengths over the skeleton chain the clip drives
    # Hips is excluded: its offset from AITrajectory is root motion (Hips.t is animated), not a bone.
    chain = [h.index[n] for n in sum(REGIONS.values(), []) if n in h.index]
    devs = {}
    for i in chain:
        p = h.parent[i]
        if p < 0:
            continue
        L = np.linalg.norm(G[:, i] - G[:, p], axis=1)
        devs[h.names[i]] = float((L.max() - L.min()) / max(L.mean(), 1e-9))
    nf = len(frames)
    dur = (nf - 1) / c_fps(ev, key) if nf > 1 else 0
    traj = G[:, h.index["AITrajectory"]]
    hips = G[:, h.index["Hips"]]
    hdist = lambda P: float(np.sum(np.linalg.norm(np.diff(P[:, [0, 2]], axis=0), axis=1)))
    net = lambda P: float(np.linalg.norm(P[-1, [0, 2]] - P[0, [0, 2]]))
    res = {"frames": nf, "duration_s": round(dur, 4), "finite": finite,
           "quat_norm_err_max": float(max(qn) if qn else 0),
           "bone_length_rel_dev_max": float(max(devs.values()) if devs else 0),
           "bone_length_worst": max(devs, key=devs.get) if devs else None,
           "traj_speed_mps": round(hdist(traj) / dur, 3) if dur else 0,
           "traj_net_speed_mps": round(net(traj) / dur, 3) if dur else 0,
           "hips_speed_mps": round(hdist(hips) / dur, 3) if dur else 0,
           "hips_net_speed_mps": round(net(hips) / dur, 3) if dur else 0,
           "head_height_m": [round(float(G[:, h.index["Head"], 1].min()), 3), round(float(G[:, h.index["Head"], 1].max()), 3)],
           "lowest_foot_m": round(float(min(G[:, h.index["LeftToeBase"], 1].min(), G[:, h.index["RightToeBase"], 1].min(),
                                            G[:, h.index["LeftFoot"], 1].min(), G[:, h.index["RightFoot"], 1].min())), 3),
           "animated_joints": sorted({n.split(".")[0] for n in names}), "flags": c.Flags, "num_keys": c.NumKeys}
    # frame 0 against the EBX bind pose
    f0 = frames[0]
    reg = {}
    for rname, js in REGIONS.items():
        angs = [pose.mat_angle(conv(f0[j + ".q"]), h.rot[h.index[j]]) for j in js if j + ".q" in f0]
        reg[rname] = {"median_deg": round(float(np.median(angs)), 1), "max_deg": round(float(max(angs)), 1)} if angs else None
    res["frame0_vs_bind"] = reg
    # frame 0 joint positions relative to Hips against the bind, after removing the root transform
    bR, bT = [h.rot[i] for i in range(len(h.names))], [h.trans[i] for i in range(len(h.names))]
    _, BG = ev.fk(bR, bT)
    hi = h.index["Hips"]
    GR0 = GRs[0][hi]
    rel = lambda P, Rh: (Rh.T @ (P - P[hi]).T).T
    a = rel(G[0], GR0)
    b = rel(BG, h.rot[hi] if h.parent[hi] < 0 else _[hi])
    posd = {n: float(np.linalg.norm(a[h.index[n]] - b[h.index[n]])) for n in ["Head", "LeftFoot", "RightFoot", "LeftHand", "RightHand", "LeftLeg", "RightLeg"]}
    res["frame0_pos_diff_m"] = {k: round(v, 3) for k, v in posd.items()}
    # smoothness: largest frame to frame local rotation change of any animated joint (degrees per frame)
    jumps = {}
    for n in [x for x in names if x.endswith(".q")]:
        a = [pose.mat_angle(conv(frames[i][n]), conv(frames[i + 1][n])) for i in range(nf - 1)]
        if a:
            jumps[n[:-2]] = max(a)
    res["max_deg_per_frame"] = round(max(jumps.values()), 2) if jumps else 0
    res["max_deg_per_frame_joint"] = max(jumps, key=jumps.get) if jumps else None
    res["median_joint_max_deg_per_frame"] = round(float(np.median(list(jumps.values()))), 2) if jumps else 0
    # loop continuity: first against last frame
    if nf > 2:
        lc = [pose.mat_angle(conv(frames[0][n]), conv(frames[-1][n])) for n in names if n.endswith(".q") and not n.startswith("AITrajectory")]
        res["first_last_median_deg"] = round(float(np.median(lc)), 2)
    # knee and elbow flexion: angle at the joint between the two segments, and whether it bends one way only
    def bend(a, b, c):
        u, v = G[:, a] - G[:, b], G[:, c] - G[:, b]
        return np.degrees(np.arccos(np.clip(np.sum(u * v, 1) / (np.linalg.norm(u, axis=1) * np.linalg.norm(v, axis=1)), -1, 1)))
    ix = h.index
    res["knee_angle_deg"] = [round(float(x), 1) for x in (bend(ix["LeftUpLeg"], ix["LeftLeg"], ix["LeftFoot"]).min(), bend(ix["LeftUpLeg"], ix["LeftLeg"], ix["LeftFoot"]).max())]
    res["elbow_angle_deg"] = [round(float(x), 1) for x in (bend(ix["LeftArm"], ix["LeftForeArm"], ix["LeftHand"]).min(), bend(ix["LeftArm"], ix["LeftForeArm"], ix["LeftHand"]).max())]
    # knee bends forward: the knee lies in front of the hip to ankle line (mesh forward is +Z)
    # anatomical forward: (left hip - right hip) x up, which is +Z on the bind mesh (left +X, up +Y)
    lat = G[:, ix["LeftUpLeg"]] - G[:, ix["RightUpLeg"]]
    fwd = np.cross(lat, [0, 1.0, 0])
    fwd /= np.maximum(np.linalg.norm(fwd, axis=1, keepdims=True), 1e-9)
    kf = []
    for side in ("Left", "Right"):
        hip, knee, ank = G[:, ix[side + "UpLeg"]], G[:, ix[side + "Leg"]], G[:, ix[side + "Foot"]]
        kf.append(float(np.mean(np.sum((knee - (hip + ank) / 2) * fwd, 1) > -0.005)))
    res["knee_forward_fraction"] = [round(x, 3) for x in kf]
    d = traj[-1] - traj[0]
    res["traj_heading_xz"] = [round(float(d[0]), 3), round(float(d[2]), 3)]
    res["facing_frame0_xz"] = [round(float(fwd[0, 0]), 3), round(float(fwd[0, 2]), 3)]
    mvn = np.linalg.norm(d[[0, 2]])
    res["facing_vs_motion_cos"] = round(float(np.dot(np.mean(fwd, 0)[[0, 2]], d[[0, 2]]) / mvn), 3) if mvn > 0.5 else None
    return res, G


def c_fps(ev, key):
    return ev._fps.get(key, 30.0)


def main(argv):
    out = argv[1]
    keys = [int(k, 16) for k in argv[2:]]
    ev = pose.Evaluator()
    ev._fps = {}
    for o in ev.db.of_type("ClipControllerAsset"):
        for role in ("Anim", "LayerAnim"):
            if o.get(role):
                ev._fps.setdefault(o[role], (o.get("FPS") or 30.0, o["__name"]))
    names = {k: v[1] for k, v in ev._fps.items()}
    ev._fps = {k: v[0] for k, v in ev._fps.items()}
    report = {}
    for k in keys:
        r, _ = evaluate(ev, k)
        r["name"] = names.get(k)
        r["fps"] = ev._fps.get(k)
        report["%016x" % k] = r
        print(json.dumps({"clip": r["name"], **{x: r[x] for x in ("frames", "duration_s", "finite", "bone_length_rel_dev_max", "traj_speed_mps",
                                                                   "hips_net_speed_mps", "head_height_m", "lowest_foot_m", "frame0_vs_bind", "max_deg_per_frame", "max_deg_per_frame_joint",
                                                                   "first_last_median_deg", "knee_angle_deg", "elbow_angle_deg", "knee_forward_fraction", "traj_heading_xz",
                                                                   "facing_frame0_xz", "facing_vs_motion_cos") if x in r}}))
    json.dump(report, open(out, "w"), indent=1)


if __name__ == "__main__":
    main(sys.argv)
