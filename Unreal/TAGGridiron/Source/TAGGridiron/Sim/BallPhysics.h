// A football as a rigid body: a prolate spheroid (the NFL ball, 11 in long, 22 in round the middle, 14-15 oz) with
// an orientation, a spin, drag, gravity and contact with the turf at the point of its surface that touches it. The
// contact point is off the centre whenever the ball comes down on a nose or a shoulder, so the impulse there turns it
// over and sends it off at an angle: the erratic bounce of a real football falls out of the shape, nothing is rolled.
// Units: yards, seconds, z up (the sim's frame). Deterministic: no random numbers in here.
#pragma once

#include <cmath>

#include "Vec.h"

namespace tag {

struct Quat {
  double w = 1, x = 0, y = 0, z = 0;
};

inline Quat qmul(const Quat& a, const Quat& b) {
  return Quat{a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat qnorm(const Quat& q) {
  const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  return n > 1e-12 ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{};
}
inline Quat qconj(const Quat& q) { return Quat{q.w, -q.x, -q.y, -q.z}; }
inline V3 qrot(const Quat& q, const V3& v) {
  const Quat p{0, v.x, v.y, v.z};
  const Quat r = qmul(qmul(q, p), qconj(q));
  return V3{r.x, r.y, r.z};
}
inline Quat qaxis(const V3& axis, double ang) {
  const double l = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
  if (l < 1e-12) return Quat{};
  const double s = std::sin(ang / 2) / l;
  return Quat{std::cos(ang / 2), axis.x * s, axis.y * s, axis.z * s};
}
/** The rotation taking the unit x axis onto `dir` with a roll of `roll` about it (the ball's long axis is its x). */
inline Quat qalong(const V3& dir, double roll) {
  const double l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (l < 1e-9) return qaxis(V3{1, 0, 0}, roll);
  const double yaw = std::atan2(dir.y, dir.x);
  const double pitch = std::atan2(dir.z, std::sqrt(dir.x * dir.x + dir.y * dir.y));
  // yaw about z, then pitch nose up (about -y in a right-handed z-up frame), then roll about the long axis
  return qmul(qmul(qaxis(V3{0, 0, 1}, yaw), qaxis(V3{0, -1, 0}, pitch)), qaxis(V3{1, 0, 0}, roll));
}

inline V3 v3add(const V3& a, const V3& b) { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 v3sub(const V3& a, const V3& b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
inline double v3dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 v3cross(const V3& a, const V3& b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double v3len(const V3& a) { return std::sqrt(v3dot(a, a)); }

struct BallBody {
  // NFL ball: 11 in tip to tip (half 0.153 yd), 22 in round the middle (diameter 7.0 in, half 0.097 yd), 0.41 kg.
  // Inertia of a leather shell over a bladder sits between a solid and a thin shell: about the long axis 0.5 m b^2,
  // across it 0.25 m (a^2 + b^2). Mass is 1 here (only ratios matter to the motion).
  static constexpr double A = 0.153;
  static constexpr double B = 0.097;
  static constexpr double IAxis = 0.5 * B * B;
  static constexpr double ITrans = 0.25 * (A * A + B * B);
  static constexpr double Restitution = 0.48;  // natural grass / turf, measured drop tests of a ball on its belly ~0.45-0.55
  static constexpr double Friction = 0.55;     // leather on turf
  static constexpr double Drag = 0.0036;       // quadratic, per yard (a tumbling ball; a spiral flies on the sim's own drag)

  V3 pos;
  V3 vel;
  Quat q;       // body -> world; the long axis is the body's x
  V3 w;         // angular velocity, world frame (rad/s)
  bool rest = false;
  int bounces = 0;
  double lastBounceT = -1;  // time of the last hard contact (s, the body's own clock)
  double clock = 0;

  V3 axis() const { return qrot(q, V3{1, 0, 0}); }

  /** World inverse inertia applied to a vector: R I^-1 R^T v. */
  V3 invI(const V3& v) const {
    const V3 b = qrot(qconj(q), v);
    const V3 r{b.x / IAxis, b.y / ITrans, b.z / ITrans};
    return qrot(q, r);
  }

  /** The point of the surface lowest along -z, relative to the centre (support point of the ellipsoid). */
  V3 lowPoint() const {
    const V3 u = axis();
    const V3 d{0, 0, -1};
    const double ud = v3dot(u, d);
    const double k = A * A - B * B;
    const V3 md{B * B * d.x + k * ud * u.x, B * B * d.y + k * ud * u.y, B * B * d.z + k * ud * u.z};
    const double s = std::sqrt((std::max)(1e-12, B * B + k * ud * ud));
    return V3{md.x / s, md.y / s, md.z / s};
  }

  /** Height of the centre when the ball lies at rest on its belly. */
  static double restZ() { return B; }

  /** Free rotation only (the centre flown elsewhere: a tipped pass on the sim's own flight). */
  void turn(double dt) {
    const int n = 4;
    for (int i = 0; i < n; i++) rotate(dt / n);
  }

  /** Lift the centre so no part of the ball is under the turf (a ball handed over from a flight that reached z = 0). */
  void clearTurf() {
    const double lo = pos.z + lowPoint().z;
    if (lo < 0) pos.z -= lo;
  }

  void step(double dt, double g) {
    if (rest) return;
    const int n = 8;
    const double h = dt / n;
    for (int i = 0; i < n && !rest; i++) sub(h, g);
  }

 private:
  void sub(double h, double g) {
    clock += h;
    // gravity + drag on the centre
    const double sp = v3len(vel);
    vel.z -= g * h;
    if (sp > 1e-6) {
      const double k = (std::min)(0.5, Drag * sp * h);
      vel = V3{vel.x * (1 - k), vel.y * (1 - k), vel.z * (1 - k)};
    }
    pos = v3add(pos, V3{vel.x * h, vel.y * h, vel.z * h});
    rotate(h);
    // turf contact at the surface point lowest down
    contact(h);
  }

  void rotate(double h) {
    // free rotation: torque-free Euler equations in the body frame (a spiral stays a spiral, a wobble precesses)
    {
      V3 wb = qrot(qconj(q), w);
      const V3 L{IAxis * wb.x, ITrans * wb.y, ITrans * wb.z};
      const V3 c = v3cross(wb, L);
      wb = V3{wb.x - h * c.x / IAxis, wb.y - h * c.y / ITrans, wb.z - h * c.z / ITrans};
      w = qrot(q, wb);
      const Quat wq{0, w.x, w.y, w.z};
      const Quat dq = qmul(wq, q);
      q = qnorm(Quat{q.w + 0.5 * h * dq.w, q.x + 0.5 * h * dq.x, q.y + 0.5 * h * dq.y, q.z + 0.5 * h * dq.z});
    }
  }

  void contact(double h) {
    const V3 r = lowPoint();
    const double depth = -(pos.z + r.z);
    if (depth <= 0) return;
    pos.z += depth;
    const V3 nrm{0, 0, 1};
    const V3 vc = v3add(vel, v3cross(w, r));
    const double vn = v3dot(vc, nrm);
    if (vn < 0) {
      // normal impulse (restitution dies away on slow contacts: a ball settling does not chatter)
      const double e = vn < -0.6 ? Restitution : Restitution * (std::max)(0.0, (-vn - 0.15) / 0.45);
      const V3 rn = v3cross(r, nrm);
      const double kn = 1 + v3dot(nrm, v3cross(invI(rn), r));
      const double jn = -(1 + e) * vn / kn;
      vel = v3add(vel, V3{nrm.x * jn, nrm.y * jn, nrm.z * jn});
      w = v3add(w, invI(V3{rn.x * jn, rn.y * jn, rn.z * jn}));
      if (-vn > 0.8) {
        if (clock - lastBounceT > 0.08) bounces++;
        lastBounceT = clock;
      }
      // friction at the contact (Coulomb): the tangential slip there is what turns a nose-first landing into a hop
      const V3 vc2 = v3add(vel, v3cross(w, r));
      const V3 vt = v3sub(vc2, V3{nrm.x * v3dot(vc2, nrm), nrm.y * v3dot(vc2, nrm), nrm.z * v3dot(vc2, nrm)});
      const double vtl = v3len(vt);
      if (vtl > 1e-6) {
        const V3 t{vt.x / vtl, vt.y / vtl, vt.z / vtl};
        const V3 rt = v3cross(r, t);
        const double kt = 1 + v3dot(t, v3cross(invI(rt), r));
        double jt = -vtl / kt;
        const double lim = Friction * jn;
        if (jt < -lim) jt = -lim;
        vel = v3add(vel, V3{t.x * jt, t.y * jt, t.z * jt});
        w = v3add(w, invI(V3{rt.x * jt, rt.y * jt, rt.z * jt}));
      }
    }
    // resting on the turf: rolling resistance of grass, and the ball comes to rest on its belly. Rolling on the belly
    // (about the long axis) is easy; rocking end over end digs a nose into the grass and dies quickly.
    {
      const V3 u = axis();
      const double wa = v3dot(w, u);
      const V3 wAx{u.x * wa, u.y * wa, u.z * wa};
      const V3 wPerp = v3sub(w, wAx);
      const double da = (std::max)(0.0, 1 - 1.6 * h), dp = (std::max)(0.0, 1 - 7.0 * h);
      w = V3{wAx.x * da + wPerp.x * dp, wAx.y * da + wPerp.y * dp, wAx.z * da + wPerp.z * dp};
      const double ld = (std::max)(0.0, 1 - 1.4 * h);
      vel.x *= ld;
      vel.y *= ld;
      if (v3len(vel) < 0.08 && v3len(w) < 0.6 && std::fabs(axis().z) < 0.25) {
        rest = true;
        vel = V3{};
        w = V3{};
        // lie flat: the long axis levelled by the least turn (the laces stay where they came to rest), the centre at
        // the belly's height
        const V3 flat{u.x, u.y, 0};
        const double fl = v3len(flat);
        if (fl > 1e-6) {
          const V3 f{flat.x / fl, flat.y / fl, 0};
          const V3 ax = v3cross(u, f);
          const double s = v3len(ax), c = v3dot(u, f);
          if (s > 1e-9) q = qnorm(qmul(qaxis(ax, std::atan2(s, c)), q));
        }
        pos.z = B;
      }
    }
  }
};

}  // namespace tag
