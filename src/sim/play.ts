// Per-play physical simulation at 60 Hz.
// Offense frame: offense attacks +x; LOS at x = los (0 = own goal line, 100 = opponent goal line).
import { Rng, clamp, sigmoid } from './rng';
import { V2, add, sub, scale, len, dist, dot, norm, interceptPoint, fromAngle, angleOf } from './vec';
import type { Player, Team, Ratings } from './types';
import {
  FIELD_W, Formation, FormationSlot, OffPlay, OffRole, DefCall, DefRole, FRONT_PERSONNEL, ROUTES, formationById,
} from './playbook';

export const G = 10.72; // gravity in yd/s^2
const DT = 1 / 60;

export interface Weather {
  windX: number; // yd/s, in WORLD frame (+x = toward east end zone)
  windY: number;
  precip: 'none' | 'rain' | 'snow';
  tempF: number;
}

export type Side = 'off' | 'def';
export type EntState =
  | 'pre' | 'qb' | 'route' | 'passblock' | 'runblock' | 'rush' | 'man' | 'zone' | 'runfit'
  | 'carrier' | 'pursue' | 'convoy' | 'down' | 'idle' | 'spy' | 'fake';

export interface V3 { x: number; y: number; z: number }

export class Ent {
  pos: V2;
  vel: V2 = { x: 0, y: 0 };
  want: V2 = { x: 0, y: 0 };
  face: number;
  state: EntState = 'pre';
  mass: number; // kg
  radius: number;
  vmax: number;
  tau: number;
  latA: number;
  engaged: Ent | null = null;
  engagedBy: Ent[] = [];
  engageEdge = 0;
  engT = 0;
  stun = 0;
  tackleCd = 0;
  route: V2[] = [];
  routeIdx = 0;
  settle = false;
  outSign = 1;
  zone: string | null = null;
  zoneSpot: V2 | null = null;
  man: Ent | null = null;
  blockTarget: Ent | null = null;
  pressDelay = 0;
  velHist: V2[] = [];
  reactT = 0;
  downT = 0;
  hasBall = false;
  anim = 'idle';
  animT = 0;
  rushTarget: V2 | null = null;
  userControlled = false;
  gap = 0; // run-fit gap y
  getOff = 0;
  eligible = false;
  beatBy: Set<Ent> = new Set();
  bitFake = 0;

  constructor(public p: Player, public side: Side, public role: string, pos: V2) {
    this.pos = { ...pos };
    this.face = side === 'off' ? 0 : Math.PI;
    const r = p.r;
    const fat = 1 - 0.1 * p.fatigue;
    this.mass = p.weightLb * 0.4536;
    this.radius = clamp(0.36 + (p.weightLb - 180) / 900, 0.36, 0.55);
    // 99 speed ~ 11.0 yd/s (22.5 mph); 50 ~ 8.6 yd/s
    this.vmax = (8.6 + (r.speed - 50) * 0.049) * fat;
    this.tau = (1.0 - (r.accel - 50) * 0.0078) / fat;
    this.latA = (8 + (r.agility - 50) * 0.12) * fat;
  }
  get r(): Ratings {
    return this.p.r;
  }
  get speed() {
    return len(this.vel);
  }
  get free() {
    return !this.engaged && this.stun <= 0 && this.state !== 'down';
  }
}

export interface Ball {
  mode: 'snap' | 'held' | 'air' | 'loose' | 'dead';
  holder: Ent | null;
  pos: V3;
  vel: V3;
  target: Ent | null;
  targetSpot: V2 | null;
  thrower: Ent | null;
  throwX: number;
  spin: number;
  lob: boolean;
  t: number;
}

export interface PlayEvent {
  t: number;
  type: string;
  a?: Ent;
  b?: Ent;
  x?: number;
  note?: string;
}

export interface PlayResult {
  kind: 'run' | 'pass' | 'sack' | 'scramble' | 'kneel' | 'spike';
  endX: number;
  yards: number;
  complete: boolean;
  incomplete: boolean;
  interception: boolean;
  fumbleLost: boolean;
  turnover: boolean;
  td: Side | null;
  safety: boolean;
  touchback: boolean;
  outOfBounds: boolean;
  duration: number;
  passer?: Player;
  rusher?: Player;
  receiver?: Player;
  target?: Player;
  tacklers: Player[];
  sackedBy?: Player;
  intBy?: Player;
  fumbleBy?: Player;
  fumbleRecBy?: Player;
  forcedBy?: Player;
  pbuBy?: Player;
  airYards: number;
  yac: number;
  deepContested: boolean;
  thrownAway: boolean;
  pressured: boolean;
  timeToThrow: number;
  brokenTackles: number;
  targetSpotY?: number;
  endY: number;
  events: PlayEvent[];
}

export interface UserInput {
  move: V2; // desired direction in offense frame (length 0..1)
  sprint: boolean;
  throwTo: OffRole | null;
  lob: boolean;
  juke: boolean;
  dive: boolean;
  switchReq: boolean;
}

export interface PlayConfig {
  off: Team;
  def: Team;
  offPlay: OffPlay;
  defCall: DefCall;
  los: number;
  ballY: number;
  flip: boolean;
  weather: Weather;
  /** +1 if offense attacks world +x, -1 otherwise (for wind) */
  worldDir: number;
  rng: Rng;
  userSide?: Side | null;
  down?: number;
  toGo?: number;
}

const isOL = (role: string) => ['LT', 'LG', 'C', 'RG', 'RT'].includes(role);
const isDL = (role: string) => ['LE', 'LDT', 'RDT', 'RE'].includes(role);

export class PlaySim {
  static tackleLog: { p: number; facing: number; dive: boolean; arm: boolean; gang: number; dx: number; vc: number; juke: boolean }[] | null = null;
  ents: Ent[] = [];
  off: Ent[] = [];
  def: Ent[] = [];
  ball: Ball;
  t = 0;
  phase: 'live' | 'dead' = 'live';
  events: PlayEvent[] = [];
  result: PlayResult | null = null;
  rng: Rng;
  qb!: Ent;
  carrier: Ent | null = null;
  input: UserInput = { move: { x: 0, y: 0 }, sprint: false, throwTo: null, lob: false, juke: false, dive: false, switchReq: false };
  user: Ent | null = null;
  readIdx = 0;
  readT = 0;
  dropDone = false;
  throwStart = -1;
  handedOff = false;
  runRead = false;
  runReadT = 99;
  passThrown = false;
  pendingThrow: { tgt: Ent | null; lob: boolean; at: number; away: boolean } | null = null;
  pressured = false;
  scramble = false;
  brokenTackles = 0;
  catchX = 0;
  firstContact = false;
  private reached: Map<Ent, number> = new Map();
  private lastBallD: Map<Ent, number> = new Map();
  private resolveBallDone = false;
  readonly los: number;
  readonly by: number;
  readonly cfg: PlayConfig;
  readonly offRoles = new Map<string, Ent>();
  readonly defRoles = new Map<string, Ent>();
  wind: V2;

  constructor(cfg: PlayConfig) {
    this.cfg = cfg;
    this.rng = cfg.rng;
    this.los = cfg.los;
    this.by = cfg.ballY;
    const wd = cfg.worldDir;
    this.wind = { x: cfg.weather.windX * wd, y: cfg.weather.windY * wd };
    this.ball = {
      mode: 'snap', holder: null, pos: { x: this.los, y: this.by, z: 0.3 }, vel: { x: 0, y: 0, z: 0 },
      target: null, targetSpot: null, thrower: null, throwX: 0, spin: 0, lob: false, t: 0,
    };
    this.setupOffense();
    this.setupDefense();
    this.ents = [...this.off, ...this.def];
    this.setupAssignments();
    if (cfg.userSide) this.pickUserEnt(cfg.userSide);
  }

  // ------------------------------------------------------------------ setup
  private pickFrom(team: Team, pos: string[], used: Set<number>): Player {
    for (const ps of pos) {
      const cands = team.roster
        .filter((p) => p.pos === ps && p.injuredWeeks <= 0 && !used.has(p.id))
        .sort((a, b) => b.ovr * (1 - 0.6 * b.fatigue) - a.ovr * (1 - 0.6 * a.fatigue));
      if (cands.length) {
        used.add(cands[0].id);
        return cands[0];
      }
    }
    // emergency: anyone
    const any = team.roster.find((p) => !used.has(p.id) && p.injuredWeeks <= 0) ?? team.roster[0];
    used.add(any.id);
    return any;
  }

  private alignPos(slot: FormationSlot, flip: boolean): V2 {
    const L = this.los;
    const by = this.by;
    const a = slot.align;
    const s = (side: 'L' | 'R') => ((side === 'L') !== flip ? 1 : -1);
    switch (a.kind) {
      case 'ol':
        return { x: L - 0.55, y: by + (flip ? -a.dy : a.dy) };
      case 'inline':
        return { x: L - (a.off ? 1.3 : 0.6), y: by + s(a.side) * 4.05 };
      case 'wide': {
        const sg = s(a.side);
        const y = sg > 0 ? FIELD_W - 9 : 9;
        return { x: L - (a.off ? 1.3 : 0.6), y: Math.abs(y - by) < 10 ? by + sg * 10 : y };
      }
      case 'slot': {
        const sg = s(a.side);
        const wideY = sg > 0 ? FIELD_W - 9 : 9;
        const span = Math.max(10, Math.abs(wideY - by));
        const k = a.n === 2 ? 0.3 : 0.55;
        return { x: L - 1.2, y: by + sg * Math.max(a.n === 2 ? 5 : 7, span * k) };
      }
      case 'back':
        return { x: L + a.x, y: by + (flip ? -a.dy : a.dy) };
    }
  }

  private setupOffense() {
    const { off, offPlay, flip } = this.cfg;
    const form: Formation = formationById(offPlay.formation);
    const used = new Set<number>();
    for (const slot of form.slots) {
      const p = this.pickFrom(off, slot.pos, used);
      const e = new Ent(p, 'off', slot.role, this.alignPos(slot, flip));
      e.eligible = !isOL(slot.role) && slot.role !== 'QB';
      this.off.push(e);
      this.offRoles.set(slot.role, e);
    }
    this.qb = this.offRoles.get('QB')!;
  }

  private setupDefense() {
    const { def, defCall } = this.cfg;
    const used = new Set<number>();
    for (const s of FRONT_PERSONNEL[defCall.front]) {
      const p = this.pickFrom(def, s.pos, used);
      const e = new Ent(p, 'def', s.role, { x: this.los + 5, y: this.by });
      this.def.push(e);
      this.defRoles.set(s.role, e);
    }
    this.alignDefense();
  }

  private eligibles(): Ent[] {
    return this.off.filter((e) => e.eligible);
  }

  /** Align defenders relative to offensive formation. */
  private alignDefense() {
    const L = this.los;
    const by = this.by;
    const D = this.defRoles;
    const O = this.offRoles;
    const lt = O.get('LT')!.pos.y;
    const rt = O.get('RT')!.pos.y;
    const cov = this.cfg.defCall.coverage;
    const gl = this.cfg.defCall.front === 'goalline';
    const tes = this.off.filter((e) => (e.role === 'TE' || e.role === 'TE2') && Math.abs(e.pos.y - by) < 5);
    const teL = tes.find((t) => t.pos.y > by);
    const teR = tes.find((t) => t.pos.y < by);
    const set = (r: DefRole, x: number, y: number) => {
      const e = D.get(r);
      if (e) e.pos = { x, y: clamp(y, 1, FIELD_W - 1) };
    };
    set('LE', L + 0.9, (teL ? teL.pos.y : lt) + 1.0);
    set('LDT', L + 0.85, by + 1.9);
    set('RDT', L + 0.85, by - 0.7);
    set('RE', L + 0.9, (teR ? teR.pos.y : rt) - 1.0);
    const lbDepth = gl ? 2.5 : 4.6;
    set('WLB', L + lbDepth, by + 3.6);
    set('MLB', L + lbDepth + 0.3, by + 0.2);
    set('SLB', L + lbDepth, by - 3.6);
    // corners over widest receivers each side
    const elig = this.eligibles();
    const leftR = elig.filter((e) => e.pos.y > by + 5).sort((a, b) => b.pos.y - a.pos.y);
    const rightR = elig.filter((e) => e.pos.y < by - 5).sort((a, b) => a.pos.y - b.pos.y);
    const press = cov === 'c0' || cov === 'c1' || cov === 'c2m';
    const cbDepth = (e: Ent | undefined) => (press && e ? 1.1 : cov === 'c2' ? 4.5 : 7);
    const lW = leftR[0];
    const rW = rightR[0];
    set('LCB', L + cbDepth(lW), lW ? lW.pos.y - 0.7 : by + 13);
    set('RCB', L + cbDepth(rW), rW ? rW.pos.y + 0.7 : by - 13);
    const slots = [...leftR.slice(1), ...rightR.slice(1)];
    if (D.get('NB')) {
      const s = slots[0];
      set('NB', L + (press ? 1.5 : 5), s ? s.pos.y + (s.pos.y > by ? -0.8 : 0.8) : by + 8);
    }
    if (D.get('DB')) {
      const s = slots[1];
      set('DB', L + (s ? 5.5 : 10), s ? s.pos.y + (s.pos.y > by ? -0.8 : 0.8) : by - 8);
    }
    const twoHigh = cov === 'c2' || cov === 'c4' || cov === 'c2m';
    if (cov === 'c0') {
      set('FS', L + 6, by + 5);
      set('SS', L + 5, by - 5);
    } else if (twoHigh) {
      set('FS', L + (gl ? 7 : 12.5), by + 9);
      set('SS', L + (gl ? 7 : 12.5), by - 9);
    } else {
      set('FS', L + (gl ? 8 : 13.5), clamp(by, 18, 35));
      const strongL = elig.filter((e) => e.pos.y > by).length >= elig.filter((e) => e.pos.y < by).length;
      set('SS', L + (gl ? 4 : 7.5), by + (strongL ? 5.5 : -5.5));
    }
    // never let defenders line up in the neutral zone
    for (const e of this.def) e.pos.x = Math.max(e.pos.x, L + 0.75);
  }

  private setupAssignments() {
    const play = this.cfg.offPlay;
    const by = this.by;
    // ---- offense
    for (const e of this.off) {
      if (e === this.qb) {
        e.state = 'qb';
        continue;
      }
      if (isOL(e.role)) {
        e.state = play.type === 'run' ? 'runblock' : 'passblock';
        continue;
      }
      const rname = play.routes[e.role as OffRole];
      const isCarrier = play.run && play.run.carrier === e.role;
      if (play.type === 'run' || play.type === 'kneel' || play.type === 'spike') {
        if (isCarrier) e.state = 'idle';
        else if (e.role === 'FB') e.state = 'runblock';
        else e.state = 'runblock';
        continue;
      }
      if (!rname || rname === 'block') {
        e.state = 'passblock';
        continue;
      }
      const rd = ROUTES[rname];
      e.state = 'route';
      e.settle = !!rd.settle;
      let side = Math.sign(e.pos.y - by);
      if (Math.abs(e.pos.y - by) < 0.6) side = e.role === 'RB' ? (this.cfg.flip ? -1 : 1) : 1;
      e.outSign = side;
      e.route = rd.pts.map(([fx, oy]) => ({ x: e.pos.x + fx, y: clamp(e.pos.y + oy * side, 0.8, FIELD_W - 0.8) }));
      e.routeIdx = 0;
      // check-release backs wait in protection briefly
      if (e.role === 'RB' && (rname === 'checkdown' || rname === 'flat')) e.pressDelay = 0.9;
      if (rname === 'screen') e.pressDelay = 1.1;
    }
    // ---- defense
    const call = this.cfg.defCall;
    const cov = call.coverage;
    const rushers = new Set<Ent>();
    for (const e of this.def) if (isDL(e.role)) rushers.add(e);
    for (const r of call.blitz) {
      const e = this.defRoles.get(r);
      if (e) rushers.add(e);
    }
    for (const e of rushers) {
      e.state = 'rush';
      e.gap = e.pos.y;
    }
    const cover = this.def.filter((e) => !rushers.has(e));
    const man = cov === 'c0' || cov === 'c1' || cov === 'c2m';
    const deepZones: Record<string, [string, DefRole][]> = {
      c1: [['deepM', 'FS']],
      c2m: [['halfL', 'FS'], ['halfR', 'SS']],
      c2: [['halfL', 'FS'], ['halfR', 'SS']],
      c3: [['thirdL', 'LCB'], ['thirdM', 'FS'], ['thirdR', 'RCB']],
      c3fire: [['thirdL', 'LCB'], ['thirdM', 'FS'], ['thirdR', 'RCB']],
      c4: [['q1', 'LCB'], ['q2', 'FS'], ['q3', 'SS'], ['q4', 'RCB']],
      c0: [],
    };
    const assigned = new Set<Ent>();
    for (const [z, role] of deepZones[cov]) {
      const e = this.defRoles.get(role);
      if (e && !rushers.has(e)) {
        e.state = 'zone';
        e.zone = z;
        assigned.add(e);
      }
    }
    let rest = cover.filter((e) => !assigned.has(e));
    if (man) {
      // man match: corners take widest, then slots by nickel/dime, then TE/backs by S/LB
      const elig = this.eligibles().filter((e) => e.state === 'route' || e.state === 'idle' || play.type === 'run' || e.state === 'passblock');
      const remaining = [...elig];
      const take = (d: Ent | undefined, pick: (c: Ent[]) => Ent | undefined) => {
        if (!d || !rest.includes(d)) return;
        const tgt = pick(remaining);
        if (!tgt) return;
        d.state = 'man';
        d.man = tgt;
        remaining.splice(remaining.indexOf(tgt), 1);
        rest = rest.filter((x) => x !== d);
      };
      const widest = (side: number) => (c: Ent[]) =>
        c.filter((e) => Math.sign(e.pos.y - by) === side && Math.abs(e.pos.y - by) > 5).sort((a, b) => Math.abs(b.pos.y - by) - Math.abs(a.pos.y - by))[0];
      const nearest = (d: Ent) => (c: Ent[]) => c.slice().sort((a, b) => dist(a.pos, d.pos) - dist(b.pos, d.pos))[0];
      take(this.defRoles.get('LCB'), widest(1));
      take(this.defRoles.get('RCB'), widest(-1));
      for (const r of ['NB', 'DB', 'SS', 'SLB', 'WLB', 'MLB', 'FS'] as DefRole[]) {
        const d = this.defRoles.get(r);
        if (d) take(d, nearest(d));
      }
      // leftovers: robber / spy
      for (const e of rest) {
        e.state = 'zone';
        e.zone = 'hookM';
      }
    } else {
      // zone: under zones by coverage
      const under: Record<string, string[]> = {
        c2: ['flatL', 'hookL', 'hookM', 'hookR', 'flatR'],
        c3: ['flatL', 'hookL', 'hookR', 'flatR'],
        c3fire: ['flatL', 'hookM', 'flatR'],
        c4: ['flatL', 'hookM', 'flatR'],
      };
      const zs = (under[cov] ?? ['hookM']).slice();
      // CBs in cover 2 take flats regardless of order
      if (cov === 'c2') {
        for (const [r, z] of [['LCB', 'flatL'], ['RCB', 'flatR']] as [DefRole, string][]) {
          const e = this.defRoles.get(r);
          if (e && rest.includes(e)) {
            e.state = 'zone';
            e.zone = z;
            rest = rest.filter((x) => x !== e);
            zs.splice(zs.indexOf(z), 1);
          }
        }
      }
      // match remaining by lateral position: sort defenders and zones by y (L = +y)
      const zoneY = (z: string) => this.zoneLandmark(z).y;
      const ds = rest.slice().sort((a, b) => b.pos.y - a.pos.y);
      const zz = zs.sort((a, b) => zoneY(b) - zoneY(a));
      // if more defenders than zones, extras become hook/robber
      ds.forEach((d, i) => {
        d.state = 'zone';
        d.zone = zz[Math.round((i * (zz.length - 1)) / Math.max(1, ds.length - 1))] ?? 'hookM';
      });
    }
    for (const e of this.def) {
      if (e.zone) e.zoneSpot = this.zoneLandmark(e.zone);
      e.reactT = 0.12 + (99 - Math.max(e.r.playRec, e.r.awareness)) * 0.006;
      e.getOff = 0.12 + this.rng.range(0, 0.12) + (99 - e.r.awareness) * 0.002;
    }
    this.assignBlocks();
    // press at the line
    for (const e of this.def) {
      if (e.state === 'man' && e.man && dist(e.pos, e.man.pos) < 2.2) {
        const win = sigmoid((e.r.press - e.man.r.release) / 10 - 0.3);
        if (this.rng.chance(win)) e.man.pressDelay = Math.max(e.man.pressDelay, this.rng.range(0.25, 0.55));
      }
    }
    // run-fit gaps for linebackers
    for (const e of this.def) if (['WLB', 'MLB', 'SLB'].includes(e.role)) e.gap = e.pos.y;
  }

  /** Snap-time blocking assignments: unique blocker->defender pairs, closest first. */
  private assignBlocks() {
    const play = this.cfg.offPlay;
    const isRun = play.type === 'run';
    const blockers = this.off.filter((e) => e.state === (isRun ? 'runblock' : 'passblock') && (isOL(e.role) || e.role === 'TE' || e.role === 'TE2' || e.role === 'FB' || e.role === 'RB'));
    const aimDy = play.run ? (this.cfg.flip ? -play.run.aimDy : play.run.aimDy) : 0;
    const playDir = Math.sign(aimDy || 0.001);
    const zone = play.run && (play.run.style === 'zone' || play.run.style === 'stretch');
    const targets = isRun
      ? this.def.filter((d) => d.pos.x < this.los + 6 && Math.abs(d.pos.y - this.by) < 8)
      : this.def.filter((d) => d.state === 'rush');
    const pairs: { b: Ent; d: Ent; c: number }[] = [];
    for (const b of blockers) {
      const backfield = b.role === 'RB' || b.role === 'FB';
      for (const d of targets) {
        const pt = { x: b.pos.x, y: b.pos.y + (zone && !backfield ? playDir * 1.3 : 0) };
        let c = Math.abs(d.pos.y - pt.y) + Math.abs(d.pos.x - pt.x) * 0.5;
        if (isRun && d.pos.x > this.los + 3) c += 1.5; // linebackers are second-level priority
        if (backfield) c += isRun ? 1.2 : 3; // backs pick up leftovers
        pairs.push({ b, d, c });
      }
    }
    pairs.sort((a, b) => a.c - b.c);
    const usedB = new Set<Ent>();
    const usedD = new Set<Ent>();
    // pass 1: every down lineman gets a (line) blocker; pass 2: leftovers climb to linebackers
    for (const pass of [1, 2]) {
      for (const { b, d, c } of pairs) {
        if (usedB.has(b) || usedD.has(d) || c > 8) continue;
        const onLine = d.pos.x < this.los + 1.6;
        const lineBlocker = isOL(b.role) || b.role === 'TE' || b.role === 'TE2';
        if (pass === 1 && !(onLine && lineBlocker)) continue;
        b.blockTarget = d;
        usedB.add(b);
        usedD.add(d);
      }
    }
  }

  zoneLandmark(z: string): V2 {
    const L = this.los;
    const by = this.by;
    const W = FIELD_W;
    const cl = (y: number) => clamp(y, 3, W - 3);
    switch (z) {
      case 'deepM': return { x: L + 15, y: cl(clamp(by, 20, 33)) };
      case 'thirdL': return { x: L + 14, y: cl(W - 10) };
      case 'thirdM': return { x: L + 15, y: cl(clamp(by, 20, 33)) };
      case 'thirdR': return { x: L + 14, y: cl(10) };
      case 'halfL': return { x: L + 15, y: cl(W * 0.73) };
      case 'halfR': return { x: L + 15, y: cl(W * 0.27) };
      case 'q1': return { x: L + 13, y: cl(W - 8) };
      case 'q2': return { x: L + 14, y: cl(W * 0.6) };
      case 'q3': return { x: L + 14, y: cl(W * 0.4) };
      case 'q4': return { x: L + 13, y: cl(8) };
      case 'flatL': return { x: L + 4.5, y: cl(Math.max(by + 11, W - 8)) };
      case 'flatR': return { x: L + 4.5, y: cl(Math.min(by - 11, 8)) };
      case 'hookL': return { x: L + 8, y: cl(by + 5.5) };
      case 'hookR': return { x: L + 8, y: cl(by - 5.5) };
      default: return { x: L + 7, y: cl(by) };
    }
  }

  pickUserEnt(side: Side) {
    if (this.user) this.user.userControlled = false;
    if (side === 'off') {
      this.user = this.qb;
    } else {
      // closest defender to ball, prefer second level
      const cands = this.def.filter((e) => !isDL(e.role));
      cands.sort((a, b) => dist(a.pos, this.qb.pos) - dist(b.pos, this.qb.pos));
      this.user = this.defRoles.get('MLB') ?? cands[0];
    }
    this.user.userControlled = true;
  }

  switchUser() {
    if (!this.user) return;
    const side = this.user.side;
    const bp = { x: this.ball.pos.x, y: this.ball.pos.y };
    const team = side === 'off' ? this.off : this.def;
    const cands = team.filter((e) => e !== this.user && e.state !== 'down');
    if (!cands.length) return;
    cands.sort((a, b) => dist(a.pos, bp) - dist(b.pos, bp));
    this.user.userControlled = false;
    this.user = cands[0];
    this.user.userControlled = true;
  }

  // ------------------------------------------------------------------ helpers
  private ev(type: string, a?: Ent, b?: Ent, note?: string) {
    this.events.push({ t: this.t, type, a, b, x: this.ballSpotX(), note });
  }
  ballSpotX() {
    return this.ball.holder ? this.ball.holder.pos.x : this.ball.pos.x;
  }
  private defendersOf(e: Ent) {
    return e.side === 'off' ? this.def : this.off;
  }
  private teammatesOf(e: Ent) {
    return e.side === 'off' ? this.off : this.def;
  }
  /** Which team currently "has" the ball: offense unless a turnover happened. */
  possSide(): Side {
    const h = this.ball.holder;
    if (h) return h.side;
    return 'off';
  }
  private goalDir(e: Ent) {
    return e.side === 'off' ? 1 : -1;
  }

  // ------------------------------------------------------------------ main loop
  step(dt = DT) {
    if (this.phase !== 'live') return;
    this.t += dt;
    if (this.input.switchReq) {
      this.input.switchReq = false;
      this.switchUser();
    }
    this.updateSnap();
    for (const e of this.ents) {
      if (e.stun > 0) e.stun -= dt;
      if (e.tackleCd > 0) e.tackleCd -= dt;
      if (e.pressDelay > 0) e.pressDelay -= dt;
      e.animT += dt;
      e.velHist.push({ ...e.vel });
      if (e.velHist.length > 40) e.velHist.shift();
    }
    if (this.pendingThrow && this.t >= this.pendingThrow.at) {
      const pt = this.pendingThrow;
      this.pendingThrow = null;
      if (this.ball.holder === this.qb && this.ball.mode === 'held' && this.qb.state !== 'down') {
        if (pt.away) this.throwAway(this.qb);
        else if (pt.tgt) this.throwBall(this.qb, pt.tgt, pt.lob);
      }
    }
    for (const e of this.ents) this.think(e, dt);
    for (const e of this.ents) this.move(e, dt);
    this.updateEngagements(dt);
    this.collide();
    this.updateBall(dt);
    if (this.phase === 'live') this.checkTackles();
    if (this.phase === 'live') this.checkBounds();
  }

  /** Run the play to completion with AI only (headless). */
  runToEnd(maxT = 40): PlayResult {
    let guard = 0;
    while (this.phase === 'live' && this.t < maxT && guard++ < maxT * 70) this.step(DT);
    if (this.phase === 'live') this.endPlay('tackle');
    return this.result!;
  }

  // ------------------------------------------------------------------ snap / handoff
  private updateSnap() {
    const b = this.ball;
    const play = this.cfg.offPlay;
    if (b.mode === 'snap') {
      const snapT = this.qb.pos.x < this.los - 2 ? 0.42 : 0.18;
      const f = Math.min(1, this.t / snapT);
      b.pos = { x: this.los + (this.qb.pos.x - this.los) * f, y: this.by + (this.qb.pos.y - this.by) * f, z: 0.3 + Math.sin(f * Math.PI) * 0.4 + f * 0.9 };
      if (f >= 1) {
        b.mode = 'held';
        b.holder = this.qb;
        this.qb.hasBall = true;
        this.ev('snap', this.qb);
        if (play.type === 'kneel') {
          this.qb.state = 'down';
          this.qb.anim = 'kneel';
          this.endPlay('kneel');
          return;
        }
        if (play.type === 'spike') {
          this.endPlay('spike');
          return;
        }
        if (play.run?.style === 'sneak') {
          this.setCarrier(this.qb);
          this.runRead = true;
          this.runReadT = this.t;
        }
      }
      return;
    }
    // handoff
    if (play.type === 'run' && play.run && !this.handedOff && b.holder === this.qb && play.run.carrier !== 'QB') {
      const rb = this.offRoles.get(play.run.carrier)!;
      const pitch = play.run.style === 'stretch' && this.qb.pos.x > this.los - 2;
      if (this.t >= play.run.handoffDelay && dist(rb.pos, this.qb.pos) < (pitch ? 5.5 : 2.0)) {
        this.handedOff = true;
        b.holder = rb;
        this.qb.hasBall = false;
        this.setCarrier(rb);
        this.qb.state = 'idle';
        this.ev('handoff', this.qb, rb);
        this.runRead = true;
        this.runReadT = this.t;
      }
    }
    if (play.type === 'playaction' && !this.handedOff && this.t > 0.85) {
      this.handedOff = true; // fake complete
      const rb = this.offRoles.get('RB');
      if (rb) {
        rb.state = 'passblock';
      }
    }
  }

  private setCarrier(e: Ent) {
    this.carrier = e;
    e.hasBall = true;
    e.state = 'carrier';
    e.anim = 'run';
    this.ball.holder = e;
    this.ball.mode = 'held';
    // teammates become lead blockers, opponents (if they were blocking/running routes) chase
    for (const o of this.teammatesOf(e)) {
      if (o === e) continue;
      if (['route', 'idle', 'qb', 'man', 'zone', 'rush', 'pursue', 'spy'].includes(o.state)) o.state = 'convoy';
    }
    for (const o of this.defendersOf(e)) {
      if (['convoy', 'route', 'idle', 'qb', 'passblock', 'runblock', 'carrier'].includes(o.state)) {
        if (o.engaged) this.disengage(o);
        o.state = 'pursue';
      }
    }
  }

  // ------------------------------------------------------------------ AI
  private think(e: Ent, dt: number) {
    if (e.state === 'down') {
      e.want = { x: 0, y: 0 };
      return;
    }
    if (e.stun > 0) {
      e.want = scale(e.vel, 0.5);
      return;
    }
    if (e.userControlled && this.applyUser(e)) return;
    if (e.side === 'def' && this.t < e.getOff) {
      e.want = { x: 0, y: 0 };
      return;
    }
    switch (e.state) {
      case 'qb': return this.thinkQB(e, dt);
      case 'route': return this.thinkRoute(e);
      case 'passblock': return this.thinkPassBlock(e);
      case 'runblock': return this.thinkRunBlock(e);
      case 'rush': return this.thinkRush(e);
      case 'man': return this.thinkMan(e);
      case 'zone': return this.thinkZone(e);
      case 'carrier': return this.thinkCarrier(e);
      case 'pursue': return this.thinkPursue(e);
      case 'convoy': return this.thinkConvoy(e);
      case 'idle': return this.thinkIdle(e);
      default:
        e.want = { x: 0, y: 0 };
    }
  }

  private applyUser(e: Ent): boolean {
    const i = this.input;
    const m = i.move;
    const ml = len(m);
    // QB throw
    if (e === this.qb && this.ball.holder === e && i.throwTo && !this.passThrown) {
      const tgt = this.offRoles.get(i.throwTo);
      i.throwTo = null;
      if (tgt && tgt.eligible && tgt.state !== 'down') {
        if (e.pos.x <= this.los + 0.2) {
          this.pendingThrow = { tgt, lob: i.lob, at: this.t + this.releaseTime(e) * 0.8, away: false };
          e.anim = 'throw';
          e.animT = 0;
        }
      }
    }
    if (e === this.qb && this.ball.holder === e && e.pos.x > this.los + 0.5 && e.state === 'qb') {
      this.scramble = true;
      this.setCarrier(e);
    }
    if (ml < 0.05) {
      // no input: let AI drive non-QB entities, QB just holds position
      if (e === this.qb && e.state === 'qb') {
        if (!this.dropDone) return false;
        e.want = scale(e.vel, 0.6);
        return true;
      }
      return false;
    }
    const sp = e.vmax * (i.sprint ? 1 : 0.82) * Math.min(1, ml);
    e.want = scale(norm(m), sp);
    if (i.juke && e.hasBall && e.animT > 0.6) {
      i.juke = false;
      // juke: sharp lateral impulse with brief speed loss
      const lat = { x: -e.vel.y, y: e.vel.x };
      const side = dot(norm(lat), m) >= 0 ? 1 : -1;
      e.vel = add(scale(e.vel, 0.55), scale(norm(lat), side * 3.2 * (0.7 + e.r.agility / 330)));
      e.anim = 'juke';
      e.animT = 0;
    }
    return true;
  }

  private thinkIdle(e: Ent) {
    const play = this.cfg.offPlay;
    if (play.run && play.run.carrier === e.role && !this.handedOff) {
      // path to mesh point then aiming point
      const run = play.run;
      const aim = this.by + (this.cfg.flip ? -run.aimDy : run.aimDy);
      const qb = this.qb.pos;
      const mesh = { x: qb.x - 0.2, y: qb.y + Math.sign(aim - qb.y || 1) * 0.9 };
      const tgt = this.t < run.handoffDelay - 0.15 ? mesh : { x: qb.x + 0.3, y: qb.y + Math.sign(aim - qb.y || 1) * 0.4 };
      const draw = run.style === 'draw' && this.t < 0.4;
      e.want = draw ? { x: 0, y: 0 } : scale(norm(sub(tgt, e.pos)), e.vmax * (run.style === 'stretch' ? 0.8 : 0.55));
      return;
    }
    e.want = { x: 0, y: 0 };
  }

  private thinkQB(e: Ent, dt: number) {
    const play = this.cfg.offPlay;
    const L = this.los;
    if (this.ball.holder !== e) {
      e.want = { x: 0, y: 0 };
      return;
    }
    if (play.type === 'run') {
      // ride the mesh then carry out fake
      const run = play.run!;
      const aim = this.by + (this.cfg.flip ? -run.aimDy : run.aimDy);
      const dir = Math.sign(aim - this.by || 1);
      e.want = this.t < run.handoffDelay * 0.6 ? { x: -1.5, y: dir * 0.6 } : { x: 0, y: 0 };
      return;
    }
    // pass play: dropback
    const form = formationById(play.formation);
    const dropX = L - form.qbDepth - (play.dropDepth ?? 2);
    const paFake = play.type === 'playaction' && this.t < 0.85;
    if (paFake) {
      e.want = { x: -1.2, y: (this.cfg.flip ? 1 : -1) * 1.5 };
      return;
    }
    if (!this.dropDone) {
      const to = { x: dropX, y: this.by };
      const d = dist(e.pos, to);
      if (d < 0.35 || e.pos.x < dropX) {
        this.dropDone = true;
        this.readT = 0;
        this.readIdx = 0;
      } else {
        e.want = scale(norm(sub(to, e.pos)), Math.min(e.vmax * 0.5, d * 4));
        e.face = 0;
        return;
      }
    }
    if (this.pendingThrow) {
      e.want = scale(e.vel, 0.3);
      return;
    }
    if (e.userControlled) return; // human decides when to throw
    // ---------------- read progression
    this.readT += dt;
    const reads = (play.reads ?? []).map((r) => this.offRoles.get(r)).filter((x): x is Ent => !!x && x.eligible);
    const tPocket = this.t;
    const pressure = this.pressureOn(e);
    if (pressure > 0.5) this.pressured = true;
    const dec = e.r.decision;
    const readTime = 0.78 - (dec - 50) * 0.005;
    const cur = reads[Math.min(this.readIdx, reads.length - 1)];
    const quick = play.tags.includes('quick') || play.type === 'screen';
    // openness threshold relaxes as time passes / pressure mounts
    const thresh = (quick ? 0.8 : 2.6) - tPocket * 0.24 - pressure * 1.1;
    if (cur) {
      const depthT = this.predictPos(cur, 1.2).x - L;
      let open = this.openness(cur, e) - (depthT > 15 ? 1.7 + (depthT - 15) * 0.09 : 0);
      // situational: on 3rd/4th down, routes past the sticks are worth more
      const down = this.cfg.down ?? 1;
      const toGo = this.cfg.toGo ?? 10;
      if (down >= 3) open += depthT >= toGo ? 0.6 : depthT < toGo - 3 ? -0.9 : 0;
      // mental noise: poor decision makers misjudge
      const perceived = open + this.rng.normal(0, 0.25 + (99 - dec) * 0.012);
      if (perceived > thresh && tPocket > (quick ? 0.7 : 1.15)) {
        this.startThrow(e, cur, cur.route.length > 0 && (cur.route[cur.route.length - 1].x - L) > 22);
        return;
      }
      if (this.readT > readTime) {
        this.readT = 0;
        this.readIdx++;
        if (this.readIdx >= reads.length) this.readIdx = 0; // cycle back
      }
    }
    // pressure handling: unblocked rusher closing -> hot read / get rid of it
    const eta = this.rushETA(e);
    if (eta < 0.75 + e.r.awareness * 0.004 && this.rng.chance(0.08 + e.r.awareness * 0.0025)) {
      let hot: Ent | null = null;
      let ho = -99;
      for (const r of reads) {
        const o = this.openness(r, e) + (r.pos.x - L < 8 ? 0.4 : 0);
        if (o > ho) { ho = o; hot = r; }
      }
      if (hot && ho > (eta < 0.35 ? -0.2 : 0.4)) {
        this.startThrow(e, hot, false);
        return;
      }
    }
    if (pressure > 0.6) {
      // best available
      let best: Ent | null = null;
      let bo = -99;
      for (const r of reads) {
        const o = this.openness(r, e);
        if (o > bo) { bo = o; best = r; }
      }
      const lane = this.scrambleLane(e);
      const throwAwayOk = tPocket > 1.3 && this.rng.chance(0.03 + e.r.awareness * 0.0006);
      if (best && bo > 0 && this.rng.chance(0.03 + e.r.awareness * 0.0007)) {
        this.startThrow(e, best, false);
        return;
      }
      if (lane && e.r.speed > 64 && this.rng.chance(0.012 + (e.r.speed - 60) * 0.0012)) {
        this.scramble = true;
        this.setCarrier(e);
        this.ev('scramble', e);
        return;
      }
      if (throwAwayOk) {
        this.pendingThrow = { tgt: null, lob: false, at: this.t + this.releaseTime(e), away: true };
        return;
      }
    }
    if (tPocket > 5.2 && this.rng.chance(0.04)) {
      this.pendingThrow = { tgt: null, lob: false, at: this.t + this.releaseTime(e), away: true };
      return;
    }
    // pocket movement: slide away from nearest free rusher, climb up
    let push = { x: 0.6, y: 0 };
    for (const d of this.def) {
      if (!d.free || d.state !== 'rush') continue;
      const dd = dist(d.pos, e.pos);
      if (dd < 4) push = add(push, scale(norm(sub(e.pos, d.pos)), (4 - dd) * 0.9));
    }
    const pocketCenter = { x: this.dropDone ? Math.max(e.pos.x, dropX) : dropX, y: this.by };
    const back = sub(pocketCenter, e.pos);
    e.want = scale(add(push, scale(back, 0.5)), 1.4);
    if (len(e.want) > e.vmax * 0.5) e.want = scale(norm(e.want), e.vmax * 0.5);
  }

  /** 0..1+ pressure based on free rushers near QB. */
  private pressureOn(qb: Ent): number {
    let p = 0;
    for (const d of this.def) {
      if (!d.free) continue;
      const dd = dist(d.pos, qb.pos);
      if (dd < 4.5) p = Math.max(p, (4.5 - dd) / 3);
    }
    return p;
  }

  private scrambleLane(qb: Ent): boolean {
    for (const d of this.def) {
      if (!d.free) continue;
      const rel = sub(d.pos, qb.pos);
      if (rel.x > -1 && rel.x < 7 && Math.abs(rel.y) < 3.5) return false;
    }
    return true;
  }

  /** Predict where a receiver will be in T seconds by walking their route. */
  predictPos(e: Ent, T: number): V2 {
    if (e.state !== 'route' || !e.route.length) return add(e.pos, scale(e.vel, T));
    let pos = { ...e.pos };
    let remaining = Math.max(0, T - Math.max(0, e.pressDelay));
    const sp = Math.max(e.speed, e.vmax * 0.85);
    let idx = e.routeIdx;
    while (remaining > 0 && idx < e.route.length) {
      const tgt = e.route[idx];
      const d = dist(pos, tgt);
      const tt = d / sp;
      if (tt >= remaining) return add(pos, scale(norm(sub(tgt, pos)), sp * remaining));
      remaining -= tt * 1.12; // breaks cost time
      pos = { ...tgt };
      idx++;
    }
    if (e.settle) return pos;
    // continue along last leg direction
    const n = e.route.length;
    const dir = n >= 2 ? norm(sub(e.route[n - 1], e.route[n - 2])) : { x: 1, y: 0 };
    const p = add(pos, scale(dir, sp * Math.max(0, remaining)));
    return { x: p.x, y: clamp(p.y, 0.8, FIELD_W - 0.8) };
  }

  /** Windup: elite quick releases ~0.2s, slow ones ~0.38s. */
  releaseTime(qb: Ent) {
    return clamp(0.3 - (qb.r.throwPower + qb.r.awareness - 120) * 0.0019, 0.16, 0.34);
  }

  /** Seconds until the nearest unblocked rusher reaches the QB. */
  private rushETA(qb: Ent): number {
    let eta = 99;
    for (const d of this.def) {
      if (!d.free || d.state !== 'rush') continue;
      const dd = dist(d.pos, qb.pos) - 1.1;
      eta = Math.min(eta, dd / Math.max(4, d.speed));
    }
    return eta;
  }

  startThrow(qb: Ent, tgt: Ent, lob: boolean) {
    if (this.pendingThrow) return;
    this.pendingThrow = { tgt, lob, at: this.t + this.releaseTime(qb), away: false };
    qb.anim = 'throw';
    qb.animT = 0;
  }

  throwSpeed(qb: Ent) {
    return 20 + (qb.r.throwPower - 50) * 0.16; // 99 -> 27.8 yd/s (~57 mph)
  }

  /** Expected separation (yards) at the catch point if thrown now. */
  openness(r: Ent, qb: Ent): number {
    if (r.state === 'down' || r.pressDelay > 0.3) return -3;
    const v0 = this.throwSpeed(qb);
    let T = dist(qb.pos, r.pos) / (v0 * 0.85) + 0.3;
    let P = this.predictPos(r, T);
    T = dist(qb.pos, P) / (v0 * 0.85) + 0.3;
    P = this.predictPos(r, T);
    let worst = 99;
    for (const d of this.def) {
      if (d.state === 'down') continue;
      // deep defenders close on long balls (they're already running): QBs respect that
      const reach = d.free ? d.vmax * Math.max(0, T - d.reactT - 0.15) * (T > 1.6 ? 0.98 : 0.85) : 0;
      const sep = dist(d.pos, P) - reach - 0.9;
      // defenders in the passing lane
      const lane = this.laneBlock(qb.pos, P, d.pos);
      worst = Math.min(worst, sep, lane);
    }
    // throws beyond the sideline are worthless
    if (P.y < 0.5 || P.y > FIELD_W - 0.5) worst -= 3;
    // more valuable if it gets a first down etc - handled elsewhere
    return worst;
  }

  private laneBlock(a: V2, b: V2, d: V2): number {
    const ab = sub(b, a);
    const L = len(ab);
    if (L < 1) return 99;
    const t = clamp(dot(sub(d, a), ab) / (L * L), 0, 1);
    if (t < 0.15 || t > 0.85) return 99;
    const c = add(a, scale(ab, t));
    // underneath defenders can only knock down lower throws (short/mid)
    const h = dist(d, c);
    return L > 18 ? 99 : h * 1.6 - 0.4;
  }

  throwBall(qb: Ent, tgt: Ent, lob: boolean) {
    const b = this.ball;
    const v0max = this.throwSpeed(qb);
    // target at chest height ~ 1.4 yd
    let T = dist(qb.pos, tgt.pos) / (v0max * 0.8) + 0.1;
    let aim = this.predictPos(tgt, T);
    let touch = lob ? 0.32 : 0.05;
    for (let k = 0; k < 4; k++) {
      const D = dist(qb.pos, aim);
      touch = Math.max(touch, lob || D > 28 ? 0.32 : D > 15 ? 0.14 : 0.05);
      // put air under it if an underneath defender sits in the lane
      if (!lob && D > 8) {
        for (const d of this.def) {
          if (d.state === 'down' || d.engaged) continue;
          const ab = sub(aim, qb.pos);
          const tt = dot(sub(d.pos, qb.pos), ab) / dot(ab, ab);
          if (tt < 0.25 || tt > 0.85) continue;
          if (dist(d.pos, add(qb.pos, scale(ab, tt))) < 1.6) touch = Math.max(touch, 0.12 + (tt < 0.6 ? 0.12 : 0.05) * (D / 15));
        }
      }
      T = this.solveT(D, v0max, Math.min(0.45, touch));
      aim = this.predictPos(tgt, T + 0.05);
    }
    aim = { x: aim.x, y: clamp(aim.y, 0.6, FIELD_W - 0.6) };
    const D = dist(qb.pos, aim);
    // accuracy model
    const depthAcc = D < 10 ? qb.r.accShort : D < 20 ? qb.r.accMid : qb.r.accDeep;
    const pressure = this.pressureOn(qb);
    const moving = qb.speed / qb.vmax;
    const onRun = moving > 0.35 ? 1 + (1 - qb.r.throwOnRun / 100) * moving * 1.5 : 1;
    const poise = 1 + pressure * (1.4 - qb.r.poise / 100);
    const weather = this.cfg.weather.precip === 'rain' ? 1.12 : this.cfg.weather.precip === 'snow' ? 1.18 : 1;
    const sd = (0.22 + (99 - depthAcc) * 0.019) * (0.5 + D / 19) * onRun * poise * weather * (1 + qb.p.fatigue * 0.2);
    aim = { x: aim.x + this.rng.normal(0, sd * 0.9), y: aim.y + this.rng.normal(0, sd) };
    const zErr = this.rng.normal(0, sd * 0.45);
    this.launch(qb, aim, T, 1.45 + zErr);
    b.target = tgt;
    b.targetSpot = aim;
    b.lob = lob;
    this.passThrown = true;
    this.throwStart = this.t;
    qb.anim = 'throw';
    qb.animT = 0;
    qb.state = 'idle';
    this.ev('throw', qb, tgt);
    // receiver tracks the ball
    tgt.zoneSpot = aim;
  }

  throwAway(qb: Ent) {
    const side = qb.pos.y > this.by ? 1 : -1;
    const aim = { x: this.los + 8, y: side > 0 ? FIELD_W + 4 : -4 };
    const D = dist(qb.pos, aim);
    const T = this.solveT(D, this.throwSpeed(qb), 0.15);
    this.launch(qb, aim, T, 2.5);
    this.ball.target = null;
    this.ball.targetSpot = aim;
    this.passThrown = true;
    this.throwStart = this.t;
    qb.anim = 'throw';
    qb.animT = 0;
    qb.state = 'idle';
    this.ev('throwaway', qb);
  }

  /** Minimum flight time for given distance & speed, plus "touch" fraction. */
  private solveT(D: number, v0: number, touch: number): number {
    let lo = 0.05;
    let hi = 6;
    for (let i = 0; i < 40; i++) {
      const T = (lo + hi) / 2;
      const vh = D / T;
      const vz = (0.5 * G * T * T) / T;
      if (vh * vh + vz * vz > v0 * v0) lo = T;
      else hi = T;
    }
    return hi * (1 + touch);
  }

  /** Launch with drag+wind compensation via shooting method. */
  private launch(qb: Ent, aim: V2, T: number, zTarget: number) {
    const z0 = 2.05; // release height ~ 6'2"
    let tgt = { ...aim };
    let vel: V3 = { x: 0, y: 0, z: 0 };
    for (let it = 0; it < 3; it++) {
      vel = {
        x: (tgt.x - qb.pos.x) / T,
        y: (tgt.y - qb.pos.y) / T,
        z: (zTarget - z0 + 0.5 * G * T * T) / T,
      };
      const land = this.simulateFlight({ x: qb.pos.x, y: qb.pos.y, z: z0 }, vel, T);
      tgt = { x: tgt.x + (aim.x - land.x), y: tgt.y + (aim.y - land.y) };
    }
    const b = this.ball;
    b.mode = 'air';
    b.holder = null;
    qb.hasBall = false;
    b.thrower = qb;
    b.throwX = qb.pos.x;
    b.pos = { x: qb.pos.x, y: qb.pos.y, z: z0 };
    b.vel = vel;
    b.t = 0;
  }

  private dragK() {
    // 0.5 * rho * Cd * A / m for a spiral (yd^-1)
    const wet = this.cfg.weather.precip !== 'none' ? 1.08 : 1;
    const cold = 1 + (60 - this.cfg.weather.tempF) * 0.0015; // denser air when cold
    return 0.0016 * wet * cold;
  }

  private simulateFlight(p0: V3, v0: V3, T: number): V3 {
    const p = { ...p0 };
    const v = { ...v0 };
    const k = this.dragK();
    const h = 1 / 120;
    for (let t = 0; t < T; t += h) this.integrate(p, v, k, h);
    return p;
  }

  private integrate(p: V3, v: V3, k: number, h: number) {
    const rx = v.x - this.wind.x;
    const ry = v.y - this.wind.y;
    const rz = v.z;
    const s = Math.hypot(rx, ry, rz);
    v.x += -k * s * rx * h;
    v.y += -k * s * ry * h;
    v.z += (-G - k * s * rz) * h;
    p.x += v.x * h;
    p.y += v.y * h;
    p.z += v.z * h;
  }

  private thinkRoute(e: Ent): void {
    if (this.ball.mode === 'air' && this.ball.target === e && this.ball.targetSpot) {
      // track the ball: go to landing spot, arriving on time
      const spot = this.ballLanding();
      e.want = scale(norm(sub(spot, e.pos)), Math.min(e.vmax, dist(spot, e.pos) * 5 + 1));
      return;
    }
    if (this.ball.mode === 'air') {
      // non-targets: drift toward the ball to block/ help
      const spot = this.ballLanding();
      e.want = scale(norm(sub(spot, e.pos)), e.vmax * 0.6);
      return;
    }
    if (e.pressDelay > 0) {
      e.want = scale(norm({ x: 0.3, y: -e.outSign * 0.6 }), 1.5);
      return;
    }
    if (e.routeIdx < e.route.length) {
      const tgt = e.route[e.routeIdx];
      const d = dist(e.pos, tgt);
      if (d < 0.7) {
        e.routeIdx++;
        // a crisp break: route runners lose less speed
        const keep = 0.55 + e.r.routeRunning * 0.004;
        e.vel = scale(e.vel, keep);
        return this.thinkRoute(e);
      }
      e.want = scale(norm(sub(tgt, e.pos)), e.vmax);
      return;
    }
    if (e.settle) {
      // settle in the hole facing QB, slight drift away from nearest defender
      let away = { x: 0, y: 0 };
      for (const d of this.def) {
        const dd = dist(d.pos, e.pos);
        if (dd < 3) away = add(away, scale(norm(sub(e.pos, d.pos)), (3 - dd) * 0.5));
      }
      const toQB = scale(norm(sub(this.qb.pos, e.pos)), 0.4);
      e.want = add(away, toQB);
      return;
    }
    // continue last leg
    const n = e.route.length;
    const dir = n >= 2 ? norm(sub(e.route[n - 1], e.route[n - 2])) : { x: 1, y: 0 };
    let w = scale(dir, e.vmax);
    if (e.pos.y < 2 && w.y < 0) w = { x: e.vmax, y: 0 };
    if (e.pos.y > FIELD_W - 2 && w.y > 0) w = { x: e.vmax, y: 0 };
    e.want = w;
  }

  ballLanding(): V2 {
    const b = this.ball;
    // predict where ball reaches catch height (~1.3 yd) on descent
    const p = { ...b.pos };
    const v = { ...b.vel };
    const k = this.dragK();
    for (let i = 0; i < 600; i++) {
      this.integrate(p, v, k, 1 / 60);
      if (v.z < 0 && p.z < 1.3) break;
    }
    return { x: p.x, y: p.y };
  }

  private thinkPassBlock(e: Ent) {
    if (this.ball.holder && this.ball.holder !== this.qb && this.ball.holder.side === 'off') {
      e.state = 'convoy';
      return;
    }
    if (this.cfg.offPlay.type === 'screen' && this.t > 1.2 && isOL(e.role)) {
      // release to set up the screen
      if (e.engaged) this.disengage(e);
      const rb = this.offRoles.get('RB')!;
      e.want = scale(norm(sub({ x: rb.pos.x + 4, y: rb.pos.y + (e.pos.y - this.by) * 0.6 }, e.pos)), e.vmax * 0.8);
      return;
    }
    if (e.engaged) return;
    const qb = this.qb.pos;
    let best: Ent | null = null;
    let bs = 1e9;
    const as = e.blockTarget;
    if (as && as.free && as.state === 'rush' && dist(as.pos, qb) < dist(e.pos, qb) + 2.5 && this.t < 2.5) {
      best = as;
      bs = -1;
    }
    // otherwise find most dangerous unblocked rusher
    for (const d of this.def) {
      if (bs < 0) break;
      if (d.state !== 'rush' || !d.free) continue;
      const dd = dist(d.pos, e.pos);
      if (dd > 5) continue;
      const threat = dist(d.pos, qb);
      const blockers = d.engagedBy.length;
      const s = dd * 0.9 + threat * 0.5 + blockers * 3;
      if (s < bs) {
        bs = s;
        best = d;
      }
    }
    e.blockTarget = best;
    const home = { x: Math.min(this.los - 1.4, qb.x + 2.8), y: e.pos.y * 0.3 + (this.by + (e.pos.y - this.by) * 1.15) * 0.7 };
    if (best) {
      // kick-slide: set up on the rusher's projected path to the QB, not where he is now
      const ahead = add(best.pos, scale(best.vel, 0.35));
      const lane = norm(sub(qb, ahead));
      const setPt = add(ahead, scale(lane, 1.0));
      e.want = scale(norm(sub(setPt, e.pos)), Math.min(e.vmax, dist(setPt, e.pos) * 6));
      if (dist(best.pos, e.pos) < e.radius + best.radius + 0.45) this.engage(e, best);
    } else {
      e.want = scale(sub(home, e.pos), 2);
      if (len(e.want) > e.vmax * 0.5) e.want = scale(norm(e.want), e.vmax * 0.5);
    }
  }

  private thinkRunBlock(e: Ent) {
    const play = this.cfg.offPlay;
    const run = play.run;
    const carrier = this.carrier;
    if (e.engaged) return;
    const aimDy = run ? (this.cfg.flip ? -run.aimDy : run.aimDy) : 0;
    const playDir = Math.sign(aimDy || 0.001);
    let best: Ent | null = null;
    let bs = 1e9;
    const isLineman = isOL(e.role) || e.role === 'TE' || e.role === 'TE2';
    const zoneShift = run && (run.style === 'zone' || run.style === 'stretch') ? playDir * 1.4 : 0;
    const myPt = { x: e.pos.x + 1.5, y: e.pos.y + zoneShift };
    const as = e.blockTarget;
    if (as && as.state !== 'down' && !as.engaged && as.stun <= 0 && dist(as.pos, e.pos) < 7) {
      best = as;
      bs = -1;
    }
    for (const d of this.def) {
      if (bs < 0) break;
      if (!d.free) continue;
      const dd = dist(d.pos, myPt);
      const range = isLineman ? 4.5 : 9;
      if (dd > range) continue;
      // prefer defenders in front, and those not already blocked
      let s = dd + d.engagedBy.length * 4 + (d.pos.x < e.pos.x - 0.5 ? 3 : 0);
      if (!isLineman && carrier) s += dist(d.pos, carrier.pos) * 0.5;
      if (s < bs) {
        bs = s;
        best = d;
      }
    }
    if (best) {
      const contact = add(best.pos, scale(norm(sub(e.pos, best.pos)), 0.6));
      e.want = scale(norm(sub(contact, e.pos)), e.vmax);
      if (dist(best.pos, e.pos) < e.radius + best.radius + 0.3) this.engage(e, best);
    } else {
      // climb to second level along the play side
      e.want = scale(norm({ x: 1, y: playDir * 0.35 }), e.vmax * 0.7);
    }
  }

  private thinkRush(e: Ent) {
    if (e.engaged) return;
    const holder = this.ball.holder;
    // run read: once a handoff is seen (after reaction time), rushers play the run
    if (this.runRead && this.t - this.runReadT > e.reactT * 0.7 && holder && holder !== this.qb) {
      e.state = 'pursue';
      return;
    }
    if (holder && holder.side === 'off' && holder.state === 'carrier' && holder !== this.qb) {
      e.state = 'pursue';
      return;
    }
    if (this.ball.mode === 'air') {
      // hands up / turn and chase
      if (this.t - this.throwStart > e.reactT) e.state = 'pursue';
      e.want = { x: 0, y: 0 };
      return;
    }
    const qb = this.qb.pos;
    // edge rushers arc around the tackle
    const isEdge = e.role === 'LE' || e.role === 'RE' || !isDL(e.role) && Math.abs(e.gap - this.by) > 4;
    let tgt = qb;
    if (isEdge && e.pos.x > qb.x + 2.5) {
      const side = Math.sign(e.pos.y - this.by) || 1;
      tgt = { x: qb.x + 1.0, y: qb.y + side * 2.6 };
    }
    e.want = scale(norm(sub(tgt, e.pos)), e.vmax);
  }

  private delayedVel(e: Ent, lag: number): V2 {
    const n = Math.round(lag * 60);
    const h = e.velHist;
    return h.length > n ? h[h.length - 1 - n] : h[0] ?? e.vel;
  }

  private thinkMan(e: Ent) {
    if (this.reactToBall(e)) return;
    const r = e.man!;
    if (!r || r.state === 'passblock' || r.state === 'runblock' || r.state === 'down') {
      // receiver stayed in to block: rush or robber
      if (this.t > 0.6) {
        e.state = 'zone';
        e.zone = 'hookM';
        e.zoneSpot = this.zoneLandmark('hookM');
      }
      e.want = { x: 0, y: 0 };
      return;
    }
    const lag = 0.1 + (99 - e.r.manCov) * 0.006 + (99 - e.r.agility) * 0.002;
    const rv = this.delayedVel(r, lag);
    // trail with inside/over leverage: stay slightly deeper (+x) on deep routes
    const depth = r.pos.x - this.los;
    const cushion = depth > 8 ? 1.4 + (e.pos.x < r.pos.x ? 1 : 0) : 0.3;
    const inside = Math.sign(this.by - r.pos.y) * 0.4;
    const tgt = add(add(r.pos, scale(rv, 0.35)), { x: cushion, y: inside });
    const d = dist(tgt, e.pos);
    e.want = scale(norm(sub(tgt, e.pos)), Math.min(e.vmax, d * 4 + len(rv)));
  }

  private thinkZone(e: Ent) {
    if (this.reactToBall(e)) return;
    const spot = e.zoneSpot ?? this.zoneLandmark(e.zone ?? 'hookM');
    const deep = !!e.zone && /^(deep|third|half|q)/.test(e.zone);
    // find receivers threatening my area
    let threat: Ent | null = null;
    let tScore = 1e9;
    const radius = deep ? 17 : 7;
    for (const r of this.off) {
      if (r.state !== 'route') continue;
      const dd = deep ? Math.abs(r.pos.y - spot.y) : dist(r.pos, spot);
      if (dd > radius) continue;
      if (deep && r.pos.x < this.los + 5) continue;
      // deep defenders: deepest threat heading into my area (velocity toward my landmark counts)
      const toward = deep ? dot(r.vel, norm({ x: 0, y: spot.y - r.pos.y })) : 0;
      const s = deep ? -r.pos.x - r.vel.x * 0.6 + dd * 0.25 - toward * 0.8 : dd;
      if (s < tScore) {
        tScore = s;
        threat = r;
      }
    }
    let tgt = spot;
    if (threat) {
      const lag = 0.15 + (99 - e.r.zoneCov) * 0.008;
      const rv = this.delayedVel(threat, lag);
      const tp = add(threat.pos, scale(rv, 0.45));
      if (deep) {
        // stay over the top: cushion grows with the receiver's speed (bail technique)
        const cushion = 2.5 + threat.speed * 0.45;
        tgt = { x: Math.max(spot.x - 2, tp.x + cushion), y: spot.y * 0.3 + tp.y * 0.7 };
      } else {
        // sit underneath, between receiver and QB, but don't leave the zone too far
        const toQB = norm(sub(this.qb.pos, tp));
        const under = add(tp, scale(toQB, 1.2));
        const off = sub(under, spot);
        const ol = len(off);
        tgt = ol > radius * 0.8 ? add(spot, scale(off, (radius * 0.8) / ol)) : under;
      }
    } else if (!deep) {
      // eyes on QB: drift toward where the QB is looking (crudely: current read)
      tgt = spot;
    }
    // play-action: underneath defenders bite on the fake
    if (this.cfg.offPlay.type === 'playaction' && this.t < 0.85 && !deep) {
      const bite = (99 - e.r.playRec) / 99;
      tgt = add(e.pos, { x: -2 * bite, y: 0 });
    }
    const d = dist(tgt, e.pos);
    e.want = scale(norm(sub(tgt, e.pos)), Math.min(e.vmax, d * (deep ? 5 : 3.5)));
  }

  /** Shared defensive reaction: run read, ball in air, ball carrier. Returns true if handled. */
  private reactToBall(e: Ent): boolean {
    const b = this.ball;
    if (b.mode === 'air' && b.target !== null) {
      if (this.t - this.throwStart > e.reactT + (e.state === 'man' ? 0.12 : 0)) {
        const spot = this.ballLanding();
        const dd = dist(spot, e.pos);
        e.want = scale(norm(sub(spot, e.pos)), Math.min(e.vmax, dd * 4 + 0.5));
        return true;
      }
      return false;
    }
    const h = b.holder;
    if (h && h.side === 'off' && h.state === 'carrier') {
      const runRead = this.t - this.runReadT > e.reactT + (this.cfg.offPlay.type === 'run' ? 0 : 0.1);
      const crossed = h.pos.x > this.los + 0.5;
      if (runRead || crossed || h !== this.carrier || this.passThrown || this.scramble) {
        // defenders in deep zones keep depth briefly on runs to avoid getting beat
        e.state = 'pursue';
        return true;
      }
    }
    if (b.mode === 'loose') {
      e.want = scale(norm(sub({ x: b.pos.x, y: b.pos.y }, e.pos)), e.vmax);
      return true;
    }
    return false;
  }

  private thinkPursue(e: Ent) {
    const b = this.ball;
    if (b.mode === 'air') {
      const spot = this.ballLanding();
      e.want = scale(norm(sub(spot, e.pos)), Math.min(e.vmax, dist(spot, e.pos) * 4 + 0.5));
      return;
    }
    if (b.mode === 'loose') {
      e.want = scale(norm(sub({ x: b.pos.x, y: b.pos.y }, e.pos)), e.vmax);
      return;
    }
    const c = b.holder;
    if (!c || c.side === e.side) {
      e.want = { x: 0, y: 0 };
      return;
    }
    if (e.engaged) return;
    // pursuit angle: better pursuit -> better intercept estimate; poor -> chase the hip
    // aim where the carrier will be when we arrive (bounded lead); good pursuit = better lead estimate
    const d = dist(e.pos, c.pos);
    const ip = interceptPoint(e.pos, c.pos, c.vel, e.vmax, 3);
    const tInt = dist(e.pos, ip) / e.vmax;
    const k = 0.75 + e.r.pursuit * 0.0035;
    // close in: cut the angle hard (aim almost at the body) instead of running parallel
    const lead = Math.min(tInt, (d / e.vmax) * 1.1 + 0.1) * k;
    const tgt = d < 1.2 ? c.pos : add(c.pos, scale(c.vel, lead));
    e.want = scale(norm(sub(tgt, e.pos)), e.vmax);
  }

  private thinkConvoy(e: Ent) {
    const c = this.ball.holder;
    if (this.ball.mode === 'air') {
      const spot = this.ballLanding();
      e.want = scale(norm(sub(spot, e.pos)), e.vmax * 0.7);
      return;
    }
    if (!c || c.side !== e.side) {
      // turnover: our side must now tackle
      e.state = 'pursue';
      return;
    }
    if (e.engaged) return;
    // block the nearest threat to the carrier
    let best: Ent | null = null;
    let bs = 1e9;
    for (const d of this.defendersOf(e)) {
      if (!d.free) continue;
      const dc = dist(d.pos, c.pos);
      const de = dist(d.pos, e.pos);
      if (de > 8 || dc > 12) continue;
      const s = de + dc * 0.6;
      if (s < bs) {
        bs = s;
        best = d;
      }
    }
    if (best) {
      const p = add(best.pos, scale(norm(sub(c.pos, best.pos)), 0.8));
      e.want = scale(norm(sub(p, e.pos)), e.vmax);
      if (dist(best.pos, e.pos) < e.radius + best.radius + 0.25 && this.rng.chance(0.25)) this.engage(e, best);
    } else {
      const gd = this.goalDir(c);
      e.want = scale(norm(sub({ x: c.pos.x + gd * 3, y: c.pos.y }, e.pos)), e.vmax * 0.8);
    }
  }

  private thinkCarrier(e: Ent) {
    const gd = this.goalDir(e);
    const goal = gd > 0 ? 100 : 0;
    const play = this.cfg.offPlay;
    const preLOS = e.side === 'off' && e.pos.x < this.los - 0.3 && !this.passThrown && e !== this.qb;
    const cands: number[] = [];
    for (let a = -85; a <= 85; a += 10) cands.push((a * Math.PI) / 180);
    let best = 0;
    let bestScore = -1e9;
    const vis = e.r.vision;
    const foes = this.defendersOf(e);
    const aimY = play.run ? this.by + (this.cfg.flip ? -play.run.aimDy : play.run.aimDy) : e.pos.y;
    for (const a of cands) {
      const dir = fromAngle(gd > 0 ? a : Math.PI - a);
      let danger = 0;
      let gain = 0;
      for (let k = 1; k <= 5; k++) {
        const p = add(e.pos, scale(dir, k * 1.3));
        if (p.y < 0.6 || p.y > FIELD_W - 0.6) {
          danger += 2.5;
          continue;
        }
        for (const d of foes) {
          if (d.state === 'down') continue;
          const w = d.engaged ? 0.25 : d.stun > 0 ? 0.3 : 1;
          const dd = dist(d.pos, p) - k * 0.35;
          danger += (w * Math.exp(-(dd * dd) / 3.2)) / k;
        }
      }
      gain = dir.x * gd;
      let score = gain * 1.6 - danger * 2.4;
      if (preLOS) score -= Math.abs(e.pos.y + dir.y * 3 - aimY) * (0.05 + (vis < 70 ? 0.08 : 0));
      score += this.rng.normal(0, 0.05 + (99 - vis) * 0.009);
      if (score > bestScore) {
        bestScore = score;
        best = a;
      }
    }
    const dir = fromAngle(gd > 0 ? best : Math.PI - best);
    // smooth heading changes (vision = decisive cuts)
    const cur = e.speed > 1 ? norm(e.vel) : dir;
    const blend = 0.25 + vis * 0.004;
    const h = norm(add(scale(cur, 1 - blend), scale(dir, blend)));
    e.want = scale(h, e.vmax);
    // situational: get out of bounds to stop the clock or go down to protect a lead is decided by game layer
    if (Math.abs(goal - e.pos.x) < 0.1) e.want = { x: gd * e.vmax, y: 0 };
    // AI juke/spin when a single tackler is right in front
    if (!e.userControlled && e.animT > 0.8) {
      for (const d of foes) {
        if (!d.free) continue;
        const rel = sub(d.pos, e.pos);
        const ahead = rel.x * gd;
        if (ahead > 0.8 && ahead < 2.4 && Math.abs(rel.y) < 1.2 && this.rng.chance(0.004 + Math.max(0, e.r.elusiveness - 60) * 0.0003)) {
          const side = rel.y > 0 ? -1 : 1;
          e.vel = add(scale(e.vel, 0.6), { x: 0, y: side * 2.8 * (0.7 + e.r.agility / 330) });
          e.anim = 'juke';
          e.animT = 0;
          break;
        }
      }
    }
  }

  // ------------------------------------------------------------------ physics
  private move(e: Ent, dt: number) {
    if (e.engaged) return; // engaged pairs are moved by updateEngagements
    if (e.state === 'down') {
      e.vel = scale(e.vel, 0.85);
      e.pos = add(e.pos, scale(e.vel, dt));
      return;
    }
    const vd = e.want;
    const s = e.speed;
    const dir = s > 0.25 ? scale(e.vel, 1 / s) : norm(vd);
    const dv = sub(vd, e.vel);
    const par = dot(dv, dir);
    const perp = sub(dv, scale(dir, par));
    const A = e.vmax / e.tau;
    const aFwd = A * Math.max(0.12, 1 - s / e.vmax);
    const aBrake = A * 1.35;
    const parC = clamp(par, -aBrake * dt, aFwd * dt);
    const pl = len(perp);
    const lat = e.latA * (s < 2.5 ? 1.8 : 1) * dt;
    const perpC = pl > lat ? scale(perp, lat / pl) : perp;
    e.vel = add(e.vel, add(scale(dir, parC), perpC));
    const ns = len(e.vel);
    const vcap = e.vmax * (e.hasBall && e.state === 'carrier' ? 0.96 : 1);
    if (ns > vcap) e.vel = scale(e.vel, vcap / ns);
    e.pos = add(e.pos, scale(e.vel, dt));
    if (ns > 0.4) e.face = angleOf(e.vel);
    else if (e.side === 'def') e.face = Math.PI;
    if (e.anim !== 'throw' && e.anim !== 'juke' && e.anim !== 'tackle' || e.animT > 0.5) e.anim = ns > 0.6 ? 'run' : 'idle';
    if (e.engaged === null && e.state === 'passblock' && ns < 2) e.anim = 'block';
  }

  private engage(blocker: Ent, d: Ent) {
    if (blocker.engaged || d.engaged && d.engaged !== blocker && d.engagedBy.length >= 2) return;
    if (blocker.beatBy.has(d)) return;
    if (blocker.stun > 0 || d.stun > 0) return;
    // defenders in space can slip a block entirely (smaller, quicker players more so)
    const inSpace = d.pos.x > this.los + 2.5 || Math.abs(d.pos.y - this.by) > 7;
    if (inSpace && d.engagedBy.length === 0) {
      const avoid = clamp(0.15 + (d.r.agility - blocker.r.agility) * 0.008 + (blocker.mass - d.mass) * 0.004, 0.05, 0.6);
      if (this.rng.chance(avoid)) {
        blocker.stun = 0.4;
        return;
      }
    }
    blocker.engaged = d;
    blocker.engT = 0;
    d.engagedBy.push(blocker);
    if (!d.engaged) d.engaged = blocker;
    blocker.engageEdge = this.rng.normal(0, 7);
    blocker.anim = 'block';
    d.anim = 'block';
    if (!this.firstContact) this.firstContact = true;
  }

  private disengage(b: Ent) {
    const d = b.engaged;
    if (!d) return;
    b.engaged = null;
    d.engagedBy = d.engagedBy.filter((x) => x !== b);
    if (d.engaged === b) d.engaged = d.engagedBy[0] ?? null;
  }

  private updateEngagements(dt: number) {
    for (const d of this.ents) {
      if (!d.engagedBy.length) continue;
      const blockers = d.engagedBy;
      const holder = this.ball.holder;
      const passPro = blockers[0].state === 'passblock';
      // defender's goal
      let goal: V2;
      if (passPro && this.ball.holder === this.qb) goal = this.qb.pos;
      else if (holder && holder.side !== d.side) goal = holder.pos;
      else goal = { x: d.pos.x - this.goalDir(blockers[0]) * 3, y: d.pos.y };
      const toGoal = norm(sub(goal, d.pos));
      // contest: defender skill vs blockers
      const dSkill = passPro
        ? Math.max(d.r.passRush * 0.7 + d.r.strength * 0.3, d.r.passRush * 0.6 + d.r.agility * 0.25 + d.r.accel * 0.15)
        : d.r.blockShed * 0.6 + d.r.strength * 0.4;
      let bSkill = 0;
      for (const b of blockers) bSkill += passPro ? b.r.passBlock * 0.7 + b.r.strength * 0.3 : b.r.runBlock * 0.65 + b.r.strength * 0.35;
      const nb = blockers.length;
      const avgB = bSkill / nb;
      const edge = blockers[0].engageEdge;
      const massAdv = (d.mass - blockers.reduce((s, b) => s + b.mass, 0) / nb) * 0.08;
      const diff = dSkill - avgB - edge + massAdv - (nb - 1) * 14;
      // movement of the pile
      const push = clamp(diff / 25, -1, 1);
      const pileV = push > 0
        ? scale(toGoal, push * (passPro ? 1.3 : 1.6))
        : scale(norm({ x: -toGoal.x, y: -toGoal.y * 0.3 }), -push * (passPro ? 0.35 : 1.6));
      // defender also slides laterally toward the goal when not winning
      const lateral = { x: 0, y: toGoal.y * 0.6 };
      let v = add(pileV, lateral);
      // pocket: pass blockers give ground in their sets, so the fight happens near the QB's depth
      if (passPro && dist(d.pos, goal) > 3.0) v = add(v, scale(toGoal, 1.1 * Math.max(0.3, 1 - blockers[0].engT / 2.2)));
      d.vel = v;
      d.pos = add(d.pos, scale(v, dt));
      d.face = angleOf(toGoal);
      if (d.anim !== 'block' && (d.anim !== 'tackle' || d.animT > 0.4)) d.anim = 'block';
      for (const b of blockers) {
        if (b.anim !== 'block') b.anim = 'block';
        const offset = scale(norm(sub(b.pos, d.pos)), b.radius + d.radius);
        b.pos = add(d.pos, offset);
        b.vel = v;
        b.face = angleOf(sub(d.pos, b.pos));
      }
      // shed hazard: skill matchup plus blocks degrade the longer they're held
      for (const b of blockers) b.engT += dt;
      const base = passPro ? 0.1 : 0.18;
      const holderNear = holder && holder.side !== d.side && dist(holder.pos, d.pos) < 1.6;
      const decay = Math.max(0, blockers[0].engT - (passPro ? 2.6 : 1.6)) * 0.5;
      const lambda = (base * Math.exp(diff / 17) + decay) * (holderNear ? 3.2 : 1);
      if (this.rng.chance(1 - Math.exp(-lambda * dt))) {
        // shed! the beaten blocker is left behind and can't re-engage this rusher
        for (const b of [...blockers]) {
          this.disengage(b);
          b.stun = (passPro ? 0.6 : 0.35) + this.rng.range(0, 0.3);
          b.beatBy.add(d);
        }
        if (passPro) d.pos = add(d.pos, scale(toGoal, 0.5));
        d.engaged = null;
        d.engagedBy = [];
        d.stun = 0;
        d.vel = scale(toGoal, 2.5);
        d.anim = 'run';
      }
    }
  }

  private collide() {
    const es = this.ents;
    for (let i = 0; i < es.length; i++) {
      const a = es[i];
      if (a.state === 'down') continue;
      for (let j = i + 1; j < es.length; j++) {
        const b = es[j];
        if (b.state === 'down') continue;
        if (a.engaged === b || b.engaged === a) continue;
        const dx = b.pos.x - a.pos.x;
        const dy = b.pos.y - a.pos.y;
        const rr = a.radius + b.radius;
        const d2 = dx * dx + dy * dy;
        if (d2 >= rr * rr || d2 < 1e-8) continue;
        const d = Math.sqrt(d2);
        const n = { x: dx / d, y: dy / d };
        const pen = rr - d;
        const wa = b.mass / (a.mass + b.mass);
        const wb = 1 - wa;
        a.pos = sub(a.pos, scale(n, pen * wa));
        b.pos = add(b.pos, scale(n, pen * wb));
        // inelastic: damp approach velocity
        const rv = dot(sub(b.vel, a.vel), n);
        if (rv < 0) {
          const imp = rv * 0.6;
          a.vel = add(a.vel, scale(n, imp * wa));
          b.vel = sub(b.vel, scale(n, imp * wb));
        }
      }
    }
  }

  // ------------------------------------------------------------------ ball
  private updateBall(dt: number) {
    const b = this.ball;
    if (b.mode === 'held' && b.holder) {
      const h = b.holder;
      b.pos = { x: h.pos.x + Math.cos(h.face) * 0.25, y: h.pos.y + Math.sin(h.face) * 0.25, z: 1.25 };
      return;
    }
    if (b.mode === 'air') {
      b.t += dt;
      const sub2 = 4;
      for (let i = 0; i < sub2; i++) this.integrate(b.pos, b.vel, this.dragK(), dt / sub2);
      b.spin += dt * 40;
      this.resolveAir();
      if (b.mode === 'air' && b.pos.z <= 0.05) {
        this.ev('incomplete', b.target ?? undefined);
        this.endPlay('incomplete');
      }
      return;
    }
    if (b.mode === 'loose') {
      b.t += dt;
      // bouncing ball
      b.vel.z -= G * dt;
      b.pos.x += b.vel.x * dt;
      b.pos.y += b.vel.y * dt;
      b.pos.z += b.vel.z * dt;
      if (b.pos.z < 0.1) {
        b.pos.z = 0.1;
        b.vel.z = Math.abs(b.vel.z) * 0.4;
        b.vel.x *= 0.7 + this.rng.range(-0.2, 0.2);
        b.vel.y = b.vel.y * 0.7 + this.rng.range(-1.5, 1.5);
      }
      if (b.pos.y < 0 || b.pos.y > FIELD_W || b.pos.x < -10 || b.pos.x > 110) {
        // out of bounds: last possession team keeps it (simplified)
        this.endPlay('oob');
        return;
      }
      // recovery attempts
      if (b.t > 0.25) {
        const near = this.ents
          .filter((e) => e.free && dist(e.pos, b.pos) < 0.9)
          .sort((x, y) => dist(x.pos, b.pos) - dist(y.pos, b.pos));
        for (const e of near) {
          if (this.rng.chance(0.18 + e.r.awareness * 0.002)) {
            this.ev('recover', e);
            this.recoveredBy = e;
            this.setCarrier(e);
            // pile-up: most recoveries are downed quickly
            if (this.rng.chance(0.75)) {
              e.state = 'down';
              this.endPlay('tackle');
            }
            break;
          }
        }
      }
    }
  }
  recoveredBy: Ent | null = null;
  fumbler: Ent | null = null;
  intBy: Ent | null = null;
  pbuBy: Ent | null = null;
  sackBy: Ent | null = null;
  forcedBy: Ent | null = null;
  tacklers: Ent[] = [];
  catcher: Ent | null = null;
  passer: Ent | null = null;
  deepContested = false;
  thrownAway = false;

  private resolveAir() {
    const b = this.ball;
    if (b.vel.z > 0 && b.t < 0.25) return;
    if (b.pos.z > 3.3) return;
    // candidates within reach
    const cands: { e: Ent; d: number }[] = [];
    for (const e of this.ents) {
      if (e === b.thrower || !e.free && !(e.engaged && e.side === 'def' && isDL(e.role) && b.t < 0.4)) continue;
      const reach = e.p.heightIn / 36 * 0.5 + 0.55 + (e.r.ballSkills > 60 && e.side === 'def' ? 0.1 : 0);
      const d = Math.hypot(e.pos.x - b.pos.x, e.pos.y - b.pos.y);
      const zOk = b.pos.z < e.p.heightIn / 36 + 0.9 && b.pos.z > 0.2;
      if (d < reach && zOk) cands.push({ e, d });
    }
    if (!cands.length) return;
    // batted at the line
    const early = cands.find((c) => c.e.side === 'def' && isDL(c.e.role));
    if (early && b.t < 0.35) {
      if (this.rng.chance(0.25)) {
        this.ev('batted', early.e);
        this.pbuBy = early.e;
        b.vel = { x: this.rng.range(-3, 3), y: this.rng.range(-3, 3), z: 2 };
        b.target = null;
      }
      return;
    }
    // each candidate gets one try per throw, at their closest approach to the ball
    cands.sort((a, c) => a.d - c.d);
    for (const { e, d } of cands) {
      if (this.reached.has(e)) continue;
      const last = this.lastBallD.get(e);
      this.lastBallD.set(e, d);
      const descendingLow = b.vel.z < 0 && b.pos.z < 0.9;
      if (d > 0.35 && !descendingLow && (last === undefined || d < last - 0.005)) continue;
      this.reached.set(e, this.t);
      const deepBall = b.pos.x - this.los > 15;
      const bp = { x: b.pos.x, y: b.pos.y };
      const oppNear = this.ents.filter((o) => o.side !== e.side && o.free && dist(o.pos, e.pos) < (deepBall ? 2.2 : 1.3));
      const contested = oppNear.length > 0;
      const airYds = b.pos.x - this.los;
      if (e.side === 'off') {
        if (!e.eligible) continue;
        // catch probability
        const skill = contested ? e.r.catchInTraffic * 0.6 + e.r.catching * 0.4 : e.r.catching;
        const off = d / 1.2; // how far off-target relative to reach
        const ballSpeed = Math.hypot(b.vel.x, b.vel.y, b.vel.z);
        const tough = off * 1.2 + (ballSpeed > 24 ? 0.25 : 0) + (contested ? 0.7 : 0) + (b.pos.z > 2.6 ? 0.5 : 0) + (b.pos.z < 0.6 ? 0.4 : 0);
        const wet = this.cfg.weather.precip !== 'none' ? 0.25 : 0;
        let p = sigmoid(4.2 + (skill - 75) / 9 - tough * 1.5 - wet);
        // defenders arriving with the ball can break it up
        if (contested) {
          // best-positioned defender: closest to the ball at arrival
          const dbest = oppNear.reduce((m, o) => (dist(o.pos, bp) < dist(m.pos, bp) ? o : m), oppNear[0]);
          const edge = d - dist(dbest.pos, bp); // >0: defender closer to the ball than the receiver
          const pbu = sigmoid((dbest.r.ballSkills - 70) / 12 + edge * 1.6 - 1.2 + (deepBall ? 0.35 : 0)) * (deepBall ? 0.6 : 0.42);
          if (this.rng.chance(pbu)) {
            this.pbuBy = dbest;
            this.deepContested = airYds > 15;
            this.ev('pbu', dbest, e);
            b.vel = scale3(b.vel, 0.25);
            b.vel.z = 1.5;
            b.target = null;
            // defender in great position may come down with it; or tipped and picked
            const pInt = sigmoid((dbest.r.catching - 62) / 10 + edge * 1.2 - 1.4);
            if (this.rng.chance(pInt)) {
              this.intercept(dbest);
            }
            return;
          }
          if (airYds > 15) this.deepContested = true;
        }
        if (this.rng.chance(p)) {
          // feet in bounds?
          if (e.pos.y < 0.15 || e.pos.y > FIELD_W - 0.15 || e.pos.x > 110) {
            this.ev('oobcatch', e);
            this.endPlay('incomplete');
            return;
          }
          this.catcher = e;
          this.catchX = e.pos.x;
          this.ev('catch', e);
          e.anim = 'catch';
          e.animT = 0;
          // momentum loss when catching
          e.vel = scale(e.vel, contested ? 0.3 : 0.45);
          this.setCarrier(e);
          return;
        }
        this.ev(contested ? 'contested_inc' : 'drop', e);
        b.vel = { x: b.vel.x * 0.2, y: b.vel.y * 0.2 + this.rng.range(-1, 1), z: 1.2 };
        b.target = null;
        return;
      } else {
        // defender: interception or breakup
        const tgt = b.target;
        const bp = { x: b.pos.x, y: b.pos.y };
        const rd = tgt ? dist(tgt.pos, bp) : 9;
        const edge = clamp(rd - d, -2, 1.5); // defender beat the receiver to the ball
        const pPlay = sigmoid((e.r.ballSkills - 70) / 10 + edge * 1.0 - 1.1 - d * 0.8 - (b.pos.z > 2.5 ? 0.8 : 0));
        if (!this.rng.chance(pPlay)) continue;
        const pInt = sigmoid((e.r.catching - 62) / 10 + edge * 0.6 - 1.2);
        if (this.rng.chance(pInt)) {
          this.intercept(e);
        } else {
          this.pbuBy = e;
          this.ev('pbu', e, tgt ?? undefined);
          b.vel = { x: b.vel.x * 0.2 + this.rng.range(-2, 2), y: b.vel.y * 0.2 + this.rng.range(-2, 2), z: 2 };
          b.target = null;
          if (airYds > 15) this.deepContested = true;
        }
        return;
      }
    }
  }

  private intercept(e: Ent) {
    this.intBy = e;
    this.ev('int', e);
    e.anim = 'catch';
    e.animT = 0;
    // switch everybody: defense now carries
    for (const o of this.off) if (o.state !== 'down') { o.state = 'pursue'; if (o.engaged) this.disengage(o); o.engagedBy = []; }
    for (const d of this.def) { if (d.engaged) this.disengage(d); d.engagedBy = []; d.engaged = null; }
    this.setCarrier(e);
    for (const d of this.def) if (d !== e && d.state !== 'down') d.state = 'convoy';
  }

  // ------------------------------------------------------------------ tackling
  private checkTackles() {
    const c = this.ball.holder;
    if (!c || this.ball.mode !== 'held') return;
    // QB in pocket is a sack target; others are carriers
    const foes = this.defendersOf(c);
    let gang = 0;
    for (const d of foes) {
      if (d.state === 'down' || d.stun > 0) continue;
      const dd = dist(d.pos, c.pos);
      const qbPocket = c === this.qb && !this.passThrown && !this.scramble && this.cfg.offPlay.type !== 'run';
      const range = c.radius + d.radius + (d.userControlled && this.input.dive ? 1.0 : qbPocket ? 0.3 : 0.52);
      if (d.engaged) {
        // engaged defenders can still reach out for a nearby carrier
        if (dd < range + 0.1 && d.tackleCd <= 0 && this.rng.chance(0.06 + d.r.blockShed * 0.0012)) {
          if (this.tryTackle(d, c, gang, true)) return;
        }
        continue;
      }
      if (d.tackleCd > 0) continue;
      if (dd > range) {
        // diving / shoestring tackle from just out of reach when the carrier isn't pulling away
        if (dd < range + 1.1) {
          const away = norm(sub(c.pos, d.pos));
          const sep = dot(sub(c.vel, d.vel), away);
          // only a last resort: carrier is pulling away / as fast as me, or I'm trailing
          const behind = dot(norm(sub(d.pos, c.pos)), c.speed > 0.5 ? norm(c.vel) : { x: this.goalDir(c), y: 0 }) < -0.2;
          const cantClose = sep > -0.4 && (behind || c.vmax >= d.vmax * 0.98);
          if (cantClose && sep < 1.5 && this.rng.chance(0.05) && this.clearPath(d, c)) {
            if (this.tryTackle(d, c, gang + 1, false, true)) return;
          }
        }
        continue;
      }
      gang++;
      if (this.tryTackle(d, c, gang, false)) return;
    }
  }

  /** No other body between tackler and carrier (can't dive through a lineman). */
  private clearPath(d: Ent, c: Ent): boolean {
    const ab = sub(c.pos, d.pos);
    const L2 = dot(ab, ab);
    for (const o of this.ents) {
      if (o === d || o === c || o.state === 'down') continue;
      const t = clamp(dot(sub(o.pos, d.pos), ab) / L2, 0, 1);
      if (t <= 0.05 || t >= 0.95) continue;
      if (dist(o.pos, add(d.pos, scale(ab, t))) < o.radius + 0.15) return false;
    }
    return true;
  }

  private tryTackle(d: Ent, c: Ent, gang: number, arm: boolean, dive = false): boolean {
    const gd = this.goalDir(c);
    const rel = norm(sub(d.pos, c.pos));
    const cdir = c.speed > 0.5 ? norm(c.vel) : { x: gd, y: 0 };
    const facing = dot(rel, cdir); // +1 = defender in front
    const mc = c.mass;
    const md = d.mass;
    const vc = c.speed;
    const vd = d.speed;
    const carrierSkill = facing > 0.3 ? c.r.breakTackle * 0.6 + c.r.strength * 0.4 : c.r.elusiveness * 0.7 + c.r.agility * 0.3;
    const dropback = this.cfg.offPlay.type !== 'run';
    const isSack = dropback && c === this.qb && c.pos.x < this.los && !this.passThrown && !this.scramble;
    let x = 2.6 + (d.r.tackling - 70) / 13 - (carrierSkill - 70) / 15;
    x += facing > 0.3 ? 0.35 : facing < -0.3 ? -0.55 : 0;
    x += -Math.log(mc / md) * 1.4;
    x += -(vc / Math.max(1, c.vmax)) * 0.35 + (vd / Math.max(1, d.vmax)) * 0.2;
    x += (gang - 1) * 0.9;
    if (arm) x -= 1.1;
    if (dive) x -= 1.3;
    if (isSack) x += 0.6;
    if (c.anim === 'juke' && c.animT < 0.35) x -= 0.6 + Math.max(0, c.r.elusiveness - 70) * 0.02;
    if (c.userControlled && this.input.juke) x -= 0.4;
    const p = sigmoid(x);
    if (PlaySim.tackleLog) PlaySim.tackleLog.push({ p, facing, dive, arm, gang, dx: c.pos.x - this.los, vc, juke: c.anim === 'juke' });
    d.tackleCd = 0.55;
    d.anim = 'tackle';
    d.animT = 0;
    if (this.rng.chance(p)) {
      // tackle! forward progress from momentum
      const pc = scale(c.vel, mc);
      const pd = scale(d.vel, md);
      const vComb = scale(add(pc, pd), 1 / (mc + md));
      let fall = vComb.x * gd * 0.38 + (c.r.strength - d.r.strength) * 0.006 + this.rng.normal(0.35, 0.35);
      fall = clamp(fall, -1.5, 2.6);
      if (isSack) fall = Math.min(fall, 0.3);
      // yards after contact: ball carriers drive the pile / fall forward unless gang-tackled
      if (!isSack && vComb.x * gd > -1) {
        let help = 0;
        for (const o of this.defendersOf(c)) if (o !== d && o.free && dist(o.pos, c.pos) < 2.2) help++;
        const mu = 0.45 + (c.r.breakTackle * 0.5 + c.r.strength * 0.5 - d.r.tackling) * 0.025 + (mc - md) * 0.012 + c.speed * 0.07 - help * 0.55 - (facing > 0.3 ? 0.25 : 0);
        fall += clamp(this.rng.normal(mu, 0.75), 0, 4);
      }
      // QB sneak: the whole line surges, QB lunges over the pile
      if (this.cfg.offPlay.run?.style === 'sneak' && c === this.qb) fall += 0.9 + this.rng.range(0, 0.8) + (c.r.strength - 60) * 0.01;
      c.pos = { x: c.pos.x + fall * gd, y: c.pos.y + vComb.y * 0.25 };
      c.state = 'down';
      c.anim = 'down';
      c.animT = 0;
      d.state = 'down';
      d.anim = 'tackle';
      this.tacklers.push(d);
      // assist: another defender right there
      for (const o of this.defendersOf(c)) {
        if (o !== d && o.free && dist(o.pos, c.pos) < 1.4) {
          this.tacklers.push(o);
          break;
        }
      }
      if (isSack) this.sackBy = d;
      // fumble?
      const hit = (md * vd) / 900;
      const pf = 0.0105 * Math.exp((70 - c.r.carrying) / 16) * (0.6 + hit) * (isSack ? 3.5 : 1) * (this.cfg.weather.precip !== 'none' ? 1.3 : 1);
      if (this.rng.chance(pf) && !(isSack && this.rng.chance(0.3))) {
        this.fumble(c, d);
        return true;
      }
      this.endPlay(isSack ? 'sack' : 'tackle');
      return true;
    }
    // missed / broken tackle
    this.brokenTackles++;
    this.ev(dive ? 'divemiss' : 'broken', c, d);
    d.stun = (dive ? 1.0 : 0.6) + this.rng.range(0, 0.6);
    if (dive) d.anim = 'dive';
    d.state = d.state === 'rush' ? 'pursue' : d.state;
    c.vel = scale(c.vel, dive ? 0.82 : 0.68);
    c.animT = 0;
    return false;
  }

  private fumble(c: Ent, by: Ent) {
    this.fumbler = c;
    this.forcedBy = by;
    this.ev('fumble', c, by);
    c.hasBall = false;
    c.state = 'down';
    const b = this.ball;
    b.mode = 'loose';
    b.holder = null;
    b.t = 0;
    b.vel = { x: this.rng.range(-4, 4), y: this.rng.range(-4, 4), z: 3 };
    b.pos = { x: c.pos.x, y: c.pos.y, z: 1 };
    this.carrier = null;
    // engaged pairs break up
    for (const e of this.ents) {
      if (e.engaged) this.disengage(e);
      e.engagedBy = [];
      if (e.state !== 'down') e.state = 'pursue';
    }
  }

  private checkBounds() {
    const c = this.ball.holder;
    if (!c || this.ball.mode !== 'held') return;
    if (c.pos.y < 0 || c.pos.y > FIELD_W) {
      this.endPlay('oob');
      return;
    }
    const gd = this.goalDir(c);
    if (gd > 0 && c.pos.x >= 100) this.endPlay('td');
    else if (gd < 0 && c.pos.x <= 0) this.endPlay('td');
    else if (c.pos.x > 110 || c.pos.x < -10) this.endPlay('oob');
  }

  // ------------------------------------------------------------------ end of play
  endReason = '';
  endPlay(reason: string) {
    if (this.phase === 'dead') return;
    this.phase = 'dead';
    this.endReason = reason;
    const b = this.ball;
    const holder = b.holder;
    const play = this.cfg.offPlay;
    const L = this.los;
    let endX = holder ? holder.pos.x : L;
    let turnover = false;
    let td: Side | null = null;
    let safety = false;
    let touchback = false;
    const incomplete = reason === 'incomplete' || reason === 'spike';
    if (incomplete) endX = L;
    if (holder && holder.side === 'def') turnover = true;
    if (reason === 'td') td = holder!.side;
    // a defensive player downed in his own end zone after a takeaway = touchback
    if (turnover && !td && endX >= 100) {
      touchback = true;
    }
    if (!turnover && holder && !td && endX <= 0 && reason !== 'incomplete') safety = true;
    // loose ball out of bounds: offense keeps at spot
    if (reason === 'oob' && b.mode === 'loose') {
      endX = clamp(b.pos.x, 0.5, 99.5);
    }
    endX = clamp(endX, -10, 110);
    let kind: PlayResult['kind'] = play.type === 'run' ? 'run' : 'pass';
    if (play.type === 'kneel') kind = 'kneel';
    if (play.type === 'spike') kind = 'spike';
    if (reason === 'sack' || (this.sackBy && !this.passThrown)) kind = 'sack';
    if (this.scramble && !this.passThrown) kind = 'scramble';
    if (kind === 'kneel') endX = L - 1;
    const passer = play.type !== 'run' && kind !== 'kneel' && kind !== 'spike' ? this.qb.p : undefined;
    const tgt = this.ball.target ?? this.events.find((e) => e.type === 'throw')?.b;
    const fumbleLost = !!this.fumbler && turnover && !this.intBy;
    this.result = {
      kind,
      endX,
      yards: Math.round(endX - L),
      complete: !!this.catcher && !this.intBy,
      incomplete,
      interception: !!this.intBy,
      fumbleLost,
      turnover,
      td,
      safety,
      touchback,
      outOfBounds: reason === 'oob',
      duration: this.t,
      passer: this.passThrown || kind === 'sack' ? passer : undefined,
      rusher: kind === 'run' || kind === 'scramble' ? (this.carrier ?? this.fumbler ?? undefined)?.p : undefined,
      receiver: this.catcher?.p,
      target: tgt?.p,
      tacklers: this.tacklers.map((t) => t.p),
      sackedBy: this.sackBy?.p,
      intBy: this.intBy?.p,
      fumbleBy: this.fumbler?.p,
      fumbleRecBy: this.recoveredBy?.p,
      forcedBy: this.forcedBy?.p,
      pbuBy: this.pbuBy?.p,
      airYards: this.catcher ? Math.round(this.catchX - L) : this.ball.targetSpot ? Math.round(this.ball.targetSpot.x - L) : 0,
      yac: this.catcher && !this.intBy ? Math.round(endX - this.catchX) : 0,
      deepContested: this.deepContested,
      thrownAway: this.events.some((e) => e.type === 'throwaway'),
      pressured: this.pressured,
      timeToThrow: this.throwStart > 0 ? this.throwStart : this.t,
      brokenTackles: this.brokenTackles,
      targetSpotY: this.ball.targetSpot?.y,
      endY: holder ? holder.pos.y : this.by,
      events: this.events,
    };
    if (kind === 'run' && this.carrier === this.qb && play.run?.carrier !== 'QB') {
      // fumbled exchange handled as run by QB
      this.result.rusher = this.qb.p;
    }
    if (holder) {
      holder.anim = td ? 'celebrate' : holder.anim;
    }
  }
}

function scale3(v: V3, s: number): V3 {
  return { x: v.x * s, y: v.y * s, z: v.z * s };
}
