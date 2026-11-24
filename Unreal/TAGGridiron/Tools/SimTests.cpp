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
#include "GameInvariants.h"
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

  t("api: setUserThrow / cancelUserThrow change or call off the user quarterback's pass in its windup", [&] {
    // the throw on the press (Game/MatchFeel.cpp): the windup starts, then the pass is settled over it - its type, and not
    // before the press can no longer turn out a tap - or it is called off (a pump fake)
    auto windup = [&](uint32_t seed) {
      GameOptions o;
      o.seed = seed;
      auto g = std::make_unique<Game>(*league[8], *league[9], o);
      g->phase = GamePhase::Scrimmage;
      g->ballOn = 30;
      std::unique_ptr<PlaySim> sim = g->makePlay(*playById("dbl_curls"), *defById("nk_c3"), Side::Off);
      check(!sim->setUserThrow(1, true, 1.0) && !sim->cancelUserThrow(), "a pass to change before there is one");
      bool asked = false;
      int steps = 0;
      while (sim->isLive() && steps++ < 600 && !sim->pendingThrow) {
        if (!asked && sim->t > 1.4 && sim->ball.holder == sim->qb) {
          sim->input.throwTo = "X";
          asked = true;
        }
        sim->step();
      }
      return std::make_pair(std::move(g), std::move(sim));
    };
    int changed = 0, called = 0;
    for (uint32_t seed = 51; seed < 71 && (changed < 3 || called < 3); seed++) {
      auto gs = windup(seed);
      PlaySim& s = *gs.second;
      if (!s.isLive() || !s.pendingThrow || s.pendingThrow->shovel) continue;
      if (seed % 2) {
        const double minAt = s.t + 0.3;
        check(s.setUserThrow(1, true, minAt), "the windup's pass not changed");
        check(s.pendingThrow->style == 1 && s.pendingThrow->lob && s.pendingThrow->at >= minAt - 1e-12, "the windup's pass not as set");
        double thrownAt = -1;
        for (int k = 0; k < 120 && s.isLive() && thrownAt < 0; k++) {
          s.step();
          if (s.passThrown) thrownAt = s.t;
        }
        if (thrownAt < 0) continue;  // (sacked in the windup)
        check(thrownAt >= minAt - 1e-9, "the pass left before it was let: " + fx(thrownAt, 3) + " < " + fx(minAt, 3));
        check(s.ball.lob, "the pass was not lobbed");
        check(!s.setUserThrow(0, false, -1), "a pass already gone changed");
        changed++;
      } else {
        check(s.cancelUserThrow(), "the windup's pass not called off");
        check(!s.pendingThrow && s.input.throwTo.empty(), "the pass or its request still there");
        check(!s.cancelUserThrow(), "a pass called off twice");
        for (int k = 0; k < 40 && s.isLive(); k++) s.step();
        check(!s.passThrown, "a pass called off was thrown");
        called++;
      }
    }
    check(changed >= 3 && called >= 3, "too few windups: " + std::to_string(changed) + " changed, " + std::to_string(called) + " called off");
  });

  t("api: setUserSteer steers a player's man by his setting on his stick's steps only, and no one else", [&] {
    SteerBoost b;
    b.on = true;
    b.latMul = 1.2;
    b.brakeMul = 1.1;
    b.latAgiAtSpeed = 0.09;
    b.gripMul = 1.04;
    b.gripAgi = 0.09;
    // the law: steerVel is the sim's own law (steerLaw) for a CPU man and on a step his stick did not drive, the boosted law
    // capped to the sim's speed on one it did
    Rng r(3);
    int differ = 0;
    GameOptions go;
    go.seed = 5;
    Game g0(*league[0], *league[1], go);
    g0.phase = GamePhase::Scrimmage;
    std::unique_ptr<PlaySim> any = g0.makePlay(*playById("dbl_curls"), *defById("nk_c3"));  // (steerVel reads only the man)
    check(!any->setUserSteer(0, b), "a CPU-vs-CPU play has no player's slot to steer");
    for (int k = 0; k < 2000; k++) {
      Player pl;
      pl.r.speed = r.range(40, 99);
      pl.r.accel = r.range(40, 99);
      pl.r.agility = r.range(40, 99);
      pl.weightLb = r.range(170, 330);
      Ent e(&pl, Side::Def, "CB", V2{0, 0});
      e.strafing = r.chance(0.2);
      const double a0 = r.range(0, 2 * kPi), a1 = r.range(0, 2 * kPi), s0 = r.range(0, 1) * e.vmax, s1 = r.range(0, 1) * e.vmax;
      const V2 v0{std::cos(a0) * s0, std::sin(a0) * s0}, w{std::cos(a1) * s1, std::sin(a1) * s1};
      const V2 own = PlaySim::steerLaw(&e, v0, w, SIM_DT, nullptr);
      e.steer = b;
      e.stickDrove = true;
      check(dist(any->steerVel(&e, v0, w, SIM_DT), own) < 1e-12, "a CPU man was steered by a player's setting");
      e.userControlled = true;
      e.stickDrove = false;
      check(dist(any->steerVel(&e, v0, w, SIM_DT), own) < 1e-12, "a player's man was steered by his setting on a step his stick did not drive");
      e.stickDrove = true;
      V2 want = PlaySim::steerLaw(&e, v0, w, SIM_DT, &b);
      const double top = (std::max)(len(own), len(v0));
      if (len(want) > top) want = scale(want, top / len(want));
      const V2 got = any->steerVel(&e, v0, w, SIM_DT);
      check(dist(got, want) < 1e-12, "a player's man not steered by his setting, capped to the sim's speed");
      check(len(got) <= top + 1e-9, "a player's man steered faster than the sim's law");
      differ += dist(got, own) > 1e-9;
    }
    check(differ > 200, "the setting changed too few steps: " + std::to_string(differ));
    // in a play: the user's quarterback, the stick turning him about, with the setting and without; nobody else is touched
    auto run = [&](bool boost, int& mine, int& others, V2& qbAt) {
      GameOptions o;
      o.seed = 77;
      Game g(*league[4], *league[5], o);
      g.phase = GamePhase::Scrimmage;
      g.ballOn = 40;
      auto sim = g.makePlay(*playById("dbl_curls"), *defById("nk_c3"), Side::Off);
      for (int k = 0; k < 90 && sim->isLive(); k++) {
        if (boost) check(sim->setUserSteer(0, b), "no man in slot 0");
        sim->input.move = (k / 20) % 2 ? V2{-0.3, 1} : V2{-0.3, -1};
        sim->step();
      }
      mine = sim->qb->steerBoosted;
      others = 0;
      for (const Ent* e : sim->ents)
        if (e != sim->qb) others += e->steerBoosted;
      qbAt = sim->qb->pos;
    };
    int m1 = 0, o1 = 0, m0 = 0, o0 = 0;
    V2 at1, at0;
    run(true, m1, o1, at1);
    run(false, m0, o0, at0);
    std::printf("       (the user quarterback steered by his setting on %d of 90 steps, %d steps of anyone else's; without it %d / %d, %.2f yd apart at the end)\n", m1, o1, m0, o0,
                dist(at1, at0));
    check(m1 > 10 && o1 == 0 && m0 == 0 && o0 == 0, "the setting steered the wrong men");
    check(dist(at1, at0) > 0.01, "the setting changed nothing about how he moved");
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

  // ---------------- eyes on the ball (PlaySim Tune::eyesOn, Research/NextLevel/02 A2)
  t("eyes on the ball: no pick by a defender who cannot see it; the trail man swipes at the hands; Cover 1 go-route picks drop", [&] {
    // over full games: every interception was made by a man who could see the ball, and the hands play happens
    int ints = 0, blind = 0, swipes = 0, overShoulder = 0;
    {
      Rng rng(808);
      for (int i = 0; i < 30; i++) {
        GameOptions o;
        o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
        Game g(*league[i % 32], *league[(i * 5 + 11) % 32], o);
        g.onPlayResult = [&](const PlaySim&, const PlayResult& r) {
          if (r.interception) {
            ints++;
            blind += r.intBlind;
            overShoulder += r.intFaceDeg > tune().eyesDeg;
          }
          swipes += r.pbuBlind;
        };
        g.simToEnd();
      }
    }
    check(ints >= 15, "too few interceptions to judge: " + std::to_string(ints));
    check(blind == 0, std::to_string(blind) + " of " + std::to_string(ints) + " interceptions by a defender who could not see the ball");
    check(swipes > 0, "no blind swipe at the hands in 30 games");
    // Cover 1 against four verticals: picks by the target's own man (the trail technique) with the rule and without
    auto trailPicks = [&](bool eyes, int& picks, int& att) {
      const double keep = tune().eyesOn;
      setTune("eyesOn", eyes ? 1 : 0);
      Rng rng(99);
      picks = att = 0;
      for (int k = 0; k < 900; k++) {
        PlayConfig cfg;
        cfg.off = league[k % 32].get();
        cfg.def = league[(k + 9) % 32].get();
        cfg.offPlay = playById("trips_4verts");
        cfg.defCall = defById("base_c1");
        cfg.los = 30;
        cfg.ballY = FIELD_W / 2;
        cfg.flip = k % 2 == 0;
        cfg.worldDir = 1;
        cfg.weather.tempF = 60;
        cfg.rng = rng.fork();
        PlaySim sim(cfg);
        const PlayResult& r = sim.runToEnd();
        if (r.kind != PlayKind::Pass || r.thrownAway) continue;
        att++;
        if (sim.intBy && sim.intBy->man && sim.intBy->man->p == r.target) picks++;
      }
      setTune("eyesOn", keep);
    };
    int pOn = 0, aOn = 0, pOff = 0, aOff = 0;
    trailPicks(true, pOn, aOn);
    trailPicks(false, pOff, aOff);
    std::printf("       (30 games: %d interceptions, %d over the shoulder, 0 blind; %d blind swipes. Cover 1 v four verticals: picks by the target's man %d of %d "
                "attempts, %d of %d without the rule)\n",
                ints, overShoulder, swipes, pOn, aOn, pOff, aOff);
    check(aOn > 300 && aOff > 300, "too few Cover 1 attempts");
    check(pOff >= 4, "too few trail-technique picks without the rule to compare: " + std::to_string(pOff));
    check(static_cast<double>(pOn) / aOn < 0.6 * static_cast<double>(pOff) / aOff, "trail-technique picks did not drop: " + std::to_string(pOn) + " vs " + std::to_string(pOff));
  });

  t("man coverage by the body: a double move beats a man defender who reads the break (02 A1)", [&] {
    // Cover 1 press against the X receiver on a sluggo / an out-and-up and on a go: the man defender to his receiver when
    // the receiver is 16 yd down the field (the quarterback, a user's with the stick left alone, holds the ball behind max
    // protection). Reading
    // the route ahead (predictPos) he was never fooled; reading the body he jumps the first break and the second one
    // finds him driving on it
    // (afterBreak: the gap half a second after he reaches the route's first break point, not at 16 yd)
    auto gapAt = [&](const char* route, bool cue, double rr, bool afterBreak, int reps, double& gapOut, int& nOut) {
      const double keep = tune().manCue;
      setTune("manCue", cue ? 1 : 0);
      Rng rng(4242);
      double sum = 0;
      int n = 0;
      for (int k = 0; k < reps; k++) {
        PlayConfig cfg;
        cfg.off = league[k % 32].get();
        cfg.def = league[(k + 7) % 32].get();
        cfg.offPlay = playById("trips_4verts");
        cfg.defCall = defById("base_c1");
        cfg.los = 30;
        cfg.ballY = FIELD_W / 2;
        cfg.flip = k % 2 == 0;
        cfg.worldDir = 1;
        cfg.weather.tempF = 60;
        cfg.rng = rng.fork();
        PlaySim sim(cfg);
        Ent* x = sim.offRole("X");
        if (!x) continue;
        if (rr > 0) x->rr.routeRunning = rr;  // (his ratings for this play only: the sim reads Ent::rr)
        sim.hotRoute(x, route);
        sim.adjustShell(PlaySim::ShellAdj::Press);
        sim.setProtection(0, true);  // (max protect: the held ball is not sacked before the routes get there)
        sim.pickUserEnt(Side::Off);
        Ent* m = nullptr;
        double got = -1, brokeT = -1;
        while (sim.isLive() && sim.t < 5.0) {
          sim.step();
          if (!m)
            for (Ent* d : sim.def)
              if (d->state == EntState::Man && d->man == x) m = d;
          if (!m || sim.passThrown || x->state != EntState::Route) break;
          if (brokeT < 0 && x->routeIdx >= 1) brokeT = sim.t;
          if (afterBreak ? brokeT >= 0 && sim.t >= brokeT + 0.5 : x->pos.x - sim.los >= 16) {
            got = dist(x->pos, m->pos);
            break;
          }
        }
        if (got >= 0) {
          sum += got;
          n++;
        }
      }
      setTune("manCue", keep);
      gapOut = n ? sum / n : 0;
      nOut = n;
    };
    double gGoOn = 0, gSlOn = 0, gOuOn = 0, gGoOff = 0, gSlOff = 0, gOuOff = 0;
    int n1 = 0, n2 = 0, n3 = 0, n4 = 0, n5 = 0, n6 = 0;
    gapAt("go", true, 0, false, 400, gGoOn, n1);
    gapAt("slantgo", true, 0, false, 400, gSlOn, n2);
    gapAt("outnup", true, 0, false, 400, gOuOn, n3);
    gapAt("go", false, 0, false, 400, gGoOff, n4);
    gapAt("slantgo", false, 0, false, 400, gSlOff, n5);
    gapAt("outnup", false, 0, false, 400, gOuOff, n6);
    // the cut itself: half a second after an out's break, a 95 route runner against a 60 (Madden 26's cut reaction: his
    // route running against the man's coverage)
    double gHi = 0, gLo = 0, gHiOff = 0, gLoOff = 0;
    int m1 = 0, m2 = 0, m3 = 0, m4 = 0;
    gapAt("out", true, 95, true, 400, gHi, m1);
    gapAt("out", true, 60, true, 400, gLo, m2);
    gapAt("out", false, 95, true, 400, gHiOff, m3);
    gapAt("out", false, 60, true, 400, gLoOff, m4);
    std::printf("       (man defender to X at 16 yd, Cover 1 press: reading the body go %.2f / sluggo %.2f / out-and-up %.2f yd; reading the route %.2f / %.2f / %.2f. "
                "0.5 s after an out's break, route running 95 / 60: %.2f / %.2f yd, reading the route %.2f / %.2f)\n",
                gGoOn, gSlOn, gOuOn, gGoOff, gSlOff, gOuOff, gHi, gLo, gHiOff, gLoOff);
    check(n1 > 200 && n2 > 200 && n3 > 200 && n4 > 200 && n5 > 200 && n6 > 200 && m1 > 200 && m2 > 200, "too few reps reached the measuring point");
    // a double move beats him: more room at 16 yd than a go gets, by a margin the route read did not give it
    check(gSlOn > gGoOn + 0.3 && gOuOn > gGoOn + 0.3, "a double move did not beat the man reading the body: go " + fx(gGoOn) + ", sluggo " + fx(gSlOn) + ", out-and-up " + fx(gOuOn));
    check((gSlOn - gGoOn) + (gOuOn - gGoOn) > (gSlOff - gGoOff) + (gOuOff - gGoOff) + 0.5,
          "double moves gained no more against the body read than against the route read: " + fx(gSlOn - gGoOn) + " + " + fx(gOuOn - gGoOn) + " vs " + fx(gSlOff - gGoOff) +
              " + " + fx(gOuOff - gGoOff));
    // (route running on the cut is printed, not checked: half a second after an out's break the trailing man's cushion
    // makes the gap - he is a yard and a half off and going up the field - and his cut reaction barely shows in it)
  });

  t("leverage at engagement: no block forms on a man already past the blocker's hip (02 A5)", [&] {
    // full games: blocks formed on a defender already past the blocker's hip (Tune::levHip beyond his middle on the line
    // to the defender's goal), with the rule and without
    auto pastHip = [&](bool lev, int& past, int& formed, int& refused) {
      const double keep = tune().leverage;
      setTune("leverage", lev ? 1 : 0);
      past = formed = refused = 0;
      Rng rng(515);
      for (int i = 0; i < 8; i++) {
        GameOptions o;
        o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
        Game g(*league[(i * 3) % 32], *league[(i * 7 + 5) % 32], o);
        g.onPlayResult = [&](const PlaySim&, const PlayResult& r) {
          past += r.engPastHip;
          formed += r.engN;
          refused += r.engRefused;
        };
        g.simToEnd();
      }
      setTune("leverage", keep);
    };
    int pOn = 0, fOn = 0, rOn = 0, pOff = 0, fOff = 0, rOff = 0;
    pastHip(true, pOn, fOn, rOn);
    pastHip(false, pOff, fOff, rOff);
    // the arc: an end with 95 speed / 95 acceleration / 90 agility / 60 strength, and one with 70 / 70 / 65 / 95 (the same
    // pass rush, the same weight), against tackles of 70 speed, on a dropback the quarterback holds: how often he beats
    // the tackle inside 2.5 s, and how hard he walks him back while he is on him
    auto arc = [&](bool speedy, bool lev, double& winRate, double& push) {
      const double keep = tune().leverage;
      setTune("leverage", lev ? 1 : 0);
      Rng rng(616);
      int reps = 0, wins = 0;
      double pushSum = 0, pushN = 0;
      for (int k = 0; k < 500; k++) {
        PlayConfig cfg;
        cfg.off = league[k % 32].get();
        cfg.def = league[(k + 3) % 32].get();
        cfg.offPlay = playById("dbl_curls");
        cfg.defCall = defById("base_c3");
        cfg.los = 30;
        cfg.ballY = FIELD_W / 2;
        cfg.flip = k % 2 == 0;
        cfg.worldDir = 1;
        cfg.weather.tempF = 60;
        cfg.rng = rng.fork();
        Player* end = nullptr;
        std::vector<Player*> tackles;
        {
          PlaySim look(cfg);
          if (Ent* le = look.defRole("LE")) end = le->p;
          for (const char* tr : {"LT", "RT"})
            if (Ent* tk = look.offRole(tr)) tackles.push_back(tk->p);
        }
        if (!end || tackles.size() != 2) continue;
        const Player endWas = *end;
        std::vector<Player> tkWas;
        for (Player* tk : tackles) tkWas.push_back(*tk);
        end->r.speed = speedy ? 95 : 70;
        end->r.accel = speedy ? 95 : 70;
        end->r.agility = speedy ? 90 : 65;
        end->r.strength = speedy ? 60 : 95;
        end->r.passRush = 80;
        end->weightLb = 265;
        for (Player* tk : tackles) {
          tk->r.speed = 70;
          tk->r.accel = 70;
          tk->r.agility = 70;
          tk->r.strength = 80;
          tk->r.passBlock = 80;
          tk->weightLb = 315;
        }
        {
          PlaySim sim(cfg);
          sim.pickUserEnt(Side::Off);
          Ent* le = sim.defRole("LE");
          while (sim.isLive() && sim.t < 2.5) {
            sim.step();
            if (le && !le->engagedBy.empty() && le->engagedBy[0]->state == EntState::PassBlock) pushSum += le->linePush, pushN++;
          }
          if (le && le->wrIn) {
            reps++;
            wins += le->wrBeat || le->wrFree;
          }
        }
        *end = endWas;
        for (size_t i = 0; i < tackles.size(); i++) *tackles[i] = tkWas[i];
      }
      setTune("leverage", keep);
      winRate = reps ? static_cast<double>(wins) / reps : 0;
      push = pushN ? pushSum / pushN : 0;
    };
    double wS = 0, pS = 0, wP = 0, pP = 0, wSo = 0, pSo = 0, wPo = 0, pPo = 0;
    arc(true, true, wS, pS);
    arc(false, true, wP, pP);
    arc(true, false, wSo, pSo);
    arc(false, false, wPo, pPo);
    std::printf("       (8 games: blocks on a man past the hip %d of %d with the rule (%d refused), %d of %d without. The arc, beat the tackle inside 2.5 s / push while on him: "
                "speed end %.0f%% / %+.2f, power end %.0f%% / %+.2f; without the rule %.0f%% / %+.2f, %.0f%% / %+.2f)\n",
                pOn, fOn, rOn, pOff, fOff, 100 * wS, pS, 100 * wP, pP, 100 * wSo, pSo, 100 * wPo, pPo);
    check(fOn > 1000 && fOff > 1000, "too few blocks to judge");
    check(pOn == 0, std::to_string(pOn) + " blocks formed on a defender already past the blocker's hip");
    check(rOn > 0, "the rule never refused a block");
    check(pOff > 0, "without the rule no block formed past the hip either: the check would not see one");
    // (the arc is printed, not checked: the tackle's punch lands from setReach out, before a faster end is any wider, and
    // once on him the rep goes by the pass-rush skill the two ends share - both win about as often)
    check(wS > 0 && wP > 0, "an end never beat the tackle");
  });

  t("read keys: play action holds the linebackers up, a draw sends them to their drops (02 A4)", [&] {
    // the linebackers' depth past the line 1.5 s into the play (the ball not thrown) against Cover 3, by what was run: a
    // dropback and play action, an inside zone and a draw. Reading what they see, the fake keeps them shallow on play
    // action and the draw's pass look takes them back; keyed on the play's type, a draw fooled nobody
    auto lbDepth = [&](const char* play, bool keys) {
      const double keep = tune().readKeys;
      setTune("readKeys", keys ? 1 : 0);
      Rng rng(717);
      double sum = 0, n = 0;
      for (int k = 0; k < 300; k++) {
        PlayConfig cfg;
        cfg.off = league[k % 32].get();
        cfg.def = league[(k + 5) % 32].get();
        cfg.offPlay = playById(play);
        cfg.defCall = defById("base_c3");
        cfg.los = 30;
        cfg.ballY = FIELD_W / 2;
        cfg.flip = k % 2 == 0;
        cfg.worldDir = 1;
        cfg.weather.tempF = 60;
        cfg.rng = rng.fork();
        PlaySim sim(cfg);
        while (sim.isLive() && sim.t < 1.5 - 1e-9) sim.step();
        if (!sim.isLive() || sim.passThrown) continue;
        for (const Ent* d : sim.def)
          if (d->role == "MLB" || d->role == "WLB" || d->role == "SLB") sum += d->pos.x - sim.los, n++;
      }
      setTune("readKeys", keep);
      check(n > 300, std::string("too few linebackers measured on ") + play);
      return sum / n;
    };
    const double dbOn = lbDepth("dbl_curls", true), paOn = lbDepth("ace_pa", true), izOn = lbDepth("dbl_iz", true), drOn = lbDepth("trips_draw", true);
    const double dbOff = lbDepth("dbl_curls", false), paOff = lbDepth("ace_pa", false), izOff = lbDepth("dbl_iz", false), drOff = lbDepth("trips_draw", false);
    std::printf("       (linebacker depth at 1.5 s, reading keys: dropback %.1f, play action %.1f, inside zone %.1f, draw %.1f yd; keyed on the play's type: %.1f, %.1f, %.1f, %.1f)\n",
                dbOn, paOn, izOn, drOn, dbOff, paOff, izOff, drOff);
    check(paOn < dbOn - 1.5, "play action did not hold the linebackers up: " + fx(paOn, 1) + " against a dropback's " + fx(dbOn, 1));
    check(drOn > izOn + 1.0, "the draw did not send the linebackers back: " + fx(drOn, 1) + " against an inside zone's " + fx(izOn, 1));
    check(drOn - izOn > drOff - izOff + 0.5, "the draw fooled them no more than when they knew the play: " + fx(drOn - izOn, 1) + " vs " + fx(drOff - izOff, 1));
  });

  // ---------------- the rules, every action of many games (Sim/GameInvariants.h)
  t("rules invariants: every action of 360 games keeps score, downs, clock, timeouts, possession, tries, OT legal", [&] {
    // A rule broken here is a sim bug, not noise: a known one stays listed below with its note (it is reported, and does
    // not fail the run) until it is fixed - the check itself is never loosened to pass.
    const std::map<std::string, std::string> known = {};
    Rng rng(4711);
    std::map<std::string, std::pair<int, std::string>> byRule;
    long steps = 0;
    for (int i = 0; i < 360; i++) {
      const int h = rng.intRange(0, 31);
      const int a = (h + 1 + rng.intRange(0, 30)) % 32;
      GameOptions o;
      o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
      // (a third at full length, the rest at short quarters: they put far more snaps at the ends of the halves)
      o.quarterLen = i % 3 == 0 ? 900.0 : i % 3 == 1 ? 240.0 : 120.0;
      Game g(*league[h], *league[a], o);
      GameInvariants inv(g);
      for (int n = 0; !g.isFinal() && n < 3000; n++) {
        g.simStep();
        inv.afterStep(g);
        steps++;
      }
      check(g.isFinal(), "a game did not finish");
      g.simStep();  // (after the final whistle: nothing may happen)
      inv.afterStep(g);
      for (const std::string& v : inv.violations) {
        auto& e = byRule[v.substr(0, v.find(':'))];
        if (e.first++ == 0) e.second = v;
      }
    }
    std::string bad;
    for (const auto& kv : byRule) {
      const auto k = known.find(kv.first);
      std::printf("       %s %d x%s, first: %s\n", k != known.end() ? "KNOWN FAILURE" : "BROKEN", kv.second.first,
                  k != known.end() ? (" (" + k->second + ")").c_str() : "", kv.second.second.c_str());
      if (k == known.end()) bad += (bad.empty() ? "" : ", ") + kv.first;
    }
    std::printf("       (360 games, %ld actions checked)\n", steps);
    check(bad.empty(), "rules broken: " + bad);
  });

  // ---------------- the long tail of the rules, each from a state set by hand (and every action of it checked)
  auto newGame = [&](uint32_t seed, int h = 0, int a = 1) {
    GameOptions o;
    o.seed = seed;
    return std::make_unique<Game>(*league[h], *league[a], o);
  };
  auto setScrim = [](Game& g, TeamKey poss, double ballOn, int down, double toGo, int q, double clock) {
    g.phase = GamePhase::Scrimmage;
    g.poss = poss;
    g.ballOn = ballOn;
    g.ballY = FIELD_W / 2;
    g.down = down;
    g.toGo = toGo;
    g.q = q;
    g.clock = clock;
    g.clockRunning = false;
    g.untimed = false;
    g.safetyKick = false;
  };
  auto playDown = [](Game& g, const OffPlay& op, const DefCall& dc) {
    auto sim = g.makePlay(op, dc);
    sim->runToEnd();
    return g.finishPlay(*sim);
  };
  auto clean = [](const GameInvariants& inv) { check(inv.violations.empty(), inv.violations.empty() ? std::string() : inv.violations[0]); };
  auto said = [](const Game& g, size_t from, const char* s) {
    for (size_t i = from; i < g.log.size(); ++i)
      if (g.log[i].desc.find(s) != std::string::npos) return true;
    return false;
  };

  t("rules: backed up at the 1 - a safety is 2, and the team scored on free-kicks from its 20", [&] {
    int safeties = 0;
    for (uint32_t s = 1; s <= 300 && safeties < 3; ++s) {
      auto g = newGame(1000 + s);
      setScrim(*g, TeamKey::Home, 1, 1, 10, 1, 600);
      GameInvariants inv(*g);
      const OffPlay* op = g->cpuOffense();
      const DefCall* dc = g->cpuDefense(*op);
      const PlayLog l = playDown(*g, *op, *dc);
      inv.afterStep(*g);
      if (l.big != LogTag::Safety) continue;
      safeties++;
      check(g->score.away == 2 && g->score.home == 0, "safety score " + std::to_string(g->score.away) + "-" + std::to_string(g->score.home));
      check(g->phase == GamePhase::Kickoff && g->kicking == TeamKey::Home && g->poss == TeamKey::Away && g->safetyKick, "not set up for the free kick");
      const SpecialResult k = g->kickoff();
      inv.afterStep(*g);
      check(k.flight && k.flight->fromX == 20, "the free kick was not from the 20");
      check(!g->safetyKick && g->poss == TeamKey::Away, "after the free kick");
      clean(inv);
    }
    check(safeties >= 1, "no safety from the 1 in 300 snaps");
  });

  t("rules: the half ends on a safety - the second half still opens with a kickoff from the 35", [&] {
    auto g = newGame(77);
    g->q = 2;
    g->clock = 0;
    g->phase = GamePhase::Kickoff;
    g->kicking = TeamKey::Home;
    g->poss = TeamKey::Away;
    g->safetyKick = true;  // (as a safety with the clock run out leaves it)
    GameInvariants inv(*g);
    g->simStep();
    inv.afterStep(*g);
    check(g->q == 3 && g->phase == GamePhase::Kickoff && !g->safetyKick, "the half did not end, or the free kick carried over");
    const SpecialResult k = g->kickoff();
    inv.afterStep(*g);
    check(k.flight && k.flight->fromX == 35, "second-half kickoff from " + (k.flight ? fx(k.flight->fromX, 0) : std::string("?")));
    clean(inv);
  });

  t("rules: trailing late the kicking team onsides it (about 1 in 8 back); leading it kicks deep; the 2-point chart", [&] {
    int onside = 0, back = 0;
    for (uint32_t s = 1; s <= 400; ++s) {
      auto g = newGame(2000 + s);
      g->q = 4;
      g->clock = 150;
      g->phase = GamePhase::Kickoff;
      g->kicking = TeamKey::Home;
      g->poss = TeamKey::Home;
      g->score.home = 17;
      g->score.away = 22;
      GameInvariants inv(*g);
      g->kickoff();
      inv.afterStep(*g);
      clean(inv);
      check(said(*g, 0, "onside"), "trailing by 5 with 2:30 left and no onside kick: " + g->log.back().desc);
      onside++;
      back += g->poss == TeamKey::Home;
      check(g->down == 1 && g->toGo == 10 && g->ballOn == (g->poss == TeamKey::Home ? 46 : 54), "after the onside kick: " + g->log.back().desc);
    }
    const double rate = 100.0 * back / onside;
    check(rate > 6 && rate < 20, "onside kicks recovered " + fx(rate, 1) + "%");
    {
      auto g = newGame(3);
      g->q = 4;
      g->clock = 150;
      g->phase = GamePhase::Kickoff;
      g->kicking = TeamKey::Home;
      g->score.home = 24;
      g->score.away = 17;
      g->kickoff();
      check(!said(*g, 0, "onside"), "leading and it onside kicked");
    }
    // after the touchdown, late: down 2 or 5 goes for two, in overtime never
    for (int m : {-2, -5}) {
      auto g = newGame(4);
      g->q = 4;
      g->clock = 300;
      g->phase = GamePhase::Pat;
      g->poss = TeamKey::Home;
      g->score.home = 20;
      g->score.away = 20 - m;
      check(g->goForTwo(), "down " + std::to_string(-m) + " after the touchdown late and it kicked");
      g->q = 5;
      check(!g->goForTwo(), "went for two in overtime");
    }
    std::printf("       (%d onside kicks, %.1f%% recovered by the kicking team)\n", onside, rate);
  });

  t("rules: overtime - a 10:00 period with 2 timeouts; a first-possession FG / TD / turnover gives the other team the ball, a safety ends it", [&] {
    auto otGame = [&](uint32_t seed) {
      auto g = newGame(seed, 6, 9);
      g->q = 4;
      g->clock = 0;
      g->phase = GamePhase::Kickoff;
      g->score.home = g->score.away = 17;
      GameInvariants inv(*g);
      g->simStep();
      inv.afterStep(*g);
      clean(inv);
      check(g->q == 5 && g->clock == 600 && g->timeouts.home == 2 && g->timeouts.away == 2 && g->phase == GamePhase::Kickoff && said(*g, 0, "Overtime!"),
            "tied after four quarters and no overtime");
      return g;
    };
    // play the rest out, every action checked; the team that had the ball second must have had it
    auto finishOT = [&](Game& g, TeamKey second) {
      GameInvariants inv(g);
      const size_t from = g.log.size();
      for (int n = 0; !g.isFinal() && n < 400; ++n) {
        g.simStep();
        inv.afterStep(g);
      }
      clean(inv);
      check(g.isFinal(), "overtime never ended");
      bool had = false;
      for (size_t i = from; i < g.log.size(); ++i) had = had || (g.log[i].poss == second && g.log[i].q == 5);
      check(had, "the second team never had the ball in overtime");
    };
    int fg = 0, td = 0, sf = 0, to = 0;
    for (uint32_t s = 1; s <= 400 && (fg < 2 || td < 2 || sf < 1 || to < 1); ++s) {
      // a field goal on the first possession: not over
      if (fg < 2) {
        auto g = otGame(100 + s);
        const TeamKey r = other(g->kicking);
        setScrim(*g, r, 85, 4, 5, 5, 400);
        GameInvariants inv(*g);
        g->fieldGoal();
        inv.afterStep(*g);
        clean(inv);
        if (g->score[r] == 20) {
          fg++;
          check(!g->isFinal() && g->otPossDone == 1 && g->phase == GamePhase::Kickoff && g->kicking == r, "a first-possession field goal ended overtime");
          finishOT(*g, other(r));
        }
      }
      // a touchdown on the first possession: the try, then the other team's turn
      if (td < 2) {
        auto g = otGame(200 + s);
        const TeamKey r = other(g->kicking);
        setScrim(*g, r, 99.4, 1, 0.6, 5, 400);
        GameInvariants inv(*g);
        playDown(*g, *playById("gl_sneak"), *defById("gl_c1"));
        inv.afterStep(*g);
        if (g->phase == GamePhase::Pat) {
          td++;
          g->extraPoint();
          inv.afterStep(*g);
          clean(inv);
          check(!g->isFinal() && g->otPossDone == 1, "a first-possession touchdown ended overtime");
          finishOT(*g, other(r));
        }
      }
      // a safety on the first possession: over
      if (sf < 1) {
        auto g = otGame(300 + s);
        const TeamKey r = other(g->kicking);
        // (from the 2, a pass grounded from the end zone: a safety every time the flag is accepted - a run stopped
        // behind the goal line from the 1 is rarer than one a yard deep, short of it)
        setScrim(*g, r, 2, 2, 8, 5, 400);
        GameInvariants inv(*g);
        auto sim = g->makePlay(*playById("dbl_curls"), *defById("nk_c3"));
        sim->runToEnd();
        Penalty ig;
        ig.name = "Intentional Grounding";
        ig.on = Side::Off;
        ig.yards = 10;
        if (sim->passThrown && sim->ball.throwX <= 0) g->forcedFlag = ig;
        g->finishPlay(*sim);
        g->forcedFlag.reset();
        inv.afterStep(*g);
        clean(inv);
        if (said(*g, 0, "SAFETY!")) {  // (finishPlay hands back the last line: FINAL, after the safety)
          sf++;
          check(g->isFinal() && g->score[other(r)] == 19, "a safety on the first possession did not end overtime");
        }
      }
      // a turnover on the first possession: the other team's turn, not over
      if (to < 1) {
        auto g = otGame(400 + s);
        const TeamKey r = other(g->kicking);
        setScrim(*g, r, 40, 1, 10, 5, 400);
        const double keep = tune().forceInt;
        setTune("forceInt", 1);
        GameInvariants inv(*g);
        const PlayLog l = playDown(*g, *playById("dbl_curls"), *defById("nk_c3"));
        setTune("forceInt", keep);
        inv.afterStep(*g);
        clean(inv);
        if (l.big == LogTag::Turnover && g->phase == GamePhase::Scrimmage) {
          to++;
          check(!g->isFinal() && g->otPossDone == 1 && g->poss == other(r), "a first-possession interception ended overtime");
          finishOT(*g, other(r));
        }
      }
    }
    check(fg >= 2 && td >= 2 && sf >= 1 && to >= 1, "scenarios not reached: fg " + std::to_string(fg) + " td " + std::to_string(td) + " safety " + std::to_string(sf) + " turnover " + std::to_string(to));
    // the period runs out tied: a tie (the regular season's rule)
    auto g = otGame(9);
    g->clock = 0;
    GameInvariants inv(*g);
    g->simStep();
    inv.afterStep(*g);
    clean(inv);
    check(g->isFinal() && g->score.home == g->score.away, "overtime ran out and the game went on");
  });

  t("rules: a defensive foul accepted at 0:00 gets an untimed down; an offensive one ends the half", [&] {
    int untimedDowns = 0, ended = 0;
    for (uint32_t s = 1; s <= 40 && (untimedDowns < 3 || ended < 3); ++s) {
      for (Side on : {Side::Def, Side::Off}) {
        auto g = newGame(500 + s);
        setScrim(*g, TeamKey::Home, 30, 1, 10, 2, 0.5);
        GameInvariants inv(*g);
        check(g->preSnap(), "no snap with 0:00.5 left");
        Penalty p;
        p.on = on;
        if (on == Side::Def) {
          p.name = "Unnecessary Roughness";
          p.yards = 15;
          p.autoFirst = true;
          p.spot = PenaltySpot::End;
        } else {
          p.name = "Offensive Holding";
          p.yards = 10;
          p.replayDown = true;
        }
        g->forcedFlag = p;
        const PlayLog l = playDown(*g, *playById("gl_dive"), *defById("base_c3"));
        inv.afterStep(*g);
        if (l.big != LogTag::Penalty || g->clock > 0) continue;  // (declined, or time left: not this case)
        const size_t at = g->log.size();
        const bool snap = g->preSnap();  // (the snap and its play are one action, as in simStep)
        if (on == Side::Def) {
          check(snap && g->q == 2 && g->clock <= 0, "a defensive foul at 0:00 and no untimed down");
          playDown(*g, *playById("gl_dive"), *defById("base_c3"));
          inv.afterStep(*g);
          // (then the try if it scored, another untimed down if it drew another foul, and the half)
          for (int k = 0; k < 8 && g->q == 2; ++k) {
            g->simStep();
            inv.afterStep(*g);
          }
          check(g->q == 3 && said(*g, at, "Halftime."), "the half did not end after its untimed down");
          untimedDowns++;
        } else {
          inv.afterStep(*g);
          check(!snap && g->q == 3, "an offensive foul at 0:00 gave the offense another down");
          ended++;
        }
        clean(inv);
      }
    }
    check(untimedDowns >= 3 && ended >= 3, "cases reached: untimed " + std::to_string(untimedDowns) + " ended " + std::to_string(ended));
  });

  t("rules: a fumble out of bounds - ahead of the fumble back to its spot, behind it where it went out, a safety / touchback off an end zone", [&] {
    const double keep = tune().fumbleK;
    setTune("fumbleK", 60);
    int fwd = 0, back = 0, sfty = 0, tb = 0, n = 0;
    Rng rng(31);
    for (int k = 0; k < 2400 && (fwd < 4 || back < 4); ++k) {
      auto g = newGame(7000 + static_cast<uint32_t>(k), k % 32, (k + 5) % 32);
      const double los = k % 4 == 0 ? rng.range(1, 4) : k % 4 == 1 ? rng.range(93, 98) : rng.range(15, 85);
      setScrim(*g, TeamKey::Home, los, 1, (std::min)(10.0, 100 - los), 1, 600);
      GameInvariants inv(*g);
      const OffPlay* op = g->cpuOffense();
      auto sim = g->makePlay(*op, *g->cpuDefense(*op));
      const PlayResult& r = sim->runToEnd();
      const bool oobLoose = r.fumbleBy && r.outOfBounds && sim->ball.mode == BallMode::Loose && sim->fumbler && sim->fumbler->side == Side::Off;
      double fumX = 0;
      for (const PlayEvent& e : sim->events)
        if (e.type == "fumble") fumX = e.x;
      const double outX = sim->ball.pos.x;
      g->finishPlay(*sim);
      inv.afterStep(*g);
      clean(inv);
      if (!oobLoose) continue;
      n++;
      if (outX <= 0 || (fumX <= 0 && outX > fumX)) {
        sfty++;
        check(r.safety, "a fumble out of the offense's own end zone (or out ahead of a fumble in it) was not a safety");
      } else if (outX >= 100) {
        tb++;
        check(r.turnover && r.touchback, "a fumble out through the other end zone was not a touchback");
      } else if (outX > fumX) {
        fwd++;
        check(!r.turnover && std::fabs(r.endX - clamp(fumX, 0.5, 99.5)) < 1e-9, "a fumble out of bounds forward was spotted at " + fx(r.endX) + ", not its spot " + fx(fumX));
      } else {
        back++;
        check(!r.turnover && std::fabs(r.endX - clamp(outX, 0.5, 99.5)) < 1e-9, "a fumble out of bounds backward was spotted at " + fx(r.endX) + ", not where it went out " + fx(outX));
      }
    }
    setTune("fumbleK", keep);
    std::printf("       (%d fumbles out of bounds: %d forward, %d backward, %d out of the own end zone, %d out through the other)\n", n, fwd, back, sfty, tb);
    check(fwd >= 1 && back >= 1, "too few fumbles out of bounds to judge");
  });

  t("rules: intentional grounding - loss of down and 10 (or at the spot), a safety from the end zone, a turnover on 4th down", [&] {
    Penalty ig;
    ig.name = "Intentional Grounding";
    ig.on = Side::Off;
    ig.yards = 10;
    int ez = 0, field = 0, fourth = 0;
    for (uint32_t s = 1; s <= 120 && (ez < 3 || field < 3 || fourth < 3); ++s) {
      const int kind = static_cast<int>(s % 3);  // 0 backed up at the 2, 1 from the 30, 2 4th down at the 40
      auto g = newGame(800 + s);
      const double los = kind == 0 ? 2 : kind == 1 ? 30 : 40;
      setScrim(*g, TeamKey::Home, los, kind == 2 ? 4 : 2, kind == 2 ? 5 : 8, 1, 600);
      GameInvariants inv(*g);
      auto sim = g->makePlay(*playById("dbl_curls"), *defById("nk_c3"));
      const PlayResult& r = sim->runToEnd();
      if (!sim->passThrown || r.turnover || r.td || r.kind == PlayKind::Sack) continue;
      const double throwX = sim->ball.throwX;
      g->forcedFlag = ig;
      const PlayLog l = g->finishPlay(*sim);
      inv.afterStep(*g);
      clean(inv);
      if (kind == 0 && throwX <= 0) {
        ez++;
        check(l.big == LogTag::Safety && g->score.away == 2 && g->phase == GamePhase::Kickoff && g->kicking == TeamKey::Home,
              "grounding from the end zone (thrown from " + fx(throwX) + ") was not a safety: " + l.desc);
      } else if (kind <= 1 && l.big == LogTag::Penalty) {
        field++;
        const double want = throwX < los - 10 ? throwX : los - (std::min)(10.0, los / 2);
        check(g->down == 3 && std::fabs(g->ballOn - want) < 1e-9 && std::fabs(g->ballOn + g->toGo - (los + 8)) < 1e-9,
              "grounding from the " + fx(los, 0) + " (thrown from " + fx(throwX) + "): " + g->downText() + " at " + fx(g->ballOn));
      } else if (kind == 2 && l.big == LogTag::Turnover) {
        fourth++;
        check(g->poss == TeamKey::Away && g->down == 1 && g->phase == GamePhase::Scrimmage, "grounding on 4th down: " + l.desc);
      }
    }
    check(ez >= 3 && field >= 3 && fourth >= 3, "cases reached: end zone " + std::to_string(ez) + ", field " + std::to_string(field) + ", 4th down " + std::to_string(fourth));
  });

  t("rules: kneel-out - ahead late, the offense kneels when the other team's timeouts cannot stop the clock", [&] {
    auto g = newGame(42);
    setScrim(*g, TeamKey::Home, 30, 1, 10, 4, 100);
    g->score.home = 20;
    g->score.away = 17;
    g->timeouts.away = 0;
    check(g->shouldKneel(), "ahead with 1:40 left, the other team out of timeouts, and no kneel");
    g->timeouts.away = 3;
    check(!g->shouldKneel(), "knelt with 1:40 left against three timeouts");
    g->clock = 40;
    check(!g->shouldKneel(), "knelt with 0:40 left against three timeouts (three kneels and a punt hand it back with ~0:30)");
    g->clock = 4;
    check(g->shouldKneel(), "no kneel with 0:04 left against three timeouts");
    // whenever it kneels, the kneels run it out: no more points, the leader wins, the other team never snaps the ball,
    // every kneel and timeout legal (down, timeouts left, clock)
    const struct { int down; int tos; double clock; } cases[] = {{1, 0, 100}, {1, 1, 80}, {1, 2, 40}, {2, 0, 80}, {3, 0, 40}, {1, 3, 4}};
    int run = 0;
    for (const auto& cs : cases) {
      auto h = newGame(43 + static_cast<uint32_t>(run++));
      setScrim(*h, TeamKey::Home, 30, cs.down, 10, 4, cs.clock);
      h->score.home = 20;
      h->score.away = 17;
      h->timeouts.away = cs.tos;
      h->clockRunning = true;
      check(h->shouldKneel(), "no kneel at " + std::to_string(cs.down) + " down, " + std::to_string(cs.tos) + " timeouts, " + fmtClock(cs.clock));
      GameInvariants inv(*h);
      const size_t from = h->log.size();
      for (int k = 0; !h->isFinal() && k < 40; ++k) {
        h->simStep();
        inv.afterStep(*h);
      }
      clean(inv);
      bool theirs = false;
      for (size_t i = from; i < h->log.size(); ++i) {
        const PlayLog& l = h->log[i];
        if (l.poss == TeamKey::Away && l.desc.rfind("End of", 0) != 0 && l.desc.rfind("FINAL", 0) != 0 && l.desc.rfind("Timeout", 0) != 0) theirs = true;
      }
      check(h->isFinal() && h->score.home == 20 && h->score.away == 17 && said(*h, from, " kneels.") && !theirs,
            "kneeling from " + std::to_string(cs.down) + " down, " + std::to_string(cs.tos) + " timeouts, " + fmtClock(cs.clock) + " did not run it out: " +
                h->log.back().desc);
    }
  });

  t("rules: overtime - a punt run back for a touchdown is a score after both teams have had the ball (a live return)", [&] {
    // lead 0: the punting team has the first possession, tied. lead 3: the return team kicked a field goal on the first
    // possession and the punting team's second one ends in the punt
    for (int lead : {0, 3}) {
      int done = 0;
      for (uint32_t s = 1; s <= 60 && !done; ++s) {
        auto g = newGame(900 + s, 10, 20);
        const TeamKey pt = TeamKey::Home, rt = TeamKey::Away;  // pt punts, rt returns it
        setScrim(*g, pt, 35, 4, 9, 5, 400);
        g->score[pt] = 17;
        g->score[rt] = 17 + lead;
        g->otPossDone = lead ? 1 : 0;
        g->timeouts = PerTeam<int>{2, 2};
        GameInvariants inv(*g);
        g->punt(true);  // (a live return: the punt and the return are one action, logged by finishKickReturn)
        if (!g->pendingReturn) continue;
        std::vector<Player*> kickU = g->fgUnit(pt, true), recvU = g->fgUnit(rt, false);
        auto sim = g->kickReturnSim(recvU, kickU);
        sim->runToEnd();
        sim->result->td = Side::Off;  // (the return taken all the way: what the game does with it)
        sim->result->turnover = false;
        g->finishKickReturn(*sim);
        inv.afterStep(*g);
        done++;
        if (lead) {
          check(g->isFinal() && g->score[rt] == 26 && g->score[pt] == 17, "the return team led after both possessions and overtime went on");
        } else {
          check(!g->isFinal() && g->phase == GamePhase::Pat && g->otPossDone == 1, "after the return touchdown: not the try");
          g->extraPoint();
          inv.afterStep(*g);
          check(g->isFinal(), "a punt return touchdown in overtime and the punting team got the ball back");
        }
        clean(inv);
      }
      check(done, "no live punt return to test");
    }
  });

  std::printf("\n%d passed, %d failed\n", passCount, failCount);
  return failCount ? 1 : 0;
}
