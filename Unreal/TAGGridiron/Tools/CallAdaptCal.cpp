// The tables adaptive play calling reads (Sim/CallAdapt.h), measured on the sim itself, written to
// Source/TAGGridiron/Sim/CallAdaptTables.inl:
//   kPrior[sit][kind]     the league's offensive calls by situation (CPU games, the lean off)
//   kDefPrior[sit][kind]  the league's defensive calls by situation
//   kClsEpa[kind]         expected points a play each kind of offensive call gets in those games
//   kMatch[kind][dkind]   controlled plays: each kind of offensive call against each kind of defensive call, the plays
//                         and calls drawn as the CPU callers draw them in real game situations; expected points a play
//                         against the kind's average over the league's defensive mix, shrunk by n / (n + 150)
// Usage: CallAdaptCal [games=300] [seed=11] [cell=400] [--out file] (run from Tools/; regenerate after a playbook or
// play-calling change, then rebuild)
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CallAdapt.h"
#include "Game.h"
#include "League.h"
#include "Rng.h"

using namespace tag;

namespace {
struct Snap {
  int down = 1;
  double toGo = 10, ballOn = 25, ballY = FIELD_W / 2;
  int q = 1;
  double clock = 900;
  int margin = 0;
};
}  // namespace

int main(int argc, char** argv) {
  int games = 300, cell = 400;
  uint32_t seed = 11;
  std::string out = "../Source/TAGGridiron/Sim/CallAdaptTables.inl";
  int pos = 0;
  bool runsOnly = false;
  int conceptsFrom = 0, conceptsTo = 0;  // --concepts <from> <to>: a slice of the per-concept matchups (to --out)
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--concepts") == 0 && i + 2 < argc) {
      conceptsFrom = std::atoi(argv[++i]);
      conceptsTo = std::atoi(argv[++i]);
      continue;
    }
    if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
      out = argv[++i];
      continue;
    }
    if (std::strcmp(argv[i], "--runs") == 0) {  // the run game's call mix by concept family and scheme, and where each went
      runsOnly = true;
      continue;
    }
    if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc) {
      const std::string kv = argv[++i];
      const size_t eq = kv.find('=');
      const double v = eq == std::string::npos ? 0 : std::atof(kv.c_str() + eq + 1);
      if (eq == std::string::npos || !(setTune(kv.substr(0, eq), v) || setCallTune(kv.substr(0, eq), v))) return std::fprintf(stderr, "unknown tune %s\n", kv.c_str()), 2;
      continue;
    }
    if (pos == 0) games = std::atoi(argv[i]);
    else if (pos == 1) seed = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10));
    else if (pos == 2) cell = std::atoi(argv[i]);
    pos++;
  }
  League league = createLeague(2027);
  Rng rng(seed);
  if (conceptsTo > conceptsFrom) {
    // per concept: its plays (from the books that have it) against each kind of defensive call in real situations, as
    // "concept<TAB>kind<TAB>n<TAB>sum epa<TAB>sum epa^2" lines (Tools/Realism/concept_tables.py merges the slices)
    callTune().adapt = 0;
    std::vector<Snap> snaps;
    for (int gi = 0; gi < 40; gi++) {
      GameOptions o;
      o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
      Game g(*league[gi % 32], *league[(gi + 5) % 32], o);
      Game* gp = &g;
      g.onPlayResult = [&snaps, gp](const PlaySim&, const PlayResult&) {
        Snap s;
        s.down = gp->down;
        s.toGo = gp->toGo;
        s.ballOn = gp->ballOn;
        s.ballY = gp->ballY;
        s.q = gp->q;
        s.clock = gp->clock;
        if (s.down <= 3 && s.ballOn < 90) snaps.push_back(s);
      };
      g.simToEnd();
    }
    std::map<std::string, std::vector<std::pair<int, const OffPlay*>>> byKey;  // concept -> (team, play) in the books
    for (int ti = 0; ti < 32; ti++)
      for (const OffPlay* p : offBookFor(*league[ti]).plays)
        if (p->type != PlayType::Kneel && p->type != PlayType::Spike && (!p->run || p->run->style != RunStyle::Sneak)) byKey[conceptKey(*p)].emplace_back(ti, p);
    std::vector<std::string> keys;
    for (const auto& kv : byKey) keys.push_back(kv.first);
    FILE* fo = std::fopen(out.c_str(), "w");
    if (!fo) return 1;
    for (int ki = conceptsFrom; ki < conceptsTo && ki < static_cast<int>(keys.size()); ki++) {
      const auto& opts = byKey[keys[ki]];
      for (int f = 0; f < kFamN; f++) {
        double n = 0, s1 = 0, s2 = 0;
        for (int tries = 0; n < cell && tries < cell * 20; tries++) {
          const auto& tp = opts[static_cast<size_t>(rng.intRange(0, static_cast<int>(opts.size()) - 1))];
          const Snap& s = snaps[static_cast<size_t>(rng.intRange(0, static_cast<int>(snaps.size()) - 1))];
          const int di = (tp.first + 1 + rng.intRange(0, 30)) % 32;
          GameOptions o;
          o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
          Game g(*league[tp.first], *league[di], o);
          g.phase = GamePhase::Scrimmage;
          g.poss = TeamKey::Home;
          g.down = s.down;
          g.toGo = s.toGo;
          g.ballOn = s.ballOn;
          g.ballY = s.ballY;
          g.q = s.q;
          g.clock = s.clock;
          std::vector<const DefCall*> dpool;
          std::vector<double> dw;
          g.cpuDefensePool(tp.second->formation, rng, dpool, dw);
          for (size_t i = 0; i < dpool.size(); i++)
            if (defFamily(*dpool[i]) != f) dw[i] = 0;
          double tdw = 0;
          for (double x : dw) tdw += x;
          if (tdw <= 0) {
            dpool.clear();
            dw.clear();
            for (const DefCall* d : defBookFor(g.defense()).calls)
              if (defFamily(*d) == f) dpool.push_back(d), dw.push_back(1);
            if (dpool.empty()) continue;
          }
          const DefCall* dc = rng.weighted(dpool, dw);
          auto sim = g.makePlay(*tp.second, *dc);
          sim->runToEnd();
          if (!sim->result) continue;
          const double e = playEpa(s.down, s.toGo, s.ballOn, *sim->result);
          n++, s1 += e, s2 += e * e;
        }
        std::fprintf(fo, "%s\t%d\t%.0f\t%.6f\t%.6f\t%d\n", keys[ki].c_str(), f, n, s1, s2, offClass(*opts[0].second));
      }
      std::fflush(fo);
    }
    std::fclose(fo);
    std::printf("concepts %d..%d of %zu done\n", conceptsFrom, conceptsTo, keys.size());
    return 0;
  }
  if (runsOnly) {
    // designed runs (no sneaks): the calls by family, by scheme and in all; where each family crossed the line (nflverse's
    // run_gap classes, as SimCalibrate's gap.*) and its yards
    std::map<std::string, std::array<double, kRunFamN>> bySch;
    std::array<double, kRunFamN> all{}, yds{};
    std::array<std::array<double, 4>, kRunFamN> where{};
    std::array<double, 4> gapAll{};
    for (int gi = 0; gi < games; gi++) {
      const int h = rng.intRange(0, 31);
      const int a = (h + 1 + rng.intRange(0, 30)) % 32;
      GameOptions o;
      o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
      Game g(*league[h], *league[a], o);
      Game* gp = &g;
      g.onPlayResult = [&, gp](const PlaySim& sim, const PlayResult& r) {
        const OffPlay& op = *sim.cfg.offPlay;
        const int fam = runFamily(op);
        if (fam < 0 || op.type != PlayType::Run || op.run->style == RunStyle::Sneak) return;
        bySch[gp->offense().offScheme][fam]++;
        all[fam]++;
        if (r.kind != PlayKind::Run || !r.runDesigned) return;
        const double ay = std::fabs(r.runCrossed ? r.runCrossDy : r.runAimDy);
        const int gc = ay < 1.35 ? 0 : ay < 2.7 ? 1 : ay < 4.05 ? 2 : 3;
        where[fam][gc]++;
        gapAll[gc]++;
        yds[fam] += r.yards;
      };
      g.simToEnd();
    }
    double n = 0;
    for (double v : all) n += v;
    std::printf("%d games, %.0f designed run calls\n%-14s", games, n, "family");
    for (int f = 0; f < kRunFamN; f++) std::printf(" %12s", runFamilyName(f));
    std::printf("\n%-14s", "league %");
    for (int f = 0; f < kRunFamN; f++) std::printf(" %12.1f", 100 * all[f] / (std::max)(1.0, n));
    std::printf("\n");
    for (const auto& kv : bySch) {
      double m = 0;
      for (double v : kv.second) m += v;
      std::printf("%-14s", kv.first.c_str());
      for (int f = 0; f < kRunFamN; f++) std::printf(" %12.1f", 100 * kv.second[f] / (std::max)(1.0, m));
      std::printf("   (%.0f)\n", m);
    }
    static const char* gn[4] = {"middle", "guard", "tackle", "end"};
    std::printf("\nwhere each family crossed (%% middle / guard / tackle / end) and yd a carry:\n");
    for (int f = 0; f < kRunFamN; f++) {
      double m = 0;
      for (double v : where[f]) m += v;
      std::printf("  %-13s", runFamilyName(f));
      for (int k = 0; k < 4; k++) std::printf(" %5.1f", 100 * where[f][k] / (std::max)(1.0, m));
      std::printf("   %.2f yd (%.0f)\n", yds[f] / (std::max)(1.0, m), m);
    }
    double gt = 0;
    for (double v : gapAll) gt += v;
    std::printf("all designed runs:");
    for (int k = 0; k < 4; k++) std::printf(" %s %.1f%%", gn[k], 100 * gapAll[k] / (std::max)(1.0, gt));
    std::printf("  (NFL 26.4 / 26.0 / 22.6 / 24.3)\n");
    return 0;
  }
  callTune().adapt = 0;  // (the league as it calls without the lean)

  // ---- 1. CPU games: who calls what where, and what it gets
  double P[kSitN][kClsN] = {}, Q[kSitN][kFamN] = {}, clsE[kClsN] = {}, clsN[kClsN] = {};
  double famAll[kFamN] = {};
  std::vector<Snap> snaps;
  double passCalls = 0, calls = 0;
  for (int gi = 0; gi < games; gi++) {
    const int h = rng.intRange(0, 31);
    const int a = (h + 1 + rng.intRange(0, 30)) % 32;
    GameOptions o;
    o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
    Game g(*league[h], *league[a], o);
    Game* gp = &g;
    g.onPlayResult = [&, gp](const PlaySim& sim, const PlayResult& res) {
      const OffPlay& op = *sim.cfg.offPlay;
      if (op.type == PlayType::Kneel || op.type == PlayType::Spike || !sim.cfg.defCall) return;
      const int b = sitBucket(gp->down, gp->toGo);
      const int c = offClass(op), f = defFamily(*sim.cfg.defCall);
      P[b][c]++;
      Q[b][f]++;
      famAll[f]++;
      clsE[c] += playEpa(gp->down, gp->toGo, gp->ballOn, res);
      clsN[c]++;
      calls++;
      passCalls += isPassClass(c) ? 1 : 0;
      Snap s;
      s.down = gp->down;
      s.toGo = gp->toGo;
      s.ballOn = gp->ballOn;
      s.ballY = gp->ballY;
      s.q = gp->q;
      s.clock = gp->clock;
      s.margin = gp->margin();
      snaps.push_back(s);
    };
    g.simToEnd();
  }
  std::printf("%d games, %.0f calls, pass calls %.1f %%\n", games, calls, 100 * passCalls / (std::max)(1.0, calls));
  for (int c = 0; c < kClsN; c++)
    std::printf("  %-18s %6.0f calls  %5.1f %%  epa %+.3f\n", offClassName(c), clsN[c], 100 * clsN[c] / calls, clsE[c] / (std::max)(1.0, clsN[c]));
  for (int f = 0; f < kFamN; f++) std::printf("  %-18s %6.0f calls  %5.1f %%\n", defFamilyName(f), famAll[f], 100 * famAll[f] / calls);

  // ---- 2. controlled plays: every kind of offensive call against every kind of defensive call
  double cE[kClsN][kFamN] = {}, cE2[kClsN][kFamN] = {}, cN[kClsN][kFamN] = {};
  for (int c = 0; c < kClsN; c++) {
    for (int f = 0; f < kFamN; f++) {
      int tries = 0;
      while (cN[c][f] < cell && tries < cell * 60) {
        tries++;
        const Snap& s = snaps[static_cast<size_t>(rng.intRange(0, static_cast<int>(snaps.size()) - 1))];
        const int h = rng.intRange(0, 31);
        const int a = (h + 1 + rng.intRange(0, 30)) % 32;
        GameOptions o;
        o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
        Game g(*league[h], *league[a], o);
        g.phase = GamePhase::Scrimmage;
        g.down = s.down;
        g.toGo = s.toGo;
        g.ballOn = s.ballOn;
        g.ballY = s.ballY;
        g.q = s.q;
        g.clock = s.clock;
        // the offense's plays of this kind as the CPU weighs them here
        std::vector<const OffPlay*> pool;
        std::vector<double> w;
        g.cpuOffensePool(isPassClass(c), pool, w);  // (screens and play action are in the pass pool, RPOs in the run's)
        for (size_t i = 0; i < pool.size(); i++)
          if (offClass(*pool[i]) != c) w[i] = 0;
        double tw = 0;
        for (double x : w) tw += x;
        if (tw <= 0) continue;
        const OffPlay* op = rng.weighted(pool, w);
        std::vector<const DefCall*> dpool;
        std::vector<double> dw;
        g.cpuDefensePool(op->formation, rng, dpool, dw);
        for (size_t i = 0; i < dpool.size(); i++)
          if (defFamily(*dpool[i]) != f) dw[i] = 0;
        double tdw = 0;
        for (double x : dw) tdw += x;
        if (tdw <= 0) {
          // (a front without this kind of call here: the defense's book has it in another set)
          dpool.clear();
          dw.clear();
          for (const DefCall* d : defBookFor(g.defense()).calls)
            if (defFamily(*d) == f) {
              dpool.push_back(d);
              dw.push_back(1);
            }
          if (dpool.empty()) continue;
        }
        const DefCall* dc = rng.weighted(dpool, dw);
        auto sim = g.makePlay(*op, *dc);
        sim->runToEnd();
        if (!sim->result) continue;
        const double e = playEpa(s.down, s.toGo, s.ballOn, *sim->result);
        cE[c][f] += e;
        cE2[c][f] += e * e;
        cN[c][f]++;
      }
    }
    std::printf("  matchups for %s done\n", offClassName(c));
    std::fflush(stdout);
  }
  // the kind's average over the league's defensive mix; each cell against it, shrunk
  double famShare[kFamN];
  for (int f = 0; f < kFamN; f++) famShare[f] = famAll[f] / (std::max)(1.0, calls);
  double M[kClsN][kFamN] = {};
  std::printf("\nmatchups (epa a play against the kind's average; se):\n%-18s", "");
  for (int f = 0; f < kFamN; f++) std::printf(" %15s", defFamilyName(f));
  std::printf("\n");
  for (int c = 0; c < kClsN; c++) {
    double avg = 0, ws = 0;
    for (int f = 0; f < kFamN; f++)
      if (cN[c][f] > 0) {
        avg += famShare[f] * cE[c][f] / cN[c][f];
        ws += famShare[f];
      }
    avg /= (std::max)(1e-9, ws);
    std::printf("%-18s", offClassName(c));
    for (int f = 0; f < kFamN; f++) {
      if (cN[c][f] <= 0) {
        std::printf(" %15s", "-");
        continue;
      }
      const double m = cE[c][f] / cN[c][f];
      const double sd = std::sqrt((std::max)(0.0, cE2[c][f] / cN[c][f] - m * m));
      M[c][f] = (m - avg) * cN[c][f] / (cN[c][f] + 150.0);
      std::printf("   %+.3f (%.3f)", m - avg, sd / std::sqrt(cN[c][f]));
    }
    std::printf("\n");
  }

  FILE* fo = std::fopen(out.c_str(), "w");
  if (!fo) {
    std::fprintf(stderr, "cannot write %s\n", out.c_str());
    return 1;
  }
  std::fprintf(fo, "// Generated by Tools/CallAdaptCal.cpp (%d CPU games, seed %u, %d controlled plays a matchup): do not edit by hand.\n", games, seed, cell);
  std::fprintf(fo, "// kinds: %s", offClassName(0));
  for (int c = 1; c < kClsN; c++) std::fprintf(fo, ", %s", offClassName(c));
  std::fprintf(fo, "\n//    vs: %s", defFamilyName(0));
  for (int f = 1; f < kFamN; f++) std::fprintf(fo, ", %s", defFamilyName(f));
  std::fprintf(fo, "\n");
  auto row = [fo](const double* v, int n) {
    std::fprintf(fo, "    {");
    for (int i = 0; i < n; i++) std::fprintf(fo, "%s%.4f", i ? ", " : "", v[i]);
    std::fprintf(fo, "}");
  };
  std::fprintf(fo, "const double kPrior[kSitN][kClsN] = {\n");
  for (int b = 0; b < kSitN; b++) {
    double n = 0, v[kClsN];
    for (int c = 0; c < kClsN; c++) n += P[b][c];
    for (int c = 0; c < kClsN; c++) v[c] = n > 0 ? P[b][c] / n : 1.0 / kClsN;
    row(v, kClsN);
    std::fprintf(fo, "%s  // %s, %.0f calls\n", b + 1 < kSitN ? "," : "", sitName(b), n);
  }
  std::fprintf(fo, "};\nconst double kDefPrior[kSitN][kFamN] = {\n");
  for (int b = 0; b < kSitN; b++) {
    double n = 0, v[kFamN];
    for (int f = 0; f < kFamN; f++) n += Q[b][f];
    for (int f = 0; f < kFamN; f++) v[f] = n > 0 ? Q[b][f] / n : 1.0 / kFamN;
    row(v, kFamN);
    std::fprintf(fo, "%s  // %s\n", b + 1 < kSitN ? "," : "", sitName(b));
  }
  std::fprintf(fo, "};\nconst double kMatch[kClsN][kFamN] = {\n");
  for (int c = 0; c < kClsN; c++) {
    row(M[c], kFamN);
    std::fprintf(fo, "%s  // %s\n", c + 1 < kClsN ? "," : "", offClassName(c));
  }
  double ce[kClsN];
  for (int c = 0; c < kClsN; c++) ce[c] = clsN[c] > 0 ? clsE[c] / clsN[c] : 0;
  std::fprintf(fo, "};\nconst double kClsEpa[kClsN] =\n");
  row(ce, kClsN);
  std::fprintf(fo, ";\n");
  std::fclose(fo);
  std::printf("wrote %s\n", out.c_str());
  return 0;
}
