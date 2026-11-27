"""Reference only project on D, and the C drive cleanup that moving the full animation batch allows.

The REFERENCE ONLY project D:\\MaddenRef\\MaddenRef_ReferenceOnly holds /Game/MaddenRef/Animations/Full, the shared
skeleton and the import pipelines at the same package paths as TAGGridiron. It is outside the repo and has a
.gitignore that ignores everything.

  python -I ref_project.py verifycopy      size and SHA256 of every copied file against the TAGGridiron original
  python -I ref_project.py removefull      delete Animations/Full from TAGGridiron, only when every gate passed
  python -I ref_project.py cleanimport     delete Saved/MaddenRefImport temp copies the manifest confirms on D

Every step works under a 480 s budget and is safe to rerun (it continues where it stopped). Deletions are file
by file, never recursive, never through a junction or symbolic link, and every removal is logged as a CSV row.
Exit 0 done, 2 budget reached (rerun), 1 refused or failed.
"""
import csv
import datetime
import glob
import hashlib
import json
import os
import stat
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
LOGS = os.path.join(REPO, "logs")
MAIN_CONTENT = os.path.join(REPO, "Unreal", "TAGGridiron", "Content")
REF_PROJECT = r"D:\MaddenRef\MaddenRef_ReferenceOnly"
REF_CONTENT = os.path.join(REF_PROJECT, "Content")
COPIED = [os.path.join("MaddenRef", "Animations", "Full"), os.path.join("MaddenRef", "Skeleton"), os.path.join("MaddenRef", "_Pipelines")]
FULL = os.path.join(MAIN_CONTENT, "MaddenRef", "Animations", "Full")
IMPORT_TMP = os.path.join(REPO, "Unreal", "TAGGridiron", "Saved", "MaddenRefImport")
BUDGET = 480
HASHES = os.path.join(LOGS, "ref_copy_hashes.json")
COPY_REPORT = os.path.join(LOGS, "ref_copy_verify.json")
# Gate reports removefull requires, all written by earlier steps.
GATES = {
    "copy": COPY_REPORT,
    "sample": os.path.join(LOGS, "ref_sample_compare.json"),
    "audit_ref": os.path.join(LOGS, "audit_ref_project.json"),
    "audit_main": os.path.join(LOGS, "audit_main_before_removal.json"),
    "refs_main": os.path.join(LOGS, "full_refs_main.json"),
}


def sha256(p):
    h = hashlib.sha256()
    with open(p, "rb") as fh:
        for b in iter(lambda: fh.read(1 << 22), b""):
            h.update(b)
    return h.hexdigest()


def is_link(p):
    """True for symbolic links and junctions (any reparse point)."""
    st = os.lstat(p)
    return stat.S_ISLNK(st.st_mode) or bool(getattr(st, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def no_link_on_path(p, stop):
    """Checks p and every parent up to and including stop for reparse points."""
    p = os.path.abspath(p)
    stop = os.path.abspath(stop)
    while True:
        if is_link(p):
            return False
        if os.path.normcase(p) == os.path.normcase(stop):
            return True
        parent = os.path.dirname(p)
        if parent == p:
            return False
        p = parent


def walk_files(root):
    """Files under root without following links. A link found anywhere is an error."""
    for d, dirs, files in os.walk(root, followlinks=False):
        for x in dirs:
            if is_link(os.path.join(d, x)):
                raise RuntimeError("link inside tree " + os.path.join(d, x))
        for f in files:
            yield os.path.join(d, f)


def now():
    return datetime.datetime.now().isoformat(timespec="seconds")


def editor_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe", "/NH"], capture_output=True, text=True, timeout=60).stdout
    return "UnrealEditor.exe" in out


def cmd_verifycopy():
    t0 = time.time()
    cache = json.load(open(HASHES)) if os.path.exists(HASHES) else {}
    files = [os.path.relpath(f, MAIN_CONTENT) for c in COPIED for f in walk_files(os.path.join(MAIN_CONTENT, c))]
    for rel in files:
        if rel in cache:
            continue
        if time.time() - t0 > BUDGET:
            json.dump(cache, open(HASHES, "w"))
            print("budget reached, hashed", len(cache), "of", len(files))
            return 2
        a, b = os.path.join(MAIN_CONTENT, rel), os.path.join(REF_CONTENT, rel)
        cache[rel] = {"size": os.path.getsize(a), "main": sha256(a),
                      "ref": sha256(b) if os.path.exists(b) else None, "ref_size": os.path.getsize(b) if os.path.exists(b) else None}
    json.dump(cache, open(HASHES, "w"))
    bad = [r for r in files if cache[r]["main"] != cache[r]["ref"] or cache[r]["size"] != cache[r]["ref_size"]]
    extra = sorted({os.path.relpath(f, REF_CONTENT) for c in COPIED for f in walk_files(os.path.join(REF_CONTENT, c))} - set(files))
    rep = {"time": now(), "files": len(files), "bytes": sum(cache[r]["size"] for r in files), "mismatch": bad[:50],
           "mismatch_count": len(bad), "extra_in_ref": extra[:50], "status": "pass" if not bad and not extra else "fail"}
    json.dump(rep, open(COPY_REPORT, "w"), indent=1)
    print("verifycopy", rep["status"], "files", rep["files"], "bytes", rep["bytes"], "mismatch", len(bad), "extra", len(extra))
    return 0 if rep["status"] == "pass" else 1


def gate_status():
    out = {}
    for k, p in GATES.items():
        if not os.path.exists(p):
            out[k] = "missing report"
            continue
        r = json.load(open(p))
        if k == "refs_main":
            ok = r.get("outside_referencing_full") == [] and r.get("text_references") == [] and r.get("packages", 0) > 0
        elif k == "sample":
            ok = r.get("status") == "pass" and r.get("clips") == 20
        else:
            ok = r.get("status") == "pass"
        out[k] = "pass" if ok else "fail"
    return out


def cmd_removefull():
    gates = gate_status()
    print("gates", gates)
    if any(v != "pass" for v in gates.values()):
        print("refused, a gate has not passed")
        return 1
    if editor_running():
        print("refused, Unreal Editor is running")
        return 1
    if not no_link_on_path(FULL, MAIN_CONTENT):
        print("refused, Full or a parent is a link")
        return 1
    cache = json.load(open(HASHES))
    log_path = os.path.join(LOGS, "removed_full.csv")
    new = not os.path.exists(log_path)
    t0 = time.time()
    n = freed = 0
    with open(log_path, "a", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        if new:
            w.writerow(["time", "path", "bytes", "sha256", "reference_copy"])
        for f in list(walk_files(FULL)):
            if time.time() - t0 > BUDGET:
                print("budget reached, removed", n, "rerun to continue")
                return 2
            rel = os.path.relpath(f, MAIN_CONTENT)
            c = cache.get(rel)
            ref = os.path.join(REF_CONTENT, rel)
            # Only files whose reference copy was verified byte for byte, and still has the same size.
            if not c or c["main"] != c["ref"] or not os.path.exists(ref) or os.path.getsize(ref) != c["size"]:
                print("kept (no verified reference copy)", rel)
                continue
            if is_link(f):
                print("kept (link)", rel)
                continue
            size = os.path.getsize(f)
            os.remove(f)
            w.writerow([now(), f, size, c["main"], ref])
            n += 1
            freed += size
    # Empty folders bottom up, one at a time (os.rmdir fails on anything non empty).
    # Listed again right before each rmdir, so a parent goes once its children are gone.
    for d, _, _ in sorted(os.walk(FULL, followlinks=False), key=lambda x: -len(x[0])):
        if not os.listdir(d) and not is_link(d):
            os.rmdir(d)
    left = list(walk_files(FULL)) if os.path.exists(FULL) else []
    print("removefull removed", n, "bytes", freed, "files left", len(left))
    return 0 if not left else 1


def job_sources():
    """Temp copy name (job name plus extension) to the source files that produced it."""
    srcs = {}
    paths = glob.glob(os.path.join(HERE, "jobs", "ue_*.json")) + glob.glob(os.path.join(LOGS, "ue_*_jobs.json")) + \
        glob.glob(os.path.join(LOGS, "ue_anim_full_*_gated.json")) + glob.glob(os.path.join(LOGS, "reimport_batch.json"))
    for p in paths:
        try:
            jobs = json.load(open(p, encoding="utf-8"))
        except Exception:
            continue
        for j in jobs if isinstance(jobs, list) else []:
            if j.get("kind") in ("mesh", "anim") and j.get("file") and j.get("name"):
                f = j["file"].replace("{REPO}", REPO.replace("\\", "/"))
                srcs.setdefault(j["name"] + os.path.splitext(f)[1], set()).add(os.path.normpath(f))
    return srcs


def cmd_cleanimport():
    sys.path.insert(0, HERE)
    import madden
    if not os.path.isdir(IMPORT_TMP) or not no_link_on_path(IMPORT_TMP, os.path.join(REPO, "Unreal", "TAGGridiron")):
        print("refused, MaddenRefImport is missing or a link")
        return 1
    manifest = madden.load_manifest()["files"]
    srcs = job_sources()
    log_path = os.path.join(LOGS, "removed_maddenrefimport.csv")
    kept_path = os.path.join(LOGS, "kept_maddenrefimport.csv")
    new = not os.path.exists(log_path)
    t0 = time.time()
    n = freed = kept = 0
    d_hash = {}
    with open(log_path, "a", newline="", encoding="utf-8") as fh, open(kept_path, "w", newline="", encoding="utf-8") as kh:
        w, k = csv.writer(fh), csv.writer(kh)
        if new:
            w.writerow(["time", "path", "bytes", "sha256", "source_on_d", "manifest_entry"])
        k.writerow(["path", "reason"])
        # Top level files only. The folder holds no subfolders, and none are entered.
        for name in sorted(os.listdir(IMPORT_TMP)):
            f = os.path.join(IMPORT_TMP, name)
            if time.time() - t0 > BUDGET:
                print("budget reached, removed", n, "rerun to continue")
                return 2
            if is_link(f) or not os.path.isfile(f):
                k.writerow([f, "link or not a regular file"])
                kept += 1
                continue
            h = sha256(f)
            match = None
            for s in sorted(srcs.get(name, ())):
                rel = madden.rel(s)
                entry = manifest.get(rel)
                real = os.path.realpath(s)
                if not entry or entry.get("sha256") != h or not real.upper().startswith("D:\\") or not os.path.exists(real):
                    continue
                if real not in d_hash:
                    d_hash[real] = sha256(real)
                if d_hash[real] == h:
                    match = (real, rel)
                    break
            if not match:
                k.writerow([f, "no manifest entry on D with a matching hash"])
                kept += 1
                continue
            size = os.path.getsize(f)
            os.remove(f)
            w.writerow([now(), f, size, h, match[0], match[1]])
            n += 1
            freed += size
    print("cleanimport removed", n, "bytes", freed, "kept", kept)
    return 0


if __name__ == "__main__":
    sys.exit({"verifycopy": cmd_verifycopy, "removefull": cmd_removefull, "cleanimport": cmd_cleanimport}[sys.argv[1]]())
