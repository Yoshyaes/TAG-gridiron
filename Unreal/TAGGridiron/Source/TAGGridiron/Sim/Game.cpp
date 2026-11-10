// Port of src/sim/game.ts
// Game rules engine: clock, downs, scoring, penalties, special teams, CPU coordinators.
// RNG call order and all decision thresholds follow the TS exactly.
#include "Game.h"

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
    if (q >= 4 && clock < 150 && defTrailing && timeouts[dk] > 0) {
      timeouts[dk]--;
      addLog("Timeout " + teamOf(dk).abbr + " (" + S(timeouts[dk]) + " left).", LogTag::Info);
      run = 0;
    } else if (tp == Tempo::Hurry && timeouts[poss] > 0 && clock < 50 && clock > 3) {
      timeouts[poss]--;
      addLog("Timeout " + offense().abbr + " (" + S(timeouts[poss]) + " left).", LogTag::Info);
      run = 0;
    } else if (q == 2 && clock < 45 && timeouts[poss] > 0 && ballOn > 55 && margin() >= -16) {
      timeouts[poss]--;
      addLog("Timeout " + offense().abbr + " (" + S(timeouts[poss]) + " left).", LogTag::Info);
      run = 0;
    }
    runClock(run);
  }
  untimed = false;
  lastOOB = false;
  if (clock <= 0) {
    endQuarter();
    return false;
  }
  return true;
}

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
  const int snaps = 5 - down;
  const double burn = (std::max)(0, snaps - oppTO) * 40 + snaps * 1.5;
  if (q >= 4 && clock <= burn) return true;
  if (q == 2 && clock <= 30 && ballOn < 60 && m > 0) return true;
  return false;
}

FourthDownCall Game::fourthDownCall() {
  const Team& t = offense();
  const double fgDist = 100 - ballOn + 17;
  const Player* k = kicker(poss);
  const double range = 55 + (k->r.kickPower - 80) * 0.3 + windHelp(poss) * 0.6;
  const int m = margin();
  const double secs = q >= 4 ? clock : 9999;
  // desperation
  if (q >= 4 && m < 0) {
    if (m >= -3 && fgDist <= range && secs < 120) return FourthDownCall::Fg;
    if (secs < 240 || (m < -8 && secs < 480)) return FourthDownCall::Go;
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
  double pPass = t.passLean - 0.04;
  const int d = down;
  const double tg = toGo;
  if (d == 1) pPass += tg > 10 ? 0.15 : -0.03;
  if (d == 2) pPass += tg >= 7 ? 0.1 : tg <= 3 ? -0.18 : 0;
  if (d == 3) pPass += tg >= 4 ? 0.3 : tg <= 2 ? -0.18 : 0.1;
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
  std::vector<const OffPlay*> pool;
  for (const OffPlay& p : OFF_PLAYS()) {
    if (p.type == PlayType::Kneel || p.type == PlayType::Spike) continue;
    const bool isPass = p.type != PlayType::Run;
    if (isPass != pass) continue;
    if (goal && p.hasTag("deep")) continue;
    if (!goal && p.formation == "heavy" && !shortY) continue;
    if (p.id == "gl_sneak" && !(shortY && tg <= 1)) continue;
    pool.push_back(&p);
  }
  std::vector<double> w;
  w.reserve(pool.size());
  for (const OffPlay* pp : pool) {
    const OffPlay& p = *pp;
    double x = 1;
    if (shortY && p.hasTag("shortyard")) x += 2.5;
    if (goal && p.hasTag("goalline")) x += 2;
    if (longY && (p.hasTag("long") || p.hasTag("deep") || p.hasTag("mid"))) x += 1.2;
    if (longY && p.hasTag("quick")) x *= 0.5;
    if (tp == Tempo::Hurry && (p.hasTag("quick") || p.hasTag("short"))) x += 0.8;
    if (p.type == PlayType::PlayAction && d == 1) x += 0.6;
    if (p.type == PlayType::Screen && longY) x += 0.6;
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
  Front front;
  if (goal || (shortY && (pers == "22" || pers == "21"))) front = Front::Goalline;
  else if (pers == "21" || pers == "12" || pers == "22") front = Front::Base;
  else front = (longY && down == 3 && rng.chance(0.5)) ? Front::Dime : Front::Nickel;
  std::vector<const DefCall*> pool;
  for (const DefCall& d : DEF_PLAYS())
    if (d.front == front) pool.push_back(&d);
  std::vector<double> w;
  for (const DefCall* dp : pool) {
    const DefCall& d = *dp;
    double x = 1;
    const bool blitz = !d.blitz.empty();
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
std::optional<Penalty> Game::rollPresnap() {
  const double r = rng.next();
  const double road = poss == TeamKey::Away ? 0.004 : 0;
  if (r < 0.014 + road) return mkPen("False Start", Side::Off, 5, false, PenaltySpot::Los, true);
  if (r < 0.018 + road) return mkPen("Delay of Game", Side::Off, 5, false, PenaltySpot::Los, true);
  if (r < 0.028 + road) {
    static const std::vector<std::string> kNames = {"Neutral Zone Infraction", "Offside", "Encroachment"};
    return mkPen(rng.pick(kNames), Side::Def, 5, false, PenaltySpot::Los, true);
  }
  return std::nullopt;
}

std::optional<Penalty> Game::rollPostsnap(const PlayResult& res, const OffPlay& play) {
  const double r = rng.next();
  const bool isPass = play.type != PlayType::Run && res.kind != PlayKind::Kneel && res.kind != PlayKind::Spike;
  double acc = 0;
  auto roll = [&](double p) { return r < (acc += p); };
  if (res.kind == PlayKind::Kneel || res.kind == PlayKind::Spike) return std::nullopt;
  if (roll(isPass ? 0.026 : 0.036)) return mkPen("Offensive Holding", Side::Off, 10, false, PenaltySpot::Los, true);
  if (isPass && res.incomplete && res.airYards >= 5 && roll(res.deepContested ? 0.09 : 0.012))
    return mkPen("Defensive Pass Interference", Side::Def, 0, true, PenaltySpot::Spot, false, static_cast<double>(res.airYards));
  if (isPass && roll(0.014))
    return mkPen(rng.chance(0.6) ? "Defensive Holding" : "Illegal Contact", Side::Def, 5, true, PenaltySpot::Los, false);
  if (isPass && res.pressured && roll(0.012)) return mkPen("Roughing the Passer", Side::Def, 15, true, PenaltySpot::End, false);
  if (isPass && res.complete && res.airYards >= 8 && roll(0.005))
    return mkPen("Offensive Pass Interference", Side::Off, 10, false, PenaltySpot::Los, true);
  if (roll(0.004)) {
    static const std::vector<std::string> kNames = {"Unnecessary Roughness", "Face Mask"};
    return mkPen(rng.pick(kNames), Side::Def, 15, true, PenaltySpot::End, false);
  }
  if (roll(0.002)) return mkPen("Illegal Block in the Back", Side::Off, 10, false, PenaltySpot::Los, true);
  return std::nullopt;
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
  return std::make_unique<PlaySim>(cfg);
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
  // fatigue / injuries
  for (Ent* e : sim.ents) {
    Player* p = e->p;
    p->fatigue = clamp(p->fatigue + (0.028 + res.duration * 0.0035) * (1.25 - p->r.stamina / 120), 0, 1);
  }
  std::set<int> onField;
  for (Ent* e : sim.ents) onField.insert(e->p->id);
  for (const auto& p : home->roster)
    if (!onField.count(p->id)) p->fatigue = (std::max)(0.0, p->fatigue - 0.05);
  for (const auto& p : away->roster)
    if (!onField.count(p->id)) p->fatigue = (std::max)(0.0, p->fatigue - 0.05);
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
  bool usePen = false;
  if (pen) {
    // compare options from offense's perspective
    const double rEP = resultEP(res, los);
    const double penBall = enforce(*pen, los, res.endX);
    double penEP;
    if (pen->on == Side::Def) {
      const bool first = pen->autoFirst || penBall >= los + toGo;
      penEP = first ? expectedPoints(penBall, 1, 10) : expectedPoints(penBall, down, toGo - (penBall - los));
      usePen = penEP > rEP && !res.td;
      if (res.td == Side::Off) usePen = false;
    } else {
      penEP = expectedPoints(penBall, down, toGo + (los - penBall));
      usePen = penEP < rEP;
    }
    if (usePen) {
      const TeamKey k = pen->on == Side::Off ? ok : dk;
      team[k].penalties++;
      team[k].penYds += jsRoundI(std::fabs(penBall - (pen->spot == PenaltySpot::End ? res.endX : los)));
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
      const std::string line = describe(res, play, sim) + " PENALTY on " + pt.abbr + ": " + pen->name + ", enforced. " + downText() + " at " + ydLine() + ".";
      addLog(line, LogTag::Penalty);
      // half-distance etc handled; stats for the nullified play are not counted
      return log.back();
    } else {
      const Team& pt = pen->on == Side::Off ? O : D;
      pen->name = pen->name + " on " + pt.abbr + " (declined)";
    }
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
      if (yards >= 12) T.explosive++;
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
    case PlayKind::Scramble:
      return qbN + " scrambles for " + yd(y) + oob + ".";
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
Player* Game::kicker(TeamKey k) const {
  Player* best = nullptr;
  for (const auto& p : teamOf(k).roster)
    if (p->pos == Pos::K && (!best || p->ovr > best->ovr)) best = p.get();
  return best;
}
Player* Game::punter(TeamKey k) const {
  Player* best = nullptr;
  for (const auto& p : teamOf(k).roster)
    if (p->pos == Pos::P && (!best || p->ovr > best->ovr)) best = p.get();
  return best;
}

double Game::fgProb(double dist, const Player& k, TeamKey kt) const {
  const double eff = dist - (k.r.kickAcc - 80) * 0.35 - windHelp(kt) * 0.5 + std::fabs(weather.windY) * 0.4 +
                     (weather.precip != Precip::None ? 3 : 0) + (weather.tempF < 32 ? 2 : 0);
  const double maxR = 62 + (k.r.kickPower - 80) * 0.3 + windHelp(kt) * 0.8;
  // logistic shaped to: 30y ~ 97%, 40y ~ 90%, 50y ~ 76%, 55y ~ 66%
  double p = sigmoid(5.4 - 0.083 * eff + (eff < 40 ? 0 : -0.012 * (eff - 40) * (eff - 40) / 10));
  if (dist > maxR) p *= (std::max)(0.0, 1 - (dist - maxR) / 5);
  return clamp(p, 0, 0.995);
}

SpecialResult Game::kickoff(bool onside, bool liveReturn) {
  const TeamKey kt = kicking;
  const TeamKey rt = other(kt);
  const Player* k = kicker(kt);
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
  const double dist = 60 + (k->r.kickPower - 75) * 0.35 + windHelp(kt) * 0.8 + rng.normal(0, 4);
  const double land = fromX + dist;  // in kicking team frame
  const double hang = 3.9 + rng.normal(0, 0.3);
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
  if (land >= 100 && rng.chance(0.75)) {
    start = 35;
    desc = nm(k) + " kicks " + SR(dist) + " yards into the end zone. Touchback.";
  } else if (liveReturn) {
    // a live return: the receiving team gets the ball when the play is over (finishKickReturn)
    KickReturnSpec s;
    s.kickX = 100 - fromX;
    s.coverX = 40;  // dynamic kickoff: the coverage lines up at the receiving 40 and releases on the catch
    s.landX = (std::max)(-6.0, 100 - land);
    s.landY = clamp(FIELD_W / 2 + rng.normal(0, 5), 8, FIELD_W - 8);
    s.hang = hang;
    s.apex = 24;
    poss = rt;
    pendingReturn = PendingReturn{s, nm(k) + " kicks " + SR(dist) + " yards", false};
    return SpecialResult{pendingReturn->kickDesc, mkFlight(fromX, (std::min)(land, 109.0), FIELD_W / 2, hang, 22)};
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
      toGo = 10;
      ballY = FIELD_W / 2;
      addLog(nm(k) + " kicks off. " + nm(kr) + " FUMBLES the return! Recovered by " + teamOf(kt).abbr + " at the " + ydLine() + ".",
             LogTag::Turnover);
      runClock(6, true);
      return SpecialResult{log.back().desc, mkFlight(fromX, land, FIELD_W / 2, hang, 22)};
    }
    if (start >= 100) {
      poss = rt;
      score[rt] += 6;
      addLog(nm(k) + " kicks off. " + nm(kr) + " returns it ALL THE WAY! TOUCHDOWN " + teamOf(rt).abbr + "!", LogTag::Td);
      stats.line(*kr, teamOf(rt).abbr).defTD++;
      phase = GamePhase::Pat;
      ballOn = 85;
      runClock(11, true);
      return SpecialResult{log.back().desc, mkFlight(fromX, land, FIELD_W / 2, hang, 22)};
    }
    const int rs = jsRoundI(start);
    desc = nm(k) + " kicks " + SR(dist) + " yards, returned by " + nm(kr) + " to the " + teamOf(rt).abbr + " " + S(rs <= 50 ? rs : 100 - rs) + ".";
    runClock(rng.range(5, 8), true);
  }
  poss = rt;
  ballOn = start;
  down = 1;
  toGo = 10;
  ballY = FIELD_W / 2;
  driveStartBallOn = ballOn;
  inRedZone = false;
  clockRunning = start != 35;  // returned kicks: clock keeps running
  addLog(desc);
  return SpecialResult{desc, mkFlight(fromX, (std::min)(land, 109.0), FIELD_W / 2, hang, 22)};
}

SpecialResult Game::punt(bool liveReturn) {
  const TeamKey kt = poss;
  const Player* p = punter(kt);
  TeamLine& T = team[kt];
  T.punts++;
  const double los = ballOn;
  PlayerLine& l = stats.line(*p, offense().abbr);
  runClock(rng.range(5, 9), true);
  if (rng.chance(0.006)) {
    addLog(nm(p) + " punt is BLOCKED! Recovered by " + defense().abbr + ".", LogTag::Turnover);
    changePossessionRaw(100 - (los - 7));
    if (q == 5) otDriveEnded();
    return SpecialResult{log.back().desc, std::nullopt};
  }
  double gross = 46.5 + (p->r.kickPower - 80) * 0.3 + windHelp(kt) * 1.0 + rng.normal(0, 6.5);
  // short-field punting: aim to pin
  const double room = 100 - los;
  double land = los + gross;
  std::string desc;
  double start;
  if (room < 50) {
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
      const double hang = 4.3 + rng.normal(0, 0.3);
      KickReturnSpec s;
      s.punt = true;
      s.kickX = 100 - (los - 14);
      s.coverX = 100 - los;
      s.landX = catchSpot;
      s.landY = clamp(FIELD_W / 2 + rng.normal(0, 6), 8, FIELD_W - 8);
      s.hang = hang;
      s.kickT = 1.1;  // snap and punt
      s.apex = 26;
      l.punts++;
      l.puntYds += jsRoundI(gross);
      poss = other(kt);
      pendingReturn = PendingReturn{s, nm(p) + " punts " + SR(gross) + " yards", true};
      return SpecialResult{pendingReturn->kickDesc, mkFlight(los - 14, (std::min)(land, 105.0), FIELD_W / 2, hang, 26)};
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
        return SpecialResult{log.back().desc, mkFlight(los - 14, land, FIELD_W / 2, 4.4, 26)};
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
        return SpecialResult{log.back().desc, mkFlight(los - 14, land, FIELD_W / 2, 4.4, 26)};
      }
      desc = nm(p) + " punts " + SR(gross) + " yards, returned " + SR(ret) + " yards.";
    }
  }
  l.punts++;
  l.puntYds += jsRoundI(gross);
  addLog(desc);
  if (q == 5) otDriveEnded();
  if (phase != GamePhase::Final) {
    changePossessionRaw(clamp(start, 1, 99));
    clockRunning = desc.find("returned") != std::string::npos;
  }
  const double hang = 4.3 + rng.normal(0, 0.3);
  return SpecialResult{desc, mkFlight(los - 14, (std::min)(land, 105.0), FIELD_W / 2, hang, 26)};
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
  return std::make_unique<PlaySim>(cfg);
}

PlayLog Game::finishKickReturn(PlaySim& sim) {
  const PlayResult& r = *sim.result;
  const PendingReturn pr = pendingReturn ? *pendingReturn : PendingReturn{};
  pendingReturn.reset();
  const TeamKey rt = poss, kt = other(rt);
  Player* kr = sim.returner ? sim.returner->p : nullptr;
  runClock(r.duration, true);
  for (Ent* e : sim.ents)
    e->p->fatigue = clamp(e->p->fatigue + (0.028 + r.duration * 0.0035) * (1.25 - e->p->r.stamina / 120), 0, 1);
  const std::string who = kr ? nm(kr) : std::string("the returner");
  down = 1;
  toGo = 10;
  ballY = FIELD_W / 2;
  inRedZone = false;
  if (r.td && *r.td == Side::Off) {
    score[rt] += 6;
    if (kr) stats.line(*kr, teamOf(rt).abbr).defTD++;
    phase = GamePhase::Pat;
    ballOn = 85;
    addLog(pr.kickDesc + ". " + who + " returns it ALL THE WAY! TOUCHDOWN " + teamOf(rt).abbr + "!", LogTag::Td);
    return log.back();
  }
  if (r.turnover) {
    if (r.td) {
      score[kt] += 6;
      poss = kt;
      phase = GamePhase::Pat;
      ballOn = 85;
      addLog(pr.kickDesc + ". " + who + " FUMBLES, returned for a TOUCHDOWN by " + teamOf(kt).abbr + "!", LogTag::Td);
      return log.back();
    }
    poss = kt;
    ballOn = clamp(100 - r.endX, 1, 99);
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
  if (tb)
    addLog(pr.kickDesc + ", fielded in the end zone by " + who + " and downed. Touchback.");
  else
    addLog(pr.kickDesc + ", returned " + SR((std::max)(0.0, r.endX - sim.catchX)) + " yards by " + who + " to the " + teamOf(rt).abbr + " " +
           S(rs <= 50 ? rs : 100 - rs) + ".");
  if (pr.punt && q == 5) otDriveEnded();
  return log.back();
}

SpecialResult Game::fieldGoal() {
  const TeamKey kt = poss;
  const Player* k = kicker(kt);
  const int dist = jsRoundI(100 - ballOn + 17);
  const double p = fgProb(dist, *k, kt);
  PlayerLine& l = stats.line(*k, offense().abbr);
  l.fga++;
  team[kt].fga++;
  runClock(rng.range(4, 6), true);
  const bool blocked = rng.chance(0.012);
  const bool good = !blocked && rng.chance(p);
  const double fy = FIELD_W / 2 + (good ? 0 : rng.chance(0.5) ? 5 : -5);
  const KickFlight flight = mkFlight(ballOn - 7, 110, fy, 2.6, 14, good);
  if (good) {
    l.fgm++;
    team[kt].fgm++;
    score[kt] += 3;
    addLog(nm(k) + " " + S(dist) + "-yard field goal is GOOD.", LogTag::Fg);
    kicking = kt;
    phase = GamePhase::Kickoff;
    clockRunning = false;
    if (q == 5) otDriveEnded();
    return SpecialResult{log.back().desc, flight};
  }
  const std::string how = blocked ? "BLOCKED" : rng.chance(0.5) ? "NO GOOD, wide right" : "NO GOOD, wide left";
  addLog(nm(k) + " " + S(dist) + "-yard field goal is " + how + ".", LogTag::Turnover);
  if (q == 5) otDriveEnded();
  if (phase != GamePhase::Final) changePossessionRaw(100 - (std::max)(ballOn - 7, 20.0));
  return SpecialResult{log.back().desc, flight};
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
  const TeamKey kt = poss;
  const Player* k = kicker(kt);
  PlayerLine& l = stats.line(*k, offense().abbr);
  l.xpa++;
  const double p = clamp(fgProb(33, *k, kt) - 0.03, 0.8, 0.99);
  const bool good = rng.chance(p);
  if (good) {
    l.xpm++;
    score[kt] += 1;
  }
  addLog(nm(k) + " extra point is " + (good ? "GOOD" : "NO GOOD") + ".", good ? LogTag::None : LogTag::Info);
  afterPAT(kt);
  return SpecialResult{log.back().desc, mkFlight(78, 110, FIELD_W / 2 + (good ? 0 : 4), 2.2, 12, good)};
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
  static const std::vector<std::string> kIds = {"trips_stick", "dbl_slants", "dbl_mesh", "gl_dive", "gl_pa", "pistol_power", "empty_quick"};
  std::vector<const OffPlay*> pool;
  for (const OffPlay& p : OFF_PLAYS())
    if (std::find(kIds.begin(), kIds.end(), p.id) != kIds.end()) pool.push_back(&p);
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
