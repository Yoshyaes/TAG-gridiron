// Port of src/sim/game.ts
// Game rules engine: clock, downs, scoring, penalties, special teams, CPU coordinators.
// RNG call order and all decision thresholds follow the TS exactly.
#include "Game.h"
#include "Roster.h"

#include <cmath>
#include <map>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <set>

namespace tag {

namespace {

std::string S(int v) { return std::to_string(v); }
std::string SR(double v) { return std::to_string(jsRoundI(v)); }  // `${Math.round(v)}`

std::string nm(const Player* p) { return p ? std::string(1, p->first.empty() ? '?' : p->first[0]) + "." + p->last : "unknown"; }

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/** Depths (yards past LOS) where each play's routes break / settle. */
std::vector<double> routeDepths(const OffPlay& p) {
  std::vector<double> out;
  for (const std::string& r : p.reads) {
    const std::string* name = p.routeFor(r);
    const RouteDef* rd = name ? routeByName(*name) : nullptr;
    if (!rd || rd->pts.empty()) continue;
    double m = -INFINITY;
    for (const auto& q : rd->pts) m = (std::max)(m, q.first);
    out.push_back(m);
  }
  return out;
}

Penalty mkPen(const std::string& name, Side on, double yards, bool autoFirst, PenaltySpot spot, bool replayDown,
              std::optional<double> spotX = std::nullopt) {
  Penalty p;
  p.name = name;
  p.on = on;
  p.yards = yards;
  p.autoFirst = autoFirst;
  p.spot = spot;
  p.spotX = spotX;
  p.replayDown = replayDown;
  return p;
}

KickFlight mkFlight(double fromX, double toX, double y, double hang, double height, std::optional<bool> good = std::nullopt) {
  KickFlight f;
  f.fromX = fromX;
  f.toX = toX;
  f.y = y;
  f.hang = hang;
  f.height = height;
  f.good = good;
  return f;
}

bool rosterHas(const Team& t, const Player* p) { return t.hasPlayer(p); }

// the goal: uprights 18 ft 6 in apart, crossbar 10 ft up (yd)
const double kUprightHalf = 3.083, kCrossbar = 3.333;

double invNorm(double q) {
  double lo = -7, hi = 7;
  for (int i = 0; i < 60; ++i) {
    const double m = 0.5 * (lo + hi);
    (0.5 * std::erfc(-m / std::sqrt(2.0)) < q ? lo : hi) = m;
  }
  return 0.5 * (lo + hi);
}

struct PlaceFly {
  double z = -1;    // height crossing the plane (-1: down before it)
  double t = 0;     // time to the plane, or to the ground
  double land = 0;  // where it came down, if before the plane
};
// a place-kicked ball: gravity and quadratic drag, the same integration the renderer flies it with
PlaceFly flyPlace(double v, double elevDeg, double plane, double drag) {
  const double grav = 10.72, h = 1.0 / 60.0;
  double x = 0, z = 0.25, ux = v * std::cos(elevDeg * kPi / 180), uz = v * std::sin(elevDeg * kPi / 180), t = 0;
  PlaceFly o;
  while (t < 9) {
    const double s = std::sqrt(ux * ux + uz * uz);
    ux -= drag * s * ux * h;
    uz -= (grav + drag * s * uz) * h;
    x += ux * h;
    z += uz * h;
    t += h;
    if (x >= plane) {
      o.z = z;
      o.t = t;
      return o;
    }
    if (uz < 0 && z <= 0) break;
  }
  o.t = t;
  o.land = x;
  return o;
}

}  // namespace

const char* gamePhaseName(GamePhase p) {
  switch (p) {
    case GamePhase::Kickoff: return "kickoff";
    case GamePhase::Scrimmage: return "scrimmage";
    case GamePhase::Pat: return "pat";
    case GamePhase::Final: return "final";
  }
  return "?";
}

const char* logTagName(LogTag t) {
  switch (t) {
    case LogTag::None: return "";
    case LogTag::Td: return "td";
    case LogTag::Turnover: return "turnover";
    case LogTag::Fg: return "fg";
    case LogTag::Safety: return "safety";
    case LogTag::Penalty: return "penalty";
    case LogTag::Info: return "info";
  }
  return "";
}

std::string fmtClock(double s) {
  const int t = static_cast<int>((std::max)(0.0, std::ceil(s)));
  const int sec = t % 60;
  return S(t / 60) + ":" + (sec < 10 ? "0" : "") + S(sec);
}

double expectedPoints(double ballOn, int down, double toGo) {
  const double base = -1.25 + 0.066 * ballOn;
  static const double kDown[] = {0, -0.4, -0.95, -1.7};
  const double d = kDown[static_cast<int>(clamp(down, 1, 4)) - 1];
  const double g = -0.035 * (clamp(toGo, 1, 30) - 10);
  return base + d + g;
}

Weather randomWeather(Rng& rng) {
  const double r = rng.next();
  const Precip precip = r < 0.1 ? Precip::Rain : r < 0.14 ? Precip::Snow : Precip::None;
  const double tempF = precip == Precip::Snow ? rng.range(15, 32) : rng.range(35, 88);
  const double wind = (std::max)(0.0, rng.normal(3.2, 2));  // yd/s (~6.5 mph mean)
  const double ang = rng.range(0, kPi * 2);
  Weather w;
  w.windX = std::cos(ang) * wind;
  w.windY = std::sin(ang) * wind;
  w.precip = precip;
  w.tempF = tempF;
  return w;
}

// =====================================================================================  Game
Game::Game(Team& homeTeam, Team& awayTeam, const GameOptions& opts) : home(&homeTeam), away(&awayTeam) {
  uint32_t seed;
  if (opts.seed) {
    seed = *opts.seed;
  } else {
    std::random_device rd;
    seed = static_cast<uint32_t>(rd() % 1000000000u);
  }
  rng = Rng(seed);
  quarterLen = opts.quarterLen.value_or(900);
  clock = quarterLen;
  weather = opts.weather ? *opts.weather : randomWeather(rng);
  userTeam = opts.userTeam;
  abilitiesOn = opts.abilities.value_or(true);
  for (const auto& p : home->roster) p->fatigue = 0;
  for (const auto& p : away->roster) p->fatigue = 0;
  // coin toss: winner defers (modern convention)
  const TeamKey winner = rng.chance(0.5) ? TeamKey::Home : TeamKey::Away;
  kicking = winner;
  openingReceiver = other(winner);
  poss = openingReceiver;
  homeDir = rng.chance(0.5) ? 1 : -1;
  addLog("Coin toss: " + teamOf(winner).abbr + " wins and defers. " + weatherText(), LogTag::Info);
}

std::string Game::weatherText() const {
  const Weather& w = weather;
  const int ws = jsRoundI(jsHypot(w.windX, w.windY) * 2.045);  // yd/s -> mph
  const char* p = w.precip == Precip::None ? "Clear" : w.precip == Precip::Rain ? "Rain" : "Snow";
  return std::string(p) + ", " + SR(w.tempF) + "F, wind " + S(ws) + " mph.";
}

void Game::addLog(const std::string& desc, LogTag big) {
  PlayLog l;
  l.q = q;
  l.clock = clock;
  l.poss = poss;
  l.down = down;
  l.toGo = toGo;
  l.ballOn = ballOn;
  l.desc = desc;
  l.score = score;
  l.big = big;
  log.push_back(std::move(l));
}

std::string Game::ydLine(double ballOnArg, TeamKey k) const {
  const std::string& t = teamOf(k).abbr;
  const std::string& o = teamOf(other(k)).abbr;
  const int b = jsRoundI(ballOnArg);
  if (b == 50) return "50";
  return b < 50 ? t + " " + S(b) : o + " " + S(100 - b);
}

std::string Game::downText() const {
  static const char* const kOrd[] = {"1st", "2nd", "3rd", "4th"};
  const std::string ord = (down >= 1 && down <= 4) ? kOrd[down - 1] : "undefined";
  const bool goal = ballOn + toGo >= 100;
  return ord + " & " + (goal ? std::string("Goal") : S((std::max)(1, jsRoundI(toGo))));
}

double Game::halfSecondsLeft() const {
  return q == 2 || q == 4 ? clock : q == 1 || q == 3 ? clock + quarterLen : clock;
}

// ------------------------------------------------------------------ clock
void Game::runClock(double sec, bool duringPlay) {
  const double before = clock;
  clock -= sec;
  if ((q == 2 || q == 4) && !twoMin[q] && before > 120 && clock <= 120) {
    twoMin[q] = true;
    if (!duringPlay) clock = 120;
    clockRunning = false;
    addLog("Two-minute warning.", LogTag::Info);
  }
  if (clock < 0) clock = 0;
}

bool Game::oobStops() const { return (q == 2 && clock <= 120) || (q >= 4 && clock <= 300); }

Tempo Game::tempo() const {
  const int m = margin();
  const bool late4 = q >= 4 && clock < 420;
  if ((q == 2 && clock < 130 && m >= -16) || (late4 && m < 0) || (q >= 4 && clock < 150 && m <= 0)) return Tempo::Hurry;
  if (q >= 4 && m > 0 && clock < 600) return Tempo::Milk;
  if (q == 2 && clock < 130 && m > 0 && ballOn < 40) return Tempo::Milk;
  return Tempo::Normal;
}

bool Game::preSnap() {
  if (clockRunning && !untimed) {
    const Tempo tp = tempo();
    double run = tp == Tempo::Hurry ? rng.range(12, 18) : tp == Tempo::Milk ? rng.range(37, 40) : rng.range(29, 37);
    if (lastOOB) run = (std::max)(4.0, run - 8);
    // timeouts by trailing defense late
    const TeamKey dk = other(poss);
    const bool defTrailing = margin(dk) < 0 && margin(dk) >= -16;
    // the clock reaching 2:00 stops for the warning first (a timeout "at 1:58" came before the warning)
    const bool warning = (q == 2 || q == 4) && !twoMin[q] && clock > 120 && clock - run <= 120;
    if (warning) {
      run = clock - 120;
    } else if (q >= 4 && clock < 150 && defTrailing && timeouts[dk] > 0) {
      timeouts[dk]--;
      addLog("Timeout " + teamOf(dk).abbr + " (" + S(timeouts[dk]) + " left).", LogTag::Info);
      run = 0;
    } else if (tp == Tempo::Hurry && timeouts[poss] > 0 && clock < 50 && clock > 3 && !shouldKneel()) {  // (a team kneeling it out keeps its timeouts)
      timeouts[poss]--;
      addLog("Timeout " + offense().abbr + " (" + S(timeouts[poss]) + " left).", LogTag::Info);
      run = 0;
    } else if (q == 2 && clock < 45 && timeouts[poss] > 0 && ballOn > 55 && margin() >= -16 && !shouldKneel()) {
      timeouts[poss]--;
      addLog("Timeout " + offense().abbr + " (" + S(timeouts[poss]) + " left).", LogTag::Info);
      run = 0;
    }
    runClock(run);
  }
  // (a defensive foul accepted at 0:00 extends the period by an untimed down: it is played with no time on the clock.
  // The flag was cleared here before the clock was looked at, and the period ended without it)
  const bool extraDown = untimed;
  untimed = false;
  lastOOB = false;
  if (clock <= 0 && !extraDown) {
    endQuarter();
    return false;
  }
  return true;
}

void Game::adjustBetweenPlays(double sec) {
  if (sec > 0) runClock(sec);
  else clock -= sec;
}

void Game::delayOfGame() { applyPresnap(mkPen("Delay of Game", Side::Off, 5, false, PenaltySpot::Los, true)); }

void Game::endQuarter() {
  static const char* const kQ[] = {"1st", "2nd", "3rd", "4th", "OT"};
  addLog(std::string("End of ") + kQ[(std::min)(q, 5) - 1] + " quarter.", LogTag::Info);
  clockRunning = false;
  if (q == 4 || q == 5) {
    if (score.home != score.away || q == 5) {
      finish();
      return;
    }
    // overtime: 10 minutes, coin toss, receiver gets ball
    q = 5;
    clock = 600;
    timeouts = PerTeam<int>{2, 2};
    safetyKick = false;  // (a safety as time ran out: overtime opens with a kickoff from the 35, not its free kick)
    kicking = rng.chance(0.5) ? TeamKey::Home : TeamKey::Away;
    poss = other(kicking);
    phase = GamePhase::Kickoff;
    otPossDone = 0;
    addLog("Overtime! 10:00 period. Both teams are guaranteed a possession.", LogTag::Info);
    return;
  }
  q++;
  clock = quarterLen;
  homeDir *= -1;
  if (q == 3) {
    // halftime: rest, timeouts reset, other team receives
    timeouts = PerTeam<int>{3, 3};
    safetyKick = false;  // (a safety as the half ran out: the second half opens with a kickoff from the 35)
    kicking = openingReceiver;
    poss = other(kicking);
    phase = GamePhase::Kickoff;
    for (const auto& p : home->roster) p->fatigue = 0;
    for (const auto& p : away->roster) p->fatigue = 0;
    addLog("Halftime.", LogTag::Info);
  } else {
    for (const auto& p : home->roster) p->fatigue *= 0.6;
    for (const auto& p : away->roster) p->fatigue *= 0.6;
  }
}

void Game::finish() {
  phase = GamePhase::Final;
  addLog("FINAL: " + away->abbr + " " + S(score.away) + " - " + home->abbr + " " + S(score.home), LogTag::Info);
}

// ------------------------------------------------------------------ drive bookkeeping
void Game::changePossession(double newBallOn, const std::string& reason) {
  team[poss].top += 0;  // TOP tracked per play
  poss = other(poss);
  ballOn = clamp(newBallOn, 1, 99);
  down = 1;
  toGo = (std::min)(10.0, 100 - ballOn);
  ballY = FIELD_W / 2;
  clockRunning = false;
  driveStartBallOn = ballOn;
  inRedZone = false;
  phase = GamePhase::Scrimmage;
  if (q == 5) otDriveEnded();
  (void)reason;
}

void Game::otDriveEnded() {
  if (q != 5 || phase == GamePhase::Final) return;
  otPossDone++;
  if (otPossDone >= 2 && score.home != score.away) finish();
}

void Game::scoreTD(TeamKey k) {
  score[k] += 6;
  phase = GamePhase::Pat;
  if (k != poss) {
    // defensive score: defense becomes kicking team for PAT
    poss = k;
    if (q == 5) {
      // defensive TD in OT ends it
      finish();
      return;
    }
  }
  ballOn = 85;
  ballY = FIELD_W / 2;
  clockRunning = false;
}

// ------------------------------------------------------------------ CPU coordinators
bool Game::shouldKneel() const {
  const int m = margin();
  if (m <= 0) return false;
  const int oppTO = timeouts[other(poss)];
  // (the kneels run the clock down to the 4th-down snap - that one is a punt or a kick, fourthDownCall: the downs left
  // before it each take a kneel and a runoff, and each of the other team's timeouts takes one runoff away. It counted a
  // runoff after the 4th down as well, and knelt with 0:40 left against three timeouts to punt it back with 0:30)
  const int snaps = 5 - down;
  const double burn = (std::max)(0, snaps - 1 - oppTO) * 40 + (snaps - 1) * 1.5;
  if (q >= 4 && clock <= burn) return true;
  if (q == 2 && clock <= 30 && ballOn < 55 && m > 0) return true;  // (past the 55 they try for points: the timeouts above)
  return false;
}

FourthDownCall Game::fourthDownCall() {
  const Team& t = offense();
  const double fgDist = 100 - ballOn + 17;
  const Player* k = kicker(poss);
  const double range = 55 + (kickR(*k, poss, false).kickPower - 80) * 0.3 + windHelp(poss) * 0.6;
  const int m = margin();
  const double secs = q >= 4 ? clock : 9999;
  // desperation
  if (q >= 4 && m < 0) {
    if (m >= -3 && fgDist <= range && secs < 120) return FourthDownCall::Fg;
    // (a one-score-by-a-field-goal deficit with four minutes left still punts and trusts the defense; any deficit
    // with under two minutes, a touchdown deficit under four, two scores under six. Every deficit under four minutes
    // and two scores under eight came to 0.7 attempts a team-game on their own, the whole lot 1.96 against ~1.4)
    if (secs < 120 || (m <= -4 && secs < 240) || (m < -8 && secs < 360)) return FourthDownCall::Go;
  }
  if (q == 2 && clock < 20 && fgDist <= range) return FourthDownCall::Fg;
  const double ag = t.aggression;
  const double tg = toGo;
  // analytics-flavored go chart
  if (tg <= 1 && ballOn >= 35 - ag * 25) return FourthDownCall::Go;
  if (tg <= 2 && ballOn >= 50 - ag * 15 && ballOn < 80) return FourthDownCall::Go;
  if (tg <= 3 && ballOn >= 60 && ballOn <= 70 && ag > 0.45) return FourthDownCall::Go;
  if (tg <= 4 && ballOn >= 57 && fgDist > range && ag > 0.4) return FourthDownCall::Go;
  if (fgDist <= range) {
    if (ballOn >= 95 && tg <= 2 && ag > 0.5) return FourthDownCall::Go;
    return FourthDownCall::Fg;
  }
  return FourthDownCall::Punt;
}

double Game::windHelp(TeamKey k) const {
  // tailwind in yd/s along attack direction
  return weather.windX * worldDir(k);
}

const OffPlay* Game::cpuOffense() {
  if (shouldKneel()) return playById("kneel");
  const Tempo tp = tempo();
  const Team& t = offense();
  // spike to stop the clock
  if (tp == Tempo::Hurry && clockRunning && timeouts[poss] == 0 && clock < 30 && clock > 4 && down < 3) return playById("spike");
  double pPass = t.passLean - 0.05;  // (-0.04 before the 2026-11-22 playbook expansion: RPOs the quarterback throws turn a run call into a pass)
  const int d = down;
  const double tg = toGo;
  // by down and distance, on the league's own curve (2023 nflverse play-by-play, neutral situations: Q1-Q3, within 10,
  // outside the 5): 1st & 10 50 %, 1st & 15+ 71; 2nd & 1-2 34, 3 40, 4-6 52, 7-9 70, 10 63, 11+ 83; 3rd & 1-2 43,
  // 3 85, 4-6 93, 7+ 96. (The offsets are from the average lean, 0.51; 2nd and long was called a pass 61 % of the time
  // against 83, and 3rd and 3 61 % against 85.)
  if (d == 1) pPass += tg >= 15 ? 0.2 : tg > 10 ? 0.0 : -0.01;
  if (d == 2) pPass += tg <= 2 ? -0.17 : tg <= 3 ? -0.11 : tg <= 6 ? 0.01 : tg <= 9 ? 0.19 : tg <= 10 ? 0.12 : 0.32;
  if (d == 3) pPass += tg <= 2 ? -0.08 : tg <= 3 ? 0.34 : tg <= 6 ? 0.42 : 0.45;
  if (d == 4) pPass += tg >= 3 ? 0.3 : -0.25;
  const int m = margin();
  if (q >= 3) pPass += clamp(-m * 0.012, -0.25, 0.3);
  if (tp == Tempo::Hurry) pPass += 0.3;
  if (tp == Tempo::Milk) pPass -= 0.25;
  if (ballOn >= 96) pPass -= 0.1;
  pPass = clamp(pPass, 0.1, 0.95);
  const bool pass = rng.chance(pPass);
  const bool goal = ballOn >= 95;
  const bool shortY = tg <= 2;
  const bool longY = tg >= 8;
  // the team's own book
  std::vector<const OffPlay*> pool;
  for (const OffPlay* pp : offBookFor(t).plays) {
    const OffPlay& p = *pp;
    if (p.type == PlayType::Kneel || p.type == PlayType::Spike) continue;
    const bool isPass = p.type != PlayType::Run;
    if (isPass != pass) continue;
    if (goal && p.hasTag("deep")) continue;
    const Formation* f = formationById(p.formation);
    if (!goal && f && (f->personnel == "22" || f->personnel == "23") && !shortY) continue;  // the heavy sets come out for short yardage
    if (p.run && p.run->style == RunStyle::Sneak && !(shortY && tg <= 1)) continue;
    pool.push_back(&p);
  }
  if (pool.empty())  // (a book with nothing for the call: the league's stock plays)
    for (const OffPlay& p : OFF_PLAYS())
      if (p.stock && p.type != PlayType::Kneel && p.type != PlayType::Spike && (p.type != PlayType::Run) == pass) pool.push_back(&p);
  // a book weighs a set by how many plays it has in it; a formation with more plays should not be called more
  // often for it, so each play is weighted by 1 / sqrt(plays in its formation)
  std::map<std::string, int> perForm;
  for (const OffPlay* pp : pool) perForm[pp->formation]++;
  // (the quarterback's legs: designed runs and read keeps are called for the mobile ones)
  const std::vector<Player*> qbs = depth(t, Pos::QB);
  const double qbLegs = qbs.empty() ? 0.5 : clamp((qbs[0]->r.speed - 62) / 18.0, 0.15, 1.3);
  std::vector<double> w;
  w.reserve(pool.size());
  for (const OffPlay* pp : pool) {
    const OffPlay& p = *pp;
    double x = 1;
    // the league's personnel on the field (2023: 11 ~60 %, 12 ~20 %, 21 / 13 a few percent each): a book's many sets of
    // one grouping must not call it more for having them
    if (const Formation* fp = formationById(p.formation)) {
      const std::string& pe = fp->personnel;
      x *= pe == "11" ? 1.0 : pe == "12" ? 0.75 : pe == "10" ? 0.55 : pe == "13" ? 0.45 : pe == "21" ? 0.65 : pe == "20" ? 0.5 : pe == "31" ? 0.3 : 1.0;
      if (fp->family == "Wildcat") x *= 0.25;
    }
    // (an RPO is a run call the quarterback may throw; trick plays are a once-a-game call)
    if (p.cat == PlayCat::RPO) x *= 0.35;
    if (p.cat == PlayCat::Trick) x *= 0.12;
    // the run game's mix by scheme: zone and stretch are the base of every modern run game, the gap schemes a
    // complement (2023: ~55 % of NFL runs are zone, ~35 % gap)
    if (p.type == PlayType::Run && p.run && p.cat == PlayCat::Run) {
      const RunStyle st = p.run->style;
      if (st == RunStyle::Gap && !p.run->jobs.empty()) x *= 0.65;
    }
    if (p.run && (p.run->carrier == "QB" || p.run->read == "end") && p.run->style != RunStyle::Sneak &&
        !(formationById(p.formation) && formationById(p.formation)->family == "Wildcat"))
      x *= 0.6 * qbLegs;
    if (shortY && p.hasTag("shortyard")) x += 2.5;
    x /= std::sqrt(static_cast<double>((std::max)(1, perForm[p.formation])) / 6.0);
    if (goal && p.hasTag("goalline")) x += 2;
    if (longY && (p.hasTag("long") || p.hasTag("deep") || p.hasTag("mid"))) x += 1.2;
    if (longY && p.hasTag("quick")) x *= 0.5;
    if (tp == Tempo::Hurry && (p.hasTag("quick") || p.hasTag("short"))) x += 0.8;
    if (p.type == PlayType::PlayAction && d == 1) x += 0.6;
    if (p.type == PlayType::PlayAction) x *= tune().paCallW;
    // from under center a team throws mostly off play action; the straight dropbacks come from the gun
    if (const Formation* fp = formationById(p.formation); fp && fp->qbDepth <= 1.5 && p.type == PlayType::Pass && !p.hasTag("quick")) x *= 0.4;
    if (p.type == PlayType::Screen && longY) x += 0.6;
    if (p.type == PlayType::Screen) x *= tune().screenW;
    if (std::find(recent.begin(), recent.end(), p.id) != recent.end()) x *= 0.4;
    // money downs: favor concepts with a route that settles at/just past the sticks
    if (d >= 3 && p.type != PlayType::Run) {
      const std::vector<double> ds = routeDepths(p);
      bool some = false;
      bool every = true;
      for (double qd : ds) {
        if (qd >= tg && qd <= tg + 7) some = true;
        if (!(qd < tg - 1)) every = false;
      }
      if (some) x *= 2.2;
      else if (every) x *= 0.35;
    }
    w.push_back(x);
  }
  const OffPlay* pick = rng.weighted(pool, w);
  recent.push_back(pick->id);
  if (recent.size() > 4) recent.erase(recent.begin());
  return pick;
}

const DefCall* Game::cpuDefense(const OffPlay& offPlay) {
  const Team& t = defense();
  const std::string& pers = formationById(offPlay.formation)->personnel;
  const bool goal = ballOn >= 93;
  const bool shortY = toGo <= 2;
  const bool longY = toGo >= 8 && down >= 2;
  const DefBook& book = defBookFor(t);
  Front front;
  if (goal || (shortY && (pers == "22" || pers == "21" || pers == "23" || pers == "31"))) front = Front::Goalline;
  else if (pers == "21" || pers == "12" || pers == "22" || pers == "13" || pers == "23" || pers == "31") front = book.base;
  else front = (longY && down == 3 && rng.chance(0.5)) ? Front::Dime : Front::Nickel;
  // the family of fronts: the book's base sets (4-3 or 3-4), its nickel packages (4-2-5, 3-3-5, 2-4-5), its dime ones
  auto fam = [](Front f) {
    switch (f) {
      case Front::Nickel33:
      case Front::Nickel24: return Front::Nickel;
      case Front::Dime32: return Front::Dime;
      default: return f;
    }
  };
  std::vector<const DefCall*> pool;
  for (const DefCall* d : book.calls)
    if (fam(d->front) == front) pool.push_back(d);
  if (pool.empty())
    for (const DefCall& d : DEF_PLAYS())
      if (fam(d.front) == front && d.set.empty()) pool.push_back(&d);
  std::vector<double> w;
  for (const DefCall* dp : pool) {
    const DefCall& d = *dp;
    double x = defSchemeLean(book.scheme, d);
    const bool blitz = d.rushers() > 4;
    if (blitz) x *= t.blitzLean * 3;
    if (longY && d.hasTag("long")) x += 1;
    if (shortY && d.hasTag("shortyard")) x += 1;
    // Cover 0 (no deep help) is a situational call: short yardage / goal line / desperation
    if (d.coverage == Coverage::C0 && !shortY && !goal) x *= 0.12;
    if (tempo() == Tempo::Hurry && margin(other(poss)) > 0 && d.hasTag("prevent")) x += 2;
    w.push_back(x);
  }
  return rng.weighted(pool, w);
}

// ------------------------------------------------------------------ penalties
/** The man who jumped: a starter at the spots that false start / jump offside, the less aware the likelier. */
static Player* jumper(Rng& rng, const Team& t, std::initializer_list<Pos> spots, int per) {
  std::vector<Player*> c;
  std::vector<double> w;
  for (Pos ps : spots) {
    const std::vector<Player*> d = depth(t, ps);
    for (int i = 0; i < per && i < static_cast<int>(d.size()); i++) {
      c.push_back(d[i]);
      w.push_back((std::max)(5.0, 105 - d[i]->r.awareness));
    }
  }
  return c.empty() ? nullptr : rng.weighted(c, w);
}

static std::string onWho(const std::string& name, const Player* p) {
  return p ? name + " (#" + std::to_string(p->number) + " " + nm(p) + ")" : name;
}

std::optional<Penalty> Game::rollPresnap() {
  const double r = rng.next();
  const double road = poss == TeamKey::Away ? 0.004 : 0;  // crowd noise
  const double ps = tune().penScale;
  if (r < (0.014 + road) * ps)
    return mkPen(onWho("False Start", jumper(rng, offense(), {Pos::OT, Pos::OG, Pos::C, Pos::TE, Pos::WR}, 2)), Side::Off, 5, false, PenaltySpot::Los, true);
  if (r < (0.018 + road) * ps) return mkPen("Delay of Game", Side::Off, 5, false, PenaltySpot::Los, true);
  // (the rates from here on: accepted flags per team game in 2023, nflverse play-by-play - defensive jumps 0.50: offside
  // 0.26, neutral zone 0.17, encroachment 0.07; they were 0.80, a third of them encroachment)
  if (r < (0.0243 + road) * ps) {
    const double k = rng.next();
    const char* nm = k < 0.52 ? "Offside" : k < 0.86 ? "Neutral Zone Infraction" : "Encroachment";
    return mkPen(onWho(nm, jumper(rng, defense(), {Pos::DE, Pos::DT, Pos::LB}, 2)), Side::Def, 5, false, PenaltySpot::Los, true);
  }
  // the rest of a Sunday's pre-snap flags: a formation or shift foul (~0.2 a team game), twelve on defense (~0.15)
  if (r < (0.0268 + road) * ps) return mkPen(rng.chance(0.56) ? "Illegal Formation" : "Illegal Shift", Side::Off, 5, false, PenaltySpot::Los, true);
  if (r < (0.0278 + road) * ps) return mkPen("Too Many Men on the Field", Side::Def, 5, false, PenaltySpot::Los, true);
  return std::nullopt;
}

std::optional<Penalty> Game::rollPostsnap(const PlayResult& res, const OffPlay& play) {
  const double r = rng.next();
  const bool isPass = play.type != PlayType::Run && res.kind != PlayKind::Kneel && res.kind != PlayKind::Spike;
  double acc = 0;
  auto roll = [&](double p) { return r < (acc += p * tune().penScale); };
  if (res.kind == PlayKind::Kneel || res.kind == PlayKind::Spike) return std::nullopt;
  // the officials call what the play gave them (PlayResult foul risks, from the sim): one draw, the fouls stacked in
  // the order they are looked at, so at most one flag a play
  // (the weights set the mix to the league's accepted flags per team game: offensive holding ~1.1, DPI ~0.5,
  // defensive holding and illegal contact ~0.45 - holding was called 1.8 times a game and coverage fouls half as often
  // as on Sundays, which put offenses in 2nd and 20 far too much)
  auto call = [&](const FoulRisk& f, double k = 1.0) { return f.p > 0 && roll(f.p * k); };
  if (call(res.foulHold, tune().holdCall)) return mkPen(onWho("Offensive Holding", res.foulHold.by), Side::Off, 10, false, PenaltySpot::Los, true);
  if (isPass && res.incomplete && res.airYards >= 5 && call(res.foulDpi, 2.2))
    return mkPen(onWho("Defensive Pass Interference", res.foulDpi.by), Side::Def, 0, true, PenaltySpot::Spot, false, static_cast<double>(res.airYards));
  if (isPass && call(res.foulDhold, 1.5))
    return mkPen(onWho(rng.chance(0.6) ? "Defensive Holding" : "Illegal Contact", res.foulDhold.by), Side::Def, 5, true, PenaltySpot::Los, false);
  if (isPass && call(res.foulRough, 0.5)) return mkPen(onWho("Roughing the Passer", res.foulRough.by), Side::Def, 15, true, PenaltySpot::End, false);
  if (isPass && res.complete && res.airYards >= 8 && call(res.foulOpi, 2.5))
    return mkPen(onWho("Offensive Pass Interference", res.foulOpi.by), Side::Off, 10, false, PenaltySpot::Los, true);
  if (call(res.foulMask)) return mkPen(onWho("Face Mask", res.foulMask.by), Side::Def, 15, true, PenaltySpot::End, false);
  // a ball thrown away from the pocket with the rush on him and nobody near it: loss of down and ten yards (nflverse
  // 2023: 0.10 a team game - it was never called, and every throwaway was a free incompletion)
  if (isPass && res.thrownAway && res.pressured && roll(0.06)) return mkPen("Intentional Grounding", Side::Off, 10, false, PenaltySpot::Los, false);
  // the rest, at the 2023 rates (accepted flags a team game: unnecessary roughness 0.24 on the defense and 0.06 on the
  // offense, illegal use of hands 0.08; an illegal block on a scrimmage play is rare - 0.002 a play made it 0.13 a game
  // on top of the returns' own)
  if (roll(0.0037)) return mkPen("Unnecessary Roughness", Side::Def, 15, true, PenaltySpot::End, false);
  if (roll(0.001)) return mkPen("Unnecessary Roughness", Side::Off, 15, false, PenaltySpot::Los, true);
  if (roll(0.0013)) return mkPen("Illegal Use of Hands", Side::Def, 5, true, PenaltySpot::Los, false);
  if (roll(0.0005)) return mkPen("Illegal Block in the Back", Side::Off, 10, false, PenaltySpot::Los, true);
  return std::nullopt;
}

bool Game::returnFoul(TeamKey rt, double& spot, std::string& desc) {
  // about one return in eighteen draws a flag on the return team, marked off from where the return ended (nflverse
  // 2023: ~0.24 a team game on the return team, holding 0.14, blocks above the waist / in the back 0.06; one in eight,
  // 60 % in the back, made illegal blocks 0.56 a game - ten times a Sunday's)
  if (!rng.chance(0.056)) return false;
  const double y = (std::min)(10.0, spot / 2);
  spot -= y;
  team[rt].penalties++;
  team[rt].penYds += jsRoundI(y);
  desc += " PENALTY on " + teamOf(rt).abbr + ": " + (rng.chance(0.3) ? "Illegal Block in the Back" : "Holding") + ", " + S(jsRoundI(y)) +
          " yards from the end of the return.";
  return true;
}

double Game::enforce(const Penalty& pen, double losArg, double endX) const {
  const double from = pen.spot == PenaltySpot::End ? endX : losArg;
  if (pen.spot == PenaltySpot::Spot) return clamp(losArg + (std::max)(1.0, pen.spotX.value_or(1)), 1, 99);
  if (pen.on == Side::Off) {
    const double y = (std::min)(pen.yards, from / 2);
    return from - y;
  }
  const double y = (std::min)(pen.yards, (100 - from) / 2);
  return from + y;
}

void Game::applyPresnap(const Penalty& pen) {
  const Team& t = pen.on == Side::Off ? offense() : defense();
  const TeamKey k = pen.on == Side::Off ? poss : other(poss);
  const double before = ballOn;
  ballOn = enforce(pen, ballOn, ballOn);
  const double moved = ballOn - before;
  toGo -= moved;
  if (toGo <= 0) {
    down = 1;
    toGo = (std::min)(10.0, 100 - ballOn);
  }
  team[k].penalties++;
  team[k].penYds += jsRoundI(std::fabs(moved));
  lastPenalty = pen;
  clockRunning = false;
  addLog("PENALTY on " + t.abbr + ": " + pen.name + ", " + S(std::abs(jsRoundI(moved))) + " yards. " + downText() + " at " + ydLine() + ".",
         LogTag::Penalty);
}

// ------------------------------------------------------------------ scrimmage
std::unique_ptr<PlaySim> Game::makePlay(const OffPlay& offPlay, const DefCall& defCall, std::optional<Side> userSide,
                                       std::optional<bool> flipOverride) {
  const bool flip = flipOverride ? *flipOverride : ballY > FIELD_W / 2 + 0.5 ? true : ballY < FIELD_W / 2 - 0.5 ? false : rng.chance(0.5);
  // strength to the field (wide) side: formation "right" is the offense's -y side
  // garbage time: four scores up in the 4th quarter (or a 38-point lead in the 3rd), a team plays its backups at the
  // skill spots, as NFL teams do (and the blowout stops growing); it comes off again if the game tightens
  // (decided when the ball changes hands, so nobody is pulled in the middle of a drive)
  if (!pullDecided_ || pullPoss_ != poss) {
    for (TeamKey k : {TeamKey::Home, TeamKey::Away}) teamOf(k).pullStarters = (q >= 4 && margin(k) >= 22) || (q == 3 && margin(k) >= 31);
    pullDecided_ = true;
    pullPoss_ = poss;
  }
  PlayConfig cfg;
  cfg.off = &offense();
  cfg.def = &defense();
  cfg.offPlay = &offPlay;
  cfg.defCall = &defCall;
  cfg.los = ballOn;
  cfg.ballY = ballY;
  cfg.flip = flip;
  cfg.weather = weather;
  cfg.worldDir = worldDir();
  cfg.rng = rng.fork();
  cfg.userSide = userSide;
  cfg.down = static_cast<double>(down);
  cfg.toGo = toGo;
  cfg.abil = abilCtx();
  auto sim = std::make_unique<PlaySim>(cfg);
  sim->onStep = onSimStep;
  return sim;
}

PlayLog Game::finishPlay(PlaySim& sim) {
  const PlayResult& res = *sim.result;
  const OffPlay& play = *sim.cfg.offPlay;
  const double los = ballOn;
  const TeamKey ok = poss;
  const TeamKey dk = other(ok);
  Team& O = offense();
  Team& D = defense();
  TeamLine& T = team[ok];
  auto st = [this](const Player& p, const Team& tm) -> PlayerLine& { return stats.line(p, tm.abbr); };
  const int startDown = down;
  const double startToGo = toGo;
  if (onPlayResult) onPlayResult(sim, res);
  // fatigue / injuries
  for (Ent* e : sim.ents) {
    Player* p = e->p;
    p->fatigue = clamp(p->fatigue + (0.028 + res.duration * 0.0035) * (1.25 - p->r.stamina / 120), 0, 1);
  }
  std::set<int> onField;
  for (Ent* e : sim.ents) onField.insert(e->p->id);
  for (const auto& p : home->roster) {
    p->onFieldLast = onField.count(p->id) > 0;
    if (!p->onFieldLast) p->fatigue = (std::max)(0.0, p->fatigue - 0.05);
  }
  for (const auto& p : away->roster) {
    p->onFieldLast = onField.count(p->id) > 0;
    if (!p->onFieldLast) p->fatigue = (std::max)(0.0, p->fatigue - 0.05);
  }
  for (Ent* e : sim.ents) {
    if (rng.chance(0.00028)) {
      static const std::vector<double> kWeeks = {0.5, 1, 2, 4, 8};
      static const std::vector<double> kW = {4, 3, 2, 1, 0.5};
      e->p->injuredWeeks = rng.weighted(kWeeks, kW);
      injuries.push_back(e->p);
      addLog(std::string("Injury: ") + posName(e->p->pos) + " " + nm(e->p) + " (" + teamOf(e->side == Side::Off ? ok : dk).abbr +
                 ") is down and will not return.",
             LogTag::Info);
    }
  }
  // clock during the play
  runClock(res.duration, true);
  team[ok].top += res.duration + (clockRunning ? 25 : 0);

  // postsnap penalty
  std::optional<Penalty> pen = rollPostsnap(res, play);
  if (forcedFlag) {
    pen = *forcedFlag;
    forcedFlag.reset();
  }
  bool usePen = false;
  if (pen) {
    // compare options from offense's perspective
    const double rEP = resultEP(res, los);
    // (a foul marked from the end of the play is marked from the line when the play ended with the other team's ball -
    // the end of a takeaway's return is in the other team's frame: it put the ball at the offense's goal line, 1st and 0)
    const double endSpot = res.turnover ? los : res.endX;
    double penBall = enforce(*pen, los, endSpot);
    // intentional grounding: loss of down and 10 yd from the line, or at the spot of the foul when the passer threw it
    // from more than 10 yd behind it - and from his own end zone it is a safety (it was always 10 yd, half the distance
    // to the goal, and a quarterback grounding it from his end zone gave up nothing)
    const bool grounding = pen->name == "Intentional Grounding";
    const double foulX = sim.ball.throwX;
    const bool groundSafety = grounding && sim.passThrown && foulX <= 0;
    if (grounding && !groundSafety && sim.passThrown && foulX < los - 10) penBall = foulX;
    double penEP;
    if (pen->on == Side::Def) {
      const bool first = pen->autoFirst || penBall >= los + toGo;
      penEP = first ? expectedPoints(penBall, 1, 10) : expectedPoints(penBall, down, toGo - (penBall - los));
      usePen = penEP > rEP && !res.td;
      if (res.td == Side::Off) usePen = false;
    } else {
      // (a loss of down on 4th down hands the ball over where it is marked)
      penEP = groundSafety ? -3.0
              : !pen->replayDown && down >= 4 ? -expectedPoints(100 - penBall, 1, 10)
                                              : expectedPoints(penBall, down, toGo + (los - penBall));
      usePen = penEP < rEP;
    }
    if (usePen && groundSafety) {
      team[ok].penalties++;
      score[dk] += 2;
      addLog(describe(res, play, sim) + " PENALTY on " + O.abbr + ": " + pen->name + " in the end zone. SAFETY! " + D.abbr + " +2.", LogTag::Safety);
      kicking = ok;
      poss = dk;
      phase = GamePhase::Kickoff;
      safetyKick = true;
      clockRunning = false;
      if (q == 5) finish();
      return log.back();
    }
    if (usePen) {
      const TeamKey k = pen->on == Side::Off ? ok : dk;
      team[k].penalties++;
      team[k].penYds += jsRoundI(std::fabs(penBall - (pen->spot == PenaltySpot::End ? endSpot : los)));
      const double moved = penBall - los;
      ballOn = penBall;
      if (pen->on == Side::Def && (pen->autoFirst || penBall >= los + toGo)) {
        down = 1;
        toGo = (std::min)(10.0, 100 - ballOn);
        T.firstDowns++;
      } else {
        toGo -= moved;
        if (!pen->replayDown) down++;
      }
      clockRunning = false;
      if (clock <= 0 && pen->on == Side::Def) untimed = true;
      const Team& pt = pen->on == Side::Off ? O : D;
      if (down > 4) {
        // (loss of down on fourth down - intentional grounding: the ball goes over where the foul is marked. It was a
        // 5th down)
        addLog(describe(res, play, sim) + " PENALTY on " + pt.abbr + ": " + pen->name + ", enforced. Turnover on downs.", LogTag::Turnover);
        if (q == 5) otDriveEnded();
        if (phase == GamePhase::Final) return log.back();
        changePossessionRaw(100 - ballOn);
        return log.back();
      }
      const std::string line = describe(res, play, sim) + " PENALTY on " + pt.abbr + ": " + pen->name + ", enforced. " + downText() + " at " + ydLine() + ".";
      addLog(line, LogTag::Penalty);
      // half-distance etc handled; stats for the nullified play are not counted
      return log.back();
    } else {
      const Team& pt = pen->on == Side::Off ? O : D;
      pen->name = pen->name + " on " + pt.abbr + " (declined)";
    }
  }

  // ---- the zone: every X-Factor man on the field moves toward it or out of it (a play the flag wiped out does not count)
  if (abilOn()) {
    abil.afterPlay(sim, res, O.abbr, D.abbr);
    if (abil.events.size() > 32) abil.events.erase(abil.events.begin(), abil.events.end() - 32);
  }

  // ---- stats
  T.plays++;
  const int yards = res.yards;
  if (res.kind == PlayKind::Pass || res.kind == PlayKind::Sack) {
    Player* qbP = res.passer;
    if (qbP) {
      PlayerLine& l = st(*qbP, O);
      if (res.kind == PlayKind::Sack) {
        l.sacked++;
        l.sackYds += -yards;
        T.sacks++;
        T.yards += yards;
        T.passYds += yards;
        team[dk].sacks += 0;
        if (res.sackedBy) st(*res.sackedBy, D).sacks++;
      } else {
        l.passAtt++;
        T.passAtt++;
        if (res.target && !res.thrownAway) st(*res.target, O).targets++;
        if (res.complete && res.receiver) {
          l.passComp++;
          T.passComp++;
          const int gain = res.interception ? 0 : (std::max)(res.yards, -10);
          l.passYds += gain;
          T.passYds += gain;
          T.yards += gain;
          PlayerLine& rl = st(*res.receiver, O);
          rl.rec++;
          rl.recYds += gain;
          rl.yac += res.yac;
          rl.recLong = (std::max)(rl.recLong, gain);
          if (res.td == Side::Off) {
            rl.recTD++;
            l.passTD++;
          }
          if (gain >= 20) T.explosive++;
        }
        if (res.interception) {
          l.passInt++;
          T.ints++;
          if (res.intBy) st(*res.intBy, D).ints++;
        }
      }
    }
    if (res.pbuBy) st(*res.pbuBy, D).pbu++;
  } else if (res.kind == PlayKind::Run || res.kind == PlayKind::Scramble) {
    if (res.rusher) {
      PlayerLine& l = st(*res.rusher, O);
      l.rushAtt++;
      l.rushYds += yards;
      l.rushLong = (std::max)(l.rushLong, yards);
      T.rushAtt++;
      T.rushYds += yards;
      T.yards += yards;
      if (res.td == Side::Off) l.rushTD++;
      if (yards >= 10) T.explosive++;  // explosive: a run of 10+, a pass of 20+
    }
  }
  if (res.brokenTackles && (res.rusher || res.receiver))
    st(*(res.receiver ? res.receiver : res.rusher), O).brokenTackles += res.brokenTackles;
  for (Player* tk : res.tacklers) {
    const Team& tm = rosterHas(D, tk) ? D : O;
    PlayerLine& l = st(*tk, tm);
    l.tackles++;
    if (yards < 0 && res.kind != PlayKind::Sack && &tm == &D) l.tfl++;
  }
  if (res.fumbleBy) {
    st(*res.fumbleBy, rosterHas(O, res.fumbleBy) ? O : D).fumbles++;
    if (res.forcedBy) st(*res.forcedBy, rosterHas(D, res.forcedBy) ? D : O).ff++;
    if (res.fumbleLost) {
      st(*res.fumbleBy, rosterHas(O, res.fumbleBy) ? O : D).fumblesLost++;
      T.fumblesLost++;
      if (res.fumbleRecBy) st(*res.fumbleRecBy, D).fr++;
    }
  }
  if (startDown == 3) T.thirdAtt++;
  if (startDown == 4) T.fourthAtt++;

  // ---- outcome
  const std::string desc = describe(res, play, sim) + (pen ? " " + pen->name + "." : std::string());
  const double newY = clamp(res.endX == los ? ballY : endY(sim), HASH_R, HASH_L);
  LogTag big = LogTag::None;
  lastOOB = res.outOfBounds;
  if (res.kind == PlayKind::Spike || res.incomplete || (res.outOfBounds && oobStops())) clockRunning = false;
  else clockRunning = true;
  if (res.outOfBounds && !oobStops()) clockRunning = true;

  if (ballOn >= 80 && !inRedZone) {
    inRedZone = true;
    T.redZoneTrips++;
  }

  if (res.td) {
    const TeamKey scorer = *res.td == Side::Off ? ok : dk;
    if (*res.td == Side::Off && inRedZone) T.redZoneTD++;
    if (*res.td == Side::Def) {
      T.turnovers++;
      PlayerLine* l = res.intBy ? &st(*res.intBy, D) : res.fumbleRecBy ? &st(*res.fumbleRecBy, D) : nullptr;
      if (l) l->defTD++;
      if (q == 5) otDriveEnded();
    }
    if (startDown == 3 && *res.td == Side::Off) T.thirdConv++;
    if (startDown == 4 && *res.td == Side::Off) T.fourthConv++;
    if (*res.td == Side::Off) T.firstDowns++;
    addLog(desc + " TOUCHDOWN " + teamOf(scorer).abbr + "!", LogTag::Td);
    scoreTD(scorer);
    return log.back();
  }
  if (res.safety) {
    score[dk] += 2;
    addLog(desc + " SAFETY! " + D.abbr + " +2.", LogTag::Safety);
    // free kick from own 20 by the team that gave up the safety
    kicking = ok;
    poss = dk;
    phase = GamePhase::Kickoff;
    safetyKick = true;
    clockRunning = false;
    if (q == 5) finish();
    return log.back();
  }
  if (res.turnover) {
    T.turnovers++;
    big = LogTag::Turnover;
    const double newBall = res.touchback ? 20 : 100 - res.endX;
    addLog(desc + (res.touchback ? " Touchback." : ""), big);
    if (q == 5) otDriveEnded();
    if (phase == GamePhase::Final) return log.back();
    changePossessionRaw(newBall);
    return log.back();
  }
  // normal advance
  ballOn = clamp(res.endX, 0.5, 99.5);
  ballY = newY;
  const double gained = ballOn - los;
  if (gained >= startToGo) {
    down = 1;
    toGo = (std::min)(10.0, 100 - ballOn);
    T.firstDowns++;
    if (startDown == 3) T.thirdConv++;
    if (startDown == 4) T.fourthConv++;
  } else {
    down++;
    toGo = startToGo - gained;
    if (down > 4) {
      addLog(desc + " Turnover on downs.", LogTag::Turnover);
      if (q == 5) otDriveEnded();
      if (phase == GamePhase::Final) return log.back();
      changePossessionRaw(100 - ballOn);
      return log.back();
    }
  }
  addLog(desc, big);
  return log.back();
}

void Game::changePossessionRaw(double newBallOn) {
  poss = other(poss);
  ballOn = clamp(newBallOn, 1, 99);
  down = 1;
  toGo = (std::min)(10.0, 100 - ballOn);
  ballY = FIELD_W / 2;
  clockRunning = false;
  driveStartBallOn = ballOn;
  inRedZone = false;
  phase = GamePhase::Scrimmage;
}

double Game::endY(const PlaySim& sim) const {
  const Ent* h = sim.ball.holder;
  return h ? h->pos.y : sim.ball.pos.y;
}

double Game::resultEP(const PlayResult& res, double losArg) const {
  if (res.td == Side::Off) return 6.95;
  if (res.td == Side::Def) return -6.95;
  if (res.turnover) return -expectedPoints(100 - res.endX, 1, 10);
  if (res.safety) return -2 - 1;
  const double gained = res.endX - losArg;
  if (gained >= toGo) return expectedPoints(res.endX, 1, 10);
  if (down == 4) return -expectedPoints(100 - res.endX, 1, 10);
  return expectedPoints(res.endX, down + 1, toGo - gained);
}

std::string Game::describe(const PlayResult& res, const OffPlay& play, const PlaySim& sim) const {
  const int y = res.yards;
  auto yd = [](int n) -> std::string {
    if (n == 0) return "no gain";
    if (n < 0) return "a loss of " + S(-n);
    return S(n) + " yard" + (n == 1 ? "" : "s");
  };
  std::string tk;
  if (!res.tacklers.empty()) {
    tk = " (";
    for (size_t i = 0; i < res.tacklers.size(); i++) {
      if (i) tk += ", ";
      tk += nm(res.tacklers[i]);
    }
    tk += ")";
  }
  const double ts = res.targetSpotY ? *res.targetSpotY : sim.by;
  auto dir = [&sim](double yy) -> std::string {
    const double rel = (yy - sim.by) * 1;
    return rel > 5 ? "left" : rel < -5 ? "right" : "middle";
  };
  const std::string qbN = nm(res.passer ? res.passer : sim.qb->p);
  const std::string oob = res.outOfBounds ? ", out of bounds" : tk;
  switch (res.kind) {
    case PlayKind::Kneel:
      return qbN + " kneels.";
    case PlayKind::Spike:
      return qbN + " spikes the ball to stop the clock.";
    case PlayKind::Sack: {
      std::string f;
      if (res.fumbleBy)
        f = std::string(" FUMBLE") + (res.fumbleLost ? ", recovered by " + defense().abbr + " " + nm(res.fumbleRecBy) : std::string(", recovered by offense")) + ".";
      return qbN + " SACKED by " + nm(res.sackedBy) + " for " + yd(y) + "." + f;
    }
    case PlayKind::Scramble: {
      bool slid = false;
      for (const PlayEvent& e : res.events) slid = slid || e.type == "slide";
      return qbN + " scrambles for " + yd(y) + oob + (slid ? " and slides" : "") + ".";
    }
    case PlayKind::Run: {
      const std::string r = nm(res.rusher);
      std::string f;
      if (res.fumbleBy)
        f = " FUMBLE by " + nm(res.fumbleBy) + (res.fumbleLost ? ", recovered by " + defense().abbr + " " + nm(res.fumbleRecBy) : std::string(", offense recovers")) + ".";
      return r + " " + lower(play.name) + " for " + yd(y) + oob + "." + f;
    }
    case PlayKind::Pass: {
      const std::string depth = res.airYards >= 16 ? "deep" : "short";
      const std::string d = dir(ts);
      if (res.thrownAway) return qbN + " throws it away under pressure.";
      if (res.interception)
        return qbN + " pass " + depth + " " + d + " INTERCEPTED by " + nm(res.intBy) +
               (res.td == Side::Def ? std::string(", returned for a TOUCHDOWN") : ", returned to the " + ydLine(100 - res.endX, other(poss))) + ".";
      if (res.incomplete) {
        std::string why;
        if (res.pbuBy) why = ", broken up by " + nm(res.pbuBy);
        else {
          for (const PlayEvent& e : sim.events)
            if (e.type == "drop") {
              why = ", dropped";
              break;
            }
        }
        return qbN + " pass " + depth + " " + d + " to " + nm(res.target) + " incomplete" + why + ".";
      }
      std::string f;
      if (res.fumbleBy) f = std::string(" FUMBLE") + (res.fumbleLost ? ", recovered by " + defense().abbr : std::string(", offense recovers")) + ".";
      return qbN + " pass " + depth + " " + d + " to " + nm(res.receiver) + " for " + yd(y) + oob + "." + f;
    }
  }
  return std::string();
}

// ------------------------------------------------------------------ special teams
bool Game::abilOn() const { return abilitiesOn && tune().abilities > 0; }

AbilityCtx Game::abilCtx() const {
  AbilityCtx c;
  c.on = abilOn();
  c.q = q;
  c.clock = clock;
  c.offMargin = margin(poss);
  if (c.on) c.zoneIds = abil.activeIds();
  return c;
}

Ratings Game::kickR(const Player& k, TeamKey kt, bool punt) const {
  if (!abilOn()) return k.r;
  AbilitySituation s;
  s.q = q;
  s.clock = clock;
  s.margin = margin(kt);
  s.down = down;
  s.toGo = toGo;
  s.los = ballOn;
  return kickRatings(k, punt, s, abil.active(k));
}

Player* Game::kicker(TeamKey k) const {
  // the depth chart's kicker / punter where the user set one (Roster.h), else the best at the spot
  if (Player* c = chartSpecialist(teamOf(k), DSlot::K)) return c;
  Player* best = nullptr;
  for (const auto& p : teamOf(k).roster)
    if (p->pos == Pos::K && (!best || p->ovr > best->ovr)) best = p.get();
  return best;
}
Player* Game::punter(TeamKey k) const {
  if (Player* c = chartSpecialist(teamOf(k), DSlot::P)) return c;
  Player* best = nullptr;
  for (const auto& p : teamOf(k).roster)
    if (p->pos == Pos::P && (!best || p->ovr > best->ovr)) best = p.get();
  return best;
}

double Game::fgProb(double dist, const Player& k, TeamKey kt) const {
  const Ratings kr = kickR(k, kt, false);
  const double eff = dist - (kr.kickAcc - 80) * 0.35 - windHelp(kt) * 0.5 + std::fabs(weather.windY) * 0.4 +
                     (weather.precip != Precip::None ? 3 : 0) + (weather.tempF < 32 ? 2 : 0);
  const double maxR = 62 + (kr.kickPower - 80) * 0.3 + windHelp(kt) * 0.8;
  // logistic shaped to: 30y ~ 97%, 40y ~ 90%, 50y ~ 76%, 55y ~ 66%
  double p = sigmoid(5.4 - 0.083 * eff + (eff < 40 ? 0 : -0.012 * (eff - 40) * (eff - 40) / 10));
  if (dist > maxR) p *= (std::max)(0.0, 1 - (dist - maxR) / 5);
  return clamp(p, 0, 0.995);
}

SpecialResult Game::kickoff(bool onside, bool liveReturn) {
  const TeamKey kt = kicking;
  const TeamKey rt = other(kt);
  // (the kickoff specialist where the depth chart has one, else the kicker)
  const Player* kos = chartSpecialist(teamOf(kt), DSlot::KOS);
  const Player* k = kos ? kos : kicker(kt);
  const Ratings kickRt = kickR(*k, kt, false);
  const double fromX = safetyKick ? 20 : 35;
  safetyKick = false;
  phase = GamePhase::Scrimmage;
  const int m = margin(kt);
  if (!onside && q >= 4 && m < 0 && m >= -16 && clock < 180) onside = true;
  clockRunning = false;
  if (onside) {
    const bool rec = rng.chance(0.12);
    const double spot = fromX + 11;
    if (rec) {
      poss = kt;
      ballOn = spot;
      down = 1;
      toGo = 10;
      ballY = FIELD_W / 2;
      driveStartBallOn = ballOn;
      inRedZone = false;
      addLog(teamOf(kt).abbr + " onside kick... RECOVERED by the kicking team at the " + ydLine() + "!", LogTag::Turnover);
    } else {
      poss = rt;
      ballOn = 100 - spot;
      down = 1;
      toGo = 10;
      ballY = FIELD_W / 2;
      driveStartBallOn = ballOn;
      inRedZone = false;
      addLog(teamOf(kt).abbr + " onside kick recovered by " + teamOf(rt).abbr + " at the " + ydLine() + ".", LogTag::Info);
    }
    runClock(rng.range(2, 4), true);
    return SpecialResult{log.back().desc, mkFlight(fromX, spot, FIELD_W / 2, 1.2, 2)};
  }
  // modern dynamic kickoff: touchback at the 35, landing zone returns
  const std::optional<UserKick> uk = userKick;
  userKick.reset();
  double dist = 60 + (kickRt.kickPower - 75) * 0.35 + windHelp(kt) * 0.8 + rng.normal(0, 4);
  double hang = 3.9 + rng.normal(0, 0.3), landY = -1;
  if (uk) {
    // the user's kick: power the distance (a full swing deep into the end zone for a big leg), aim and accuracy its line
    const double pw = clamp(uk->power, 0, 1.15), over = (std::max)(0.0, pw - 1.0) / 0.15;
    const double full = 67 + (kickRt.kickPower - 75) * 0.35 + windHelp(kt) * 0.8;
    const double accDeg = uk->acc * 16.0 * (1.35 - kickRt.kickAcc / 100.0 * 0.7) + (over > 0 ? rng.normal(0, over * 3.0) : 0.0);
    const double ang = (uk->aimDeg + accDeg) * kPi / 180;
    const double g = full * (0.45 + 0.55 * (std::min)(pw, 1.0));
    hang = 3.0 + 1.1 * (std::min)(pw, 1.0);
    dist = g * std::cos(ang);
    landY = FIELD_W / 2 + g * std::sin(ang) + 0.3 * (-weather.windY * worldDir(kt)) * hang;
    const double landU = fromX + dist;
    // out of bounds, or short of the landing zone (the receiving 20): the receiving team takes it at its 40
    const bool oob = landY < 0 || landY > FIELD_W;
    if (oob || landU < 80) {
      poss = rt;
      ballOn = 40;
      down = 1;
      toGo = 10;
      ballY = FIELD_W / 2;
      driveStartBallOn = ballOn;
      inRedZone = false;
      clockRunning = false;
      addLog(nm(k) + " kicks " + SR(dist) + " yards, " + (oob ? "out of bounds" : "short of the landing zone") + ". " + teamOf(rt).abbr + " ball at its 40.", LogTag::Penalty);
      KickFlight f = mkFlight(fromX, (std::min)(landU, 109.0), FIELD_W / 2, hang, 20);
      f.endY = oob ? (landY < 0 ? -1.0 : FIELD_W + 1.0) : clamp(landY, 1, FIELD_W - 1);
      return SpecialResult{log.back().desc, f};
    }
  }
  const double land = fromX + dist;  // in kicking team frame
  // the flight the renderer draws: down the line the user kicked it (it went straight down the middle whatever the aim)
  auto koFlight = [&](double toX) {
    KickFlight f = mkFlight(fromX, toX, FIELD_W / 2, hang, 22);
    if (landY >= 0) f.endY = clamp(landY, 1, FIELD_W - 1);
    return f;
  };
  double start;
  std::string desc;
  Player* kr = nullptr;
  {
    std::vector<Player*> c;
    for (const auto& p : teamOf(rt).roster)
      if (p->pos == Pos::WR || p->pos == Pos::RB || p->pos == Pos::CB) c.push_back(p.get());
    std::stable_sort(c.begin(), c.end(), [](const Player* a, const Player* b) {
      return b->r.speed + b->r.elusiveness - a->r.speed - a->r.elusiveness < 0;
    });
    kr = c.empty() ? nullptr : c[0];
  }
  // a kick into the end zone is downed there three times in four (a user's kick: always from 3 yd deep). On the field
  // (liveReturn) the returner still fields it and takes the knee himself - only a ball over the end line is nobody's
  const bool downed = land >= 100 && (uk ? land >= 103 || rng.chance(0.5) : rng.chance(0.75));
  if (downed && !(liveReturn && land < 109.0)) {
    start = 35;
    desc = nm(k) + " kicks " + SR(dist) + " yards into the end zone. Touchback.";
  } else if (liveReturn) {
    // a live return: the receiving team gets the ball when the play is over (finishKickReturn)
    KickReturnSpec s;
    s.kickX = 100 - fromX;
    s.coverX = 40;  // dynamic kickoff: the coverage lines up at the receiving 40 and releases on the catch
    s.landX = (std::max)(downed ? -8.6 : -6.0, 100 - land);
    s.downIt = downed;
    // (the return is played in the receiving team's frame, the kicking team's turned round: its y mirrored - the
    // aimed kick came down on the far side of the field from the arrow)
    s.landY = landY >= 0 ? FIELD_W - clamp(landY, 3, FIELD_W - 3) : clamp(FIELD_W / 2 + rng.normal(0, 5), 8, FIELD_W - 8);
    s.hang = hang;
    s.apex = 24;
    s.kickT = 40.0 / 30.0;  // the kicker's whole approach (the pack's kickoff capture: foot on the ball 1.33 s in)
    poss = rt;
    pendingReturn = PendingReturn{s, nm(k) + " kicks " + SR(dist) + " yards", false};
    return SpecialResult{pendingReturn->kickDesc, koFlight((std::min)(land, 109.0))};
  } else {
    const double catchAt = clamp(100 - land, -5, 25);
    const double ret = (std::max)(0.0, rng.normal(23.5, 7) + (kr->r.speed - 85) * 0.2);
    start = clamp((std::max)(0.0, catchAt) + ret, 1, 99);
    // long returns / TD
    if (rng.chance(0.012 + (kr->r.elusiveness - 75) * 0.0004)) start += rng.range(20, 50);
    if (rng.chance(0.004)) start = 100;
    // muffed / fumbled return
    if (rng.chance(0.004)) {
      poss = kt;
      ballOn = 100 - clamp(start, 5, 95);
      down = 1;
      toGo = (std::min)(10.0, 100 - ballOn);  // (inside the 10 it is 1st and goal: it was 1st and 10 at the 5)
      ballY = FIELD_W / 2;
      addLog(nm(k) + " kicks off. " + nm(kr) + " FUMBLES the return! Recovered by " + teamOf(kt).abbr + " at the " + ydLine() + ".",
             LogTag::Turnover);
      runClock(6, true);
      return SpecialResult{log.back().desc, koFlight(land)};
    }
    if (start >= 100) {
      poss = rt;
      score[rt] += 6;
      addLog(nm(k) + " kicks off. " + nm(kr) + " returns it ALL THE WAY! TOUCHDOWN " + teamOf(rt).abbr + "!", LogTag::Td);
      stats.line(*kr, teamOf(rt).abbr).defTD++;
      phase = GamePhase::Pat;
      ballOn = 85;
      runClock(11, true);
      return SpecialResult{log.back().desc, koFlight(land)};
    }
    const int rs = jsRoundI(start);
    desc = nm(k) + " kicks " + SR(dist) + " yards, returned by " + nm(kr) + " to the " + teamOf(rt).abbr + " " + S(rs <= 50 ? rs : 100 - rs) + ".";
    returnFoul(rt, start, desc);
    runClock(rng.range(5, 8), true);
  }
  poss = rt;
  ballOn = start;
  down = 1;
  toGo = (std::min)(10.0, 100 - ballOn);  // (a return inside the 10: 1st and goal)
  ballY = FIELD_W / 2;
  driveStartBallOn = ballOn;
  inRedZone = false;
  clockRunning = start != 35;  // returned kicks: clock keeps running
  addLog(desc);
  return SpecialResult{desc, koFlight((std::min)(land, 109.0))};
}

SpecialResult Game::punt(bool liveReturn) {
  const TeamKey kt = poss;
  const Player* p = punter(kt);
  const Ratings pr = kickR(*p, kt, true);
  TeamLine& T = team[kt];
  T.punts++;
  const double los = ballOn;
  PlayerLine& l = stats.line(*p, offense().abbr);
  runClock(rng.range(5, 9), true);
  const std::optional<UserKick> uk = userKick;
  userKick.reset();
  if (rng.chance(tune().kickBlock > 0 ? tune().kickBlock : 0.006)) {
    addLog(nm(p) + " punt is BLOCKED! Recovered by " + defense().abbr + ".", LogTag::Turnover);
    changePossessionRaw(100 - (los - 7));
    if (q == 5) otDriveEnded();
    // (a flight for the renderer: off the rusher's hands at the punter's foot, up and loose to where it is recovered -
    // without one the broadcast showed a full 40-yard punt and then gave the ball to the defense at the line)
    return SpecialResult{log.back().desc, mkFlight(los - 14, los - 7, FIELD_W / 2, 1.1, 4)};
  }
  double gross = 46.5 + (pr.kickPower - 80) * 0.3 + windHelp(kt) * 1.0 + rng.normal(0, 6.5);
  // short-field punting: aim to pin
  const double room = 100 - los;
  double landY = FIELD_W / 2, hangU = -1;
  if (uk) {
    // the user's punt: power is the distance (a full swing this punter's best), aim and the accuracy miss its line, a
    // swing well off the centre a shank
    const double pw = clamp(uk->power, 0, 1.15), over = (std::max)(0.0, pw - 1.0) / 0.15;
    const double full = 52 + (pr.kickPower - 80) * 0.3 + windHelp(kt) * 1.0;
    double g = full * (0.35 + 0.65 * (std::min)(pw, 1.0));
    double accDeg = uk->acc * 16.0 * (1.35 - pr.kickAcc / 100.0 * 0.7) + (over > 0 ? rng.normal(0, over * 4.0) : 0.0);
    const bool shank = std::fabs(uk->acc) > 0.85 || (over > 0.6 && rng.chance(0.5));
    if (shank) {
      g *= 0.55;
      accDeg *= 1.6;
    }
    hangU = 3.0 + 1.5 * (std::min)(pw, 1.0) - (shank ? 1.1 : 0.0);
    const double ang = (uk->aimDeg + accDeg) * kPi / 180;
    landY = FIELD_W / 2 + g * std::sin(ang) + 0.3 * (-weather.windY * worldDir(kt)) * hangU;
    gross = g * std::cos(ang);
    if (landY < 0 || landY > FIELD_W) {
      // out of bounds: dead where it crosses the sideline (no return)
      const double cross = los + gross * (FIELD_W / 2) / (std::max)(0.5, std::fabs(landY - FIELD_W / 2));
      const double start = cross >= 100 ? 20 : 100 - cross;
      std::string desc;
      if (cross >= 100) desc = nm(p) + " punts " + SR(100 - los) + " yards into the end zone. Touchback.";
      else {
        const int cs = jsRoundI(start);
        desc = nm(p) + " punts " + SR(cross - los) + " yards, out of bounds at the " + (cs <= 50 ? defense().abbr + " " + S(cs) : SR(100 - start)) + ".";
      }
      l.punts++;
      l.puntYds += jsRoundI((std::min)(cross, 100.0) - los);
      addLog(desc);
      if (q == 5) otDriveEnded();
      if (phase != GamePhase::Final) {
        changePossessionRaw(clamp(start, 1, 99));
        clockRunning = false;
      }
      KickFlight f = mkFlight(los - 14, (std::min)(cross, 105.0), FIELD_W / 2, hangU, 22);
      f.endY = landY < 0 ? -1.0 : FIELD_W + 1.0;
      return SpecialResult{desc, f};
    }
  }
  double land = los + gross;
  // the flight the renderer draws: down the line the user punted it (it went straight down the middle whatever the aim)
  auto ptFlight = [&](KickFlight f) {
    if (uk) f.endY = clamp(landY, 1, FIELD_W - 1);
    return f;
  };
  std::string desc;
  double start;
  if (room < 50 && !uk) {
    // coffin corner style
    const double target = 100 - rng.range(3, 15);
    land = (std::min)(land, target + rng.normal(0, 4));
    gross = land - los;
  }
  if (land >= 100) {
    start = 20;
    desc = nm(p) + " punts " + SR(100 - los) + " yards into the end zone. Touchback.";
    gross = 100 - los;
  } else {
    const double r = rng.next();
    const double catchSpot = 100 - land;  // receiving team frame
    if (liveReturn && !(r < 0.38 || catchSpot < 12)) {
      // a live return (no muff roll: the return play decides what happens)
      const double hang = hangU > 0 ? hangU : 4.3 + rng.normal(0, 0.3);
      KickReturnSpec s;
      s.punt = true;
      // (the punt point of the pack's punt capture: the punter sets up 13.8 yd behind the ball and walks 4 yd into it)
      s.kickX = 100 - (los - 9.5);
      s.coverX = 100 - los;
      s.landX = catchSpot;
      // (the receiving team's frame: the kicking team's turned round, its y mirrored)
      s.landY = uk ? FIELD_W - clamp(landY, 3, FIELD_W - 3) : clamp(FIELD_W / 2 + rng.normal(0, 6), 8, FIELD_W - 8);
      s.hang = hang;
      s.kickT = 2.0;  // the snap, the catch, two steps and the punt (the capture's 1.93 s)
      s.apex = 26;
      l.punts++;
      l.puntYds += jsRoundI(gross);
      poss = other(kt);
      pendingReturn = PendingReturn{s, nm(p) + " punts " + SR(gross) + " yards", true};
      KickFlight lf = mkFlight(los - 14, (std::min)(land, 105.0), FIELD_W / 2, hang, 26);
      if (uk) lf.endY = clamp(landY, 1, FIELD_W - 1);
      return SpecialResult{pendingReturn->kickDesc, lf};
    }
    if (r < 0.38 || catchSpot < 12) {
      start = catchSpot;
      const int cs = jsRoundI(catchSpot);
      desc = nm(p) + " punts " + SR(gross) + " yards, " + (catchSpot < 12 ? "downed" : "fair catch") + " at the " +
             (cs <= 50 ? defense().abbr + " " + S(cs) : SR(100 - catchSpot)) + ".";
    } else {
      double ret = (std::max)(-2.0, rng.normal(9.5, 7));
      if (rng.chance(0.03)) ret += rng.range(15, 40);
      if (rng.chance(0.003)) ret = 200;
      start = catchSpot + ret;
      if (rng.chance(0.006)) {
        // muff
        addLog(nm(p) + " punts " + SR(gross) + " yards. MUFFED! " + offense().abbr + " recovers!", LogTag::Turnover);
        l.punts++;
        l.puntYds += jsRoundI(gross);
        ballOn = clamp(land, 1, 99);
        down = 1;
        toGo = (std::min)(10.0, 100 - ballOn);
        ballY = FIELD_W / 2;
        return SpecialResult{log.back().desc, ptFlight(mkFlight(los - 14, land, FIELD_W / 2, 4.4, 26))};
      }
      if (start >= 100) {
        l.punts++;
        l.puntYds += jsRoundI(gross);
        score[other(kt)] += 6;
        poss = other(kt);
        phase = GamePhase::Pat;
        ballOn = 85;
        addLog(nm(p) + " punts " + SR(gross) + " yards... RETURNED FOR A TOUCHDOWN by " + defense().abbr + "!", LogTag::Td);
        if (q == 5) finish();
        return SpecialResult{log.back().desc, ptFlight(mkFlight(los - 14, land, FIELD_W / 2, 4.4, 26))};
      }
      desc = nm(p) + " punts " + SR(gross) + " yards, returned " + SR(ret) + " yards.";
      returnFoul(other(kt), start, desc);
    }
  }
  l.punts++;
  l.puntYds += jsRoundI(gross);
  addLog(desc);
  if (q == 5) otDriveEnded();
  if (phase != GamePhase::Final) {
    changePossessionRaw(clamp(start, 1, 99));
    clockRunning = desc.find("returned") != std::string::npos && desc.find("PENALTY") == std::string::npos;
  }
  const double hang = hangU > 0 ? hangU : 4.3 + rng.normal(0, 0.3);
  KickFlight f = mkFlight(los - 14, (std::min)(land, 105.0), FIELD_W / 2, hang, 26);
  if (uk) f.endY = clamp(landY, 1, FIELD_W - 1);
  return SpecialResult{desc, f};
}

std::unique_ptr<PlaySim> Game::kickReturnSim(const std::vector<Player*>& recv, const std::vector<Player*>& kick,
                                             std::optional<Side> userSide) {
  // the return is the receiving team's "play": a run with no scheme (the carrier AI picks its lanes)
  static OffPlay ret;
  if (ret.id.empty()) {
    ret.id = "kick_return";
    ret.name = "Kick return";
    ret.formation = FORMATIONS()[0].id;
    ret.type = PlayType::Run;
  }
  PlayConfig cfg;
  cfg.off = &offense();
  cfg.def = &defense();
  cfg.offPlay = &ret;
  cfg.defCall = nullptr;
  cfg.los = pendingReturn ? pendingReturn->spec.landX : ballOn;
  cfg.ballY = FIELD_W / 2;
  cfg.weather = weather;
  cfg.worldDir = worldDir();
  cfg.rng = rng.fork();
  cfg.userSide = userSide;
  if (pendingReturn) cfg.kick = pendingReturn->spec;
  cfg.offUnit = recv;
  cfg.defUnit = kick;
  cfg.abil = abilCtx();
  return std::make_unique<PlaySim>(cfg);
}

PlayLog Game::finishKickReturn(PlaySim& sim) {
  const PlayResult& r = *sim.result;
  const PendingReturn pr = pendingReturn ? *pendingReturn : PendingReturn{};
  pendingReturn.reset();
  const TeamKey rt = poss, kt = other(rt);
  Player* kr = sim.returner ? sim.returner->p : nullptr;
  // (a kick downed in the end zone takes no time off the clock)
  if (!(r.endX <= 0 && !r.turnover && !r.td)) runClock(r.duration, true);
  for (Ent* e : sim.ents)
    e->p->fatigue = clamp(e->p->fatigue + (0.028 + r.duration * 0.0035) * (1.25 - e->p->r.stamina / 120), 0, 1);
  const std::string who = kr ? nm(kr) : std::string("the returner");
  down = 1;
  toGo = 10;
  ballY = FIELD_W / 2;
  inRedZone = false;
  // (overtime: the punt ended the kicking team's possession, and a touchdown on the return - the return team's, or the
  // kicking team's off a fumble - is a score after both teams have had the ball. Only a return that did not score counted
  // the punt, and a punt run back for a touchdown in overtime gave the punting team another possession)
  const bool otPuntScore = pr.punt && q == 5 && r.td;
  if (r.td && *r.td == Side::Off) {
    score[rt] += 6;
    if (kr) stats.line(*kr, teamOf(rt).abbr).defTD++;
    phase = GamePhase::Pat;
    ballOn = 85;
    addLog(pr.kickDesc + ". " + who + " returns it ALL THE WAY! TOUCHDOWN " + teamOf(rt).abbr + "!", LogTag::Td);
    if (otPuntScore) otDriveEnded();  // (ahead after both have had it: over, no try)
    return log.back();
  }
  if (r.turnover) {
    if (r.td) {
      score[kt] += 6;
      poss = kt;
      phase = GamePhase::Pat;
      ballOn = 85;
      addLog(pr.kickDesc + ". " + who + " FUMBLES, returned for a TOUCHDOWN by " + teamOf(kt).abbr + "!", LogTag::Td);
      if (otPuntScore) otDriveEnded();
      return log.back();
    }
    poss = kt;
    ballOn = clamp(100 - r.endX, 1, 99);
    toGo = (std::min)(10.0, 100 - ballOn);
    phase = GamePhase::Scrimmage;
    driveStartBallOn = ballOn;
    addLog(pr.kickDesc + ". " + who + " FUMBLES the return! Recovered by " + teamOf(kt).abbr + " at the " + ydLine() + ".", LogTag::Turnover);
    return log.back();
  }
  phase = GamePhase::Scrimmage;
  // downed in his own end zone after fielding the kick: a touchback, not a safety
  const bool tb = r.endX <= 0;
  ballOn = tb ? (pr.punt ? 20 : 35) : clamp(r.endX, 1, 99);
  driveStartBallOn = ballOn;
  clockRunning = !tb && !r.outOfBounds;
  const int rs = jsRoundI(ballOn);
  std::string foul;
  if (!tb && returnFoul(rt, ballOn, foul)) {
    driveStartBallOn = ballOn;
    clockRunning = false;
  }
  toGo = (std::min)(10.0, 100 - ballOn);  // (a return inside the 10: 1st and goal)
  if (tb)
    addLog(pr.kickDesc + ", fielded in the end zone by " + who + " and downed. Touchback.");
  else
    addLog(pr.kickDesc + ", returned " + SR((std::max)(0.0, r.endX - sim.catchX)) + " yards by " + who + " to the " + teamOf(rt).abbr + " " +
           S(rs <= 50 ? rs : 100 - rs) + "." + foul);
  if (pr.punt && q == 5) otDriveEnded();
  return log.back();
}

// A place kick is played, not rolled. The strike is drawn - the leg (how far out this one would still clear the bar) and
// the line (where the ball is across the posts' plane) - the ball is flown, and the kick is good if it crosses above the
// crossbar between the uprights. The two spreads come from the kicker, the distance and the weather, set so that kicks go
// through as often as fgProb says.
Game::PlaceKick Game::placeKick(double fromX, double p, const Player& k, TeamKey kt, double blockP) {
  PlaceKick o;
  const double plane = 110 - fromX;
  const double drag = 0.0045 * (1 + (60 - weather.tempF) * 0.0015);
  const Ratings kr = kickR(k, kt, false);
  const double legMu = 62 + (kr.kickPower - 80) * 0.3 + windHelp(kt) * 0.8 + 3, legSd = 3.5;
  const double pShort = 0.5 * std::erfc(-(plane - legMu) / (legSd * std::sqrt(2.0)));
  const double pLine = clamp(p / (std::max)(1e-6, 1 - pShort), 0.02, 0.9995);
  const double sigma = kUprightHalf / invNorm(0.5 + 0.5 * pLine);
  o.blocked = rng.chance(blockP);
  double leg = rng.normal(legMu, legSd);
  o.miss = clamp(rng.normal(0, sigma), -14, 14);
  if (userKick) {
    // the user's swing: the meter's power is the leg (a full one is this kicker's range on a good day), its line the
    // aim plus the accuracy miss (degrees per unit off the centre from the kicker's accuracy), and the crosswind
    const UserKick u = *userKick;
    userKick.reset();
    const double pw = clamp(u.power, 0, 1.15), over = (std::max)(0.0, pw - 1.0) / 0.15;
    leg = (legMu + 2) * (0.42 + 0.58 * (std::min)(pw, 1.0));
    const double devDeg = 13.0 * (1.35 - kr.kickAcc / 100.0 * 0.7);
    const double accDeg = u.acc * devDeg + (over > 0 ? rng.normal(0, over * 2.5) : 0.0);
    const double latWind = -weather.windY * worldDir(kt);  // yd/s toward the kicking team's left
    o.miss = clamp(plane * std::tan((u.aimDeg + accDeg) * kPi / 180) + 0.25 * latWind * plane / 20.0, -14, 14);
  }
  // (the launch angle about the kicker's line for this length: a low one is the one a hand at the line gets to -
  // Sim/PlaceKick.cpp plays the rush against it; the leg and the line above decide the make, whatever the angle)
  double elev = tune().fgPlay > 0 ? clamp(40 - 0.22 * plane + rng.normal(0, tune().fgElevSd), 23.0, 39.0) : clamp(40 - 0.22 * plane, 27.0, 36.0), v = 0;
  if (o.blocked) {
    // off a hand at the line: the ball pops up and dies
    elev = 52;
    v = rng.range(8, 11);
    o.miss = rng.range(-7, 7);
  } else {
    // the strike that would just clear the bar from `leg` yards
    double lo = 10, hi = 70;
    for (int i = 0; i < 40; ++i) {
      const double m = 0.5 * (lo + hi);
      (flyPlace(m, elev, leg, drag).z >= kCrossbar + 0.1 ? hi : lo) = m;
    }
    v = hi;
  }
  const PlaceFly at = flyPlace(v, elev, plane, drag);
  o.isShort = !o.blocked && at.z < kCrossbar + 0.1;
  o.good = !o.blocked && !o.isShort && std::fabs(o.miss) < kUprightHalf;
  o.v = v;
  o.elevDeg = elev;
  o.drag = drag;
  o.plane = plane;
  KickFlight& f = o.flight;
  f.fromX = fromX;
  f.y = FIELD_W / 2;
  f.hang = at.t;
  f.height = 12;
  f.good = o.good;
  f.vx = v * std::cos(elev * kPi / 180);
  f.vz = v * std::sin(elev * kPi / 180);
  if (at.z >= 0) {
    f.toX = 110;
    f.endY = FIELD_W / 2 + o.miss;
  } else {
    f.toX = fromX + at.land;
    f.endY = FIELD_W / 2 + o.miss * at.land / plane;
  }
  return o;
}

// A field goal is a play (Sim/PlaceKick.cpp): the snap, the hold, the protection against the rush, and the strike
// placeKick draws, flown from the spot - blocked only when a rusher gets a hand into its path.
SpecialResult Game::fieldGoal() {
  if (tune().fgPlay <= 0) return legacyFieldGoal();
  std::unique_ptr<PlaySim> sim = placeKickSim(false);
  sim->runToEnd(30);
  return finishPlaceKick(*sim);
}

// (the roll field goals were before they were played: Tune::fgPlay = 0, for A/B)
SpecialResult Game::legacyFieldGoal() {
  const TeamKey kt = poss;
  const Player* k = kicker(kt);
  const int dist = jsRoundI(100 - ballOn + 17);
  const double p = fgProb(dist, *k, kt);
  PlayerLine& l = stats.line(*k, offense().abbr);
  l.fga++;
  team[kt].fga++;
  runClock(rng.range(4, 6), true);
  const PlaceKick pk = placeKick(ballOn - 7, p / (1 - 0.012), *k, kt, tune().kickBlock > 0 ? tune().kickBlock : 0.012);
  const bool blocked = pk.blocked, good = pk.good;
  const KickFlight flight = pk.flight;
  if (good) {
    l.fgm++;
    team[kt].fgm++;
    score[kt] += 3;
    if (abilOn()) abil.afterKick(*k, true, true, offense().abbr);
    addLog(nm(k) + " " + S(dist) + "-yard field goal is GOOD.", LogTag::Fg);
    kicking = kt;
    phase = GamePhase::Kickoff;
    clockRunning = false;
    if (q == 5) otDriveEnded();
    return SpecialResult{log.back().desc, flight};
  }
  const bool post = std::fabs(pk.miss) < kUprightHalf + 0.25;
  const std::string how = blocked ? "BLOCKED" : pk.isShort ? "NO GOOD, short" : pk.miss < 0 ? (post ? "NO GOOD, off the right upright" : "NO GOOD, wide right")
                                                                                          : (post ? "NO GOOD, off the left upright" : "NO GOOD, wide left");
  if (abilOn()) abil.afterKick(*k, false, true, offense().abbr);
  addLog(nm(k) + " " + S(dist) + "-yard field goal is " + how + ".", LogTag::Turnover);
  if (q == 5) otDriveEnded();
  if (phase != GamePhase::Final) changePossessionRaw(100 - (std::max)(ballOn - 7, 20.0));
  return SpecialResult{log.back().desc, flight};
}

SpecialResult Game::legacyExtraPoint() {
  const TeamKey kt = poss;
  const Player* k = kicker(kt);
  PlayerLine& l = stats.line(*k, offense().abbr);
  l.xpa++;
  const double p = clamp(fgProb(33, *k, kt) - 0.03, 0.8, 0.99);
  const PlaceKick pk = placeKick(78, p, *k, kt, 0.0);
  const bool good = pk.good;
  if (good) {
    l.xpm++;
    score[kt] += 1;
  }
  if (abilOn()) abil.afterKick(*k, good, false, offense().abbr);
  addLog(nm(k) + " extra point is " + (good ? "GOOD" : "NO GOOD") + ".", good ? LogTag::None : LogTag::Info);
  afterPAT(kt);
  return SpecialResult{log.back().desc, pk.flight};
}

double Game::placeBlockRate(double dist, bool pat) {
  // nflverse 2023: 19 of 1,107 field goals blocked (0.4 % at 18-29 yd, 1.3 % 30-39, 2.4 % 40-49, 2.9 % 50+: the longer
  // the kick, the flatter it comes off the foot), 8 of 1,238 extra points (0.65 %)
  if (pat) return 0.0065;
  return clamp(0.004 + (dist - 25) * 0.001, 0.003, 0.032);
}

std::vector<Player*> Game::fgUnit(TeamKey key, bool kickTeam) const {
  // (the director's Pick11 for "K" and "D", for a game with nobody to show it to)
  const Team& T = teamOf(key);
  std::vector<Player*> healthy;
  for (const auto& p : T.roster)
    if (p->injuredWeeks <= 0) healthy.push_back(p.get());
  auto by = [&](std::initializer_list<Pos> ps) {
    std::vector<Player*> out;
    for (Player* p : healthy)
      for (Pos q2 : ps)
        if (p->pos == q2) {
          out.push_back(p);
          break;
        }
    std::stable_sort(out.begin(), out.end(), [](const Player* a, const Player* b) { return a->r.speed > b->r.speed; });
    return out;
  };
  std::vector<Player*> out;
  auto add1 = [&](Player* p) {
    if (p && out.size() < 11 && std::find(out.begin(), out.end(), p) == out.end() && p->injuredWeeks <= 0) out.push_back(p);
  };
  if (kickTeam) {
    add1(kicker(key));
    add1(punter(key));  // (the holder, as on most teams)
    add1(chartSpecialist(T, DSlot::LS));
    for (Player* p : by({Pos::C, Pos::OT, Pos::OG, Pos::TE}))
      if (out.size() < 9) add1(p);
    for (Player* p : by({Pos::TE, Pos::LB, Pos::FB, Pos::DE, Pos::OT, Pos::OG, Pos::RB, Pos::S})) add1(p);
  } else {
    for (Player* p : by({Pos::DT, Pos::DE})) add1(p);
    for (Player* p : by({Pos::LB, Pos::DE, Pos::S, Pos::CB, Pos::DT})) add1(p);
  }
  for (Player* p : healthy) add1(p);
  return out;
}

void Game::orderPlaceUnits(TeamKey kt, std::vector<Player*>& kick, std::vector<Player*>& block) const {
  Player* k = kicker(kt);
  // the eleven in their jobs (PlaySim::setupPlace): K, H, LS, LG, RG, LT, RT, TE, TE2, LW, RW
  std::vector<Player*> ku;
  {
    std::vector<Player*> pool = kick;
    auto takeIf = [&](auto pred) -> Player* {
      for (size_t i = 0; i < pool.size(); ++i)
        if (pred(pool[i])) {
          Player* p = pool[i];
          pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(i));
          return p;
        }
      return nullptr;
    };
    Player* kk = takeIf([&](const Player* p) { return p == k; });
    if (!kk) kk = takeIf([](const Player* p) { return p->pos == Pos::K; });
    if (!kk && !pool.empty()) kk = takeIf([](const Player*) { return true; });
    Player* pn = punter(kt);
    Player* h = takeIf([&](const Player* p) { return p == pn; });
    for (Pos hp : {Pos::P, Pos::QB, Pos::WR, Pos::CB, Pos::RB, Pos::S})
      if (!h) h = takeIf([hp](const Player* p) { return p->pos == hp; });
    const Player* lsChart = chartSpecialist(teamOf(kt), DSlot::LS);
    Player* ls = takeIf([&](const Player* p) { return p == lsChart; });
    if (!ls) ls = takeIf([](const Player* p) { return p->pos == Pos::C; });
    // the line heaviest first (guards, tackles), the ends tight ends where there are any, the wings what is left
    auto isLine = [](const Player* p) { return p->pos == Pos::OT || p->pos == Pos::OG || p->pos == Pos::C; };
    std::stable_sort(pool.begin(), pool.end(), [&](const Player* a, const Player* b) {
      if (isLine(a) != isLine(b)) return isLine(a);
      return a->weightLb > b->weightLb;
    });
    if (!ls && !pool.empty()) ls = takeIf([](const Player*) { return true; });
    if (!h && !pool.empty()) {
      h = pool.back();
      pool.pop_back();
    }
    std::vector<Player*> line;
    for (int i = 0; i < 4 && !pool.empty(); ++i) line.push_back(takeIf([](const Player*) { return true; }));
    // (both sides as good: the best two pass blockers at guard, then snaked - LG 1st, RG 2nd, RT 3rd, LT 4th)
    std::stable_sort(line.begin(), line.end(), [](const Player* a, const Player* b) { return a->r.passBlock > b->r.passBlock; });
    if (line.size() == 4) std::swap(line[2], line[3]);
    std::vector<Player*> ends;
    for (int i = 0; i < 2; ++i) {
      Player* te = takeIf([](const Player* p) { return p->pos == Pos::TE; });
      if (!te && !pool.empty()) te = takeIf([](const Player*) { return true; });
      if (te) ends.push_back(te);
    }
    for (Player* p : {kk, h, ls}) ku.push_back(p);
    for (Player* p : line) ku.push_back(p);
    for (Player* p : ends) ku.push_back(p);
    for (Player* p : pool) ku.push_back(p);
    ku.erase(std::remove(ku.begin(), ku.end(), nullptr), ku.end());
    if (ku.size() > 11) ku.resize(11);
  }
  // AL, AR, BL, BR, CL, CR (the six heaviest), EL, ER (the quickest of the rest), J (the tallest), SP, FL
  std::vector<Player*> bu = block;
  if (bu.size() > 11) bu.resize(11);
  std::stable_sort(bu.begin(), bu.end(), [](const Player* a, const Player* b) { return a->weightLb > b->weightLb; });
  if (bu.size() >= 6) {
    // (the six inside, the best rushers in the A gaps, snaked so neither side is the soft one: AL 1st, AR 2nd, BR 3rd,
    // BL 4th, CL 5th, CR 6th)
    std::stable_sort(bu.begin(), bu.begin() + 6, [](const Player* a, const Player* b) { return a->r.passRush + a->r.strength > b->r.passRush + b->r.strength; });
    std::swap(bu[2], bu[3]);
  }
  if (bu.size() > 6) {
    std::stable_sort(bu.begin() + 6, bu.end(), [](const Player* a, const Player* b) { return a->r.speed > b->r.speed; });
    if (bu.size() > 9) {
      std::stable_sort(bu.begin() + 8, bu.end(), [](const Player* a, const Player* b) { return a->heightIn > b->heightIn; });
    }
  }
  kick = ku;
  block = bu;
}

std::unique_ptr<PlaySim> Game::placeKickSim(bool pat, std::vector<Player*> kick, std::vector<Player*> block, std::optional<Side> userSide,
                                           double setT, int look) {
  static OffPlay place;
  if (place.id.empty()) {
    place.id = "place_kick";
    place.name = "Field goal";
    place.formation = FORMATIONS()[0].id;
    place.type = PlayType::Run;
  }
  const TeamKey kt = poss;
  if (kick.empty()) kick = fgUnit(kt, true);
  if (block.empty()) block = fgUnit(other(kt), false);
  Player* k = kicker(kt);
  std::vector<Player*> ku = kick, bu = block;
  orderPlaceUnits(kt, ku, bu);
  // the strike: the make rate is the game's (fgProb, for the length, the kicker, the weather) with the blocks the rush
  // makes in it
  const double los = pat ? 85.0 : ballOn;
  const double spotX = los - 7;
  const int dist = jsRoundI(100 - los + 17);
  const double pMake = pat ? clamp(fgProb(33, *k, kt) - 0.03, 0.8, 0.99) : fgProb(dist, *k, kt);
  const PlaceKick pk = placeKick(spotX, pMake / (1 - placeBlockRate(dist, pat)), *k, kt, 0.0);
  placeFlight_ = pk.flight;
  PlaceKickSpec s;
  s.pat = pat;
  s.spotX = spotX;
  s.setT = setT;
  // the operation: the snap, the hold and the foot, NFL-quick (1.25-1.35 s); now and then a snap off the holder's
  // hands that he has to dig out, and the rush has a tenth or two longer
  double op = clamp(tune().fgOpT + rng.normal(0, 0.04), tune().fgOpT - 0.1, tune().fgOpT + 0.12);
  if (rng.chance(0.03)) op += rng.range(0.1, 0.28);
  s.opT = op;
  s.catchT = 0.42 * op / 1.30;
  s.placeT = 0.78 * op / 1.30;
  s.v = pk.v;
  s.elevDeg = pk.elevDeg;
  s.drag = pk.drag;
  s.miss = pk.miss;
  s.plane = pk.plane;
  s.good = pk.good;
  s.isShort = pk.isShort;
  s.forceBlock = tune().kickBlock > 0 && rng.chance(tune().kickBlock);
  s.look = look;
  PlayConfig cfg;
  cfg.off = &offense();
  cfg.def = &defense();
  cfg.offPlay = &place;
  cfg.defCall = nullptr;
  cfg.los = los;
  cfg.ballY = FIELD_W / 2;
  cfg.weather = weather;
  cfg.worldDir = worldDir();
  cfg.rng = rng.fork();
  cfg.userSide = userSide;
  cfg.place = s;
  cfg.offUnit = ku;
  cfg.defUnit = bu;
  cfg.abil = abilCtx();
  return std::make_unique<PlaySim>(cfg);
}

SpecialResult Game::finishPlaceKick(PlaySim& sim) {
  const PlaceKickSpec& s = *sim.cfg.place;
  const PlayResult& r = *sim.result;
  const TeamKey kt = poss, dt = other(kt);
  Player* k = sim.placeKicker ? sim.placeKicker->p : kicker(kt);
  const double los = sim.los;
  const int dist = jsRoundI(100 - los + 17);
  const std::optional<KickFlight> flight = sim.placeBlocked ? std::nullopt : placeFlight_;
  placeFlight_.reset();
  PlayerLine& l = stats.line(*k, offense().abbr);
  if (s.pat) l.xpa++;
  else {
    l.fga++;
    team[kt].fga++;
    runClock((std::max)(rng.range(4, 6), r.duration - s.setT), true);
  }
  for (Ent* e : sim.ents)
    if (e->p) e->p->fatigue = clamp(e->p->fatigue + 0.01 * (1.25 - e->p->r.stamina / 120), 0, 1);
  const std::string what = s.pat ? nm(k) + " extra point" : nm(k) + " " + S(dist) + "-yard field goal";
  if (!sim.placeBlocked) {
    const bool good = sim.placeGood;
    const std::string tip = sim.placeTipped ? ", tipped at the line" : "";
    if (abilOn()) abil.afterKick(*k, good, !s.pat, offense().abbr);
    if (s.pat) {
      if (good) {
        l.xpm++;
        score[kt] += 1;
      }
      addLog(what + " is " + (good ? "GOOD" : "NO GOOD") + tip + ".", good ? LogTag::None : LogTag::Info);
      afterPAT(kt);
      return SpecialResult{log.back().desc, flight};
    }
    if (good) {
      l.fgm++;
      team[kt].fgm++;
      score[kt] += 3;
      addLog(what + " is GOOD" + tip + ".", LogTag::Fg);
      kicking = kt;
      phase = GamePhase::Kickoff;
      clockRunning = false;
      if (q == 5) otDriveEnded();
      return SpecialResult{log.back().desc, flight};
    }
    const double miss = sim.placeTipped ? sim.placeCrossY : s.miss;
    const bool post = std::fabs(miss) < kUprightHalf + 0.25;
    const std::string how = sim.placeShort ? "NO GOOD, short" : miss < 0 ? (post ? "NO GOOD, off the right upright" : "NO GOOD, wide right")
                                                                         : (post ? "NO GOOD, off the left upright" : "NO GOOD, wide left");
    addLog(what + " is " + how + tip + ".", LogTag::Turnover);
    if (q == 5) otDriveEnded();
    if (phase != GamePhase::Final) changePossessionRaw(100 - (std::max)(s.spotX, 20.0));
    return SpecialResult{log.back().desc, flight};
  }
  // blocked: off a rusher's hands, then a loose ball - whoever came up with it, wherever it ended
  if (abilOn()) abil.afterKick(*k, false, !s.pat, offense().abbr);
  std::string desc = what + " is BLOCKED";
  if (sim.placeBlockBy && sim.placeBlockBy->p) desc += " by " + nm(sim.placeBlockBy->p);
  Ent* rec = sim.recoveredBy;
  const Ent* holder = sim.ball.holder;
  const bool defBall = holder ? holder->side == Side::Def : true;
  const double endX = holder ? r.endX : clamp(sim.ball.pos.x, 0.5, 99.5);  // (nobody came up with it: where it lies)
  if (rec && rec->p) desc += std::string(", ") + (rec->side == Side::Def ? "recovered by " + teamOf(dt).abbr + " " : "recovered by ") + nm(rec->p);
  if (r.td) {
    const TeamKey scorer = *r.td == Side::Off ? kt : dt;
    if (holder && holder->p && scorer == dt) stats.line(*holder->p, teamOf(dt).abbr).defTD++;
    if (s.pat) {
      // a try: a blocked kick run back is two points for the defense (and two for a kick unit that ran it in)
      score[scorer] += 2;
      addLog(desc + " and returned for a DEFENSIVE TWO-POINT CONVERSION " + teamOf(scorer).abbr + "!", LogTag::Td);
      afterPAT(kt);
      return SpecialResult{log.back().desc, std::nullopt};
    }
    addLog(desc + (scorer == dt ? " and returned " + SR(los - endX) + " yards for a TOUCHDOWN " : ", TOUCHDOWN ") + teamOf(scorer).abbr + "!", LogTag::Td);
    scoreTD(scorer);
    return SpecialResult{log.back().desc, std::nullopt};
  }
  if (s.pat) {
    addLog(desc + ".", LogTag::Info);
    afterPAT(kt);
    return SpecialResult{log.back().desc, std::nullopt};
  }
  if (r.safety) {
    score[dt] += 2;
    addLog(desc + ", downed in the end zone. SAFETY! " + teamOf(dt).abbr + " +2.", LogTag::Safety);
    kicking = kt;
    poss = dt;
    phase = GamePhase::Kickoff;
    safetyKick = true;
    clockRunning = false;
    if (q == 5) finish();
    return SpecialResult{log.back().desc, std::nullopt};
  }
  if (!defBall && endX >= los + toGo) {
    // the kick unit fell on it and ran it past the sticks: a first down
    ballOn = clamp(endX, 1, 99);
    down = 1;
    toGo = (std::min)(10.0, 100 - ballOn);
    ballY = FIELD_W / 2;
    addLog(desc + " at the " + ydLine(ballOn, kt) + ". FIRST DOWN!");
    return SpecialResult{log.back().desc, std::nullopt};
  }
  if (!defBall && down < 4) {
    // (a field goal on an earlier down - the end of a half: the next down from where it ended)
    const double tg = toGo - (endX - los);
    ballOn = clamp(endX, 1, 99);
    down++;
    toGo = tg;  // (short of the sticks, so > 0; it was at least 1, which moved the line to gain - past the goal from inside the 1)
    ballY = FIELD_W / 2;
    addLog(desc + " at the " + ydLine(ballOn, kt) + ".");
    return SpecialResult{log.back().desc, std::nullopt};
  }
  const double at = clamp(endX, 0.5, 99.5);
  addLog(desc + (rec ? " at the " : ", dead at the ") + ydLine(at, kt) + ".", LogTag::Turnover);
  if (q == 5) otDriveEnded();
  if (phase != GamePhase::Final) changePossessionRaw(r.touchback ? 20.0 : 100 - at);
  return SpecialResult{log.back().desc, std::nullopt};
}

bool Game::goForTwo() {
  const int m = margin();  // after the 6
  const bool late = q >= 4 || (q == 3 && clock < 300);
  if (q == 5) return false;
  if (late) {
    for (int v : {-2, -5, -10, -13, -16, 1, 5, 12, -9})
      if (m == v) return true;
  }
  return rng.chance(0.04 + offense().aggression * 0.04);
}

SpecialResult Game::extraPoint() {
  // (played as a field goal from the 15: Sim/PlaceKick.cpp)
  if (tune().fgPlay <= 0) return legacyExtraPoint();
  std::unique_ptr<PlaySim> sim = placeKickSim(true);
  sim->runToEnd(30);
  return finishPlaceKick(*sim);
}

std::unique_ptr<PlaySim> Game::twoPointSim(const OffPlay& offPlay, const DefCall& defCall, std::optional<Side> userSide,
                                          std::optional<bool> flip) {
  ballOn = 98;
  ballY = FIELD_W / 2;
  down = 1;
  toGo = 2;
  return makePlay(offPlay, defCall, userSide, flip);
}

void Game::finishTwoPoint(PlaySim& sim) {
  const PlayResult& res = *sim.result;
  const TeamKey kt = poss;
  const bool good = res.td == Side::Off;
  if (good) score[kt] += 2;
  if (res.td == Side::Def) score[other(kt)] += 2;
  addLog("Two-point attempt: " + describe(res, *sim.cfg.offPlay, sim) + " " + (good ? "GOOD!" : "FAILED."), good ? LogTag::Td : LogTag::Info);
  afterPAT(kt);
}

void Game::afterPAT(TeamKey kt) {
  kicking = kt;
  phase = GamePhase::Kickoff;
  clockRunning = false;
  if (q == 5) otDriveEnded();
  if (clock <= 0 && phase != GamePhase::Final) endQuarter();
}

const OffPlay* Game::cpuTwoPointPlay() {
  // the team's short game from its book: quick passes, the goal line set, the short-yardage runs
  std::vector<const OffPlay*> pool;
  for (const OffPlay* p : offBookFor(offense()).plays) {
    if (p->hasTag("deep") || p->type == PlayType::Screen || (p->run && p->run->style == RunStyle::Sneak)) continue;
    if (p->hasTag("quick") || p->hasTag("goalline") || (p->run && p->hasTag("shortyard"))) pool.push_back(p);
  }
  if (pool.empty()) {
    static const std::vector<std::string> kIds = {"trips_stick", "dbl_slants", "dbl_mesh", "gl_dive", "gl_pa", "pistol_power", "empty_quick"};
    for (const OffPlay& p : OFF_PLAYS())
      if (std::find(kIds.begin(), kIds.end(), p.id) != kIds.end()) pool.push_back(&p);
  }
  return rng.pick(pool);
}

// ------------------------------------------------------------------ headless driver
std::optional<PlayLog> Game::simStep() {
  if (phase == GamePhase::Final) return std::nullopt;
  if (phase == GamePhase::Kickoff) {
    if (clock <= 0) {
      endQuarter();
      return std::nullopt;
    }
    kickoff();
    return log.back();
  }
  if (phase == GamePhase::Pat) {
    if (goForTwo()) {
      const OffPlay* op = cpuTwoPointPlay();
      const DefCall* dc = cpuDefense(*op);
      auto sim = twoPointSim(*op, *dc);
      sim->runToEnd();
      finishTwoPoint(*sim);
    } else {
      extraPoint();
    }
    return log.back();
  }
  if (!preSnap()) return std::nullopt;
  if (down == 4) {
    const FourthDownCall call = fourthDownCall();
    if (call == FourthDownCall::Punt) {
      punt();
      return log.back();
    }
    if (call == FourthDownCall::Fg) {
      fieldGoal();
      return log.back();
    }
  } else if (q == 2 || q >= 4) {
    // end-of-half field goal on any down
    const double fgDist = 100 - ballOn + 17;
    if (clock <= 6 && fgDist <= 56 && margin() >= -3 && (q == 2 || margin() >= -3)) {
      fieldGoal();
      return log.back();
    }
  }
  const std::optional<Penalty> pre = rollPresnap();
  if (pre) {
    applyPresnap(*pre);
    return log.back();
  }
  const OffPlay* op = cpuOffense();
  const DefCall* dc = cpuDefense(*op);
  auto sim = makePlay(*op, *dc);
  sim->runToEnd();
  return finishPlay(*sim);
}

void Game::simToEnd(int maxSteps) {
  int n = 0;
  while (phase != GamePhase::Final && n++ < maxSteps) simStep();
}

}  // namespace tag
