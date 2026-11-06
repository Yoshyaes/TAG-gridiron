// Minimal mutable/immutable 2D vector helpers. Units are yards and seconds.
export interface V2 {
  x: number;
  y: number;
}
export const v2 = (x = 0, y = 0): V2 => ({ x, y });
export const add = (a: V2, b: V2): V2 => ({ x: a.x + b.x, y: a.y + b.y });
export const sub = (a: V2, b: V2): V2 => ({ x: a.x - b.x, y: a.y - b.y });
export const scale = (a: V2, s: number): V2 => ({ x: a.x * s, y: a.y * s });
export const len = (a: V2) => Math.hypot(a.x, a.y);
export const dist = (a: V2, b: V2) => Math.hypot(a.x - b.x, a.y - b.y);
export const dot = (a: V2, b: V2) => a.x * b.x + a.y * b.y;
export const norm = (a: V2): V2 => {
  const l = Math.hypot(a.x, a.y);
  return l > 1e-9 ? { x: a.x / l, y: a.y / l } : { x: 0, y: 0 };
};
export const fromAngle = (ang: number, l = 1): V2 => ({ x: Math.cos(ang) * l, y: Math.sin(ang) * l });
export const angleOf = (a: V2) => Math.atan2(a.y, a.x);
export const copy = (a: V2): V2 => ({ x: a.x, y: a.y });

/**
 * Earliest time a chaser (max speed `chaserSpeed`, at `from`) can meet a target moving
 * at constant velocity. Returns the intercept point; falls back to target position.
 */
export function interceptPoint(from: V2, target: V2, targetVel: V2, chaserSpeed: number, maxT = 4): V2 {
  const dx = target.x - from.x;
  const dy = target.y - from.y;
  const a = targetVel.x * targetVel.x + targetVel.y * targetVel.y - chaserSpeed * chaserSpeed;
  const b = 2 * (dx * targetVel.x + dy * targetVel.y);
  const c = dx * dx + dy * dy;
  let t = -1;
  if (Math.abs(a) < 1e-6) {
    if (Math.abs(b) > 1e-6) t = -c / b;
  } else {
    const disc = b * b - 4 * a * c;
    if (disc >= 0) {
      const s = Math.sqrt(disc);
      const t1 = (-b - s) / (2 * a);
      const t2 = (-b + s) / (2 * a);
      const cands = [t1, t2].filter((q) => q > 0);
      if (cands.length) t = Math.min(...cands);
    }
  }
  if (t < 0 || !isFinite(t)) t = maxT;
  t = Math.min(t, maxT);
  return { x: target.x + targetVel.x * t, y: target.y + targetVel.y * t };
}
