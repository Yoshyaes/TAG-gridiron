// Port of src/sim/play.ts
// Per-play physical simulation at 60 Hz.
// Offense frame: offense attacks +x; LOS at x = los (0 = own goal line, 100 = opponent goal line).
// y in [0, FIELD_W]; +y is the offense's LEFT. A renderer maps this frame to world space using
// Game::worldDir() (+1: offense attacks world +x) -- see Weather wind for the same convention.
//
// Porting notes / representation choices:
//  - Entities live in `PlaySim::storage` (std::vector<std::unique_ptr<Ent>>, never resized after
//    construction). Every Ent* (ents/off/def, Ent::engaged, Ent::man, Ball::holder, PlayEvent::a,
//    ...) points into that storage and is valid for the PlaySim's lifetime only. PlaySim is
//    non-copyable and non-movable; Game::makePlay hands it out as std::unique_ptr.
//  - Player* (Ent::p, PlayResult::passer, ...) point into Team::roster and outlive the sim.
//  - TS string unions used for logic are enum classes (Side, EntState, Anim, BallMode, PlayKind);
//    each has a *Name() helper returning the original TS string. Roles stay std::string.
//  - TS `null`/`undefined` object refs -> nullptr; optional values -> std::optional;
//    `Ent::zone` uses "" for null.
//  - PlayResult::events is a copy of PlaySim::events taken in endPlay(); any event emitted later
//    in the same step is appended to both (TS shared the array), so they always agree.
#pragma once

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>
#include <utility>
#include <vector>

#include "Playbook.h"
#include "Rng.h"
#include "Types.h"
#include "Vec.h"

namespace tag {

constexpr double G = 10.72;  // gravity in yd/s^2
constexpr double SIM_DT = 1.0 / 60.0;

enum class Precip { None, Rain, Snow };
inline const char* precipName(Precip p) { return p == Precip::Rain ? "rain" : p == Precip::Snow ? "snow" : "none"; }

struct Weather {
  double windX = 0;  // yd/s, in WORLD frame (+x = toward east end zone)
  double windY = 0;
  Precip precip = Precip::None;
  double tempF = 60;
};

enum class Side { Off, Def };
inline const char* sideName(Side s) { return s == Side::Off ? "off" : "def"; }

enum class EntState { Pre, QB, Route, PassBlock, RunBlock, Rush, Man, Zone, RunFit, Carrier, Pursue, Convoy, Down, Idle, Spy, Fake };
const char* entStateName(EntState s);

/** Animation state for the renderer ("idle","run","throw","juke","tackle","block","kneel","catch","down","dive","celebrate"). */
enum class Anim { Idle, Run, Throw, Juke, Tackle, Block, Kneel, Catch, Down, Dive, Celebrate };
const char* animName(Anim a);

class Ent {
 public:
  Ent(Player* player, Side side, std::string role, const V2& pos);

  Player* p;
  Side side;
  std::string role;
  bool isOL = false;  // cached: role in LT/LG/C/RG/RT
  bool isDL = false;  // cached: role in LE/LDT/RDT/RE

  V2 pos;
  V2 vel{0, 0};
  V2 want{0, 0};
  double face;  // radians, offense frame (0 = +x)
  EntState state = EntState::Pre;
  double mass;  // kg
  double radius;
  double vmax;
  double tau;
  double latA;
  Ent* engaged = nullptr;
  std::vector<Ent*> engagedBy;
  double engageEdge = 0;
  double engT = 0;
  double stun = 0;
  /** Engaged block: who is winning the rep, from the defender's side (-1 blocker driving him, +1 defender driving
   *  the blocker). Same value on the defender and his blockers; 0 when not engaged. Presentation only. */
  double linePush = 0;
  /** Pancaked: down on his back for the rest of the stun (run blocking). */
  bool pancaked = false;
  double tackleCd = 0;
  std::vector<V2> route;
  size_t routeIdx = 0;
  bool settle = false;
  double outSign = 1;
  /** The route he runs ("block" for a blocker; "" when he has none: the QB, a run play). Hot routes change it. */
  std::string routeName;
  /** Pre-snap: walking to a new alignment (a shift, a flipped formation); PlaySim::presnapStep moves him there. */
  std::optional<V2> shiftTo;
  std::string zone;  // "" = no zone
  std::optional<V2> zoneSpot;
  Ent* man = nullptr;
  Ent* blockTarget = nullptr;
  double pressDelay = 0;
  std::deque<V2> velHist;
  double reactT = 0;
  double downT = 0;
  bool hasBall = false;
  Anim anim = Anim::Idle;
  double animT = 0;
  std::optional<V2> rushTarget;
  bool userControlled = false;
  double gap = 0;  // run-fit gap y
  double getOff = 0;
  double sprintT = 0;
  /** deep-third / half / quarter / single-high defender: keeps last-line leverage */
  bool deepRole = false;
  bool eligible = false;
  std::vector<Ent*> beatBy;
  double bitFake = 0;

  const Ratings& r() const { return p->r; }
  double speed() const { return len(vel); }
  bool free() const { return !engaged && stun <= 0 && state != EntState::Down; }
  const char* animName() const { return tag::animName(anim); }
  const char* stateName() const { return entStateName(state); }
};

enum class BallMode { Snap, Held, Air, Loose, Dead };
const char* ballModeName(BallMode m);

struct Ball {
  BallMode mode = BallMode::Snap;
  Ent* holder = nullptr;
  V3 pos;
  V3 vel;
  Ent* target = nullptr;
  std::optional<V2> targetSpot;
  Ent* thrower = nullptr;
  double throwX = 0;
  double spin = 0;
  bool lob = false;
  double t = 0;
};

struct PlayEvent {
  double t = 0;
  std::string type;   // "snap","handoff","throw","throwaway","catch","drop","pbu","int","batted",...
  Ent* a = nullptr;   // valid while the PlaySim lives
  Ent* b = nullptr;
  double x = 0;
  std::string note;
};

enum class PlayKind { Run, Pass, Sack, Scramble, Kneel, Spike };
const char* playKindName(PlayKind k);

struct PlayResult {
  PlayKind kind = PlayKind::Run;
  double endX = 0;
  int yards = 0;
  bool complete = false;
  bool incomplete = false;
  bool interception = false;
  bool fumbleLost = false;
  bool turnover = false;
  std::optional<Side> td;  // which side scored, if any
  bool safety = false;
  bool touchback = false;
  bool outOfBounds = false;
  double duration = 0;
  Player* passer = nullptr;
  Player* rusher = nullptr;
  Player* receiver = nullptr;
  Player* target = nullptr;
  std::vector<Player*> tacklers;
  Player* sackedBy = nullptr;
  Player* intBy = nullptr;
  Player* fumbleBy = nullptr;
  Player* fumbleRecBy = nullptr;
  Player* forcedBy = nullptr;
  Player* pbuBy = nullptr;
  int airYards = 0;
  int yac = 0;
  bool deepContested = false;
  bool thrownAway = false;
  bool pressured = false;
  double timeToThrow = 0;
  int brokenTackles = 0;
  std::optional<double> targetSpotY;
  double endY = 0;
  std::vector<PlayEvent> events;
};

/** Human input for the user-controlled entity. Written by the game/engine layer each frame. */
struct UserInput {
  V2 move{0, 0};         // desired direction in offense frame (length 0..1)
  bool sprint = false;
  std::string throwTo;   // offensive role to throw to ("" = none); consumed by the sim
  bool lob = false;
  bool juke = false;
  bool dive = false;
  bool switchReq = false;
};

/** A live kick return (kickoff or punt), in the RECEIVING team's frame: they are the offense and attack +x. */
struct KickReturnSpec {
  double kickX = 65;              // where the ball leaves the kicker's foot
  double coverX = 65;             // the coverage team's starting line
  double landX = 5;               // where the ball comes down (the returner fields it here)
  double landY = FIELD_W / 2;
  double hang = 3.9;              // seconds in the air
  double kickT = 0.45;            // the kick happens this far into the play (the kicker's approach)
  double apex = 22;               // flight height, yards (presentation)
  bool punt = false;
};

struct PlayConfig {
  Team* off = nullptr;
  Team* def = nullptr;
  const OffPlay* offPlay = nullptr;
  const DefCall* defCall = nullptr;
  double los = 25;
  double ballY = FIELD_W / 2;
  bool flip = false;
  Weather weather;
  /** +1 if offense attacks world +x, -1 otherwise (for wind) */
  double worldDir = 1;
  Rng rng;
  std::optional<Side> userSide;
  std::optional<double> down;
  std::optional<double> toGo;
  /** Live kick return: the sim fields the kick and runs the return instead of a scrimmage play. The units are the
   *  11 players on the field (offUnit[0] = the returner, defUnit[0] = the kicker / punter). */
  std::optional<KickReturnSpec> kick;
  std::vector<Player*> offUnit, defUnit;
};

struct TackleLogEntry {
  double p;
  double facing;
  bool dive;
  bool arm;
  int gang;
  double dx;
  double vc;
  bool juke;
};

enum class SimPhase { Live, Dead };

class PlaySim {
 public:
  /** Debug hook (tools only): when non-null every tackle attempt is appended. Not thread-safe. */
  static std::vector<TackleLogEntry>* tackleLog;

  explicit PlaySim(const PlayConfig& cfg);
  PlaySim(const PlaySim&) = delete;
  PlaySim& operator=(const PlaySim&) = delete;

  // ---- entities (storage owns them; the other vectors are views)
  std::vector<std::unique_ptr<Ent>> storage;
  std::vector<Ent*> ents;
  std::vector<Ent*> off;
  std::vector<Ent*> def;
  Ball ball;
  double t = 0;
  SimPhase phase = SimPhase::Live;
  std::vector<PlayEvent> events;
  std::optional<PlayResult> result;
  Rng rng;
  Ent* qb = nullptr;
  // kick returns
  Ent* kickerEnt = nullptr;
  Ent* returner = nullptr;
  bool kicked = false;
  bool isReturn() const { return cfg.kick.has_value(); }

  // ---- pre-snap adjustments (before the first step only). Deterministic: none of them draws from the rng, and a
  // play nobody adjusts runs exactly as before. Left / right are the offense's (+y / -y).
  enum class LineAdj { Base, ShiftL, ShiftR, Pinch, Spread };
  enum class ShellAdj { Base, Press, Off, ShowBlitz, Disguise };
  enum class DefAssign { Blitz, Spy, Hook, Flat, Deep, Man };
  /** The QB's progression: the play's reads, updated by hot routes (a receiver kept in to block drops out). */
  std::vector<std::string> readOrder;
  int slide = 0;              // pass protection slide: +1 toward +y, -1 toward -y
  bool maxProtect = false;    // backs and tight ends stay in to block
  LineAdj dlAdj = LineAdj::Base;
  LineAdj lbAdj = LineAdj::Base;
  ShellAdj shellAdj = ShellAdj::Base;
  bool preSnap() const { return t == 0 && phase == SimPhase::Live && !cfg.kick; }
  bool passPlay() const;
  /** Hot route: a new route (any ROUTES() name, "block" keeps him in) for an eligible receiver on a pass play. */
  bool hotRoute(Ent* e, const std::string& route);
  void setProtection(int slideDir, bool maxProt);
  void adjustLine(LineAdj a);
  void adjustLBs(LineAdj a);
  void adjustShell(ShellAdj a);
  /** A defender's new assignment (the user's defensive hot route). */
  bool assignDefender(Ent* d, DefAssign a);
  /** Motion / receiver shift: move an eligible receiver dy yards across the formation; his route runs from the new
   *  spot, and a defender manned up on him travels with him (motion shows man coverage). */
  bool motion(Ent* e, double dy);
  /** The blocking the line would set against the defense as it now stands (the play art shows it before the snap). */
  void previewBlocks();
  /** Walk a player to his new alignment from where he stands (an audible / flipped formation rebuilt the play). */
  void walkFrom(Ent* e, const V2& from);
  /** Presentation clock before the snap: shifted players walk to their new spots. */
  void presnapStep(double dt);
  /** A deep zone (middle of the field, thirds, halves, quarters), for the play art. */
  static bool deepZone(const std::string& z);
  Ent* carrier = nullptr;
  UserInput input;
  Ent* user = nullptr;
  size_t readIdx = 0;
  double readT = 0;
  bool dropDone = false;
  double throwStart = -1;
  bool handedOff = false;
  bool runRead = false;
  double runReadT = 99;
  bool passThrown = false;
  struct PendingThrow {
    Ent* tgt;
    bool lob;
    double at;
    bool away;
  };
  std::optional<PendingThrow> pendingThrow;
  bool pressured = false;
  bool scramble = false;
  int brokenTackles = 0;
  double catchX = 0;
  bool firstContact = false;
  const PlayConfig cfg;
  const double los;
  const double by;
  std::vector<std::pair<std::string, Ent*>> offRoles;
  std::vector<std::pair<std::string, Ent*>> defRoles;
  V2 wind;

  Ent* recoveredBy = nullptr;
  Ent* fumbler = nullptr;
  Ent* intBy = nullptr;
  Ent* pbuBy = nullptr;
  Ent* sackBy = nullptr;
  Ent* forcedBy = nullptr;
  std::vector<Ent*> tacklers;
  Ent* catcher = nullptr;
  Ent* passer = nullptr;
  bool deepContested = false;
  bool thrownAway = false;
  std::string endReason;

  // ---- lookups
  Ent* offRole(const std::string& role) const;
  Ent* defRole(const std::string& role) const;
  bool isLive() const { return phase == SimPhase::Live; }

  // ---- main loop
  /** Advance one tick (default 1/60 s). No-op once the play is dead. */
  void step(double dt = SIM_DT);
  /** Run the play to completion with AI only (headless). */
  const PlayResult& runToEnd(double maxT = 40);
  void endPlay(const std::string& reason);

  // ---- user control
  void pickUserEnt(Side side);
  void switchUser();

  // ---- queries / actions (public in TS)
  V2 zoneLandmark(const std::string& z) const;
  double ballSpotX() const;
  /** Which team currently "has" the ball: offense unless a turnover happened. */
  Side possSide() const;
  V2 predictPos(const Ent* e, double T) const;
  double releaseTime(const Ent* qbEnt) const;
  void startThrow(Ent* qbEnt, Ent* tgt, bool lob);
  double throwSpeed(const Ent* qbEnt) const;
  /** Expected separation (yards) at the catch point if thrown now. */
  double openness(const Ent* r, const Ent* qbEnt) const;
  void throwBall(Ent* qbEnt, Ent* tgt, bool lob);
  void throwAway(Ent* qbEnt);
  /** Where the airborne ball reaches catch height (~1.3 yd) on descent. */
  V2 ballLanding() const;

 private:
  const Formation* form_ = nullptr;
  std::unordered_map<const Ent*, double> reached_;
  std::unordered_map<const Ent*, double> lastBallD_;
  // exact memo for ballLanding(): pure function of ball pos/vel within a play
  mutable bool landingValid_ = false;
  mutable V3 landingPos_;
  mutable V3 landingVel_;
  mutable V2 landingOut_;

  Player* pickFrom(Team& team, const std::vector<Pos>& pos, std::vector<int>& used);
  V2 alignPos(const FormationSlot& slot, bool flip) const;
  void setupOffense();
  void setupDefense();
  std::vector<Ent*> eligibles() const;
  void alignDefense();
  void setupAssignments();
  void assignBlocks();
  void assignRoute(Ent* e, const std::string& rname);
  /** At the snap after any pre-snap change: everyone at his spot, blocking re-assigned against the new looks. */
  void finishPresnap();
  bool adjusted_ = false;
  std::unordered_map<const Ent*, V2> basePos_;       // the called alignment (shifts are relative to it)
  std::unordered_map<const Ent*, double> pressRoll_;  // the jam a pressing defender put on him at the line
  std::unordered_map<const Ent*, std::string> maxProtFrom_;  // routes max protect took away
  void moveTo(Ent* e, const V2& to);
  void thinkSpy(Ent* e);

  void ev(const char* type, Ent* a = nullptr, Ent* b = nullptr, const std::string& note = std::string());
  const std::vector<Ent*>& defendersOf(const Ent* e) const;
  const std::vector<Ent*>& teammatesOf(const Ent* e) const;
  double goalDir(const Ent* e) const;

  void updateSnap();
  void setCarrier(Ent* e);
  void think(Ent* e, double dt);
  bool applyUser(Ent* e);
  void thinkIdle(Ent* e);
  void thinkQB(Ent* e, double dt);
  double pressureOn(const Ent* qbEnt) const;
  bool scrambleLane(const Ent* qbEnt) const;
  double rushETA(const Ent* qbEnt) const;
  double laneBlock(const V2& a, const V2& b, const V2& d) const;
  double solveT(double D, double v0, double touch) const;
  void launch(Ent* qbEnt, const V2& aim, double T, double zTarget);
  double dragK() const;
  V3 simulateFlight(const V3& p0, const V3& v0, double T) const;
  void integrate(V3& p, V3& v, double k, double h) const;
  void thinkRoute(Ent* e);
  void thinkPassBlock(Ent* e);
  void thinkRunBlock(Ent* e);
  void thinkRush(Ent* e);
  V2 delayedVel(const Ent* e, double lag) const;
  void thinkMan(Ent* e);
  void thinkZone(Ent* e);
  bool reactToBall(Ent* e);
  void thinkPursue(Ent* e);
  void thinkConvoy(Ent* e);
  void thinkCarrier(Ent* e);
  void move(Ent* e, double dt);
  void engage(Ent* blocker, Ent* d);
  void disengage(Ent* b);
  void setupReturn();
  /** Kick return AI before the return starts (approach, flight, coverage lanes, the wall); false = normal AI. */
  bool thinkReturn(Ent* e);
  void updateKickBall(double dt);
  /** The pass rush / block shedding move that beat this blocker (presentation: "swim", "rip", "club", "spin",
   *  "bull", "longarm", "diprip"). Deterministic: does not draw from the rng. */
  const char* shedMove(const Ent* d, const Ent* b, bool passPro, double push) const;
  double pancakeRoll(const Ent* d) const;
  void updateEngagements(double dt);
  void collide();
  void updateBall(double dt);
  void resolveAir();
  void intercept(Ent* e);
  void checkTackles();
  bool clearPath(const Ent* d, const Ent* c) const;
  bool tryTackle(Ent* d, Ent* c, int gang, bool arm, bool dive = false);
  void fumble(Ent* c, Ent* by);
  void checkBounds();
};

}  // namespace tag
