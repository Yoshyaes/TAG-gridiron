// Port of tools/calibrate.ts
// Headless realism calibration: simulate many full games and compare to NFL per-team-game averages.
// Usage: SimCalibrate [games=40] [seed=7] [--scores]
//   --scores  also print one line per game (teams, final score, log length) for diffing vs TS.
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
  int positional = 0;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--scores") == 0) {
      printScores = true;
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

  for (int g = 0; g < N; g++) {
    const int hi = rng.intRange(0, 31);
    int ai = rng.intRange(0, 31);
    if (ai == hi) ai = (ai + 1) % 32;
    GameOptions opts;
    opts.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
    Game game(*league[hi], *league[ai], opts);
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
  }
  std::printf("QB rating avg: %.1f (NFL ~ 92)\n", mean(qbRatings));
  const double ms = mean(scores);
  std::vector<double> sq;
  for (double s : scores) sq.push_back((s - ms) * (s - ms));
  std::printf("Points SD: %.1f (NFL ~ 10)\n", std::sqrt(mean(sq)));
  std::printf("%d stats outside tolerance\n", off);
  return 0;
}
