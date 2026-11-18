// Superstar and X-Factor abilities, TAG Gridiron's way.
//
// How good a man is decides what he can carry. Every player is graded against the rest of the league at his position
// (the top 22% are Stars, the top 10% Superstars, the top 3% X-Factors: TAG's ratings are tight, so the cut is a share
// of the position, not a fixed overall), and the grade gives him ability slots and ability points (AP):
//
//   Star        1 Superstar slot,   3 AP
//   Superstar   2 Superstar slots,  5 AP
//   X-Factor    3 Superstar slots,  7 AP, and one X-Factor
//
// Each ability costs 1-3 AP, is open to a set of positions, needs a grade (some only open to Superstars) and the
// ratings to back it ("Moonball Touch" wants a deep arm). A user picks his team's loadouts on the Superstars & X-Factors
// screen; the CPU (and any team nobody touched) gets the coach's picks (autoLoadout).
//
// Superstar abilities are always on (some only in their moment: the red zone, third down, the last two minutes).
// An X-Factor sleeps until its man earns it: its trigger (two completions of 20+ air yards, three tackles, ...) puts
// him IN THE ZONE, and the zone holds play after play until its defeat condition (a sack, a big play allowed, ...)
// knocks him out and the count starts again (AbilityState).
//
// In the sim an ability is never a script: it is numbers on the man (AbilityFx) - rating points for the play, and a
// few multipliers the play sim reads where it rolls a throw, a catch, a tackle, a shed. None of it draws from the rng,
// so a play without abilities runs exactly as before (Tune::abilities = 0 turns them all off: calibration A/B).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Types.h"

namespace tag {

class PlaySim;
struct PlayResult;

enum class AbilityId : uint8_t {
  None = 0,
  // ---- Superstar: quarterback
  PocketPoise, LaserLine, MoonballTouch, OffScript, FilmJunkie, TwoMinuteMaestro, EscapeHatch,
  // ---- ball carriers
  BatteringRam, AnkleTaker, ViseGrip, HomeRunHitter, SecondEffort, ReceivingBack,
  // ---- receivers / tight ends
  GlueHands, JumpBallAce, RouteArtist, JamBreaker, CatchAndGo, RedZoneThreat, ChainMover,
  // ---- blockers
  BrickWall, RoadGrader, Anchor, QuickFeet, Climber, SafetyValve,
  // ---- front seven
  EdgeBender, BullCharge, GapPlugger, Motor, Finisher, GetOff,
  // ---- tacklers / coverage
  SureTackler, BallPunch, Thumper, SidelineToSideline, Diagnoser, Blanket, ZoneReader, PickArtist, SwatTeam,
  PressMaster, DeepRange, ClosingSpeed,
  // ---- kickers / punters
  BigLeg, SteadyFoot, IceWater, PinPoint,
  // ---- X-Factors
  IceVeins, RocketArm, Juggernaut, Phantom, Spotlight, Slipstream, Fortress, Havoc, BlackHole, Enforcer, Lockdown,
  LastLine, Automatic,
  Count
};

enum class AbilityTier : uint8_t { Superstar, XFactor };

/** A player's grade: what he can carry. */
enum class StarGrade : uint8_t { None = 0, Star = 1, Superstar = 2, XFactor = 3 };
const char* starGradeName(StarGrade g);  // "", "Star", "Superstar", "X-Factor"
int gradeSlots(StarGrade g);             // Superstar slots: 0 / 1 / 2 / 3
int gradeAP(StarGrade g);                // ability points:  0 / 3 / 5 / 7

/** A rating an ability needs ("DAC 85"). */
struct AbilityReq {
  double Ratings::*key;
  int min;
  const char* abbr;
};

struct AbilityDef {
  AbilityId id = AbilityId::None;
  const char* key = "";     // stable save key ("pocket_poise")
  const char* name = "";    // "Pocket Poise"
  const char* blurb = "";   // what it does, one line
  AbilityTier tier = AbilityTier::Superstar;
  uint32_t posMask = 0;     // bit (1 << Pos) per position that can carry it
  StarGrade minGrade = StarGrade::Star;
  int cost = 1;             // AP (X-Factors: 0, they take the X-Factor slot)
  std::vector<AbilityReq> req;
  // X-Factor only: the zone
  int need = 0;             // trigger events to get in
  const char* trigger = ""; // "Two completions of 20+ air yards"
  const char* defeat = "";  // "A sack or an interception"
  const char* zoneFx = "";  // what the zone does
  const char* icon = "";    // two-letter badge ("IV")
};

const std::vector<AbilityDef>& abilityDefs();
const AbilityDef& abilityDef(AbilityId id);
const AbilityDef* abilityByKey(const std::string& key);
/** The abilities a position can ever carry (any grade), Superstar ones first, in catalog order. */
std::vector<const AbilityDef*> abilitiesFor(Pos pos, AbilityTier tier);

/** Why a man cannot equip it ("" = he can, budget aside): position, grade, a rating short. */
std::string abilityLock(const Player& p, const AbilityDef& a);
/** AP his equipped Superstar abilities use. */
int abilityAPUsed(const Player& p);
/** Equip / remove. Equip fails (false, why) when locked, out of slots or AP, or already on. An X-Factor replaces the one
 *  he had. */
bool equipAbility(Player& p, AbilityId id, std::string* why = nullptr);
bool unequipAbility(Player& p, AbilityId id);
bool hasAbility(const Player& p, AbilityId id);
/** The coach's picks: the X-Factor he fits best and the Superstar abilities that suit his ratings within his AP.
 *  Deterministic (ratings and catalog order only). */
void autoLoadout(Player& p);
/** Drop whatever he no longer qualifies for (a re-graded roster, a hand-edited save). */
void validateLoadout(Player& p);

/** Grade every player against his position across the league (percentiles), then give everyone the coach's picks. */
void gradeLeague(std::vector<Team*>& teams);
/** "xf=ice_veins;ss=pocket_poise,laser_line" (empty: nothing equipped) and back (unknown / locked keys dropped). */
std::string loadoutString(const Player& p);
void applyLoadoutString(Player& p, const std::string& s);

// ------------------------------------------------------------------ in the play
/** What an ability does on one play: rating points for the play and the multipliers the sim reads where it rolls. All
 *  defaults are neutral (a man without abilities is exactly himself). */
struct AbilityFx {
  Ratings add;              // rating points for this play (situational ones already decided)
  // passing
  double shortAcc = 1;      // x placement error of throws under 20 air yards
  double deepAcc = 1;       // x placement error of throws 20+
  double pressAcc = 1;      // x the accuracy lost to pressure
  double runAcc = 1;        // x the accuracy lost throwing on the move
  double armAdd = 0;        // yards of extra air the arm can put on a ball
  double qbEscape = 0;      // tackle logit against him in the pocket (- = harder to bring down)
  double ballSafe = 0;      // interception logit taken off his throws (a defender at the ball comes down with it less)
  // receiving
  double contested = 0;     // catch skill on a contested ball (rating points)
  double catchKeep = 0;     // share of his speed kept through the catch (added)
  double sepLag = 0;        // seconds added to the read lag of the man covering him
  double winBall = 0;       // logit taken off a defender's breakup / pick on a ball thrown to him
  // carrying
  double powerAdj = 0;      // tackle logit against him, tackler in front (- = harder)
  double elusAdj = 0;       // ... tackler from the side or behind
  double moveAdj = 0;       // ... while one of his moves is live
  double driveAdd = 0;      // yards after contact (the pile's mean drive)
  double wrapBreak = 0;     // chance added to tear out of a moving pile
  double fumble = 1;        // x his fumble chance
  double topSpeed = 0;      // share of top speed added with the ball in his hands
  // blocking
  double holdShed = 1;      // x the pass-rush shed hazard of a rusher he blocks
  double runHold = 1;       // x the run shed hazard of a man he blocks
  // the front
  double shed = 1;          // x his pass-rush shed hazard
  double runShed = 1;       // x his run shed hazard
  double moveWin = 0;       // logit on his rush moves (the user's)
  double energyRegen = 1;   // x his rush energy coming back
  double sackArm = 1;       // x his arm-free hazard on a quarterback while blocked
  double sackFinish = 0;    // tackle logit on a sack
  // tackling / coverage
  double tackleAdj = 0;     // tackle logit as the tackler
  double stopDrive = 0;     // yards taken off the pile's drive when he makes the tackle
  double hitFumble = 1;     // x the fumble chance of his tackles
  double coverLag = 1;      // x his man-coverage read lag
  double zoneLag = 1;       // x his zone break lag
  double pbu = 0;           // logit on breakups
  double intAdj = 0;        // logit on interceptions
  bool any = false;         // something is on him
};

/** The game situation an ability reads at the snap. */
struct AbilitySituation {
  int down = 1;
  double toGo = 10;
  double los = 25;          // ball on (offense frame: 100 = the goal line he attacks... the offense's)
  int q = 1;
  double clock = 900;
  int margin = 0;           // his team's score minus the other's
  bool offense = true;      // his side has the ball
  bool kick = false;        // a kick play (returns)
};
bool clutchTime(const AbilitySituation& s);   // 4th quarter / OT, 5:00 or less, within one score
bool twoMinute(const AbilitySituation& s);    // the last 2:00 of a half

/** The per-play context the game hands the sim (PlayConfig::abil). */
struct AbilityCtx {
  bool on = false;
  int q = 1;
  double clock = 900;
  int offMargin = 0;                 // offense score minus defense
  std::vector<int> zoneIds;          // players in the zone (their X-Factor is live)
  bool inZone(int id) const;
};

/** r += d over every rating (capped at 110: a boost on a 99 is past the scale, not wasted). */
void addRatings(Ratings& r, const Ratings& d);
/** Everything his abilities do on this play. */
void abilityFx(const Player& p, const AbilitySituation& s, bool inZone, AbilityFx& fx);
/** A kicker / punter's ratings for this kick (Big Leg, Steady Foot, Ice Water in the clutch, Automatic in the zone). */
Ratings kickRatings(const Player& k, bool punt, const AbilitySituation& s, bool inZone);

// ------------------------------------------------------------------ the zone, across the game
struct ZoneState {
  int progress = 0;
  bool active = false;
  int defeats = 0;     // (a streak count for two-strike defeats: two incompletions running)
  int entered = 0;     // times in the zone this game
};

/** A man in or out of the zone (the presentation shows it, the booth calls it). */
struct AbilityEvent {
  const Player* p = nullptr;
  AbilityId id = AbilityId::None;
  bool entered = true;        // false: knocked out
  std::string team;           // abbr
  std::string why;            // "2 completions of 20+ air yards" / "sacked"
};

struct AbilityState {
  std::unordered_map<int, ZoneState> zones;  // by Player::id
  std::vector<AbilityEvent> events;           // drained by the presentation (and logged)
  std::vector<AbilityEvent> history;          // every event of the game, never drained (the booth reads it)
  const ZoneState* zone(const Player& p) const;
  bool active(const Player& p) const;
  /** After a scrimmage play that counted (penalties aside): every X-Factor man on the field moves toward his zone or
   *  out of it. offAbbr / defAbbr name the teams for the events. */
  void afterPlay(const PlaySim& sim, const PlayResult& res, const std::string& offAbbr, const std::string& defAbbr);
  /** After a field goal (fieldGoal) or an extra point try: a made field goal counts toward Automatic, any miss
   *  defeats it. */
  void afterKick(const Player& k, bool made, bool fieldGoal, const std::string& abbr);
  /** The ids in the zone (for AbilityCtx). */
  std::vector<int> activeIds() const;
};

}  // namespace tag
