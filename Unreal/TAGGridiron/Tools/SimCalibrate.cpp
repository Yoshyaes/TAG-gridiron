// Port of tools/calibrate.ts
// Headless realism calibration: simulate many full games and compare to NFL per-team-game averages.
// Usage: SimCalibrate [games=40] [seed=7] [--scores] [--set name=value] [--json file] [--season | --seasons n]
//   --scores  also print one line per game (teams, final score, log length) for diffing vs TS.
//   --season  play whole 17-game seasons (n of them; the games count is ignored) for the season and star spreads.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Game.h"
#include "League.h"
#include "Rng.h"
#include "Stats.h"

using namespace tag;

int main(int argc, char** argv) {
  int N = 40;
  uint32_t seed = 7;
  bool printScores = false;
  // --season [n]: play n whole seasons (17 rounds, every team once a round) instead of N random games: the season spreads
  // (wins, point differential, OVR against wins) and every per-man rate over a real season's sample
  int seasons = 0;
  const char* jsonOut = nullptr;  // --json <file>: every number below as one JSON object (Tools/Realism/score.py reads it)
  int positional = 0;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--scores") == 0) {
      printScores = true;
      continue;
    }
    if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc) {  // --set name=value: override a tag::Tune constant
      const std::string kv = argv[++i];
      const size_t eq = kv.find('=');
      if (eq == std::string::npos || !setTune(kv.substr(0, eq), std::atof(kv.c_str() + eq + 1))) {
        std::fprintf(stderr, "unknown tune %s\n", kv.c_str());
        return 2;
      }
      continue;
    }
    if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
      jsonOut = argv[++i];
      continue;
    }
    if (std::strcmp(argv[i], "--season") == 0) {
      seasons = (std::max)(seasons, 1);
      continue;
    }
    if (std::strcmp(argv[i], "--seasons") == 0 && i + 1 < argc) {
      seasons = (std::max)(1, std::atoi(argv[++i]));
      continue;
    }
    if (positional == 0) N = std::atoi(argv[i]);
    else if (positional == 1) seed = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
    positional++;
  }
  Rng rng(seed);
  League league = createLeague(2027);

  // NFL 2023-2025 regular season, per team per game (approx.)
  // (passYdsGross is before sack yards: nflverse 2023 has 236.3 a team game on 33.7 attempts, 7.0 an attempt - 215 was
  // the net figure, after the 17.4 sack yards, and held the sim's gross yards to a number no NFL team-season meets)
  const std::vector<std::pair<std::string, std::pair<double, double>>> NFL = {
      {"points", {22.0, 1.5}},   {"plays", {62.5, 2.0}},     {"yards", {330, 15}},        {"ydsPerPlay", {5.3, 0.25}},
      {"passAtt", {33.5, 2}},    {"compPct", {64.5, 1.5}},   {"passYdsGross", {236, 12}}, {"ypa", {7.0, 0.35}},
      {"rushAtt", {27, 2}},      {"ypc", {4.3, 0.2}},        {"sacks", {2.5, 0.4}},       {"sackRate", {6.8, 1}},
      {"ints", {0.8, 0.2}},      {"fumblesLost", {0.45, 0.15}}, {"firstDowns", {19.5, 1.2}}, {"thirdPct", {39, 3}},
      {"fgPct", {85, 4}},        {"punts", {4.0, 0.5}},      {"penalties", {6.0, 1}},     {"tds", {2.5, 0.3}},
      {"explosive", {6.2, 1.5}},  // runs of 10+ (~3.1 a team game) and passes of 20+ (~3.1)
  };

  std::map<std::string, std::vector<double>> acc;
  auto push = [&](const char* k, double v) { acc[k].push_back(v); };
  int ties = 0;
  int ots = 0;
  int homeWins = 0;
  long long totalPlays = 0;
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<double> qbRatings;
  std::vector<double> scores;
  // realism extras (per play)
  double depthAtt[4] = {0, 0, 0, 0}, depthComp[4] = {0, 0, 0, 0};
  double airSum = 0, airN = 0, dropbacksX = 0, pressured = 0, tackles = 0, tacklerSum = 0, throwAways = 0, scrN = 0, scrYds = 0;
  int maxAirSeen = 0;
  std::map<std::string, int> deepOutcome, shortOutcome;
  std::vector<double> sepAt[4], sepTgt[4], sepThrow, sepFly;
  double mGap[6] = {0, 0, 0, 0, 0, 0}, mGapN[6] = {0, 0, 0, 0, 0, 0}, mSpd[6] = {0, 0, 0, 0, 0, 0}, rSpd[6] = {0, 0, 0, 0, 0, 0};
  double nearN[4] = {0, 0, 0, 0}, nearSep[4] = {0, 0, 0, 0};
  std::vector<double> ttt;
  std::vector<double> pathT[4];
  std::vector<double> olDeep;
  long dblPlays = 0, dblAny = 0;
  double dblSecs = 0;
  std::vector<double> dblFirst;
  // free-running acceleration over 0.2 s windows (the full-game telemetry's measure); TAG_ACCEL=1 turns it on
  const bool accelOn = std::getenv("TAG_ACCEL") != nullptr;
  const PlaySim* accSim = nullptr;
  // where passes arrive relative to the target (TAG_MISS=1): along his run (+ ahead) and across
  const bool missOn = std::getenv("TAG_MISS") != nullptr;
  const bool yacDbg = std::getenv("TAG_YACDBG") != nullptr;
  std::string yacTrace;
  const PlaySim* yacSim = nullptr;
  double yacT0 = -1, yacLastT = 0, yacNext = 0;
  int yacShown = 0;
  const PlaySim* missSim = nullptr;
  double missLastT = 0;
  bool missDone = false;
  bool predDone = false;
  std::vector<double> predAlong[2];
  std::vector<std::pair<double, double>> missDeep, missShort;
  double accLastT = 0;
  std::map<const Ent*, std::vector<std::pair<V2, bool>>> hist;
  std::map<const Ent*, double> wrTop;  // a wide receiver's top speed this play (yd/s), live ball only
  std::vector<double> wrTops;
  std::vector<double> tanA, latA;
  double accN = 0, accOver1g = 0;
  double accKind[4] = {0, 0, 0, 0};
  std::map<std::string, int> sackHow, unblockedPos, pressWhy;
  std::vector<double> pressTs;
  std::vector<double> sackT;
  double pathN[4] = {0, 0, 0, 0}, pathBehind[4] = {0, 0, 0, 0}, pathAir[4] = {0, 0, 0, 0};
  int lastQb = -1, lastQbPoss = -1;
  double kickoffs = 0, koReturned = 0, fourthGo = 0, fourthWhy[5] = {0, 0, 0, 0, 0};
  double kickN[5] = {0, 0, 0, 0, 0}, kickBlk[5] = {0, 0, 0, 0, 0}, kickGood[5] = {0, 0, 0, 0, 0}, kickTip = 0, kickBlkTD = 0;  // [0] extra points, [1..4] field goals by length
  std::map<std::string, double> J;  // --json
  double qbSwaps = 0, qbSnaps = 0;
  double yacSum = 0, compAirSum = 0, compN = 0;
  double fHold[2] = {0, 0}, fN[2] = {0, 0}, fDpi = 0, fDpiN = 0, fDhold = 0, fRough = 0, fOpi = 0, fOpiN = 0, fMask = 0;
  double dN[5] = {0, 0, 0, 0, 0}, dYds[5] = {0, 0, 0, 0, 0}, dPass[5] = {0, 0, 0, 0, 0}, dTg[5] = {0, 0, 0, 0, 0}, dSucc[5] = {0, 0, 0, 0, 0};
  std::vector<double> runYds, passYdsEarly, run2L, pass2L, run1, pass1, pen1;
  double edN = 0, edSucc = 0, edRun = 0, edRunSucc = 0;
  double tdN[4] = {0, 0, 0, 0}, tdConv[4] = {0, 0, 0, 0}, tdPass[4] = {0, 0, 0, 0};
  double tlWhy[4] = {0, 0, 0, 0}, serG = 0;
  int serSack = 0, serLoss = 0;
  std::map<std::string, int> penBy; double bhN[4][3] = {{0}}, bhNeg[4][3] = {{0}}, bhAir[4] = {0}, bhYac[4] = {0}; double cgN[4] = {0}, cgNeg[4] = {0}, cgZero[4] = {0}, cgSm[4] = {0}, cgAir[4] = {0}; std::vector<double> cgYacV[2], cgAirV[2], cgSepV[2];
  double lastTg1 = 10, lastY1 = 0; int lastK1 = 0;
  double mtBroken = 0, mtMade = 0, yac0 = 0, yac0N = 0, yacMt = 0, yacMtN = 0;
  double yacSep[5] = {0, 0, 0, 0, 0}, yacSepN[5] = {0, 0, 0, 0, 0};
  double yacB[4] = {0, 0, 0, 0}, yacBN[4] = {0, 0, 0, 0}, yacBig[4] = {0, 0, 0, 0};
  // eyes on the ball (PlaySim Tune::eyesOn): interceptions by how the man faced the ball, breakups, blind swipes
  double eyAtt = 0, eyInt = 0, eyBlind = 0, eyFace[3] = {0, 0, 0}, eyPbu = 0, eyPbuBlind = 0;
  // win rates (ESPN's PRWR / PBWR, Research/NextLevel/02 A3) and the star spread: per class, per team, per man
  struct Rate {
    double n = 0, w = 0;
    double r() const { return n > 0 ? w / n : 0.0; }
  };
  Rate prCls[3], pbCls[3], wrTeam, wrFreeN;
  std::map<std::string, Rate> teamPB, teamPR;  // (by abbreviation: the protection of a team on offense, its rush on defense)
  std::map<const Player*, Rate> manPR, manSep, manTack, manRun;
  std::map<const Player*, std::vector<double>> manPRcls;
  struct Att {
    const Player* qb;
    int air, sep;
    bool pr, c;
  };
  std::vector<Att> atts;  // (CPOE: each attempt against the league's completion at its depth, separation and pressure)
  // separation at arrival (A1): share of targets with a defender within a yard (NGS "aggressiveness", attempt-weighted 14.6 %)
  Rate tightWin;
  // play action (A4): yards an attempt and sacks a dropback with and without the fake (nflverse 2023 x FTN charting:
  // 8.31 against 6.69 an attempt, sacks 6.5 % against 6.9 %)
  Rate paYpa[2], paSack[2], paComp[2];
  // leverage at engagement (A5): blocks formed, those on a man already past the blocker's hip, those refused
  double engN = 0, engHip = 0, engRef = 0, engLat = 0, engGeo = 0;
  // read keys (A4): linebacker depth at 0.5 / 1 / 1.5 s by what was called (run, draw, dropback, play action), and yards
  // a carry by the run's style (a draw should not trail the inside zone by much)
  double lbD[4][3] = {{0}}, lbN[4][3] = {{0}};
  Rate ypcStyle[5];
  // the season: every team's wins and point differential (ties a half) and its OVR, by season
  std::vector<std::pair<int, int>> sched;
  if (seasons > 0) {
    N = seasons * 17 * 16;
    for (int s = 0; s < seasons * 17; s++) {
      std::vector<int> perm(32);
      for (int k = 0; k < 32; k++) perm[k] = k;
      for (int k = 31; k > 0; k--) std::swap(perm[k], perm[rng.intRange(0, k)]);
      for (int k = 0; k < 32; k += 2) sched.push_back(std::make_pair(perm[k], perm[k + 1]));
    }
  }
  std::vector<double> seasonWins(32, 0), seasonPd(32, 0), allWins, allPd, allOvr;
  std::vector<double> teamOvrNow(32, 0);

  for (int g = 0; g < N; g++) {
    int hi = 0, ai = 0;
    if (seasons > 0) {
      // (a week between rounds: injuries heal a week's worth, and a new season starts everyone healthy - left to pile up,
      // 34 games of them had every team on its backups)
      if (g % 16 == 0)
        for (int k = 0; k < 32; k++)
          for (const auto& pl : league[k]->roster) pl->injuredWeeks = g % (17 * 16) == 0 ? 0.0 : (std::max)(0.0, pl->injuredWeeks - 1);
      if (g % (17 * 16) == 0)
        for (int k = 0; k < 32; k++) {
          seasonWins[k] = seasonPd[k] = 0;
          teamOvrNow[k] = teamOvr(*league[k]).ovr;
        }
      hi = sched[g].first;
      ai = sched[g].second;
    } else {
      hi = rng.intRange(0, 31);
      ai = rng.intRange(0, 31);
      if (ai == hi) ai = (ai + 1) % 32;
    }
    GameOptions opts;
    opts.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
    Game game(*league[hi], *league[ai], opts);
    lastQb = -1;
    lastQbPoss = -1;
    game.onPlayResult = [&](const PlaySim& sim, const PlayResult& r) {
      if (sim.qb && sim.qb->p && sim.qb->p->pos == Pos::QB) {  // (a wildcat snap's runner is not a QB change)
        const int qid = sim.qb->p->id;
        if (lastQbPoss == static_cast<int>(game.poss) && lastQb >= 0 && qid != lastQb) {
          qbSwaps++;
          if (std::getenv("TAG_QBDBG") && qbSwaps <= 12)
            std::printf("qb swap: now #%d %s ovr %d fat %.2f inj %.1f (play %s)\n", sim.qb->p->number, posName(sim.qb->p->pos),
                        sim.qb->p->ovr, sim.qb->p->fatigue, sim.qb->p->injuredWeeks, sim.cfg.offPlay->id.c_str());
        }
        lastQb = qid;
        lastQbPoss = static_cast<int>(game.poss);
        qbSnaps++;
      }
      if (yacDbg && r.complete && r.catchSep >= 2 && r.catchSep < 3.5 && r.yac >= 8 && r.airYards >= 1 && yacShown < 10) {
        yacShown++;
        std::printf("YAC %.0f air %.0f sep %.1f play %s missed %d\n%s", static_cast<double>(r.yac), static_cast<double>(r.airYards), r.catchSep, sim.cfg.offPlay->id.c_str(), r.brokenTackles, yacTrace.c_str());
      }
      if (r.olBlockDepth > -99) olDeep.push_back(r.olBlockDepth);
      if (r.kind == PlayKind::Pass || r.kind == PlayKind::Sack) { dblPlays++; if (r.dblTeamT > 0) dblAny++; dblSecs += r.dblTeamT; if (r.dblFirstT >= 0) dblFirst.push_back(r.dblFirstT); }
      for (int k = 0; k < 6; k++) mGap[k] += r.manGap[k], mGapN[k] += r.manGapN[k], mSpd[k] += r.manSpd[k], rSpd[k] += r.recSpd[k];
      if (r.kind == PlayKind::Sack) {
        std::string how = "?";
        double at = 0;
        for (const PlayEvent& e : r.events)
          if (e.type == "tackle") {
            how = e.note.empty() ? "wrap" : e.note;
            at = e.t;
            if (how == "wrap") {
              bool shedFirst = false;
              for (const PlayEvent& s : r.events)
                if (s.type == "shed" && s.a == e.a && s.t <= e.t) shedFirst = true;
              how = shedFirst ? "wrap-after-shed" : "wrap-unblocked";
              if (!shedFirst && e.a && e.a->p) unblockedPos[posName(e.a->p->pos)]++;
            }
          }
        sackHow[how]++;
        sackT.push_back(at);
      }
      if (r.kind == PlayKind::Pass || r.kind == PlayKind::Sack) {
        static std::map<std::string, int> callN;
        static bool printed = false;
        const std::string ck = sim.cfg.offPlay->type == PlayType::Screen ? "screen" : sim.cfg.offPlay->type == PlayType::PlayAction ? "playaction" : sim.cfg.offPlay->hasTag("quick") ? "quick" : "dropback";
        callN[ck]++;
        static std::map<std::string, double> callY, callAir, callC;
        if (r.complete) callC[ck] += 1, callAir[ck] += r.airYards;
        callY[ck] += r.yards;
        static std::map<std::string, double> callSack, callAtt;
        callSack[ck] += r.kind == PlayKind::Sack;
        callAtt[ck] += r.kind == PlayKind::Pass && !r.thrownAway;
        {
          // per call: where the throws go and how they end (deep share, catch / ground / broken up / drop / int)
          static std::map<std::string, std::map<std::string, int>> callOut;
          static std::map<std::string, std::vector<double>> callAirAtt;
          if (r.kind == PlayKind::Pass && !r.thrownAway) {
            std::string oc = "ground";
            for (const PlayEvent& e : r.events)
              if (e.type == "catch" || e.type == "divecatch" || e.type == "drop" || e.type == "contested_inc" || e.type == "pbu" || e.type == "int" || e.type == "oobcatch") oc = e.type;
            callOut[ck][oc]++;
            callAirAtt[ck].push_back(r.airYards);
          }
          if (r.thrownAway)
            for (const PlayEvent& e : r.events)
              if (e.type == "throwaway") callOut[ck]["throwaway-" + e.note]++;
          static bool reg5 = false;
          if (!reg5) {
            reg5 = true;
            std::atexit([] {
              for (auto& kv : callAirAtt) {
                std::sort(kv.second.begin(), kv.second.end());
                double deep = 0;
                for (double a : kv.second) deep += a >= 15;
                std::printf("throws on %s: air p25/50/75 %.0f/%.0f/%.0f, 15+ yd %.0f%%; ends", kv.first.c_str(), kv.second[kv.second.size() / 4], kv.second[kv.second.size() / 2],
                            kv.second[kv.second.size() * 3 / 4], 100 * deep / kv.second.size());
                for (const auto& o : callOut[kv.first]) std::printf(" %s %d", o.first.c_str(), o.second);
                std::printf("%c", 10);
              }
            });
          }
        }
        static std::map<std::string, std::vector<double>> callSackT;
        static std::map<std::string, std::map<std::string, int>> callSackBy;
        if (r.kind == PlayKind::Sack)
          for (const PlayEvent& e : r.events)
            if (e.type == "tackle") {
              callSackT[ck].push_back(e.t);
              bool shed = false;
              for (const PlayEvent& sh : r.events)
                if (sh.type == "shed" && sh.a == e.a && sh.t <= e.t) shed = true;
              callSackBy[ck][(shed ? "shed " : "free ") + std::string(e.a && e.a->p ? posName(e.a->p->pos) : "?")]++;
            }
        static bool printed4 = false;
        if (!printed4) {
          printed4 = true;
          std::atexit([] {
            for (auto& kv : callSackT) {
              std::sort(kv.second.begin(), kv.second.end());
              std::printf("sacks on %s: n %zu, at p25/50/75 %.2f/%.2f/%.2f s; by", kv.first.c_str(), kv.second.size(), kv.second[kv.second.size() / 4],
                          kv.second[kv.second.size() / 2], kv.second[kv.second.size() * 3 / 4]);
              for (const auto& b : callSackBy[kv.first]) std::printf(" %s %d", b.first.c_str(), b.second);
              std::printf("\n");
            }
          });
        }
        static bool printed3 = false;
        if (!printed3) {
          printed3 = true;
          std::atexit([] {
            std::printf("by call: sack %% / comp %% of attempts:");
            for (const auto& kv : callN) std::printf(" %s %.1f / %.0f", kv.first.c_str(), 100.0 * callSack[kv.first] / (std::max)(1, kv.second), 100.0 * callC[kv.first] / (std::max)(1.0, callAtt[kv.first]));
            std::printf("\n");
          });
        }
        static bool printed2 = false;
        if (!printed2) {
          printed2 = true;
          std::atexit([] {
            std::printf("yards a dropback by call (comp%%, air of completions):");
            for (const auto& kv : callN) std::printf(" %s %.2f (%.0f%%, %.1f)", kv.first.c_str(), callY[kv.first] / (std::max)(1, kv.second), 100.0 * callC[kv.first] / (std::max)(1, kv.second), callAir[kv.first] / (std::max)(1.0, callC[kv.first]));
            std::printf(" (NFL screens ~5.5-6 yd an attempt)\n");
          });
        }
        if (!printed) {
          printed = true;
          std::atexit([] {
            int tot = 0;
            for (const auto& kv : callN) tot += kv.second;
            std::printf("pass calls (dropbacks):");
            for (const auto& kv : callN) std::printf(" %s %.1f%%", kv.first.c_str(), 100.0 * kv.second / (std::max)(1, tot));
            std::printf(" (NFL screens ~9-10%% of passes)\n");
          });
        }
      }
      if (r.kind == PlayKind::Pass && !r.thrownAway && !sim.isReturn()) {
        eyAtt++;
        if (r.interception) {
          eyInt++;
          eyBlind += r.intBlind;
          eyFace[r.intFaceDeg <= 90 ? 0 : r.intFaceDeg <= 150 ? 1 : 2]++;
        }
        if (r.pbuBy && !r.interception) {
          eyPbu++;
          eyPbuBlind += r.pbuBlind;
        }
      }
      if (r.winPlay && !sim.isReturn()) {
        bool rushWon = false;
        for (const PlayResult::WinRep& w : r.winReps) {
          const int c = (std::min)(2, (std::max)(0, w.cls));
          if (w.rusher) {
            rushWon = rushWon || w.beat || w.unblocked;
            wrFreeN.n++;
            wrFreeN.w += w.unblocked;
            if (!w.engaged) continue;
            prCls[c].n++;
            prCls[c].w += w.beat;
            manPR[w.p].n++;
            manPR[w.p].w += w.beat;
            std::vector<double>& cl = manPRcls[w.p];
            cl.resize(3, 0.0);
            cl[c]++;
          } else if (w.engaged) {
            pbCls[c].n++;
            pbCls[c].w += !w.beat;
          }
        }
        wrTeam.n++;
        wrTeam.w += !rushWon;
        teamPB[sim.cfg.off->abbr].n++;
        teamPB[sim.cfg.off->abbr].w += !rushWon;
        teamPR[sim.cfg.def->abbr].n++;
        teamPR[sim.cfg.def->abbr].w += rushWon;
      }
      engN += r.engN;
      engHip += r.engPastHip;
      engRef += r.engRefused;
      engLat += r.engLatSum;
      engGeo += r.engGeoSum;
      if (!sim.isReturn() && !sim.cfg.place && sim.cfg.offPlay) {
        const OffPlay& op = *sim.cfg.offPlay;
        const int ty = op.type == PlayType::Run ? (op.run && op.run->style == RunStyle::Draw ? 1 : 0) : op.type == PlayType::PlayAction ? 3 : op.type == PlayType::Pass ? 2 : -1;
        if (ty >= 0)
          for (int k = 0; k < 3; k++) lbD[ty][k] += r.lbDepth[k], lbN[ty][k] += r.lbDepthN[k];
        if (r.kind == PlayKind::Run && op.type == PlayType::Run && op.run) {
          const int st = static_cast<int>(op.run->style);
          if (st >= 0 && st < 5) ypcStyle[st].n++, ypcStyle[st].w += r.yards;
        }
      }
      if (!sim.isReturn() && !sim.cfg.place) {
        if (r.kind == PlayKind::Pass && !r.thrownAway && r.target && r.arriveSep >= 0) {
          manSep[r.target].n++;
          manSep[r.target].w += r.arriveSep;
          tightWin.n++;
          tightWin.w += r.arriveSep < 1.0;
        }
        if (r.kind == PlayKind::Pass && !r.thrownAway && r.passer && r.throwSep >= 0)
          atts.push_back(Att{r.passer, r.airYards < 0 ? 0 : r.airYards < 5 ? 1 : r.airYards < 10 ? 2 : r.airYards < 15 ? 3 : r.airYards < 20 ? 4 : r.airYards < 30 ? 5 : 6,
                             r.throwSep < 1 ? 0 : r.throwSep < 2 ? 1 : r.throwSep < 3 ? 2 : r.throwSep < 5 ? 3 : 4, r.pressured, r.complete});
        for (const PlayEvent& e : r.events)
          if ((e.type == "broken" || e.type == "divemiss") && e.b && e.b->p && e.b->side != Side::Off) {
            manTack[e.b->p].n++;
            manTack[e.b->p].w++;
          }
        for (const Player* tk : r.tacklers) manTack[tk].n++;
        if (r.kind == PlayKind::Run && r.rusher) {
          manRun[r.rusher].n++;
          manRun[r.rusher].w += r.yards;
        }
        if ((r.kind == PlayKind::Pass || r.kind == PlayKind::Sack) && sim.cfg.offPlay && sim.cfg.offPlay->type != PlayType::Screen) {
          const int pa = sim.cfg.offPlay->type == PlayType::PlayAction ? 1 : 0;
          paSack[pa].n++;
          paSack[pa].w += r.kind == PlayKind::Sack;
        }
        if (r.kind == PlayKind::Pass && sim.cfg.offPlay) {
          // (nflverse's "not play action" holds the screens: so does this)
          const int pa = sim.cfg.offPlay->type == PlayType::PlayAction ? 1 : 0;
          paYpa[pa].n++;
          paYpa[pa].w += r.interception ? 0 : r.yards;
          paComp[pa].n++;
          paComp[pa].w += r.complete;
        }
      }
      if (r.kind == PlayKind::Pass && !r.thrownAway) {
        const int b = r.airYards < 0 ? 0 : r.airYards < 10 ? 1 : r.airYards < 20 ? 2 : 3;
        depthAtt[b]++;
        if (r.throwPath >= 0 && r.throwPath < 4) {
          pathN[r.throwPath]++;
          pathBehind[r.throwPath] += r.airYards < 0 ? 1 : 0;
          pathAir[r.throwPath] += r.airYards;
        }
        if (r.complete) depthComp[b]++; if (r.complete && b == 0 && r.throwPath >= 0 && r.throwPath < 4) { const int sb = r.catchSep < 3 ? 0 : r.catchSep < 6 ? 1 : 2; bhN[r.throwPath][sb]++; bhNeg[r.throwPath][sb] += r.yards < -0.5; bhAir[r.throwPath] += r.airYards; bhYac[r.throwPath] += r.yac; } if (r.complete && b < 2) { cgYacV[b].push_back(r.yac); cgAirV[b].push_back(r.airYards); cgSepV[b].push_back(r.catchSep); } if (r.complete) { cgN[b]++; cgNeg[b] += r.yards < -0.5; cgZero[b] += r.yards >= -0.5 && r.yards < 0.5; cgSm[b] += r.yards >= 0.5 && r.yards < 3.5; cgAir[b] += r.airYards; }
        if (b == 3 || b == 1) {
          std::string o = "ground";
          for (const PlayEvent& e : r.events)
            if (e.type == "catch" || e.type == "divecatch" || e.type == "drop" || e.type == "contested_inc" ||
                e.type == "pbu" || e.type == "int" || e.type == "oobcatch")
              o = e.type;
          (b == 3 ? deepOutcome : shortOutcome)[o]++;
        }
        if (r.complete && r.catchSep >= 0) sepAt[b].push_back(r.catchSep);
        if (r.arriveSep >= 0) sepTgt[b].push_back(r.arriveSep);
        if (r.complete && r.catchSep >= 0 && b == 2) { sepThrow.push_back(r.throwSep); sepFly.push_back(r.flightT); if (r.catchNear >= 0 && r.catchNear < 4) { nearN[r.catchNear]++; nearSep[r.catchNear] += r.catchSep; } }
        airSum += r.airYards;
        if (r.complete) {
          yacSum += r.yac;
          yacB[b] += r.yac;
          if (r.catchSep >= 0 && r.catchSep < 90) { const int sb = r.catchSep < 1 ? 0 : r.catchSep < 2 ? 1 : r.catchSep < 3 ? 2 : r.catchSep < 5 ? 3 : 4; yacSep[sb] += r.yac; yacSepN[sb]++; }
          (r.brokenTackles ? yacMt : yac0) += r.yac;
          (r.brokenTackles ? yacMtN : yac0N)++;
          yacBN[b]++;
          if (r.yac >= 15) yacBig[b]++;
          compAirSum += r.airYards;
          compN++;
        }
        airN++;
        maxAirSeen = (std::max)(maxAirSeen, r.airYards);
      }
      // (a scramble is a dropback too: PFF's throwaway and nflverse's scramble rates are both per dropback)
      if (r.kind == PlayKind::Scramble && !sim.isReturn()) {
        dropbacksX++;
        scrN++;
        scrYds += r.yards;
        if (r.pressured) pressured++;
      }
      if (r.kind == PlayKind::Pass || r.kind == PlayKind::Sack) {
        dropbacksX++;
        throwAways += r.thrownAway;
        if (r.pressured || r.kind == PlayKind::Sack) pressured++;
        if (r.pressured) {
          pressWhy[r.pressWhy]++;
          pressTs.push_back(r.pressT);
        }
        if (r.kind == PlayKind::Pass && r.timeToThrow > 0) ttt.push_back(r.timeToThrow);
        if (!sim.isReturn() && sim.cfg.down && (r.kind == PlayKind::Pass || r.kind == PlayKind::Sack)) {
          // completion, sacks, outcome mix by down (NFL ~ comp 1st 66, 2nd 65, 3rd 59)
          static double dN[5], dAtt[5], dC[5], dS[5], dInt[5], dDrop[5], dGround[5], dPbu[5];
          static bool reg = false;
          const int dn = (std::min)(4, (std::max)(1, static_cast<int>(*sim.cfg.down)));
          dN[dn]++;
          if (r.kind == PlayKind::Sack) dS[dn]++;
          if (r.kind == PlayKind::Pass && !r.thrownAway) {
            std::string oc = "ground";
            for (const PlayEvent& e : r.events)
              if (e.type == "catch" || e.type == "divecatch" || e.type == "drop" || e.type == "contested_inc" || e.type == "pbu" || e.type == "int" || e.type == "oobcatch") oc = e.type;
            dAtt[dn]++;
            dC[dn] += r.complete;
            dInt[dn] += oc == "int";
            dDrop[dn] += oc == "drop";
            dGround[dn] += oc == "ground";
            dPbu[dn] += oc == "pbu" || oc == "contested_inc";
          }
          if (!reg) {
            reg = true;
            std::atexit([] {
              std::printf("passing by down (comp / sack %% / of attempts: drop, off target, broken up, int %%):");
              for (int k = 1; k <= 3; k++)
                std::printf("  %d: %.1f / %.1f / %.1f %.1f %.1f %.1f", k, 100 * dC[k] / (std::max)(1.0, dAtt[k]), 100 * dS[k] / (std::max)(1.0, dN[k]), 100 * dDrop[k] / (std::max)(1.0, dAtt[k]),
                            100 * dGround[k] / (std::max)(1.0, dAtt[k]), 100 * dPbu[k] / (std::max)(1.0, dAtt[k]), 100 * dInt[k] / (std::max)(1.0, dAtt[k]));
              std::printf("\n");
            });
          }
        }
        if (r.kind == PlayKind::Pass && r.timeToThrow > 0 && r.throwPath >= 0 && r.throwPath < 4) pathT[r.throwPath].push_back(r.timeToThrow);
      }
      mtBroken += r.brokenTackles;
      if (!sim.isReturn() && sim.cfg.down && sim.cfg.toGo && r.kind != PlayKind::Kneel && r.kind != PlayKind::Spike) {
        const int dn = (std::min)(4, (std::max)(1, static_cast<int>(*sim.cfg.down)));
        dN[dn]++;
        dYds[dn] += r.yards;
        dPass[dn] += r.kind != PlayKind::Run;
        dTg[dn] += *sim.cfg.toGo;
        dSucc[dn] += r.yards >= *sim.cfg.toGo || r.td == Side::Off;
        // how a third and long came about: what the series' first two downs were
        if (dn == 1) { serG = 0; serSack = serLoss = 0; }
        if (dn == 2 && *sim.cfg.toGo >= 10) (r.kind == PlayKind::Run ? run2L : pass2L).push_back(r.yards);
        if (dn == 1) (r.kind == PlayKind::Run ? run1 : pass1).push_back(r.yards);
        if (dn == 2) pen1.push_back(*sim.cfg.toGo - (lastTg1 - lastY1)); if (dn == 2 && std::getenv("TAG_DBGPEN") && *sim.cfg.toGo - (lastTg1 - lastY1) > 2.5) std::printf("PUSH tg1 %.1f y1 %.1f tg2 %.1f kind1 %d\n", lastTg1, lastY1, *sim.cfg.toGo, lastK1);
        if (dn == 1) { lastTg1 = *sim.cfg.toGo; lastY1 = r.yards; lastK1 = (int)r.kind; }
        if (dn <= 2) {
          serG += r.yards;
          serSack += r.kind == PlayKind::Sack;
          serLoss += r.kind != PlayKind::Sack && r.yards < 0;
        }
      }
      if (!sim.isReturn() && sim.cfg.down && *sim.cfg.down <= 2 && sim.cfg.toGo && r.kind != PlayKind::Kneel && r.kind != PlayKind::Spike) {
        const double need = (*sim.cfg.down == 1 ? 0.4 : 0.6) * *sim.cfg.toGo;
        const bool ok = r.yards >= need || r.td == Side::Off;
        edN++;
        edSucc += ok;
        if (r.kind == PlayKind::Run) { edRun++; edRunSucc += ok; runYds.push_back(r.yards); }
        else passYdsEarly.push_back(r.yards);
      }
      if (!sim.isReturn() && sim.cfg.down && *sim.cfg.down == 3 && sim.cfg.toGo && r.kind != PlayKind::Kneel && r.kind != PlayKind::Spike) {
        const double tg = *sim.cfg.toGo;
        const int tb = tg < 2.5 ? 0 : tg < 5.5 ? 1 : tg < 9.5 ? 2 : 3;  // as the down box shows it (rounded): 9.3 to go is 3rd & 9
        tdN[tb]++;
        if (tb == 3) tlWhy[tg - (10 - serG) > 2.5 ? 0 : serSack ? 1 : serLoss ? 2 : 3]++;
        if (r.yards >= tg || r.td == Side::Off) tdConv[tb]++;
        if (r.kind != PlayKind::Run) tdPass[tb]++;
      }
      if (r.kind != PlayKind::Kneel && r.kind != PlayKind::Spike && !sim.isReturn()) {
        const int pk = (r.kind == PlayKind::Run) ? 1 : 0;
        fHold[pk] += r.foulHold.p;
        fN[pk]++;
        if (pk == 0) {
          fDhold += r.foulDhold.p;
          fRough += r.foulRough.p;
          if (r.incomplete && r.airYards >= 5) { fDpi += r.foulDpi.p; fDpiN++; }
          if (r.complete && r.airYards >= 8) { fOpi += r.foulOpi.p; fOpiN++; }
        }
        fMask += r.foulMask.p;
      }
      if (!r.tacklers.empty()) mtMade++;
      if (!r.tacklers.empty() && r.kind != PlayKind::Sack) {
        tackles++;
        tacklerSum += static_cast<double>(r.tacklers.size());
      }
    };
    if (yacDbg)
      game.onSimStep = [&](const PlaySim& sim) {
        if (&sim != yacSim || sim.t < yacLastT) {
          yacSim = &sim;
          yacT0 = -1;
          yacTrace.clear();
        }
        yacLastT = sim.t;
        const Ent* c = sim.ball.holder;
        if (yacT0 < 0) {
          if (!c || !sim.passThrown || sim.ball.mode != BallMode::Held || c->side != Side::Off || c == sim.qb) return;
          yacT0 = sim.t;
          yacNext = sim.t;
        }
        if (!c || sim.t < yacNext) return;
        yacNext = sim.t + 0.25;
        char buf[512];
        std::snprintf(buf, sizeof buf, "  t+%.2f carrier %s (%.1f,%.1f) v(%.1f,%.1f) %s |", sim.t - yacT0, c->role.c_str(), c->pos.x - sim.los, c->pos.y,
                      c->vel.x, c->vel.y, c->stateName());
        yacTrace += buf;
        std::vector<std::pair<double, const Ent*>> nd;
        for (const Ent* d : (c->side == Side::Off ? sim.def : sim.off)) nd.push_back(std::make_pair(dist(d->pos, c->pos), d));
        std::sort(nd.begin(), nd.end(), [](const std::pair<double, const Ent*>& a, const std::pair<double, const Ent*>& b) { return a.first < b.first; });
        for (size_t k = 0; k < 3 && k < nd.size(); k++) {
          const Ent* d = nd[k].second;
          const double closing = dot(sub(d->vel, c->vel), norm(sub(c->pos, d->pos)));
          std::snprintf(buf, sizeof buf, " %s %s d%.1f sp%.1f cl%.1f%s%s", d->role.c_str(), d->stateName(), nd[k].first, d->speed(), closing,
                        d->engaged ? " ENG" : "", d->stun > 0 ? " STUN" : "");
          yacTrace += buf;
        }
        yacTrace += "\n";
      };
    if (missOn)
      game.onSimStep = [&](const PlaySim& sim) {
        if (&sim != missSim || sim.t < missLastT) {
          missSim = &sim;
          missDone = false;
          predDone = false;
        }
        missLastT = sim.t;
        const Ball& b = sim.ball;
        if (b.mode == BallMode::Air && b.target && b.flightT > 0 && !predDone && b.t >= b.flightT) {
          predDone = true;
          const Ent* r = b.target;
          const V2 rv = r->speed() > 0.5 ? norm(r->vel) : V2{1, 0};
          const V2 off = sub(r->pos, b.aimNoErr);  // + = receiver past the aim point
          predAlong[b.aimNoErr.x - sim.los >= 20 ? 1 : 0].push_back(dot(off, rv));
        }
        if (b.mode != BallMode::Air) predDone = false;
        if (missDone || b.mode != BallMode::Air || !b.target || !b.targetSpot || b.vel.z > 0 || b.pos.z > 1.4) return;
        missDone = true;
        const double air = b.targetSpot->x - sim.los;
        const Ent* r = b.target;
        const V2 rv = r->speed() > 0.5 ? norm(r->vel) : V2{1, 0};
        const V2 off = sub(V2{b.pos.x, b.pos.y}, r->pos);
        const double lon = dot(off, rv);
        const double latd = std::fabs(rv.x * off.y - rv.y * off.x);
        (air >= 20 ? missDeep : missShort).push_back(std::make_pair(lon, latd));
      };
    if (accelOn)
      game.onSimStep = [&](const PlaySim& sim) {
        if (&sim != accSim || sim.t < accLastT) {
          accSim = &sim;
          hist.clear();
          for (const auto& kv : wrTop) wrTops.push_back(kv.second);
          wrTop.clear();
        }
        accLastT = sim.t;
        if (sim.isLive() && !sim.cfg.kick)
          for (const Ent* e : sim.ents)
            if (e->p && e->p->pos == Pos::WR) wrTop[e] = (std::max)(wrTop[e], e->speed());
        for (const Ent* e : sim.ents) {
          auto& h = hist[e];
          // free running: on his feet, out of any block or tackle, and not touching another body anywhere in the 0.4 s
          // window (a bump or a hit stops a man faster than his legs can: 95 % of the samples braking past 1.07 g were
          // a body touching another or a tackle being driven, and none of it is what the human braking band measures)
          bool fr = !e->engaged && e->state != EntState::Down && e->stun <= 0 && !e->pancaked && !sim.inWrap(e);
          for (size_t k = 0; fr && k < sim.ents.size(); k++) {
            const Ent* o = sim.ents[k];
            if (o != e && o->state != EntState::Down && len(sub(o->pos, e->pos)) < (e->radius + o->radius) * 1.05) fr = false;
          }
          h.push_back(std::make_pair(e->pos, fr));
          const size_t i = h.size() - 1;
          if (i < 24 || i % 6) continue;
          bool allFree = true;
          for (size_t k = i - 24; allFree && k <= i; k++) allFree = h[k].second;
          if (!allFree) continue;
          const double M = 0.9144;  // yd -> m
          const V2 v1 = scale(sub(h[i - 12].first, h[i - 24].first), M / 0.2);
          const V2 v2 = scale(sub(h[i].first, h[i - 12].first), M / 0.2);
          const V2 a = scale(sub(v2, v1), 1 / 0.2);
          const V2 va = scale(add(v1, v2), 0.5);
          const double sp = len(va);
          accN++;
          if (len(a) > 9.81) accOver1g++;
          if (len(a) > 9.81) {  // what kind of move: a start (under 3 m/s), speeding up, braking or a cut at speed
            const V2 u1 = sp > 0.1 ? scale(va, 1 / sp) : V2{1, 0};
            const double t1 = dot(a, u1);
            const double l1 = len(sub(a, scale(u1, t1)));
            accKind[sp < 3 ? 0 : std::fabs(t1) < l1 ? 3 : t1 > 0 ? 1 : 2]++;
          }
          if (sp > 1) {
            const V2 u = scale(va, 1 / sp);
            const double tan = dot(a, u);
            tanA.push_back(tan);
            if (sp > 6) latA.push_back(len(sub(a, scale(u, tan))));
          }
        }
      };
    game.simToEnd();
    if (printScores)
      std::printf("game %d: %s %d - %s %d  (log %zu)\n", g, game.away->abbr.c_str(), game.score.away, game.home->abbr.c_str(),
                  game.score.home, game.log.size());
    if (game.score.home == game.score.away) ties++;
    if (game.score.home > game.score.away) homeWins++;
    if (seasons > 0) {
      const double hw = game.score.home > game.score.away ? 1.0 : game.score.home == game.score.away ? 0.5 : 0.0;
      seasonWins[hi] += hw;
      seasonWins[ai] += 1 - hw;
      seasonPd[hi] += game.score.home - game.score.away;
      seasonPd[ai] += game.score.away - game.score.home;
      if (g % (17 * 16) == 17 * 16 - 1)
        for (int k = 0; k < 32; k++) {
          allWins.push_back(seasonWins[k]);
          allPd.push_back(seasonPd[k]);
          allOvr.push_back(teamOvrNow[k]);
        }
    }
    for (const PlayLog& l : game.log) {
      // accepted penalties by foul ("PENALTY on XXX: <foul>[ (#nn)], ...")
      const size_t at = l.desc.find("PENALTY on ");
      if (at == std::string::npos) continue;
      size_t a = l.desc.find(": ", at), b = l.desc.find(',', at);
      if (a == std::string::npos || b == std::string::npos || b < a) continue;
      std::string nm = l.desc.substr(a + 2, b - a - 2);
      const size_t on = nm.find(" on ");
      if (on != std::string::npos) nm = nm.substr(0, on);
      const size_t par = nm.find(" (");
      if (par != std::string::npos) nm = nm.substr(0, par);
      penBy[nm]++;
    }
    for (const PlayLog& l : game.log)
      if (l.desc.rfind("Overtime", 0) == 0) {
        ots++;
        break;
      }
    for (size_t li = 0; li < game.log.size(); li++) {
      const PlayLog& l = game.log[li];
      const std::string& d = l.desc;
      if (d.find(" kicks ") != std::string::npos && d.find("onside") == std::string::npos) {
        kickoffs++;
        if (d.find("Touchback") == std::string::npos && d.find("out of bounds") == std::string::npos) koReturned++;
      }
      // place kicks (Sim/PlaceKick.cpp): made, blocked, the blocked ones run back, by length
      {
        const size_t fgAt = d.find("-yard field goal is ");
        const bool xp = d.find(" extra point is ") != std::string::npos;
        if (fgAt != std::string::npos || xp) {
          const bool blk = d.find("is BLOCKED") != std::string::npos;
          const bool score6 = blk && (d.find("TOUCHDOWN") != std::string::npos || d.find("TWO-POINT") != std::string::npos);
          int len = 33;
          if (!xp) {
            size_t s0 = fgAt;
            while (s0 > 0 && std::isdigit(static_cast<unsigned char>(d[s0 - 1]))) s0--;
            len = std::atoi(d.c_str() + s0);
          }
          const int bk = xp ? 0 : len < 30 ? 1 : len < 40 ? 2 : len < 50 ? 3 : 4;
          kickN[bk]++;
          kickBlk[bk] += blk;
          kickGood[bk] += d.find("is GOOD") != std::string::npos;
          kickTip += d.find("tipped") != std::string::npos;
          kickBlkTD += score6;
        }
      }
      // a log line carries the state AFTER its play: a 4th-down play is the next scrimmage line after one that left 4th down
      auto scrimmage = [](const std::string& s) {
        for (const char* k : {" punts ", "field goal", "PENALTY", " kicks ", "kneel", "extra point", "Timeout", "END OF", "spike", "two-point"})
          if (s.find(k) != std::string::npos) return false;
        return true;
      };
      if (li > 0 && game.log[li - 1].down == 4 && game.log[li - 1].poss == l.poss && scrimmage(d))
      {
        fourthGo++;
        // why: trailing late, short yardage, or the chart's longer ones
        const PlayLog& b = game.log[li - 1];
        const int mg = b.score[b.poss] - b.score[other(b.poss)];
        const int why = (b.q >= 4 && mg < 0) ? 0 : b.toGo <= 1 ? 1 : b.toGo <= 2 ? 2 : b.ballOn >= 95 ? 3 : 4;
        fourthWhy[why]++;
      }
    }
    for (TeamKey k : {TeamKey::Home, TeamKey::Away}) {
      const TeamLine& T = game.team[k];
      const double sacks = T.sacks;
      const double dropbacks = T.passAtt + sacks;
      const std::vector<const PlayerLine*> lines = game.stats.forTeam(game.teamOf(k).abbr);
      double grossPass = 0, tds = 0, fgm = 0, fga = 0;
      for (const PlayerLine* l : lines) {
        grossPass += l->passYds;
        tds += l->passTD + l->rushTD;
        fgm += l->fgm;
        fga += l->fga;
      }
      totalPlays += T.plays;
      push("points", game.score[k]);
      scores.push_back(game.score[k]);
      push("plays", T.plays);
      push("yards", T.yards);
      push("ydsPerPlay", T.yards / (std::max)(1.0, static_cast<double>(T.plays)));
      push("passAtt", T.passAtt);
      push("compPct", (100.0 * T.passComp) / (std::max)(1.0, static_cast<double>(T.passAtt)));
      push("passYdsGross", grossPass);
      push("ypa", grossPass / (std::max)(1.0, static_cast<double>(T.passAtt)));
      push("rushAtt", T.rushAtt);
      push("ypc", T.rushYds / (std::max)(1.0, static_cast<double>(T.rushAtt)));
      push("sacks", sacks);
      push("sackRate", (100 * sacks) / (std::max)(1.0, dropbacks));
      push("ints", T.ints);
      push("fumblesLost", T.fumblesLost);
      push("firstDowns", T.firstDowns);
      if (T.thirdAtt) push("thirdPct", (100.0 * T.thirdConv) / T.thirdAtt);
      if (fga > 0) push("fgPct", (100 * fgm) / fga);
      push("punts", T.punts);
      push("penalties", T.penalties);
      push("tds", tds);
      push("explosive", T.explosive);
      for (const PlayerLine* l : lines)
        if (l->passAtt >= 15) qbRatings.push_back(passerRating(*l));
    }
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  auto mean = [](const std::vector<double>& a) {
    double s = 0;
    for (double x : a) s += x;
    return s / (std::max)(static_cast<size_t>(1), a.size());
  };
  std::printf("\nSimulated %d games in %.1fs  (ties %d, OT games %d, home wins %d)\n", N, secs, ties, ots, homeWins);
  std::printf("Speed: %.1f games/sec, %.0f scrimmage plays/sec\n", N / (std::max)(1e-9, secs), totalPlays / (std::max)(1e-9, secs));
  std::printf("%-14s %8s %8s   verdict\n", "stat", "SIM", "NFL");
  int off = 0;
  for (const auto& kv : NFL) {
    const double target = kv.second.first;
    const double tol = kv.second.second;
    const double v = mean(acc[kv.first]);
    const double z = (v - target) / tol;
    const char* verdict = std::fabs(z) <= 1 ? "ok" : std::fabs(z) <= 2 ? (z > 0 ? "high" : "low") : z > 0 ? "HIGH!!" : "LOW!!";
    if (std::fabs(z) > 1) off++;
    std::printf("%-14s %8.2f %8.2f   %s\n", kv.first.c_str(), v, target, verdict);
    J["box." + kv.first] = v;
    J["boxz." + kv.first] = z;
  }
  std::printf("QB rating avg: %.1f (NFL ~ 92)\n", mean(qbRatings));
  const double ms = mean(scores);
  std::vector<double> sq;
  for (double s : scores) sq.push_back((s - ms) * (s - ms));
  std::printf("Points SD: %.1f (NFL ~ 10)\n", std::sqrt(mean(sq)));
  J["box.pointsSD"] = std::sqrt(mean(sq));
  std::printf("%d stats outside tolerance\n", off);
  J["box.qbRating"] = mean(qbRatings);
  J["box.offTolerance"] = off;
  J["games"] = N;
  J["ko_return_rate"] = koReturned / (std::max)(1.0, kickoffs);
  J["fourth_down_go_per_team_game"] = fourthGo / (2.0 * N);
  std::printf("kickoffs returned %.1f%% (2025 NFL 74.5%%), 4th-down attempts %.2f per team-game (2024 ~1.4)\n",
              100 * J["ko_return_rate"], J["fourth_down_go_per_team_game"]);
  std::printf("4th-down attempts per team-game by reason: trailing in Q4 %.2f, 4th & 1 %.2f, 4th & 2 %.2f, goal line %.2f, other %.2f\n",
              fourthWhy[0] / (2.0 * N), fourthWhy[1] / (2.0 * N), fourthWhy[2] / (2.0 * N), fourthWhy[3] / (2.0 * N), fourthWhy[4] / (2.0 * N));
  {
    // place kicks, played (nflverse 2023: FG 85.8 % made, 1.72 % blocked - 0.4 / 1.3 / 2.4 / 2.9 % at 18-29 / 30-39 /
    // 40-49 / 50+; extra points 95.9 % good, 0.65 % blocked; 1 of the 27 blocked kicks run back for a score)
    const double fgN = kickN[1] + kickN[2] + kickN[3] + kickN[4], fgB = kickBlk[1] + kickBlk[2] + kickBlk[3] + kickBlk[4];
    const double fgG = kickGood[1] + kickGood[2] + kickGood[3] + kickGood[4];
    auto pc = [](double a, double b) { return b > 0 ? 100.0 * a / b : 0.0; };
    std::printf("kicking: FG %.0f att, made %.1f%%, blocked %.2f%% (18-29 %.2f, 30-39 %.2f, 40-49 %.2f, 50+ %.2f; NFL 1.72: 0.4 / 1.3 / 2.4 / 2.9); "
                "XP %.0f att, good %.1f%%, blocked %.2f%% (NFL 0.65); tipped %.0f; blocks run back for a score %.0f of %.0f\n",
                fgN, pc(fgG, fgN), pc(fgB, fgN), pc(kickBlk[1], kickN[1]), pc(kickBlk[2], kickN[2]), pc(kickBlk[3], kickN[3]), pc(kickBlk[4], kickN[4]), kickN[0],
                pc(kickGood[0], kickN[0]), pc(kickBlk[0], kickN[0]), kickTip, kickBlkTD, fgB + kickBlk[0]);
    J["fg_blocked_pct"] = pc(fgB, fgN);
    J["xp_blocked_pct"] = pc(kickBlk[0], kickN[0]);
    J["xp_good_pct"] = pc(kickGood[0], kickN[0]);
    J["fg_made_pct"] = pc(fgG, fgN);
    J["kick_block_scores"] = kickBlkTD;
  }
  // Realism extras. NFL 2023-24 approximations until Phase 0 re-sources them:
  // attempt share / completion % by air yards: behind LOS ~18% / 80%, 0-9 ~45% / 74%, 10-19 ~25% / 57%, 20+ ~12% / 37%;
  // aDOT ~7.9; NGS time to throw ~2.8 s; pressure ~33% of dropbacks; ~1.35 tacklers per tackle.
  {
    const char* nm[4] = {"behind LOS", "0-9 yd", "10-19 yd", "20+ yd"};
    const double nflShare[4] = {18, 45, 25, 12}, nflComp[4] = {80, 74, 57, 37};
    double all = 0;
    for (double a : depthAtt) all += a;
    std::printf("\nPass depth        share%%  (NFL)   comp%%  (NFL)\n");
    const char* key[4] = {"behind", "short", "mid", "deep"};
    for (int i = 0; i < 4; i++) {
      std::printf("%-14s %8.1f %7.0f %7.1f %6.0f\n", nm[i], 100 * depthAtt[i] / (std::max)(1.0, all), nflShare[i],
                  100 * depthComp[i] / (std::max)(1.0, depthAtt[i]), nflComp[i]);
      J[std::string("pass.share.") + key[i]] = 100 * depthAtt[i] / (std::max)(1.0, all);
      J[std::string("pass.comp.") + key[i]] = 100 * depthComp[i] / (std::max)(1.0, depthAtt[i]);
    }
    std::sort(ttt.begin(), ttt.end());
    double tttSum = 0;
    for (double v : ttt) tttSum += v;
    std::printf("aDOT %.1f (NFL ~7.9), longest air %d yd, time to throw median %.2f mean %.2f s (PFF average 2.81), pressured %.1f%% (NFL ~33)\n",
                airSum / (std::max)(1.0, airN), maxAirSeen, ttt.empty() ? 0.0 : ttt[ttt.size() / 2], tttSum / (std::max<size_t>)(1, ttt.size()),
                100 * pressured / (std::max)(1.0, dropbacksX));
    std::printf("tacklers per tackle %.2f (NFL ~1.35)\n", tacklerSum / (std::max)(1.0, tackles));
    J["adot"] = airSum / (std::max)(1.0, airN);
    J["max_air_yd"] = maxAirSeen;
    J["ttt_median_s"] = ttt.empty() ? 0.0 : ttt[ttt.size() / 2];
    {
      // (PFF's 2.81 s and NGS's ~2.77 s are averages over every attempt, throwaways included: a right-skewed spread's
      // median sits a few tenths under its mean)
      double s = 0;
      for (double v : ttt) s += v;
      J["ttt_mean_s"] = ttt.empty() ? 0.0 : s / ttt.size();
    }
    J["pressured_pct"] = 100 * pressured / (std::max)(1.0, dropbacksX);
    J["throwaway_pct"] = 100 * throwAways / (std::max)(1.0, dropbacksX);  // (PFF: ~3-4 % of dropbacks)
    std::printf("throwaways %.1f%% of dropbacks (PFF ~3-4%%), scrambles %.1f%% at %.1f yd (nflverse 2023: 5.4%% at 6.7)\n",
                100 * throwAways / (std::max)(1.0, dropbacksX), 100 * scrN / (std::max)(1.0, dropbacksX), scrYds / (std::max)(1.0, scrN));
    J["scramble_pct"] = 100 * scrN / (std::max)(1.0, dropbacksX);
    J["scramble_ypc"] = scrYds / (std::max)(1.0, scrN);
    {
      std::printf("first pressure by:");
      for (const auto& kv : pressWhy) std::printf("  %s %d", kv.first.c_str(), kv.second);
      std::sort(pressTs.begin(), pressTs.end());
      if (!pressTs.empty()) std::printf("  | at t: p10 %.2f p50 %.2f p90 %.2f s", pressTs[pressTs.size() / 10], pressTs[pressTs.size() / 2], pressTs[pressTs.size() * 9 / 10]);
      std::printf("\n");
    }
    J["tacklers_per_tackle"] = tacklerSum / (std::max)(1.0, tackles);
    J["yac_per_completion"] = yacSum / (std::max)(1.0, compN);
    J["air_per_completion"] = compAirSum / (std::max)(1.0, compN);
    J["qb_changes_per_game"] = qbSwaps / (std::max)(1, N);
    std::printf("per completion: air yards %.1f, YAC %.1f (NFL 2024 ~5.6 / ~5.2)\n", compAirSum / (std::max)(1.0, compN),
                yacSum / (std::max)(1.0, compN));
    J["missed_tackle_rate"] = mtBroken / (std::max)(1.0, mtBroken + mtMade);
    std::printf("missed tackles: %.1f%% of attempts (PFF team range 12-16%%); YAC with none %.1f, with one+ %.1f (%.0f%% of catches)\n",
                100 * mtBroken / (std::max)(1.0, mtBroken + mtMade), yac0 / (std::max)(1.0, yac0N), yacMt / (std::max)(1.0, yacMtN),
                100 * yacMtN / (std::max)(1.0, yac0N + yacMtN));
    std::printf("YAC by separation at the catch (<1, 1-2, 2-3, 3-5, 5+ yd): %.1f (%.0f%%)  %.1f (%.0f%%)  %.1f (%.0f%%)  %.1f (%.0f%%)  %.1f (%.0f%%)\n",
                yacSep[0] / (std::max)(1.0, yacSepN[0]), 100 * yacSepN[0] / (std::max)(1.0, compN), yacSep[1] / (std::max)(1.0, yacSepN[1]), 100 * yacSepN[1] / (std::max)(1.0, compN),
                yacSep[2] / (std::max)(1.0, yacSepN[2]), 100 * yacSepN[2] / (std::max)(1.0, compN), yacSep[3] / (std::max)(1.0, yacSepN[3]), 100 * yacSepN[3] / (std::max)(1.0, compN),
                yacSep[4] / (std::max)(1.0, yacSepN[4]), 100 * yacSepN[4] / (std::max)(1.0, compN));
    std::printf("foul risk per play (old flat rolls): holding pass %.3f (0.026) run %.3f (0.036), DPI per incomplete 5+ %.3f (~0.03), def holding %.3f (0.014), roughing %.4f (~0.004), OPI per complete 8+ %.4f (0.005), facemask %.4f (0.002)\n",
                fHold[0] / (std::max)(1.0, fN[0]), fHold[1] / (std::max)(1.0, fN[1]), fDpi / (std::max)(1.0, fDpiN), fDhold / (std::max)(1.0, fN[0]),
                fRough / (std::max)(1.0, fN[0]), fOpi / (std::max)(1.0, fOpiN), fMask / (std::max)(1.0, fN[0] + fN[1]));
    {
      auto pc = [](std::vector<double> v, double q) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[static_cast<size_t>(q * (v.size() - 1))]; };
      double neg = 0, zero = 0;
      for (double y : runYds) { neg += y < 0; zero += y <= 0; }
      {
        auto hist = [](const std::vector<double>& v, const char* nm) {
          double h[7] = {0};
          for (double y : v) h[y < -0.5 ? 0 : y < 0.5 ? 1 : y < 3.5 ? 2 : y < 6.5 ? 3 : y < 9.5 ? 4 : y < 19.5 ? 5 : 6]++;
          std::printf("  %s gains (<0, 0, 1-3, 4-6, 7-9, 10-19, 20+):", nm);
          for (double x : h) std::printf(" %.1f%%", 100 * x / (std::max)(1.0, (double)v.size()));
          std::printf("\n");
        };
        hist(runYds, "early run");
        hist(passYdsEarly, "early pass play");
        hist(run1, "1st down run");
        hist(pass1, "1st down pass play");
        hist(run2L, "2nd & 10+ run");
        hist(pass2L, "2nd & 10+ pass play");
        std::printf("  2nd & 10+: %.0f%% of 2nd downs, pass %.0f%%\n", 100.0 * (run2L.size() + pass2L.size()) / (std::max)(1.0, dN[2]), 100.0 * pass2L.size() / (std::max)(1.0, (double)(run2L.size() + pass2L.size())));
        double pb = 0, pf = 0;
        for (double x : pen1) { pb += x > 2.5; pf += x < -2.5; }
        std::printf("  between 1st and 2nd down: pushed back by a penalty %.1f%%, moved up %.1f%%\n", 100 * pb / (std::max)(1.0, (double)pen1.size()), 100 * pf / (std::max)(1.0, (double)pen1.size()));
      }
      std::printf("early downs: success %.0f%% (NFL ~47%%), runs %.0f%% (NFL ~42%%); run yards p10 %.0f p25 %.0f p50 %.0f p75 %.0f p90 %.0f, lost yards %.0f%%, <=0 %.0f%% (NFL ~ p50 3, <=0 ~22%%); early pass plays p25 %.0f p50 %.0f p75 %.0f\n",
                  100 * edSucc / (std::max)(1.0, edN), 100 * edRunSucc / (std::max)(1.0, edRun), pc(runYds, 0.1), pc(runYds, 0.25), pc(runYds, 0.5), pc(runYds, 0.75), pc(runYds, 0.9),
                  100 * neg / (std::max)(1.0, (double)runYds.size()), 100 * zero / (std::max)(1.0, (double)runYds.size()), pc(passYdsEarly, 0.25), pc(passYdsEarly, 0.5), pc(passYdsEarly, 0.75));
      J["early_success_pct"] = 100 * edSucc / (std::max)(1.0, edN);
      J["run_lost_pct"] = 100 * neg / (std::max)(1.0, (double)runYds.size());
      J["run_p75_yd"] = pc(runYds, 0.75);
      J["early_pass_p50_yd"] = pc(passYdsEarly, 0.5);
      J["second_down_to_go"] = dTg[2] / (std::max)(1.0, dN[2]);
      J["third_long_share_pct"] = 100 * tdN[3] / (std::max)(1.0, tdN[0] + tdN[1] + tdN[2] + tdN[3]);
      J["third_short_share_pct"] = 100 * tdN[0] / (std::max)(1.0, tdN[0] + tdN[1] + tdN[2] + tdN[3]);
      // 1st-down play gains against the 2023 NFL's (nflverse play-by-play, regular season): runs <0 9.5, 0 8.3, 1-3 36.7,
      // 4-6 24.6, 7-9 10.4, 10-19 8.0, 20+ 2.3 %; pass plays (sacks and scrambles in) 6.9, 31.0, 8.5, 14.2, 13.2, 17.1, 9.1 %
      {
        auto bins = [](const std::vector<double>& v, double* h) {
          for (int k = 0; k < 7; k++) h[k] = 0;
          for (double y : v) h[y < -0.5 ? 0 : y < 0.5 ? 1 : y < 3.5 ? 2 : y < 6.5 ? 3 : y < 9.5 ? 4 : y < 19.5 ? 5 : 6]++;
          for (int k = 0; k < 7; k++) h[k] *= 100.0 / (std::max)(1.0, (double)v.size());
        };
        static const double kRun[7] = {9.5, 8.3, 36.7, 24.6, 10.4, 8.0, 2.3}, kPass[7] = {6.9, 31.0, 8.5, 14.2, 13.2, 17.1, 9.1};
        double hr[7], hp[7], tvR = 0, tvP = 0;
        bins(run1, hr);
        bins(pass1, hp);
        for (int k = 0; k < 7; k++) tvR += std::fabs(hr[k] - kRun[k]) / 2, tvP += std::fabs(hp[k] - kPass[k]) / 2;
        J["first_run_le0_pct"] = hr[0] + hr[1];
        J["first_run_1to6_pct"] = hr[2] + hr[3];
        J["first_run_tv"] = tvR;
        J["first_pass_le0_pct"] = hp[0] + hp[1];
        J["first_pass_tv"] = tvP;
      }
    }
    for (int dn = 1; dn <= 3; dn++)
      std::printf("down %d: %.0f%% of plays, to go %.1f, pass %.0f%%, avg gain %.2f, converted %.0f%%   (NFL ~ 1st: 41%%, 10.0, 49%%, 5.5; 2nd: 32%%, 7.6, 56%%, 5.2, 30%%; 3rd: 23%%, 7.3, 75%%, 5.4, 39%%)\n",
                  dn, 100 * dN[dn] / (std::max)(1.0, dN[1] + dN[2] + dN[3] + dN[4]), dTg[dn] / (std::max)(1.0, dN[dn]), 100 * dPass[dn] / (std::max)(1.0, dN[dn]),
                  dYds[dn] / (std::max)(1.0, dN[dn]), 100 * dSucc[dn] / (std::max)(1.0, dN[dn]));
    std::printf("3rd downs (share of 3rd downs / converted / pass rate) NFL ~ 1-2: 22%% 62%%, 3-5: 25%% 48%%, 6-9: 26%% 36%%, 10+: 27%% 20%%\n");
    {
      const double tot = (std::max)(1.0, tdN[0] + tdN[1] + tdN[2] + tdN[3]);
      for (int b = 0; b < 2; b++) std::printf("completions %s: lost yards %.1f%%, no gain %.1f%%, 1-3 yd %.1f%%, mean air %.1f (of %.0f)   ", b ? "0-9" : "behind LOS", 100 * cgNeg[b] / (std::max)(1.0, cgN[b]), 100 * cgZero[b] / (std::max)(1.0, cgN[b]), 100 * cgSm[b] / (std::max)(1.0, cgN[b]), cgAir[b] / (std::max)(1.0, cgN[b]), cgN[b]);
      std::puts("");
      for (int k = 0; k < 2; k++) {
        // (NFL 2023 for comparison, nflverse pbp: behind LOS air p25/50/75 -4/-3/-2, yac p10/25/50/75 2/4/8/11, yac < 1 6%;
        //  0-9 air 2/4/6, yac 0/0/2/6, yac < 1 28%)
        auto q = [](std::vector<double> v, double f) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[(size_t)(f * (v.size() - 1))]; };
        const auto& Y = cgYacV[k]; const auto& A = cgAirV[k];
        const double lt1 = Y.empty() ? 0 : 100.0 * std::count_if(Y.begin(), Y.end(), [](double y) { return y < 1; }) / Y.size();
        std::printf("completions %s: air p25/50/75 %.1f/%.1f/%.1f  yac p10/25/50/75 %.1f/%.1f/%.1f/%.1f  yac < 1 %.0f%%  sep p50 %.1f\n", k ? "0-9" : "behind LOS",
          q(A, 0.25), q(A, 0.5), q(A, 0.75), q(Y, 0.1), q(Y, 0.25), q(Y, 0.5), q(Y, 0.75), lt1, q(cgSepV[k], 0.5));
      }
      for (int k = 0; k < 4; k++) { const double n = bhN[k][0] + bhN[k][1] + bhN[k][2]; std::printf("behind-LOS completions path %d: n %.0f air %.1f yac %.1f; lost yards by separation (<3, 3-6, 6+): %.0f%% (%.0f) %.0f%% (%.0f) %.0f%% (%.0f)", k, n, bhAir[k] / (std::max)(1.0, n), bhYac[k] / (std::max)(1.0, n), 100 * bhNeg[k][0] / (std::max)(1.0, bhN[k][0]), bhN[k][0], 100 * bhNeg[k][1] / (std::max)(1.0, bhN[k][1]), bhN[k][1], 100 * bhNeg[k][2] / (std::max)(1.0, bhN[k][2]), bhN[k][2]); std::puts(""); }
      std::printf("accepted penalties per team game:");
      for (const auto& kv : penBy) std::printf("  %s %.2f", kv.first.c_str(), kv.second / (2.0 * N));
      std::puts("");
      std::printf("3rd & 10+ came from (%% of all 3rd downs): a penalty in the series %.1f, a sack %.1f, a play for a loss %.1f, two plays for nothing %.1f\n",
                  100 * tlWhy[0] / tot, 100 * tlWhy[1] / tot, 100 * tlWhy[2] / tot, 100 * tlWhy[3] / tot);
      const char* nm3[4] = {"1-2", "3-5", "6-9", "10+"};
      for (int k = 0; k < 4; k++)
        std::printf("   3rd & %s: %4.0f%% of 3rd downs, converted %4.0f%%, passed %4.0f%%\n", nm3[k], 100 * tdN[k] / tot, 100 * tdConv[k] / (std::max)(1.0, tdN[k]), 100 * tdPass[k] / (std::max)(1.0, tdN[k]));
    }
    std::printf("YAC by air depth (NFL ~ behind 7.5, 0-9 5.0, 10-19 4.0, 20+ 4.5):  behind %.1f  0-9 %.1f  10-19 %.1f  20+ %.1f   15+ YAC share: %.0f%% %.0f%% %.0f%% %.0f%%\n",
                yacB[0] / (std::max)(1.0, yacBN[0]), yacB[1] / (std::max)(1.0, yacBN[1]), yacB[2] / (std::max)(1.0, yacBN[2]), yacB[3] / (std::max)(1.0, yacBN[3]),
                100 * yacBig[0] / (std::max)(1.0, yacBN[0]), 100 * yacBig[1] / (std::max)(1.0, yacBN[1]), 100 * yacBig[2] / (std::max)(1.0, yacBN[2]), 100 * yacBig[3] / (std::max)(1.0, yacBN[3]));
    {
      const char* pn[4] = {"read", "hot", "pressure", "quick"};
      std::printf("throws by decision:");
      for (int i = 0; i < 4; i++)
        std::printf("  %s %.0f (behind LOS %.0f%%, aDOT %.1f, release p50 %.2f s)", pn[i], pathN[i], 100 * pathBehind[i] / (std::max)(1.0, pathN[i]),
                    pathAir[i] / (std::max)(1.0, pathN[i]), pathT[i].empty() ? 0.0 : (std::sort(pathT[i].begin(), pathT[i].end()), pathT[i][pathT[i].size() / 2]));
      std::printf("\n");
    }
    std::sort(sackT.begin(), sackT.end());
    std::printf("sacks by tackle kind:");
    for (const auto& kv : sackHow) std::printf("  %s %d", kv.first.c_str(), kv.second);
    if (!sackT.empty()) std::printf("   time to sack median %.2f s (NFL ~4)", sackT[sackT.size() / 2]);
    std::printf("\n  unblocked sackers by position:");
    for (const auto& kv : unblockedPos) std::printf("  %s %d", kv.first.c_str(), kv.second);
    std::printf("\n");
    for (int k = 0; k < 2; k++) {
      auto& P = predAlong[k];
      if (P.empty()) continue;
      std::sort(P.begin(), P.end());
      double m = 0;
      for (double x : P) m += x;
      std::printf("%s receiver at planned arrival vs the QB's aim (+ = past it): mean %.2f p10 %.1f p50 %.1f p90 %.1f\n",
                  k ? "20+ yd" : "<20 yd", m / P.size(), P[P.size() / 10], P[P.size() / 2], P[P.size() * 9 / 10]);
    }
    for (int k = 0; k < 2; k++) {
      auto& M = k ? missDeep : missShort;
      if (M.empty()) continue;
      std::vector<double> lo, la;
      double over = 0, under = 0;
      for (const auto& m : M) {
        lo.push_back(m.first);
        la.push_back(m.second);
        over += m.first > 2.0;
        under += m.first < -2.0;
      }
      std::sort(lo.begin(), lo.end());
      std::sort(la.begin(), la.end());
      std::printf("%s ball at catch height vs target: along p10 %.1f p50 %.1f p90 %.1f, across p50 %.1f p90 %.1f, >2 yd ahead %.0f%%, >2 yd short %.0f%%\n",
                  k ? "20+ yd" : "<20 yd", lo[lo.size() / 10], lo[lo.size() / 2], lo[lo.size() * 9 / 10], la[la.size() / 2],
                  la[la.size() * 9 / 10], 100 * over / M.size(), 100 * under / M.size());
    }
    if (accelOn && !tanA.empty() && !latA.empty()) {
      std::sort(tanA.begin(), tanA.end());
      std::sort(latA.begin(), latA.end());
      std::printf("free-running accel (m/s2, 0.2 s windows): over 1 g %.1f%% of samples | speed-up p99 %.1f | braking p1 %.1f | "
                  "lateral p95 at >6 m/s %.1f   (telemetry baseline 17.3%% / 11.9 / -17.4 / 10.4)\n",
                  100 * accOver1g / accN, tanA[tanA.size() * 99 / 100], tanA[tanA.size() / 100], latA[latA.size() * 95 / 100]);
      J["accel.over_1g_pct"] = 100 * accOver1g / accN;
      std::printf("  over 1 g by move (%% of all samples): under 3 m/s %.1f | speeding up %.1f | braking %.1f | cutting %.1f\n",
                  100 * accKind[0] / accN, 100 * accKind[1] / accN, 100 * accKind[2] / accN, 100 * accKind[3] / accN);
      J["accel.speedup_p99"] = tanA[tanA.size() * 99 / 100];
      J["accel.braking_p1"] = tanA[tanA.size() / 100];
      J["accel.lateral_p95_at_6ms"] = latA[latA.size() * 95 / 100];
      for (const auto& kv : wrTop) wrTops.push_back(kv.second);
      std::sort(wrTops.begin(), wrTops.end());
      if (!wrTops.empty()) {
        const double mph = 0.9144 * 2.23694;
        std::printf("WR top speed a play (mph): p50 %.1f p90 %.1f max %.1f (%zu plays)\n", wrTops[wrTops.size() / 2] * mph,
                    wrTops[wrTops.size() * 9 / 10] * mph, wrTops.back() * mph, wrTops.size());
        J["wr_play_top_p90_mph"] = wrTops[wrTops.size() * 9 / 10] * mph;
      }
    }
    std::sort(olDeep.begin(), olDeep.end());
    std::printf("pass-pro double teams: on %.1f%% of dropbacks, %.2f s a dropback\n", dblPlays ? 100.0 * dblAny / dblPlays : 0.0, dblPlays ? dblSecs / dblPlays : 0.0);
    if (!dblFirst.empty())
    {
      std::sort(dblFirst.begin(), dblFirst.end());
      std::printf("  first double team formed at: p10 %.2f p50 %.2f p90 %.2f s\n", dblFirst[dblFirst.size() / 10], dblFirst[dblFirst.size() / 2], dblFirst[dblFirst.size() * 9 / 10]);
    }
    if (!olDeep.empty())
      std::printf("deepest engaged OL block past the LOS: p50 %.1f p99 %.1f max %.1f yd (real: rarely past ~12)\n",
                  olDeep[olDeep.size() / 2], olDeep[olDeep.size() * 99 / 100], olDeep.back());
    if (!olDeep.empty()) J["ol_block_depth_max_yd"] = olDeep.back();
    std::printf("QB changes within a possession: %.0f in %.0f snaps (NFL ~0 outside injuries)\n", qbSwaps, qbSnaps);
    std::printf("separation at the catch (median yd, NGS ~2.9 overall):");
    for (int i = 0; i < 4; i++) {
      std::sort(sepAt[i].begin(), sepAt[i].end());
      std::printf("  %s %.2f", nm[i], sepAt[i].empty() ? 0.0 : sepAt[i][sepAt[i].size() / 2]);
      J[std::string("separation.") + key[i]] = sepAt[i].empty() ? 0.0 : sepAt[i][sepAt[i].size() / 2];
    }
    std::printf("\n");
    std::printf("separation when the ball arrives, every target (mean yd, NGS 2.9 overall):");
    {
      double all = 0, alln = 0;
      for (int i = 0; i < 4; i++) {
        double s = 0;
        for (double v : sepTgt[i]) s += v;
        all += s, alln += static_cast<double>(sepTgt[i].size());
        std::sort(sepTgt[i].begin(), sepTgt[i].end());
        std::printf("  %s %.2f (p50 %.2f, n %zu)", nm[i], sepTgt[i].empty() ? 0.0 : s / sepTgt[i].size(), sepTgt[i].empty() ? 0.0 : sepTgt[i][sepTgt[i].size() / 2], sepTgt[i].size());
        J[std::string("separation_tgt.") + key[i]] = sepTgt[i].empty() ? 0.0 : s / sepTgt[i].size();
      }
      std::printf("  all %.2f\n", alln ? all / alln : 0.0);
      J["separation_tgt.all"] = alln ? all / alln : 0.0;
    }
    std::printf("man defender to his receiver (yd / his speed / the receiver's), at 0.5 s steps:");
    for (int k = 0; k < 6; k++) std::printf("  %.1fs %.2f/%.1f/%.1f", 0.5 * (k + 1), mGap[k] / (std::max)(mGapN[k], 1.0), mSpd[k] / (std::max)(mGapN[k], 1.0), rSpd[k] / (std::max)(mGapN[k], 1.0));
    std::printf("\n");
    if (!sepThrow.empty()) {
      std::sort(sepThrow.begin(), sepThrow.end());
      std::sort(sepFly.begin(), sepFly.end());
      std::printf("10-19 yd completions: separation at the throw p50 %.2f, flight p50 %.2f s; nearest at the catch: own man %.0f (%.2f yd)  other man %.0f (%.2f)  zone %.0f (%.2f)  other %.0f (%.2f)\n",
                  sepThrow[sepThrow.size() / 2], sepFly[sepFly.size() / 2], nearN[0], nearSep[0] / (std::max)(nearN[0], 1.0), nearN[1], nearSep[1] / (std::max)(nearN[1], 1.0),
                  nearN[2], nearSep[2] / (std::max)(nearN[2], 1.0), nearN[3], nearSep[3] / (std::max)(nearN[3], 1.0));
    }
    std::printf("eyes on the ball: interceptions by defenders not facing the ball: %.0f of %.0f (his body to the ball <=90 deg %.0f, 90-150 %.0f, "
                ">150 %.0f); breakups %.2f%% of attempts (%.0f blind swipes at the hands), passes defensed %.2f%%\n",
                eyBlind, eyInt, eyFace[0], eyFace[1], eyFace[2], 100 * eyPbu / (std::max)(1.0, eyAtt), eyPbuBlind, 100 * (eyPbu + eyInt) / (std::max)(1.0, eyAtt));
    J["int_blind"] = eyBlind;
    J["int_total"] = eyInt;
    J["pbu_per_att_pct"] = 100 * eyPbu / (std::max)(1.0, eyAtt);
    J["pd_per_att_pct"] = 100 * (eyPbu + eyInt) / (std::max)(1.0, eyAtt);
    std::printf("20+ yd outcomes:");
    for (const auto& kv : deepOutcome) std::printf("  %s %d", kv.first.c_str(), kv.second);
    std::printf("\n0-9 yd outcomes:");
    for (const auto& kv : shortOutcome) std::printf("  %s %d", kv.first.c_str(), kv.second);
    std::printf("\n");
  }
  {
    // win rates (ESPN, built on NGS tracking: a rusher wins by beating his block inside 2.5 s, a blocker by holding it;
    // screens out; unblocked rushers count for the rush at team level). ESPN: individual PRWR ~25 %, edge 22 %; PBWR ~75 %
    // across blockers, tackles 79 %; team ~50 %; 2023 team PBWR 43-76 %, team PRWR 31-60 %; top edges 30-35 %, top DTs ~20 %
    const double perTeam = 2.0 * N / 32 / 17;  // (seasons' worth of games each team played: the qualifying bars scale with it)
    const Rate prAll{prCls[0].n + prCls[1].n + prCls[2].n, prCls[0].w + prCls[1].w + prCls[2].w};
    const Rate pbAll{pbCls[0].n + pbCls[1].n + pbCls[2].n, pbCls[0].w + pbCls[1].w + pbCls[2].w};
    double pbMin = 1, pbMax = 0, prMin = 1, prMax = 0;
    for (const auto& kv : teamPB) pbMin = (std::min)(pbMin, kv.second.r()), pbMax = (std::max)(pbMax, kv.second.r());
    for (const auto& kv : teamPR) prMin = (std::min)(prMin, kv.second.r()), prMax = (std::max)(prMax, kv.second.r());
    std::printf("\nwin rates (2.5 s, screens out): PRWR %.1f%% (edge %.1f, interior DL %.1f, other blitzers %.1f; ESPN ~25, edge 22) | PBWR %.1f%% "
                "(tackles %.1f, interior OL %.1f, TE/backs %.1f; ESPN ~75, tackles 79) | team PBWR %.1f%% (ESPN ~50; %.0f plays), by team %.0f-%.0f%% "
                "(2023 43-76), team PRWR by team %.0f-%.0f%% (31-60) | unblocked rushers free at the QB %.1f%% of rushes\n",
                100 * prAll.r(), 100 * prCls[0].r(), 100 * prCls[1].r(), 100 * prCls[2].r(), 100 * pbAll.r(), 100 * pbCls[0].r(), 100 * pbCls[1].r(),
                100 * pbCls[2].r(), 100 * wrTeam.r(), wrTeam.n, 100 * pbMin, 100 * pbMax, 100 * prMin, 100 * prMax, 100 * wrFreeN.r());
    J["wr.prwr"] = 100 * prAll.r();
    J["wr.prwr_edge"] = 100 * prCls[0].r();
    J["wr.prwr_dt"] = 100 * prCls[1].r();
    J["wr.pbwr"] = 100 * pbAll.r();
    J["wr.pbwr_tackle"] = 100 * pbCls[0].r();
    J["wr.pbwr_interior"] = 100 * pbCls[1].r();
    J["wr.team_pbwr"] = 100 * wrTeam.r();
    J["wr.team_pbwr_lo"] = 100 * pbMin;
    J["wr.team_pbwr_hi"] = 100 * pbMax;
    J["wr.team_prwr_lo"] = 100 * prMin;
    J["wr.team_prwr_hi"] = 100 * prMax;
    // the stars: each qualified man's own PRWR (150 blocked rushes a season), the best edges and interior linemen
    std::vector<std::pair<double, const Player*>> edges, dts;
    for (const auto& kv : manPR) {
      if (kv.second.n < 150 * perTeam) continue;
      const std::vector<double>& cl = manPRcls[kv.first];
      const int c = cl[0] >= cl[1] && cl[0] >= cl[2] ? 0 : cl[1] >= cl[2] ? 1 : 2;
      if (c == 0) edges.push_back(std::make_pair(kv.second.r(), kv.first));
      if (c == 1) dts.push_back(std::make_pair(kv.second.r(), kv.first));
    }
    auto sdOf = [](const std::vector<double>& v) {
      if (v.size() < 2) return 0.0;
      double m = 0, s = 0;
      for (double x : v) m += x;
      m /= static_cast<double>(v.size());
      for (double x : v) s += (x - m) * (x - m);
      return std::sqrt(s / static_cast<double>(v.size() - 1));
    };
    for (int k = 0; k < 2; k++) {
      std::vector<std::pair<double, const Player*>>& v = k ? dts : edges;
      std::sort(v.begin(), v.end(), [](const std::pair<double, const Player*>& a, const std::pair<double, const Player*>& b) { return a.first > b.first; });
      std::vector<double> rs;
      for (const auto& x : v) rs.push_back(x.first);
      std::printf("  qualified %s rushers %zu (PRWR SD %.1f%%): best", k ? "interior" : "edge", v.size(), 100 * sdOf(rs));
      for (size_t i = 0; i < v.size() && i < 4; i++) std::printf("  %.1f%% (%s pr %.0f str %.0f agi %.0f)", 100 * v[i].first, posName(v[i].second->pos), v[i].second->r.passRush, v[i].second->r.strength, v[i].second->r.agility);
      if (!v.empty()) std::printf("  ... worst %.1f%%", 100 * v.back().first);
      std::printf("   (ESPN 2023: top edge 30-35%%, top DT ~20%%)\n");
      J[k ? "wr.top_dt" : "wr.top_edge"] = v.empty() ? 0.0 : 100 * v[0].first;
    }
    // per-man spreads over qualified men (NGS 2023 season rows, nflverse: separation SD 0.525 yd over receivers with 45+
    // targets; CPOE SD 2.91 over passers with 136+ attempts; RYOE/att SD 0.62 and YPC SD 0.78 over 100+ carries)
    std::vector<double> sepV, ypcV, mtrV, cpoeV;
    for (const auto& kv : manSep)
      if (kv.second.n >= 45 * perTeam) sepV.push_back(kv.second.r());
    for (const auto& kv : manRun)
      if (kv.second.n >= 100 * perTeam) ypcV.push_back(kv.second.r());
    for (const auto& kv : manTack)
      if (kv.second.n >= 40 * perTeam) mtrV.push_back(kv.second.r());
    {
      // (expected completion: the league's own rate at the attempt's depth, separation at the throw and pressure - NGS's
      // model reads the same things off the tracking)
      std::map<int, Rate> cell;
      auto key = [](const Att& x) { return x.air * 100 + x.sep * 10 + (x.pr ? 1 : 0); };
      for (const Att& x : atts) cell[key(x)].n++, cell[key(x)].w += x.c;
      std::map<const Player*, Rate> qbC, qbX;
      for (const Att& x : atts) {
        qbC[x.qb].n++;
        qbC[x.qb].w += x.c;
        qbX[x.qb].w += cell[key(x)].r();
      }
      for (const auto& kv : qbC)
        if (kv.second.n >= 136 * perTeam) cpoeV.push_back(100 * (kv.second.w - qbX[kv.first].w) / kv.second.n);
    }
    std::printf("star spread (SD across qualified men): separation at arrival %.3f yd (n %zu; NGS 0.525), CPOE %.2f%% (n %zu; NGS 2.91), "
                "YPC %.2f (n %zu; NGS 0.78, RYOE/att 0.62), missed-tackle rate %.1f%% (n %zu)\n",
                sdOf(sepV), sepV.size(), sdOf(cpoeV), cpoeV.size(), sdOf(ypcV), ypcV.size(), 100 * sdOf(mtrV), mtrV.size());
    J["spread.sep_sd"] = sdOf(sepV);
    J["spread.cpoe_sd"] = sdOf(cpoeV);
    J["spread.ypc_sd"] = sdOf(ypcV);
    J["spread.mtr_sd"] = 100 * sdOf(mtrV);
    std::printf("blocks formed %.0f a game: on a man already past the blocker's hip %.0f (%.2f%%; the leverage rule makes it 0), refused as past %.0f; "
                "his offset from the blocker's line %.2f yd, the edge's geometric part %+.2f points (mean)\n",
                engN / (std::max)(1, N), engHip, 100 * engHip / (std::max)(1.0, engN), engRef, engLat / (std::max)(1.0, engN), engGeo / (std::max)(1.0, engN));
    J["eng.past_hip"] = engHip;
    J["eng.past_hip_pct"] = 100 * engHip / (std::max)(1.0, engN);
    J["eng.refused"] = engRef;
    std::printf("targets with a defender within 1 yd at arrival %.1f%% (NGS aggressiveness 14.6%%)\n", 100 * tightWin.r());
    J["tight_window_pct"] = 100 * tightWin.r();
    std::printf("play action: %.2f yd an attempt against %.2f without (lift %+.2f; nflverse x FTN 2023 8.31 / 6.69, +1.62), comp %.1f%% / %.1f%%, "
                "sacks %.1f%% / %.1f%% of dropbacks (6.5 / 6.9), %.1f%% of dropbacks (20.4)\n",
                paYpa[1].r(), paYpa[0].r(), paYpa[1].r() - paYpa[0].r(), 100 * paComp[1].r(), 100 * paComp[0].r(), 100 * paSack[1].r(), 100 * paSack[0].r(),
                100 * paSack[1].n / (std::max)(1.0, paSack[0].n + paSack[1].n));
    {
      const char* tn[4] = {"run", "draw", "dropback", "play action"};
      std::printf("linebacker depth past the line at 0.5 / 1.0 / 1.5 s:");
      for (int k = 0; k < 4; k++)
        std::printf("  %s %.1f / %.1f / %.1f", tn[k], lbD[k][0] / (std::max)(1.0, lbN[k][0]), lbD[k][1] / (std::max)(1.0, lbN[k][1]), lbD[k][2] / (std::max)(1.0, lbN[k][2]));
      std::printf("\nyards a carry by run style: zone %.2f (%.0f), gap %.2f (%.0f), stretch %.2f (%.0f), draw %.2f (%.0f), sneak %.2f (%.0f)\n", ypcStyle[0].r(), ypcStyle[0].n,
                  ypcStyle[1].r(), ypcStyle[1].n, ypcStyle[2].r(), ypcStyle[2].n, ypcStyle[3].r(), ypcStyle[3].n, ypcStyle[4].r(), ypcStyle[4].n);
      J["lb.depth1_run"] = lbD[0][1] / (std::max)(1.0, lbN[0][1]);
      J["lb.depth1_draw"] = lbD[1][1] / (std::max)(1.0, lbN[1][1]);
      J["lb.depth1_pass"] = lbD[2][1] / (std::max)(1.0, lbN[2][1]);
      J["lb.depth1_pa"] = lbD[3][1] / (std::max)(1.0, lbN[3][1]);
      J["ypc.zone"] = ypcStyle[0].r();
      J["ypc.draw"] = ypcStyle[3].r();
    }
    J["pa.ypa_lift"] = paYpa[1].r() - paYpa[0].r();
    J["pa.ypa"] = paYpa[1].r();
    J["pa.sack_pct"] = 100 * paSack[1].r();
    if (!allWins.empty()) {
      double mo = 0, mw = 0, cov = 0, vo = 0, vw = 0;
      for (size_t k = 0; k < allWins.size(); k++) mo += allOvr[k], mw += allWins[k];
      mo /= static_cast<double>(allWins.size());
      mw /= static_cast<double>(allWins.size());
      for (size_t k = 0; k < allWins.size(); k++) cov += (allOvr[k] - mo) * (allWins[k] - mw), vo += (allOvr[k] - mo) * (allOvr[k] - mo), vw += (allWins[k] - mw) * (allWins[k] - mw);
      const double rr = vo > 0 && vw > 0 ? cov / std::sqrt(vo * vw) : 0.0;
      std::printf("seasons %d: team wins SD %.2f (nflverse 2023 2.75), point differential SD %.1f a season (100.1), team OVR against wins r = %.2f\n", seasons,
                  sdOf(allWins), sdOf(allPd), rr);
      J["season.wins_sd"] = sdOf(allWins);
      J["season.pd_sd"] = sdOf(allPd);
      J["season.ovr_wins_r"] = rr;
    }
  }
  if (jsonOut) {
    if (FILE* f = std::fopen(jsonOut, "w")) {
      std::fprintf(f, "{");
      bool first = true;
      for (const auto& kv : J) {
        std::fprintf(f, "%s\n  \"%s\": %.6g", first ? "" : ",", kv.first.c_str(), kv.second);
        first = false;
      }
      std::fprintf(f, "\n}\n");
      std::fclose(f);
      std::printf("wrote %s\n", jsonOut);
    }
  }
  return 0;
}
