# Box-score style numbers from the play log ("TAG hl clip N end ... desc" lines) of one or more logs (later logs win
# for the same clip number). usage: python gamestats.py <out.json> <log> [<log> ...]
import json, re, sys
out, logs = sys.argv[1], sys.argv[2:]
desc, meta = {}, {}
for lg in logs:
    for l in open(lg, encoding="utf-8", errors="replace"):
        m = re.search(r'TAG hl clip (\d+) end frames (\d+) big (\d+) desc "(.*)"', l)
        if m:
            desc[int(m.group(1))] = m.group(4)
        m = re.search(r"TAG hl clip (\d+) game q(\d) clock (\d+) score (\d+)-(\d+)", l)
        if m:
            meta[int(m.group(1))] = dict(q=int(m.group(2)), clock=int(m.group(3)), away=int(m.group(4)), home=int(m.group(5)))
S = dict(plays=0, rush=0, rush_yd=0, pass_att=0, comp=0, pass_yd=0, sacks=0, sack_yd=0, ints=0, fumbles=0, fum_lost=0, td=0,
         punts=0, punt_yd=0, fg_att=0, fg_made=0, xp_att=0, xp_made=0, two_pt=0, penalties=0, kickoffs=0, ko_ret=0, ko_tb=0,
         spikes=0, kneels=0, scrambles=0, drops=0, long=0)
pen_kinds = {}
for c in sorted(desc):
    d = desc[c]
    def yds():
        m = re.search(r"for (?:a )?loss of (\d+)", d)
        if m: return -int(m.group(1))
        if "no gain" in d: return 0
        m = re.search(r"for (\d+) yards?", d)
        return int(m.group(1)) if m else 0
    if "PENALTY" in d:
        S["penalties"] += 1
        m = re.search(r"PENALTY on \w+: ([A-Za-z ]+?)[,.]", d)
        if m: pen_kinds[m.group(1)] = pen_kinds.get(m.group(1), 0) + 1
    if re.search(r"\bkicks \d+ yards", d):
        S["kickoffs"] += 1
        S["ko_tb"] += "Touchback" in d
        S["ko_ret"] += "returned" in d
        continue
    if " punts " in d:
        S["punts"] += 1
        m = re.search(r"punts (\d+) yards", d)
        S["punt_yd"] += int(m.group(1)) if m else 0
        continue
    if "field goal" in d:
        S["fg_att"] += 1; S["fg_made"] += "is GOOD" in d; continue
    if "extra point" in d:
        S["xp_att"] += 1; S["xp_made"] += "is GOOD" in d; continue
    if "spike" in d.lower():
        S["spikes"] += 1; continue
    if "kneel" in d.lower():
        S["kneels"] += 1; continue
    if re.match(r"^PENALTY", d) or ("No Play" in d):
        continue
    S["plays"] += 1
    y = yds()
    S["long"] = max(S["long"], y)
    if "SACKED" in d:
        S["sacks"] += 1; S["sack_yd"] += -y
    elif " pass " in d:
        S["pass_att"] += 1
        if "incomplete" not in d and "INTERCEPTED" not in d.upper():
            S["comp"] += 1; S["pass_yd"] += y
        S["drops"] += "dropped" in d
        S["ints"] += "INTERCEPT" in d.upper()
    elif "scrambles" in d:
        S["scrambles"] += 1; S["rush"] += 1; S["rush_yd"] += y
    else:
        S["rush"] += 1; S["rush_yd"] += y
    if "FUMBLE" in d:
        S["fumbles"] += 1; S["fum_lost"] += "offense recovers" not in d
    S["td"] += "TOUCHDOWN" in d
last = meta[max(meta)] if meta else {}
dropbacks = S["pass_att"] + S["sacks"]
R = dict(S, clips=len(desc), final=last, penalty_kinds=pen_kinds,
         ypc=round(S["rush_yd"] / max(S["rush"], 1), 2), comp_pct=round(100 * S["comp"] / max(S["pass_att"], 1), 1),
         ypa=round(S["pass_yd"] / max(S["pass_att"], 1), 2), sack_rate=round(100 * S["sacks"] / max(dropbacks, 1), 1),
         int_rate=round(100 * S["ints"] / max(S["pass_att"], 1), 1),
         pass_share=round(100 * dropbacks / max(S["plays"], 1), 1),
         yds_per_play=round((S["rush_yd"] + S["pass_yd"] - S["sack_yd"]) / max(S["plays"], 1), 2),
         punt_avg=round(S["punt_yd"] / max(S["punts"], 1), 1),
         plays_per_team=round(S["plays"] / 2, 1), points_total=(last.get("away", 0) + last.get("home", 0)))
json.dump(R, open(out, "w"), indent=1)
print(json.dumps(R, indent=1))
