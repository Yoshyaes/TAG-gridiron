// Port of src/sim/types.ts
// Core data model: positions, ratings, players, teams.
//
// Porting notes:
//  - All ratings / measurables are `double` (TS `number`) so every formula keeps float
//    semantics (e.g. `agility / 330` must never become integer division).
//  - Players are owned by `Team::roster` through std::unique_ptr, so `Player*` handed to the
//    play simulation / stat book stays valid for the lifetime of the Team.
//  - `injuredWeeks` stays a double because the TS injury roll can produce 0.5.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tag {

// (Abilities.h: the Superstar / X-Factor catalog and grades; declared here so a Player can carry them)
enum class AbilityId : uint8_t;
enum class StarGrade : uint8_t;

enum class Pos { QB, RB, FB, WR, TE, OT, OG, C, DE, DT, LB, CB, S, K, P };

inline const char* posName(Pos p) {
  switch (p) {
    case Pos::QB: return "QB";
    case Pos::RB: return "RB";
    case Pos::FB: return "FB";
    case Pos::WR: return "WR";
    case Pos::TE: return "TE";
    case Pos::OT: return "OT";
    case Pos::OG: return "OG";
    case Pos::C: return "C";
    case Pos::DE: return "DE";
    case Pos::DT: return "DT";
    case Pos::LB: return "LB";
    case Pos::CB: return "CB";
    case Pos::S: return "S";
    case Pos::K: return "K";
    case Pos::P: return "P";
  }
  return "?";
}

/** All ratings are 25..99. Physical ones map to real-world measurables (see physics in PlaySim). */
struct Ratings {
  double speed = 0;        // top speed: 99 ~= 4.28 forty, 50 ~= 5.25 forty
  double accel = 0;        // how fast top speed is reached
  double agility = 0;      // lateral acceleration / change of direction
  double strength = 0;
  double stamina = 0;
  double awareness = 0;
  // ball carrier
  double carrying = 0;     // ball security
  double breakTackle = 0;
  double elusiveness = 0;
  double vision = 0;
  // receiving
  double catching = 0;
  double catchInTraffic = 0;
  double routeRunning = 0;
  double release = 0;
  // passing
  double throwPower = 0;
  double accShort = 0;
  double accMid = 0;
  double accDeep = 0;
  double throwOnRun = 0;
  double poise = 0;        // accuracy under pressure
  double decision = 0;     // read progression quality
  // blocking
  double passBlock = 0;
  double runBlock = 0;
  // defense
  double passRush = 0;
  double blockShed = 0;
  double tackling = 0;
  double pursuit = 0;
  double playRec = 0;
  double manCov = 0;
  double zoneCov = 0;
  double press = 0;
  double ballSkills = 0;
  // kicking
  double kickPower = 0;
  double kickAcc = 0;
};

struct Player {
  int id = 0;
  std::string first;
  std::string last;
  Pos pos = Pos::QB;
  int number = 0;
  int age = 0;
  double heightIn = 0;
  double weightLb = 0;
  Ratings r;
  int ovr = 0;
  /** 0 = fresh, 1 = exhausted. Persisted across plays within a game. */
  double fatigue = 0;
  double injuredWeeks = 0;
  /** Was on the field for the last snap (substitution hysteresis: the man in the game stays in). */
  bool onFieldLast = false;
  /** His grade against the league at his position (Abilities.h: Star / Superstar / X-Factor), and his loadout: the
   *  Superstar abilities he has equipped and his X-Factor (AbilityId::None = none). */
  StarGrade grade{};
  std::vector<AbilityId> abilities;
  AbilityId xFactor{};
  /** Create-a-Player: made by the user (saved in GameUserSettings.ini, laid onto his team at startup), his archetype
   *  (Roster.h capArchetypes, -1 none) and his look: the face (an index into the character pack's heads) and the
   *  helmet (into the helmets of his position's facemask style); -1 = picked from his id like everybody's. */
  bool created = false;
  int archetype = -1;
  int face = -1;
  int helmet = -1;
};

struct TeamColors {
  std::string primary;
  std::string secondary;
  std::string accent;
};

struct Team {
  int id = 0;
  std::string city;
  std::string name;
  std::string abbr;
  TeamColors colors;
  std::string conference;  // "North" | "South"
  std::string division;
  /** Owning storage; Player addresses are stable for the Team's lifetime. */
  std::vector<std::unique_ptr<Player>> roster;
  /** Garbage time: well ahead late, the starters at QB/RB/WR/TE sit (set by Game before each play). */
  bool pullStarters = false;
  /** Offensive identity 0..1 (0 = run heavy, 1 = air raid). Drives CPU play calling. */
  double passLean = 0.5;
  /** Aggressiveness on 4th downs 0..1. */
  double aggression = 0.5;
  /** Defensive blitz tendency 0..1 */
  double blitzLean = 0.28;
  /** The playbooks: an offensive scheme ("West Coast", "Spread", ...) and a defensive one ("4-3 Cover 3", "3-4
   *  Multiple", ...), from Playbook.h OFF_SCHEMES / DEF_SCHEMES; the team's own book is built from them (offBookFor). */
  std::string offScheme;
  std::string defScheme;
  /** The depth chart the user set (Roster.h): per DSlot, the men in order by Player::id; an empty slot (or none at
   *  all) is the coach's - the sim's old pick, best by OVR. */
  std::vector<std::vector<int>> chart;

  bool hasPlayer(const Player* p) const {
    for (const auto& q : roster)
      if (q.get() == p) return true;
    return false;
  }
};

/** Healthy players at a position, best first: the depth chart's order where the user has set it (Roster.h; a man
 *  listed there from another position counts), else best overall first. */
std::vector<Player*> depth(const Team& team, Pos pos);

}  // namespace tag
