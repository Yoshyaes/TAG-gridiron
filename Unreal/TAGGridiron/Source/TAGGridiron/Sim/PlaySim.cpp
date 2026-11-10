// Port of src/sim/play.ts -- per-play physical simulation at 60 Hz.
// Control flow, constants and RNG call order follow the TS line by line.
#include "PlaySim.h"

#include <algorithm>
#include <cmath>

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
  const double fat = 1 - 0.1 * p->fatigue;
  mass = p->weightLb * 0.4536;
  radius = clamp(0.36 + (p->weightLb - 180) / 900, 0.36, 0.55);
  // 99 speed ~ 11.0 yd/s (22.5 mph); 50 ~ 8.6 yd/s
  vmax = (8.6 + (rt.speed - 50) * 0.049) * fat;
  tau = (1.0 - (rt.accel - 50) * 0.0078) / fat;
  latA = (8 + (rt.agility - 50) * 0.12) * fat;
}

// =====================================================================================  PlaySim
PlaySim::PlaySim(const PlayConfig& c) : rng(c.rng), cfg(c), los(c.los), by(c.ballY) {
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
      returner = mk(p, Side::Off, "KR", V2{k.landX - 4, clamp(k.landY + 3, 3, W - 3)});
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
      // punt: six on the line (they block the rush, then peel back), four setting a wall in front of the returner
      front = j < 6;
      y = front ? 5 + j * (W - 10) / 5 : W / 2 + (j - 7.5) * 6.5;
      x = front ? k.coverX - 1.2 : k.landX + 14;
    }
    Ent* e = mk(p, Side::Off, front ? "KB1" : "KB2", V2{x, y});
    e->state = EntState::RunBlock;
  }
  // the coverage team: the kicker (stays back as the safety) and ten across the field on the kick line
  for (size_t i = 0; i < cfg.defUnit.size() && i < 11; ++i) {
    Player* p = cfg.defUnit[i];
    if (i == 0) {
      kickerEnt = mk(p, Side::Def, k.punt ? "P" : "K", V2{k.kickX + (k.punt ? 0.5 : 6), W / 2 - (k.punt ? 0 : 1.5)});
      kickerEnt->state = EntState::Pursue;
      kickerEnt->deepRole = true;
      continue;
    }
    const int j = static_cast<int>(i) - 1;
    const double y = 4 + j * (W - 8) / 9;
    Ent* e = mk(p, Side::Def, "KC", V2{k.coverX + (k.punt ? 0.6 : 1.0), y});
    e->state = EntState::Pursue;
    e->gap = y; // his coverage lane
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
  if (!kicked) {
    // before the kick: only the kicker moves (his approach to the ball)
    if (e == kickerEnt && !k.punt) {
      const V2 to = sub(V2{k.kickX, k.landY > 0 ? FIELD_W / 2 : 0}, e->pos);
      e->want = scale(norm(to), (std::min)(e->vmax * 0.8, len(to) / (std::max)(0.05, k.kickT - t)));
    } else {
      e->want = V2{0, 0};
    }
    return true;
  }
  if (ball.mode != BallMode::Air) return false;
  if (e == returner) {
    // settle under it: arrive a beat early, a step behind the spot, moving forward into the catch
    const double left = k.hang - ball.t;
    const V2 spot{land.x - 0.6, land.y};
    const V2 to = sub(spot, e->pos);
    e->want = scale(norm(to), (std::min)(e->vmax, len(to) / (std::max)(0.25, left - 0.3)));
    return true;
  }
  if (e->side == Side::Def) {
    if (e == kickerEnt) {
      e->want = scale(norm(sub(V2{land.x + 30, FIELD_W / 2}, e->pos)), e->vmax * 0.6);
      return true;
    }
    // kickoff: the coverage may not move until the ball is caught (dynamic kickoff); punts: block, then release
    if (!k.punt || t < k.kickT + 0.5) {
      e->want = V2{0, 0};
      return true;
    }
    // stay in your lane on the way down, closing toward the ball as the gap shrinks
    const double gapX = e->pos.x - land.x;
    const double tgtY = e->gap + (land.y - e->gap) * clamp(1 - gapX / 35, 0, 1) * 0.8;
    e->want = scale(norm(sub(V2{land.x + 2, tgtY}, e->pos)), e->vmax);
    return true;
  }
  // the return team holds its wall (a few steps back toward the returner) until he has it
  const V2 to = sub(V2{e->pos.x - 0.5, e->pos.y}, e->pos);
  e->want = scale(to, 1.5);
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
    if (returner && dist(returner->pos, V2{k.landX, k.landY}) < 2.2) {
      catchX = returner->pos.x;
      catcher = returner;
      returner->vel = scale(returner->vel, 0.6);
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
Player* PlaySim::pickFrom(Team& team, const std::vector<Pos>& posList, std::vector<int>& used) {
  for (Pos ps : posList) {
    std::vector<Player*> cands;
    for (const auto& p : team.roster)
      if (p->pos == ps && p->injuredWeeks <= 0 && !contains(used, p->id)) cands.push_back(p.get());
    std::stable_sort(cands.begin(), cands.end(), [](const Player* a, const Player* b) {
      return b->ovr * (1 - 0.6 * b->fatigue) - a->ovr * (1 - 0.6 * a->fatigue) < 0;
    });
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
      return V2{L - 0.55, by + (flip ? -a.dy : a.dy)};
    case AlignKind::Inline:
      return V2{L - (a.off ? 1.3 : 0.6), by + s(a.side) * 4.05};
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

void PlaySim::setupOffense() {
  std::vector<int> used;
  for (const FormationSlot& slot : form_->slots) {
    Player* p = pickFrom(*cfg.off, slot.pos, used);
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
  for (const FrontSlot& s : FRONT_PERSONNEL(cfg.defCall->front)) {
    Player* p = pickFrom(*cfg.def, s.pos, used);
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
  set("LE", L + 0.9, (teL ? teL->pos.y : lt) + 1.0);
  set("LDT", L + 0.85, by + 1.9);
  set("RDT", L + 0.85, by - 0.7);
  set("RE", L + 0.9, (teR ? teR->pos.y : rt) - 1.0);
  const double lbDepth = gl ? 2.5 : 4.6;
  set("WLB", L + lbDepth, by + 3.6);
  set("MLB", L + lbDepth + 0.3, by + 0.2);
  set("SLB", L + lbDepth, by - 3.6);
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
  auto cbDepth = [&](const Ent* e) { return (press && e) ? 1.1 : cov == Coverage::C2 ? 4.5 : 7.0; };
  Ent* lW = leftR.empty() ? nullptr : leftR[0];
  Ent* rW = rightR.empty() ? nullptr : rightR[0];
  set("LCB", L + cbDepth(lW), lW ? lW->pos.y - 0.7 : by + 13);
  set("RCB", L + cbDepth(rW), rW ? rW->pos.y + 0.7 : by - 13);
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
  const bool twoHigh = cov == Coverage::C2 || cov == Coverage::C4 || cov == Coverage::C2M;
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
  for (Ent* e : def) e->pos.x = (std::max)(e->pos.x, L + 0.75);
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
    for (const char* r : {"NB", "DB", "SS", "SLB", "WLB", "MLB", "FS"}) {
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
      default: zs = {"hookM"}; break;
    }
    // CBs in cover 2 take flats regardless of order
    if (cov == Coverage::C2) {
      const std::pair<const char*, const char*> cbFlats[] = {{"LCB", "flatL"}, {"RCB", "flatR"}};
      for (const auto& rz : cbFlats) {
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
    if (e->role == "WLB" || e->role == "MLB" || e->role == "SLB") e->gap = e->pos.y;
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
  const double aimDy = play.run ? (cfg.flip ? -play.run->aimDy : play.run->aimDy) : 0;
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
}

// ------------------------------------------------------------------ pre-snap adjustments
namespace {
/** A back's check release / a screen's slow release (the route's own delay before he goes). */
double routeDelay(const Ent* e) {
  if (e->role == "RB" && (e->routeName == "checkdown" || e->routeName == "flat")) return 0.9;
  if (e->routeName == "screen") return 1.1;
  return 0;
}
bool isLB(const Ent* e) { return e->role == "WLB" || e->role == "MLB" || e->role == "SLB"; }
bool isCorner(const Ent* e) { return e->role == "LCB" || e->role == "RCB" || e->role == "NB" || e->role == "DB"; }
}  // namespace

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
    e->route.push_back(V2{at.x + pt.first, clamp(at.y + pt.second * side, 1.5, FIELD_W - 1.5)});
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
  if (e->state == EntState::Route && !e->routeName.empty()) assignRoute(e, e->routeName);
  for (Ent* d : def) {
    if (d->state != EntState::Man || d->man != e) continue;
    const V2 dp = d->shiftTo ? *d->shiftTo : d->pos;
    moveTo(d, V2{dp.x, clamp(dp.y + (y - at.y), 1, FIELD_W - 1)});
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
    // a brisk shuffle settling into the spot; a jog (~4.6 yd/s) when there is ground to cover (back from the last play)
    const double sp = 3.4 * (std::min)(1.0, 0.3 + l / 0.8) + 1.2 * clamp((l - 4.0) / 4.0, 0.0, 1.0);
    if (l <= sp * dt || l < 0.02) {
      e->pos = *e->shiftTo;
      e->vel = V2{0, 0};
      e->shiftTo.reset();
    } else {
      e->vel = scale(norm(d), sp);
      e->pos = add(e->pos, scale(e->vel, dt));
    }
  }
}

void PlaySim::finishPresnap() {
  for (Ent* e : ents) {
    if (e->shiftTo) e->pos = *e->shiftTo;
    e->shiftTo.reset();
    e->vel = V2{0, 0};
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
  if (z == "deepM") return V2{L + 15, cl(clamp(by, 20, 33))};
  if (z == "thirdL") return V2{L + 14, cl(W - 10)};
  if (z == "thirdM") return V2{L + 15, cl(clamp(by, 20, 33))};
  if (z == "thirdR") return V2{L + 14, cl(10)};
  if (z == "halfL") return V2{L + 15, cl(W * 0.73)};
  if (z == "halfR") return V2{L + 15, cl(W * 0.27)};
  if (z == "q1") return V2{L + 13, cl(W - 8)};
  if (z == "q2") return V2{L + 14, cl(W * 0.6)};
  if (z == "q3") return V2{L + 14, cl(W * 0.4)};
  if (z == "q4") return V2{L + 13, cl(8)};
  if (z == "flatL") return V2{L + 4.5, cl((std::max)(by + 11, W - 8))};
  if (z == "flatR") return V2{L + 4.5, cl((std::min)(by - 11, 8.0))};
  if (z == "hookL") return V2{L + 8, cl(by + 5.5)};
  if (z == "hookR") return V2{L + 8, cl(by - 5.5)};
  return V2{L + 7, cl(by)};
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
  std::stable_sort(cands.begin(), cands.end(), [&bp](const Ent* a, const Ent* b) { return dist(a->pos, bp) - dist(b->pos, bp) < 0; });
  user->userControlled = false;
  user = cands[0];
  user->userControlled = true;
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
    if (e->tackleCd > 0) e->tackleCd -= dt;
    if (e->pressDelay > 0) e->pressDelay -= dt;
    e->animT += dt;
    e->velHist.push_back(e->vel);
    if (e->velHist.size() > 40) e->velHist.pop_front();
  }
  if (pendingThrow && t >= pendingThrow->at) {
    const PendingThrow pt = *pendingThrow;
    pendingThrow.reset();
    if (ball.holder == qb && ball.mode == BallMode::Held && qb->state != EntState::Down) {
      if (pt.away) throwAway(qb);
      else if (pt.tgt) throwBall(qb, pt.tgt, pt.lob);
    }
  }
  if (cfg.kick) updateKickBall(dt);
  for (Ent* e : ents) think(e, dt);
  for (Ent* e : ents) move(e, dt);
  updateEngagements(dt);
  collide();
  updateBall(dt);
  if (phase == SimPhase::Live) checkTackles();
  if (phase == SimPhase::Live) checkBounds();
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
    if (t >= play.run->handoffDelay && dist(rb->pos, qb->pos) < (pitch ? 5.5 : 2.0)) {
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
  if (e->state == EntState::Down) {
    e->want = V2{0, 0};
    return;
  }
  if (e->stun > 0) {
    e->want = scale(e->vel, 0.5);
    return;
  }
  if (cfg.kick && (!kicked || ball.mode == BallMode::Air) && !(e->userControlled && kicked && e->side == Side::Def)) {
    if (thinkReturn(e)) return;
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
        pendingThrow = PendingThrow{tgt, i.lob, t + releaseTime(e) * 0.8, false};
        e->anim = Anim::Throw;
        e->animT = 0;
      }
    }
  }
  if (e == qb && ball.holder == e && e->pos.x > los + 0.5 && e->state == EntState::QB) {
    scramble = true;
    setCarrier(e);
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
    // juke: sharp lateral impulse with brief speed loss
    const V2 lat{-e->vel.y, e->vel.x};
    const double side = dot(norm(lat), m) >= 0 ? 1 : -1;
    e->vel = add(scale(e->vel, 0.55), scale(norm(lat), side * 3.2 * (0.7 + e->r().agility / 330)));
    e->anim = Anim::Juke;
    e->animT = 0;
  }
  return true;
}

void PlaySim::thinkIdle(Ent* e) {
  const OffPlay& play = *cfg.offPlay;
  if (play.run && play.run->carrier == e->role && !handedOff) {
    // path to mesh point then aiming point
    const RunScheme& run = *play.run;
    const double aim = by + (cfg.flip ? -run.aimDy : run.aimDy);
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
    const double aim = by + (cfg.flip ? -run.aimDy : run.aimDy);
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
  if (pressure > 0.5) pressured = true;
  const double dec = e->r().decision;
  const double readTime = 0.78 - (dec - 50) * 0.005;
  Ent* cur = reads.empty() ? nullptr : reads[(std::min)(readIdx, reads.size() - 1)];
  const bool quick = play.hasTag("quick") || play.type == PlayType::Screen;
  // openness threshold relaxes as time passes / pressure mounts
  const double thresh = (quick ? 0.8 : 2.6) - tPocket * 0.24 - pressure * 1.1;
  if (cur) {
    const double depthT = predictPos(cur, 1.2).x - L;
    double open = openness(cur, e) - (depthT > 15 ? 1.35 + (depthT - 15) * 0.08 : 0);
    // situational: on 3rd/4th down, routes past the sticks are worth more
    const double down = cfg.down.value_or(1);
    const double toGo = cfg.toGo.value_or(10);
    if (down >= 3) open += depthT >= toGo ? 0.6 : depthT < toGo - 3 ? -0.9 : 0;
    // mental noise: poor decision makers misjudge
    const double perceived = open + rng.normal(0, 0.25 + (99 - dec) * 0.012);
    if (perceived > thresh && tPocket > (quick ? 0.7 : 1.15)) {
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
  if (eta < 0.75 + e->r().awareness * 0.004 && rng.chance(0.08 + e->r().awareness * 0.0025)) {
    Ent* hot = nullptr;
    double ho = -99;
    for (Ent* r : reads) {
      const double o = openness(r, e) + (r->pos.x - L < 8 ? 0.4 : 0);
      if (o > ho) {
        ho = o;
        hot = r;
      }
    }
    if (hot && ho > (eta < 0.35 ? -0.2 : 0.4)) {
      startThrow(e, hot, false);
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
      startThrow(e, best, false);
      return;
    }
    if (lane && e->r().speed > 64 && rng.chance(0.012 + (e->r().speed - 60) * 0.0012)) {
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
  // pocket movement: slide away from nearest free rusher, climb up
  V2 push{0.6, 0};
  for (Ent* d : def) {
    if (!d->free() || d->state != EntState::Rush) continue;
    const double dd = dist(d->pos, e->pos);
    if (dd < 4) push = add(push, scale(norm(sub(e->pos, d->pos)), (4 - dd) * 0.9));
  }
  const V2 pocketCenter{dropDone ? (std::max)(e->pos.x, dropX) : dropX, by};
  const V2 back = sub(pocketCenter, e->pos);
  e->want = scale(add(push, scale(back, 0.5)), 1.4);
  if (len(e->want) > e->vmax * 0.5) e->want = scale(norm(e->want), e->vmax * 0.5);
}

/** 0..1+ pressure based on free rushers near QB. */
double PlaySim::pressureOn(const Ent* q) const {
  double p = 0;
  for (Ent* d : def) {
    if (!d->free()) continue;
    const double dd = dist(d->pos, q->pos);
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

/** Predict where a receiver will be in T seconds by walking their route. */
V2 PlaySim::predictPos(const Ent* e, double T) const {
  if (e->state != EntState::Route || e->route.empty()) return add(e->pos, scale(e->vel, T));
  V2 p = e->pos;
  double remaining = (std::max)(0.0, T - (std::max)(0.0, e->pressDelay));
  const double sp = (std::max)(e->speed(), e->vmax * 0.85);
  size_t idx = e->routeIdx;
  while (remaining > 0 && idx < e->route.size()) {
    const V2 tgt = e->route[idx];
    const double d = dist(p, tgt);
    const double tt = d / sp;
    if (tt >= remaining) return add(p, scale(norm(sub(tgt, p)), sp * remaining));
    remaining -= tt * 1.12;  // breaks cost time
    p = tgt;
    idx++;
  }
  if (e->settle) return p;
  // continue along last leg direction
  const size_t n = e->route.size();
  const V2 dir = n >= 2 ? norm(sub(e->route[n - 1], e->route[n - 2])) : V2{1, 0};
  const V2 q = add(p, scale(dir, sp * (std::max)(0.0, remaining)));
  return V2{q.x, clamp(q.y, 0.8, FIELD_W - 0.8)};
}

/** Windup: elite quick releases ~0.2s, slow ones ~0.38s. */
double PlaySim::releaseTime(const Ent* q) const {
  return clamp(0.3 - (q->r().throwPower + q->r().awareness - 120) * 0.0019, 0.16, 0.34);
}

/** Seconds until the nearest unblocked rusher reaches the QB. */
double PlaySim::rushETA(const Ent* q) const {
  double eta = 99;
  for (Ent* d : def) {
    if (!d->free() || d->state != EntState::Rush) continue;
    const double dd = dist(d->pos, q->pos) - 1.1;
    eta = (std::min)(eta, dd / (std::max)(4.0, d->speed()));
  }
  return eta;
}

void PlaySim::startThrow(Ent* q, Ent* tgt, bool lob) {
  if (pendingThrow) return;
  pendingThrow = PendingThrow{tgt, lob, t + releaseTime(q), false};
  q->anim = Anim::Throw;
  q->animT = 0;
}

double PlaySim::throwSpeed(const Ent* q) const {
  return 20 + (q->r().throwPower - 50) * 0.16;  // 99 -> 27.8 yd/s (~57 mph)
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
    const double reach = d->free() ? d->vmax * (std::max)(0.0, T - d->reactT - 0.15) * (T > 1.6 ? 0.98 : 0.85) : 0;
    const double sep = dist(d->pos, P) - reach - 0.9;
    // defenders in the passing lane
    const double lane = laneBlock(q->pos, P, d->pos);
    worst = (std::min)({worst, sep, lane});
  }
  // throws beyond the sideline are worthless
  if (P.y < 0.5 || P.y > FIELD_W - 0.5) worst -= 3;
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

void PlaySim::throwBall(Ent* q, Ent* tgt, bool lob) {
  Ball& b = ball;
  const double v0max = throwSpeed(q);
  // target at chest height ~ 1.4 yd
  double T = dist(q->pos, tgt->pos) / (v0max * 0.8) + 0.1;
  V2 aim = predictPos(tgt, T);
  double touch = lob ? 0.32 : 0.05;
  for (int k = 0; k < 4; k++) {
    const double D = dist(q->pos, aim);
    touch = (std::max)(touch, (lob || D > 28) ? 0.32 : D > 15 ? 0.14 : 0.05);
    // put air under it if an underneath defender sits in the lane
    if (!lob && D > 8) {
      for (Ent* d : def) {
        if (d->state == EntState::Down || d->engaged) continue;
        const V2 ab = sub(aim, q->pos);
        const double tt = dot(sub(d->pos, q->pos), ab) / dot(ab, ab);
        if (tt < 0.25 || tt > 0.85) continue;
        if (dist(d->pos, add(q->pos, scale(ab, tt))) < 1.6) touch = (std::max)(touch, 0.12 + (tt < 0.6 ? 0.12 : 0.05) * (D / 15));
      }
    }
    T = solveT(D, v0max, (std::min)(0.45, touch));
    aim = predictPos(tgt, T + 0.05);
  }
  // QBs throw receivers away from the sideline so they can stay in bounds
  aim = V2{aim.x, clamp(aim.y, 1.8, FIELD_W - 1.8)};
  const double D = dist(q->pos, aim);
  // accuracy model
  const double depthAcc = D < 10 ? q->r().accShort : D < 20 ? q->r().accMid : q->r().accDeep;
  const double pressure = pressureOn(q);
  const double moving = q->speed() / q->vmax;
  const double onRun = moving > 0.35 ? 1 + (1 - q->r().throwOnRun / 100) * moving * 1.5 : 1;
  const double poise = 1 + pressure * (1.4 - q->r().poise / 100);
  const double weather = cfg.weather.precip == Precip::Rain ? 1.12 : cfg.weather.precip == Precip::Snow ? 1.18 : 1;
  const double sd = (0.36 + (99 - depthAcc) * 0.021) * (0.5 + D / 19) * onRun * poise * weather * (1 + q->p->fatigue * 0.2);
  {
    const double nx = rng.normal(0, sd * 0.9);
    const double ny = rng.normal(0, sd);
    aim = V2{aim.x + nx, aim.y + ny};
  }
  const double zErr = rng.normal(0, sd * 0.45);
  launch(q, aim, T, 1.45 + zErr);
  b.target = tgt;
  b.targetSpot = aim;
  b.lob = lob;
  passThrown = true;
  throwStart = t;
  q->anim = Anim::Throw;
  q->animT = 0;
  q->state = EntState::Idle;
  ev("throw", q, tgt);
  // receiver tracks the ball
  tgt->zoneSpot = aim;
}

void PlaySim::throwAway(Ent* q) {
  const double side = q->pos.y > by ? 1 : -1;
  const V2 aim{los + 8, side > 0 ? FIELD_W + 4 : -4};
  const double D = dist(q->pos, aim);
  const double T = solveT(D, throwSpeed(q), 0.15);
  launch(q, aim, T, 2.5);
  ball.target = nullptr;
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
void PlaySim::launch(Ent* q, const V2& aim, double T, double zTarget) {
  const double z0 = 2.05;  // release height ~ 6'2"
  V2 tgt = aim;
  V3 vel{0, 0, 0};
  for (int it = 0; it < 3; it++) {
    vel = V3{(tgt.x - q->pos.x) / T, (tgt.y - q->pos.y) / T, (zTarget - z0 + 0.5 * G * T * T) / T};
    const V3 land = simulateFlight(V3{q->pos.x, q->pos.y, z0}, vel, T);
    tgt = V2{tgt.x + (aim.x - land.x), tgt.y + (aim.y - land.y)};
  }
  Ball& b = ball;
  b.mode = BallMode::Air;
  b.holder = nullptr;
  q->hasBall = false;
  b.thrower = q;
  b.throwX = q->pos.x;
  b.pos = V3{q->pos.x, q->pos.y, z0};
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
  if (ball.mode == BallMode::Air && ball.target == e && ball.targetSpot) {
    // track the ball: go to landing spot, arriving on time
    const V2 spot = ballLanding();
    e->want = scale(norm(sub(spot, e->pos)), (std::min)(e->vmax, dist(spot, e->pos) * 5 + 1));
    return;
  }
  if (ball.mode == BallMode::Air) {
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
    const V2 tgt = e->route[e->routeIdx];
    const double d = dist(e->pos, tgt);
    if (d < 0.7) {
      e->routeIdx++;
      // a crisp break: route runners lose less speed
      const double keep = 0.55 + e->r().routeRunning * 0.004;
      e->vel = scale(e->vel, keep);
      thinkRoute(e);
      return;
    }
    e->want = scale(norm(sub(tgt, e->pos)), e->vmax);
    return;
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
  const size_t n = e->route.size();
  const V2 dir = n >= 2 ? norm(sub(e->route[n - 1], e->route[n - 2])) : V2{1, 0};
  V2 w = scale(dir, e->vmax);
  if (e->pos.y < 2 && w.y < 0) w = V2{e->vmax, 0};
  if (e->pos.y > FIELD_W - 2 && w.y > 0) w = V2{e->vmax, 0};
  e->want = w;
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
  if (e->engaged) return;
  const V2 q = qb->pos;
  Ent* best = nullptr;
  double bs = 1e9;
  Ent* as = e->blockTarget;
  if (as && as->free() && as->state == EntState::Rush && dist(as->pos, q) < dist(e->pos, q) + 2.5 && t < 2.5) {
    best = as;
    bs = -1;
  }
  // otherwise find most dangerous unblocked rusher
  for (Ent* d : def) {
    if (bs < 0) break;
    if (d->state != EntState::Rush || !d->free()) continue;
    const double dd = dist(d->pos, e->pos);
    if (dd > 5) continue;
    const double threat = dist(d->pos, q);
    const double blockers = static_cast<double>(d->engagedBy.size());
    const double s = dd * 0.9 + threat * 0.5 + blockers * 3;
    if (s < bs) {
      bs = s;
      best = d;
    }
  }
  e->blockTarget = best;
  const V2 home{(std::min)(los - 1.4, q.x + 2.8), e->pos.y * 0.3 + (by + (e->pos.y - by) * 1.15) * 0.7};
  if (best) {
    // kick-slide: set up on the rusher's projected path to the QB, not where he is now
    const V2 ahead = add(best->pos, scale(best->vel, 0.35));
    const V2 lane = norm(sub(q, ahead));
    const V2 setPt = add(ahead, scale(lane, 1.0));
    e->want = scale(norm(sub(setPt, e->pos)), (std::min)(e->vmax, dist(setPt, e->pos) * 6));
    if (dist(best->pos, e->pos) < e->radius + best->radius + 0.45) engage(e, best);
  } else {
    e->want = scale(sub(home, e->pos), 2);
    if (len(e->want) > e->vmax * 0.5) e->want = scale(norm(e->want), e->vmax * 0.5);
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
  if (e->engaged) return;
  const double aimDy = run ? (cfg.flip ? -run->aimDy : run->aimDy) : 0;
  const double playDir = jsSign(orIfZero(aimDy, 0.001));
  Ent* best = nullptr;
  double bs = 1e9;
  const bool isLineman = e->isOL || e->role == "TE" || e->role == "TE2";
  const double zoneShift = run && (run->style == RunStyle::Zone || run->style == RunStyle::Stretch) ? playDir * 1.4 : 0;
  const V2 myPt{e->pos.x + 1.5, e->pos.y + zoneShift};
  Ent* as = e->blockTarget;
  if (as && as->state != EntState::Down && !as->engaged && as->stun <= 0 && dist(as->pos, e->pos) < 7) {
    best = as;
    bs = -1;
  }
  for (Ent* d : def) {
    if (bs < 0) break;
    if (!d->free()) continue;
    const double dd = dist(d->pos, myPt);
    const double range = isLineman ? 4.5 : 9;
    if (dd > range) continue;
    // prefer defenders in front, and those not already blocked
    double s = dd + static_cast<double>(d->engagedBy.size()) * 4 + (d->pos.x < e->pos.x - 0.5 ? 3 : 0);
    if (!isLineman && c) s += dist(d->pos, c->pos) * 0.5;
    if (s < bs) {
      bs = s;
      best = d;
    }
  }
  if (best) {
    const V2 contact = add(best->pos, scale(norm(sub(e->pos, best->pos)), 0.6));
    e->want = scale(norm(sub(contact, e->pos)), e->vmax);
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
  const double lag = 0.1 + (99 - e->r().manCov) * 0.006 + (99 - e->r().agility) * 0.002;
  const V2 rv = delayedVel(r, lag);
  // trail with inside/over leverage: stay slightly deeper (+x) on deep routes
  const double depth = r->pos.x - los;
  const double cushion = depth > 8 ? 1.4 + (e->pos.x < r->pos.x ? 1 : 0) : 0.3;
  const double inside = jsSign(by - r->pos.y) * 0.4;
  const V2 tgt = add(add(r->pos, scale(rv, 0.35)), V2{cushion, inside});
  const double d = dist(tgt, e->pos);
  e->want = scale(norm(sub(tgt, e->pos)), (std::min)(e->vmax, d * 4 + len(rv)));
}

void PlaySim::thinkZone(Ent* e) {
  if (reactToBall(e)) return;
  const V2 spot = e->zoneSpot ? *e->zoneSpot : zoneLandmark(e->zone.empty() ? std::string("hookM") : e->zone);
  const bool deep = !e->zone.empty() && isDeepZone(e->zone);
  // find receivers threatening my area
  Ent* threat = nullptr;
  double tScore = 1e9;
  const double radius = deep ? 17 : 7;
  for (Ent* r : off) {
    if (r->state != EntState::Route) continue;
    const double dd = deep ? std::fabs(r->pos.y - spot.y) : dist(r->pos, spot);
    if (dd > radius) continue;
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
    const double lag = 0.15 + (99 - e->r().zoneCov) * 0.008;
    const V2 rv = delayedVel(threat, lag);
    const V2 tp = add(threat->pos, scale(rv, 0.45));
    if (deep) {
      // stay over the top: cushion grows with the receiver's speed (bail technique)
      const double cushion = 2.5 + threat->speed() * 0.45;
      tgt = V2{(std::max)(spot.x - 2, tp.x + cushion), spot.y * 0.3 + tp.y * 0.7};
    } else {
      // sit underneath, between receiver and QB, but don't leave the zone too far
      const V2 toQB = norm(sub(qb->pos, tp));
      const V2 under = add(tp, scale(toQB, 1.2));
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
    if (t - throwStart > e->reactT + (e->state == EntState::Man ? 0.12 : 0)) {
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
  const double k = 0.75 + e->r().pursuit * 0.0035;
  // close in: cut the angle hard (aim almost at the body) instead of running parallel
  const double lead = (std::min)(tInt, (d / e->vmax) * 1.1 + 0.1) * k;
  V2 tgt = d < 1.2 ? c->pos : add(c->pos, scale(c->vel, lead));
  // last line of defense: deep defenders keep depth (stay over the top) until the carrier is close
  if (e->deepRole && c->side == Side::Off && d > 3.5) {
    const double keep = c->pos.x + (std::min)(4.0, 1 + d * 0.3);
    if (tgt.x < keep) tgt = V2{keep, tgt.y};
  }
  e->want = scale(norm(sub(tgt, e->pos)), e->vmax);
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
  const double aimY = play.run ? by + (cfg.flip ? -play.run->aimDy : play.run->aimDy) : e->pos.y;
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
    score += rng.normal(0, 0.05 + (99 - vis) * 0.009);
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
        const double side = rel.y > 0 ? -1 : 1;
        e->vel = add(scale(e->vel, 0.6), V2{0, side * 2.8 * (0.7 + e->r().agility / 330)});
        e->anim = Anim::Juke;
        e->animT = 0;
        break;
      }
    }
  }
}

// ------------------------------------------------------------------ physics
void PlaySim::move(Ent* e, double dt) {
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
  const V2 vd = e->want;
  const double s = e->speed();
  const V2 dir = s > 0.25 ? scale(e->vel, 1 / s) : norm(vd);
  const V2 dv = sub(vd, e->vel);
  const double par = dot(dv, dir);
  const V2 perp = sub(dv, scale(dir, par));
  const double A = e->vmax / e->tau;
  const double aFwd = A * (std::max)(0.12, 1 - s / e->vmax);
  const double aBrake = A * 1.35;
  const double parC = clamp(par, -aBrake * dt, aFwd * dt);
  const double pl = len(perp);
  const double lat = e->latA * (s < 2.5 ? 1.8 : 1) * dt;
  const V2 perpC = pl > lat ? scale(perp, lat / pl) : perp;
  e->vel = add(e->vel, add(scale(dir, parC), perpC));
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
  if (e == qb && ball.holder == e && !dropDone && e->state == EntState::QB) e->face = 0;
  else if (e->side == Side::Off && e->state == EntState::PassBlock) e->face = 0;
  else if (e->side == Side::Def && !passThrown && ball.mode != BallMode::Air && ns < 5.5 && e->vel.x > -0.3 &&
           (e->state == EntState::Zone || e->state == EntState::Man || e->state == EntState::Spy))
    e->face = angleOf(sub(V2{ball.pos.x, ball.pos.y}, e->pos));
  if ((e->anim != Anim::Throw && e->anim != Anim::Juke && e->anim != Anim::Tackle) || e->animT > 0.5)
    e->anim = ns > 0.6 ? Anim::Run : Anim::Idle;
  if (e->engaged == nullptr && e->state == EntState::PassBlock && ns < 2) e->anim = Anim::Block;
}

void PlaySim::engage(Ent* blocker, Ent* d) {
  if (blocker->engaged || (d->engaged && d->engaged != blocker && d->engagedBy.size() >= 2)) return;
  if (contains(blocker->beatBy, d)) return;
  if (blocker->stun > 0 || d->stun > 0) return;
  // defenders in space can slip a block entirely (smaller, quicker players more so)
  const bool inSpace = d->pos.x > los + 2.5 || std::fabs(d->pos.y - by) > 7;
  if (inSpace && d->engagedBy.empty()) {
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
    const double diff = dSkill - avgB - edge + massAdv - (nb - 1) * 14;
    // movement of the pile
    const double push = clamp(diff / 25, -1, 1);
    d->linePush = push;
    for (Ent* b : blockers) b->linePush = push;
    const V2 pileV = push > 0 ? scale(toGoal, push * (passPro ? 1.3 : 1.6))
                              : scale(norm(V2{-toGoal.x, -toGoal.y * 0.3}), -push * (passPro ? 0.35 : 1.6));
    // defender also slides laterally toward the goal when not winning
    const V2 lateral{0, toGoal.y * 0.6};
    V2 v = add(pileV, lateral);
    // pocket: pass blockers give ground in their sets, so the fight happens near the QB's depth
    if (passPro && dist(d->pos, goal) > 3.0) v = add(v, scale(toGoal, 1.1 * (std::max)(0.3, 1 - blockers[0]->engT / 2.2)));
    d->vel = v;
    d->pos = add(d->pos, scale(v, dt));
    d->face = angleOf(toGoal);
    if (d->anim != Anim::Block && (d->anim != Anim::Tackle || d->animT > 0.4)) d->anim = Anim::Block;
    for (Ent* b : blockers) {
      if (b->anim != Anim::Block) b->anim = Anim::Block;
      const V2 offset = scale(norm(sub(b->pos, d->pos)), b->radius + d->radius);
      b->pos = add(d->pos, offset);
      b->vel = v;
      b->face = angleOf(sub(d->pos, b->pos));
    }
    // shed hazard: skill matchup plus blocks degrade the longer they're held
    for (Ent* b : blockers) b->engT += dt;
    const double base = passPro ? 0.1 : 0.09;
    const bool holderNear = holder && holder->side != d->side && dist(holder->pos, d->pos) < 1.6;
    const double decay = (std::max)(0.0, blockers[0]->engT - (passPro ? 2.6 : 1.6)) * 0.5;
    const double lambda = (base * std::exp(diff / 17) + decay) * (holderNear ? 3.2 : 1);
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
      if (ball.mode != BallMode::Snap) ev("shed", d, blockers[0], shedMove(d, blockers[0], passPro, push)); // events start at the snap
      for (Ent* b : blockers) {
        disengage(b);
        b->stun = (passPro ? 0.6 : 0.35) + rng.range(0, 0.3);
        if (!contains(b->beatBy, d)) b->beatBy.push_back(d);
      }
      if (passPro) d->pos = add(d->pos, scale(toGoal, 0.5));
      d->engaged = nullptr;
      d->engagedBy.clear();
      d->stun = 0;
      d->vel = scale(toGoal, 2.5);
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
    auto lastIt = lastBallD_.find(e);
    const bool hasLast = lastIt != lastBallD_.end();
    const double last = hasLast ? lastIt->second : 0;
    lastBallD_[e] = d;
    const bool descendingLow = b.vel.z < 0 && b.pos.z < 0.9;
    if (d > 0.35 && !descendingLow && (!hasLast || d < last - 0.005)) continue;
    reached_[e] = t;
    const bool deepBall = b.pos.x - los > 15;
    const V2 bp{b.pos.x, b.pos.y};
    std::vector<Ent*> oppNear;
    for (Ent* o : ents)
      if (o->side != e->side && o->free() && dist(o->pos, e->pos) < (deepBall ? 2.2 : 1.3)) oppNear.push_back(o);
    const bool contested = !oppNear.empty();
    const double airYds = b.pos.x - los;
    if (e->side == Side::Off) {
      if (!e->eligible) continue;
      // catch probability
      const double skill = contested ? e->r().catchInTraffic * 0.6 + e->r().catching * 0.4 : e->r().catching;
      const double offT = d / 1.2;  // how far off-target relative to reach
      const double ballSpeed = jsHypot(b.vel.x, b.vel.y, b.vel.z);
      const double tough = offT * 1.2 + (ballSpeed > 24 ? 0.25 : 0) + (contested ? 0.7 : 0) + (b.pos.z > 2.6 ? 0.5 : 0) + (b.pos.z < 0.6 ? 0.4 : 0);
      const double wet = cfg.weather.precip != Precip::None ? 0.25 : 0;
      const double p = sigmoid(4.2 + (skill - 75) / 9 - tough * 1.5 - wet);
      // defenders arriving with the ball can break it up
      if (contested) {
        // best-positioned defender: closest to the ball at arrival
        Ent* dbest = oppNear[0];
        for (Ent* o : oppNear)
          if (dist(o->pos, bp) < dist(dbest->pos, bp)) dbest = o;
        const double edge = d - dist(dbest->pos, bp);  // >0: defender closer to the ball than the receiver
        const double pbu = sigmoid((dbest->r().ballSkills - 70) / 12 + edge * 1.6 - 1.2 + (deepBall ? 0.35 : 0)) * (deepBall ? 0.6 : 0.62);
        if (rng.chance(pbu)) {
          pbuBy = dbest;
          deepContested = airYds > 15;
          ev("pbu", dbest, e);
          b.vel = scale3(b.vel, 0.25);
          b.vel.z = 1.5;
          b.target = nullptr;
          // defender in great position may come down with it; or tipped and picked
          const double pInt = sigmoid((dbest->r().catching - 62) / 10 + edge * 1.2 - 1.4 - (deepBall ? 1.0 : 0));
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
        // a ball well off target, or a low one dying at the turf, with a defender right there, is caught laid out and
        // touched down: the play ends at the spot (outside the end zone; there the normal scoring rules apply)
        bool defenderClose = false;
        for (Ent* o : ents)
          if (o->side != e->side && o->free() && dist(o->pos, e->pos) < 2.0) defenderClose = true;
        const bool diving = (d > 1.05 || (descendingLow && b.pos.z < 0.30)) && defenderClose && e->pos.x < 99.0;
        ev(diving ? "divecatch" : "catch", e);
        e->anim = Anim::Catch;
        e->animT = 0;
        // momentum loss when catching
        e->vel = scale(e->vel, contested ? 0.3 : 0.45);
        setCarrier(e);
        if (diving) {
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
      const double pPlay = sigmoid((e->r().ballSkills - 70) / 10 + edge * 1.0 - 1.1 - d * 0.8 - (b.pos.z > 2.5 ? 0.8 : 0));
      if (!rng.chance(pPlay)) continue;
      const double pInt = sigmoid((e->r().catching - 62) / 10 + edge * 0.6 - 1.2 - (b.pos.x - los > 18 ? 1.0 : 0));
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
  Ent* c = ball.holder;
  if (!c || ball.mode != BallMode::Held) return;
  // QB in pocket is a sack target; others are carriers
  const std::vector<Ent*>& foes = defendersOf(c);
  int gang = 0;
  for (Ent* d : foes) {
    if (d->state == EntState::Down || d->stun > 0) continue;
    const double dd = dist(d->pos, c->pos);
    const bool qbPocket = c == qb && !passThrown && !scramble && cfg.offPlay->type != PlayType::Run;
    const double range = c->radius + d->radius + (d->userControlled && input.dive ? 1.0 : qbPocket ? 0.3 : 0.52);
    if (d->engaged) {
      // engaged defenders can still reach out for a nearby carrier
      if (dd < range + 0.1 && d->tackleCd <= 0 && rng.chance(0.06 + d->r().blockShed * 0.0012)) {
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
  double x = 2.6 + (d->r().tackling - 70) / 13 - (carrierSkill - 70) / 15;
  x += facing > 0.3 ? 0.35 : facing < -0.3 ? -0.55 : 0;
  x += -std::log(mc / md) * 1.4;
  x += -(vc / (std::max)(1.0, c->vmax)) * 0.35 + (vd / (std::max)(1.0, d->vmax)) * 0.2;
  x += (gang - 1) * 0.9;
  if (arm) x -= 1.1;
  if (dive) x -= 1.3;
  if (isSack) x += 0.6;
  if (c->anim == Anim::Juke && c->animT < 0.35) x -= 0.6 + (std::max)(0.0, c->r().elusiveness - 70) * 0.02;
  if (c->userControlled && input.juke) x -= 0.4;
  const double p = sigmoid(x);
  if (tackleLog) tackleLog->push_back(TackleLogEntry{p, facing, dive, arm, gang, c->pos.x - los, vc, c->anim == Anim::Juke});
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
    // yards after contact: ball carriers drive the pile / fall forward unless gang-tackled
    if (!isSack && vComb.x * gd > -1) {
      double help = 0;
      for (Ent* o : defendersOf(c))
        if (o != d && o->free() && dist(o->pos, c->pos) < 2.2) help++;
      const double mu = 0.45 + (c->r().breakTackle * 0.5 + c->r().strength * 0.5 - d->r().tackling) * 0.025 + (mc - md) * 0.012 +
                        c->speed() * 0.07 - help * 0.55 - (facing > 0.3 ? 0.25 : 0);
      fall += clamp(rng.normal(mu, 0.75), 0, 4);
    }
    // QB sneak: the whole line surges, QB lunges over the pile
    if (cfg.offPlay->run && cfg.offPlay->run->style == RunStyle::Sneak && c == qb)
      fall += 0.9 + rng.range(0, 0.8) + (c->r().strength - 60) * 0.01;
    c->pos = V2{c->pos.x + fall * gd, c->pos.y + vComb.y * 0.25};
    c->state = EntState::Down;
    c->anim = Anim::Down;
    c->animT = 0;
    d->state = EntState::Down;
    d->anim = Anim::Tackle;
    tacklers.push_back(d);
    ev("tackle", d, c, dive ? "dive" : arm ? "arm" : "");  // presentation: the paired fall
    // assist: another defender right there
    for (Ent* o : defendersOf(c)) {
      if (o != d && o->free() && dist(o->pos, c->pos) < 1.4) {
        tacklers.push_back(o);
        ev("assist", o, c);
        break;
      }
    }
    if (isSack) sackBy = d;
    // fumble?
    const double hit = (md * vd) / 900;
    const double pf = 0.0105 * std::exp((70 - c->r().carrying) / 16) * (0.6 + hit) * (isSack ? 3.5 : 1) *
                      (cfg.weather.precip != Precip::None ? 1.3 : 1) * (cfg.kick ? 0.12 : 1);  // returners: ~1% (they are picked for it)
    if (rng.chance(pf) && !(isSack && rng.chance(0.3))) {
      fumble(c, d);
      return true;
    }
    endPlay(isSack ? "sack" : "tackle");
    return true;
  }
  // missed / broken tackle
  brokenTackles++;
  ev(dive ? "divemiss" : "broken", c, d);
  d->stun = (dive ? 1.0 : 0.6) + rng.range(0, 0.6);
  if (dive) d->anim = Anim::Dive;
  d->state = d->state == EntState::Rush ? EntState::Pursue : d->state;
  c->vel = scale(c->vel, dive ? 0.82 : 0.68);
  c->animT = 0;
  return false;
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
  // a defensive player downed in his own end zone after a takeaway = touchback
  if (turnover && !td && endX >= 100) touchback = true;
  if (!turnover && holder && !td && endX <= 0 && reason != "incomplete") safety = true;
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
  bool ta = false;
  for (const PlayEvent& e : events)
    if (e.type == "throwaway") ta = true;
  r.thrownAway = ta;
  r.pressured = pressured;
  r.timeToThrow = throwStart > 0 ? throwStart : t;
  r.brokenTackles = brokenTackles;
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
