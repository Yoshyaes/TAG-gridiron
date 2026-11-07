"""Minimal binary FBX 7.x reader for export checks.

Reports unit scale, axis settings, model nodes, per geometry vertex and polygon counts,
bounding box (in FBX file units and in centimeters), skin deformer and cluster counts,
and limb (bone) node count.

Usage
  python -I fbxinfo.py <file.fbx> [--json]
"""
import json
import struct
import sys
import zlib


def _read_array(b, pos, code):
    n, enc, clen = struct.unpack_from("<III", b, pos)
    pos += 12
    raw = b[pos:pos + clen]
    pos += clen
    if enc == 1:
        raw = zlib.decompress(raw)
    fmt = {"f": "f", "d": "d", "l": "q", "i": "i", "b": "B"}[code]
    return list(struct.unpack("<%d%s" % (n, fmt), raw)), pos


def _read_prop(b, pos):
    code = chr(b[pos])
    pos += 1
    if code == "Y":
        return struct.unpack_from("<h", b, pos)[0], pos + 2
    if code == "C":
        return bool(b[pos]), pos + 1
    if code == "I":
        return struct.unpack_from("<i", b, pos)[0], pos + 4
    if code == "F":
        return struct.unpack_from("<f", b, pos)[0], pos + 4
    if code == "D":
        return struct.unpack_from("<d", b, pos)[0], pos + 8
    if code == "L":
        return struct.unpack_from("<q", b, pos)[0], pos + 8
    if code in "fdlib":
        return _read_array(b, pos, code)
    if code in "SR":
        n = struct.unpack_from("<I", b, pos)[0]
        pos += 4
        raw = b[pos:pos + n]
        return (raw.decode("utf-8", "replace") if code == "S" else raw), pos + n
    raise ValueError("bad property code %r at %d" % (code, pos))


def parse(path):
    b = open(path, "rb").read()
    if not b.startswith(b"Kaydara FBX Binary"):
        raise ValueError("not a binary FBX")
    version = struct.unpack_from("<I", b, 23)[0]
    wide = version >= 7500
    hdr = 25 if wide else 13

    def node(pos):
        if wide:
            end, nprops, plen = struct.unpack_from("<QQQ", b, pos)
        else:
            end, nprops, plen = struct.unpack_from("<III", b, pos)
        nlen = b[pos + hdr - 1]
        if end == 0:
            return None, pos + hdr
        name = b[pos + hdr:pos + hdr + nlen].decode("ascii", "replace")
        p = pos + hdr + nlen
        props = []
        for _ in range(nprops):
            v, p = _read_prop(b, p)
            props.append(v)
        children = []
        while p < end:
            c, p = node(p)
            if c is None:
                break
            children.append(c)
        return {"name": name, "props": props, "children": children}, end

    pos = 27
    top = []
    while pos < len(b) - hdr:
        n, pos = node(pos)
        if n is None:
            break
        top.append(n)
    return version, top


def find(nodes, name):
    for n in nodes:
        if n["name"] == name:
            yield n


def info(path):
    version, top = parse(path)
    out = {"file": path, "version": version}
    gs = next(find(top, "GlobalSettings"), None)
    if gs:
        p70 = next(find(gs["children"], "Properties70"), None)
        for p in (p70["children"] if p70 else []):
            if p["props"] and p["props"][0] in ("UnitScaleFactor", "OriginalUnitScaleFactor", "UpAxis", "UpAxisSign", "FrontAxis", "CoordAxis"):
                out[p["props"][0]] = p["props"][-1]
    objs = next(find(top, "Objects"), {"children": []})["children"]
    models, geoms, deformers = [], [], {"Skin": 0, "Cluster": 0}
    limbs = 0
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    for o in objs:
        if o["name"] == "Model":
            mname = o["props"][1].split("\x00")[0] if len(o["props"]) > 1 else ""
            mtype = o["props"][2] if len(o["props"]) > 2 else ""
            models.append({"name": mname, "type": mtype})
            if mtype == "LimbNode":
                limbs += 1
        elif o["name"] == "Geometry":
            gname = o["props"][1].split("\x00")[0] if len(o["props"]) > 1 else ""
            verts = next(find(o["children"], "Vertices"), None)
            polys = next(find(o["children"], "PolygonVertexIndex"), None)
            v = verts["props"][0] if verts else []
            nv = len(v) // 3
            for i in range(nv):
                for k in range(3):
                    x = v[i * 3 + k]
                    lo[k] = min(lo[k], x)
                    hi[k] = max(hi[k], x)
            npoly = sum(1 for x in (polys["props"][0] if polys else []) if x < 0)
            uv = sum(1 for _ in find(o["children"], "LayerElementUV"))
            geoms.append({"name": gname, "vertices": nv, "polygons": npoly, "uv_sets": uv})
        elif o["name"] == "Deformer":
            dtype = o["props"][2] if len(o["props"]) > 2 else ""
            if dtype in deformers:
                deformers[dtype] += 1
    unit = out.get("UnitScaleFactor", 1.0) or 1.0
    out["models"] = models
    out["geometries"] = geoms
    out["deformers"] = deformers
    out["limb_nodes"] = limbs
    if geoms and lo[0] != float("inf"):
        out["bbox_file_units"] = [round(h - l, 5) for l, h in zip(lo, hi)]
        out["bbox_cm"] = [round((h - l) * unit, 3) for l, h in zip(lo, hi)]
    return out


if __name__ == "__main__":
    r = info(sys.argv[1])
    if "--json" in sys.argv:
        print(json.dumps(r, indent=1))
    else:
        print("version", r["version"], "unit", r.get("UnitScaleFactor"), "up", r.get("UpAxis"), "bbox_cm", r.get("bbox_cm"))
        print("deformers", r["deformers"], "limbs", r["limb_nodes"])
        for g in r["geometries"]:
            print("  geom", g)
        print("  models", [(m["name"], m["type"]) for m in r["models"]])
