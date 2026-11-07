"""Multi bank ANT database and rig index for the Madden 19 reference pipeline.

Clips live in the gameplay bank (levels/maddengame) while their ChannelToDofAssets, DofSetLists, rigs and
skeletons live in the launch bank (content/common/configs/bundles/launch_sba). AntDb resolves keys across
banks. RigIndex names DOF ids the way the FIFA 17 notes describe: RigAsset.DofIds split by DofSetIdIndices,
one run per RigDofSets LayoutAsset whose Slots {Name, Type} name the DOFs ("Joint.q", "Joint.t", "Joint.s").
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gd  # noqa: E402

SLOT_QUAT, SLOT_TRANS, SLOT_SCALE, SLOT_FLOAT, SLOT_VEC3 = 14, 2049856663, 2049856454, 10, 12


class AntDb:
    def __init__(self, paths):
        self.banks, self.where, self.types = [], {}, []
        for p in paths:
            bk = gd.load(p)
            idx, types = gd.build_key_index(bk)
            bi = len(self.banks)
            self.banks.append(bk)
            self.types.append(types)
            for k, i in idx.items():
                self.where.setdefault(k, (bi, i))
        self.cache = {}

    def _activate(self, bi):
        # gd keeps byte order and header offsets in module globals; set them for this bank
        bk = self.banks[bi]
        gd.E = bk.endian
        gd.MK, gd.TI = (0, 0x10) if bk.endian == "<" else (4, 0x14)

    def get(self, key):
        if not key or key not in self.where:
            return None
        if key in self.cache:
            return self.cache[key]
        bi, i = self.where[key]
        self._activate(bi)
        o = self.banks[bi].decode(i)
        self.cache[key] = o
        return o

    def of_type(self, name):
        for bi, types in enumerate(self.types):
            for i, n in enumerate(types):
                if n == name:
                    self._activate(bi)
                    yield self.banks[bi].decode(i)


def inherited(db, o, field):
    """LayoutAsset fields may sit on a base object (__base DataRef)."""
    while o is not None:
        if o.get(field) is not None:
            return o[field]
        o = o.get("__base")
    return None


class RigIndex:
    def __init__(self, db):
        self.db = db
        self.dof = {}       # dof id -> (name, slot type)
        self.rigs = {}
        self.defaults = {}  # rig key -> {dof id: value}
        for r in db.of_type("RigAsset"):
            self.rigs[r["__key"]] = r
            ids, idx, sets = list(r.get("DofIds") or []), list(r.get("DofSetIdIndices") or []), list(r.get("RigDofSets") or [])
            for j, sk in enumerate(sets):
                if j >= len(idx):
                    break
                a = idx[j]
                b = idx[j + 1] if j + 1 < len(idx) else len(ids)
                la = db.get(sk)
                slots = inherited(db, la, "Slots") if la else None
                if not slots or len(slots) != b - a:
                    continue
                for s, sl in enumerate(slots):
                    d = ids[a + s]
                    self.dof.setdefault(d, (sl.get("Name") or "", sl.get("Type")))
            dv = {}
            for e in r.get("DefaultVector4Values") or []:
                dv[e["DofId"]] = list(e["Value"])
            for e in r.get("DefaultVector3Values") or []:
                dv[e["DofId"]] = list(e["Value"])[:3]
            for e in r.get("DefaultFloatValues") or []:
                dv[e["DofId"]] = e["Value"]
            self.defaults[r["__key"]] = dv

    def name(self, d):
        return self.dof.get(d, ("dof_%d" % d, None))[0]

    def rig_by_name(self, name):
        for r in self.rigs.values():
            if r.get("__name") == name:
                return r
        return None

    def skeleton(self, rig):
        sk = self.db.get(rig.get("Skeleton"))
        if not sk:
            return None
        return {"name": sk.get("__name"), "traj": sk.get("TrajJointIndex"),
                "joints": [(j.get("JointName"), j.get("ParentIndex"), j.get("JointType")) for j in sk.get("Joints") or []]}

    def bind_pose(self, rig):
        """{joint: {"q": [x,y,z,w], "t": [x,y,z], "s": [x,y,z]}} from the rig defaults (rig units)."""
        out = {}
        for d, v in self.defaults.get(rig["__key"], {}).items():
            n, typ = self.dof.get(d, (None, None))
            if not n or "." not in n or typ not in (SLOT_QUAT, SLOT_TRANS, SLOT_SCALE) or not isinstance(v, list):
                continue
            j, comp = n.rsplit(".", 1)
            out.setdefault(j, {})[comp] = v
        return out
