// Port of src/sim/play.ts -- per-play physical simulation at 60 Hz.
// Control flow, constants and RNG call order follow the TS line by line.
#include "PlaySim.h"

#include <cstdio>
#include <cstdlib>
#include "Roster.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace tag {

std::vector<TackleLogEntry>* PlaySim::tackleLog = nullptr;

namespace {

bool isOLRole(const std::string& role) { return role == "LT" || role == "LG" || role == "C" || role == "RG" || role == "RT"; }
bool isDLRole(const std::string& role) { return role == "LE" || role == "LDT" || role == "RDT" || role == "RE"; }
/** A tight end ("TE", "TE2", "TE3": 13 personnel and the jumbo sets). */
bool isTERole(const std::string& role) { return role.size() >= 2 && role[0] == 'T' && role[1] == 'E'; }
/** A back in the backfield's roles ("RB", "FB", "RB2": the third back of the wishbone, full house and power I). */
bool isBackRole(const std::string& role) { return role == "RB" || role == "FB" || role == "RB2"; }
bool isDeepZone(const std::string& z) {
  return z.rfind("deep", 0) == 0 || z.rfind("third", 0) == 0 || z.rfind("half", 0) == 0 || z.rfind("q", 0) == 0;
}
template <class T>
bool contains(const std::vector<T>& v, const T& x) {
  return std::find(v.begin(), v.end(), x) != v.end();
}
template <class T>
void removeValue(std::vector<T>& v, const T& x) {
  v.erase(std::remove(v.begin(), v.end(), x), v.end());
}
bool stateIn(EntState s, std::initializer_list<EntState> list) {
  for (EntState x : list)
    if (x == s) return true;
  return false;
}
bool isLB(const Ent* e) { return e->role == "WLB" || e->role == "MLB" || e->role == "SLB" || e->role == "LOLB" || e->role == "ROLB"; }
bool isCorner(const Ent* e) { return e->role == "LCB" || e->role == "RCB" || e->role == "NB" || e->role == "DB"; }
/** The offense's strength as the defense reads it (+1 = +y): the attached tight end's side, else the side with more
 *  receivers (the right on a balanced set) - the rule alignDefense lines up by. */
double strengthSide(const std::vector<Ent*>& off, double by) {
  bool teL = false, teR = false;
  int nl = 0, nr = 0;
  for (const Ent* e : off) {
    if (isTERole(e->role) && std::fabs(e->pos.y - by) < 5) (e->pos.y > by ? teL : teR) = true;
    if (e->eligible && std::fabs(e->pos.y - by) > 2) (e->pos.y > by ? nl : nr)++;
  }
  return teL && !teR ? 1 : teR && !teL ? -1 : nl > nr ? 1 : nr > nl ? -1 : -1;
}

}  // namespace

const char* entStateName(EntState s) {
  switch (s) {
    case EntState::Pre: return "pre";
    case EntState::QB: return "qb";
    case EntState::Route: return "route";
    case EntState::PassBlock: return "passblock";
    case EntState::RunBlock: return "runblock";
    case EntState::Rush: return "rush";
    case EntState::Man: return "man";
    case EntState::Zone: return "zone";
    case EntState::RunFit: return "runfit";
    case EntState::Carrier: return "carrier";
    case EntState::Pursue: return "pursue";
    case EntState::Convoy: return "convoy";
    case EntState::Down: return "down";
    case EntState::Idle: return "idle";
    case EntState::Spy: return "spy";
    case EntState::Fake: return "fake";
  }
  return "?";
}

const char* moveName(Move m) {
  switch (m) {
    case Move::None: return "";
    case Move::Juke: return "juke";
    case Move::Spin: return "spin";
    case Move::Truck: return "truck";
    case Move::StiffArm: return "stiffarm";
    case Move::Hurdle: return "hurdle";
    case Move::Hit: return "hitstick";
    case Move::Cut: return "cut";
  }
  return "";
}

double moveWindow(Move m) {
  switch (m) {
    case Move::None: return 0;
    case Move::Juke: return 0.35;
    case Move::Spin: return 0.5;
    case Move::Truck: return 0.45;
    case Move::StiffArm: return 0.6;
    case Move::Hurdle: return 0.55;
    case Move::Hit: return 0.45;
    case Move::Cut: return 0.45;
  }
  return 0;
}

const char* animName(Anim a) {
  switch (a) {
    case Anim::Idle: return "idle";
    case Anim::Run: return "run";
    case Anim::Throw: return "throw";
    case Anim::Juke: return "juke";
    case Anim::Tackle: return "tackle";
    case Anim::Block: return "block";
    case Anim::Kneel: return "kneel";
    case Anim::Catch: return "catch";
    case Anim::Down: return "down";
    case Anim::Dive: return "dive";
    case Anim::Celebrate: return "celebrate";
  }
  return "?";
}

const char* ballModeName(BallMode m) {
  switch (m) {
    case BallMode::Snap: return "snap";
    case BallMode::Held: return "held";
    case BallMode::Air: return "air";
    case BallMode::Loose: return "loose";
    case BallMode::Dead: return "dead";
  }
  return "?";
}

const char* playKindName(PlayKind k) {
  switch (k) {
    case PlayKind::Run: return "run";
    case PlayKind::Pass: return "pass";
    case PlayKind::Sack: return "sack";
    case PlayKind::Scramble: return "scramble";
    case PlayKind::Kneel: return "kneel";
    case PlayKind::Spike: return "spike";
  }
  return "?";
}

// =====================================================================================  Ent
Ent::Ent(Player* player, Side s, std::string r_, const V2& position)
    : p(player), side(s), role(std::move(r_)), pos(position) {
  isOL = isOLRole(role);
  isDL = isDLRole(role);
  face = side == Side::Off ? 0 : kPi;
  const Ratings& rt = p->r;
  rr = p->r;  // (PlaySim::applyAbilities adds his abilities' points)
  const double fat = 1 - 0.1 * p->fatigue;
  mass = p->weightLb * 0.4536;
  radius = clamp(0.36 + (p->weightLb - 180) / 900, 0.36, 0.55);
  // 99 speed ~ 10.7 yd/s (21.9 mph, the fastest in-game speeds of an NFL season ~22); 50 ~ 7.9 yd/s (16 mph, a 5.25 forty).
  // (8.6 at 50 put linemen at 18+ mph and most receivers at their top speed on most plays)
  vmax = (8.1 + (rt.speed - 50) * 0.057) * 0.97 * fat;
  tau = (1.0 - (rt.accel - 50) * 0.0078) / fat;
  latA = (8 + (rt.agility - 50) * 0.12) * fat;
}

Tune& tune() {
  static Tune t;
  return t;
}

bool setTune(const std::string& name, double v) {
  Tune& t = tune();
  if (name == "tackleBase") return t.tackleBase = v, true;
  if (name == "aiMoveK") return t.aiMoveK = v, true;
  if (name == "helpPen") return t.helpPen = v, true;
  if (name == "edgeKeep") return t.edgeKeep = v, true;
  if (name == "stepUp") return t.stepUp = v, true;
  if (name == "lineOrder") return t.lineOrder = v, true;
  if (name == "frontGap") return t.frontGap = v, true;
  if (name == "smartBlock") return t.smartBlock = v, true;
  if (name == "passProShed") return t.passProShed = v, true;
  if (name == "runShed") return t.runShed = v, true;
  if (name == "helpBlock") return t.helpBlock = v, true;
  if (name == "kickBlock") return t.kickBlock = v, true;
  if (name == "fgShed") return t.fgShed = v, true;
  if (name == "fgEdgeK") return t.fgEdgeK = v, true;
  if (name == "fgReach") return t.fgReach = v, true;
  if (name == "fgJump") return t.fgJump = v, true;
  if (name == "fgElevSd") return t.fgElevSd = v, true;
  if (name == "fgOpT") return t.fgOpT = v, true;
  if (name == "fgPatRush") return t.fgPatRush = v, true;
  if (name == "fgPlay") return t.fgPlay = v, true;
  if (name == "abilities") return t.abilities = v, true;
  if (name == "abilTackle") return t.abilTackle = v, true;
  if (name == "abilAcc") return t.abilAcc = v, true;
  if (name == "runJobs") return t.runJobs = v, true;
  if (name == "jobEdge") return t.jobEdge = v, true;
  if (name == "downBlock") return t.downBlock = v, true;
  if (name == "comboBlock") return t.comboBlock = v, true;
  if (name == "comboT") return t.comboT = v, true;
  if (name == "comboHold") return t.comboHold = v, true;
  if (name == "comboMax") return t.comboMax = v, true;
  if (name == "comboDrive") return t.comboDrive = v, true;
  if (name == "comboPush") return t.comboPush = v, true;
  if (name == "dblArm") return t.dblArm = v, true;
  if (name == "comboNear") return t.comboNear = v, true;
  if (name == "comboFill") return t.comboFill = v, true;
  if (name == "comboLate") return t.comboLate = v, true;
  if (name == "comboDeclare") return t.comboDeclare = v, true;
  if (name == "comboReach") return t.comboReach = v, true;
  if (name == "readBlocks") return t.readBlocks = v, true;
  if (name == "readOpen") return t.readOpen = v, true;
  if (name == "readShut") return t.readShut = v, true;
  if (name == "readPress") return t.readPress = v, true;
  if (name == "readPressD") return t.readPressD = v, true;
  if (name == "readLead") return t.readLead = v, true;
  if (name == "gapFit") return t.gapFit = v, true;
  if (name == "gapGive") return t.gapGive = v, true;
  if (name == "gapCommit") return t.gapCommit = v, true;
  if (name == "containW") return t.containW = v, true;
  if (name == "gapFlow") return t.gapFlow = v, true;
  if (name == "stretchAim") return t.stretchAim = v, true;
  if (name == "armSide") return t.armSide = v, true;
  if (name == "armShut") return t.armShut = v, true;
  if (name == "runNearShed") return t.runNearShed = v, true;
  if (name == "pdCredit") return t.pdCredit = v, true;
  if (name == "armPen") return t.armPen = v, true;
  if (name == "matchCov") return t.matchCov = v, true;
  if (name == "matchCarry") return t.matchCarry = v, true;
  if (name == "stunts") return t.stunts = v, true;
  if (name == "stuntEarly") return t.stuntEarly = v, true;
  if (name == "stuntJab") return t.stuntJab = v, true;
  if (name == "throwStyle") return t.throwStyle = v, true;
  if (name == "accBase") return t.accBase = v, true;
  if (name == "accSlope") return t.accSlope = v, true;
  if (name == "accMidK") return t.accMidK = v, true;
  if (name == "deepErr") return t.deepErr = v, true;
  if (name == "deepReadK") return t.deepReadK = v, true;
  if (name == "deepReadSlope") return t.deepReadSlope = v, true;
  if (name == "hotDeepK") return t.hotDeepK = v, true;
  if (name == "hotDeepSlope") return t.hotDeepSlope = v, true;
  if (name == "manCushion") return t.manCushion = v, true;
  if (name == "zoneCushion") return t.zoneCushion = v, true;
  if (name == "zoneCushionV") return t.zoneCushionV = v, true;
  if (name == "routeRead") return t.routeRead = v, true;
  if (name == "zoneReactK") return t.zoneReactK = v, true;
  if (name == "shortPen") return t.shortPen = v, true;
  if (name == "midContest") return t.midContest = v, true;
  if (name == "reachA") return t.reachA = v, true;
  if (name == "shortPenT") return t.shortPenT = v, true;
  if (name == "zoneEyes") return t.zoneEyes = v, true;
  if (name == "manLagK") return t.manLagK = v, true;
  if (name == "zoneVert") return t.zoneVert = v, true;
  if (name == "zoneUnder") return t.zoneUnder = v, true;
  if (name == "passProShed") return t.passProShed = v, true;
  if (name == "qbThresh") return t.qbThresh = v, true;
  if (name == "qbDecay") return t.qbDecay = v, true;
  if (name == "qbLate") return t.qbLate = v, true;
  if (name == "tightDeep") return t.tightDeep = v, true;
  if (name == "tightShort") return t.tightShort = v, true;
  if (name == "contested") return t.contested = v, true;
  if (name == "breakdownBase") return t.breakdownBase = v, true;
  if (name == "breakdownK") return t.breakdownK = v, true;
  if (name == "catchKeep") return t.catchKeep = v, true;
  if (name == "penScale") return t.penScale = v, true;
  if (name == "assistRange") return t.assistRange = v, true;
  if (name == "tackleReach") return t.tackleReach = v, true;
  if (name == "pursuitK") return t.pursuitK = v, true;
  if (name == "deepKeep") return t.deepKeep = v, true;
  if (name == "paCallW") return t.paCallW = v, true;
  if (name == "routePace") return t.routePace = v, true;
  if (name == "releaseRate") return t.releaseRate = v, true;
  if (name == "intBias") return t.intBias = v, true;
  if (name == "manCue") return t.manCue = v, true;
  if (name == "manCutBase") return t.manCutBase = v, true;
  if (name == "manCutRating") return t.manCutRating = v, true;
  if (name == "manCutAgi") return t.manCutAgi = v, true;
  if (name == "manCutLev") return t.manCutLev = v, true;
  if (name == "manBreakDeg") return t.manBreakDeg = v, true;
  if (name == "manDriveT") return t.manDriveT = v, true;
  if (name == "manDriveLead") return t.manDriveLead = v, true;
  if (name == "routePass") return t.routePass = v, true;
  if (name == "manLead") return t.manLead = v, true;
  if (name == "manCueCushion") return t.manCueCushion = v, true;
  if (name == "readKeys") return t.readKeys = v, true;
  if (name == "keyTau") return t.keyTau = v, true;
  if (name == "keyTauK") return t.keyTauK = v, true;
  if (name == "keyStepT") return t.keyStepT = v, true;
  if (name == "keyOL") return t.keyOL = v, true;
  if (name == "keyBack") return t.keyBack = v, true;
  if (name == "keyBiteK") return t.keyBiteK = v, true;
  if (name == "drawSet") return t.drawSet = v, true;
  if (name == "keyFill") return t.keyFill = v, true;
  if (name == "keySafety") return t.keySafety = v, true;
  if (name == "keyPaLine") return t.keyPaLine = v, true;
  if (name == "keyZoneFill") return t.keyZoneFill = v, true;
  if (name == "leverage") return t.leverage = v, true;
  if (name == "levHip") return t.levHip = v, true;
  if (name == "levGrabHold") return t.levGrabHold = v, true;
  if (name == "levK") return t.levK = v, true;
  if (name == "levCen") return t.levCen = v, true;
  if (name == "levCap") return t.levCap = v, true;
  if (name == "levClose") return t.levClose = v, true;
  if (name == "levSet") return t.levSet = v, true;
  if (name == "levSd") return t.levSd = v, true;
  if (name == "eyesOn") return t.eyesOn = v, true;
  if (name == "eyesDeg") return t.eyesDeg = v, true;
  if (name == "eyesHead") return t.eyesHead = v, true;
  if (name == "eyesTurnT") return t.eyesTurnT = v, true;
  if (name == "handsT") return t.handsT = v, true;
  if (name == "swipeK") return t.swipeK = v, true;
  if (name == "dpiBlind") return t.dpiBlind = v, true;
  if (name == "landErr") return t.landErr = v, true;
  if (name == "runShed") return t.runShed = v, true;
  if (name == "hotRate") return t.hotRate = v, true;
  if (name == "outletPen") return t.outletPen = v, true;
  if (name == "outletBase") return t.outletBase = v, true;
  if (name == "outletT") return t.outletT = v, true;
  if (name == "screenW") return t.screenW = v, true;
  if (name == "setLead") return t.setLead = v, true;
  if (name == "passProHold") return t.passProHold = v, true;
  if (name == "setReach") return t.setReach = v, true;
  if (name == "holdK") return t.holdK = v, true;
  if (name == "holdShed") return t.holdShed = v, true;
  if (name == "yacNoise") return t.yacNoise = v, true;
  if (name == "behindK") return t.behindK = v, true;
  if (name == "behindRoom") return t.behindRoom = v, true;
  if (name == "behindDepthK") return t.behindDepthK = v, true;
  if (name == "screenT") return t.screenT = v, true;
  if (name == "scrambleK") return t.scrambleK = v, true;
  if (name == "scrCov") return t.scrCov = v, true;
  if (name == "scrT") return t.scrT = v, true;
  if (name == "scrGrass") return t.scrGrass = v, true;
  if (name == "scrZoneT") return t.scrZoneT = v, true;
  if (name == "slideK") return t.slideK = v, true;
  if (name == "paBack") return t.paBack = v, true;
  if (name == "lvl2Shed") return t.lvl2Shed = v, true;
  if (name == "lvl2Near") return t.lvl2Near = v, true;
  if (name == "rushEarly") return t.rushEarly = v, true;
  if (name == "rushLate") return t.rushLate = v, true;
  if (name == "wrapBreak") return t.wrapBreak = v, true;
  if (name == "secondLong") return t.secondLong = v, true;
  if (name == "paBite") return t.paBite = v, true;
  if (name == "paDrop") return t.paDrop = v, true;
  if (name == "paRead") return t.paRead = v, true;
  if (name == "readMinT") return t.readMinT = v, true;
  if (name == "lbFill") return t.lbFill = v, true;
  if (name == "mesh") return t.mesh = v, true;
  if (name == "meshTempo") return t.meshTempo = v, true;
  if (name == "meshArm") return t.meshArm = v, true;
  if (name == "runPress") return t.runPress = v, true;
  if (name == "driveBase") return t.driveBase = v, true;
  if (name == "holdCall") return t.holdCall = v, true;
  if (name == "throwAwayK") return t.throwAwayK = v, true;
  if (name == "lateOpen") return t.lateOpen = v, true;
  if (name == "engArmK") return t.engArmK = v, true;
  if (name == "lbKeyT") return t.lbKeyT = v, true;
  if (name == "lbRead") return t.lbRead = v, true;
  if (name == "fallMu") return t.fallMu = v, true;
  if (name == "pursueAvoid") return t.pursueAvoid = v, true;
  if (name == "quickMinT") return t.quickMinT = v, true;
  if (name == "softZone") return t.softZone = v, true;
  if (name == "behindHotK") return t.behindHotK = v, true;
  if (name == "runNoise") return t.runNoise = v, true;
  if (name == "fumbleK") return t.fumbleK = v, true;
  if (name == "forceInt") return t.forceInt = v, true;
  if (name == "homeA") return t.homeA = v, true;
  if (name == "handReach") return t.handReach = v, true;
  return false;
}

// =====================================================================================  PlaySim
PlaySim::PlaySim(const PlayConfig& c) : rng(c.rng), cfg(c), los(c.los), by(c.ballY) {
  static unsigned long long nextSerial = 0;
  serial = ++nextSerial;
  const double wd = cfg.worldDir;
  wind = V2{cfg.weather.windX * wd, cfg.weather.windY * wd};
  ball.mode = BallMode::Snap;
  ball.holder = nullptr;
  ball.pos = V3{los, by, 0.3};
  ball.vel = V3{0, 0, 0};
  form_ = formationById(cfg.offPlay->formation);
  readOrder = cfg.offPlay->reads;
  storage.reserve(24);
  if (cfg.kick) {
    setupReturn();
  } else if (cfg.place) {
    setupPlace();  // (Sim/PlaceKick.cpp)
  } else {
    setupOffense();
    setupDefense();
  }
  ents.reserve(off.size() + def.size());
  for (Ent* e : off) ents.push_back(e);
  for (Ent* e : def) ents.push_back(e);
  applyAbilities();
  if (cfg.place) {
    placeUser();
    return;
  }
  if (cfg.kick) {
    // the user returns the kick, or covers it as the man nearest the landing spot
    if (cfg.userSide) {
      Ent* u = returner;
      if (*cfg.userSide == Side::Def) {
        double bd = 1e9;
        for (Ent* e : def)
          if (e != kickerEnt && dist(e->pos, V2{cfg.kick->landX, cfg.kick->landY}) < bd) {
            bd = dist(e->pos, V2{cfg.kick->landX, cfg.kick->landY});
            u = e;
          }
      }
      user = u;
      if (user) user->userControlled = true;
    }
    return;
  }
  setupAssignments();
  if (cfg.userSide) pickUserEnt(*cfg.userSide);
}

// ------------------------------------------------------------------ abilities
void PlaySim::applyAbilities() {
  if (!cfg.abil.on || tune().abilities <= 0) return;
  abilOn_ = true;
  for (Ent* e : ents) {
    const Player& p = *e->p;
    if (p.abilities.empty() && p.xFactor == AbilityId::None) continue;
    AbilitySituation s;
    s.down = cfg.down ? static_cast<int>(*cfg.down) : 1;
    s.toGo = cfg.toGo.value_or(10.0);
    s.los = los;
    s.q = cfg.abil.q;
    s.clock = cfg.abil.clock;
    s.offense = e->side == Side::Off;
    s.margin = s.offense ? cfg.abil.offMargin : -cfg.abil.offMargin;
    s.kick = cfg.kick.has_value();
    e->inZone = p.xFactor != AbilityId::None && cfg.abil.inZone(p.id);
    abilityFx(p, s, e->inZone, e->fx);
    if (!e->fx.any) continue;
    addRatings(e->rr, e->fx.add);
    // the physical ones move his body too (the same formulas as the Ent's)
    const double fat = 1 - 0.1 * p.fatigue;
    e->vmax += e->fx.add.speed * 0.057 * 0.97 * fat;
    e->tau = (std::max)(0.3, e->tau - e->fx.add.accel * 0.0078 / fat);
    e->latA += e->fx.add.agility * 0.12 * fat;
  }
}

// ------------------------------------------------------------------ kick returns
void PlaySim::setupReturn() {
  const KickReturnSpec& k = *cfg.kick;
  const double W = FIELD_W;
  auto mk = [&](Player* p, Side s, const char* role, const V2& at) {
    storage.push_back(std::make_unique<Ent>(p, s, role, at));
    Ent* e = storage.back().get();
    (s == Side::Off ? off : def).push_back(e);
    return e;
  };
  // the return team: the returner deep, a front wall across the field and a second level in front of the returner
  for (size_t i = 0; i < cfg.offUnit.size() && i < 11; ++i) {
    Player* p = cfg.offUnit[i];
    if (i == 0) {
      // (a kick deep into the end zone: he waits inside the end line, not behind it)
      returner = mk(p, Side::Off, "KR", V2{(std::max)(k.landX - 4, -9.0), clamp(k.landY + 3, 3, W - 3)});
      returner->state = EntState::Idle;
      continue;
    }
    const int j = static_cast<int>(i) - 1;
    double x, y;
    bool front;
    if (!k.punt) {
      // dynamic kickoff: nine in the setup zone (the 30 to the 35) across the field, one lead blocker with the returner
      front = j < 9;
      y = front ? 4 + j * (W - 8) / 8 : clamp(k.landY - 3, 4, W - 4);
      x = front ? 33 + (j % 2) * 1.5 : k.landX + 5;
    } else {
      // punt return: six across from the punt team's line (they rush and hold, then peel back into the wall), two vise
      // men over the gunners split wide, two in front of the returner (the wall's start)
      front = j < 6;
      if (front) {
        y = W / 2 + (j - 2.5) * 1.5;
        x = k.coverX - 1.2;
      } else if (j < 8) {
        y = j == 6 ? 5.0 : W - 5.0;
        x = k.coverX - 1.4;
      } else {
        y = clamp(k.landY + (j == 8 ? -3.5 : 3.5), 4, W - 4);
        x = k.landX + 9;
      }
    }
    const char* role = front ? "KB1" : (k.punt && j < 8) ? "KV" : "KB2";
    Ent* e = mk(p, Side::Off, role, V2{x, y});
    e->state = EntState::RunBlock;
    // (a punt return's front: the two in the middle go after the punter, the four outside hold up the men across)
    e->kickHoldup = k.punt && front && j != 2 && j != 3;
  }
  // the coverage team: the kicker (stays back as the safety) and ten across the field on the kick line
  for (size_t i = 0; i < cfg.defUnit.size() && i < 11; ++i) {
    Player* p = cfg.defUnit[i];
    if (i == 0) {
      // (a kicker at the top of his run-up: at the capture's 4.6 yd/s he reaches the ball, half a yard behind it, at
      // the kick - he was 3-4 yd short of the tee when the ball left)
      kickerEnt = mk(p, Side::Def, k.punt ? "P" : "K", V2{k.kickX + (k.punt ? 0.5 : 0.5 + k.kickT * 4.6), W / 2 - (k.punt ? 0 : 0.3)});
      kickerEnt->state = EntState::Pursue;
      kickerEnt->deepRole = true;
      continue;
    }
    const int j = static_cast<int>(i) - 1;
    const double y = 4 + j * (W - 8) / 9;
    // a punt team lines up as one: the gunners split wide (the two outside lanes), seven on the line a yard and a bit
    // apart with the snapper in the middle, the personal protector 6 yd back; each still has his lane to cover
    V2 at{k.coverX + (k.punt ? 0.6 : 1.0), y};
    if (k.punt) {
      if (j == 0 || j == 9) at.y = j == 0 ? 5 : W - 5;
      else if (j == 8) at = V2{k.coverX + 6.0, W / 2 + 1.4};
      else at.y = W / 2 + (j - 4) * 1.3;
    }
    Ent* e = mk(p, Side::Def, "KC", at);
    e->state = EntState::Pursue;
    e->gap = y; // his coverage lane
    e->kickGunner = k.punt && (j == 0 || j == 9);
  }
  qb = returner;  // the play's "ball handler" for code that expects one
  ball.mode = BallMode::Dead;  // on the tee / in the punter's hands until the kick
  ball.pos = V3{k.kickX, k.punt ? W / 2 : W / 2, k.punt ? 1.0 : 0.1};
  ball.vel = V3{0, 0, 0};
}

bool PlaySim::thinkReturn(Ent* e) {
  if (!cfg.kick || carrier) return false;
  const KickReturnSpec& k = *cfg.kick;
  const V2 land{k.landX, k.landY};
  if (k.punt && e != kickerEnt && e != returner && thinkPuntUnit(e)) return true;
  if (!kicked) {
    // before the kick: only the kicker moves (his approach to the ball)
    if (e == kickerEnt && !k.punt) {
      // his approach, timed so the foot meets the ball at the kick (the speed to cover what is left in the time left)
      const V2 to = sub(V2{k.kickX + 0.5, FIELD_W / 2 - 0.3}, e->pos);
      const V2 v = scale(norm(to), (std::min)(e->vmax, len(to) / (std::max)(0.05, k.kickT - t)));
      e->want = v;
      if (t < 0.05) e->vel = v;  // (under way from the first step: no acceleration lag)
    } else if (k.punt && e->kickGunner && t >= k.kickT - 1.733) {
      // a gunner goes at the snap: down his sideline lane toward where the punt comes down
      const V2 to = sub(V2{k.landX + 3, e->pos.y + (k.landY - e->pos.y) * 0.25}, e->pos);
      e->want = scale(norm(to), e->vmax);
    } else {
      e->want = V2{0, 0};
    }
    return true;
  }
  if (ball.mode != BallMode::Air) return false;
  if (e == returner) {
    // settle under it: arrive a beat early, a short step behind the spot, moving forward into the catch (a foot: the
    // ball comes down into the basket of his arms, 30 cm ahead of his chest. At 0.6 yd it came down 57 cm ahead of him,
    // at arm's length - TAG_CATCH_STATS, every kick)
    const double left = k.hang - ball.t;
    const V2 spot{(std::max)(land.x - 0.35, -9.4), clamp(land.y, 1.0, FIELD_W - 1.0)};
    const V2 to = sub(spot, e->pos);
    e->want = scale(norm(to), (std::min)(e->vmax, len(to) / (std::max)(0.25, left - 0.3)));
    return true;
  }
  if (e->side == Side::Def) {
    if (e == kickerEnt) {
      // (a punter finishes his kick - the follow-through and the two steps out of it - before he goes anywhere)
      if (k.punt && t < k.kickT + 2.0) e->want = V2{0, 0};
      else e->want = scale(norm(sub(V2{land.x + 30, FIELD_W / 2}, e->pos)), e->vmax * 0.6);
      return true;
    }
    // kickoff: the coverage may not move until the ball is caught (dynamic kickoff); punts: block, then release
    // (the gunners go at the snap; the line holds its blocks until the ball is away)
    if (!k.punt || t < (e->kickGunner ? 0.45 : k.kickT + 0.5)) {
      e->want = V2{0, 0};
      return true;
    }
    // stay in your lane on the way down, closing toward the ball as the gap shrinks
    const double gapX = e->pos.x - land.x;
    const double tgtY = e->gap + (land.y - e->gap) * clamp(1 - gapX / 35, 0, 1) * 0.8;
    e->want = scale(norm(sub(V2{land.x + 2, tgtY}, e->pos)), e->vmax);
    return true;
  }
  // dynamic kickoff: nobody but the returner moves until the ball is down or touched; on a punt the return team
  // holds its wall (a few steps back toward the returner) until he has it
  if (!k.punt) {
    e->want = V2{0, 0};
    return true;
  }
  const V2 to = sub(V2{e->pos.x - 0.5, e->pos.y}, e->pos);
  e->want = scale(to, 1.5);
  return true;
}

bool PlaySim::thinkPuntUnit(Ent* e) {
  // a punt as it is played: everyone set until the snap; from the snap the punt team's line blocks the six rushing
  // across from it while its gunners release down the sidelines (two vise men on them), until the ball is away; then
  // the line comes off its blocks into its coverage lanes and the rushers peel back into the wall for the returner.
  // (The punt team's linemen used to run straight down the field and the return team never touched them.)
  const KickReturnSpec& k = *cfg.kick;
  const double snapT = k.kickT - 1.733;  // the pack's punt capture: snap 0.20 s, foot 1.93 s
  const double release = k.kickT + 0.3;
  if (t < snapT) {
    e->want = V2{0, 0};
    return true;
  }
  if (e->side == Side::Def) {
    if (e->kickGunner) return false;  // down the sideline at the snap (the coverage code below)
    if (t < release) {
      // the punt team's line: block the man across, a yard in front of the punter's spot at most
      e->state = EntState::PassBlock;
      if (!e->engaged) {
        Ent* best = nullptr;
        double bd = 1.8;
        for (Ent* o : off)
          if (o->role == "KB1" && o->engagedBy.size() < 2 && dist(o->pos, e->pos) < bd) {
            bd = dist(o->pos, e->pos);
            best = o;
          }
        if (best && bd < e->radius + best->radius + 0.35) engage(e, best);
        else e->want = best ? scale(norm(sub(best->pos, e->pos)), 2.0) : V2{0, 0};
      }
      return true;
    }
    if (e->state == EntState::PassBlock) {
      if (e->engaged) disengage(e);
      e->state = EntState::Pursue;
    }
    return false;  // into his coverage lane
  }
  // the return team
  if (e->role == "KB1" && e->kickHoldup) {
    // a hold-up man: on the punt team's lineman across from him from the snap, and after the kick he stays on him down
    // the field - between him and the ball, his hands on him - for the first part of the coverage, then peels back into
    // the wall. (Every man in the front turned and ran for his wall spot the moment the ball was kicked, and the punt
    // team's line ran down the field untouched.)
    if (!e->kickMan) {
      double bd = 4.0;
      for (Ent* d : def)
        if (!d->kickGunner && d != kickerEnt && dist(d->pos, e->pos) < bd) {
          bool taken = false;
          for (Ent* o : off) taken = taken || (o != e && o->kickMan == d);
          if (taken) continue;
          bd = dist(d->pos, e->pos);
          e->kickMan = d;
        }
    }
    Ent* m = e->kickMan;
    if (m && t < release) {
      // (until the ball is away the punt team's man is the one blocking: he takes him on where he stands)
      if (!e->engaged && e->engagedBy.empty()) e->want = scale(norm(sub(m->pos, e->pos)), (std::min)(e->vmax * 0.6, dist(m->pos, e->pos) * 3));
      return true;
    }
    const bool riding = m && m->state != EntState::Down && (t < k.kickT + 1.7) && (m->pos.x - k.landX) > 14;
    if (m && riding) {
      if (!e->engaged && e->engagedBy.empty() && m->engagedBy.size() < 2 && !m->engaged && dist(m->pos, e->pos) < e->radius + m->radius + 0.3 && e->stun <= 0)
        engage(e, m);
      if (!e->engaged) {
        // (on his inside hip, between him and the returner)
        const V2 toBall = norm(sub(V2{k.landX, k.landY}, m->pos));
        const V2 at = add(m->pos, scale(toBall, 0.9));
        e->want = scale(norm(sub(at, e->pos)), (std::min)(e->vmax, dist(at, e->pos) * 4 + 1.0));
      }
      return true;
    }
    if (e->engaged) disengage(e);
    // then the wall, as the others do (below)
  }
  if (e->role == "KB1") {
    if (t < release && kicked == false) {
      // go for the block: through the punt team's line at the punter (the blockers take them on; a man who slips his
      // stops two yards short of the punter's spot - whether a punt is blocked is the sim's call, made before the snap)
      if (e->engagedBy.empty()) {
        const V2 at{k.kickX - 2.0, FIELD_W / 2 + clamp(e->pos.y - FIELD_W / 2, -1.5, 1.5)};
        e->want = scale(norm(sub(at, e->pos)), (std::min)(e->vmax * 0.9, dist(at, e->pos) * 3));
      }
      return true;
    }
    if (!e->engagedBy.empty()) {
      const std::vector<Ent*> bs = e->engagedBy;
      for (Ent* b : bs) disengage(b);
    }
    // peel back: the wall forms between the coverage and the returner's side
    const double sideY = k.landY < FIELD_W / 2 ? 1.0 : -1.0;
    const int idx = e->p ? static_cast<int>(e->p->number % 6) : 0;
    const V2 spot{k.landX + 8 + (idx % 3) * 1.5, clamp(k.landY + sideY * (4 + idx * 2.0), 3, FIELD_W - 3)};
    e->want = scale(norm(sub(spot, e->pos)), (std::min)(e->vmax * 0.85, dist(spot, e->pos) * 2.5));
    return true;
  }
  if (e->role == "KV") {
    // the vise: on the gunner on his side, between him and the returner, with his hands on him
    Ent* g = nullptr;
    for (Ent* d : def)
      if (d->kickGunner && (!g || std::fabs(d->pos.y - e->pos.y) < std::fabs(g->pos.y - e->pos.y))) g = d;
    if (!g) return false;
    if (!e->engaged && dist(g->pos, e->pos) < e->radius + g->radius + 0.3 && g->engagedBy.empty()) engage(e, g);
    if (!e->engaged) {
      const V2 shade{g->pos.x - 1.0, g->pos.y + (g->pos.y < FIELD_W / 2 ? 0.6 : -0.6)};
      e->want = scale(norm(sub(shade, e->pos)), (std::min)(e->vmax, dist(shade, e->pos) * 4));
    }
    return true;
  }
  // the wall's start: set until the catch
  e->want = V2{0, 0};
  return true;
}

namespace {
// Where a man on his feet can put his hands on a ball (yards; bp the ball, e at his spot on the turf): above his waist
// within an arm and a lean of his shoulders (0.95 yd: arm and hand ~0.42 of his height, plus a lean / a step's
// reach), a ball over his head as high as he can jump for it (a foot); below his waist he bends to it, a long arm's
// length out from his feet. Past that a ball goes by him - or is caught laid out (inDive).
bool inHands(const Ent* e, const V3& bp, double armK) {
  const double h = e->p ? e->p->heightIn / 36.0 : 2.05;
  const double flat = jsHypot(e->pos.x - bp.x, e->pos.y - bp.y);
  const double sh = 0.82 * h;
  if (bp.z < 0.12 || bp.z > h + 0.75) return false;
  if (bp.z < 0.5 * h) return flat <= 0.8 * armK;
  return jsHypot(flat, (std::max)(0.0, std::fabs(bp.z - sh) - (bp.z > sh ? 0.3 : 0.0))) <= 0.95 * armK;
}
// A layout: the ball ahead of him (within 60 degrees of where he faces - the dive capture goes the way he faces), a
// body's length out at most, below his chest: the dive's hands get there.
bool inDive(const Ent* e, const V3& bp) {
  const V2 to{bp.x - e->pos.x, bp.y - e->pos.y};
  const double d = len(to);
  if (d < 0.3 || d > 1.9 || bp.z > 1.25 || bp.z < 0.1) return false;
  return (to.x * std::cos(e->face) + to.y * std::sin(e->face)) / d > 0.5;
}
// TAG_CATCH_STATS=1 (tools only): where the ball is against the man credited with it, at every contact the sim awards -
// a catch, a drop, a breakup, an interception, a batted pass, a fielded kick, a loose-ball recovery. Yards from his
// spot on the turf to the ball (flat), the ball's height, and cm from the middle of his shoulders to the ball (an arm
// with the hand is ~0.42 of his height: 75-80 cm). Printed at exit; no part in the play.
struct ContactStats {
  struct Kind {
    std::vector<double> flat, z, arm, armUn;  // (armUn: the ones not rolled ahead of the ball)
    long farLow = 0, farMid = 0, farHigh = 0;
  };
  static const int N = 9;
  Kind kinds[N];
  const bool on = std::getenv("TAG_CATCH_STATS") != nullptr;
  void add(int kind, const Ent* e, const V3& bp, bool rolled = true) {
    if (!on || !e || kind < 0 || kind >= N) return;
    const double h = e->p ? e->p->heightIn / 36.0 : 2.05;
    const double flat = jsHypot(e->pos.x - bp.x, e->pos.y - bp.y);
    kinds[kind].flat.push_back(flat);
    kinds[kind].z.push_back(bp.z);
    kinds[kind].arm.push_back(jsHypot(flat, bp.z - 0.82 * h) * 91.44);
    if (!rolled) kinds[kind].armUn.push_back(kinds[kind].arm.back());
    // (where the > 90 cm ones are: a ball below his waist he bends to, one over his head he jumps for - his shoulders
    // are not where they stand - or one at his body's height, which is the one that is truly off his hands)
    if (kinds[kind].arm.back() > 90) (bp.z < 0.5 * h ? kinds[kind].farLow : bp.z > h ? kinds[kind].farHigh : kinds[kind].farMid)++;
  }
  static double pct(std::vector<double>& v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(q * (v.size() - 1))];
  }
  ~ContactStats() {
    if (!on) return;
    static const char* const names[N] = {"catch", "dive catch", "drop", "breakup at the receiver", "int at the receiver", "breakup, defender first", "int, defender first",
                                         "kick fielded", "loose ball recovered"};
    std::printf("contact stats (ball against the man credited, at the award):\n");
    for (int i = 0; i < N; i++) {
      Kind& k = kinds[i];
      if (k.flat.empty()) continue;
      long far90 = 0, far120 = 0;
      for (double a : k.arm) {
        far90 += a > 90;
        far120 += a > 120;
      }
      std::printf("  %-26s n %6zu | flat yd p50 %.2f p90 %.2f p99 %.2f max %.2f | ball z yd p05 %.2f p50 %.2f p95 %.2f | shoulder-to-ball cm p50 %.0f p90 %.0f p99 %.0f max %.0f | > 90 cm %.1f%% > 120 cm %.1f%%\n",
                  names[i], k.flat.size(), pct(k.flat, 0.5), pct(k.flat, 0.9), pct(k.flat, 0.99), pct(k.flat, 1.0), pct(k.z, 0.05), pct(k.z, 0.5), pct(k.z, 0.95),
                  pct(k.arm, 0.5), pct(k.arm, 0.9), pct(k.arm, 0.99), pct(k.arm, 1.0), 100.0 * far90 / k.arm.size(), 100.0 * far120 / k.arm.size());
      std::printf("  %-26s   > 90 cm by the ball's height: below his waist %.1f%%, waist to head %.1f%%, over his head %.1f%%\n", "",
                  100.0 * k.farLow / k.arm.size(), 100.0 * k.farMid / k.arm.size(), 100.0 * k.farHigh / k.arm.size());
      if (!k.armUn.empty())
        std::printf("  %-26s   of them not rolled ahead of the ball: n %zu, shoulder-to-ball cm p50 %.0f p90 %.0f max %.0f\n", "", k.armUn.size(), pct(k.armUn, 0.5), pct(k.armUn, 0.9), pct(k.armUn, 1.0));
    }
  }
} gContact;
}  // namespace

void PlaySim::updateKickBall(double dt) {
  const KickReturnSpec& k = *cfg.kick;
  if (!kicked) {
    if (t < k.kickT) return;
    kicked = true;
    ball.mode = BallMode::Air;
    ball.t = 0;
    ball.thrower = kickerEnt;
    ev(k.punt ? "punt" : "kick", kickerEnt);
  }
  if (ball.mode != BallMode::Air) return;
  ball.t += dt;
  const double f = clamp(ball.t / k.hang, 0, 1);
  const double z0 = k.punt ? 1.0 : 0.1, z1 = 1.3;
  ball.pos = V3{k.kickX + (k.landX - k.kickX) * f, FIELD_W / 2 + (k.landY - FIELD_W / 2) * f,
                z0 + (z1 - z0) * f + 4 * k.apex * f * (1 - f)};
  ball.vel = V3{(k.landX - k.kickX) / k.hang, (k.landY - FIELD_W / 2) / k.hang, 4 * k.apex * (1 - 2 * f) / k.hang};
  ball.spin += dt * 30;
  if (f >= 1) {
    // (the returner is always under an inbounds kick: he goes for it from wherever he got to)
    if (returner && returner->state != EntState::Down && dist(returner->pos, V2{k.landX, k.landY}) < 3.0) {
      catchX = returner->pos.x;
      kickCatchT = t;
      catcher = returner;
      impulse(returner, scale(returner->vel, -0.4), 0.3);
      gContact.add(7, returner, ball.pos);
      setCarrier(returner);
      ev("catch", returner);
    } else {
      // nobody under it: it bounces
      ball.mode = BallMode::Loose;
      ball.t = 0;
      ball.vel = V3{ball.vel.x * 0.4, ball.vel.y * 0.4, 2.5};
    }
  }
}

bool PlaySim::thinkDownIt(Ent* e) {
  // the kick fielded in his own end zone: a returner told to down it (the game's call for a CPU team), or a user's
  // returner whose stick is left alone, stands a beat with the ball and takes a knee - the touchback. Once he starts
  // out, or steps over the goal line, the return is on.
  if (kickCatchT < 0 || returnOut || kneelT >= 0) return kneelT >= 0;
  if (e->pos.x > 0.2 || catchX > 0) {
    returnOut = true;
    return false;
  }
  const double since = t - kickCatchT;
  if (e->userControlled) {
    if (len(inputOf(e).move) > 0.25) returnMoved = true;
    if (returnMoved) return false;
  } else if (!cfg.kick->downIt) {
    return false;
  }
  e->want = V2{0, 0};
  if (since > (e->userControlled ? 0.7 : 0.45) && e->speed() < 1.6) {
    kneelT = t;
    e->vel = V2{0, 0};
    e->anim = Anim::Kneel;
    e->animT = 0;
    ev("kneel", e);
  }
  return true;
}

Ent* PlaySim::offRole(const std::string& role) const {
  for (const auto& kv : offRoles)
    if (kv.first == role) return kv.second;
  return nullptr;
}
Ent* PlaySim::defRole(const std::string& role) const {
  for (const auto& kv : defRoles)
    if (kv.first == role) return kv.second;
  return nullptr;
}

// ------------------------------------------------------------------ setup
/** NFL snap shares: quarterbacks, specialists and the offensive line play every snap; linebackers and defensive
 *  backs nearly all; receivers and tight ends rotate some; backs and the defensive line rotate a lot. */
static double rotationWeight(Pos ps) {
  switch (ps) {
    case Pos::QB: case Pos::K: case Pos::P: return 0;
    case Pos::OT: case Pos::OG: case Pos::C: return 0.1;
    case Pos::LB: case Pos::CB: case Pos::S: return 0.25;
    case Pos::WR: case Pos::TE: case Pos::FB: return 0.4;
    default: return 0.6;
  }
}

Player* PlaySim::pickFrom(Team& team, const std::vector<Pos>& posList, std::vector<int>& used, const std::vector<DSlot>& slots) {
  // the depth chart (Roster.h), where the user has set the role's slot: its order, most specific slot first. The k-th
  // man on the list plays at the k-th best OVR of the list, so the rotation spells a worn starter as it always has,
  // and the user's order is the order they come in.
  for (DSlot s : slots) {
    std::vector<Player*> list;
    for (Player* p : chartOrder(team, s))
      if (p->injuredWeeks <= 0 && !contains(used, p->id)) list.push_back(p);
    if (list.empty()) continue;
    std::vector<int> ovrs;
    for (const Player* p : list) ovrs.push_back(p->ovr);
    std::sort(ovrs.begin(), ovrs.end(), [](int a, int b) { return a > b; });
    const Pos nat = dslotInfo(s).natural;
    const double w = rotationWeight(nat);
    const bool sit = team.pullStarters && (nat == Pos::QB || nat == Pos::RB || nat == Pos::WR || nat == Pos::TE);
    size_t best = 0;
    double bestS = -1e9;
    for (size_t i = 0; i < list.size(); i++) {
      const Player* p = list[i];
      const double keep = p->onFieldLast ? 4.0 : 0.0;
      const double sc = (sit ? -static_cast<double>(ovrs[i]) : ovrs[i] * (1 - w * p->fatigue)) + keep;
      if (sc > bestS) {
        bestS = sc;
        best = i;
      }
    }
    used.push_back(list[best]->id);
    return list[best];
  }
  for (Pos ps : posList) {
    std::vector<Player*> cands;
    for (const auto& p : team.roster)
      if (p->pos == ps && p->injuredWeeks <= 0 && !contains(used, p->id)) cands.push_back(p.get());
    // (rotationWeight: the snap shares.) The man already in the game keeps his spot unless he is clearly worn down
    // (no swapping every play).
    const double w = rotationWeight(ps);
    const bool sit = team.pullStarters && (ps == Pos::QB || ps == Pos::RB || ps == Pos::WR || ps == Pos::TE);
    auto score = [w, sit](const Player* p) {
      const double keep = p->onFieldLast ? 4.0 : 0.0;  // the man in the game stays in
      return (sit ? -p->ovr : p->ovr * (1 - w * p->fatigue)) + keep;  // garbage time: the depth chart from the bottom
    };
    std::stable_sort(cands.begin(), cands.end(), [&score](const Player* a, const Player* b) { return score(b) - score(a) < 0; });
    if (!cands.empty()) {
      used.push_back(cands[0]->id);
      return cands[0];
    }
  }
  // emergency: the position group is out of healthy men. A body from the nearest group (a lineman short: a defensive
  // lineman or a tight end; anyone else: the best healthy man left), never the quarterbacks or the kickers while anyone
  // else is there (with the guards hurt the first name on the roster - the starting quarterback - played guard and the
  // backup quarterback finished the drive, SimTests "starters stay in")
  const bool lineSlot = std::find(posList.begin(), posList.end(), Pos::OG) != posList.end() || std::find(posList.begin(), posList.end(), Pos::OT) != posList.end() ||
                        std::find(posList.begin(), posList.end(), Pos::C) != posList.end();
  auto rank = [lineSlot](const Player* p) {
    if (p->pos == Pos::QB || p->pos == Pos::K || p->pos == Pos::P) return 0;
    if (lineSlot) return p->pos == Pos::DT || p->pos == Pos::DE ? 3 : p->pos == Pos::TE ? 2 : 1;
    return 1;
  };
  Player* any = nullptr;
  for (const auto& p : team.roster)
    if (!contains(used, p->id) && p->injuredWeeks <= 0 && (!any || rank(p.get()) > rank(any) || (rank(p.get()) == rank(any) && p->ovr > any->ovr))) any = p.get();
  if (!any) any = team.roster[0].get();
  used.push_back(any->id);
  return any;
}

V2 PlaySim::alignPos(const FormationSlot& slot, bool flip) const {
  const double L = los;
  const Align& a = slot.align;
  auto s = [flip](char side) { return ((side == 'L') != flip) ? 1.0 : -1.0; };
  switch (a.kind) {
    case AlignKind::OL:
      return V2{L - 0.75, by + (flip ? -a.dy : a.dy)};
    case AlignKind::Inline:
      return V2{L - (a.off ? 1.5 : 0.8), by + s(a.side) * 4.05};
    case AlignKind::Wide: {
      const double sg = s(a.side);
      const double y = sg > 0 ? FIELD_W - 9 : 9;
      return V2{L - (a.off ? 1.3 : 0.6), std::fabs(y - by) < 10 ? by + sg * 10 : y};
    }
    case AlignKind::Slot: {
      const double sg = s(a.side);
      const double wideY = sg > 0 ? FIELD_W - 9 : 9;
      const double span = (std::max)(10.0, std::fabs(wideY - by));
      // (n 2: the inside slot; 3: the outside slot of quads, between the slot and the wide man)
      const double k = a.n == 2 ? 0.3 : a.n == 3 ? 0.78 : 0.55;
      return V2{L - 1.2, by + sg * (std::max)(a.n == 2 ? 5.0 : a.n == 3 ? 9.5 : 7.0, span * k)};
    }
    case AlignKind::Back:
      return V2{L + a.x, by + (flip ? -a.dy : a.dy)};
  }
  return V2{L, by};
}

/** The order a side's roles are filled in: the formation's, except that a role whose depth chart starter is out of
 *  his position (a corner the user put at free safety) goes first, so the man plays where he was put rather than
 *  where his own position's list would take him first. No chart set: the formation's order. */
static std::vector<size_t> fillOrder(const std::vector<std::vector<DSlot>>& slots, const Team& t) {
  std::vector<size_t> order(slots.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  if (t.chart.empty()) return order;
  auto moved = [&](size_t i) {
    if (slots[i].empty()) return false;
    for (const Player* p : chartOrder(t, slots[i][0]))
      if (p->injuredWeeks <= 0) return p->pos != dslotInfo(slots[i][0]).natural;
    return false;
  };
  std::stable_partition(order.begin(), order.end(), moved);
  return order;
}

void PlaySim::setupOffense() {
  std::vector<int> used;
  const auto& fs = form_->slots;
  std::vector<std::vector<DSlot>> slots(fs.size());
  for (size_t i = 0; i < fs.size(); i++) slots[i] = roleSlots(*cfg.off, fs[i].role, -1, cfg.down.value_or(1), cfg.toGo.value_or(10), cfg.los);
  std::vector<Player*> picks(fs.size(), nullptr);
  for (size_t i : fillOrder(slots, *cfg.off)) picks[i] = pickFrom(*cfg.off, fs[i].pos, used, slots[i]);
  for (size_t i = 0; i < fs.size(); i++) {
    const FormationSlot& slot = fs[i];
    Player* p = picks[i];
    storage.push_back(std::make_unique<Ent>(p, Side::Off, slot.role, alignPos(slot, cfg.flip)));
    Ent* e = storage.back().get();
    e->eligible = !e->isOL && slot.role != "QB";
    off.push_back(e);
    offRoles.emplace_back(slot.role, e);
  }
  qb = offRole("QB");
}

void PlaySim::setupDefense() {
  std::vector<int> used;
  const std::vector<FrontSlot>& fp = FRONT_PERSONNEL(cfg.defCall->front);
  std::vector<std::vector<DSlot>> slots(fp.size());
  for (size_t i = 0; i < fp.size(); i++)
    slots[i] = roleSlots(*cfg.def, fp[i].role, static_cast<int>(cfg.defCall->front), cfg.down.value_or(1), cfg.toGo.value_or(10), cfg.los);
  std::vector<Player*> picks(fp.size(), nullptr);
  for (size_t i : fillOrder(slots, *cfg.def)) picks[i] = pickFrom(*cfg.def, fp[i].pos, used, slots[i]);
  for (size_t i = 0; i < fp.size(); i++) {
    const FrontSlot& s = fp[i];
    Player* p = picks[i];
    storage.push_back(std::make_unique<Ent>(p, Side::Def, s.role, V2{los + 5, by}));
    Ent* e = storage.back().get();
    def.push_back(e);
    defRoles.emplace_back(s.role, e);
  }
  alignDefense();
}

std::vector<Ent*> PlaySim::eligibles() const {
  std::vector<Ent*> out;
  for (Ent* e : off)
    if (e->eligible) out.push_back(e);
  return out;
}

/** Align defenders relative to offensive formation. */
void PlaySim::alignDefense() {
  const double L = los;
  const double lt = offRole("LT")->pos.y;
  const double rt = offRole("RT")->pos.y;
  const Coverage cov = cfg.defCall->coverage;
  const bool gl = cfg.defCall->front == Front::Goalline;
  std::vector<Ent*> tes;
  for (Ent* e : off)
    if (isTERole(e->role) && std::fabs(e->pos.y - by) < 5) tes.push_back(e);
  Ent* teL = nullptr;
  for (Ent* t2 : tes)
    if (t2->pos.y > by) { teL = t2; break; }
  Ent* teR = nullptr;
  for (Ent* t2 : tes)
    if (t2->pos.y < by) { teR = t2; break; }
  auto set = [this](const char* r, double x, double y) {
    Ent* e = defRole(r);
    if (e) e->pos = V2{x, clamp(y, 1, FIELD_W - 1)};
  };
  // the strength: the tight end's side, else the side with more receivers (+1 = +y)
  double strongS = 0;
  {
    int nl = 0, nr = 0;
    for (Ent* e : off)
      if (e->eligible && std::fabs(e->pos.y - by) > 2) (e->pos.y > by ? nl : nr)++;
    strongS = teL && !teR ? 1 : teR && !teL ? -1 : nl > nr ? 1 : nr > nl ? -1 : -1;
  }
  // (the down linemen line up 1.0-1.15 yd past the ball and the offensive line 0.75 behind it, 1.8 yd hip to hip: at 0.8-0.95 and 0.55 the two
  // lines' helmets were 6 cm apart in their stances - through each other - and there was no neutral zone to see)
  const FrontAlign fa = cfg.defCall->align;
  const Front fr = cfg.defCall->front;
  // three down linemen: the 3-4 and the 3-3-5 / 3-2-6 sub fronts; two: the 2-4-5
  const bool odd = fr == Front::Base34 || fr == Front::Nickel33 || fr == Front::Dime32;
  const double lbDepth = gl ? 2.5 : 4.6;
  if (fr == Front::Nickel24) {
    // 2-4-5: two down linemen in the B gaps (3 techniques), the edge backers standing up outside the tackles, two
    // inside backers stacked off the guards
    const double edgeL = (teL ? teL->pos.y : lt) + 1.6, edgeR = (teR ? teR->pos.y : rt) - 1.6;
    set("LDT", L + 1.05, by + 2.0);
    set("RDT", L + 1.05, by - 2.0);
    set("LOLB", L + 1.15, edgeL);
    set("ROLB", L + 1.15, edgeR);
    set("WLB", L + lbDepth, by + 1.6);
    set("MLB", L + lbDepth + 0.2, by - 1.6);
  } else if (odd) {
    // 3-4: ends in 4i/5 techniques on the tackles, the nose over the center; the outside backers stand on the
    // edges outside the end man, the two inside backers off the guards
    const double edgeL = (teL ? teL->pos.y : lt) + 1.6, edgeR = (teR ? teR->pos.y : rt) - 1.6;
    if (fa == FrontAlign::Bear) {
      set("LE", L + 1.05, by + 1.6);  // both guards and the center covered
      set("LDT", L + 1.00, by);
      set("RE", L + 1.05, by - 1.6);
    } else if (fa == FrontAlign::Okie) {
      set("LE", L + 1.10, lt + 1.0);  // 5 techniques: outside the tackles
      set("LDT", L + 1.00, by);
      set("RE", L + 1.10, rt - 1.0);
    } else if (fa == FrontAlign::Tite) {
      set("LE", L + 1.10, lt - 0.45);  // 4i: inside the tackles, the B gaps squeezed
      set("LDT", L + 1.00, by);
      set("RE", L + 1.10, rt + 0.45);
    } else {
      set("LE", L + 1.10, lt + 0.35);
      set("LDT", L + 1.00, by + (fa == FrontAlign::Over ? strongS * 0.7 : 0));
      set("RE", L + 1.10, rt - 0.35);
    }
    set("LOLB", L + 1.15, edgeL);
    set("ROLB", L + 1.15, edgeR);
    if (fr == Front::Nickel33) {
      // the 3-3-5: three backers stacked behind the down three (the "stack")
      set("WLB", L + lbDepth, by + 3.4);
      set("MLB", L + lbDepth + 0.3, by);
      set("SLB", L + lbDepth, by - 3.4);
    } else {
      set("WLB", L + lbDepth, by + 2.2);
      set("MLB", L + lbDepth + 0.2, by - 2.2);
    }
  } else {
    set("LE", L + 1.10, (teL ? teL->pos.y : lt) + 1.0);
    set("LDT", L + 1.05, by + 1.9);
    set("RDT", L + 1.05, by - 0.7);
    set("RE", L + 1.10, (teR ? teR->pos.y : rt) - 1.0);
    // the set's look: Over puts the 3 technique to the strength, Under away from it, Wide 9 splits the ends, Bear
    // covers both guards
    if (fa == FrontAlign::Over || fa == FrontAlign::Under) {
      const double threeTech = fa == FrontAlign::Over ? strongS : -strongS;
      set("LDT", L + 1.05, by + (threeTech > 0 ? 1.9 : 0.7));
      set("RDT", L + 1.05, by - (threeTech > 0 ? 0.7 : 1.9));
    } else if (fa == FrontAlign::Wide9) {
      set("LE", L + 1.0, (teL ? teL->pos.y : lt) + 2.6);
      set("RE", L + 1.0, (teR ? teR->pos.y : rt) - 2.6);
    } else if (fa == FrontAlign::Bear) {
      set("LDT", L + 1.05, by + 1.35);
      set("RDT", L + 1.05, by - 1.35);
    }
    set("WLB", L + lbDepth, by + 3.6);
    set("MLB", L + lbDepth + 0.3, by + 0.2);
    set("SLB", L + lbDepth, by - 3.6);
    if (fa == FrontAlign::Bear && !gl) {
      // the 46 Bear: the ends wide over the tight ends, the Sam walked up on the strong edge, the Will on the weak one
      set("LE", L + 1.10, (teL ? teL->pos.y : lt) + 0.6);
      set("RE", L + 1.10, (teR ? teR->pos.y : rt) - 0.6);
      set(strongS > 0 ? "WLB" : "SLB", L + 1.6, (strongS > 0 ? (teL ? teL->pos.y : lt) + 2.6 : (teR ? teR->pos.y : rt) - 2.6));
    }
  }
  // the A-gap mug: the two inside backers walked up over the center's shoulders (just off the line, where they can
  // still bail into their drops)
  if (cfg.defCall->mug) {
    set("MLB", L + 1.95, by - 0.9);
    set(defRole("WLB") ? "WLB" : "SLB", L + 1.95, by + 0.9);
  }
  // corners over widest receivers each side
  const std::vector<Ent*> elig = eligibles();
  std::vector<Ent*> leftR, rightR;
  for (Ent* e : elig)
    if (e->pos.y > by + 5) leftR.push_back(e);
  std::stable_sort(leftR.begin(), leftR.end(), [](const Ent* a, const Ent* b) { return b->pos.y - a->pos.y < 0; });
  for (Ent* e : elig)
    if (e->pos.y < by - 5) rightR.push_back(e);
  std::stable_sort(rightR.begin(), rightR.end(), [](const Ent* a, const Ent* b) { return a->pos.y - b->pos.y < 0; });
  const bool press = cov == Coverage::C0 || cov == Coverage::C1 || cov == Coverage::C2M;
  // squat corners in the two-deep shells (Cover 6: the cloud corner on the half side, to the right)
  const CoverTech tech = cfg.defCall->tech;
  auto cbDepth = [&](const Ent* e, bool right) {
    // (Cover 3 Cloud: the strong corner squats as a Cover 2 corner does; Cover 2 Invert: the corners are the deep
    // halves and line up off)
    if (tech == CoverTech::Cloud && (right ? -1.0 : 1.0) == strongS) return 4.5;
    if (tech == CoverTech::Invert) return 8.0;
    return (press && e) ? 1.1 : (cov == Coverage::C2 || cov == Coverage::C2T || (cov == Coverage::C6 && right)) ? 4.5 : 7.0;
  };
  Ent* lW = leftR.empty() ? nullptr : leftR[0];
  Ent* rW = rightR.empty() ? nullptr : rightR[0];
  set("LCB", L + cbDepth(lW, false), lW ? lW->pos.y - 0.7 : by + 13);
  set("RCB", L + cbDepth(rW, true), rW ? rW->pos.y + 0.7 : by - 13);
  std::vector<Ent*> slots;
  for (size_t i = 1; i < leftR.size(); i++) slots.push_back(leftR[i]);
  for (size_t i = 1; i < rightR.size(); i++) slots.push_back(rightR[i]);
  if (defRole("NB")) {
    Ent* s = slots.size() > 0 ? slots[0] : nullptr;
    set("NB", L + (press ? 1.5 : 5), s ? s->pos.y + (s->pos.y > by ? -0.8 : 0.8) : by + 8);
  }
  if (defRole("DB")) {
    Ent* s = slots.size() > 1 ? slots[1] : nullptr;
    set("DB", L + (s ? 5.5 : 10), s ? s->pos.y + (s->pos.y > by ? -0.8 : 0.8) : by - 8);
  }
  bool twoHigh = cov == Coverage::C2 || cov == Coverage::C4 || cov == Coverage::C2M || cov == Coverage::C2T || cov == Coverage::C6;
  // a disguised call shows the other shell and rotates to its own at the snap (the safeties' landmarks are the call's)
  if (cfg.defCall->rotate && cov != Coverage::C0) twoHigh = !twoHigh;
  if (cov == Coverage::C0) {
    set("FS", L + 6, by + 5);
    set("SS", L + 5, by - 5);
  } else if (twoHigh) {
    set("FS", L + (gl ? 7 : 12.5), by + 9);
    set("SS", L + (gl ? 7 : 12.5), by - 9);
  } else {
    set("FS", L + (gl ? 8 : 13.5), clamp(by, 18, 35));
    size_t nl = 0, nr = 0;
    for (Ent* e : elig) {
      if (e->pos.y > by) nl++;
      if (e->pos.y < by) nr++;
    }
    const bool strongL = nl >= nr;
    set("SS", L + (gl ? 4 : 7.5), by + (strongL ? 5.5 : -5.5));
  }
  // never let defenders line up in the neutral zone
  for (Ent* e : def) e->pos.x = (std::max)(e->pos.x, L + 0.95);
  spaceFront(false);
}

void PlaySim::setupAssignments() {
  const OffPlay& play = *cfg.offPlay;
  // ---- offense
  for (Ent* e : off) {
    if (e == qb) {
      e->state = EntState::QB;
      continue;
    }
    if (e->isOL) {
      e->state = play.type == PlayType::Run ? EntState::RunBlock : EntState::PassBlock;
      continue;
    }
    const std::string* rname = play.routeFor(e->role);
    const bool isCarrier = play.run && play.run->carrier == e->role;
    if (play.type == PlayType::Run || play.type == PlayType::Kneel || play.type == PlayType::Spike) {
      // an RPO's receiver runs his route (the quarterback may throw it); a reverse's second man waits for his exchange
      if (play.run && !play.run->rpo.empty() && play.run->rpo == e->role && rname && routeByName(*rname) && !routeByName(*rname)->block) {
        assignRoute(e, *rname);
        rpoRecv_ = e;
        continue;
      }
      if (isCarrier || (!play.reverse.empty() && play.reverse == e->role)) e->state = EntState::Idle;
      else e->state = EntState::RunBlock;
      if (!play.reverse.empty() && play.reverse == e->role) revEnt_ = e;
      continue;
    }
    if (!rname || *rname == "block") {
      e->state = EntState::PassBlock;
      e->routeName = "block";
      continue;
    }
    assignRoute(e, *rname);
    if (*rname == "screen" || *rname == "tunnel" || *rname == "slip" || (*rname == "bubble" && play.type == PlayType::Screen)) screenEnt_ = e;
  }
  if (play.motion) {
    motionEnt_ = offRole(play.motion->role);
    if (motionEnt_ && play.motion->jet) motionSide_ = jsSign(orIfZero(play.motion->dy * (cfg.flip ? -1.0 : 1.0), 1));
  }
  // ---- defense
  const DefCall& call = *cfg.defCall;
  const Coverage cov = call.coverage;
  std::vector<Ent*> rushers;  // ordered set
  // (a zone blitz or a simulated pressure drops a lineman into coverage: he is a cover man like any other)
  for (Ent* e : def)
    if (e->isDL && !contains(rushers, e) && std::find(call.drop.begin(), call.drop.end(), e->role) == call.drop.end()) rushers.push_back(e);
  for (const std::string& r : call.blitz) {
    Ent* e = defRole(r);
    if (e && !contains(rushers, e)) rushers.push_back(e);
  }
  for (Ent* e : rushers) {
    e->state = EntState::Rush;
    e->gap = e->pos.y;
  }
  std::vector<Ent*> cover;
  for (Ent* e : def)
    if (!contains(rushers, e)) cover.push_back(e);
  const bool man = cov == Coverage::C0 || cov == Coverage::C1 || cov == Coverage::C2M;
  const CoverTech tech = call.tech;
  const double strongS = strengthSide(off, by);
  const char* strongCB = strongS > 0 ? "LCB" : "RCB";
  std::vector<std::pair<const char*, const char*>> deepZones;
  switch (cov) {
    case Coverage::C1: deepZones = {{"deepM", "FS"}}; break;
    case Coverage::C2M: deepZones = {{"halfL", "FS"}, {"halfR", "SS"}}; break;
    case Coverage::C2: deepZones = {{"halfL", "FS"}, {"halfR", "SS"}}; break;
    case Coverage::C3: deepZones = {{"thirdL", "LCB"}, {"thirdM", "FS"}, {"thirdR", "RCB"}}; break;
    case Coverage::C3Fire: deepZones = {{"thirdL", "LCB"}, {"thirdM", "FS"}, {"thirdR", "RCB"}}; break;
    case Coverage::C4: deepZones = {{"q1", "LCB"}, {"q2", "FS"}, {"q3", "SS"}, {"q4", "RCB"}}; break;
    case Coverage::C2T: deepZones = {{"halfL", "FS"}, {"halfR", "SS"}}; break;
    case Coverage::C6: deepZones = {{"q1", "LCB"}, {"q2", "FS"}, {"halfR", "SS"}}; break;
    case Coverage::C0: break;
  }
  // the techniques that move a deep zone to another man: Cloud rotates the strong safety over the squatting corner,
  // Invert sends the corners to the halves
  if (tech == CoverTech::Cloud && cov == Coverage::C3)
    for (auto& zr : deepZones)
      if (std::string(zr.second) == strongCB) zr.second = "SS";
  if (tech == CoverTech::Invert && (cov == Coverage::C2 || cov == Coverage::C2T)) deepZones = {{"halfL", "LCB"}, {"halfR", "RCB"}};
  std::vector<Ent*> assigned;
  // (the shell's own men first, then the zones whose man is blitzing go to whoever rotates there)
  for (const auto& zr : deepZones) {
    Ent* e = defRole(zr.second);
    if (e && !contains(rushers, e)) {
      e->state = EntState::Zone;
      e->zone = zr.first;
      assigned.push_back(e);
    }
  }
  for (const auto& zr : deepZones) {
    Ent* e = defRole(zr.second);
    if (e && !contains(rushers, e)) continue;
    {
      // the man the shell puts there is blitzing (a corner or safety pressure): the nearest safety, else the nearest
      // other cover man, rotates to the deep zone - a fire zone never plays with a hole in the deep third
      const V2 lm = zoneLandmark(zr.first);
      Ent* best = nullptr;
      double bd = 1e9;
      for (Ent* c : cover) {
        if (contains(assigned, c) || c->isDL) continue;
        const bool safety = c->role == "FS" || c->role == "SS";
        const double dd = dist(c->pos, lm) - (safety ? 8.0 : isCorner(c) ? 3.0 : 0.0);
        if (dd < bd) {
          bd = dd;
          best = c;
        }
      }
      if (best) {
        best->state = EntState::Zone;
        best->zone = zr.first;
        assigned.push_back(best);
      }
    }
  }
  std::vector<Ent*> rest;
  for (Ent* e : cover)
    if (!contains(assigned, e)) rest.push_back(e);
  // the Spy: the Mike mirrors the quarterback (Cover 1 / Cover 0 Spy)
  if (tech == CoverTech::Spy) {
    Ent* s = defRole("MLB");
    if (s && contains(rest, s)) {
      s->state = EntState::Spy;
      removeValue(rest, s);
    }
  }
  // the Robber: the strong safety sits in the hole over the middle (Cover 1 Robber)
  if (tech == CoverTech::Robber) {
    Ent* s = defRole("SS");
    if (s && contains(rest, s)) {
      s->state = EntState::Zone;
      s->zone = "robM";
      removeValue(rest, s);
    }
  }
  if (man) {
    // man match: corners take widest, then slots by nickel/dime, then TE/backs by S/LB
    std::vector<Ent*> remaining;
    for (Ent* e : eligibles())
      if (e->state == EntState::Route || e->state == EntState::Idle || play.type == PlayType::Run || e->state == EntState::PassBlock)
        remaining.push_back(e);
    auto take = [&](Ent* d, Ent* tgt) {
      if (!tgt) return;
      d->state = EntState::Man;
      d->man = tgt;
      remaining.erase(std::find(remaining.begin(), remaining.end(), tgt));
      removeValue(rest, d);
    };
    // widest(side): stable sort by |y-by| desc, [0] == first strict maximum
    auto widest = [&](double sideSign) -> Ent* {
      Ent* best = nullptr;
      double bv = 0;
      for (Ent* e : remaining) {
        if (!(jsSign(e->pos.y - by) == sideSign && std::fabs(e->pos.y - by) > 5)) continue;
        const double v = std::fabs(e->pos.y - by);
        if (!best || v > bv) { best = e; bv = v; }
      }
      return best;
    };
    // nearest(d): stable sort by distance asc, [0] == first strict minimum
    auto nearest = [&](const Ent* d) -> Ent* {
      Ent* best = nullptr;
      double bv = 0;
      for (Ent* e : remaining) {
        const double v = dist(e->pos, d->pos);
        if (!best || v < bv) { best = e; bv = v; }
      }
      return best;
    };
    {
      Ent* d = defRole("LCB");
      if (d && contains(rest, d)) take(d, widest(1));
    }
    {
      Ent* d = defRole("RCB");
      if (d && contains(rest, d)) take(d, widest(-1));
    }
    // the bracket: the strong safety doubles the strong corner's man (over the top, the corner under), and the rest
    // man up as they would
    if (tech == CoverTech::Bracket) {
      Ent* cb = defRole(strongCB);
      Ent* s = defRole("SS");
      if (cb && s && cb->state == EntState::Man && cb->man && contains(rest, s)) {
        s->state = EntState::Man;
        s->man = cb->man;
        removeValue(rest, s);
      }
    }
    for (const char* r : {"NB", "DB", "SS", "SLB", "LOLB", "ROLB", "WLB", "MLB", "FS"}) {
      Ent* d = defRole(r);
      if (d && contains(rest, d)) take(d, nearest(d));
    }
    // leftovers: robber / spy
    for (Ent* e : rest) {
      e->state = EntState::Zone;
      e->zone = "hookM";
    }
  } else {
    // zone: under zones by coverage
    std::vector<std::string> zs;
    switch (cov) {
      case Coverage::C2: zs = {"flatL", "hookL", "hookM", "hookR", "flatR"}; break;
      case Coverage::C3: zs = {"flatL", "hookL", "hookR", "flatR"}; break;
      case Coverage::C3Fire: zs = {"flatL", "hookM", "flatR"}; break;
      case Coverage::C4: zs = {"flatL", "hookM", "flatR"}; break;
      case Coverage::C2T: zs = {"flatL", "hookL", "midM", "hookR", "flatR"}; break;
      case Coverage::C6: zs = {"flatL", "hookL", "hookM", "flatR"}; break;
      default: zs = {"hookM"}; break;
    }
    // Cover 3 Buzz: the strong safety buzzes down to the hook on his side (the outside backer has the flat)
    if (tech == CoverTech::Buzz && cov == Coverage::C3) {
      Ent* s = defRole("SS");
      const std::string hz = strongS > 0 ? "hookL" : "hookR";
      auto it = std::find(zs.begin(), zs.end(), hz);
      if (s && contains(rest, s) && it != zs.end()) {
        s->state = EntState::Zone;
        s->zone = hz;
        removeValue(rest, s);
        zs.erase(it);
      }
    }
    // Tampa 2: the Mike opens and runs the middle hole between the safeties
    if (cov == Coverage::C2T) {
      Ent* m = defRole("MLB");
      if (m && contains(rest, m)) {
        m->state = EntState::Zone;
        m->zone = "midM";
        removeValue(rest, m);
        zs.erase(std::find(zs.begin(), zs.end(), std::string("midM")));
      }
    }
    // CBs in cover 2 take flats regardless of order (cover 6: the cloud corner on the half side)
    if (cov == Coverage::C2 || cov == Coverage::C2T || cov == Coverage::C6) {
      const std::pair<const char*, const char*> cbFlats[] = {{"LCB", "flatL"}, {"RCB", "flatR"}};
      for (const auto& rz : cbFlats) {
        if (cov == Coverage::C6 && std::string(rz.first) == "LCB") continue;  // he has the deep quarter
        Ent* e = defRole(rz.first);
        if (e && contains(rest, e)) {
          e->state = EntState::Zone;
          e->zone = rz.second;
          removeValue(rest, e);
          auto it = std::find(zs.begin(), zs.end(), std::string(rz.second));
          if (it != zs.end()) zs.erase(it);
          else if (!zs.empty()) zs.pop_back();  // JS splice(-1, 1)
        }
      }
    }
    // match remaining by lateral position: sort defenders and zones by y (L = +y)
    std::vector<Ent*> ds = rest;
    std::stable_sort(ds.begin(), ds.end(), [](const Ent* a, const Ent* b) { return b->pos.y - a->pos.y < 0; });
    std::stable_sort(zs.begin(), zs.end(), [this](const std::string& a, const std::string& b) {
      return zoneLandmark(b).y - zoneLandmark(a).y < 0;
    });
    // if more defenders than zones, extras become hook/robber
    for (size_t i = 0; i < ds.size(); i++) {
      Ent* d = ds[i];
      d->state = EntState::Zone;
      const double denom = (std::max)(1.0, static_cast<double>(ds.size()) - 1);
      const double idx = jsRound((static_cast<double>(i) * (static_cast<double>(zs.size()) - 1)) / denom);
      d->zone = (idx >= 0 && idx < static_cast<double>(zs.size())) ? zs[static_cast<size_t>(idx)] : std::string("hookM");
    }
  }
  for (Ent* e : def) {
    if (!e->zone.empty() && isDeepZone(e->zone)) e->deepRole = true;
    if (!e->zone.empty()) e->zoneSpot = zoneLandmark(e->zone);
    // prevent: everyone plays off and deep - nothing over the top, the underneath given up
    if (tech == CoverTech::Prevent && e->zoneSpot) e->zoneSpot->x = (std::min)(e->zoneSpot->x + (e->deepRole ? 6.0 : 4.0), DEEP_AIM - 0.5);
    e->reactT = 0.12 + (99 - (std::max)(e->r().playRec, e->r().awareness)) * 0.006;
    e->getOff = 0.12 + rng.range(0, 0.12) + (99 - e->r().awareness) * 0.002;
  }
  assignBlocks();
  // press at the line
  for (Ent* e : def) {
    if (e->state == EntState::Man && e->man && dist(e->pos, e->man->pos) < 2.2) {
      const double win = sigmoid((e->r().press - e->man->r().release) / 10 - 0.3);
      if (rng.chance(win)) {
        const double jam = rng.range(0.25, 0.55);
        e->man->pressDelay = (std::max)(e->man->pressDelay, jam);
        pressRoll_[e->man] = (std::max)(pressRoll_[e->man], jam);
      }
    }
  }
  // run-fit gaps for linebackers
  for (Ent* e : def)
    if ((e->role == "WLB" || e->role == "MLB" || e->role == "SLB" || e->role == "LOLB" || e->role == "ROLB") && e->state != EntState::Rush)
      e->gap = e->pos.y;
  numberReceivers();
  setupFits();
  if (tune().stunts > 0) setupStunt();  // (rolled on the play's rng only with stunts on)
}

/** Snap-time blocking assignments: unique blocker->defender pairs, closest first. */
void PlaySim::assignBlocks() {
  const OffPlay& play = *cfg.offPlay;
  const bool isRun = play.type == PlayType::Run;
  std::vector<Ent*> blockers;
  for (Ent* e : off)
    if (e->state == (isRun ? EntState::RunBlock : EntState::PassBlock) &&
        (e->isOL || isTERole(e->role) || isBackRole(e->role)))
      blockers.push_back(e);
  const double aimDy = runAimDy();
  const double playDir = jsSign(orIfZero(aimDy, 0.001));
  const bool zone = play.run && (play.run->style == RunStyle::Zone || play.run->style == RunStyle::Stretch);
  std::vector<Ent*> targets;
  for (Ent* d : def) {
    if (isRun ? (d->pos.x < los + 6 && std::fabs(d->pos.y - by) < 8) : d->state == EntState::Rush) targets.push_back(d);
  }
  struct Pair {
    Ent* b;
    Ent* d;
    double c;
  };
  std::vector<Pair> pairs;
  // gap blocking (power, counter, trap): the play side blocks down on the man in its inside gap, the back side
  // hinges toward the play to close the hole a puller leaves; zone blocking steps every man a gap to the play side
  const bool downBlock = isRun && play.run && play.run->down && tune().runJobs > 0 && tune().downBlock > 0;
  for (Ent* b : blockers) {
    const bool backfield = isBackRole(b->role);
    for (Ent* d : targets) {
      // a protection slide: every lineman takes the man one gap over toward the slide
      const double sl = !isRun && b->isOL ? slide * 1.35 : 0;
      double shift = zone && !backfield ? playDir * 1.3 : 0;
      if (downBlock && !backfield) shift = (b->pos.y - by) * playDir > -0.3 ? -playDir * 1.3 : playDir * 1.3;
      const V2 pt{b->pos.x, b->pos.y + shift + sl};
      double c = std::fabs(d->pos.y - pt.y) + std::fabs(d->pos.x - pt.x) * 0.5;
      if (isRun && d->pos.x > los + 3) c += 1.5;  // linebackers are second-level priority
      if (backfield) c += isRun ? 1.2 : 3;         // backs pick up leftovers
      pairs.push_back(Pair{b, d, c});
    }
  }
  std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.c - b.c < 0; });
  std::vector<Ent*> usedB, usedD;
  // the run's special blocks first (pullers, lead and kick-out men take their men; a trapped or read man is left alone)
  jobs_.clear();
  if (isRun && play.run && ((!play.run->jobs.empty() && tune().runJobs > 0) || play.run->read == "end")) resolveRunJobs(usedB, usedD);
  // pass protection, the line first and in order across it (big on big): the rushers on the line matched to the five
  // linemen without crossing - the outside men to the tackles, the extra lineman whoever is left in the middle (the
  // nearest-first pairing gave the center an end five yards outside him: a third of all pressures were an end nobody
  // could reach, with the numbers to block him)
  if (!isRun && tune().lineOrder > 0 && slide == 0) {  // (a slide the user called: the line slides, each man a gap over, as the nearest-first pairing below does it)
    std::vector<Ent*> ol, rl;
    for (Ent* b : blockers) if (b->isOL) ol.push_back(b);
    for (Ent* d : targets) if (d->pos.x < los + 1.6) rl.push_back(d);
    std::sort(ol.begin(), ol.end(), [](const Ent* a, const Ent* b) { return a->pos.y < b->pos.y; });
    std::sort(rl.begin(), rl.end(), [](const Ent* a, const Ent* b) { return a->pos.y < b->pos.y; });
    const size_t n = ol.size(), m = rl.size();
    if (n && m) {
      const double sl = slide * 1.35;
      auto cost = [&](size_t i, size_t j) { return std::fabs(rl[j]->pos.y - (ol[i]->pos.y + sl)); };
      // dp over (blockers used, rushers used): skip a blocker or skip a rusher (only the side with more), or pair them
      const double INF = 1e18;
      std::vector<std::vector<double>> dp(n + 1, std::vector<double>(m + 1, INF));
      std::vector<std::vector<int>> how(n + 1, std::vector<int>(m + 1, 0));
      dp[0][0] = 0;
      for (size_t i = 0; i <= n; ++i)
        for (size_t j = 0; j <= m; ++j) {
          if (dp[i][j] >= INF) continue;
          // a blocker spare: with a slide, the man on the side away from it (the line slid on, he is left over)
          const double spare = slide != 0 && i < n ? (jsSign(ol[i]->pos.y - by) == -slide ? -0.5 : 0.8) : 0.0;
          if (i < n && n > m && dp[i + 1][j] > dp[i][j] + spare) { dp[i + 1][j] = dp[i][j] + spare; how[i + 1][j] = 1; }
          // a rusher left over (to a back): with a slide, the man on the side away from it (the line slid to the other)
          const double skip = 3.0 + (slide != 0 && j < m ? (jsSign(rl[j]->pos.y - by) == slide ? 2.5 : -1.0) : 0.0);
          if (j < m && m > n && dp[i][j + 1] > dp[i][j] + skip) { dp[i][j + 1] = dp[i][j] + skip; how[i][j + 1] = 2; }
          if (i < n && j < m && dp[i + 1][j + 1] > dp[i][j] + cost(i, j)) { dp[i + 1][j + 1] = dp[i][j] + cost(i, j); how[i + 1][j + 1] = 3; }
        }
      size_t i = n, j = m;
      while (i > 0 || j > 0) {
        const int h = how[i][j];
        if (h == 3) {
          ol[i - 1]->blockTarget = rl[j - 1];
          usedB.push_back(ol[i - 1]);
          usedD.push_back(rl[j - 1]);
          --i;
          --j;
        } else if (h == 1) --i;
        else if (h == 2) --j;
        else break;
      }
    }
  }
  // pass 1: every down lineman gets a (line) blocker; pass 2: leftovers climb to linebackers
  for (int pass = 1; pass <= 2; pass++) {
    for (const Pair& pr : pairs) {
      if (contains(usedB, pr.b) || contains(usedD, pr.d) || pr.c > 8) continue;
      const bool onLine = pr.d->pos.x < los + 1.6;
      const bool lineBlocker = pr.b->isOL || isTERole(pr.b->role);
      if (pass == 1 && !(onLine && lineBlocker)) continue;
      pr.b->blockTarget = pr.d;
      usedB.push_back(pr.b);
      usedD.push_back(pr.d);
    }
  }
  // a run's perimeter: each receiver has the man over him (his own cover man, else the nearest defender nobody has) -
  // the stalk block. They had no man at all and took whoever came near.
  // (and a screen's stalk blockers: the receivers on "stalk", RunBlock on a pass play)
  if ((isRun && tune().smartBlock > 0) || (!isRun && !play.run)) {
    for (Ent* b : off) {
      if (b->state != EntState::RunBlock || b->blockTarget || contains(blockers, b)) continue;
      Ent* best = nullptr;
      double bd = 11;
      for (Ent* d : def) {
        if (contains(usedD, d) || d->pos.x < los || (!isRun && d->state == EntState::Rush)) continue;
        const double dd = dist(d->pos, b->pos) - (d->state == EntState::Man && d->man == b ? 4.0 : 0.0);
        if (dd < bd) {
          bd = dd;
          best = d;
        }
      }
      if (best) {
        b->blockTarget = best;
        usedD.push_back(best);
      }
    }
  }
  setupCombos();
  for (Ent* b : off) b->snapTarget = b->blockTarget;
}

void PlaySim::setupCombos() {
  for (Ent* e : off) {
    e->combo = -1;
    e->climbFor = nullptr;
  }
  combos_.clear();
  const OffPlay& play = *cfg.offPlay;
  if (tune().comboBlock <= 0 || cfg.kick || cfg.place || play.type != PlayType::Run || !play.run || play.run->style == RunStyle::Sneak) return;
  const double playDir = jsSign(orIfZero(runAimDy(), 0.001));
  const bool zone = play.run->style == RunStyle::Zone || play.run->style == RunStyle::Stretch;
  if (!zone && tune().comboBlock < 2) return;  // (1: the zone runs' combos; 2: gap runs and draws double too)
  std::vector<Ent*> ol;
  for (Ent* b : off)
    if (b->isOL && b->state == EntState::RunBlock && !jobs_.count(b)) ol.push_back(b);
  std::sort(ol.begin(), ol.end(), [](const Ent* a, const Ent* b) { return a->pos.y < b->pos.y; });
  auto onLine = [&](const Ent* d) { return d && d->side == Side::Def && d->pos.x < los + 1.6; };
  for (Ent* u : ol) {
    // uncovered: the line's rules gave him a backer (or nobody), not a man on the line
    if (u->combo >= 0 || onLine(u->blockTarget)) continue;
    Ent* mate = nullptr;
    double best = 2.2;  // (the down lineman within this of him, across: the man in the gap beside him or on his neighbour's near shoulder)
    for (Ent* c : ol) {
      if (c == u || c->combo >= 0 || std::fabs(c->pos.y - u->pos.y) > 1.9 || !onLine(c->blockTarget)) continue;
      bool taken = false;
      for (const Combo& k : combos_) taken = taken || k.dl == c->blockTarget;
      if (taken) continue;
      // (a zone run doubles toward the play side first: the uncovered man steps play side with his neighbour)
      const double s = std::fabs(c->blockTarget->pos.y - u->pos.y) + (zone && (c->pos.y - u->pos.y) * playDir < 0 ? 0.6 : 0.0);
      if (s < best) {
        best = s;
        mate = c;
      }
    }
    if (!mate) continue;
    Combo k;
    k.a = mate;
    k.b = u;
    k.dl = mate->blockTarget;
    k.lb = u->blockTarget;
    k.uncovered = u;
    u->blockTarget = k.dl;
    mate->combo = u->combo = static_cast<int>(combos_.size());
    combos_.push_back(k);
  }
}

bool PlaySim::thinkCombo(Ent* e) {
  if (e->combo < 0 || e->combo >= static_cast<int>(combos_.size())) return false;
  Combo& k = combos_[static_cast<size_t>(e->combo)];
  auto over = [&] {
    k.done = true;
    k.a->combo = k.b->combo = -1;
  };
  if (k.done) {
    e->combo = -1;
    return false;
  }
  Ent* dl = k.dl;
  Ent* other = k.a == e ? k.b : k.a;
  // the down lineman got off it (shed, past one of them), or is on the turf: each man plays on by the line's rules
  if (dl->state == EntState::Down || dl->pancaked || contains(e->beatBy, dl) || contains(other->beatBy, dl) ||
      (e->engaged && e->engaged != dl) || (other->engaged && other->engaged != dl)) {
    over();
    return false;
  }
  const bool eOn = e->engaged == dl, oOn = other->engaged == dl;
  if (eOn && oOn && k.formT < 0) {
    k.formT = t;
    k.dlAt = dl->pos;
    comboDbl_++;
  }
  // the backer: his, or once he is out of it (blocked, on the turf) the free one nearest the double that nobody has
  Ent* lb = k.lb;
  if (lb && (!lb->free() || lb->pos.x < los - 0.5)) lb = nullptr;
  if (!lb) {
    double bd = 7.0;
    for (Ent* d : def) {
      if (!d->free() || d->pos.x < los + 1.0 || d->isDL || d->deepRole) continue;
      bool had = false;
      for (const Ent* o : off) had = had || (o != e && o != other && o->blockTarget == d && o->state == EntState::RunBlock);
      if (had) continue;
      const double dd = dist(d->pos, dl->pos);
      if (dd < bd) {
        bd = dd;
        lb = d;
      }
    }
  }
  Ent* c = carrier && carrier->side == Side::Off ? carrier : nullptr;
  bool go = false;
  if (k.formT >= 0) {
    // (the backer declares: he comes downhill, or near the double, or into the line)
    const bool commits = lb && (dist(lb->pos, dl->pos) < tune().comboNear || lb->pos.x < los + tune().comboFill || (tune().comboDeclare > 0 && lb->vel.x < -tune().comboDeclare));
    const double held = t - k.formT;
    if (tune().comboHold > 0) {
      // a real double: the lineman is moved (or beaten in the rep) before one comes off him, unless the backer forces it
      const bool moved = dist(dl->pos, k.dlAt) > tune().comboDrive || dl->linePush < -tune().comboPush;
      go = held > tune().comboMax || (held > tune().comboHold && (moved || commits || (c && c->pos.x > los - 1.0)));
    } else {
      go = held > tune().comboT || (c && c->pos.x > los - 1.0) || commits;
    }
  } else if (t > tune().comboLate) {
    go = true;  // (the double never formed: the helper goes on up)
  }
  if (go) {
    // the one with a way to the backer comes off - the down lineman not in his road (the man square on him would have to
    // go through him), the nearer of the two on a tie; without a backer the uncovered man; a man not on the double is the one
    Ent* up = !eOn ? e : !oOn ? other : nullptr;
    if (!up && lb) {
      auto clear = [&](const Ent* m) {
        const V2 to = sub(lb->pos, m->pos);
        const double L = len(to);
        if (L < 0.1) return 9.0;
        const V2 rel = sub(dl->pos, m->pos);
        const double along = dot(rel, to) / L;
        return along <= 0 ? 9.0 : std::fabs(rel.x * to.y - rel.y * to.x) / L - L * 0.05;  // (his offset from the man's road)
      };
      const double ca = clear(k.a), cb = clear(k.b);
      up = std::fabs(ca - cb) > 0.25 ? (ca > cb ? k.a : k.b) : (std::fabs(k.a->pos.y - lb->pos.y) <= std::fabs(k.b->pos.y - lb->pos.y) ? k.a : k.b);
    }
    if (!up) up = k.uncovered;
    if (up->engaged == dl) disengage(up);
    up->blockTarget = lb;
    up->climbFor = lb;
    if (k.formT >= 0) comboClimb_++, comboHoldT_ += t - k.formT;
    over();
    return false;
  }
  if (!eOn) {
    // on to the double: the lineman's near hip, beside his partner - level with the lineman on the helper's side of him
    // (aimed at his middle, the helper ran into his partner's back), a hand on him from a step away
    // (his partner is square in front of the man: the helper's spot is a body's width to the side of him, not behind him)
    const double side = orIfZero(jsSign(e->pos.y - dl->pos.y), 1);
    const V2 contact{dl->pos.x - 0.25, dl->pos.y + side * (dl->radius + e->radius + 0.1)};
    e->want = scale(norm(sub(contact, e->pos)), (std::min)(e->vmax, dist(contact, e->pos) * 6 + 1.0));
    if (dist(dl->pos, e->pos) < e->radius + dl->radius + 0.55) engage(e, dl);
  }
  return true;
}

// ------------------------------------------------------------------ pre-snap adjustments
namespace {
/** A back's check release / a screen's slow release (the route's own delay before he goes). */
double routeDelay(const Ent* e) {
  if (e->role == "RB" && (e->routeName == "checkdown" || e->routeName == "flat" || e->routeName == "release" || e->routeName == "releaseflat")) return 0.9;
  if (e->routeName == "screen") return 1.1;
  if (e->routeName == "slip") return 0.9;  // (a tight end or back shows his block, then slips out behind the rush)
  return 0;
}
}  // namespace

double PlaySim::runAimDy() const {
  const OffPlay& play = *cfg.offPlay;
  if (!play.run) return 0;
  // (an outside zone / stretch aims at the end man's outside hip: Tune::stretchAim x the play's drawn point)
  const double k = play.run->style == RunStyle::Stretch ? tune().stretchAim : 1.0;
  return play.run->aimDy * k * (cfg.flip ? -1.0 : 1.0) * (runFlip ? -1.0 : 1.0);
}

bool PlaySim::flipRun() {
  if (!preSnap() || !cfg.offPlay->run) return false;
  if (cfg.offPlay->run->jet || !cfg.offPlay->reverse.empty()) return false;  // (the motion decides the side)
  runFlip = !runFlip;
  // a back offset beside the quarterback (the gun, an offset pistol) crosses behind him to the other side, so the
  // exchange is the mirror of the one called; a man covering him goes across with him. Everyone else stays put.
  Ent* c = offRole(cfg.offPlay->run->carrier);
  if (c && c != qb && !c->isOL) {
    const V2 at = c->shiftTo ? *c->shiftTo : c->pos;
    const double dy = at.y - by;
    if (std::fabs(dy) > 0.6 && std::fabs(dy) < 4.0 && at.x < los - 2.5) {
      moveTo(c, V2{at.x, by - dy});
      for (Ent* d : def) {
        if (d->state != EntState::Man || d->man != c) continue;
        const V2 dp = d->shiftTo ? *d->shiftTo : d->pos;
        moveTo(d, V2{dp.x, clamp(dp.y - 2 * dy, 1, FIELD_W - 1)});
      }
    }
  }
  adjusted_ = true;
  return true;
}

void PlaySim::spaceFront(bool targets) {
  // The men on the line of scrimmage each in his own gap: the down linemen at least 2.2 yd apart centre to centre (a
  // body, its pads and an arm: at 1.8 two linemen in their stances stood shoulder against shoulder), spread from the
  // ball outward so the inside techniques keep their spots and the ends widen; then anyone else up on the line (a 3-4's
  // outside backers, a linebacker walked up to show blitz) takes the nearest spot 2 yd clear of every man already there.
  const double gap = tune().frontGap, gap2 = gap - 0.2;
  if (gap <= 0) return;
  struct M {
    Ent* e;
    V2 at;
    double y;
  };
  std::vector<M> dl, rest;
  for (Ent* e : def) {
    if (isCorner(e) || e->role == "FS" || e->role == "SS") continue;
    const V2 at = targets && e->shiftTo ? *e->shiftTo : e->pos;
    if (at.x > los + 1.9 || std::fabs(at.y - by) > 14) continue;
    (e->isDL ? dl : rest).push_back(M{e, at, at.y});
  }
  if (dl.size() + rest.size() < 2) return;
  std::sort(dl.begin(), dl.end(), [](const M& a, const M& b) { return a.y < b.y; });
  size_t mid = 0;
  while (mid < dl.size() && dl[mid].y < by) ++mid;  // [0, mid) on the offense's right, [mid, n) on its left
  if (mid > 0 && mid < dl.size() && dl[mid].y - dl[mid - 1].y < gap) {
    const double c = (dl[mid].y + dl[mid - 1].y) / 2;
    dl[mid].y = c + gap / 2;
    dl[mid - 1].y = c - gap / 2;
  }
  for (size_t i = mid + 1; i < dl.size(); ++i) dl[i].y = (std::max)(dl[i].y, dl[i - 1].y + gap);
  for (size_t i = mid; i-- > 1;) dl[i - 1].y = (std::min)(dl[i - 1].y, dl[i].y - gap);
  if (mid == 0)
    for (size_t i = 1; i < dl.size(); ++i) dl[i].y = (std::max)(dl[i].y, dl[i - 1].y + gap);
  std::vector<double> placed;
  for (const M& m : dl) placed.push_back(m.y);
  std::sort(rest.begin(), rest.end(), [this](const M& a, const M& b) { return std::fabs(a.y - by) < std::fabs(b.y - by); });
  for (M& m : rest) {
    // (the user's own man stands where he was walked to)
    if (!(m.e == user && m.e->userControlled)) {
      auto clear = [&](double y) {
        for (double p : placed)
          if (std::fabs(p - y) < gap2 - 1e-6) return false;
        return true;
      };
      if (!clear(m.y)) {
        double best = m.y, bd = 1e9;
        for (double p : placed)
          for (double c : {p - gap2, p + gap2})
            if (clear(c) && std::fabs(c - m.at.y) < bd) {
              bd = std::fabs(c - m.at.y);
              best = c;
            }
        m.y = best;
      }
    }
    placed.push_back(m.y);
  }
  auto apply = [&](const M& m) {
    const double y = clamp(m.y, 1, FIELD_W - 1);
    if (std::fabs(y - m.at.y) < 0.02) return;
    if (targets) moveTo(m.e, V2{m.at.x, y});
    else m.e->pos.y = y;
  };
  for (const M& m : dl) apply(m);
  for (const M& m : rest) apply(m);
}

void PlaySim::assignRoute(Ent* e, const std::string& rname) {
  e->routeName = rname;
  e->route.clear();
  e->routeIdx = 0;
  if (rname == "block") {
    e->state = EntState::PassBlock;
    e->settle = false;
    return;
  }
  if (rname == "stalk") {
    // a receiver's stalk block on a screen to his side: he blocks the man over him as on a run (assignBlocks gives
    // him that man)
    e->state = EntState::RunBlock;
    e->settle = false;
    return;
  }
  const RouteDef& rd = *routeByName(rname);
  e->state = EntState::Route;
  e->settle = rd.settle;
  const V2 at = e->shiftTo ? *e->shiftTo : e->pos;  // a man still walking to his spot runs it from there
  double side = jsSign(at.y - by);
  if (std::fabs(at.y - by) < 0.6) side = e->role == "RB" ? (cfg.flip ? -1 : 1) : 1;
  // (a man in jet motion runs his route on across, the way he is going: his "outside" is the far side)
  if (e == motionEnt_ && motionSide_ != 0) side = motionSide_;
  e->outSign = side;
  for (const auto& pt : rd.pts)
    e->route.push_back(V2{(std::min)(at.x + pt.first, DEEP_AIM), clamp(at.y + pt.second * side, 1.5, FIELD_W - 1.5)});
  // check-release backs wait in protection briefly
  const double d = routeDelay(e);
  if (d > 0) e->pressDelay = d;
}

bool PlaySim::deepZone(const std::string& z) { return isDeepZone(z); }

bool PlaySim::passPlay() const {
  const PlayType ty = cfg.offPlay->type;
  return ty == PlayType::Pass || ty == PlayType::PlayAction || ty == PlayType::Screen;
}

void PlaySim::moveTo(Ent* e, const V2& to) {
  if (!basePos_.count(e)) basePos_[e] = e->shiftTo ? *e->shiftTo : e->pos;
  const V2 at{to.x, clamp(to.y, 1, FIELD_W - 1)};
  if (dist(at, e->pos) < 0.02) {
    e->pos = at;
    e->shiftTo.reset();
  } else {
    e->shiftTo = at;
  }
  adjusted_ = true;
}

void PlaySim::walkFrom(Ent* e, const V2& from) {
  if (!preSnap() || dist(from, e->pos) < 0.05) return;
  if (!basePos_.count(e)) basePos_[e] = e->pos;
  e->shiftTo = e->pos;
  e->pos = from;
  adjusted_ = true;
}

bool PlaySim::hotRoute(Ent* e, const std::string& route) {
  if (!preSnap() || !e || e->side != Side::Off || !e->eligible || !passPlay()) return false;
  if (route != "block" && !routeByName(route)) return false;
  e->pressDelay = 0;
  assignRoute(e, route);
  if (route == "block") removeValue(readOrder, e->role);
  else if (!contains(readOrder, e->role)) readOrder.push_back(e->role);
  maxProtFrom_.erase(e);
  adjusted_ = true;
  return true;
}

void PlaySim::setProtection(int slideDir, bool maxProt) {
  if (!preSnap()) return;
  slide = slideDir;
  if (maxProt != maxProtect && passPlay()) {
    for (Ent* e : off) {
      const bool back = isBackRole(e->role) || isTERole(e->role);
      if (!back || std::fabs(e->pos.y - by) > 6) continue;  // only men in the box stay in
      if (maxProt && e->state == EntState::Route && e->routeName != "screen") {
        const std::string was = e->routeName;
        hotRoute(e, "block");
        maxProtFrom_[e] = was;
      } else if (!maxProt) {
        auto it = maxProtFrom_.find(e);
        if (it != maxProtFrom_.end()) {
          const std::string was = it->second;
          hotRoute(e, was);
        }
      }
    }
  }
  maxProtect = maxProt;
  adjusted_ = true;
}

void PlaySim::adjustLine(LineAdj a) {
  if (!preSnap()) return;
  dlAdj = a;
  for (Ent* e : def) {
    if (!e->isDL) continue;
    if (!basePos_.count(e)) basePos_[e] = e->shiftTo ? *e->shiftTo : e->pos;
    const V2 b = basePos_[e];
    const double out = orIfZero(jsSign(b.y - by), 1);  // his outside
    const bool end = e->role == "LE" || e->role == "RE";
    V2 to = b;
    switch (a) {
      case LineAdj::Base: break;
      case LineAdj::ShiftL: to.y += 1.35; break;  // one gap over
      case LineAdj::ShiftR: to.y -= 1.35; break;
      case LineAdj::Pinch: to.y = end ? b.y - out * 1.4 : by + out * 0.7; break;  // ends tighten, tackles in the A gaps
      case LineAdj::Spread: to.y = b.y + out * (end ? 2.2 : 0.9); break;          // wide-nine ends
    }
    moveTo(e, to);
  }
  spaceFront(true);
}

void PlaySim::adjustLBs(LineAdj a) {
  if (!preSnap()) return;
  lbAdj = a;
  for (Ent* e : def) {
    if (!isLB(e)) continue;
    if (!basePos_.count(e)) basePos_[e] = e->shiftTo ? *e->shiftTo : e->pos;
    const V2 b = basePos_[e];
    const double out = orIfZero(jsSign(b.y - by), 1);
    V2 to = b;
    switch (a) {
      case LineAdj::Base: break;
      case LineAdj::ShiftL: to.y += 2.0; break;
      case LineAdj::ShiftR: to.y -= 2.0; break;
      case LineAdj::Pinch: to.y = by + (b.y - by) * 0.35; break;
      case LineAdj::Spread: to.y = b.y + (e->role == "MLB" ? 0 : out * 1.8); break;
    }
    if (shellAdj == ShellAdj::ShowBlitz) to.x = los + 1.6;  // still walked up to the line
    moveTo(e, to);
  }
  spaceFront(true);
}

void PlaySim::adjustShell(ShellAdj a) {
  if (!preSnap()) return;
  shellAdj = a;
  auto base = [this](Ent* e) {
    if (!basePos_.count(e)) basePos_[e] = e->shiftTo ? *e->shiftTo : e->pos;
    return basePos_[e];
  };
  for (Ent* e : def) {
    if (isCorner(e)) {
      V2 to = base(e);
      if (a == ShellAdj::Press) to.x = los + 1.1;
      else if (a == ShellAdj::Off) to.x = los + 7.5;
      moveTo(e, to);
    } else if (isLB(e)) {
      V2 to{base(e).x, e->shiftTo ? e->shiftTo->y : e->pos.y};  // keep any linebacker shift
      if (a == ShellAdj::ShowBlitz) to.x = los + 1.6;
      moveTo(e, to);
    }
  }
  Ent* fs = defRole("FS");
  Ent* ss = defRole("SS");
  if (fs && ss) {
    const V2 bf = base(fs), bs = base(ss);
    V2 tf = bf, ts = bs;
    const bool twoHigh = bf.x > los + 10 && bs.x > los + 10;
    if (a == ShellAdj::ShowBlitz) {
      ts = V2{los + 4.5, bs.y};  // the strong safety walks down into the box
    } else if (a == ShellAdj::Disguise) {
      // show the opposite shell: two high rotates to one high at the snap, and one high splits to two
      if (twoHigh) {
        tf = V2{los + 13.5, clamp(by, 18, 35)};
        ts = V2{los + 7.5, by + (bs.y > by ? 5.5 : -5.5)};
      } else {
        tf = V2{los + 12.5, by + 9};
        ts = V2{los + 12.5, by - 9};
      }
    }
    moveTo(fs, tf);
    moveTo(ss, ts);
  }
  spaceFront(true);
}

bool PlaySim::assignDefender(Ent* d, DefAssign a) {
  if (!preSnap() || !d || d->side != Side::Def) return false;
  const V2 at = d->shiftTo ? *d->shiftTo : d->pos;
  const double dy = at.y - by;
  d->man = nullptr;
  d->zone.clear();
  d->zoneSpot.reset();
  d->deepRole = false;
  std::string z;
  switch (a) {
    case DefAssign::Blitz:
      d->state = EntState::Rush;
      d->gap = at.y;
      break;
    case DefAssign::Spy: d->state = EntState::Spy; break;
    case DefAssign::Hook: z = std::fabs(dy) < 3 ? "hookM" : dy > 0 ? "hookL" : "hookR"; break;
    case DefAssign::Flat: z = dy >= 0 ? "flatL" : "flatR"; break;
    case DefAssign::Deep: z = std::fabs(dy) < 8 ? "deepM" : dy > 0 ? "thirdL" : "thirdR"; break;
    case DefAssign::Man: {
      // the nearest receiver nobody else has
      Ent* best = nullptr;
      double bd = 1e9;
      for (Ent* r : eligibles()) {
        bool taken = false;
        for (Ent* o : def)
          if (o != d && o->state == EntState::Man && o->man == r) taken = true;
        const double dd = dist(r->pos, at) + (taken ? 30 : 0) + (r->state == EntState::Route ? 0 : 8);
        if (dd < bd) {
          bd = dd;
          best = r;
        }
      }
      if (!best) return false;
      d->state = EntState::Man;
      d->man = best;
      break;
    }
  }
  if (!z.empty()) {
    d->state = EntState::Zone;
    d->zone = z;
    d->zoneSpot = zoneLandmark(z);
    d->deepRole = isDeepZone(z);
  }
  adjusted_ = true;
  return true;
}

bool PlaySim::motion(Ent* e, double dy) {
  if (!preSnap() || !e || e->side != Side::Off || !e->eligible) return false;
  const V2 at = e->shiftTo ? *e->shiftTo : e->pos;
  const double y = clamp(at.y + dy, 3, FIELD_W - 3);
  if (std::fabs(y - at.y) < 0.1) return false;
  // off the ball when he crosses the formation (he can't cover up the end man on the line)
  moveTo(e, V2{(std::min)(at.x, los - 1.2), y});
  e->inMotion = e->shiftTo.has_value();
  if (e->state == EntState::Route && !e->routeName.empty()) assignRoute(e, e->routeName);
  for (Ent* d : def) {
    if (d->state != EntState::Man || d->man != e) continue;
    const V2 dp = d->shiftTo ? *d->shiftTo : d->pos;
    moveTo(d, V2{dp.x, clamp(dp.y + (y - at.y), 1, FIELD_W - 1)});
    d->inMotion = d->shiftTo.has_value();
  }
  return true;
}

void PlaySim::previewBlocks() {
  std::vector<std::pair<Ent*, V2>> was;
  for (Ent* e : ents)
    if (e->shiftTo) {
      was.emplace_back(e, e->pos);
      e->pos = *e->shiftTo;
    }
  for (Ent* e : off) e->blockTarget = nullptr;
  assignBlocks();
  for (auto& w : was) w.first->pos = w.second;
}

void PlaySim::presnapStep(double dt) {
  if (!preSnap()) return;
  // the play's motion starts once everyone else is set (half a second of the formation standing still) and is done
  // when he gets to his spot (walking() holds the snap until then)
  if (motionEnt_ && motionState_ == 0) {
    bool set = true;
    for (const Ent* e : ents)
      if (e->shiftTo && !e->inMotion) set = false;
    motionWait_ = set ? motionWait_ + dt : 0.0;
    // (a jet motion goes as the formation sets: it takes the set's second and the ball is snapped as he arrives)
    if (motionWait_ > (cfg.offPlay->motion->jet ? 0.1 : 0.5)) startPlayMotion();
  }
  for (Ent* e : ents) {
    if (!e->shiftTo) continue;
    const V2 d = sub(*e->shiftTo, e->pos);
    const double l = len(d);
    if (e == motionEnt_ && motionState_ == 1 && cfg.offPlay->motion->jet) {
      // jet motion: across at a run, facing where he goes, all the way to his spot
      const double sp = motionPreSpeed_ > 0 ? motionPreSpeed_ : (std::max)(4.5, len(motionVel_) * 0.8);
      e->face = angleOf(d);
      e->anim = Anim::Run;
      if (l <= sp * dt) {
        e->pos = *e->shiftTo;
        e->vel = V2{0, 0};
        e->shiftTo.reset();
        e->setFace.reset();
      } else {
        e->vel = scale(norm(d), sp);
        e->pos = add(e->pos, scale(e->vel, dt));
      }
      continue;
    }
    // ground to cover (back from the last play, a flipped formation): a jog, facing where he goes, 4.2 yd/s and up to
    // 5.4 from far off; the last two and a half yards a walk into his spot, turning back to face the way he lines up.
    // (3.4 yd/s sat between the captured walks and jogs: the feet slid, and he crabbed or backpedalled the whole way
    // facing the formation's direction)
    if (!e->setFace) e->setFace = e->face;
    const bool far = l > 2.5 && !e->inMotion;
    const double sp = far ? 4.2 + 1.2 * clamp((l - 6.0) / 6.0, 0.0, 1.0) : 1.9 * (std::min)(1.0, 0.35 + l / 0.8);
    if (far) {
      e->face = angleOf(d);
      e->anim = Anim::Run;
    } else if (!e->inMotion) {
      e->face = *e->setFace;
      if (e->anim == Anim::Run) e->anim = Anim::Idle;
    }
    if (l <= sp * dt || l < 0.02) {
      e->pos = *e->shiftTo;
      e->vel = V2{0, 0};
      e->shiftTo.reset();
      if (e->setFace) e->face = *e->setFace;
      e->setFace.reset();
      e->inMotion = false;
    } else {
      e->vel = scale(norm(d), sp);
      e->pos = add(e->pos, scale(e->vel, dt));
    }
  }
  if (motionEnt_ && motionState_ == 1 && !motionEnt_->shiftTo) motionState_ = 2;
}

bool PlaySim::userPresnapMove(const V2& stick, double dt, int slot) {
  Ent* const me = slotEnt(slot);  // (the human walking his man: slot 0 is the one-player game's)
  if (!preSnap() || !me || me->side != Side::Def) return false;
  const double m = (std::min)(1.0, len(stick));
  if (m < 0.2) {
    if (!me->shiftTo) me->vel = V2{0, 0};
    return false;
  }
  // a shuffle for a nudge, a jog (4.5 yd/s) for a full push
  const double sp = 1.2 + 3.3 * clamp((m - 0.2) / 0.8, 0.0, 1.0);
  me->shiftTo.reset();
  me->vel = scale(norm(stick), sp);
  V2 to = add(me->pos, scale(me->vel, dt));
  // his side of the neutral zone (a lineman's hand at the ball, everyone else a yard off it), on the field
  to.x = clamp(to.x, los + (me->isDL ? 0.55 : 1.0), los + 25.0);
  to.y = clamp(to.y, 1.0, FIELD_W - 1.0);
  me->pos = to;
  basePos_[me] = to;  // (a shift called after this is relative to where he is now)
  adjusted_ = true;
  return true;
}

bool PlaySim::rushMove(Ent* d, const std::string& m) {
  if (!d || d->engagedBy.empty() || d->moveCd > 0 || ball.mode == BallMode::Snap) return false;
  const std::vector<Ent*> blockers = d->engagedBy;
  Ent* b = blockers[0];
  const bool passPro = b->state == EntState::PassBlock;
  const bool power = m == "bull" || m == "club" || m == "longarm";
  const double cost = power ? 0.3 : 0.25;
  const bool tired = d->rushEnergy < cost * 0.8;
  d->rushEnergy = (std::max)(0.0, d->rushEnergy - cost);
  const Ratings& r = d->r();
  const Ratings& br = b->r();
  // the move's skill against the blocker's answer to it: power against his anchor, finesse against his feet
  double ms = power ? r.passRush * 0.45 + r.strength * 0.55 : r.passRush * 0.45 + r.agility * 0.35 + r.accel * 0.2;
  double bs = power ? br.passBlock * 0.5 + br.strength * 0.5 : br.passBlock * 0.6 + br.agility * 0.4;
  if (!passPro) {
    ms = r.blockShed * 0.6 + (power ? r.strength : r.agility) * 0.4;
    bs = br.runBlock * 0.6 + br.strength * 0.4;
  }
  // the same move twice running is read; a second blocker on him is twice the hands to beat
  const double repeat = m == d->lastMove && t - d->lastMoveT < 3.0 ? 0.35 : 0.0;
  const double x = (ms - bs) / 9 + 1.6 * (1 - b->blockPower) - 0.55 - (tired ? 1.5 : 0.0) - repeat -
                   (static_cast<double>(blockers.size()) - 1) * 1.0 + (power ? 0.6 * (std::max)(0.0, d->linePush) : 0.0) +
                   d->fx.moveWin;  // (abilities: Bull Charge, Havoc)
  const bool won = rng.chance(sigmoid(x));
  d->lastMove = m;
  d->lastMoveT = t;
  d->lastMoveWon = won;
  d->moveCd = power ? 0.9 : 0.75;
  b->blockPower = (std::max)(0.05, b->blockPower - (power ? 0.12 : 0.04));
  if (!won) {
    ev("rushfail", d, b, m);
    d->stallT = power ? 0.4 : 0.55;  // (a stoned finesse move is a man out of position)
    d->rushEnergy = (std::max)(0.0, d->rushEnergy - cost * 0.4);
    return true;
  }
  // won: off the block (as the hazard's shed)
  ev("shed", d, b, m);
  if (b->side == Side::Off) foulHold.add(tune().holdShed, b->p);
  if (passPro) noteBeat(d, blockers);
  V2 goal = qb && ball.holder == qb ? qb->pos : V2{d->pos.x - goalDir(b) * 3, d->pos.y};
  if (ball.holder && ball.holder->side != d->side) goal = ball.holder->pos;
  for (Ent* bb : blockers) {
    disengage(bb);
    bb->stun = (passPro ? 0.6 : 0.35) + rng.range(0, 0.3);
    if (passPro) bb->slipT = bb->stun;
    if (!contains(bb->beatBy, d)) bb->beatBy.push_back(d);
  }
  d->engaged = nullptr;
  d->engagedBy.clear();
  d->stun = 0;
  d->vel = scale(norm(sub(goal, d->pos)), passPro ? 3.5 : 2.5);
  d->anim = Anim::Run;
  return true;
}

void PlaySim::finishPresnap() {
  for (Ent* e : ents) {
    // (snapped with a man in motion: he goes from where he is, at his speed; anyone else still walking is set)
    if (e->shiftTo && !e->inMotion) e->pos = *e->shiftTo;
    if (e->setFace && !e->inMotion) e->face = *e->setFace;
    e->setFace.reset();
    if (!(e->shiftTo && e->inMotion)) e->vel = V2{0, 0};
    e->shiftTo.reset();
    e->inMotion = false;
  }
  // receivers' release: the route's own delay, then the jam at the line (the called press, or a pressed corner)
  for (Ent* r : off) {
    if (!r->eligible || r->state != EntState::Route) continue;
    double jam = 0;
    auto it = pressRoll_.find(r);
    if (it != pressRoll_.end() && shellAdj != ShellAdj::Off) jam = it->second;
    if (shellAdj == ShellAdj::Press) {
      for (Ent* d : def) {
        if (!isCorner(d) || dist(d->pos, r->pos) > 2.6) continue;
        jam = (std::max)(jam, sigmoid((d->r().press - r->r().release) / 10 - 0.3) * 0.45);
      }
    }
    r->pressDelay = (std::max)(routeDelay(r), jam);
  }
  // rushers rush from where they stand, linebackers fit the gaps in front of them
  for (Ent* e : def) {
    if (e->state == EntState::Rush || isLB(e)) e->gap = e->pos.y;
  }
  // the line re-sets its blocking against the new look
  for (Ent* e : off) e->blockTarget = nullptr;
  assignBlocks();
  setupFits();
}

void PlaySim::setupFits() {
  for (Ent* e : def) {
    e->fitOn = false;
    e->contain = 0;
  }
  if (tune().gapFit <= 0 || cfg.kick || cfg.place) return;
  // the gaps: between the men on the offensive line (the tight ends on it too), and one outside the last man each side
  std::vector<double> ys;
  for (const Ent* o : off)
    if (o->isOL || (isTERole(o->role) && o->pos.x > los - 2.0 && std::fabs(o->pos.y - by) < 6)) ys.push_back(o->pos.y);
  if (ys.size() < 2) return;
  std::sort(ys.begin(), ys.end());
  std::vector<double> gaps;
  for (size_t i = 0; i + 1 < ys.size(); i++) gaps.push_back((ys[i] + ys[i + 1]) / 2);
  gaps.push_back(ys.front() - 1.0);
  gaps.push_back(ys.back() + 1.0);
  // the front: the men on the line (the down linemen, an end standing up), then the backers in the box
  std::vector<Ent*> line, backers;
  for (Ent* d : def) {
    if (d->deepRole || std::fabs(d->pos.y - by) > 8) continue;
    if (d->pos.x < los + 2.0) line.push_back(d);
    else if (isLB(d) && d->pos.x < los + 7) backers.push_back(d);
  }
  std::vector<bool> used(gaps.size(), false);
  auto fit = [&](const std::vector<Ent*>& men) {
    for (;;) {  // nearest man-to-gap pair first, one man a gap
      double bd = 3.5;
      size_t bi = 0;
      Ent* bm = nullptr;
      for (Ent* m : men) {
        if (m->fitOn) continue;
        for (size_t i = 0; i < gaps.size(); i++) {
          const double dd = std::fabs(gaps[i] - m->pos.y);
          if (!used[i] && dd < bd) {
            bd = dd;
            bi = i;
            bm = m;
          }
        }
      }
      if (!bm) break;
      used[bi] = true;
      bm->fitOn = true;
      bm->fitY = gaps[bi];
    }
  };
  fit(line);
  fit(backers);
  // the edge: the widest man of the front each side, outside the last man on the line or on his shoulder; in a two-deep
  // shell the flat corner is the force on his side as well
  std::vector<Ent*> front = line;
  front.insert(front.end(), backers.begin(), backers.end());
  for (int s : {1, -1}) {
    const double lastY = s > 0 ? ys.back() : ys.front();
    Ent* best = nullptr;
    double bw = 0;
    for (Ent* m : front) {
      const double w = (m->pos.y - by) * s;
      if (w > bw && (m->pos.y - lastY) * s > -1.0) {
        bw = w;
        best = m;
      }
    }
    if (best) best->contain = s;
  }
  const Coverage cov = cfg.defCall->coverage;
  if (cov == Coverage::C2 || cov == Coverage::C2T || cov == Coverage::C6)
    for (Ent* d : def)
      if (isCorner(d) && (d->zone == "flatL" || d->zone == "flatR")) d->contain = d->zone == "flatL" ? 1 : -1;
}

bool PlaySim::fitRun(Ent* e, const Ent* c) {
  if (tune().gapFit <= 0 || (!e->fitOn && !e->contain) || e->userControlled || cfg.kick || cfg.place) return false;
  if (!e->contain && tune().gapFit >= 2) return false;  // (2: the edge only - the inside men pursue as before)
  if (cfg.offPlay->type != PlayType::Run || passThrown || c->side != Side::Off || (c == qb && scramble)) return false;
  // past the line, or a back dancing behind it this long after the run was read: everyone to the ball
  if (c->pos.x > los + 0.5 || (runRead && t - runReadT > 1.6)) return false;
  V2 tgt;
  if (e->contain) {
    const double s = e->contain;
    // the edge: outside the runner, squeezing down to the line - he keeps him on his inside shoulder. Turned well inside
    // him, or out round him already, he plays the ball as everyone does.
    if ((e->pos.y - c->pos.y) * s > tune().containW + 2.5 || (c->pos.y - e->pos.y) * s > 0.5) return false;
    tgt = V2{clamp(c->pos.x + 2.0, los + 0.3, (std::max)(los + 0.3, e->pos.x)), c->pos.y + s * tune().containW};
  } else {
    // his gap, flowing with the play: the front slides with the back (gapFlow x his move across from the ball) and keeps
    // its order, each man in his gap of it; the ball coming into his gap, he goes and gets it; otherwise he fills it,
    // drifting toward the back no more than gapGive off it (the cutback has to find somebody home)
    const double gy = e->fitY + (c->pos.y - by) * tune().gapFlow;
    const double dyc = c->pos.y - gy;
    if (std::fabs(dyc) < tune().gapCommit) return false;
    const double y = gy + clamp(dyc, -tune().gapGive, tune().gapGive);
    const double x = e->pos.x < los + 1.5 ? los + 0.6 : clamp(e->pos.x, los + 1.2, los + 3.0);
    tgt = V2{x, y};
  }
  const double d = dist(tgt, e->pos);
  e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, d * 4 + 0.5));
  return true;
}

// ------------------------------------------------------------------ match coverage (Tune::matchCov)
void PlaySim::numberReceivers() {
  for (int s : {1, -1}) {
    std::vector<Ent*> rs;
    for (Ent* e : off) {
      if (!e->eligible || e == qb) continue;
      if ((e->pos.y > by ? 1 : -1) == s) rs.push_back(e);
    }
    std::stable_sort(rs.begin(), rs.end(), [this](const Ent* a, const Ent* b) { return std::fabs(a->pos.y - by) > std::fabs(b->pos.y - by); });
    for (size_t i = 0; i < rs.size(); i++) {
      rs[i]->recNum = static_cast<int>(i) + 1;
      rs[i]->recSide = s;
    }
  }
}

Ent* PlaySim::recNo(int side, int n) const {
  for (Ent* e : off)
    if (e->recSide == side && e->recNum == n && e->state == EntState::Route) return e;
  return nullptr;
}

bool PlaySim::matchZone(Ent* e, const V2& spot) {
  if (tune().matchCov <= 0 || e->userControlled || !qb || passThrown || ball.mode == BallMode::Air || cfg.kick || cfg.place) return false;
  if (!(ball.holder == qb || ball.mode == BallMode::Snap) || e->zone.empty()) return false;
  const Coverage cov = cfg.defCall->coverage;
  const std::string& z = e->zone;
  // (L = the +y side, as the landmarks have it; the inside quarters by where theirs is)
  int side = z.back() == 'L' ? 1 : z.back() == 'R' ? -1 : 0;
  if (z == "q2" || z == "q3") side = spot.y > by ? 1 : -1;
  auto vertical = [&](const Ent* r) { return r && r->pos.x - los > 4.0 && r->vel.x > 2.5 && std::fabs(r->vel.y) < r->vel.x; };
  auto go = [&](const V2& tgt) {
    e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, dist(tgt, e->pos) * 4 + 0.5));
    if (!e->matchOn) {
      e->matchOn = true;
      matchCarries_++;
    }
    return true;
  };
  // run with him up the field: underneath and inside him, between him and the quarterback
  auto carry = [&](const Ent* r, double under) {
    const V2 tp = add(r->pos, scale(r->vel, 0.3));
    return go(add(tp, scale(norm(sub(qb->pos, tp)), under)));
  };
  const double carryTo = los + tune().matchCarry;
  if (cov == Coverage::C3 || cov == Coverage::C3Fire) {
    // Cover 3 Match: the curl / flat man carries #2 vertical, the hook man the inside vertical (#3; the middle hook either
    // side's), each to matchCarry yd, then passes him on to the deep third and sinks back into his zone
    Ent* r = nullptr;
    if (side != 0 && z.rfind("flat", 0) == 0) r = recNo(side, 2);
    else if (z.rfind("hook", 0) == 0) {
      r = side != 0 ? recNo(side, 3) : nullptr;
      if (side == 0)
        for (int s : {1, -1})
          if (Ent* x = recNo(s, 3); vertical(x)) r = x;
    }
    if (vertical(r) && r->pos.x < carryTo) return carry(r, z.rfind("flat", 0) == 0 ? 1.0 : 0.8);
  } else if (cov == Coverage::C4 && (z == "q2" || z == "q3")) {
    // Quarters: the inside safety reads #2 - vertical past 7 yd, he has him over the top and inside; anything else and
    // (once he has seen it) he helps over #1
    Ent* r2 = recNo(side, 2);
    if (vertical(r2) && r2->pos.x - los > 7.0) return go(V2{(std::min)(r2->pos.x + 1.5 + r2->speed() * 0.12, DEEP_AIM), r2->pos.y - side * 0.7});
    Ent* r1 = recNo(side, 1);
    if (t > 1.0 && r1 && r1->pos.x - los > 5.0) return go(V2{(std::min)((std::max)(spot.x, r1->pos.x + 3.0), DEEP_AIM), r1->pos.y * 0.6 + spot.y * 0.4});
  } else if ((cov == Coverage::C2 || cov == Coverage::C2T) && isCorner(e) && side != 0 && z.rfind("flat", 0) == 0) {
    // Cover 2: nobody in his flat, he sinks under #1 going deep (the hole shot behind him and in front of the half safety)
    bool flatEmpty = t > 1.0;
    for (const Ent* r : off)
      if (r->state == EntState::Route && dist(r->pos, spot) < 6.0) flatEmpty = false;
    Ent* r1 = recNo(side, 1);
    if (flatEmpty && vertical(r1)) return go(V2{(std::min)(r1->pos.x - 1.0, los + 15.0), r1->pos.y - side * 1.0});
  }
  if (cov == Coverage::C2T && z == "midM") {
    // the Tampa Mike carries a vertical down the middle
    for (Ent* r : off)
      if (r->state == EntState::Route && vertical(r) && std::fabs(r->pos.y - by) < 9 && r->pos.x - los > 6 && r->pos.x - los < 22) return carry(r, 0.3);
  }
  e->matchOn = false;
  return false;
}

// ------------------------------------------------------------------ line stunts (Tune::stunts)
void PlaySim::setupStunt() {
  stunt_ = 0;
  for (Ent* e : def) {
    e->stunt = 0;
    e->stuntDone = e->stuntSet = false;
    e->stuntMate = nullptr;
  }
  if (tune().stunts <= 0 || cfg.kick || cfg.place || !qb) return;
  const int dn = cfg.down ? static_cast<int>(*cfg.down) : 1;
  const double tg = cfg.toGo.value_or(10);
  const bool passDown = (dn >= 3 && tg >= 5) || (dn == 2 && tg >= 8);
  const double p = tune().stunts * (passDown ? 1.0 : tune().stuntEarly);
  if (!rng.chance(p)) return;
  std::vector<Ent*> dl;
  for (Ent* d : def)
    if (d->isDL && d->state == EntState::Rush) dl.push_back(d);
  if (dl.size() < 3) return;
  std::sort(dl.begin(), dl.end(), [](const Ent* a, const Ent* b) { return a->pos.y < b->pos.y; });
  int kind = rng.intRange(1, dl.size() >= 4 ? 3 : 2);
  const bool plus = rng.chance(0.5);
  Ent* end = plus ? dl.back() : dl.front();
  Ent* tackle = plus ? dl[dl.size() - 2] : dl[1];
  Ent* pen = nullptr;
  Ent* loop = nullptr;
  if (kind == 1) pen = tackle, loop = end;       // tackle-end: the tackle crosses first, the end loops inside
  else if (kind == 2) pen = end, loop = tackle;  // end-tackle: the end crashes inside, the tackle loops outside
  else {                                          // tackle-tackle: one tackle across the center, the other behind him
    pen = plus ? dl[2] : dl[1];
    loop = plus ? dl[1] : dl[2];
  }
  pen->stunt = 1;
  loop->stunt = 2;
  pen->stuntMate = loop;
  loop->stuntMate = pen;
  loop->stuntT = tune().stuntJab;
  stunt_ = kind;
}

bool PlaySim::thinkStunt(Ent* e) {
  if (!e->stunt || e->stuntDone || e->userControlled || !e->stuntMate) return false;
  if (!e->stuntSet) {
    // the gap he goes for: where his partner lined up (the penetrator crosses his face into it, the looper fills the one
    // the penetrator left)
    e->stuntSet = true;
    e->stuntY = e->stuntMate->pos.y;
  }
  if (t > 1.6) {
    e->stuntDone = true;
    return false;
  }
  if (e->stunt == 1) {
    // the penetrator: hard and flat across into the gap beside him, into the blocker there (the pick)
    if (std::fabs(e->pos.y - e->stuntY) < 0.5 || e->pos.x < los - 1.5) {
      e->stuntDone = true;
      return false;
    }
    V2 tgt{los - 0.6, e->stuntY};
    for (const Ent* o : off)
      if (o->snapTarget == e->stuntMate && o->state == EntState::PassBlock) tgt = V2{o->pos.x + 0.3, o->pos.y};
    e->want = scale(norm(sub(tgt, e->pos)), e->vmax);
    return true;
  }
  // the looper: a jab up the field to hold his blocker, then round behind the penetrator into the gap he left
  if (t < e->stuntT) {
    e->want = scale(norm(sub(qb->pos, e->pos)), e->vmax * 0.35);
    return true;
  }
  const V2 tgt{los - 0.6, e->stuntY};
  if (std::fabs(e->pos.y - e->stuntY) < 0.6) {
    e->stuntDone = true;
    return false;
  }
  // (round behind: across at his depth first, then down into the gap - not through the pile)
  const V2 via{(std::max)(los + 0.6, e->pos.x), e->stuntY};
  e->want = scale(norm(sub(std::fabs(e->pos.y - e->stuntY) > 1.0 ? via : tgt, e->pos)), e->vmax);
  return true;
}

void PlaySim::thinkSpy(Ent* e) {
  if (reactToBall(e)) return;
  Ent* h = ball.holder;
  if (h && h->side == Side::Off && (h != qb || h->state == EntState::Carrier || scramble)) {
    e->state = EntState::Pursue;
    return;
  }
  // mirror the quarterback from five yards off the ball
  const V2 spot{(std::max)(los + 4.5, qb->pos.x + 6), qb->pos.y};
  const V2 to = sub(spot, e->pos);
  e->want = scale(norm(to), (std::min)(e->vmax * 0.9, len(to) * 3));
}

V2 PlaySim::zoneLandmark(const std::string& z) const {
  const double L = los;
  const double W = FIELD_W;
  auto cl = [W](double y) { return clamp(y, 3, W - 3); };
  // near the goal line the zones compress: nobody drops past the back of the end zone
  auto dx = [L](double d) { return (std::min)(L + d, DEEP_AIM - 0.5); };
  if (z == "deepM") return V2{dx(15), cl(clamp(by, 20, 33))};
  if (z == "thirdL") return V2{dx(14), cl(W - 10)};
  if (z == "thirdM") return V2{dx(15), cl(clamp(by, 20, 33))};
  if (z == "thirdR") return V2{dx(14), cl(10)};
  if (z == "halfL") return V2{dx(15), cl(W * 0.73)};
  if (z == "halfR") return V2{dx(15), cl(W * 0.27)};
  if (z == "q1") return V2{dx(13), cl(W - 8)};
  if (z == "q2") return V2{dx(14), cl(W * 0.6)};
  if (z == "q3") return V2{dx(14), cl(W * 0.4)};
  if (z == "q4") return V2{dx(13), cl(8)};
  if (z == "flatL") return V2{dx(4.5), cl((std::max)(by + 11, W - 8))};
  if (z == "flatR") return V2{dx(4.5), cl((std::min)(by - 11, 8.0))};
  if (z == "hookL") return V2{dx(8), cl(by + 5.5)};
  if (z == "hookR") return V2{dx(8), cl(by - 5.5)};
  if (z == "midM") return V2{dx(12), cl(by)};  // Tampa 2: the hole between the safeties
  if (z == "robM") return V2{dx(9.5), cl(by)};  // Cover 1 Robber: the hole under the free safety, over the crossers
  return V2{dx(7), cl(by)};
}

void PlaySim::pickUserEnt(Side side) {
  if (user) user->userControlled = false;
  if (side == Side::Off) {
    user = qb;
  } else {
    // closest defender to ball, prefer second level
    std::vector<Ent*> cands;
    for (Ent* e : def)
      if (!e->isDL) cands.push_back(e);
    std::stable_sort(cands.begin(), cands.end(), [this](const Ent* a, const Ent* b) {
      return dist(a->pos, qb->pos) - dist(b->pos, qb->pos) < 0;
    });
    Ent* mlb = defRole("MLB");
    user = mlb ? mlb : (cands.empty() ? nullptr : cands[0]);
  }
  if (user) user->userControlled = true;
}

void PlaySim::switchUser(int slot) {
  Ent* const cur = slotEnt(slot);
  if (!cur) return;
  const Side side = cur->side;
  const V2 bp{ball.pos.x, ball.pos.y};
  const std::vector<Ent*>& team = side == Side::Off ? off : def;
  std::vector<Ent*> cands;
  for (Ent* e : team)
    if (e != cur && e->state != EntState::Down && !e->userControlled) cands.push_back(e);  // (never another human's man)
  if (cands.empty()) return;
  // before the snap the switch cycles the whole defense, nearest the ball first (Madden's CIRCLE): taking the nearest
  // other man each time only ever flipped between the same two
  if (preSnap() && side == Side::Def) cands.push_back(cur);
  std::stable_sort(cands.begin(), cands.end(), [&bp](const Ent* a, const Ent* b) { return dist(a->pos, bp) - dist(b->pos, bp) < 0; });
  Ent* next = cands[0];
  if (preSnap() && side == Side::Def) {
    const auto it = std::find(cands.begin(), cands.end(), cur);
    next = (it == cands.end() || it + 1 == cands.end()) ? cands[0] : *(it + 1);
  }
  setSlotEnt(slot, next);
}

bool PlaySim::switchUserToward(const V2& dir, int slot) {
  Ent* const me = slotEnt(slot);
  if (!me || len(dir) < 0.1) return false;
  const V2 d = norm(dir);
  const std::vector<Ent*>& team = me->side == Side::Off ? off : def;
  Ent* best = nullptr;
  double bs = 1e9;
  for (Ent* e : team) {
    if (e == me || e->state == EntState::Down || e->userControlled) continue;
    const V2 rel = sub(e->pos, me->pos);
    const double l = len(rel);
    if (l < 0.05) continue;
    const double c = dot(rel, d) / l;
    if (c < 0.64) continue;  // within about 50 degrees of the stick
    const double s = l * (1 + 2.5 * (1 - c));  // the nearest man that way, a man dead on the line before one off it
    if (s < bs) {
      bs = s;
      best = e;
    }
  }
  if (!best) return false;
  return setSlotEnt(slot, best);
}

int PlaySim::slotOf(const Ent* e) const {
  if (!e) return -1;
  if (e == user) return 0;
  for (size_t i = 0; i < mates.size(); ++i)
    if (mates[i].ent == e) return static_cast<int>(i) + 1;
  return -1;
}

bool PlaySim::setSlotEnt(int s, Ent* e) {
  const int has = slotOf(e);
  if (e && has >= 0 && has != s) return false;  // another human's man
  Ent* const was = slotEnt(s);
  if (was == e) return true;
  if (was) was->userControlled = false;
  if (s <= 0) user = e;
  else if (s - 1 < static_cast<int>(mates.size())) mates[s - 1].ent = e;
  else return false;
  if (e) {
    e->userControlled = true;
    if (s > 0) mates[s - 1].side = e->side;
  }
  return true;
}

namespace {
// the order a second, third... human on a side is given his man: the playmakers first (the backs and the receivers
// on offense, the linebackers and the secondary on defense), the line last
int mateRank(const Ent* e) {
  switch (e->p->pos) {
    case Pos::RB: return 0;
    case Pos::WR: return 1;
    case Pos::TE: return 2;
    case Pos::FB: return 3;
    case Pos::LB: return 0;
    case Pos::S: return 1;
    case Pos::CB: return 2;
    case Pos::DE: return 3;
    case Pos::DT: return 4;
    default: return 9;
  }
}
}  // namespace

int PlaySim::addMate(Side side, Ent* prefer) {
  UserSlot sl;
  sl.side = side;
  mates.push_back(sl);
  const int s = static_cast<int>(mates.size());
  const std::vector<Ent*>& team = side == Side::Off ? off : def;
  Ent* pick = nullptr;
  if (prefer && !prefer->userControlled && std::find(team.begin(), team.end(), prefer) != team.end()) pick = prefer;
  if (!pick) {
    std::vector<Ent*> cands;
    for (Ent* e : team)
      if (!e->userControlled && e != qb && e != kickerEnt && !(side == Side::Off && !cfg.kick && !e->eligible)) cands.push_back(e);
    // (a kick return: every man on the field may be had; the returner is usually slot 0's)
    const V2 bp{ball.pos.x, ball.pos.y};
    std::stable_sort(cands.begin(), cands.end(), [&](const Ent* a, const Ent* b) {
      const int ra = cfg.kick ? 0 : mateRank(a), rb = cfg.kick ? 0 : mateRank(b);
      if (ra != rb) return ra < rb;
      return dist(a->pos, bp) < dist(b->pos, bp);
    });
    if (!cands.empty()) pick = cands[0];
  }
  setSlotEnt(s, pick);
  return s;
}

bool PlaySim::cycleEligible(int slot, int dir) {
  Ent* const cur = slotEnt(slot);
  if (!preSnap() || !cur || cur->side != Side::Off || cur == qb) return false;
  // the eligible men across the formation (left to right as the offense sees it), the ones another human has skipped
  std::vector<Ent*> el;
  for (Ent* e : off)
    if (e->eligible && e != qb && (e == cur || !e->userControlled)) el.push_back(e);
  if (el.size() < 2) return false;
  std::stable_sort(el.begin(), el.end(), [](const Ent* a, const Ent* b) { return a->pos.y > b->pos.y; });
  const auto it = std::find(el.begin(), el.end(), cur);
  if (it == el.end()) return setSlotEnt(slot, el[0]);
  const int n = static_cast<int>(el.size()), i = static_cast<int>(it - el.begin());
  return setSlotEnt(slot, el[((i + (dir >= 0 ? 1 : -1)) % n + n) % n]);
}

// ------------------------------------------------------------------ helpers
void PlaySim::ev(const char* type, Ent* a, Ent* b, const std::string& note) {
  PlayEvent e;
  e.t = t;
  e.type = type;
  e.a = a;
  e.b = b;
  e.x = ballSpotX();
  e.note = note;
  events.push_back(e);
  if (result) result->events.push_back(e);  // TS result.events aliases this.events
  // the first play on a pass that was forecast (Ball::fcFate): what happened, and a revision when it is not what the
  // forecast had (the presentation re-picks)
  if (ball.fcFate != 0 && ball.fcOut == 0 && passThrown && !cfg.kick && !cfg.place) {
    const std::string ty = type;
    const int oc = ty == "catch" || ty == "divecatch" || ty == "oobcatch" ? 1 : ty == "drop" || ty == "contested_inc" ? 2 : ty == "pbu" || ty == "batted" ? 3 : ty == "int" ? 4 : ty == "incomplete" ? 5 : 0;
    if (oc) {
      Ent* who = oc == 5 ? nullptr : a;
      ball.fcOut = oc;
      ball.fcOutBy = who;
      ball.fcOutT = t;
      ball.fcOutAt = ball.pos;
      if (oc != ball.fcFate || who != ball.fcBy) {
        ball.fcFate = oc;
        ball.fcBy = who;
        ball.fcAt = V2{ball.pos.x, ball.pos.y};
        ball.fcZ = ball.pos.z;
        ball.fcT = t;
        ball.fcRev++;
        ball.fcWhy = "at the ball: " + ty;
      }
    }
  }
}
double PlaySim::ballSpotX() const { return ball.holder ? ball.holder->pos.x : ball.pos.x; }
const std::vector<Ent*>& PlaySim::defendersOf(const Ent* e) const { return e->side == Side::Off ? def : off; }
const std::vector<Ent*>& PlaySim::teammatesOf(const Ent* e) const { return e->side == Side::Off ? off : def; }
Side PlaySim::possSide() const {
  if (ball.holder) return ball.holder->side;
  return Side::Off;
}
double PlaySim::goalDir(const Ent* e) const { return e->side == Side::Off ? 1 : -1; }

// ------------------------------------------------------------------ main loop
void PlaySim::step(double dt) {
  if (phase != SimPhase::Live) return;
  // the play's own motion: he is at his spot as the ball is snapped (walked there by presnapStep, or put there now when
  // nothing walked him: the headless sim), a jet man at full speed across
  const bool motionSnap = t == 0 && motionEnt_ && !cfg.kick;
  if (motionSnap) snapPlayMotion();
  if (adjusted_ && t == 0) {
    adjusted_ = false;
    finishPresnap();
  }
  if (motionSnap && cfg.offPlay->motion->jet) motionEnt_->vel = motionVel_;
  t += dt;
  for (int s = 0; s < userSlots(); ++s) {
    UserInput& in = slotInput(s);
    if (in.switchReq) {
      in.switchReq = false;
      switchUser(s);
    }
    // R1 on defense: he goes for the ball for the next 0.8 s (a punch at it as he arrives)
    if (in.strip) {
      in.strip = false;
      if (Ent* u = slotEnt(s); u && u->side == Side::Def) u->stripT = 0.8;
    }
  }
  if (cfg.place) {
    updatePlace(dt);  // (the snap, the hold, the strike: Sim/PlaceKick.cpp)
    if (phase != SimPhase::Live) return;
  } else {
    updateSnap();
  }
  updateReads(dt);
  for (Ent* e : ents) {
    if (e->stun > 0) e->stun -= dt;
    if (e->slipT > 0) e->slipT -= dt;
    if (e->tackleCd > 0) e->tackleCd -= dt;
    if (e->stripT > 0) e->stripT -= dt;
    if (e->pressDelay > 0) e->pressDelay -= dt;
    e->animT += dt;
    e->moveT += dt;
    e->velHist.push_back(e->vel);
    if (e->velHist.size() > 40) e->velHist.pop_front();
  }
  if (pendingThrow && t >= pendingThrow->at) {
    const PendingThrow pt = *pendingThrow;
    pendingThrow.reset();
    if (ball.holder == qb && ball.mode == BallMode::Held && qb->state != EntState::Down) {
      if (pt.away) throwAway(qb);
      else if (pt.tgt) throwBall(qb, pt.tgt, pt.lob, pt.style, pt.shovel ? 1 : 0);
    }
  }
  if (cfg.kick) updateKickBall(dt);
  // the returner on his knee in the end zone: the whistle once he is down
  if (kneelT >= 0 && t - kneelT > 0.8) {
    endPlay("kneel");
    return;
  }
  for (Ent* e : ents) think(e, dt);
  for (Ent* e : ents) move(e, dt);
  if (cfg.place) placeAfterMove();
  for (int s = 0; s < userSlots(); ++s) {
    UserInput& in = slotInput(s);
    if (in.rushMove.empty()) continue;
    Ent* u = slotEnt(s);
    if (u && u->side == Side::Def) rushMove(u, in.rushMove);
    in.rushMove.clear();
  }
  updateEngagements(dt);
  collide();
  noteWinReps();
  for (Ent* e : off) {
    // (a lineman 16 yd downfield is done blocking, whatever he was doing: a block was once carried 40 yd past the line)
    if (e->isOL && e->engaged && !cfg.kick && e->pos.x - los > 16) disengage(e);
    if (e->isOL && e->engaged && !cfg.kick) olBlockDepth = (std::max)(olBlockDepth, e->pos.x - los);
  }
  for (Ent* d : def) {
    if (d->engagedBy.size() >= 2 && d->engagedBy[0]->state == EntState::PassBlock) {
      dblTeamT += dt;
      if (dblFirstT < 0) dblFirstT = t;
    }
    if (d->engagedBy.size() >= 2 && d->engagedBy[0]->state == EntState::RunBlock && d->isDL && !cfg.kick) runDblT_ += dt;
  }
  if (!cfg.kick && !cfg.place && !passThrown)
    for (int k = 0; k < 3; k++)
      if (t - dt < 0.5 * (k + 1) - 1e-9 && t >= 0.5 * (k + 1) - 1e-9)
        for (const Ent* e : def)
          if (isLB(e)) lbDepth_[k] += e->pos.x - los, lbDepthN_[k]++;
  for (Ent* e : def)
    if (e->state == EntState::Man && e->man && e->man->state == EntState::Route && !passThrown && !cfg.kick) {
      const int k = static_cast<int>(t * 2 + 1e-6) - 1;
      if (k >= 0 && k < 6 && static_cast<int>((t - dt) * 2 + 1e-6) - 1 != k) {
        manGap[k] += dist(e->pos, e->man->pos), manGapN[k] += 1;
        manSpd[k] += e->speed(), recSpd[k] += e->man->speed();
      }
    }
  // a designed run's path (telemetry): when and where the ball crossed the line, how far the man with it ran (and the
  // play's back from the snap, the way NGS measures it)
  if (!cfg.kick && !cfg.place && cfg.offPlay->type == PlayType::Run && cfg.offPlay->run && !passThrown && (!ball.holder || ball.holder->side == Side::Off)) {
    if (const Ent* rb = offRole(cfg.offPlay->run->carrier)) {
      if (t <= dt + 1e-9) runLast0_ = rb->pos;
      runDist0_ += dist(rb->pos, runLast0_);
      runLast0_ = rb->pos;
    }
  }
  if (!cfg.kick && !cfg.place && cfg.offPlay->type == PlayType::Run && carrier && carrier->side == Side::Off && ball.holder == carrier &&
      ball.mode == BallMode::Held && !passThrown) {
    if (runHandT_ < 0) {
      runHandT_ = t;
      runLastPos_ = carrier->pos;
    }
    runDist_ += dist(carrier->pos, runLastPos_);
    runLastPos_ = carrier->pos;
    if (!runCrossed_ && carrier->pos.x > los) {
      runCrossed_ = true;
      runCrossT_ = t;
      runCrossDy_ = carrier->pos.y - by;
    }
  }
  if (wrap_ && phase == SimPhase::Live) stepWrap(dt);
  updateBall(dt);
  if (!cfg.kick && qb && passPlay()) {
    if (!passThrown && ball.holder == qb) {
      // contact past 5 yd: a cover man on top of his receiver downfield (hand fighting is what draws the flag)
      for (Ent* r : off) {
        if (r->state != EntState::Route || r->pos.x < los + 5) continue;
        for (Ent* d : def)
          if ((d->state == EntState::Man || d->state == EntState::Zone) && dist(d->pos, r->pos) < 0.75)
            foulDhold.add(0.7 * dt * (1.2 - d->r().manCov / 100), d->p);
      }
    } else if (passThrown && t - throwStart < 0.6 && qb->state != EntState::Down) {
      // a rusher arriving after the ball is gone (a step or two late is a hit on the passer)
      for (Ent* d : def)
        if (d->free() && dist(d->pos, qb->pos) < 1.0 && t - throwStart > 0.25) foulRough.add(0.5 * dt, d->p);
    }
  }
  if (phase == SimPhase::Live) checkTackles();
  if (phase == SimPhase::Live) checkBounds();
  if (onStep) onStep(*this);
}

const PlayResult& PlaySim::runToEnd(double maxT) {
  int guard = 0;
  while (phase == SimPhase::Live && t < maxT && guard++ < maxT * 70) step(SIM_DT);
  if (phase == SimPhase::Live) endPlay("tackle");
  return *result;
}

// ------------------------------------------------------------------ the run exchange
namespace {
/** Seconds a man takes to cover d yards from rest, building to v, on the sim's sprint curve (steerVel: a = vmax / tau x
 *  (1 - s / vmax)); `vEnd` is the speed he gets there at. */
double coverTime(double d, double v, double vmax, double tau, double* vEnd = nullptr) {
  double s = 0, tm = 0;
  const double h = 1.0 / 120.0;
  while (d > 0 && tm < 4.0) {
    s = (std::min)(v, s + vmax / tau * (std::max)(0.12, 1 - s / vmax) * h);
    d -= s * h;
    tm += h;
  }
  if (vEnd) *vEnd = s;
  return tm;
}
}  // namespace

bool PlaySim::meshOn() const {
  const OffPlay* p = cfg.offPlay;
  return tune().mesh > 0 && p && p->type == PlayType::Run && p->run && p->run->carrier != "QB" && p->run->style != RunStyle::Sneak && qb && !cfg.kick;
}

const PlaySim::Mesh& PlaySim::mesh() const {
  // (fixed at the snap, not before it: asked for during the pre-snap walk-in - the director's look and framing do - it
  // was fixed from where the men were walking, before finishPresnap set them, a flip or a motion; in the game the
  // quarterback went to a mesh yards from the back's path and the ball changed hands 6 s in, or never)
  if (preSnap()) return premesh_ = meshFor(cfg.offPlay->run->carrier, runAimDy(), cfg.offPlay->run->style, cfg.offPlay->run->handoffDelay);
  if (!mesh_.ok) mesh_ = meshFor(cfg.offPlay->run->carrier, runAimDy(), cfg.offPlay->run->style, cfg.offPlay->run->handoffDelay);
  return mesh_;
}

double PlaySim::meshIn() const {
  if (handedOff || !meshOn()) return -1.0;
  const Mesh& m = mesh();
  if (m.toss) return (std::max)(0.0, (tossT0 >= 0 ? tossT0 : (std::max)(t, m.T)) + m.tossDur - t);
  // by the clock until the back is on his way in, then by where he is: the ball changes hands as he comes up to the mesh
  // point (updateSnap), whatever the clock says
  const Ent* rb = offRole(cfg.offPlay->run->carrier);
  // (he is timed to be at the mesh point a little ahead of T, and takes it a step short of the point: 0.15 s early)
  const double byClock = (std::max)(0.0, m.T - 0.15 - t);
  if (!rb || !meshBurst) return byClock;
  const double left = -dot(sub(rb->pos, m.at), m.dir) - 0.15;
  const double sp = dot(rb->vel, m.dir);
  if (left > 2.5 || sp < 2.0) return byClock;
  return (std::max)((std::max)(0.0, m.T - 0.2 - t), left / sp);
}

const PlaySim::Mesh& PlaySim::paMesh() const {
  // play action shows the defense the formation's inside run to the fake's side: the same mesh, the same footwork, the
  // back down the same path - at the fake's own time (paMeshT), so the pass behind it is timed as it was
  if (preSnap()) {  // (not fixed before the snap: see mesh())
    premesh_ = meshFor("RB", paSide() * 2.0, RunStyle::Zone, 0.8);
    return premesh_;
  }
  if (!paMesh_.ok) {
    paMesh_ = meshFor("RB", paSide() * 2.0, RunStyle::Zone, 0.8);
    paMesh_.T = paMeshT();
    paMesh_.go = (std::min)(paMesh_.go, 0.3);
  }
  return paMesh_;
}

PlaySim::Mesh PlaySim::meshFor(const std::string& role, double aimDy, RunStyle style, double nominalT) const {
  // Where a handoff is made, as it is coached. The back runs a line from his stance to the hole and takes the ball on it
  // without breaking stride; the quarterback brings the ball to that line and stands an arm's length off it, on his own
  // side, turned to him. Under center he comes back off the line to a mesh 3-5 yd deep (the back is 7 deep and needs the
  // time to read his blocks: 3.8 on an inside run, deeper and wider on a stretch); from the gun or the pistol he
  // catches the snap and steps to the back's path where it passes him, the back holding his stance for a beat (the
  // timing step) so that he gets there at a run and not before the ball. Both arrive together: the time is the later of
  // the two men's. (The old exchange was on a clock at the quarterback's hip: a gun back was there in 0.3 s and stood
  // waiting, took the ball at 1.4 yd/s and pushed the quarterback sideways doing it; under center the quarterback stopped
  // 2 yd deep facing the defense and the ball went to a man a yard behind his back - Tools/HandoffCal.)
  Mesh m;
  m.ok = true;
  const Ent* rb = offRole(role);
  if (!rb || !qb) return m;
  const V2 S = rb->pos, Q0 = qb->pos;
  const bool under = Q0.x > los - 2;
  const double snapT = under ? 0.18 : 0.42;
  if (rb == motionEnt_ && cfg.offPlay->run && cfg.offPlay->run->jet) {
    // the jet sweep: he comes across flat behind the line at full speed and the quarterback puts it in his belly as he
    // goes by (under center he opens to him; from the gun he steps up to the path in front of him) - no stance, no
    // timing step: the motion was timed to it
    const double side = motionSide_ != 0 ? motionSide_ : -orIfZero(jsSign(S.y - by), 1);
    const double v = (std::min)(rb->vmax * 0.9, 7.6);
    const double pathX = under ? los - 2.0 : los - 2.5;
    m.dir = norm(V2{0.06, side});
    m.at = V2{pathX, by + side * 0.2};
    m.qbAt = V2{under ? pathX + 0.9 : pathX - 0.9, by};
    m.away = -side;
    m.tempo = clamp(v / (std::max)(1.0, rb->vmax), 0.5, 1.0);
    m.go = 0;
    const double tQb = snapT + 0.05 + coverTime(dist(Q0, m.qbAt), qb->vmax * 0.6, qb->vmax, (std::max)(0.3, qb->tau)) + 0.1;
    m.T = clamp(tQb, under ? 0.6 : 0.8, 1.2);
    return m;
  }
  const double backDepth = los - S.x;
  m.tempo = tune().meshTempo + (style == RunStyle::Stretch ? 0.08 : style == RunStyle::Draw ? -0.06 : 0.0);
  // (a stretch presses the edge at an angle, not flat down the line: the blocking is not a real outside zone's, and a
  // back sent the full width behind it lost yards two times in five and broke the rest - HandoffCal --plays)
  const V2 gate{los - 0.8, by + aimDy * (style == RunStyle::Stretch ? 0.6 : 1.0)};
  m.dir = norm(sub(gate, S));
  if (m.dir.x < 0.25) m.dir = norm(V2{0.25, m.dir.y});  // (he always gains ground)
  // a toss, not a handoff: the toss play from under center, and from the gun a stretch to the back's own side - he runs
  // away from the quarterback from his first step, and there is no path of his the quarterback can get the ball to
  bool gunToss = false;
  if (!under && style == RunStyle::Stretch) {
    const V2 nt{-m.dir.y, m.dir.x};
    gunToss = dist(Q0, add(add(S, scale(m.dir, 1.0)), scale(nt, jsSign(orIfZero(dot(sub(Q0, S), nt), 1)) * tune().meshArm))) > 1.7;
  }
  m.toss = gunToss || (under && style == RunStyle::Stretch && std::fabs(aimDy) >= 5.5 && backDepth > 4);
  if (m.toss) {
    // the toss: he turns (under center: reverses out) and tosses it underhand to a back already running flat for the
    // edge, who takes it on the move 3-4 yd outside him (it used to change hands in one tick across 3.8 yd)
    m.T = under ? (std::max)(0.5, (std::min)(nominalT, 0.7)) : snapT + 0.28;
    m.tossDur = under ? 0.3 : 0.28;
    m.go = under ? 0 : 0.3;
    double far = 0;
    // (how far along his path he is when it gets to him)
    for (double d = 0.25; d < 9; d += 0.25)
      if (m.go + coverTime(d, rb->vmax * m.tempo, rb->vmax, (std::max)(0.3, rb->tau)) <= m.T + m.tossDur) far = d;
    m.at = add(S, scale(m.dir, far));
    m.away = -jsSign(orIfZero(aimDy, 1));
    m.qbAt = under ? V2{Q0.x - 0.9, Q0.y - m.away * 0.5} : V2{Q0.x + 0.3, Q0.y - m.away * 0.5};
    return m;
  }
  // how far along it the ball is given
  double a;
  if (under) {
    // (a fullback 4 yd deep is a quick hitter: the quarterback turns and gives it a step off the line)
    const double deep = backDepth < 5.5 ? 2.2 : style == RunStyle::Stretch ? 4.2 : style == RunStyle::Draw ? 4.6 : style == RunStyle::Gap ? 4.0 : 3.8;
    const double depth = clamp((std::min)(deep, backDepth - 1.6), (los - Q0.x) + 0.9, (std::max)((los - Q0.x) + 0.9, backDepth - 0.8));
    a = (los - depth - S.x) / m.dir.x;
  } else {
    // where his path passes the quarterback, two steps from his stance at least (he takes it at a run)
    a = clamp(dot(sub(Q0, S), m.dir), 1.7, (std::max)(1.7, backDepth - 1.5));
    // (a back angling away from him is met sooner: the quarterback has two yards in him)
    const V2 nq{-m.dir.y, m.dir.x};
    while (a > 1.0 && dist(Q0, add(add(S, scale(m.dir, a)), scale(nq, jsSign(orIfZero(dot(sub(Q0, S), nq), 1)) * tune().meshArm))) > 1.7) a -= 0.1;
  }
  m.at = add(S, scale(m.dir, a));
  // the quarterback's side of the path: where he is now; on it, the side away from the run
  const V2 n0{-m.dir.y, m.dir.x};
  const double offPath = dot(sub(Q0, S), n0);
  const double qbSide = std::fabs(offPath) > 0.08 ? jsSign(offPath) : jsSign(n0.y) * -jsSign(orIfZero(aimDy, 1));
  const V2 n = scale(n0, qbSide);
  m.qbAt = add(m.at, scale(n, tune().meshArm));
  m.away = jsSign(orIfZero(n.y, -orIfZero(aimDy, 1)));
  // when: the later of the back running there and the quarterback bringing the ball (the snap in his hands, his steps,
  // a beat to seat it)
  const double bTau = (std::max)(0.3, rb->tau);
  const double tRun = coverTime(a, rb->vmax * m.tempo, rb->vmax, bTau);
  // (under center he is there a beat ahead of the back, settled, the ball out: he was still backing up at 3 yd/s as it
  // changed hands)
  const double tQb = snapT + 0.05 + coverTime(dist(Q0, m.qbAt), qb->vmax * 0.6, qb->vmax, (std::max)(0.3, qb->tau)) + (!under ? 0.12 : backDepth < 5.5 ? 0.05 : 0.19);
  // (a draw is a pass until it is not: the back sets as if to block and the quarterback shows his drop first)
  const double hold = style == RunStyle::Draw ? 0.4 : 0.0;
  m.T = clamp((std::max)(tRun + hold, tQb + hold * 0.5), under ? 0.75 : 0.85, 1.6);
  // the back is not left standing for the ball: beside the quarterback in the gun he holds his stance while the snap is
  // in the air (the timing step, 0.4 s), behind him he goes on the snap - and the time left over he takes out of his
  // first steps ("slow to it, fast through it": thinkIdle jogs him until it is time to burst)
  const double goMax = hold + (under || backDepth > (los - Q0.x) + 1.5 ? 0.08 : 0.42);
  m.go = (std::max)(0.0, (std::min)(m.T - tRun, goMax));
  return m;
}

// ------------------------------------------------------------------ snap / handoff
void PlaySim::updateSnap() {
  Ball& b = ball;
  const OffPlay& play = *cfg.offPlay;
  if (b.mode == BallMode::Snap) {
    const double snapT = qb->pos.x < los - 2 ? 0.42 : 0.18;
    const double f = (std::min)(1.0, t / snapT);
    b.pos = V3{los + (qb->pos.x - los) * f, by + (qb->pos.y - by) * f, 0.3 + std::sin(f * kPi) * 0.4 + f * 0.9};
    if (f >= 1) {
      b.mode = BallMode::Held;
      b.holder = qb;
      qb->hasBall = true;
      ev("snap", qb);
      if (play.type == PlayType::Kneel) {
        qb->state = EntState::Down;
        qb->anim = Anim::Kneel;
        endPlay("kneel");
        return;
      }
      if (play.type == PlayType::Spike) {
        endPlay("spike");
        return;
      }
      if (play.run && play.run->style == RunStyle::Sneak) {
        setCarrier(qb);
        runRead = true;
        runReadT = t;
      }
      // a designed quarterback run (QB power / counter / draw, the wildcat): he takes off with it a beat after the
      // catch (on the draw, once he has shown his drop)
      // (and on power and counter once his pullers are on their way: a ride fake to the back first)
      if (designedQbRun()) qbRunT_ = t + (play.run->style == RunStyle::Draw ? 0.55 : play.run->style == RunStyle::Gap && !isWildcatPlay() ? 0.4 : 0.12);
      // the zone read's end: he squeezes the back or stays home for the quarterback (decided once, at the snap)
      if (play.run && play.run->read == "end") endSqueeze_ = rng.chance(0.5);
    }
    return;
  }
  if (qbRunT_ >= 0 && t >= qbRunT_ && b.holder == qb && carrier != qb && b.mode == BallMode::Held) {
    qbRunT_ = -1;
    setCarrier(qb);
    runRead = true;
    runReadT = t;
  }
  // the reverse: the carrier hands it on to the man coming back the other way, behind the line
  if (revEnt_ && !reversed_ && handedOff && b.holder && b.holder == offRole(play.run->carrier) && b.mode == BallMode::Held &&
      t > meshDoneT + 0.3 && dist(b.holder->pos, revEnt_->pos) < 1.3 && b.holder->pos.x < los) {
    Ent* from = b.holder;
    reversed_ = true;
    from->hasBall = false;
    setCarrier(revEnt_);
    from->state = EntState::Convoy;
    const int s = slotOf(from);
    if (s >= 0 && !revEnt_->userControlled) setSlotEnt(s, revEnt_);
    ev("handoff", from, revEnt_, "reverse");
  }
  // the exchange at the mesh (PlaySim::mesh): the ball changes hands as the back comes by the quarterback at a run,
  // within reach of him and up to the mesh point on his path - not on a clock with the back wherever he had got to. A
  // late man (held up, knocked off his path) still gets it: within reach a third of a second on, within 2 yd after 0.6 s.
  // A toss leaves the quarterback's hands at its time and is in the air to the back ("toss" marks the release, the
  // defense reads run from it; "handoff" the catch).
  // the option read at the mesh (zone read / RPO): the quarterback keeps it, or pulls it and throws
  if (meshOn() && !handedOff && b.holder == qb && !readDone_ && !play.run->read.empty() && !mesh().toss && t >= mesh().T - 0.25) optionRead();
  if (meshOn() && !handedOff && b.holder == qb && !qbKept && !rpoThrow) {
    const Mesh& m = mesh();
    Ent* rb = offRole(play.run->carrier);
    bool give = false;
    // the defense reads run when it did - off the blocks and the back's first steps, at the play's own time, not off the
    // ball changing hands (the mesh is a quarter second later than the old exchange, and with the read waiting for it
    // one run in five went for 10+)
    if (!runRead && t >= (std::min)(play.run->handoffDelay, m.T)) {
      runRead = true;
      runReadT = t;
    }
    if (m.toss) {
      if (tossT0 < 0 && t >= m.T) {
        tossT0 = t;
        tossFrom = qb->pos;
        ev("toss", qb, rb);
        runRead = true;
        runReadT = t;
      }
      give = tossT0 >= 0 && t >= tossT0 + m.tossDur;
    } else {
      const double apart = dist(rb->pos, qb->pos);
      const double along = dot(sub(rb->pos, m.at), m.dir);
      // (and a back already by a quarterback who got there late has it reached out to him)
      give = (t >= m.T - 0.2 && apart < 1.3 && along > -0.2) || (along > 0.3 && apart < 1.7) || (t >= m.T + 0.35 && apart < 1.3) || (t >= m.T + 0.6 && apart < 2.0);
    }
    if (give) {
      handedOff = true;
      meshDoneT = t;
      b.holder = rb;
      qb->hasBall = false;
      setCarrier(rb);
      qb->state = EntState::Idle;
      // the human on the quarterback has the back the tick the ball is in his hands, the stick he is holding with him
      // (the director's switch came a frame later: a tick or two of the back on his own); a back another human has
      // already is left to him
      const int qbSlot = slotOf(qb);
      if (qbSlot >= 0 && rb && !rb->userControlled) setSlotEnt(qbSlot, rb);
      ev("handoff", qb, rb);
      if (!runRead) runReadT = t;
      runRead = true;
    }
  } else if (play.type == PlayType::Run && play.run && !handedOff && b.holder == qb && play.run->carrier != "QB" && !qbKept && !rpoThrow && !meshOn()) {
    Ent* rb = offRole(play.run->carrier);
    const bool pitch = play.run->style == RunStyle::Stretch && qb->pos.x > los - 2;
    // (the exchange at the mesh, the back at the quarterback's hip: at 2 yd the ball jumped across the gap between them;
    // a late back still gets it within 2 yd)
    const double meshD = pitch ? 5.5 : t < play.run->handoffDelay + 0.5 ? 1.25 : 2.0;
    if (t >= play.run->handoffDelay && dist(rb->pos, qb->pos) < meshD) {
      handedOff = true;
      b.holder = rb;
      qb->hasBall = false;
      setCarrier(rb);
      qb->state = EntState::Idle;
      ev("handoff", qb, rb);
      runRead = true;
      runReadT = t;
    }
  }
  if (play.type == PlayType::PlayAction && !handedOff && t > paPullT()) {
    handedOff = true;  // fake complete
    Ent* rb = offRole("RB");
    if (rb) rb->state = EntState::PassBlock;
  }
}

// ------------------------------------------------------------------ the expanded playbook's mechanics
bool PlaySim::isWildcatPlay() const {
  return form_ && form_->family == "Wildcat";
}

bool PlaySim::designedQbRun() const {
  const OffPlay* p = cfg.offPlay;
  return p && p->type == PlayType::Run && p->run && p->run->carrier == "QB" && p->run->style != RunStyle::Sneak && !cfg.kick;
}

const char* PlaySim::runJob(const Ent* e) const {
  auto it = jobs_.find(e);
  return it == jobs_.end() ? "" : it->second.job.c_str();
}

void PlaySim::optionRead() {
  readDone_ = true;
  const RunScheme& rs = *cfg.offPlay->run;
  // the read is right this often: a sharp quarterback all but always, a poor one one time in six wrong
  const double right = clamp(0.8 + (qb->r().decision - 50) * 0.003, 0.75, 0.96);
  const bool correct = rng.chance(right);
  if (rs.read == "end") {
    // the zone read: the end squeezing the back is a give to nobody - the quarterback pulls it and runs where the end
    // was; an end staying home for him is a handoff
    if (!readEnt) return;
    const bool keep = endSqueeze_ ? correct : !correct;
    if (!keep) return;
    qbKept = true;
    const double outside = orIfZero(jsSign(readEnt->pos.y - by), -jsSign(orIfZero(runAimDy(), 1)));
    keepAim_ = clamp(readEnt->pos.y + outside * 3.0, 3.0, FIELD_W - 3.0);
    setCarrier(qb);
    runRead = true;
    runReadT = t;
    ev("keep", qb, readEnt);
    return;
  }
  if (rs.read == "rpo" && rpoRecv_ && rpoRecv_->state == EntState::Route) {
    // the RPO: the quarterback counts the conflict defender - nobody within reach of where the quick route will be
    // caught (the overhang man has stepped into the box for the run) is a throw; covered, the ball goes to the back
    const V2 at = predictPos(rpoRecv_, 0.55);
    Ent* near = nullptr;
    double nd = 1e9;
    for (Ent* d : def) {
      if (d->state == EntState::Rush || d->isDL || d->deepRole) continue;
      const double dd = dist(d->pos, at);
      if (dd < nd) {
        nd = dd;
        near = d;
      }
    }
    readEnt = near;
    const bool open = nd > 4.0;
    if (!(open ? correct : !correct)) return;
    rpoThrow = true;
    if (Ent* rb = offRole(rs.carrier)) rb->state = EntState::RunBlock;  // (he carries out the fake into the line)
    throwPath = 3;
    startThrow(qb, rpoRecv_, false);
    ev("rpo", qb, rpoRecv_);
  }
}

Ent* PlaySim::jobMan(const std::string& who) const {
  const double dir = jsSign(orIfZero(runAimDy(), 0.001));  // +1: the run goes to +y
  auto sideOf = [&](const char* a, const char* b2, bool play) -> Ent* {
    Ent* x = offRole(a);
    Ent* y = offRole(b2);
    if (!x || !y) return x ? x : y;
    const bool xPlay = (x->pos.y - by) * dir > 0;
    return xPlay == play ? x : y;
  };
  if (who == "PG") return sideOf("LG", "RG", true);
  if (who == "BG") return sideOf("LG", "RG", false);
  if (who == "PT") return sideOf("LT", "RT", true);
  if (who == "BT") return sideOf("LT", "RT", false);
  if (who == "PTE" || who == "BTE") {
    // the tight end attached on that side
    for (Ent* e : off)
      if (isTERole(e->role) && std::fabs(e->pos.y - by) < 6.5 && ((e->pos.y - by) * dir > 0) == (who == "PTE")) return e;
    return nullptr;
  }
  if (who == "H") {
    // the H-back: a tight end off the line or in the backfield, else the fullback
    for (Ent* e : off)
      if (isTERole(e->role) && e->pos.x < los - 1.1 && std::fabs(e->pos.y - by) < 6.5) return e;
    return offRole("FB");
  }
  return offRole(who);
}

void PlaySim::resolveRunJobs(std::vector<Ent*>& usedB, std::vector<Ent*>& usedD) {
  const RunScheme& rs = *cfg.offPlay->run;
  const double aim = runAimDy();
  const double dir = jsSign(orIfZero(aim, 0.001));
  const double holeY = by + aim;
  const Ent* ballMan = offRole(rs.carrier);
  auto onLine = [&](const Ent* d) { return d->pos.x < los + 2.3 && d->state != EntState::Down; };
  auto isFree = [&](const Ent* d) { return !contains(usedD, const_cast<Ent*>(d)); };
  // the zone read's end: left unblocked, read by the quarterback
  if (rs.read == "end") {
    Ent* bt = jobMan("BT");
    Ent* best = nullptr;
    double bw = -1;
    for (Ent* d : def) {
      if (!onLine(d) || (d->pos.y - by) * dir >= 0 || (bt && std::fabs(d->pos.y - bt->pos.y) > 4.5)) continue;
      const double w = std::fabs(d->pos.y - by);
      if (w > bw) {
        bw = w;
        best = d;
      }
    }
    readEnt = best;
    if (best) usedD.push_back(best);
  }
  // (no H-back to kick out the end: the back-side guard's pull is the kick-out, as gun power is run without one)
  std::vector<BlockJob> jobs = rs.jobs;
  for (const BlockJob& j : rs.jobs)
    if (j.job == "kick" && !jobMan(j.who))
      for (BlockJob& w : jobs)
        if (w.job == "wrap") {
          w.job = "kick";
          break;
        }
  for (const BlockJob& j : jobs) {
    if (tune().runJobs <= 0) break;
    Ent* m = jobMan(j.who);
    if (!m || m == ballMan || m == qb || contains(usedB, m) || m->state != EntState::RunBlock) continue;
    RunJob rj;
    rj.job = j.job;
    rj.pulled = m->isOL || (isTERole(m->role) && j.job != "lead");
    // the gap scheme's rule for a puller: covered, he blocks the man on him (a lineman leaving a down lineman head up on
    // him to pull lets that man straight into the backfield); uncovered, he pulls
    if (m->isOL) {
      bool covered = false;
      for (const Ent* d : def)
        if (d->isDL && d->pos.x < los + 2.0 && std::fabs(d->pos.y - m->pos.y) < 0.75 && !contains(usedD, const_cast<Ent*>(d))) covered = true;
      if (covered) continue;
    }
    Ent* tgt = nullptr;
    double bs = 1e9;
    for (Ent* d : def) {
      if (!isFree(d) || d->state == EntState::Down) continue;
      const double out = (d->pos.y - by) * dir;  // how far to the play side
      double s = 1e9;
      if (j.job == "kick") {
        // the end man on the line at the point of attack (the first man outside the down blocks)
        if (onLine(d) && out >= std::fabs(aim) - 1.2) s = out;
      } else if (j.job == "trap") {
        // the first down lineman past the center on the play side
        if (onLine(d) && out > 0.2) s = out;
      } else if (j.job == "arc") {
        // the back-side end
        if (onLine(d) && out < -1.0) s = out;  // (most negative = widest)
      } else {
        // wrap / lead: the linebacker the hole leads to (outside runs: the first man to the edge)
        const V2 hole{los + 3.5, holeY + (std::fabs(aim) > 4 ? dir * 2.0 : 0.0)};
        if (!d->isDL && (!onLine(d) || std::fabs(aim) > 4) && d->pos.x < los + 9) s = dist(d->pos, hole);
      }
      if (s < bs) {
        bs = s;
        tgt = d;
      }
    }
    m->blockTarget = tgt;
    usedB.push_back(m);
    if (tgt) usedD.push_back(tgt);
    if (j.job == "kick" || j.job == "trap") rj.toY = tgt ? tgt->pos.y - dir * 0.7 : holeY;
    else if (j.job == "arc") rj.toY = tgt ? tgt->pos.y + dir * 0.6 : by - dir * 4.0;
    else rj.toY = holeY;
    jobs_[m] = rj;
  }
}

bool PlaySim::thinkRunJob(Ent* e) {
  auto it = jobs_.find(e);
  if (it == jobs_.end() || t > 2.6) return false;
  RunJob& j = it->second;
  Ent* tg = e->blockTarget;
  // his man gone (blocked by someone else, on the ground, past the ball): the line's rules again
  if (!tg || tg->state == EntState::Down || !tg->engagedBy.empty() || tg->stun > 0) {
    jobs_.erase(it);
    return false;
  }
  const double dir = jsSign(orIfZero(runAimDy(), 0.001));
  if (!j.turned) {
    // the pull: a drop step and flat down the line behind it, to the hole
    if (j.pulled && std::fabs(j.toY - e->pos.y) > 1.0 && t < 1.5) {
      const V2 to{los - 1.3, j.toY};
      e->want = scale(norm(sub(to, e->pos)), e->vmax);
      return true;
    }
    j.turned = true;
  }
  // up through the hole at his man: a kick-out from the inside, a wrap / lead / arc on the man's near side
  const V2 at = interceptPoint(e->pos, tg->pos, tg->vel, e->vmax, 0.6);
  const double inside = j.job == "arc" ? dir : -dir;
  const V2 contact{at.x - 0.3, at.y + inside * 0.45};
  e->want = scale(norm(sub(contact, e->pos)), e->vmax);
  if (dist(tg->pos, e->pos) < e->radius + tg->radius + 0.3) {
    engage(e, tg);
    // the angle is the block's point: a kick-out or a trap hits a man from the side he is not looking at, a puller or
    // lead back arrives with a head of steam (skill points on the blocker's side of the rep; the engage roll is as
    // before)
    if (e->engaged == tg) e->engageEdge += (j.job == "kick" || j.job == "trap" ? tune().jobEdge : j.job == "arc" ? tune().jobEdge * 0.75 : tune().jobEdge * 0.5);
  }
  return true;
}

void PlaySim::jetSpot(Ent* e, V2& at, V2& vel) const {
  // the jet man is snapped to meet the quarterback at the mesh at his speed: the mesh a yard off the ball's line, the
  // quarterback a step from it (under center he opens to it; from the gun he steps up to a path in front of him)
  const bool under = qb->pos.x > los - 2;
  const double v = (std::min)(e->vmax * 0.9, 7.6);
  const double pathX = under ? los - 2.0 : los - 2.5;
  const V2 qbAt{under ? pathX + 0.9 : pathX - 0.9, by};
  const double snapT = under ? 0.18 : 0.42;
  const double tQb = snapT + 0.05 + coverTime(dist(qb->pos, qbAt), qb->vmax * 0.6, qb->vmax, (std::max)(0.3, qb->tau)) + 0.1;
  const double T = clamp(tQb, under ? 0.6 : 0.8, 1.2);
  const double side = motionSide_ != 0 ? motionSide_ : -orIfZero(jsSign(e->pos.y - by), 1);
  at = V2{pathX, by + side * 0.2 - side * v * T};
  vel = V2{0, side * v};
}

void PlaySim::startPlayMotion() {
  Ent* e = motionEnt_;
  if (!e || motionState_ != 0) return;
  const PlayMotion& mo = *cfg.offPlay->motion;
  V2 to, v{0, 0};
  if (mo.jet) jetSpot(e, to, v);
  else to = V2{(std::min)(e->pos.x, los - 1.2), clamp(e->pos.y + mo.dy * (cfg.flip ? -1.0 : 1.0), 3, FIELD_W - 3)};
  motionTo_ = to;
  motionVel_ = v;
  const double dy = to.y - e->pos.y;
  motionPreSpeed_ = mo.jet ? clamp(dist(to, e->pos) / 0.9, 4.5, (std::max)(4.5, len(v))) : 0.0;
  moveTo(e, to);
  // a man defender goes across with him (motion shows man coverage)
  for (Ent* d : def) {
    if (d->state != EntState::Man || d->man != e) continue;
    const V2 dp = d->shiftTo ? *d->shiftTo : d->pos;
    moveTo(d, V2{dp.x, clamp(dp.y + dy * 0.85, 1, FIELD_W - 1)});
  }
  if (e->state == EntState::Route && !e->routeName.empty()) assignRoute(e, e->routeName);
  motionState_ = 1;
}

void PlaySim::snapPlayMotion() {
  Ent* e = motionEnt_;
  if (!e) return;
  if (motionState_ == 0) startPlayMotion();
  // (snapped without the walk - the headless sim - or before he got there: he is at his spot at the snap)
  if (e->shiftTo) {
    e->pos = *e->shiftTo;
    e->shiftTo.reset();
  }
  for (Ent* d : def)
    if (d->shiftTo && d->state == EntState::Man && d->man == e) {
      d->pos = *d->shiftTo;
      d->shiftTo.reset();
    }
  if (e->state == EntState::Route && !e->routeName.empty()) assignRoute(e, e->routeName);
  motionState_ = 2;
  adjusted_ = true;
}

void PlaySim::setCarrier(Ent* e) {
  carrier = e;
  // (abilities: Home Run Hitter / Slipstream find another gear with the ball)
  if (e->fx.topSpeed > 0 && !e->fastBall) {
    e->vmax *= 1 + e->fx.topSpeed;
    e->fastBall = true;
  }
  e->hasBall = true;
  e->state = EntState::Carrier;
  e->anim = Anim::Run;
  ball.holder = e;
  ball.mode = BallMode::Held;
  // teammates become lead blockers, opponents (if they were blocking/running routes) chase
  for (Ent* o : teammatesOf(e)) {
    if (o == e || (o == revEnt_ && !reversed_)) continue;  // (a reverse's second man still has his exchange to make)
    if (stateIn(o->state, {EntState::Route, EntState::Idle, EntState::QB, EntState::Man, EntState::Zone, EntState::Rush,
                           EntState::Pursue, EntState::Spy}))
      o->state = EntState::Convoy;
  }
  for (Ent* o : defendersOf(e)) {
    if (stateIn(o->state, {EntState::Convoy, EntState::Route, EntState::Idle, EntState::QB, EntState::PassBlock,
                           EntState::RunBlock, EntState::Carrier})) {
      if (o->engaged) disengage(o);
      o->state = EntState::Pursue;
    }
  }
}

// ------------------------------------------------------------------ AI
void PlaySim::think(Ent* e, double dt) {
  e->stickDrove = false;  // (applyUser says so when his stick drives this step: steerVel's player steering)
  if (wrap_ && (e == wrap_->c || e == wrap_->d)) {
    e->want = V2{0, 0};
    return;
  }
  if (e->state == EntState::Down) {
    e->want = V2{0, 0};
    return;
  }
  if (e->looseDiveT >= 0) {
    // in the air after a loose ball: he goes where he dived, at the speed he left his feet with
    e->want = e->looseDiveV;
    return;
  }
  if (e->stun > 0) {
    e->want = scale(e->vel, 0.5);
    return;
  }
  // a kick in the air: the return play's own rules drive everyone (dynamic kickoff: the coverage, the user's man
  // included, may not move until the ball is caught; a punt's coverage releases half a second after the kick and
  // the user's man is his own from then)
  if (cfg.kick && (!kicked || ball.mode == BallMode::Air) &&
      !(e->userControlled && kicked && e->side == Side::Def && cfg.kick->punt && t > cfg.kick->kickT + 0.5)) {
    if (thinkReturn(e)) return;
  }
  if (cfg.kick && e == returner && carrier == e && thinkDownIt(e)) {
    e->want = V2{0, 0};
    return;
  }
  // a field goal / extra point: the operation, the protection and the rush (Sim/PlaceKick.cpp), until a block puts the
  // ball on the ground (then it is everyone's, as a fumble is)
  if (cfg.place && thinkPlace(e)) return;
  // the run exchange is the play's, not the stick's: a human on the quarterback or on the back is carried through the
  // mesh (the stick could walk the quarterback away from his back and the ball was never handed off), and has the
  // back the tick the ball is in his hands. Let go again if the exchange has broken down a second past its time.
  const bool meshLock = e->userControlled && e->side == Side::Off && meshOn() && !handedOff && (e == qb || e->role == cfg.offPlay->run->carrier) &&
                        t < mesh().T + 1.0;
  if (e->userControlled && !meshLock && applyUser(e)) return;
  if (e->side == Side::Def && t < e->getOff) {
    e->want = V2{0, 0};
    return;
  }
  if (e->side == Side::Off && thinkPaBack(e)) return;
  switch (e->state) {
    case EntState::QB: thinkQB(e, dt); return;
    case EntState::Route: thinkRoute(e); return;
    case EntState::PassBlock: thinkPassBlock(e); return;
    case EntState::RunBlock: thinkRunBlock(e); return;
    case EntState::Rush: thinkRush(e); return;
    case EntState::Man: thinkMan(e); return;
    case EntState::Zone: thinkZone(e); return;
    case EntState::Carrier: thinkCarrier(e); return;
    case EntState::Pursue: thinkPursue(e); return;
    case EntState::Convoy: thinkConvoy(e); return;
    case EntState::Idle: thinkIdle(e); return;
    case EntState::Spy: thinkSpy(e); return;
    default: e->want = V2{0, 0};
  }
}

bool PlaySim::applyUser(Ent* e) {
  UserInput& i = inputOf(e);
  const V2 m = i.move;
  const double ml = len(m);
  // QB throw
  if (e == qb && ball.holder == e && !i.throwTo.empty() && !passThrown) {
    Ent* tgt = offRole(i.throwTo);
    i.throwTo.clear();
    if (tgt && tgt->eligible && tgt->state != EntState::Down) {
      if (e->pos.x <= los + 0.2) {
        // (a shovel goes off quicker: no windup)
        // (close now and still close as it goes: a back running past him on a route is not a shovel)
        const bool shov = dist(tgt->pos, e->pos) < 6.0 && dist(predictPos(tgt, 0.5), e->pos) < 6.5;
        pendingThrow = PendingThrow{tgt, i.lob || i.passStyle == 1, t + releaseTime(e) * (shov ? 0.5 : 0.8), false, i.passStyle, shov};
        e->anim = Anim::Throw;
        e->animT = 0;
      }
    }
  }
  // a pump fake: the arm comes through and the ball stays; the men in coverage over the play bite on it for a beat
  if (e == qb && ball.holder == e && i.pumpFake && !passThrown && !pendingThrow) {
    ev("pump", e);
    for (Ent* d : def)
      if ((d->state == EntState::Zone || d->state == EntState::Man) && dist(d->pos, e->pos) < 22 && d->stun <= 0) d->stun = 0.25 + (99 - d->r().playRec) * 0.004;
  }
  i.pumpFake = false;
  if (e == qb && ball.holder == e && i.throwAway && !passThrown && !pendingThrow && e->pos.x <= los + 0.2) {
    pendingThrow = PendingThrow{nullptr, false, t + releaseTime(e) * 0.8, true};
    taWhy_ = 1;
    e->anim = Anim::Throw;
    e->animT = 0;
  }
  i.throwAway = false;
  if (e == qb && ball.holder == e && e->pos.x > los + 0.5 && e->state == EntState::QB) {
    scramble = true, scrambleT = t;
    setCarrier(e);
  }
  // R1: the ball carrier covers up (both arms on the ball: no moves while he holds it)
  e->protecting = e->hasBall && i.protectBall && ball.mode == BallMode::Held && e->anim != Anim::Throw;
  // L2: the defender strafes (square to the line, eyes on the play)
  e->strafing = e->side == Side::Def && i.strafe && !e->hasBall;
  // the highlight stick (a move started here plays out under the AI or the stick alike)
  if (i.stickMove != Move::None) {
    const Move mv = i.stickMove;
    i.stickMove = Move::None;
    if (!e->protecting || mv == Move::Hit || mv == Move::Cut) startMove(e, mv, i.stickSide);
  }
  if (ml < 0.05) {
    // no input: let AI drive non-QB entities, QB just holds position
    if (e == qb && e->state == EntState::QB) {
      if (!dropDone) return false;
      e->want = scale(e->vel, 0.6);
      return true;
    }
    return false;
  }
  // (covered up he runs a step slower, no sprinting the ball loose; strafing, a shuffle and a backpedal top out well
  // short of a sprint - the price of keeping his shoulders square)
  const double cap = e->protecting ? 0.93 : e->strafing ? 0.7 : 1.0;
  const double sp = e->vmax * (i.sprint && !e->strafing ? 1 : 0.82) * (std::min)(1.0, ml) * cap;
  e->want = scale(norm(m), (std::min)(sp, e->vmax * cap));
  e->stickDrove = true;
  if (i.juke && e->hasBall && e->animT > 0.6 && !e->protecting) {
    i.juke = false;
    // juke: sharp lateral impulse with brief speed loss, to the side the stick leans
    const V2 lat{-e->vel.y, e->vel.x};
    startMove(e, Move::Juke, dot(norm(lat), m) >= 0 ? 1 : -1);
  }
  return true;
}

bool PlaySim::startMove(Ent* e, Move m, double side) {
  if (!e || m == Move::None || e->state == EntState::Down || e->engaged || e->stun > 0 || (wrap_ && (e == wrap_->c || e == wrap_->d)))
    return false;
  const bool carrierMove = m != Move::Hit && m != Move::Cut;
  Ent* c = ball.holder;
  if (carrierMove) {
    if (!e->hasBall || ball.mode != BallMode::Held || e->animT <= 0.6 || e->anim == Anim::Throw) return false;
  } else {
    // a tackling move: a free defender with a carrier to hit, not one already mid-move
    if (!c || ball.mode != BallMode::Held || c->side == e->side || e->moveT < 0.9 || e->tackleCd > 0) return false;
  }
  const double s0 = e->speed();
  const V2 fwd = s0 > 0.5 ? norm(e->vel) : V2{goalDir(e), 0};
  const V2 lat{-fwd.y, fwd.x};  // his left
  const Ent* threat = nullptr;
  if (carrierMove) suggestMove(e, &threat);
  if (side == 0) {
    if (threat) {
      const double ty = dot(sub(threat->pos, e->pos), lat);  // + = the tackler is to his left
      side = m == Move::StiffArm ? (ty >= 0 ? 1 : -1) : (ty >= 0 ? -1 : 1);  // the arm toward him, a cut away
    } else side = dot(lat, e->want) >= 0 ? 1 : -1;
  }
  const double agi = 0.7 + e->r().agility / 330;
  switch (m) {
    case Move::Juke:  // a sharp cut: the hips stay, the feet go
      impulse(e, sub(add(scale(e->vel, 0.55), scale(lat, side * 3.2 * agi)), e->vel), 0.4);
      break;
    case Move::Spin:  // a full turn out of the arms: gives up speed, slides a step over
      impulse(e, sub(add(scale(e->vel, 0.76), scale(lat, side * 1.2 * agi)), e->vel), 0.45);
      break;
    case Move::Truck:  // the shoulder down: a short gather, straight ahead
      impulse(e, scale(e->vel, -0.1), 0.3);
      break;
    case Move::StiffArm:  // the arm out: he keeps his line and leans away from the man
      impulse(e, sub(add(scale(e->vel, 0.93), scale(lat, -side * 0.6)), e->vel), 0.3);
      break;
    case Move::Hurdle:  // a leap: no cutting in the air
      impulse(e, scale(e->vel, -0.15), 0.35);
      break;
    case Move::Hit:
    case Move::Cut: {
      // the lunge: at where the carrier will be, a burst past running speed (a missed lunge is a long way down)
      const V2 aim = add(c->pos, scale(c->vel, 0.25));
      const V2 dir = norm(sub(aim, e->pos));
      const double burst = m == Move::Hit ? 2.6 : 1.6;
      impulse(e, sub(scale(dir, (std::max)(s0, e->vmax * 0.8) + burst), e->vel), 0.25);
      break;
    }
    case Move::None: break;
  }
  e->move = m;
  e->moveT = 0;
  e->moveSide = side;
  if (carrierMove) {
    e->anim = Anim::Juke;  // the renderer plays the move's clip (moveName) for it
    e->animT = 0;
  } else {
    e->anim = m == Move::Cut ? Anim::Dive : Anim::Tackle;
    e->animT = 0;
  }
  ev("move", e, const_cast<Ent*>(carrierMove ? threat : c), moveName(m));
  return true;
}

Move PlaySim::suggestMove(const Ent* c, const Ent** threat) const {
  if (threat) *threat = nullptr;
  if (!c || !c->hasBall) return Move::None;
  const double s0 = c->speed();
  const V2 fwd = s0 > 0.5 ? norm(c->vel) : V2{goalDir(c), 0};
  const V2 lat{-fwd.y, fwd.x};
  const Ent* best = nullptr;
  double bd = 3.6;
  for (const Ent* d : defendersOf(c)) {
    if (!d->free()) continue;
    const V2 rel = sub(d->pos, c->pos);
    const double dd = len(rel);
    const double ahead = dot(rel, fwd);
    // closing on him: in front, or beside and coming; not one he has already run past
    const double closing = dot(sub(d->vel, c->vel), scale(rel, -1 / (std::max)(0.1, dd)));
    if (ahead < -0.6 || (ahead < 0.8 && closing < 0.5)) continue;
    if (dd < bd) {
      bd = dd;
      best = d;
    }
  }
  if (!best) return Move::None;
  if (threat) *threat = best;
  const V2 rel = sub(best->pos, c->pos);
  const double facing = dot(norm(rel), fwd);
  const Ratings& r = c->r();
  const double power = r.breakTackle * 0.5 + r.strength * 0.5;
  const double wiggle = r.elusiveness * 0.5 + r.agility * 0.5;
  if (best->anim == Anim::Dive || best->moveOn(Move::Cut)) return Move::Hurdle;
  if (facing < 0.55 && std::fabs(dot(rel, lat)) > 0.5) return Move::StiffArm;  // he comes from the side
  if (power > wiggle + 4) return Move::Truck;
  return wiggle > 78 && bd < 2.0 ? Move::Spin : Move::Juke;
}

double PlaySim::moveTackleAdj(const Ent* d, const Ent* c, double facing, bool arm, bool dive) const {
  const Ratings& cr = c->r();
  const Ratings& dr = d->r();
  const double k = c->userControlled ? 1.0 : tune().aiMoveK;
  double adj = 0;
  // the carrier's moves (these lower the tackle odds; the wrong one for the man raises them)
  if (c->moveOn(Move::Spin) && facing > -0.3) adj -= (0.9 + (cr.agility * 0.5 + cr.elusiveness * 0.5 - 70) * 0.025) * k;
  if (c->moveOn(Move::Truck) && facing > 0.2) {
    // power against mass: a back not built for it bounces off, a big man runs a corner over
    const double tr = 0.8 + (cr.breakTackle * 0.5 + cr.strength * 0.5 - 70) / 12 + std::log(c->mass / d->mass) * 1.6;
    adj -= clamp(tr, -1.0, 2.6) * k;
  }
  if (c->moveOn(Move::StiffArm) && facing > -0.4 && facing < 0.85)
    adj -= (0.7 + (cr.strength - 70) / 15 + (cr.breakTackle - 70) / 20 + (arm || dive ? 0.5 : 0)) * k;
  if (c->moveOn(Move::Hurdle)) adj += (dive || d->moveOn(Move::Cut)) ? -2.2 * k : facing > 0.3 ? 0.6 : 0;  // hit in the air
  // the defender's
  if (d->moveOn(Move::Hit)) {
    const double vd = d->speed() / (std::max)(1.0, d->vmax);
    adj += facing > -0.3 ? 1.0 + (dr.tackling * 0.5 + dr.strength * 0.5 - 70) / 20 + vd * 0.5 : -0.6;  // from behind: a shove
    if (c->moveOn(Move::Truck)) adj += (dr.strength - cr.strength) / 25;  // two men meeting square: the stronger wins
  }
  if (d->moveOn(Move::Cut) && !c->moveOn(Move::Hurdle)) adj += c->moveOn(Move::Truck) ? 1.2 : 0.5;  // under the shoulder
  return adj;
}

void PlaySim::thinkIdle(Ent* e) {
  const OffPlay& play = *cfg.offPlay;
  if (meshOn() && play.run->carrier == e->role && !handedOff) {
    // the back to the mesh (PlaySim::mesh): in his stance until it is time, then down his path at his tempo and on
    // through the mesh point - he takes the ball in stride. Only a back who has gone by without it (the quarterback
    // was held up) comes back for it.
    const Mesh& m = mesh();
    if (t < m.go) {
      e->want = V2{0, 0};
      return;
    }
    const double along = dot(sub(e->pos, m.at), m.dir);
    if (!m.toss && along > 0.8 && t > m.T + 0.3) {
      const V2 back = add(qb->pos, scale(m.dir, 0.6));
      e->want = scale(norm(sub(back, e->pos)), e->vmax * 0.45);
      return;
    }
    // slow to the mesh, fast through it: his first steps at the pace that has him there with the ball (a jog at most, a
    // walk at least) while the quarterback brings it, the burst timed so that he is at the mesh point at his tempo
    // (latched: once he goes he goes)
    const double vT = e->vmax * m.tempo;
    // (a jet man is at full speed from the snap: no stance, no timing step)
    if (!meshBurst) meshBurst = m.toss || (play.run->jet && e == motionEnt_) || along > -0.3 || m.T - t <= -along / (std::max)(1.0, 0.5 * (e->speed() + vT)) + 0.15;
    const double pace = clamp(-along / (std::max)(0.05, m.T - t), e->vmax * 0.22, e->vmax * 0.42);
    // (steered at a point a yard and a half on down the line from where he is abreast of: he holds the line)
    const V2 ahead = add(m.at, scale(m.dir, along + 1.5));
    e->want = scale(norm(sub(ahead, e->pos)), meshBurst ? vT : pace);
    return;
  }
  if (e == qb && handedOff && meshOn() && meshDoneT >= 0) {
    // the quarterback carries out his fake: a boot away from the run on zone and stretch plays (and after a toss), a
    // few steps of a drop on gap runs and draws - then he pulls up and watches. (He stood where he had handed it off.)
    const Mesh& m = mesh();
    const double since = t - meshDoneT;
    const bool boot = play.run->style == RunStyle::Zone || play.run->style == RunStyle::Stretch;
    const V2 to = boot ? V2{m.qbAt.x - 2.2, m.qbAt.y + m.away * 4.5} : V2{m.qbAt.x - 2.6, m.qbAt.y + m.away * 0.7};
    const double d = dist(e->pos, to);
    // (his hands ride the ball into the back's belly for a beat before he goes)
    e->want = since > 0.18 && since < (boot ? 1.5 : 1.2) && d > 0.3 ? scale(norm(sub(to, e->pos)), (std::min)(e->vmax * (boot ? 0.5 : 0.42), d * 3 + 0.5)) : V2{0, 0};
    return;
  }
  if (e == revEnt_ && !reversed_) {
    // the reverse: he comes back behind the line against the flow, to meet the carrier where the sweep turns
    // (he goes once the back has it: crossing the backfield before the exchange he ran through the mesh)
    if (t < 0.35 || (!handedOff && t < 2.0)) {
      e->want = V2{0, 0};
      return;
    }
    // (across the backfield three yards deep, from his side to the far one: he takes it going the way he will run)
    const double away = -jsSign(orIfZero(runAimDy(), 1));
    const V2 to{los - 3.2, by + away * 12.0};
    e->want = scale(norm(sub(to, e->pos)), e->vmax * 0.85);
    return;
  }
  if (play.run && play.run->carrier == e->role && !handedOff) {
    // path to mesh point then aiming point
    const RunScheme& run = *play.run;
    const double aim = by + runAimDy();
    const V2 q = qb->pos;
    const V2 mesh{q.x - 0.2, q.y + jsSign(orIfZero(aim - q.y, 1)) * 0.9};
    const V2 tgt = t < run.handoffDelay - 0.15 ? mesh : V2{q.x + 0.3, q.y + jsSign(orIfZero(aim - q.y, 1)) * 0.4};
    const bool draw = run.style == RunStyle::Draw && t < 0.4;
    e->want = draw ? V2{0, 0} : scale(norm(sub(tgt, e->pos)), e->vmax * (run.style == RunStyle::Stretch ? 0.8 : 0.55));
    return;
  }
  e->want = V2{0, 0};
}

void PlaySim::thinkQB(Ent* e, double dt) {
  const OffPlay& play = *cfg.offPlay;
  const double L = los;
  if (ball.holder != e) {
    e->want = V2{0, 0};
    return;
  }
  if (play.type == PlayType::Run && meshOn()) {
    // to his side of the mesh with the ball (PlaySim::mesh), there before the back and still as he comes by
    // (and he rides it: as the back comes through he gives a step with him, the ball at his belly the whole way)
    const Mesh& m = mesh();
    const Ent* rb = offRole(play.run->carrier);
    const double ride = rb && !m.toss ? clamp(dot(sub(rb->pos, m.at), m.dir), -0.15, 0.5) : 0.0;
    const V2 to = add(m.qbAt, scale(m.dir, ride));
    const double d = dist(e->pos, to);
    e->want = d < 0.1 ? V2{0, 0} : scale(norm(sub(to, e->pos)), (std::min)(e->vmax * 0.6, d * 6 + 0.5));
    return;
  }
  if (play.type == PlayType::Run) {
    // ride the mesh then carry out fake
    const RunScheme& run = *play.run;
    const double aim = by + runAimDy();
    const double dir = jsSign(orIfZero(aim - by, 1));
    // a designed quarterback run before he takes off: the draw's drop steps; on power and counter he rides the fake
    // with the back a step toward the hole, so he has his feet moving when he goes
    if (designedQbRun() && qbRunT_ >= 0) {
      e->want = run.style == RunStyle::Draw ? V2{0, 0} : V2{0.9, dir * 1.6};
      return;
    }
    e->want = t < run.handoffDelay * 0.6 ? V2{-1.5, dir * 0.6} : V2{0, 0};
    return;
  }
  // pass play: dropback
  // the drop: a play's own depth, else 2 yd from the gun (a quick three step, setting up ~7 deep) and 5 yd from under
  // center (between a three and a five step: 5 to 7 yd deep in the real game; 3 yd was a half drop)
  // (play action off the run's own mesh - PlaySim::paMesh - leaves him 4 yd deep and over the ball at the pull, where the
  // old sideways fake left him 2.5 deep and 2 yd wide: he was set 0.3 s sooner and sacked a fifth less often. The drop
  // behind the fake is 15 % deeper for it, 7-8 yd as play action is thrown from, and the pass is timed as it was:
  // 3 seeds x 200 games, sacks 2.23 -> 2.08, ypa 7.15 -> 7.14, points 22.2 -> 22.3.)
  const double paDeep = play.type == PlayType::PlayAction ? tune().paDrop * (tune().mesh > 0 ? 1.15 : 1.0) : 1.0;
  const double dropX = L - form_->qbDepth - play.dropDepth.value_or(form_->qbDepth <= 1.5 ? 5.0 : 2.0) * paDeep;
  const bool paFake = play.type == PlayType::PlayAction && t < paPullT();
  if (paFake && tune().mesh > 0 && offRole("RB")) {
    // the fake is the run's exchange up to the ball leaving his hands: to his side of the same mesh (PlaySim::paMesh)
    const Mesh& m = paMesh();
    const double d = dist(e->pos, m.qbAt);
    e->want = d < 0.1 ? V2{0, 0} : scale(norm(sub(m.qbAt, e->pos)), (std::min)(e->vmax * 0.6, d * 6 + 0.5));
    return;
  }
  if (paFake) {
    e->want = V2{-1.2, paSide() * 1.5};
    return;
  }
  if (!dropDone) {
    const V2 to{paBoot() ? L - 6.0 : dropX, qbSetY()};
    const double d = dist(e->pos, to);
    if (d < 0.35 || e->pos.x < dropX) {
      dropDone = true;
      dropDoneT = t;
      readT = 0;
      readIdx = 0;
    } else {
      e->want = scale(norm(sub(to, e->pos)), (std::min)(e->vmax * 0.5, d * 4));
      e->face = 0;
      return;
    }
  }
  if (pendingThrow) {
    e->want = scale(e->vel, 0.3);
    return;
  }
  if (e->userControlled) return;  // human decides when to throw
  // ---------------- read progression
  readT += dt;
  std::vector<Ent*> reads;
  for (const std::string& r : readOrder) {
    Ent* x = offRole(r);
    if (x && x->eligible) reads.push_back(x);
  }
  const double tPocket = t;
  const double pressure = pressureOn(e);
  // (filming aid: scrambleK above 50 sends every quarterback out of the pocket a second after his drop)
  if (tune().scrambleK > 50 && dropDone && t - dropDoneT > 1.0 && !pendingThrow && !e->userControlled) {
    scramble = true, scrambleT = t;
    setCarrier(e);
    ev("scramble", e);
    return;
  }
  // a pressure as charted (PFF/NGS): a free rusher bearing down on him (~3.6 yd), a throw he is forced into, or a sack
  // (not on a screen: there the rush is let through on purpose, and charters do not count it)
  const bool charted = play.type != PlayType::Screen;
  if (pressure > 0.3 && charted) {
    if (!pressured) notePressure(e, "near");
    pressured = true;
  }
  const double dec = e->r().decision;
  // (play action is a defined high-low read off the fake: he comes off each man quicker than on a full progression)
  const double readTime = (0.78 - (dec - 50) * 0.005) * (play.type == PlayType::PlayAction ? tune().paRead : 1.0);
  Ent* cur = reads.empty() ? nullptr : reads[(std::min)(readIdx, reads.size() - 1)];
  qbLook = cur;
  // a catch behind the line is only worth throwing if he can get back to it: the nearest defender has to be further
  // from where he catches it than he is from the line, with room to spare (three in ten of those completions lost
  // yards - a swing pass to a covered back - against about one in eight on Sundays)
  auto behindPen = [&](const V2& at) {
    const double back = L - at.x;
    if (back <= 0 || tune().behindK <= 0) return 0.0;
    double nd = 99;
    for (Ent* d : def) nd = (std::min)(nd, dist(d->pos, at));
    return clamp((back + tune().behindRoom - nd) * tune().behindK, 0.0, 3.0);
  };
  const bool quick = play.hasTag("quick") || play.type == PlayType::Screen;
  // openness threshold relaxes as time passes / pressure mounts
  // and drops faster once the play is late (past ~2.5 s the QB takes what is there)
  const double thresh = (quick ? 0.8 : tune().qbThresh) - tPocket * tune().qbDecay - (std::max)(0.0, tPocket - 2.5) * tune().qbLate - pressure * 1.1;
  if (cur) {
    // how deep the catch would be: where he is when a throw now arrives (a go route at 12 yd is caught at 25), not in 1.2 s
    const double arriveT = dist(e->pos, cur->pos) / (throwSpeed(e) * 0.85) + 0.3 + releaseTime(e);
    const double depthT = predictPos(cur, (std::max)(1.2, arriveT)).x - L;
    double open = openness(cur, e) - (depthT > 15 ? tune().deepReadK + (depthT - 15) * tune().deepReadSlope : 0);
    open -= behindPen(predictPos(cur, arriveT));
    // a dump-off five yards behind the line has to make all five back: the deeper behind it, the less it is worth
    // (quick-game throws to a back in the backfield lost yards four times in ten)
    const bool screen = play.type == PlayType::Screen;
    if (!screen && depthT < -1) open -= tune().behindDepthK * (-depthT - 1);
    // situational: on 3rd/4th down, routes past the sticks are worth more
    const double down = cfg.down.value_or(1);
    const double toGo = cfg.toGo.value_or(10);
    if (down >= 3) open += depthT >= toGo ? 0.6 : depthT < toGo - 3 ? -0.9 : 0;
    // 2nd and long is played to get half of it back: the throw that is there underneath is taken sooner than on 1st
    // and 10 (fewer of them end in a sack or a ball thrown away, and third and 10+ with them)
    if (down == 2 && toGo >= 10 && depthT >= 2 && depthT < toGo) open += tune().secondLong;
    // QBs read downfield first: an outlet at or behind the line is the last option until the play has had time
    if (!quick && depthT < 2 && tPocket < tune().outletT) open -= tune().outletBase + tune().outletPen;
    // and the short routes wait their turn while the deeper ones are still coming open (they were thrown the moment
    // they cleared the bar: a fifth of all passes went 10-19 yd against the NFL's quarter, at a 66 % completion rate)
    else if (!quick && depthT < 6 && tPocket < tune().shortPenT && down < 3) open -= tune().shortPen;
    // a receiver is a target once his route has declared itself (past his release / first break), not before
    // (a sit-down route - checkdown, hitch, curl - counted as developed from the snap: a checkdown went to a back still
    // at his 5 yd alignment, and a quarter of all behind-the-line catches were 7 yd deep against the NFL's 4. It waits
    // for its first point like any route, and caught behind the line, for the end of it: the back settles past it)
    const bool sitBehind = cur->settle && depthT < 0.5 && cur->routeIdx < cur->route.size();
    const bool developed = quick || cur->route.empty() || (cur->routeIdx >= 1 && !sitBehind);
    // mental noise: poor decision makers misjudge
    const double perceived = open + rng.normal(0, 0.25 + (99 - dec) * 0.012);
    const bool feetSet = quick || (dropDone && t - dropDoneT > 0.12);
    // (a receiver's tunnel screen is thrown as he turns back for it; a slip once he is out behind the rush)
    const double screenAt = !screen ? 0 : screenEnt_ && screenEnt_->routeName == "tunnel" ? 0.75 : screenEnt_ && screenEnt_->routeName == "bubble" ? 0.5 :
                            screenEnt_ && screenEnt_->routeName == "slip" ? 1.6 : tune().screenT;
    if (perceived > thresh && developed && feetSet && tPocket > (screen ? screenAt : quick ? tune().quickMinT : tune().readMinT)) {
      throwPath = quick ? 3 : 0;
      startThrow(e, cur, !cur->route.empty() && (cur->route.back().x - L) > 22);
      return;
    }
    if (readT > readTime) {
      readT = 0;
      readIdx++;
      if (readIdx >= reads.size()) readIdx = 0;  // cycle back
    }
  }
  // pressure handling: unblocked rusher closing -> hot read / get rid of it
  const double eta = rushETA(e);
  // a hazard per second (aware QBs feel it sooner), not a per-tick coin flip
  if (eta < 0.65 && !pressured && charted) notePressure(e, "eta");
  if (eta < 0.65 && charted) pressured = true;  // a free rusher 0.65 s from him: a hurry even if he gets it off
  if (eta < 0.75 + e->r().awareness * 0.004 && rng.chance((2.5 + e->r().awareness * 0.05) * tune().hotRate * dt)) {
    Ent* hot = nullptr;
    double ho = -99;
    for (Ent* r : reads) {
      // under pressure the QB gets it out short: a hot read is a quick target, not a shot downfield
      const V2 at = predictPos(r, dist(e->pos, r->pos) / (throwSpeed(e) * 0.85) + 0.3 + releaseTime(e));
      const double rd = at.x - L;  // where he catches it
      const double o = openness(r, e) + (rd >= 1 && rd < 8 ? 0.4 : rd < 1 ? -tune().outletPen : 0) - (rd > 12 ? tune().hotDeepK + (rd - 12) * tune().hotDeepSlope : 0) - behindPen(at) -
                       (rd < -1 ? tune().behindHotK * (-rd - 1) : 0);
      if (o > ho) {
        ho = o;
        hot = r;
      }
    }
    if (hot && ho > (eta < 0.35 ? -0.2 : 0.4)) {
      throwPath = 1;
      startThrow(e, hot, false);
      return;
    }
    // nobody to hit and the rusher is on him: an aware QB throws it away rather than take the sack
    // (NFL quarterbacks throw it away about once a game, 3-4 % of dropbacks: at 0.7 here, with the pressure and late
    // throwaways, it was four a game - a quarter of the incompletions on early downs, and third and long with them)
    if (eta < 0.55 && tPocket > 1.0 && rng.chance((0.25 + e->r().awareness * 0.006) * tune().throwAwayK)) {
      pendingThrow = PendingThrow{nullptr, false, t + releaseTime(e), true};
      taWhy_ = 2;
      return;
    }
  }
  if (pressure > 0.6) {
    // best available
    Ent* best = nullptr;
    double bo = -99;
    for (Ent* r : reads) {
      const double o = openness(r, e);
      if (o > bo) {
        bo = o;
        best = r;
      }
    }
    const bool lane = scrambleLane(e);
    const bool throwAwayOk = tPocket > 1.3 && rng.chance((0.03 + e->r().awareness * 0.0006) * tune().throwAwayK);
    if (best && bo > 0 && rng.chance(0.03 + e->r().awareness * 0.0007)) {
      throwPath = 2;
      startThrow(e, best, false);
      return;
    }
    // (every quarterback escapes a rush through an open lane; the quick ones far more readily - it was a 65+ speed gate,
    // and a pocket passer stood in and took the sack or threw it away)
    if (lane && rng.chance((0.004 + (std::max)(0.0, e->r().speed - 50) * 0.0008) * tune().scrambleK)) {
      scramble = true, scrambleT = t;
      setCarrier(e);
      ev("scramble", e);
      return;
    }
    if (throwAwayOk) {
      pendingThrow = PendingThrow{nullptr, false, t + releaseTime(e), true};
      taWhy_ = 3;
      return;
    }
  }
  // the coverage scramble: the drop is done, he has been through his reads, nobody has come open and the rush has left
  // him a lane - he takes the yards in front of him (a hazard per second; the quick ones go far more readily)
  if (!quick && play.type != PlayType::Screen && dropDone && tPocket > tune().scrT && tune().scrCov > 0 && scrambleLane(e)) {
    double bo = -99;
    for (Ent* r : reads) bo = (std::max)(bo, openness(r, e));
    const double mob = clamp((e->r().speed - 50) / 25.0, 0.2, 1.6);
    if (bo < thresh && rng.chance(tune().scrCov * mob * dt)) {
      scramble = true, scrambleT = t;
      setCarrier(e);
      ev("scramble", e);
      return;
    }
  }
  if (tPocket > 5.2 && rng.chance(0.04)) {
    // the play is over: he takes what is there (the man most open, a checkdown) and throws it away only when nobody is
    Ent* best = nullptr;
    double bo = -99;
    for (Ent* r : reads) {
      const double o = openness(r, e);
      if (o > bo) {
        bo = o;
        best = r;
      }
    }
    if (best && bo > tune().lateOpen && !rng.chance(tune().throwAwayK * 0.5)) {
      throwPath = 2;
      startThrow(e, best, false);
      return;
    }
    pendingThrow = PendingThrow{nullptr, false, t + releaseTime(e), true};
    taWhy_ = 4;
    return;
  }
  // pocket movement, as a quarterback moves in it: away from a free rusher bearing down; up into the pocket when an end
  // is riding his tackle around the edge (engaged, outside him and level with or behind him); a hitch step otherwise
  V2 push{0.6, 0};
  bool edge = false;
  for (Ent* d : def) {
    if (d->state != EntState::Rush && !d->isDL) continue;
    const double dd = dist(d->pos, e->pos);
    if (d->free() && dd < 4.5) push = add(push, scale(norm(sub(e->pos, d->pos)), (4.5 - dd) * 1.0));
    else if (!d->free() && dd < 4.0 && std::fabs(d->pos.y - e->pos.y) > 1.5 && d->pos.x < e->pos.x + 1.2) {
      edge = true;
      push = add(push, scale(norm(sub(e->pos, d->pos)), (4.0 - dd) * 0.35));
    }
  }
  if (edge) push.x += tune().stepUp;  // the step up
  const V2 pocketCenter{dropDone ? (std::max)(e->pos.x, dropX) : dropX, qbSetY()};
  const V2 back = sub(pocketCenter, e->pos);
  e->want = scale(add(push, scale(back, edge ? 0.15 : 0.5)), 1.4);
  if (len(e->want) > e->vmax * 0.6) e->want = scale(norm(e->want), e->vmax * 0.6);
  e->face = 0;  // (eyes downfield while he moves)
}

/** 0..1+ pressure based on free rushers near QB. */
void PlaySim::notePressure(const Ent* q, const char* how) {
  // who it is (the nearest free defender) and how he got free: he beat a blocker (shed), nobody ever had him
  // (unblocked), or a blocker had him and lost him without a shed (loose)
  const Ent* who = nullptr;
  double best = 1e9;
  for (Ent* d : def) {
    if (!d->free()) continue;
    const double dd = dist(d->pos, q->pos);
    if (dd < best) {
      best = dd;
      who = d;
    }
  }
  if (!who) return;
  bool shed = false;
  for (Ent* o : off)
    if (contains(o->beatBy, const_cast<Ent*>(who))) shed = true;
  // (an unblocked rusher when the protection had the numbers is a protection failure; with more rushers than blockers,
  // the hot read's man: " xs")
  int nRush = 0, nBlock = 0;
  for (Ent* d : def) if (d->state == EntState::Rush) nRush++;
  for (Ent* o : off) if (o->state == EntState::PassBlock) nBlock++;
  pressWhy = std::string(how) + (shed ? " shed " : who->state == EntState::Rush ? " unblocked " : " nonrusher ") + posName(who->p->pos) +
             (!shed && who->state == EntState::Rush && nRush > nBlock ? " xs" : "");
  if (!shed && who->state == EntState::Rush && nRush <= nBlock && std::getenv("TAG_PRESS_WHO")) {
    std::string owner = " [nobody's]";
    for (Ent* o : off)
      if (o->snapTarget == who) owner = " [" + o->role + (o->state == EntState::PassBlock ? (o->engaged ? " on another" : " free") : " gone") + "]";
    pressWhy += owner;
  }
  pressT = t;
}

double PlaySim::pressureOn(const Ent* q) const {
  // pressure = a defender who is through the protection and closing; linemen across from an under-center QB at the
  // snap are close but not pressure (the blocks have not landed yet)
  double lineX = 0;
  int nOL = 0;
  for (Ent* o : off)
    if (o->isOL && o->state != EntState::Down) {
      lineX += o->pos.x;
      nOL++;
    }
  lineX = nOL ? lineX / nOL : los;
  double p = 0;
  for (Ent* d : def) {
    if (!d->free()) continue;
    const double dd = dist(d->pos, q->pos);
    if (t < 1.6 && d->pos.x > lineX - 0.4 && dd > 1.5) continue;  // (as rushETA: not through until he is past the blockers' line)
    if (dd < 4.5) p = (std::max)(p, (4.5 - dd) / 3);
  }
  return p;
}

bool PlaySim::scrambleLane(const Ent* q) const {
  // a way out of the pocket: a direction (straight up to flat out to either side) whose first ~7 yd are clear of every
  // body - his own blockers and the rushers on them as much as a free man - with nobody free already on top of him.
  // (it looked only for a free defender in front of him: he 'escaped' into the back of his own pocket, took 1.7 s to make
  // five yards through it, and scrambles went for 0.8 yd against the NFL's 6.7)
  for (Ent* d : def)
    if (d->free() && d->state != EntState::Down && dist(d->pos, q->pos) < 2.2) return false;
  for (int ai = -80; ai <= 80; ai += 10) {
    const V2 dir = fromAngle(ai * kPi / 180);
    bool clear = true;
    for (int k = 1; k <= 6 && clear; k++) {
      const V2 p = add(q->pos, scale(dir, k * 1.2));
      if (p.y < 1 || p.y > FIELD_W - 1) clear = false;
      for (const std::vector<Ent*>* side : {&off, &def})
        for (const Ent* o : *side) {
          if (!clear) break;
          if (o == q || o->state == EntState::Down) continue;
          const double room = o->side == q->side || o->engaged ? 1.15 : 2.0;
          if (dist(o->pos, p) < room) clear = false;
        }
    }
    if (!clear) continue;
    // and grass on the far side of it: nobody who is free to come up (not blocked, not turned and running with a
    // receiver) within ~4.5 yd of where the lane crosses the line - he runs when the yards are there, not into a
    // linebacker sitting in his hook zone
    const V2 out = add(q->pos, scale(dir, 7.2));
    const V2 at{los + 2.5, out.y + dir.y * (std::max)(0.0, los + 2.5 - out.x) / (std::max)(0.3, dir.x)};
    if (at.y < 1 || at.y > FIELD_W - 1) continue;
    bool grass = true;
    for (const Ent* d : def) {
      if (d->state == EntState::Down || d->engaged || !d->engagedBy.empty()) continue;
      const double r = d->state == EntState::Man ? 2.0 : tune().scrGrass;
      if (dist(d->pos, at) < r) {
        grass = false;
        break;
      }
    }
    if (grass) return true;
  }
  return false;
}

/** Predict where a receiver will be in T seconds: run his own route steering and the same human acceleration law
 *  forward (rounded cuts, sinking into breaks), so the QB leads him to where he really will be. */
V2 PlaySim::predictPos(const Ent* e, double T) const {
  if (e->state != EntState::Route || e->route.empty()) return add(e->pos, scale(e->vel, T));
  V2 p = e->pos;
  V2 v = e->vel;
  size_t idx = e->routeIdx;
  double pd = e->pressDelay;
  const double h = 1.0 / 30;
  for (double tt = 0; tt < T - 1e-9; tt += h) {
    const double dt = (std::min)(h, T - tt);
    V2 want;
    if (pd > 0) {
      want = scale(norm(V2{0.3, -e->outSign * 0.6}), 1.5);
      pd -= dt;
    } else {
      want = routeWant(e, p, v, idx);
    }
    v = steerVel(e, v, want, dt);
    p = add(p, scale(v, dt));
  }
  return V2{(std::min)(p.x, DEEP_AIM), clamp(p.y, 0.8, FIELD_W - 0.8)};
}

/** Where a route runner at (pos, vel) wants to go next: the route's next point (sinking into sharp breaks), then the
 *  last leg run on (or settling), stopping at the back of the end zone. Advances idx past reached points. */
V2 PlaySim::routeWant(const Ent* e, const V2& pos, const V2& vel, size_t& idx) const {
  while (idx < e->route.size() && dist(pos, e->route[idx]) < 0.7) idx++;
  // a break point he has run past - beyond it along the leg into it, within a couple of yards - is behind him: the next
  // leg starts from where he is (Tune::routePass). At speed he cannot turn inside a 3 yd leg, and he looped back to touch
  // the point: an out-and-up's receiver ran back toward the line of scrimmage, a sluggo's curled 1 yd back, and the loop
  // was a "double move" no defender could have played. (A point he has to come back to - a hitch - is still his.)
  if (tune().routePass > 0)
    while (idx > 0 && idx + 1 < e->route.size()) {
      const V2 tgt = e->route[idx];
      const V2 legIn = norm(sub(tgt, e->route[idx - 1]));
      if (dist(pos, tgt) > 2.5 || dot(sub(pos, tgt), legIn) <= 0) break;
      idx++;
    }
  const double s = len(vel);
  if (idx < e->route.size()) {
    const V2 tgt = e->route[idx];
    const double d = dist(pos, tgt);
    double sp = e->vmax * (ball.mode == BallMode::Air && ball.target == e ? 1.0 : tune().routePace);
    if (idx + 1 < e->route.size()) {
      const V2 legIn = norm(sub(tgt, idx ? e->route[idx - 1] : pos));
      const V2 legOut = norm(sub(e->route[idx + 1], tgt));
      const double turn = std::acos(clamp(dot(legIn, legOut), -1, 1));  // radians
      if (turn > 0.5) {
        // sink into a break: a receiver gathers his feet before a sharp cut (the sharper, the slower), starting early
        // enough to brake at a human rate; good route runners carry more speed through it
        const double keep = 0.55 + e->r().routeRunning * 0.004;
        const double vBreak = e->vmax * keep * clamp(1.15 - turn * 0.45, 0.4, 1.0);
        const double need = (s * s - vBreak * vBreak) / (2 * 8.5) + 0.4;
        if (d < need) sp = vBreak;
      }
    }
    return scale(norm(sub(tgt, pos)), sp);
  }
  if (e->settle) return scale(vel, -1.5);  // stop in the hole
  const size_t n = e->route.size();
  const V2 dir = n >= 2 ? norm(sub(e->route[n - 1], e->route[n - 2])) : V2{1, 0};
  V2 w = scale(dir, e->vmax);
  // turn upfield before the sideline: as far out as he needs to take his sideways speed off at a human rate (a flat
  // at 4-5 yd/s needs ~2 yd of room; turning at a fixed 2 yd ran half of them out of bounds)
  const double edge = 1.2 + vel.y * vel.y / (2 * 6.0);
  // a short route that finishes going sideways (a flat, an out, an arrow) is not a wheel: at the sideline he turns up,
  // works a few yards up it under control and sits down facing the quarterback. Every one used to turn upfield flat out
  // and run to the end zone - on play action, with the longer drop, the flat man was 25 yd down the sideline when the
  // ball came out (thrown flats: 26.5 air yards, 9 yd clear of the nearest man)
  // (the same for a crosser at the far sideline, a dig, an out: run on sideways they turned into a go - crossers were
  // thrown at 17.9 air yards on play action, deep crossers at 21)
  const bool shortSide = n >= 2 && std::fabs(dir.y) > std::fabs(dir.x);
  const double sEdge = (std::max)(edge, 2.0);  // (the sideways speed gone, he stays where he turned up, not creeping out)
  const bool atEdge = (pos.y < sEdge && w.y < 0) || (pos.y > FIELD_W - sEdge && w.y > 0);
  if (shortSide && atEdge) {
    const double cap = e->route[n - 1].x + 3;
    return pos.x < cap ? V2{(std::min)(e->vmax * 0.5, (cap - pos.x) * 2 + 0.5), 0} : scale(vel, -1.5);
  }
  if (pos.y < edge && w.y < 0) w = V2{e->vmax, 0};
  if (pos.y > FIELD_W - edge && w.y > 0) w = V2{e->vmax, 0};
  if (pos.x > DEEP_AIM - 1.5 && w.x > 0) {
    // out of room at the back of the end zone: stop, square up to the QB and work along the end line
    const double toQB = qb ? jsSign(qb->pos.y - pos.y) : 0;
    w = V2{(DEEP_AIM - 0.5 - pos.x) * 3, toQB * 2.5};
  }
  return w;
}

/** Windup: elite quick releases ~0.2s, slow ones ~0.38s. */
double PlaySim::releaseTime(const Ent* q) const {
  return clamp(0.3 - (q->r().throwPower + q->r().awareness - 120) * 0.0019, 0.16, 0.34);
}

/** Seconds until the nearest unblocked rusher reaches the QB. */
double PlaySim::rushETA(const Ent* q) const {
  // only a rusher who has come through (or around) the protection counts: at the snap every lineman is "free"
  // for a moment before the blocks land, and that is not a reason to bail out of the play
  double lineX = 0;
  int nOL = 0;
  for (Ent* o : off)
    if (o->isOL && o->state != EntState::Down) {
      lineX += o->pos.x;
      nOL++;
    }
  lineX = nOL ? lineX / nOL : los;
  double eta = 99;
  for (Ent* d : def) {
    if (!d->free() || d->state != EntState::Rush) continue;
    // through means past the blockers' line (or the first half second gone and nobody left to pick him up is the
    // pressure meter's business): a rusher still in front of the line at 0.6 s had simply not reached his man yet -
    // the sets give ground - and read as a free rusher a second away; nearly half of all throws were hot reads
    if (d->pos.x > lineX - 0.4 && t < 1.6) continue;
    const double dd = dist(d->pos, q->pos) - 1.1;
    eta = (std::min)(eta, dd / (std::max)(4.0, d->speed()));
  }
  return eta;
}

void PlaySim::startThrow(Ent* q, Ent* tgt, bool lob) {
  if (pendingThrow) return;
  const int forced = static_cast<int>(tune().throwStyle);
  pendingThrow = PendingThrow{tgt, lob || forced == 1, t + releaseTime(q), false, forced >= 0 ? forced : 0,
                              forced >= 0 && tgt && dist(q->pos, tgt->pos) < 6.0 && dist(predictPos(tgt, 0.5), q->pos) < 6.5};
  q->anim = Anim::Throw;
  q->animT = 0;
}

double PlaySim::throwSpeed(const Ent* q) const {
  return 20 + (q->r().throwPower - 50) * 0.16;  // 99 -> 27.8 yd/s (~57 mph)
}

double PlaySim::maxAir(const Ent* q) const {
  // NFL arms: the strongest put ~65 yd of air on a catchable ball. Never farther than the arm's launch speed can
  // carry it (range ~ v^2/g, less drag): a throw past that solved to a 6 s moonball.
  const double v0 = throwSpeed(q);
  return (std::min)(48 + (q->r().throwPower - 50) * 0.4 + q->fx.armAdd, 0.9 * v0 * v0 / G + q->fx.armAdd);
}

double PlaySim::openness(const Ent* r, const Ent* q) const {
  if (r->state == EntState::Down || r->pressDelay > 0.3) return -3;
  const double v0 = throwSpeed(q);
  double T = dist(q->pos, r->pos) / (v0 * 0.85) + 0.3;
  V2 P = predictPos(r, T);
  T = dist(q->pos, P) / (v0 * 0.85) + 0.3;
  P = predictPos(r, T);
  double worst = 99;
  for (Ent* d : def) {
    if (d->state == EntState::Down) continue;
    // deep defenders close on long balls (they're already running): QBs respect that
    // (what a man can cover once he has seen the ball go: from the speed he already has toward the spot, at a
    // human's acceleration. Read as top speed from the first step, a mid-range window had to be 8 yd wide before it
    // counted as open: a fifth of all passes went 10-19 yd against the NFL's quarter, and two thirds were caught.)
    double reach = d->free() ? d->vmax * (std::max)(0.0, T - d->reactT - 0.15) * (T > 1.6 ? 0.98 : 0.85) : 0;
    if (tune().reachA > 0 && d->free()) {
      const double tau = (std::max)(0.0, T - d->reactT - 0.15);
      const double toward = (std::max)(0.0, dot(d->vel, norm(sub(P, d->pos))));
      reach = (std::min)(reach, toward * (T - tau) + (std::min)(d->vmax * tau, toward * tau + 0.5 * tune().reachA * tau * tau));
    }
    const double sep = dist(d->pos, P) - reach - 0.9;
    // defenders in the passing lane
    const double lane = laneBlock(q->pos, P, d->pos);
    worst = (std::min)({worst, sep, lane});
  }
  // throws beyond the sideline or the end line, or past the arm's range, are worthless
  if (P.y < 0.5 || P.y > FIELD_W - 0.5) worst -= 3;
  if (P.x > DEEP_AIM + 0.5) worst -= 3;
  if (dist(q->pos, P) > maxAir(q)) worst -= 3;
  return worst;
}

double PlaySim::laneBlock(const V2& a, const V2& b, const V2& d) const {
  const V2 ab = sub(b, a);
  const double L = len(ab);
  if (L < 1) return 99;
  const double tt = clamp(dot(sub(d, a), ab) / (L * L), 0, 1);
  if (tt < 0.15 || tt > 0.85) return 99;
  const V2 c = add(a, scale(ab, tt));
  // underneath defenders can only knock down lower throws (short/mid)
  const double h = dist(d, c);
  return L > 18 ? 99 : h * 1.6 - 0.4;
}

void PlaySim::throwBall(Ent* q, Ent* tgt, bool lob, int style, int shovelAt) {
  Ball& b = ball;
  b.away = false;
  // the pass types (the user's, Madden's): a bullet on a line through the window, a lob over the underneath man, a
  // high ball to where only his hands get it, a low one at his knees, a shovel to a man a few yards off
  const bool userish = q->userControlled || tune().throwStyle >= 0;  // (the look-dev check: the CPU throws the user's types)
  shovel = shovelAt >= 0 ? shovelAt == 1 && userish : userish && dist(q->pos, tgt->pos) < 6.0;  // (the user's: the CPU's short throws keep their calibrated flight)
  const bool bullet = style == 0 && userish && !lob && !shovel;
  // the user's lob: a touch ball that hangs (over the underneath man's hands - the arm's quickest arc plus its touch
  // only put a short lob a foot over a bullet), the longer the throw the longer it hangs
  auto lobHang = [&](double D, double T) { return userish && lob && !shovel ? (std::max)(T, 0.85 + 0.028 * D) : T; };
  double v0max = throwSpeed(q) * (shovel ? 0.45 : bullet ? 1.05 : 1.0);
  // a shovel gets the arm its distance needs (at a soft toss's speed a man moving away was out of range: the solver
  // ran to its hang cap and the ball went up 27 yd for 4 s)
  auto shovelArm = [&](double D) { if (shovel) v0max = clamp(std::sqrt(G * D) * 1.35, throwSpeed(q) * 0.45, throwSpeed(q)); };
  shovelArm(dist(q->pos, tgt->pos));
  // target at chest height ~ 1.4 yd
  double T = dist(q->pos, tgt->pos) / (v0max * 0.8) + 0.1;
  V2 aim = predictPos(tgt, T);
  double touch = lob ? 0.32 : 0.05;
  for (int k = 0; k < 4; k++) {
    const double D = dist(q->pos, aim);
    touch = (std::max)(touch, (lob || D > 28) ? 0.32 : D > 15 ? 0.14 : 0.05);
    if (bullet) touch = (std::min)(touch, D > 28 ? 0.2 : D > 15 ? 0.08 : 0.03);  // (on a line)
    if (style == 2) touch = (std::max)(touch, 0.2);
    if (shovel) touch = 0.12;
    // put air under it if an underneath defender sits in the lane (not a bullet: that is thrown through the window)
    if (!lob && !bullet && !shovel && D > 8) {
      for (Ent* d : def) {
        if (d->state == EntState::Down || d->engaged) continue;
        const V2 ab = sub(aim, q->pos);
        const double tt = dot(sub(d->pos, q->pos), ab) / dot(ab, ab);
        if (tt < 0.25 || tt > 0.85) continue;
        if (dist(d->pos, add(q->pos, scale(ab, tt))) < 1.6) touch = (std::max)(touch, 0.12 + (tt < 0.6 ? 0.12 : 0.05) * (D / 15));
      }
    }
    shovelArm(D);
    T = lobHang(D, (std::min)(solveT(D, v0max, (std::min)(0.45, touch)), 4.4));
    aim = predictPos(tgt, T + 0.05);
  }
  // QBs throw receivers away from the sideline so they can stay in bounds, keep the ball inside the
  // end zone, and cannot throw farther than the arm allows (the receiver comes back to an underthrow)
  aim = V2{(std::min)(aim.x, DEEP_AIM), clamp(aim.y, 1.8, FIELD_W - 1.8)};
  {
    const double reach = maxAir(q);
    const V2 toAim = sub(aim, q->pos);
    const double L = len(toAim);
    if (L > reach) aim = add(q->pos, scale(toAim, reach / L));
    // re-solve the flight for the spot actually thrown at; hang time capped so a bomb peaks below ~26 yd (24 m)
    shovelArm(dist(q->pos, aim));
    T = lobHang(dist(q->pos, aim), (std::min)(solveT(dist(q->pos, aim), v0max, (std::min)(0.45, touch)), 4.4));
  }
  const double D = dist(q->pos, aim);
  ball.aimNoErr = aim;
  ball.flightT = T;
  // accuracy model
  const double depthAcc = D < 10 ? q->r().accShort : D < 20 ? q->r().accMid : q->r().accDeep;
  const double pressure = pressureOn(q);
  const double moving = q->speed() / q->vmax;
  // (abilities: Off Script / Pocket Poise / Ice Veins take off part of what the move and the rush cost him)
  const double onRun = moving > 0.35 ? 1 + (1 - q->r().throwOnRun / 100) * moving * 1.5 * q->fx.runAcc : 1;
  const double poise = 1 + pressure * (std::max)(0.0, 1.4 - q->r().poise / 100) * q->fx.pressAcc;
  const double weather = cfg.weather.precip == Precip::Rain ? 1.12 : cfg.weather.precip == Precip::Snow ? 1.18 : 1;
  // placement error grows faster than linearly past 20 yards: deep balls are dropped in a bucket, not darted
  const double deepK = D > 20 ? 1 + (D - 20) * tune().deepErr : 1;
  const double abilAcc = (D >= 20 ? q->fx.deepAcc : q->fx.shortAcc) * (abilOn_ ? tune().abilAcc : 1.0);
  const double sd = (tune().accBase + (std::max)(0.0, 99 - depthAcc) * tune().accSlope) * (0.5 + D / 19) * (1 + tune().accMidK * clamp((D - 12) / 10, 0.0, 1.0)) * deepK * onRun * poise * weather * (1 + q->p->fatigue * 0.2) * abilAcc;
  {
    const double nx = rng.normal(0, sd * 0.9);
    const double ny = rng.normal(0, sd);
    aim = V2{aim.x + nx, aim.y + ny};
  }
  const double zErr = rng.normal(0, sd * 0.45);
  // where it arrives: the chest; a high pass over the head (his hands only), a low one at the knees; a shovel at the waist
  const double arriveZ = style == 2 ? 2.15 : style == 3 ? 0.7 : shovel ? 1.15 : 1.45;
  launch(q, aim, T, arriveZ + zErr, shovel ? 1.05 : 2.05, shovel ? 0.5 : 0.0);
  b.target = tgt;
  b.targetSpot = aim;
  b.lob = lob;
  passThrown = true;
  throwStart = t;
  // the draws the outcome is rolled with (0.4 s out: preRollCatch; a defender in the lane: preRollLane), taken now so the
  // forecast of it at the release (forecastFate) reads the same ones
  for (double& u : b.fateU) u = rng.next();
  b.fateKey = static_cast<uint32_t>(rng.next() * 4294967295.0);
  forecastFate();
  throwSep = 99;
  for (Ent* o : def)
    if (o->state != EntState::Down) throwSep = (std::min)(throwSep, dist(o->pos, tgt->pos));
  q->anim = Anim::Throw;
  q->animT = 0;
  q->state = EntState::Idle;
  ev("throw", q, tgt);
  // receiver tracks the ball
  tgt->zoneSpot = aim;
}

void PlaySim::throwAway(Ent* q) {
  const double side = q->pos.y > by ? 1 : -1;
  V2 aim{los + 8, side > 0 ? FIELD_W + 4 : -4};
  // (no further than he can throw it on a line: from the far hash the sideline spot was out of his range, the flight
  // solver ran out at its 6 s limit and the ball went up 60 yd)
  double D = dist(q->pos, aim);
  if (D > 38) {
    aim = add(q->pos, scale(norm(sub(aim, q->pos)), 38));
    D = 38;
  }
  const double T = (std::min)(solveT(D, throwSpeed(q), 0.15), 3.4);
  launch(q, aim, T, 2.5);
  ball.target = nullptr;
  ball.away = true;
  ball.targetSpot = aim;
  passThrown = true;
  throwStart = t;
  q->anim = Anim::Throw;
  q->animT = 0;
  q->state = EntState::Idle;
  static const char* const kWhy[] = {"", "user", "hot", "pressure", "late"};
  ev("throwaway", q, nullptr, kWhy[taWhy_]);
}

/** Minimum flight time for given distance & speed, plus "touch" fraction. */
double PlaySim::solveT(double D, double v0, double touch) const {
  double lo = 0.05;
  double hi = 6;
  // the speed a flight of time T needs is least at Ts = sqrt(2D/G): the quickest flight the arm can make lies below it.
  // (searched from 0.05..6 the first guess, 3 s, was past every flight a soft arm can make - a shovel - and the search
  // ran to the cap: a 4.4 s toss straight up. A full arm's flights reach past 3 s, so its answer is unchanged.)
  const double Ts = std::sqrt(2 * (std::max)(0.01, D) / G);
  if (D * D / (Ts * Ts) + 0.25 * G * G * Ts * Ts <= v0 * v0) hi = (std::min)(hi, Ts);
  for (int i = 0; i < 40; i++) {
    const double T = (lo + hi) / 2;
    const double vh = D / T;
    const double vz = (0.5 * G * T * T) / T;
    if (vh * vh + vz * vz > v0 * v0) lo = T;
    else hi = T;
  }
  return hi * (1 + touch);
}

/** Launch with drag+wind compensation via shooting method. */
void PlaySim::launch(Ent* q, const V2& aim, double T, double zTarget, double z0, double fwd) {
  // (z0 default: release height ~ 6'2")
  const V2 to = sub(aim, q->pos);
  const double tl = len(to);
  const V2 from = tl > 0.01 && fwd > 0 ? add(q->pos, scale(to, (std::min)(fwd, tl * 0.5) / tl)) : q->pos;
  V2 tgt = aim;
  V3 vel{0, 0, 0};
  for (int it = 0; it < 3; it++) {
    vel = V3{(tgt.x - from.x) / T, (tgt.y - from.y) / T, (zTarget - z0 + 0.5 * G * T * T) / T};
    const V3 land = simulateFlight(V3{from.x, from.y, z0}, vel, T);
    tgt = V2{tgt.x + (aim.x - land.x), tgt.y + (aim.y - land.y)};
  }
  Ball& b = ball;
  b.mode = BallMode::Air;
  b.holder = nullptr;
  q->hasBall = false;
  b.thrower = q;
  b.throwX = q->pos.x;
  b.pos = V3{from.x, from.y, z0};
  b.vel = vel;
  b.t = 0;
}

double PlaySim::dragK() const {
  // 0.5 * rho * Cd * A / m for a spiral (yd^-1)
  const double wet = cfg.weather.precip != Precip::None ? 1.08 : 1;
  const double cold = 1 + (60 - cfg.weather.tempF) * 0.0015;  // denser air when cold
  return 0.0016 * wet * cold;
}

V3 PlaySim::simulateFlight(const V3& p0, const V3& v0, double T) const {
  V3 p = p0;
  V3 v = v0;
  const double k = dragK();
  const double h = 1.0 / 120;
  for (double tt = 0; tt < T; tt += h) integrate(p, v, k, h);
  return p;
}

void PlaySim::integrate(V3& p, V3& v, double k, double h) const {
  const double rx = v.x - wind.x;
  const double ry = v.y - wind.y;
  const double rz = v.z;
  const double s = jsHypot(rx, ry, rz);
  v.x += -k * s * rx * h;
  v.y += -k * s * ry * h;
  v.z += (-G - k * s * rz) * h;
  p.x += v.x * h;
  p.y += v.y * h;
  p.z += v.z * h;
}

void PlaySim::thinkRoute(Ent* e) {
  e->catchAdjust = false;
  if (ball.mode == BallMode::Air && ball.target == e && ball.targetSpot) {
    // track the ball: go to landing spot, arriving on time. A receiver reads the ball only once it is up
    // (looking back over his shoulder on a deep ball takes longer); until then he keeps running his route.
    const double airYd = ball.targetSpot->x - los;
    const double trackT = 0.22 + (airYd > 18 ? 0.25 : 0) + (99 - e->r().catching) * 0.004;
    if (ball.t >= trackT) {
      e->catchAdjust = true;
      // a ball drifting out: he keeps his feet in (a toe tap at the sideline, the end line) and reaches for it
      const V2 land = ballLanding();
      const V2 spot{(std::min)(land.x, END_LINE - 0.35), clamp(land.y, 0.35, FIELD_W - 0.35)};
      e->want = scale(norm(sub(spot, e->pos)), (std::min)(e->vmax, dist(spot, e->pos) * 5 + 1));
      return;
    }
  } else if (ball.mode == BallMode::Air) {
    // non-targets: drift toward the ball to block/ help
    const V2 spot = ballLanding();
    e->want = scale(norm(sub(spot, e->pos)), e->vmax * 0.6);
    return;
  }
  if (e->pressDelay > 0) {
    e->want = scale(norm(V2{0.3, -e->outSign * 0.6}), 1.5);
    return;
  }
  if (e->routeIdx < e->route.size()) {
    e->want = routeWant(e, e->pos, e->vel, e->routeIdx);
    if (e->routeIdx < e->route.size()) return;
  }
  if (e->settle) {
    // settle in the hole facing QB, slight drift away from nearest defender
    V2 away{0, 0};
    for (Ent* d : def) {
      const double dd = dist(d->pos, e->pos);
      if (dd < 3) away = add(away, scale(norm(sub(e->pos, d->pos)), (3 - dd) * 0.5));
    }
    const V2 toQB = scale(norm(sub(qb->pos, e->pos)), 0.4);
    e->want = add(away, toQB);
    return;
  }
  // continue last leg
  size_t idx = e->routeIdx;
  e->want = routeWant(e, e->pos, e->vel, idx);
}

V2 PlaySim::ballLanding() const {
  const Ball& b = ball;
  // (a pass being brought onto the hands of the man who plays it, homeBall: everyone runs at where the throw as it
  // was thrown comes down - the men go where the throw sends them and the ball is bent to the one who gets it. Run
  // at the bent flight instead, the receiver stopped short of where he would have got to and the pass fell incomplete)
  const bool asThrown = b.ghost && b.target && !b.tumble && b.mode == BallMode::Air;
  const V3& bpos = asThrown ? b.ghostPos : b.pos;
  const V3& bvel = asThrown ? b.ghostVel : b.vel;
  // exact memo: identical inputs give identical outputs (wind / drag are fixed per play)
  if (landingValid_ && landingPos_.x == bpos.x && landingPos_.y == bpos.y && landingPos_.z == bpos.z &&
      landingVel_.x == bvel.x && landingVel_.y == bvel.y && landingVel_.z == bvel.z)
    return landingOut_;
  // predict where ball reaches catch height (~1.3 yd) on descent
  V3 p = bpos;
  V3 v = bvel;
  const double k = dragK();
  for (int i = 0; i < 600; i++) {
    integrate(p, v, k, 1.0 / 60);
    if (v.z < 0 && p.z < 1.3) break;
  }
  landingValid_ = true;
  landingPos_ = bpos;
  landingVel_ = bvel;
  landingOut_ = V2{p.x, p.y};
  return landingOut_;
}

namespace {
// a uniform in (0, 1) off a 64-bit key (splitmix64): a fixed draw that leaves the play's rng where it was
double hashUnit(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  x ^= x >> 31;
  return (static_cast<double>(x >> 11) + 0.5) * (1.0 / 9007199254740992.0);
}
}  // namespace

V2 PlaySim::landingFor(const Ent* d) const {
  // A defender reads the flight: early his spot is off by a yard or two (more for poor ball skills), and it closes on
  // the real one as the ball comes down. Everyone ran at the exact spot from his reaction on - the "psychic" break on
  // the ball a defender cannot yet know.
  const V2 land = ballLanding();
  const Ball& b = ball;
  if (tune().eyesOn <= 0 || tune().landErr <= 0 || !d->p || b.mode != BallMode::Air || !b.target || b.flightT <= 0.05) return land;
  const double left = clamp(1 - b.t / b.flightT, 0, 1);
  const double sd = tune().landErr * left * clamp(1.6 - d->r().ballSkills / 100.0, 0.5, 1.2);
  uint64_t key = 0;
  std::memcpy(&key, &throwStart, sizeof key);
  // (the man by his jersey and side, not Player::id: ids count up across leagues, and the same game in a second league
  // read the ball differently - SimDigest's twice-in-one-process check)
  key ^= static_cast<uint64_t>(static_cast<uint32_t>(d->p->number) * 2u + (d->side == Side::Def ? 1u : 0u) + 1u) * 0x9E3779B97F4A7C15ull;
  key ^= static_cast<uint64_t>(std::llround(los * 64.0));
  const double u1 = hashUnit(key), u2 = hashUnit(key ^ 0xD1B54A32D192ED03ull);
  const double rad = std::sqrt(-2 * std::log(u1)) * sd;
  return V2{land.x + rad * std::cos(2 * kPi * u2), land.y + rad * std::sin(2 * kPi * u2)};
}

double PlaySim::ballFacingDeg(const Ent* d) const {
  const Ball& b = ball;
  V2 from{-b.vel.x, -b.vel.y};  // (where it is coming from: back along its flight)
  if (len(from) < 0.5) from = sub(V2{b.pos.x, b.pos.y}, d->pos);
  if (len(from) < 1e-6) return 0;
  const V2 f = fromAngle(d->face);
  return std::acos(clamp(dot(norm(from), f), -1, 1)) * 180 / kPi;
}

bool PlaySim::eyesOnBall(const Ent* d) const { return tune().eyesOn <= 0 || canSeeBall(d); }

bool PlaySim::canSeeBall(const Ent* d) const {
  const double ang = ballFacingDeg(d);
  if (ang <= tune().eyesDeg) return true;
  // the trail man: on the target with his back to the ball, he stays on his man (turning to look loses him)
  if (d->state == EntState::Man && d->man && d->man == ball.target) return false;
  // anyone else looks back over his shoulder once he has read the throw and turned his head
  const double react = d->state == EntState::Man ? d->reactT + 0.12 : d->reactT * tune().zoneReactK;
  return ang <= tune().eyesDeg + tune().eyesHead && throwStart >= 0 && t - throwStart >= react + tune().eyesTurnT;
}

void PlaySim::thinkPassBlock(Ent* e) {
  if (ball.holder && ball.holder != qb && ball.holder->side == Side::Off) {
    e->state = EntState::Convoy;
    return;
  }
  Ent* scr = screenEnt_ ? screenEnt_ : offRole("RB");
  // (a back's screen sets up behind the rush let through; a receiver's tunnel screen is thrown at once, the play-side
  // linemen releasing to lead it - the back side keeps blocking; a slip a beat later than the tunnel)
  const bool tunnel = scr && scr->routeName == "tunnel";
  // (a bubble screen is the receivers' to block: the line keeps protecting)
  const double relT = tunnel ? 0.3 : scr && scr->routeName == "slip" ? 1.0 : scr && scr->routeName == "bubble" ? 99.0 : 1.2;
  if (cfg.offPlay->type == PlayType::Screen && t > relT && e->isOL && scr &&
      (!tunnel || (e->pos.y - by) * jsSign(orIfZero(scr->pos.y - by, 1)) > -1.5)) {
    // release to set up the screen
    if (e->engaged) disengage(e);
    const Ent* rb = scr;
    // (a tunnel: the play-side linemen out flat in front of where it is caught, the wall the receiver runs behind)
    const V2 to = tunnel ? V2{los + 1.0, scr->route.empty() ? rb->pos.y : scr->route.back().y + (e->pos.y - by) * 0.5} : V2{rb->pos.x + 4, rb->pos.y + (e->pos.y - by) * 0.6};
    e->want = scale(norm(sub(to, e->pos)), e->vmax * (tunnel ? 0.95 : 0.8));
    return;
  }
  // the helper on a double team comes off it for a rusher coming free (a blitzer, a twist): eyes up, as a real
  // center or guard does (staying on the double left the blitz unblocked - pressure 33.8 -> 36% with the help blocks)
  if (e->engaged && tune().helpBlock > 0 && e->engaged->engagedBy.size() >= 2 && e->engaged->engagedBy[0] != e) {
    for (Ent* d : def)
      if (d->state == EntState::Rush && d->free() && !contains(e->beatBy, d) && dist(d->pos, e->pos) < 4.0 && dist(d->pos, qb->pos) < dist(e->pos, qb->pos) + 3.0) {
        disengage(e);
        break;
      }
  }
  if (e->engaged) return;
  const V2 q = qb->pos;
  Ent* best = nullptr;
  double bs = 1e9;
  Ent* as = e->blockTarget;
  const bool smart = tune().smartBlock > 0;
  // passing a man off: two blockers whose rushers have crossed (a twist, a blitzer coming through the next gap) trade
  // them, each taking the one now coming at him, instead of chasing his own across his neighbour's face
  if (smart && as && as->free() && as->state == EntState::Rush) {
    const V2 asAt = add(as->pos, scale(as->vel, 0.3));
    for (Ent* o : off) {
      if (o == e || o->state != EntState::PassBlock || o->engaged || o->stun > 0) continue;
      Ent* os = o->blockTarget;
      if (!os || os == as || !os->free() || os->state != EntState::Rush) continue;
      const V2 osAt = add(os->pos, scale(os->vel, 0.3));
      if (dist(e->pos, osAt) + dist(o->pos, asAt) + 1.2 < dist(e->pos, asAt) + dist(o->pos, osAt)) {
        std::swap(e->blockTarget, o->blockTarget);
        as = e->blockTarget;
        passOffs_++;
        break;
      }
    }
  }
  // a man another free blocker already has, and is nearer to: not mine to chase (two on one and a rusher loose)
  auto claimed = [&](const Ent* d) {
    for (const Ent* o : off)
      if (o != e && o->state == EntState::PassBlock && !o->engaged && o->stun <= 0 && o->blockTarget == d &&
          dist(o->pos, d->pos) <= dist(e->pos, d->pos))
        return true;
    return false;
  };
  // (a tackle stays on the edge rusher he was given however wide that man loops - dropping him once he was further from
  // the quarterback than the tackle left him unblocked on the arc: four in ten pressures came from a man nobody blocked)
  const bool tackle = e->role == "LT" || e->role == "RT";
  if (as && as->free() && as->state == EntState::Rush &&
      (tackle && tune().edgeKeep > 0 ? dist(as->pos, e->pos) < tune().edgeKeep : dist(as->pos, q) < dist(e->pos, q) + 2.5) && t < 2.5) {
    best = as;
    bs = -1;
  }
  // a twist's penetrator coming across into his gap: he takes him and lets his own man go to the next blocker (the pass
  // off a line coach teaches: "take the man who comes to you")
  if (smart && tune().stunts > 0)
    for (Ent* d : def)
      if (d->stunt == 1 && !d->stuntDone && d->free() && d != as && e->snapTarget != d && dist(d->pos, e->pos) < 2.2 && !claimed(d)) {
        best = d;
        bs = -1;
        break;
      }
  // otherwise find most dangerous unblocked rusher
  // (eyes up: a back or tight end kept in scans the whole pocket for the free man nearest the quarterback - the
  // blitzer off the far edge, a linebacker green-dogging late - and a lineman with nobody on him looks a gap further
  // than his own; a defender who was not sent but is now coming for the passer counts as a rusher)
  const bool lineman = e->isOL;
  const double reach = smart ? (lineman ? 6.0 : 9.0) : 5.0;
  for (Ent* d : def) {
    if (bs < 0) break;
    if (!d->free()) continue;
    const double threat = dist(d->pos, q);
    const bool coming = d->state == EntState::Rush ||
                        (smart && ball.holder == qb && (d->state == EntState::Pursue || d->state == EntState::Spy) && threat < 8 &&
                         dot(d->vel, sub(q, d->pos)) > 0.5);
    if (!coming) continue;
    const double dd = dist(d->pos, e->pos);
    if (dd > reach) continue;
    if (smart && claimed(d)) continue;
    const double blockers = static_cast<double>(d->engagedBy.size());
    const double s = (smart && !lineman ? dd * 0.5 + threat * 0.9 : dd * 0.9 + threat * 0.5) + blockers * 3;
    if (s < bs) {
      bs = s;
      best = d;
    }
  }
  e->blockTarget = best;
  const V2 home{(std::min)(los - 1.4, q.x + 2.8), e->pos.y * 0.3 + (by + (e->pos.y - by) * 1.15) * 0.7};
  if (best) {
    // kick-slide: set up on the rusher's projected path to the QB, not where he is now
    // (half a second ahead and a long arm: with 0.35 s and a reach of 0.45 yd an end on the arc ran past the tackle's
    // set untouched on a tenth of all dropbacks - 1,329 first pressures by an unblocked end in 200 games)
    const V2 ahead = add(best->pos, scale(best->vel, tune().setLead));
    const V2 lane = norm(sub(q, ahead));
    const V2 setPt = add(ahead, scale(lane, 1.0));
    e->want = scale(norm(sub(setPt, e->pos)), (std::min)(e->vmax, dist(setPt, e->pos) * 6));
    if (dist(best->pos, e->pos) < e->radius + best->radius + tune().setReach) engage(e, best);
  } else {
    e->want = scale(sub(home, e->pos), 2);
    if (len(e->want) > e->vmax * 0.5) e->want = scale(norm(e->want), e->vmax * 0.5);
    // nobody coming at him: an interior lineman helps on the rusher already locked up beside him (the double team -
    // a center or guard left uncovered by a four-man rush). Only a man who has set (half a second in), on a rusher
    // within 2 yd who has one blocker.
    if (tune().helpBlock > 0 && e->isOL && !(e->role == "LT" || e->role == "RT") && t > 0.3) {  // (a tackle has the edge: he does not help inside)
      Ent* help = nullptr;
      double hd = 3.0;
      for (Ent* d : def) {
        if (d->state != EntState::Rush || d->engagedBy.size() != 1 || d->stun > 0 || contains(e->beatBy, d)) continue;
        if (d->engagedBy[0]->state != EntState::PassBlock) continue;
        const double dd = dist(d->pos, e->pos);
        if (dd < hd) {
          hd = dd;
          help = d;
        }
      }
      if (help) {
        e->want = scale(norm(sub(help->pos, e->pos)), (std::min)(e->vmax * 0.6, hd * 4));
        if (hd < e->radius + help->radius + 0.25) engage(e, help);
      }
    }
  }
}

void PlaySim::thinkRunBlock(Ent* e) {
  if (cfg.kick && carrier && carrier->side == e->side) {
    // kick return blocking: take the cover man who threatens the returner most and step into his path (intercept),
    // not where he is now: coverage arrives at full speed
    if (e->engaged) return;
    Ent* c = carrier;
    Ent* best = nullptr;
    double bs = 1e9;
    for (Ent* d : def) {
      if (!d->free() || contains(e->beatBy, d)) continue;
      const double de = dist(d->pos, e->pos), dc = dist(d->pos, c->pos);
      if (de > 14 || d->pos.x < c->pos.x - 1.5) continue;  // behind the returner: nothing to block
      const double s = de + dc * 0.7 + static_cast<double>(d->engagedBy.size()) * 6;
      if (s < bs) {
        bs = s;
        best = d;
      }
    }
    e->blockTarget = best;  // (the returner reads it: he stays behind a man on his way to a block)
    if (best) {
      const V2 path = add(best->pos, scale(norm(sub(c->pos, best->pos)), 1.2));  // between him and the returner
      const V2 ip = interceptPoint(e->pos, path, best->vel, e->vmax, 2);
      e->want = scale(norm(sub(ip, e->pos)), e->vmax);
      if (dist(best->pos, e->pos) < e->radius + best->radius + 0.55) engage(e, best);
    } else {
      e->want = scale(norm(sub(V2{c->pos.x + 4, c->pos.y}, e->pos)), e->vmax * 0.8);  // lead the way
    }
    return;
  }
  const OffPlay& play = *cfg.offPlay;
  const std::optional<RunScheme>& run = play.run;
  Ent* c = carrier;
  if (c && c->side == e->side && trailPlay(e, c)) return;
  // a combo block: two on the down lineman, one of them coming off it to the second level (Tune::comboBlock)
  if (e->combo >= 0 && thinkCombo(e)) return;
  if (e->engaged) return;
  // a pull, a kick-out, a lead block (RunScheme::jobs)
  if (thinkRunJob(e)) return;
  const double aimDy = runAimDy();
  const double playDir = jsSign(orIfZero(aimDy, 0.001));
  Ent* best = nullptr;
  double bs = 1e9;
  const bool isLineman = e->isOL || isTERole(e->role);
  const double zoneShift = run && (run->style == RunStyle::Zone || run->style == RunStyle::Stretch) ? playDir * 1.4 : 0;
  const V2 myPt{e->pos.x + 1.5, e->pos.y + zoneShift};
  Ent* as = e->blockTarget;
  const bool smart = tune().smartBlock > 0;
  const bool ours = c && c->side == e->side;
  // where the ball is going: the carrier once he has it, the play's aiming point until then
  const V2 hole = ours ? c->pos : V2{los + 1.0, by + aimDy};
  bool asOk = as && as->state != EntState::Down && !as->engaged && as->stun <= 0 && dist(as->pos, e->pos) < (smart && !isLineman ? 11 : 7);
  // his man is out of the play (the ball is by him and going away): he leaves him for someone who can still make it
  if (asOk && smart && ours && c->pos.x > as->pos.x + 1.5 && dist(as->pos, c->pos) > 4.5) asOk = false;
  if (asOk) {
    best = as;
    bs = -1;
  }
  // a man another free blocker already has, and is nearer to: not mine (two on one and the next man unblocked)
  auto claimed = [&](const Ent* d) {
    for (const Ent* o : off)
      if (o != e && o->state == EntState::RunBlock && !o->engaged && o->stun <= 0 && o->blockTarget == d &&
          dist(o->pos, d->pos) <= dist(e->pos, d->pos))
        return true;
    for (const Combo& k : combos_)
      if (!k.done && k.lb == d && k.a != e && k.b != e) return true;  // (a combo's backer: one of the two is coming for him)
    return false;
  };
  for (Ent* d : def) {
    if (bs < 0) break;
    if (!d->free()) continue;
    const double dd = dist(d->pos, myPt);
    const double range = isLineman ? 4.5 : 9;
    if (dd > range) continue;
    if (smart && claimed(d)) continue;
    // prefer defenders in front, and those not already blocked
    double s = dd + static_cast<double>(d->engagedBy.size()) * 4 + (d->pos.x < e->pos.x - 0.5 ? 3 : 0);
    if (!isLineman && c) s += dist(d->pos, c->pos) * 0.5;
    else if (smart) s += dist(d->pos, hole) * 0.3;  // (the man nearer the hole before the one further from it)
    if (s < bs) {
      bs = s;
      best = d;
    }
  }
  // nobody in reach: climb to the man who matters - the free defender nearest the ball's path that nobody has -
  // on the angle that cuts him off (the old climb ran straight up the field and blocked whoever it bumped into)
  if (!best && smart) {
    double cs = 1e9;
    for (Ent* d : def) {
      if (!d->free() || d->pos.x < e->pos.x - 0.5 || claimed(d)) continue;
      const double de = dist(d->pos, e->pos);
      if (de > 13) continue;
      const double s = dist(d->pos, hole) * 0.6 + de * 0.5;
      if (s < cs) {
        cs = s;
        best = d;
      }
    }
  }
  if (smart) e->blockTarget = best;
  if (best) {
    V2 contact = add(best->pos, scale(norm(sub(e->pos, best->pos)), 0.6));
    double sp = e->vmax;
    if (smart) {
      // leverage: meet him where he is going, on the side the ball is on (between him and the hole), under control
      // from a few yards out
      const double de = dist(best->pos, e->pos);
      const V2 at = interceptPoint(e->pos, best->pos, best->vel, e->vmax, (std::min)(1.2, de / (std::max)(1.0, e->vmax)));
      const V2 side = norm(add(norm(sub(e->pos, at)), scale(norm(sub(hole, at)), 0.7)));
      contact = add(at, scale(side, 0.6));
      if (de > 4.5) sp = e->vmax * 0.9;
    }
    V2 dir = norm(sub(contact, e->pos));
    // (a man climbing off a combo goes round the bodies at the line, not into the back of the pile he left)
    const bool climbing = e->climbFor == best && best;
    if (climbing && dist(best->pos, e->pos) > 1.5) dir = steerAround(e, dir, 2.5, best);
    e->want = scale(dir, sp);
    // (he comes off the combo with his hands up and the angle the double gave him: a fit from a step further out)
    if (dist(best->pos, e->pos) < e->radius + best->radius + (climbing ? tune().comboReach : 0.3)) engage(e, best);
  } else {
    // climb to second level along the play side
    e->want = scale(norm(V2{1, playDir * 0.35}), e->vmax * 0.7);
  }
}

double PlaySim::paMeshT() const {
  // when the ball is shown in the back's belly: as long as it takes him to get to the quarterback's hip from where he
  // lined up (the quarterback comes back to meet him) - half a second from the gun, about one from 7 yd deep in the I
  // (a fixed 0.55 s had the fake pulled with an I-formation tailback still 3.6 m short of the mesh)
  if (paMeshT_ < 0) {
    const Ent* rb = offRole("RB");
    const double d = rb && qb ? (std::max)(0.0, dist(rb->pos, qb->pos) - 1.2) : 1.0;
    // (the time he takes from his stance at the human acceleration law, a = (v - speed) / tau, at 85 % of his top speed:
    // about 1.05 s from 7 yd deep in the I, as the pack's I-formation fake has it (1.17 s from its start); a guessed
    // speed had the quarterback pull the ball 1.8 yd before the back got there)
    const double v = rb ? rb->vmax * 0.85 : 7.0, tau = rb ? (std::max)(0.4, rb->tau) : 0.8;
    double tm = 0.45;
    while (tm < 1.3 && v * (tm - tau * (1 - std::exp(-tm / tau))) < d) tm += 0.01;
    paMeshT_ = clamp(tm, 0.45, 1.3);
  }
  return paMeshT_;
}

bool PlaySim::paBoot() const {
  // a bootleg: the play is one ("PA Boot", "PA Boot Over")
  const OffPlay* p = cfg.offPlay;
  return p && p->type == PlayType::PlayAction && (p->name.find("Boot") != std::string::npos || p->id.find("boot") != std::string::npos);
}

double PlaySim::qbSetY() const {
  // the boot: he sells the run one way and rolls out the other, to set up 6-7 yd outside where the ball was snapped
  return paBoot() ? clamp(by - paSide() * 6.5, 6.0, FIELD_W - 6.0) : by;
}

bool PlaySim::thinkPaBack(Ent* e) {
  // the back sells the fake: to the mesh at the quarterback's hip on the run side, through it toward the line as though
  // he had the ball, then to his job (his block or his route). He stood in his pass set or ran his route from the snap,
  // and the fake was the quarterback alone.
  const OffPlay& play = *cfg.offPlay;
  if (play.type != PlayType::PlayAction || e->role != "RB" || t > paPullT() + 0.15 || ball.holder != qb || e->userControlled || tune().paBack <= 0) return false;
  if (tune().mesh > 0) {
    // the run's own mesh (PlaySim::paMesh): down the run's path at the run's pace, through the mesh point as the ball is
    // shown, and on at the line as if he had it
    const Mesh& m = paMesh();
    if (t < m.go) {
      e->want = V2{0, 0};
      return true;
    }
    const double along = dot(sub(e->pos, m.at), m.dir);
    if (!meshBurst) meshBurst = along > -0.4 || m.T - t <= -along / (std::max)(1.0, 0.5 * (e->speed() + e->vmax * m.tempo)) + 0.15;
    const V2 ahead = add(m.at, scale(m.dir, along + 1.5));
    e->want = scale(norm(sub(ahead, e->pos)), e->vmax * (meshBurst ? m.tempo : 0.4));
    return true;
  }
  const V2 q = qb->pos;
  const V2 mesh{q.x + 0.1, q.y + paSide() * 0.85};
  const V2 tgt = t < paMeshT() ? mesh : V2{los - 0.8, q.y + paSide() * 2.5};
  e->want = scale(norm(sub(tgt, e->pos)), e->vmax * 0.85);
  return true;
}

double PlaySim::fakeBite(const Ent* d) const {
  // the front reads the mesh: a lineman sees run first on a play-action fake and plays it until the quarterback pulls
  // the ball (0.85 s) and his own read catches up; the less he recognises plays, the longer he stays fooled
  if (!cfg.offPlay || cfg.offPlay->type != PlayType::PlayAction || ball.mode == BallMode::Air || tune().paBite <= 0) return 0;
  const double fooled = 0.35 + 0.65 * (99 - d->r().playRec) / 99;
  const double end = paPullT() + d->reactT * (0.6 + fooled);
  if (t >= end) return 0;
  return clamp(tune().paBite * fooled * (t < paPullT() ? 1.0 : (end - t) / (end - paPullT())), 0.0, 1.0);
}

void PlaySim::updateReads(double dt) {
  // read keys (Tune::readKeys): what the offense shows - not what it called
  if (tune().readKeys <= 0 || cfg.kick || cfg.place || !qb || phase != SimPhase::Live) return;
  // the line: low, firing out at the defense is run; high hats, hands up in a set is pass. The draw's line shows pass
  // (drawSet); play action's fires out to sell the fake while it lasts, as far as a line that may not go downfield can.
  // (Where the linemen go tells it badly here: the sim's pass sets step up to meet the rush, and a pile goes the way
  // whoever is winning it pushes.)
  {
    const OffPlay& op = *cfg.offPlay;
    const bool draw = op.run && op.run->style == RunStyle::Draw && t < tune().drawSet;
    const bool fake = op.type == PlayType::PlayAction && t < paPullT();
    double s = 0;
    int n = 0;
    for (const Ent* o : off)
      if (o->isOL) {
        s += draw ? -1.0 : fake ? tune().keyPaLine : o->state == EntState::RunBlock ? 1.0 : o->state == EntState::PassBlock ? -1.0 : 0.0;
        n++;
      }
    olKey_ = n ? s / n : 0.0;
  }
  // the backfield: the ball in a back's hands (or shown in his belly as he goes by the quarterback at the mesh) is run;
  // the quarterback set with it, or still going back deep with nobody at his hip, is pass (his first steps back from
  // under center say nothing: he takes them to hand off too)
  const Ent* h = ball.holder;
  double bk = 0;
  if (passThrown || ball.mode == BallMode::Air) {
    bk = -1;
  } else if (h && h->side == Side::Off && h != qb) {
    bk = 1;
  } else if (h == qb) {
    bool atMesh = false;
    for (const Ent* o : off)
      if (o != qb && isBackRole(o->role) && dist(o->pos, qb->pos) < 2.0 && o->speed() > 3.0 && o->vel.x > -0.5) atMesh = true;
    if (carrier == qb && !scramble) bk = 1;  // (a designed run: he has taken off with it)
    else if (atMesh) bk = 0.8;
    else if (dropDone || (qb->vel.x < -1.0 && qb->pos.x < los - 5.5)) bk = -0.8;
  }
  backKey_ = bk;
  // (the ball in a back's hands is the whole story: nothing the line showed changes it)
  const double target = bk >= 1 ? 1.0 : clamp(0.5 + 0.5 * (tune().keyOL * olKey_ + tune().keyBack * backKey_), 0, 1);
  for (Ent* d : def) {
    if (!d->rkSet) {
      d->rkSet = true;
      d->rkOn = isLB(d) || (tune().keySafety > 0 && (d->role == "FS" || d->role == "SS") && d->pos.x - los < 9);
    }
    if (!d->rkOn) continue;
    const double tau = tune().keyTau + (99 - d->r().playRec) * tune().keyTauK;
    d->rkRun += (target - d->rkRun) * (std::min)(1.0, dt / (std::max)(0.02, tau));
  }
}

bool PlaySim::keyReader(const Ent* e) const {
  return tune().readKeys > 0 && e->rkOn && !e->userControlled && !cfg.kick && !cfg.place;
}

bool PlaySim::keyStep(const Ent* e) const { return t < e->reactT + tune().keyStepT; }

double PlaySim::keyBite(const Ent* e) const {
  // a run read on a play the ball has not been given on: he comes up for it (the fake working on him)
  if (ball.mode == BallMode::Air || (ball.holder && ball.holder->side == Side::Off && ball.holder != qb)) return 0;
  return clamp((e->rkRun - 0.5) * 2 * tune().keyBiteK, 0.0, 1.0);
}

void PlaySim::thinkRush(Ent* e) {
  if (e->engaged) return;
  Ent* holder = ball.holder;
  // run read: once a handoff is seen (after reaction time), rushers play the run
  if (runRead && t - runReadT > e->reactT * 0.7 && holder && holder != qb) {
    e->state = EntState::Pursue;
    return;
  }
  if (holder && holder->side == Side::Off && holder->state == EntState::Carrier && holder != qb) {
    e->state = EntState::Pursue;
    return;
  }
  if (ball.mode == BallMode::Air) {
    // hands up / turn and chase
    if (t - throwStart > e->reactT) e->state = EntState::Pursue;
    e->want = V2{0, 0};
    return;
  }
  const V2 q = qb->pos;
  // the zone read's end, left unblocked: he squeezes down the line after the back, or stays home, square, for the
  // quarterback (endSqueeze_, rolled at the snap)
  // (a squeezing end the quarterback kept it on is committed: his feet are going inside after the back for a beat
  // before he can turn and chase - that beat is the whole point of reading him)
  if (e == readEnt && cfg.offPlay->run && cfg.offPlay->run->read == "end" && holder == qb && meshOn() &&
      (!qbKept || (endSqueeze_ && t < runReadT + 0.25 + e->reactT))) {
    // (the squeeze is flat down the line into the B gap after the back, not up the field at the mesh)
    const double outside = orIfZero(jsSign(e->pos.y - by), 1);
    const V2 tgt = endSqueeze_ ? V2{los + 0.2, by + outside * 1.2} : V2{los + 0.6, e->pos.y + outside * 0.3};
    e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax * (endSqueeze_ ? 0.9 : 0.5), dist(tgt, e->pos) * 3 + 0.3));
    return;
  }
  // a line stunt: the penetrator across first, the looper round behind him (Tune::stunts)
  if (thinkStunt(e)) return;
  // edge rushers arc around the tackle
  const bool isEdge =e->role == "LE" || e->role == "RE" || (!e->isDL && std::fabs(e->gap - by) > 4);
  V2 tgt = q;
  if (isEdge && e->pos.x > q.x + 2.5) {
    const double side = orIfZero(jsSign(e->pos.y - by), 1);
    tgt = V2{q.x + 1.0, q.y + side * 2.6};
  }
  // play action: a fooled rusher squeezes the mesh (the back) and comes off it late
  if (const double bite = fakeBite(e); bite > 0) {
    // (he plays the run at the line - his gap where the back is coming, not the mesh behind it: with the back now
    // carrying out the fake at the quarterback's hip, squeezing "the back" sent every fooled rusher at the passer,
    // and play action was sacked 13 %)
    if (Ent* rb = offRole("RB")) tgt = add(scale(tgt, 1 - bite), scale(V2{los + 0.4, rb->pos.y + paSide() * 1.5}, bite));
    e->want = scale(norm(sub(tgt, e->pos)), e->vmax * (1 - 0.55 * bite));
    return;
  }
  e->want = scale(norm(sub(tgt, e->pos)), e->vmax);
}

V2 PlaySim::delayedVel(const Ent* e, double lag) const {
  const double n = jsRound(lag * 60);
  const std::deque<V2>& h = e->velHist;
  if (static_cast<double>(h.size()) > n) return h[static_cast<size_t>(static_cast<double>(h.size()) - 1 - n)];
  return h.empty() ? e->vel : h[0];
}

double PlaySim::manCutTime(const Ent* d, const Ent* r, const V2& cut) const {
  // Madden 26's cut reaction: the receiver's route running against the man's coverage and his feet; a cut into his
  // leverage he is already standing in, one away from it he has to open his hips to
  const double lev = jsSign(d->pos.y - r->pos.y);  // (the side of the receiver he is on)
  const double into = cut.y * lev > 0.3 ? -1.0 : cut.y * lev < -0.3 ? 1.0 : 0.0;
  const double base = tune().manCutBase + (r->r().routeRunning - d->r().manCov) * tune().manCutRating + (80 - d->r().agility) * tune().manCutAgi +
                      into * tune().manCutLev;
  // (abilities, as the old read had them: Blanket / Lockdown answer sooner, Route Artist / Slipstream make it later)
  return clamp(base * d->fx.coverLag + r->fx.sepLag, 0.04, 0.8);
}

void PlaySim::thinkMan(Ent* e) {
  if (reactToBall(e)) return;
  Ent* r = e->man;
  if (!r || r->state == EntState::PassBlock || r->state == EntState::RunBlock || r->state == EntState::Down) {
    // receiver stayed in to block: rush or robber
    if (t > 0.6) {
      e->state = EntState::Zone;
      e->zone = "hookM";
      e->zoneSpot = zoneLandmark("hookM");
    }
    e->want = V2{0, 0};
    return;
  }
  // (abilities: Blanket / Lockdown read him sooner; Route Artist / Slipstream on the receiver make the read late)
  const double lag = clamp((0.1 + (99 - e->r().manCov) * 0.006 + (99 - e->r().agility) * 0.002) * tune().manLagK * e->fx.coverLag + r->fx.sepLag, 0.02, 1.2);
  const V2 rv = delayedVel(r, lag);
  // trail with inside/over leverage: stay slightly deeper (+x) on deep routes
  const double depth = r->pos.x - los;
  // a man defender who has to turn and run (human hips: no instant flip) plays with more cushion and lead downfield
  const double cushion = depth > 8 ? tune().manCushion + (e->pos.x < r->pos.x ? 1 : 0) : 0.3;
  const double inside = jsSign(by - r->pos.y) * 0.4;
  // route recognition: a good cover man reads the receiver's hips and plays where the route is going (the same
  // physical path the QB reads), a poor one chases where he has been
  const double read = clamp((e->r().manCov - 55) / 40, 0, 1);
  if (tune().manCue > 0 && !e->userControlled && r->state == EntState::Route) {
    // the body, not the route (Tune::manCue): he plays the receiver going the way he has seen him go - not where the
    // route goes next, which he cannot know (predictPos showed him the break, a double move's real one included, 0.45 s
    // before it came)
    const V2 rvNow = r->vel;
    const double rs = len(rvNow);
    if (len(e->mcDir) < 0.5 || (depth < 2.5 && e->mcSeenT < 0)) {
      // (his release off the line is no break: the man on him mirrors it step for step)
      if (rs > 1.0) e->mcDir = norm(rvNow);
    } else if (rs > 1.5) {
      const V2 h = norm(rvNow);
      const double offDeg = std::acos(clamp(dot(h, e->mcDir), -1, 1)) * 180 / kPi;
      // (a rounded cut is a cut too: turned that far from where he was going a fifth of a second ago)
      const V2 before = delayedVel(r, 0.2);
      const double turnDeg = len(before) > 1.5 ? std::acos(clamp(dot(h, norm(before)), -1, 1)) * 180 / kPi : 0.0;
      if (e->mcSeenT < 0) {
        if (offDeg > tune().manBreakDeg || turnDeg > tune().manBreakDeg) {
          // a break: seen now, answered after his cut reaction
          e->mcSeenT = t;
          e->mcNext = h;
          e->mcReact = manCutTime(e, r, h);
        } else {
          // (a route's drift and bend he runs with, a quarter second behind it: a rounded cut that keeps turning is a
          // break once it has turned far enough from where he thought the man was going)
          e->mcDir = norm(add(scale(e->mcDir, 0.94), scale(h, 0.06)));
        }
      } else if (offDeg < tune().manBreakDeg * 0.5) {
        e->mcSeenT = -1;  // (a wobble, not a break)
      } else {
        e->mcNext = h;  // (still turning: he answers the cut where it ends up)
      }
    }
    // the gather: a receiver braking hard for a cut shows it. He reads the break coming the way his leverage and the stem
    // say (inside leverage: the out; a deep stem sinking: back to the ball) and sits on it
    if (!e->mcSank && e->mcSeenT < 0 && depth > 2.5) {
      const double was = len(delayedVel(r, 0.2));
      if (was > 4.0 && rs < was * 0.8) {
        const double lev = orIfZero(jsSign(e->pos.y - r->pos.y), jsSign(by - r->pos.y));
        e->mcSank = true;
        e->mcPrior = depth < 9 ? norm(V2{0.3, -lev}) : norm(V2{-0.5, -lev});
      }
    }
    // answered: he turns with the break and drives on it (the harder when it went where he read it going). A break that
    // comes while he drives waits for the drive to end: the false step
    if (e->mcSeenT >= 0 && t - e->mcSeenT >= e->mcReact && t >= e->mcDriveEnd) {
      const bool expected = e->mcSank && dot(e->mcNext, e->mcPrior) > 0.5;
      e->mcDir = e->mcNext;
      e->mcSeenT = -1;
      e->mcDriveEnd = t + tune().manDriveT;
      e->mcBite = expected ? 0.5 + 0.5 * read : 0.4;
      e->mcSank = false;
    }
    V2 cue = add(r->pos, scale(e->mcDir, rs * tune().manLead * (depth > 12 ? 1.43 : 1.0)));
    if (t < e->mcDriveEnd) cue = add(cue, scale(e->mcDir, tune().manDriveLead * e->mcBite));
    else if (e->mcSank && e->mcSeenT < 0) cue = add(cue, scale(e->mcPrior, 0.6 * read));
    // (the cushion as the old read had it, its fitted part manCueCushion: he no longer gets the break early to make up
    // for playing off)
    const double cush = depth > 8 ? tune().manCueCushion + (e->pos.x < r->pos.x ? 1 : 0) : 0.3;
    const V2 tgt = add(cue, V2{cush, inside});
    const double d = dist(tgt, e->pos);
    e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, d * 4 + rs));
    if (const double bite = keyReader(e) ? keyBite(e) : fakeBite(e); bite > 0) e->want = scale(e->want, 1 - 0.45 * bite);
    return;
  }
  const V2 seen = add(r->pos, scale(rv, depth > 12 ? 0.5 : 0.35));
  const V2 ahead = r->state == EntState::Route ? predictPos(r, 0.45) : seen;
  const V2 tgt = add(add(scale(seen, 1 - read * tune().routeRead), scale(ahead, read * tune().routeRead)), V2{cushion, inside});
  const double d = dist(tgt, e->pos);
  e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, d * 4 + len(rv)));
  // play action: a man defender who reads run peeks into the backfield and gives his receiver a step (how long, by his
  // play recognition: fakeBite; a linebacker's or box safety's own run read with read keys)
  if (const double bite = keyReader(e) ? keyBite(e) : fakeBite(e); bite > 0) e->want = scale(e->want, 1 - 0.45 * bite);
}

void PlaySim::thinkZone(Ent* e) {
  if (reactToBall(e)) return;
  V2 spot = e->zoneSpot ? *e->zoneSpot : zoneLandmark(e->zone.empty() ? std::string("hookM") : e->zone);
  const bool deep = !e->zone.empty() && isDeepZone(e->zone);
  // long yardage: the underneath zones sink toward the sticks and give up the short throw (2nd and 12 is played to
  // make it 3rd and 6, not to stop it for nothing - which is how a third of third downs came to be 10+ to go)
  if (!deep && tune().softZone > 0 && cfg.down.value_or(1) >= 2 && cfg.toGo.value_or(10) >= 10)
    spot.x += (std::min)(4.0, (cfg.toGo.value_or(10) - 7) * 0.5) * tune().softZone;
  // the match rules: a zone that plays the receivers' numbers (Tune::matchCov)
  if (matchZone(e, spot)) return;
  // find receivers threatening my area
  Ent* threat = nullptr;
  double tScore = 1e9;
  const double radius = deep ? 17 : 7;
  for (Ent* r : off) {
    if (r->state != EntState::Route) continue;
    double dd = deep ? std::fabs(r->pos.y - spot.y) : dist(r->pos, spot);
    // eyes on the quarterback: the receiver he is looking at pulls an underneath defender his way (once the drop is
    // under way: a look at the snap is not yet a throw)
    const double eyes = !deep && r == qbLook && t > 1.0 ? tune().zoneEyes : 0;
    if (dd > radius + eyes) continue;
    dd -= eyes;
    if (deep && r->pos.x < los + 5) continue;
    // deep defenders: deepest threat heading into my area (velocity toward my landmark counts)
    const double toward = deep ? dot(r->vel, norm(V2{0, spot.y - r->pos.y})) : 0;
    const double s = deep ? -r->pos.x - r->vel.x * 0.6 + dd * 0.25 - toward * 0.8 : dd;
    if (s < tScore) {
      tScore = s;
      threat = r;
    }
  }
  V2 tgt = spot;
  if (threat) {
    const double lag = clamp((0.15 + (99 - e->r().zoneCov) * 0.008) * e->fx.zoneLag + threat->fx.sepLag * 0.5, 0.02, 1.2);
    const V2 rv = delayedVel(threat, lag);
    const V2 tp = add(threat->pos, scale(rv, 0.45));
    if (deep) {
      // stay over the top: cushion grows with the receiver's speed (bail technique)
      const double cushion = tune().zoneCushion + (tune().zoneVert > 0.5 ? (std::max)(0.0, rv.x) : threat->speed()) * tune().zoneCushionV;
      tgt = V2{(std::min)((std::max)(spot.x - 2, tp.x + cushion), DEEP_AIM), spot.y * 0.3 + tp.y * 0.7};
    } else {
      // sit underneath, between receiver and QB, but don't leave the zone too far
      const V2 toQB = norm(sub(qb->pos, tp));
      const V2 under = add(tp, scale(toQB, tune().zoneUnder));
      const V2 offs = sub(under, spot);
      const double ol = len(offs);
      tgt = ol > radius * 0.8 ? add(spot, scale(offs, (radius * 0.8) / ol)) : under;
    }
  } else if (!deep) {
    // eyes on QB: drift toward where the QB is looking (crudely: current read)
    tgt = spot;
  }
  // play action: the run fake read as a run. Underneath defenders fill downhill toward the back's gap, a safety takes his
  // false steps up; each comes back to his drop once his read catches up (fakeBite: his play recognition and reaction -
  // a sharp linebacker for a beat past the pull, a poor one well after it). It was the underneath men only, a 2 yd
  // step for the 0.85 s of the fake, whatever their read, and nobody stayed fooled.
  // (read keys: a linebacker's or box safety's own run read does it, on whatever play made him read run - keyBite)
  if (const double bite = keyReader(e) ? keyBite(e) : fakeBite(e); bite > 0) {
    const Ent* rb = offRole("RB");
    const V2 fill{los + (deep ? 7.0 : 1.5), rb ? rb->pos.y : by};
    const double k = deep ? 0.3 * bite : bite;
    tgt = add(scale(tgt, 1 - k), scale(fill, k));
  }
  const double d = dist(tgt, e->pos);
  e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, d * (deep ? 5 : 3.5)));
}

/** Shared defensive reaction: run read, ball in air, ball carrier. Returns true if handled. */
bool PlaySim::reactToBall(Ent* e) {
  const Ball& b = ball;
  if (b.mode == BallMode::Air && b.target != nullptr) {
    if (t - throwStart > (e->state == EntState::Man ? e->reactT + 0.12 : e->reactT * tune().zoneReactK)) {
      V2 spot = landingFor(e);
      // the trail man, his back to the ball: he runs with his man (at his hip) and goes for the catch point only when the
      // receiver's hands come up, handsT before it gets there
      if (tune().eyesOn > 0 && e->state == EntState::Man && e->man == b.target && b.flightT - b.t > tune().handsT && !eyesOnBall(e))
        spot = add(b.target->pos, scale(b.target->vel, 0.3));
      const double dd = dist(spot, e->pos);
      e->want = scale(norm(sub(spot, e->pos)), (std::min)(e->vmax, dd * 4 + 0.5));
      return true;
    }
    return false;
  }
  Ent* h = b.holder;
  // read keys (Tune::readKeys): what he reads from the line and the backfield, not the play's type - a run read, or still
  // reading (his read step, every snap); off, the play's type said run
  const bool keys = keyReader(e);
  // (keyZoneFill: a run read with the ball not yet given is played as play action's bite was - downhill at the back -
  // not in the read step's hold)
  const bool runLook = keys ? keyStep(e) || (tune().keyZoneFill > 0 ? runRead : e->rkRun > 0.5) : cfg.offPlay->type == PlayType::Run;
  // run keys: a linebacker reads the line, not the handoff. Linemen firing out in run blocks say run, and he fills
  // downhill along the back's flow (his own gap's side of it) before the ball is handed off. Keyed on the handoff,
  // half of them were still in their zone drops a second into a run, 4.7 yd deep and 8 yd from the ball: runs past the
  // line ran on to the safeties (first contact 4-9 yd: safeties 58-80 %), 4-6 yd gains 17 % against the NFL's 25 %.
  if (tune().lbFill > 0 && isLB(e) && (keys ? e->rkRun > 0.5 : runLook) && !runRead && !e->engaged && e->engagedBy.empty() &&
      t > e->reactT + tune().lbKeyT && b.mode != BallMode::Air) {
    const Ent* rb = cfg.offPlay->run ? offRole(cfg.offPlay->run->carrier) : offRole("RB");  // (the run's back: a jet man, a QB)
    const double flowY = rb ? rb->pos.y + rb->vel.y * 0.6 : by + runAimDy();
    const V2 fit{(std::max)(los + 1.2, (std::min)(e->pos.x, los + 3.0)), flowY + (e->gap - by) * 0.4};
    const double d = dist(fit, e->pos);
    e->want = scale(norm(sub(fit, e->pos)), (std::min)(e->vmax * tune().lbFill, d * 3 + 0.4));
    return true;
  }
  // read step: until he has read the run, a zone linebacker on a run play does not bail into his drop - he holds his
  // depth (steps up if he is deeper than 4.5 yd) and shuffles with the back's flow, so when he triggers he is over the
  // ball, not 5 yd wide and 5 deep in pursuit.
  // (read keys: from the snap, on every play - the hold while he reads it is the step every linebacker takes)
  if (tune().lbRead > 0 && isLB(e) && e->state == EntState::Zone && runLook &&
      (!runRead || t - runReadT <= e->reactT) && !e->engaged && e->engagedBy.empty() && (keys || t > e->reactT) &&
      b.mode != BallMode::Air && !(keys && (passThrown || scramble))) {
    const Ent* rb = cfg.offPlay->run ? offRole(cfg.offPlay->run->carrier) : offRole("RB");  // (the run's back: a jet man, a QB)
    const double flowY = rb ? rb->pos.y + rb->vel.y * 0.6 : by + runAimDy();
    // (gap integrity: he shuffles over his own gap, giving ground to the flow no more than gapGive)
    const double ty = tune().gapFit > 0 && tune().gapFit < 2 && e->fitOn ? e->fitY + clamp(flowY - e->fitY, -tune().gapGive, tune().gapGive) : flowY + (e->gap - by) * 0.6;
    // (read keys: the ball shown at the mesh, not yet given, is what a linebacker comes downhill for - on a run for the
    // beat before the handoff, on play action for as long as the fake lasts: keyFill yd at a full run read)
    const bool shown = keys && backKey_ > 0 && backKey_ < 1;
    const double hold = 4.5 - (shown ? tune().keyFill * keyBite(e) : 0.0);
    const V2 w{(std::min)(0.0, (los + hold - e->pos.x) * 2.0), (ty - e->pos.y) * 2.0};
    const double sp = std::sqrt(w.x * w.x + w.y * w.y);
    const double cap = e->vmax * tune().lbRead;
    e->want = sp > cap ? scale(w, cap / sp) : w;
    return true;
  }
  if (h && h->side == Side::Off && h->state == EntState::Carrier) {
    // (a man who read pass is a beat later to it: read keys say so from what he saw, the old read from the play's type)
    const bool rr = t - runReadT > e->reactT + ((keys ? e->rkRun > 0.5 : cfg.offPlay->type == PlayType::Run) ? 0 : 0.1);
    const bool crossed = h->pos.x > los + 0.5;
    // a scramble is read like anything else, after the man's reaction: a zone defender has his eyes on the quarterback
    // but is dropping - he has to see it, plant and come back downhill; a man defender stays on his receiver (the
    // plaster rule: leave him and the quarterback dumps it to him) until the quarterback is at the line. (Every defender broke on the tick he took off - man
    // coverage included - and met him at the line: scrambles went for under a yard against the NFL's 6.7.)
    if (scramble && h == qb && !crossed && !passThrown) {
      const double since = t - scrambleT;
      const bool go = e->state == EntState::Man    ? h->pos.x > los - 1.5 || since > e->reactT + 0.9
                      : e->state == EntState::Zone ? since > e->reactT + tune().scrZoneT
                                                   : since > e->reactT;
      if (!go) return false;
      e->state = EntState::Pursue;
      return true;
    }
    if (rr || crossed || h != carrier || passThrown || scramble) {
      // defenders in deep zones keep depth briefly on runs to avoid getting beat
      e->state = EntState::Pursue;
      return true;
    }
  }
  if (b.mode == BallMode::Loose) {
    e->want = scale(norm(sub(V2{b.pos.x, b.pos.y}, e->pos)), e->vmax);
    return true;
  }
  return false;
}

void PlaySim::thinkPursue(Ent* e) {
  const Ball& b = ball;
  if (b.mode == BallMode::Air) {
    const V2 spot = e->side == Side::Def ? landingFor(e) : ballLanding();
    e->want = scale(norm(sub(spot, e->pos)), (std::min)(e->vmax, dist(spot, e->pos) * 4 + 0.5));
    return;
  }
  if (b.mode == BallMode::Loose) {
    e->want = scale(norm(sub(V2{b.pos.x, b.pos.y}, e->pos)), e->vmax);
    return;
  }
  Ent* c = b.holder;
  if (!c || c->side == e->side) {
    e->want = V2{0, 0};
    return;
  }
  if (e->engaged) return;
  // gap integrity and the edge on a run, until the ball crosses the line (Tune::gapFit)
  if (fitRun(e, c)) return;
  // pursuit angle: aim where the carrier will be when we arrive (bounded lead)
  const double d = dist(e->pos, c->pos);
  const V2 ip = interceptPoint(e->pos, c->pos, c->vel, e->vmax, 3);
  const double tInt = dist(e->pos, ip) / e->vmax;
  const double k = tune().pursuitK + e->r().pursuit * 0.0035;
  // close in: cut the angle hard (aim almost at the body) instead of running parallel
  const double lead = (std::min)(tInt, (d / e->vmax) * 1.1 + 0.1) * k;
  V2 tgt = d < 1.2 ? c->pos : add(c->pos, scale(c->vel, lead));
  // last line of defense: deep defenders keep depth (stay over the top) until the carrier is close
  if (e->deepRole && c->side == Side::Off && d > 3.5 && tune().deepKeep > 0) {
    const double keep = (std::min)(c->pos.x + (std::min)(4.0, 1 + d * 0.3) * tune().deepKeep, DEEP_AIM);
    if (tgt.x < keep) tgt = V2{keep, tgt.y};
  }
  // break down: close under control in the last few yards (a pursuer at full tilt overruns a cut and misses)
  double sp = e->vmax;
  if (d < 3.5) sp = (std::min)(e->vmax, c->speed() + tune().breakdownBase + d * tune().breakdownK);
  V2 dir = norm(sub(tgt, e->pos));
  // scrape: run around the bodies in the way, not into them. Aimed straight at the ball, a linebacker ran into the
  // blocks at the line and the bump took his speed every tick: free pursuers on runs went 3-4 yd/s at 1.5-2 s (top
  // speed 9.5), a quarter to half of them with a blocker within 1.5 yd, and the run reached the safeties.
  if (tune().pursueAvoid > 0 && d > 2.0) dir = steerAround(e, dir, 1.5 + 2.0 * tune().pursueAvoid, c);
  e->want = scale(dir, sp);
}

V2 PlaySim::steerAround(const Ent* e, const V2& dir, double look, const Ent* ignore) const {
  const Ent* hit = nullptr;
  double best = look;
  for (const Ent* o : ents) {
    if (o == e || o == ignore || o->state == EntState::Down) continue;
    const V2 r = sub(o->pos, e->pos);
    const double along = dot(r, dir);
    if (along <= 0.1 || along > best) continue;
    const double lo = r.x * -dir.y + r.y * dir.x;  // its offset to the left of the path
    if (std::fabs(lo) < e->radius + o->radius + 0.3) {
      best = along;
      hit = o;
    }
  }
  if (!hit) return dir;
  const V2 r = sub(hit->pos, e->pos);
  const double lo = r.x * -dir.y + r.y * dir.x;
  const V2 left{-dir.y, dir.x};
  const double side = lo > 0 ? -1.0 : 1.0;  // pass it on the side it is not on
  const double k = 0.35 + 1.1 * (1 - best / look);
  return norm(add(dir, scale(left, side * k)));
}

/** Linemen cannot keep up with a breakaway: once the carrier is well past them, or they are deep downfield,
 *  they let go of their block and trail the play at a jog instead of blocking 60 yards downfield. */
bool PlaySim::trailPlay(Ent* e, Ent* c) {
  const bool big = e->isOL || e->isDL || isTERole(e->role);
  if (!big || c == e) return false;
  const double gd = goalDir(c);
  const double past = (c->pos.x - e->pos.x) * gd;      // carrier ahead of me
  const double deep = (e->pos.x - los) * gd;            // how far I have climbed
  if (past < 5 && deep < 15) return false;
  if (e->engaged) disengage(e);
  e->want = scale(norm(sub(c->pos, e->pos)), e->vmax * 0.55);
  return true;
}

void PlaySim::thinkConvoy(Ent* e) {
  Ent* c = ball.holder;
  if (ball.mode == BallMode::Air) {
    const V2 spot = ballLanding();
    e->want = scale(norm(sub(spot, e->pos)), e->vmax * 0.7);
    return;
  }
  if (!c || c->side != e->side) {
    // turnover: our side must now tackle
    e->state = EntState::Pursue;
    return;
  }
  if (trailPlay(e, c)) return;
  if (e->engaged) return;
  const double gd = goalDir(c);
  // the scramble drill: the quarterback on the move behind the line can still throw - his receivers break off their
  // routes and work with him, toward the side he is rolling to (the deep ones back toward him, the short ones up the
  // field), getting open rather than running back to him
  if (c == qb && scramble && !passThrown && c->pos.x < los - 0.3 && e->eligible && e->routeName != "block") {
    const double roll = std::fabs(c->vel.y) > 1.0 ? jsSign(c->vel.y) : jsSign(orIfZero(c->pos.y - by, 1));
    const double depth = e->pos.x - los;
    const double wantX = depth > 14 ? los + 11 : depth < 5 ? los + 8 : e->pos.x;
    const V2 spot{wantX, clamp(e->pos.y + roll * 5.0, 3.0, FIELD_W - 3.0)};
    e->want = scale(norm(sub(spot, e->pos)), (std::min)(e->vmax * 0.85, dist(spot, e->pos) * 2.5 + 1.0));
    return;
  }
  // a block for the carrier: the defender ahead of the run (level or in front of the ball - a man chasing from behind
  // cannot be blocked) with the best shot at him, a man nobody nearer has already taken. He used to run back to the
  // carrier whenever no defender was within 8 yd of him, and on a scramble the receivers converged on the quarterback.
  Ent* best = nullptr;
  double bs = 1e9;
  for (Ent* d : defendersOf(e)) {
    if (!d->free() || d->state == EntState::Down) continue;
    if ((d->pos.x - c->pos.x) * gd < -1.0) continue;
    const double dc = dist(d->pos, c->pos);
    const double de = dist(d->pos, e->pos);
    if (de > 16 || dc > 22) continue;
    bool taken = false;
    for (Ent* o : teammatesOf(e))
      if (o != e && o->state == EntState::Convoy && o->blockTarget == d && dist(o->pos, d->pos) < de) taken = true;
    if (taken) continue;
    const double s = de + dc * 0.6;
    if (s < bs) {
      bs = s;
      best = d;
    }
  }
  e->blockTarget = best;
  if (best) {
    // between him and the ball: on the line from the defender to the carrier, a yard off the defender
    const V2 p = add(best->pos, scale(norm(sub(c->pos, best->pos)), 0.9));
    const double dp = dist(p, e->pos);
    e->want = scale(norm(sub(p, e->pos)), (std::min)(e->vmax, dp * 3 + 1.5));
    if (dist(best->pos, e->pos) < e->radius + best->radius + 0.25 && rng.chance(0.25)) engage(e, best);
  } else {
    // nobody to block: lead the run - out in front of him, not back at him
    const V2 lead{c->pos.x + gd * 6, c->pos.y + (e->pos.y - c->pos.y) * 0.5};
    e->want = (e->pos.x - c->pos.x) * gd > 4 ? scale(c->vel, 0.9) : scale(norm(sub(lead, e->pos)), e->vmax * 0.85);
  }
}

void PlaySim::thinkCarrier(Ent* e) {
  const double gd = goalDir(e);
  const double goal = gd > 0 ? 100 : 0;
  // the reverse: the first carrier sells the sweep flat along the line and brings the ball to the man coming back
  if (revEnt_ && !reversed_ && e != revEnt_ && e->side == Side::Off && !cfg.kick && t < meshDoneT + 2.2 && revEnt_->state != EntState::Down) {
    // (at the man coming across: where he will be in a beat)
    const V2 to = add(revEnt_->pos, scale(revEnt_->vel, 0.3));
    const V2 flat{(std::min)(to.x, los - 1.6), to.y};
    e->want = scale(norm(sub(flat, e->pos)), e->vmax * 0.7);
    return;
  }
  // a scrambling quarterback past the line gives himself up before the hit: feet first, the ball dead where he starts
  // down (decided once, when the first man is about to get to him; not at the goal line, and not a yard short of the
  // sticks, where he lowers his shoulder for it)
  if (e == qb && scramble && !slideRolled && !e->userControlled && e->pos.x > los + 0.5 && e->pos.x < 97 && !cfg.kick) {
    for (Ent* d : defendersOf(e)) {
      if (!d->free() || d->state == EntState::Down) continue;
      const V2 rel = sub(d->pos, e->pos);
      const double dd = len(rel);
      const double closing = dd > 0.01 ? -dot(sub(d->vel, e->vel), scale(rel, 1 / dd)) : 0;
      if (dd > 2.4 || dd / (std::max)(1.0, closing) > 0.35) continue;
      slideRolled = true;
      const double sticks = los + cfg.toGo.value_or(10) - e->pos.x;
      const bool fight = sticks > 0 && sticks < 1.8;
      if (!fight && rng.chance(tune().slideK)) {
        ev("slide", e, d);
        e->state = EntState::Down;
        // (as a diving catch: the slide capture takes him to the turf, not a tackled man's fall - with Anim::Down the
        // presentation cut the slide off after 0.12 s for a fall from behind. He stays at the spot he went down at: moving
        // the sim man on to where the slide ends jumped the body 2 m in one frame, p90 clip 2)
        e->anim = Anim::Catch;
        e->vel = V2{0, 0};
        endPlay("tackle");
        return;
      }
      break;
    }
  }
  const OffPlay& play = *cfg.offPlay;
  // (a quarterback on a designed run or a zone-read keep reads his hole like a back; a scrambler does not)
  const bool qbRun = e == qb && (designedQbRun() || qbKept);
  const bool preLOS = e->side == Side::Off && e->pos.x < los - 0.3 && !passThrown && (e != qb || qbRun);
  double best = 0;
  double bestScore = -1e9;
  const double vis = e->r().vision;
  const std::vector<Ent*>& foes = defendersOf(e);
  double aimY = qbKept && e == qb ? keepAim_ : reversed_ && e == revEnt_ ? by - runAimDy() : play.run ? by + runAimDy() : e->pos.y;
  // a gap run's hole is inside the kick-out: once the puller has the end, the back aims a yard and a half inside him
  if (play.run && !qbKept && !reversed_ && e->side == Side::Off)
    for (const auto& kv : jobs_)
      if (kv.second.job == "kick" && kv.first->engaged && kv.first->engaged == kv.first->blockTarget) {
        aimY = kv.first->engaged->pos.y - jsSign(orIfZero(runAimDy(), 1)) * 1.5;
        break;
      }
  const bool pocketRun = e == qb && scramble && e->pos.x < los + 0.5;
  // reading the blocks (Tune::readBlocks): a designed run's carrier, not a scrambler, a receiver after the catch or a
  // returner (their own reads are as they were)
  const bool reads = tune().readBlocks > 0 && !cfg.kick && e->side == Side::Off && play.type == PlayType::Run && !passThrown && !pocketRun &&
                     (e != qb || qbRun);
  // the lead: a puller or lead back of his ahead of him, not yet on his man (he stays on his back until the block is made)
  const Ent* lead = nullptr;
  if (reads && preLOS)
    for (const auto& kv : jobs_) {
      const Ent* j = kv.first;
      if (j == e || j->engaged || j->stun > 0 || (kv.second.job != "lead" && kv.second.job != "wrap" && kv.second.job != "kick")) continue;
      const double ahead = (j->pos.x - e->pos.x) * gd;
      if (ahead < 0.8 || ahead > 6.0 || std::fabs(j->pos.y - e->pos.y) > 4.0) continue;
      if (!lead || ahead < (lead->pos.x - e->pos.x) * gd) lead = j;
    }
  for (int ai = -85; ai <= 85; ai += 10) {
    const double a = (static_cast<double>(ai) * kPi) / 180;
    const V2 dir = fromAngle(gd > 0 ? a : kPi - a);
    double danger = 0;
    double gain = 0;
    for (int k = 1; k <= 5; k++) {
      const V2 p = add(e->pos, scale(dir, k * 1.3));
      if (p.y < 0.6 || p.y > FIELD_W - 0.6) {
        danger += 2.5;
        continue;
      }
      for (Ent* d : foes) {
        if (d->state == EntState::Down) continue;
        double w = d->engaged ? (pocketRun ? 0.8 : 0.25) : d->stun > 0 ? 0.3 : 1;
        if (reads && d->engaged && !d->engagedBy.empty()) {
          // the block's shut side (the blocker between him and the spot) against its open side (his head free there),
          // and a man winning his rep is more of a danger than one being driven
          const V2 u = norm(sub(d->engagedBy[0]->pos, d->pos));
          const V2 to = sub(p, d->pos);
          const double c = len(to) > 1e-6 ? dot(scale(to, 1 / len(to)), u) : 0.0;
          w = tune().readShut + (tune().readOpen - tune().readShut) * (1 - c) / 2;
          w *= (1 + (std::max)(0.0, d->linePush)) * (d->engagedBy.size() >= 2 ? 0.6 : 1.0);
        }
        const double dd = dist(d->pos, p) - k * 0.35;
        danger += (w * std::exp(-(dd * dd) / 3.2)) / k;
      }
      // a quarterback getting out of his pocket goes around the wall of his own blockers, not through it
      if (pocketRun)
        for (Ent* o : teammatesOf(e)) {
          if (o == e || o->state == EntState::Down) continue;
          const double dd = dist(o->pos, p);
          danger += 0.8 * std::exp(-(dd * dd) / 1.6) / k;
        }
    }
    gain = dir.x * gd;
    double score = gain * 1.6 - danger * 2.4;
    // (a quarterback's designed run or read keep goes where the play is blocked - behind his pulling guard, around the
    // end he read: from a standstill five yards deep he had no momentum to the hole and drifted to wherever it looked
    // open, into the back side's traffic)
    // (reading the blocks: he presses the aiming point until he is near the line, then reads off it; a lead blocker
    // ahead of him he follows)
    const bool pressAim = reads && preLOS && !qbRun && e->pos.x * gd < (los - tune().readPressD) * gd;
    if (preLOS) score -= std::fabs(e->pos.y + dir.y * 3 - aimY) * (qbRun ? 0.4 : pressAim ? tune().readPress : 0.05 + (vis < 70 ? 0.08 : 0));
    if (lead) score += tune().readLead * (std::max)(0.0, dot(dir, norm(sub(lead->pos, e->pos))));
    // reading the field at speed: a receiver who has just caught it reads it worse than a back reading his blocks
    score += rng.normal(0, (0.05 + (99 - vis) * 0.009) * (e == catcher ? tune().yacNoise : tune().runNoise));
    if (score > bestScore) {
      bestScore = score;
      best = a;
    }
  }
  const V2 dir = fromAngle(gd > 0 ? best : kPi - best);
  // smooth heading changes (vision = decisive cuts)
  const V2 cur = e->speed() > 1 ? norm(e->vel) : dir;
  const double blend = 0.25 + vis * 0.004;
  const V2 h = norm(add(scale(cur, 1 - blend), scale(dir, blend)));
  // a back presses the line at a tempo (lets the blocks set and the second level commit), then bursts through it: at
  // full speed from the mesh he hit the line at 19 mph and ran past the filling linebackers to the safeties
  const bool pressing = preLOS && cfg.offPlay->type == PlayType::Run && tune().runPress < 1;
  e->want = scale(h, e->vmax * (pressing ? tune().runPress + (vis - 70) * 0.002 : 1.0));
  // a returner sets up his blocks: with a man of his in front of him, not yet on anyone, he stays on that man's back
  // at his pace until the block is made, then goes. (He caught it on the move and outran his own lead blocker, who
  // started from a standstill, past him and into the coverage.)
  if (cfg.kick && e == returner && !e->userControlled && kickCatchT >= 0 && t - kickCatchT < 1.8) {
    Ent* retLead = nullptr;
    double bl = 1e9;
    for (Ent* o : teammatesOf(e)) {
      if (o == e || o->engaged || (o->state != EntState::Convoy && o->state != EntState::RunBlock)) continue;
      const V2 rel = sub(o->pos, e->pos);
      const double ahead = rel.x * gd;
      if (ahead < 1.0 || ahead > 7.0 || std::fabs(rel.y) > 3.0) continue;
      if (ahead < bl) {
        bl = ahead;
        retLead = o;
      }
    }
    if (retLead && retLead->blockTarget && dist(retLead->blockTarget->pos, retLead->pos) < 12.0) {
      const V2 behind{retLead->pos.x - gd * 1.6, retLead->pos.y + (e->pos.y - retLead->pos.y) * 0.3};
      const double cap = len(retLead->vel) + 0.6;
      const V2 to = sub(behind, e->pos);
      e->want = scale(norm(to), (std::min)(e->vmax, (std::max)(cap, len(to) * 2.0)));
    }
  }
  // situational: get out of bounds to stop the clock or go down to protect a lead is decided by game layer
  if (std::fabs(goal - e->pos.x) < 0.1) e->want = V2{gd * e->vmax, 0};
  // AI juke/spin when a single tackler is right in front
  if (!e->userControlled && e->animT > 0.8) {
    for (Ent* d : foes) {
      if (!d->free()) continue;
      const V2 rel = sub(d->pos, e->pos);
      const double ahead = rel.x * gd;
      if (ahead > 0.8 && ahead < 2.4 && std::fabs(rel.y) < 1.2 &&
          rng.chance(0.004 + (std::max)(0.0, e->r().elusiveness - 60) * 0.0003)) {
        // the move that fits the runner and the man (the highlight a user would see); now and then a plain juke
        const Move m = rng.chance(0.3) ? Move::Juke : suggestMove(e);
        if (m == Move::Juke || m == Move::None) {
          const double side = rel.y > 0 ? -1 : 1;
          impulse(e, sub(add(scale(e->vel, 0.6), V2{0, side * 2.8 * (0.7 + e->r().agility / 330)}), e->vel), 0.4);
          e->anim = Anim::Juke;
          e->animT = 0;
          e->move = Move::Juke;
          e->moveT = 0;
          e->moveSide = side;
          ev("move", e, d, "juke");
        } else startMove(e, m);
        break;
      }
      // a man coming from the side at a power runner: the stiff arm
      const double pw = e->r().breakTackle * 0.5 + e->r().strength * 0.5;
      if (ahead > -0.4 && ahead < 1.4 && std::fabs(rel.y) > 0.8 && std::fabs(rel.y) < 1.9 && pw > 70 &&
          rng.chance(0.003 + (pw - 70) * 0.0003)) {
        startMove(e, Move::StiffArm, rel.y * gd > 0 ? 1 : -1);
        break;
      }
    }
  }
}

// ------------------------------------------------------------------ physics
/** One step of the human acceleration law: velocity vel heads toward want within the man's limits. */
V2 PlaySim::steerVel(const Ent* e, const V2& vel, const V2& want, double dt) const {
  const V2 own = steerLaw(e, vel, want, dt, nullptr);
  // a player's own steering (Responsive): only his man, only on a step his stick drove him
  if (!e->steer.on || !e->userControlled || !e->stickDrove) return own;
  V2 v = steerLaw(e, vel, want, dt, &e->steer);
  // (never faster than the sim's own law would have him, or than he was: top speed is the sim's)
  const double top = (std::max)(len(own), len(vel));
  const double lv = len(v);
  if (lv > top && lv > 1e-9) v = scale(v, top / lv);
  if (dist(v, own) > 1e-12) e->steerBoosted++;
  return v;
}

V2 PlaySim::steerLaw(const Ent* e, const V2& vel, const V2& want, double dt, const SteerBoost* boost) {
  const V2 vd = want;
  const double s = len(vel);
  const V2 dir = s > 0.25 ? scale(vel, 1 / s) : norm(vd);
  const V2 dv = sub(vd, vel);
  const double par = dot(dv, dir);
  const V2 perp = sub(dv, scale(dir, par));
  // Human limits (yd/s^2; 1 g = 10.72). Speeding up follows the sprint curve that gives the right 40 times
  // (a = A (1 - v/vmax)); braking tops out near 0.9 g; cornering grip falls with speed (a jump cut at a jog,
  // a rounded turn at full speed: ~0.6 g lateral at top speed); and the whole push never exceeds ~1.05 g of grip.
  // (boost: a player's Responsive numbers on each limit, never below the sim's own; speeding up is left alone)
  const double A = e->vmax / e->tau;
  const double aFwd = A * (std::max)(0.12, 1 - s / e->vmax);
  double aBrake = (std::min)(A * 1.35, 9.6 + (e->r().agility - 50) * 0.03) * (e->strafing ? 1.15 : 1.0);
  if (boost) aBrake = (std::max)(aBrake, aBrake * boost->brakeMul);
  double parC = clamp(par, -aBrake * dt, aFwd * dt);
  const double pl = len(perp);
  double latLow = e->latA * 1.2;                                    // planting at a jog
  // at speed; a receiver adjusting to a ball in the air gathers (gives up speed) and gets his full cut back
  double latHigh = e->catchAdjust ? e->latA : (std::min)(e->latA, 6.6 + (e->r().agility - 50) * 0.03);
  if (boost) {
    latLow = (std::max)(latLow, latLow * boost->latMul);
    latHigh = (std::max)(latHigh, (e->catchAdjust ? e->latA : (std::min)(e->latA, 6.6 + (e->r().agility - 50) * boost->latAgiAtSpeed)) * boost->latMul);
  }
  // (the jog's plant is gone by 6.5 yd/s: blended out to 7.5 a man at 6 m/s still cut at the p95 sharper than a man in
  // full stride can, 7.1 m/s2 sideways)
  const double kSp = clamp((s - 2.5) / 4.0, 0, 1);
  // (strafing, square to the play, he plants and goes either way quicker than a man running flat out turns)
  const double lat = (latLow + (latHigh - latLow) * kSp) * dt * (e->strafing ? 1.3 : 1.0);
  V2 perpC = pl > lat ? scale(perp, lat / pl) : perp;
  {
    double gripA = 9.9 + (e->r().agility - 50) * 0.03;  // ~0.92 g (at 1.05 g a sixth of all free-running 0.2 s windows read over 1 g, half of them cuts)
    if (boost) gripA = (std::max)(gripA, (9.9 + (e->r().agility - 50) * boost->gripAgi) * boost->gripMul);
    const double grip = (std::max)(gripA, aFwd) * dt;
    const double tot = std::sqrt(parC * parC + dot(perpC, perpC));
    if (tot > grip) {
      parC *= grip / tot;
      perpC = scale(perpC, grip / tot);
    }
  }
  return add(vel, add(scale(dir, parC), perpC));
}

bool PlaySim::setUserSteer(int slot, const SteerBoost& b) {
  Ent* e = slotEnt(slot);
  if (!e) return false;
  e->steer = b;
  return true;
}

bool PlaySim::setUserThrow(int style, bool lob, double minAt) {
  if (!pendingThrow || pendingThrow->away || !qb || !qb->userControlled || ball.holder != qb || passThrown) return false;
  pendingThrow->style = style;
  pendingThrow->lob = lob;
  if (minAt >= 0 && !pendingThrow->shovel) pendingThrow->at = (std::max)(pendingThrow->at, minAt);
  return true;
}

bool PlaySim::cancelUserThrow() {
  if (!pendingThrow || pendingThrow->away || !qb || !qb->userControlled || ball.holder != qb || passThrown) return false;
  pendingThrow.reset();
  inputOf(qb).throwTo.clear();
  return true;
}

void PlaySim::impulse(Ent* e, const V2& dv, double dur) {
  e->impulseRate = scale(dv, 1 / dur);
  e->impulseT = dur;
}

void PlaySim::move(Ent* e, double dt) {
  if (wrap_ && (e == wrap_->c || e == wrap_->d)) return;  // the pile moves them (stepWrap)
  if (e->engaged) return;  // engaged pairs are moved by updateEngagements
  if (e->looseDiveT >= 0) {
    // in the air after a loose ball: he flies along the dive to the spot he left his feet for (no steering in the air)
    // (at the speed he left his feet with: a body in the way holds him short, he does not make it up in the air)
    const V2 to = sub(e->looseDiveAt, e->pos);
    const V2 step = scale(e->looseDiveV, dt);
    e->vel = e->looseDiveV;
    e->pos = len(to) <= len(step) ? e->looseDiveAt : add(e->pos, step);
    if (len(e->looseDiveV) > 0.05) e->face = angleOf(e->looseDiveV);
    return;
  }
  if (e->state == EntState::Down || (e->pancaked && e->stun > 0)) {
    e->vel = scale(e->vel, 0.85);
    e->pos = add(e->pos, scale(e->vel, dt));
    return;
  }
  if (e->pancaked) {  // back on his feet
    e->pancaked = false;
    e->anim = Anim::Idle;
  }
  e->vel = steerVel(e, e->vel, e->want, dt);
  if (e->impulseT > 0) {
    const double h = (std::min)(dt, e->impulseT);
    e->vel = add(e->vel, scale(e->impulseRate, h));
    e->impulseT -= dt;
  }
  const double ns = len(e->vel);
  // sustained sprinting with the ball: top speed fades after ~2.5s (stamina matters)
  if (ns > e->vmax * 0.85) e->sprintT += dt;
  const double fade = e->hasBall && e->state == EntState::Carrier
                          ? (std::max)(0.86, 0.96 - (std::max)(0.0, e->sprintT - 2.5) * (0.04 - e->r().stamina * 0.0002))
                          : 1;
  const double vcap = e->vmax * fade;
  if (ns > vcap) e->vel = scale(e->vel, vcap / ns);
  e->pos = add(e->pos, scale(e->vel, dt));
  if (ns > 0.4) e->face = angleOf(e->vel);
  else if (e->side == Side::Def) e->face = kPi;
  // the men who move with their eyes on the ball keep facing it: the quarterback through his drop, pass blockers
  // setting up, and defenders in coverage backpedalling or shuffling before the ball is thrown (a defender turning
  // and running deep faces where he runs). Facing is the renderer's; nothing in the play reads it.
  // (and in the pocket after it: a quarterback sliding or stepping back keeps his shoulders downfield, and turns to
  // the man he is throwing to. He faced whichever way he moved - backing up he had his back to the defense, and the
  // ball left him flying over his own shoulder. Running for it, he faces where he runs.)
  if (e == qb && ball.holder == e && e->state == EntState::QB) {
    if (pendingThrow && pendingThrow->tgt) qbThrowFace = angleOf(sub(pendingThrow->tgt->pos, e->pos)), e->face = qbThrowFace;
    else if (!dropDone || ns < 3.3 || e->vel.x < -0.5 * ns) e->face = 0;  // (at a run, not backing up: he turns and goes)
  } else if (e == qb && e->anim == Anim::Throw && e->animT < 0.6 && passThrown) e->face = qbThrowFace;  // the follow-through
  else if (e->side == Side::Off && e->state == EntState::PassBlock) e->face = 0;
  else if (e->strafing && ball.mode != BallMode::Air) e->face = angleOf(sub(ball.holder ? ball.holder->pos : V2{ball.pos.x, ball.pos.y}, e->pos));  // (L2: eyes on the ball)
  else if (e->side == Side::Def && !passThrown && ball.mode != BallMode::Air && ns < 5.5 && e->vel.x > -0.3 &&
           (e->state == EntState::Zone || e->state == EntState::Man || e->state == EntState::Spy))
    e->face = angleOf(sub(V2{ball.pos.x, ball.pos.y}, e->pos));
  // the run exchange: with the ball in his hands the quarterback is turned to his back (under center that is his back to
  // the defense, as he reverses out to the mesh; he faced downfield and handed it off behind him), and once he has
  // carried out his fake and pulled up he watches the run
  if (e == qb && meshOn()) {
    if (!handedOff && ball.holder == e) {
      // (to the mesh point, on the back's path: turned to the back himself he spun at the 600 deg/s cap as the back ran
      // by him at arm's length - the bearing swings 90 degrees in a fifth of a second - in every filmed exchange; a toss
      // is thrown at the man)
      const Ent* rb = offRole(cfg.offPlay->run->carrier);
      const Mesh& m = mesh();
      if (!m.toss && dist(m.at, e->pos) > 0.3) e->face = angleOf(sub(m.at, e->pos));
      else if (rb && dist(rb->pos, e->pos) > 0.3) e->face = angleOf(sub(rb->pos, e->pos));
    } else if (handedOff && e->state == EntState::Idle && carrier && carrier != e && (ns <= 0.4 || (meshDoneT >= 0 && t - meshDoneT < 0.3)))
      e->face = angleOf(sub(carrier->pos, e->pos));  // (and follows the ball into the back's belly before he turns away)
  } else if (e == qb && tune().mesh > 0 && cfg.offPlay->type == PlayType::PlayAction && ball.holder == e && e->state == EntState::QB && !pendingThrow &&
             t < paPullT()) {
    // (and the fake is sold the same way: turned to the back, the ball shown to his belly)
    const Ent* rb = offRole("RB");
    if (rb && dist(rb->pos, e->pos) > 0.3) e->face = angleOf(sub(rb->pos, e->pos));
  }
  if (e->anim == Anim::Kneel && kneelT >= 0) {
    // (down on a knee with the kick: he stays there)
  } else if ((e->anim != Anim::Throw && e->anim != Anim::Juke && e->anim != Anim::Tackle) || e->animT > 0.5)
    e->anim = ns > 0.6 ? Anim::Run : Anim::Idle;
  if (e->engaged == nullptr && e->state == EntState::PassBlock && ns < 2) e->anim = Anim::Block;
}

V2 PlaySim::blockGoal(const Ent* blocker, const Ent* d) const {
  if (blocker->state == EntState::PassBlock && qb && ball.holder == qb) return qb->pos;
  if (ball.holder && ball.holder->side != d->side) return ball.holder->pos;
  return V2{d->pos.x - goalDir(blocker) * 3, d->pos.y};
}

void PlaySim::engage(Ent* blocker, Ent* d) {
  if (blocker->engaged || (d->engaged && d->engaged != blocker && d->engagedBy.size() >= 2)) return;
  if (contains(blocker->beatBy, d)) return;
  if (blocker->stun > 0 || d->stun > 0) return;
  // a stunt man crossing his own blocker's face (the penetrator) or looping off him (the looper) is not his to block:
  // the twist is beaten only by the protection passing the two off (Tune::stunts)
  if (d->stunt && !d->stuntDone && blocker->snapTarget == d && blocker->state == EntState::PassBlock && t < 1.6) return;
  // where the two are on the blocker's line to the defender's goal: how far past the blocker he is (> 0: by his hip,
  // going away) and how far off the line - square in front of the blocker, or at his shoulder
  const bool scrim = !cfg.place && blocker->side != d->side;
  const V2 goal = blockGoal(blocker, d);
  const V2 u = norm(sub(goal, blocker->pos));
  const V2 rel = sub(d->pos, blocker->pos);
  const double along = dot(rel, u);
  const double lat = std::fabs(u.x * rel.y - u.y * rel.x);
  const bool lev = tune().leverage > 0 && scrim;
  const bool past = scrim && along > tune().levHip;
  if (lev && past) {
    // leverage (Tune::leverage): he is by him - a hand on him as he goes, not a block (snapped back into a block he had
    // already beaten was the "suction"). The blocker is beaten by him.
    if (blocker->side == Side::Off && ball.mode != BallMode::Snap) foulHold.add(tune().levGrabHold, blocker->p);
    if (!contains(blocker->beatBy, d)) blocker->beatBy.push_back(d);
    engRefused_++;
    if (blocker->state == EntState::PassBlock && d->state == EntState::Rush) noteBeat(d, std::vector<Ent*>{blocker});  // (he won the rep at the set)
    return;
  }
  // defenders in space can slip a block entirely (smaller, quicker players more so)
  const bool inSpace = d->pos.x > los + 2.5 || std::fabs(d->pos.y - by) > 7;
  // (not a pass rusher coming at his blocker: a wide end is 'in space' by where he lines up, and slipping the tackle's
  // set outright left him unblocked - 132 unblocked sacks by ends in 200 games, and a free rusher on nearly half of all dropbacks)
  if (inSpace && d->engagedBy.empty() && !(d->state == EntState::Rush && blocker->state == EntState::PassBlock)) {
    const double avoid = clamp(0.15 + (d->r().agility - blocker->r().agility) * 0.008 + (blocker->mass - d->mass) * 0.004, 0.05, 0.6);
    if (rng.chance(avoid)) {
      blocker->stun = 0.4;
      return;
    }
  }
  blocker->engaged = d;
  blocker->engT = 0;
  d->engagedBy.push_back(blocker);
  if (!d->engaged) d->engaged = blocker;
  if (lev) {
    // the rep's edge from the angle they met at (head leverage): square in front of him the blocker has it, at his
    // shoulder the defender does; a defender coming hard at his goal and a pass blocker late to his set give it away
    const bool passSet = blocker->state == EntState::PassBlock;
    const double dClose = dot(d->vel, norm(sub(goal, d->pos)));
    const double geo = clamp(tune().levK * (tune().levCen - lat), -tune().levCap, tune().levCap) - tune().levClose * (std::max)(0.0, dClose - 2.5) -
                       (passSet ? tune().levSet * (std::max)(0.0, len(blocker->vel) - 1.5) : 0.0);
    blocker->engageEdge = geo + rng.normal(0, tune().levSd);
    engGeoSum_ += geo;
  } else {
    blocker->engageEdge = rng.normal(0, 7);
  }
  if (scrim) {
    engN_++;
    engPastHip_ += past;
    engLatSum_ += lat;
  }
  // (telemetry: a climb off a combo that got there; a linebacker a lineman got a block on, on a run)
  if (d->stunt) {
    // (a stunt man picked up: he rushes off the block as ever; by a man the protection did not give him at the snap, the
    // twist was passed off)
    if (!d->stuntDone && blocker->state == EntState::PassBlock && blocker->snapTarget != d) passOffs_++;
    d->stuntDone = true;
  }
  if (blocker->climbFor == d) {
    comboHit_++;
    comboHitT_ += t;
    blocker->climbFor = nullptr;
  }
  if (scrim && blocker->isOL && blocker->state == EntState::RunBlock && isLB(d) && !contains(lbBlocked_, static_cast<const Ent*>(d))) lbBlocked_.push_back(d);
  blocker->anim = Anim::Block;
  d->anim = Anim::Block;
  if (!firstContact) firstContact = true;
}

void PlaySim::disengage(Ent* b) {
  Ent* d = b->engaged;
  if (!d) return;
  b->engaged = nullptr;
  b->linePush = 0;
  removeValue(d->engagedBy, b);
  if (d->engaged == b) d->engaged = d->engagedBy.empty() ? nullptr : d->engagedBy[0];
  if (d->engagedBy.empty()) d->linePush = 0;
}

// a per-player key that is the same in every build of the league (Player::id comes from a process-wide counter, so
// two leagues in one process number their players differently): jersey number and side
static unsigned stableKey(const Ent* d) {
  return static_cast<unsigned>((d->p ? d->p->number : 0) * 2 + (d->side == Side::Def ? 1 : 0)) + 1u;
}

double PlaySim::pancakeRoll(const Ent* d) const {
  // a uniform draw that does not touch the play's rng stream (so adding pancakes changes nothing but the pancakes)
  unsigned h = stableKey(d) * 2654435761u ^ static_cast<unsigned>(t * 60.0 + 0.5) * 2246822519u;
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return (h & 0xFFFFFF) / static_cast<double>(0x1000000);
}

const char* PlaySim::shedMove(const Ent* d, const Ent* b, bool passPro, double push) const {
  const Ratings& r = d->r();
  const unsigned h = stableKey(d) * 2654435761u + static_cast<unsigned>(t * 10.0) * 40503u;
  const bool power = r.strength > r.agility + 4;
  if (!passPro) return power ? (h % 2 ? "longarm" : "club") : (h % 3 == 0 ? "club" : "rip");  // stack and shed
  if (push > 0.35) return h % 2 ? "longarm" : "bull";  // he was already walking the blocker back: finish the power rush
  // edge or inside: is the rusher wider of the quarterback's lane than his blocker?
  const double qy = qb ? qb->pos.y : by;
  const bool edge = std::fabs(d->pos.y - qy) > std::fabs(b->pos.y - qy) + 0.25;
  if (edge) return (r.accel + r.agility > 170 || h % 3 == 0) ? "diprip" : "club";
  if (r.agility > 80 && h % 4 == 0) return "spin";
  return power ? (h % 2 ? "longarm" : "bull") : (h % 2 ? "swim" : "rip");
}

bool PlaySim::winWindow() const {
  // (ESPN's window: 2.5 s from the snap, screens not counted; it closes early when the ball is out or he leaves the pocket)
  return phase == SimPhase::Live && !cfg.kick && !cfg.place && qb && cfg.offPlay && passPlay() && cfg.offPlay->type != PlayType::Screen &&
         t <= 2.5 + 1e-9 && (ball.holder == qb || ball.mode == BallMode::Snap) && !passThrown && !scramble && !pendingThrow;
}

void PlaySim::noteWinReps() {
  // pass rush / pass block win rates (telemetry, Research/NextLevel/02 A3): a rusher wins by beating his block inside
  // 2.5 s, a blocker by holding his that long; an unblocked rusher who gets to the quarterback is the rush's win at the
  // team level (he beat nobody, so not his own)
  if (!winWindow()) return;
  winPlay_ = true;
  for (Ent* d : def) {
    if (!d->wrIn) {
      if (d->state != EntState::Rush) continue;
      d->wrIn = true;
      d->wrCls = d->isDL ? (d->role == "LE" || d->role == "RE" ? 0 : 1) : std::fabs(d->pos.y - by) > 3.5 ? 0 : 2;
    }
    if (!d->engagedBy.empty() && d->engagedBy[0]->state == EntState::PassBlock) d->wrEng = true;
    if (!d->wrEng && d->free() && dist(d->pos, qb->pos) < 2.5) d->wrFree = true;
  }
  for (Ent* e : off) {
    if (e == qb) continue;
    if (!e->wrIn) {
      if (e->state != EntState::PassBlock) continue;
      e->wrIn = true;
      e->wrCls = e->role == "LT" || e->role == "RT" ? 0 : e->isOL ? 1 : 2;
    }
    if (e->engaged && e->state == EntState::PassBlock) e->wrEng = true;
  }
}

void PlaySim::noteBeat(Ent* d, const std::vector<Ent*>& blockers) {
  if (!winWindow()) return;
  d->wrEng = true;
  d->wrBeat = true;
  for (Ent* b : blockers)
    if (b->side != d->side) b->wrEng = b->wrBeat = true;
}

void PlaySim::updateEngagements(double dt) {
  for (Ent* d : ents) {
    if (d->engagedBy.empty()) continue;
    // TS keeps a reference to the array that disengage() later replaces: a snapshot is equivalent
    const std::vector<Ent*> blockers = d->engagedBy;
    Ent* holder = ball.holder;
    const bool passPro = blockers[0]->state == EntState::PassBlock;
    // defender's goal
    V2 goal;
    // (a field goal's rusher: the ball's path in front of the holder; its protection does not give ground)
    const PlaceMan* fgRush = passPro && cfg.place && !placeBlocked ? placeMan(d) : nullptr;
    if (fgRush) goal = fgRush->aim;
    else if (passPro && ball.holder == qb) goal = qb->pos;
    else if (holder && holder->side != d->side) goal = holder->pos;
    else goal = V2{d->pos.x - goalDir(blockers[0]) * 3, d->pos.y};
    const V2 toGoal = norm(sub(goal, d->pos));
    // contest: defender skill vs blockers
    const Ratings& dr = d->r();
    const double dSkill = passPro ? (std::max)(dr.passRush * 0.7 + dr.strength * 0.3, dr.passRush * 0.6 + dr.agility * 0.25 + dr.accel * 0.15)
                                  : dr.blockShed * 0.6 + dr.strength * 0.4;
    double bSkill = 0;
    for (Ent* b : blockers)
      bSkill += passPro ? b->r().passBlock * 0.7 + b->r().strength * 0.3 : b->r().runBlock * 0.65 + b->r().strength * 0.35;
    const double nb = static_cast<double>(blockers.size());
    const double avgB = bSkill / nb;
    const double edge = blockers[0]->engageEdge;
    double massSum = 0;
    for (Ent* b : blockers) massSum += b->mass;
    const double massAdv = (d->mass - massSum / nb) * 0.08;
    const double diff = dSkill - avgB - edge + massAdv - (nb - 1) * (passPro ? tune().helpPen : 14.0);  // (a pass-pro help is a hand, not a second drive block)
    // movement of the pile (a stoned move: the blocker has him for a beat)
    if (d->stallT > 0) d->stallT -= dt;
    const double push = clamp(diff / 25 - (d->stallT > 0 ? 0.6 : 0.0), -1, 1) * (passPro ? 1 - 0.6 * fakeBite(d) : 1.0);
    d->linePush = push;
    for (Ent* b : blockers) b->linePush = push;
    // a blocker losing the rep hangs on: the worse he is beaten, the likelier the hold is seen (run or pass)
    if (push > 0.3 && ball.mode != BallMode::Snap)
      for (Ent* b : blockers)
        if (b->side == Side::Off) foulHold.add(tune().holdK * (push - 0.3) * dt * (passPro ? 0.75 : 1.4), b->p);  // (a return's blocks after a turnover are not offensive holding)
    const V2 pileV = push > 0 ? scale(toGoal, push * (passPro ? 1.3 : 1.6))
                              : scale(norm(V2{-toGoal.x, -toGoal.y * 0.3}), -push * (passPro ? 0.35 : 1.6));
    // defender also slides laterally toward the goal when not winning
    V2 lateral{0, toGoal.y * 0.6};
    // (the edge man on a run, blocked, works to stay outside the runner - he sets the edge with the man on him: gapFit)
    if (!passPro && d->contain && tune().gapFit > 0 && holder && holder->side != d->side && holder->pos.x < los + 0.5 && !cfg.kick && !cfg.place) {
      const double wantY = holder->pos.y + d->contain * tune().containW;
      if ((wantY - d->pos.y) * d->contain > 0) lateral.y = clamp((wantY - d->pos.y) * 1.5, -1.2, 1.2) * (1 - 0.5 * (std::max)(0.0, -push));
    }
    V2 v = add(pileV, lateral);
    // pocket: pass blockers give ground in their sets, so the fight happens near the QB's depth
    if (passPro && !fgRush && dist(d->pos, goal) > 3.0) v = add(v, scale(toGoal, 1.1 * (std::max)(0.3, 1 - blockers[0]->engT / 2.2)));
    d->vel = v;
    const V2 dWas = d->pos;
    d->pos = add(d->pos, scale(v, dt));
    d->face = angleOf(toGoal);
    if (d->anim != Anim::Block && (d->anim != Anim::Tackle || d->animT > 0.4)) d->anim = Anim::Block;
    for (Ent* b : blockers) {
      if (b->anim != Anim::Block) b->anim = Anim::Block;
      // the blocker rides the pile at hand-fighting distance. He closes to that distance (or gives the ground back) at
      // a step's pace, 2.2 yd/s: set there outright, a lineman who engaged from 40 cm further out was moved those
      // 40 cm in one tick (a 1.2 linemen a play jumped 15-50 cm a tenth of a second after the snap, p17 telemetry)
      const double want = b->radius + d->radius;
      const double cur = dist(b->pos, dWas);
      const double gap = cur + clamp(want - cur, -2.2 * dt, 2.2 * dt);
      const V2 offset = scale(norm(sub(b->pos, dWas)), gap);
      b->pos = add(d->pos, offset);
      b->vel = v;
      b->face = angleOf(sub(d->pos, b->pos));
    }
    // shed hazard: skill matchup plus blocks degrade the longer they're held
    for (Ent* b : blockers) b->engT += dt;
    // the bars: the rusher's energy comes back slowly; the blocker's power wears with the rep's length (the hazard's own
    // decay below) and a pile going his way backwards, and is never more than it was (the moves he took stay taken)
    d->rushEnergy = (std::min)(1.0, d->rushEnergy + 0.07 * dt * d->fx.energyRegen);
    if (d->moveCd > 0) d->moveCd -= dt;
    for (Ent* b : blockers) {
      const double worn = 1 - 0.32 * (std::max)(0.0, b->engT - (passPro ? tune().passProHold : 1.6)) - 0.3 * (std::max)(0.0, push);
      b->blockPower = clamp((std::min)(b->blockPower + 0.04 * dt, worn), 0.05, 1.0);
    }
    // the CPU's rusher works his moves through the rep: most are stoned (the hazard below decides the one that wins).
    // Shown only: a stable hash, not the play's rng.
    // (a field goal rep lasts a second and a half: the move comes early in it)
    if (!d->userControlled && passPro && ball.mode != BallMode::Snap && blockers[0]->engT > (fgRush ? 0.35 : 0.7) && d->moveCd <= 0 && d->rushEnergy > 0.3) {
      const char* m = shedMove(d, blockers[0], passPro, push);
      const bool pw = !std::strcmp(m, "bull") || !std::strcmp(m, "club") || !std::strcmp(m, "longarm");
      ev("rushfail", d, blockers[0], m);
      d->rushEnergy -= pw ? 0.24 : 0.2;
      blockers[0]->blockPower = (std::max)(0.05, blockers[0]->blockPower - (pw ? 0.1 : 0.04));
      d->lastMove = m;
      d->lastMoveT = t;
      d->lastMoveWon = false;
      d->moveCd = 1.3 + 0.9 * pancakeRoll(d);
    }
    // pass pro: an average matchup holds 2.5 s about 90% of the time (ESPN pass block win rates ~0.9 per lineman)
    // (an extra point is rushed with less abandon than a field goal: the try's two-point fake has to be played)
    const double base = fgRush ? tune().fgShed * (cfg.place->pat ? tune().fgPatRush : 1.0) : passPro ? tune().passProShed : tune().runShed;
    // a linebacker (or safety) a lineman has climbed to is in space: he plays off the block as the back comes to him and
    // comes off it with the runner a step further away than a lineman in the trenches does
    const bool second = !passPro && !d->isDL;
    const bool holderNear = holder && holder->side != d->side && dist(holder->pos, d->pos) < (second ? tune().lvl2Near : 1.6);
    const double decay = (std::max)(0.0, blockers[0]->engT - (passPro ? tune().passProHold : 1.6)) * 0.5;
    double lambda = (base * std::exp(diff / 17) + decay) * (holderNear ? (passPro ? 3.2 : tune().runNearShed) : 1);
    if (passPro) lambda *= 1 - 0.8 * fakeBite(d);  // (reading the run fake, he is not working to get off the block yet)
    if (second) lambda *= tune().lvl2Shed;
    // the rush by the situation: on 1st and 2nd down the front plays the run first and rushes under control; on 3rd and
    // 5+ it pins its ears back (nflverse 2023 sacks: 5.4 % of dropbacks on 1st down, 5.5 on 2nd, 10.1 on 3rd - the sim's
    // were 6.6 / 6.3 / 5.9, too many early-down sacks feeding third and long and too few on the down itself)
    if (passPro && cfg.down && !cfg.kick) {
      const int dn = static_cast<int>(*cfg.down);
      lambda *= dn <= 2 ? tune().rushEarly : cfg.toGo.value_or(10) >= 5 ? tune().rushLate : 1.0;
    }
    {
      // abilities: the rusher's (Edge Bender, Havoc; Gap Plugger, Black Hole against the run) against his blockers'
      // (Brick Wall, Fortress; Road Grader)
      double hold = 1;
      for (Ent* b : blockers) hold *= passPro ? b->fx.holdShed : b->fx.runHold;
      lambda *= (passPro ? d->fx.shed : d->fx.runShed) * hold;
      if (passPro && holderNear) lambda *= d->fx.sackArm;  // (Finisher, Havoc: off the block with the quarterback in reach)
    }
    // the ball is caught (or handed off) away from the quarterback: a pass rusher stops rushing a quarterback who no
    // longer has it, turns and chases; his blocker cannot hold him (that is holding), so the rep breaks up in a beat
    // (the blockers turned convoy at the catch: a pass rep still running when the ball was caught)
    if (passThrown && holder && holder != qb && holder->side != d->side && blockers[0]->engT > t - throwStart) lambda += tune().releaseRate;
    // pancake: a run block won decisively puts the defender on his back (rare; the drive has to be dominant)
    if (!passPro && push < -0.6 && blockers[0]->engT > 0.5 &&
        pancakeRoll(d) < 1 - std::exp(-0.35 * (-push - 0.6) / 0.4 * dt)) {
      if (ball.mode != BallMode::Snap) ev("pancake", d, blockers[0]);
      for (Ent* b : blockers) {
        disengage(b);
        b->stun = 0.25;
      }
      d->engaged = nullptr;
      d->engagedBy.clear();
      d->linePush = 0;
      d->pancaked = true;
      d->stun = 2.4;
      d->anim = Anim::Down;
      d->animT = 0;
      d->vel = scale(norm(V2{-toGoal.x, -toGoal.y}), 1.2);
      continue;
    }
    if (rng.chance(1 - std::exp(-lambda * dt))) {
      // shed! the beaten blocker is left behind and can't re-engage this rusher. The move that won it (for the
      // presentation): picked from the rusher's build and leverage, not from the rng, so the sim is unchanged
      if (ball.mode != BallMode::Snap) {
        const char* m = shedMove(d, blockers[0], passPro, push);
        ev("shed", d, blockers[0], m);  // events start at the snap
        d->lastMove = m;
        d->lastMoveT = t;
        d->lastMoveWon = true;
      }
      if (ball.mode != BallMode::Snap && blockers[0]->side == Side::Off) foulHold.add(tune().holdShed, blockers[0]->p);
      if (passPro) noteBeat(d, blockers);
      for (Ent* b : blockers) {
        disengage(b);
        b->stun = (passPro ? 0.6 : 0.35) + rng.range(0, 0.3);
        if (passPro) b->slipT = b->stun;  // (while he reels: the fitted constants are for this window; 0.35 s cost the box score 5 stats)
        if (!contains(b->beatBy, d)) b->beatBy.push_back(d);
      }
      // off the block with a burst: the half yard past his man that the old code moved him in one tick (a 46 cm jump
      // of the rusher at every pass-rush win) is now covered by the speed he leaves with, over the next quarter second
      d->engaged = nullptr;
      d->engagedBy.clear();
      d->stun = 0;
      d->vel = scale(toGoal, passPro ? 3.5 : 2.5);
      d->anim = Anim::Run;
    }
  }
}

void PlaySim::collide() {
  const std::vector<Ent*>& es = ents;
  for (size_t i = 0; i < es.size(); i++) {
    Ent* a = es[i];
    if (a->state == EntState::Down || (a->pancaked && a->stun > 0)) continue;  // on the ground: no longer in the way
    for (size_t j = i + 1; j < es.size(); j++) {
      Ent* b = es[j];
      if (b->state == EntState::Down || (b->pancaked && b->stun > 0)) continue;
      if (a->engaged == b || b->engaged == a) continue;
      // a rusher who has just beaten his man goes past him (the rip, the swim): the two do not collide while the beaten
      // blocker is still reeling. (The win used to move the rusher half a yard past his man in one tick instead.)
      if ((a->slipT > 0 && contains(a->beatBy, b)) || (b->slipT > 0 && contains(b->beatBy, a))) continue;  // (pass rush wins: a run block shed leaves the man in the hole)
      const double dx = b->pos.x - a->pos.x;
      const double dy = b->pos.y - a->pos.y;
      const double rr = a->radius + b->radius;
      const double d2 = dx * dx + dy * dy;
      if (d2 >= rr * rr || d2 < 1e-8) continue;
      const double d = std::sqrt(d2);
      const V2 n{dx / d, dy / d};
      const double pen = rr - d;
      const double wa = b->mass / (a->mass + b->mass);
      const double wb = 1 - wa;
      a->pos = sub(a->pos, scale(n, pen * wa));
      b->pos = add(b->pos, scale(n, pen * wb));
      // inelastic: damp approach velocity
      const double rv = dot(sub(b->vel, a->vel), n);
      if (rv < 0) {
        const double imp = rv * 0.6;
        a->vel = add(a->vel, scale(n, imp * wa));
        b->vel = sub(b->vel, scale(n, imp * wb));
      }
    }
  }
}

// ------------------------------------------------------------------ ball
void PlaySim::coastBall(double dt) {
  // after the whistle the ball is still a ball: a pass out of bounds (or past the end line) flies on to the ground, an
  // incomplete one bounces off the turf on whatever end it lands on, a loose ball rolls out; the stands stop it
  Ball& b = ball;
  if (phase != SimPhase::Dead || cfg.kick) return;
  if (b.mode == BallMode::Air && !b.phys) {
    const int sub2 = 4;
    for (int i = 0; i < sub2; i++) integrate(b.pos, b.vel, dragK(), dt / sub2);
    b.spin += dt * 40;
    if (b.tumble) b.body.turn(dt);
    else b.body.q = qalong(b.vel, b.spin);
    if (b.pos.z <= 0.15) startGround();
  } else if ((b.mode == BallMode::Air || b.mode == BallMode::Loose || b.mode == BallMode::Dead) && b.phys && !b.body.rest) {
    stepBody(dt);
  }
  b.pos.y = clamp(b.pos.y, -12, FIELD_W + 12);
  b.pos.x = clamp(b.pos.x, -12, 112);
  if (b.phys) {
    b.body.pos.x = b.pos.x;
    b.body.pos.y = b.pos.y;
  }
}

void PlaySim::startGround() {
  // a flight meeting the turf: from here the rigid body flies it (a spiral lands nose first and kicks up end over end)
  Ball& b = ball;
  if (b.phys) return;
  if (!b.tumble) {
    b.body.q = qalong(b.vel, b.spin);
    const V3 ax = b.body.axis();
    b.body.w = V3{ax.x * 40, ax.y * 40, ax.z * 40};  // the spiral's spin about the long axis
  }
  b.body.pos = b.pos;
  b.body.vel = b.vel;
  b.body.rest = false;
  b.body.clearTurf();
  b.phys = true;
  b.pos = b.body.pos;
}

void PlaySim::updateBall(double dt) {
  Ball& b = ball;
  if (b.mode == BallMode::Held && b.holder) {
    Ent* h = b.holder;
    b.pos = V3{h->pos.x + std::cos(h->face) * 0.25, h->pos.y + std::sin(h->face) * 0.25, 1.25};
    return;
  }
  if (b.mode == BallMode::Air && (cfg.kick || cfg.place)) return;  // (a kick flies on its own: updateKickBall / updatePlace)
  if (b.mode == BallMode::Air) {
    b.t += dt;
    const int sub2 = 4;
    for (int i = 0; i < sub2; i++) integrate(b.pos, b.vel, dragK(), dt / sub2);
    if (b.ghost && b.ghostPos.z > 0.05) {
      for (int i = 0; i < sub2; i++) integrate(b.ghostPos, b.ghostVel, dragK(), dt / sub2);
      if (const Ent* ge = b.target) {
        const double gh = ge->p->heightIn / 36;
        b.ghostHit = b.ghostHit || (jsHypot(ge->pos.x - b.ghostPos.x, ge->pos.y - b.ghostPos.y) < gh * 0.5 + 0.55 && b.ghostPos.z < gh + 0.9 && b.ghostPos.z > 0.2);
      }
    }
    b.spin += dt * 40;
    // the spiral's nose follows the flight; a ball knocked off it turns over freely
    if (b.tumble) b.body.turn(dt);
    else b.body.q = qalong(b.vel, b.spin);
    static const bool noFate = std::getenv("TAG_NO_FATE") != nullptr;
    if (!noFate && !b.fateRolled && b.target && !b.away && b.flightT - b.t <= 0.4) preRollCatch();
    if (!noFate && tune().homeA > 0 && b.target && !b.away && !b.tumble) preRollLane();
    if (tune().homeA > 0 && b.fate != 0 && b.target && !b.away && !b.tumble) {
      if (!b.ghost) {
        b.ghost = true;
        b.fateLeft = b.flightT - b.t;
        b.ghostPos = b.pos;
        b.ghostVel = b.vel;
      }
      homeBall(dt);
    }
    resolveAir();
    if (b.mode == BallMode::Air && b.pos.z <= 0.05) {
      ev("incomplete", b.target);
      endPlay("incomplete");
      return;
    }
    // a pass that sails past the end line or a sideline cannot be caught in bounds: dead there
    if (b.mode == BallMode::Air &&
        (b.pos.x > END_LINE + 1 || b.pos.x < 100 - END_LINE - 1 || b.pos.y < -1.5 || b.pos.y > FIELD_W + 1.5)) {
      ev("incomplete", b.target);
      endPlay("incomplete");
    }
    return;
  }
  if (b.mode == BallMode::Loose) {
    b.t += dt;
    if (!b.phys) {
      // (a loose ball that was not put in play by fumble(): a kick nobody fielded) - a football from here on
      b.body = BallBody{};
      b.body.pos = b.pos;
      b.body.vel = b.vel;
      b.body.q = qalong(b.vel, b.spin);
      b.body.w = V3{rng.range(-10, 10), rng.range(-10, 10), rng.range(-6, 6)};
      b.phys = true;
      b.tumble = true;
    }
    stepBody(dt);
    if (b.pos.y < 0 || b.pos.y > FIELD_W || b.pos.x < -10 || b.pos.x > 110) {
      // out of bounds: last possession team keeps it (simplified)
      endPlay("oob");
      return;
    }
    // going for it. A man near the ball either scoops it on the run (it is sitting up, he is running, it is moving with
    // him: one go when he gets there) or leaves his feet for it - a dive from up to 2.4 yd at where it will be when his
    // hands get there (ev "dive": the presentation's capture lands him on that spot then). Whether he comes up with it is
    // the ball's state against his hands at that moment: a ball lying still is easy, one skipping off the turf at speed
    // is not, and a miss knocks it on (it squirts away along his dive) and leaves him on the turf. The next man in has
    // his own go: two or three diving at one ball make the pile.
    if (b.t > 0.15) {
      const V2 bp{b.pos.x, b.pos.y};
      const double up = (std::max)(0.0, b.pos.z - BallBody::B);
      auto secure = [&](Ent* e, double rel, double gap) {
        const bool bouncing = std::fabs(b.vel.z) > 1.2 || up > 0.25;
        const double p = clamp(0.82 + (e->r().awareness - 70) * 0.006 + (e->r().catching - 60) * 0.002 - 0.05 * rel - (bouncing ? 0.15 : 0) - 0.2 * gap,
                               0.1, 0.94);
        return rng.chance(p);
      };
      auto squirt = [&](Ent* e, const V2& along) {
        const double kick = rng.range(0.6, 2.0);
        b.body.vel = V3{b.body.vel.x * 0.5 + along.x * kick, b.body.vel.y * 0.5 + along.y * kick, rng.range(0.4, 1.6)};
        b.body.w = V3{b.body.w.x + rng.range(-10, 10), b.body.w.y + rng.range(-10, 10), b.body.w.z + rng.range(-6, 6)};
        b.body.rest = false;
        ev("squirt", e);
      };
      auto recover = [&](Ent* e, const char* how) {
        recoverHow = how;
        gContact.add(8, e, b.pos);
        ev("recover", e, nullptr, how);
        recoveredBy = e;
        b.phys = false;
        b.tumble = false;
        // (a man already in the air for it still lands: on the turf like a miss, the dive's capture and a get-up. Cut
        // off and freed at once, a lineman lying on the turf was run on by the sim and, when the ball came out again
        // half a second later, launched from flat on his back into a second dive - to14 c032)
        for (Ent* o : ents) {
          if (o != e && o->looseDiveT >= 0) {
            o->stun = (std::max)(o->stun, 2.2);
            o->vel = scale(o->vel, 0.25);
          }
          o->looseDiveT = -1;
        }
        setCarrier(e);
        if (recoverHow != "scoop") {
          e->state = EntState::Down;
          e->anim = Anim::Catch;  // (as a diving catch: the presentation's dive is already taking him down, not a tackle's fall)
          e->vel = scale(e->vel, 0.3);
          endPlay("tackle");
        }
      };
      // the dives landing this step
      for (Ent* e : ents) {
        if (e->looseDiveT < 0) continue;
        e->looseDiveT -= dt;
        if (e->looseDiveT > 0) continue;
        e->looseDiveT = -1;
        const double gap = dist(e->pos, bp);
        const bool there = gap < 0.9 && up < 0.6;
        if (there && secure(e, jsHypot(b.vel.x, b.vel.y), gap)) {
          recover(e, "dive");
          return;
        }
        // on the turf: up and after it again in a couple of seconds (the dive's capture and a get-up)
        e->stun = (std::max)(e->stun, 2.2);
        e->vel = scale(e->vel, 0.25);
        if (there) squirt(e, e->speed() > 0.3 ? norm(e->vel) : norm(sub(bp, e->pos)));
        else ev("divemiss", e);
      }
      // new goes at it: the man of each side nearest the ball, two in the air at most (the rest run at it: a fumble is
      // two or three men on the turf, not the whole field - the first film had twenty diving in turn, a pile of bodies)
      int diving = 0;
      for (Ent* e : ents) diving += e->looseDiveT >= 0;
      Ent* nearest[2] = {nullptr, nullptr};
      for (Ent* e : ents) {
        if (!e->free() || e->looseDiveT >= 0 || e == fumbler) continue;
        Ent*& n = nearest[e->side == Side::Off ? 0 : 1];
        if (!n || dist(e->pos, bp) < dist(n->pos, bp)) n = e;
      }
      std::vector<Ent*> near;
      for (Ent* e : ents)
        if (e->free() && e->looseDiveT < 0 && dist(e->pos, bp) < 2.4 && up < 0.8 && e != fumbler) near.push_back(e);
      std::stable_sort(near.begin(), near.end(), [&bp](const Ent* x, const Ent* y) { return dist(x->pos, bp) - dist(y->pos, bp) < 0; });
      for (Ent* e : near) {
        const auto it = looseTry_.find(e);
        if (it != looseTry_.end() && t - it->second < 0.4) continue;
        const double gap = dist(e->pos, bp);
        const double rel = jsHypot(b.vel.x - e->vel.x, b.vel.y - e->vel.y);
        // (a blocked kick: the rush is running at a ball that bounces up off the turf toward it - the scoop and score, not
        // a dive on it; Sim/PlaceKick.cpp, the defense only)
        const bool blockScoop = placeBlocked && e->side == Side::Def;
        const bool sittingUp = blockScoop ? up < 0.5 && std::fabs(b.vel.z) < 3.0 : up < 0.15 && std::fabs(b.vel.z) < 1.0;
        if (gap < 0.95 && e->speed() > 3.0 && rel < (blockScoop ? 7.0 : 4.5) && sittingUp &&
            rng.chance((blockScoop ? 0.6 : 0.3) + (e->r().catching - 60) * 0.004)) {
          // the scoop: picked up in his stride
          looseTry_[e] = t;
          if (secure(e, rel, gap)) {
            recover(e, "scoop");
            return;
          }
          squirt(e, e->speed() > 0.5 ? norm(e->vel) : norm(sub(bp, e->pos)));
          break;
        }
        // the dive: when he is close enough that it beats running to it (a man still closing fast runs on)
        const double closing = dot(e->vel, norm(sub(bp, e->pos)));
        // (and only at a ball in front of him: a man on the move does not lay out back the way he came - he runs on and
        // turns for it. Three dives in to13 had men turned 120-190 deg in a frame onto a ball behind them.)
        const bool inFront = e->speed() < 2.0 || closing >= 0.5 * e->speed();
        // (a blocked kick's rusher with the ball to himself runs on to scoop it; he falls on it only with a kick-unit man
        // at it too)
        bool contested = true;
        if (blockScoop && e->speed() > 3.0) {
          contested = false;
          for (const Ent* o : off)
            if (o->free() && dist(o->pos, bp) < 4.0) contested = true;  // (nflverse 2023: about 1 blocked kick in 25 run back for a score)
        }
        if (contested && diving < 2 && inFront && (e == nearest[0] || e == nearest[1]) && (gap < 0.9 || (gap < 1.9 && (closing < 3.0 || gap < 1.2 + 0.1 * closing)))) {
          looseTry_[e] = t;
          const double T = clamp(0.18 + gap / 5.0, 0.3, 0.6);
          // (where it will be when his hands get there: the ball itself flown ahead - a copy of the rigid body stepped
          // T on. Guessed from its roll, 44 % of dives landed where it no longer was)
          BallBody ahead = b.body;
          ahead.step(T, G);
          V2 at{ahead.pos.x, ahead.pos.y};
          // a dive carries the speed he had into it (a standing man's lunge ~4.5 yd/s): a ball rolling away faster
          // than that is not caught up with in the air - he lands short of it. (Flown at the distance over the time
          // left, a ball squirting off at 6 yd/s and bodies holding him back made the air speed climb to 11 yd/s,
          // the game's top speed of 23 mph: to6 c075.)
          const V2 to = sub(at, e->pos);
          const double reach = clamp((std::max)(e->speed() + 1.0, 4.5), 0.0, e->vmax) * T;
          if (len(to) > reach) at = add(e->pos, scale(to, reach / len(to)));
          e->looseDiveT = T;
          e->looseDiveAt = at;
          e->looseDiveV = scale(sub(at, e->pos), 1.0 / T);
          e->stun = (std::max)(e->stun, T + 0.05);
          ev("dive", e);  // (the presentation reads looseDiveT / looseDiveAt off him)
          break;
        }
      }
    }
  }
}

void PlaySim::stepBody(double dt) {
  Ball& b = ball;
  b.body.step(dt, G);
  b.pos = b.body.pos;
  b.vel = b.body.vel;
}

void PlaySim::startTumble(double spinScale) {
  // a pass off its spiral: the turn the deflection gave it, about any axis, from where it is now
  Ball& b = ball;
  b.body.q = qalong(b.vel, b.spin);
  b.body.w = V3{rng.range(-12, 12) * spinScale, rng.range(-12, 12) * spinScale, rng.range(-8, 8) * spinScale};
  b.tumble = true;
}

namespace {
struct FateStats {
  long n = 0, nCont = 0, nContPre = 0, nBoth = 0, intRecv = 0, intDef = 0, intRecvF = 0, intDefF = 0;
  double pPre = 0, pArr = 0, dPre = 0, dArr = 0, pbuPre = 0, pbuArr = 0;
  ~FateStats() {
    if (!n || !std::getenv("TAG_FATE_STATS")) return;
    std::printf("fate stats: %ld target arrivals; p catch pre %.3f arr %.3f; d pre %.2f arr %.2f; contested pre %.2f arr %.2f both %.2f; pbu pre %.3f arr %.3f\n", n,
                pPre / n, pArr / n, dPre / n, dArr / n, double(nContPre) / n, double(nCont) / n, double(nBoth) / n, pbuPre / n, pbuArr / n);
    std::printf("fate ints: at the receiver %ld (%ld fated), defender first %ld (%ld fated)\n", intRecv, intRecvF, intDef, intDefF);
  }
} gFate;
}  // namespace

void PlaySim::preRollCatch() {
  // the arrival as it will be: the receiver and every defender running at the spot the ball comes down to (as they do,
  // thinkPursue / the routes' ball tracking), the same odds resolveAir uses at the ball, rolled now
  Ball& b = ball;
  b.fateRolled = true;
  Ent* e = b.target;
  if (!e || b.away || !e->eligible || e->state == EntState::Down) return;
  const FateRoll r = rollCatch((std::max)(0.0, b.flightT - b.t), false);
  if (!r.rolled) {
    b.fateRolled = false;
    return;
  }
  if (r.passed) b.fatePassed = r.passed;
  if (r.p >= 0) {
    b.fateP = r.p;
    b.fateD = r.d;
    b.fateCont = r.cont;
    b.fatePbu = r.pbu;
  }
  if (r.fate == 0) return;
  b.fate = r.fate;
  b.fateBy = r.by;
  if (r.dive) b.fateDive = true;
  // (the forecast made at the release, revised if the roll came out otherwise)
  reviseFate(r.fate, r.fate >= 3 ? r.by : e, "roll");
}

double PlaySim::laneU(const Ent* o, int k) const {
  // (a uniform draw per pass, defender and decision off the key drawn at the release: the forecast and the lane roll
  // read the same one)
  uint64_t h = (static_cast<uint64_t>(ball.fateKey) << 21) ^ (static_cast<uint64_t>(stableKey(o)) * 0x9e3779b97f4a7c15ull) ^ (static_cast<uint64_t>(k + 1) * 0xbf58476d1ce4e5b9ull);
  h ^= h >> 31;
  h *= 0x94d049bb133111ebull;
  h ^= h >> 29;
  return static_cast<double>(h >> 11) / 9007199254740992.0;
}

namespace {
/** Where a man will be in s seconds running at a spot: his run carried on, blended (k) with running straight at it (the
 *  preRollCatch model of the arrival). */
V2 fateRunTo(const Ent* x, const V2& spot, double s, double k) {
  const V2 lin{x->pos.x + x->vel.x * s, x->pos.y + x->vel.y * s};
  const double d0 = dist(x->pos, spot);
  const double run = (std::min)(d0, x->vmax * s);
  const V2 chase = d0 > 1e-6 ? add(x->pos, scale(sub(spot, x->pos), run / d0)) : x->pos;
  return V2{lin.x * (1 - k) + chase.x * k, lin.y * (1 - k) + chase.y * k};
}
}  // namespace

void PlaySim::fateGeom(const Ent* x, bool chase, V2& at, double& z, double& dt) const {
  // the flight from here on, at 60 Hz, against where the man will be (running at the ball's landing): the nearest pass
  // of it to him at a height he can play; with no man, where it comes down
  const Ball& b = ball;
  const V2 bp = ballLanding();
  V3 p = b.pos, v = b.vel;
  const double k = dragK();
  const double h = 1.0 / 60.0;
  double best = 1e9;
  at = V2{p.x, p.y};
  z = p.z;
  dt = 0;
  const double horizon = (std::max)(0.2, b.flightT - b.t) + 0.6;
  for (double s = 0; s <= horizon; s += h) {
    if (x) {
      const V2 xp = chase ? fateRunTo(x, bp, s, x == b.target ? 0.9 : 0.6) : V2{x->pos.x + x->vel.x * s, x->pos.y + x->vel.y * s};
      const double dd = jsHypot(p.x - xp.x, p.y - xp.y) + (p.z < 0.2 ? 5.0 : 0.0);
      if (dd < best) {
        best = dd;
        at = V2{p.x, p.y};
        z = p.z;
        dt = s;
      }
    }
    if (p.z <= 0.05) {
      if (!x) {
        at = V2{p.x, p.y};
        z = 0;
        dt = s;
      }
      break;
    }
    integrate(p, v, k, h);
  }
}

void PlaySim::reviseFate(int fate, Ent* who, const std::string& why) {
  Ball& b = ball;
  if (b.fcFate == 0 || b.fcOut != 0) return;  // (no forecast; or the ball has been played: that is the outcome)
  if (fate == b.fcFate && who == b.fcBy) return;
  b.fcFate = fate;
  b.fcBy = who;
  b.fcRev++;
  b.fcWhy = why;
  double dt = 0;
  fateGeom(who, true, b.fcAt, b.fcZ, dt);
  b.fcT = t + dt;
  b.fcCont = b.fateCont;
  b.fcDive = b.fateDive;
}

void PlaySim::forecastFate() {
  // the outcome at the release: a defender in the lane first (preRollLane's test along the flight, his own draws), then
  // the receiver's arrival (preRollCatch's model and odds over the whole flight, the roll's own draws)
  Ball& b = ball;
  Ent* e = b.target;
  b.fcFate = 0;
  if (!e || b.away || cfg.kick || cfg.place) return;
  const double k = dragK();
  const double h = 1.0 / 60.0;
  const V2 bp = ballLanding();
  // the flight from the release, and when it passes nearest the receiver
  std::vector<V3> path;
  {
    V3 p = b.pos, v = b.vel;
    const double horizon = b.flightT + 0.6;
    for (double s = 0; s <= horizon && p.z > 0.05; s += h) {
      path.push_back(p);
      integrate(p, v, k, h);
    }
  }
  double tgtT = b.flightT, tgtBest = 1e9;
  for (size_t i = 0; i < path.size(); i++) {
    const V2 rp = fateRunTo(e, bp, i * h, 0.9);
    const double dd = jsHypot(path[i].x - rp.x, path[i].y - rp.y);
    if (dd < tgtBest) tgtBest = dd, tgtT = i * h;
  }
  struct Lane {
    Ent* o;
    double s, cd, cz, rd;
    V3 at;
  };
  std::vector<Lane> lanes;
  for (Ent* o : def) {
    if (!o->free() || !eyesOnBall(o)) continue;
    const double oh = o->p->heightIn / 36;
    const double reach = oh * 0.5 + 0.55 + (o->r().ballSkills > 60 ? 0.1 : 0);
    for (size_t i = 1; i < path.size(); i++) {
      const double s = i * h;
      if (s <= 0.05 || b.t + s <= 0.36 || s >= tgtT - 0.02) continue;
      const V2 op = fateRunTo(o, bp, s, 0.6);
      const double cd = jsHypot(path[i].x - op.x, path[i].y - op.y);
      const double cz = path[i].z;
      if (cd < reach - 0.15 && cz > 0.3 && cz < oh + 0.8) {
        const V2 rp = fateRunTo(e, bp, s, 0.9);
        lanes.push_back(Lane{o, s, cd, cz, jsHypot(path[i].x - rp.x, path[i].y - rp.y), path[i]});
        break;
      }
    }
  }
  std::stable_sort(lanes.begin(), lanes.end(), [](const Lane& x, const Lane& y) { return x.s < y.s; });
  bool set = false;
  for (const Lane& ln : lanes) {
    Ent* o = ln.o;
    const double edge = clamp(ln.rd - ln.cd, -2, 1.5);
    const double pPlay = sigmoid((o->r().ballSkills - 70) / 10 + edge * 1.0 - 1.1 - ln.cd * 0.8 - (ln.cz > 2.5 ? 0.8 : 0) + o->fx.pbu - e->fx.winBall);
    if (!(laneU(o, 0) < pPlay)) continue;
    const double pInt = sigmoid((o->r().catching - 62) / 10 + edge * 0.6 - 1.2 - (ln.at.x - los > 18 ? 1.0 : 0) + tune().intBias + o->fx.intAdj - (b.thrower ? b.thrower->fx.ballSafe : 0.0));
    b.fcFate = laneU(o, 1) < pInt ? 4 : 3;
    b.fcBy = o;
    b.fcAt = V2{ln.at.x, ln.at.y};
    b.fcZ = ln.at.z;
    b.fcT = t + ln.s;
    set = true;
    break;
  }
  if (!set) {
    FateRoll r;
    if (e->eligible && e->state != EntState::Down) r = rollCatch(b.flightT, true);
    if (!r.rolled || r.fate == 0) {
      b.fcFate = 5;
      b.fcBy = nullptr;
    } else {
      b.fcFate = r.fate;
      b.fcBy = r.fate >= 3 ? r.by : e;
      b.fcCont = r.cont;
      b.fcDive = r.dive;
    }
    double dt = 0;
    fateGeom(b.fcBy, true, b.fcAt, b.fcZ, dt);
    b.fcT = t + dt;
  }
  b.fcFate0 = b.fcFate;
  b.fcBy0 = b.fcBy;
  b.fcAt0 = b.fcAt;
  b.fcZ0 = b.fcZ;
  b.fcT0 = b.fcT;
}

PlaySim::FateRoll PlaySim::rollCatch(double Ta, bool forecast) {
  Ball& b = ball;
  FateRoll out;
  Ent* e = b.target;
  const V2 bp = ballLanding();
  static const double kr = std::getenv("TAG_FATE_KR") ? std::atof(std::getenv("TAG_FATE_KR")) : 0.6;
  static const double kd = std::getenv("TAG_FATE_KD") ? std::atof(std::getenv("TAG_FATE_KD")) : 0.35;
  // where a man will be: his run carried on, blended with running straight at the ball (k: the receiver tracks it more
  // than a defender closes on it; fit so the arrival odds come out as resolveAir's)
  // (the forecast at the release looks the whole flight ahead: the receiver runs to the ball he was thrown (it is thrown
  // to where he is going) and a defender breaks on it once he has seen it - fcKr / fcKd, his reaction first)
  static const double fcKr = std::getenv("TAG_FC_KR") ? std::atof(std::getenv("TAG_FC_KR")) : 0.9;
  static const double fcKd = std::getenv("TAG_FC_KD") ? std::atof(std::getenv("TAG_FC_KD")) : 0.6;
  auto runTo = [&](const Ent* x, double rk) {
    const double k = rk >= 1.0 ? (forecast ? fcKr : kr) : (forecast ? fcKd : kd);
    const V2 lin{x->pos.x + x->vel.x * Ta, x->pos.y + x->vel.y * Ta};
    const double d0 = dist(x->pos, bp);
    const double run = (std::min)(d0, x->vmax * (forecast && rk < 1.0 ? (std::max)(0.0, Ta - x->reactT * tune().zoneReactK) : Ta));
    const V2 chase = d0 > 1e-6 ? add(x->pos, scale(sub(bp, x->pos), run / d0)) : x->pos;
    const V2 at{lin.x * (1 - k) + chase.x * k, lin.y * (1 - k) + chase.y * k};
    return std::make_pair(dist(at, bp), at);
  };
  const auto rr = runTo(e, 1.0);
  bool flown = false;  // (rolled on the flight's own nearest pass to him, below: the arrival as it is, its height known)
  double flownZ = 0;
  double d = rr.first;
  const V2 ep = rr.second;
  if (d > e->p->heightIn / 36 * 0.5 + 0.55) {
    // he will not get to it: nothing to roll. (Asked again as the ball comes on, and of the flight itself - a ball
    // that passes within his reach at a height he can play is his to play wherever it would have come down, a high
    // one yards past him: a quarter of all catches were made by a man this said would not get there, rolled at the
    // ball and awarded from where he was - the farthest ones)
    if (tune().homeA <= 0) return out;
    const double eh = e->p->heightIn / 36;
    const V2 cr{b.pos.x - e->pos.x, b.pos.y - e->pos.y};
    const V2 cu{b.vel.x - e->vel.x, b.vel.y - e->vel.y};
    const double cuu = dot(cu, cu);
    const double ct = cuu > 1e-6 ? -dot(cr, cu) / cuu : -1.0;
    const double cz = b.pos.z + b.vel.z * ct - 0.5 * G * ct * ct;
    const double cd = jsHypot(cr.x + cu.x * ct, cr.y + cu.y * ct);
    if (!(ct >= 0 && ct <= 0.4 && cd < eh * 0.5 + 0.55 && cz > 0.2 && cz < eh + 0.9)) {
      out.rolled = false;
      return out;
    }
    d = cd;
    flown = true;
    flownZ = cz;
  }
  if (tune().forceInt > 0 && !forecast) {
    // (filming interceptions: the defender who will be nearest the ball takes it, when he will be within a stretch of
    // it - homeBall then brings it onto his hands. It was the man nearest the receiver from up to 6 yd: a lineman
    // took a pass meant for a back four yards from him, the ball 80 cm from his hands when it was given to him)
    Ent* dn = nullptr;
    double dnD = 1e9;
    for (Ent* o : def) {
      if (!o->free() || dist(o->pos, e->pos) > 6.0 || !eyesOnBall(o)) continue;
      const double od = runTo(o, 0.9).first;
      if (od < o->p->heightIn / 36 * 0.5 + 0.85 && od < dnD) {
        dn = o;
        dnD = od;
      }
    }
    if (dn && rng.chance(tune().forceInt)) {
      out.fate = 4;
      out.by = dn;
      return out;
    }
  }
  const bool deepBall = bp.x - los > 15;
  const double midK = tune().midContest > 0 ? clamp((bp.x - los - 6) / 9, 0, 1) * tune().midContest : 0;
  const double cRad = deepBall ? 2.2 : 1.3 + 0.9 * midK;
  const double airYds = bp.x - los;
  // a defender to the ball first plays it (resolveAir takes the nearest man first) - one who can see it: a man with his
  // back to it plays the receiver, below
  Ent* first = nullptr;
  double fd = d;
  for (Ent* o : def) {
    if (!o->free() || !eyesOnBall(o)) continue;
    const double od = runTo(o, 0.9).first;
    const double reach = o->p->heightIn / 36 * 0.5 + 0.55 + (o->r().ballSkills > 60 ? 0.1 : 0);
    if (od < reach && od < fd) {
      first = o;
      fd = od;
    }
  }
  if (first) {
    const double edge = clamp(d - fd, -2, 1.5);
    const double pPlay = sigmoid((first->r().ballSkills - 70) / 10 + edge * 1.0 - 1.1 - fd * 0.8 + first->fx.pbu - e->fx.winBall);
    if (b.fateU[0] < pPlay) {
      const double pInt = sigmoid((first->r().catching - 62) / 10 + edge * 0.6 - 1.2 - (airYds > 18 ? 1.0 : 0) + tune().intBias + first->fx.intAdj -
                                  (b.thrower ? b.thrower->fx.ballSafe : 0.0));
      out.fate = b.fateU[1] < pInt ? 4 : 3;
      out.by = first;
      return out;
    }
    out.passed = first;
  }
  // the receiver's catch, contested by the defenders there with him
  std::vector<Ent*> oppNear;
  for (Ent* o : def)
    if (o->free() && dist(runTo(o, 0.9).second, ep) < cRad) oppNear.push_back(o);
  const bool contested = !oppNear.empty();
  const double skill = contested ? e->r().catchInTraffic * 0.6 + e->r().catching * 0.4 + e->fx.contested : e->r().catching;
  const double offT = d / 1.2;
  const double ballSpeed = jsHypot(b.vel.x, b.vel.y, b.vel.z);
  double nearOpp = 9;
  for (Ent* o : oppNear) nearOpp = (std::min)(nearOpp, dist(runTo(o, 0.9).second, ep));
  const double tight = contested ? clamp(1 - nearOpp / cRad, 0, 1) : 0;
  const double tough = offT * 1.2 + (ballSpeed > 24 ? 0.25 : 0) +
                       (contested ? tune().contested + tight * (deepBall ? tune().tightDeep : tune().tightShort + (tune().tightDeep - tune().tightShort) * midK) : 0);
  const double wet = cfg.weather.precip != Precip::None ? 0.25 : 0;
  // (fateBias: the prediction reads the catch a shade harder than the arrival turns out - its spread of where the men
  // will be is wider than theirs - measured with TAG_FATE_STATS over 200 games and taken back out here)
  static const double fateBias = std::getenv("TAG_FATE_BIAS") ? std::atof(std::getenv("TAG_FATE_BIAS")) : 0.3;
  // (a roll on the flight itself is resolveAir's own: the ball's height counted, no bias)
  const double toughZ = flown ? (flownZ > 2.6 ? 0.5 : 0) + (flownZ < 0.6 ? 0.4 : 0) : 0.0;
  const double p = sigmoid(4.2 + (skill - 75) / 9 - (tough + toughZ) * 1.5 - wet + (flown ? 0.0 : fateBias));
  out.p = p;
  out.d = d;
  out.cont = contested;
  out.pbu = 0;
  if (contested) {
    Ent* dbest = oppNear[0];
    for (Ent* o : oppNear)
      if (runTo(o, 0.9).first < runTo(dbest, 0.9).first) dbest = o;
    const double edge = d - runTo(dbest, 0.9).first;
    // (a man who cannot see it swipes at the hands: lower odds, and never comes down with it)
    const bool sees = eyesOnBall(dbest);
    const double pbu = sigmoid((dbest->r().ballSkills - 70) / 12 + edge * 1.6 - 1.2 + (deepBall ? 0.35 : 0) + dbest->fx.pbu - e->fx.winBall) * (deepBall ? 0.6 : 0.62) *
                       (sees ? 1.0 : tune().swipeK);
    out.pbu = pbu;
    if (b.fateU[2] < pbu) {
      if (!sees) {
        out.fate = 3;
        out.by = dbest;
        return out;
      }
      const double pInt = sigmoid((dbest->r().catching - 62) / 10 + edge * 1.2 - 1.4 - (deepBall ? 1.0 : 0) + tune().intBias + dbest->fx.intAdj -
                                  (b.thrower ? b.thrower->fx.ballSafe : 0.0) - e->fx.winBall);
      out.fate = b.fateU[3] < pInt ? 4 : 3;
      out.by = dbest;
      return out;
    }
  }
  out.fate = b.fateU[4] < p ? 1 : 2;
  out.by = nullptr;
  // a catch he has to lay out for (resolveAir's own test, on where the men will be): known now, so the ball is brought
  // to where a dive's hands are and the dive can start before it gets there
  // (a dive goes the way he faces - the capture's way: only with the ball ahead of him that way, inDive's test)
  const V2 toLand = sub(bp, ep);
  const bool diveAhead = len(toLand) > 0.3 && (toLand.x * std::cos(e->face) + toLand.y * std::sin(e->face)) / len(toLand) > 0.5;
  if (out.fate == 1 && d > 1.05 && ep.x < 99.0 && (tune().homeA <= 0 || diveAhead))
    for (Ent* o : def)
      if (o->free() && dist(runTo(o, 0.9).second, ep) < 2.0) out.dive = true;
  return out;
}

void PlaySim::preRollLane() {
  // A defender the ball goes within reach of before it gets to the man it was thrown to - an underneath man in the
  // lane, a corner undercutting the route - had his go at it rolled at the ball, at his nearest to it: from up to
  // 1.7 yd off, three in four of all breakups and interceptions by a defender first to the ball (TAG_CATCH_STATS).
  // Rolled here instead, with the same odds, as soon as the flight says it will pass within his reach (0.35 s out or
  // less, on where he and the ball will be), so the ball can be brought onto his hands (homeBall) and his play on it
  // started in time. One go a man, as before; the ones this does not see coming are still rolled at the ball.
  Ball& b = ball;
  Ent* tgt = b.target;
  if (!tgt || b.fate >= 3) return;
  auto approach = [&b](const Ent* x, double& ct, double& cd, double& cz) {
    const V2 cr{b.pos.x - x->pos.x, b.pos.y - x->pos.y};
    const V2 cu{b.vel.x - x->vel.x, b.vel.y - x->vel.y};
    const double cuu = dot(cu, cu);
    ct = cuu > 1e-6 ? -dot(cr, cu) / cuu : -1.0;
    cd = jsHypot(cr.x + cu.x * ct, cr.y + cu.y * ct);
    cz = b.pos.z + b.vel.z * ct - 0.5 * G * ct * ct;
  };
  double tgtT = 0, tgtD = 0, tgtZ = 0;
  approach(tgt, tgtT, tgtD, tgtZ);
  if (tgtT < 0) tgtT = (std::max)(0.0, b.flightT - b.t);
  for (Ent* o : def) {
    if (!o->free() || reached_.count(o) || lanePassed_.count(o) || b.fatePassed == o || !eyesOnBall(o)) continue;  // (one who cannot see it does not undercut it)
    double ct = 0, cd = 0, cz = 0;
    approach(o, ct, cd, cz);
    const double oh = o->p->heightIn / 36;
    const double reach = oh * 0.5 + 0.55 + (o->r().ballSkills > 60 ? 0.1 : 0);
    if (!(ct > 0.05 && ct <= 0.35 && ct < tgtT - 0.02 && b.t + ct > 0.36 && cd < reach - 0.15 && cz > 0.3 && cz < oh + 0.8)) continue;
    lanePassed_[o] = t;
    const V2 bAt{b.pos.x + b.vel.x * ct, b.pos.y + b.vel.y * ct};
    const double rd = dist(V2{tgt->pos.x + tgt->vel.x * ct, tgt->pos.y + tgt->vel.y * ct}, bAt);
    const double edge = clamp(rd - cd, -2, 1.5);
    const double pPlay = sigmoid((o->r().ballSkills - 70) / 10 + edge * 1.0 - 1.1 - cd * 0.8 - (cz > 2.5 ? 0.8 : 0) + o->fx.pbu - tgt->fx.winBall);
    // (his draws are the ones the release's forecast read: laneU)
    if (!(laneU(o, 0) < pPlay)) continue;
    const double pInt = sigmoid((o->r().catching - 62) / 10 + edge * 0.6 - 1.2 - (bAt.x - los > 18 ? 1.0 : 0) + tune().intBias + o->fx.intAdj - (b.thrower ? b.thrower->fx.ballSafe : 0.0));
    b.fate = laneU(o, 1) < pInt ? 4 : 3;
    b.fateBy = o;
    b.fateDive = false;
    b.fateRolled = true;
    reviseFate(b.fate, o, "lane");
    return;
  }
}

void PlaySim::homeBall(double dt) {
  // The outcome is rolled 0.4 s out for a man who will be within a body's reach of where the ball comes down - up to
  // 1.6 yd from his feet, any height from his shins to a yard over his head - and it was then awarded from there: a
  // quarter of all catches with the ball more than an arm from his shoulders, 1 in 10 more than 1.2 m (TAG_CATCH_STATS).
  // From the roll on the throw is the one that outcome needs: the ball's flight is bent, by no more than homeA, so
  // that at its nearest to him it is where his hands can be - within half a yard of his body between his knees and
  // the top of his reach, or a yard ahead of him at his waist when he lays out for it. The sim's ball stays the one
  // ball there is (what is drawn, what is deflected): nothing is moved at the catch.
  Ball& b = ball;
  Ent* e = b.fate >= 3 ? b.fateBy : b.target;
  if (!e || e->state == EntState::Down || (b.fate >= 3 && !e->free()) || reached_.count(e)) return;
  const V2 r{b.pos.x - e->pos.x, b.pos.y - e->pos.y};
  const V2 u{b.vel.x - e->vel.x, b.vel.y - e->vel.y};
  const double uu = dot(u, u);
  double tc = uu > 0.25 ? -dot(r, u) / uu : 9.0;  // until it is nearest him
  // (a ball dropping in over a man running under it never gets "nearest" before it is at the turf: his go at it is
  // when it comes down to his hands, a yard up)
  if (tc > 0 && b.pos.z + b.vel.z * tc - 0.5 * G * tc * tc < 0.35 && b.pos.z > 1.0)
    tc = (std::min)(tc, (b.vel.z + std::sqrt(b.vel.z * b.vel.z + 2 * G * (b.pos.z - 1.0))) / G);
  if (tc < dt || tc > 0.6) return;
  const double h = e->p->heightIn / 36.0;
  if (b.fate <= 2 && !b.ghostHit) {
    // (only a ball the throw itself puts within his reach: one sailing past him is left to)
    const V2 gr{b.ghostPos.x - e->pos.x, b.ghostPos.y - e->pos.y};
    const V2 gu{b.ghostVel.x - e->vel.x, b.ghostVel.y - e->vel.y};
    const double guu = dot(gu, gu);
    const double gt = guu > 1e-6 ? (std::max)(0.0, -dot(gr, gu) / guu) : 0.0;
    const double gz = b.ghostPos.z + b.ghostVel.z * gt - 0.5 * G * gt * gt;
    // (he is running at it: 2.5 yd/s of what is left is his to close - the roll itself said he gets there)
    if (jsHypot(gr.x + gu.x * gt, gr.y + gu.y * gt) > h * 0.5 + 0.55 + 2.5 * gt || gz > h + 0.9 || gz < 0.2 - 2.0 * gt) return;
  }
  const V2 m{r.x + u.x * tc, r.y + u.y * tc};  // where it will be then, from his feet
  const double z = b.pos.z + b.vel.z * tc - 0.5 * G * tc * tc;
  const double ml = len(m);
  V2 at = m;
  double zLo = 0.35, zHi = h + 0.2;
  if (b.fate == 1 && b.fateDive) {
    // (the way he faces: the dive's capture goes that way, inDive)
    at = V2{std::cos(e->face) * 1.1, std::sin(e->face) * 1.1};
    zLo = 0.3;
    zHi = 1.1;
  } else if (ml > 0.45) {
    at = scale(m, 0.45 / ml);
  }
  const V3 need{(at.x - m.x) / tc, (at.y - m.y) / tc, (clamp(z, zLo, zHi) - z) / tc};
  const double nl = jsHypot(need.x, need.y, need.z);
  if (nl < 1e-9) return;
  // (a throw that is in the air less than the 0.4 s the roll looks ahead - a dump-off, a quick out - gets the same
  // reach of steering in the time it has: homeA scaled by (0.4 / its time left at the roll)^2, up to 4x)
  const double shortK = b.fateLeft > 0.05 ? std::pow(clamp(0.4 / b.fateLeft, 1.0, 2.0), 2) : 1.0;
  const double k = (std::min)(1.0, tune().homeA * shortK * dt / nl);
  b.vel = V3{b.vel.x + need.x * k, b.vel.y + need.y * k, b.vel.z + need.z * k};
}

void PlaySim::resolveAir() {
  Ball& b = ball;
  if (b.vel.z > 0 && b.t < 0.25) return;
  if (b.pos.z > 3.3) return;
  // candidates within reach
  struct Cand {
    Ent* e;
    double d;
  };
  std::vector<Cand> cands;
  for (Ent* e : ents) {
    if (e == b.thrower || (!e->free() && !(e->engaged && e->side == Side::Def && e->isDL && b.t < 0.4))) continue;
    const double reach = e->p->heightIn / 36 * 0.5 + 0.55 + (e->r().ballSkills > 60 && e->side == Side::Def ? 0.1 : 0);
    const double d = jsHypot(e->pos.x - b.pos.x, e->pos.y - b.pos.y);
    const bool zOk = b.pos.z < e->p->heightIn / 36 + 0.9 && b.pos.z > 0.2;
    if (d < reach && zOk) cands.push_back(Cand{e, d});
  }
  if (cands.empty()) return;
  // batted at the line
  const Cand* early = nullptr;
  for (const Cand& c : cands)
    if (c.e->side == Side::Def && c.e->isDL && !reached_.count(c.e) && c.d < 0.8) {
      early = &c;
      break;
    }
  if (early && b.t < 0.35) {
    Ent* ee = early->e;
    reached_[ee] = t;  // one swipe per lineman
    if (rng.chance(0.07 + (std::max)(0.0, ee->p->heightIn - 76) * 0.02)) {
      ev("batted", ee);
      pbuBy = ee;
      const double vx = rng.range(-3, 3);
      const double vy = rng.range(-3, 3);
      b.vel = V3{vx, vy, 2};
      b.target = nullptr;
      startTumble(1.0);
    }
    return;
  }
  // each candidate gets one try per throw, at their closest approach to the ball
  std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& c) { return a.d - c.d < 0; });
  for (const Cand& cd : cands) {
    Ent* e = cd.e;
    const double d = cd.d;
    if (reached_.count(e)) continue;
    // a teammate who is not the man it was thrown to only catches a ball thrown to his hands: late in the flight, on
    // the spot the QB threw to, at hands height (a back underneath took passes meant for a crosser over him), and
    // never a throwaway. (A tipped ball has no target: anyone's.)
    if (e->side == Side::Off && (b.away || (b.target && e != b.target))) {
      if (b.away) continue;
      const double frac = b.flightT > 0 ? b.t / b.flightT : 1.0;
      const bool onSpot = b.targetSpot && dist(e->pos, *b.targetSpot) < 1.5;
      const bool handsZ = b.pos.z > 0.6 && b.pos.z < e->p->heightIn / 36.0 + 0.3;
      if (!(frac > 0.8 && onSpot && handsZ && d < 0.8)) continue;
    }
    auto lastIt = lastBallD_.find(e);
    const bool hasLast = lastIt != lastBallD_.end();
    const double last = hasLast ? lastIt->second : 0;
    lastBallD_[e] = d;
    const bool descendingLow = b.vel.z < 0 && b.pos.z < 0.9;
    // (a low ball on its way down is played the moment it is within reach - from 1.5 yd off - except by the man it is
    // being brought to, homeBall: he plays it when it gets to him, above his shins)
    bool homedTo = tune().homeA > 0 && b.ghost && b.fate != 0 && e == (b.fate >= 3 ? b.fateBy : b.target) && b.pos.z > 0.3;
    if (homedTo) {
      // (unless it will be at the turf before it is any nearer: a ball dropping in over a man running under it)
      const double hx = b.pos.x - e->pos.x, hy = b.pos.y - e->pos.y, hux = b.vel.x - e->vel.x, huy = b.vel.y - e->vel.y;
      const double huu = hux * hux + huy * huy;
      const double ht = huu > 0.25 ? -(hx * hux + hy * huy) / huu : 9.0;
      homedTo = ht > 0 && b.pos.z + b.vel.z * ht - 0.5 * G * ht * ht >= 0.35;
    }
    if (d > 0.35 && (!descendingLow || homedTo) && (!hasLast || d < last - 0.005)) continue;
    // a ball that was not brought to him (no outcome rolled ahead of it), passing more than handReach from his feet: past
    // what a man on his feet gets his hands to (1.3 yd: his arm, the lean and a step). Caught only laid out for it -
    // with a defender on him, where the play ends on the turf (below) - otherwise it goes by off his fingertips. (It
    // was caught standing from 1.6 yd, the ball shown going past him and then in his arm.)
    // (2026-11-21: the test is now where the ball is against his hands, inHands - height counted - and holds for a ball
    // brought to him too: a pass the steering could not bring in, a 0.2 s dump-off, was caught 1.2 m from his hands,
    // and a dive went one way with the ball 2 m off the other. Laid out for only where a dive's hands get, inDive.)
    bool layoutCatch = false;
    if (tune().homeA > 0 && tune().handReach > 0 && e->side == Side::Off && !inHands(e, b.pos, 1.0)) {
      bool layout = e == b.target && b.fate == 1 && b.fateDive;
      for (const Ent* o : def)
        if (o->free() && dist(o->pos, e->pos) < 2.0) layout = true;
      if (!layout || e->pos.x >= 99.0 || !inDive(e, b.pos)) continue;
      layoutCatch = true;
    }
    // (a ball brought onto his hands that the throw as it was thrown never put within his reach: not his - see Ball::ghost)
    if (tune().homeA > 0 && e == b.target && (b.fate == 1 || b.fate == 2) && b.ghost && !b.ghostHit) continue;
    // a breakup / interception rolled for a defender the ball is still on its way to: the receiver's go waits for him
    if (tune().homeA > 0 && e == b.target && b.fate >= 3 && b.fateBy && b.fateBy->free() && !reached_.count(b.fateBy)) {
      const Ent* fb = b.fateBy;
      const double fbx = b.pos.x - fb->pos.x, fby = b.pos.y - fb->pos.y;
      if (fbx * (b.vel.x - fb->vel.x) + fby * (b.vel.y - fb->vel.y) < 0 && jsHypot(fbx, fby) < 4.0) continue;
    }
    reached_[e] = t;
    if (arriveSep < 0 && b.target && (e == b.target || dist(e->pos, b.target->pos) < 4)) {
      arriveSep = 99;
      for (Ent* o : def)
        if (o->state != EntState::Down) arriveSep = (std::min)(arriveSep, dist(o->pos, b.target->pos));
    }
    const bool deepBall = b.pos.x - los > 15;
    // how far off a defender still contests the catch: 1.3 yd on a short ball, 2.2 on a deep one, growing between 6
    // and 15 yd (a step at 15 left every 10-15 yd throw as easy as a slant: 66 % caught against the NFL's 57)
    const double midK = tune().midContest > 0 ? clamp((b.pos.x - los - 6) / 9, 0, 1) * tune().midContest : 0;
    const double cRad = deepBall ? 2.2 : 1.3 + 0.9 * midK;
    const V2 bp{b.pos.x, b.pos.y};
    std::vector<Ent*> oppNear;
    for (Ent* o : ents)
      if (o->side != e->side && o->free() && dist(o->pos, e->pos) < cRad) oppNear.push_back(o);
    const bool contested = !oppNear.empty();
    const double airYds = b.pos.x - los;
    if (e->side == Side::Off) {
      if (!e->eligible) continue;
      // catch probability
      const double skill = contested ? e->r().catchInTraffic * 0.6 + e->r().catching * 0.4 + e->fx.contested : e->r().catching;
      const double offT = d / 1.2;  // how far off-target relative to reach
      const double ballSpeed = jsHypot(b.vel.x, b.vel.y, b.vel.z);
      // how tight the coverage is at the catch: a defender in phase (hip to hip) makes it a ~45% catch (PFF contested
      // catch rates), one a couple of yards off barely matters
      double nearOpp = 9;
      for (Ent* o : oppNear) nearOpp = (std::min)(nearOpp, dist(o->pos, e->pos));
      const double tight = contested ? clamp(1 - nearOpp / cRad, 0, 1) : 0;
      if (contested && e == b.target) {
        // a defender in phase and not ahead to the ball is playing the man: the tighter, the likelier the flag
        Ent* dm = oppNear[0];
        for (Ent* o : oppNear)
          if (dist(o->pos, e->pos) < dist(dm->pos, e->pos)) dm = o;
        const bool behind = dist(dm->pos, bp) > d - 0.2;
        // (one who cannot see the ball is playing nothing but the man: more of it is interference)
        foulDpi.add(tight * tight * (deepBall ? 2.4 : 0.8) * (behind ? 1.0 : 0.35) * (eyesOnBall(dm) ? 1.0 : tune().dpiBlind), dm->p);
        foulOpi.add(tight * 0.075, e->p);
      }
      const double tough = offT * 1.2 + (ballSpeed > 24 ? 0.25 : 0) + (contested ? tune().contested + tight * (deepBall ? tune().tightDeep : tune().tightShort + (tune().tightDeep - tune().tightShort) * midK) : 0) +
                           (b.pos.z > 2.6 ? 0.5 : 0) + (b.pos.z < 0.6 ? 0.4 : 0);
      const double wet = cfg.weather.precip != Precip::None ? 0.25 : 0;
      const double p = sigmoid(4.2 + (skill - 75) / 9 - tough * 1.5 - wet);
      if (e == b.target && b.fateP >= 0) {
        gFate.n++;
        gFate.pPre += b.fateP;
        gFate.pArr += p;
        gFate.dPre += b.fateD;
        gFate.dArr += d;
        gFate.nCont += contested;
        gFate.nContPre += b.fateCont;
        gFate.nBoth += contested && b.fateCont;
        gFate.pbuPre += b.fatePbu;
        if (contested) {
          Ent* db = oppNear[0];
          for (Ent* o : oppNear)
            if (dist(o->pos, bp) < dist(db->pos, bp)) db = o;
          const double edge = d - dist(db->pos, bp);
          gFate.pbuArr += sigmoid((db->r().ballSkills - 70) / 12 + edge * 1.6 - 1.2 + (deepBall ? 0.35 : 0) + db->fx.pbu - e->fx.winBall) * (deepBall ? 0.6 : 0.62);
        }
      }
      // the outcome rolled before the ball got here (preRollCatch): it stands when the men it names are there to play
      // it (a breakup / interception needs its defender free and near; otherwise the ball is rolled here as before)
      int fate = e == b.target ? b.fate : 0;
      if ((fate == 3 || fate == 4) && !(b.fateBy && b.fateBy->free() && dist(b.fateBy->pos, e->pos) < (tune().forceInt > 0 ? 4.0 : cRad))) fate = 0;
      // the defender a breakup / interception was rolled for plays it with his own hands (the receiver's go waited for
      // him above): if the ball never got within his reach it is rolled here as before. (It was the defender's at
      // once, off a ball up to 2.3 yd from him.)
      if (fate >= 3 && tune().homeA > 0) {
        const Ent* fb = b.fateBy;
        bool inReach = false;
        for (const Cand& c2 : cands) inReach = inReach || c2.e == fb;
        if (!inReach) fate = 0;
      }
      // defenders arriving with the ball can break it up
      Ent* contestBy = nullptr;  // (the man at the catch point, and how much nearer the ball than the receiver he was)
      double contestEdge = -9;
      if (contested || fate == 3 || fate == 4) {
        // best-positioned defender: closest to the ball at arrival
        Ent* dbest = fate >= 3 ? b.fateBy : oppNear[0];
        if (fate < 3)
          for (Ent* o : oppNear)
            if (dist(o->pos, bp) < dist(dbest->pos, bp)) dbest = o;
        const double edge = d - dist(dbest->pos, bp);  // >0: defender closer to the ball than the receiver
        contestBy = dbest;  // (a trail man playing through the hands is credited too, as the charters do)
        contestEdge = edge;
        // (eyes on the ball: one with his back to it swipes at the hands as they come up - swipeK x the odds, no pick)
        const bool sees = eyesOnBall(dbest);
        const double faceDeg = ballFacingDeg(dbest);
        const bool seeable = canSeeBall(dbest);  // (telemetry, before the breakup takes the ball off its target)
        const double pbu = sigmoid((dbest->r().ballSkills - 70) / 12 + edge * 1.6 - 1.2 + (deepBall ? 0.35 : 0) + dbest->fx.pbu - e->fx.winBall) * (deepBall ? 0.6 : 0.62) *
                           (sees ? 1.0 : tune().swipeK);
        if (fate >= 3 || (fate == 0 && rng.chance(pbu))) {
          pbuBy = dbest;
          pbuBlind = !sees;
          deepContested = airYds > 15;
          gContact.add(3, dbest, b.pos, fate >= 3);
          ev("pbu", dbest, e);
          b.vel = scale3(b.vel, 0.25);
          b.vel.z = 1.5;
          b.target = nullptr;
          startTumble(1.0);
          // defender in great position may come down with it; or tipped and picked
          const double pInt = sigmoid((dbest->r().catching - 62) / 10 + edge * 1.2 - 1.4 - (deepBall ? 1.0 : 0) + tune().intBias + dbest->fx.intAdj - (b.thrower ? b.thrower->fx.ballSafe : 0.0) - e->fx.winBall);
          // (an interception is the defender's hands on it, inHands; a ball past them he only tips)
          const bool dHands = sees && !(tune().homeA > 0 && tune().handReach > 0 && !inHands(dbest, b.pos, 1.0));
          if (dHands && (fate == 4 || (fate == 0 && rng.chance(pInt)))) {
            gFate.intRecv++;
            gFate.intRecvF += fate == 4;
            gContact.add(4, dbest, b.pos, fate == 4);
            intBlind = !seeable;  // (telemetry: never with the rule on - any other way to a pick would show here)
            intFaceDeg = faceDeg;
            intercept(dbest);
          }
          return;
        }
        if (airYds > 15) deepContested = true;
      }
      if (fate == 1 || (fate != 2 && rng.chance(p))) {
        // feet in bounds?
        if (e->pos.y < -0.05 || e->pos.y > FIELD_W + 0.05 || e->pos.x > 110) {
          ev("oobcatch", e);
          endPlay("incomplete");
          return;
        }
        catcher = e;
        catchX = e->pos.x;
        catchSep = 99;
        for (Ent* o : ents)
          if (o->side != e->side && o->state != EntState::Down && dist(o->pos, e->pos) < catchSep) {
            catchSep = dist(o->pos, e->pos);
            catchNear = o->man == e ? 0 : o->man ? 1 : !o->zone.empty() ? 2 : 3;
          }
        // a ball well off target, or a low one dying at the turf, with a defender right there, is caught laid out and
        // touched down: the play ends at the spot (outside the end zone; there the normal scoring rules apply)
        bool defenderClose = false;
        for (Ent* o : ents)
          if (o->side != e->side && o->free() && dist(o->pos, e->pos) < 2.0) defenderClose = true;
        // (a catch rolled before the ball got here was rolled a dive or not then, and the ball flown to it: homeBall)
        // (and only a dive whose hands get to it - inDive: ahead of him the way he faces, a body's length out, low)
        const bool diving = tune().homeA > 0 ? (layoutCatch || (fate == 1 && b.fateDive && inDive(e, b.pos))) && e->pos.x < 99.0
                                             : (d > 1.05 || (descendingLow && b.pos.z < 0.30)) && defenderClose && e->pos.x < 99.0;
        // a diving catch is down where he lands, not where the ball met his hands: the layout carries him on 2.8 yd
        // (the capture the game plays; it was shown 2.8 yd past the scored spot on every one, p60-p65) - toward the
        // ball, the way the capture goes (along his run it went sideways while the capture went the way he faced)
        const V2 toBallD{b.pos.x - e->pos.x, b.pos.y - e->pos.y};
        const V2 diveDir = tune().homeA > 0 && len(toBallD) > 0.3 ? scale(toBallD, 1.0 / len(toBallD))
                           : len(e->vel) > 0.5 ? scale(e->vel, 1.0 / len(e->vel)) : V2{1, 0};
        gContact.add(diving ? 1 : 0, e, b.pos, fate == 1);
        if (std::getenv("TAG_CATCH_DBG") && d > 0.8 && !diving) std::printf("FAR d %.2f z %.2f fate %d t %.2f flightT %.2f low %d hasLast %d ghost %d hit %d gd %.2f sp %.1f bsp %.1f left %.2f\n", d, b.pos.z, fate, b.t, b.flightT, descendingLow ? 1 : 0, hasLast ? 1 : 0, b.ghost ? 1 : 0, b.ghostHit ? 1 : 0, jsHypot(e->pos.x - b.ghostPos.x, e->pos.y - b.ghostPos.y), e->speed(), jsHypot(b.vel.x, b.vel.y), b.fateLeft);
        ev(diving ? "divecatch" : "catch", e);
        e->anim = Anim::Catch;
        e->animT = 0;
        // momentum loss when catching
        impulse(e, scale(e->vel, (std::min)(0.9, (contested ? 0.3 : tune().catchKeep) + e->fx.catchKeep) - 1), 0.35);
        setCarrier(e);
        if (diving) {
          e->pos = add(e->pos, scale(diveDir, 2.8));
          e->pos.x = std::min(e->pos.x, 99.4);  // (short of the goal line: a dive that scores is not this)
          e->pos.y = clamp(e->pos.y, 0.3, FIELD_W - 0.3);
          e->vel = V2{0, 0};
          e->state = EntState::Down;
          endPlay("tackle");
        }
        return;
      }
      gContact.add(2, e, b.pos, fate == 2);
      ev(contested ? "contested_inc" : "drop", e);
      // (the box score: a contested ball he was at the catch point for is a pass he defended - Tune::pdCredit)
      if (contested && contestBy && tune().pdCredit > 0 && contestEdge > -tune().pdCredit && !pbuBy) pbuBy = contestBy;
      {
        const double vy = b.vel.y * 0.2 + rng.range(-1, 1);
        b.vel = V3{b.vel.x * 0.2, vy, 1.2};
      }
      b.target = nullptr;
      startTumble(0.8);
      return;
    } else {
      // defender: interception or breakup
      Ent* tgt = b.target;
      const double rd = tgt ? dist(tgt->pos, bp) : 9;
      const double edge = clamp(rd - d, -2, 1.5);  // defender beat the receiver to the ball
      // (an outcome rolled before the ball got here for this defender: he plays it as rolled; a breakup / interception
      // rolled for another man leaves it to him. A catch or a drop rolled for the receiver does not stop a man who gets
      // to the ball first from trying for it, as he always could.)
      const bool fated = b.fate >= 3 && b.fateBy == e && tgt && tgt->state != EntState::Down;
      if (b.fate >= 3 && b.fateBy != e && b.fateBy && b.fateBy->free()) continue;
      if (b.fatePassed == e) continue;  // (his go at it was rolled early)
      if (!fated && lanePassed_.count(e)) continue;  // (and so was his: preRollLane)
      // (eyes on the ball: a man who cannot see it gets only a blind hand to it - swipeK x the odds - and never the pick)
      const bool sees = eyesOnBall(e);
      const double pPlay = sigmoid((e->r().ballSkills - 70) / 10 + edge * 1.0 - 1.1 - d * 0.8 - (b.pos.z > 2.5 ? 0.8 : 0) + e->fx.pbu - (tgt ? tgt->fx.winBall : 0.0)) *
                           (sees ? 1.0 : tune().swipeK);
      if (!fated && !rng.chance(pPlay)) continue;
      const double pInt = sigmoid((e->r().catching - 62) / 10 + edge * 0.6 - 1.2 - (b.pos.x - los > 18 ? 1.0 : 0) + tune().intBias + e->fx.intAdj - (b.thrower ? b.thrower->fx.ballSafe : 0.0));
      // (not rolled ahead and so not brought to him: from past handReach he gets a hand to it, no more)
      // (and one rolled ahead too: an interception is his hands on it - inHands - or it is a tip)
      const bool handsOn = sees && !(tune().homeA > 0 && tune().handReach > 0 && !inHands(e, b.pos, 1.0));
      pbuBlind = !sees;
      if (fated ? b.fate == 4 && handsOn : handsOn && rng.chance(pInt)) {
        gFate.intDef++;
        gFate.intDefF += fated;
        gContact.add(6, e, b.pos, fated);
        intBlind = !canSeeBall(e);
        intFaceDeg = ballFacingDeg(e);
        intercept(e);
      } else {
        pbuBy = e;
        gContact.add(5, e, b.pos, fated);
        ev("pbu", e, tgt);
        const double vx = b.vel.x * 0.2 + rng.range(-2, 2);
        const double vy = b.vel.y * 0.2 + rng.range(-2, 2);
        b.vel = V3{vx, vy, 2};
        b.target = nullptr;
        startTumble(1.0);
        if (airYds > 15) deepContested = true;
      }
      return;
    }
  }
}

void PlaySim::intercept(Ent* e) {
  intBy = e;
  ev("int", e);
  e->anim = Anim::Catch;
  e->animT = 0;
  // switch everybody: defense now carries
  for (Ent* o : off)
    if (o->state != EntState::Down) {
      o->state = EntState::Pursue;
      if (o->engaged) disengage(o);
      o->engagedBy.clear();
    }
  for (Ent* d : def) {
    if (d->engaged) disengage(d);
    d->engagedBy.clear();
    d->engaged = nullptr;
  }
  setCarrier(e);
  for (Ent* d : def)
    if (d != e && d->state != EntState::Down) d->state = EntState::Convoy;
}

// ------------------------------------------------------------------ tackling
void PlaySim::checkTackles() {
  if (wrap_) return;  // a pile on the move: it ends itself (stepWrap)
  Ent* c = ball.holder;
  if (!c || ball.mode != BallMode::Held) return;
  // QB in pocket is a sack target; others are carriers
  const std::vector<Ent*>& foes = defendersOf(c);
  int gang = 0;
  for (Ent* d : foes) {
    if (d->state == EntState::Down || d->stun > 0) continue;
    const double dd = dist(d->pos, c->pos);
    const bool qbPocket = c == qb && !passThrown && !scramble && cfg.offPlay->type != PlayType::Run;
    const double range = c->radius + d->radius + (d->userControlled && inputOf(d).dive ? 1.0 : qbPocket ? 0.3 : tune().tackleReach) +
                         (d->moveOn(Move::Hit) ? 0.6 : d->moveOn(Move::Cut) ? 0.4 : 0);
    if (d->engaged) {
      // engaged defenders can still reach out for a nearby carrier
      // a hazard per second (a blocked man gets an arm free now and then), not a per-tick roll
      // against a runner the shed-and-tackle stays a per-tick roll (calibrated for run defence)
      const double armP = (qbPocket ? (0.5 + d->r().blockShed * 0.01) * SIM_DT * d->fx.sackArm : (0.06 + d->r().blockShed * 0.0012) * tune().engArmK) *
                          (d->engagedBy.size() >= 2 ? tune().dblArm : 1.0);  // (a double team takes up both his arms)
      // (his free side only: the runner going by on his blocker's side is behind the block - Tune::armSide)
      bool freeSide = true;
      if (tune().armSide > 0 && !qbPocket && dd > 1e-6) {
        const V2 toB = norm(sub(d->engaged->pos, d->pos));
        freeSide = dot(scale(sub(c->pos, d->pos), 1 / dd), toB) < tune().armShut;
      }
      if (dd < range + 0.1 && d->tackleCd <= 0 && freeSide && rng.chance(armP)) {
        if (tryTackle(d, c, gang, true)) return;
      }
      continue;
    }
    if (d->tackleCd > 0) continue;
    if (dd > range) {
      // diving / shoestring tackle from just out of reach when the carrier isn't pulling away
      if (dd < range + 1.1) {
        const V2 away = norm(sub(c->pos, d->pos));
        const double sep = dot(sub(c->vel, d->vel), away);
        // only a last resort: carrier is pulling away / as fast as me, or I'm trailing
        const bool behind = dot(norm(sub(d->pos, c->pos)), c->speed() > 0.5 ? norm(c->vel) : V2{goalDir(c), 0}) < -0.2;
        const bool cantClose = sep > -0.4 && (behind || c->vmax >= d->vmax * 0.98);
        if (cantClose && sep < 1.5 && rng.chance(0.05) && clearPath(d, c)) {
          if (tryTackle(d, c, gang + 1, false, true)) return;
        }
      }
      continue;
    }
    gang++;
    if (tryTackle(d, c, gang, false)) return;
  }
}

/** No other body between tackler and carrier (can't dive through a lineman). */
bool PlaySim::clearPath(const Ent* d, const Ent* c) const {
  const V2 ab = sub(c->pos, d->pos);
  const double L2 = dot(ab, ab);
  for (Ent* o : ents) {
    if (o == d || o == c || o->state == EntState::Down) continue;
    const double tt = clamp(dot(sub(o->pos, d->pos), ab) / L2, 0, 1);
    if (tt <= 0.05 || tt >= 0.95) continue;
    if (dist(o->pos, add(d->pos, scale(ab, tt))) < o->radius + 0.15) return false;
  }
  return true;
}

bool PlaySim::tryTackle(Ent* d, Ent* c, int gang, bool arm, bool dive) {
  if (runContactX_ < -98 && c == carrier && c->side == Side::Off && !passThrown && !cfg.kick && cfg.offPlay->type == PlayType::Run) runContactX_ = c->pos.x - los;
  const double gd = goalDir(c);
  const V2 rel = norm(sub(d->pos, c->pos));
  const V2 cdir = c->speed() > 0.5 ? norm(c->vel) : V2{gd, 0};
  const double facing = dot(rel, cdir);  // +1 = defender in front
  const double mc = c->mass;
  const double md = d->mass;
  const double vc = c->speed();
  const double vd = d->speed();
  const double carrierSkill = facing > 0.3 ? c->r().breakTackle * 0.6 + c->r().strength * 0.4 : c->r().elusiveness * 0.7 + c->r().agility * 0.3;
  const bool dropback = cfg.offPlay->type != PlayType::Run;
  const bool isSack = dropback && c == qb && c->pos.x < los && !passThrown && !scramble;
  // calibrated to a ~12% missed-tackle rate per attempt (PFF league average)
  double x = tune().tackleBase + (d->r().tackling - 70) / 13 - (carrierSkill - 70) / 15;
  x += facing > 0.3 ? 0.35 : facing < -0.3 ? -0.55 : 0;
  x += -std::log(mc / md) * 1.4;
  x += -(vc / (std::max)(1.0, c->vmax)) * 0.35 + (vd / (std::max)(1.0, d->vmax)) * 0.2;
  x += (gang - 1) * 0.9;
  if (arm) x -= isSack ? 1.1 : tune().armPen;  // (a sack's arm is the pass rush's own, fitted with it)
  if (dive) x -= 1.3;
  if (isSack) x += 0.6;
  if (c->moveOn(Move::Juke)) x -= 0.6 + (std::max)(0.0, c->r().elusiveness - 70) * 0.02;
  if (c->userControlled && inputOf(c).juke) x -= 0.4;
  // a strip attempt (R1): he punches at the ball, not wraps the man - the carrier goes down less often (about 70 % of
  // the time against 88), the ball comes out far more often (finishTackle). Covered up, the carrier just goes down.
  const bool strip = d->stripT > 0;
  if (strip) x -= 0.9;
  if (c->protecting) x += 0.15;
  x += moveTackleAdj(d, c, facing, arm, dive);
  // abilities: the tackler's (Sure Tackler, Enforcer, Finisher on a sack), the carrier's (Battering Ram / Juggernaut
  // against a man in front, Ankle Taker / Phantom against one from the side, more off a live move, Escape Hatch)
  x += d->fx.tackleAdj + (facing > 0.3 ? c->fx.powerAdj : c->fx.elusAdj);
  if (c->move != Move::None && c->moveOn(c->move)) x += c->fx.moveAdj;
  if (isSack) x += d->fx.sackFinish + c->fx.qbEscape;
  if (abilOn_) x += tune().abilTackle;
  const double p = sigmoid(x);
  if (tackleLog) tackleLog->push_back(TackleLogEntry{p, facing, dive, arm, gang, c->pos.x - los, vc, c->move != Move::None && c->moveOn(c->move)});
  d->tackleCd = 0.55;
  d->anim = Anim::Tackle;
  d->animT = 0;
  if (rng.chance(p)) {
    // tackle! forward progress from momentum
    const V2 pc = scale(c->vel, mc);
    const V2 pd = scale(d->vel, md);
    const V2 vComb = scale(add(pc, pd), 1 / (mc + md));
    double fall = vComb.x * gd * 0.38 + (c->r().strength - d->r().strength) * 0.006 + rng.normal(tune().fallMu, 0.35);
    fall = clamp(fall, -1.5, 2.6);
    if (isSack) fall = (std::min)(fall, 0.3);
    // yards after contact: ball carriers drive the pile forward unless gang-tackled
    double drive = 0;
    if (!isSack && vComb.x * gd > -1) {
      double help = 0;
      for (Ent* o : defendersOf(c))
        if (o != d && o->free() && dist(o->pos, c->pos) < 2.2) help++;
      const double mu = tune().driveBase + (c->r().breakTackle * 0.5 + c->r().strength * 0.5 - d->r().tackling) * 0.025 + (mc - md) * 0.012 +
                        c->speed() * 0.07 - help * 0.55 - (facing > 0.3 ? 0.25 : 0) - (d->moveOn(Move::Cut) ? 0.8 : 0) +
                        (c->moveOn(Move::Truck) && facing > 0.2 ? 0.5 : 0) +  // cut at the knees / the shoulder still driving
                        c->fx.driveAdd - d->fx.stopDrive;                     // (abilities: the pile's way)
      drive = clamp(rng.normal(mu, 0.75), 0, 4);
    }
    // (a strip: the ball is the target, decided at the hit - no pile to drive)
    if (strip) drive = 0;
    // a big hit stops him where he stands: no pile to drive, he goes back the way the hit came
    if (d->moveOn(Move::Hit) && facing > -0.3 && !isSack) {
      drive = 0;
      fall = (std::min)(fall, 0.2) - 0.4;
      ev("bighit", d, c);
    }
    // QB sneak: the whole line surges, QB lunges over the pile
    if (cfg.offPlay->run && cfg.offPlay->run->style == RunStyle::Sneak && c == qb)
      fall += 0.9 + rng.range(0, 0.8) + (c->r().strength - 60) * 0.01;
    if (drive > 0.4 && !cfg.kick) {
      // the pile moves: the carrier drives on with the tackler wrapped on him (stepWrap) and goes down at its end
      Wrap w;
      w.c = c;
      w.d = d;
      w.left = drive;
      w.total = drive;
      w.natFall = fall;
      w.latY = vComb.y * 0.25;
      w.dive = dive;
      w.arm = arm;
      w.vd = vd;
      w.md = md;
      w.off = sub(d->pos, c->pos);
      if (len(w.off) > 1.0) w.off = scale(norm(w.off), 1.0);
      wrap_ = w;
      ev("wrap", d, c);
      return true;
    }
    finishTackle(d, c, fall + drive, vComb.y * 0.25, dive, arm, vd, md, isSack, nullptr);
    return true;
  }
  // missed / broken tackle
  brokenTackles++;
  d->stripT = 0;  // (the punch missed with the man)
  // the move that beat him (the presentation plays its clip instead of a stumble)
  Move won = Move::None;
  for (Move m : {Move::Truck, Move::StiffArm, Move::Spin, Move::Hurdle, Move::Juke})
    if (c->moveOn(m)) {
      won = m;
      break;
    }
  ev(dive ? "divemiss" : "broken", c, d, moveName(won));
  d->stun = (dive ? 1.0 : 0.6) + rng.range(0, 0.6);
  if (dive) d->anim = Anim::Dive;
  d->state = d->state == EntState::Rush ? EntState::Pursue : d->state;
  double keep = dive ? 0.82 : 0.68;
  if (won == Move::Truck && facing > 0.2) {
    // run over: the tackler goes down on his back, the runner barely breaks stride
    keep = 0.85;
    d->pancaked = true;
    d->stun = 1.8 + rng.range(0, 0.5);
    d->anim = Anim::Down;
    d->animT = 0;
    d->vel = scale(cdir, 1.6);
    ev("trucked", c, d);
  } else if (won == Move::StiffArm) {
    keep = 0.85;  // the hand in his chest: shoved off, the runner keeps his line
    d->stun += 0.4;
    impulse(d, scale(norm(sub(d->pos, c->pos)), 2.6), 0.25);
  } else if (won == Move::Spin) {
    keep = 0.82;  // spun out of it: he comes out of the turn still running
  } else if (won == Move::Hurdle) {
    keep = 0.85;
  }
  if (d->moveOn(Move::Hit) || d->moveOn(Move::Cut)) {
    d->stun += 0.8;  // the whiffed lunge: on the turf
    d->anim = Anim::Dive;
  }
  impulse(c, scale(c->vel, keep - 1), 0.3);
  if (won == Move::None) c->animT = 0;  // (a move's own clip plays on)
  return false;
}

bool PlaySim::wrapEnd(V2& outSpot, double& outSecs) const {
  if (!wrap_ || !wrap_->c) return false;
  const Wrap& w = *wrap_;
  const double gd = goalDir(w.c);
  outSpot = V2{w.c->pos.x + (w.left + w.natFall) * gd, w.c->pos.y + w.latY};
  outSecs = w.left / (2.2 + (w.c->r().strength - 60) * 0.015);  // stepWrap's pile speed
  return true;
}

void PlaySim::stepWrap(double dt) {
  Wrap& w = *wrap_;
  Ent* c = w.c;
  Ent* d = w.d;
  const double gd = goalDir(c);
  // help arriving stops the pile (the next man in finishes it, an assist)
  Ent* help = nullptr;
  for (Ent* o : defendersOf(c))
    if (o != d && o->free() && o->state != EntState::Down && dist(o->pos, c->pos) < c->radius + o->radius + 0.35) {
      help = o;
      break;
    }
  const double v = 2.2 + (c->r().strength - 60) * 0.015;  // a pile moves at a hard walk
  const double step = (std::min)(w.left, v * dt);
  const bool edge = c->pos.y < 0.15 || c->pos.y > FIELD_W - 0.15;  // driven to the sideline: the drive ends there
  if (!edge && !help) {
    c->pos = V2{c->pos.x + step * gd, c->pos.y};
    w.left -= step;
  }
  c->vel = V2{(edge || help ? 0.0 : step / dt) * gd, 0};
  d->pos = add(c->pos, w.off);
  d->vel = c->vel;
  c->anim = Anim::Run;
  d->anim = Anim::Tackle;
  // a tackle is not over at the hit. Halfway through the drive, one man on him and nobody else there yet, the carrier
  // has his chance to tear out of it: his strength and balance against the tackler's grip, and the stick - a user
  // working a move through the wrap doubles it, a user doing nothing all but gives it up. And the next man in can
  // take the ball instead of the body.
  if (!w.rolled && !help && !edge && w.left < 0.5 * w.total && tune().wrapBreak > 0) {
    w.rolled = true;
    double p = tune().wrapBreak + (c->r().breakTackle - 70) * 0.004 + (c->r().strength - d->r().tackling) * 0.002 + (w.arm ? 0.05 : 0) + c->fx.wrapBreak;
    if (c->userControlled) p *= (inputOf(c).juke || inputOf(c).stickMove != Move::None || c->move != Move::None) ? 2.2 : 0.5;
    if (rng.chance(clamp(p, 0.005, 0.3))) {
      wrap_.reset();
      d->stun = (std::max)(d->stun, 0.9);
      d->tackleCd = 1.2;
      d->anim = Anim::Idle;
      brokenTackles++;
      ev("broken", c, d, "wrap");
      return;
    }
  }
  if (help && !w.rolled && rng.chance(0.03 + (help->r().tackling - 70) * 0.0006)) {
    // the second man punches at the ball while the first holds him up
    const Wrap done = w;
    wrap_.reset();
    done.d->state = EntState::Down;  // (he goes to the ground with the carrier, as the capture has him)
    done.d->anim = Anim::Tackle;
    fumble(c, help, "strip");
    return;
  }
  if (w.left <= 1e-6 || help || edge) {
    const Wrap done = w;
    wrap_.reset();
    finishTackle(done.d, done.c, done.natFall, done.latY, done.dive, done.arm, done.vd, done.md, false, help);
  }
}

void PlaySim::finishTackle(Ent* d, Ent* c, double fall, double latY, bool dive, bool arm, double vd, double md, bool isSack, Ent* help) {
  const double gd = goalDir(c);
  c->pos = V2{c->pos.x + fall * gd, c->pos.y + latY};
  c->state = EntState::Down;
  c->anim = Anim::Down;
  c->animT = 0;
  d->state = EntState::Down;
  d->anim = Anim::Tackle;
  tacklers.push_back(d);
  ev("tackle", d, c, dive ? "dive" : arm ? "arm" : "");  // presentation: the paired fall
  foulMask.add(0.0012 + 0.0024 * (vd / (std::max)(1.0, d->vmax)) * (arm || dive ? 1.5 : 1), d->p);
  // gang tackle: the man who stopped the pile, then pursuers arriving with the tackle (about a third of NFL tackles
  // are shared). Anyone free within ~2 yd and closing, nearest first, up to two assists.
  {
    std::vector<std::pair<double, Ent*>> near;
    if (help) near.push_back(std::make_pair(-1.0, help));
    for (Ent* o : defendersOf(c)) {
      if (o == d || o == help || !o->free() || o->state == EntState::Down) continue;
      const double od = dist(o->pos, c->pos);
      const double closing = dot(sub(o->vel, c->vel), norm(sub(c->pos, o->pos)));
      if (od < 1.4 || (od < tune().assistRange && closing > 1.0)) near.push_back(std::make_pair(od, o));
    }
    std::stable_sort(near.begin(), near.end(), [](const std::pair<double, Ent*>& a, const std::pair<double, Ent*>& b) { return a.first < b.first; });
    for (size_t k = 0; k < near.size() && k < 2; k++) {
      tacklers.push_back(near[k].second);
      ev("assist", near[k].second, c);
    }
  }
  if (isSack) sackBy = d;
  // fumble?
  const double hit = (md * vd) / 900;
  // the hit stick jars it loose; a spin or a hurdle leaves the ball out away from the body
  const double moveF = (d->moveOn(Move::Hit) ? 2.4 : 1) * (c->moveOn(Move::Spin) ? 1.5 : 1) * (c->moveOn(Move::Hurdle) && !dive ? 1.8 : 1)
                     * (d->stripT > 0 ? 4.5 : 1)        // a strip attempt that lands: the punch at the ball (R1)
                     * (c->protecting ? 0.25 : 1);      // covered up (R1): both arms on it
  const bool stripTry = d->stripT > 0;
  d->stripT = 0;
  const double pf = 0.0118 * std::exp((70 - c->r().carrying) / 16) * (0.6 + hit) * (isSack ? 3.5 : 1) *
                    (cfg.weather.precip != Precip::None ? 1.3 : 1) * (cfg.kick ? 0.12 : 1) * moveF *  // returners: ~1% (they are picked for it)
                    c->fx.fumble * d->fx.hitFumble * tune().fumbleK;  // (abilities: Vise Grip; Ball Punch, Enforcer)
  if (rng.chance(pf) && !(isSack && rng.chance(0.3))) {
    fumble(c, d, isSack ? "sack" : stripTry ? "strip" : "hit");
    return;
  }
  endPlay(isSack ? "sack" : "tackle");
}

void PlaySim::fumble(Ent* c, Ent* byEnt, const char* how) {
  fumbler = c;
  if (!firstFumbler) firstFumbler = c;
  forcedBy = byEnt;
  fumbleHow = how ? how : "hit";
  ev("fumble", c, byEnt, fumbleHow);
  c->hasBall = false;
  c->state = EntState::Down;
  Ball& b = ball;
  b.mode = BallMode::Loose;
  b.holder = nullptr;
  b.t = 0;
  // the ball out of his arm: what it had with him (his momentum) plus the blow that took it. A punch from the second
  // man knocks it out low along the punch; a hit jars it out ahead of the collision; a sack's swipe at the arm sends it
  // forward or back off the passer. Then it is a football on the turf (BallBody): where it goes after the first bounce
  // is the bounce's, not a roll of the dice.
  {
    const bool strip = fumbleHow == "strip", sack = fumbleHow == "sack";
    V2 blow = byEnt ? norm(sub(c->pos, byEnt->pos)) : fromAngle(c->face);
    if (len(blow) < 0.5) blow = fromAngle(c->face);
    const double turn = rng.range(-1.0, 1.0) * (strip ? 0.7 : 1.1);
    const V2 dir{blow.x * std::cos(turn) - blow.y * std::sin(turn), blow.x * std::sin(turn) + blow.y * std::cos(turn)};
    const double pop = strip ? rng.range(1.5, 3.5) : sack ? rng.range(2.0, 5.0) : rng.range(1.5, 4.5);
    const double keep = sack ? 0.4 : 0.6;
    b.vel = V3{c->vel.x * keep + dir.x * pop, c->vel.y * keep + dir.y * pop, strip ? rng.range(0.2, 1.4) : rng.range(0.8, 2.8)};
    b.pos = V3{c->pos.x + std::cos(c->face) * 0.2, c->pos.y + std::sin(c->face) * 0.2, sack ? 1.25 : 1.05};
    BallBody& bb = b.body;
    bb = BallBody{};
    bb.pos = b.pos;
    bb.vel = b.vel;
    bb.q = qalong(V3{std::cos(c->face), std::sin(c->face), rng.range(-0.4, 0.6)}, rng.range(-3.14159, 3.14159));
    bb.w = V3{rng.range(-14, 14), rng.range(-14, 14), rng.range(-8, 8)};
    b.phys = true;
    b.tumble = true;
  }
  looseTry_.clear();
  carrier = nullptr;
  // engaged pairs break up
  for (Ent* e : ents) {
    if (e->engaged) disengage(e);
    e->engagedBy.clear();
    if (e->state != EntState::Down) e->state = EntState::Pursue;
  }
}

void PlaySim::checkBounds() {
  Ent* c = ball.holder;
  if (!c || ball.mode != BallMode::Held) return;
  if (c->pos.y < 0 || c->pos.y > FIELD_W) {
    endPlay("oob");
    return;
  }
  const double gd = goalDir(c);
  if (gd > 0 && c->pos.x >= 100) endPlay("td");
  else if (gd < 0 && c->pos.x <= 0) endPlay("td");
  else if (c->pos.x > 110 || c->pos.x < -10) endPlay("oob");
}

// ------------------------------------------------------------------ end of play
void PlaySim::endPlay(const std::string& reason) {
  if (phase == SimPhase::Dead) return;
  wrap_.reset();
  phase = SimPhase::Dead;
  endReason = reason;
  const Ball& b = ball;
  Ent* holder = b.holder;
  const OffPlay& play = *cfg.offPlay;
  const double L = los;
  double endX = holder ? holder->pos.x : L;
  bool turnover = false;
  std::optional<Side> td;
  bool safety = false;
  bool touchback = false;
  const bool incomplete = reason == "incomplete" || reason == "spike";
  if (incomplete) endX = L;
  if (holder && holder->side == Side::Def) turnover = true;
  if (reason == "td") td = holder->side;
  // forward progress: a carrier whose fall carries the ball over the goal line has scored (the ball broke the plane)
  if (!td && !incomplete && holder && b.mode == BallMode::Held && reason != "oob") {
    const double gd = goalDir(holder);
    if ((gd > 0 && endX >= 100) || (gd < 0 && endX <= 0)) td = holder->side;
  }
  // a defensive player downed in his own end zone after a takeaway = touchback
  if (turnover && !td && endX >= 100) touchback = true;
  if (!turnover && holder && !td && endX <= 0 && reason != "incomplete") safety = true;
  // a kick fielded and downed in the receiving team's own end zone is a touchback, not a safety
  if (cfg.kick && safety && holder->side == Side::Off) {
    safety = false;
    touchback = true;
  }
  // loose ball out of bounds: offense keeps at spot
  if (reason == "oob" && b.mode == BallMode::Loose) endX = clamp(b.pos.x, 0.5, 99.5);
  // a fumble out of bounds: the team that fumbled keeps it - at the spot of the fumble when it went out ahead of it, where
  // it went out when behind it; out behind the fumbling offense's own goal line it is a safety, out through the other
  // end zone a touchback to the other team. (The offense kept it where it went out, wherever that was: a fumble rolled
  // forward out of bounds gained its yards, one out of the end zone in front was still the offense's at the 1, one out
  // of its own end zone was no safety, and a defender's fumble out of bounds gave the ball back to the offense.)
  if (reason == "oob" && b.mode == BallMode::Loose && fumbler && !cfg.kick && !cfg.place) {
    double fumX = b.pos.x;
    for (const PlayEvent& e : events)
      if (e.type == "fumble") fumX = e.x;  // (the last one: where the ball came out)
    const bool offBall = fumbler->side == Side::Off;
    // in the fumbling team's frame: 0 its own goal line, 100 the one it attacks
    const double outU = offBall ? b.pos.x : 100 - b.pos.x, fumU = offBall ? fumX : 100 - fumX;
    if (outU >= 100) {
      turnover = offBall;  // (a defender's fumble out through the offense's end zone: the offense's ball, as before)
      touchback = offBall;
      endX = offBall ? 100 : 0.5;
    } else if (outU <= 0) {
      // (a defender's fumble out of his own end zone would be a safety for the offense; it stays the defense's, at its 20)
      safety = offBall;
      turnover = !offBall;
      touchback = !offBall;
      endX = offBall ? 0 : 100;
    } else if (offBall && outU > fumU && fumU <= 0) {
      safety = true;  // (fumbled in his own end zone and out ahead of it: back to the spot of the fumble, in the end zone)
      endX = 0;
    } else {
      const double spotU = outU > fumU ? clamp(fumU, 0.5, 99.5) : clamp(outU, 0.5, 99.5);
      endX = offBall ? spotU : 100 - spotU;
      turnover = !offBall;
    }
  }
  endX = clamp(endX, -10, 110);
  // (a run the quarterback pulled and threw - an RPO - is a pass)
  PlayKind kind = play.type == PlayType::Run && !passThrown ? PlayKind::Run : PlayKind::Pass;
  if (play.type == PlayType::Kneel) kind = PlayKind::Kneel;
  if (play.type == PlayType::Spike) kind = PlayKind::Spike;
  if (reason == "sack" || (sackBy && !passThrown)) kind = PlayKind::Sack;
  if (scramble && !passThrown) kind = PlayKind::Scramble;
  if (kind == PlayKind::Kneel) endX = L - 1;
  Player* passerP = ((play.type != PlayType::Run || passThrown) && kind != PlayKind::Kneel && kind != PlayKind::Spike) ? qb->p : nullptr;
  Ent* tgt = ball.target;
  if (!tgt) {
    for (const PlayEvent& e : events)
      if (e.type == "throw") {
        tgt = e.b;
        break;
      }
  }
  const bool fumbleLost = fumbler && turnover && !intBy;
  PlayResult r;
  r.kind = kind;
  r.endX = endX;
  r.yards = jsRoundI(endX - L);
  r.complete = catcher && !intBy;
  r.incomplete = incomplete;
  r.interception = intBy != nullptr;
  r.fumbleLost = fumbleLost;
  r.turnover = turnover;
  r.td = td;
  r.safety = safety;
  r.touchback = touchback;
  r.outOfBounds = reason == "oob";
  r.duration = t;
  r.passer = (passThrown || kind == PlayKind::Sack) ? passerP : nullptr;
  if (kind == PlayKind::Run || kind == PlayKind::Scramble) {
    // (the man who ran it: after a fumble the recoverer holds the ball, and was credited with the run - "J.Hargrove hb
    // draw for a loss of 3 ... recovered by MEM J.Hargrove", the defender who fell on it)
    Ent* re = firstFumbler && firstFumbler->side == Side::Off ? firstFumbler : carrier ? carrier : fumbler;
    r.rusher = re ? re->p : nullptr;
  }
  // the pass's forecast against what happened (a ball dead in the air with nobody's play on it - out past a line - is
  // "nobody gets to it")
  if (ball.fcFate != 0 && ball.fcOut == 0) {
    ball.fcOut = 5;
    ball.fcOutT = t;
    ball.fcOutAt = ball.pos;
    if (ball.fcFate != 5) {
      ball.fcFate = 5;
      ball.fcBy = nullptr;
      ball.fcRev++;
      ball.fcWhy = "at the ball: dead";
    }
  }
  r.fcFate0 = ball.fcFate0;
  r.fcOut = ball.fcOut;
  r.fcRev = ball.fcRev;
  r.fcWhy = ball.fcWhy;
  if (ball.fcFate0 != 0) {
    r.fcErrT = ball.fcOutT - ball.fcT0;
    r.fcErrD = jsHypot(ball.fcOutAt.x - ball.fcAt0.x, ball.fcOutAt.y - ball.fcAt0.y);
    r.fcErrZ = ball.fcOutAt.z - ball.fcZ0;
    r.fcByOk = ball.fcOut == 5 || ball.fcOutBy == ball.fcBy0;
  }
  r.receiver = catcher ? catcher->p : nullptr;
  r.target = tgt ? tgt->p : nullptr;
  if (catcher && !intBy && tgt && tgt != catcher) {
    if (ball.target == tgt) {
      // (untouched on its way: it went to the spot he was on - he is the man it was thrown to)
      r.target = catcher->p;
    } else {
      // (it went off someone first: the last touch before the catch - the man it was thrown to, or a defender's tip)
      for (const PlayEvent& ev0 : events)
        if ((ev0.type == "contested_inc" || ev0.type == "drop" || ev0.type == "batted" || ev0.type == "pbu" || ev0.type == "tip") && ev0.a && ev0.a->p)
          r.tippedBy = ev0.a->p;
    }
  }
  for (Ent* tk : tacklers) r.tacklers.push_back(tk->p);
  r.sackedBy = sackBy ? sackBy->p : nullptr;
  r.intBy = intBy ? intBy->p : nullptr;
  r.fumbleBy = fumbler ? fumbler->p : nullptr;
  r.fumbleRecBy = recoveredBy ? recoveredBy->p : nullptr;
  r.forcedBy = forcedBy ? forcedBy->p : nullptr;
  r.pbuBy = pbuBy ? pbuBy->p : nullptr;
  r.intFaceDeg = intBy ? intFaceDeg : -1;
  r.intBlind = intBy && intBlind;
  r.pbuBlind = pbuBy && pbuBlind && !intBy;
  r.engN = engN_;
  r.engPastHip = engPastHip_;
  r.engRefused = engRefused_;
  r.engLatSum = engLatSum_;
  r.engGeoSum = engGeoSum_;
  for (int k = 0; k < 3; k++) r.lbDepth[k] = lbDepth_[k], r.lbDepthN[k] = lbDepthN_[k];
  r.comboSet = static_cast<int>(combos_.size());
  r.comboDbl = comboDbl_;
  r.comboClimb = comboClimb_;
  r.comboHit = comboHit_;
  r.comboHitT = comboHitT_;
  r.comboHoldT = comboHoldT_;
  r.runDblT = runDblT_;
  if (play.type == PlayType::Run && !cfg.kick && !cfg.place)
    for (const Ent* d : def) r.lbN += isLB(d);
  r.lbBlocked = static_cast<int>(lbBlocked_.size());
  r.runDesigned = play.type == PlayType::Run && !cfg.kick && !cfg.place && runHandT_ >= 0;
  r.runAimDy = runAimDy();
  r.runCrossed = runCrossed_;
  r.runCrossDy = runCrossed_ ? runCrossDy_ : (holder ? holder->pos.y : runLastPos_.y) - by;
  r.runCrossT = runCrossT_;
  r.runHandT = runHandT_;
  r.runContactX = runContactX_;
  r.runDist = runDist_;
  r.runDist0 = runDist0_;
  r.matchCarries = matchCarries_;
  r.stunt = stunt_;
  r.passOffs = passOffs_;
  if (stunt_)
    for (const Ent* d : def)
      if (d->stunt && (d->wrBeat || d->wrFree || (pressured && d->p && pressWhy.size() > d->role.size() && pressWhy.compare(pressWhy.size() - d->role.size(), d->role.size(), d->role) == 0)))
        r.stuntWon = true;
  r.winPlay = winPlay_;
  if (winPlay_)
    for (const Ent* e : ents)
      if (e->wrIn && e->p) r.winReps.push_back(PlayResult::WinRep{e->p, e->side == Side::Def, e->wrCls, e->wrEng, e->wrBeat, e->wrFree});
  r.airYards = catcher ? jsRoundI(catchX - L) : ball.targetSpot ? jsRoundI(ball.targetSpot->x - L) : 0;
  r.yac = (catcher && !intBy) ? jsRoundI(endX - catchX) : 0;
  r.deepContested = deepContested;
  r.catchSep = catchSep;
  r.throwSep = throwSep;
  r.arriveSep = arriveSep;
  r.catchNear = catchNear;
  r.flightT = ball.flightT;
  r.olBlockDepth = olBlockDepth;
  r.dblTeamT = dblTeamT;
  r.dblFirstT = dblFirstT;
  for (int k = 0; k < 6; k++) r.manGap[k] = manGap[k], r.manGapN[k] = manGapN[k], r.manSpd[k] = manSpd[k], r.recSpd[k] = recSpd[k];
  r.throwPath = throwPath;
  bool ta = false;
  for (const PlayEvent& e : events)
    if (e.type == "throwaway") ta = true;
  r.thrownAway = ta;
  r.pressured = pressured;
  r.pressWhy = pressWhy;
  r.pressT = pressT;
  r.timeToThrow = throwStart > 0 ? throwStart : t;
  r.brokenTackles = brokenTackles;
  r.foulHold = foulHold;
  r.foulDpi = foulDpi;
  r.foulDhold = foulDhold;
  r.foulRough = foulRough;
  r.foulMask = foulMask;
  r.foulOpi = foulOpi;
  if (ball.targetSpot) r.targetSpotY = ball.targetSpot->y;
  r.endY = holder ? holder->pos.y : by;
  r.events = events;
  result = std::move(r);
  if (kind == PlayKind::Run && carrier == qb && !(play.run && play.run->carrier == "QB")) {
    // fumbled exchange handled as run by QB
    result->rusher = qb->p;
  }
  if (holder) holder->anim = td ? Anim::Celebrate : holder->anim;
}

}  // namespace tag
