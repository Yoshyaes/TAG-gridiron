// What the highlight stick is worth: a user ball carrier on inside zone / power (and a user defender against them),
// run N times per mode with the same seeds.
//   build\MoveCal.exe [reps=400]
// Carrier modes: none (no moves), highlight (the suggested move when the tackler is within 1.6 yd), truck / spin /
// stiffarm / hurdle (always that move at that moment), and a defender with the hit stick / cut at 2 yd.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "Game.h"
#include "League.h"
#include "PlaySim.h"
#include "Playbook.h"

using namespace tag;

namespace {
struct Out {
  int n = 0, broken = 0, fum = 0, moves = 0, won = 0, trucked = 0, bighit = 0;
  double yds = 0;
};

Out run(League& league, int reps, int mode, bool power) {
  // mode: 0 none, 1 highlight, 2 truck, 3 spin, 4 stiff arm, 5 hurdle, 6 user defender hit stick, 7 user defender cut
  Out o;
  const OffPlay* plays[2] = {playById("dbl_iz"), playById("i_power")};
  for (int r = 0; r < reps; r++) {
    GameOptions go;
    go.seed = 1000 + r;
    Team& offT = *league[(r * 2) % 32];
    Team& defT = *league[(r * 2 + 1) % 32];
    Game g(offT, defT, go);
    g.phase = GamePhase::Scrimmage;
    g.ballOn = 30;
    const bool userDef = mode >= 6;  // (8: the user defender, no move)
    auto sim = g.makePlay(*plays[r % 2], *defById(r % 3 ? "base_c3" : "nk_c3"), userDef ? Side::Def : Side::Off);
    // a power back (break tackle and strength over elusiveness) or an elusive one, as asked
    Ent* rb = sim->offRole("RB");
    if (rb && power) {
      rb->p->r.breakTackle = (std::max)(rb->p->r.breakTackle, 88.0);
      rb->p->r.strength = (std::max)(rb->p->r.strength, 80.0);
      rb->p->r.elusiveness = (std::min)(rb->p->r.elusiveness, 70.0);
    }
    const Ratings saved = rb ? rb->p->r : Ratings();
    while (sim->isLive() && sim->t < 15) {
      Ent* c = sim->ball.holder;
      if (!userDef && c && c != sim->qb && c->side == Side::Off && sim->user != c) {
        if (sim->user) sim->user->userControlled = false;
        sim->user = c;
        c->userControlled = true;
      }
      if (userDef && sim->user && c && c->side == Side::Off) {
        // the user defender: the nearest free man to the carrier, the move at 2 yd
        Ent* best = nullptr;
        double bd = 1e9;
        for (Ent* d : sim->def)
          if (d->free() && dist(d->pos, c->pos) < bd) {
            bd = dist(d->pos, c->pos);
            best = d;
          }
        if (best && best != sim->user) {
          sim->user->userControlled = false;
          sim->user = best;
          best->userControlled = true;
        }
        if (mode != 8 && best && bd < 2.0 && best->moveT > 1.0) sim->input.stickMove = mode == 6 ? Move::Hit : Move::Cut;
        if (best) sim->input.move = norm(sub(c->pos, best->pos));
      }
      if (!userDef && c && c->userControlled && mode > 0) {
        const Ent* th = nullptr;
        const Move m = sim->suggestMove(c, &th);
        if (th && dist(th->pos, c->pos) < 1.6 && c->animT > 0.6) {
          const Move want = mode == 1 ? m : mode == 2 ? Move::Truck : mode == 3 ? Move::Spin : mode == 4 ? Move::StiffArm : Move::Hurdle;
          sim->input.stickMove = want;
        }
      }
      if (!userDef) sim->input.move = V2{0, 0};  // the AI picks his path (no stick), the user only times the moves
      sim->step(SIM_DT);
      sim->input.stickMove = Move::None;
    }
    if (rb) rb->p->r = saved;
    const PlayResult& res = sim->result ? *sim->result : sim->runToEnd();
    o.n++;
    o.yds += res.yards;
    o.broken += res.brokenTackles;
    if (res.fumbleBy) o.fum++;
    for (const PlayEvent& e : sim->events) {
      if (e.type == "move" && e.a && e.a->userControlled) o.moves++;
      if ((e.type == "broken" || e.type == "divemiss") && !e.note.empty() && e.a && e.a->userControlled) o.won++;
      if (e.type == "trucked") o.trucked++;
      if (e.type == "bighit") o.bighit++;
    }
  }
  return o;
}
}  // namespace

int main(int argc, char** argv) {
  const int reps = argc > 1 ? std::atoi(argv[1]) : 400;
  League league = createLeague(2027);
  const char* names[] = {"no moves", "highlight", "always truck", "always spin", "always stiff arm", "always hurdle", "user D hit stick", "user D cut", "user D no move"};
  for (int power = 0; power < 2; power++) {
    std::printf("\n%s back (user carrier on inside zone / power, %d carries per mode)\n", power ? "power" : "league", reps);
    std::printf("%-18s %7s %8s %8s %7s %8s %8s %7s\n", "mode", "yds", "broken", "moves", "won", "trucked", "fumbles", "bighit");
    for (int mode = 0; mode < 9; mode++) {
      if (power && mode >= 6) continue;
      const Out o = run(league, reps, mode, power == 1);
      std::printf("%-18s %7.2f %8.3f %8.2f %7.3f %8.3f %8.3f %7.3f\n", names[mode], o.yds / o.n, static_cast<double>(o.broken) / o.n,
                  static_cast<double>(o.moves) / o.n, static_cast<double>(o.won) / o.n, static_cast<double>(o.trucked) / o.n,
                  static_cast<double>(o.fum) / o.n, static_cast<double>(o.bighit) / o.n);
    }
  }
  return 0;
}
