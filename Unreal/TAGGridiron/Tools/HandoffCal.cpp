// The run exchange, measured: every handoff play in the library run a few times, and at the moment the ball changes
// hands where the two men are and what they are doing - how far apart, the back's speed (and how much of his top speed),
// where he is around the quarterback, which way the quarterback faces, whether the back had to wait for the ball, and
// what the quarterback does in the second after it.
//   build\HandoffCal.exe [reps=4] [--plays]     (--plays: a row per play instead of the summary by kind)
// A real exchange: 0.6-1.2 s after the snap by depth, the back within about a yard of the quarterback (the ball is put in
// his belly at arm's length: 0.6-1.0 m between their hips in the pack's captures) and running (he does not stop for it),
// the quarterback turned to him, and the quarterback moving on afterwards (a boot or a drop fake), not standing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Game.h"
#include "League.h"
#include "PlaySim.h"
#include "Playbook.h"

using namespace tag;

namespace {
struct Row {
  int n = 0, none = 0, waited = 0, behind = 0, pitch = 0;
  double t = 0, late = 0, d = 0, dMax = 0, sp = 0, frac = 0, qsp = 0, qAfter = 0, faceOff = 0, wait = 0, cross = 0, ballJump = 0, vjump = 0;
  int crossN = 0;
  int plays = 0, ten = 0, stuffed = 0, fum = 0;
  double yds = 0;
};
const char* styleName(RunStyle s) {
  switch (s) {
    case RunStyle::Zone: return "zone";
    case RunStyle::Gap: return "gap";
    case RunStyle::Stretch: return "stretch";
    case RunStyle::Draw: return "draw";
    case RunStyle::Sneak: return "sneak";
  }
  return "?";
}
double wrapPi(double a) { return std::atan2(std::sin(a), std::cos(a)); }
}  // namespace

int main(int argc, char** argv) {
  int reps = 4;
  bool perPlay = false;
  bool quiet = false;  // (many reps: the plays with no exchange are counted, not listed)
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--plays") == 0) perPlay = true;
    else if (std::strcmp(argv[i], "--trace") == 0) i++;
    else if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc) {  // --set name=value: a tag::Tune constant (mesh=0: the old exchange)
      const std::string kv = argv[++i];
      const size_t eq = kv.find('=');
      if (eq == std::string::npos || !setTune(kv.substr(0, eq), std::atof(kv.c_str() + eq + 1))) return std::fprintf(stderr, "unknown tune %s\n", kv.c_str()), 2;
    } else reps = (std::max)(1, std::atoi(argv[i]));
  }
  quiet = reps > 8;
  League league = createLeague(2027);
  // --trace <play id>: the two men every 0.1 s (yards behind the line, yards left of the ball), until the back is past it
  for (int i = 1; i + 1 < argc; i++)
    if (std::strcmp(argv[i], "--trace") == 0) {
      const OffPlay* tp = playById(argv[i + 1]);
      if (!tp || !tp->run) return std::printf("no run play %s\n", argv[i + 1]), 2;
      GameOptions go;
      go.seed = 4000;
      Game g(*league[0], *league[1], go);
      g.phase = GamePhase::Scrimmage;
      g.ballOn = 35;
      auto sim = g.makePlay(*tp, *defById("base_c3"));
      Ent* rb = sim->offRole(tp->run->carrier);
      Ent* qb = sim->qb;
      std::printf("%s (%s, handoff at %.2f s, aim %+.1f)\n", tp->id.c_str(), styleName(tp->run->style), tp->run->handoffDelay, sim->runAimDy());
      int k = 0;
      while (sim->isLive() && sim->t < 3 && rb->pos.x < sim->los + 1) {
        if (k++ % 6 == 0)
          std::printf("t %.2f  qb (%5.2f,%5.2f) v %.1f face %4.0f   back (%5.2f,%5.2f) v %.1f   apart %.2f  %s\n", sim->t, qb->pos.x - sim->los, qb->pos.y - sim->by, qb->speed(),
                      qb->face * 180 / kPi, rb->pos.x - sim->los, rb->pos.y - sim->by, rb->speed(), dist(qb->pos, rb->pos),
                      sim->ball.holder == rb ? "BACK" : sim->ball.holder == qb ? "qb" : "-");
        sim->step(SIM_DT);
      }
      return 0;
    }
  std::map<std::string, Row> rows;
  std::vector<std::string> order;
  int serial = 0, etaN = 0, etaOut = 0;
  double etaAbs = 0, etaMax = 0, etaSum = 0;
  for (const OffPlay& play : OFF_PLAYS()) {
    if (play.type != PlayType::Run || !play.run || play.run->carrier == "QB") continue;
    const Formation* fm = formationById(play.formation);
    if (!fm) continue;
    const char* align = fm->qbDepth <= 1.5 ? "under" : fm->qbDepth < 4.5 ? "pistol" : "gun";
    char key[160];
    if (perPlay) std::snprintf(key, sizeof key, "%-34s %-7s %-6s", play.id.c_str(), styleName(play.run->style), align);
    else std::snprintf(key, sizeof key, "%-7s %-6s %-3s", styleName(play.run->style), align, play.run->carrier.c_str());
    if (!rows.count(key)) order.push_back(key);
    Row& R = rows[key];
    for (int r = 0; r < reps; r++, serial++) {
      GameOptions go;
      go.seed = 4000 + static_cast<uint32_t>(serial);
      Team& offT = *league[static_cast<size_t>(serial * 2) % 32];
      Team& defT = *league[static_cast<size_t>(serial * 2 + 1) % 32];
      Game g(offT, defT, go);
      g.phase = GamePhase::Scrimmage;
      g.ballOn = 35;
      auto sim = g.makePlay(play, *defById(serial % 3 ? "base_c3" : "nk_c3"));
      Ent* rb = sim->offRole(play.run->carrier);
      Ent* qb = sim->qb;
      if (!rb || !qb) continue;
      bool done = false, crossed = false;
      double tHand = -1, waitT = 0, qbX0 = 0, qbY0 = 0, eta04 = -1;
      V2 ballPrev{0, 0};
      V2 velPrev{0, 0};
      while (sim->isLive() && sim->t < 6) {
        const bool heldByQb = sim->ball.holder == qb;
        const V2 before = heldByQb ? qb->pos : V2{0, 0};
        ballPrev = before;
        velPrev = rb->vel;
        // (waiting: at the quarterback's side with the ball still in his hands, going nowhere)
        if (!done && heldByQb && sim->t > 0.2 && dist(rb->pos, qb->pos) < 1.6 && rb->speed() < 1.5) waitT += SIM_DT;
        // (the sim's own forecast of the exchange, PlaySim::meshIn, as it stood 0.4 s out: what the presentation times
        // the two-man capture by)
        if (!done && eta04 < 0 && sim->meshOn() && sim->meshIn() >= 0 && sim->meshIn() <= 0.4) eta04 = sim->t + sim->meshIn();
        sim->step(SIM_DT);
        if (!done && sim->ball.holder == rb) {
          done = true;
          tHand = sim->t;
          const double dd = dist(rb->pos, qb->pos);
          R.n++;
          R.t += sim->t;
          R.late += sim->t - play.run->handoffDelay;
          R.d += dd;
          R.dMax = (std::max)(R.dMax, dd);
          R.sp += rb->speed();
          R.frac += rb->speed() / (std::max)(1.0, rb->vmax);
          R.qsp += qb->speed();
          R.wait += waitT;
          R.waited += waitT > 0.1;
          R.pitch += dd > 2.5;
          // the quarterback's facing against the bearing to his back (0 = square to him, 180 = his back to him)
          R.faceOff += std::fabs(wrapPi(std::atan2(rb->pos.y - qb->pos.y, rb->pos.x - qb->pos.x) - qb->face)) * 180 / kPi;
          // the back behind the quarterback (further from the line than he is, by more than half a yard) at the exchange
          R.behind += rb->pos.x < qb->pos.x - 0.5;
          // how far the held ball moved in the tick it changed hands (body centre to body centre), and the back's change of
          // velocity in that tick
          if (eta04 >= 0) etaN++, etaAbs += std::fabs(sim->t - eta04), etaSum += sim->t - eta04, etaMax = (std::max)(etaMax, std::fabs(sim->t - eta04)), etaOut += std::fabs(sim->t - eta04) > 0.1;
          R.ballJump += dist(ballPrev, rb->pos);
          R.vjump += len(sub(rb->vel, velPrev));
          qbX0 = qb->pos.x, qbY0 = qb->pos.y;
        }
        if (done && !crossed && rb->pos.x >= sim->los) {
          crossed = true;
          R.cross += sim->t;
          R.crossN++;
        }
        if (done && tHand >= 0 && sim->t >= tHand + 1.0) {
          R.qAfter += dist(qb->pos, V2{qbX0, qbY0});
          tHand = -1;
        }
      }
      // (the run itself, played out: what the exchange is worth in yards)
      {
        const PlayResult& res = sim->result ? *sim->result : sim->runToEnd();
        R.plays++;
        R.yds += res.yards;
        R.ten += res.yards >= 10;
        R.stuffed += res.yards <= 0;
        R.fum += res.fumbleBy != nullptr;
      }
      if (!done) R.none++;
      if (!done && !quiet) {
        // (no exchange: what became of the play)
        std::printf("  no exchange: %s seed %u, over at %.2f s, %.2f yd apart, ball %s, back %s\n", play.id.c_str(), go.seed, sim->t, dist(rb->pos, qb->pos),
                    sim->ball.holder == qb ? "with the quarterback" : sim->ball.holder ? sim->ball.holder->role.c_str() : "loose", rb->state == EntState::Down ? "down" : "up");
      }
    }
  }
  std::printf("%-*s %4s %5s %6s %6s %6s %6s %5s %6s %6s %6s %7s %6s %6s %6s %5s %6s %5s %5s %5s\n", perPlay ? 49 : 18, perPlay ? "play" : "kind", "n", "none", "t", "late", "dist",
              "dmax", "v", "v/vmx", "qb v", "qb off", "wait s", "waitd", "behind", "qb+1s", "LOS t", "yds", "10+%", "<=0%", "fum%");
  Row all;
  for (const std::string& k : order) {
    const Row& R = rows[k];
    const double n = (std::max)(1, R.n), np = (std::max)(1, R.plays);
    std::printf("%s %4d %5d %6.2f %6.2f %6.2f %6.2f %5.1f %6.2f %6.1f %6.0f %7.2f %6d %6d %6.1f %5.2f %6.2f %5.1f %5.1f %5.2f\n", k.c_str(), R.n, R.none, R.t / n, R.late / n, R.d / n,
                R.dMax, R.sp / n, R.frac / n, R.qsp / n, R.faceOff / n, R.wait / n, R.waited, R.behind, R.qAfter / n, R.crossN ? R.cross / R.crossN : 0.0, R.yds / np,
                100.0 * R.ten / np, 100.0 * R.stuffed / np, 100.0 * R.fum / np);
    all.plays += R.plays, all.yds += R.yds, all.ten += R.ten, all.stuffed += R.stuffed, all.fum += R.fum, all.none += R.none;
  }
  std::printf("all: %d runs, %.2f yd a carry, 10+ %.1f%%, no gain or a loss %.1f%%, fumbles %.2f%%, no exchange %d\n", all.plays, all.yds / (std::max)(1, all.plays),
              100.0 * all.ten / (std::max)(1, all.plays), 100.0 * all.stuffed / (std::max)(1, all.plays), 100.0 * all.fum / (std::max)(1, all.plays), all.none);
  if (etaN)
    std::printf("the sim's forecast of the exchange 0.4 s out: off by %.3f s on average (%+.3f s mean), more than 0.1 s on %.1f%%, %.2f s at most (%d exchanges)\n", etaAbs / etaN,
                etaSum / etaN, 100.0 * etaOut / etaN, etaMax, etaN);
  std::printf("\n(t: seconds after the snap; late: after the play's own handoff time; dist: yards between them, body centres;\n"
              " v: the back's speed, yd/s; qb off: degrees from the quarterback's facing to his back; wait: seconds the back stood\n"
              " at his side before the ball; behind: exchanges with the back deeper than the quarterback; qb+1s: yards the\n"
              " quarterback covers in the second after it; LOS t: when the back reaches the line)\n");
  return 0;
}
