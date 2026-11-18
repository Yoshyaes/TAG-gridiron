"""Checkpoint C and D. Decoded clips -> ASCII FBX 7.4 against MaddenFB_Hero_Skeleton.

Matches the Frosty skeleton FBX the meshes were exported with. Y up, meters (UnitScaleFactor 100), the
same 268 bone names and parents, bind = EBX LocalPose, Euler XYZ (R = Rz Ry Rx) which reproduces Frosty's
own Lcl Rotation values. Every bone is keyed on every frame. Joints the clip does not drive keep the EBX
bind (RM_ deformation helpers, face, fingers when no finger layer is given).

Usage
  py -3.13 -I fbxanim.py <out dir> <clip key hex>[:<layer key hex>] [...]
Writes <out dir>/<controller name>.fbx and <out dir>/<controller name>.json (frames, fps, source keys).
"""
import json
import math
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pose  # noqa: E402

KTIME = 46186158000
# FBX time modes. A standard mode is written when the clip rate has one (Interchange ignores CustomFrameRate).
TIME_MODES = {120.0: 1, 100.0: 2, 60.0: 3, 50.0: 4, 48.0: 5, 30.0: 6, 24.0: 11, 96.0: 15, 72.0: 16, 59.94: 17}


def euler_xyz(R):
    b = math.asin(max(-1.0, min(1.0, -R[2, 0])))
    a = math.atan2(R[2, 1], R[2, 2])
    g = math.atan2(R[1, 0], R[0, 0])
    return [math.degrees(a), math.degrees(b), math.degrees(g)]


def local_frames(ev, poses):
    """per frame lists of (R, T) for every Hero bone. Unanimated joints keep the EBX bind."""
    h = ev.hero
    out = []
    for p in poses:
        R, T = [], []
        for i, n in enumerate(h.names):
            R.append(pose.quat_to_mat(p[n + ".q"]) if n + ".q" in p else h.rot[i])
            T.append(np.asarray(p[n + ".t"]) if n + ".t" in p else h.trans[i])
        out.append((R, T))
    return out


def make_in_place(ev, frames):
    """Preview only. Locks the horizontal Hips position at the origin on every frame and turns the clip so
    frame 0 faces the mesh's forward axis (+Z in FBX space). Done by a per frame world transform on
    AITrajectory, whose parent (Reference) is the identity, so every other joint, all rotations and the
    vertical motion are unchanged. The source clip is not modified."""
    h = ev.hero
    ti, hi, lu, ru = h.index["AITrajectory"], h.index["Hips"], h.index["LeftUpLeg"], h.index["RightUpLeg"]
    _, G0 = ev.fk(*frames[0])
    f = np.cross(G0[lu] - G0[ru], [0.0, 1.0, 0.0])
    f[1] = 0.0
    phi = math.atan2(f[0], f[2]) if np.linalg.norm(f) > 1e-6 else 0.0
    c, s = math.cos(-phi), math.sin(-phi)
    yaw = np.array([[c, 0.0, s], [0.0, 1.0, 0.0], [-s, 0.0, c]])
    out = []
    for R, T in frames:
        _, G = ev.fk(R, T)
        hips = G[hi].copy()
        hips[1] = 0.0
        R2, T2 = list(R), list(T)
        R2[ti] = yaw @ R[ti]
        T2[ti] = yaw @ (np.asarray(T[ti]) - hips)
        out.append((R2, T2))
    return out


def fmt(v):
    return ("%.7g" % v).replace("e+0", "e+").replace("e-0", "e-")


def arr(vals):
    return ",".join(fmt(v) for v in vals)


def write_fbx(path, hero, frames, fps, take="Take 001", keyed=None):
    """keyed = bone indices that get curves (None keys every bone). Unkeyed bones carry the EBX bind in
    their Lcl values, which is also the reference pose Unreal falls back to for bones without a track."""
    n = len(hero.names)
    nf = len(frames)
    keyed = list(range(n)) if keyed is None else sorted(keyed)
    stop = int(round((nf - 1) * KTIME / fps))
    times = [int(round(i * KTIME / fps)) for i in range(nf)]
    # curves: translation and rotation per bone, Euler unwrapped over time
    trans = np.array([[frames[f][1][b] for f in range(nf)] for b in range(n)])          # n x nf x 3
    rot = np.array([[euler_xyz(frames[f][0][b]) for f in range(nf)] for b in range(n)])  # n x nf x 3
    rot = np.degrees(np.unwrap(np.radians(rot), axis=1))
    nid = [1000000 + i * 10 for i in range(n)]
    aid = [2000000 + i * 10 for i in range(n)]
    stack_id, layer_id = 3000000, 3000001
    L = []
    w = L.append
    w("; FBX 7.4.0 project file")
    w("; Madden 19 private reference animation, TAG Gridiron. Not for distribution.")
    w("FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n\tCreator: \"MaddenRef fbxanim\"\n}")
    w("GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {")
    for k, t, v in (("UpAxis", "int", 1), ("UpAxisSign", "int", 1), ("FrontAxis", "int", 2), ("FrontAxisSign", "int", 1),
                    ("CoordAxis", "int", 0), ("CoordAxisSign", "int", 1), ("OriginalUpAxis", "int", 1), ("OriginalUpAxisSign", "int", 1)):
        w("\t\tP: \"%s\", \"int\", \"Integer\", \"\",%d" % (k, v))
    w("\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100")
    w("\t\tP: \"OriginalUnitScaleFactor\", \"double\", \"Number\", \"\",100")
    w("\t\tP: \"TimeMode\", \"enum\", \"\", \"\",%d" % TIME_MODES.get(round(fps, 2), 14))
    w("\t\tP: \"CustomFrameRate\", \"double\", \"Number\", \"\",%s" % fmt(fps))
    w("\t\tP: \"TimeSpanStart\", \"KTime\", \"Time\", \"\",0")
    w("\t\tP: \"TimeSpanStop\", \"KTime\", \"Time\", \"\",%d" % stop)
    w("\t}\n}")
    ncurve_nodes, ncurves = 2 * len(keyed), 6 * len(keyed)
    w("Definitions:  {\n\tVersion: 100\n\tCount: %d" % (1 + 2 * n + 3 + ncurve_nodes + ncurves))
    for t, c in (("GlobalSettings", 1), ("Model", n), ("NodeAttribute", n), ("Pose", 1), ("AnimationStack", 1), ("AnimationLayer", 1),
                 ("AnimationCurveNode", ncurve_nodes), ("AnimationCurve", ncurves)):
        w("\tObjectType: \"%s\" {\n\t\tCount: %d\n\t}" % (t, c))
    w("}")
    w("Objects:  {")
    conns = []
    for i, name in enumerate(hero.names):
        kind = "Root" if hero.parent[i] < 0 else "LimbNode"
        w("\tNodeAttribute: %d, \"NodeAttribute::%s\", \"%s\" {\n\t\tTypeFlags: \"Skeleton\"\n\t}" % (aid[i], name, kind))
        t0, r0 = hero.trans[i], euler_xyz(hero.rot[i])
        w("\tModel: %d, \"Model::%s\", \"LimbNode\" {\n\t\tVersion: 232\n\t\tProperties70:  {" % (nid[i], name))
        w("\t\t\tP: \"RotationOrder\", \"enum\", \"\", \"\",0")
        w("\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",%s" % arr(t0))
        w("\t\t\tP: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",%s" % arr(r0))
        w("\t\t\tP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",1,1,1")
        w("\t\t}\n\t\tShading: Y\n\t\tCulling: \"CullingOff\"\n\t}")
        conns.append("\tC: \"OO\",%d,%d" % (aid[i], nid[i]))
        conns.append("\tC: \"OO\",%d,%d" % (nid[i], nid[hero.parent[i]] if hero.parent[i] >= 0 else 0))
    # BindPose with the EBX global bind matrices (FBX stores them column major, translation in 12..14)
    G = [None] * n
    for i in range(n):
        Lm = np.eye(4)
        Lm[:3, :3] = hero.rot[i]
        Lm[:3, 3] = hero.trans[i]
        G[i] = Lm if hero.parent[i] < 0 else G[hero.parent[i]] @ Lm
    w("\tPose: %d, \"Pose::BIND_POSES\", \"BindPose\" {\n\t\tType: \"BindPose\"\n\t\tVersion: 100\n\t\tNbPoseNodes: %d" % (5000000, n))
    for i in range(n):
        w("\t\tPoseNode:  {\n\t\t\tNode: %d\n\t\t\tMatrix: *16 {\n\t\t\t\ta: %s\n\t\t\t}\n\t\t}" % (nid[i], arr(G[i].flatten(order="F"))))
    w("\t}")
    w("\tAnimationStack: %d, \"AnimStack::%s\", \"\" {\n\t\tProperties70:  {" % (stack_id, take))
    for k in ("LocalStart", "ReferenceStart"):
        w("\t\t\tP: \"%s\", \"KTime\", \"Time\", \"\",0" % k)
    for k in ("LocalStop", "ReferenceStop"):
        w("\t\t\tP: \"%s\", \"KTime\", \"Time\", \"\",%d" % (k, stop))
    w("\t\t}\n\t}")
    w("\tAnimationLayer: %d, \"AnimLayer::BaseLayer\", \"\" {\n\t}" % layer_id)
    conns.append("\tC: \"OO\",%d,%d" % (layer_id, stack_id))
    cid = 4000000
    # KeyTime is an int64 tick array. Written through %.7g it became "1.539539e+9", which FBX readers parse
    # as 1, so every key collapsed onto the first ticks and Unreal held the last pose (the twitch bug).
    ktimes = ",".join(str(int(t)) for t in times)
    for i in keyed:
        for prop, short, data in (("Lcl Translation", "T", trans[i]), ("Lcl Rotation", "R", rot[i])):
            cn = cid
            cid += 1
            w("\tAnimationCurveNode: %d, \"AnimCurveNode::%s\", \"\" {\n\t\tProperties70:  {" % (cn, short))
            for ax, c in zip("XYZ", range(3)):
                w("\t\t\tP: \"d|%s\", \"Number\", \"\", \"A\",%s" % (ax, fmt(data[0, c])))
            w("\t\t}\n\t}")
            conns.append("\tC: \"OO\",%d,%d" % (cn, layer_id))
            conns.append("\tC: \"OP\",%d,%d, \"%s\"" % (cn, nid[i], prop))
            for ax, c in zip("XYZ", range(3)):
                cv = cid
                cid += 1
                w("\tAnimationCurve: %d, \"AnimCurve::\", \"\" {" % cv)
                w("\t\tDefault: %s\n\t\tKeyVer: 4009" % fmt(data[0, c]))
                w("\t\tKeyTime: *%d {\n\t\t\ta: %s\n\t\t}" % (nf, ktimes))
                w("\t\tKeyValueFloat: *%d {\n\t\t\ta: %s\n\t\t}" % (nf, arr(data[:, c])))
                w("\t\tKeyAttrFlags: *1 {\n\t\t\ta: 4\n\t\t}")
                w("\t\tKeyAttrDataFloat: *4 {\n\t\t\ta: 0,0,218434821,0\n\t\t}")
                w("\t\tKeyAttrRefCount: *1 {\n\t\t\ta: %d\n\t\t}" % nf)
                w("\t}")
                conns.append("\tC: \"OP\",%d,%d, \"d|%s\"" % (cv, cn, ax))
    w("}")
    w("Connections:  {")
    L.extend(conns)
    w("}")
    w("Takes:  {\n\tCurrent: \"%s\"\n\tTake: \"%s\" {\n\t\tFileName: \"Take_001.tak\"\n\t\tLocalTime: 0,%d\n\t\tReferenceTime: 0,%d\n\t}\n}" % (take, take, stop, stop))
    tmp = path + ".partial"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")
    os.replace(tmp, path)


def safe(name):
    return re.sub(r"[^A-Za-z0-9_]+", "_", name).strip("_")


class Exporter:
    def __init__(self):
        self.ev = pose.Evaluator()
        self.ctrl = {}
        for o in self.ev.db.of_type("ClipControllerAsset"):
            for role in ("Anim", "LayerAnim"):
                if o.get(role):
                    self.ctrl.setdefault(o[role], o)

    def export(self, out_dir, key, layer=None, skip_existing=True, sparse=False, fname=None, in_place=False):
        c = self.ctrl.get(key)
        name = fname or (safe(c["__name"]) if c else "%016x" % key)
        src_fps = float(c.get("FPS") or 30.0) if c else 30.0
        # NTSC rates (29.97, 59.94) are written at the nearest integer rate. Unreal bakes at integer rates and
        # refuses lengths that are not frame aligned. Duration changes by 0.1 percent (source rate kept in meta).
        fps = float(round(src_fps)) if abs(src_fps - round(src_fps)) < 0.1 else src_fps
        path = os.path.join(out_dir, name + ".fbx")
        meta_path = os.path.join(out_dir, name + ".json")
        if skip_existing and os.path.exists(path) and os.path.exists(meta_path):
            return path, json.load(open(meta_path))
        clip, names, poses = self.ev.sample(key)
        if layer:
            _, lnames, lposes = self.ev.sample(layer)
            for i, p in enumerate(poses if lposes else []):   # empty FRAM placeholder layers add nothing
                lp = lposes[min(i, len(lposes) - 1)]
                for k, v in lp.items():
                    p.setdefault(k, v)
        frames = local_frames(self.ev, poses)
        if in_place:
            frames = make_in_place(self.ev, frames)
        keyed = None
        if sparse:
            driven = {n.rsplit(".", 1)[0] for n in poses[0] if n.endswith((".q", ".t"))}
            if in_place:
                driven.add("AITrajectory")
            keyed = [i for i, n in enumerate(self.ev.hero.names) if n in driven]
        write_fbx(path, self.ev.hero, frames, fps, keyed=keyed)
        meta = {"name": c["__name"] if c else None, "clip_key": "%016x" % key, "layer_key": "%016x" % layer if layer else None,
                "frames": len(poses), "fps": fps, "source_fps": src_fps, "duration_s": round((len(poses) - 1) / fps, 4),
                "animated": sorted({n.rsplit(".", 1)[0] for n in poses[0] if n.endswith((".q", ".t"))})}
        json.dump(meta, open(meta_path, "w"), indent=1)
        return path, meta


def main(argv):
    out_dir = argv[1]
    os.makedirs(out_dir, exist_ok=True)
    ex = Exporter()
    force = "--force" in argv
    for spec in [a for a in argv[2:] if not a.startswith("--")]:
        parts = spec.split(":")
        key = int(parts[0], 16)
        layer = int(parts[1], 16) if len(parts) > 1 and parts[1] else None
        path, meta = ex.export(out_dir, key, layer, skip_existing=not force, sparse="--sparse" in argv)
        print("wrote", path, meta["frames"], "frames at", meta["fps"], "fps")


if __name__ == "__main__":
    main(sys.argv)
