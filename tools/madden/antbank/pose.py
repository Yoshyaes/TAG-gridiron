"""Clip evaluation on MaddenFB_Hero_Skeleton for the Madden 19 reference pipeline.

Units. Madden rig translations are yards (rig default / EBX bind = 1.0936 on every joint), so meters =
rig x 0.9144. Channels are absolute local transforms (as in FIFA 17): animated joints take the clip value,
the others the rig default, then the EBX bind pose for joints the rig does not cover.
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import antdb  # noqa: E402
import vbr  # noqa: E402

YARD = 0.9144
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HERO_EBX = os.path.join(REPO, "extracted", "stage2", "research", "content__characters__rig__skeleton__player__MaddenFB_Hero_Skeleton.ebx.json")
BANKS = [os.path.join(REPO, "extracted", "animation", "banks", "levels__maddengame__maddengame_win32_antstate.assetbank"),
         os.path.join(REPO, "extracted", "animation", "banks", "content__common__configs__bundles__launch_sba_win32_antstate.assetbank")]
RIG = "MaddenRig"


def quat_to_mat(q):
    """(x, y, z, w) -> 3x3, column vector convention (R @ v)."""
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def mat_angle(a, b):
    c = (np.trace(a.T @ b) - 1) / 2
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


class Hero:
    """MaddenFB_Hero_Skeleton from the Frosty EBX dump. Local rotation as 3x3 (column convention) + translation (m)."""

    def __init__(self, path=HERO_EBX):
        o = json.load(open(path))["objects"][0]
        self.names = o["BoneNames"]
        self.parent = list(o["Hierarchy"])
        self.index = {n: i for i, n in enumerate(self.names)}
        self.rot, self.trans = [], []
        for lt in o["LocalPose"]:
            v = lambda k: np.array([lt[k]["x"], lt[k]["y"], lt[k]["z"]])
            m = np.stack([v("right"), v("up"), v("forward")])   # rows = local axes in parent space (row vector form)
            self.rot.append(m.T)                                  # column vector form
            self.trans.append(v("trans"))


class Evaluator:
    def __init__(self, banks=BANKS, rig_name=RIG):
        self.db = antdb.AntDb(banks)
        self.ri = antdb.RigIndex(self.db)
        self.rig = self.ri.rig_by_name(rig_name)
        self.bind = self.ri.bind_pose(self.rig)
        self.hero = Hero()

    def clip(self, key):
        o = self.db.get(key)
        if o is None or o["__type"] != "VbrAnimationAsset":
            raise ValueError("not a VBR clip")
        c = vbr.VbrClip(o)
        base = o.get("__base") or {}
        c2d = self.db.get(base.get("ChannelToDofAsset"))
        dofs = list(c2d["DofIds"]) if c2d else []
        names = [self.ri.name(d) for d in dofs]
        return c, names

    def sample_frame_asset(self, o):
        """FrameAnimationAsset ("FRAM"): one stored pose. Many finger layers are empty placeholders (no
        channels, no data) and contribute nothing. Data is QuatCount x 4 floats, then Vec3Count vectors
        (3 or 4 floats each, detected from the length), then FloatCount floats."""
        base = o.get("__base") or {}
        c2d = self.db.get(base.get("ChannelToDofAsset"))
        dofs = list(c2d["DofIds"]) if c2d else []
        names = [self.ri.name(d) for d in dofs]
        data = list(o.get("Data") or [])
        Q, V, F = int(o.get("QuatCount") or 0), int(o.get("Vec3Count") or 0), int(o.get("FloatCount") or 0)
        if not data or not names:
            return None, names, []
        if len(data) == 4 * Q + 3 * V + F:
            vs = 3
        elif len(data) in (4 * Q + 4 * V + F, 4 * Q + 4 * V + (F + 3) // 4 * 4):
            vs = 4      # vectors take 4 lanes, the float block is padded to whole groups of 4 lanes
        else:
            raise ValueError("FRAM layout unknown (data %d, Q %d, V %d, F %d)" % (len(data), Q, V, F))
        pose_ = {}
        for j, n in enumerate(names):
            if j < Q:
                pose_[n] = np.array(data[4 * j:4 * j + 4])
            elif j < Q + V:
                a = 4 * Q + vs * (j - Q)
                pose_[n] = np.array(data[a:a + 3]) * YARD
            else:
                pose_[n] = data[4 * Q + vs * V + (j - Q - V)]
        return None, names, [pose_]

    def sample(self, key, frames=None):
        """-> per frame dict joint -> {"q": quat, "t": meters} for every animated joint, plus floats."""
        o = self.db.get(key)
        if o is not None and o["__type"] == "FrameAnimationAsset":
            return self.sample_frame_asset(o)
        c, names = self.clip(key)
        q, v, f = c.sample_frames(frames)
        nq, nv = c.Q + c.CQ, c.V + c.CV
        kinds = ["q"] * nq + ["t"] * nv + ["f"] * (c.F + c.CF)
        out = []
        for fr in range(q.shape[0]):
            pose = {}
            for j, (n, k) in enumerate(zip(names, kinds)):
                if k == "q":
                    pose[n] = q[fr, j]
                elif k == "t":
                    pose[n] = v[fr, j - nq] * YARD
                else:
                    pose[n] = f[fr, j - nq - nv]
            out.append(pose)
        return c, names, out

    def local(self, pose, rot_conv):
        """Per Hero bone (R 3x3, t m). rot_conv maps a rig quaternion to the EBX rotation convention."""
        h = self.hero
        R, T = [], []
        for i, n in enumerate(h.names):
            if n + ".q" in pose:
                r = rot_conv(pose[n + ".q"])
            elif n in self.bind and "q" in self.bind[n]:
                r = rot_conv(self.bind[n]["q"])
            else:
                r = h.rot[i]
            if n + ".t" in pose:
                t = np.array(pose[n + ".t"])
            elif n in self.bind and "t" in self.bind[n]:
                t = np.array(self.bind[n]["t"][:3]) * YARD
            else:
                t = h.trans[i]
            R.append(r)
            T.append(t)
        return R, T

    def fk(self, R, T):
        h = self.hero
        GR, GT = [None] * len(R), [None] * len(R)
        for i in range(len(R)):
            p = h.parent[i]
            if p < 0:
                GR[i], GT[i] = R[i], T[i]
            else:
                GR[i] = GR[p] @ R[i]
                GT[i] = GR[p] @ T[i] + GT[p]
        return GR, np.array(GT)


def pick_convention(ev):
    """Which quaternion -> matrix mapping makes the rig defaults equal the EBX bind rotations."""
    cands = {"R(q)": lambda q: quat_to_mat(q), "R(q)^T": lambda q: quat_to_mat(q).T}
    scores = {}
    for name, f in cands.items():
        errs = []
        for n, d in ev.bind.items():
            if "q" in d and n in ev.hero.index:
                errs.append(mat_angle(f(d["q"]), ev.hero.rot[ev.hero.index[n]]))
        errs.sort()
        scores[name] = (np.median(errs), sum(1 for e in errs if e < 1.0), len(errs))
    best = min(scores, key=lambda k: scores[k][0])
    return best, cands[best], scores
