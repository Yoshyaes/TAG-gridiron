"""Runs pending Checkpoint E animation chunks in order inside one time window (overnight rules: no command
past 10 minutes). A chunk counts as done when its kit result has every step at 0 and ends with the audit.

  python -I anim_driver.py [budget seconds, default 270] [first] [last]

Starts a chunk only while the elapsed time is under the budget, so a window is at most budget plus one
chunk. Exit 3 if the install guard reported a change (stop everything), 0 otherwise. Prints a summary line.
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


def done(name):
    p = os.path.join(LOGS, "kit_%s_result.json" % name)
    if not os.path.exists(p):
        return False
    r = json.load(open(p))
    steps = dict((s[0], s[1]) for s in r.get("steps", []))
    return steps.get("audit") == 0 and all(v == 0 for v in steps.values())


def main(argv):
    budget = float(argv[1]) if len(argv) > 1 else 270.0
    first = int(argv[2]) if len(argv) > 2 else 0
    last = int(argv[3]) if len(argv) > 3 else 10 ** 6
    specs = sorted(glob.glob(os.path.join(HERE, "kits", "anim_full_*.json")))
    t0 = time.time()
    ran = []
    for sp in specs:
        name = os.path.splitext(os.path.basename(sp))[0]
        idx = int(name.rsplit("_", 1)[1])
        if idx < first or idx > last or done(name):
            continue
        if time.time() - t0 > budget:
            break
        log = os.path.join(LOGS, "kit_%s_driver.log" % name)
        with open(log, "a", encoding="utf-8") as fh:
            try:
                rc = subprocess.run([sys.executable, "-I", os.path.join(HERE, "madden.py"), "kit", sp], stdout=fh,
                                    stderr=subprocess.STDOUT, timeout=570).returncode
            except subprocess.TimeoutExpired:
                rc = -9
        ran.append((name, rc, round(time.time() - t0)))
        if rc == 3:
            print("GUARD CHANGE in", name, "- stopping")
            print(json.dumps(ran))
            return 3
    pending = [os.path.splitext(os.path.basename(s))[0] for s in specs if not done(os.path.splitext(os.path.basename(s))[0])]
    print("ran", json.dumps(ran), "pending", len(pending), "next", pending[:1])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
