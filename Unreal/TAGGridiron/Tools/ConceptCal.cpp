// Per-concept play outcomes: every offensive play (grouped by concept: the stock play's id, or the generated play's
// concept suffix) run N times, 1st & 10 at the 30, against the stock defensive calls (or, with --def, every defensive
// call grouped by its call suffix against the stock plays). Prints yards/play, completion, sacks, air yards.
//   build\ConceptCal.exe [reps=40] [--def] [--form]   (--form: grouped by formation, runs and passes apart)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "Game.h"
#include "League.h"
#include "PlaySim.h"
#include "Playbook.h"

using namespace tag;

namespace {
struct Acc {
  int n = 0, pass = 0, comp = 0, sack = 0, ints = 0, behind = 0;
  double yds = 0, air = 0;
};
std::string group(const std::string& id) {
  const size_t at = id.find("__");
  return at == std::string::npos ? "stock:" + id : id.substr(at + 2);
}
}  // namespace

int main(int argc, char** argv) {
  int reps = 40;
  bool defMode = false, byForm = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--def") == 0) defMode = true;
    else if (std::strcmp(argv[i], "--form") == 0) byForm = true;
    else reps = std::atoi(argv[i]);
  }
  League league = createLeague(2027);
  std::map<std::string, Acc> acc;
  std::vector<const OffPlay*> offs;
  std::vector<const DefCall*> defs;
  for (const OffPlay& p : OFF_PLAYS())
    if (p.type != PlayType::Kneel && p.type != PlayType::Spike && (!defMode || p.stock)) offs.push_back(&p);
  for (const DefCall& d : DEF_PLAYS())
    if (defMode || d.set.empty()) defs.push_back(&d);
  uint32_t seed = 1;
  if (!defMode) {
    for (const OffPlay* op : offs) {
      for (int r = 0; r < reps; r++) {
        GameOptions o;
        o.seed = seed++;
        Game g(*league[(r * 2) % 32], *league[(r * 2 + 1) % 32], o);
        g.phase = GamePhase::Scrimmage;
        g.ballOn = 30;
        g.down = 1;
        g.toGo = 10;
        const DefCall* dc = defs[(r * 7 + seed) % defs.size()];
        if (dc->front == Front::Goalline) dc = defs[0];
        auto sim = g.makePlay(*op, *dc);
        const PlayResult& res = sim->runToEnd();
        Acc& a = acc[byForm ? (op->type == PlayType::Run ? "run " : "pass ") + op->formation : group(op->id)];
        a.n++;
        a.yds += res.yards;
        if (op->type != PlayType::Run) {
          a.pass++;
          if (res.complete) a.comp++;
          if (res.kind == PlayKind::Sack) a.sack++;
          if (res.interception) a.ints++;
          if (res.complete || res.incomplete) {
            a.air += res.airYards;
            if (res.airYards < 0) a.behind++;
          }
        }
      }
    }
  } else {
    for (const DefCall* dc : defs) {
      if (dc->front == Front::Goalline) continue;
      for (int r = 0; r < reps; r++) {
        GameOptions o;
        o.seed = seed++;
        Game g(*league[(r * 2) % 32], *league[(r * 2 + 1) % 32], o);
        g.phase = GamePhase::Scrimmage;
        g.ballOn = 30;
        g.down = 1;
        g.toGo = 10;
        const OffPlay* op = offs[(r * 11 + seed) % offs.size()];
        if (formationById(op->formation)->personnel == "22") op = offs[0];
        auto sim = g.makePlay(*op, *dc);
        const PlayResult& res = sim->runToEnd();
        const std::string key = std::string(coverageName(dc->coverage)) + " r" + std::to_string(dc->rushers()) + " " + frontName(dc->front);
        Acc& a = acc[key];
        a.n++;
        a.yds += res.yards;
        if (op->type != PlayType::Run) {
          a.pass++;
          if (res.complete) a.comp++;
          if (res.kind == PlayKind::Sack) a.sack++;
          if (res.interception) a.ints++;
          if (res.complete || res.incomplete) a.air += res.airYards;
        }
      }
    }
  }
  std::printf("%-26s %6s %6s %6s %6s %6s %6s\n", "group", "n", "yds", "comp%", "sack%", "air", "behind%");
  Acc tot;
  for (const auto& kv : acc) {
    const Acc& a = kv.second;
    const int thrown = (std::max)(1, a.pass - a.sack);
    std::printf("%-26s %6d %6.2f %6.1f %6.1f %6.1f %6.1f\n", kv.first.c_str(), a.n, a.yds / a.n, a.pass ? 100.0 * a.comp / thrown : 0.0,
                a.pass ? 100.0 * a.sack / a.pass : 0.0, a.pass ? a.air / thrown : 0.0, a.pass ? 100.0 * a.behind / thrown : 0.0);
  }
  return 0;
}
