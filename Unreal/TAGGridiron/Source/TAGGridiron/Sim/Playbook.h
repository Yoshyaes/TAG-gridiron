// Port of src/sim/playbook.ts
// Playbook: formations, route tree, run schemes, defensive calls.
// Offense frame: offense always attacks +x. y in [0, 53.33]; +y is the offense's LEFT.
//
// Porting notes:
//  - Roles (offensive "QB","RB","X","Z","SLOT","SLOT2","TE","TE2","TE3","RB2","LT","LG","C","RG","RT" and
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
  int n = 0;          // slot: 2 = second (inside) slot, 3 = outside slot (quads, a nasty split); 0 = the slot
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
   *  "Goal Line", "Wildcat", "Full House", "Wishbone"); empty = derived from the id. */
  std::string family;
  /** UI: a line on the set ("Three receivers to the strength, the tight end attached"). */
  std::string desc;
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
/** The play-call screen's kind of play, finer than PlayType (which drives the sim): an RPO is a Run in the sim (the
 *  quarterback may pull it and throw), a trick play a Run or a PlayAction. Names: playCatName(). */
enum class PlayCat { Run, Pass, PlayAction, Screen, RPO, Trick, Kneel, Spike };

/** A run's special block (pulls, lead and kick-out blocks). `who` is a lineman relative to the run's direction - "PG" /
 *  "BG" the play-side / back-side guard, "PT" / "BT" the tackles, "C" - or an eligible role ("FB", "TE", "TE2", "TE3",
 *  "RB", "RB2"); the PlaySim resolves it at the snap, after any flip. Jobs:
 *   "kick"  pull (a lineman) or attack (a back) to the end man on the line at the point of attack and kick him out
 *   "wrap"  pull, turn up inside the kick-out through the hole and block the first linebacker
 *   "lead"  a back leads through the hole for the linebacker (iso, lead, QB power); a puller leads around the edge
 *   "trap"  pull and trap the first down lineman past the center on the play side (he is left unblocked on purpose)
 *   "arc"   cross the formation and block the back-side end (split zone)  */
struct BlockJob {
  std::string who;
  std::string job;
};

struct RunScheme {
  /** gap aim relative to center in yards (+ = left). */
  double aimDy = 0;
  /** zone (outside zone reads) vs gap (power) */
  RunStyle style = RunStyle::Zone;
  double handoffDelay = 0;
  std::string carrier;
  /** Pulls and lead blocks (see BlockJob). Empty: everyone blocks the man the line's rules give him. */
  std::vector<BlockJob> jobs;
  /** Gap blocking: the play side blocks down (the man in his inside gap), the back side hinges; a zone run reaches. */
  bool down = false;
  /** Option reads: "end" - the back-side end is left unblocked and read at the mesh (zone read: he squeezes the back,
   *  the quarterback keeps it around him); "rpo" - the quarterback reads the conflict defender over the `rpo` receiver
   *  and throws the quick route instead of handing off when the numbers favour it. "" = a called handoff. */
  std::string read;
  std::string rpo;
  /** The carrier comes across the formation in jet motion and takes it on the move (OffPlay::motion is his). */
  bool jet = false;
};

/** Motion before the snap, part of the play: `role` goes across (dy yards, + = toward the offense's left before any
 *  flip) and the ball is snapped as he gets to his spot. jet: at full speed, on the snap timed to the handoff (a jet
 *  sweep, or the fake of one), else he jogs to a new alignment and sets. */
struct PlayMotion {
  std::string role;
  double dy = 0;
  bool jet = false;
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

  // ---- what the play-call screen shows (and the CPU caller reads): set for every play in OFF_PLAYS()
  /** Run / Pass / PA / Screen / RPO / Trick / Kneel / Spike (playCatName). */
  PlayCat cat = PlayCat::Pass;
  /** The concept or scheme it is built on, the same across formations ("Mesh", "Counter GT", "Zone Read"). */
  std::string conceptName;  // ("concept" is a C++20 keyword: the UE module builds as C++20)
  /** One line on the play: what it attacks and who the quarterback reads ("Two crossers under the linebackers; read
   *  the mesh, then the corner"). */
  std::string desc;
  /** The formation's personnel ("11", "12", "21", "10", "13", "22", "23", "31"). */
  std::string personnel;
  /** Motion that is part of the play (jet sweeps, jet fakes); none on most plays. */
  std::optional<PlayMotion> motion;
  /** Pass protection called with it: slide (+1 toward the offense's left / -1 right, 0 none); max protect is in the
   *  routes (the backs and tight ends on "block"). */
  int slide = 0;
  /** A trick play's second exchange: the man the carrier hands it on to, running the other way (a reverse). */
  std::string reverse;

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
/** Base = 4-3 (four down linemen, three backers); Base34 = 3-4 (three down, two outside backers on the edge).
 *  The sub packages: Nickel = 4-2-5, Nickel33 = 3-3-5 (three down, three backers), Nickel24 = 2-4-5 (two down, two
 *  edge backers standing up, two inside), Dime = 4-1-6, Dime32 = 3-2-6. frontFamily() groups them for the screen. */
enum class Front { Base, Nickel, Dime, Goalline, Base34, Nickel33, Nickel24, Dime32 };
/** C2T = Tampa 2 (the Mike runs the middle hole); C6 = quarter-quarter-half (quarters to the left, cloud half right).
 *  A call's CoverTech refines the shell (Cover 3 Buzz / Cloud, Cover 1 Robber, Cover 2 Invert ...). */
enum class Coverage { C0, C1, C2, C3, C4, C2M, C3Fire, C2T, C6 };
/** The technique inside a coverage shell (DefCall::tech): None = the shell as drawn. Robber (Cover 1): the strong safety
 *  sits in the hole at 9 yd instead of manning the tight end. Spy (Cover 1 / 0): the Mike spies the quarterback.
 *  Buzz (Cover 3): the strong safety buzzes to the hook / curl, the outside backer runs to the flat. Cloud (Cover 3):
 *  the strong corner squats in the flat and the strong safety rotates over the top to his deep third. Invert (Cover 2):
 *  the corners bail to the halves, the safeties come down to the flats. Prevent: every zone 4-6 yd deeper. Bracket
 *  (Cover 1 / 2-Man): the free man doubles the widest receiver to the strength, under and over. */
enum class CoverTech { None, Robber, Spy, Buzz, Cloud, Invert, Prevent, Bracket };
/** How the front lines up inside its set: Over / Under shift the line a gap toward / away from the strength, Wide 9
 *  splits the ends, Bear covers the guards and center. Okie (3-4): the nose head up, the ends outside the tackles (5
 *  techniques). Tite (3-4): the ends inside the tackles (4i), the nose head up - the B gaps squeezed. */
enum class FrontAlign { Normal, Over, Under, Wide9, Bear, Okie, Tite };

struct DefCall {
  std::string id;
  std::string name;
  Front front = Front::Base;
  Coverage coverage = Coverage::C3;
  /** defenders (by role) sent on the rush in addition to the down linemen */
  std::vector<std::string> blitz;
  std::vector<std::string> tags;
  /** The set it is called from on the play-call screen ("4-3 Over", "Nickel Normal"); empty = the front's name. */
  std::string set;
  FrontAlign align = FrontAlign::Normal;
  /** Down linemen who drop into coverage instead of rushing (a zone blitz, a simulated pressure). */
  std::vector<std::string> drop;
  CoverTech tech = CoverTech::None;
  /** Disguise: the safeties show the other shell before the snap (two high for a single-high call, one high for a
   *  two-high one) and rotate to the call's at the snap. */
  bool rotate = false;
  /** The two inside backers walked up into the A gaps before the snap (a mug): whether each comes is in `blitz`. */
  bool mug = false;

  // ---- what the play-call screen shows (set for every call in DEF_PLAYS())
  /** The front ("4-3 Over", "3-4 Okie", "Nickel 3-3-5"). */
  std::string frontLabel;
  /** The coverage as called ("Cover 3 Buzz", "Cover 1 Robber", "Tampa 2"). */
  std::string coverLabel;
  /** "Base" (four or fewer rush, no linebacker or back sent), "Man Blitz", "Zone Blitz" (five with three under, three
   *  deep), "Sim Pressure" (four rush, one of them from the second level, a lineman dropping), "DB Blitz", "Mug",
   *  "Prevent". */
  std::string pressure;
  std::string desc;

  bool hasTag(const char* t) const {
    for (const auto& s : tags)
      if (s == t) return true;
    return false;
  }
  /** Men rushing at the snap: the down linemen and the blitz, less any lineman who drops. */
  int rushers() const;
  /** A linebacker or defensive back is sent (a blitz or a simulated pressure). */
  bool blitzing() const { return !blitz.empty(); }
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
/** "Run", "Pass", "PA", "Screen", "RPO", "Trick", "Kneel", "Spike". */
const char* playCatName(PlayCat c);
const char* frontName(Front f);
const char* coverageName(Coverage c);
/** "", "robber", "spy", "buzz", "cloud", "invert", "prevent", "bracket". */
const char* coverTechName(CoverTech t);
/** Down linemen in the front (4 in the 4-3 sets, 3 in the 3-4, 3-3-5 and 3-2-6, 2 in the 2-4-5). */
int frontLinemen(Front f);
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
