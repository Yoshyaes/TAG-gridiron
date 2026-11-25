# Fit the sim's tag::Tune constants to the NFL bands in rubric.json (headless, SimCalibrate --set).
#   python fit.py [--games 100] [--seed 11] [--rounds 12] [--jobs 6] [--out fit_result.json]
# Loss: for every rubric metric read from the sim, the squared distance outside its band (in units of the band-to-fail
# distance), times its weight; plus each box-score stat's z beyond 1 (SimCalibrate's NFL table). Coordinate search with
# common random numbers (one seed for every candidate, so differences are the parameters, not noise), step halving, and a
# held-out validation on another seed at the end.
import argparse, json, os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "..", "build", "SimCalibrate.exe")
ap = argparse.ArgumentParser()
ap.add_argument("--games", type=int, default=100)
ap.add_argument("--seed", type=int, default=11)
ap.add_argument("--val-seed", type=int, default=29)
ap.add_argument("--rounds", type=int, default=12)
ap.add_argument("--jobs", type=int, default=6)
ap.add_argument("--out", default=os.path.join(HERE, "fit_result.json"))
ap.add_argument("--start", help="a previous fit_result.json to start from")
ap.add_argument("--seeds", help="several seeds, comma separated: the loss is the mean over them (one seed fits its own noise: a fit at 100 games on seed 11 took the loss from 6.5 to 0.5 there and from 6.9 to 7.1 on seed 29)")
ap.add_argument("--only", help="fit these parameters only (comma separated); the rest stay at their defaults")
ap.add_argument("--exe", default=EXE, help="SimCalibrate build to run (a copy, so the tools can be rebuilt during a fit)")
ap.add_argument("--fixed", default="", help="k=v,k2=v2: tunes set on every run and not fitted (a feature switched on for the fit, e.g. comboBlock=1)")
ap.add_argument("--gap-w", type=float, default=0.0, help="weight of the run-gap term: designed runs' share and yards a carry by where they went "
                "(SimCalibrate gap.*) against nflverse 2023's run_gap classes - one unit a class 4 points of share or 0.5 yd off")
a = ap.parse_args()
FIXED = [kv for kv in a.fixed.split(",") if kv]
# nflverse 2023 regular-season designed runs by run_gap class: share %, yards a carry
GAP_NFL = {"middle": (26.4, 3.70), "guard": (26.0, 4.07), "tackle": (22.6, 4.07), "end": (24.3, 4.88)}
EXE = a.exe

# name: (default, lo, hi, first step)
P = {
    "tackleBase": (3.5, 2.4, 4.2, 0.25),
    "behindK": (0.25, 0.0, 1.5, 0.15),
    "accBase": (0.19, 0.18, 0.50, 0.04),
    "accSlope": (0.0157, 0.008, 0.03, 0.003),
    "deepErr": (0.0, 0.0, 0.04, 0.006),
    "deepReadK": (1.68, 0.5, 3.0, 0.3),
    "deepReadSlope": (0.16873, 0.04, 0.35, 0.04),
    "hotDeepK": (0.13598, 0.0, 2.5, 0.3),
    "hotDeepSlope": (0.07041, 0.0, 0.3, 0.04),
    "manCushion": (1.1, 0.4, 3.0, 0.3),
    "manLagK": (0.6, 0.3, 1.0, 0.15),
    # (man coverage by the body, Research/NextLevel/02 A1: the cut reaction, the drive on a break, the lead it plays)
    "manCutBase": (0.2, 0.04, 0.4, 0.05),
    "manDriveLead": (1.2, 0.0, 2.5, 0.3),
    "manDriveT": (0.3, 0.1, 0.6, 0.08),
    "manLead": (0.35, 0.2, 0.7, 0.06),
    "manCueCushion": (0.6, 0.0, 2.0, 0.25),
    # (leverage at engagement, A5: where the edge is even, how far past the blocker's middle he is by him, its spread)
    "levCen": (0.9, 0.3, 1.5, 0.15),
    "levHip": (0.3, 0.0, 0.8, 0.1),
    "levK": (6.0, 2.0, 12.0, 1.5),
    "levSd": (4.0, 1.0, 8.0, 1.0),
    # (read keys, A4: how fast a man reads them, how long he holds, how hard a run read pulls him up)
    "keyTau": (0.15, 0.05, 0.4, 0.05),
    "keyTauK": (0.006, 0.0, 0.015, 0.002),
    "keyStepT": (0.0, 0.0, 0.4, 0.08),
    "keyBiteK": (3.0, 0.5, 4.0, 0.5),
    "keyFill": (4.0, 0.0, 5.0, 0.5),
    "zoneEyes": (4.0, 0.0, 8.0, 1.5),
    "reachA": (14, 6.0, 30.0, 3.0),
    "midContest": (1.0, 0.0, 1.0, 0.25),
    "shortPen": (0.0, 0.0, 2.0, 0.4),
    "passProHold": (3.8, 2.5, 5.0, 0.4),
    "zoneReactK": (0.3, 0.2, 1.0, 0.15),
    "zoneUnder": (1.2, 0.3, 2.0, 0.3),
    "zoneCushion": (3.0, 1.5, 5.0, 0.4),
    "zoneCushionV": (0.55, 0.2, 0.9, 0.08),
    "routeRead": (0.75, 0.0, 1.0, 0.15),
    "passProShed": (0.08, 0.02, 0.12, 0.012),
    "qbThresh": (3.06, 1.6, 3.6, 0.25),
    "qbDecay": (0.25352, 0.1, 0.45, 0.05),
    "qbLate": (0.7, 0.2, 1.4, 0.15),
    "tightDeep": (1.9527, 1.0, 3.5, 0.3),
    "tightShort": (2.51944, 0.6, 2.8, 0.25),
    "contested": (0.45, 0.3, 1.4, 0.15),
    "breakdownBase": (1.32, 0.5, 3.0, 0.3),
    "breakdownK": (1.98, 0.8, 3.0, 0.3),
    "catchKeep": (0.22, 0.25, 0.7, 0.06),
    "penScale": (1.10, 0.8, 2.0, 0.15),
    "assistRange": (1.6, 1.4, 4.0, 0.4),
    "tackleReach": (0.44, 0.3, 0.9, 0.08),
    "pursuitK": (0.97, 0.4, 1.1, 0.1),
    "deepKeep": (0.8, 0.0, 1.5, 0.25),
    "yacNoise": (2.5, 1.0, 4.0, 0.4),
    "runNoise": (1.0, 0.5, 2.0, 0.2),
    "releaseRate": (5, 1.0, 20.0, 3.0),
    "intBias": (2.3, -0.5, 4.0, 0.4),
    "runShed": (0.048, 0.03, 0.2, 0.02),
    "hotRate": (0.5, 0.3, 2.0, 0.25),
    "outletPen": (0.4303, 0.0, 1.5, 0.25),
    # (abilities on: the league-wide tackle logit and throw placement factor that hold the average where the NFL has it)
    "abilAcc": (1.04352, 0.85, 1.2, 0.03),
    "abilTackle": (0.28, -0.3, 0.6, 0.08),
    "screenW": (0.85, 0.8, 3.0, 0.3),
    "secondLong": (0.6, 0.0, 2.5, 0.4),
    "paBite": (2.0, 0.5, 3.0, 0.3),
    "throwAwayK": (0.4568, 0.1, 1.5, 0.2),
    "behindHotK": (1.0, 0.0, 1.5, 0.3),
    "driveBase": (1.3, 0.2, 3.0, 0.3),
    "engArmK": (2.0, 0.5, 4.0, 0.5),
    "lbFill": (0.0, 0.0, 1.0, 0.3),
    "lbRead": (0.8, 0.0, 1.2, 0.2),
    "pursueAvoid": (1.0, 0.0, 2.0, 0.4),
    "runPress": (1.0, 0.6, 1.0, 0.1),
    "holdCall": (0.46, 0.3, 0.6, 0.05),
    "paRead": (1.0, 0.4, 1.2, 0.15),
    "paDrop": (1.0, 0.4, 1.2, 0.15),
    "scrCov": (0.35, 0.1, 1.0, 0.1),
    "scrZoneT": (1.1, 0.3, 1.2, 0.15),
    "scrGrass": (8.0, 4.0, 12.0, 1.5),
    "quickMinT": (0.7, 0.5, 1.6, 0.2),
    "lvl2Shed": (10.0, 2.0, 14.0, 1.5),
    "lvl2Near": (1.36, 1.0, 3.5, 0.4),
    "accMidK": (0.6, 0.0, 0.8, 0.1),
    "rushEarly": (0.6, 0.3, 1.2, 0.1),
    "rushLate": (2.5, 1.0, 4.0, 0.4),
    "fumbleK": (0.704, 0.4, 1.5, 0.15),
    "slideK": (0.654, 0.2, 0.9, 0.15),
    # (backs read blocks, 02 A7: the open / shut side of a blocked man, the press of the aiming point; on by default)
    "readOpen": (0.6, 0.2, 1.2, 0.15),
    "readShut": (0.05, 0.0, 0.25, 0.05),
    "readPress": (0.25, 0.05, 0.6, 0.08),
    # (combos, gap fits, match coverage, stunts: off by default - fit them with --fixed comboBlock=1 etc.)
    "comboT": (0.1, 0.0, 0.8, 0.15),
    "comboReach": (0.9, 0.3, 1.2, 0.15),
    "gapGive": (1.2, 0.3, 4.0, 0.6),
    "gapFlow": (0.8, 0.0, 1.2, 0.2),
    "matchCarry": (11, 7, 15, 1.5),
    "stunts": (0.0, 0.0, 0.5, 0.08),
    "runNearShed": (3.2, 1.0, 5.0, 0.6),
    "armShut": (0.2, -0.4, 0.8, 0.2),
}
SEEDS = [int(x) for x in a.seeds.split(",")] if a.seeds else []
if a.only:
    P = {k: v for k, v in P.items() if k in a.only.split(",")}
rub = json.load(open(os.path.join(HERE, "rubric.json"), encoding="utf-8"))
SIM_METRICS = [m for m in rub["metrics"] if m["src"].startswith("sim:") and not m["src"].startswith("sim:accel.")
               and m["src"] != "sim:box.offTolerance"]


def run1(params, seed, games):
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    cmd = [EXE, str(games), str(seed), "--json", path]
    for kv in FIXED:
        cmd += ["--set", kv]
    for k, v in params.items():
        cmd += ["--set", f"{k}={v:.6g}"]
    subprocess.run(cmd, capture_output=True, check=True)
    J = json.load(open(path))
    os.remove(path)
    return J


def run(params, seed, games):
    """One seed's result, or (fitting over several seeds) the list of every seed's."""
    if SEEDS and seed == a.seed:
        return [run1(params, s, games) for s in SEEDS]
    return run1(params, seed, games)


def loss(J):
    if isinstance(J, list):
        ls = [loss(j) for j in J]
        parts = {}
        for _, p in ls:
            for k, v in p.items():
                parts[k] = round(parts.get(k, 0) + v / len(ls), 3)
        return sum(l for l, _ in ls) / len(ls), parts
    L = 0.0
    parts = {}
    for m in SIM_METRICS:
        v = J.get(m["src"][4:])
        if v is None:
            continue
        if v < m["lo"]:
            d = (m["lo"] - v) / max(m["lo"] - m["fail_lo"], 1e-9)
        elif v > m["hi"]:
            d = (v - m["hi"]) / max(m["fail_hi"] - m["hi"], 1e-9)
        else:
            d = 0.0
        x = m["w"] * 10 * d * d
        parts[m["id"]] = round(x, 3)
        L += x
    for k, z in J.items():
        if k.startswith("boxz."):
            x = max(0.0, abs(z) - 1.0) ** 2 * 0.5
            if x:
                parts[k] = round(x, 3)
            L += x
    if a.gap_w > 0 and "gap.middle.share" in J:
        for g, (s, y) in GAP_NFL.items():
            x = a.gap_w * (((J[f"gap.{g}.share"] - s) / 4) ** 2 + ((J[f"gap.{g}.ypc"] - y) / 0.5) ** 2)
            parts["gap." + g] = round(x, 3)
            L += x
    return L, parts


cur = {k: v[0] for k, v in P.items()}
if a.start:
    cur.update({k: v for k, v in json.load(open(a.start))["params"].items() if k in P})
step = {k: v[3] for k, v in P.items()}
pool = ThreadPoolExecutor(a.jobs)
base = run(cur, a.seed, a.games)
best, parts = loss(base)
print(f"start loss {best:.3f}  off-tolerance {(base[0] if isinstance(base, list) else base)['box.offTolerance']:.0f}", flush=True)
hist = [dict(round=0, loss=best, params=dict(cur))]
for rnd in range(1, a.rounds + 1):
    cands = []
    for k in P:
        for sgn in (-1, 1):
            v = min(P[k][2], max(P[k][1], cur[k] + sgn * step[k]))
            if abs(v - cur[k]) > 1e-12:
                c = dict(cur)
                c[k] = v
                cands.append((k, v, c))
    res = list(pool.map(lambda kc: (kc[0], kc[1], loss(run(kc[2], a.seed, a.games))[0]), cands))
    res.sort(key=lambda r: r[2])
    moved = []
    # take the best move, then any other improving move on a different parameter (re-checked together)
    if res and res[0][2] < best:
        trial = dict(cur)
        for k, v, l in res:
            if l < best and k not in moved and len(moved) < 4:
                trial[k] = v
                moved.append(k)
        lt = loss(run(trial, a.seed, a.games))[0]
        if lt < res[0][2]:
            cur, best = trial, lt
        else:
            cur[res[0][0]] = res[0][1]
            best = res[0][2]
            moved = [res[0][0]]
    tried = {r[0] for r in res if r[2] >= best}
    for k in P:
        if k not in moved:
            step[k] *= 0.6
    hist.append(dict(round=rnd, loss=best, moved=moved, params=dict(cur)))
    print(f"round {rnd}: loss {best:.3f} moved {moved}", flush=True)
    json.dump(dict(params=cur, history=hist), open(a.out, "w"), indent=1)
    if all(step[k] < P[k][3] * 0.05 for k in P):
        break

final = run(cur, a.seed, a.games)
val = run(cur, a.val_seed, 200)
val0 = run({k: v[0] for k, v in P.items()}, a.val_seed, 200)
lv, pv = loss(val)
lv0, _ = loss(val0)
print(f"validation seed {a.val_seed} (200 games): loss {lv:.3f} vs defaults {lv0:.3f}; off-tolerance {val['box.offTolerance']:.0f} vs {val0['box.offTolerance']:.0f}")
print("remaining loss parts:", json.dumps(dict(sorted(pv.items(), key=lambda kv: -kv[1])[:12])))
json.dump(dict(params=cur, history=hist, validation=val, validation_defaults=val0, validation_loss=lv, validation_loss_defaults=lv0),
          open(a.out, "w"), indent=1)
