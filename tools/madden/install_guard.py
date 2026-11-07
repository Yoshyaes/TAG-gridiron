"""Install guard for the Madden 19 reference pipeline.

Takes a snapshot of the game install (path, size, mtime for every file, plus
SHA256 for small control files) and compares snapshots. Never writes to the
install. Symbolic links are recorded as links and never followed.

Usage
  python -I install_guard.py snapshot <out.json> [--exclude-moddata]
  python -I install_guard.py compare <before.json> <after.json>
Exit code 0 means identical, 3 means a difference was found.
"""
import hashlib
import json
import os
import sys
import time

GAME = r"D:\Games\Madden NFL 19"
HASH_EXT = {".toc", ".cat", ".exe", ".dll", ".sb", ".json", ".cfg", ".buildsettings"}
HASH_MAX = 64 * 1024 * 1024


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def snapshot(exclude_moddata=False):
    entries = {}
    root_len = len(GAME) + 1
    for root, dirs, files in os.walk(GAME, followlinks=False):
        rel_root = root[root_len:]
        if exclude_moddata and (rel_root == "ModData" or rel_root.startswith("ModData" + os.sep)):
            dirs[:] = []
            continue
        if exclude_moddata and not rel_root:
            dirs[:] = [d for d in dirs if d != "ModData"]
        for name in dirs + files:
            p = os.path.join(root, name)
            rel = p[root_len:]
            if exclude_moddata and not rel_root and name == "CryptBase.dll":
                continue
            if os.path.islink(p):
                entries[rel] = {"link": os.readlink(p)}
                continue
            if name in dirs:
                continue
            st = os.lstat(p)
            e = {"size": st.st_size, "mtime": int(st.st_mtime)}
            if os.path.splitext(name)[1].lower() in HASH_EXT and st.st_size <= HASH_MAX:
                e["sha256"] = sha256(p)
            entries[rel] = e
    return entries


def main(argv):
    if len(argv) >= 3 and argv[1] == "snapshot":
        t = time.time()
        snap = snapshot("--exclude-moddata" in argv)
        with open(argv[2], "w") as fh:
            json.dump({"game": GAME, "taken": time.strftime("%Y-%m-%d %H:%M:%S"), "files": snap}, fh, indent=0, sort_keys=True)
        print("snapshot", len(snap), "entries in", round(time.time() - t, 1), "s")
        return 0
    if len(argv) == 4 and argv[1] == "compare":
        a = json.load(open(argv[2]))["files"]
        b = json.load(open(argv[3]))["files"]
        added = sorted(set(b) - set(a))
        removed = sorted(set(a) - set(b))
        changed = sorted(k for k in set(a) & set(b) if a[k] != b[k])
        for label, items in (("ADDED", added), ("REMOVED", removed), ("CHANGED", changed)):
            for k in items[:50]:
                print(label, k)
        print("added", len(added), "removed", len(removed), "changed", len(changed))
        return 3 if (added or removed or changed) else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
