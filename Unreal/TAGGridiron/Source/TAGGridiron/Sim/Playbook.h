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
  /** The play-call screen's group ("Shotgun", "Pistol", "Singleback", "I-Form", "Strong I", "Weak I", "Split Backs",
   *  "Goal Line"); empty = derived from the id. */
  std::string family;
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
  /** Generated from a concept (playbook.cpp): the schemes that run it, as two-letter codes ("WC SP AR ..."); empty =
   *  every scheme. The hand-written plays are "stock" and in every book that has their formation. */
  std::string styles;
  bool stock = false;

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
/** Base = 4-3 (four down linemen, three backers); Base34 = 3-4 (three down, two outside backers on the edge). */
enum class Front { Base, Nickel, Dime, Goalline, Base34 };
/** C2T = Tampa 2 (the Mike runs the middle hole); C6 = quarter-quarter-half (quarters to the left, cloud half right). */
enum class Coverage { C0, C1, C2, C3, C4, C2M, C3Fire, C2T, C6 };
/** How the front lines up inside its set: Over / Under shift the line a gap toward / away from the strength, Wide 9
 *  splits the ends, Bear covers the guards and center. */
enum class FrontAlign { Normal, Over, Under, Wide9, Bear };

struct DefCall {
  std::string id;
  std::string name;
  Front front = Front::Base;
  Coverage coverage = Coverage::C3;
  /** defenders (by role) sent on the rush in addition to the 4 DL */
  std::vector<std::string> blitz;
  std::vector<std::string> tags;
  /** The set it is called from on the play-call screen ("4-3 Over", "Nickel Normal"); empty = the front's name. */
  std::string set;
  FrontAlign align = FrontAlign::Normal;

  bool hasTag(const char* t) const {
    for (const auto& s : tags)
      if (s == t) return true;
    return false;
  }
  /** Men rushing at the snap: the down linemen and the blitz. */
  int rushers() const;
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
/** The formation's play-call group (Formation::family, or one derived from its id). */
std::string formationFamily(const Formation& f);
/** The play-call group of a front: "4-3", "3-4", "Nickel", "Dime", "Goal Line". */
const char* frontFamily(Front f);
std::string defSet(const DefCall& d);

// ---------------- Playbooks ----------------
/** A team's offensive playbook: its formations in screen order and every play in them (pointers into OFF_PLAYS),
 *  plus the scheme it is built on. */
struct OffBook {
  std::string id;      // team abbreviation, or the scheme id for a generic book
  std::string name;    // "Austin Outlaws"
  std::string scheme;  // "West Coast", "Spread", "Air Raid", ...
  std::vector<std::string> formations;
  std::vector<const OffPlay*> plays;
  std::vector<const OffPlay*> playsIn(const std::string& formation) const;
  bool has(const OffPlay* p) const;
};
struct DefBook {
  std::string id;
  std::string name;
  std::string scheme;  // "4-3 Cover 3", "Tampa 2", "3-4 Multiple", ...
  std::vector<std::string> sets;  // in screen order
  std::vector<const DefCall*> calls;
  std::vector<const DefCall*> callsIn(const std::string& set) const;
  bool has(const DefCall* d) const;
  /** The book's base front: Base (4-3) or Base34. */
  Front base = Front::Base;
};
struct Team;
const std::vector<std::string>& OFF_SCHEMES();
const std::vector<std::string>& DEF_SCHEMES();
/** The team's books (built once per team from its Team::offScheme / defScheme and id; stable addresses). A team with
 *  no scheme gets the first scheme. */
const OffBook& offBookFor(const Team& t);
const DefBook& defBookFor(const Team& t);
/** A scheme's whole book (every formation and play the scheme runs), for tools and tests. */
const OffBook& offSchemeBook(const std::string& scheme);
const DefBook& defSchemeBook(const std::string& scheme);
/** How much a defensive scheme leans on a call (a multiplier on the CPU caller's situational weight). */
double defSchemeLean(const std::string& scheme, const DefCall& d);

}  // namespace tag
