"""Crash register (Research/NextLevel/07 A1).

Reads every Saved/Crashes/*/CrashContext.runtime-xml (never writes or deletes there), sorts each report into a game run
(EngineMode Game, i.e. `UnrealEditor.exe <uproject> -game`) or an editor session (Editor / Commandlet), groups the
reports by a signature that survives code edits (kind + normalised message + first symbolised frames, no line numbers),
and compares the groups with the register kept in the repo (Tools/QA/crash_register.json).

    python Tools/QA/crashes.py                     list the groups, exit 1 if a game crash has a signature not in the register
    python Tools/QA/crashes.py --update            same, then add / refresh the groups in the register (keeps status and notes)
    python Tools/QA/crashes.py --since 2026-11-18  only reports from that local time on (also --days 5)
    python Tools/QA/crashes.py --fail-since "2026-11-24 09:00"   also exit 1 on ANY game crash after that time (the gate)
    python Tools/QA/crashes.py --editor            list editor / commandlet groups in full too (default: one line each)
    python Tools/QA/crashes.py --tail 12           more log lines before each crash (default 6)
    python Tools/QA/crashes.py --json out.json     the scan as JSON (for the gate)

Register entries: status is "open" (default), "fixed" or "ignore"; edit status / note / fixed_on by hand. A "fixed"
signature that comes back in a report newer than fixed_on is a regression and fails the check like a new one.
Ensures (non-fatal) are registered but only fail the check with --strict-ensures.

Exit codes: 0 clean, 1 new / regressed game crash (or one after --fail-since), 2 usage or I/O error.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
CRASHES = os.path.join(PROJ, "Saved", "Crashes")
REGISTER = os.path.join(HERE, "crash_register.json")
ENGINE_BIN = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64"
PROJECT_MODULE = "TAGGridiron"

# frames that belong to the crash / assert machinery, never the cause
PLUMBING = re.compile(r"(FDebug::|FWindowsErrorOutputDevice|FOutputDevice|ReportAssert|ReportEnsure|CheckVerifyFailed|"
                      r"AssertFailed|EnsureFailed|OptionallyReportEnsure|UE::Assert|FGenericPlatformMisc::|"
                      r"FWindowsPlatformMisc::RaiseException|FPlatformMallocCrash|FGenericPlatformMemory::OnOutOfMemory|"
                      r"FMallocBinned|FMemory::|RaiseException|__report_gsfailure|FMallocWrapper|FMallocPoison)")


# ------------------------------------------------------------------ parsing
def ticks_to_local(ticks):
    """CrashContext TimeOfCrash is .NET ticks (100 ns since 0001-01-01, UTC)."""
    try:
        utc = dt.datetime(1, 1, 1, tzinfo=dt.timezone.utc) + dt.timedelta(microseconds=int(ticks) // 10)
        return utc.astimezone().replace(tzinfo=None)
    except (ValueError, OverflowError):
        return None


def parse_frames(text):
    frames = []
    for line in (text or "").splitlines():
        line = line.strip()
        if not line:
            continue
        m = re.match(r"^([^!\s]+)!(.+?)\s*(?:\[(.+):(\d+)\])?\s*$", line)
        if m:
            frames.append({"module": m.group(1), "func": m.group(2).strip(), "file": m.group(3), "line": int(m.group(4)) if m.group(4) else None})
        else:
            frames.append({"module": line.split()[0], "func": None, "file": None, "line": None})
    return frames


def parse_pcallstack(text):
    out = []
    for line in (text or "").splitlines():
        m = re.match(r"^\s*(\S+)\s+0x([0-9a-fA-F]+)\s*\+\s*([0-9a-fA-F]+)", line)
        if m:
            out.append("%s+0x%s" % (m.group(1), m.group(3)))
    return out


def classify(props, msg):
    low = msg.lower()
    if props.get("MemoryStats.bIsOOM") == "1" or "out of memory" in low or "genericplatformmemory.cpp" in low or "ran out of" in low:
        return "oom"
    if "gpu crash" in low or "device removed" in low or "dxgi_error" in low or "d3d12util.cpp" in low or "device_hung" in low:
        return "gpu"
    if props.get("IsEnsure") == "true" or low.startswith("ensure condition failed"):
        return "ensure"
    if props.get("IsStall") == "true":
        return "stall"
    if props.get("IsAssert") == "true" or low.startswith("assertion failed") or low.startswith("fatal error"):
        return "assert"
    if "exception" in low:
        return "crash"
    return (props.get("CrashType") or "crash").lower()


def norm_message(msg, kind):
    m = msg.replace("\r", " ").replace("\n", " ")
    m = re.sub(r"Unhandled Exception:\s*", "", m)
    if kind == "crash":
        # an exception's address and its "reading address" wording change between runs of the same fault: the code
        # name and the stack carry the signature
        code = re.search(r"EXCEPTION_[A-Z_]+", m)
        if code:
            return code.group(0)

    def addr(mo):
        try:
            v = int(mo.group(0), 16)
        except ValueError:
            return "ADDR"
        if v < 0x10000:
            return "NEAR_NULL"
        if v >= 0xFFFFFFFFFFFF0000:
            return "NEG_ADDR"
        return "ADDR"
    m = re.sub(r"0x[0-9a-fA-F]+", addr, m)
    m = re.sub(r"\[Line:\s*\d+\]", "", m)
    m = re.sub(r"\[File:([^\]]*)\]", lambda mo: "[%s]" % re.split(r"[\\/]", mo.group(1).strip())[-1], m)
    m = re.sub(r"\b\d+(\.\d+)?\b", "N", m)
    m = re.sub(r"\s+", " ", m).strip()
    return m[:220]


def short_file(path):
    if not path:
        return ""
    p = path.replace("\\", "/")
    for key in ("/Source/TAGGridiron/", "/Engine/Source/", "/Engine/Plugins/"):
        i = p.find(key)
        if i >= 0:
            return p[i + 1:]
    return p.split("/")[-1]


def read_log(folder):
    logs = [f for f in os.listdir(folder) if f.lower().endswith(".log")]
    if not logs:
        return None, []
    path = os.path.join(folder, max(logs, key=lambda f: os.path.getsize(os.path.join(folder, f))))
    try:
        with open(path, "rb") as fh:
            size = os.path.getsize(path)
            head = fh.read(min(size, 300000)).decode("utf-8", "replace")
            fh.seek(max(0, size - 400000))
            tail = fh.read().decode("utf-8", "replace")
    except OSError:
        return None, []
    m = re.search(r"Command Line:\s*(.*)", head)
    return (m.group(1).strip() if m else None), tail.splitlines()


STAMP = re.compile(r"^\[\d{4}\.\d\d\.\d\d-[\d.:]+\]\[\s*\d+\]")
CRIT = re.compile(r"(=== Critical error: ===|Fatal error!|Unhandled Exception:|Assertion failed:|Ensure condition failed:|"
                  r"begin: stack for UAT|appError called)")
LEADIN = re.compile(r"(Script Stack \(|LogOutputDevice: (Warning|Error):|Assertion failed|Ensure condition failed|Allocator Stats|"
                    r"LogMemory: |LogWindows: Error:|=== Handled ensure|Ran out of memory)")
NOISE = re.compile(r"(LogStreaming: Display: FlushAsyncLoading|LogSlate: |LogRHI: |LogD3D12RHI: Display|LogPSO|LogShaderCompilers|"
                   r"LogStats: |InternalLoadLibrary|LogChaosDD|AttemptToFindUninitializedScriptStructMembers)")


def log_context(lines, n):
    """The log lines just before the crash: cut at the LAST crash marker (a game log can hold earlier handled
    ensures), then step back over the crash block's own lead-in and its un-stamped continuation lines."""
    cut = len(lines)
    for i in range(len(lines) - 1, -1, -1):
        if CRIT.search(lines[i]):
            cut = i
            break
    lo = max(0, cut - 40)
    while cut > lo and (not lines[cut - 1].strip() or LEADIN.search(lines[cut - 1]) or not STAMP.match(lines[cut - 1])):
        cut -= 1
    before = [STAMP.sub("", ln).strip() for ln in lines[:cut]]
    kept = []
    shape = lambda s: re.sub(r"[\d.]+", "#", s)
    for ln in before:
        if ln and not NOISE.search(ln) and (not kept or shape(kept[-1]) != shape(ln)):
            kept.append(ln)
    before = kept
    last_tag = next((ln for ln in reversed(before) if "TAG " in ln), None)
    return before[-n:], last_tag


def tag_flags(cmdline):
    flags = []
    for f in re.findall(r"-(Tag\w+(?:=\S+)?)", cmdline or ""):
        k, _, v = f.partition("=")
        if v and ("/" in v or "\\" in v):
            v = "<path>"
        flags.append("-" + k + ("=" + v if v else ""))
    return flags


def parse_report(folder, tail_n):
    xml_path = os.path.join(folder, "CrashContext.runtime-xml")
    if not os.path.isfile(xml_path):
        return None
    try:
        root = ET.parse(xml_path).getroot()
    except ET.ParseError:
        with open(xml_path, "rb") as fh:
            txt = fh.read().decode("utf-8", "replace")
        txt = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", txt)
        root = ET.fromstring(txt)
    rp = root.find("RuntimeProperties")
    props = {}
    for el in (rp if rp is not None else []):
        if el.tag not in props:  # (the first of duplicate tags: CrashReporterMessage appears twice)
            props[el.tag] = el.text or ""
    msg = props.get("ErrorMessage", "").strip()
    log_cmd, log_lines = read_log(folder)
    cmd = props.get("CommandLine", "")
    if not cmd or cmd == "CommandLineRemoved":
        cmd = log_cmd or ""
    mode = props.get("EngineMode", "")
    process = "game" if mode == "Game" or re.search(r"(^|\s)-game(\s|$)", cmd) else ("commandlet" if mode == "Commandlet" else "editor")
    frames = parse_frames(props.get("CallStack", ""))
    kind = classify(props, msg)
    sym = [f for f in frames if f["func"] and not PLUMBING.search(f["func"])]
    game_frames = [f for f in sym if PROJECT_MODULE in f["module"]]
    when = ticks_to_local(props.get("TimeOfCrash", "0")) or dt.datetime.fromtimestamp(os.path.getmtime(xml_path))
    before, last_tag = log_context(log_lines, tail_n)
    # signature: survives line-number drift and differing addresses; the project frames say most about the cause
    key_frames = [f["func"] for f in (game_frames[:2] or sym[:2])]
    if not key_frames:
        mods = []
        for f in frames:
            if not mods or mods[-1] != f["module"]:
                mods.append(f["module"])
        key_frames = ["mods:" + ">".join(mods[:4])]
    sig_text = "%s | %s | %s" % (kind, norm_message(msg, kind), " < ".join(key_frames))
    if kind == "oom":
        # where an allocation finally fails is incidental: one group per kind of memory
        sig_text = "oom | %s" % ("video memory" if "video memory" in msg.lower() else "system memory")
    top = game_frames[0] if game_frames else (sym[0] if sym else None)
    return {
        "folder": os.path.basename(folder),
        "time": when,
        "process": process,
        "engine_mode": mode,
        "kind": kind,
        "message": msg,
        "flags": tag_flags(cmd),
        "cmdline": cmd,
        "sig_text": sig_text,
        "sig": hashlib.sha1(("%s|%s" % (process, sig_text)).encode("utf-8")).hexdigest()[:10],
        "top": ("%s %s:%s" % (top["func"], short_file(top["file"]), top["line"]) if top and top["file"] else (top["func"] if top else None)),
        "game_frames": ["%s %s:%s" % (f["func"], short_file(f["file"]), f["line"]) for f in game_frames[:6]],
        "engine_frames": [f["module"] + "!" + f["func"] for f in sym if PROJECT_MODULE not in f["module"]][:4],
        "raw_top": parse_pcallstack(props.get("PCallStack", ""))[:4],
        "symbolised": bool(sym),
        "log_before": before,
        "last_tag": last_tag,
        "seconds_since_start": props.get("SecondsSinceStart"),
    }


# ------------------------------------------------------------------ register
def load_register(path):
    if not os.path.isfile(path):
        return {"version": 1, "signatures": {}}
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def fmt_t(t):
    return t.strftime("%Y-%m-%d %H:%M") if t else "?"


def parse_when(s):
    s = s.strip()
    for f in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%dT%H:%M", "%Y-%m-%d"):
        try:
            return dt.datetime.strptime(s, f)
        except ValueError:
            pass
    raise argparse.ArgumentTypeError("time must look like 2026-11-24 or '2026-11-24 09:30': %r" % s)


def symbols_status():
    eng = [n for n in ("UnrealEditor-Engine.pdb", "UnrealEditor-Core.pdb", "UnrealEditor-Renderer.pdb") if os.path.isfile(os.path.join(ENGINE_BIN, n))]
    proj = os.path.isfile(os.path.join(PROJ, "Binaries", "Win64", "UnrealEditor-%s.pdb" % PROJECT_MODULE))
    return {"engine_pdbs": bool(eng), "project_pdb": proj}


def main():
    ap = argparse.ArgumentParser(description="TAG crash register: group Saved/Crashes reports and check for new game crashes.")
    ap.add_argument("--crashes", default=CRASHES, help="crash folder (default Saved/Crashes)")
    ap.add_argument("--register", default=REGISTER, help="register file (default Tools/QA/crash_register.json)")
    ap.add_argument("--update", action="store_true", help="write new / refreshed groups into the register")
    ap.add_argument("--since", type=parse_when, help="only reports at or after this local time")
    ap.add_argument("--days", type=float, help="only reports from the last N days")
    ap.add_argument("--fail-since", type=parse_when, help="also fail on any game crash at or after this local time")
    ap.add_argument("--strict-ensures", action="store_true", help="new game ensures fail the check too")
    ap.add_argument("--editor", action="store_true", help="show editor / commandlet groups in full")
    ap.add_argument("--tail", type=int, default=6, help="log lines shown before each crash (latest report of a group)")
    ap.add_argument("--json", help="write the scan as JSON here")
    ap.add_argument("--quiet", action="store_true", help="only the verdict lines")
    a = ap.parse_args()

    if not os.path.isdir(a.crashes):
        print("crashes: no folder %s (nothing to check)" % a.crashes)
        return 0
    since = a.since
    if a.days is not None:
        d = dt.datetime.now() - dt.timedelta(days=a.days)
        since = max(since, d) if since else d
    reports = []
    bad = []
    for name in sorted(os.listdir(a.crashes)):
        folder = os.path.join(a.crashes, name)
        if not os.path.isdir(folder):
            continue
        try:
            r = parse_report(folder, a.tail)
        except Exception as e:  # one broken report must not hide the others
            bad.append("%s: %s" % (name, e))
            continue
        if r and (since is None or r["time"] >= since):
            reports.append(r)
    try:
        reg = load_register(a.register)
    except (OSError, ValueError) as e:
        print("crashes: cannot read register %s: %s" % (a.register, e))
        return 2
    known = reg.get("signatures", {})

    groups = {}
    for r in reports:
        g = groups.setdefault(r["sig"], {"sig": r["sig"], "process": r["process"], "kind": r["kind"], "sig_text": r["sig_text"], "reports": []})
        g["reports"].append(r)
    for g in groups.values():
        g["reports"].sort(key=lambda r: r["time"])
        g["first"], g["last"] = g["reports"][0]["time"], g["reports"][-1]["time"]
        e = known.get(g["sig"])
        g["status"] = e.get("status", "open") if e else "NEW"
        g["note"] = e.get("note", "") if e else ""
        fixed_on = e.get("fixed_on") if e else None
        g["regressed"] = False
        if e and g["status"] == "fixed" and fixed_on:
            try:
                g["regressed"] = g["last"] > parse_when(fixed_on)
            except argparse.ArgumentTypeError:
                pass

    fatal = lambda g: g["kind"] != "ensure" or a.strict_ensures
    failing = []
    for g in groups.values():
        if g["process"] != "game" or not fatal(g):
            continue
        if g["status"] == "NEW":
            failing.append((g, "new signature"))
        elif g["regressed"]:
            failing.append((g, "came back after fixed_on"))
    late = []
    if a.fail_since:
        late = [r for r in reports if r["process"] == "game" and r["time"] >= a.fail_since and (r["kind"] != "ensure" or a.strict_ensures)]

    order = sorted(groups.values(), key=lambda g: g["last"], reverse=True)
    games = [g for g in order if g["process"] == "game"]
    others = [g for g in order if g["process"] != "game"]
    if not a.quiet:
        n_game = sum(len(g["reports"]) for g in games)
        print("crash reports: %d (%d game runs, %d editor/commandlet)%s, %d groups" % (
            len(reports), n_game, len(reports) - n_game, (" since " + fmt_t(since)) if since else "", len(groups)))
        print()
        print("== GAME RUNS (UnrealEditor -game), newest first")
        for g in games:
            r = g["reports"][-1]
            mark = "NEW " if g["status"] == "NEW" else ("REGRESSED " if g["regressed"] else "")
            print("[%s] %s%s x%d  %s .. %s  status=%s%s" % (g["sig"], mark, g["kind"].upper(), len(g["reports"]), fmt_t(g["first"]), fmt_t(g["last"]),
                                                       g["status"], ("  note: " + g["note"]) if g["note"] else ""))
            print("    message: %s" % r["message"][:240].replace("\n", " "))
            if r["game_frames"]:
                print("    game stack: " + "\n                ".join(r["game_frames"][:4]))
            if r["engine_frames"]:
                print("    engine frames: " + ", ".join(r["engine_frames"][:3]))
            if not r["symbolised"]:
                print("    no symbols; raw top frames: " + ", ".join(r["raw_top"]))
            flagsets = []
            for x in g["reports"]:
                s = " ".join(x["flags"]) or "(no -Tag flags)"
                if s not in flagsets:
                    flagsets.append(s)
            for s in flagsets[-3:]:
                print("    run: %s" % s[:200])
            print("    reports: " + ", ".join("%s %s" % (fmt_t(x["time"])[5:], x["folder"][13:21]) for x in g["reports"][-6:]))
            if r["last_tag"]:
                print("    last TAG line: %s" % r["last_tag"][:200])
            for ln in r["log_before"]:
                print("    | %s" % ln[:200])
            print()
        print("== EDITOR / COMMANDLET SESSIONS")
        for g in others:
            r = g["reports"][-1]
            mark = "NEW " if g["status"] == "NEW" else ""
            print("[%s] %s%s %s x%d  %s .. %s  %s" % (g["sig"], mark, g["process"], g["kind"], len(g["reports"]), fmt_t(g["first"]), fmt_t(g["last"]), r["message"][:150].replace("\n", " ")))
            if a.editor:
                for f in (r["game_frames"] or r["engine_frames"])[:3]:
                    print("      " + f)
        print()
        for b in bad:
            print("unreadable report: " + b)
        s = symbols_status()
        print("symbols: engine PDBs %s, project PDB %s" % ("installed" if s["engine_pdbs"] else
              "NOT installed (Epic Games Launcher > Library > UE 5.8 > Options > 'Editor symbols for debugging')",
              "present" if s["project_pdb"] else "missing"))

    if a.update:
        sigs = reg.setdefault("signatures", {})
        for g in groups.values():
            e = sigs.get(g["sig"])
            r = g["reports"][-1]
            if not e:
                e = sigs[g["sig"]] = {"status": "open", "note": "", "fixed_on": None, "first_seen": fmt_t(g["first"]), "reports": []}
            e.update({"process": g["process"], "kind": g["kind"], "signature": g["sig_text"], "message": r["message"][:300],
                      "top": r["top"], "last_seen": max(e.get("last_seen", ""), fmt_t(g["last"])),
                      "first_seen": min(e.get("first_seen") or "9999", fmt_t(g["first"]))})
            e["reports"] = sorted(set(e.get("reports", [])) | {x["folder"] for x in g["reports"]})
            e["count"] = len(e["reports"])
            e["flags_seen"] = sorted(set(e.get("flags_seen", [])) | {f.split("=")[0] for x in g["reports"] for f in x["flags"]})
        items = sorted(sigs.items(), key=lambda kv: kv[1].get("last_seen", ""), reverse=True)
        items.sort(key=lambda kv: kv[1].get("process") != "game")  # (game runs first, newest first within each)
        reg = {"version": 1, "updated": dt.datetime.now().strftime("%Y-%m-%d %H:%M"),
               "help": "status: open | fixed | ignore; set fixed_on ('YYYY-MM-DD HH:MM') with fixed - a later report of the "
                       "signature fails the check as a regression. Written by Tools/QA/crashes.py --update; status, note "
                       "and fixed_on are kept across updates.",
               "signatures": dict(items)}
        with open(a.register, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(reg, fh, indent=1, ensure_ascii=False)
            fh.write("\n")
        print("register: %d signatures -> %s" % (len(reg["signatures"]), a.register))

    if a.json:
        def enc(o):
            return fmt_t(o) if isinstance(o, dt.datetime) else str(o)
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump({"groups": order, "failing": [{"sig": g["sig"], "why": why} for g, why in failing],
                       "late": [r["folder"] for r in late], "symbols": symbols_status()}, fh, indent=1, default=enc)

    failing.sort(key=lambda gw: gw[0]["last"], reverse=True)
    for g, why in failing:
        print("CRASH CHECK FAIL: game %s [%s] %s: %s" % (g["kind"], g["sig"], why, g["reports"][-1]["message"][:160].replace("\n", " ")))
    for r in late:
        print("CRASH CHECK FAIL: game %s at %s (%s) after %s: %s" % (r["kind"], fmt_t(r["time"]), r["folder"], fmt_t(a.fail_since), r["message"][:120]))
    if failing or late:
        if not a.update:
            print("(after triage: python Tools/QA/crashes.py --update, then set status/note in the register)")
        return 1
    print("CRASH CHECK PASS: no unregistered game crashes%s" % ((" and none since " + fmt_t(a.fail_since)) if a.fail_since else ""))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(2)
