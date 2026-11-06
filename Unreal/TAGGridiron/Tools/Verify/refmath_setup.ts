// Verification-only: swaps Math.exp/log/sin/cos/atan2 for deterministic reference versions
// (same algorithm and op order as RefMath.cpp) so the TS and C++ sims can be compared bit-for-bit.
// Deterministic reference transcendental functions using only + - * / floor sqrt.
const LN2 = 0.6931471805599453;
const TWO_PI = 6.283185307179586;
const PI = 3.141592653589793;
const HALF_PI = 1.5707963267948966;
function rexp(x: number): number {
  if (x !== x) return NaN;
  if (x > 709.7) return Infinity;
  if (x < -745) return 0;
  const k = Math.floor(x / LN2 + 0.5);
  const r = x - k * LN2;
  let term = 1, sum = 1;
  for (let i = 1; i < 24; i++) { term = term * r / i; sum += term; }
  let p = sum;
  if (k > 0) for (let i = 0; i < k; i++) p *= 2;
  else for (let i = 0; i < -k; i++) p /= 2;
  return p;
}
function rlog(x: number): number {
  if (x !== x || x < 0) return NaN;
  if (x === 0) return -Infinity;
  if (x === Infinity) return Infinity;
  let e = 0, m = x;
  while (m >= 2) { m /= 2; e++; }
  while (m < 1) { m *= 2; e--; }
  if (m > 1.4142135623730951) { m /= 2; e++; }
  const s = (m - 1) / (m + 1), s2 = s * s;
  let term = s, sum = 0;
  for (let i = 0; i < 40; i++) { sum += term / (2 * i + 1); term *= s2; }
  return 2 * sum + e * LN2;
}
function red(x: number): number { const k = Math.floor(x / TWO_PI + 0.5); return x - k * TWO_PI; }
function rsin(x: number): number {
  if (!isFinite(x)) return NaN;
  const r = red(x); const r2 = r * r; let term = r, sum = r;
  for (let i = 1; i < 30; i++) { term = -term * r2 / ((2 * i) * (2 * i + 1)); sum += term; }
  return sum;
}
function rcos(x: number): number {
  if (!isFinite(x)) return NaN;
  const r = red(x); const r2 = r * r; let term = 1, sum = 1;
  for (let i = 1; i < 30; i++) { term = -term * r2 / ((2 * i - 1) * (2 * i)); sum += term; }
  return sum;
}
function ratan(z: number): number {
  const neg = z < 0; if (neg) z = -z;
  let inv = false; if (z > 1) { z = 1 / z; inv = true; }
  let halv = 0;
  while (z > 0.2) { z = z / (1 + Math.sqrt(1 + z * z)); halv++; }
  const z2 = z * z; let term = z, sum = 0;
  for (let i = 0; i < 30; i++) { sum += term / (2 * i + 1); term *= -z2; }
  let a = sum; for (let i = 0; i < halv; i++) a *= 2;
  if (inv) a = HALF_PI - a;
  return neg ? -a : a;
}
function ratan2(y: number, x: number): number {
  if (y !== y || x !== x) return NaN;
  if (x > 0) return ratan(y / x);
  if (x < 0) return y >= 0 ? ratan(y / x) + PI : ratan(y / x) - PI;
  if (y > 0) return HALF_PI;
  if (y < 0) return -HALF_PI;
  return 0;
}
(Math as any).exp = rexp; (Math as any).log = rlog; (Math as any).sin = rsin; (Math as any).cos = rcos; (Math as any).atan2 = ratan2;
export {};
