// Port of tools/test.ts (+ a few C++-side API checks).
// Invariant / regression tests for the simulation. Exit code 1 on any failure.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include "Game.h"
#include "League.h"
#include "PlaySim.h"
#include "Playbook.h"
#include "Rng.h"
#include "Roster.h"

using namespace tag;

namespace {

int passCount = 0;
int failCount = 0;

void t(const char* name, const std::function<void()>& fn) {
  // TAG_TEST=<text>: run only the tests whose name holds it
  if (const char* only = std::getenv("TAG_TEST"); only && !std::strstr(name, only)) return;
  try {
    fn();
    passCount++;
    std::printf("  ok   %s\n", name);
  } catch (const std::exception& e) {
    failCount++;
    std::printf("  FAIL %s: %s\n", name, e.what());
  }
}

void check(bool c, const std::string& msg) {
  if (!c) throw std::runtime_error(msg);
}

std::string fx(double v, int prec = 2) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", prec, v);
  return buf;
}

}  // namespace

int main() {
  League league = createLeague(2027);

  t("league: 32 fictional teams with full rosters", [&] {
    check(league.size() == 32, "team count");
    for (const auto& tm : league) {
      check(tm->roster.size() >= 50, tm->abbr + " roster " + std::to_string(tm->roster.size()));
      for (Pos pos : {Pos::QB, Pos::RB, Pos::WR, Pos::TE, Pos::OT, Pos::OG, Pos::C, Pos::DE, Pos::DT, Pos::LB, Pos::CB, Pos::S, Pos::K, Pos::P}) {
        bool has = false;
        for (const auto& p : tm->roster) has = has || p->pos == pos;
        check(has, tm->abbr + " missing " + posName(pos));
      }
      std::set<int> nums;
      for (const auto& p : tm->roster) nums.insert(p->number);
      check(nums.size() == tm->roster.size(), tm->abbr + " duplicate jersey numbers");
    }
  });

  t("determinism: same seed => identical game", [&] {
    auto run = [] {
      League L = createLeague(2027);
      GameOptions o;
      o.seed = 4242;
      Game g(*L[3], *L[7], o);
      g.simToEnd();
      return std::to_string(g.score.home) + "-" + std::to_string(g.score.away) + "-" + std::to_string(g.log.size());
    };
    const std::string a = run();
    const std::string b = run();
    check(a == b, a + " vs " + b);
  });

  t("100 full games complete with sane totals", [&] {
    // NOTE: tools/test.ts asserts 35 <= plays <= 95 for every one of these 200 team-games. Exact
    // trajectories differ between V8 and the C++ CRT (last-ulp exp/log/sin/cos/atan2 differences
    // grow chaotically -- with identical math the port is bit-exact, see Tools/Verify), and both
    // engines put ~0.2% of team-games just outside that band (measured minimum ~30). So the band is
    // checked as a rate, with hard bounds per game.
    Rng rng(99);
    int outsideBand = 0;
    for (int i = 0; i < 100; i++) {
      const int h = rng.intRange(0, 31);
      const int a = (h + 1 + rng.intRange(0, 30)) % 32;
      GameOptions o;
      o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
      Game g(*league[h], *league[a], o);
      g.simToEnd();
      check(g.phase == GamePhase::Final, "game did not finish");
      check(g.score.home >= 0 && g.score.away >= 0, "negative score");
      check(g.score.home <= 80 && g.score.away <= 80,
            "absurd score " + std::to_string(g.score.home) + "-" + std::to_string(g.score.away));
      check(g.q >= 4, "ended before 4th quarter");
      for (TeamKey k : {TeamKey::Home, TeamKey::Away}) {
        const TeamLine& T = g.team[k];
        check(T.plays >= 25 && T.plays <= 110, "plays " + std::to_string(T.plays));
        if (T.plays < 35 || T.plays > 95) outsideBand++;
        check(std::isfinite(static_cast<double>(T.yards)), "yards NaN");
      }
      // scores are reachable football scores (no 1-point totals unless safety+... allowed)
      check(g.score.home != 1 && g.score.away != 1, "impossible score of 1");
    }
    check(outsideBand <= 3, std::to_string(outsideBand) + " of 200 team-games outside 35..95 plays");
  });

  t("every offensive play vs every defense runs without NaN and ends", [&] {
    Rng rng(5);
    const std::vector<double> ys = {FIELD_W / 2 - 3.08, FIELD_W / 2, FIELD_W / 2 + 3.08};
    const std::vector<Precip> precs = {Precip::None, Precip::Rain, Precip::Snow};
    // (the expanded books: 3,700 plays x 590 calls is two million snaps - every play against three calls and every call
    // against three plays, rotating, covers each of them and every front and coverage against every kind of play)
    const size_t nO = OFF_PLAYS().size(), nD = DEF_PLAYS().size();
    std::vector<std::pair<size_t, size_t>> pairs;
    for (size_t i = 0; i < nO; i++)
      for (size_t k = 0; k < 3; k++) pairs.emplace_back(i, (i * 7 + k * 131) % nD);
    for (size_t j = 0; j < nD; j++)
      for (size_t k = 0; k < 3; k++) pairs.emplace_back((j * 11 + k * 977) % nO, j);
    for (const auto& pr : pairs) {
      const OffPlay& op = OFF_PLAYS()[pr.first];
      {
        const DefCall& dc = DEF_PLAYS()[pr.second];
        PlayConfig cfg;
        cfg.off = league[1].get();
        cfg.def = league[2].get();
        cfg.offPlay = &op;
        cfg.defCall = &dc;
        cfg.los = rng.range(5, 95);
        cfg.ballY = rng.pick(ys);
        cfg.flip = rng.chance(0.5);
        cfg.weather.windX = rng.range(-5, 5);
        cfg.weather.windY = rng.range(-5, 5);
        cfg.weather.precip = rng.pick(precs);
        cfg.weather.tempF = 40;
        cfg.worldDir = 1;
        cfg.rng = rng.fork();
        PlaySim sim(cfg);
        const PlayResult& r = sim.runToEnd();
        check(sim.phase == SimPhase::Dead, op.id + " vs " + dc.id + " never ended");
        check(std::isfinite(r.endX), op.id + " vs " + dc.id + " endX NaN");
        for (const Ent* e : sim.ents) check(std::isfinite(e->pos.x) && std::isfinite(e->pos.y), e->role + " NaN position");
        check(r.duration < 30, op.id + " lasted " + fx(r.duration) + "s");
      }
    }
  });

  t("football: a dropped ball bounces off its ends, rolls on, and comes to rest on its belly (BallPhysics.h)", [&] {
    Rng rng(11);
    double sumX = 0, sumY = 0, sumSq = 0;
    for (int k = 0; k < 60; ++k) {
      BallBody b;
      b.pos = V3{0, 0, 1.0};
      b.vel = V3{rng.range(1, 4), rng.range(-2, 2), rng.range(1, 3)};
      b.q = qnorm(Quat{rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)});
      b.w = V3{rng.range(-12, 12), rng.range(-12, 12), rng.range(-12, 12)};
      double t = 0, peakAfter = 0;
      bool bounced = false;
      while (!b.rest && t < 8) {
        b.step(1.0 / 60, G);
        t += 1.0 / 60;
        check(std::isfinite(b.pos.x) && std::isfinite(b.pos.z) && std::isfinite(b.w.x), "ball NaN");
        check(b.pos.z + b.lowPoint().z > -0.02, "ball through the turf " + fx(b.pos.z + b.lowPoint().z));
        bounced = bounced || b.bounces > 0;
        if (bounced) peakAfter = (std::max)(peakAfter, b.pos.z);
      }
      check(b.rest, "ball never came to rest (" + fx(t) + " s)");
      check(std::fabs(b.pos.z - BallBody::B) < 1e-6 && std::fabs(b.axis().z) < 1e-3, "ball not lying on its belly");
      check(peakAfter < 1.6, "a bounce went higher than the drop could give: " + fx(peakAfter));
      // where it ends up off its line of flight: a football's bounce sends it off at an angle
      const double ang = std::atan2(b.pos.y, b.pos.x);
      sumX += std::cos(ang), sumY += std::sin(ang), sumSq += 1;
    }
    const double R = std::sqrt(sumX * sumX + sumY * sumY) / sumSq;  // 1 = always the same way
    check(R < 0.85, "the bounces all go one way (resultant " + fx(R) + ")");
  });

  t("fumbles: no more than two men leave their feet at once for a loose ball, and every fumble ends", [&] {
    const double keep = tune().fumbleK;
    setTune("fumbleK", 60);
    Rng rng(23);
    int fumbles = 0, maxDiving = 0, dives = 0, squirts = 0, misses = 0, recs = 0, oob = 0;
    for (int k = 0; k < 120; ++k) {
      PlayConfig cfg;
      cfg.off = league[4].get();
      cfg.def = league[9].get();
      cfg.offPlay = &OFF_PLAYS()[k % OFF_PLAYS().size()];
      cfg.defCall = &DEF_PLAYS()[k % DEF_PLAYS().size()];
      cfg.los = rng.range(20, 80);
      cfg.ballY = FIELD_W / 2;
      cfg.worldDir = 1;
      cfg.weather.tempF = 60;
      cfg.rng = rng.fork();
      PlaySim sim(cfg);
      sim.onStep = [&](const PlaySim& s) {
        int n = 0;
        for (const Ent* e : s.ents) n += e->looseDiveT >= 0;
        maxDiving = (std::max)(maxDiving, n);
      };
      const PlayResult& r = sim.runToEnd();
      check(sim.phase == SimPhase::Dead, "a fumbled play never ended");
      if (r.fumbleBy) ++fumbles;
      for (const PlayEvent& e : sim.events) {
        dives += e.type == "dive";
        squirts += e.type == "squirt";
        misses += e.type == "divemiss";
        recs += e.type == "recover";
        oob += r.fumbleBy && r.outOfBounds && &e == &sim.events.front();
      }
    }
    setTune("fumbleK", keep);
    check(fumbles >= 10, "too few fumbles to judge: " + std::to_string(fumbles));
    check(maxDiving <= 2, std::to_string(maxDiving) + " men in the air at once");
    std::printf("       (fumbles %d: dives %d, knocked on %d, dived and missed %d, recovered %d, out of bounds %d)\n", fumbles, dives, squirts, misses, recs, oob);
    check(dives <= fumbles * 3, std::to_string(dives) + " dives for " + std::to_string(fumbles) + " fumbles");
  });

  t("physics: 99-speed player runs a ~4.3 forty, lineman ~5.2", [&] {
    auto forty = [](double spd, double acc) {
      const double vmax = 8.6 + (spd - 50) * 0.049;
      const double tau = 1.0 - (acc - 50) * 0.0078;
      double x = 0, v = 0, tt = 0;
      while (x < 40) {
        v += (vmax / tau) * (std::max)(0.12, 1 - v / vmax) * 0.001;
        v = (std::min)(v, vmax);
        x += v * 0.001;
        tt += 0.001;
      }
      return tt;
    };
    const double f99 = forty(99, 99);
    const double f55 = forty(55, 62);
    check(f99 > 4.1 && f99 < 4.5, "elite forty " + fx(f99));
    check(f55 > 4.9 && f55 < 5.6, "lineman forty " + fx(f55));
  });

  t("field goal probability decreases with distance and weather", [&] {
    GameOptions o;
    o.seed = 1;
    o.weather = Weather{0, 0, Precip::None, 70};
    Game g(*league[0], *league[1], o);
    const Player& k = *g.kicker(TeamKey::Home);
    double prev = 1;
    for (int d = 20; d <= 65; d += 5) {
      const double p = g.fgProb(d, k, TeamKey::Home);
      check(p <= prev + 1e-9, "fg prob not monotonic at " + std::to_string(d));
      prev = p;
    }
    check(g.fgProb(25, k, TeamKey::Home) > 0.93, "chip shot too hard");
    GameOptions o2;
    o2.seed = 1;
    o2.weather = Weather{0, 0, Precip::Snow, 20};
    Game snow(*league[0], *league[1], o2);
    check(snow.fgProb(45, k, TeamKey::Home) < g.fgProb(45, k, TeamKey::Home), "snow should hurt kicking");
  });

  t("expected points: better field position and down = more value", [&] {
    check(expectedPoints(80, 1, 10) > expectedPoints(20, 1, 10), "field position");
    check(expectedPoints(50, 1, 10) > expectedPoints(50, 3, 10), "down");
    check(expectedPoints(50, 3, 2) > expectedPoints(50, 3, 12), "distance");
  });

  t("kneel-down burns the clock and loses a yard", [&] {
    GameOptions o;
    o.seed = 3;
    Game g(*league[0], *league[1], o);
    g.phase = GamePhase::Scrimmage;
    g.ballOn = 40;
    auto sim = g.makePlay(*playById("kneel"), *defById("base_c3"));
    const PlayResult& r = sim->runToEnd();
    check(r.kind == PlayKind::Kneel && r.yards == -1, std::string("kneel result ") + playKindName(r.kind) + " " + std::to_string(r.yards));
  });

  t("touchdown awards 6 and moves to PAT", [&] {
    GameOptions o;
    o.seed = 9;
    Game g(*league[0], *league[1], o);
    g.phase = GamePhase::Scrimmage;
    g.poss = TeamKey::Home;
    g.ballOn = 99.4;
    g.toGo = 1;
    bool scored = false;
    for (int i = 0; i < 30 && !scored; i++) {
      g.ballOn = 99.4;
      g.down = 1;
      g.phase = GamePhase::Scrimmage;
      g.poss = TeamKey::Home;
      const int before = g.score.home;
      auto sim = g.makePlay(*playById("gl_sneak"), *defById("gl_c1"));
      sim->runToEnd();
      g.finishPlay(*sim);
      if (g.score.home == before + 6) {
        scored = true;
        check(g.phase == GamePhase::Pat, "should be PAT phase");
      }
    }
    check(scored, "no sneak TD from the 1 in 30 tries");
  });

  t("two-minute warning fires in each half", [&] {
    GameOptions o;
    o.seed = 77;
    Game g(*league[4], *league[5], o);
    g.simToEnd();
    int n = 0;
    for (const PlayLog& l : g.log)
      if (l.desc == "Two-minute warning.") n++;
    check(n >= 1, "two-minute warnings: " + std::to_string(n));
  });

  // ---------------- C++-side API checks (no TS counterpart)
  t("cross-language: rng matches the TS mulberry32 stream bit-for-bit", [&] {
    // first outputs of `new Rng(7).next()` printed by node (shortest round-trip decimal)
    const double ref[] = {0.011704753153026104, 0.06195825757458806, 0.97690763277933, 0.6990287057124078, 0.5214452685322613};
    Rng r(7);
    for (double v : ref) check(r.next() == v, "rng stream differs from TS");
    Rng z(0);
    Rng g(0x9e3779b9u);
    check(z.next() == g.next(), "seed 0 must map to 0x9e3779b9");
  });

  t("cross-language: createLeague(2027) matches the TS league", [&] {
    // spot values from the TS createLeague(2027) (identical RNG consumption order; the C++ league draws the franchise
    // talent at half the TS spread since 2026-11-19, so the AUS QB's throw power is 78 where the TS has 77)
    const Player& p0 = *league[0]->roster[0];
    check(league[0]->abbr == "AUS" && p0.id == 1, "team order / player ids");
    check(p0.first == "Jamal" && p0.last == "Sorensen" && p0.pos == Pos::QB && p0.number == 8 && p0.age == 26, "AUS QB1 identity");
    check(p0.heightIn == 77 && p0.weightLb == 221 && p0.ovr == 70, "AUS QB1 measurables");
    check(p0.r.speed == 66 && p0.r.throwPower == 78 && p0.r.decision == 79 && p0.r.kickAcc == 27, "AUS QB1 ratings");
    const Player& wr = *league[0]->roster[7];
    check(wr.first == "Keon" && wr.last == "Whitfield" && wr.r.speed == 99 && wr.ovr == 84, "AUS WR1");
    check(std::fabs(league[0]->passLean - 0.5768404039069508) < 1e-15, "AUS passLean");
  });

  t("api: result.events mirrors sim.events and pointers resolve", [&] {
    GameOptions o;
    o.seed = 11;
    Game g(*league[6], *league[7], o);
    g.phase = GamePhase::Scrimmage;
    g.ballOn = 30;
    for (const OffPlay& op : OFF_PLAYS()) {
      auto sim = g.makePlay(op, *defById("nk_c3"));
      const PlayResult& r = sim->runToEnd();
      check(r.events.size() == sim->events.size(), op.id + " events size mismatch");
      for (const PlayEvent& e : r.events) {
        bool okA = e.a == nullptr;
        for (const Ent* x : sim->ents) okA = okA || x == e.a;
        check(okA, op.id + " dangling event entity");
      }
      check(!r.events.empty() && r.events[0].type == "snap", op.id + " first event should be snap");
    }
  });

  t("api: user-controlled QB can throw via UserInput", [&] {
    GameOptions o;
    o.seed = 21;
    Game g(*league[8], *league[9], o);
    g.phase = GamePhase::Scrimmage;
    g.ballOn = 30;
    bool anyThrow = false;
    for (int trial = 0; trial < 10 && !anyThrow; trial++) {
      auto sim = g.makePlay(*playById("dbl_curls"), *defById("nk_c3"), Side::Off);
      check(sim->user == sim->qb && sim->qb->userControlled, "user should start on QB");
      int steps = 0;
      bool requested = false;
      while (sim->isLive() && steps++ < 2400) {
        // throwTo is a one-shot request (re-sending it every frame would keep re-arming the release)
        if (!requested && sim->t > 1.6 && sim->ball.holder == sim->qb) {
          sim->input.throwTo = "X";
          requested = true;
        }
        sim->step();
      }
      if (sim->isLive()) sim->endPlay("tackle");
      check(!sim->isLive(), "user play never ended");
      anyThrow = anyThrow || sim->passThrown;
    }
    check(anyThrow, "user QB never threw");
  });

  t("api: user-controlled defender + switchUser", [&] {
    GameOptions o;
    o.seed = 31;
    Game g(*league[10], *league[11], o);
    g.phase = GamePhase::Scrimmage;
    g.ballOn = 50;
    auto sim = g.makePlay(*playById("dbl_iz"), *defById("base_c3"), Side::Def);
    check(sim->user && sim->user->side == Side::Def && sim->user->role == "MLB", "user should start on MLB");
    int steps = 0;
    while (sim->isLive() && steps++ < 2400) {
      sim->input.move = V2{-1, 0};
      sim->input.sprint = true;
      if (steps == 30) sim->input.switchReq = true;
      sim->step();
    }
    int nUser = 0;
    for (const Ent* e : sim->ents) nUser += e->userControlled ? 1 : 0;
    check(nUser == 1, "exactly one user-controlled entity");
    check(!sim->isLive() || steps >= 2400, "loop exit");
  });

  t("api: special teams return flights", [&] {
    GameOptions o;
    o.seed = 41;
    Game g(*league[12], *league[13], o);
    const SpecialResult k = g.kickoff();
    check(k.flight.has_value() && !k.desc.empty(), "kickoff flight");
    g.phase = GamePhase::Scrimmage;
    g.ballOn = 70;
    const SpecialResult fg = g.fieldGoal();
    check(fg.flight.has_value() && fg.flight->good.has_value(), "fg flight with good flag");
    check(fmtClock(65.2) == "1:06" && fmtClock(0) == "0:00", "fmtClock " + fmtClock(65.2));
  });

  // ---------------- pre-snap adjustments (hot routes, protection, shifts, defensive assignments, audibles)
  auto scrimmage = [&](uint32_t seed, int a, int b) {
    GameOptions o;
    o.seed = seed;
    auto g = std::make_unique<Game>(*league[a], *league[b], o);
    g->phase = GamePhase::Scrimmage;
    g->ballOn = 35;
    return g;
  };

  t("presnap: hot routes rebuild the route and the QB's reads", [&] {
    auto g = scrimmage(51, 2, 3);
    auto sim = g->makePlay(*playById("dbl_curls"), *defById("nk_c3"), Side::Off, false);
    Ent* x = sim->offRole("X");
    Ent* rb = sim->offRole("RB");
    check(x->routeName == "curl" && rb->routeName == "checkdown", "called routes " + x->routeName + " " + rb->routeName);
    check(sim->hotRoute(x, "go"), "hot route refused");
    check(x->routeName == "go" && x->state == EntState::Route && x->route.back().x - sim->los > 40, "go route not rebuilt");
    check(sim->hotRoute(rb, "block") && rb->state == EntState::PassBlock, "RB kept in");
    bool hasRb = false;
    for (const std::string& r : sim->readOrder) hasRb = hasRb || r == "RB";
    check(!hasRb, "a blocker stays in the read order");
    check(!sim->hotRoute(sim->offRole("LT"), "go") && !sim->hotRoute(sim->qb, "go"), "linemen and the QB have no routes");
    check(!sim->hotRoute(x, "nonsense"), "unknown route accepted");
    sim->step();
    check(!sim->hotRoute(x, "slant"), "hot route after the snap");
    const PlayResult& r = sim->runToEnd();
    check(r.target == nullptr || r.target != rb->p, "threw to a blocker");
    auto run = g->makePlay(*playById("dbl_iz"), *defById("nk_c3"), Side::Off);
    check(!run->hotRoute(run->offRole("X"), "go"), "hot route on a run play");
  });

  t("presnap: shifts walk players over, blocking re-sets at the snap", [&] {
    auto g = scrimmage(61, 4, 5);
    auto sim = g->makePlay(*playById("dbl_mesh"), *defById("base_c3"), Side::Def, false);
    Ent* le = sim->defRole("LE");
    const double y0 = le->pos.y;
    sim->adjustLine(PlaySim::LineAdj::ShiftL);
    check(le->shiftTo && std::fabs(le->shiftTo->y - (y0 + 1.35)) < 1e-9, "shift target");
    for (int i = 0; i < 60 && le->shiftTo; ++i) sim->presnapStep(1.0 / 60);
    check(!le->shiftTo && std::fabs(le->pos.y - (y0 + 1.35)) < 1e-9, "did not arrive in a second");
    check(sim->t == 0 && sim->preSnap(), "presnap clock moved the play clock");
    sim->adjustLine(PlaySim::LineAdj::Base);
    sim->adjustLBs(PlaySim::LineAdj::Pinch);
    sim->adjustShell(PlaySim::ShellAdj::Disguise);
    sim->step();  // the snap: everyone at his spot
    check(std::fabs(le->pos.y - y0) < 1e-9, "base did not walk him back");
    for (const Ent* e : sim->ents) check(!e->shiftTo, e->role + " still shifting after the snap");
    sim->runToEnd();
    // a slide: the line picks different men
    auto pick = [&](int slide) {
      auto s = g->makePlay(*playById("dbl_mesh"), *defById("nk_c0"), std::nullopt, false);
      s->setProtection(slide, false);
      s->step();
      std::string out;
      for (const Ent* e : s->off)
        if (e->isOL) out += e->role + ">" + (e->blockTarget ? e->blockTarget->role : std::string("-")) + " ";
      return out;
    };
    check(pick(1) != pick(-1), "a slide left and right block the same men: " + pick(1) + " / " + pick(-1));
  });

  t("presnap: defensive assignments (blitz, spy, zones, man) play out", [&] {
    auto g = scrimmage(71, 6, 7);
    auto sim = g->makePlay(*playById("trips_flood"), *defById("base_c3"), Side::Def, false);
    Ent* mlb = sim->defRole("MLB");
    Ent* ss = sim->defRole("SS");
    Ent* fs = sim->defRole("FS");
    Ent* le = sim->defRole("LE");
    check(sim->assignDefender(mlb, PlaySim::DefAssign::Blitz) && mlb->state == EntState::Rush, "blitz");
    check(sim->assignDefender(ss, PlaySim::DefAssign::Spy) && ss->state == EntState::Spy, "spy");
    check(sim->assignDefender(le, PlaySim::DefAssign::Flat) && le->state == EntState::Zone && le->zone.rfind("flat", 0) == 0, "zone drop");
    check(sim->assignDefender(fs, PlaySim::DefAssign::Man) && fs->state == EntState::Man && fs->man, "man");
    // the protection picks him up as he comes (a blocker sets on him or engages him)
    bool picked = false;
    while (sim->isLive() && sim->t < 2.0 && !picked) {
      sim->step();
      for (const Ent* e : sim->off) picked = picked || e->blockTarget == mlb || e->engaged == mlb;
    }
    check(picked, "nobody picked up the blitzing linebacker");
    const PlayResult& r = sim->runToEnd();
    check(r.duration > 0, "play did not run");
  });

  t("presnap: motion moves the receiver and his route, a man defender travels with him", [&] {
    auto g = scrimmage(91, 10, 11);
    auto sim = g->makePlay(*playById("dbl_slants"), *defById("nk_c1"), Side::Off, false);
    Ent* z = sim->offRole("Z");
    Ent* cb = nullptr;
    for (Ent* d : sim->def)
      if (d->state == EntState::Man && d->man == z) cb = d;
    check(cb != nullptr, "nobody manned up on Z in cover 1");
    const double zy = z->pos.y, cy = cb->pos.y, r0 = z->route.back().y;
    check(sim->motion(z, 6), "motion refused");
    check(z->shiftTo && std::fabs(z->shiftTo->y - (zy + 6)) < 1e-9, "receiver target");
    check(std::fabs(z->route.back().y - (r0 + 6)) < 1.6, "route did not move with him");
    check(cb->shiftTo && std::fabs(cb->shiftTo->y - (cy + 6)) < 1e-9, "man defender did not travel");
    sim->previewBlocks();
    const PlayResult& r = sim->runToEnd();
    check(r.duration > 0, "play did not run");
  });

  t("presnap: adjusted plays stay deterministic, flip mirrors the formation", [&] {
    auto play = [&](bool flip) {
      auto g = scrimmage(81, 8, 9);
      auto sim = g->makePlay(*playById("trips_dagger"), *defById("nk_c1"), std::nullopt, flip);
      sim->hotRoute(sim->offRole("TE"), "corner");
      sim->setProtection(-1, true);
      sim->adjustShell(PlaySim::ShellAdj::Press);
      sim->adjustLine(PlaySim::LineAdj::Spread);
      for (int i = 0; i < 30; ++i) sim->presnapStep(1.0 / 60);
      const double xY = sim->offRole("X")->pos.y - sim->by;
      const PlayResult& r = sim->runToEnd();
      return std::make_pair(xY, fx(r.endX, 6) + "/" + std::to_string(r.events.size()) + "/" + fx(r.duration, 6));
    };
    const auto a = play(false), b = play(false), c = play(true);
    check(a.second == b.second, a.second + " vs " + b.second);
    check(std::fabs(a.first + c.first) < 1e-9, "flip did not mirror X");
  });

  // ---- realism plan, phase 1 regressions
  t("realism: passes stay on the field (no ball past the end line or sidelines, sane apex)", [&] {
    double maxX = 0, maxZ = 0, maxAirYd = 0;
    for (int i = 0; i < 160; ++i) {
      auto g = scrimmage(700 + i, i % 32, (i + 5) % 32);
      g->ballOn = i % 2 ? 40 : 75;  // a long field and the red zone
      auto sim = g->makePlay(*playById(i % 3 ? "empty_verts" : "dbl_postwheel"), *defById(i % 2 ? "nk_c3" : "base_c1"));
      while (sim->isLive()) {
        sim->step();
        if (sim->ball.mode == BallMode::Air) {
          maxX = (std::max)(maxX, sim->ball.pos.x);
          maxZ = (std::max)(maxZ, sim->ball.pos.z);
        }
      }
      maxAirYd = (std::max)(maxAirYd, static_cast<double>(sim->result->airYards));
    }
    check(maxX <= END_LINE + 1.6, "ball in the air reached x=" + fx(maxX, 1));
    check(maxZ <= 33, "pass apex " + fx(maxZ, 1) + " yd (30 m = 32.8 yd)");
    check(maxAirYd <= 85, "air yards " + fx(maxAirYd, 0));
  });

  t("realism: starters stay in (no QB change within a possession without an injury)", [&] {
    int swaps = 0;
    for (int gi = 0; gi < 6; ++gi) {
      GameOptions o;
      o.seed = 900 + gi;
      Game g(*league[gi], *league[gi + 10], o);
      int lastQb = -1, lastPoss = -1;
      g.onPlayResult = [&](const PlaySim& sim, const PlayResult&) {
        if (sim.qb->p->pos != Pos::QB) return;  // (a wildcat snap: the back took it)
        const int qid = sim.qb->p->id;
        const bool hurt = lastQb >= 0 && sim.qb->p->id != lastQb;
        if (hurt && lastPoss == static_cast<int>(g.poss)) {
          bool starterHurt = false;
          for (const auto& p : g.offense().roster)
            if (p->id == lastQb && p->injuredWeeks > 0) starterHurt = true;
          if (!starterHurt) {
            swaps++;
            const Player* was = nullptr;
            for (const auto& p : g.offense().roster)
              if (p->id == lastQb) was = p.get();
            std::printf("       (swap: game %d q%d clock %.0f score %d-%d, QB %d -> %d, the starter: injured %d fatigue %.2f)\n", gi, g.q, g.clock, g.score.home,
                        g.score.away, lastQb, qid, was ? was->injuredWeeks : -1, was ? was->fatigue : -1.0);
            for (const auto& p : g.offense().roster)
              if (p->pos == Pos::QB)
                std::printf("         QB %d ovr %d injured %.1f onFieldLast %d fatigue %.2f\n", p->id, p->ovr, static_cast<double>(p->injuredWeeks), p->onFieldLast ? 1 : 0, p->fatigue);
            std::printf("         this play: %s (formation %s)\n", sim.cfg.offPlay->id.c_str(), sim.cfg.offPlay->formation.c_str());
            std::printf("         last plays: %s | %s\n", g.log.size() > 1 ? g.log[g.log.size() - 2].desc.c_str() : "", g.log.empty() ? "" : g.log.back().desc.c_str());
          }
        }
        lastQb = qid;
        lastPoss = static_cast<int>(g.poss);
      };
      g.simToEnd();
    }
    check(swaps == 0, std::to_string(swaps) + " QB swaps within possessions");
  });

  t("realism: offensive linemen do not block far downfield on breakaways", [&] {
    double deepest = -99;
    for (int i = 0; i < 300; ++i) {
      auto g = scrimmage(1300 + i, i % 32, (i + 9) % 32);
      g->ballOn = 20 + (i % 5) * 10;
      auto sim = g->makePlay(*playById(i % 2 ? "trips_draw" : "i_power"), *defById("nk_c3"));
      deepest = (std::max)(deepest, sim->runToEnd().olBlockDepth);
    }
    check(deepest <= 17, "an OL was still blocking " + fx(deepest, 1) + " yd past the line");
  });

  t("realism: flags come from the play (holding on a blocker, interference on a defender)", [&] {
    double hold = 0, n = 0;
    int badHold = 0, badDpi = 0, dpiSeen = 0;
    for (int i = 0; i < 400; ++i) {
      auto g = scrimmage(1700 + i, i % 32, (i + 13) % 32);
      g->ballOn = 25 + (i % 4) * 10;
      auto sim = g->makePlay(*playById(i % 3 == 0 ? "i_power" : i % 3 == 1 ? "empty_verts" : "dbl_slants"), *defById(i % 2 ? "nk_c3" : "base_c1"));
      const PlayResult& r = sim->runToEnd();
      hold += r.foulHold.p;
      n++;
      if (r.foulHold.p > 0 && (!r.foulHold.by || !g->offense().hasPlayer(r.foulHold.by))) badHold++;
      if (r.foulDpi.p > 0) {
        dpiSeen++;
        if (!r.foulDpi.by || !g->defense().hasPlayer(r.foulDpi.by)) badDpi++;
      }
    }
    check(badHold == 0, std::to_string(badHold) + " holding risks not on an offensive player");
    check(badDpi == 0, std::to_string(badDpi) + " interference risks not on a defender");
    check(dpiSeen > 0, "no pass interference risk in 400 plays");
    check(hold / n > 0.01 && hold / n < 0.08, "holding risk per play " + fx(hold / n, 3));
  });

  t("playbooks: every team has its own full book, and every play in it can be run", [&] {
    std::set<std::string> offSchemes, defSchemes, sizes;
    for (const auto& tm : league) {
      check(!tm->offScheme.empty() && !tm->defScheme.empty(), tm->abbr + " has no scheme");
      const OffBook& ob = offBookFor(*tm);
      const DefBook& db = defBookFor(*tm);
      offSchemes.insert(ob.scheme);
      defSchemes.insert(db.scheme);
      sizes.insert(std::to_string(ob.plays.size()) + "/" + std::to_string(db.calls.size()));
      check(ob.formations.size() >= 9, tm->abbr + " offense has " + std::to_string(ob.formations.size()) + " formations");
      // (an NFL / Madden-sized book: 250-400 offensive plays from 25-35 sets, 250-350 defensive calls)
      check(ob.plays.size() >= 300, tm->abbr + " offense has " + std::to_string(ob.plays.size()) + " plays");
      check(ob.formations.size() >= 20, tm->abbr + " offense has only " + std::to_string(ob.formations.size()) + " sets");
      check(db.sets.size() >= 10 && db.calls.size() >= 240, tm->abbr + " defense has " + std::to_string(db.calls.size()) + " calls");
      {
        std::set<std::string> concepts, kinds;
        for (const OffPlay* p : ob.plays) {
          concepts.insert(p->conceptName);
          kinds.insert(playCatName(p->cat));
          check(!p->conceptName.empty() && !p->desc.empty() && !p->personnel.empty(), p->id + " lacks its play-call screen info");
        }
        check(concepts.size() >= 45, tm->abbr + " book has only " + std::to_string(concepts.size()) + " distinct concepts");
        check(kinds.count("Run") && kinds.count("Pass") && kinds.count("PA") && kinds.count("Screen"), tm->abbr + " book lacks a kind of play");
        std::set<std::string> covers, pressures;
        for (const DefCall* d : db.calls) {
          covers.insert(d->coverLabel);
          pressures.insert(d->pressure);
          check(!d->frontLabel.empty() && !d->coverLabel.empty() && !d->pressure.empty() && !d->desc.empty(), d->id + " lacks its play-call screen info");
        }
        check(covers.size() >= 14 && pressures.size() >= 5, tm->abbr + " defense: " + std::to_string(covers.size()) + " coverages, " + std::to_string(pressures.size()) + " pressure kinds");
      }
      bool goalLine = false, runs = false, pa = false;
      for (const std::string& f : ob.formations) check(ob.playsIn(f).size() >= 6, tm->abbr + " " + f + " has too few plays");
      for (const OffPlay* p : ob.plays) {
        const Formation* f = formationById(p->formation);
        check(f != nullptr, p->id + " formation missing");
        goalLine = goalLine || f->personnel == "22";
        runs = runs || p->type == PlayType::Run;
        pa = pa || p->type == PlayType::PlayAction;
        // every read is an eligible man in the formation with a route
        for (const std::string& r : p->reads) {
          bool inForm = false;
          for (const FormationSlot& s : f->slots) inForm = inForm || s.role == r;
          check(inForm, p->id + " reads " + r + " who is not in " + f->id);
          const std::string* rt = p->routeFor(r);
          check(rt && *rt != "block" && routeByName(*rt), p->id + " reads " + r + " without a route");
        }
        for (const auto& kv : p->routes) check(routeByName(kv.second) != nullptr, p->id + " unknown route " + kv.second);
        if (p->type != PlayType::Run && p->type != PlayType::Screen) check(p->reads.size() >= 2, p->id + " has one read");
      }
      check(goalLine && runs && pa, tm->abbr + " book lacks the goal line set, runs or play action");
    }
    check(offSchemes.size() == 8 && defSchemes.size() == 5, "every scheme is someone's");
    check(sizes.size() >= 24, "books are not each team's own: only " + std::to_string(sizes.size()) + " distinct sizes");
  });

  t("playbooks: the CPU calls only from its own book, the 3-4 lines up with three down", [&] {
    auto g = scrimmage(4100, 1, 4);
    const OffBook& ob = offBookFor(g->offense());
    for (int i = 0; i < 300; i++) {
      g->down = 1 + i % 3;
      g->toGo = 1 + i % 12;
      g->ballOn = 20 + (i * 7) % 75;
      const OffPlay* p = g->cpuOffense();
      check(p->type == PlayType::Kneel || p->type == PlayType::Spike || ob.has(p), p->id + " is not in the " + ob.name + " book");
      const DefCall* d = g->cpuDefense(*p);
      check(defBookFor(g->defense()).has(d) || d->set.empty(), d->id + " is not in the defense's book");
    }
    for (const DefCall& d : DEF_PLAYS()) {
      if (d.front != Front::Base34) continue;
      auto sim = g->makePlay(*playById("dbl_iz"), d);
      int dl = 0, olb = 0;
      for (const Ent* e : sim->def) {
        dl += e->isDL;
        olb += e->role == "LOLB" || e->role == "ROLB";
      }
      check(dl == 3 && olb == 2, d.id + " has " + std::to_string(dl) + " down linemen");
      sim->runToEnd();
      break;
    }
  });

  // ---- the expanded playbook's mechanics (2026-11-22)
  auto findPlay = [&](const std::function<bool(const OffPlay&)>& f) -> const OffPlay* {
    for (const OffPlay& p : OFF_PLAYS())
      if (f(p)) return &p;
    return nullptr;
  };
  t("playbook mechanics: a gap run's pullers pull and kick out / wrap; a lead back leads", [&] {
    const OffPlay* ctr = findPlay([](const OffPlay& p) { return p.conceptName == "Counter GT" && !p.stock && formationById(p.formation)->qbDepth < 2; });
    check(ctr != nullptr, "no counter play");
    if (!ctr) return;
    int pulls = 0, snaps = 0, onMan = 0;
    for (int i = 0; i < 60; i++) {
      auto g = scrimmage(9100 + i, i % 32, (i + 5) % 32);
      auto sim = g->makePlay(*ctr, *defById(i % 2 ? "base_c3" : "nk_c3"));
      std::vector<Ent*> men;
      std::vector<double> y0;
      sim->step(SIM_DT);
      for (Ent* e : sim->off)
        if (std::string(sim->runJob(e)) != "" && e->isOL) men.push_back(e), y0.push_back(e->pos.y);
      snaps++;
      while (sim->isLive() && sim->t < 1.1) sim->step(SIM_DT);
      for (size_t k = 0; k < men.size(); k++) {
        if (std::fabs(men[k]->pos.y - y0[k]) > 1.8 && men[k]->pos.x < sim->los + 1.5) pulls++;
        if (men[k]->engaged) onMan++;
      }
      if (sim->isLive()) sim->runToEnd();
    }
    std::printf("       (%d snaps: %d linemen pulled 1.8+ yd down the line in 1.1 s, %d were on their man)\n", snaps, pulls, onMan);
    check(pulls >= snaps / 2, "counter GT: only " + std::to_string(pulls) + " pulls in " + std::to_string(snaps) + " snaps");
    auto g = scrimmage(9200, 3, 4);
    auto sim = g->makePlay(*playById("i_iso"), *defById("base_c3"));
    sim->step(SIM_DT);
    Ent* fb = sim->offRole("FB");
    check(fb && std::string(sim->runJob(fb)) == "lead" && fb->blockTarget && !fb->blockTarget->isDL, "iso: the fullback leads for a linebacker");
  });

  t("playbook mechanics: the zone read leaves the end unblocked and the quarterback keeps it when he squeezes", [&] {
    const OffPlay* zr = findPlay([](const OffPlay& p) { return p.run && p.run->read == "end"; });
    check(zr != nullptr, "no zone read");
    if (!zr) return;
    int keeps = 0, gives = 0, blocked = 0, qbRush = 0;
    for (int i = 0; i < 80; i++) {
      auto g = scrimmage(9300 + i, i % 32, (i + 9) % 32);
      auto sim = g->makePlay(*zr, *defById(i % 2 ? "base_c3" : "nk_c1"));
      while (sim->isLive() && sim->t < 0.6) sim->step(SIM_DT);
      if (sim->readEnt && !sim->readEnt->engagedBy.empty()) blocked++;
      const PlayResult& r = sim->runToEnd();
      if (sim->qbKept) {
        keeps++;
        qbRush += r.rusher == sim->qb->p;
      } else gives++;
    }
    check(blocked == 0, std::to_string(blocked) + " read ends blocked before the mesh");
    check(keeps >= 15 && gives >= 15, "zone read: " + std::to_string(keeps) + " keeps, " + std::to_string(gives) + " gives");
    check(qbRush == keeps, "a keep is the quarterback's run");
  });

  t("playbook mechanics: an RPO is thrown when the conflict defender is out of the play, and then it is a pass", [&] {
    const OffPlay* rp = findPlay([](const OffPlay& p) { return p.run && p.run->read == "rpo" && !p.run->rpo.empty(); });
    check(rp != nullptr, "no RPO");
    if (!rp) return;
    int thrown = 0, passes = 0, n = 0;
    for (int i = 0; i < 80; i++) {
      auto g = scrimmage(9400 + i, i % 32, (i + 3) % 32);
      auto sim = g->makePlay(*rp, *defById(i % 3 == 0 ? "nk_c1" : i % 3 == 1 ? "nk_c3" : "base_c2"));
      const PlayResult& r = sim->runToEnd();
      n++;
      if (sim->rpoThrow) {
        thrown++;
        passes += r.kind == PlayKind::Pass || r.interception;
        check(r.passer == sim->qb->p, "an RPO throw has a passer");
      } else check(r.kind == PlayKind::Run, "an RPO handed off is a run");
    }
    check(thrown > 5 && thrown < n - 5, "RPO thrown " + std::to_string(thrown) + " of " + std::to_string(n));
    check(passes == thrown, "every RPO throw is scored a pass");
  });

  t("playbook mechanics: the jet man is snapped at full speed and takes the sweep on the move", [&] {
    const OffPlay* js = findPlay([](const OffPlay& p) { return p.run && p.run->jet && p.motion; });
    check(js != nullptr, "no jet sweep");
    if (!js) return;
    double vSum = 0, tSum = 0;
    int got = 0;
    for (int i = 0; i < 40; i++) {
      auto g = scrimmage(9500 + i, i % 32, (i + 7) % 32);
      auto sim = g->makePlay(*js, *defById("base_c3"));
      Ent* jm = sim->offRole(js->motion->role);
      sim->step(SIM_DT);
      vSum += jm->speed();
      while (sim->isLive() && sim->t < 3 && sim->ball.holder != jm) sim->step(SIM_DT);
      if (sim->ball.holder == jm) got++, tSum += sim->t;
      if (sim->isLive()) sim->runToEnd();
    }
    check(vSum / 40 > 5.0, "the jet man is at " + fx(vSum / 40) + " yd/s at the snap");
    check(got >= 36, "the jet man got the ball on " + std::to_string(got) + " of 40");
    check(got == 0 || tSum / got < 1.6, "the jet handoff at " + fx(tSum / (std::max)(1, got)) + " s");
  });

  t("playbook mechanics: zone blitzes drop a lineman, fire zones keep three deep, disguises show the other shell", [&] {
    auto g = scrimmage(9600, 2, 6);
    int checked = 0;
    for (const DefCall& d : DEF_PLAYS()) {
      if (d.front == Front::Goalline) continue;
      auto sim = g->makePlay(*playById("dbl_mesh"), d);
      int rush = 0, deep = 0;
      for (const Ent* e : sim->def) {
        rush += e->state == EntState::Rush;
        deep += e->deepRole;
        if (std::find(d.drop.begin(), d.drop.end(), e->role) != d.drop.end())
          check(e->state == EntState::Zone || e->state == EntState::Man, d.id + ": the dropping " + e->role + " is not in coverage");
      }
      check(rush == d.rushers(), d.id + " rushes " + std::to_string(rush) + ", rushers() says " + std::to_string(d.rushers()));
      if (d.coverage == Coverage::C3Fire || d.coverage == Coverage::C3) check(deep == 3, d.id + " has " + std::to_string(deep) + " deep");
      if (d.coverage == Coverage::C4 && d.tech != CoverTech::Prevent) check(deep == 4, d.id + " has " + std::to_string(deep) + " deep in quarters");
      if (d.rotate && d.coverage == Coverage::C3) {
        const Ent* fs = sim->defRole("FS");
        const Ent* ss = sim->defRole("SS");
        check(fs && ss && fs->pos.x > sim->los + 10 && ss->pos.x > sim->los + 10, d.id + ": a Cover 3 rotate shows two high");
      }
      checked++;
    }
    check(checked > 500, "only " + std::to_string(checked) + " calls checked");
  });

  t("playbook mechanics: a new play type gives the same result twice from the same seed", [&] {
    for (const char* kind : {"rpo", "end", "jet"}) {
      const OffPlay* p = findPlay([&](const OffPlay& x) { return x.run && (std::string(kind) == "jet" ? x.run->jet : x.run->read == kind); });
      if (!p) continue;
      auto run = [&]() {
        auto g = scrimmage(9700, 1, 2);
        auto sim = g->makePlay(*p, *defById("nk_c3"));
        const PlayResult& r = sim->runToEnd();
        return std::to_string(r.yards) + "/" + fx(r.duration, 4) + "/" + std::to_string(static_cast<int>(r.kind));
      };
      check(run() == run(), std::string(kind) + " play is not deterministic");
    }
  });

  t("highlight stick: moves change the tackle odds the way they should", [&] {
    auto g = scrimmage(4200, 3, 6);
    auto sim = g->makePlay(*playById("dbl_iz"), *defById("base_c3"), Side::Off);
    Ent* c = sim->offRole("RB");
    Ent* d = sim->defRole("MLB");
    c->userControlled = true;
    auto adj = [&](Move cm, Move dm, double facing, bool dive) {
      // (the sim reads the play's ratings, Ent::rr, taken at the snap: the test's own numbers go there too)
      c->rr = c->p->r;
      d->rr = d->p->r;
      c->move = cm;
      c->moveT = cm == Move::None ? 9 : 0.1;
      d->move = dm;
      d->moveT = dm == Move::None ? 9 : 0.1;
      return sim->moveTackleAdj(d, c, facing, false, dive);
    };
    const Ratings keep = c->p->r;
    c->p->r.strength = 75;  // a league-average starter (the roster's own man may be anyone)
    c->p->r.breakTackle = 78;
    c->p->r.agility = 86;
    c->p->r.elusiveness = 80;
    check(adj(Move::None, Move::None, 1, false) == 0, "no move, no change");
    check(adj(Move::Spin, Move::None, 1, false) < -0.5, "a spin in his face helps the runner");
    check(adj(Move::StiffArm, Move::None, 0.2, false) < -0.3, "a stiff arm on a man from the side helps");
    check(adj(Move::StiffArm, Move::None, -0.9, false) == 0, "a stiff arm does nothing for a man behind him");
    check(adj(Move::Hurdle, Move::None, 1, true) < -1.5, "a hurdle beats a diving tackle");
    check(adj(Move::Hurdle, Move::None, 1, false) > 0, "a hurdle into a man standing up gets him hit in the air");
    check(adj(Move::None, Move::Hit, 1, false) > 0.5, "the hit stick from the front raises the tackle odds");
    check(adj(Move::Truck, Move::Cut, 1, false) > adj(Move::Truck, Move::None, 1, false) + 1, "the cut beats the truck");
    check(adj(Move::Hurdle, Move::Cut, 1, false) < -1.5, "the hurdle beats the cut");
    // a power back trucks better than a scat back
    c->p->r.strength = 90;
    c->p->r.breakTackle = 92;
    const double big = adj(Move::Truck, Move::None, 1, false);
    c->p->r.strength = 50;
    c->p->r.breakTackle = 55;
    const double small = adj(Move::Truck, Move::None, 1, false);
    c->p->r = keep;
    check(big < small - 1, "truck: strength matters (" + fx(big) + " vs " + fx(small) + ")");
    // a user's move starts through the input and plays out: the event, the move, its window
    c->move = Move::None;
    d->move = Move::None;
    c->moveT = d->moveT = 9;
    int started = 0;
    for (int i = 0; i < 200; i++) {
      auto g2 = scrimmage(4300 + i, i % 32, (i + 5) % 32);
      auto s2 = g2->makePlay(*playById(i % 2 ? "i_power" : "dbl_iz"), *defById("base_c3"), Side::Off);
      while (s2->isLive() && s2->t < 12) {
        Ent* h = s2->ball.holder;
        if (h && h != s2->qb && h->side == Side::Off && s2->user != h) {
          s2->user->userControlled = false;
          s2->user = h;
          h->userControlled = true;
        }
        if (h && h->userControlled && h->animT > 0.7) {
          const Ent* th = nullptr;
          const Move m = s2->suggestMove(h, &th);
          if (th && dist(th->pos, h->pos) < 2) s2->input.stickMove = m;
        }
        s2->step();
        s2->input.stickMove = Move::None;
      }
      if (s2->isLive()) s2->runToEnd();
      for (const PlayEvent& e : s2->events) started += e.type == "move" && e.a && e.a->userControlled;
      for (const Ent* e : s2->ents) check(std::isfinite(e->pos.x) && std::isfinite(e->pos.y), "NaN after a move");
    }
    check(started > 60, "only " + std::to_string(started) + " user moves started in 200 carries");
  });

  t("R1 / L2: covering up keeps the ball, the strip knocks it out, the strafe stays square", [&] {
    // fumbles made common for the count (x20), then the three cases over the same 240 runs: as played, every defender
    // going for the ball (R1 on defense), every carrier covered up (R1 with the ball)
    const double keepF = tune().fumbleK;
    setTune("fumbleK", 20);
    auto run = [&](int mode, int& fum, int& broken, int& tackles) {
      fum = broken = tackles = 0;
      for (int i = 0; i < 240; i++) {
        auto g2 = scrimmage(5100 + i, i % 32, (i + 9) % 32);
        auto s2 = g2->makePlay(*playById(i % 2 ? "i_power" : "dbl_iz"), *defById("base_c3"));
        while (s2->isLive() && s2->t < 12) {
          if (mode == 1)
            for (Ent* d : s2->def) d->stripT = 0.8;
          if (mode == 2)
            if (Ent* h = s2->ball.holder) h->protecting = h->hasBall && h != s2->qb;
          s2->step();
        }
        if (s2->isLive()) s2->runToEnd();
        for (const PlayEvent& e : s2->events) {
          fum += e.type == "fumble";
          broken += e.type == "broken" || e.type == "divemiss";
          tackles += e.type == "tackle" || e.type == "wrap";
        }
      }
    };
    int f0, b0, t0, f1, b1, t1, f2, b2, t2;
    run(0, f0, b0, t0);
    run(1, f1, b1, t1);
    run(2, f2, b2, t2);
    setTune("fumbleK", keepF);
    check(f1 > f0 * 2, "the strip: " + std::to_string(f1) + " fumbles against " + std::to_string(f0) + " as played");
    check(b1 > b0, "the strip misses more tackles: " + std::to_string(b1) + " broken against " + std::to_string(b0));
    check(f2 * 2 < f0 + 1, "covered up: " + std::to_string(f2) + " fumbles against " + std::to_string(f0) + " as played");
    // the strafe: square to the ball (his facing on it) and held to 70 % of his speed, flat out on the stick
    auto g3 = scrimmage(5400, 3, 6);
    auto s3 = g3->makePlay(*playById("trips_flood"), *defById("base_c3"), Side::Def);  // (a pass: nobody blocks him)
    Ent* u = s3->user;
    double maxSp = 0, offFace = 0;
    int n = 0;
    while (s3->isLive() && s3->t < 2.0) {
      s3->input.strafe = true;
      s3->input.move = V2{0, 1};
      s3->input.sprint = true;
      s3->step();
      if (s3->t > 1.0 && s3->ball.holder) {
        maxSp = (std::max)(maxSp, len(u->vel));
        const double want = std::atan2(s3->ball.holder->pos.y - u->pos.y, s3->ball.holder->pos.x - u->pos.x);
        offFace = (std::max)(offFace, std::fabs(std::remainder(u->face - want, 2 * kPi)));
        n++;
      }
    }
    check(n > 0 && u->strafing, "the user's man strafes while L2 is held");
    check(maxSp <= u->vmax * 0.72 + 0.05, "strafing speed " + fx(maxSp) + " of " + fx(u->vmax));
    check(offFace < 0.35, "strafing, he faces the ball (off by " + fx(offFace) + " rad)");
  });

  // ------------------------------------------------------------------ Superstar / X-Factor abilities (Abilities.h)
  t("abilities: grades by position share, loadouts inside slots and AP, X-Factors on the elite", [&] {
    int graded[4] = {0, 0, 0, 0}, xfWith = 0;
    for (const auto& tm : league) {
      int stars = 0;
      for (const auto& p : tm->roster) {
        const int g = static_cast<int>(p->grade);
        graded[g]++;
        stars += g > 0;
        check(static_cast<int>(p->abilities.size()) <= gradeSlots(p->grade), p->last + " over his slots");
        check(abilityAPUsed(*p) <= gradeAP(p->grade), p->last + " over his AP");
        if (p->xFactor != AbilityId::None) check(p->grade == StarGrade::XFactor, p->last + " has an X-Factor without the grade");
        if (p->grade == StarGrade::XFactor) xfWith += p->xFactor != AbilityId::None;
        for (AbilityId id : p->abilities) check(abilityLock(*p, abilityDef(id)).empty(), p->last + " carries a locked ability");
        if (g > 0) check(!p->abilities.empty() || p->xFactor != AbilityId::None, p->last + " (" + starGradeName(p->grade) + ") has nothing equipped");
      }
      check(stars >= 5, tm->abbr + " has only " + std::to_string(stars) + " graded players");
      int top = 0;
      for (const auto& p : tm->roster) top += static_cast<int>(p->grade) >= static_cast<int>(StarGrade::Superstar);
      check(top >= 1, tm->abbr + " has no Superstar");
    }
    check(graded[3] >= 30 && graded[3] <= 70, "X-Factor grades " + std::to_string(graded[3]));
    check(graded[2] >= 100 && graded[2] <= 175, "Superstar grades " + std::to_string(graded[2]));
    check(xfWith * 10 >= graded[3] * 7, "only " + std::to_string(xfWith) + " of " + std::to_string(graded[3]) + " X-Factor men fit one");
  });

  t("abilities: the catalog (every id its own entry, unique keys, every graded man has a choice)", [&] {
    std::set<std::string> keys;
    for (int i = 1; i < static_cast<int>(AbilityId::Count); i++) {
      const AbilityDef& a = abilityDef(static_cast<AbilityId>(i));
      check(static_cast<int>(a.id) == i, "id " + std::to_string(i) + " resolves to " + std::to_string(static_cast<int>(a.id)));
      check(keys.insert(a.key).second, std::string("duplicate key ") + a.key);
      check(abilityByKey(a.key) == &a, std::string("key lookup ") + a.key);
      check(a.posMask != 0 && std::string(a.icon).size() == 2, std::string("positions / icon of ") + a.key);
      if (a.tier == AbilityTier::XFactor) check(a.need > 0 && *a.trigger && *a.defeat, std::string("zone rules of ") + a.key);
    }
    int thin = 0;
    for (const auto& tm : league)
      for (const auto& p : tm->roster) {
        if (p->grade == StarGrade::None) continue;
        int open = 0;
        for (const AbilityDef* a : abilitiesFor(p->pos, AbilityTier::Superstar)) open += abilityLock(*p, *a).empty();
        check(open >= 1, p->last + " (" + posName(p->pos) + ", " + starGradeName(p->grade) + ") can equip nothing");
        thin += open < 2;
      }
    check(thin <= 25, std::to_string(thin) + " graded men with a single choice");
  });

  t("abilities: locks (position, grade, ratings, AP) and the loadout save string", [&] {
    Player p;
    p.pos = Pos::QB;
    p.r.poise = 90;
    p.r.accShort = 90;
    p.r.accMid = 90;
    p.r.accDeep = 90;
    p.r.throwPower = 95;
    p.r.throwOnRun = 90;
    p.r.decision = 90;
    p.r.awareness = 90;
    p.r.agility = 80;
    p.grade = StarGrade::Star;
    std::string why;
    check(!equipAbility(p, AbilityId::Blanket, &why) && why.find("Not for") == 0, "a QB took a corner's ability: " + why);
    check(!equipAbility(p, AbilityId::MoonballTouch, &why) && why.find("Superstar") != std::string::npos, "a Star took a Superstar ability: " + why);
    check(!equipAbility(p, AbilityId::IceVeins, &why), "a Star took an X-Factor");
    check(equipAbility(p, AbilityId::PocketPoise, &why), "Pocket Poise: " + why);
    check(!equipAbility(p, AbilityId::LaserLine, &why), "a Star took a second ability");
    p.grade = StarGrade::Superstar;  // 2 slots, 5 AP
    check(equipAbility(p, AbilityId::MoonballTouch, &why), "Moonball Touch: " + why);
    check(!equipAbility(p, AbilityId::EscapeHatch, &why), "a third ability on two slots");
    p.grade = StarGrade::XFactor;  // 3 slots, 7 AP: 2 + 3 used
    check(equipAbility(p, AbilityId::LaserLine, &why), "Laser Line on 2 AP left: " + why);
    check(abilityAPUsed(p) == 7, "AP used " + std::to_string(abilityAPUsed(p)));
    check(equipAbility(p, AbilityId::IceVeins, &why) && equipAbility(p, AbilityId::RocketArm, &why) && p.xFactor == AbilityId::RocketArm,
          "an X-Factor replaces the last");
    Player q = p;
    q.abilities.clear();
    q.xFactor = AbilityId::None;
    applyLoadoutString(q, loadoutString(p));
    check(q.abilities == p.abilities && q.xFactor == p.xFactor, "round trip: " + loadoutString(p) + " -> " + loadoutString(q));
    q.r.throwPower = 70;  // no longer has the arm: the save's Rocket Arm and Moonball Touch are dropped
    applyLoadoutString(q, loadoutString(p));
    check(q.xFactor == AbilityId::None && !hasAbility(q, AbilityId::MoonballTouch) && hasAbility(q, AbilityId::LaserLine), "locked keys kept: " + loadoutString(q));
    p.r.poise = 50;
    validateLoadout(p);
    check(!hasAbility(p, AbilityId::PocketPoise), "validate kept Pocket Poise on 50 poise");
  });

  t("abilities: neutral when off or unequipped (same game as before), and they change games when on", [&] {
    auto run = [](int mode) {
      League L = createLeague(2027);
      if (mode == 2)
        for (auto& tm : L)
          for (auto& p : tm->roster) {
            p->abilities.clear();
            p->xFactor = AbilityId::None;
          }
      GameOptions o;
      o.seed = 777;
      o.abilities = mode != 0;
      Game g(*L[5], *L[9], o);
      g.simToEnd();
      std::string s = std::to_string(g.score.home) + "-" + std::to_string(g.score.away) + "-" + std::to_string(g.log.size());
      for (const PlayLog& l : g.log) s += l.desc.substr(0, 12);
      return s;
    };
    // (the league offsets that hold the average with stars in it, Tune::abilTackle / abilAcc, set aside: the abilities
    // themselves must be neutral on a man who carries none)
    const double keepT = tune().abilTackle, keepA = tune().abilAcc;
    tune().abilTackle = 0;
    tune().abilAcc = 1;
    const std::string off = run(0), bare = run(2);
    tune().abilTackle = keepT;
    tune().abilAcc = keepA;
    check(off == bare, "abilities off != abilities on with none equipped");
    int differ = 0;
    for (uint32_t seed = 1; seed <= 4; seed++) {
      auto one = [seed](bool on) {
        League L = createLeague(2027);
        GameOptions o;
        o.seed = 900 + seed;
        o.abilities = on;
        Game g(*L[seed], *L[seed + 10], o);
        g.simToEnd();
        return std::to_string(g.score.home) + "-" + std::to_string(g.score.away) + "-" + std::to_string(g.log.size());
      };
      differ += one(true) != one(false);
    }
    check(differ >= 3, "abilities changed only " + std::to_string(differ) + " of 4 games");
  });

  t("abilities: the zone (trigger, defeat, events) and what a live X-Factor does to a man", [&] {
    Player k;
    k.id = 99001;
    k.pos = Pos::K;
    k.grade = StarGrade::XFactor;
    k.r.kickAcc = 90;
    k.r.kickPower = 85;
    check(equipAbility(k, AbilityId::Automatic), "Automatic");
    AbilityState st;
    st.afterKick(k, true, true, "TST");
    check(!st.active(k) && st.zone(k)->progress == 1, "one make: not yet");
    st.afterKick(k, true, false, "TST");  // an extra point does not count toward it
    check(st.zone(k)->progress == 1, "a PAT counted");
    st.afterKick(k, true, true, "TST");
    check(st.active(k) && st.events.size() == 1 && st.events[0].entered, "two makes: in the zone");
    AbilitySituation s;
    const Ratings in = kickRatings(k, false, s, true), out = kickRatings(k, false, s, false);
    check(in.kickAcc > out.kickAcc + 10, "the zone's kick accuracy");
    check(st.activeIds() == std::vector<int>{99001}, "active ids");
    st.afterKick(k, false, false, "TST");
    check(!st.active(k) && st.zone(k)->progress == 0 && st.events.size() == 2 && !st.events[1].entered, "a miss knocks him out");
    // an X-Factor out of the zone does nothing; in it, it moves the numbers the sim reads
    Player cb;
    cb.pos = Pos::CB;
    cb.grade = StarGrade::XFactor;
    cb.r.manCov = 90;
    cb.r.speed = 94;
    check(equipAbility(cb, AbilityId::Lockdown), "Lockdown");
    AbilityFx fx;
    abilityFx(cb, s, false, fx);
    check(!fx.any && fx.coverLag == 1, "Lockdown out of the zone");
    abilityFx(cb, s, true, fx);
    check(fx.any && fx.coverLag < 0.6 && fx.pbu > 0.5 && fx.add.manCov > 0, "Lockdown in the zone");
    // situational Superstar abilities: Red Zone Threat only inside the 20
    Player wr;
    wr.pos = Pos::WR;
    wr.grade = StarGrade::Star;
    wr.r.catchInTraffic = 80;
    check(equipAbility(wr, AbilityId::RedZoneThreat), "Red Zone Threat");
    s.los = 50;
    abilityFx(wr, s, false, fx);
    check(!fx.any, "Red Zone Threat at midfield");
    s.los = 88;
    abilityFx(wr, s, false, fx);
    check(fx.any && fx.add.catchInTraffic >= 10, "Red Zone Threat at the 12");
  });

  t("abilities: X-Factor zones come and go over full games", [&] {
    int in = 0, out = 0;
    std::set<int> kinds;
    for (uint32_t seed = 1; seed <= 12; seed++) {
      League L = createLeague(2027);
      GameOptions o;
      o.seed = 5000 + seed;
      Game g(*L[seed % 32], *L[(seed * 7 + 3) % 32], o);
      int guard = 0;
      while (!g.isFinal() && guard++ < 2000) {
        g.simStep();
        for (const AbilityEvent& e : g.abil.events) {  // (drained as the presentation does)
          (e.entered ? in : out)++;
          kinds.insert(static_cast<int>(e.id));
        }
        g.abil.events.clear();
      }
    }
    check(in >= 12, "only " + std::to_string(in) + " zone entries in 12 games");
    check(out >= 3, "only " + std::to_string(out) + " zone exits in 12 games");
    check(kinds.size() >= 5, "only " + std::to_string(kinds.size()) + " different X-Factors reached the zone");
  });


  t("depth chart: the user's order starts, a moved man plays where he was put, subs come in on their downs", [&] {
    League L = createLeague(2027);
    Team& o = *L[4];
    Team& d = *L[9];
    const OffPlay* op = nullptr;
    for (const OffPlay& x : OFF_PLAYS()) {
      const Formation* f = formationById(x.formation);
      bool rb = false, slot = false;
      for (const FormationSlot& fs : f->slots) {
        rb = rb || fs.role == "RB";
        slot = slot || fs.role == "SLOT";
      }
      if (rb && slot && x.type != PlayType::Run) { op = &x; break; }
    }
    auto callOf = [](Front fr) {
      for (const DefCall& x : DEF_PLAYS())
        if (x.front == fr) return &x;
      return static_cast<const DefCall*>(nullptr);
    };
    check(op && callOf(Front::Base) && callOf(Front::Nickel), "no test play");
    if (!op) return;
    auto sim = [&](double down, double toGo, Front fr) {
      PlayConfig cfg;
      cfg.off = &o;
      cfg.def = &d;
      cfg.offPlay = op;
      cfg.defCall = callOf(fr);
      cfg.los = 40;
      cfg.down = down;
      cfg.toGo = toGo;
      cfg.rng = Rng(11);
      return std::unique_ptr<PlaySim>(new PlaySim(cfg));
    };
    const std::vector<Player*> qbs = depth(o, Pos::QB), rbs = depth(o, Pos::RB), cbs = depth(d, Pos::CB), ss = depth(d, Pos::S);
    {
      auto s0 = sim(1, 10, Front::Base);
      check(s0->offRole("QB")->p == qbs[0], "no chart: the best QB starts");
      check(s0->defRole("FS")->p->pos == Pos::S, "no chart: a safety at FS");
    }
    // the backup quarterback named the starter
    setChart(o, DSlot::QB, {qbs[1], qbs[0], qbs[2]});
    // the best corner moved to free safety
    std::vector<Player*> fsList = {cbs[0]};
    for (Player* p : chartOrder(d, DSlot::FS)) fsList.push_back(p);
    setChart(d, DSlot::FS, fsList);
    // a third down back
    setChart(o, DSlot::TDRB, {rbs[2]});
    {
      auto s1 = sim(1, 10, Front::Base);
      check(s1->offRole("QB")->p == qbs[1], "the QB the chart starts");
      check(s1->defRole("FS")->p == cbs[0], "the corner put at FS plays FS");
      check(s1->defRole("LCB")->p != cbs[0] && s1->defRole("RCB")->p != cbs[0], "and not at corner too");
      check(s1->offRole("RB")->p == rbs[0], "1st down: the halfback");
      auto s3 = sim(3, 6, Front::Nickel);
      check(s3->offRole("RB")->p == rbs[2], "3rd down: the third down back");
    }
    // the specialists: the punter named the kicker
    Player* pun = depth(o, Pos::P)[0];
    setChart(o, DSlot::K, {pun});
    GameOptions go;
    go.seed = 3;
    Game g(o, d, go);
    check(g.kicker(TeamKey::Home) == pun, "the chart's kicker kicks");
    // a set slot keeps a new man on it; a cleared chart is the coach's again
    clearAllCharts(o);
    clearAllCharts(d);
    check(sim(1, 10, Front::Base)->offRole("QB")->p == qbs[0], "cleared: the best QB again");
    check(g.kicker(TeamKey::Home)->pos == Pos::K, "cleared: the kicker kicks");
    // the coach's order mirrors the old pick: LT the best tackle, RT the second
    const std::vector<Player*> ots = depth(o, Pos::OT);
    check(autoChart(o, DSlot::LT)[0] == ots[0] && autoChart(o, DSlot::RT)[0] == ots[1], "coach's LT / RT");
  });

  t("create-a-player: archetype ratings, a free number, a full slot on the chart", [&] {
    for (int pi = 0; pi <= static_cast<int>(Pos::P); pi++) {
      const Pos pos = static_cast<Pos>(pi);
      const auto& arches = capArchetypes(pos);
      check(arches.size() >= 2, std::string("archetypes for ") + posName(pos));
      for (size_t a = 0; a < arches.size(); a++) {
        const int lo = computeOvr(pos, capRatings(pos, static_cast<int>(a), -8));
        const int mid = computeOvr(pos, capRatings(pos, static_cast<int>(a), 0));
        const int hi = computeOvr(pos, capRatings(pos, static_cast<int>(a), 14));
        check(lo < mid && mid < hi, std::string(posName(pos)) + " " + arches[a].name + " talent order");
        check(mid >= 60 && mid <= 90, std::string(posName(pos)) + " " + arches[a].name + " starter OVR " + std::to_string(mid));
      }
    }
    check(capRatings(Pos::QB, 3, 0).speed > capRatings(Pos::QB, 0, 0).speed + 10, "the scrambler is faster");
    League L = createLeague(2027);
    Team& t0 = *L[0];
    const int n = freeNumber(t0, Pos::QB, -1);
    check(n >= 0 && numberFree(t0, n), "a free number");
    std::vector<Player*> wr = chartOrder(t0, DSlot::WR);
    setChart(t0, DSlot::WR, wr);
    auto p = std::make_unique<Player>();
    p->id = 700001;
    p->pos = Pos::WR;
    p->number = freeNumber(t0, Pos::WR, -1);
    p->r = capRatings(Pos::WR, 0, 10);
    p->ovr = computeOvr(Pos::WR, p->r);
    chartAddPlayer(t0, *p);
    t0.roster.push_back(std::move(p));
    check(chartOrder(t0, DSlot::WR).back()->id == 700001, "the new man at the bottom of the set slot");
    check(depth(t0, Pos::WR).size() == wr.size() + 1, "and in the position's depth");
  });

  t("run exchange: the back takes the ball at a run, within reach, the quarterback turned to him, on every handoff play", [&] {
    // every run play with an exchange, three snaps each (Tools/HandoffCal prints the same numbers by play)
    int n = 0, none = 0, tosses = 0;
    double tSum = 0, vSum = 0, qbMove = 0;
    for (const OffPlay& play : OFF_PLAYS()) {
      // (option plays - a zone read the quarterback keeps, an RPO he throws - have their own test)
      if (play.type != PlayType::Run || !play.run || play.run->carrier == "QB" || !play.run->read.empty() || !formationById(play.formation)) continue;
      for (int rep = 0; rep < 3; rep++) {
        auto g = scrimmage(9000 + static_cast<uint32_t>(n), (n * 2) % 32, (n * 2 + 1) % 32);
        auto sim = g->makePlay(play, *defById(n % 3 ? "base_c3" : "nk_c3"));
        Ent* rb = sim->offRole(play.run->carrier);
        Ent* qb = sim->qb;
        n++;
        if (!rb || !qb) continue;
        const bool toss = sim->mesh().toss;
        tosses += toss;
        double tAt = -1, faceOff = 0, apart = 0, v = 0;
        V2 qbAt{0, 0};
        while (sim->isLive() && sim->t < 6) {
          const double face = qb->face, d = dist(rb->pos, qb->pos);
          const V2 qp = qb->pos, bp = rb->pos;
          sim->step(SIM_DT);
          if (tAt < 0 && sim->ball.holder == rb) {
            tAt = sim->t, apart = d, v = rb->speed(), qbAt = qb->pos;
            faceOff = std::fabs(std::remainder(std::atan2(bp.y - qp.y, bp.x - qp.x) - face, 2 * kPi)) * 180 / kPi;
          }
          if (tAt >= 0 && sim->t >= tAt + 1.2) break;
        }
        if (tAt < 0) {  // (the quarterback run down before the mesh: rare, and never the rule)
          none++;
          continue;
        }
        tSum += tAt, vSum += v, qbMove += dist(qb->pos, qbAt);
        check(tAt > 0.5 && tAt < 2.2, play.id + ": the ball changed hands " + std::to_string(tAt) + " s after the snap");
        check(toss ? apart < 6.5 : apart < 2.05, play.id + ": changed hands " + std::to_string(apart) + " yd apart");
        if (!toss) check(faceOff < 60 || tAt > sim->mesh().T + 0.3, play.id + ": the quarterback " + std::to_string(faceOff) + " deg off his back at the exchange");
        if (sim->isLive()) sim->runToEnd();
      }
    }
    check(n > 300, "too few handoff plays: " + std::to_string(n));
    check(none * 100 < n, std::to_string(none) + " of " + std::to_string(n) + " snaps without an exchange");
    const int got = (std::max)(1, n - none);
    std::printf("       (%d snaps, %d tosses: the ball changes hands %.2f s after the snap on average, the back at %.1f yd/s; the quarterback carries his fake %.1f yd)\n", n, tosses,
                tSum / got, vSum / got, qbMove / got);
    check(tSum / got > 0.8 && tSum / got < 1.25, "mean exchange time " + std::to_string(tSum / got));
    check(vSum / got > 4.0, "the back takes it at " + std::to_string(vSum / got) + " yd/s on average (he stood for it at 1.4 from the gun)");
    check(qbMove / got > 1.0, "the quarterback stands after the exchange (" + std::to_string(qbMove / got) + " yd in 1.2 s)");
  });

  t("run exchange: a user on the quarterback is carried through the mesh and has the back the tick the ball is his", [&] {
    for (const char* id : {"dbl_iz", "ace_iz", "gun_trips__counter"}) {
      const OffPlay* play = playById(id);
      check(play != nullptr, std::string("no play ") + id);
      if (!play) continue;
      auto g = scrimmage(9700, 4, 5);
      auto sim = g->makePlay(*play, *defById("base_c3"), Side::Off, false);
      Ent* qb = sim->qb;
      Ent* rb = sim->offRole(play->run->carrier);
      check(sim->user == qb, std::string(id) + ": the user starts on the quarterback");
      // the stick held hard toward the sideline the whole way: it must not walk him off the mesh
      sim->input.move = V2{0, 1};
      // (stepped until the tick the ball changes hands: the user must be on the back at the end of that same tick)
      while (sim->isLive() && sim->t < 3 && !sim->handedOff) sim->step(SIM_DT);
      const bool switched = sim->user == rb && rb->userControlled && !qb->userControlled;
      check(sim->ball.holder == rb, std::string(id) + ": the back did not get the ball with a user on the quarterback");
      check(switched, std::string(id) + ": the user is not on the back in the tick he has the ball");
      // and the stick has him at once: the next ticks go the stick's way
      const double y0 = rb->pos.y;
      for (int k = 0; k < 20 && sim->isLive(); k++) sim->step(SIM_DT);
      check(!sim->isLive() || rb->vel.y > 0.5 || rb->pos.y > y0 + 0.1, std::string(id) + ": the back ignores the stick after the handoff");
    }
  });

  // place kicks (Sim/PlaceKick.cpp): a kick from `dist` yd (33: an extra point) in a fresh game, played to its end
  auto placeKick = [&](int dist, uint32_t seed, Game*& gOut) {
    static std::unique_ptr<Game> g;
    GameOptions o;
    o.seed = seed;
    g = std::make_unique<Game>(*league[seed % 32], *league[(seed * 7 + 3) % 32 == seed % 32 ? (seed + 1) % 32 : (seed * 7 + 3) % 32], o);
    const bool pat = dist == 33;
    g->phase = pat ? GamePhase::Pat : GamePhase::Scrimmage;
    g->ballOn = pat ? 85 : 100 - (dist - 17);
    g->down = 4;
    g->toGo = 4;
    gOut = g.get();
    return g->placeKickSim(pat);
  };

  t("place kicks: the snap, the hold and the foot on time, the line engaged, nobody standing still", [&] {
    int kicks = 0, early = 0, lineMen = 0, lineEngaged = 0, idle = 0, men = 0;
    for (uint32_t s = 1; s <= 300; s++) {
      Game* g = nullptr;
      auto sim = placeKick(s % 3 == 0 ? 33 : 25 + static_cast<int>(s % 30), s, g);
      std::map<const Ent*, double> firstEng, moved;
      double snapAt = -1, kickAt = -1;
      while (sim->isLive() && sim->t < 30) {
        sim->step(SIM_DT);
        for (Ent* e : sim->ents) {
          if ((e->engaged || !e->engagedBy.empty()) && !firstEng.count(e)) firstEng[e] = sim->t;
          if (sim->t > sim->placeSnapT() + 0.2 && sim->t < sim->placeKickT()) moved[e] += e->speed() * SIM_DT;
        }
      }
      for (const PlayEvent& ev : sim->events) {
        if (ev.type == "snap" && snapAt < 0) snapAt = ev.t;
        if (ev.type == "fgkick" && kickAt < 0) kickAt = ev.t;
      }
      check(snapAt > 0 && std::fabs(snapAt - sim->placeSnapT()) < 0.02, "no snap at its time (" + fx(snapAt) + ")");
      check(kickAt > 0, "the ball was never kicked");
      const double op = kickAt - snapAt;
      check(op > 1.15 && op < 1.62, "snap to foot " + fx(op) + " s");
      early += op < 1.2;
      check(sim->phase == SimPhase::Dead && sim->result, "the kick never ended");
      if (!sim->placeBlocked) {
        check(sim->t < sim->placeKickT() + 4.5, "an unblocked kick ran " + fx(sim->t) + " s");
        // untouched, the sim's flight is the game's kick
        if (!sim->placeTipped) check(sim->placeGood == sim->cfg.place->good, "the flown kick and the game's call disagree");
      }
      for (Ent* e : sim->ents) {
        const PlaceMan* m = sim->placeMan(e);
        check(m != nullptr, "a man with no job");
        if (m->job == PlaceJob::Guard || m->job == PlaceJob::Tackle || m->job == PlaceJob::End) {
          lineMen++;
          lineEngaged += firstEng.count(e) && firstEng[e] - snapAt <= 0.45;
        }
        if (m->job != PlaceJob::Hold && m->job != PlaceJob::Snap) {
          men++;
          idle += moved[e] < 0.15 && !firstEng.count(e);
        }
      }
      g->finishPlaceKick(*sim);
      kicks++;
    }
    check(lineEngaged >= 0.97 * lineMen, "the line engaged within 0.45 s of the snap on " + fx(100.0 * lineEngaged / lineMen, 1) + "% of its reps");
    check(idle <= 0.01 * men, fx(100.0 * idle / men, 1) + "% of the men never moved between the snap and the kick");
    check(kicks == 300 && early < 60, "operation times");
  });

  t("place kicks: blocked at NFL rates, by a hand where the ball is, the ball then loose", [&] {
    int fg = 0, fgB = 0, xp = 0, xpB = 0, defTD = 0;
    std::string firstEvents;
    for (uint32_t s = 1; s <= 6000; s++) {
      const bool pat = s % 2 == 0;
      Game* g = nullptr;
      auto sim = placeKick(pat ? 33 : 40 + static_cast<int>(s % 15), 1000 + s, g);
      bool looseSeen = false;
      while (sim->isLive() && sim->t < 30) {
        sim->step(SIM_DT);
        looseSeen = looseSeen || sim->ball.mode == BallMode::Loose;
      }
      (pat ? xp : fg)++;
      if (sim->placeBlocked) {
        (pat ? xpB : fgB)++;
        check(sim->placeBlockBy && sim->placeBlockBy->side == Side::Def, "a block by nobody on the defense");
        check(looseSeen, "a blocked kick that never came loose");
        check(sim->placeBlockX >= 0 && sim->placeBlockX < 10.5, "blocked " + fx(sim->placeBlockX, 1) + " yd past the spot");
        const SpecialResult r = g->finishPlaceKick(*sim);
        check(r.desc.find("BLOCKED") != std::string::npos, "the log does not say blocked: " + r.desc);
        defTD += r.desc.find("TOUCHDOWN") != std::string::npos || r.desc.find("TWO-POINT") != std::string::npos;
      } else {
        g->finishPlaceKick(*sim);
      }
    }
    const double fgR = 100.0 * fgB / fg, xpR = 100.0 * xpB / xp;
    // (NFL 2023: 40-54 yd ~2.6 %, extra points 0.65 %)
    check(fgR >= 1.2 && fgR <= 4.5, "40-54 yd field goals blocked " + fx(fgR) + "%");
    check(xpR >= 0.15 && xpR <= 1.5, "extra points blocked " + fx(xpR) + "%");
    std::printf("       (40-54 yd FG blocked %.2f%%, XP %.2f%%, %d run back for a score)\n", fgR, xpR, defTD);
  });

  t("place kicks: deterministic (the same game seed plays the same kick)", [&] {
    for (uint32_t s : {7u, 77u, 777u}) {
      std::string a, b;
      for (std::string* out : {&a, &b}) {
        Game* g = nullptr;
        auto sim = placeKick(44, s, g);
        sim->runToEnd(30);
        for (const PlayEvent& ev : sim->events) *out += ev.type + "@" + fx(ev.t, 3) + " ";
        *out += fx(sim->ball.pos.x, 4) + "," + fx(sim->ball.pos.y, 4);
      }
      check(a == b, "seed " + std::to_string(s) + " played two different kicks");
    }
  });

  std::printf("\n%d passed, %d failed\n", passCount, failCount);
  return failCount ? 1 : 0;
}
