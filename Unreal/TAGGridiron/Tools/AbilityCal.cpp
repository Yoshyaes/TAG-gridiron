// What each Superstar / X-Factor ability is worth on the field, measured.
// Usage: AbilityCal [plays=3000] [seed=11] [ability_key ... | all | league]
//
// An ability: every player in the league who could carry it (his position and ratings; the grade waived for a big
// sample) gets it and nothing else, an X-Factor held in the zone. Then the same plays are run twice, seed for seed -
// once with abilities on, once off - with a carrier on the field (his team on offense for an offensive ability, on
// defense for a defensive one), and his plays compared: passing, rushing, receiving, defense, protection, kicking,
// whatever he has numbers in. Paired plays: the only difference between the two runs is the ability.
// "league": the shipped league (the coach's picks, zones earned) against no abilities, over whole games.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Abilities.h"
#include "Game.h"
#include "League.h"

using namespace tag;

namespace {

bool offensePos(Pos p) {
  return p == Pos::QB || p == Pos::RB || p == Pos::FB || p == Pos::WR || p == Pos::TE || p == Pos::OT || p == Pos::OG || p == Pos::C;
}

struct Tally {
  double plays = 0, yards = 0, oppPass = 0, oppComp = 0;
  double dropbacks = 0, att = 0, comp = 0, passYds = 0, ints = 0, sacked = 0, pressured = 0;
  double carries = 0, rushYds = 0, btk = 0, fum = 0;
  double targets = 0, rec = 0, recYds = 0, yac = 0;
  double tkl = 0, sacks = 0, intBy = 0, pbu = 0, ff = 0, tfl = 0, missed = 0;
  double runs = 0, runYds = 0;
};

double rate(double a, double b, double k = 1) { return b > 0 ? a / b * k : 0; }

void tally(Tally& t, const PlaySim& sim, const PlayResult& r, const Player* me) {
  const bool off = offensePos(me->pos);
  t.plays++;
  t.yards += r.yards;
  const bool drop = r.kind == PlayKind::Pass || r.kind == PlayKind::Sack;
  if (r.kind == PlayKind::Run) {
    t.runs++;
    t.runYds += r.yards;
  }
  if (drop) {
    t.dropbacks++;
    t.sacked += r.kind == PlayKind::Sack;
    t.pressured += r.pressured;
    if (r.kind == PlayKind::Pass && !r.thrownAway) {
      t.oppPass++;
      t.oppComp += r.complete;
    }
  }
  if (off) {
    if ((r.passer == me || (r.kind == PlayKind::Sack && sim.qb && sim.qb->p == me)) && drop) {
      if (r.kind == PlayKind::Pass && !r.thrownAway) {
        t.att++;
        t.comp += r.complete;
        t.passYds += r.complete ? r.yards : 0;
        t.ints += r.interception;
      }
    }
    if (r.rusher == me && (r.kind == PlayKind::Run || r.kind == PlayKind::Scramble)) {
      t.carries++;
      t.rushYds += r.yards;
      t.btk += r.brokenTackles;
      t.fum += r.fumbleLost && r.fumbleBy == me;
    }
    if (r.target == me && r.kind == PlayKind::Pass && !r.thrownAway) {
      t.targets++;
      if (r.complete && r.receiver == me) {
        t.rec++;
        t.recYds += r.yards;
        t.yac += r.yac;
        t.btk += r.brokenTackles;
        t.fum += r.fumbleLost && r.fumbleBy == me;
      }
    }
  } else {
    for (const Player* p : r.tacklers) t.tkl += p == me;
    t.tfl += r.yards < 0 && r.kind == PlayKind::Run && !r.tacklers.empty() && r.tacklers.front() == me;
    t.sacks += r.kind == PlayKind::Sack && (r.sackedBy == me || (!r.tacklers.empty() && r.tacklers.front() == me));
    t.intBy += r.intBy == me;
    t.pbu += r.pbuBy == me;
    t.ff += r.forcedBy == me && r.fumbleLost;
    for (const PlayEvent& e : sim.events)
      if ((e.type == "broken" || e.type == "divemiss") && e.b && e.b->p == me) t.missed++;
  }
}

void row(const char* what, double base, double with) {
  std::printf("    %-30s %8.2f -> %8.2f  (%+.2f)\n", what, base, with, with - base);
}

void report(const Tally& b, const Tally& w) {
  if (b.att > 40) {
    row("comp %", rate(b.comp, b.att, 100), rate(w.comp, w.att, 100));
    row("yards / attempt", rate(b.passYds, b.att), rate(w.passYds, w.att));
    row("int %", rate(b.ints, b.att, 100), rate(w.ints, w.att, 100));
  }
  if (b.dropbacks > 40 && (b.att > 40 || b.targets < 20)) {
    row("sacked / dropback %", rate(b.sacked, b.dropbacks, 100), rate(w.sacked, w.dropbacks, 100));
    row("pressured / dropback %", rate(b.pressured, b.dropbacks, 100), rate(w.pressured, w.dropbacks, 100));
  }
  if (b.carries > 40) {
    row("yards / carry", rate(b.rushYds, b.carries), rate(w.rushYds, w.carries));
  }
  if (b.carries + b.rec > 40) {
    row("broken tackles / 100 touches", rate(b.btk, b.carries + b.rec, 100), rate(w.btk, w.carries + w.rec, 100));
    row("fumbles lost / 100 touches", rate(b.fum, b.carries + b.rec, 100), rate(w.fum, w.carries + w.rec, 100));
  }
  if (b.targets > 40) {
    row("catch % of targets", rate(b.rec, b.targets, 100), rate(w.rec, w.targets, 100));
    row("yards / catch", rate(b.recYds, b.rec), rate(w.recYds, w.rec));
    row("yac / catch", rate(b.yac, b.rec), rate(w.yac, w.rec));
  }
  if (b.tkl + b.pbu + b.sacks > 20) {
    row("tackles / 100 plays", rate(b.tkl, b.plays, 100), rate(w.tkl, w.plays, 100));
    row("missed tackles / 100 attempts", rate(b.missed, b.missed + b.tkl, 100), rate(w.missed, w.missed + w.tkl, 100));
    row("sacks / 100 dropbacks", rate(b.sacks, b.dropbacks, 100), rate(w.sacks, w.dropbacks, 100));
    row("tfl / 100 runs", rate(b.tfl, b.runs, 100), rate(w.tfl, w.runs, 100));
    row("ints / 100 passes", rate(b.intBy, b.oppPass, 100), rate(w.intBy, w.oppPass, 100));
    row("pbu / 100 passes", rate(b.pbu, b.oppPass, 100), rate(w.pbu, w.oppPass, 100));
    row("forced fumbles lost / 100 plays", rate(b.ff, b.plays, 100), rate(w.ff, w.plays, 100));
  }
  std::printf("    -- the play with him on the field\n");
  row("yards / play", rate(b.yards, b.plays), rate(w.yards, w.plays));
  row("yards / run", rate(b.runYds, b.runs), rate(w.runYds, w.runs));
  row("comp % (all passes)", rate(b.oppComp, b.oppPass, 100), rate(w.oppComp, w.oppPass, 100));
}

/** The same plays with abilities off (on = false) and on: a carrier on the field, his team on the side his ability is for. */
Tally run(const AbilityDef& a, int plays, uint32_t seed, bool on, int& carriersOut) {
  League L = createLeague(2027);
  std::vector<std::pair<int, Player*>> carriers;  // team index, man
  for (size_t ti = 0; ti < L.size(); ti++)
    for (auto& p : L[ti]->roster) {
      p->abilities.clear();
      p->xFactor = AbilityId::None;
      const StarGrade was = p->grade;
      p->grade = StarGrade::XFactor;  // (the grade waived)
      if (equipAbility(*p, a.id)) carriers.emplace_back(static_cast<int>(ti), p.get());
      p->grade = was;
    }
  carriersOut = static_cast<int>(carriers.size());
  Tally t;
  if (carriers.empty()) return t;
  for (int i = 0; i < plays; i++) {
    const auto& c = carriers[static_cast<size_t>(i) % carriers.size()];
    Player* me = c.second;
    const int opp = (c.first + 1 + i % 31) % 32;
    GameOptions o;
    o.seed = seed * 100003u + static_cast<uint32_t>(i);
    o.abilities = on;
    o.quarterLen = 900;
    Game g(*L[c.first], *L[opp], o);
    const bool off = offensePos(me->pos);
    g.poss = off ? TeamKey::Home : TeamKey::Away;
    g.phase = GamePhase::Scrimmage;
    // a spread of situations (the rng is the game's own: the same for both runs)
    g.ballOn = 15 + (i * 37) % 70;
    g.down = 1 + (i % 3);
    g.toGo = g.down == 1 ? 10 : static_cast<double>(1 + (i * 7) % 12);
    g.q = 1 + (i % 4);
    g.clock = 60 + (i * 53) % 840;
    if (on && a.tier == AbilityTier::XFactor) g.abil.zones[me->id].active = true;
    if (me->pos == Pos::K || me->pos == Pos::P) continue;
    const OffPlay* op = g.cpuOffense();
    const DefCall* dc = g.cpuDefense(*op);
    auto sim = g.makePlay(*op, *dc);
    bool here = false;
    for (const Ent* e : sim->ents) here = here || e->p == me;
    if (!here) continue;
    const PlayResult& r = sim->runToEnd();
    tally(t, *sim, r, me);
  }
  return t;
}

/** A kicker's abilities, read off the game's own field goal odds. */
void kickReport(const AbilityDef& a) {
  League L = createLeague(2027);
  int n = 0;
  double base[4] = {0, 0, 0, 0}, with[4] = {0, 0, 0, 0};
  const double dists[4] = {35, 45, 52, 57};
  for (auto& tm : L)
    for (auto& p : tm->roster) {
      if (p->pos != Pos::K) continue;
      Player k = *p;
      k.abilities.clear();
      k.xFactor = AbilityId::None;
      k.grade = StarGrade::XFactor;
      if (!equipAbility(k, a.id)) continue;
      n++;
      Player k0 = k;
      k0.abilities.clear();
      k0.xFactor = AbilityId::None;
      for (int d = 0; d < 4; d++) {
        GameOptions o;
        o.seed = 3;
        Game g(*tm, L[0].get() == tm.get() ? *L[1] : *L[0], o);
        g.q = 4;
        g.clock = 120;  // (the clutch: Ice Water)
        g.abil.zones[k.id].active = true;  // (the zone: Automatic)
        base[d] += g.fgProb(dists[d], k0, TeamKey::Home);
        with[d] += g.fgProb(dists[d], k, TeamKey::Home);
      }
    }
  std::printf("== %s (kicker, %d carriers; field goal odds in a close 4th quarter, in the zone)\n", a.name, n);
  for (int d = 0; d < 4 && n; d++) {
    char lbl[32];
    std::snprintf(lbl, sizeof lbl, "fg %% from %.0f", dists[d]);
    row(lbl, base[d] / n * 100, with[d] / n * 100);
  }
}

// ---- league mode: whole games, the shipped league against no abilities
struct GameAgg {
  double games = 0, points = 0, plays = 0, yards = 0, att = 0, comp = 0, rushAtt = 0, rushYds = 0, sacks = 0, ints = 0, fum = 0;
};
/** group: "" = every man keeps the coach's picks; else only these positions do ("QB", "RB", "WR", "TE", "OL", "DL",
 *  "LB", "DB", "K") */
bool inGroup(Pos p, const std::string& g) {
  if (g.empty()) return true;
  if (g == "QB") return p == Pos::QB;
  if (g == "RB") return p == Pos::RB || p == Pos::FB;
  if (g == "WR") return p == Pos::WR;
  if (g == "TE") return p == Pos::TE;
  if (g == "OL") return p == Pos::OT || p == Pos::OG || p == Pos::C;
  if (g == "DL") return p == Pos::DE || p == Pos::DT;
  if (g == "LB") return p == Pos::LB;
  if (g == "DB") return p == Pos::CB || p == Pos::S;
  if (g == "K") return p == Pos::K || p == Pos::P;
  return false;
}
GameAgg games(int n, uint32_t seed, bool on, const std::string& group = std::string()) {
  GameAgg a;
  for (int i = 0; i < n; i++) {
    League L = createLeague(2027);
    for (auto& tm : L)
      for (auto& p : tm->roster)
        if (!inGroup(p->pos, group)) {
          p->abilities.clear();
          p->xFactor = AbilityId::None;
        }
    const int h = static_cast<int>((seed + i * 7) % 32), w0 = static_cast<int>((seed + i * 13 + 5) % 32);
    const int w = w0 == h ? (w0 + 1) % 32 : w0;
    GameOptions o;
    o.seed = seed * 1000 + i;
    o.abilities = on;
    Game g(*L[h], *L[w], o);
    g.simToEnd();
    a.games++;
    for (TeamKey k : {TeamKey::Home, TeamKey::Away}) {
      const TeamLine& t = g.team[k];
      a.points += g.score[k];
      a.plays += t.plays;
      a.yards += t.yards;
      a.att += t.passAtt;
      a.comp += t.passComp;
      a.rushAtt += t.rushAtt;
      a.rushYds += t.rushYds;
      a.sacks += t.sacks;
      a.ints += t.ints;
      a.fum += t.fumblesLost;
    }
  }
  return a;
}

}  // namespace

int main(int argc, char** argv) {
  int plays = 3000;
  uint32_t seed = 11;
  std::vector<std::string> keys;
  int positional = 0;
  for (int i = 1; i < argc; i++) {
    const bool num = argv[i][0] >= '0' && argv[i][0] <= '9';
    if (num && positional == 0) plays = std::atoi(argv[i]), positional++;
    else if (num && positional == 1) seed = static_cast<uint32_t>(std::strtoul(argv[i], nullptr, 10)), positional++;
    else keys.push_back(argv[i]);
  }
  if (keys.empty()) keys.push_back("league");
  if (keys.size() == 1 && keys[0] == "all") {
    keys.clear();
    for (const AbilityDef& d : abilityDefs())
      if (d.id != AbilityId::None) keys.push_back(d.key);
  }
  for (const std::string& k : keys) {
    if (k == "league" || k.rfind("league:", 0) == 0) {
      const std::string group = k.size() > 7 ? k.substr(7) : std::string();
      const int n = (std::max)(10, plays / 60);
      const GameAgg b = games(n, seed, false), w = games(n, seed, true, group);
      if (!group.empty()) std::printf("(only the %s keep their abilities)\n", group.c_str());
      const double tb = 2 * b.games, tw = 2 * w.games;
      std::printf("== league: the shipped loadouts vs no abilities (%d games, seed %u; per team game)\n", n, seed);
      row("points", b.points / tb, w.points / tw);
      row("yards / play", rate(b.yards, b.plays), rate(w.yards, w.plays));
      row("comp %", rate(b.comp, b.att, 100), rate(w.comp, w.att, 100));
      row("yards / carry", rate(b.rushYds, b.rushAtt), rate(w.rushYds, w.rushAtt));
      row("sacks", b.sacks / tb, w.sacks / tw);
      row("ints", b.ints / tb, w.ints / tw);
      row("fumbles lost", b.fum / tb, w.fum / tw);
      continue;
    }
    const AbilityDef* a = abilityByKey(k);
    if (!a) {
      std::fprintf(stderr, "unknown ability %s\n", k.c_str());
      continue;
    }
    if (a->posMask == (1u << static_cast<unsigned>(Pos::K)) || a->posMask == (1u << static_cast<unsigned>(Pos::P)) ||
        a->posMask == ((1u << static_cast<unsigned>(Pos::K)) | (1u << static_cast<unsigned>(Pos::P)))) {
      kickReport(*a);
      continue;
    }
    int n0 = 0, n1 = 0;
    const Tally b = run(*a, plays, seed, false, n0);
    const Tally w = run(*a, plays, seed, true, n1);
    std::printf("== %s (%s, %d carriers, %.0f of %d plays with him on)\n", a->name, a->tier == AbilityTier::XFactor ? "X-Factor, held in the zone" : "Superstar",
                n1, w.plays, plays);
    report(b, w);
  }
  return 0;
}
