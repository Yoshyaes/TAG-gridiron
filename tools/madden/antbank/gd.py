"""EA GenericData (ANT AssetBank) reader for the Madden 19 reference pipeline.

Reads the AssetBank RES that FrostyHost exports through the Frosty SDK (op "raw"). Read only.
Format knowledge comes from reading the FIFA 17 edition FrostyAnimationEditor (no licence file, so this is
an independent reimplementation kept private like the rest of the pipeline) and from the Madden data itself.

Container (body starts at the version dword, big endian unless noted)
  0x00 u32 version (FIFA 17 = 3, Madden 19 = 5)
  0x04 8 x u32 H. H[0] = bytes from 0x04 to GD.STRMb, H[2..5] = |T| |C| |A| |B|, H[7] = allocator size
  0x24 T |T| x {u64 key LE, u64 0}, C |C| x u64 LE, A |A| x 16 bytes, B |B| x 16 bytes
  GD.STRMb {u32 size, u32 aux}, GD.REFLb {u32 size, u32 aux} + type table, GD.DATAb blocks until EOF
Object  0x20 byte header {.., +0x04 marker 0x030E6205, +0x14 type id, +0x1C u16 data offset} then field slots.
"""
import struct

MARKER = 0x030E6205
PRIM = {1: "Bool", 2: "Int8", 3: "UInt8", 4: "Int16", 5: "UInt16", 6: "Int32", 7: "UInt32", 8: "Int64",
        0x0A: "Float", 0x0B: "Vector2", 0x0C: "Vector3", 0x0D: "Vector4", 0x0E: "Quaternion", 0x0F: "Matrix44",
        0x11: "String", 0x12: "DataRef", 0x23: "Key"}


# Byte order. The container header is big endian. GD blocks carry it in the tag suffix: "b" big endian
# (FIFA 17), "l" little endian (Madden 19). E is set per bank before decoding.
E = ">"
# Object header offsets. Big endian (FIFA 17) marker +0x04, type id +0x14. Little endian (Madden 19)
# marker +0x00, type id +0x10 (the 64 bit header words are byte swapped). Data offset u16 at +0x1C in both.
MK, TI = 4, 0x14


def u16(b, o): return struct.unpack_from(E + "H", b, o)[0]
def s16(b, o): return struct.unpack_from(E + "h", b, o)[0]
def u32(b, o): return struct.unpack_from(E + "I", b, o)[0]
def s32(b, o): return struct.unpack_from(E + "i", b, o)[0]
def u64(b, o): return struct.unpack_from(E + "Q", b, o)[0]
def s64(b, o): return struct.unpack_from(E + "q", b, o)[0]
def f32(b, o): return struct.unpack_from(E + "f", b, o)[0]
def u64le(b, o): return struct.unpack_from("<Q", b, o)[0]
def be32(b, o): return struct.unpack_from(">I", b, o)[0]


class Field:
    __slots__ = ("name", "ftype", "size", "offset", "count", "attr", "align", "ordinal", "type")

    @property
    def is_list(self):
        return bool(self.attr & 1)

    def tname(self):
        if self.type is not None:
            return self.type.name
        return PRIM.get(self.ftype, "0x%08X" % self.ftype)


class TypeDesc:
    __slots__ = ("tid", "name", "size", "align", "flags", "fields", "by_name", "pad0", "last")

    def stride(self):
        a = self.align or 1
        return (self.size + a - 1) // a * a


class TypeTable:
    def __init__(self, m):
        cnt = u64(m, 0)
        offs = [u64(m, 8 + 8 * i) for i in range(cnt)]
        self.types, self.by_name, self.order = {}, {}, []
        for i, d in enumerate(offs):
            t = TypeDesc()
            t.pad0, t.last, t.size, t.align = u32(m, d), u32(m, d + 4), u32(m, d + 8), u32(m, d + 12)
            nbo, nbs, t.flags, t.tid = u32(m, d + 16), u32(m, d + 20), u32(m, d + 24), u32(m, d + 28)
            blk = d + nbo
            t.name = m[blk + 1:m.index(b"\0", blk + 1)].decode("latin1")
            t.fields, t.by_name = [], {}
            for k in range((nbo - 0x20) // 32):
                r = d + 0x20 + 32 * k
                f = Field()
                f.ftype, f.size, f.offset, noff = u32(m, r), u32(m, r + 4), u32(m, r + 8), u32(m, r + 12)
                f.count, f.attr, f.align, f.ordinal = u16(m, r + 16), u16(m, r + 18), u16(m, r + 20), u16(m, r + 22)
                f.name = m[blk + noff:m.index(b"\0", blk + noff)].decode("latin1")
                f.type = None
                t.fields.append(f)
                t.by_name[f.name] = f
            self.types[t.tid] = t
            self.by_name[t.name] = t
            self.order.append(t)
        for t in self.order:
            for f in t.fields:
                f.type = self.types.get(f.ftype)


class Obj(dict):
    """Decoded object: field name -> value, plus __type and __off."""


class Bank:
    def __init__(self, data):
        global E, MK, TI
        self.b = data
        b = data
        self.version = be32(b, 0)
        self.H = [be32(b, 4 + 4 * i) for i in range(8)]
        nT, nC, nA, nB = self.H[2:6]
        o = 0x24
        self.T = [u64le(b, o + 16 * i) for i in range(nT)]
        o += 16 * nT
        self.C = [u64le(b, o + 8 * i) for i in range(nC)]
        o += 8 * nC + 16 * nA + 16 * nB
        if b[o:o + 7] != b"GD.STRM":
            raise ValueError("GD.STRM not at 0x%x" % o)
        self.endian = "<" if b[o + 7:o + 8] == b"l" else ">"
        E = self.endian
        MK, TI = (0, 0x10) if E == "<" else (4, 0x14)
        ro = o + 16
        if b[ro:ro + 7] != b"GD.REFL":
            raise ValueError("GD.REFL not at 0x%x" % ro)
        rsize = u32(b, ro + 8)
        self.types = TypeTable(b[ro + 16:ro + rsize])
        self.data_off = ro + rsize
        self.blocks = []
        off, end = self.data_off, len(b)
        while off < end:
            if b[off:off + 7] != b"GD.DATA":
                raise ValueError("expected GD.DATA at 0x%x" % off)
            s = u32(b, off + 8)
            self.blocks.append((off, s))
            off += s

    def block_type(self, i):
        p = self.blocks[i][0] + 16
        if u32(self.b, p + MK) != MARKER:
            return None
        t = self.types.types.get(u32(self.b, p + TI))
        return t

    def decode(self, i, depth_limit=64):
        return Decoder(self.b, self.blocks[i][0] + 16, self.types).obj(0)


class Decoder:
    def __init__(self, b, p0, types):
        self.b, self.p0, self.types, self.seen = b, p0, types, {}

    def obj(self, off):
        if off in self.seen:
            return self.seen[off]
        a = self.p0 + off
        if u32(self.b, a + MK) != MARKER:
            raise ValueError("no marker at payload+0x%x" % off)
        t = self.types.types[u32(self.b, a + TI)]
        o = Obj()
        o["__type"] = t.name
        o["__off"] = off
        self.seen[off] = o
        self.fields(t, o, off + u16(self.b, a + 0x1C))
        return o

    def fields(self, t, o, slot0):
        for f in t.fields:
            s = slot0 + f.offset
            if f.is_list:
                o[f.name] = self.lst(f, s)
            elif f.count != 1 and f.size != 0:
                o[f.name] = [self.val(f, s + k * f.size) for k in range(f.count)]
            elif f.size != 0 or f.ftype in PRIM or (f.type is not None and f.type.size != 0):
                o[f.name] = self.val(f, s)

    def val(self, f, s):
        b, a, ft = self.b, self.p0 + s, f.ftype
        if ft == 0x23: return u64(b, a)
        if ft == 1: return b[a] != 0
        if ft == 2: return struct.unpack_from("b", b, a)[0]
        if ft == 3: return b[a]
        if ft == 4: return s16(b, a)
        if ft == 5: return u16(b, a)
        if ft == 6: return s32(b, a)
        if ft == 7: return u32(b, a)
        if ft == 8: return s64(b, a)
        if ft == 0x0A: return f32(b, a)
        if ft == 0x12:
            p = u64(b, a)
            return self.obj(p) if p else None
        if ft == 0x11: return self.string(s)
        if ft in (0x0B, 0x0C, 0x0D, 0x0E): return list(struct.unpack_from(E + "4f", b, a))
        if ft == 0x0F: return list(struct.unpack_from(E + "16f", b, a))
        t = f.type
        if t is None:
            return bytes(b[a:a + f.size])
        o = Obj()
        o["__type"] = t.name
        self.fields(t, o, s)
        return o

    def string(self, s):
        a = self.p0 + s
        cnt, ptr = u32(self.b, a), u64(self.b, a + 8)
        if cnt == 0:
            return None
        pa = self.p0 + ptr
        return bytes(self.b[pa:pa + cnt]).rstrip(b"\0").decode("latin1", "replace")

    def lst(self, f, s):
        b, a = self.b, self.p0 + s
        cnt, cap, ptr = u32(b, a), u32(b, a + 4), u64(b, a + 8)
        if cnt == 0:
            return []
        pa = self.p0 + ptr
        ft, et = f.ftype, f.type
        if ft in (2, 3) and not (et is not None and et.fields):
            return bytes(b[pa:pa + cnt])
        fmt = {1: "B", 4: "h", 5: "H", 6: "i", 7: "I", 8: "q", 0x0A: "f", 0x23: "Q"}.get(ft)
        if fmt:
            return list(struct.unpack_from(E + "%d%s" % (cnt, fmt), b, pa))
        if ft == 0x12:
            return [self.obj(p) if p else None for p in struct.unpack_from(E + "%dQ" % cnt, b, pa)]
        if ft == 0x11:
            return [self.string(ptr + 16 * k) for k in range(cnt)]
        if ft in (0x0B, 0x0C, 0x0D, 0x0E):
            return [list(struct.unpack_from(E + "4f", b, pa + 16 * k)) for k in range(cnt)]
        if ft == 0x0F:
            return [list(struct.unpack_from(E + "16f", b, pa + 64 * k)) for k in range(cnt)]
        if et is None:
            raise ValueError("list of unknown type 0x%08X" % ft)
        st = et.stride()
        out = []
        for k in range(cnt):
            o = Obj()
            o["__type"] = et.name
            self.fields(et, o, ptr + k * st)
            out.append(o)
        return out


def load(path):
    with open(path, "rb") as fh:
        return Bank(fh.read())


def build_key_index(bank):
    """key -> block index, plus the root type name of every block (one root object per block)."""
    idx, types = {}, []
    b = bank.b
    for i, (off, _) in enumerate(bank.blocks):
        p = off + 16
        t = bank.types.types.get(u32(b, p + TI)) if u32(b, p + MK) == MARKER else None
        types.append(t.name if t else None)
        if t is None or "__key" not in t.by_name:
            continue
        doff = u16(b, p + 0x1C)
        idx[u64(b, p + doff + t.by_name["__key"].offset)] = i
    return idx, types
