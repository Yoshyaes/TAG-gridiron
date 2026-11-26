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
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CallAdapt.h"
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
  // a played place kick: the launch (yd/s) the ball is flown with, and where it is across the field at toX
  double vx = 0, vz = 0;
  std::optional<double> endY;
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
  /** Superstar / X-Factor abilities in this game (unset: on; Tune::abilities = 0 turns them off everywhere). */
  std::optional<bool> abilities;
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
  /** The two-minute mark passed during a play: the warning is logged after that play's own line (twoMinPending), and
   *  the clock stays stopped until the next snap (twoMinStop: preSnap runs nothing off). */
  bool twoMinPending = false;
  bool twoMinStop = false;
  bool lastOOB = false;
  int otPossDone = 0;
  double driveStartBallOn = 25;
  bool inRedZone = false;
  std::string lastPlayName;
  std::vector<std::string> recent;
  /** pending penalty to show on next snap */
  std::optional<Penalty> lastPenalty;
  /** (tests) the flag the next finishPlay throws instead of the one it rolls (the roll is still drawn), then cleared */
  std::optional<Penalty> forcedFlag;
  std::vector<Player*> injuries;
  /** extra-time flag for untimed down */
  bool untimed = false;
  bool safetyKick = false;
  /** Superstar / X-Factor abilities: on for this game, and every X-Factor man's zone (Abilities.h). The zone events
   *  (in / knocked out) pile up in abil.events for the presentation to take (the last 32 are kept). */
  bool abilitiesOn = true;
  AbilityState abil;
  bool abilOn() const;
  /** What the play's sim needs to know about abilities (PlayConfig::abil). */
  AbilityCtx abilCtx() const;
  /** A kicker / punter's ratings for this kick, his abilities in (kickRatings). */
  Ratings kickR(const Player& k, TeamKey kt, bool punt) const;

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
  /** A human game: the offense's real seconds between the plays replace the runoff preSnap() drew. sec > 0 takes more
   *  off a running clock (the two-minute warning still stops it), sec < 0 gives back what the snap came before. */
  void adjustBetweenPlays(double sec);
  /** The play clock ran out on the offense (a human that did not snap): 5 yards, the down again. */
  void delayOfGame();

  // ---- CPU coordinators
  bool shouldKneel() const;
  FourthDownCall fourthDownCall();
  const OffPlay* cpuOffense();
  /** offPlay: only its formation is read (what a defense sees before it calls). */
  const DefCall* cpuDefense(const OffPlay& offPlay);
  /** The CPU offense's chance of a pass call here (cpuOffense draws against it). */
  double cpuPassProb() const;
  /** The plays the CPU offense calls from here (pass or run) and their weights, the tendencies' lean in. */
  void cpuOffensePool(bool pass, std::vector<const OffPlay*>& pool, std::vector<double>& w) const;
  /** The calls the CPU defense picks from against a formation, and their weights (r: the front's coin on 3rd & long). */
  void cpuDefensePool(const std::string& formation, Rng& r, std::vector<const DefCall*>& pool, std::vector<double>& w) const;
  /** Each team's calls this game and what they got (CallAdapt.h): noted at every scrimmage play, whoever called it. */
  PerTeam<Tendency> tend;
  /** (tests) a scripted offense: a play it returns is called instead of the CPU's (nullptr: the CPU calls). */
  std::function<const OffPlay*(Game&)> scriptOffense;

  // ---- penalties
  std::optional<Penalty> rollPresnap();
  /** Pre-snap penalty: apply (no play). */
  void applyPresnap(const Penalty& pen);

  // ---- scrimmage
  /** flip: the formation's strength (an audible keeps it, flip play changes it); unset = to the field side. */
  std::unique_ptr<PlaySim> makePlay(const OffPlay& offPlay, const DefCall& defCall, std::optional<Side> userSide = std::nullopt,
                                    std::optional<bool> flip = std::nullopt);
  /** Apply finished play. Returns (a copy of) the log line. */
  PlayLog finishPlay(PlaySim& sim);
  /** Optional observer of every finished scrimmage play (headless realism metrics). */
  std::function<void(const PlaySim&, const PlayResult&)> onPlayResult;
  /** Optional observer handed to every scrimmage sim (called after each step). */
  std::function<void(const PlaySim&)> onSimStep;
  bool pullDecided_ = false;  // garbage-time substitutions, decided per possession
  TeamKey pullPoss_ = TeamKey::Home;
  std::string describe(const PlayResult& res, const OffPlay& play, const PlaySim& sim) const;

  // ---- special teams
  Player* kicker(TeamKey k) const;
  Player* punter(TeamKey k) const;
  /** Probability a kick of `dist` yards is good. Fit to 2020s NFL by distance. */
  double fgProb(double dist, const Player& k, TeamKey kt) const;
  struct PlaceKick {
    KickFlight flight;
    bool good = false, blocked = false, isShort = false;
    double miss = 0;  // across the posts' plane, yd left (+) of centre
    double v = 0, elevDeg = 0, drag = 0, plane = 0;  // the strike: launch (yd/s, deg), drag, spot to the posts (yd)
  };
  /** A place kick played out: leg and line drawn, the ball flown, good if it crosses over the bar between the uprights. */
  PlaceKick placeKick(double fromX, double p, const Player& k, TeamKey kt, double blockP);
  /** liveReturn: a returnable kick is not resolved here; it is left in pendingReturn for a live return play
   *  (kickReturnSim / finishKickReturn). Touchbacks, fair catches, downed and onside kicks resolve as before. */
  SpecialResult kickoff(bool onside = false, bool liveReturn = false);
  SpecialResult punt(bool liveReturn = false);
  struct PendingReturn {
    KickReturnSpec spec;
    std::string kickDesc;
    bool punt = false;
  };
  std::optional<PendingReturn> pendingReturn;
  /** A kick the user swung on the meter: power 0..1.15 (past 1 is the red: no farther, only wilder), aim in degrees
   *  off straight down the field (+ = the kicking team's left), accuracy -1..1 off the meter's centre. Read and
   *  cleared by the next kickoff / punt / field goal / extra point, which then play it instead of rolling one. */
  struct UserKick {
    double power = 1;
    double aimDeg = 0;
    double acc = 0;
  };
  std::optional<UserKick> userKick;
  /** The live return play: recv / kick are the units on the field (recv[0] the returner, kick[0] the kicker). */
  std::unique_ptr<PlaySim> kickReturnSim(const std::vector<Player*>& recv, const std::vector<Player*>& kick,
                                         std::optional<Side> userSide = std::nullopt);
  PlayLog finishKickReturn(PlaySim& sim);
  SpecialResult fieldGoal();
  /** A field goal (pat = false: from the line of scrimmage) or extra point, played (Sim/PlaceKick.cpp): the kick unit
   *  and the block unit on the field (empty: the game's own, fgUnit), the strike drawn now (placeKick, the user's swing
   *  if Game::userKick is set), the snap setT seconds in, the block unit in rush look `look` (-1: its own pick). Step it,
   *  then finishPlaceKick. fieldGoal() / extraPoint() are this run to the end. */
  std::unique_ptr<PlaySim> placeKickSim(bool pat, std::vector<Player*> kick = {}, std::vector<Player*> block = {},
                                        std::optional<Side> userSide = std::nullopt, double setT = 0.9, int look = -1);
  /** The two units in the order the place kick gives out its jobs (PlaySim::placeAlign: K, H, LS, LG, RG, LT, RT, TE,
   *  TE2, LW, RW; AL, AR, BL, BR, CL, CR, EL, ER, J, SP, FL) - kt is the kicking team. */
  void orderPlaceUnits(TeamKey kt, std::vector<Player*>& kick, std::vector<Player*>& block) const;
  /** The played place kick into the game: the score, the stats, the ball (a block: who has it where). */
  SpecialResult finishPlaceKick(PlaySim& sim);
  /** The eleven a team sends on for a field goal / extra point (kicking) or to block one. */
  std::vector<Player*> fgUnit(TeamKey k, bool kickTeam) const;
  /** The share of place kicks the rush blocks at this length (nflverse 2023; the make rate fgProb gives is with them in). */
  static double placeBlockRate(double dist, bool pat);
  /** Decide 2-point try per a standard chart (CPU). */
  bool goForTwo();
  /** A flag on the return team during a return (block in the back, holding): 10 yd back from where it ended. */
  bool returnFoul(TeamKey rt, double& spot, std::string& desc);
  /** PAT kick (33 yards). */
  SpecialResult extraPoint();
  /** Two-point try: a real scrimmage play from the 2. */
  std::unique_ptr<PlaySim> twoPointSim(const OffPlay& offPlay, const DefCall& defCall, std::optional<Side> userSide = std::nullopt,
                                       std::optional<bool> flip = std::nullopt);
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
  std::optional<KickFlight> placeFlight_;  // the strike placeKickSim drew (finishPlaceKick hands it on)
  SpecialResult legacyFieldGoal();   // (Tune::fgPlay = 0: the old roll)
  SpecialResult legacyExtraPoint();
};

}  // namespace tag
