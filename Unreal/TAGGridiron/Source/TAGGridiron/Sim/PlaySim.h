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
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>
#include <utility>
#include <vector>

#include "Abilities.h"
#include "BallPhysics.h"
#include "Playbook.h"
#include "Rng.h"
#include "Roster.h"
#include "Types.h"
#include "Vec.h"

namespace tag {

constexpr double G = 10.72;  // gravity in yd/s^2
// The end line (back of the end zone) and the deepest spot a pass, route or zone may aim at.
constexpr double END_LINE = 110.0;
constexpr double DEEP_AIM = 108.5;
constexpr double SIM_DT = 1.0 / 60.0;

/** Calibration constants of the play sim (realism plan): defaults are the shipped values; the headless tools
 *  can override them (SimCalibrate --set name=value) to fit the sim to NFL numbers. Values from Tools/Realism/fit.py
 *  (2026-11-12: 150 games seed 11, validated on 200 games seed 29: loss 18.4 -> 15.2, 15 -> 10 stats out of NFL tolerance;
 *  2026-11-19, with scrambles, the slide and second-level sheds: seeds 11/29/41, held-out 53, loss 21.2 -> 5.5 -> 2.8). */
struct Tune {
  double tackleBase = 3.5;   // tackle success logit base (refit 2026-11-20 after the sideline-route fix, with engArmK / runShed)
  // (accBase / accMidK / deepErr / intBias re-centred 2026-11-21 when a pass became catchable only within his hands'
  // reach - inHands - instead of from 1.6 yd at any height: were 0.2944 / 0.35 / 0.01498 / 0.71. 6 seeds x 200 games
  // against the old numbers: comp 64.74 vs 64.97, ypa 7.15 vs 7.11, ints 0.88 vs 0.89, 20+ yd comp 36.7 vs 38.2)
  double accBase = 0.19;    // throw placement error: base sd (yd)
  double accMidK = 0.6;      // extra placement error on throws of 12-22 yd (x up to 1 + this)
  double accSlope = 0.0157;  // throw placement error: per rating point below 99
  double deepErr = 0.0;      // extra error per air yard past 20
  double deepReadK = 1.68;  // QB read penalty for a target past 15 yd
  double deepReadSlope = 0.16873;  // ... per yard past 15
  double hotDeepK = 0.13598;  // hot-read penalty for a target past 12 yd
  double hotDeepSlope = 0.07041;  // ... per yard past 12
  double manCushion = 1.1;  // man coverage cushion on routes past 8 yd
  double zoneCushion = 3;  // deep zone cushion
  double zoneCushionV = 0.55;  // ... per yd/s of the receiver's speed
  double routeRead = 0.75;  // how far a good cover man plays the route ahead
  double zoneEyes = 4;  // yards of pull the quarterback's eyes have on an underneath zone defender (he leans to the receiver being read)
  double reachA = 14;  // yd/s2 a defender is taken to close on a throw with, in the quarterback's read (0: at once at top speed, the old read)
  double midContest = 1;  // 0..1: how much of the deep ball's contest radius and tightness a 15 yd throw already has
  double shortPen = 0;  // how much less open a target under 6 yd looks to the quarterback until shortPenT (he reads downfield first)
  double shortPenT = 2.2;
  double zoneReactK = 0.3;  // share of his reaction time a zone defender (eyes on the quarterback) needs to break on a throw
  double manLagK = 0.6;  // share of the read lag a man defender plays his receiver's moves with
  double zoneVert = 0;  // 1: a deep zone's cushion goes by the threat's speed upfield, 0: by his speed any way
  double zoneUnder = 1.2;  // how far under a receiver in his zone an underneath defender sits (yd toward the quarterback)
  double passProShed = 0.08;  // (0.075 before the 2026-11-22 playbook expansion: its quick game, screens and RPOs cut sacks 2.08 -> 1.9) pass-pro shed hazard base (per s) (0.045 before help blocks, 0.065 before blockers passed rushers off and backs picked up the blitz: pressured ~33%, sacks 6.7%)
  double qbThresh = 3.06;  // QB openness bar at the snap
  double qbDecay = 0.25352;  // ... drop per second
  double qbLate = 0.7;  // ... extra drop per second past 2.5 s
  double tightDeep = 1.9527;  // catch difficulty from a defender in phase, deep
  double tightShort = 2.51944;  // ... short
  double contested = 0.45;  // catch difficulty of any contested ball (0.6978 before non-target teammates stopped catching passes thrown past them)
  double breakdownBase = 1.32;  // pursuit breakdown: speed over the carrier's
  double breakdownK = 1.98;  // ... per yard away
  double catchKeep = 0.22;  // speed kept through an uncontested catch (0.55: 7.3-7.5 yd an attempt, too much run after the catch)
  double penScale = 1.10;    // penalty rate multiplier
  double assistRange = 1.6;  // a closing pursuer this near joins the tackle (yd)
  double tackleReach = 0.44;  // contact reach beyond the two bodies (yd)
  double pursuitK = 0.97;  // pursuit lead base
  double deepKeep = 0.8;   // how far a deep defender stays over the top of a carrier (1 = 4 yd cap)
  double behindK = 0.25;   // QB read penalty per yard a man behind the line lacks to get back to it
  double behindDepthK = 0.3;  // ... and per yard past the first that the catch is behind the line (not on a screen)
  double screenT = 1.9;       // a screen is thrown once the line has let the rush by (s)
  double behindHotK = 1.0;    // ... the same on a hot throw
  double softZone = 0.5;      // how far the underneath zones sink on 2nd/3rd and long (x up to 4 yd) (0: 3rd & 10+ converted 21-23%, NFL 20%)
  double paBite = 2.0;        // play action: how hard the front reads the run fake (a rusher plays the mesh for a beat;
                              // 0: only the linebackers bit, and play action was sacked 12 % against ~8 % on Sundays)
  double paRead = 1.0;        // x the time a play-action quarterback spends on each read
  double paCallW = 1.0;       // x how often a CPU offense calls play action (Game::cpuOffense)
  double routePace = 1.0;     // x top speed a receiver runs his route at until the ball is in the air to him
  double paBack = 1.0;        // > 0: the back carries out the play-action fake before his job (0: he blocks / runs his route from the snap)
  double throwAwayK = 0.4568;    // x how readily a quarterback throws it away (hot read with nobody, under pressure, late)
  double lateOpen = -0.5;     // a late quarterback throws to his most open man above this openness
  double holdCall = 0.46;     // offensive holding: x the play's holding risk the officials flag (0.54 gave 1.2 a team game; 2023 NFL 0.88)
  double driveBase = 1.3;     // yards after contact: the pile drive's base (yd, before ratings, mass, speed, help)
  double engArmK = 2.0;       // (fit 2026-11-20: first contact earlier, at the line) x a blocked defender's chance to reach out for a runner going by him
  double runPress = 1.0;      // a ball carrier's tempo behind the line on a run (x top speed; 1 = flat out)
  double mesh = 1.0;          // > 0: the run exchange is a mesh both men arrive at together (PlaySim::mesh); 0: the old exchange
                              // (the back runs at the quarterback and takes it at his hip, standing; A/B and Tools/HandoffCal)
  double meshTempo = 0.72;    // x top speed the back runs to the mesh at (he takes it at a run; 0.8 on a stretch)
  double meshArm = 0.9;       // yd from the quarterback to the back's path at the exchange (hip to hip 0.6-1.0 m in the pack's handoffs)
  double lbFill = 0.0;        // run keys: a linebacker fills downhill on run blocks before the handoff (x his top speed; 0: keyed on the handoff)
  double lbKeyT = 0.15;       // ... s after his reaction time
  double pursueAvoid = 1.0;   // pursuers steer around bodies in their path (look-ahead 1.5 + 2x this yd; 0: straight at the ball)
  double fallMu = 0.35;      // a tackled carrier's forward fall past his momentum's share (yd, mean)
  double lbRead = 0.8;       // read step: a zone linebacker on a run holds depth and shuffles with the flow until he reads it (x top speed cap; 0: drops)
  double readMinT = 1.15;     // s into the play before a progression read can be thrown
  double quickMinT = 0.7;     // ... and a quick-game throw
  double paDrop = 1.0;        // x the drop a play-action quarterback takes after the fake
  double secondLong = 0.6;    // QB read bonus for a throw short of the sticks on 2nd and 10+ (he takes the underneath throw)
  double wrapBreak = 0.05;    // a wrapped carrier's chance to tear free mid-drive (before ratings and the stick)
  double scrambleK = 1.0;     // how readily a quarterback with a lane runs (x; raised to film scrambles)
  double scrCov = 0.35;       // coverage scramble: per second, once nobody has come open and the rush has left a lane
                              // (nflverse 2023: 5.4 % of dropbacks are scrambles at 6.7 yd; with only the pressure
                              // scramble it was ~1 %, and the plays that were scrambles became throwaways and sacks)
  double scrT = 2.7;          // ... s into the play before he looks to run
  double scrZoneT = 1.1;      // ... s past his reaction time a dropping zone defender takes to come up on a scramble
  double scrGrass = 8.0;      // ... and the yd of grass he needs past the line (nobody free to come up that close)
  double slideK = 0.654;        // a scrambling quarterback about to be hit past the line slides (chance; most do, the rest fight for it)
  double behindRoom = 3.0; // ... the yards to spare he needs on the nearest defender
  double yacNoise = 2.5;   // a receiver's path-reading noise after the catch (x a back's)
  double runNoise = 1.0;   // a ball carrier's path-reading noise
  double releaseRate = 5;  // per second: a pass-pro rep breaks up once the ball is caught away from the QB
  double hotRate = 0.5;  // how readily a QB with a free rusher closing gets rid of it (x the hazard)
  double outletPen = 0.4303; // how much a QB passes up a target at or behind the line
  double outletBase = 1.2;   // ... in his read, on top of it, until outletT into the play (he reads downfield first)
  double outletT = 2.4;
  double screenW = 0.85;     // (1.7 before the 2026-11-22 playbook expansion, which has five screens to the old one: behind-the-line share 21 % -> 14.4 %, base 15.4) x how often the CPU calls a screen (play calling, Game::cpuOffense; 1.0 gave screens 5.2 % of dropbacks, NFL ~9-10 %,
                             // and passes behind the line 13 % against 18). Refit 2026-11-19 with tackleBase / qbThresh / runShed / secondLong.
  double passProHold = 3.8;  // s a pass block holds before it starts to give
  double setLead = 0.5;  // s ahead of the rusher a pass blocker sets up on his path
  double setReach = 1.4; // yd beyond touching at which a pass blocker's punch lands (he is engaged)
  double runShed = 0.048;   // run-block shed hazard base (per s; fit 2026-11-20)
  double lvl2Shed = 10.0;    // x that hazard for a linebacker or safety a lineman has climbed to (the second level)
  double lvl2Near = 1.36;    // ... and the yd the runner is from him when he works to get off it (a lineman's: 1.6)
  double rushEarly = 0.6;   // x the pass-rush shed hazard on 1st and 2nd down (the front plays the run first)
  double rushLate = 2.5;    // ... and on 3rd / 4th and 5+ (ears pinned back)
  double intBias = 2.3;  // a defender at the ball comes down with it (logit shift; NFL 2.35% of passes intercepted. 0.56 before only men with the ball in their hands' reach could intercept it - inHands, homeBall - 2026-11-21; 1.15 before eyes on the ball took the blind picks away - 54 % of them - 2026-11-24: 200 games x seeds 11/29/41, held-out 53: 0.78 -> 0.80 a team game)
  // eyes on the ball (Research/NextLevel/02 A2, Madden 26 / CFB 26's fix for "psychic" picks): a defender intercepts, or
  // breaks a pass up cleanly, only when he can see it - his body within eyesDeg of where the ball is coming from, or
  // within eyesDeg + eyesHead looking back over his shoulder once he has reacted to the throw and had eyesTurnT to turn
  // his head. A man defender on the target with his back to the ball (the trail man) does not turn: he runs with his
  // man and plays the hands as they come up (handsT before the ball gets there) - a late swipe at swipeK x the breakup
  // odds, never an interception, and dpiBlind x the interference risk. Every defender runs at a landing spot he reads
  // with an error (landErr yd at the release, scaled by his ball skills) that closes to nothing as the ball comes down.
  double eyesOn = 1;       // > 0: as above; 0: every defender at the ball plays it, and runs at its exact landing spot
  double eyesDeg = 90;
  double eyesHead = 90;    // (60 left the picks at 0.6 a team game even with every play on the ball a pick: too few men saw it)
  double eyesTurnT = 0.3;
  double handsT = 0.4;
  double swipeK = 0.85;
  double dpiBlind = 3.0;   // (fit to hold accepted DPI at 0.35 a team game, as before; nflverse 2023: 0.50 flagged)
  double landErr = 2.0;
  double holdK = 0.06;       // holding: per second of a rep the defender is winning (x how badly)
  double holdShed = 0.003;   // holding: a beaten blocker grabbing as his man gets away
  double aiMoveK = 0.65;     // a CPU ball carrier's move is worth this much of a user's well-timed one (calibrated)
  double frontGap = 2.2;     // yd between the men on the defensive line, centre to centre, at least (0: as each set draws them)
  double smartBlock = 1;     // > 0: blockers read the play as it goes (pass-offs, blitz pickup, climbing to the man who matters)
  double edgeKeep = 9;      // > 0: a tackle keeps his edge rusher while within this many yards of him (0: the old rule)
  double lineOrder = 1;      // pass protection matches the line to the rushers in order across it (0: nearest first)
  double stepUp = 1.6;       // the quarterback's step up into the pocket from an edge rush (yd/s of push)
  double helpPen = 5;        // a pass-pro double team: how much the second blocker takes off the rusher (skill points)
  double helpBlock = 1;      // a pass blocker with nobody to block helps on the rusher next to him (a double team); 0 off
  double throwStyle = -1;    // >= 0: the CPU quarterback throws the user's pass types (0 bullet, 1 lob, 2 high, 3 low; a
                             // man inside 6 yd gets the shovel), for a filmed check (-1: his own calibrated throws)
  double kickBlock = 0;      // > 0: the chance a punt or field goal is blocked, for a filmed check (0: the real rates)
  // a field goal / extra point played (Sim/PlaceKick.cpp; nflverse 2023: 1.7 % of field goals blocked - 0.4 % under 30
  // yd, 2.9 % from 50+ - and 0.65 % of extra points). Blocks are not rolled: a rusher's hand has to be where the ball is.
  // (fit 2026-11-22 with scratch KickCal, 3000 kicks a length bucket: blocked PAT 0.60, FG 0.47 / 1.03 / 1.93 / 3.17 %)
  double fgShed = 0.085;     // per s: an interior rusher's shed hazard against field goal protection (step and hinge)
  double fgEdgeK = 1.0;      // x an edge rusher's get-off quickness off the wing
  double fgReach = 1.36;     // a defender's hands up: x his height (standing reach, the arms stretched at the ball)
  double fgJump = 0.45;      // the jumper's leap at the line (yd at the top)
  double fgElevSd = 3.5;     // the kick's launch angle: spread (deg) about the kicker's line for its length
  double fgOpT = 1.30;       // snap to the foot on the ball (s; NFL operation 1.25-1.35)
  double fgPatRush = 0.65;   // x the rush's shed hazard on an extra point
  double fgPlay = 1;         // > 0: field goals and extra points are played (Sim/PlaceKick.cpp); 0: the old roll (A/B)
  double forceInt = 0;       // > 0: the chance a pass with a defender near the receiver is rolled an interception (film only)
  double handReach = 1.3;    // yd from his feet (flat) past which a ball not brought to him (homeA) is only caught laid out, and not intercepted, only tipped (with homeA 0: unused)
  double homeA = 12;         // the last 0.4 s of a pass whose outcome is rolled: yd/s^2 the ball may be brought onto the hands of the man who plays it (PlaySim::homeBall; 0: off, the ball is awarded from up to 1.6 yd off him)
  double fumbleK = 0.704;        // x the fumble chance at a tackle (> 1 to film fumbles; 0.65 holds fumbles lost near the NFL's 0.45 a team game)
  double abilities = 1;      // Superstar / X-Factor abilities in play (Abilities.h); 0: every man plays as his ratings
  // with abilities in play the league's stars are better than their ratings, so the league average is held where the NFL
  // has it (the stars stand above it, the rest a shade below): a tackle logit and a throw placement factor for every man
  // (fit with SimCalibrate, abilities on; with them off neither applies and every play is as it was)
  // (refit 2026-11-19 over seeds 11/29/47/83, held-out 101: 1 box stat out against 3 with 0.12 / 1.05)
  double abilTackle = 0.28;  // (refit 2026-11-19 on seeds 11/29/41 with accBase, qbDecay, intBias after the league parity change)
  double abilAcc = 1.04352;
  double runJobs = 1;  // > 0: runs block as drawn - pulls, kick-outs, lead blocks, down blocks (RunScheme::jobs / down); 0: every man by the line's rules
  double jobEdge = 10;  // skill points a kick-out / trap block gets for its angle (an arc 3/4 of it, a wrap or lead half)
  // > 0: gap runs block down on the play side and hinge on the back side (RunScheme::down) by shifting each lineman's pairing a gap
  // inside. Off: as a cost shift it left the man over a puller's vacated gap free - power 3.2 / counter 3.5 / buck 3.8 yd
  // against 3.8 / 4.6 / 5.1 with the pulls alone (probe, 600 snaps each, 2026-11-22); pullers' gaps want a real
  // back-block / hinge rule before it goes back on
  double downBlock = 0;
};
Tune& tune();
/** Set a tune field by name; false if there is none. */
bool setTune(const std::string& name, double v);

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

/** The highlight stick. A ball carrier's moves (the renderer plays Anim::Juke with the move's own clip): a juke (a
 *  sharp cut), a spin (a full turn out of a tackler's arms), a truck (the shoulder lowered into a man in front), a stiff
 *  arm (a hand to the chest of a man coming from the side), a hurdle (over a man going low). A defender's: the hit
 *  stick (a lunge into the carrier for a big hit: it stops him dead and jars the ball, a whiff leaves him on the turf)
 *  and the cut (a low tackle at the legs: it beats a truck, a hurdle beats it). Each is live for a fraction of a
 *  second (moveWindow) and changes the tackle odds of a contact in that time by who has the leverage. */
enum class Move { None, Juke, Spin, Truck, StiffArm, Hurdle, Hit, Cut };
const char* moveName(Move m);
double moveWindow(Move m);

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
  bool kickGunner = false;  // a punt team's gunner: released at the snap
  bool kickHoldup = false;  // a punt return's hold-up man: on the punt team's lineman across from him, then riding him
  Ent* kickMan = nullptr;    // ... the man he holds up (and stays on, down the field, after the kick)
  double slipT = 0;  // a beaten pass blocker: seconds his rusher still goes past him without bumping
  /** Engaged block: who is winning the rep, from the defender's side (-1 blocker driving him, +1 defender driving
   *  the blocker). Same value on the defender and his blockers; 0 when not engaged. Presentation only. */
  double linePush = 0;
  /** Pancaked: down on his back for the rest of the stun (run blocking). */
  bool pancaked = false;
  double tackleCd = 0;
  bool protecting = false;  // a ball carrier with the ball covered up (UserInput::protectBall)
  bool strafing = false;    // a defender strafing (UserInput::strafe): square to the line, eyes on the ball
  double stripT = 0;        // a defender going for the ball: his tackles in this window are strip attempts
  std::vector<V2> route;
  size_t routeIdx = 0;
  bool settle = false;
  double outSign = 1;
  /** The route he runs ("block" for a blocker; "" when he has none: the QB, a run play). Hot routes change it. */
  std::string routeName;
  /** Pre-snap: walking to a new alignment (a shift, a flipped formation); PlaySim::presnapStep moves him there. */
  std::optional<V2> shiftTo;
  /** In motion (PlaySim::motion), or a man defender walking across with him: the ball can be snapped while he moves,
   *  and he keeps his spot and his speed at the snap (a running start). */
  bool inMotion = false;
  /** Walking to his spot: the way he faces once he is there (he faces where he walks meanwhile). */
  std::optional<double> setFace;
  std::string zone;  // "" = no zone
  std::optional<V2> zoneSpot;
  Ent* man = nullptr;
  Ent* blockTarget = nullptr;
  Ent* snapTarget = nullptr;  // (the man the protection gave him at the snap: diagnostics)
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
  /** A velocity change (juke, catch, broken tackle) spread over a human contact time instead of one tick. */
  V2 impulseRate{0, 0};
  double impulseT = 0;
  /** The highlight-stick move in progress (moveT = seconds since it started; live while moveT < moveWindow). */
  Move move = Move::None;
  double moveT = 9;
  double moveSide = 0;  // +1 = to his left (the offense frame's +y as he faces), -1 right
  bool moveOn(Move m) const { return move == m && moveT < moveWindow(m); }
  bool catchAdjust = false;  // tracking a pass thrown to him (relaxed lateral limit)
  /** deep-third / half / quarter / single-high defender: keeps last-line leverage */
  bool deepRole = false;
  bool eligible = false;
  std::vector<Ent*> beatBy;
  double bitFake = 0;
  /** The trenches, Madden's way: a rusher's energy for his moves (0..1, each move costs it, it comes back slowly) and a
   *  blocker's power (0..1: worn down by the length of the rep, a pile going backwards and the power moves he takes).
   *  The user's moves are played against them (PlaySim::rushMove); the CPU's sheds keep the fitted hazard, and its
   *  failed moves are shown (presentation, off a stable hash: the play's rng is untouched). */
  double rushEnergy = 1;
  double blockPower = 1;
  double moveCd = 0;      // seconds before the next move
  double stallT = 0;      // a move stoned: the blocker drives him this long
  std::string lastMove;   // the last move tried, and when, and whether it won
  double lastMoveT = -9;
  bool lastMoveWon = false;

  /** His ratings on this play: the player's, plus what his Superstar abilities (and a live X-Factor) add (Abilities.h).
   *  fx: the rest of what they do, read where the sim rolls. Without abilities rr == p->r and fx is neutral. */
  Ratings rr;
  AbilityFx fx;
  bool inZone = false;     // his X-Factor is live
  bool fastBall = false;   // (his top speed with the ball already raised: Home Run Hitter, Slipstream)
  /** Going for a loose ball: committed to a dive at it (seconds until his hands get there; < 0 not diving) at the spot
   *  he left his feet for. While diving he is not free (stun) and flies along the dive; at the end the ball is his or it
   *  squirts on and he is on the turf. */
  double looseDiveT = -1;
  V2 looseDiveAt{0, 0};
  V2 looseDiveV{0, 0};  // the dive's speed, fixed when he leaves his feet
  const Ratings& r() const { return rr; }
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
  V2 aimNoErr{0, 0};  // where the QB meant the ball to meet the receiver (before his error)
  double flightT = 0;  // planned flight time
  bool away = false;   // thrown away: nobody on the offense catches it
  // the ball as a rigid body (BallPhysics.h): a loose ball, a tipped pass and a dead one bouncing on are flown by it
  // (pos / vel follow it while `phys` is set); otherwise its orientation is kept along the flight with the spiral's roll
  BallBody body;
  bool phys = false;
  bool tumble = false;  // a pass knocked off its spiral (tipped, batted, dropped): it turns over in the air
  // what happens at the ball, rolled ~0.4 s before it gets there (preRollCatch) with the arrival's own odds, so the
  // presentation can start the capture of it in time - a two-man interception, a breakup, a drop - the way a sports game
  // picks the interaction before the contact. 0 none (rolled at the ball), 1 catch, 2 drop, 3 breakup, 4 interception.
  int fate = 0;
  Ent* fateBy = nullptr;  // the defender in a breakup / interception
  Ent* fatePassed = nullptr;  // a defender first to the ball whose play on it was rolled early and did not come off
  bool fateRolled = false;
  bool fateDive = false;  // a catch rolled as one he lays out for (decided with the roll, so the dive can start before the ball is there)
  // the flight as thrown, flown on beside the one brought onto the rolled man's hands (PlaySim::homeBall): whether he
  // gets a go at the ball is still what the throw itself gave him (ghostHit: it came within his reach), so bringing
  // the ball to his hands does not turn passes that sailed past him into catches
  double fateLeft = -1;  // s of flight left when the outcome was rolled (0.4, or less when it could only be rolled later)
  bool ghost = false, ghostHit = false;
  V3 ghostPos, ghostVel;
  double fateP = -1, fateD = -1, fatePbu = -1;  // (diagnostics: the odds the roll was made with)
  bool fateCont = false;
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

/** A foul the play gave the officials to see: the chance it is flagged and the man it would be on (the one whose act
 *  carried the most of it). Game::rollPostsnap makes the calls. */
struct FoulRisk {
  double p = 0;
  double top = 0;
  Player* by = nullptr;
  void add(double q, Player* who) {
    if (q <= 0) return;
    p = 1 - (1 - p) * (1 - (std::min)(q, 1.0));
    if (q > top) {
      top = q;
      by = who;
    }
  }
};
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
  int throwPath = -1;  // what made the QB throw: 0 read, 1 hot (rusher closing), 2 best under pressure, 3 quick game
  double olBlockDepth = -99;  // deepest an offensive lineman was engaged in a block (yards past the LOS)
  double dblFirstT = -1;      // when the first pass-pro double team formed (-1: none)
  double dblTeamT = 0;        // seconds a pass rusher had two pass blockers on him (summed over rushers)
  double manGap[6] = {0, 0, 0, 0, 0, 0}, manGapN[6] = {0, 0, 0, 0, 0, 0};  // man defender to his receiver, each half second from 0.5 s (sum yd, n)
  double manSpd[6] = {0, 0, 0, 0, 0, 0}, recSpd[6] = {0, 0, 0, 0, 0, 0};   // ... and their speeds
  double catchSep = -1;  // yards from the catcher to the nearest defender at the catch (-1: no catch)
  double throwSep = -1;  // ... and from the target to the nearest defender when the ball left the hand
  double arriveSep = -1; // ... and when the ball got there, caught or not (the NGS definition: every target)
  double flightT = 0;    // seconds the pass was in the air
  int catchNear = -1;    // who that nearest defender was: 0 the catcher's own man, 1 another man defender, 2 a zone defender, 3 other
  bool thrownAway = false;
  // eyes on the ball (Tune::eyesOn): at an interception, how far the man's body faced from where the ball came (deg) and
  // whether he could not see it (telemetry: must never happen); a breakup that was a blind swipe at the hands
  double intFaceDeg = -1;
  bool intBlind = false;
  bool pbuBlind = false;
  bool pressured = false;
  std::string pressWhy;  // how the first pressure came: "<near|eta> <shed|unblocked|loose> <role>" (calibration)
  double pressT = 0;
  double timeToThrow = 0;
  int brokenTackles = 0;
  // fouls from what happened on the field: a blocker beaten and hanging on (holding), a defender playing the man and not
  // the ball (pass interference), a cover man grabbing past 5 yd (defensive holding / illegal contact), a rusher landing
  // on the passer after the ball is gone (roughing), a tackle with a hand in the mask, a receiver pushing off
  FoulRisk foulHold, foulDpi, foulDhold, foulRough, foulMask, foulOpi;
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
  /** A highlight-stick move asked for this frame (consumed by the sim): the carrier's juke / spin / truck / stiff arm /
   *  hurdle, the defender's hit stick / cut. stickSide: +1 left, -1 right, 0 = let the sim pick (toward the tackler). */
  Move stickMove = Move::None;
  double stickSide = 0;
  /** A pass rush move asked for this frame by the user's engaged rusher (consumed by the sim): "swim", "rip", "spin"
   *  (finesse) or "bull", "club", "longarm" (power). */
  std::string rushMove;
  /** The quarterback throws it away (out of bounds, over everyone): consumed by the sim. */
  bool throwAway = false;
  /** The pass the user throws (Madden's): 0 bullet (button held), 1 lob / touch (tapped), 2 high (L1), 3 low (L2). A
   *  throw to a man a few yards away is a shovel whatever was asked. */
  int passStyle = 0;
  /** A field goal block: the user's rusher leaps at the line, hands up (consumed by the sim; `dive` lays him out
   *  across the kick). */
  bool blockJump = false;
  /** A pump fake (the receiver's button tapped twice): consumed by the sim. */
  bool pumpFake = false;
  /** Held (R1): the ball carrier covers the ball up with both arms - far harder to strip, but no moves and a step slower. */
  bool protectBall = false;
  /** Held (L2): the user's defender strafes - square to the line of scrimmage, shuffling and backpedalling with his eyes
   *  on the play, quicker to change direction, slower flat out. */
  bool strafe = false;
  /** Pressed (R1): the user's defender goes for the ball, not the man - the next tackle he makes is a strip attempt (less
   *  likely to bring the carrier down, far more likely to knock the ball out). Consumed by the sim when it is used. */
  bool strip = false;
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
  /** Fielded in the end zone and downed there: the returner catches it and takes a knee (a touchback). A CPU returner
   *  does as told; a user's returner kneels when the stick is left alone after the catch. */
  bool downIt = false;
};

/** A field goal or an extra point, played (Sim/PlaceKick.cpp), in the KICKING team's frame (they are the offense and
 *  attack +x, the posts at x = END_LINE). The kick unit protects (step and hinge), the block unit rushes; the snap, the
 *  hold and the foot are on the clock below; the strike (Game::placeKick: its speed, angle and line) is flown from the
 *  spot, and it is blocked only if a rusher's hands are where it goes. */
struct PlaceKickSpec {
  bool pat = false;
  double spotX = 0;       // the hold: 7 yd behind the line
  double setT = 0.9;      // the units set this long before the snap
  double opT = 1.30;      // snap to the foot (Tune::fgOpT)
  double catchT = 0.42;   // snap to the holder's hands
  double placeT = 0.78;   // ... and the ball down on its point
  // the strike: launch speed (yd/s) and angle (deg), drag (per yd), across at the posts' plane (yd, + the kicking
  // team's left), the plane's distance from the spot; and what it is if nothing touches it (Game::placeKick's call)
  double v = 33, elevDeg = 30, drag = 0.0045, miss = 0, plane = 33;
  bool good = false, isShort = false;
  int look = -1;           // the block unit's rush (0 middle push, 1 overload left, 2 overload right, 3 stunt; -1 drawn)
  bool forceBlock = false; // film: the protection breaks down in the middle (Tune::kickBlock)
};
constexpr double PLACE_CROSSBAR = 3.333, PLACE_UPRIGHT = 3.083;  // the goal (yd): crossbar 10 ft up, uprights 18'6" apart

/** A man's job on a field goal (PlaySim::placeMan). */
enum class PlaceJob : uint8_t { None, Snap, Guard, Tackle, End, Wing, Hold, Kick, Push, Edge, Jump, Spy, Flat };
struct PlaceMan {
  PlaceJob job = PlaceJob::None;
  double side = 0;     // +1 the offense's left (+y), -1 its right, 0 the middle
  V2 home{0, 0};       // where he lined up
  V2 aim{0, 0};        // a rusher's point to get to: the ball's path at the line; a blocker's gap
  double jumpT = -1;   // he left his feet for the jump at the line (sim time)
  double diveT = -1;   // he laid out across the kick
  V2 diveDir{0, 0};
  double stuntT = -1;  // a looper: when he comes round
  Ent* target = nullptr;  // a blocker's man
  int stage = 0;       // an edge rusher: 0 round the wing's corner, 1 at the ball; a man inside: 1 through to it
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
  /** A field goal / extra point played: offUnit the kick unit, defUnit the block unit (11 each; their jobs are given
   *  by position, PlaySim::setupPlace). */
  std::optional<PlaceKickSpec> place;
  std::vector<Player*> offUnit, defUnit;
  /** Superstar / X-Factor abilities: on, the game situation they read, who is in the zone (Game::makePlay fills it). */
  AbilityCtx abil;
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
  /** A number no other play has had (the renderer keys per-play state on it: a new play can reuse the old one's memory). */
  unsigned long long serial = 0;
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
  /** The kick fielded: when, and whether the returner has since brought it out of his end zone; kneelT: when he
   *  started down onto a knee (< 0: he has not). */
  double kickCatchT = -1, kneelT = -1;
  bool returnOut = false, returnMoved = false;

  // ---- a field goal / extra point (cfg.place, Sim/PlaceKick.cpp)
  bool isPlaceKick() const { return cfg.place.has_value(); }
  double placeSnapT() const { return cfg.place ? cfg.place->setT : 0.0; }
  double placeKickT() const { return cfg.place ? cfg.place->setT + cfg.place->opT : 0.0; }
  /** Before the snap of a place kick (the units set, linemen in their stances). */
  bool placeSet() const { return cfg.place && t < cfg.place->setT; }
  Ent* placeSnapper = nullptr;
  Ent* placeHolder = nullptr;
  Ent* placeKicker = nullptr;
  int placeLook = 0;             // the rush the block unit is in (PlaceKickSpec::look)
  bool placeSnapped = false, placeKicked = false;
  bool placeBlocked = false;     // off a hand at the line: a loose ball, live for both teams
  bool placeTipped = false;      // fingertips on it: it flies on, slower and off its line
  Ent* placeBlockBy = nullptr;
  double placeBlockT = -1;       // when (sim time)
  double placeBlockX = 0;        // where the ball was touched (yd past the spot)
  bool placeCrossed = false;     // reached the posts' plane
  bool placeGood = false, placeShort = false;
  double placeCrossZ = -1, placeCrossY = 0;  // height and across (+ left of the middle) at the plane
  double placeLandX = 0;         // where a short kick came down
  /** His job and what he is doing (nullptr: not a place kick, or not one of the 22). */
  const PlaceMan* placeMan(const Ent* e) const;
  /** Where the i-th man of a unit (as Game::orderPlaceUnits orders it) lines up for a place kick, the block unit in
   *  rush look `look` (the presentation stages a user's kick in the same spots before its play starts). */
  static V2 placeAlign(bool kickUnit, size_t i, int look, double losX, double spotX, double mid);

  // ---- pre-snap adjustments (before the first step only). Deterministic: none of them draws from the rng, and a
  // play nobody adjusts runs exactly as before. Left / right are the offense's (+y / -y).
  enum class LineAdj { Base, ShiftL, ShiftR, Pinch, Spread };
  enum class ShellAdj { Base, Press, Off, ShowBlitz, Disguise };
  enum class DefAssign { Blitz, Spy, Hook, Flat, Deep, Man };
  /** The QB's progression: the play's reads, updated by hot routes (a receiver kept in to block drops out). */
  std::vector<std::string> readOrder;
  int slide = 0;              // pass protection slide: +1 toward +y, -1 toward -y
  bool maxProtect = false;    // backs and tight ends stay in to block
  /** The run sent the other way at the line (the quarterback's call): the formation stays as it is, the play's aiming
   *  point and its blocking mirror; an offset back beside the quarterback crosses to his other side. */
  bool runFlip = false;
  bool flipRun();
  /** The run's aiming point across from the ball as it will be run (+ = the offense's left): the play's, mirrored by a
   *  flipped formation and by a run flipped at the line. 0 when the play is not a run. */
  double runAimDy() const;
  LineAdj dlAdj = LineAdj::Base;
  LineAdj lbAdj = LineAdj::Base;
  ShellAdj shellAdj = ShellAdj::Base;
  bool preSnap() const { return t == 0 && phase == SimPhase::Live && !cfg.kick; }
  /** Someone is still walking to his spot (walkFrom / motion / a shift): the formation is not set. */
  bool walking() const {
    // (the play's own motion - OffPlay::motion - is part of getting set: the ball is snapped as he gets to his spot)
    if (motionEnt_ && motionState_ < 2) return true;
    for (const Ent* e : ents)
      if (e->shiftTo && !e->inMotion) return true;
    return false;
  }
  /** The play's motion man (OffPlay::motion; nullptr: none) and where he is in it (0 waiting for the formation to set,
   *  1 moving, 2 at his spot: the ball can be snapped). */
  Ent* motionEnt_ = nullptr;
  int motionState_ = 0;
  /** The formation is set but for the play's motion man on his way (the set clock runs through a jet motion: the snap is
   *  timed to it, and the director snaps once he is there - motionPending() false). */
  bool setButMotion() const {
    for (const Ent* e : ents)
      if (e->shiftTo && !e->inMotion && !(e == motionEnt_ && motionState_ == 1)) return false;
    return !(motionEnt_ && motionState_ == 0);
  }
  bool motionPending() const { return motionEnt_ && motionState_ < 2; }
  double motionPreSpeed_ = 0;  // a jet man's pre-snap speed (his motion takes about the set's second)
  /** Option plays: the defender read (the zone read's back-side end, the RPO's conflict defender), and the
   *  quarterback's decision (he kept it / pulled it to throw). */
  Ent* readEnt = nullptr;
  bool qbKept = false;
  bool rpoThrow = false;
  /** The run's blocking job of a man at the snap (RunScheme::jobs: "kick", "wrap", "lead", "trap", "arc"; "" none). */
  const char* runJob(const Ent* e) const;
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
  /** The user's engaged rusher tries a move (see Ent::rushEnergy): won, he sheds the block; stoned, he stalls and loses
   *  more energy. False when he is not engaged or still recovering from the last one. */
  bool rushMove(Ent* d, const std::string& move);
  /** After the whistle: a pass dead in the air (out of bounds, past the end line) flies on until it lands. */
  void coastBall(double dt);
  /** A punt's two units from the snap to the catch (see PlaySim.cpp). False: the man is left to the ordinary return code. */
  bool thinkPuntUnit(Ent* e);
  /** Pre-snap: the user walks his defender to a new spot (stick = direction in the offense frame, length 0..1; a
   *  jog at full tilt). He stays on his side of the ball, on the field; his assignment runs from where he stands at
   *  the snap (a rusher's lane, a linebacker's gap, a zone dropped from there, his man followed). */
  bool userPresnapMove(const V2& stick, double dt, int slot = 0);
  /** A deep zone (middle of the field, thirds, halves, quarters), for the play art. */
  static bool deepZone(const std::string& z);
  Ent* carrier = nullptr;
  UserInput input;
  Ent* user = nullptr;
  /** Local multiplayer: the other humans on the field this play, each driving one man of his own side with his own
   *  input. Slot 0 is user / input above (the play's first human: the offense's play caller, else the defense's);
   *  mates[i] is slot i + 1. A man is never driven by two slots. */
  struct UserSlot {
    Ent* ent = nullptr;
    UserInput input;
    Side side = Side::Off;
  };
  std::vector<UserSlot> mates;
  int userSlots() const { return 1 + static_cast<int>(mates.size()); }
  Ent* slotEnt(int s) const { return s <= 0 ? user : (s - 1 < static_cast<int>(mates.size()) ? mates[s - 1].ent : nullptr); }
  UserInput& slotInput(int s) { return s <= 0 || s - 1 >= static_cast<int>(mates.size()) ? input : mates[s - 1].input; }
  /** The slot driving e (-1: the CPU's man). */
  int slotOf(const Ent* e) const;
  /** The input of the human driving e (slot 0's when e is nobody's). */
  UserInput& inputOf(const Ent* e) { const int s = slotOf(e); return slotInput(s < 0 ? 0 : s); }
  /** Slot s takes e (the man he had goes back to the CPU). False if another slot has e. */
  bool setSlotEnt(int s, Ent* e);
  /** Another human on side `side`: his man is `prefer` if it is on the field and free, else the next free man by the
   *  side's order (offense: the backs, then the ends and receivers; defense: the linebackers, then the secondary, then
   *  the line). Returns his slot. */
  int addMate(Side side, Ent* prefer = nullptr);
  /** Before the snap, a human on offense who is not on the quarterback steps through the eligible men (dir +1 / -1). */
  bool cycleEligible(int slot, int dir);
  size_t readIdx = 0;
  const Ent* qbLook = nullptr;  // the receiver the quarterback is reading now (zone defenders read his eyes)
  double readT = 0;
  bool dropDone = false;
  double dropDoneT = 0;  // sim time the QB hit the top of his drop
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
    int style = 0;  // UserInput::passStyle (the CPU throws 0 / 1 by its lob call)
    bool shovel = false;  // decided as the throw starts (the windup's clip and the flight agree: a back running past
                          // the quarterback was shovelled to in the clip and thrown to 18 yd downfield in the sim)
  };
  std::optional<PendingThrow> pendingThrow;
  bool pressured = false;
  std::string pressWhy;
  double pressT = 0;
  void notePressure(const Ent* q, const char* how);
  bool scramble = false;
  double scrambleT = -1;  // when he took off
  /** Play action: the side the run is faked to (+1 left / -1 right, offense frame), whether the quarterback boots
   *  away from it after the fake, and where he sets up (y): the middle, or on a boot the far side. */
  double paSide() const { return cfg.flip ? 1.0 : -1.0; }
  bool paBoot() const;
  /** Play action: when the fake reaches the mesh (from the back's depth) and when the ball is pulled after it. */
  double paMeshT() const;
  double paPullT() const { return paMeshT() + 0.2; }
  mutable double paMeshT_ = -1;
  /** The run exchange, fixed as the ball is snapped (the first time it is asked for). `at` is where the back takes the
   *  ball, on his path (`dir`) from his stance to the hole; `qbAt` where the quarterback gives it from, an arm's length
   *  off that path on his own side of it; `T` when the two get there together; `go` when the back leaves his stance to
   *  be there at a run. A toss has no mesh: `T` is its release and the ball is `tossDur` in the air to the back. */
  struct Mesh {
    bool ok = false, toss = false;
    V2 at{0, 0}, qbAt{0, 0}, dir{1, 0};
    double T = 0, go = 0, tempo = 0.7, tossDur = 0;
    double away = 1;  // the side the quarterback carries his fake out to after it (+1 left / -1 right): away from the run
  };
  const Mesh& mesh() const;
  /** Play action's mesh: the inside run to the fake's side (paSide), at the fake's own time (paMeshT). */
  const Mesh& paMesh() const;
  Mesh meshFor(const std::string& role, double aimDy, RunStyle style, double nominalT) const;
  mutable Mesh paMesh_;
  /** A run with an exchange (not a sneak, not a quarterback run), played through the mesh (Tune::mesh). */
  bool meshOn() const;
  /** Seconds until the ball changes hands (the presentation starts the two-man capture ahead of it); < 0 once it has. */
  double meshIn() const;
  /** A toss in the air: when it left the quarterback's hands (-1: none), and from where (the back has it at
   *  tossT0 + mesh().tossDur; the "toss" event marks the release, "handoff" the catch). */
  double tossT0 = -1;
  V2 tossFrom{0, 0};
  double meshDoneT = -1;  // when the ball changed hands (the quarterback carries out his fake from it)
  bool meshBurst = false; // the back has started his burst to the mesh (he jogs his first steps: thinkIdle)
  mutable Mesh mesh_;
  mutable Mesh premesh_;  // (what mesh() / paMesh() answer before the snap: worked out afresh each time, never fixed)
  double qbSetY() const;
  bool slideRolled = false;  // a scrambling quarterback's slide-or-not, decided once
  double qbThrowFace = 0;  // where the quarterback turned for his throw (the renderer's)
  FoulRisk foulHold, foulDpi, foulDhold, foulRough, foulMask, foulOpi;
  int brokenTackles = 0;
  double catchX = 0;
  double catchSep = -1;
  double throwSep = -1;
  double arriveSep = -1;
  int catchNear = -1;
  double dblFirstT = -1;
  double dblTeamT = 0;
  double olBlockDepth = -99;
  double manGap[6] = {0, 0, 0, 0, 0, 0}, manGapN[6] = {0, 0, 0, 0, 0, 0}, manSpd[6] = {0, 0, 0, 0, 0, 0}, recSpd[6] = {0, 0, 0, 0, 0, 0};
  int throwPath = -1;
  int taWhy_ = 0;  // why the ball was thrown away: 1 the user, 2 hot read with nobody, 3 under pressure, 4 late
  bool firstContact = false;
  const PlayConfig cfg;
  const double los;
  const double by;
  std::vector<std::pair<std::string, Ent*>> offRoles;
  std::vector<std::pair<std::string, Ent*>> defRoles;
  V2 wind;

  Ent* recoveredBy = nullptr;
  Ent* fumbler = nullptr;
  Ent* firstFumbler = nullptr;  // (the man who put the ball on the ground first: the run is his)
  std::string fumbleHow;     // "strip" (punched out by the second man), "hit" (jarred loose at the tackle), "sack"
  std::string recoverHow;    // "dive" (on the ground with it), "scoop" (picked up on the run)
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
  /** The two men in the wrap-up tackle being driven (the pile moves them, not their legs). */
  bool inWrap(const Ent* e) const { return wrap_ && (e == wrap_->c || e == wrap_->d); }
  /** Optional observer called after every step (headless realism metrics). */
  std::function<void(const PlaySim&)> onStep;

  // ---- main loop
  /** Advance one tick (default 1/60 s). No-op once the play is dead. */
  void step(double dt = SIM_DT);
  /** Run the play to completion with AI only (headless). */
  const PlayResult& runToEnd(double maxT = 40);
  void endPlay(const std::string& reason);

  // ---- user control
  void pickUserEnt(Side side);
  /** (slot: which human switches - he never takes a man another human has) */
  void switchUser(int slot = 0);
  /** Switch to the teammate that lies in a direction from the man the user has (dir: offense frame, any length): the
   *  nearest man within about 50 degrees of it. False when nobody is that way. */
  bool switchUserToward(const V2& dir, int slot = 0);

  // ---- queries / actions (public in TS)
  V2 zoneLandmark(const std::string& z) const;
  double ballSpotX() const;
  /** Which team currently "has" the ball: offense unless a turnover happened. */
  Side possSide() const;
  V2 predictPos(const Ent* e, double T) const;
  double releaseTime(const Ent* qbEnt) const;
  void startThrow(Ent* qbEnt, Ent* tgt, bool lob);
  double throwSpeed(const Ent* qbEnt) const;
  /** Longest pass (yards of air from the QB) the arm can put on a receiver. */
  double maxAir(const Ent* qbEnt) const;
  bool trailPlay(Ent* e, Ent* c);
  /** Change e's velocity by dv over dur seconds (no single-frame velocity jumps). */
  void impulse(Ent* e, const V2& dv, double dur);
  /** Start a highlight-stick move (false if he can't now: no ball, a move still on, down, engaged). side 0 = the sim
   *  picks (a juke away from the tackler, a stiff arm toward him). */
  bool startMove(Ent* e, Move m, double side = 0);
  /** The highlight: the move that suits this carrier against the man about to tackle him (Move::None if nobody is
   *  close), and that man. A power back trucks a man in front, an elusive one spins him; a man coming from the side
   *  gets the stiff arm, a man going low gets hurdled. */
  Move suggestMove(const Ent* c, const Ent** threat = nullptr) const;
  /** The tackle logit shift from the moves live at a contact (the carrier's lower it, the defender's raise it). */
  double moveTackleAdj(const Ent* d, const Ent* c, double facing, bool arm, bool dive) const;
  V2 steerVel(const Ent* e, const V2& vel, const V2& want, double dt) const;
  V2 routeWant(const Ent* e, const V2& pos, const V2& vel, size_t& idx) const;
  /** Expected separation (yards) at the catch point if thrown now. */
  double openness(const Ent* r, const Ent* qbEnt) const;
  /** shovelAt: the shovel decided as the throw started (1 / 0), or -1 to decide it at the release. */
  void throwBall(Ent* qbEnt, Ent* tgt, bool lob, int style = 0, int shovelAt = -1);
  /** The pass in the air was a shovel (an underhand flip to a man a few yards away): the renderer plays one. */
  bool shovel = false;
  void throwAway(Ent* qbEnt);
  /** Where the airborne ball reaches catch height (~1.3 yd) on descent. */
  V2 ballLanding() const;
  /** Where defender d reads the ball coming down: ballLanding() plus his read's error, which closes as it comes down
   *  (Tune::landErr; a fixed draw per man and throw off a hash - the play's rng is not touched). */
  V2 landingFor(const Ent* d) const;
  /** How far (deg) d's body faces from where the ball in the air is coming from (0: square to it). */
  double ballFacingDeg(const Ent* d) const;
  /** Defender d can see the pass now (Tune::eyesOn): facing it, or looking back over his shoulder in time - never the
   *  man on the target with his back to it. */
  bool eyesOnBall(const Ent* d) const;
  /** The rule itself, whether or not it is in play (the telemetry asks it with Tune::eyesOn 0 too). */
  bool canSeeBall(const Ent* d) const;
  bool intBlind = false;    // (telemetry: an interception by a man who could not see the ball - PlayResult::intBlind)
  double intFaceDeg = -1;
  bool pbuBlind = false;
  /** A pile on the move (stepWrap): where the carrier goes down if nothing stops it (his yards still to drive plus the
   *  fall), and the seconds the drive takes. False when no pile is moving. Read only (the presentation starts the paired
   *  tackle at the wrap, not at its end). */
  bool wrapEnd(V2& outSpot, double& outSecs) const;

 private:
  // ---- the expanded playbook's mechanics (Playbook.h RunScheme / PlayMotion / DefCall)
  struct RunJob {
    std::string job;      // "kick", "wrap", "lead", "trap", "arc"
    bool pulled = false;  // a lineman pulling (down the line behind it) rather than a back attacking
    bool turned = false;  // he has turned up into the hole / reached the man
    double toY = 0;       // where he pulls to (y)
  };
  std::unordered_map<const Ent*, RunJob> jobs_;
  void resolveRunJobs(std::vector<Ent*>& usedB, std::vector<Ent*>& usedD);
  bool thinkRunJob(Ent* e);
  /** The man the run's direction gives a relative label ("PG", "BT", ...) or the role itself. */
  Ent* jobMan(const std::string& who) const;
  bool endSqueeze_ = false;  // the zone read's end: squeezes the back (true) or plays the quarterback (false)
  bool readDone_ = false;
  double keepAim_ = 0;       // the quarterback's aim when he keeps it (y)
  Ent* rpoRecv_ = nullptr;
  double qbRunT_ = -1;       // a designed quarterback run: when he takes off with it
  bool designedQbRun() const;
  bool isWildcatPlay() const;
  void optionRead();         // the read at the mesh (zone read / RPO)
  Ent* screenEnt_ = nullptr; // the man the screen is thrown to
  // play motion (OffPlay::motion)
  double motionWait_ = 0;
  V2 motionTo_{0, 0};
  V2 motionVel_{0, 0};       // his speed at the snap (a jet man's running start)
  double motionSide_ = 0;    // a jet man's direction across the formation (+1 toward +y)
  void startPlayMotion();
  void snapPlayMotion();     // at the snap: he is at his spot, with his running start
  /** Where a jet man has to be at the snap, and how fast he goes, to meet the quarterback at the mesh. */
  void jetSpot(Ent* e, V2& at, V2& vel) const;
  // reverse (OffPlay::reverse)
  Ent* revEnt_ = nullptr;
  bool reversed_ = false;
  const Formation* form_ = nullptr;
  std::unordered_map<const Ent*, double> reached_;
  std::unordered_map<const Ent*, double> lastBallD_;
  std::unordered_map<const Ent*, double> looseTry_;  // when each man last went for the loose ball
  void stepBody(double dt);                          // the rigid ball, keeping pos / vel in step with it
  void startTumble(double spinScale);                // a pass knocked off its spiral
  void startGround();                                // a flight meets the turf: the rigid body takes it from here
  void preRollCatch();                               // the outcome at the ball, rolled before it gets there (Ball::fate)
  void homeBall(double dt);                          // the rolled outcome's man and the ball brought to the same place (Tune::homeA)
  void preRollLane();                                // a defender the pass will go within reach of on its way: his play on it rolled before it gets to him
  std::unordered_map<const Ent*, double> lanePassed_;  // defenders whose go at this pass was rolled ahead (preRollLane)
  // exact memo for ballLanding(): pure function of ball pos/vel within a play
  mutable bool landingValid_ = false;
  mutable V3 landingPos_;
  mutable V3 landingVel_;
  mutable V2 landingOut_;

  /** slots: the depth chart slots the role reads, set ones only (Roster.h roleSlots); empty: the old pick. */
  Player* pickFrom(Team& team, const std::vector<Pos>& pos, std::vector<int>& used, const std::vector<DSlot>& slots = {});
  V2 alignPos(const FormationSlot& slot, bool flip) const;
  void setupOffense();
  void setupDefense();
  /** Every man's abilities for this play (Ent::rr / fx), before anyone's assignment reads his ratings. */
  void applyAbilities();
  bool abilOn_ = false;  // abilities in play on this play (Tune::abilTackle / abilAcc apply)
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
  /** z0 / fwd: the release's height and how far in front of him (an overhand ball leaves at 6'2" over his feet; a
   *  shovel at the waist, out in front - from up there it jumped from the clip's hand to his helmet) */
  void launch(Ent* qbEnt, const V2& aim, double T, double zTarget, double z0 = 2.05, double fwd = 0.0);
  double dragK() const;
  V3 simulateFlight(const V3& p0, const V3& v0, double T) const;
  void integrate(V3& p, V3& v, double k, double h) const;
  void thinkRoute(Ent* e);
  void thinkPassBlock(Ent* e);
  void thinkRunBlock(Ent* e);
  void thinkRush(Ent* e);
  double fakeBite(const Ent* d) const;  // play action: how much a front man is still playing the run fake (0..1)
  /** The back on a play-action fake: to the mesh and through it toward the line, then his job. */
  bool thinkPaBack(Ent* e);
  V2 delayedVel(const Ent* e, double lag) const;
  void thinkMan(Ent* e);
  void thinkZone(Ent* e);
  bool reactToBall(Ent* e);
  /** A path direction bent around the nearest body in the way within `look` yd (a pursuer scraping over the trash). */
  V2 steerAround(const Ent* e, const V2& dir, double look, const Ent* ignore) const;
  void thinkPursue(Ent* e);
  void thinkConvoy(Ent* e);
  void thinkCarrier(Ent* e);
  void move(Ent* e, double dt);
  void engage(Ent* blocker, Ent* d);
  void disengage(Ent* b);
  void setupReturn();
  // a place kick (Sim/PlaceKick.cpp)
  std::unordered_map<const Ent*, PlaceMan> placeMen_;
  double pkX_ = 0, pkZ_ = 0, pkUx_ = 0, pkUz_ = 0, pkLat_ = 0, pkY0_ = 0;  // the strike's flight: along / up from the spot
  bool pkFly_ = false;
  double pkDoneT_ = -1;  // the play ends then (the ball in the net or on the turf)
  PlaceMan* pm(const Ent* e);
  void setupPlace();
  void placeUser();
  /** The kick's men before a block (the operation, the protection, the rush, after the kick); false = normal AI. */
  bool thinkPlace(Ent* e);
  /** The place kick's clock: the snap, the hold, the strike and its flight, the block check; before think(). */
  void updatePlace(double dt);
  void placeAfterMove();
  /** A hand in the ball's path this step: the block (or the tip). */
  bool placeBlockCheck();
  void placeTouch(Ent* d, bool tip);
  /** Kick return AI before the return starts (approach, flight, coverage lanes, the wall); false = normal AI. */
  bool thinkReturn(Ent* e);
  /** The returner with the ball in his own end zone: downs it (a knee, the touchback) when that is the call. */
  bool thinkDownIt(Ent* e);
  /** The front spaced out: no two men on the line of scrimmage closer than a body and an arm (yards between centres). */
  void spaceFront(bool targets);
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
  /** A tackle made with the pile still moving: the carrier drives on for his yards after contact with the tackler
   *  wrapped on him (stepWrap, ~2.2 yd/s), until the yards are spent, help arrives or the sideline stops it; then he goes
   *  down there (finishTackle). The old tackle jumped him up to four yards ahead in one step, a spot no body reached. */
  struct Wrap {
    Ent* c = nullptr;
    Ent* d = nullptr;
    double left = 0;     // yards still to drive
    double natFall = 0;  // the forward fall at the end (body over the feet)
    double latY = 0;     // sideways drift of the fall
    bool dive = false, arm = false;
    double vd = 0, md = 0;
    V2 off{0, 0};        // the tackler's place on the carrier
    double total = 0;    // the whole drive (yd)
    bool rolled = false; // his one chance to tear out of it has been taken
  };
  std::optional<Wrap> wrap_;
  void stepWrap(double dt);
  void finishTackle(Ent* d, Ent* c, double fall, double latY, bool dive, bool arm, double vd, double md, bool isSack, Ent* help);
  void fumble(Ent* c, Ent* by, const char* how = "hit");
  void checkBounds();
};

}  // namespace tag
