// Port of src/sim/vec.ts
// Minimal 2D/3D vector helpers. Units are yards and seconds.
// Free functions mirror the TS helpers (add/sub/scale/len/dist/dot/norm/...), all by value.
#pragma once

#include <cmath>

namespace tag {

struct V2 {
  double x = 0;
  double y = 0;
};

struct V3 {
  double x = 0;
  double y = 0;
  double z = 0;
};

/**
 * Math.hypot exactly as V8 computes it (max-normalised Kahan sum), so lengths are
 * bit-identical to the TypeScript build. Plain sqrt(x*x+y*y) can differ in the last ulp.
 */
inline double jsHypot(double a, double b) {
  a = std::fabs(a);
  b = std::fabs(b);
  if (std::isinf(a) || std::isinf(b)) return INFINITY;
  if (std::isnan(a) || std::isnan(b)) return NAN;
  const double mx = a > b ? a : b;
  if (mx == 0) return 0;
  // V8 loop: sum the squares of (v / mx) with Kahan compensation. With two args the larger
  // one normalises to exactly 1.0 and the compensation stays 0, so the loop reduces to
  // sqrt(1 + r*r) * mx (IEEE addition is commutative) -- bit-identical, one division.
  const double r = (a > b ? b : a) / mx;
  return std::sqrt(1 + r * r) * mx;
}
inline double jsHypot(double a, double b, double c) {
  a = std::fabs(a);
  b = std::fabs(b);
  c = std::fabs(c);
  if (std::isinf(a) || std::isinf(b) || std::isinf(c)) return INFINITY;
  if (std::isnan(a) || std::isnan(b) || std::isnan(c)) return NAN;
  double mx = a;
  if (b > mx) mx = b;
  if (c > mx) mx = c;
  if (mx == 0) return 0;
  double sum = 0, comp = 0;
  const double vals[3] = {a, b, c};
  for (double v : vals) {
    const double n = v / mx;
    const double summand = n * n - comp;
    const double prelim = sum + summand;
    comp = (prelim - sum) - summand;
    sum = prelim;
  }
  return std::sqrt(sum) * mx;
}

inline V2 v2(double x = 0, double y = 0) { return V2{x, y}; }
inline V2 add(const V2& a, const V2& b) { return V2{a.x + b.x, a.y + b.y}; }
inline V2 sub(const V2& a, const V2& b) { return V2{a.x - b.x, a.y - b.y}; }
inline V2 scale(const V2& a, double s) { return V2{a.x * s, a.y * s}; }
inline double len(const V2& a) { return jsHypot(a.x, a.y); }
inline double dist(const V2& a, const V2& b) { return jsHypot(a.x - b.x, a.y - b.y); }
inline double dot(const V2& a, const V2& b) { return a.x * b.x + a.y * b.y; }
inline V2 norm(const V2& a) {
  const double l = jsHypot(a.x, a.y);
  return l > 1e-9 ? V2{a.x / l, a.y / l} : V2{0, 0};
}
inline V2 fromAngle(double ang, double l = 1) { return V2{std::cos(ang) * l, std::sin(ang) * l}; }
inline double angleOf(const V2& a) { return std::atan2(a.y, a.x); }
inline V2 copy(const V2& a) { return a; }
inline V3 scale3(const V3& v, double s) { return V3{v.x * s, v.y * s, v.z * s}; }

/**
 * Earliest time a chaser (max speed `chaserSpeed`, at `from`) can meet a target moving
 * at constant velocity. Returns the intercept point; falls back to target position.
 */
inline V2 interceptPoint(const V2& from, const V2& target, const V2& targetVel, double chaserSpeed, double maxT = 4) {
  const double dx = target.x - from.x;
  const double dy = target.y - from.y;
  const double a = targetVel.x * targetVel.x + targetVel.y * targetVel.y - chaserSpeed * chaserSpeed;
  const double b = 2 * (dx * targetVel.x + dy * targetVel.y);
  const double c = dx * dx + dy * dy;
  double t = -1;
  if (std::fabs(a) < 1e-6) {
    if (std::fabs(b) > 1e-6) t = -c / b;
  } else {
    const double disc = b * b - 4 * a * c;
    if (disc >= 0) {
      const double s = std::sqrt(disc);
      const double t1 = (-b - s) / (2 * a);
      const double t2 = (-b + s) / (2 * a);
      bool any = false;
      double best = 0;
      if (t1 > 0) { best = t1; any = true; }
      if (t2 > 0) { best = any ? (best < t2 ? best : t2) : t2; any = true; }
      if (any) t = best;
    }
  }
  if (t < 0 || !std::isfinite(t)) t = maxT;
  t = t < maxT ? t : maxT;
  return V2{target.x + targetVel.x * t, target.y + targetVel.y * t};
}

}  // namespace tag
