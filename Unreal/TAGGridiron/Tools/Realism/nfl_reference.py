# NFL reference numbers for the sim's down-and-distance chain, from nflverse play-by-play (public).
# Data: curl -L -o pbp2023.csv.gz https://github.com/nflverse/nflverse-data/releases/download/pbp/play_by_play_2023.csv.gz
#   python nfl_reference.py pbp2023.csv.gz   (2023 output is quoted in rubric.json refs.nflverse_2023 and PlaySim/Game comments)
# NFL 2023 reference numbers from nflverse play-by-play (regular season), to compare the sim's down-and-distance chain
import sys, pandas as pd, numpy as np
P = pd.read_csv(sys.argv[1], low_memory=False, usecols=["season_type", "down", "ydstogo", "play_type", "yards_gained", "pass", "rush",
    "complete_pass", "incomplete_pass", "sack", "interception", "penalty", "first_down", "qb_scramble", "posteam", "game_id",
    "fixed_drive_result", "drive", "yardline_100", "qtr", "wp", "half_seconds_remaining", "air_yards", "qb_kneel", "qb_spike", "two_point_attempt"])
P = P[(P.season_type == "REG") & P.down.notna() & (P.two_point_attempt != 1) & (P.qb_kneel != 1) & (P.qb_spike != 1)]
plays = P[P.play_type.isin(["run", "pass"])]
print("plays by down share:", (plays.down.value_counts(normalize=True).sort_index() * 100).round(1).to_dict())
for d in (1, 2, 3):
    x = P[(P.down == d)]
    print(f"down {d}: mean to go {x.ydstogo.mean():.2f}, median {x.ydstogo.median():.0f}")
t3 = P[(P.down == 3)]
b = pd.cut(t3.ydstogo, [0, 2, 5, 9, 99], labels=["1-2", "3-5", "6-9", "10+"])
print("3rd down to go:", (b.value_counts(normalize=True).sort_index() * 100).round(1).to_dict())
conv = t3[t3.play_type.isin(["run", "pass"])].groupby(b[t3.play_type.isin(["run", "pass"])]).first_down.mean()
print("3rd down conversion by to go:", (conv * 100).round(1).to_dict())
print("3rd down conversion overall:", round(t3[t3.play_type.isin(["run", "pass"])].first_down.mean() * 100, 1))
t2 = P[P.down == 2]
b2 = pd.cut(t2.ydstogo, [0, 3, 6, 9, 10, 99], labels=["1-3", "4-6", "7-9", "10", "11+"])
print("2nd down to go:", (b2.value_counts(normalize=True).sort_index() * 100).round(1).to_dict())
# early-down play gains (runs incl. scrambles as pass plays)
for d in (1, 2):
    x = plays[plays.down == d]
    r = x[x.rush == 1]; p = x[x["pass"] == 1]
    bins = [-99, -0.5, 0.5, 3.5, 6.5, 9.5, 19.5, 999]
    lab = ["<0", "0", "1-3", "4-6", "7-9", "10-19", "20+"]
    print(f"down {d} run gains:", (pd.cut(r.yards_gained, bins, labels=lab).value_counts(normalize=True).sort_index() * 100).round(1).to_dict(), "pass rate", round(len(p) / len(x) * 100, 1))
    print(f"down {d} pass play gains:", (pd.cut(p.yards_gained, bins, labels=lab).value_counts(normalize=True).sort_index() * 100).round(1).to_dict())
    att = p[(p.sack != 1) & (p.qb_scramble != 1)]
    print(f"down {d} comp% {att.complete_pass.mean()*100:.1f}, sack% of dropbacks {p.sack.mean()*100:.1f}")
att3 = plays[(plays.down == 3) & (plays["pass"] == 1) & (plays.sack != 1) & (plays.qb_scramble != 1)]
print(f"down 3 comp% {att3.complete_pass.mean()*100:.1f}")
# 2nd & 10+ pass rate
s2 = plays[(plays.down == 2) & (plays.ydstogo >= 10)]
print(f"2nd & 10+ pass rate {s2['pass'].mean()*100:.1f}")
# drives: punts per team game
games = P.game_id.nunique()
punts = (P.play_type == "punt").sum()
print(f"punts per team-game {punts / games / 2:.2f}")
pens = P[(P.penalty == 1)]
print("penalty plays share of downs:", round(len(pens) / len(P) * 100, 1))

# ---- where 3rd & 10+ comes from (SimCalibrate's categories)

P = pd.read_csv(sys.argv[1], low_memory=False, usecols=["season_type", "game_id", "play_id", "drive", "posteam", "down", "ydstogo", "play_type",
    "yards_gained", "sack", "penalty", "penalty_team", "series", "qb_kneel", "qb_spike", "two_point_attempt", "first_down", "penalty_yards"])
P = P[(P.season_type == "REG")].sort_values(["game_id", "play_id"])
P = P[P.series.notna()]
third = 0
cause = {"penalty": 0, "sack": 0, "loss": 0, "nothing": 0, "other": 0}
long3 = 0
for (g, s), x in P.groupby(["game_id", "series"], sort=False):
    t3 = x[(x.down == 3) & x.play_type.isin(["run", "pass", "no_play", "punt", "field_goal"])]
    if t3.empty:
        continue
    first3 = t3.iloc[0]
    third += 1
    if first3.ydstogo < 10:
        continue
    long3 += 1
    before = x[x.play_id < first3.play_id]
    pos = first3.posteam
    offpen = ((before.penalty == 1) & (before.penalty_team == pos)).any()
    if offpen:
        cause["penalty"] += 1
    elif (before.sack == 1).any():
        cause["sack"] += 1
    elif ((before.play_type.isin(["run", "pass"])) & (before.yards_gained < 0)).any():
        cause["loss"] += 1
    elif ((before.play_type.isin(["run", "pass"])) & (before.yards_gained <= 0)).sum() >= 2:
        cause["nothing"] += 1
    else:
        cause["other"] += 1
print(f"3rd downs (first of each series) {third}, 3rd & 10+ {100 * long3 / third:.1f}%")
print("came from (% of all 3rd downs):", {k: round(100 * v / third, 1) for k, v in cause.items()})

# ---- passing per team-game (gross: before sack yards), scrambles, rushes
P = pd.read_csv(sys.argv[1], low_memory=False)  # (all columns)
P = P[(P.season_type == "REG")]
tg = P.game_id.nunique() * 2
pa = P[(P.pass_attempt == 1) & (P.sack == 0) & (P.play_type == "pass")]
print("pass attempts", round(len(pa) / tg, 2), "gross passing yds", round(pa.passing_yards.fillna(0).sum() / tg, 1),
      "comp%", round(100 * pa.complete_pass.mean(), 1))
sk = P[(P.sack == 1)]
print("sacks", round(len(sk) / tg, 2), "sack yards", round(-sk.yards_gained.sum() / tg, 1))
sc = P[(P.qb_scramble == 1)]
db = P[(P.qb_dropback == 1)]
print("dropbacks", round(len(db) / tg, 2), "scrambles", round(len(sc) / tg, 2), f"{100 * len(sc) / len(db):.1f}% of dropbacks, ypc",
      round(sc.yards_gained.mean(), 2))
ru = P[(P.play_type == "run")]
print("rush att (play_type run)", round(len(ru) / tg, 2), "kneels", round(P.qb_kneel.sum() / tg, 2), "ypc",
      round(ru.yards_gained.mean(), 2))
print("rush_attempt flag", round(P.rush_attempt.sum() / tg, 2))
if "play_action" in P.columns:
    print("has play_action")

# ---- 3rd & 10+ causes by SimCalibrate's own method (to go - (10 - yards gained in the series) > 2.5 => a penalty)
import sys, pandas as pd
P = pd.read_csv(sys.argv[1], low_memory=False, usecols=["season_type", "game_id", "play_id", "posteam", "down", "ydstogo", "play_type",
    "yards_gained", "sack", "series", "penalty", "penalty_team", "penalty_yards"])
P = P[(P.season_type == "REG") & P.series.notna()].sort_values(["game_id", "play_id"])
tot = 0; why = [0, 0, 0, 0]; long3 = 0
first_pen = 0; first_n = 0
for (g, s), x in P.groupby(["game_id", "series"], sort=False):
    x = x[x.down.notna()]
    if x.empty or x.iloc[0].down != 1 or x.iloc[0].ydstogo != 10:
        continue
    serG = 0; sack = loss = False
    for _, r in x.iterrows():
        if r.down == 1 and r.play_type in ("run", "pass", "no_play"):
            first_n += 1
            if r.penalty == 1 and r.penalty_team == r.posteam and (r.penalty_yards or 0) > 0:
                first_pen += 1
        if r.down == 3 and r.play_type in ("run", "pass", "no_play", "punt", "field_goal"):
            tot += 1
            if r.ydstogo >= 10:
                long3 += 1
                tg = r.ydstogo
                why[0 if tg - (10 - serG) > 2.5 else 1 if sack else 2 if loss else 3] += 1
        if r.play_type in ("run", "pass"):
            serG += r.yards_gained
            if r.sack == 1: sack = True
            elif r.yards_gained < 0: loss = True
print(f"3rd downs {tot}, 3rd & 10+ {100 * long3 / tot:.1f}%")
print("came from (%% of all 3rd downs): penalty %.1f sack %.1f loss %.1f nothing %.1f" % tuple(100 * w / tot for w in why))
print(f"1st-down snaps with an accepted offensive penalty {100 * first_pen / first_n:.1f}%")
