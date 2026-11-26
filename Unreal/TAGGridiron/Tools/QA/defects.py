"""Defect list from -TagHlFull telemetry (Research/NextLevel/07 A6).

Turns one telemetry folder (c<clip>.csv per play, written by AMatchDirector::HlTelemetry; with -TagHlNoFrames no frames
are written, only the telemetry) plus the run's log into a ranked list of individual defects, each with the play (clip),
the frame, the game time, the men involved, a severity and a command line that films that play again (the sim is
deterministic under -UseFixedTimeStep: -TagHlFromQ + -TagHlMaxClips reproduce the clip with frames).

    python Tools/QA/defects.py <run dir or telemetry dir> [--log run.log] [--out-dir <dir>] [--top 8]
    python Tools/QA/defects.py <dir> --budget Tools/QA/defect_budget.json      exit 1 when a class is over its budget
    python Tools/QA/defects.py <dir> --no-budget                               report only (exit 0)
    python Tools/QA/defects.py <dir> --propose-budget new_budget.json [--also other/defects.json ...]
    python Tools/QA/defects.py <dir> --only foot_slide,pose_pop                only these classes

Writes defects.json (every event, the per-class counts, the budget verdict) and defects.md (top events per class with
their replay commands) into --out-dir (default: the run folder, the telemetry folder's parent).

Classes (thresholds in THRESH below; sev 3 high, 2 medium, 1 low):
  foot_slide         a planted foot (the capture's own verdict, plf) travelling over the turf as shown (sls)
  pose_pop           feet / pelvis / head / hand jump in one frame that the motion cannot explain (analyze.py body_pops)
  actor_teleport     the actor (wx, wy) jumps with no pop of the body (root re-seat, snap to a spot)
  sim_teleport       the sim's man (sx, sy) moves faster than any man can in one frame (live play)
  interpenetration   two upright men's pelvises closer than 30 cm
  frozen             sim moving but the body standing still, or a pose that does not change while the body travels
  tpose              hands spread at shoulder height (reference pose) or no animation on a shown man
  nan_bones          a shown man with no pelvis / head bone positions
  ball_teleport      the shown ball (or the sim ball) jumping more than 1.5 m in one frame while in play
  ball_off_hand      the carrier's ball over 25 cm from both hands
  feet_under_turf    a foot over 8 cm under the turf
  facing_backward    running over 3 m/s facing more than 120 deg off the travel (not backpedals, drops, pass sets)
  official_near      an official within 1 m of a player
  camera_in_body     the lens within 60 cm of a head or pelvis
  camera_cut_storm   more than 2 camera cuts inside one second
  carrier_off_screen the ball carrier off screen for more than 0.5 s of live play
  stall              a play's pre-snap / post-whistle running long, or a long gap between plays (needs 'gf' in the log)
  clock              the game clock / quarter / score going backwards, jumping or freezing (from the log)
  run                the run did not reach 'TAG hl done' (timeout, crash, cap)

Exit codes: 0 within budget (or no budget), 1 over budget, 2 usage / no telemetry.
"""
import argparse
import glob
import json
import math
import os
import re
import sys
import warnings

import numpy as np
import pandas as pd

warnings.filterwarnings("ignore", category=RuntimeWarning)  # (all-NaN slices of hidden men / missing bones are expected)

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, "..", ".."))
UPROJECT = os.path.join(PROJ, "TAGGridiron.uproject")
EDITOR = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
DEFAULT_BUDGET = os.path.join(HERE, "defect_budget.json")
YD_CM = 91.44
YD = 0.9144

CLASSES = ["foot_slide", "pose_pop", "scene_reset", "actor_teleport", "sim_teleport", "interpenetration", "frozen", "tpose", "nan_bones",
           "ball_teleport", "ball_off_hand", "feet_under_turf", "facing_backward", "official_near", "camera_in_body",
           "camera_cut_storm", "carrier_off_screen", "stall", "clock", "run"]

THRESH = {
    "slide_frame_cm": 1.5,      # planted-foot travel shown in one frame (per planted foot) that starts counting (45 cm/s)
    "slide_event_cm": 8.0,      # a slide event: that much shown travel over one run of sliding frames
    "pop_cm": 60.0,             # feet / pelvis / head jump in one frame (18 m/s)
    "pop_hand_cm": 100.0,       # hands move faster (a throw): 30 m/s
    "teleport_cm": 60.0,        # actor / sim jump in one frame
    "overlap_cm": 25.0,         # pelvis to pelvis, both upright (torsos touch at ~35; ~10 cm of overlap is capture level)
    "overlap_frames": 3,
    "stuck_sim_ms": 2.0,        # frozen: the sim man moves this fast ...
    "stuck_body_ms": 0.25,      # ... and the shown body slower than this ...
    "stuck_frames": 15,         # ... for this many frames
    "statue_cm": 0.3,           # frozen pose: no joint moves more than this against the pelvis in a frame ...
    "statue_body_ms": 1.0,      # ... while the body travels this fast ...
    "statue_frames": 15,        # ... for this many frames
    "tpose_span_cm": 150.0,     # hand to hand
    "tpose_shoulder_cm": 15.0,  # both hands within this of shoulder height (head - 25 cm)
    "ball_jump_cm": 150.0,      # ball in one frame (45 m/s)
    "ball_hand_cm": 25.0,
    "ball_hand_frames": 3,
    "turf_cm": -8.0,
    "turf_frames": 3,
    "backward_deg": 120.0,
    "backward_ms": 3.0,
    "backward_frames": 6,
    "official_cm": 100.0,
    "official_frames": 3,
    "lens_cm": 60.0,
    "cuts_per_sec": 2,          # more than this many cuts inside one second is a storm
    "offscreen_frames": 15,
    "presnap_s": 40.0,          # a clip's pre-snap longer than this (the walk to the line + the cadence)
    "post_s": 20.0,             # post-whistle longer than this
    "other_s": 20.0,            # between plays inside a clip (the huddle and the shots after a replay: 10-14 s is normal)
    "gap_s": 45.0,              # between one play's end and the next's start (game seconds, fixed step)
    "runoff_s": 75.0,           # the clock dropping more than this between two plays of one quarter
    "frozen_clock_plays": 6,
}
# states the man is judged in (replays cut and re-pose by design)
PLAY_STATES = ("presnap", "live", "post")
NOT_UPRIGHT_MODES = ("fall", "ragdoll", "replay", "getup")
# sim states in which running backwards is the job: backpedal (man / zone), the QB's drop, the pass set
BACKWARD_OK = ("man", "zone", "qb", "passblock", "spy", "down")
OFFICIALS = {0: "referee", 1: "umpire", 2: "down judge", 3: "line judge", 4: "side judge", 5: "field judge", 6: "back judge"}

USECOLS = ["frame", "state", "simt", "ballmode", "bx", "by", "bz", "bwx", "bwy", "bwz", "pid", "num", "pos", "side", "role",
           "sstate", "sanim", "sx", "sy", "svx", "svy", "eng", "ball", "stun", "hidden", "wx", "wy", "wz", "yaw", "down",
           "flx", "fly", "flz", "frx", "fry", "frz", "pz", "hdz", "hrx", "hry", "hrz", "hlx", "hly", "hlz", "refd", "refw",
           "slr", "sls", "pvx", "pvy", "hdx", "hdy", "plf", "anim", "camx", "camy", "camz", "camang", "camcut", "camnear",
           "camhold"]
STRCOLS = {"state": str, "ballmode": str, "pos": str, "side": str, "role": str, "sstate": str, "sanim": str, "eng": str, "anim": str}


# ------------------------------------------------------------------ inputs
def find_inputs(path, log):
    path = os.path.abspath(path)
    if os.path.isdir(os.path.join(path, "telemetry")):
        tele, run = os.path.join(path, "telemetry"), path
    else:
        tele, run = path, os.path.dirname(path)
    if not log:
        for cand in (os.path.join(run, "run.log"), os.path.join(tele, "run.log")):
            if os.path.isfile(cand):
                log = cand
                break
        else:
            logs = sorted(glob.glob(os.path.join(run, "*.log")), key=os.path.getsize, reverse=True)
            log = logs[0] if logs else None
    return tele, run, log


def parse_log(path):
    """The clip lines of a -TagHighlights -TagHlFull log: desc, game q / clock / score, gf (frame counter) at start / end."""
    info = {"cmdline": "", "clips": {}, "done": False, "final": None, "timeout": False, "holds": [], "fps": 30.0, "qlen": 900.0}
    if not path or not os.path.isfile(path):
        return info
    clips = info["clips"]
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "Command Line:" in line and not info["cmdline"]:
                info["cmdline"] = line.split("Command Line:", 1)[1].strip()
                continue
            if "TAG " not in line:
                continue
            m = re.search(r"TAG hl clip (\d+) start cam (\d+)(?: gf (\d+))?", line)
            if m:
                c = clips.setdefault(int(m.group(1)), {})
                c["cam"] = int(m.group(2))
                if m.group(3):
                    c["gf_start"] = int(m.group(3))
                continue
            m = re.search(r'TAG hl clip (\d+) end frames (\d+) big (\d+) desc "(.*)"', line)
            if m:
                c = clips.setdefault(int(m.group(1)), {})
                c.update(frames=int(m.group(2)), big=int(m.group(3)), desc=m.group(4))
                continue
            m = re.search(r"TAG hl clip (\d+) game q(\d+) clock (-?[\d.]+) score (\d+)-(\d+)(?: gf (\d+))?", line)
            if m:
                c = clips.setdefault(int(m.group(1)), {})
                c.update(q=int(m.group(2)), clock=float(m.group(3)), away=int(m.group(4)), home=int(m.group(5)))
                if m.group(6):
                    c["gf_end"] = int(m.group(6))
                continue
            m = re.search(r"TAG hl (hold|resume) for compiles at clip (\d+)", line)
            if m:
                info["holds"].append((m.group(1), int(m.group(2))))
                continue
            if "TAG hl done" in line:
                info["done"] = True
            m = re.search(r"TAG hl final score (\d+)-(\d+)", line)
            if m:
                info["final"] = (int(m.group(1)), int(m.group(2)))
            if "TAG timeout" in line:
                info["timeout"] = True
    m = re.search(r"-FPS=([\d.]+)", info["cmdline"])
    if m:
        info["fps"] = float(m.group(1))
    m = re.search(r"-TagQuarterLen=([\d.]+)", info["cmdline"])
    if m:
        info["qlen"] = float(m.group(1))
    return info


def load_clip(f):
    head = pd.read_csv(f, nrows=0).columns
    cols = [c for c in USECOLS if c in head]
    d = pd.read_csv(f, usecols=cols, dtype={k: v for k, v in STRCOLS.items() if k in cols}, low_memory=False)
    for c in USECOLS:
        if c not in d.columns:
            d[c] = np.nan
    d["mode"] = d["anim"].fillna("").str.split(":").str[0]
    return d.sort_values(["pid", "frame"]).reset_index(drop=True)


# ------------------------------------------------------------------ helpers
def runs(mask, cont, min_len=1):
    """Runs of True in mask over rows where cont[i] says row i continues row i-1 (same man, next frame)."""
    m = np.asarray(mask, dtype=bool)
    if not m.any():
        return []
    c = np.asarray(cont, dtype=bool)
    prev = np.r_[False, m[:-1]] & c
    start = m & ~prev
    rid = np.cumsum(start)
    idx = np.flatnonzero(m)
    r = rid[idx]
    cuts = np.flatnonzero(np.diff(r)) + 1
    out = [a for a in np.split(idx, cuts) if len(a) >= min_len]
    return out


def man(row):
    num = row.get("num")
    try:
        num = int(num)
    except (TypeError, ValueError):
        num = "?"
    return "#%s %s %s %s" % (num, row.get("pos", "?"), row.get("side", "?"), row.get("role", "?"))


def fmt_clock(q, clock):
    if q is None or clock is None:
        return "?"
    clock = max(0.0, float(clock))
    return "Q%d %d:%02d" % (q, int(clock) // 60, int(clock) % 60)


class Events:
    def __init__(self, only):
        self.ev = []
        self.only = only

    def want(self, cls):
        return not self.only or cls in self.only

    def add(self, cls, sev, size, unit, clip, f0, f1, players, state="", anim="", detail=""):
        self.ev.append({"cls": cls, "sev": int(sev), "size": round(float(size), 1), "unit": unit, "clip": int(clip),
                        "frame": int(f0), "frame_end": int(f1), "players": players, "state": state or "",
                        "anim": anim or "", "detail": detail})


def sev3(v, hi, mid):
    return 3 if v >= hi else 2 if v >= mid else 1


# ------------------------------------------------------------------ per-clip detectors (man rows)
def detect_clip(clip, d, E, fps):
    dt = 1.0 / fps
    T = THRESH
    same = (d.pid.values == np.r_[np.nan, d.pid.values[:-1]]) & (d.frame.values - np.r_[np.nan, d.frame.values[:-1]] == 1)
    vis = (d.hidden.fillna(0).values == 0)
    ok = same & vis & np.r_[False, vis[:-1]]
    st = d.state.fillna("").values
    mode = d["mode"].values
    in_play = np.isin(st, PLAY_STATES)
    upright = (d.down.fillna(0).values == 0) & ~np.isin(mode, NOT_UPRIGHT_MODES)

    def dif(col):
        v = d[col].values.astype(float)
        return np.where(same, v - np.r_[np.nan, v[:-1]], np.nan)

    dwx, dwy = dif("wx"), dif("wy")
    body_step = np.hypot(dwx, dwy)                      # cm this frame
    body_v = body_step / dt / 100.0                     # m/s
    sim_v = np.hypot(d.svx.values, d.svy.values) * YD   # m/s
    expect = 2.5 * sim_v * 100 * dt                     # cm a frame the motion can explain (x2.5 for a swinging limb)
    # get-up edges and down changes re-seat the actor (stale bones under a moved actor; analyze.py)
    prev_mode = np.r_[[""], mode[:-1]]
    down = d.down.fillna(0).values
    edge = same & ((mode != prev_mode) & ((mode == "getup") | (prev_mode == "getup")) | (down != np.r_[0, down[:-1]]))
    edge |= ~same
    # the frame the state changes (huddle -> pre-snap: the next play's men are put on their spots) re-places men by
    # design; it is judged on its own (scene_reset: only when no camera cut hides it), not as a pop or a teleport
    reset = same & (st != np.r_[[""], st[:-1]])
    edge |= reset

    def R(i):
        return d.iloc[int(i)]

    # ---- foot slide (planted feet, the capture's verdict)
    if E.want("foot_slide"):
        plf = d.plf.fillna(0).values.astype(float)
        per = np.where(plf > 0, d.sls.fillna(0).values / np.maximum(plf, 1), 0.0)
        m = vis & upright & in_play & np.isin(mode, ("mm", "one", "clip")) & (per > T["slide_frame_cm"])
        for r in runs(m, same, 1):
            tot = per[r].sum()
            if tot < T["slide_event_cm"]:
                continue
            a = R(r[0])
            E.add("foot_slide", sev3(tot, 40, 20), tot, "cm", clip, a.frame, R(r[-1]).frame, [man(a)], a.state, a.anim,
                  "%.0f cm over %d frames (%.0f cm/s)" % (tot, len(r), tot / len(r) * fps))

    # ---- pops: a joint jumping in one frame, standing out from the frames on both sides
    pop_rows = set()
    if E.want("pose_pop") or E.want("actor_teleport"):
        joints = {
            "feet": (np.hypot((dif("flx") + dif("frx")) / 2, (dif("fly") + dif("fry")) / 2), T["pop_cm"]),
            "pelvis": (np.sqrt(dif("pvx") ** 2 + dif("pvy") ** 2 + dif("pz") ** 2), T["pop_cm"]),
            "head": (np.sqrt(dif("hdx") ** 2 + dif("hdy") ** 2 + dif("hdz") ** 2), T["pop_cm"]),
            "hand": (np.fmax(np.sqrt(dif("hrx") ** 2 + dif("hry") ** 2 + dif("hrz") ** 2),
                             np.sqrt(dif("hlx") ** 2 + dif("hly") ** 2 + dif("hlz") ** 2)), T["pop_hand_cm"]),
        }
        base = ok & in_play & (d.frame.values > 4) & ~edge & (d.pz.fillna(0).values > 45)
        best = np.zeros(len(d))
        best_j = np.array([""] * len(d), dtype=object)
        for name, (j, thr) in joints.items():
            nxt_same = np.r_[same[1:], False]
            prv = np.where(same, np.r_[np.nan, j[:-1]], np.nan)
            nxt = np.where(nxt_same, np.r_[j[1:], np.nan], np.nan)
            spike = j > 1.8 * np.fmax(np.nan_to_num(prv), np.nan_to_num(nxt))
            hit = base & (j > thr) & (j > expect) & spike
            upd = hit & (j > best)
            best = np.where(upd, j, best)
            best_j = np.where(upd, name, best_j)
        cand = np.flatnonzero(best > 0)
        if len(cand):
            # a scene reset (the next play set up) moves many men in one frame: not a glitch on one body
            fr = d.frame.values[cand]
            vals, counts = np.unique(fr, return_counts=True)
            mass = set(vals[counts >= 5])
            for i in cand:
                if d.frame.values[i] in mass:
                    continue
                pop_rows.add(i)
                if E.want("pose_pop"):
                    a = R(i)
                    E.add("pose_pop", sev3(best[i], 150, 90), best[i], "cm", clip, a.frame, a.frame, [man(a)], a.state, a.anim,
                          "%s jumped %.0f cm in one frame (sim %.1f m/s)" % (best_j[i], best[i], sim_v[i]))
    if E.want("scene_reset"):
        # men moved more than a step on the frame the state changes, with no camera cut on that frame (the cut usually
        # lands a frame later: one frame shows the men on their new spots in the old shot)
        cut = d.camcut.fillna(0).values.astype(float) > 0
        m = ok & reset & (body_step > T["teleport_cm"]) & ~cut
        if m.any():
            sub = d[m].assign(_jump=body_step[m])
            for (fr_, st_), g in sub.groupby(["frame", "state"]):
                cut_next = bool(((d.frame.values == fr_ + 1) & cut).any())
                a = g.loc[g._jump.idxmax()]
                E.add("scene_reset", 1, float(g._jump.max()), "cm", clip, fr_, fr_, [man(a)] + (["+%d more" % (len(g) - 1)] if len(g) > 1 else []), st_, "",
                      "%d men re-placed %.0f cm (max) as the state changed to %s, %s" % (len(g), g._jump.max(), st_,
                                                                                        "camera cut one frame later" if cut_next else "no camera cut"))
    if E.want("actor_teleport"):
        m = ok & in_play & (d.frame.values > 4) & ~edge & (body_step > T["teleport_cm"]) & (body_step > expect)
        cand = [i for i in np.flatnonzero(m) if i not in pop_rows]
        if cand:
            fr = d.frame.values[cand]
            vals, counts = np.unique(fr, return_counts=True)
            mass = set(vals[counts >= 5])
            for i in cand:
                if d.frame.values[i] in mass:
                    continue
                a = R(i)
                E.add("actor_teleport", sev3(body_step[i], 300, 120), body_step[i], "cm", clip, a.frame, a.frame, [man(a)], a.state, a.anim,
                      "actor moved %.0f cm in one frame (sim %.1f m/s)" % (body_step[i], sim_v[i]))
    if E.want("sim_teleport"):
        sstep = np.hypot(dif("sx"), dif("sy")) * YD_CM
        m = same & (st == "live") & (np.r_[[""], st[:-1]] == "live") & (d.frame.values > 2) & (sstep > T["teleport_cm"])
        for r in runs(m, same, 1):
            a = R(r[0])
            E.add("sim_teleport", 2, sstep[r].max(), "cm", clip, a.frame, R(r[-1]).frame, [man(a)], a.state, a.sstate,
                  "sim man moved %.0f cm in one frame (state %s, anim %s)" % (sstep[r].max(), a.sstate, a.sanim))

    # ---- frozen: sim moves, body does not; or a pose that does not change while the body travels
    if E.want("frozen"):
        m = ok & (st == "live") & upright & (sim_v > T["stuck_sim_ms"]) & (body_v < T["stuck_body_ms"])
        for r in runs(m, same, T["stuck_frames"]):
            a = R(r[0])
            E.add("frozen", 3 if len(r) >= 3 * T["stuck_frames"] else 2, len(r) / fps, "s", clip, a.frame, R(r[-1]).frame, [man(a)],
                  a.state, a.anim, "body still for %.1f s while the sim runs %.1f m/s (%s)" % (len(r) / fps, np.nanmean(sim_v[r]), a.sstate))
        rel = []
        for jx, jy, jz in (("hdx", "hdy", "hdz"), ("hrx", "hry", "hrz"), ("hlx", "hly", "hlz"), ("flx", "fly", "flz"), ("frx", "fry", "frz")):
            for jc, pc in ((jx, "pvx"), (jy, "pvy"), (jz, "pz")):
                v = d[jc].values.astype(float) - d[pc].values.astype(float)
                rel.append(np.abs(np.where(same, v - np.r_[np.nan, v[:-1]], np.nan)))
        jmax = np.nanmax(np.vstack(rel), axis=0) if rel else np.full(len(d), np.nan)
        m = ok & in_play & upright & (body_v > T["statue_body_ms"]) & (jmax < T["statue_cm"])
        for r in runs(m, same, T["statue_frames"]):
            a = R(r[0])
            E.add("frozen", 3, len(r) / fps, "s", clip, a.frame, R(r[-1]).frame, [man(a)], a.state, a.anim,
                  "pose unchanged for %.1f s while moving %.1f m/s" % (len(r) / fps, np.nanmean(body_v[r])))

    # ---- reference pose / no animation / no bones
    if E.want("tpose"):
        span = np.sqrt((d.hrx - d.hlx) ** 2 + (d.hry - d.hly) ** 2 + (d.hrz - d.hlz) ** 2).values
        sh = d.hdz.values - 25.0
        m = vis & upright & (st != "replay") & (span > T["tpose_span_cm"]) & (np.abs(d.hrz.values - sh) < T["tpose_shoulder_cm"]) & \
            (np.abs(d.hlz.values - sh) < T["tpose_shoulder_cm"])
        for r in runs(m, same, 2):
            a = R(r[0])
            E.add("tpose", 3, len(r) / fps, "s", clip, a.frame, R(r[-1]).frame, [man(a)], a.state, a.anim,
                  "arms spread %.0f cm at shoulder height for %d frames" % (np.nanmax(span[r]), len(r)))
        m = vis & (st != "replay") & (d.anim.fillna("").values == "")
        for r in runs(m, same, 2):
            a = R(r[0])
            E.add("tpose", 3, len(r) / fps, "s", clip, a.frame, R(r[-1]).frame, [man(a)], a.state, "",
                  "no animation on a shown man for %d frames" % len(r))
    if E.want("nan_bones"):
        m = vis & (d.pz.isna().values | d.hdz.isna().values)
        for r in runs(m, same, 1):
            a = R(r[0])
            E.add("nan_bones", 3, len(r), "frames", clip, a.frame, R(r[-1]).frame, [man(a)], a.state, a.anim,
                  "no pelvis / head position for %d frames" % len(r))

    # ---- ball in the carrier's hands
    if E.want("ball_off_hand"):
        dh = np.fmin(np.sqrt((d.bwx - d.hrx) ** 2 + (d.bwy - d.hry) ** 2 + (d.bwz - d.hrz) ** 2).values,
                     np.sqrt((d.bwx - d.hlx) ** 2 + (d.bwy - d.hly) ** 2 + (d.bwz - d.hlz) ** 2).values)
        m = vis & (d.ball.fillna(0).values == 1) & (d.ballmode.values == "held") & (st == "live") & d.bwx.notna().values & \
            (dh > T["ball_hand_cm"])
        for r in runs(m, same, T["ball_hand_frames"]):
            a = R(r[0])
            E.add("ball_off_hand", 2 if np.nanmax(dh[r]) > 45 else 1, np.nanmax(dh[r]), "cm", clip, a.frame, R(r[-1]).frame, [man(a)],
                  a.state, a.anim, "ball %.0f cm from the nearer hand for %d frames" % (np.nanmax(dh[r]), len(r)))

    # ---- feet in the turf
    if E.want("feet_under_turf"):
        z = np.fmin(d.flz.values.astype(float), d.frz.values.astype(float))
        m = vis & upright & (st != "replay") & (z < T["turf_cm"])
        for r in runs(m, same, T["turf_frames"]):
            a = R(r[0])
            E.add("feet_under_turf", 2 if np.nanmin(z[r]) < -15 else 1, -np.nanmin(z[r]), "cm", clip, a.frame, R(r[-1]).frame, [man(a)],
                  a.state, a.anim, "foot %.0f cm under the turf for %d frames" % (-np.nanmin(z[r]), len(r)))

    # ---- facing backwards while running
    if E.want("facing_backward"):
        trav = np.degrees(np.arctan2(dwy, dwx))
        off = np.abs((d.yaw.values - trav + 180.0) % 360.0 - 180.0)
        m = ok & upright & (st == "live") & (body_v > T["backward_ms"]) & (off > T["backward_deg"]) & \
            ~np.isin(d.sstate.fillna("").values, BACKWARD_OK)
        for r in runs(m, same, T["backward_frames"]):
            a = R(r[0])
            E.add("facing_backward", 1, len(r) / fps, "s", clip, a.frame, R(r[-1]).frame, [man(a)], a.state, a.anim,
                  "facing %.0f deg off the travel at %.1f m/s for %d frames (%s)" % (np.nanmedian(off[r]), np.nanmedian(body_v[r]), len(r), a.sstate))

    # ---- interpenetration: pelvises of two upright men
    if E.want("interpenetration"):
        u = d[vis & upright & in_play & (d.pz.fillna(0).values > 50) & ~np.isin(d.sanim.fillna("").values, ("tackle",)) & (d.frame.values > 4)]
        if len(u):
            px = u.pivot_table(index="frame", columns="pid", values="pvx", aggfunc="first")
            py = u.pivot_table(index="frame", columns="pid", values="pvy", aggfunc="first")
            frames = px.index.values
            fstate = d.drop_duplicates("frame").set_index("frame").state.reindex(frames).fillna("").values
            cont = np.r_[False, np.diff(frames) == 1]
            pids = list(px.columns)
            X, Y = px.values, py.values
            info = d.drop_duplicates("pid").set_index("pid")
            for a_i in range(len(pids)):
                for b_i in range(a_i + 1, len(pids)):
                    dd = np.hypot(X[:, a_i] - X[:, b_i], Y[:, a_i] - Y[:, b_i])
                    m = dd < T["overlap_cm"]
                    if not np.any(m):
                        continue
                    for r in runs(m, cont, T["overlap_frames"]):
                        mn = np.nanmin(dd[r])
                        ra, rb = info.loc[pids[a_i]], info.loc[pids[b_i]]
                        E.add("interpenetration", 3 if mn < 12 else 2 if mn < 18 else 1, mn, "cm", clip, frames[r[0]], frames[r[-1]],
                              [man(ra), man(rb)], fstate[r[0]], "", "pelvises %.0f cm apart for %d frames" % (mn, len(r)))
    return d


# ------------------------------------------------------------------ per-clip detectors (frame rows: ball, camera, officials, states)
def detect_frames(clip, d, E, fps):
    T = THRESH
    F = d.sort_values("frame").drop_duplicates("frame").reset_index(drop=True)
    if not len(F):
        return {}
    fr = F.frame.values
    cont = np.r_[False, np.diff(fr) == 1]
    st = F.state.fillna("").values
    prev_st = np.r_[[""], st[:-1]]

    if E.want("ball_teleport"):
        bw = F[["bwx", "bwy", "bwz"]].values.astype(float)
        jump = np.r_[np.nan, np.sqrt(((bw[1:] - bw[:-1]) ** 2).sum(1))]
        b = F[["bx", "by", "bz"]].values.astype(float)
        sjump = np.r_[np.nan, np.sqrt(((b[1:] - b[:-1]) ** 2).sum(1)) * YD_CM]
        bm = F.ballmode.fillna("").values
        prev_bm = np.r_[[""], bm[:-1]]
        live_ball = cont & np.isin(st, ("live", "post")) & (st == prev_st) & (bm != "dead") & (prev_bm != "dead") & (fr > 2)
        for name, jj in (("shown", jump), ("sim", sjump)):
            m = live_ball & (np.nan_to_num(jj) > T["ball_jump_cm"])
            for r in runs(m, cont, 1):
                mx = np.nanmax(jj[r])
                E.add("ball_teleport", 3 if mx > 400 else 2, mx, "cm", clip, fr[r[0]], fr[r[-1]], [], st[r[0]], "",
                      "%s ball jumped %.0f cm in one frame (ball %s)" % (name, mx, bm[r[0]]))

    if E.want("camera_in_body") and F.camnear.notna().any():
        m = F.camnear.values.astype(float) < T["lens_cm"]
        for r in runs(m, cont, 1):
            E.add("camera_in_body", 3, np.nanmin(F.camnear.values[r]), "cm", clip, fr[r[0]], fr[r[-1]], [], st[r[0]], "",
                  "lens %.0f cm from a head / pelvis for %d frames" % (np.nanmin(F.camnear.values[r]), len(r)))

    if E.want("camera_cut_storm") and F.camcut.notna().any():
        cuts = fr[F.camcut.fillna(0).values.astype(float) > 0]
        win = int(round(fps))
        i = 0
        while i < len(cuts):
            j = np.searchsorted(cuts, cuts[i] + win, side="left")
            n = j - i
            if n > T["cuts_per_sec"]:
                # one storm: extend while the next window is still over
                k = j
                while k < len(cuts) and np.searchsorted(cuts, cuts[k] + win, side="left") - k > T["cuts_per_sec"]:
                    k = np.searchsorted(cuts, cuts[k] + win, side="left")
                end = cuts[min(k, len(cuts)) - 1]
                E.add("camera_cut_storm", 2, n, "cuts/s", clip, cuts[i], end, [], st[np.searchsorted(fr, cuts[i])], "",
                      "%d cuts inside one second (%d in frames %d-%d)" % (n, int(((cuts >= cuts[i]) & (cuts <= end)).sum()), cuts[i], end))
                i = max(k, j)
            else:
                i += 1

    if E.want("carrier_off_screen") and F.camhold.notna().any():
        m = (st == "live") & (F.camhold.values.astype(float) == 0)
        for r in runs(m, cont, T["offscreen_frames"] + 1):
            E.add("carrier_off_screen", 3 if len(r) > 2 * fps else 2, len(r) / fps, "s", clip, fr[r[0]], fr[r[-1]], [], "live", "",
                  "ball carrier off screen for %.1f s" % (len(r) / fps))

    if E.want("official_near") and F.refd.notna().any():
        m = (st != "replay") & (F.refd.values.astype(float) < T["official_cm"])
        for r in runs(m, cont, T["official_frames"]):
            mn = np.nanmin(F.refd.values[r])
            who = OFFICIALS.get(int(F.refw.values[r[0]]) if not np.isnan(F.refw.values[r[0]]) else -1, "official")
            E.add("official_near", 2 if mn < 50 else 1, mn, "cm", clip, fr[r[0]], fr[r[-1]], [who], st[r[0]], "",
                  "%s %.0f cm from a player for %d frames" % (who, mn, len(r)))

    counts = pd.Series(st).value_counts().to_dict()
    if E.want("stall"):
        for s, lim in (("presnap", T["presnap_s"]), ("post", T["post_s"]), ("other", T["other_s"])):
            n = counts.get(s, 0)
            if n / fps > lim:
                f0 = fr[st == s][0]
                E.add("stall", 3 if n / fps > 3 * lim else 2, n / fps, "s", clip, f0, fr[st == s][-1], [], s, "",
                      "%s lasted %.0f s (limit %.0f)" % (s, n / fps, lim))
    return counts


# ------------------------------------------------------------------ log-level checks: clock, gaps between plays, the run
def detect_log(info, E, nclips_tele):
    T = THRESH
    fps, qlen = info["fps"], info["qlen"]
    clips = info["clips"]
    order = sorted(c for c in clips if "q" in clips[c])
    if E.want("clock"):
        prev = None
        frozen_run = []
        for c in order:
            x = clips[c]
            q, clk, a, h = x["q"], x["clock"], x["away"], x["home"]
            lim = 600.0 if q >= 5 else qlen  # (overtime is 10 minutes whatever -TagQuarterLen says: Sim/Game.cpp)
            if clk < -0.5 or clk > lim + 1:
                E.add("clock", 3, clk, "s", c, 0, 0, [], "", "", "clock %.0f outside 0..%.0f" % (clk, lim))
            if prev is not None:
                pq, pclk, pa, ph = prev[1]["q"], prev[1]["clock"], prev[1]["away"], prev[1]["home"]
                if q < pq or q > pq + 1:
                    E.add("clock", 3, q - pq, "q", c, 0, 0, [], "", "", "quarter went %d -> %d" % (pq, q))
                elif q == pq:
                    if clk > pclk + 0.5:
                        E.add("clock", 3, clk - pclk, "s", c, 0, 0, [], "", "", "clock went up %.0f -> %.0f in Q%d" % (pclk, clk, q))
                    elif pclk - clk > T["runoff_s"]:
                        E.add("clock", 2, pclk - clk, "s", c, 0, 0, [], "", "", "clock ran %.0f s between plays (%.0f -> %.0f, Q%d)" % (pclk - clk, pclk, clk, q))
                for side, now, was in (("away", a, pa), ("home", h, ph)):
                    dlt = now - was
                    if dlt < 0:
                        E.add("clock", 3, dlt, "pts", c, 0, 0, [side], "", "", "%s score went down %d -> %d" % (side, was, now))
                    elif dlt not in (0, 1, 2, 3, 6, 7, 8):
                        E.add("clock", 2, dlt, "pts", c, 0, 0, [side], "", "", "%s scored %d on one play" % (side, dlt))
                if q == pq and abs(clk - pclk) < 0.5:
                    frozen_run.append(c)
                else:
                    frozen_run = []
                if len(frozen_run) + 1 == T["frozen_clock_plays"]:
                    E.add("clock", 1, len(frozen_run) + 1, "plays", frozen_run[0], 0, 0, [], "", "",
                          "clock stuck at %s for %d plays" % (fmt_clock(q, clk), len(frozen_run) + 1))
            prev = (c, x)
    if E.want("stall"):
        hold_clips = {c for k, c in info["holds"] if k == "hold"}
        for c in order:
            nx = clips.get(c + 1)
            if not nx or "gf_end" not in clips[c] or "gf_start" not in nx:
                continue
            gap = (nx["gf_start"] - clips[c]["gf_end"]) / fps
            if gap > T["gap_s"]:
                held = c in hold_clips or (c + 1) in hold_clips
                E.add("stall", 1 if held else (3 if gap > 3 * T["gap_s"] else 2), gap, "s", c + 1, 0, 0, [], "between", "",
                      "%.0f s (game) between the end of play %d and the start of play %d%s" % (gap, c, c + 1, " (a compile hold in it)" if held else ""))
    if E.want("run"):
        last = max(clips) if clips else 0
        if not info["done"]:
            why = "watchdog timeout" if info["timeout"] else "no 'TAG hl done' line (crash, kill or still running)"
            E.add("run", 3, last, "clips", max(last, 1), 0, 0, [], "", "", "run did not finish: %s after clip %d" % (why, last))
        if clips and nclips_tele and nclips_tele < len([c for c in clips if "frames" in clips[c]]) - 1:
            E.add("run", 2, nclips_tele, "clips", 1, 0, 0, [], "", "", "telemetry for %d clips, log has %d" % (nclips_tele, len(clips)))


# ------------------------------------------------------------------ the replay command
def tokens(cmd):
    return re.findall(r'(?:[^\s"]+="[^"]*"|"[^"]*"|\S+)', cmd or "")


def repro_cmd(cmdline, clip, q):
    drop = re.compile(r"^-(TagHlNoFrames|TagHlDir=|TagHlMaxClips=|TagHlFromQ=|TagTimeout=|abslog=|TagBench|log$|game$)", re.I)
    keep = [t for t in tokens(cmdline) if not drop.match(t) and not t.lower().endswith(".uproject") and not t.lower().endswith('.uproject"')]
    if not any(t.startswith("-TagHighlights") for t in keep):
        keep.insert(0, "-TagHighlights")
    if not any(t.startswith("-TagHlFull") for t in keep):
        keep.insert(1, "-TagHlFull")
    if not any(t.lower().startswith("-usefixedtimestep") for t in keep):
        keep += ["-UseFixedTimeStep", "-FPS=30"]
    out = os.path.join(os.environ.get("LOCALAPPDATA", r"C:\Temp"), "TAGRepro", "c%03d" % clip).replace("\\", "/")
    extra = ["-TagHlMaxClips=%d" % clip, "-TagHlDir=%s/frames" % out, '-abslog="%s/run.log"' % out]
    if q and q > 1:
        extra.insert(0, "-TagHlFromQ=%d" % q)
    return '& "%s" "%s" -game %s %s' % (EDITOR, UPROJECT, " ".join(keep), " ".join(extra))


# ------------------------------------------------------------------ budget
def load_budget(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def check_budget(summary, budget, plays):
    rows = []
    over = False
    b = budget.get("classes", {})
    for cls in CLASSES:
        s = summary.get(cls, {"count": 0, "high": 0, "per100": 0.0, "high_per100": 0.0})
        lim = b.get(cls, {})
        verdict = "-"
        why = []
        # allowed = rate x plays + a fixed allowance (so one event in a short run does not fail a small rate)
        if lim.get("per100") is not None:
            cap = lim["per100"] * plays / 100.0 + lim.get("allow", 0)
            if s["count"] > cap:
                why.append("%d > %.1f (%.1f/100 plays + %d)" % (s["count"], cap, lim["per100"], lim.get("allow", 0)))
        if lim.get("high_per100") is not None:
            cap = lim["high_per100"] * plays / 100.0 + lim.get("high_allow", 0)
            if s["high"] > cap:
                why.append("high %d > %.1f" % (s["high"], cap))
        if lim:
            verdict = "OVER" if why else "ok"
        over |= bool(why)
        rows.append({"cls": cls, "count": s["count"], "high": s["high"], "per100": s["per100"], "high_per100": s["high_per100"],
                     "budget": lim, "verdict": verdict, "why": "; ".join(why)})
    return over, rows


ZERO_TOLERANCE = ("tpose", "nan_bones", "camera_in_body", "clock", "run")


def propose(summaries):
    """Budgets from one or more baseline runs: the worst rate seen x1.25, plus a fixed allowance of 2 events (1 high);
    zero-tolerance classes get no allowance and keep the baseline's own rate (0 when it was clean)."""
    ceil1 = lambda v: math.ceil(v * 10 - 1e-9) / 10.0
    out = {}
    for cls in CLASSES:
        rate = max(s.get(cls, {}).get("per100", 0.0) for s in summaries)
        hi = max(s.get(cls, {}).get("high_per100", 0.0) for s in summaries)
        if cls in ZERO_TOLERANCE:
            out[cls] = {"per100": ceil1(rate), "allow": 0, "high_per100": ceil1(hi), "high_allow": 0}
        else:
            out[cls] = {"per100": ceil1(rate * 1.25), "allow": 2, "high_per100": ceil1(hi * 1.25), "high_allow": 1}
    return out


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description="TAG defect list from -TagHlFull telemetry (07 A6).")
    ap.add_argument("path", help="run folder (with telemetry/ and run.log) or the telemetry folder itself")
    ap.add_argument("--log", help="the run's log (default <run>/run.log)")
    ap.add_argument("--out-dir", help="where defects.json / defects.md go (default the run folder)")
    ap.add_argument("--top", type=int, default=8, help="events listed per class in the markdown (default 8)")
    ap.add_argument("--budget", default=DEFAULT_BUDGET, help="budget file (default Tools/QA/defect_budget.json)")
    ap.add_argument("--no-budget", action="store_true", help="report only, exit 0")
    ap.add_argument("--propose-budget", help="write a budget file proposed from this run (and --also runs)")
    ap.add_argument("--also", nargs="*", default=[], help="other runs' defects.json to take into --propose-budget")
    ap.add_argument("--only", default="", help="comma list of classes")
    ap.add_argument("--clips", default="", help="only these clips, e.g. 1-20 or 3,7,9")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    tele, run, log = find_inputs(a.path, a.log)
    files = sorted(glob.glob(os.path.join(tele, "c[0-9][0-9][0-9].csv")))
    if a.clips:
        want = set()
        for part in a.clips.split(","):
            lo, _, hi = part.partition("-")
            want |= set(range(int(lo), int(hi or lo) + 1))
        files = [f for f in files if int(os.path.basename(f)[1:4]) in want]
    if not files:
        print("defects: no c<clip>.csv in %s" % tele)
        return 2
    only = {c.strip() for c in a.only.split(",") if c.strip()}
    bad = only - set(CLASSES)
    if bad:
        print("defects: unknown classes %s (classes: %s)" % (", ".join(sorted(bad)), ", ".join(CLASSES)))
        return 2
    info = parse_log(log)
    fps = info["fps"]
    E = Events(only)
    frames_total = 0
    state_frames = {}
    for f in files:
        clip = int(os.path.basename(f)[1:4])
        try:
            d = load_clip(f)
        except (pd.errors.ParserError, pd.errors.EmptyDataError, ValueError) as e:
            E.add("run", 2, 0, "", clip, 0, 0, [], "", "", "unreadable telemetry %s: %s" % (os.path.basename(f), e))
            continue
        if not len(d):
            continue
        detect_clip(clip, d, E, fps)
        cnt = detect_frames(clip, d, E, fps)
        frames_total += int(d.frame.nunique())
        for k, v in cnt.items():
            state_frames[k] = state_frames.get(k, 0) + int(v)
    detect_log(info, E, len(files))

    plays = len(files)
    # game time, play and replay command on every event
    for e in E.ev:
        c = info["clips"].get(e["clip"], {})
        e["q"] = c.get("q")
        e["clock_at_end"] = c.get("clock")
        e["game_time"] = fmt_clock(c.get("q"), c.get("clock")) + " (play end)" if "q" in c else "?"
        e["t_in_clip_s"] = round(e["frame"] / fps, 2)
        e["desc"] = c.get("desc", "")[:140]
        e["replay"] = repro_cmd(info["cmdline"], e["clip"], c.get("q"))
    # rank: severity, then size against the class's own spread
    by = {}
    for e in E.ev:
        by.setdefault(e["cls"], []).append(e)
    for cls, lst in by.items():
        sizes = np.array([abs(x["size"]) for x in lst])
        med = float(np.median(sizes)) if len(sizes) else 1.0
        for x in lst:
            x["score"] = round(x["sev"] * 10 + min(9.9, abs(x["size"]) / max(med, 1e-6)), 2)
        lst.sort(key=lambda x: -x["score"])
    summary = {}
    for cls in CLASSES:
        lst = by.get(cls, [])
        hi = sum(1 for x in lst if x["sev"] >= 3)
        summary[cls] = {"count": len(lst), "high": hi, "per100": round(len(lst) * 100.0 / max(plays, 1), 2),
                        "high_per100": round(hi * 100.0 / max(plays, 1), 2), "clips_with": len({x["clip"] for x in lst})}
    ranked = sorted(E.ev, key=lambda x: -x["score"])

    budget, over, rows, budget_note = None, False, None, ""
    if not a.no_budget and a.budget and os.path.isfile(a.budget):
        try:
            budget = load_budget(a.budget)
            over, rows = check_budget(summary, budget, plays)
            budget_note = a.budget
        except (OSError, ValueError) as e:
            print("defects: cannot read budget %s: %s" % (a.budget, e))
            return 2
    elif not a.no_budget and a.budget != DEFAULT_BUDGET:
        print("defects: no budget file %s" % a.budget)
        return 2
    if rows is None:
        _, rows = check_budget(summary, {"classes": {}}, plays)

    out_dir = a.out_dir or run
    os.makedirs(out_dir, exist_ok=True)
    result = {"telemetry": tele, "log": log, "cmdline": info["cmdline"], "plays": plays, "frames": frames_total,
              "fps": fps, "finished": info["done"], "final_score": info["final"], "state_frames": state_frames,
              "thresholds": THRESH, "summary": summary, "budget_file": budget_note, "over_budget": over, "budget_rows": rows,
              "events": ranked}
    jpath = os.path.join(out_dir, "defects.json")
    with open(jpath, "w", encoding="utf-8") as fh:
        json.dump(result, fh, indent=1, default=lambda o: o.item() if hasattr(o, "item") else str(o))
    mpath = os.path.join(out_dir, "defects.md")
    write_md(mpath, result, by, a.top)

    if a.propose_budget:
        sums = [summary]
        for p in a.also:
            with open(p, encoding="utf-8") as fh:
                sums.append(json.load(fh)["summary"])
        prop = {"version": 1, "provisional": False,
                "note": "proposed by defects.py --propose-budget from %d run(s): %s" % (len(sums), ", ".join([tele] + list(a.also))),
                "unit": "a class fails when count > per100 x plays / 100 + allow (high: severity-3 events, high_per100 / high_allow)",
                "classes": propose(sums)}
        with open(a.propose_budget, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(prop, fh, indent=1)
            fh.write("\n")
        print("budget proposed -> %s" % a.propose_budget)

    if not a.quiet:
        print("defects: %d plays, %d frames, %d events (%d high)%s" % (plays, frames_total, len(E.ev), sum(1 for x in E.ev if x["sev"] >= 3),
                                                                      "" if info["done"] else "  [run not finished]"))
        print("%-19s %6s %5s %8s %8s  %s" % ("class", "count", "high", "/100", "budget", "verdict"))
        for r in rows:
            lim = r["budget"].get("per100") if r["budget"] else None
            print("%-19s %6d %5d %8.1f %8s  %s %s" % (r["cls"], r["count"], r["high"], r["per100"], "-" if lim is None else "%.1f" % lim, r["verdict"], r["why"]))
        print("-> %s" % mpath)
    if budget is None:
        print("DEFECTS REPORT: %d events, no budget checked" % len(E.ev))
        return 0
    if over:
        print("DEFECTS FAIL: over budget in %s" % ", ".join(r["cls"] for r in rows if r["verdict"] == "OVER"))
        return 1
    print("DEFECTS PASS: every class within budget (%s)" % os.path.basename(budget_note))
    return 0


def write_md(path, R, by, top):
    L = ["# Defects: %s" % R["telemetry"], ""]
    L.append("%d plays, %d frames at %.0f fps, run %s%s." % (R["plays"], R["frames"], R["fps"], "finished" if R["finished"] else "NOT finished",
                                                               (", final %d-%d" % tuple(R["final_score"])) if R["final_score"] else ""))
    L.append("")
    L.append("Command line: `%s`" % (R["cmdline"][:600] or "?"))
    L.append("")
    L.append("| class | count | high | per 100 plays | budget | verdict |")
    L.append("|---|---:|---:|---:|---:|---|")
    for r in R["budget_rows"]:
        lim = r["budget"].get("per100") if r["budget"] else None
        L.append("| %s | %d | %d | %.1f | %s | %s %s |" % (r["cls"], r["count"], r["high"], r["per100"], "-" if lim is None else "%.1f" % lim,
                                                         r["verdict"], r["why"]))
    L.append("")
    for cls in CLASSES:
        lst = by.get(cls, [])
        if not lst:
            continue
        L.append("## %s (%d)" % (cls, len(lst)))
        L.append("")
        for e in lst[:top]:
            L.append("- **sev %d** %s %s: clip %d frame %d-%d (%.1f s in), %s, %s%s" % (
                e["sev"], e["size"], e["unit"], e["clip"], e["frame"], e["frame_end"], e["t_in_clip_s"], e["game_time"],
                e["detail"], (" -- " + ", ".join(e["players"])) if e["players"] else ""))
            if e["anim"]:
                L.append("  - anim `%s`, state %s" % (e["anim"], e["state"]))
            if e["desc"]:
                L.append("  - play: %s" % e["desc"])
            L.append("  - replay: `%s`" % e["replay"])
        L.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(2)
