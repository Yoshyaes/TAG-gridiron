// Verification-only reference math (see refmath_setup.ts). Compiled WITHOUT RefMathFI.h.
#include <cmath>
namespace std {
static const double LN2 = 0.6931471805599453, TWO_PI = 6.283185307179586, PI_ = 3.141592653589793, HALF_PI = 1.5707963267948966;
double refm_exp(double x) {
  if (x != x) return NAN; if (x > 709.7) return INFINITY; if (x < -745) return 0;
  const double k = std::floor(x / LN2 + 0.5); const double r = x - k * LN2;
  double term = 1, sum = 1; for (int i = 1; i < 24; i++) { term = term * r / i; sum += term; }
  double p = sum; if (k > 0) for (int i = 0; i < k; i++) p *= 2; else for (int i = 0; i < -k; i++) p /= 2;
  return p;
}
double refm_log(double x) {
  if (x != x || x < 0) return NAN; if (x == 0) return -INFINITY; if (x == INFINITY) return INFINITY;
  double e = 0, m = x; while (m >= 2) { m /= 2; e++; } while (m < 1) { m *= 2; e--; }
  if (m > 1.4142135623730951) { m /= 2; e++; }
  const double s = (m - 1) / (m + 1), s2 = s * s; double term = s, sum = 0;
  for (int i = 0; i < 40; i++) { sum += term / (2 * i + 1); term *= s2; }
  return 2 * sum + e * LN2;
}
static double red(double x) { const double k = std::floor(x / TWO_PI + 0.5); return x - k * TWO_PI; }
double refm_sin(double x) {
  if (!std::isfinite(x)) return NAN; const double r = red(x); const double r2 = r * r; double term = r, sum = r;
  for (int i = 1; i < 30; i++) { term = -term * r2 / ((2 * i) * (2 * i + 1)); sum += term; } return sum;
}
double refm_cos(double x) {
  if (!std::isfinite(x)) return NAN; const double r = red(x); const double r2 = r * r; double term = 1, sum = 1;
  for (int i = 1; i < 30; i++) { term = -term * r2 / ((2 * i - 1) * (2 * i)); sum += term; } return sum;
}
static double ratan(double z) {
  const bool neg = z < 0; if (neg) z = -z; bool inv = false; if (z > 1) { z = 1 / z; inv = true; }
  int halv = 0; while (z > 0.2) { z = z / (1 + std::sqrt(1 + z * z)); halv++; }
  const double z2 = z * z; double term = z, sum = 0;
  for (int i = 0; i < 30; i++) { sum += term / (2 * i + 1); term *= -z2; }
  double a = sum; for (int i = 0; i < halv; i++) a *= 2; if (inv) a = HALF_PI - a; return neg ? -a : a;
}
double refm_atan2(double y, double x) {
  if (y != y || x != x) return NAN;
  if (x > 0) return ratan(y / x);
  if (x < 0) return y >= 0 ? ratan(y / x) + PI_ : ratan(y / x) - PI_;
  if (y > 0) return HALF_PI; if (y < 0) return -HALF_PI; return 0;
}
}
