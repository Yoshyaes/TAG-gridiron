// Port of src/sim/play.ts -- per-play physical simulation at 60 Hz.
// Control flow, constants and RNG call order follow the TS line by line.
#include "PlaySim.h"
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
  if (name == "abilities") return t.abilities = v, true;
  if (name == "abilTackle") return t.abilTackle = v, true;
  if (name == "abilAcc") return t.abilAcc = v, true;
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
  if (name == "releaseRate") return t.releaseRate = v, true;
  if (name == "intBias") return t.intBias = v, true;
  if (name == "runShed") return t.runShed = v, true;
  if (name == "hotRate") return t.hotRate = v, true;
  if (name == "outletPen") return t.outletPen = v, true;
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
  if (name == "wrapBreak") return t.wrapBreak = v, true;
  if (name == "secondLong") return t.secondLong = v, true;
  if (name == "softZone") return t.softZone = v, true;
  if (name == "behindHotK") return t.behindHotK = v, true;
  if (name == "runNoise") return t.runNoise = v, true;
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
  } else {
    setupOffense();
    setupDefense();
  }
  ents.reserve(off.size() + def.size());
  for (Ent* e : off) ents.push_back(e);
  for (Ent* e : def) ents.push_back(e);
  applyAbilities();
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
    // settle under it: arrive a beat early, a step behind the spot, moving forward into the catch
    const double left = k.hang - ball.t;
    const V2 spot{(std::max)(land.x - 0.6, -9.4), clamp(land.y, 1.0, FIELD_W - 1.0)};
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
    if (len(input.move) > 0.25) returnMoved = true;
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
  // emergency: anyone
  Player* any = nullptr;
  for (const auto& p : team.roster)
    if (!contains(used, p->id) && p->injuredWeeks <= 0) {
      any = p.get();
      break;
    }
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
      const double k = a.n == 2 ? 0.3 : 0.55;
      return V2{L - 1.2, by + sg * (std::max)(a.n == 2 ? 5.0 : 7.0, span * k)};
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
    if ((e->role == "TE" || e->role == "TE2") && std::fabs(e->pos.y - by) < 5) tes.push_back(e);
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
  const bool odd = cfg.defCall->front == Front::Base34;
  const double lbDepth = gl ? 2.5 : 4.6;
  if (odd) {
    // 3-4: ends in 4i/5 techniques on the tackles, the nose over the center; the outside backers stand on the
    // edges outside the end man, the two inside backers off the guards
    const double edgeL = (teL ? teL->pos.y : lt) + 1.6, edgeR = (teR ? teR->pos.y : rt) - 1.6;
    if (fa == FrontAlign::Bear) {
      set("LE", L + 1.05, by + 1.6);  // both guards and the center covered
      set("LDT", L + 1.00, by);
      set("RE", L + 1.05, by - 1.6);
    } else {
      set("LE", L + 1.10, lt + 0.35);
      set("LDT", L + 1.00, by + (fa == FrontAlign::Over ? strongS * 0.7 : 0));
      set("RE", L + 1.10, rt - 0.35);
    }
    set("LOLB", L + 1.15, edgeL);
    set("ROLB", L + 1.15, edgeR);
    set("WLB", L + lbDepth, by + 2.2);
    set("MLB", L + lbDepth + 0.2, by - 2.2);
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
  auto cbDepth = [&](const Ent* e, bool right) {
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
  const bool twoHigh = cov == Coverage::C2 || cov == Coverage::C4 || cov == Coverage::C2M || cov == Coverage::C2T || cov == Coverage::C6;
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
      if (isCarrier) e->state = EntState::Idle;
      else e->state = EntState::RunBlock;
      continue;
    }
    if (!rname || *rname == "block") {
      e->state = EntState::PassBlock;
      e->routeName = "block";
      continue;
    }
    assignRoute(e, *rname);
  }
  // ---- defense
  const DefCall& call = *cfg.defCall;
  const Coverage cov = call.coverage;
  std::vector<Ent*> rushers;  // ordered set
  for (Ent* e : def)
    if (e->isDL && !contains(rushers, e)) rushers.push_back(e);
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
  std::vector<Ent*> assigned;
  for (const auto& zr : deepZones) {
    Ent* e = defRole(zr.second);
    if (e && !contains(rushers, e)) {
      e->state = EntState::Zone;
      e->zone = zr.first;
      assigned.push_back(e);
    }
  }
  std::vector<Ent*> rest;
  for (Ent* e : cover)
    if (!contains(assigned, e)) rest.push_back(e);
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
}

/** Snap-time blocking assignments: unique blocker->defender pairs, closest first. */
void PlaySim::assignBlocks() {
  const OffPlay& play = *cfg.offPlay;
  const bool isRun = play.type == PlayType::Run;
  std::vector<Ent*> blockers;
  for (Ent* e : off)
    if (e->state == (isRun ? EntState::RunBlock : EntState::PassBlock) &&
        (e->isOL || e->role == "TE" || e->role == "TE2" || e->role == "FB" || e->role == "RB"))
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
  for (Ent* b : blockers) {
    const bool backfield = b->role == "RB" || b->role == "FB";
    for (Ent* d : targets) {
      // a protection slide: every lineman takes the man one gap over toward the slide
      const double sl = !isRun && b->isOL ? slide * 1.35 : 0;
      const V2 pt{b->pos.x, b->pos.y + (zone && !backfield ? playDir * 1.3 : 0) + sl};
      double c = std::fabs(d->pos.y - pt.y) + std::fabs(d->pos.x - pt.x) * 0.5;
      if (isRun && d->pos.x > los + 3) c += 1.5;  // linebackers are second-level priority
      if (backfield) c += isRun ? 1.2 : 3;         // backs pick up leftovers
      pairs.push_back(Pair{b, d, c});
    }
  }
  std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.c - b.c < 0; });
  std::vector<Ent*> usedB, usedD;
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
      const bool lineBlocker = pr.b->isOL || pr.b->role == "TE" || pr.b->role == "TE2";
      if (pass == 1 && !(onLine && lineBlocker)) continue;
      pr.b->blockTarget = pr.d;
      usedB.push_back(pr.b);
      usedD.push_back(pr.d);
    }
  }
  // a run's perimeter: each receiver has the man over him (his own cover man, else the nearest defender nobody has) -
  // the stalk block. They had no man at all and took whoever came near.
  if (isRun && tune().smartBlock > 0) {
    for (Ent* b : off) {
      if (b->state != EntState::RunBlock || b->blockTarget || contains(blockers, b)) continue;
      Ent* best = nullptr;
      double bd = 11;
      for (Ent* d : def) {
        if (contains(usedD, d) || d->pos.x < los) continue;
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
  for (Ent* b : off) b->snapTarget = b->blockTarget;
}

// ------------------------------------------------------------------ pre-snap adjustments
namespace {
/** A back's check release / a screen's slow release (the route's own delay before he goes). */
double routeDelay(const Ent* e) {
  if (e->role == "RB" && (e->routeName == "checkdown" || e->routeName == "flat" || e->routeName == "release" || e->routeName == "releaseflat")) return 0.9;
  if (e->routeName == "screen") return 1.1;
  return 0;
}
bool isLB(const Ent* e) { return e->role == "WLB" || e->role == "MLB" || e->role == "SLB" || e->role == "LOLB" || e->role == "ROLB"; }
bool isCorner(const Ent* e) { return e->role == "LCB" || e->role == "RCB" || e->role == "NB" || e->role == "DB"; }
}  // namespace

double PlaySim::runAimDy() const {
  const OffPlay& play = *cfg.offPlay;
  if (!play.run) return 0;
  return play.run->aimDy * (cfg.flip ? -1.0 : 1.0) * (runFlip ? -1.0 : 1.0);
}

bool PlaySim::flipRun() {
  if (!preSnap() || !cfg.offPlay->run) return false;
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
  const RouteDef& rd = *routeByName(rname);
  e->state = EntState::Route;
  e->settle = rd.settle;
  const V2 at = e->shiftTo ? *e->shiftTo : e->pos;  // a man still walking to his spot runs it from there
  double side = jsSign(at.y - by);
  if (std::fabs(at.y - by) < 0.6) side = e->role == "RB" ? (cfg.flip ? -1 : 1) : 1;
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
      const bool back = e->role == "RB" || e->role == "FB" || e->role == "TE" || e->role == "TE2";
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
  for (Ent* e : ents) {
    if (!e->shiftTo) continue;
    const V2 d = sub(*e->shiftTo, e->pos);
    const double l = len(d);
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
}

bool PlaySim::userPresnapMove(const V2& stick, double dt) {
  if (!preSnap() || !user || user->side != Side::Def) return false;
  const double m = (std::min)(1.0, len(stick));
  if (m < 0.2) {
    if (!user->shiftTo) user->vel = V2{0, 0};
    return false;
  }
  // a shuffle for a nudge, a jog (4.5 yd/s) for a full push
  const double sp = 1.2 + 3.3 * clamp((m - 0.2) / 0.8, 0.0, 1.0);
  user->shiftTo.reset();
  user->vel = scale(norm(stick), sp);
  V2 to = add(user->pos, scale(user->vel, dt));
  // his side of the neutral zone (a lineman's hand at the ball, everyone else a yard off it), on the field
  to.x = clamp(to.x, los + (user->isDL ? 0.55 : 1.0), los + 25.0);
  to.y = clamp(to.y, 1.0, FIELD_W - 1.0);
  user->pos = to;
  basePos_[user] = to;  // (a shift called after this is relative to where he is now)
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

void PlaySim::switchUser() {
  if (!user) return;
  const Side side = user->side;
  const V2 bp{ball.pos.x, ball.pos.y};
  const std::vector<Ent*>& team = side == Side::Off ? off : def;
  std::vector<Ent*> cands;
  for (Ent* e : team)
    if (e != user && e->state != EntState::Down) cands.push_back(e);
  if (cands.empty()) return;
  // before the snap the switch cycles the whole defense, nearest the ball first (Madden's CIRCLE): taking the nearest
  // other man each time only ever flipped between the same two
  if (preSnap() && side == Side::Def) cands.push_back(user);
  std::stable_sort(cands.begin(), cands.end(), [&bp](const Ent* a, const Ent* b) { return dist(a->pos, bp) - dist(b->pos, bp) < 0; });
  Ent* next = cands[0];
  if (preSnap() && side == Side::Def) {
    const auto it = std::find(cands.begin(), cands.end(), user);
    next = (it == cands.end() || it + 1 == cands.end()) ? cands[0] : *(it + 1);
  }
  user->userControlled = false;
  user = next;
  user->userControlled = true;
}

bool PlaySim::switchUserToward(const V2& dir) {
  if (!user || len(dir) < 0.1) return false;
  const V2 d = norm(dir);
  const std::vector<Ent*>& team = user->side == Side::Off ? off : def;
  Ent* best = nullptr;
  double bs = 1e9;
  for (Ent* e : team) {
    if (e == user || e->state == EntState::Down) continue;
    const V2 rel = sub(e->pos, user->pos);
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
  user->userControlled = false;
  user = best;
  user->userControlled = true;
  return true;
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
  if (adjusted_ && t == 0) {
    adjusted_ = false;
    finishPresnap();
  }
  t += dt;
  if (input.switchReq) {
    input.switchReq = false;
    switchUser();
  }
  updateSnap();
  for (Ent* e : ents) {
    if (e->stun > 0) e->stun -= dt;
    if (e->slipT > 0) e->slipT -= dt;
    if (e->tackleCd > 0) e->tackleCd -= dt;
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
  if (!input.rushMove.empty()) {
    if (user && user->side == Side::Def) rushMove(user, input.rushMove);
    input.rushMove.clear();
  }
  updateEngagements(dt);
  collide();
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
  }
  for (Ent* e : def)
    if (e->state == EntState::Man && e->man && e->man->state == EntState::Route && !passThrown && !cfg.kick) {
      const int k = static_cast<int>(t * 2 + 1e-6) - 1;
      if (k >= 0 && k < 6 && static_cast<int>((t - dt) * 2 + 1e-6) - 1 != k) {
        manGap[k] += dist(e->pos, e->man->pos), manGapN[k] += 1;
        manSpd[k] += e->speed(), recSpd[k] += e->man->speed();
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
    }
    return;
  }
  // handoff
  if (play.type == PlayType::Run && play.run && !handedOff && b.holder == qb && play.run->carrier != "QB") {
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
  if (play.type == PlayType::PlayAction && !handedOff && t > 0.85) {
    handedOff = true;  // fake complete
    Ent* rb = offRole("RB");
    if (rb) rb->state = EntState::PassBlock;
  }
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
    if (o == e) continue;
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
  if (wrap_ && (e == wrap_->c || e == wrap_->d)) {
    e->want = V2{0, 0};
    return;
  }
  if (e->state == EntState::Down) {
    e->want = V2{0, 0};
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
  if (e->userControlled && applyUser(e)) return;
  if (e->side == Side::Def && t < e->getOff) {
    e->want = V2{0, 0};
    return;
  }
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
  UserInput& i = input;
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
    e->anim = Anim::Throw;
    e->animT = 0;
  }
  i.throwAway = false;
  if (e == qb && ball.holder == e && e->pos.x > los + 0.5 && e->state == EntState::QB) {
    scramble = true;
    setCarrier(e);
  }
  // the highlight stick (a move started here plays out under the AI or the stick alike)
  if (i.stickMove != Move::None) {
    const Move mv = i.stickMove;
    i.stickMove = Move::None;
    startMove(e, mv, i.stickSide);
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
  const double sp = e->vmax * (i.sprint ? 1 : 0.82) * (std::min)(1.0, ml);
  e->want = scale(norm(m), sp);
  if (i.juke && e->hasBall && e->animT > 0.6) {
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
  if (play.type == PlayType::Run) {
    // ride the mesh then carry out fake
    const RunScheme& run = *play.run;
    const double aim = by + runAimDy();
    const double dir = jsSign(orIfZero(aim - by, 1));
    e->want = t < run.handoffDelay * 0.6 ? V2{-1.5, dir * 0.6} : V2{0, 0};
    return;
  }
  // pass play: dropback
  // the drop: a play's own depth, else 2 yd from the gun (a quick three step, setting up ~7 deep) and 5 yd from under
  // center (between a three and a five step: 5 to 7 yd deep in the real game; 3 yd was a half drop)
  const double dropX = L - form_->qbDepth - play.dropDepth.value_or(form_->qbDepth <= 1.5 ? 5.0 : 2.0);
  const bool paFake = play.type == PlayType::PlayAction && t < 0.85;
  if (paFake) {
    e->want = V2{-1.2, (cfg.flip ? 1 : -1) * 1.5};
    return;
  }
  if (!dropDone) {
    const V2 to{dropX, by};
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
    scramble = true;
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
  const double readTime = 0.78 - (dec - 50) * 0.005;
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
    if (!quick && depthT < 2 && tPocket < 2.4) open -= 1.2 + tune().outletPen;
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
    if (perceived > thresh && developed && feetSet && tPocket > (screen ? tune().screenT : quick ? 0.7 : 1.15)) {
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
    if (eta < 0.55 && tPocket > 1.0 && rng.chance(0.25 + e->r().awareness * 0.006)) {
      pendingThrow = PendingThrow{nullptr, false, t + releaseTime(e), true};
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
    const bool throwAwayOk = tPocket > 1.3 && rng.chance(0.03 + e->r().awareness * 0.0006);
    if (best && bo > 0 && rng.chance(0.03 + e->r().awareness * 0.0007)) {
      throwPath = 2;
      startThrow(e, best, false);
      return;
    }
    if (lane && (e->r().speed > 64 || tune().scrambleK > 5) && rng.chance((0.012 + (e->r().speed - 60) * 0.0012) * tune().scrambleK)) {
      scramble = true;
      setCarrier(e);
      ev("scramble", e);
      return;
    }
    if (throwAwayOk) {
      pendingThrow = PendingThrow{nullptr, false, t + releaseTime(e), true};
      return;
    }
  }
  if (tPocket > 5.2 && rng.chance(0.04)) {
    pendingThrow = PendingThrow{nullptr, false, t + releaseTime(e), true};
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
  const V2 pocketCenter{dropDone ? (std::max)(e->pos.x, dropX) : dropX, by};
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
  for (Ent* d : def) {
    if (!d->free()) continue;
    const V2 rel = sub(d->pos, q->pos);
    if (rel.x > -1 && rel.x < 7 && std::fabs(rel.y) < 3.5) return false;
  }
  return true;
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
  const double s = len(vel);
  if (idx < e->route.size()) {
    const V2 tgt = e->route[idx];
    const double d = dist(pos, tgt);
    double sp = e->vmax;
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
  ev("throwaway", q);
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
  // exact memo: identical inputs give identical outputs (wind / drag are fixed per play)
  if (landingValid_ && landingPos_.x == b.pos.x && landingPos_.y == b.pos.y && landingPos_.z == b.pos.z &&
      landingVel_.x == b.vel.x && landingVel_.y == b.vel.y && landingVel_.z == b.vel.z)
    return landingOut_;
  // predict where ball reaches catch height (~1.3 yd) on descent
  V3 p = b.pos;
  V3 v = b.vel;
  const double k = dragK();
  for (int i = 0; i < 600; i++) {
    integrate(p, v, k, 1.0 / 60);
    if (v.z < 0 && p.z < 1.3) break;
  }
  landingValid_ = true;
  landingPos_ = b.pos;
  landingVel_ = b.vel;
  landingOut_ = V2{p.x, p.y};
  return landingOut_;
}

void PlaySim::thinkPassBlock(Ent* e) {
  if (ball.holder && ball.holder != qb && ball.holder->side == Side::Off) {
    e->state = EntState::Convoy;
    return;
  }
  if (cfg.offPlay->type == PlayType::Screen && t > 1.2 && e->isOL) {
    // release to set up the screen
    if (e->engaged) disengage(e);
    Ent* rb = offRole("RB");
    e->want = scale(norm(sub(V2{rb->pos.x + 4, rb->pos.y + (e->pos.y - by) * 0.6}, e->pos)), e->vmax * 0.8);
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
  if (e->engaged) return;
  const double aimDy = runAimDy();
  const double playDir = jsSign(orIfZero(aimDy, 0.001));
  Ent* best = nullptr;
  double bs = 1e9;
  const bool isLineman = e->isOL || e->role == "TE" || e->role == "TE2";
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
    e->want = scale(norm(sub(contact, e->pos)), sp);
    if (dist(best->pos, e->pos) < e->radius + best->radius + 0.3) engage(e, best);
  } else {
    // climb to second level along the play side
    e->want = scale(norm(V2{1, playDir * 0.35}), e->vmax * 0.7);
  }
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
  // edge rushers arc around the tackle
  const bool isEdge = e->role == "LE" || e->role == "RE" || (!e->isDL && std::fabs(e->gap - by) > 4);
  V2 tgt = q;
  if (isEdge && e->pos.x > q.x + 2.5) {
    const double side = orIfZero(jsSign(e->pos.y - by), 1);
    tgt = V2{q.x + 1.0, q.y + side * 2.6};
  }
  e->want = scale(norm(sub(tgt, e->pos)), e->vmax);
}

V2 PlaySim::delayedVel(const Ent* e, double lag) const {
  const double n = jsRound(lag * 60);
  const std::deque<V2>& h = e->velHist;
  if (static_cast<double>(h.size()) > n) return h[static_cast<size_t>(static_cast<double>(h.size()) - 1 - n)];
  return h.empty() ? e->vel : h[0];
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
  const V2 seen = add(r->pos, scale(rv, depth > 12 ? 0.5 : 0.35));
  const V2 ahead = r->state == EntState::Route ? predictPos(r, 0.45) : seen;
  const V2 tgt = add(add(scale(seen, 1 - read * tune().routeRead), scale(ahead, read * tune().routeRead)), V2{cushion, inside});
  const double d = dist(tgt, e->pos);
  e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, d * 4 + len(rv)));
}

void PlaySim::thinkZone(Ent* e) {
  if (reactToBall(e)) return;
  V2 spot = e->zoneSpot ? *e->zoneSpot : zoneLandmark(e->zone.empty() ? std::string("hookM") : e->zone);
  const bool deep = !e->zone.empty() && isDeepZone(e->zone);
  // long yardage: the underneath zones sink toward the sticks and give up the short throw (2nd and 12 is played to
  // make it 3rd and 6, not to stop it for nothing - which is how a third of third downs came to be 10+ to go)
  if (!deep && tune().softZone > 0 && cfg.down.value_or(1) >= 2 && cfg.toGo.value_or(10) >= 10)
    spot.x += (std::min)(4.0, (cfg.toGo.value_or(10) - 7) * 0.5) * tune().softZone;
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
  // play-action: underneath defenders bite on the fake
  if (cfg.offPlay->type == PlayType::PlayAction && t < 0.85 && !deep) {
    const double bite = (99 - e->r().playRec) / 99;
    tgt = add(e->pos, V2{-2 * bite, 0});
  }
  const double d = dist(tgt, e->pos);
  e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, d * (deep ? 5 : 3.5)));
}

/** Shared defensive reaction: run read, ball in air, ball carrier. Returns true if handled. */
bool PlaySim::reactToBall(Ent* e) {
  const Ball& b = ball;
  if (b.mode == BallMode::Air && b.target != nullptr) {
    if (t - throwStart > (e->state == EntState::Man ? e->reactT + 0.12 : e->reactT * tune().zoneReactK)) {
      const V2 spot = ballLanding();
      const double dd = dist(spot, e->pos);
      e->want = scale(norm(sub(spot, e->pos)), (std::min)(e->vmax, dd * 4 + 0.5));
      return true;
    }
    return false;
  }
  Ent* h = b.holder;
  if (h && h->side == Side::Off && h->state == EntState::Carrier) {
    const bool rr = t - runReadT > e->reactT + (cfg.offPlay->type == PlayType::Run ? 0 : 0.1);
    const bool crossed = h->pos.x > los + 0.5;
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
    const V2 spot = ballLanding();
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
  e->want = scale(norm(sub(tgt, e->pos)), sp);
}

/** Linemen cannot keep up with a breakaway: once the carrier is well past them, or they are deep downfield,
 *  they let go of their block and trail the play at a jog instead of blocking 60 yards downfield. */
bool PlaySim::trailPlay(Ent* e, Ent* c) {
  const bool big = e->isOL || e->isDL || e->role == "TE" || e->role == "TE2";
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
  // block the nearest threat to the carrier
  Ent* best = nullptr;
  double bs = 1e9;
  for (Ent* d : defendersOf(e)) {
    if (!d->free()) continue;
    const double dc = dist(d->pos, c->pos);
    const double de = dist(d->pos, e->pos);
    if (de > 8 || dc > 12) continue;
    const double s = de + dc * 0.6;
    if (s < bs) {
      bs = s;
      best = d;
    }
  }
  if (best) {
    const V2 p = add(best->pos, scale(norm(sub(c->pos, best->pos)), 0.8));
    e->want = scale(norm(sub(p, e->pos)), e->vmax);
    if (dist(best->pos, e->pos) < e->radius + best->radius + 0.25 && rng.chance(0.25)) engage(e, best);
  } else {
    const double gd = goalDir(c);
    e->want = scale(norm(sub(V2{c->pos.x + gd * 3, c->pos.y}, e->pos)), e->vmax * 0.8);
  }
}

void PlaySim::thinkCarrier(Ent* e) {
  const double gd = goalDir(e);
  const double goal = gd > 0 ? 100 : 0;
  const OffPlay& play = *cfg.offPlay;
  const bool preLOS = e->side == Side::Off && e->pos.x < los - 0.3 && !passThrown && e != qb;
  double best = 0;
  double bestScore = -1e9;
  const double vis = e->r().vision;
  const std::vector<Ent*>& foes = defendersOf(e);
  const double aimY = play.run ? by + runAimDy() : e->pos.y;
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
        const double w = d->engaged ? 0.25 : d->stun > 0 ? 0.3 : 1;
        const double dd = dist(d->pos, p) - k * 0.35;
        danger += (w * std::exp(-(dd * dd) / 3.2)) / k;
      }
    }
    gain = dir.x * gd;
    double score = gain * 1.6 - danger * 2.4;
    if (preLOS) score -= std::fabs(e->pos.y + dir.y * 3 - aimY) * (0.05 + (vis < 70 ? 0.08 : 0));
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
  e->want = scale(h, e->vmax);
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
  const V2 vd = want;
  const double s = len(vel);
  const V2 dir = s > 0.25 ? scale(vel, 1 / s) : norm(vd);
  const V2 dv = sub(vd, vel);
  const double par = dot(dv, dir);
  const V2 perp = sub(dv, scale(dir, par));
  // Human limits (yd/s^2; 1 g = 10.72). Speeding up follows the sprint curve that gives the right 40 times
  // (a = A (1 - v/vmax)); braking tops out near 0.9 g; cornering grip falls with speed (a jump cut at a jog,
  // a rounded turn at full speed: ~0.6 g lateral at top speed); and the whole push never exceeds ~1.05 g of grip.
  const double A = e->vmax / e->tau;
  const double aFwd = A * (std::max)(0.12, 1 - s / e->vmax);
  const double aBrake = (std::min)(A * 1.35, 9.6 + (e->r().agility - 50) * 0.03);
  double parC = clamp(par, -aBrake * dt, aFwd * dt);
  const double pl = len(perp);
  const double latLow = e->latA * 1.2;                                    // planting at a jog
  // at speed; a receiver adjusting to a ball in the air gathers (gives up speed) and gets his full cut back
  const double latHigh = e->catchAdjust ? e->latA : (std::min)(e->latA, 6.6 + (e->r().agility - 50) * 0.03);
  const double kSp = clamp((s - 2.5) / 5.0, 0, 1);
  const double lat = (latLow + (latHigh - latLow) * kSp) * dt;
  V2 perpC = pl > lat ? scale(perp, lat / pl) : perp;
  {
    const double grip = (std::max)(9.9 + (e->r().agility - 50) * 0.03, aFwd) * dt;  // ~0.92 g (at 1.05 g a sixth of all free-running 0.2 s windows read over 1 g, half of them cuts)
    const double tot = std::sqrt(parC * parC + dot(perpC, perpC));
    if (tot > grip) {
      parC *= grip / tot;
      perpC = scale(perpC, grip / tot);
    }
  }
  return add(vel, add(scale(dir, parC), perpC));
}

void PlaySim::impulse(Ent* e, const V2& dv, double dur) {
  e->impulseRate = scale(dv, 1 / dur);
  e->impulseT = dur;
}

void PlaySim::move(Ent* e, double dt) {
  if (wrap_ && (e == wrap_->c || e == wrap_->d)) return;  // the pile moves them (stepWrap)
  if (e->engaged) return;  // engaged pairs are moved by updateEngagements
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
  else if (e->side == Side::Def && !passThrown && ball.mode != BallMode::Air && ns < 5.5 && e->vel.x > -0.3 &&
           (e->state == EntState::Zone || e->state == EntState::Man || e->state == EntState::Spy))
    e->face = angleOf(sub(V2{ball.pos.x, ball.pos.y}, e->pos));
  if (e->anim == Anim::Kneel && kneelT >= 0) {
    // (down on a knee with the kick: he stays there)
  } else if ((e->anim != Anim::Throw && e->anim != Anim::Juke && e->anim != Anim::Tackle) || e->animT > 0.5)
    e->anim = ns > 0.6 ? Anim::Run : Anim::Idle;
  if (e->engaged == nullptr && e->state == EntState::PassBlock && ns < 2) e->anim = Anim::Block;
}

void PlaySim::engage(Ent* blocker, Ent* d) {
  if (blocker->engaged || (d->engaged && d->engaged != blocker && d->engagedBy.size() >= 2)) return;
  if (contains(blocker->beatBy, d)) return;
  if (blocker->stun > 0 || d->stun > 0) return;
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
  blocker->engageEdge = rng.normal(0, 7);
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

void PlaySim::updateEngagements(double dt) {
  for (Ent* d : ents) {
    if (d->engagedBy.empty()) continue;
    // TS keeps a reference to the array that disengage() later replaces: a snapshot is equivalent
    const std::vector<Ent*> blockers = d->engagedBy;
    Ent* holder = ball.holder;
    const bool passPro = blockers[0]->state == EntState::PassBlock;
    // defender's goal
    V2 goal;
    if (passPro && ball.holder == qb) goal = qb->pos;
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
    const double push = clamp(diff / 25 - (d->stallT > 0 ? 0.6 : 0.0), -1, 1);
    d->linePush = push;
    for (Ent* b : blockers) b->linePush = push;
    // a blocker losing the rep hangs on: the worse he is beaten, the likelier the hold is seen (run or pass)
    if (push > 0.3 && ball.mode != BallMode::Snap)
      for (Ent* b : blockers)
        if (b->side == Side::Off) foulHold.add(tune().holdK * (push - 0.3) * dt * (passPro ? 0.75 : 1.4), b->p);  // (a return's blocks after a turnover are not offensive holding)
    const V2 pileV = push > 0 ? scale(toGoal, push * (passPro ? 1.3 : 1.6))
                              : scale(norm(V2{-toGoal.x, -toGoal.y * 0.3}), -push * (passPro ? 0.35 : 1.6));
    // defender also slides laterally toward the goal when not winning
    const V2 lateral{0, toGoal.y * 0.6};
    V2 v = add(pileV, lateral);
    // pocket: pass blockers give ground in their sets, so the fight happens near the QB's depth
    if (passPro && dist(d->pos, goal) > 3.0) v = add(v, scale(toGoal, 1.1 * (std::max)(0.3, 1 - blockers[0]->engT / 2.2)));
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
    if (!d->userControlled && passPro && ball.mode != BallMode::Snap && blockers[0]->engT > 0.7 && d->moveCd <= 0 && d->rushEnergy > 0.3) {
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
    const double base = passPro ? tune().passProShed : tune().runShed;
    const bool holderNear = holder && holder->side != d->side && dist(holder->pos, d->pos) < 1.6;
    const double decay = (std::max)(0.0, blockers[0]->engT - (passPro ? tune().passProHold : 1.6)) * 0.5;
    double lambda = (base * std::exp(diff / 17) + decay) * (holderNear ? 3.2 : 1);
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
  // a pass dead out of bounds (or past the end line) is still in the air: it flies on to the ground (presentation:
  // the play is over), bounces twice and lies there; the stands stop it
  Ball& b = ball;
  if (phase != SimPhase::Dead || b.mode != BallMode::Air || cfg.kick) return;
  const int sub2 = 4;
  for (int i = 0; i < sub2; i++) integrate(b.pos, b.vel, dragK(), dt / sub2);
  b.spin += dt * 40;
  if (b.pos.z <= 0.1) {
    b.pos.z = 0.1;
    b.vel.z = std::fabs(b.vel.z) * 0.35;
    b.vel.x *= 0.6;
    b.vel.y *= 0.6;
    if (jsHypot(b.vel.x, b.vel.y, b.vel.z) < 0.6) {
      b.vel = V3{0, 0, 0};
      b.mode = BallMode::Dead;
    }
  }
  b.pos.y = clamp(b.pos.y, -12, FIELD_W + 12);
  b.pos.x = clamp(b.pos.x, -12, 112);
}

void PlaySim::updateBall(double dt) {
  Ball& b = ball;
  if (b.mode == BallMode::Held && b.holder) {
    Ent* h = b.holder;
    b.pos = V3{h->pos.x + std::cos(h->face) * 0.25, h->pos.y + std::sin(h->face) * 0.25, 1.25};
    return;
  }
  if (b.mode == BallMode::Air && cfg.kick) return;
  if (b.mode == BallMode::Air) {
    b.t += dt;
    const int sub2 = 4;
    for (int i = 0; i < sub2; i++) integrate(b.pos, b.vel, dragK(), dt / sub2);
    b.spin += dt * 40;
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
    // bouncing ball
    b.vel.z -= G * dt;
    b.pos.x += b.vel.x * dt;
    b.pos.y += b.vel.y * dt;
    b.pos.z += b.vel.z * dt;
    if (b.pos.z < 0.1) {
      b.pos.z = 0.1;
      b.vel.z = std::fabs(b.vel.z) * 0.4;
      b.vel.x *= 0.7 + rng.range(-0.2, 0.2);
      b.vel.y = b.vel.y * 0.7 + rng.range(-1.5, 1.5);
    }
    if (b.pos.y < 0 || b.pos.y > FIELD_W || b.pos.x < -10 || b.pos.x > 110) {
      // out of bounds: last possession team keeps it (simplified)
      endPlay("oob");
      return;
    }
    // recovery attempts
    if (b.t > 0.25) {
      const V2 bp{b.pos.x, b.pos.y};
      std::vector<Ent*> near;
      for (Ent* e : ents)
        if (e->free() && dist(e->pos, bp) < 0.9) near.push_back(e);
      std::stable_sort(near.begin(), near.end(), [&bp](const Ent* x, const Ent* y) { return dist(x->pos, bp) - dist(y->pos, bp) < 0; });
      for (Ent* e : near) {
        if (rng.chance(0.18 + e->r().awareness * 0.002)) {
          ev("recover", e);
          recoveredBy = e;
          setCarrier(e);
          // pile-up: most recoveries are downed quickly
          if (rng.chance(0.75)) {
            e->state = EntState::Down;
            endPlay("tackle");
          }
          break;
        }
      }
    }
  }
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
    if (d > 0.35 && !descendingLow && (!hasLast || d < last - 0.005)) continue;
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
        foulDpi.add(tight * tight * (deepBall ? 2.4 : 0.8) * (behind ? 1.0 : 0.35), dm->p);
        foulOpi.add(tight * 0.075, e->p);
      }
      const double tough = offT * 1.2 + (ballSpeed > 24 ? 0.25 : 0) + (contested ? tune().contested + tight * (deepBall ? tune().tightDeep : tune().tightShort + (tune().tightDeep - tune().tightShort) * midK) : 0) +
                           (b.pos.z > 2.6 ? 0.5 : 0) + (b.pos.z < 0.6 ? 0.4 : 0);
      const double wet = cfg.weather.precip != Precip::None ? 0.25 : 0;
      const double p = sigmoid(4.2 + (skill - 75) / 9 - tough * 1.5 - wet);
      // defenders arriving with the ball can break it up
      if (contested) {
        // best-positioned defender: closest to the ball at arrival
        Ent* dbest = oppNear[0];
        for (Ent* o : oppNear)
          if (dist(o->pos, bp) < dist(dbest->pos, bp)) dbest = o;
        const double edge = d - dist(dbest->pos, bp);  // >0: defender closer to the ball than the receiver
        const double pbu = sigmoid((dbest->r().ballSkills - 70) / 12 + edge * 1.6 - 1.2 + (deepBall ? 0.35 : 0) + dbest->fx.pbu - e->fx.winBall) * (deepBall ? 0.6 : 0.62);
        if (rng.chance(pbu)) {
          pbuBy = dbest;
          deepContested = airYds > 15;
          ev("pbu", dbest, e);
          b.vel = scale3(b.vel, 0.25);
          b.vel.z = 1.5;
          b.target = nullptr;
          // defender in great position may come down with it; or tipped and picked
          const double pInt = sigmoid((dbest->r().catching - 62) / 10 + edge * 1.2 - 1.4 - (deepBall ? 1.0 : 0) + tune().intBias + dbest->fx.intAdj - (b.thrower ? b.thrower->fx.ballSafe : 0.0) - e->fx.winBall);
          if (rng.chance(pInt)) intercept(dbest);
          return;
        }
        if (airYds > 15) deepContested = true;
      }
      if (rng.chance(p)) {
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
        const bool diving = (d > 1.05 || (descendingLow && b.pos.z < 0.30)) && defenderClose && e->pos.x < 99.0;
        // a diving catch is down where he lands, not where the ball met his hands: the layout carries him on 2.8 yd
        // (the capture the game plays; it was shown 2.8 yd past the scored spot on every one, p60-p65)
        const V2 diveDir = len(e->vel) > 0.5 ? scale(e->vel, 1.0 / len(e->vel)) : V2{1, 0};
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
      ev(contested ? "contested_inc" : "drop", e);
      {
        const double vy = b.vel.y * 0.2 + rng.range(-1, 1);
        b.vel = V3{b.vel.x * 0.2, vy, 1.2};
      }
      b.target = nullptr;
      return;
    } else {
      // defender: interception or breakup
      Ent* tgt = b.target;
      const double rd = tgt ? dist(tgt->pos, bp) : 9;
      const double edge = clamp(rd - d, -2, 1.5);  // defender beat the receiver to the ball
      const double pPlay = sigmoid((e->r().ballSkills - 70) / 10 + edge * 1.0 - 1.1 - d * 0.8 - (b.pos.z > 2.5 ? 0.8 : 0) + e->fx.pbu - (tgt ? tgt->fx.winBall : 0.0));
      if (!rng.chance(pPlay)) continue;
      const double pInt = sigmoid((e->r().catching - 62) / 10 + edge * 0.6 - 1.2 - (b.pos.x - los > 18 ? 1.0 : 0) + tune().intBias + e->fx.intAdj - (b.thrower ? b.thrower->fx.ballSafe : 0.0));
      if (rng.chance(pInt)) {
        intercept(e);
      } else {
        pbuBy = e;
        ev("pbu", e, tgt);
        const double vx = b.vel.x * 0.2 + rng.range(-2, 2);
        const double vy = b.vel.y * 0.2 + rng.range(-2, 2);
        b.vel = V3{vx, vy, 2};
        b.target = nullptr;
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
    const double range = c->radius + d->radius + (d->userControlled && input.dive ? 1.0 : qbPocket ? 0.3 : tune().tackleReach) +
                         (d->moveOn(Move::Hit) ? 0.6 : d->moveOn(Move::Cut) ? 0.4 : 0);
    if (d->engaged) {
      // engaged defenders can still reach out for a nearby carrier
      // a hazard per second (a blocked man gets an arm free now and then), not a per-tick roll
      // against a runner the shed-and-tackle stays a per-tick roll (calibrated for run defence)
      const double armP = (qbPocket ? (0.5 + d->r().blockShed * 0.01) * SIM_DT * d->fx.sackArm : 0.06 + d->r().blockShed * 0.0012);
      if (dd < range + 0.1 && d->tackleCd <= 0 && rng.chance(armP)) {
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
  if (arm) x -= 1.1;
  if (dive) x -= 1.3;
  if (isSack) x += 0.6;
  if (c->moveOn(Move::Juke)) x -= 0.6 + (std::max)(0.0, c->r().elusiveness - 70) * 0.02;
  if (c->userControlled && input.juke) x -= 0.4;
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
    double fall = vComb.x * gd * 0.38 + (c->r().strength - d->r().strength) * 0.006 + rng.normal(0.35, 0.35);
    fall = clamp(fall, -1.5, 2.6);
    if (isSack) fall = (std::min)(fall, 0.3);
    // yards after contact: ball carriers drive the pile forward unless gang-tackled
    double drive = 0;
    if (!isSack && vComb.x * gd > -1) {
      double help = 0;
      for (Ent* o : defendersOf(c))
        if (o != d && o->free() && dist(o->pos, c->pos) < 2.2) help++;
      const double mu = 0.45 + (c->r().breakTackle * 0.5 + c->r().strength * 0.5 - d->r().tackling) * 0.025 + (mc - md) * 0.012 +
                        c->speed() * 0.07 - help * 0.55 - (facing > 0.3 ? 0.25 : 0) - (d->moveOn(Move::Cut) ? 0.8 : 0) +
                        (c->moveOn(Move::Truck) && facing > 0.2 ? 0.5 : 0) +  // cut at the knees / the shoulder still driving
                        c->fx.driveAdd - d->fx.stopDrive;                     // (abilities: the pile's way)
      drive = clamp(rng.normal(mu, 0.75), 0, 4);
    }
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
    if (c->userControlled) p *= (input.juke || input.stickMove != Move::None || c->move != Move::None) ? 2.2 : 0.5;
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
    fumble(c, help);
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
  const double moveF = (d->moveOn(Move::Hit) ? 2.4 : 1) * (c->moveOn(Move::Spin) ? 1.5 : 1) * (c->moveOn(Move::Hurdle) && !dive ? 1.8 : 1);
  const double pf = 0.0135 * std::exp((70 - c->r().carrying) / 16) * (0.6 + hit) * (isSack ? 3.5 : 1) *
                    (cfg.weather.precip != Precip::None ? 1.3 : 1) * (cfg.kick ? 0.12 : 1) * moveF *  // returners: ~1% (they are picked for it)
                    c->fx.fumble * d->fx.hitFumble;  // (abilities: Vise Grip; Ball Punch, Enforcer)
  if (rng.chance(pf) && !(isSack && rng.chance(0.3))) {
    fumble(c, d);
    return;
  }
  endPlay(isSack ? "sack" : "tackle");
}

void PlaySim::fumble(Ent* c, Ent* byEnt) {
  fumbler = c;
  forcedBy = byEnt;
  ev("fumble", c, byEnt);
  c->hasBall = false;
  c->state = EntState::Down;
  Ball& b = ball;
  b.mode = BallMode::Loose;
  b.holder = nullptr;
  b.t = 0;
  {
    const double vx = rng.range(-4, 4);
    const double vy = rng.range(-4, 4);
    b.vel = V3{vx, vy, 3};
  }
  b.pos = V3{c->pos.x, c->pos.y, 1};
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
  endX = clamp(endX, -10, 110);
  PlayKind kind = play.type == PlayType::Run ? PlayKind::Run : PlayKind::Pass;
  if (play.type == PlayType::Kneel) kind = PlayKind::Kneel;
  if (play.type == PlayType::Spike) kind = PlayKind::Spike;
  if (reason == "sack" || (sackBy && !passThrown)) kind = PlayKind::Sack;
  if (scramble && !passThrown) kind = PlayKind::Scramble;
  if (kind == PlayKind::Kneel) endX = L - 1;
  Player* passerP = (play.type != PlayType::Run && kind != PlayKind::Kneel && kind != PlayKind::Spike) ? qb->p : nullptr;
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
    Ent* re = carrier ? carrier : fumbler;
    r.rusher = re ? re->p : nullptr;
  }
  r.receiver = catcher ? catcher->p : nullptr;
  r.target = tgt ? tgt->p : nullptr;
  for (Ent* tk : tacklers) r.tacklers.push_back(tk->p);
  r.sackedBy = sackBy ? sackBy->p : nullptr;
  r.intBy = intBy ? intBy->p : nullptr;
  r.fumbleBy = fumbler ? fumbler->p : nullptr;
  r.fumbleRecBy = recoveredBy ? recoveredBy->p : nullptr;
  r.forcedBy = forcedBy ? forcedBy->p : nullptr;
  r.pbuBy = pbuBy ? pbuBy->p : nullptr;
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
