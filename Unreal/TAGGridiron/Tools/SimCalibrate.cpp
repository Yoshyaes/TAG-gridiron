// Port of tools/calibrate.ts
// Headless realism calibration: simulate many full games and compare to NFL per-team-game averages.
// Usage: SimCalibrate [games=40] [seed=7] [--scores]
//   --scores  also print one line per game (teams, final score, log length) for diffing vs TS.
#include <algorithm>
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
    if (positional == 0) N = std::atoi(argv[i]);
    else if (positional == 1) seed = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
    positional++;
  }
  Rng rng(seed);
  League league = createLeague(2027);

  // NFL 2023-2025 regular season, per team per game (approx.)
  const std::vector<std::pair<std::string, std::pair<double, double>>> NFL = {
      {"points", {22.0, 1.5}},   {"plays", {62.5, 2.0}},     {"yards", {330, 15}},        {"ydsPerPlay", {5.3, 0.25}},
      {"passAtt", {33.5, 2}},    {"compPct", {64.5, 1.5}},   {"passYdsGross", {215, 12}}, {"ypa", {6.9, 0.35}},
      {"rushAtt", {27, 2}},      {"ypc", {4.3, 0.2}},        {"sacks", {2.5, 0.4}},       {"sackRate", {6.8, 1}},
      {"ints", {0.8, 0.2}},      {"fumblesLost", {0.45, 0.15}}, {"firstDowns", {19.5, 1.2}}, {"thirdPct", {39, 3}},
      {"fgPct", {85, 4}},        {"punts", {4.0, 0.5}},      {"penalties", {6.0, 1}},     {"tds", {2.5, 0.3}},
      {"explosive", {7.0, 1.5}},
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
  double airSum = 0, airN = 0, dropbacksX = 0, pressured = 0, tackles = 0, tacklerSum = 0;
  int maxAirSeen = 0;
  std::map<std::string, int> deepOutcome, shortOutcome;
  std::vector<double> sepAt[4];
  std::vector<double> ttt;
  std::vector<double> olDeep;
  // free-running acceleration over 0.2 s windows (the full-game telemetry's measure); TAG_ACCEL=1 turns it on
  const bool accelOn = std::getenv("TAG_ACCEL") != nullptr;
  const PlaySim* accSim = nullptr;
  // where passes arrive relative to the target (TAG_MISS=1): along his run (+ ahead) and across
  const bool missOn = std::getenv("TAG_MISS") != nullptr;
  const PlaySim* missSim = nullptr;
  double missLastT = 0;
  bool missDone = false;
  bool predDone = false;
  std::vector<double> predAlong[2];
  std::vector<std::pair<double, double>> missDeep, missShort;
  double accLastT = 0;
  std::map<const Ent*, std::vector<std::pair<V2, bool>>> hist;
  std::vector<double> tanA, latA;
  double accN = 0, accOver1g = 0;
  std::map<std::string, int> sackHow, unblockedPos;
  std::vector<double> sackT;
  double pathN[4] = {0, 0, 0, 0}, pathBehind[4] = {0, 0, 0, 0}, pathAir[4] = {0, 0, 0, 0};
  int lastQb = -1, lastQbPoss = -1;
  double kickoffs = 0, koReturned = 0, fourthGo = 0;
  std::map<std::string, double> J;  // --json
  double qbSwaps = 0, qbSnaps = 0;
  double yacSum = 0, compAirSum = 0, compN = 0;

  for (int g = 0; g < N; g++) {
    const int hi = rng.intRange(0, 31);
    int ai = rng.intRange(0, 31);
    if (ai == hi) ai = (ai + 1) % 32;
    GameOptions opts;
    opts.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
    Game game(*league[hi], *league[ai], opts);
    lastQb = -1;
    lastQbPoss = -1;
    game.onPlayResult = [&](const PlaySim& sim, const PlayResult& r) {
      if (sim.qb && sim.qb->p) {
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
      if (r.olBlockDepth > -99) olDeep.push_back(r.olBlockDepth);
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
      if (r.kind == PlayKind::Pass && !r.thrownAway) {
        const int b = r.airYards < 0 ? 0 : r.airYards < 10 ? 1 : r.airYards < 20 ? 2 : 3;
        depthAtt[b]++;
        if (r.throwPath >= 0 && r.throwPath < 4) {
          pathN[r.throwPath]++;
          pathBehind[r.throwPath] += r.airYards < 0 ? 1 : 0;
          pathAir[r.throwPath] += r.airYards;
        }
        if (r.complete) depthComp[b]++;
        if (b == 3 || b == 1) {
          std::string o = "ground";
          for (const PlayEvent& e : r.events)
            if (e.type == "catch" || e.type == "divecatch" || e.type == "drop" || e.type == "contested_inc" ||
                e.type == "pbu" || e.type == "int" || e.type == "oobcatch")
              o = e.type;
          (b == 3 ? deepOutcome : shortOutcome)[o]++;
        }
        if (r.complete && r.catchSep >= 0) sepAt[b].push_back(r.catchSep);
        airSum += r.airYards;
        if (r.complete) {
          yacSum += r.yac;
          compAirSum += r.airYards;
          compN++;
        }
        airN++;
        maxAirSeen = (std::max)(maxAirSeen, r.airYards);
      }
      if (r.kind == PlayKind::Pass || r.kind == PlayKind::Sack) {
        dropbacksX++;
        if (r.pressured || r.kind == PlayKind::Sack) pressured++;
        if (r.kind == PlayKind::Pass && r.timeToThrow > 0) ttt.push_back(r.timeToThrow);
      }
      if (!r.tacklers.empty() && r.kind != PlayKind::Sack) {
        tackles++;
        tacklerSum += static_cast<double>(r.tacklers.size());
      }
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
        }
        accLastT = sim.t;
        for (const Ent* e : sim.ents) {
          auto& h = hist[e];
          const bool fr = !e->engaged && e->state != EntState::Down && e->stun <= 0 && !e->pancaked;
          h.push_back(std::make_pair(e->pos, fr));
          const size_t i = h.size() - 1;
          if (i < 24 || i % 6) continue;
          if (!h[i].second || !h[i - 12].second || !h[i - 24].second) continue;
          const double M = 0.9144;  // yd -> m
          const V2 v1 = scale(sub(h[i - 12].first, h[i - 24].first), M / 0.2);
          const V2 v2 = scale(sub(h[i].first, h[i - 12].first), M / 0.2);
          const V2 a = scale(sub(v2, v1), 1 / 0.2);
          const V2 va = scale(add(v1, v2), 0.5);
          const double sp = len(va);
          accN++;
          if (len(a) > 9.81) accOver1g++;
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
      // a log line carries the state AFTER its play: a 4th-down play is the next scrimmage line after one that left 4th down
      auto scrimmage = [](const std::string& s) {
        for (const char* k : {" punts ", "field goal", "PENALTY", " kicks ", "kneel", "extra point", "Timeout", "END OF", "spike", "two-point"})
          if (s.find(k) != std::string::npos) return false;
        return true;
      };
      if (li > 0 && game.log[li - 1].down == 4 && game.log[li - 1].poss == l.poss && scrimmage(d)) fourthGo++;
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
    std::printf("aDOT %.1f (NFL ~7.9), longest air %d yd, time to throw median %.2f s (NFL ~2.8), pressured %.1f%% (NFL ~33)\n",
                airSum / (std::max)(1.0, airN), maxAirSeen, ttt.empty() ? 0.0 : ttt[ttt.size() / 2],
                100 * pressured / (std::max)(1.0, dropbacksX));
    std::printf("tacklers per tackle %.2f (NFL ~1.35)\n", tacklerSum / (std::max)(1.0, tackles));
    J["adot"] = airSum / (std::max)(1.0, airN);
    J["max_air_yd"] = maxAirSeen;
    J["ttt_median_s"] = ttt.empty() ? 0.0 : ttt[ttt.size() / 2];
    J["pressured_pct"] = 100 * pressured / (std::max)(1.0, dropbacksX);
    J["tacklers_per_tackle"] = tacklerSum / (std::max)(1.0, tackles);
    J["yac_per_completion"] = yacSum / (std::max)(1.0, compN);
    J["air_per_completion"] = compAirSum / (std::max)(1.0, compN);
    J["qb_changes_per_game"] = qbSwaps / (std::max)(1, N);
    std::printf("per completion: air yards %.1f, YAC %.1f (NFL 2024 ~5.6 / ~5.2)\n", compAirSum / (std::max)(1.0, compN),
                yacSum / (std::max)(1.0, compN));
    {
      const char* pn[4] = {"read", "hot", "pressure", "quick"};
      std::printf("throws by decision:");
      for (int i = 0; i < 4; i++)
        std::printf("  %s %.0f (behind LOS %.0f%%, aDOT %.1f)", pn[i], pathN[i], 100 * pathBehind[i] / (std::max)(1.0, pathN[i]),
                    pathAir[i] / (std::max)(1.0, pathN[i]));
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
      J["accel.speedup_p99"] = tanA[tanA.size() * 99 / 100];
      J["accel.braking_p1"] = tanA[tanA.size() / 100];
      J["accel.lateral_p95_at_6ms"] = latA[latA.size() * 95 / 100];
    }
    std::sort(olDeep.begin(), olDeep.end());
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
    std::printf("20+ yd outcomes:");
    for (const auto& kv : deepOutcome) std::printf("  %s %d", kv.first.c_str(), kv.second);
    std::printf("\n0-9 yd outcomes:");
    for (const auto& kv : shortOutcome) std::printf("  %s %d", kv.first.c_str(), kv.second);
    std::printf("\n");
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
