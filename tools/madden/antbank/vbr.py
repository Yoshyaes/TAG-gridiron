"""VBR animation codec ("VBR ", VbrAnimationAsset) for the Madden 19 reference pipeline.

Independent reimplementation from reading the FIFA 17 edition FrostyAnimationEditor (VbrClip.cs). Same
algorithm, Madden differences are marked MADDEN where found.

Data layout: key time RLE (KeyTimeSize), constants (4*CQ + 3*CV + CF bytes), constant channel map RLE,
per animated scalar a u32 LE of eight 4 bit coefficient widths, vector offset ramp, float offset ramp, then
blocks of 8 keys (FrameBlockSizes). Block = 3 row weight bytes + body stored byte reversed. Per scalar a flag
bit per non zero width, a sign bit per set flag (1 = positive), then magnitudes LSB first. Dequantise with
step rows, 8 point IDCT, map to [min, max] per 4 lane group.
"""
import math
import struct

import numpy as np

LN = np.array([math.log(k + 2) for k in range(8)], dtype=np.float64)
BASIS = np.array([[0.125 if k == 0 else math.cos(math.pi * (2 * r + 1) * k / 16.0) / 4.0 for k in range(8)] for r in range(8)],
                 dtype=np.float64)


class VbrClip:
    def __init__(self, o):
        g = o.get
        for n in ("QuatMin", "TrajMin", "Vec3Min", "FloatMin", "QuatMax", "TrajMax", "Vec3Max", "FloatMax",
                  "VectorOffsetScale", "FloatOffsetScale", "Dct"):
            setattr(self, n, float(g(n) or 0.0))
        for n in ("QuaternionCount", "Vector3Count", "FloatCount", "ConstQuaternionCount", "ConstVector3Count",
                  "ConstFloatCount", "KeyTimeSize", "NumKeys", "ConstChanMapSize", "ConstPaletteSize",
                  "VectorOffsetSize", "FloatOffsetSize", "Flags"):
            setattr(self, n, int(g(n) or 0))
        self.palette = list(g("ConstantPalette") or [])
        self.block_sizes = list(g("FrameBlockSizes") or [])
        self.data = bytes(g("Data") or b"")
        base = g("__base") or {}
        self.end_frame = base.get("EndFrame")
        self.trim_offset = base.get("TrimOffset", 0.0)
        self.additive = base.get("Additive", False)
        self.layout()

    def layout(self):
        Q, V, F = self.QuaternionCount, self.Vector3Count, self.FloatCount
        CQ, CV, CF = self.ConstQuaternionCount, self.ConstVector3Count, self.ConstFloatCount
        self.Q, self.V, self.F, self.CQ, self.CV, self.CF = Q, V, F, CQ, CV, CF
        self.nscal = 4 * Q + 3 * V + F
        self.groups = Q + V + (F + 3) // 4
        self.lanes = 4 * self.groups
        o = 0
        self.kts_off = o; o += self.KeyTimeSize
        self.const_off = o; o += 4 * CQ + 3 * CV + CF
        self.ccm_off = o; o += self.ConstChanMapSize
        self.width_off = o; o += 4 * self.nscal
        self.vos_off = o; o += self.VectorOffsetSize
        self.fos_off = o; o += self.FloatOffsetSize
        self.blocks_off = o
        self.layout_ok = o + sum(self.block_sizes) == len(self.data)
        self.layout_diff = len(self.data) - (o + sum(self.block_sizes))
        if not self.layout_ok:
            return
        self.widths = struct.unpack_from("<%dI" % self.nscal, self.data, self.width_off) if self.nscal else ()
        self.block_starts = []
        p = self.blocks_off
        for s in self.block_sizes:
            self.block_starts.append(p)
            p += s

    @property
    def loops(self):
        return bool(self.Flags & 1)

    @property
    def one_key_per_frame(self):
        return bool(self.Flags & 2)

    def channel_map(self):
        """per rig channel (quats, vec3, floats): True = constant"""
        total = self.Q + self.CQ + self.V + self.CV + self.F + self.CF
        bits, val = [], False
        for i in range(self.ConstChanMapSize):
            bits.extend([val] * self.data[self.ccm_off + i])
            val = not val
        while len(bits) < total:
            bits.append(val)
        return bits[:total]

    def constants(self):
        d = self.data
        qs, vs, fs = self.QuatMax - self.QuatMin, self.Vec3Max - self.Vec3Min, self.FloatMax - self.FloatMin
        if self.ConstPaletteSize == 0:
            val = lambda i, s, mn: d[self.const_off + i] * (s * 0.003921569) + mn
        else:
            val = lambda i, s, mn: self.palette[d[self.const_off + i]] * s + mn
        q = []
        for j in range(self.CQ):
            e = np.array([val(4 * j + c, qs, self.QuatMin) for c in range(4)])
            n = np.linalg.norm(e) or 1.0
            q.append(e / n)
        nq = 4 * self.CQ
        v = [np.array([val(nq + 3 * j + c, vs, self.Vec3Min) for c in range(3)]) for j in range(self.CV)]
        f = [val(nq + 3 * self.CV + j, fs, self.FloatMin) for j in range(self.CF)]
        return q, v, f

    def unpack_block(self, bi):
        st, size = self.block_starts[bi], self.block_sizes[bi]
        d = self.data
        w = (d[st], d[st + 1], d[st + 2])
        body = d[st + 3:st + size][::-1]
        bits = int.from_bytes(body, "little")
        pos = 0
        coeff = np.zeros((8, self.lanes), dtype=np.int64)
        q4, v3, V = 4 * self.Q, 3 * self.V, self.V
        for s in range(self.nscal):
            wd = self.widths[s]
            nib = [(wd >> (4 * c)) & 0xF for c in range(8)]
            flags = [False] * 8
            for c in range(8):
                if nib[c]:
                    flags[c] = bool((bits >> pos) & 1)
                    pos += 1
            signs = [False] * 8
            for c in range(8):
                if flags[c]:
                    signs[c] = bool((bits >> pos) & 1)
                    pos += 1
            lane = s if s < q4 else (s + (s - q4) // 3 if s < q4 + v3 else s + V)
            for c in range(8):
                if flags[c]:
                    n = nib[c]
                    m = (bits >> pos) & ((1 << n) - 1)
                    pos += n
                    coeff[c, lane] = m if signs[c] else -m
        self.last_bits_used = pos
        self.last_bits_avail = 8 * len(body)
        return coeff, w

    def step_row(self, w):
        d = self.Dct
        bas = d * 3.0517578e-05
        fw = (w + 1.0) * (d * 0.2)
        return (fw * LN + 1.0) * bas

    def decode_block(self, bi):
        coeff, (w0, w1, w2) = self.unpack_block(bi)
        r0, r1, r2 = self.step_row(w0), self.step_row(w1), self.step_row(w2)
        mins = (self.QuatMin, self.TrajMin, self.Vec3Min, self.FloatMin)
        maxs = (self.QuatMax, self.TrajMax, self.Vec3Max, self.FloatMax)
        out = np.zeros((8, self.lanes))
        Q, V = self.Q, self.V
        for g in range(self.groups):
            rs = 0 if g < Q else (1 if g == Q else (2 if g == Q + 1 else 3))
            rng = 0 if g < Q else (1 if (g == Q and V != 0) else (2 if g < Q + V else 3))
            step = np.zeros((8, 4))
            for l in range(4):
                if rs == 0:
                    step[:, l] = r0
                elif rs == 1:
                    step[:, l] = r1
                elif rs == 2:
                    step[:, l] = r1 if l == 0 else r2
                else:
                    step[:, l] = r2
            c = coeff[:, 4 * g:4 * g + 4] * step
            dc = c[0] * 0.125 + 0.5
            acc = dc[None, :] + BASIS[:, 1:] @ c[1:]
            out[:, 4 * g:4 * g + 4] = acc * (maxs[rng] - mins[rng]) + mins[rng]
        return out

    def apply_ramp(self, keys, lane0, scale, off, size):
        d = self.data
        tab = lambda i: d[off + i] if i < size else 0
        nk = self.NumKeys
        sc = scale / 127.0
        i8 = lambda b: b - 256 if b > 127 else b
        for bi in range((nk + 7) >> 3):
            blk, u13 = bi, 0
            if blk != 0 and blk == (nk >> 3):
                blk -= 1
                u13 = nk & 7
            ch, p = 0, 1
            for _ in range(tab(0)):
                runs = tab(p + 1)
                ch += tab(p)
                r, rem = 0, blk
                if p + 3 < size and tab(p + 3) <= blk:
                    k = 1
                    while True:
                        if runs - 1 <= r:
                            break
                        r += 1
                        rem -= tab(p + k + 2)
                        k += 2
                        if p + k + 2 >= size or not (tab(p + k + 2) <= rem):
                            break
                v0 = i8(tab(p + 2 * r + 2)) * sc
                if r == runs - 1:
                    vals = [v0] * 8
                else:
                    v1 = i8(tab(p + 2 * r + 4)) * sc
                    dd = (v1 - v0) / (tab(p + 2 * r + 3) << 3)
                    x = (u13 + rem * 8) * dd + v0
                    vals = [x + j * dd for j in range(8)]
                for j in range(8):
                    kk = bi * 8 + j
                    if kk < keys.shape[0]:
                        keys[kk, lane0 + ch] += vals[j]
                p += runs + (runs - 1) + 2

    def decode_all_keys(self):
        nk = self.NumKeys
        nb = (nk + 7) >> 3
        allk = np.zeros((nb * 8, self.lanes))
        if self.nscal:
            for bi in range(nb):
                allk[bi * 8:bi * 8 + 8] = self.decode_block(bi)
        if self.VectorOffsetSize:
            self.apply_ramp(allk, 4 * self.Q, self.VectorOffsetScale, self.vos_off, self.VectorOffsetSize)
        if self.FloatOffsetSize:
            self.apply_ramp(allk, 4 * (self.Q + self.V), self.FloatOffsetScale, self.fos_off, self.FloatOffsetSize)
        return allk[:nk]

    def key_channels(self):
        """-> quats [K, Q+CQ, 4], vecs [K, V+CV, 3], floats [K, F+CF]"""
        k = self.decode_all_keys()
        K = k.shape[0]
        cq, cv, cf = self.constants()
        cmap = self.channel_map()
        nq, nv, nf = self.Q + self.CQ, self.V + self.CV, self.F + self.CF
        quats, vecs, flts = np.zeros((K, nq, 4)), np.zeros((K, nv, 3)), np.zeros((K, nf))
        ia = ic = 0
        for j in range(nq):
            if cmap[j]:
                quats[:, j] = cq[ic]; ic += 1
            else:
                quats[:, j] = k[:, 4 * ia:4 * ia + 4]; ia += 1
        ia = ic = 0
        for j in range(nv):
            if cmap[nq + j]:
                vecs[:, j] = cv[ic]; ic += 1
            else:
                b = 4 * self.Q + 4 * ia
                vecs[:, j] = k[:, b:b + 3]; ia += 1
        ia = ic = 0
        for j in range(nf):
            if cmap[nq + nv + j]:
                flts[:, j] = cf[ic]; ic += 1
            else:
                flts[:, j] = k[:, 4 * (self.Q + self.V) + ia]; ia += 1
        return quats, vecs, flts

    def frame_count(self):
        return (self.end_frame if self.end_frame is not None else self.NumKeys - 1) + 1

    def seek(self, t):
        """frame -> (keyA, keyB, alpha). Port of the game's seek over the key time RLE."""
        f = math.floor(t)
        frac = t - f
        u9 = max(int(f), 0)
        u13 = u9 + 1
        last = self.end_frame if self.end_frame is not None else max(self.NumKeys - 1, 0)
        if (self.Flags & 1) == 0:
            if last <= u9:
                u9 = last
            u10 = u9
            if last <= u13:
                u13 = last
        else:
            m = last + 1
            u10 = u9 % m
            u13 = u13 % m
        u9 = u10
        if self.Flags & 2:
            return u9, u13, frac
        p = self.data
        u8 = i11 = i12 = pb = 0
        if p[0] <= u9:
            while True:
                i7 = u8
                u8 = i7 + 1
                i12 = i11 + (p[pb] - 1)
                u9 = u10 + (1 - p[pb])
                u10 = u9
                if u9 < p[u8] + 1:
                    break
                u8 = i7 + 2
                i12 = i12 + 1
                u9 = u9 + (-1 - p[u8 - 1])
                u10 = u9
                pb = u8
                i11 = i12
                if not (p[u8] <= u9):
                    break
        u9 = u10
        b4 = (u8 & 1) == 0 and u9 < p[u8] - 1
        b3 = (u8 & 1) == 0 and u9 <= p[u8]
        nk = self.NumKeys
        u5 = (u9 if b3 else 0) + i12
        if b3:
            u10 = 0
        ka = u5
        if nk <= u5:
            ka = nk - 1
            frac = 0.0
        if u13 != 0:
            if not b4:
                frac = (u10 + frac) / (p[((u8 - 1) & 1) + u8] + 1)
            u13 = u5 + 1
            if nk <= u13:
                u13 = nk - 1
                frac = 0.0
        return ka, u13, frac

    def sample_frames(self, frames=None):
        quats, vecs, flts = self.key_channels()
        K = quats.shape[0]
        if frames is None:
            frames = range(self.frame_count())
        qo, vo, fo = [], [], []
        for fr in frames:
            a, b, al = self.seek(fr)
            a, b = min(max(a, 0), K - 1), min(max(b, 0), K - 1)
            if abs(al) < 1.5258789e-05:
                qo.append(quats[a]); vo.append(vecs[a]); fo.append(flts[a])
            else:
                q = quats[a] + (quats[b] - quats[a]) * al
                q = q / np.maximum(np.linalg.norm(q, axis=1, keepdims=True), 1e-12)
                qo.append(q); vo.append(vecs[a] + (vecs[b] - vecs[a]) * al); fo.append(flts[a] + (flts[b] - flts[a]) * al)
        return np.array(qo), np.array(vo), np.array(fo)
