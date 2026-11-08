"""Repairs KeyTime arrays in animation FBX files written before the fbxanim fix (2026-11-07).

The old writer printed int64 tick times through %.7g ("1.539539e+9"). FBX readers parse that as 1, so all
keys collapsed onto the first ticks and Unreal held the last pose. Key times are fully determined by the
frame index and the written rate (tick = round(i * 46186158000 / fps)), so only the "a:" line of every
KeyTime block is rewritten, as the fixed writer would print it. Nothing else in the file changes.

Each block is checked first. Its count must equal the frame count, and every old value must agree with
the new tick within the %.7g rounding (relative 1e-6), otherwise the file is reported and left untouched.
Manifest entries (size, sha256) are updated. Idempotent (files already in integer form are skipped).

Usage
  py -3.13 -I fix_keytimes.py <dir> [<dir> ...]
"""
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import madden  # noqa: E402

KTIME = 46186158000
BLOCK = re.compile(r"(KeyTime: \*(\d+) \{\n\t\t\ta: )([^\n]*)(\n)")


def fix_file(fbx, fps):
    text = open(fbx, encoding="utf-8").read()
    if "e+" not in text.split("KeyTime", 1)[-1][:400]:
        return "already"
    cache = {}

    def repl(m):
        n = int(m.group(2))
        if n not in cache:
            cache[n] = [int(round(i * KTIME / fps)) for i in range(n)]
        new = cache[n]
        old = [float(x) for x in m.group(3).split(",")]
        if len(old) != n:
            raise ValueError("count mismatch")
        for o, t in zip(old, new):
            if abs(o - t) > max(1.0, 1e-6 * t):
                raise ValueError("old key %r does not match tick %d at %s fps" % (o, t, fps))
        return m.group(1) + ",".join(str(t) for t in new) + m.group(4)

    fixed, count = BLOCK.subn(repl, text)
    if count == 0:
        raise ValueError("no KeyTime blocks")
    tmp = fbx + ".partial"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(fixed)
    os.replace(tmp, fbx)
    return "fixed"


def main(dirs):
    t0 = time.time()
    m = madden.load_manifest()
    stats = {"fixed": 0, "already": 0, "error": 0}
    errors = []
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in sorted(files):
                if not f.endswith(".fbx"):
                    continue
                fbx = os.path.join(root, f)
                meta = fbx[:-4] + ".json"
                try:
                    fps = float(json.load(open(meta))["fps"])
                    r = fix_file(fbx, fps)
                    stats[r] += 1
                    key = madden.rel(fbx)
                    if r == "fixed" and key in m["files"]:
                        m["files"][key]["size"] = os.path.getsize(fbx)
                        m["files"][key]["sha256"] = madden.sha256(fbx)
                        m["files"][key].setdefault("params", {})["keytime_fix"] = "2026-11-07"
                except Exception as e:
                    stats["error"] += 1
                    errors.append((fbx, repr(e)[:200]))
            if time.time() - t0 > 480:
                madden.save_manifest(m)
                print("time budget reached, rerun to continue", stats)
                return 2
    madden.save_manifest(m)
    print("keytime fix", stats, "in %.0fs" % (time.time() - t0))
    for e in errors[:20]:
        print("ERROR", e)
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
