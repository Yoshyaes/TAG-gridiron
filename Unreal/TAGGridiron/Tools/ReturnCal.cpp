// Live kick returns, measured: kickoffs and punts played through the return sim (the game's own units and rules).
//   build\ReturnCal.exe [games=300]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Game.h"
#include "League.h"
#include "PlaySim.h"
#include "Playbook.h"

using namespace tag;

namespace {
std::vector<Player*> pick11(Team& t, const Game& g, const std::string& role) {
  std::vector<Player*> healthy;
  for (auto& p : t.roster)
    if (p->injuredWeeks <= 0) healthy.push_back(p.get());
  auto by = [&](std::initializer_list<Pos> ps) {
    std::vector<Player*> out;
    for (Player* p : healthy)
      for (Pos q : ps)
        if (p->pos == q) {
          out.push_back(p);
          break;
        }
    std::stable_sort(out.begin(), out.end(), [](Player* a, Player* b) { return a->r.speed > b->r.speed; });
    return out;
  };
  std::vector<Player*> out;
  if (role == "K" || role == "P") {
    auto l = by({role == "K" ? Pos::K : Pos::P});
    if (!l.empty()) out.push_back(l[0]);
    if (role == "P" || g.phase != GamePhase::Kickoff)
      for (Player* p : by({Pos::OT, Pos::OG, Pos::C, Pos::TE}))
        if (out.size() < 9) out.push_back(p);
  }
  if (role == "KR") {
    auto l = by({Pos::WR, Pos::CB, Pos::RB});
    if (!l.empty()) out.push_back(l[0]);
  }
  auto rest = role == "D" ? by({Pos::DE, Pos::DT, Pos::LB}) : by({Pos::LB, Pos::S, Pos::CB, Pos::TE, Pos::WR, Pos::RB, Pos::FB, Pos::DE});
  for (Player* p : rest)
    if (out.size() < 11 && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
  return out;
}

struct Acc {
  std::vector<double> yds;
  int n = 0, td = 0, fum = 0, kicks = 0;
  void report(const char* nm) {
    std::sort(yds.begin(), yds.end());
    double s = 0, big = 0;
    for (double y : yds) { s += y; big += y >= 40; }
    auto pc = [&](double q) { return yds.empty() ? 0.0 : yds[static_cast<size_t>(q * (yds.size() - 1))]; };
    std::printf("%s: %d kicks, %d returned (%.0f%%)  avg %.1f yd  median %.1f  p90 %.1f  TD %.1f%%  40+ %.1f%%  fumbles lost %.1f%%\n", nm, kicks, n,
                100.0 * n / (std::max)(1, kicks), s / (std::max)(1, n), pc(0.5), pc(0.9), 100.0 * td / (std::max)(1, n), 100.0 * big / (std::max)(1, n),
                100.0 * fum / (std::max)(1, n));
  }
};

void play(Game& g, std::vector<Player*> kickU, std::vector<Player*> recvU, Acc& a) {
  a.kicks++;
  if (!g.pendingReturn) return;
  auto sim = g.kickReturnSim(recvU, kickU);
  int n = 0;
  while (sim->isLive() && n++ < 60 * 40) sim->step(SIM_DT);
  if (!sim->result) return;
  const PlayResult& r = *sim->result;
  a.n++;
  a.yds.push_back((std::max)(0.0, r.endX - sim->catchX));
  a.td += r.td && *r.td == Side::Off;
  a.fum += r.turnover;
  g.finishKickReturn(*sim);
}
}  // namespace

int main(int argc, char** argv) {
  const int games = argc > 1 ? std::atoi(argv[1]) : 300;
  League league = createLeague(2027);
  Acc ko, pt;
  for (int i = 0; i < games; i++) {
    GameOptions go;
    go.seed = 5000 + i;
    Game g(*league[(i * 2) % 32], *league[(i * 2 + 1) % 32], go);
    for (int k = 0; k < 4; k++) {
      // a kickoff
      g.phase = GamePhase::Kickoff;
      g.kicking = k % 2 ? TeamKey::Home : TeamKey::Away;
      {
        auto kickU = pick11(g.teamOf(g.kicking), g, "K");
        auto recvU = pick11(g.teamOf(other(g.kicking)), g, "KR");
        g.kickoff(false, true);
        play(g, kickU, recvU, ko);
      }
      // a punt from between a team's own 20 and midfield
      g.pendingReturn.reset();
      g.phase = GamePhase::Scrimmage;
      g.down = 4;
      g.toGo = 8;
      g.ballOn = 20 + (i * 7 + k * 11) % 31;
      {
        auto kickU = pick11(g.offense(), g, "P");
        auto recvU = pick11(g.defense(), g, "KR");
        g.punt(true);
        play(g, kickU, recvU, pt);
      }
      g.pendingReturn.reset();
      if (g.phase == GamePhase::Final) break;
    }
  }
  ko.report("KICKOFF");
  pt.report("PUNT");
  return 0;
}
