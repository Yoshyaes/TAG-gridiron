// A field goal / extra point, played (PlaySim with cfg.place): the operation, the protection, the rush, the strike.
//
// The kick unit lines up as the NFL's does - the long snapper over the ball, two guards, two tackles and two ends
// shoulder to shoulder (a foot between them), a wing a yard behind and outside each end angled out, the holder 7 yd
// deep on one knee and the kicker two steps back and two across. At the snap the line steps inside and holds its gap
// (step and hinge: nobody gives ground or chases), the wings hinge out and take the edge. The block unit rushes the
// way the NFL does: six inside to push the middle back into the kick, an edge rusher off each wing, a jumper behind the
// push who leaps at the line as the foot meets the ball, two back for a fake; some plays overload a side (the safety
// comes up as a second edge) or run a stunt inside. The rushers fight the blockers through the same engagement code
// the scrimmage plays use (PlaySim::updateEngagements: push, the shed hazard, the moves the presentation shows).
//
// The ball: snapped (the holder has it 0.42 s later, down on its point at 0.78), struck at the operation time
// (Tune::fgOpT, 1.30 s), flown from the spot with Game::placeKick's launch and drag - the same integration as its
// flyPlace, so an untouched kick is exactly the kick the game decided. It is blocked when it passes through a
// defender's reach: a man in its path with his hands up (his height x Tune::fgReach, less engaged), the jumper's leap,
// a layout across its path near the spot. A block puts it on the ground as a loose ball (BallPhysics) that either team
// may fall on or pick up and run with, as a fumble; a fingertip only takes some of it, and it flies on, slower, off line.
//
// Calibration (Tools/SimCalibrate "kicking" lines, nflverse 2023): blocked FG 1.7 % (by distance 0.4 / 1.3 / 2.4 / 2.9
// % for 18-29 / 30-39 / 40-49 / 50+), blocked PAT 0.65 %; the make rates are the game's (fgProb).
#include <algorithm>
#include <cmath>

#include "PlaySim.h"

namespace tag {

namespace {
bool hasEnt(const std::vector<Ent*>& v, const Ent* x) { return std::find(v.begin(), v.end(), x) != v.end(); }
// where the ball's path crosses the near ground: its line across the field at a distance `ahead` past the spot
V2 pathAt(const PlaceKickSpec& k, double mid, double ahead) { return V2{k.spotX + ahead, mid + ahead * k.miss / (std::max)(1.0, k.plane)}; }
}  // namespace

PlaceMan* PlaySim::pm(const Ent* e) {
  const auto it = placeMen_.find(e);
  return it == placeMen_.end() ? nullptr : &it->second;
}

const PlaceMan* PlaySim::placeMan(const Ent* e) const {
  const auto it = placeMen_.find(e);
  return it == placeMen_.end() ? nullptr : &it->second;
}

V2 PlaySim::placeAlign(bool kickUnit, size_t i, int look, double losX, double spotX, double mid) {
  if (kickUnit) {
    // K, H, LS, LG, RG, LT, RT, TE, TE2, LW, RW
    if (i == 0) return V2{spotX - 2.35, mid + 2.05};  // two steps back and two across (soccer style, from the holder's other side)
    if (i == 1) return V2{spotX + 0.05, mid - 0.62};  // on a knee a half yard to the spot's right, facing the kicker
    if (i == 2) return V2{losX - 0.4, mid};
    // shoulder to shoulder: a foot between them (centres 1.05 yd apart), the ends a hair off the ball, the wings a yard
    // and a half behind and outside the ends
    const int j = static_cast<int>(i) - 3;
    const double side = j % 2 == 0 ? 1.0 : -1.0;
    const int rank = j / 2;  // 0 guard, 1 tackle, 2 end, 3 wing
    return rank < 3 ? V2{losX - 0.55 - 0.05 * rank, mid + side * 1.05 * (rank + 1)} : V2{losX - 1.6, mid + side * 3.85};
  }
  // AL, AR, BL, BR, CL, CR, EL, ER, J, SP, FL
  const double ov = look == 1 ? 1.0 : look == 2 ? -1.0 : 0.0;  // the overloaded side
  if (i < 6) {
    // in the gaps across the line, a yard off the ball (none over the snapper: the rule since 2017)
    const double side = i % 2 == 0 ? 1.0 : -1.0;
    return V2{losX + 0.85, mid + side * (i < 2 ? 0.58 : i < 4 ? 1.58 : 2.6)};
  }
  if (i < 8) return V2{losX + 0.9, mid + (i == 6 ? 4.75 : -4.75)};  // the edges, off the wing's outside shoulder
  if (i == 8) return V2{losX + 1.7, mid + (ov != 0 ? ov : 1.0) * 1.1};  // the jumper behind the push (the overload's side)
  if (i == 9) return V2{losX + 8.5, mid};
  // the flat man: wide on the side away from an overload (a fake's first throw); in an overload, the second edge
  return ov != 0 ? V2{losX + 0.9, mid + ov * 5.7} : V2{losX + 5.0, mid - 7.0};
}

void PlaySim::setupPlace() {
  const PlaceKickSpec& k = *cfg.place;
  const double mid = by;
  auto mk = [&](Player* p, Side s, const char* role, const V2& at, PlaceJob job, double side) {
    storage.push_back(std::make_unique<Ent>(p, s, role, at));
    Ent* e = storage.back().get();
    (s == Side::Off ? off : def).push_back(e);
    PlaceMan m;
    m.job = job;
    m.side = side;
    m.home = at;
    placeMen_[e] = m;
    return e;
  };
  // ---- the kick unit (Game::placeKickSim orders it: K, H, LS, LG, RG, LT, RT, TE, TE2, LW, RW)
  static const char* const kRoles[11] = {"K", "H", "LS", "LG", "RG", "LT", "RT", "TE", "TE2", "LW", "RW"};
  for (size_t i = 0; i < cfg.offUnit.size() && i < 11; ++i) {
    Player* p = cfg.offUnit[i];
    const V2 at = placeAlign(true, i, 0, los, k.spotX, mid);
    PlaceJob job = i == 0 ? PlaceJob::Kick : i == 1 ? PlaceJob::Hold : PlaceJob::Snap;
    double side = 0;
    if (i >= 3) {
      const int j = static_cast<int>(i) - 3;  // 0 LG, 1 RG, 2 LT, 3 RT, 4 TE, 5 TE2, 6 LW, 7 RW
      side = j % 2 == 0 ? 1.0 : -1.0;
      const int rank = j / 2;
      job = rank == 0 ? PlaceJob::Guard : rank == 1 ? PlaceJob::Tackle : rank == 2 ? PlaceJob::End : PlaceJob::Wing;
    }
    Ent* e = mk(p, Side::Off, kRoles[i], at, job, side);
    if (job == PlaceJob::Kick) {
      placeKicker = e;
      e->state = EntState::Idle;
      e->face = angleOf(sub(V2{k.spotX, mid}, at));
    } else if (job == PlaceJob::Hold) {
      placeHolder = e;
      e->state = EntState::Idle;
      e->face = kPi / 2;
      e->anim = Anim::Kneel;
    } else {
      if (job == PlaceJob::Snap) placeSnapper = e;
      e->state = EntState::PassBlock;
      e->isOL = job != PlaceJob::Wing;  // (stances and get-offs: the presentation's lineman)
      if (job == PlaceJob::Wing) e->face = side * 0.6;  // angled out at the edge
      // his gap: the inside one (the snapper's both A gaps)
      PlaceMan& m = placeMen_[e];
      m.aim = job == PlaceJob::Snap ? V2{los - 0.3, mid} : job == PlaceJob::Wing ? V2{los - 0.9, mid + side * 4.6}
                                                                                : V2{los - 0.3, at.y - side * 0.52};
    }
  }
  qb = placeHolder ? placeHolder : (off.empty() ? nullptr : off[0]);
  // ---- the block unit (Game::placeKickSim orders it: the six inside heaviest first - A, B, C gaps left then right -
  // the two edges, the jumper, the spy, the flat man)
  const double r = rng.next();
  placeLook = k.look >= 0 ? k.look : r < 0.4 ? 0 : r < 0.6 ? 1 : r < 0.8 ? 2 : 3;
  const double ov = placeLook == 1 ? 1.0 : placeLook == 2 ? -1.0 : 0.0;  // the overloaded side
  static const char* const dRoles[11] = {"AL", "AR", "BL", "BR", "CL", "CR", "EL", "ER", "J", "SP", "FL"};
  for (size_t i = 0; i < cfg.defUnit.size() && i < 11; ++i) {
    Player* p = cfg.defUnit[i];
    const V2 at = placeAlign(false, i, placeLook, los, k.spotX, mid);
    PlaceJob job = PlaceJob::Push;
    double side = 0;
    const char* role = dRoles[i];
    if (i < 6) {
      side = i % 2 == 0 ? 1.0 : -1.0;
    } else if (i < 8) {
      side = i == 6 ? 1.0 : -1.0;
      job = PlaceJob::Edge;
    } else if (i == 8) {
      side = ov != 0 ? ov : 1.0;
      job = PlaceJob::Jump;
    } else if (i == 9) {
      job = PlaceJob::Spy;
    } else if (ov != 0) {
      side = ov;
      job = PlaceJob::Edge;
      role = ov > 0 ? "EL2" : "ER2";
    } else {
      side = -1.0;
      job = PlaceJob::Flat;
    }
    Ent* e = mk(p, Side::Def, role, at, job, side);
    e->isDL = job == PlaceJob::Push;
    e->state = job == PlaceJob::Spy || job == PlaceJob::Flat ? EntState::Spy : EntState::Rush;
    e->face = kPi;
    // off the ball on the snap: a beat for the eyes, quicker hands than feet (an edge rusher times the snap: Tune::fgEdgeK)
    e->getOff = k.setT + (0.05 + (100 - e->r().accel) * 0.0012 + rng.range(0, 0.05)) / (job == PlaceJob::Edge ? tune().fgEdgeK : 1.0);
    PlaceMan& m = placeMen_[e];
    if (job == PlaceJob::Push) {
      // the ball's path two yards behind the line (the middle collapsed into it), from his gap's side: the hands up
      // there get to a kick that comes off the foot low (a long one, flatter), not to one that climbs
      m.aim = pathAt(k, mid, 4.8);
      m.aim.y += side * (i < 2 ? 0.2 : 0.45);
    } else if (job == PlaceJob::Edge) {
      m.aim = pathAt(k, mid, 1.2);
      m.aim.y += side * 0.7;
    } else if (job == PlaceJob::Jump) {
      m.aim = V2{los + 0.75, mid + side * 0.95};  // (up into the seam behind the A and B gap men, timing his leap)
    } else {
      m.aim = at;
    }
    // the stunt: the B gap man crashes the A gap, the C gap man loops round behind him into the B gap
    // (on the side the look's draw gives: its top half the left, the rest the right)
    const size_t crash = r < 0.9 ? 2 : 3;
    if (placeLook == 3 && i == crash) m.aim = pathAt(k, mid, 4.6), m.aim.y += 0.15 * side;
    if (placeLook == 3 && i == crash + 2) m.stuntT = e->getOff + 0.3;
  }
  ball.mode = BallMode::Dead;  // (on the turf at the snapper until the snap: PlaySim::updatePlace carries it)
  ball.holder = nullptr;
  ball.pos = V3{los, mid, 0.12};
  ball.vel = V3{0, 0, 0};
}

void PlaySim::placeUser() {
  if (!cfg.userSide) return;
  // a human blocking it has the edge on the kick unit's left (the kicker's side), a human kicking it the holder (the
  // swing was his on the meter; nobody runs anywhere until the ball is loose)
  Ent* u = nullptr;
  if (*cfg.userSide == Side::Def) {
    for (Ent* e : def)
      if (e->role == "EL") u = e;
    if (!u && !def.empty()) u = def[0];
  } else {
    u = placeHolder;
  }
  user = u;
  if (user) user->userControlled = true;
}

void PlaySim::updatePlace(double dt) {
  const PlaceKickSpec& k = *cfg.place;
  const double mid = by;
  const double snapT = k.setT, kickT = k.setT + k.opT;
  Ball& b = ball;
  // the user's man: his leap or his layout (asked before the ball is kicked)
  for (int s = 0; s < userSlots(); ++s) {
    UserInput& in = slotInput(s);
    Ent* u = slotEnt(s);
    if (!u || u->side != Side::Def || placeKicked || placeBlocked) {
      in.blockJump = false;
      continue;
    }
    PlaceMan* m = pm(u);
    if (m && in.blockJump && t >= u->getOff && u->free() && m->jumpT < 0) {
      m->jumpT = t;
      ev("kickjump", u);
    }
    if (m && in.dive && t >= u->getOff && u->free() && u->looseDiveT < 0 && m->diveT < 0) {
      // laid out the way he is going (or at the ball's path from where he stands)
      const V2 path = pathAt(k, mid, 1.3);
      const V2 dir = u->speed() > 1.0 ? norm(u->vel) : norm(sub(path, u->pos));
      m->diveT = t;
      m->diveDir = dir;
      u->looseDiveT = 0.42;
      u->looseDiveV = scale(dir, (std::max)(4.5, u->speed() + 1.0));
      u->looseDiveAt = add(u->pos, scale(u->looseDiveV, 0.42));
      u->stun = (std::max)(u->stun, 0.47);
      u->anim = Anim::Dive;
      u->animT = 0;
      ev("kickdive", u);
    }
    in.blockJump = false;
    in.dive = false;
  }
  // a layout with the ball not loose: he lands and lies there (the loose-ball code lands its own divers)
  if (b.mode != BallMode::Loose)
    for (Ent* e : ents) {
      if (e->looseDiveT < 0) continue;
      e->looseDiveT -= dt;
      if (e->looseDiveT > 0) continue;
      e->looseDiveT = -1;
      e->stun = (std::max)(e->stun, 1.3);
      e->vel = scale(e->vel, 0.2);
      e->anim = Anim::Down;
      e->animT = 0;
    }
  // the snap
  if (!placeSnapped && t >= snapT) {
    placeSnapped = true;
    ev("snap", placeSnapper);
    // (film: the middle of the protection whiffs - Tune::kickBlock)
    // (the guard and the snapper whiff, and the A gap man on that side goes straight for where the ball will be low)
    if (k.forceBlock) {
      const double fs = placeLook == 2 ? -1.0 : 1.0;
      Ent* free1 = nullptr;
      for (Ent* d : def)
        if (d->role == (fs > 0 ? "AL" : "AR")) {
          free1 = d;
          if (PlaceMan* m = pm(d)) m->aim = add(pathAt(k, mid, 2.6), V2{0, fs * 0.2});
        }
      for (Ent* e : off) {
        const PlaceMan* m = pm(e);
        if (m && free1 && (m->job == PlaceJob::Snap || (m->job == PlaceJob::Guard && m->side == fs))) {
          // (beaten at the snap: he does not get a hand on him, and the rusher goes by him - PlaySim::collide's slip)
          e->stun = 1.6;
          e->slipT = 1.6;
          e->beatBy.push_back(free1);
        }
      }
    }
  }
  // the ball before the foot: under the snapper, back to the holder's hands, down on its point
  if (!placeKicked && !placeBlocked) {
    const V3 turf{los, mid, 0.12};
    const V3 hands{k.spotX + 0.3, mid - 0.3, 0.55};
    const V3 tee{k.spotX, mid, 0.25};
    const double tc = t - snapT;
    V3 p = turf;
    if (tc >= 0 && tc < k.catchT) {
      const double f = tc / k.catchT;
      p = V3{turf.x + (hands.x - turf.x) * f, turf.y + (hands.y - turf.y) * f, 0.35 + (hands.z - 0.35) * f + 0.25 * std::sin(f * kPi)};
    } else if (tc >= k.catchT) {
      const double f = clamp((tc - k.catchT) / (std::max)(0.05, k.placeT - k.catchT), 0, 1);
      p = V3{hands.x + (tee.x - hands.x) * f, hands.y + (tee.y - hands.y) * f, hands.z + (tee.z - hands.z) * f};
    }
    b.pos = p;
    b.vel = V3{0, 0, 0};
  }
  // the strike: flown as Game::placeKick flew it (flyPlace: its step, its drag on the speed along and up)
  if (!placeKicked && t >= kickT) {
    placeKicked = true;
    pkFly_ = true;
    pkX_ = 0;
    pkZ_ = 0.25;
    pkY0_ = mid;
    pkUx_ = k.v * std::cos(k.elevDeg * kPi / 180);
    pkUz_ = k.v * std::sin(k.elevDeg * kPi / 180);
    pkLat_ = k.miss / (std::max)(1.0, k.plane);
    b.mode = BallMode::Air;
    b.t = 0;
    b.thrower = placeKicker;
    b.holder = nullptr;
    b.target = nullptr;
    ev("fgkick", placeKicker);
  }
  if (pkFly_ && b.mode == BallMode::Air) {
    const double h = 1.0 / 60.0;
    const int n = (std::max)(1, static_cast<int>(dt / h + 0.5));
    for (int i = 0; i < n && pkFly_; ++i) {
      const double s = std::sqrt(pkUx_ * pkUx_ + pkUz_ * pkUz_);
      pkUx_ -= k.drag * s * pkUx_ * h;
      pkUz_ -= (G + k.drag * s * pkUz_) * h;
      pkX_ += pkUx_ * h;
      pkZ_ += pkUz_ * h;
      b.t += h;
      if (!placeCrossed && pkX_ >= k.plane) {
        placeCrossed = true;
        placeCrossZ = pkZ_;
        placeCrossY = pkX_ * pkLat_;
        // untouched it is the game's call; a fingertip on it, the posts' (over the bar, inside the uprights)
        if (!placeTipped) placeGood = k.good, placeShort = k.isShort;
        else placeGood = pkZ_ >= PLACE_CROSSBAR + 0.1 && std::fabs(placeCrossY) < PLACE_UPRIGHT, placeShort = pkZ_ < PLACE_CROSSBAR + 0.1;
        ev(placeGood ? "fggood" : "fgmiss", placeKicker);
        pkDoneT_ = (std::max)(t + 1.0, kickT + 2.4);
      }
      if (placeCrossed && pkX_ - k.plane > 2.5 && pkUx_ > 4) pkUx_ *= 0.08, pkUz_ *= 0.3;  // (the net behind the posts)
      if (pkUz_ < 0 && pkZ_ <= 0) {
        // down: short of the plane (no good), or in the net
        pkZ_ = 0.1;
        pkFly_ = false;
        if (!placeCrossed) {
          placeShort = true;
          placeGood = false;
          placeLandX = k.spotX + pkX_;
          ev("fgmiss", placeKicker, nullptr, "short");
          pkDoneT_ = (std::max)(t + 0.9, kickT + 2.0);
        }
      }
    }
    b.pos = V3{k.spotX + pkX_, pkY0_ + pkX_ * pkLat_, pkZ_};
    b.vel = V3{pkUx_, pkUx_ * pkLat_, pkUz_};
    b.spin += dt * 25;  // (end over end: the presentation's)
    if (!pkFly_) {
      b.mode = BallMode::Dead;
      b.vel = V3{0, 0, 0};
    }
    // a hand in its path near the line
    if (pkFly_ && !placeBlocked && !placeTipped && pkX_ < 10) placeBlockCheck();
  }
  // the play is over: the kick in the net or on the turf (and the reaction to it), or a blocked ball nobody fell on
  if (!placeBlocked && pkDoneT_ > 0 && t >= pkDoneT_) {
    endPlay("kick");
    return;
  }
  if (placeBlocked && b.mode == BallMode::Loose && t - placeBlockT > 6.0) endPlay("dead");
}

bool PlaySim::placeBlockCheck() {
  const Ball& b = ball;
  Ent* best = nullptr;
  bool tip = false;
  double bestOver = -1;
  for (Ent* d : def) {
    if (d->state == EntState::Down) continue;
    const PlaceMan* m = placeMan(d);
    const double flat = jsHypot(b.pos.x - d->pos.x, b.pos.y - d->pos.y);
    if (flat > 2.2) continue;
    if (d->looseDiveT >= 0 || (m && m->diveT >= 0 && t - m->diveT < 0.6)) {
      // laid out across it: his body and arms along the dive, a half yard to a yard off the turf
      const V2 dir = m && m->diveT >= 0 ? m->diveDir : norm(d->looseDiveV);
      const V2 rel{b.pos.x - d->pos.x, b.pos.y - d->pos.y};
      const double along = clamp(dot(rel, dir), -0.6, 1.0);
      const double off2 = len(sub(rel, scale(dir, along)));
      if (off2 < 0.42 && b.pos.z < 1.35 && b.pos.z > 0.1) {
        best = d;
        tip = off2 > 0.38 || b.pos.z > 1.25;  // (the body laid across it blocks it; the hands at the end of the dive tip it)
        break;
      }
      continue;
    }
    // hands up: as high as he reaches (less fighting a block), the jumper's leap on top; as wide as his arms spread
    const double hYd = (d->p ? d->p->heightIn : 74.0) / 36.0;
    double reach = hYd * tune().fgReach * (d->engaged ? (d->linePush < -0.4 ? 0.88 : 0.94) : 1.0);
    if (m && m->jumpT >= 0) {
      const double f = (t - m->jumpT) / 0.55;
      if (f >= 0 && f <= 1) reach += tune().fgJump * 4 * f * (1 - f);
    }
    const double arms = d->engaged ? 0.38 : 0.5;
    if (flat > arms || b.pos.z > reach || b.pos.z < 0.15) continue;
    const double over = reach - b.pos.z;
    if (over > bestOver) {
      bestOver = over;
      best = d;
      // (the fingertips: the last inch of his reach, the edge of his hands - a ball into his body below the shoulders is
      // simply blocked)
      tip = b.pos.z > 0.8 * hYd && (over < 0.04 || flat > 0.9 * arms);
    }
  }
  if (!best) return false;
  placeTouch(best, tip);
  return true;
}

void PlaySim::placeTouch(Ent* d, bool tip) {
  Ball& b = ball;
  placeBlockBy = d;
  placeBlockT = t;
  placeBlockX = pkX_;
  if (tip) {
    // fingertips: it flies on with some of its pace gone and off its line, end over end
    placeTipped = true;
    pkUx_ *= rng.range(0.72, 0.9);
    pkUz_ = pkUz_ * rng.range(0.75, 0.95) + rng.range(0.0, 1.2);
    pkLat_ += rng.range(-0.06, 0.06);
    b.tumble = true;
    ev("kicktip", d);
    return;
  }
  // off the hands: it comes back off them - up, down, sideways - and it is a football on the turf from there (live for
  // both teams: the defense may run it back, the kick unit may fall on it)
  placeBlocked = true;
  pkFly_ = false;
  pkDoneT_ = -1;
  const V3 in{pkUx_, pkUx_ * pkLat_, pkUz_};
  b.mode = BallMode::Loose;
  b.holder = nullptr;
  b.t = 0;
  b.vel = V3{in.x * rng.range(-0.35, 0.1), in.y * 0.3 + rng.range(-3.5, 3.5), in.z * rng.range(-0.25, 0.3) + rng.range(0.5, 3.5)};
  BallBody& bb = b.body;
  bb = BallBody{};
  bb.pos = b.pos;
  bb.vel = b.vel;
  bb.q = qalong(in, b.spin);
  bb.w = V3{rng.range(-16, 16), rng.range(-16, 16), rng.range(-9, 9)};
  bb.clearTurf();
  b.phys = true;
  b.tumble = true;
  ev("kickblock", d);
  // everyone after it: the blocks break up, the rush turns round, the kick unit comes back for it (the loose-ball code
  // from here: PlaySim::updateBall, the men going for it, a scoop and a run, a dive on it)
  for (Ent* e : ents) {
    if (e->engaged) disengage(e);
    e->engagedBy.clear();
    e->linePush = 0;
    if (e->state != EntState::Down) e->state = EntState::Pursue;
    PlaceMan* m = pm(e);
    // who sees it first: the rush, running at it; the holder and the kicker, a beat; the kick unit's line has its back
    // to it and turns round
    const bool line = e->side == Side::Off && m && m->job != PlaceJob::Hold && m->job != PlaceJob::Kick;
    // (nflverse 2023: of 19 blocked field goals the defense came up with 11, the kick unit 2)
    const double react = e->side == Side::Def ? rng.range(0.1, 0.25) : line ? rng.range(0.7, 1.1) : rng.range(0.55, 0.85);
    if (e->looseDiveT < 0) e->stun = (std::max)(e->stun, react);
    if (m && m->jumpT >= 0 && t - m->jumpT < 0.55) e->stun = (std::max)(e->stun, 0.55 - (t - m->jumpT));  // (lands first)
  }
}

bool PlaySim::thinkPlace(Ent* e) {
  if (placeBlocked) return false;  // a loose ball: everyone's own (Pursue, the loose-ball code)
  const PlaceKickSpec& k = *cfg.place;
  PlaceMan* m = pm(e);
  if (!m) return false;
  const double mid = by;
  const double snapT = k.setT, kickT = k.setT + k.opT;
  const bool operation = m->job == PlaceJob::Snap || m->job == PlaceJob::Hold || m->job == PlaceJob::Kick;
  // a human's man is his own once the ball is snapped and he is off (the operation's three are the play's)
  if (e->userControlled && !operation && t >= e->getOff && len(inputOf(e).move) > 0.05) return false;
  if (t < snapT || (e->side == Side::Def && t < e->getOff)) {
    e->want = V2{0, 0};
    return true;
  }
  const bool after = placeKicked;
  const double since = placeKicked ? t - kickT : -1;
  // ---- the operation
  if (m->job == PlaceJob::Hold) {
    e->want = V2{0, 0};
    return true;
  }
  if (m->job == PlaceJob::Kick) {
    // still until his approach (two steps: three quarters of a second), onto the ball at the foot, through it after
    const V2 plant{k.spotX - 0.45, mid + 0.35};
    if (!after) {
      const double left = kickT - t;
      if (left > 0.75) e->want = V2{0, 0};
      else {
        const V2 to = sub(plant, e->pos);
        e->want = scale(norm(to), (std::min)(e->vmax, len(to) / (std::max)(0.05, left)));
      }
    } else if (since < 0.5) {
      e->want = scale(V2{1, -0.2}, (std::max)(0.0, 3.0 * (1 - since / 0.5)));
    } else {
      e->want = V2{0, 0};
    }
    return true;
  }
  // ---- the protection: step inside, hold the gap, hinge at the wing; off the blocks once the ball is gone
  if (e->side == Side::Off) {
    const double react = snapT + (m->job == PlaceJob::Snap ? 0.0 : 0.06);
    if (t < react) {
      e->want = V2{0, 0};
      return true;
    }
    if (after && since > 0.4) {
      // released: a few steps up the field after it (a short kick is still a ball in play), then watching it
      if (e->engaged) disengage(e);
      const double go = clamp(1 - (since - 0.4) / 1.0, 0, 1);
      e->want = V2{2.4 * go, 0};
      return true;
    }
    if (e->engaged) return true;
    const bool wing = m->job == PlaceJob::Wing;
    // (the wing: the outside foot out and the hips turned to the edge - a hinge, not a retreat)
    const V2 set = wing ? add(m->home, V2{-0.2, m->side * 0.4}) : add(m->home, V2{-0.15, -m->side * 0.28});
    const V2 gap = m->aim;
    // who: the rusher coming at his gap (inside out), else the man on him; a free rusher nobody has before one already
    // blocked; never one who has beaten him
    Ent* best = nullptr;
    double bs = 1e9;
    for (Ent* d : def) {
      const PlaceMan* dm = placeMan(d);
      if (!dm || !d->free() || d->state == EntState::Down || hasEnt(e->beatBy, d)) continue;
      if (dm->job != PlaceJob::Push && dm->job != PlaceJob::Edge && dm->job != PlaceJob::Jump) continue;
      const V2 ahead = add(d->pos, scale(d->vel, 0.25));
      const double across = std::fabs(ahead.y - gap.y);
      const double far2 = (std::max)(0.0, ahead.x - (los + 1.3));
      // (his gap before his neighbour's man: the gap is the job, a double on a man in it is fine. The snapper has his
      // head down: only a man straight at him - the jumper stepping up - is his)
      const double s = across + 0.6 * far2 + (d->engagedBy.empty() ? 0.0 : 0.5);
      const double lane = wing ? 2.8 : m->job == PlaceJob::Snap ? 0.35 : 1.2;
      if (s < bs && across < lane && dist(d->pos, e->pos) < (wing ? 4.0 : 2.4)) {
        bs = s;
        best = d;
      }
    }
    m->target = best;
    V2 to = set;
    if (best) {
      // into his path to the ball, from the gap (never more than a step off the set)
      const V2 inPath = add(best->pos, scale(norm(sub(V2{k.spotX + 1.5, mid}, best->pos)), 0.55));
      const V2 off2 = sub(inPath, set);
      const double cap = wing ? 1.3 : 0.6;
      to = len(off2) > cap ? add(set, scale(norm(off2), cap)) : inPath;
      if (dist(best->pos, e->pos) < e->radius + best->radius + 0.45 && !after) engage(e, best);
    }
    const V2 d2 = sub(to, e->pos);
    e->want = scale(norm(d2), (std::min)(wing ? 4.5 : 3.0, len(d2) * 6));
    return true;
  }
  // ---- the rush
  if (m->job == PlaceJob::Spy || m->job == PlaceJob::Flat) {
    // eyes on the holder for a fake: the spy settles at 7 yd over the ball, the flat man creeps in toward the wing
    const V2 to = m->job == PlaceJob::Spy ? V2{los + 5.5, mid + 0.8} : add(m->home, V2{-0.9, -m->side * 1.4});
    const V2 d2 = sub(to, e->pos);
    e->want = after && since > 0.6 ? V2{0, 0} : scale(norm(d2), (std::min)(2.2, len(d2) * 3));
    return true;
  }
  if (after) {
    // the ball is gone: the hands stay up a beat, then they stop and watch it; once it is through some of them walk off
    if (e->engaged) return true;
    if (since < 0.3) e->want = scale(e->vel, 0.6);
    else if (placeCrossed && (e->p ? e->p->number : 0) % 2 == 1) {
      const double sideY = jsSign(orIfZero(e->pos.y - mid, m->side != 0 ? m->side : 1.0));
      e->want = V2{0.4, sideY * 1.5};
    } else e->want = V2{0, 0};
    return true;
  }
  if (e->engaged) return true;
  if (m->jumpT >= 0 && t - m->jumpT < 0.6) {
    e->want = V2{0, 0};  // (in the air)
    return true;
  }
  // the jumper: up into the line behind the push, and off his feet as the foot meets the ball (the top of his leap when
  // the ball gets to the line)
  if (m->job == PlaceJob::Jump && m->jumpT < 0 && !e->userControlled && t >= kickT - 0.02) {
    m->jumpT = t;
    e->vel = scale(e->vel, 0.3);
    ev("kickjump", e);
    e->want = V2{0, 0};
    return true;
  }
  // a man loose near the spot as the ball is struck lays out across its path (two yards out at most: the dive's length)
  if (!e->userControlled && m->diveT < 0 && e->looseDiveT < 0 && t >= kickT - 0.3 && t <= kickT + 0.12) {
    const V2 path = pathAt(k, mid, 1.3);
    const double dd = dist(e->pos, path);
    if (dd < 2.6 && dd > 0.4) {
      const V2 dir = norm(sub(path, e->pos));
      m->diveT = t;
      m->diveDir = dir;
      const double T = clamp(0.25 + dd / 6.0, 0.3, 0.6);
      e->looseDiveT = T;
      e->looseDiveV = scale(dir, (std::min)(e->vmax, (std::max)(4.5, dd / T)));
      e->looseDiveAt = add(e->pos, scale(e->looseDiveV, T));
      e->stun = (std::max)(e->stun, T + 0.05);
      e->anim = Anim::Dive;
      e->animT = 0;
      ev("kickdive", e);
      return true;
    }
  }
  // the stunt's looper: a jab step, then round behind the crasher into his gap
  V2 aim = m->aim;
  if (m->stuntT > 0) {
    if (t < m->stuntT) {
      e->want = V2{-0.6, 0};
      return true;
    }
    aim = pathAt(k, mid, 4.8);
    aim.y += m->side * 0.6;
  }
  // an edge rusher: round the corner of the wing's hinge first (a yard behind the line, outside him), then flat at the
  // spot the ball comes off the foot
  if (m->job == PlaceJob::Edge && m->stage == 0) {
    const V2 corner{los - 1.3, mid + m->side * 4.3};
    if (e->pos.x < los - 0.7 || dist(e->pos, corner) < 0.9) m->stage = 1;
    else aim = corner;
  }
  // a man through the line: on at the kick's path, collapsing the pocket toward the spot (where the ball is lower)
  if (m->job == PlaceJob::Push && m->stage == 0 && dist(e->pos, aim) < 0.5) m->stage = 1;
  if (m->job == PlaceJob::Push && m->stage == 1) {
    const V2 deep = add(pathAt(k, mid, 2.6), V2{0, m->side * 0.3});
    const V2 d2 = sub(deep, e->pos);
    e->want = len(d2) < 0.3 ? V2{0, 0} : scale(norm(d2), (std::min)(2.5, len(d2) * 4));
    return true;
  }
  // the jumper follows the push in: a step behind the A gap man on his side, over his outside shoulder
  if (m->job == PlaceJob::Jump)
    for (Ent* d : def)
      if (d->role == (m->side > 0 ? "AL" : "AR") && d->state != EntState::Down) aim = add(d->pos, V2{0.95, m->side * 0.5});
  // at the point to get to: hands up and wait for it there (the jumper rocks on his feet, timing the leap)
  const double dAim = dist(e->pos, aim);
  if (dAim < 0.35) {
    e->want = m->job == PlaceJob::Jump ? V2{0, 0.9 * std::sin((t - k.setT) * 7.0)} : V2{0, 0};
    return true;
  }
  const V2 dir = norm(sub(aim, e->pos));
  e->want = scale(dir, (std::min)(m->job == PlaceJob::Jump ? 2.8 : e->vmax, dAim * 4 + 1.0));
  return true;
}

void PlaySim::placeAfterMove() {
  if (placeBlocked) return;
  const PlaceKickSpec& k = *cfg.place;
  const double mid = by;
  const V2 bp{ball.pos.x, ball.pos.y};
  for (Ent* e : ents) {
    const PlaceMan* m = placeMan(e);
    if (!m) continue;
    if (e->looseDiveT >= 0) continue;
    const double ns = e->speed();
    if (m->job == PlaceJob::Hold) {
      e->face = kPi / 2;
      e->anim = placeKicked && t - placeKickT() > 0.6 ? (placeCrossed && placeGood ? Anim::Celebrate : Anim::Idle) : Anim::Kneel;
      continue;
    }
    if (m->job == PlaceJob::Kick) {
      if (!placeKicked) e->face = angleOf(sub(V2{k.spotX + 0.6, mid}, e->pos));
      if (placeCrossed && placeGood) e->anim = Anim::Celebrate;
      continue;
    }
    if (m->job == PlaceJob::Wing && !e->engaged && !placeKicked) e->face = m->side * 0.6;
    // watching it go: the men standing turn to the ball
    if (placeKicked && !e->engaged && ns < 1.5) e->face = angleOf(sub(bp, e->pos));
    if (placeCrossed && placeGood && e->side == Side::Off && !e->engaged && (e->p ? e->p->number : 0) % 3 == 0) e->anim = Anim::Celebrate;
  }
}

}  // namespace tag
