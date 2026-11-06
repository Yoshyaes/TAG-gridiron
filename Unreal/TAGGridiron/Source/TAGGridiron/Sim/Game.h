// Port of src/sim/game.ts
// Game rules engine: clock, downs, scoring, penalties, special teams, CPU coordinators.
//
// Porting notes:
//  - TS `Record<TeamKey, T>` / `{home, away}` objects -> PerTeam<T> (fields `home`/`away`, plus
//    operator[](TeamKey)), so `game.score.home` and `game.score[k]` both work.
//  - Game holds non-owning Team* (league storage must outlive the Game).
//  - makePlay()/twoPointSim() return std::unique_ptr<PlaySim> (PlaySim is pinned in memory).
//  - finishPlay()/simStep() return copies of the PlayLog entry (the log vector may grow).
//  - Optional TS fields -> std::optional; `big?: 'td' | ...` -> LogTag (None = undefined).
//
// Driving a game from an engine (what Game::simStep() does, split so each play can be rendered):
//   phase == Kickoff : SpecialResult r = game.kickoff();            // animate r.flight, show r.desc
//   phase == Pat     : game.goForTwo() ? (sim = game.twoPointSim(op, dc, side), ..., game.finishTwoPoint(*sim))
//                                      : game.extraPoint();
//   phase == Scrimmage:
//     if (!game.preSnap()) -> period ended (log has "End of ..." / halftime / OT / FINAL)
//     down == 4: game.fourthDownCall() -> punt() / fieldGoal() / go for it
//     optional pre-snap flag: if (auto pen = game.rollPresnap()) game.applyPresnap(*pen);
//     const OffPlay* op = game.cpuOffense() (or user pick); const DefCall* dc = game.cpuDefense(*op);
//     auto sim = game.makePlay(*op, *dc, userSide);      // userSide: Side::Off / Side::Def / nullopt
//     per frame: write sim->input, sim->step();            // fixed 1/60 s ticks
//                read sim->ents[i] (pos, vel, face, anim, animT, role, side, state, hasBall,
//                userControlled) and sim->ball (mode, pos z-up, holder, target)
//     when !sim->isLive(): PlayLog line = game.finishPlay(*sim);   // or sim->runToEnd() to skip
// Coordinates are the offense frame of the play (see PlaySim.h); worldDir() maps it to the stadium.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Playbook.h"
#include "PlaySim.h"
#include "Rng.h"
#include "Stats.h"
#include "Types.h"

namespace tag {

enum class TeamKey { Home, Away };
inline TeamKey other(TeamKey k) { return k == TeamKey::Home ? TeamKey::Away : TeamKey::Home; }
inline const char* teamKeyName(TeamKey k) { return k == TeamKey::Home ? "home" : "away"; }

template <class T>
struct PerTeam {
  T home{};
  T away{};
  T& operator[](TeamKey k) { return k == TeamKey::Home ? home : away; }
  const T& operator[](TeamKey k) const { return k == TeamKey::Home ? home : away; }
};

enum class GamePhase { Kickoff, Scrimmage, Pat, Final };
const char* gamePhaseName(GamePhase p);

enum class LogTag { None, Td, Turnover, Fg, Safety, Penalty, Info };
const char* logTagName(LogTag t);  // "" for None

struct PlayLog {
  int q = 1;
  double clock = 0;
  TeamKey poss = TeamKey::Away;
  int down = 1;
  double toGo = 10;
  double ballOn = 25;
  std::string desc;
  PerTeam<int> score;
  LogTag big = LogTag::None;
};

/** Ball flight for the renderer, offense frame of the kicking team. */
struct KickFlight {
  double fromX = 0;
  double toX = 0;
  double y = FIELD_W / 2;
  double hang = 2;
  double height = 10;
  std::optional<bool> good;
};

struct SpecialResult {
  std::string desc;
  std::optional<KickFlight> flight;
};

struct GameOptions {
  std::optional<uint32_t> seed;
  std::optional<double> quarterLen;
  std::optional<Weather> weather;
  std::optional<TeamKey> userTeam;
};

enum class PenaltySpot { Los, End, Spot };

struct Penalty {
  std::string name;
  Side on = Side::Off;
  double yards = 0;
  bool autoFirst = false;
  PenaltySpot spot = PenaltySpot::Los;
  std::optional<double> spotX;
  bool replayDown = false;
};

enum class Tempo { Normal, Hurry, Milk };
enum class FourthDownCall { Go, Punt, Fg };

std::string fmtClock(double s);

/** Expected points (offense) for a down/distance/field position. Simple fit to modern NFL EP curves. */
double expectedPoints(double ballOn, int down, double toGo);

Weather randomWeather(Rng& rng);

class Game {
 public:
  Game(Team& home, Team& away, const GameOptions& opts = GameOptions());

  Team* home;
  Team* away;
  PerTeam<int> score;
  int q = 1;
  double quarterLen = 900;
  double clock = 900;
  TeamKey poss = TeamKey::Away;
  double ballOn = 35;
  double ballY = FIELD_W / 2;
  int down = 1;
  double toGo = 10;
  GamePhase phase = GamePhase::Kickoff;
  TeamKey kicking = TeamKey::Home;
  TeamKey openingReceiver = TeamKey::Away;
  PerTeam<int> timeouts{3, 3};
  bool clockRunning = false;
  int homeDir = 1;
  StatBook stats;
  PerTeam<TeamLine> team;
  std::vector<PlayLog> log;
  Weather weather;
  Rng rng;
  std::optional<TeamKey> userTeam;
  bool twoMin[8] = {false, false, false, false, false, false, false, false};
  bool lastOOB = false;
  int otPossDone = 0;
  double driveStartBallOn = 25;
  bool inRedZone = false;
  std::string lastPlayName;
  std::vector<std::string> recent;
  /** pending penalty to show on next snap */
  std::optional<Penalty> lastPenalty;
  std::vector<Player*> injuries;
  /** extra-time flag for untimed down */
  bool untimed = false;
  bool safetyKick = false;

  Team& teamOf(TeamKey k) const { return k == TeamKey::Home ? *home : *away; }
  Team& offense() const { return teamOf(poss); }
  Team& defense() const { return teamOf(other(poss)); }
  /** +1 if possessing team attacks world +x */
  int worldDir() const { return worldDir(poss); }
  int worldDir(TeamKey k) const { return k == TeamKey::Home ? homeDir : -homeDir; }
  std::string weatherText() const;
  void addLog(const std::string& desc, LogTag big = LogTag::None);
  std::string ydLine() const { return ydLine(ballOn, poss); }
  std::string ydLine(double ballOnArg, TeamKey k) const;
  std::string downText() const;
  bool isFinal() const { return phase == GamePhase::Final; }
  double halfSecondsLeft() const;
  int margin() const { return margin(poss); }
  int margin(TeamKey k) const { return score[k] - score[other(k)]; }
  Tempo tempo() const;
  /** Called right before each snap (scrimmage). Applies runoff. Returns false if the period ended. */
  bool preSnap();

  // ---- CPU coordinators
  bool shouldKneel() const;
  FourthDownCall fourthDownCall();
  const OffPlay* cpuOffense();
  const DefCall* cpuDefense(const OffPlay& offPlay);

  // ---- penalties
  std::optional<Penalty> rollPresnap();
  /** Pre-snap penalty: apply (no play). */
  void applyPresnap(const Penalty& pen);

  // ---- scrimmage
  std::unique_ptr<PlaySim> makePlay(const OffPlay& offPlay, const DefCall& defCall, std::optional<Side> userSide = std::nullopt);
  /** Apply finished play. Returns (a copy of) the log line. */
  PlayLog finishPlay(PlaySim& sim);
  std::string describe(const PlayResult& res, const OffPlay& play, const PlaySim& sim) const;

  // ---- special teams
  Player* kicker(TeamKey k) const;
  Player* punter(TeamKey k) const;
  /** Probability a kick of `dist` yards is good. Fit to 2020s NFL by distance. */
  double fgProb(double dist, const Player& k, TeamKey kt) const;
  SpecialResult kickoff(bool onside = false);
  SpecialResult punt();
  SpecialResult fieldGoal();
  /** Decide 2-point try per a standard chart (CPU). */
  bool goForTwo();
  /** PAT kick (33 yards). */
  SpecialResult extraPoint();
  /** Two-point try: a real scrimmage play from the 2. */
  std::unique_ptr<PlaySim> twoPointSim(const OffPlay& offPlay, const DefCall& defCall, std::optional<Side> userSide = std::nullopt);
  void finishTwoPoint(PlaySim& sim);
  const OffPlay* cpuTwoPointPlay();

  // ---- headless driver
  /** Simulate one "action" (kickoff, PAT, or a scrimmage down) with CPU on both sides. */
  std::optional<PlayLog> simStep();
  void simToEnd(int maxSteps = 2000);

 private:
  void runClock(double sec, bool duringPlay = false);
  /** Clock stops after OOB only in final 2:00 of 1st half and final 5:00 of 2nd half. */
  bool oobStops() const;
  void endQuarter();
  void finish();
  void changePossession(double newBallOn, const std::string& reason);
  void otDriveEnded();
  void scoreTD(TeamKey k);
  double windHelp(TeamKey k) const;
  std::optional<Penalty> rollPostsnap(const PlayResult& res, const OffPlay& play);
  /** Apply a penalty's yardage from LOS with half-the-distance rule. Returns new ballOn. */
  double enforce(const Penalty& pen, double los, double endX) const;
  void changePossessionRaw(double newBallOn);
  double endY(const PlaySim& sim) const;
  double resultEP(const PlayResult& res, double los) const;
  void afterPAT(TeamKey kt);
};

}  // namespace tag
