// Port of src/sim/playbook.ts
// Playbook: formations, route tree, run schemes, defensive calls.
// Offense frame: offense always attacks +x. y in [0, 53.33]; +y is the offense's LEFT.
//
// Porting notes:
//  - Roles (offensive "QB","RB","X","Z","SLOT","SLOT2","TE","TE2","LT","LG","C","RG","RT" and
//    defensive "LE","LDT","RDT","RE","WLB","MLB","SLB","NB","DB","LCB","RCB","FS","SS") stay
//    std::string, as in TS: they are display labels, map keys and UserInput::throwTo values.
//  - Closed unions that drive logic (play type, run style, front, coverage) are enum classes.
//  - Tables are function-local statics (no static-init-order issues inside a UE module).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Types.h"

namespace tag {

constexpr double FIELD_W = 160.0 / 3.0;          // 53.33 yards
constexpr double HASH_L = FIELD_W / 2 + 3.083;   // NFL hashes (70'9" from sideline)
constexpr double HASH_R = FIELD_W / 2 - 3.083;

enum class AlignKind { OL, Inline, Wide, Slot, Back };

/** TS `Align` discriminated union, flattened. Unused members keep their defaults. */
struct Align {
  AlignKind kind = AlignKind::OL;
  double dy = 0;      // ol, back
  char side = 'L';    // inline, wide, slot: 'L' | 'R'
  bool off = false;   // inline, wide: lined up off the ball
  int n = 0;          // slot: 2 = second slot (0 = unset)
  double x = 0;       // back
};

struct FormationSlot {
  std::string role;
  std::vector<Pos> pos;  // depth chart preference
  Align align;
};

struct Formation {
  std::string id;
  std::string name;
  std::string personnel;
  double qbDepth = 0;  // yards behind LOS at snap (shotgun 5, pistol 4, under center 1)
  std::vector<FormationSlot> slots;
};

// ---------------- Routes ----------------
// Points are [forward, outward] yards relative to the receiver's snap position.
// "outward" points toward the receiver's sideline. `settle` = stop & work back to QB.
struct RouteDef {
  std::vector<std::pair<double, double>> pts;
  bool settle = false;
  bool block = false;
};

enum class PlayType { Run, Pass, PlayAction, Screen, Kneel, Spike };
enum class RunStyle { Zone, Gap, Stretch, Draw, Sneak };

struct RunScheme {
  /** gap aim relative to center in yards (+ = left). */
  double aimDy = 0;
  /** zone (outside zone reads) vs gap (power) */
  RunStyle style = RunStyle::Zone;
  double handoffDelay = 0;
  std::string carrier;
};

struct OffPlay {
  std::string id;
  std::string name;
  std::string formation;
  PlayType type = PlayType::Pass;
  /** role -> route name (TS Partial<Record<OffRole, string>>) */
  std::map<std::string, std::string> routes;
  /** QB read order of eligible roles (empty = TS undefined) */
  std::vector<std::string> reads;
  std::optional<RunScheme> run;
  std::optional<double> dropDepth;  // extra drop yards for QB
  std::vector<std::string> tags;     // 'short', 'deep', 'quick', 'goalline', 'shortyard'

  bool hasTag(const char* t) const {
    for (const auto& s : tags)
      if (s == t) return true;
    return false;
  }
  /** Route name for a role, or nullptr if the role has none. */
  const std::string* routeFor(const std::string& role) const {
    auto it = routes.find(role);
    return it == routes.end() ? nullptr : &it->second;
  }
};

// ---------------- Defense ----------------
enum class Front { Base, Nickel, Dime, Goalline };
enum class Coverage { C0, C1, C2, C3, C4, C2M, C3Fire };

struct DefCall {
  std::string id;
  std::string name;
  Front front = Front::Base;
  Coverage coverage = Coverage::C3;
  /** defenders (by role) sent on the rush in addition to the 4 DL */
  std::vector<std::string> blitz;
  std::vector<std::string> tags;

  bool hasTag(const char* t) const {
    for (const auto& s : tags)
      if (s == t) return true;
    return false;
  }
};

struct FrontSlot {
  std::string role;
  std::vector<Pos> pos;
};

const std::vector<Formation>& FORMATIONS();
const std::map<std::string, RouteDef>& ROUTES();
const std::vector<OffPlay>& OFF_PLAYS();
const std::vector<DefCall>& DEF_PLAYS();
const std::vector<FrontSlot>& FRONT_PERSONNEL(Front f);

/** Lookups return nullptr if the id is unknown (TS used a non-null assertion). */
const OffPlay* playById(const std::string& id);
const Formation* formationById(const std::string& id);
const DefCall* defById(const std::string& id);
const RouteDef* routeByName(const std::string& name);

const char* playTypeName(PlayType t);
const char* frontName(Front f);
const char* coverageName(Coverage c);

}  // namespace tag
