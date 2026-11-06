// Port of src/sim/rng.ts
// Deterministic, seedable RNG (mulberry32) so every game/play can be replayed exactly.
// Bit-identical to the TypeScript implementation: the same seed yields the same stream.
//
// Also hosts the small numeric helpers the TS code takes from rng.ts (clamp/lerp/sigmoid)
// plus JS-semantics helpers (jsRound = Math.round, jsSign = Math.sign) used across the port.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace tag {

/** Math.PI (named kPi to avoid clashing with engine-level PI macros). */
constexpr double kPi = 3.14159265358979323846;

inline double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
/** Logistic helper used throughout the rating model. */
inline double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

/** JavaScript Math.round: nearest integer, ties toward +infinity. */
inline double jsRound(double x) {
  if (!std::isfinite(x)) return x;
  double r = std::ceil(x);
  if (r - 0.5 > x) r -= 1.0;
  return r;
}
/** Math.round(x) as an int (all call sites are well inside int range). */
inline int jsRoundI(double x) { return static_cast<int>(jsRound(x)); }

/** JavaScript Math.sign (NaN -> 0 here; never hit by the sim). */
inline double jsSign(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0; }
/** JS `x || alt` for numbers: 0 / NaN are falsy. */
inline double orIfZero(double x, double alt) { return (x == 0.0 || std::isnan(x)) ? alt : x; }

class Rng {
 public:
  explicit Rng(uint32_t seed = 0) : s_(seed ? seed : 0x9e3779b9u) {}

  double next() {
    s_ += 0x6d2b79f5u;
    uint32_t t = s_;
    t = (t ^ (t >> 15)) * (t | 1u);              // Math.imul
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
  double range(double a, double b) { return a + (b - a) * next(); }
  /** TS `int(a, b)`: uniform integer in [a, b]. */
  int intRange(int a, int b) { return static_cast<int>(std::floor(range(a, static_cast<double>(b) + 1.0))); }
  bool chance(double p) { return next() < p; }
  double normal(double mean = 0, double sd = 1) {
    const double u = (std::max)(1e-12, next());
    const double v = next();
    return mean + sd * std::sqrt(-2 * std::log(u)) * std::cos(2 * kPi * v);
  }
  template <class C>
  const typename C::value_type& pick(const C& arr) {
    return arr[static_cast<size_t>(std::floor(next() * static_cast<double>(arr.size())))];
  }
  template <class T, size_t N>
  const T& pick(const T (&arr)[N]) {
    return arr[static_cast<size_t>(std::floor(next() * static_cast<double>(N)))];
  }
  template <class C, class W>
  const typename C::value_type& weighted(const C& items, const W& weights) {
    double total = 0;
    for (double w : weights) total += (std::max)(0.0, w);
    double r = next() * total;
    for (size_t i = 0; i < items.size(); i++) {
      r -= (std::max)(0.0, static_cast<double>(weights[i]));
      if (r <= 0) return items[i];
    }
    return items[items.size() - 1];
  }
  Rng fork() { return Rng(static_cast<uint32_t>(std::floor(next() * 4294967296.0))); }

  uint32_t state() const { return s_; }
  void setState(uint32_t s) { s_ = s; }

 private:
  uint32_t s_;
};

}  // namespace tag
