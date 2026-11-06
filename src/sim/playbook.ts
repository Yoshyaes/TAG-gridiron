// Playbook: formations, route tree, run schemes, defensive calls.
// Offense frame: offense always attacks +x. y in [0, 53.33]; +y is the offense's LEFT.
import type { Pos } from './types';

export const FIELD_W = 160 / 3; // 53.33 yards
export const HASH_L = FIELD_W / 2 + 3.083; // NFL hashes (70'9" from sideline)
export const HASH_R = FIELD_W / 2 - 3.083;

export type Side = 'L' | 'R';
export type OffRole =
  | 'QB' | 'RB' | 'FB' | 'X' | 'Z' | 'SLOT' | 'SLOT2' | 'TE' | 'TE2'
  | 'LT' | 'LG' | 'C' | 'RG' | 'RT';

export type Align =
  | { kind: 'ol'; dy: number }
  | { kind: 'inline'; side: Side; off?: boolean }
  | { kind: 'wide'; side: Side; off?: boolean }
  | { kind: 'slot'; side: Side; n?: number }
  | { kind: 'back'; x: number; dy: number };

export interface FormationSlot {
  role: OffRole;
  pos: Pos[]; // depth chart preference
  align: Align;
}

export interface Formation {
  id: string;
  name: string;
  personnel: string;
  qbDepth: number; // yards behind LOS at snap (shotgun 5, pistol 4, under center 1)
  slots: FormationSlot[];
}

const OL: FormationSlot[] = [
  { role: 'LT', pos: ['OT'], align: { kind: 'ol', dy: 2.7 } },
  { role: 'LG', pos: ['OG'], align: { kind: 'ol', dy: 1.35 } },
  { role: 'C', pos: ['C'], align: { kind: 'ol', dy: 0 } },
  { role: 'RG', pos: ['OG'], align: { kind: 'ol', dy: -1.35 } },
  { role: 'RT', pos: ['OT'], align: { kind: 'ol', dy: -2.7 } },
];

export const FORMATIONS: Formation[] = [
  {
    id: 'gun_trips', name: 'Gun Trips TE', personnel: '11', qbDepth: 5,
    slots: [...OL,
      { role: 'QB', pos: ['QB'], align: { kind: 'back', x: -5, dy: 0 } },
      { role: 'RB', pos: ['RB'], align: { kind: 'back', x: -5, dy: 1.2 } },
      { role: 'X', pos: ['WR'], align: { kind: 'wide', side: 'L' } },
      { role: 'Z', pos: ['WR'], align: { kind: 'wide', side: 'R', off: true } },
      { role: 'SLOT', pos: ['WR'], align: { kind: 'slot', side: 'R' } },
      { role: 'TE', pos: ['TE'], align: { kind: 'inline', side: 'R' } },
    ],
  },
  {
    id: 'gun_doubles', name: 'Gun Doubles', personnel: '11', qbDepth: 5,
    slots: [...OL,
      { role: 'QB', pos: ['QB'], align: { kind: 'back', x: -5, dy: 0 } },
      { role: 'RB', pos: ['RB'], align: { kind: 'back', x: -5, dy: -1.2 } },
      { role: 'X', pos: ['WR'], align: { kind: 'wide', side: 'L' } },
      { role: 'Z', pos: ['WR'], align: { kind: 'wide', side: 'R', off: true } },
      { role: 'SLOT', pos: ['WR'], align: { kind: 'slot', side: 'L' } },
      { role: 'TE', pos: ['TE'], align: { kind: 'slot', side: 'R' } },
    ],
  },
  {
    id: 'gun_empty', name: 'Gun Empty', personnel: '10', qbDepth: 5,
    slots: [...OL,
      { role: 'QB', pos: ['QB'], align: { kind: 'back', x: -5, dy: 0 } },
      { role: 'X', pos: ['WR'], align: { kind: 'wide', side: 'L' } },
      { role: 'Z', pos: ['WR'], align: { kind: 'wide', side: 'R', off: true } },
      { role: 'SLOT', pos: ['WR'], align: { kind: 'slot', side: 'L' } },
      { role: 'SLOT2', pos: ['WR'], align: { kind: 'slot', side: 'R', n: 2 } },
      { role: 'RB', pos: ['RB'], align: { kind: 'slot', side: 'R' } },
    ],
  },
  {
    id: 'pistol', name: 'Pistol Strong', personnel: '11', qbDepth: 4,
    slots: [...OL,
      { role: 'QB', pos: ['QB'], align: { kind: 'back', x: -4, dy: 0 } },
      { role: 'RB', pos: ['RB'], align: { kind: 'back', x: -7, dy: 0 } },
      { role: 'X', pos: ['WR'], align: { kind: 'wide', side: 'L' } },
      { role: 'Z', pos: ['WR'], align: { kind: 'wide', side: 'R', off: true } },
      { role: 'SLOT', pos: ['WR'], align: { kind: 'slot', side: 'L' } },
      { role: 'TE', pos: ['TE'], align: { kind: 'inline', side: 'R' } },
    ],
  },
  {
    id: 'singleback', name: 'Singleback Ace', personnel: '12', qbDepth: 1,
    slots: [...OL,
      { role: 'QB', pos: ['QB'], align: { kind: 'back', x: -1, dy: 0 } },
      { role: 'RB', pos: ['RB'], align: { kind: 'back', x: -7, dy: 0 } },
      { role: 'X', pos: ['WR'], align: { kind: 'wide', side: 'L' } },
      { role: 'Z', pos: ['WR'], align: { kind: 'wide', side: 'R', off: true } },
      { role: 'TE', pos: ['TE'], align: { kind: 'inline', side: 'R' } },
      { role: 'TE2', pos: ['TE'], align: { kind: 'inline', side: 'L', off: true } },
    ],
  },
  {
    id: 'iform', name: 'I-Form Pro', personnel: '21', qbDepth: 1,
    slots: [...OL,
      { role: 'QB', pos: ['QB'], align: { kind: 'back', x: -1, dy: 0 } },
      { role: 'FB', pos: ['FB'], align: { kind: 'back', x: -4.5, dy: 0 } },
      { role: 'RB', pos: ['RB'], align: { kind: 'back', x: -7, dy: 0 } },
      { role: 'X', pos: ['WR'], align: { kind: 'wide', side: 'L' } },
      { role: 'Z', pos: ['WR'], align: { kind: 'wide', side: 'R', off: true } },
      { role: 'TE', pos: ['TE'], align: { kind: 'inline', side: 'R' } },
    ],
  },
  {
    id: 'heavy', name: 'Goal Line Heavy', personnel: '22', qbDepth: 1,
    slots: [...OL,
      { role: 'QB', pos: ['QB'], align: { kind: 'back', x: -1, dy: 0 } },
      { role: 'FB', pos: ['FB'], align: { kind: 'back', x: -4, dy: 0 } },
      { role: 'RB', pos: ['RB'], align: { kind: 'back', x: -6.5, dy: 0 } },
      { role: 'TE', pos: ['TE'], align: { kind: 'inline', side: 'R' } },
      { role: 'TE2', pos: ['TE'], align: { kind: 'inline', side: 'L', off: true } },
      { role: 'X', pos: ['WR'], align: { kind: 'wide', side: 'L' } },
    ],
  },
];

// ---------------- Routes ----------------
// Points are [forward, outward] yards relative to the receiver's snap position.
// "outward" points toward the receiver's sideline. `settle` = stop & work back to QB.
export interface RouteDef {
  pts: [number, number][];
  settle?: boolean;
  /** depth class for QB accuracy: short < 10, mid 10-20, deep 20+ */
  block?: boolean;
}

export const ROUTES: Record<string, RouteDef> = {
  go: { pts: [[6, 0.5], [45, 2]] },
  fade: { pts: [[4, 1.5], [40, 4]] },
  seam: { pts: [[8, -0.5], [45, -1.5]] },
  slant: { pts: [[2.5, 0], [10, -7]] },
  quickout: { pts: [[5, 0], [4.5, 8]] },
  out: { pts: [[11, 0], [10.5, 10]] },
  in: { pts: [[6, 0], [6, -14]] },
  dig: { pts: [[13, 0], [13, -18]] },
  curl: { pts: [[12, 0], [10.5, -1.2]], settle: true },
  hitch: { pts: [[6, 0], [5, -0.5]], settle: true },
  comeback: { pts: [[16, 0], [14, 3]], settle: true },
  post: { pts: [[12, 0], [38, -12]] },
  corner: { pts: [[11, 0], [26, 10]] },
  flat: { pts: [[1.5, 3], [4, 12]] },
  arrow: { pts: [[4, 6], [6, 14]] },
  wheel: { pts: [[1.5, 4], [5, 9], [40, 10]] },
  drag: { pts: [[2.5, -1], [5, -25]] },
  shallow: { pts: [[2, -1], [4, -22]] },
  cross: { pts: [[10, -2], [16, -24]] },
  sail: { pts: [[10, 0], [18, 10]] },
  stick: { pts: [[6, 0], [5.5, -0.5]], settle: true },
  swing: { pts: [[-1, 4], [1, 10]] },
  angle: { pts: [[3, 3], [6, -3]] },
  checkdown: { pts: [[2, 2], [5, 3]], settle: true },
  screen: { pts: [[-2, 3], [-2.5, 4]], settle: true },
  block: { pts: [], block: true },
};

export type PlayType = 'run' | 'pass' | 'playaction' | 'screen' | 'kneel' | 'spike';

export interface RunScheme {
  /** gap aim relative to center in yards (+ = left). */
  aimDy: number;
  /** zone (outside zone reads) vs gap (power) */
  style: 'zone' | 'gap' | 'stretch' | 'draw' | 'sneak';
  handoffDelay: number;
  carrier: OffRole;
}

export interface OffPlay {
  id: string;
  name: string;
  formation: string;
  type: PlayType;
  routes: Partial<Record<OffRole, string>>;
  /** QB read order of eligible roles */
  reads?: OffRole[];
  run?: RunScheme;
  dropDepth?: number; // extra drop yards for QB
  tags: string[]; // 'short', 'deep', 'quick', 'goalline', 'shortyard'
}

const P = (id: string, name: string, formation: string, type: PlayType, routes: Partial<Record<OffRole, string>>, reads: OffRole[], tags: string[], dropDepth = 2): OffPlay =>
  ({ id, name, formation, type, routes, reads, tags, dropDepth });
const R = (id: string, name: string, formation: string, run: RunScheme, tags: string[]): OffPlay =>
  ({ id, name, formation, type: 'run', routes: { X: 'block', Z: 'block', SLOT: 'block', SLOT2: 'block', TE: 'block', TE2: 'block', FB: 'block' }, run, tags });

export const OFF_PLAYS: OffPlay[] = [
  // ---- Gun Trips
  P('trips_flood', 'Flood', 'gun_trips', 'pass', { X: 'post', Z: 'go', SLOT: 'sail', TE: 'flat', RB: 'checkdown' }, ['SLOT', 'TE', 'X', 'RB'], ['mid']),
  P('trips_stick', 'Stick', 'gun_trips', 'pass', { X: 'slant', Z: 'go', SLOT: 'stick', TE: 'flat', RB: 'checkdown' }, ['SLOT', 'TE', 'X', 'RB'], ['short', 'quick'], 1),
  P('trips_4verts', 'Four Verticals', 'gun_trips', 'pass', { X: 'go', Z: 'go', SLOT: 'seam', TE: 'seam', RB: 'checkdown' }, ['SLOT', 'TE', 'X', 'Z', 'RB'], ['deep'], 3),
  P('trips_dagger', 'Dagger', 'gun_trips', 'pass', { X: 'curl', Z: 'dig', SLOT: 'seam', TE: 'shallow', RB: 'checkdown' }, ['Z', 'X', 'TE', 'RB'], ['mid'], 3),
  R('trips_iz', 'Inside Zone', 'gun_trips', { aimDy: -0.7, style: 'zone', handoffDelay: 0.65, carrier: 'RB' }, ['run']),
  R('trips_draw', 'HB Draw', 'gun_trips', { aimDy: 0.6, style: 'draw', handoffDelay: 0.9, carrier: 'RB' }, ['run', 'long']),
  { id: 'trips_screen', name: 'HB Screen', formation: 'gun_trips', type: 'screen', routes: { X: 'go', Z: 'go', SLOT: 'in', TE: 'block', RB: 'screen' }, reads: ['RB'], tags: ['screen', 'short'], dropDepth: 4 },
  // ---- Gun Doubles
  P('dbl_mesh', 'Mesh', 'gun_doubles', 'pass', { X: 'corner', Z: 'dig', SLOT: 'drag', TE: 'shallow', RB: 'swing' }, ['SLOT', 'TE', 'RB', 'Z'], ['short']),
  P('dbl_smash', 'Smash', 'gun_doubles', 'pass', { X: 'hitch', Z: 'hitch', SLOT: 'corner', TE: 'corner', RB: 'checkdown' }, ['SLOT', 'X', 'TE', 'Z', 'RB'], ['mid']),
  P('dbl_slants', 'Slant Flat', 'gun_doubles', 'pass', { X: 'slant', Z: 'slant', SLOT: 'flat', TE: 'flat', RB: 'checkdown' }, ['X', 'SLOT', 'Z', 'TE', 'RB'], ['short', 'quick'], 1),
  P('dbl_curls', 'Curl Flat', 'gun_doubles', 'pass', { X: 'curl', Z: 'curl', SLOT: 'arrow', TE: 'flat', RB: 'checkdown' }, ['X', 'SLOT', 'Z', 'TE', 'RB'], ['mid']),
  P('dbl_postwheel', 'Post Wheel', 'gun_doubles', 'pass', { X: 'post', Z: 'out', SLOT: 'wheel', TE: 'dig', RB: 'checkdown' }, ['X', 'SLOT', 'TE', 'Z', 'RB'], ['deep'], 3),
  P('dbl_spacing', 'Spacing', 'gun_doubles', 'pass', { X: 'hitch', Z: 'quickout', SLOT: 'stick', TE: 'hitch', RB: 'flat' }, ['SLOT', 'TE', 'X', 'Z', 'RB'], ['short', 'quick'], 1),
  R('dbl_oz', 'Outside Zone', 'gun_doubles', { aimDy: 4.5, style: 'stretch', handoffDelay: 0.7, carrier: 'RB' }, ['run']),
  R('dbl_iz', 'Inside Zone', 'gun_doubles', { aimDy: 0.7, style: 'zone', handoffDelay: 0.65, carrier: 'RB' }, ['run']),
  // ---- Empty
  P('empty_quick', 'Quick Outs', 'gun_empty', 'pass', { X: 'quickout', Z: 'quickout', SLOT: 'stick', SLOT2: 'slant', RB: 'hitch' }, ['SLOT', 'X', 'Z', 'SLOT2', 'RB'], ['short', 'quick'], 1),
  P('empty_ycross', 'Y Cross', 'gun_empty', 'pass', { X: 'post', Z: 'curl', SLOT: 'cross', SLOT2: 'shallow', RB: 'flat' }, ['SLOT', 'Z', 'SLOT2', 'RB'], ['mid', 'long'], 3),
  P('empty_verts', 'Spread Verts', 'gun_empty', 'pass', { X: 'go', Z: 'comeback', SLOT: 'seam', SLOT2: 'seam', RB: 'hitch' }, ['SLOT', 'SLOT2', 'X', 'Z', 'RB'], ['deep', 'long'], 3),
  // ---- Pistol
  R('pistol_iz', 'Inside Zone', 'pistol', { aimDy: -0.7, style: 'zone', handoffDelay: 0.75, carrier: 'RB' }, ['run']),
  R('pistol_power', 'Power', 'pistol', { aimDy: 2.0, style: 'gap', handoffDelay: 0.8, carrier: 'RB' }, ['run', 'shortyard']),
  { id: 'pistol_pa', name: 'PA Boot', formation: 'pistol', type: 'playaction', routes: { X: 'post', Z: 'cross', SLOT: 'corner', TE: 'flat', RB: 'block' }, reads: ['TE', 'Z', 'SLOT', 'X'], tags: ['mid', 'playaction'], dropDepth: 3 },
  P('pistol_slants', 'Double Slants', 'pistol', 'pass', { X: 'slant', Z: 'go', SLOT: 'slant', TE: 'flat', RB: 'checkdown' }, ['SLOT', 'X', 'TE', 'RB'], ['short', 'quick'], 1),
  // ---- Singleback
  R('ace_iz', 'Inside Zone', 'singleback', { aimDy: -0.7, style: 'zone', handoffDelay: 0.85, carrier: 'RB' }, ['run']),
  R('ace_counter', 'Counter', 'singleback', { aimDy: 2.6, style: 'gap', handoffDelay: 1.0, carrier: 'RB' }, ['run']),
  R('ace_stretch', 'Stretch', 'singleback', { aimDy: -5, style: 'stretch', handoffDelay: 0.8, carrier: 'RB' }, ['run']),
  { id: 'ace_pa', name: 'PA Shot', formation: 'singleback', type: 'playaction', routes: { X: 'post', Z: 'dig', TE: 'seam', TE2: 'block', RB: 'block' }, reads: ['X', 'Z', 'TE'], tags: ['deep', 'playaction'], dropDepth: 6 },
  P('ace_curl', 'Curl Flat', 'singleback', 'pass', { X: 'curl', Z: 'curl', TE: 'flat', TE2: 'flat', RB: 'checkdown' }, ['X', 'TE', 'Z', 'TE2', 'RB'], ['mid'], 5),
  // ---- I-form
  R('i_power', 'Power O', 'iform', { aimDy: -2.0, style: 'gap', handoffDelay: 0.95, carrier: 'RB' }, ['run', 'shortyard']),
  R('i_iso', 'Iso', 'iform', { aimDy: 0.7, style: 'gap', handoffDelay: 0.9, carrier: 'RB' }, ['run', 'shortyard']),
  R('i_toss', 'Toss Crack', 'iform', { aimDy: -6, style: 'stretch', handoffDelay: 0.55, carrier: 'RB' }, ['run']),
  { id: 'i_pa', name: 'PA Crossers', formation: 'iform', type: 'playaction', routes: { X: 'post', Z: 'cross', TE: 'flat', FB: 'flat', RB: 'block' }, reads: ['Z', 'X', 'TE', 'FB'], tags: ['mid', 'playaction'], dropDepth: 6 },
  // ---- Goal line
  R('gl_dive', 'HB Dive', 'heavy', { aimDy: -0.7, style: 'gap', handoffDelay: 0.75, carrier: 'RB' }, ['run', 'shortyard', 'goalline']),
  R('gl_sneak', 'QB Sneak', 'heavy', { aimDy: 0.6, style: 'sneak', handoffDelay: 0, carrier: 'QB' }, ['run', 'shortyard', 'goalline']),
  { id: 'gl_pa', name: 'PA TE Flat', formation: 'heavy', type: 'playaction', routes: { TE: 'flat', TE2: 'corner', X: 'fade', FB: 'flat', RB: 'block' }, reads: ['TE', 'TE2', 'FB', 'X'], tags: ['goalline', 'playaction'], dropDepth: 4 },
  // ---- clock
  { id: 'kneel', name: 'QB Kneel', formation: 'singleback', type: 'kneel', routes: {}, tags: ['clock'] },
  { id: 'spike', name: 'Spike', formation: 'gun_doubles', type: 'spike', routes: {}, tags: ['clock'] },
];

export const playById = (id: string) => OFF_PLAYS.find((p) => p.id === id)!;
export const formationById = (id: string) => FORMATIONS.find((f) => f.id === id)!;

// ---------------- Defense ----------------
export type Front = 'base' | 'nickel' | 'dime' | 'goalline';
export type Coverage = 'c0' | 'c1' | 'c2' | 'c3' | 'c4' | 'c2m' | 'c3fire';
export type DefRole = 'LE' | 'LDT' | 'RDT' | 'RE' | 'WLB' | 'MLB' | 'SLB' | 'NB' | 'DB' | 'LCB' | 'RCB' | 'FS' | 'SS';

export interface DefCall {
  id: string;
  name: string;
  front: Front;
  coverage: Coverage;
  /** defenders (by role) sent on the rush in addition to the 4 DL */
  blitz: DefRole[];
  tags: string[];
}

export const DEF_PLAYS: DefCall[] = [
  { id: 'base_c3', name: '4-3 Cover 3', front: 'base', coverage: 'c3', blitz: [], tags: ['run', 'base'] },
  { id: 'base_c2', name: '4-3 Cover 2', front: 'base', coverage: 'c2', blitz: [], tags: ['base'] },
  { id: 'base_c1', name: '4-3 Cover 1 Lurk', front: 'base', coverage: 'c1', blitz: [], tags: ['run', 'base'] },
  { id: 'base_mike', name: '4-3 Mike Blitz', front: 'base', coverage: 'c1', blitz: ['MLB'], tags: ['blitz', 'run'] },
  { id: 'base_fire', name: '4-3 Fire Zone', front: 'base', coverage: 'c3fire', blitz: ['WLB'], tags: ['blitz'] },
  { id: 'nk_c3', name: 'Nickel Cover 3', front: 'nickel', coverage: 'c3', blitz: [], tags: ['pass'] },
  { id: 'nk_c4', name: 'Nickel Quarters', front: 'nickel', coverage: 'c4', blitz: [], tags: ['pass'] },
  { id: 'nk_c2m', name: 'Nickel 2-Man', front: 'nickel', coverage: 'c2m', blitz: [], tags: ['pass', 'long'] },
  { id: 'nk_c1', name: 'Nickel Cover 1', front: 'nickel', coverage: 'c1', blitz: [], tags: ['pass'] },
  { id: 'nk_nbblitz', name: 'Nickel Corner Blitz', front: 'nickel', coverage: 'c3fire', blitz: ['NB'], tags: ['blitz', 'pass'] },
  { id: 'nk_c0', name: 'Nickel Cover 0', front: 'nickel', coverage: 'c0', blitz: ['WLB', 'MLB'], tags: ['blitz', 'pass', 'shortyard'] },
  { id: 'dime_c4', name: 'Dime Quarters', front: 'dime', coverage: 'c4', blitz: [], tags: ['long', 'prevent'] },
  { id: 'dime_c2', name: 'Dime Cover 2', front: 'dime', coverage: 'c2', blitz: [], tags: ['long', 'prevent'] },
  { id: 'gl_c0', name: 'Goal Line Cover 0', front: 'goalline', coverage: 'c0', blitz: ['SLB', 'WLB'], tags: ['goalline', 'shortyard', 'run'] },
  { id: 'gl_c1', name: 'Goal Line Cover 1', front: 'goalline', coverage: 'c1', blitz: [], tags: ['goalline', 'shortyard', 'run'] },
];

export const defById = (id: string) => DEF_PLAYS.find((d) => d.id === id)!;

export const FRONT_PERSONNEL: Record<Front, { role: DefRole; pos: Pos[] }[]> = {
  base: [
    { role: 'LE', pos: ['DE'] }, { role: 'LDT', pos: ['DT'] }, { role: 'RDT', pos: ['DT'] }, { role: 'RE', pos: ['DE'] },
    { role: 'WLB', pos: ['LB'] }, { role: 'MLB', pos: ['LB'] }, { role: 'SLB', pos: ['LB'] },
    { role: 'LCB', pos: ['CB'] }, { role: 'RCB', pos: ['CB'] }, { role: 'FS', pos: ['S'] }, { role: 'SS', pos: ['S'] },
  ],
  nickel: [
    { role: 'LE', pos: ['DE'] }, { role: 'LDT', pos: ['DT'] }, { role: 'RDT', pos: ['DT'] }, { role: 'RE', pos: ['DE'] },
    { role: 'WLB', pos: ['LB'] }, { role: 'MLB', pos: ['LB'] }, { role: 'NB', pos: ['CB'] },
    { role: 'LCB', pos: ['CB'] }, { role: 'RCB', pos: ['CB'] }, { role: 'FS', pos: ['S'] }, { role: 'SS', pos: ['S'] },
  ],
  dime: [
    { role: 'LE', pos: ['DE'] }, { role: 'LDT', pos: ['DT'] }, { role: 'RDT', pos: ['DT'] }, { role: 'RE', pos: ['DE'] },
    { role: 'MLB', pos: ['LB'] }, { role: 'NB', pos: ['CB'] }, { role: 'DB', pos: ['CB', 'S'] },
    { role: 'LCB', pos: ['CB'] }, { role: 'RCB', pos: ['CB'] }, { role: 'FS', pos: ['S'] }, { role: 'SS', pos: ['S'] },
  ],
  goalline: [
    { role: 'LE', pos: ['DE'] }, { role: 'LDT', pos: ['DT'] }, { role: 'RDT', pos: ['DT'] }, { role: 'RE', pos: ['DE'] },
    { role: 'WLB', pos: ['LB'] }, { role: 'MLB', pos: ['LB'] }, { role: 'SLB', pos: ['LB'] },
    { role: 'LCB', pos: ['CB'] }, { role: 'RCB', pos: ['CB'] }, { role: 'FS', pos: ['S'] }, { role: 'SS', pos: ['S'] },
  ],
};
