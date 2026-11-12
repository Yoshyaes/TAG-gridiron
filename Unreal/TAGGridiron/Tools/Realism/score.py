# Realism scorecard v2: score a build against rubric.json.
#   python score.py --sim sim.json [--game game.json] [--box box.json] [--visual visual_checks.json] [--out score.json] [--md score.md]
# sim.json:  Tools/build/SimCalibrate.exe 200 11 --json sim.json   (run with TAG_ACCEL=1 for the acceleration metrics)
# game.json: python analyze.py <telemetry dir> <game log> game.json   (a -TagHighlights -TagHlFull capture)
# box.json:  python gamestats.py box.json <game log>
# A metric scores 10 inside [lo, hi], falling linearly to 0 at fail_lo / fail_hi. A category is the weighted mean of its
# measured metrics, capped by its open visual checks (high -> 8.0, medium -> 9.0); a category with no metrics scores
# 10 - 2 per open high - 1 per medium - 0.5 per low. 'review' checks count as open. Unmeasured metrics are listed, not scored.
import argparse, json, os

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--sim")
ap.add_argument("--game")
ap.add_argument("--box")
ap.add_argument("--visual", default=os.path.join(HERE, "visual_checks.json"))
ap.add_argument("--rubric", default=os.path.join(HERE, "rubric.json"))
ap.add_argument("--out")
ap.add_argument("--md")
a = ap.parse_args()

rub = json.load(open(a.rubric, encoding="utf-8"))
src = {"sim": json.load(open(a.sim)) if a.sim else {},
       "game": json.load(open(a.game))["R"] if a.game else {},
       "box": json.load(open(a.box)) if a.box else {}}


def derived(name):
    g = src["game"]
    if not g:
        return None
    if name == "max_speed":
        return max(v["game_max"] for v in g["top_speed_mph"].values())
    if name == "pops_per_game":  # per full game (45,000 recorded frames, the 2026-11-11 capture)
        pops = g.get("body_pops", g["actor_teleports"])["count"]  # the visible body; older captures only have the root
        return pops * 45000 / max(g["frames"], 1)
    return None


def value(spec):
    kind, key = spec.split(":", 1)
    if kind == "game" and key.startswith("derived."):
        return derived(key[8:])
    d = src[kind]
    if not d:
        return None
    if kind == "sim":
        return d.get(key)
    for part in key.split("."):
        if not isinstance(d, dict) or part not in d:
            return None
        d = d[part]
    return d if isinstance(d, (int, float)) else None


def metric_score(m, v):
    if v is None:
        return None
    if m["lo"] <= v <= m["hi"]:
        return 10.0
    if v < m["lo"]:
        span = m["lo"] - m["fail_lo"]
        return max(0.0, 10.0 * (1 - (m["lo"] - v) / span)) if span > 0 else 0.0
    span = m["fail_hi"] - m["hi"]
    return max(0.0, 10.0 * (1 - (v - m["hi"]) / span)) if span > 0 else 0.0


checks = json.load(open(a.visual, encoding="utf-8"))["checks"] if a.visual and os.path.exists(a.visual) else []
out = {"categories": {}, "metrics": [], "unmeasured": [], "open_checks": []}
for cid, cat in rub["categories"].items():
    ms = [m for m in rub["metrics"] if m["cat"] == cid]
    num = den = 0.0
    rows = []
    for m in ms:
        v = value(m["src"])
        s = metric_score(m, v)
        row = dict(id=m["id"], cat=cid, label=m["label"], value=v, band=[m["lo"], m["hi"]], score=s, ref=m["ref"])
        rows.append(row)
        out["metrics"].append(row)
        if s is None:
            out["unmeasured"].append(m["id"])
            continue
        num += s * m["w"]
        den += m["w"]
    opn = [c for c in checks if c["cat"] == cid and c["status"] != "closed"]
    out["open_checks"] += [dict(c) for c in opn]
    nh = sum(c["sev"] == "high" for c in opn)
    nm = sum(c["sev"] == "medium" for c in opn)
    nl = sum(c["sev"] == "low" for c in opn)
    if den > 0:
        base = num / den
        cap = 8.0 if nh else 9.0 if nm else 10.0
        score = min(base, cap)
    elif ms:
        base = cap = score = None  # metrics defined but none measured: not scored (left out of the overall)
    else:
        base = None
        cap = None
        score = max(0.0, 10 - 2 * nh - 1 * nm - 0.5 * nl)
    out["categories"][cid] = dict(label=cat["label"], weight=cat["weight"], score=round(score, 2) if score is not None else None,
                                  metric_score=round(base, 2) if base is not None else None, cap=cap,
                                  open_high=nh, open_medium=nm, open_low=nl,
                                  measured=sum(r["score"] is not None for r in rows), metrics=len(rows))
scored_ = [c for c in out["categories"].values() if c["score"] is not None]
tw = sum(c["weight"] for c in scored_)
out["overall"] = round(sum(c["score"] * c["weight"] for c in scored_) / tw, 2) if tw else None
out["overall_covers_weight"] = tw

lines = [f"Realism scorecard v2: overall {out['overall']:.2f} / 10 (categories scored: {tw} of 100 weight)", ""]
lines.append(f"{'category':26s} {'score':>6s} {'metrics':>8s} {'cap':>5s}  open H/M/L  measured")
for cid, c in out["categories"].items():
    ms = f"{c['metric_score']:.2f}" if c["metric_score"] is not None else "  -  "
    cp = f"{c['cap']:.0f}" if c["cap"] is not None else " - "
    sc = f"{c['score']:6.2f}" if c["score"] is not None else "   n/a"
    lines.append(f"{c['label']:26s} {sc} {ms:>8s} {cp:>5s}  {c['open_high']}/{c['open_medium']}/{c['open_low']}        {c['measured']}/{c['metrics']}")
lines.append("")
lines.append("metrics below 10:")
for r in sorted(out["metrics"], key=lambda r: (r["score"] is None, r["score"] if r["score"] is not None else 0)):
    if r["score"] is None or r["score"] < 10:
        val = "unmeasured" if r["value"] is None else f"{r['value']:.3g}"
        sc = "  -" if r["score"] is None else f"{r['score']:4.1f}"
        lines.append(f"  {sc}  {r['cat']:12s} {r['label']:62s} {val:>10s}  band {r['band'][0]:g}-{r['band'][1]:g}  [{r['ref']}]")
text = "\n".join(lines)
print(text)
if a.out:
    json.dump(out, open(a.out, "w"), indent=1)
if a.md:
    open(a.md, "w", encoding="utf-8").write("```\n" + text + "\n```\n")
