"""Checkpoint A. Clip index of a Madden 19 ANT AssetBank -> animation_inventory.csv.

Every animation asset (VbrAnimationAsset, FrameAnimationAsset) with the ClipControllerAsset(s) that play it.
A clip's own name is the hex key of its controller (as in FIFA 17), so the readable name is the controller's.
Frame rate = controller FPS (FPS x TimeScale ticks at 60 Hz, as the FIFA 17 notes describe), frames = EndFrame + 1.

Usage
  py -3.13 -I inventory.py <bank> <out.csv>
"""
import csv
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gd  # noqa: E402

CLIP_CLASSES = ("VbrAnimationAsset", "FrameAnimationAsset", "RawAnimationAsset", "DctAnimationAsset")


def codec_tag(v):
    try:
        return struct.pack(">I", v).decode("ascii")
    except Exception:
        return "0x%08X" % v


def main(bank_path, out_csv):
    bk = gd.load(bank_path)
    idx, types = gd.build_key_index(bk)
    controllers = {}
    for i, n in enumerate(types):
        if n != "ClipControllerAsset":
            continue
        c = bk.decode(i)
        for role in ("Anim", "LayerAnim"):
            k = c.get(role)
            if k:
                controllers.setdefault(k, []).append((c["__name"], role, c.get("FPS"), c.get("TimeScale"), c.get("Distance"), c["__key"]))
    rows = []
    for i, n in enumerate(types):
        if n not in CLIP_CLASSES:
            continue
        o = bk.decode(i)
        base = o.get("__base") or {}
        ctrls = controllers.get(o["__key"], [])
        name = ctrls[0][0] if ctrls else o["__name"]
        fps = ctrls[0][2] if ctrls and ctrls[0][2] else 30.0
        end = base.get("EndFrame", 0) or 0
        frames = end + 1
        row = {
            "clip_key": "%016x" % o["__key"], "clip_name": o["__name"], "name": name,
            "controllers": len(ctrls), "role": ctrls[0][1] if ctrls else "",
            "class": n, "codec_id": codec_tag(base.get("CodecType", 0)), "codec_type": base.get("CodecType", 0),
            "frames": frames, "end_frame": end, "fps": fps, "duration_s": round(frames / fps, 4),
            "trim_offset": base.get("TrimOffset"), "trimmed_duration": base.get("TrimmedDuration"),
            "trimmed_duration_s": round((base.get("TrimmedDuration") or 0) / fps, 4),
            "time_scale": ctrls[0][3] if ctrls else "", "distance": ctrls[0][4] if ctrls else "",
            "additive": base.get("Additive"), "channel_to_dof": "%016x" % (base.get("ChannelToDofAsset") or 0),
            "dof_set_list": "%016x" % (base.get("DofSetList") or 0), "block": i,
        }
        if n == "VbrAnimationAsset":
            row.update({"num_keys": o.get("NumKeys"), "flags": o.get("Flags"), "quats": o.get("QuaternionCount", 0) + o.get("ConstQuaternionCount", 0),
                        "vec3": o.get("Vector3Count", 0) + o.get("ConstVector3Count", 0), "floats": o.get("FloatCount", 0) + o.get("ConstFloatCount", 0),
                        "data_bytes": len(o.get("Data") or b"")})
        else:
            row.update({"num_keys": 1, "flags": "", "quats": o.get("QuatCount", ""), "vec3": o.get("Vec3Count", ""), "floats": o.get("FloatCount", ""),
                        "data_bytes": 4 * len(o.get("Data") or [])})
        rows.append(row)
    rows.sort(key=lambda r: r["name"].lower())
    cols = list(rows[0].keys())
    with open(out_csv, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    named = sum(1 for r in rows if r["controllers"])
    print("clips", len(rows), "named by a controller", named, "->", out_csv)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
