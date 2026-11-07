r"""Orchestrator for the private Madden 19 reference pipeline (TAG Gridiron).

Every command is idempotent. Exports land in extracted\ (a junction to D:\\MaddenRef\\extracted),
which is gitignored. The game install is only read. Each external process call has a timeout of
at most 540 seconds so no single step runs past the 10 minute rule.

Commands
  init                      write extracted\\README.md, check the junction
  guard                     snapshot the install and compare with logs\\guard_baseline.json (exit 3 on change)
  diskcheck <bytes>         exit 4 if D: free space minus <bytes> would fall under the 50 GB floor
  inventory                 FrostyHost inventory into extracted\\inventory, then the summary
  host <jobs.json> <stage>  run FrostyHost jobs in chunks, merge manifest.json and failures.csv
  ue <jobs.json> <stage>    headless Unreal import (maddenref_import.py)
  materials <spec> <stage>  headless reference materials and preview level (maddenref_materials.py)
  audit <stage>             headless MaddenRef audit (maddenref_audit.py), exit 5 on fail
  verify                    manifest counts and hashes against the disk, exit 6 on mismatch
  kit <spec.json>           run one kit end to end (plan, export, texfix, material JSON, UE import,
                            materials, verify, guard, audit). Exit 3 on install change, 4 on disk floor
"""
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOLS = os.path.join(REPO, "tools", "madden")
EXTRACTED = os.path.join(REPO, "extracted")
LOGS = os.path.join(REPO, "logs")
HOST = os.path.join(TOOLS, "FrostyHost", "bin", "FrostyHost.exe")
UE_CMD = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
UPROJECT = os.path.join(REPO, "Unreal", "TAGGridiron", "TAGGridiron.uproject")
ART = os.path.join(REPO, "Unreal", "TAGGridiron", "Tools", "Art")
MANIFEST = os.path.join(EXTRACTED, "manifest.json")
FAILURES = os.path.join(EXTRACTED, "failures.csv")
FLOOR = 50 * 1024 ** 3
csv.field_size_limit(sys.maxsize)
STEP_TIMEOUT = 540
CHUNK = 40
EXCLUDE_NAMES = {"codex.cfg", "orangeemu64.dll"}

README = """# extracted

Private reference material pulled from Madden NFL 19 for the TAG Gridiron project.

These assets belong to Electronic Arts and the NFL. They are private reference only.

- This folder is gitignored and must never be pushed to any remote or cloud repository.
- Nothing in this folder, or imported from it into Unreal under /Game/MaddenRef, ships in any
  TAG Gridiron build. /Game/MaddenRef is listed in DirectoriesToNeverCook.
- Production assets are rebuilt or replaced with original or licensed work.
- Every file here is reproducible by tools/madden/madden.py and the job files in tools/madden/jobs.
  Do not hand edit anything here.
"""


def log(msg):
    line = time.strftime("%H:%M:%S ") + msg
    print(line, flush=True)
    os.makedirs(LOGS, exist_ok=True)
    with open(os.path.join(LOGS, "madden.log"), "a", encoding="utf-8") as fh:
        fh.write(line + "\n")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_manifest():
    if os.path.exists(MANIFEST):
        return json.load(open(MANIFEST, encoding="utf-8"))
    return {"files": {}}


def save_manifest(m):
    tmp = MANIFEST + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(m, fh, indent=1, sort_keys=True)
    os.replace(tmp, MANIFEST)


def rel(path):
    return os.path.relpath(os.path.abspath(path), EXTRACTED).replace("\\", "/")


_inventory = None


def bundle_lookup(asset):
    """Bundles and superbundles of an EBX asset, from the Stage 1 inventory when present."""
    global _inventory
    if _inventory is None:
        _inventory = {}
        p = os.path.join(EXTRACTED, "inventory", "inventory.csv")
        if os.path.exists(p):
            with open(p, encoding="utf-8") as fh:
                for row in csv.DictReader(fh):
                    if row["kind"] == "ebx":
                        b = row["bundles"].split(";")
                        short = ";".join(b[:8]) + (";+%d more" % (len(b) - 8) if len(b) > 8 else "")
                        _inventory[row["path"].lower()] = (short, row["superbundles"])
    return _inventory.get((asset or "").lower(), ("", ""))


def append_failure(stage, asset, op, error):
    new = not os.path.exists(FAILURES)
    with open(FAILURES, "a", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        if new:
            w.writerow(["timestamp", "stage", "asset", "op", "error"])
        w.writerow([time.strftime("%Y-%m-%d %H:%M:%S"), stage, asset, op, error])


def load_jobs(path):
    """Job files use {REPO} for the repo root so they stay portable."""
    text = open(path, encoding="utf-8").read().replace("{REPO}", REPO.replace("\\", "/"))
    return json.loads(text)


def outputs_of(job):
    if job["op"] == "texture":
        return [job["out"] + "." + f for f in job.get("formats", ["dds", "png"])]
    if job.get("out"):
        return [job["out"]]
    return []


def cmd_init():
    os.makedirs(EXTRACTED, exist_ok=True)
    with open(os.path.join(EXTRACTED, "README.md"), "w", encoding="utf-8") as fh:
        fh.write(README)
    real = os.path.realpath(EXTRACTED)
    log("init extracted -> " + real)
    return 0


def cmd_guard():
    base = os.path.join(LOGS, "guard_baseline.json")
    now = os.path.join(LOGS, "guard_latest.json")
    g = os.path.join(TOOLS, "install_guard.py")
    subprocess.run([sys.executable, "-I", g, "snapshot", now], check=True, timeout=STEP_TIMEOUT)
    p = subprocess.run([sys.executable, "-I", g, "compare", base, now], capture_output=True, text=True, timeout=STEP_TIMEOUT)
    log("guard " + p.stdout.strip().splitlines()[-1])
    if p.returncode != 0:
        log("GUARD CHANGE DETECTED\n" + p.stdout)
    return p.returncode


def cmd_diskcheck(projected):
    free = shutil.disk_usage(os.path.realpath(EXTRACTED)).free
    ok = free - projected >= FLOOR
    log("diskcheck free=%.1fGB projected=%.2fGB floor=50GB -> %s" % (free / 1024 ** 3, projected / 1024 ** 3, "ok" if ok else "SKIP"))
    return 0 if ok else 4


def run_host(args, logname):
    env = dict(os.environ)
    with open(os.path.join(LOGS, logname), "a", encoding="utf-8") as fh:
        p = subprocess.run([HOST] + args, stdout=fh, stderr=subprocess.STDOUT, timeout=STEP_TIMEOUT, env=env)
    return p.returncode


def cmd_inventory():
    out = os.path.join(EXTRACTED, "inventory")
    if os.path.exists(os.path.join(out, "inventory.csv")):
        log("inventory exists, skipping export")
    else:
        rc = run_host(["inventory", out], "inventory_host.log")
        if rc != 0:
            log("inventory host failed rc=%d" % rc)
            return rc
    import inventory_summary
    inventory_summary.write(out)
    return 0


def cmd_host(jobs_path, stage):
    jobs = load_jobs(jobs_path)
    m = load_manifest()
    todo = []
    for j in jobs:
        outs = outputs_of(j)
        done = outs and all(os.path.exists(o) and m["files"].get(rel(o), {}).get("sha256") == sha256(o) for o in outs)
        if done:
            continue
        j = dict(j)
        j["skip_existing"] = False
        todo.append(j)
    log("host %s jobs=%d todo=%d" % (stage, len(jobs), len(todo)))
    results_path = os.path.join(LOGS, "results_%s.jsonl" % stage)
    fails = 0
    for i in range(0, len(todo), CHUNK):
        chunk = todo[i:i + CHUNK]
        cpath = os.path.join(LOGS, "chunk_%s.json" % stage)
        json.dump(chunk, open(cpath, "w", encoding="utf-8"))
        start = sum(1 for _ in open(results_path, encoding="utf-8")) if os.path.exists(results_path) else 0
        try:
            rc = run_host(["run", cpath, results_path], "host_%s.log" % stage)
        except subprocess.TimeoutExpired:
            rc = -1
            log("host chunk timed out at %d" % i)
        lines = open(results_path, encoding="utf-8").read().splitlines()[start:] if os.path.exists(results_path) else []
        seen = set()
        byid = {j.get("id"): j for j in chunk}
        for line in lines:
            r = json.loads(line)
            seen.add(r.get("id"))
            job = byid.get(r.get("id"), {})
            if r["status"] == "fail":
                fails += 1
                append_failure(stage, r.get("asset"), r.get("op"), r.get("error"))
                continue
            for o in r.get("outputs", []) or []:
                if not os.path.exists(o):
                    continue
                bundles, sbs = bundle_lookup(job.get("asset"))
                entry = {"source_asset": job.get("asset"), "op": job.get("op"), "stage": stage,
                         "bundles": bundles, "superbundles": sbs, "size": os.path.getsize(o), "sha256": sha256(o),
                         "params": {k: v for k, v in job.items() if k not in ("out", "asset", "op", "id", "skip_existing")}}
                for k in ("pixel_format", "width", "height", "mips", "mesh_type", "tex_type", "slices"):
                    if k in r:
                        entry[k] = r[k]
                if "meta" in job:
                    entry["meta"] = job["meta"]
                m["files"][rel(o)] = entry
        for j in chunk:
            if j.get("id") not in seen:
                fails += 1
                append_failure(stage, j.get("asset"), j.get("op"), "no result (host rc=%s, crash or timeout)" % rc)
        save_manifest(m)
    log("host %s done fails=%d" % (stage, fails))
    return 0


def run_ue(script, env_extra, logname):
    env = dict(os.environ)
    env.update(env_extra)
    with open(os.path.join(LOGS, logname), "w", encoding="utf-8") as fh:
        p = subprocess.run([UE_CMD, UPROJECT, "-run=pythonscript", "-script=" + os.path.join(ART, script),
                            "-unattended", "-nosplash", "-nullrhi", "-stdout"],
                           stdout=fh, stderr=subprocess.STDOUT, timeout=STEP_TIMEOUT, env=env)
    return p.returncode


UE_STATE = os.path.join(LOGS, "ue_state.json")
CONTENT = os.path.join(REPO, "Unreal", "TAGGridiron", "Content")


def uasset_path(game_path):
    return os.path.join(CONTENT, *game_path[len("/Game/"):].split("/")) + ".uasset"


def cmd_ue(jobs_path, stage):
    """Headless import. A job is skipped when its .uasset exists and the source file hash matches the
    hash recorded at its last successful import (logs/ue_state.json)."""
    report = os.path.join(LOGS, "ue_%s_report.json" % stage)
    expanded = os.path.join(LOGS, "ue_%s_jobs.json" % stage)
    state = json.load(open(UE_STATE)) if os.path.exists(UE_STATE) else {}
    todo = []
    for j in load_jobs(jobs_path):
        key = j["dest"] + "/" + j["name"]
        if os.path.exists(uasset_path(key)) and os.path.exists(j["file"]) and state.get(key) == sha256(j["file"]):
            continue
        # Skeleton bootstrap job (its mesh is named after the skeleton). Needed once, whichever kit runs first.
        if j.get("skeleton") == key + "_Skeleton" and os.path.exists(uasset_path(j["skeleton"])):
            continue
        todo.append(j)
    log("ue %s todo=%d" % (stage, len(todo)))
    if not todo:
        return 0
    json.dump(todo, open(expanded, "w", encoding="utf-8"), indent=1)
    rc = run_ue("maddenref_import.py", {"MADDENREF_JOBS": expanded, "MADDENREF_REPORT": report}, "ue_%s.log" % stage)
    if not os.path.exists(report):
        log("ue %s produced no report rc=%d" % (stage, rc))
        return 7
    r = json.load(open(report))
    bad = [x for x in r["results"] if x["status"] != "ok"]
    for x in bad:
        append_failure(stage, x["job"].get("file"), "ue_import", x.get("error"))
    for x in r["results"]:
        if x["status"] == "ok":
            state[x["job"]["dest"] + "/" + x["job"]["name"]] = sha256(x["job"]["file"])
    json.dump(state, open(UE_STATE, "w"), indent=1, sort_keys=True)
    log("ue %s ok=%d fail=%d" % (stage, len(r["results"]) - len(bad), len(bad)))
    return 0


def cmd_materials(spec_path, stage):
    report = os.path.join(LOGS, "mat_%s_report.json" % stage)
    if os.path.exists(report):
        os.remove(report)
    run_ue("maddenref_materials.py", {"MADDENREF_MATERIALS": os.path.abspath(spec_path), "MADDENREF_REPORT": report}, "mat_%s.log" % stage)
    if not os.path.exists(report):
        log("materials %s produced no report" % stage)
        return 7
    r = json.load(open(report))
    bad = [x for x in r["results"] if x["status"] != "ok"]
    for x in bad:
        append_failure(stage, x.get("binding", {}).get("mesh"), "ue_material", x.get("error"))
    log("materials %s ok=%d fail=%d preview=%s" % (stage, len(r["results"]) - len(bad), len(bad), r.get("preview")))
    return 0


def cmd_uereport(jobs_path, stage):
    """Report only Unreal run (bounds, bones, skeleton) for spot checks. Bypasses the import skip state."""
    report = os.path.join(LOGS, "uereport_%s.json" % stage)
    expanded = os.path.join(LOGS, "uereport_%s_jobs.json" % stage)
    json.dump(load_jobs(jobs_path), open(expanded, "w", encoding="utf-8"), indent=1)
    run_ue("maddenref_import.py", {"MADDENREF_JOBS": expanded, "MADDENREF_REPORT": report}, "uereport_%s.log" % stage)
    return 0 if os.path.exists(report) else 7


def cmd_register(stage, op, paths):
    """Record derived files (made by tools/madden/antbank scripts) in the manifest."""
    m = load_manifest()
    for p in paths:
        p = os.path.abspath(p)
        m["files"][rel(p)] = {"op": op, "stage": stage, "source_asset": "", "bundles": "", "superbundles": "",
                              "size": os.path.getsize(p), "sha256": sha256(p), "params": {}}
    save_manifest(m)
    log("register %s %s %d files" % (stage, op, len(paths)))
    return 0


def cmd_audit(stage):
    out = os.path.join(LOGS, "audit_%s.json" % stage)
    if os.path.exists(out):
        os.remove(out)
    run_ue("maddenref_audit.py", {"MADDENREF_AUDIT": out}, "audit_%s.log" % stage)
    if not os.path.exists(out):
        log("audit %s produced no report" % stage)
        return 5
    r = json.load(open(out))
    log("audit %s %s packages=%d problems=%s" % (stage, r["status"], r["maddenref_packages"], r["problems"]))
    return 0 if r["status"] == "pass" else 5


PY313 = ["py", "-3.13", "-I"]


def cmd_animkit(spec_path, spec):
    """Animation kit: plan, disk floor, decode and FBX with plausibility gate, UE import, preview, verify, guard, audit."""
    name = spec["name"]
    ak = PY313 + [os.path.join(TOOLS, "antbank", "animkit.py")]
    result = {"kit": name, "kind": "anim", "steps": []}

    def step(label, rc):
        result["steps"].append([label, rc])
        log("kit %s step %s rc=%s" % (name, label, rc))
        return rc

    def save():
        json.dump(result, open(os.path.join(LOGS, "kit_%s_result.json" % name), "w"), indent=1)

    if step("plan", subprocess.run(ak + ["plan", spec_path], timeout=STEP_TIMEOUT).returncode):
        save()
        return 1
    summary = json.load(open(os.path.join(LOGS, "kit_%s_summary.json" % name)))
    result["summary"] = summary
    for n in summary.get("missing", []):
        append_failure(name, n, "anim_plan", "controller name not found among body clips")
    if step("diskcheck", cmd_diskcheck(summary["projected_bytes"])):
        result["skipped"] = "projected size would break the 50 GB floor on D"
        save()
        return 4
    # Unreal assets land on C, which is 95 percent full. Own safety floor, 40 GB free after the chunk.
    ue_bytes = summary.get("frames", 0) * 10 * 1024
    c_free = shutil.disk_usage(CONTENT).free
    if c_free - ue_bytes < 40 * 1024 ** 3:
        log("C floor: free %.1f GB, chunk needs %.2f GB, floor 40 GB -> SKIP" % (c_free / 1024 ** 3, ue_bytes / 1024 ** 3))
        result["skipped"] = "projected Unreal size would break the 40 GB floor on C"
        save()
        return 4
    if step("export", subprocess.run(ak + ["export", spec_path], timeout=STEP_TIMEOUT).returncode):
        save()
        return 1
    step("ue", cmd_ue(os.path.join(LOGS, "ue_%s_gated.json" % name), name))
    step("mat", subprocess.run(ak + ["mat", spec_path], timeout=STEP_TIMEOUT).returncode)
    if spec.get("preview"):
        step("preview", cmd_materials(os.path.join(TOOLS, "jobs", "mat_" + name + ".json"), name))
    step("verify", cmd_verify(name if spec.get("quick_verify") else None))
    g = step("guard", cmd_guard())
    save()
    if g != 0:
        return 3
    step("audit", cmd_audit(name))
    save()
    return 0


def cmd_kit(spec_path):
    """Runs one kit end to end with gates. Exit 3 means the install guard saw a change (stop everything)."""
    spec = json.load(open(spec_path, encoding="utf-8"))
    if spec.get("kind") == "anim":
        return cmd_animkit(spec_path, spec)
    name = spec["name"]
    kit = [sys.executable, "-I", os.path.join(TOOLS, "kit.py")]
    result = {"kit": name, "steps": []}

    def step(label, rc):
        result["steps"].append([label, rc])
        log("kit %s step %s rc=%s" % (name, label, rc))
        return rc

    def save():
        json.dump(result, open(os.path.join(LOGS, "kit_%s_result.json" % name), "w"), indent=1)

    if step("plan", subprocess.run(kit + ["plan", spec_path], timeout=STEP_TIMEOUT).returncode):
        save()
        return 1
    step("host_plan", cmd_host(os.path.join(TOOLS, "jobs", name + "_plan.json"), name + "_plan"))
    step("plan2", subprocess.run(kit + ["plan2", spec_path], timeout=STEP_TIMEOUT).returncode)
    step("host_plan2", cmd_host(os.path.join(TOOLS, "jobs", name + "_plan2.json"), name + "_plan2"))
    if step("jobs", subprocess.run(kit + ["jobs", spec_path], timeout=STEP_TIMEOUT).returncode):
        save()
        return 1
    summary = json.load(open(os.path.join(LOGS, "kit_%s_summary.json" % name)))
    result["summary"] = summary
    if step("diskcheck", cmd_diskcheck(summary["projected_bytes"])):
        result["skipped"] = "projected size would break the 50 GB floor on D"
        save()
        return 4
    step("host", cmd_host(os.path.join(TOOLS, "jobs", name + ".json"), name))
    tex_dir = os.path.join(EXTRACTED, spec["out"], "textures")
    if os.path.isdir(tex_dir):
        step("texfix", subprocess.run(PY313 + [os.path.join(TOOLS, "texfix.py"), tex_dir], timeout=STEP_TIMEOUT).returncode)
    step("matdesc", subprocess.run(kit + ["matdesc", spec_path], timeout=STEP_TIMEOUT).returncode)
    step("mat", subprocess.run(kit + ["mat", spec_path], timeout=STEP_TIMEOUT).returncode)
    step("ue", cmd_ue(os.path.join(TOOLS, "jobs", "ue_" + name + ".json"), name))
    step("materials", cmd_materials(os.path.join(TOOLS, "jobs", "mat_" + name + ".json"), name))
    step("verify", cmd_verify())
    g = step("guard", cmd_guard())
    save()
    if g != 0:
        return 3
    step("audit", cmd_audit(name))
    save()
    return 0


def cmd_verify(stage=None):
    """Full verify hashes every file. With a stage (quick mode, used per animation chunk) only that stage's
    files are hashed and the rest are checked by size. A full verify runs at the end of a checkpoint."""
    m = load_manifest()
    missing, bad = [], []
    for k, e in m["files"].items():
        p = os.path.join(EXTRACTED, k)
        if not os.path.exists(p):
            missing.append(k)
        elif stage is not None and e.get("stage") != stage:
            if os.path.getsize(p) != e.get("size"):
                bad.append(k)
        elif sha256(p) != e["sha256"]:
            bad.append(k)
    on_disk = 0
    for root, dirs, files in os.walk(EXTRACTED):
        dirs[:] = [d for d in dirs if d not in ("inventory",)]
        for f in files:
            if f.lower() in EXCLUDE_NAMES:
                log("EXCLUDED FILE PRESENT " + os.path.join(root, f))
                bad.append(f)
            if f in ("manifest.json", "failures.csv", "README.md") and root == EXTRACTED:
                continue
            if f.endswith((".partial.fbx", ".tmp")) or ".partial." in f:
                continue
            on_disk += 1
    log("verify manifest=%d on_disk=%d missing=%d hash_mismatch=%d" % (len(m["files"]), on_disk, len(missing), len(bad)))
    return 0 if not missing and not bad and on_disk == len(m["files"]) else 6


def main(argv):
    os.makedirs(LOGS, exist_ok=True)
    sys.path.insert(0, TOOLS)
    c = argv[1] if len(argv) > 1 else ""
    if c == "init":
        return cmd_init()
    if c == "guard":
        return cmd_guard()
    if c == "diskcheck":
        return cmd_diskcheck(int(float(argv[2])))
    if c == "inventory":
        return cmd_inventory()
    if c == "host":
        return cmd_host(argv[2], argv[3])
    if c == "ue":
        return cmd_ue(argv[2], argv[3])
    if c == "materials":
        return cmd_materials(argv[2], argv[3])
    if c == "uereport":
        return cmd_uereport(argv[2], argv[3])
    if c == "register":
        return cmd_register(argv[2], argv[3], argv[4:])
    if c == "audit":
        return cmd_audit(argv[2])
    if c == "kit":
        return cmd_kit(argv[2])
    if c == "verify":
        return cmd_verify(argv[argv.index("--stage") + 1] if "--stage" in argv else None)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
