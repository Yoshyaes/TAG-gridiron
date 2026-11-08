"""Re-imports repaired animation FBX files (key time fix) into Unreal in batches.

Walks the gated Unreal job lists of the full batch chunks in order and keeps the jobs whose source hash in
the manifest differs from logs/ue_state.json (every repaired file). Before a batch, the stale .uasset files
of that batch are deleted from disk (nothing outside their own folder references the full batch clips),
because a fresh import runs about twice as fast as Interchange's replace path. Each batch is one headless
Unreal run of at most BATCH clips, so every run stays under the 10 minute rule. A new batch starts only
while the elapsed time is under the budget. The install guard runs at the end of every window.

  python -I anim_reimport.py [budget seconds, default 60]
Exit 3 on an install guard change, 0 otherwise. Prints the remaining job count.
"""
import glob
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
LOGS = os.path.join(REPO, "logs")
CONTENT = os.path.join(REPO, "Unreal", "TAGGridiron", "Content")
EXTRACTED = os.path.join(REPO, "extracted")
BATCH = 230


def pending():
    state = json.load(open(os.path.join(LOGS, "ue_state.json")))
    manifest = json.load(open(os.path.join(EXTRACTED, "manifest.json"), encoding="utf-8"))["files"]
    out = []
    for p in sorted(glob.glob(os.path.join(LOGS, "ue_anim_full_*_gated.json"))):
        for j in json.load(open(p)):
            rel = j["file"].replace("{REPO}/extracted/", "")
            sha = manifest.get(rel, {}).get("sha256")
            if sha is None or state.get(j["dest"] + "/" + j["name"]) != sha:
                out.append(j)
    return out


def uasset(game_path):
    return os.path.join(CONTENT, *game_path[len("/Game/"):].split("/")) + ".uasset"


def main(argv):
    budget = float(argv[1]) if len(argv) > 1 else 60.0
    t0 = time.time()
    todo = pending()
    n = 0
    while todo and time.time() - t0 < budget:
        batch, todo = todo[:BATCH], todo[BATCH:]
        for j in batch:
            p = uasset(j["dest"] + "/" + j["name"])
            if os.path.exists(p):
                os.remove(p)
        path = os.path.join(LOGS, "reimport_batch.json")
        json.dump(batch, open(path, "w"), indent=1)
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "madden.py"), "ue", path, "reimport"], timeout=570)
        n += len(batch)
    rc = subprocess.run([sys.executable, "-I", os.path.join(HERE, "madden.py"), "guard"], timeout=300).returncode
    left = len(pending())
    print("reimported this window", n, "remaining", left, "guard rc", rc, "in %.0fs" % (time.time() - t0))
    return 3 if rc else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
