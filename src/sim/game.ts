// Game rules engine: clock, downs, scoring, penalties, special teams, CPU coordinators.
import { Rng, clamp, sigmoid } from './rng';
import type { Player, Team } from './types';
import {
  DEF_PLAYS, DefCall, FIELD_W, HASH_L, HASH_R, OFF_PLAYS, OffPlay, ROUTES, formationById, playById,
} from './playbook';

/** Depths (yards past LOS) where each play's routes break / settle. */
function routeDepths(p: OffPlay): number[] {
  const out: number[] = [];
  for (const r of p.reads ?? []) {
    const name = p.routes[r];
    const rd = name ? ROUTES[name] : undefined;
    if (!rd || !rd.pts.length) continue;
    out.push(Math.max(...rd.pts.map((q) => q[0])));
  }
  return out;
}
import { PlayConfig, PlayResult, PlaySim, Weather, Side } from './play';
import { StatBook, TeamLine, emptyTeamLine } from './stats';

export type TeamKey = 'home' | 'away';
export const other = (k: TeamKey): TeamKey => (k === 'home' ? 'away' : 'home');

export type Phase = 'kickoff' | 'scrimmage' | 'pat' | 'final';

export interface PlayLog {
  q: number;
  clock: number;
  poss: TeamKey;
  down: number;
  toGo: number;
  ballOn: number;
  desc: string;
  score: { home: number; away: number };
  big?: 'td' | 'turnover' | 'fg' | 'safety' | 'penalty' | 'info';
}

export interface SpecialResult {
  desc: string;
  /** ball flight for the renderer, offense frame of the kicking team */
  flight?: { fromX: number; toX: number; y: number; hang: number; height: number; good?: boolean };
}

export interface GameOptions {
  seed?: number;
  quarterLen?: number;
  weather?: Weather;
  userTeam?: TeamKey | null;
}

export interface Penalty {
  name: string;
  on: 'off' | 'def';
  yards: number;
  autoFirst: boolean;
  spot: 'los' | 'end' | 'spot';
  spotX?: number;
  replayDown: boolean;
}

const fmtClock = (s: number) => {
  const t = Math.max(0, Math.ceil(s));
  return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, '0')}`;
};
export { fmtClock };

const nm = (p?: Player) => (p ? `${p.first[0]}.${p.last}` : 'unknown');

/** Expected points (offense) for a down/distance/field position. Simple fit to modern NFL EP curves. */
export function expectedPoints(ballOn: number, down: number, toGo: number): number {
  const base = -1.25 + 0.066 * ballOn;
  const d = [0, -0.4, -0.95, -1.7][clamp(down, 1, 4) - 1];
  const g = -0.035 * (clamp(toGo, 1, 30) - 10);
  return base + d + g;
}

export class Game {
  home: Team;
  away: Team;
  score = { home: 0, away: 0 };
  q = 1;
  quarterLen: number;
  clock: number;
  poss: TeamKey = 'away';
  ballOn = 35;
  ballY = FIELD_W / 2;
  down = 1;
  toGo = 10;
  phase: Phase = 'kickoff';
  kicking: TeamKey;
  openingReceiver: TeamKey;
  timeouts = { home: 3, away: 3 };
  clockRunning = false;
  homeDir = 1;
  stats = new StatBook();
  team: Record<TeamKey, TeamLine> = { home: emptyTeamLine(), away: emptyTeamLine() };
  log: PlayLog[] = [];
  weather: Weather;
  rng: Rng;
  userTeam: TeamKey | null;
  twoMin: Record<number, boolean> = {};
  lastOOB = false;
  otPossDone = 0;
  driveStartBallOn = 25;
  inRedZone = false;
  lastPlayName = '';
  recent: string[] = [];
  /** pending penalty to show on next snap */
  lastPenalty: Penalty | null = null;
  injuries: Player[] = [];
  /** extra-time flag for untimed down */
  untimed = false;

  constructor(home: Team, away: Team, opts: GameOptions = {}) {
    this.home = home;
    this.away = away;
    this.rng = new Rng(opts.seed ?? Math.floor(Math.random() * 1e9));
    this.quarterLen = opts.quarterLen ?? 900;
    this.clock = this.quarterLen;
    this.weather = opts.weather ?? randomWeather(this.rng);
    this.userTeam = opts.userTeam ?? null;
    for (const p of [...home.roster, ...away.roster]) p.fatigue = 0;
    // coin toss: winner defers (modern convention)
    const winner: TeamKey = this.rng.chance(0.5) ? 'home' : 'away';
    this.kicking = winner;
    this.openingReceiver = other(winner);
    this.poss = this.openingReceiver;
    this.homeDir = this.rng.chance(0.5) ? 1 : -1;
    this.addLog(`Coin toss: ${this.teamOf(winner).abbr} wins and defers. ${this.weatherText()}`, 'info');
  }

  teamOf(k: TeamKey) {
    return k === 'home' ? this.home : this.away;
  }
  get offense() {
    return this.teamOf(this.poss);
  }
  get defense() {
    return this.teamOf(other(this.poss));
  }
  /** +1 if possessing team attacks world +x */
  worldDir(k: TeamKey = this.poss) {
    return k === 'home' ? this.homeDir : -this.homeDir;
  }
  weatherText() {
    const w = this.weather;
    const ws = Math.round(Math.hypot(w.windX, w.windY) * 2.045); // yd/s -> mph
    const p = w.precip === 'none' ? 'Clear' : w.precip === 'rain' ? 'Rain' : 'Snow';
    return `${p}, ${Math.round(w.tempF)}F, wind ${ws} mph.`;
  }

  addLog(desc: string, big?: PlayLog['big']) {
    this.log.push({
      q: this.q, clock: this.clock, poss: this.poss, down: this.down, toGo: this.toGo, ballOn: this.ballOn,
      desc, score: { ...this.score }, big,
    });
  }

  ydLine(ballOn = this.ballOn, k: TeamKey = this.poss): string {
    const t = this.teamOf(k).abbr;
    const o = this.teamOf(other(k)).abbr;
    const b = Math.round(ballOn);
    if (b === 50) return '50';
    return b < 50 ? `${t} ${b}` : `${o} ${100 - b}`;
  }

  downText() {
    const ord = ['1st', '2nd', '3rd', '4th'][this.down - 1];
    const goal = this.ballOn + this.toGo >= 100;
    return `${ord} & ${goal ? 'Goal' : Math.max(1, Math.round(this.toGo))}`;
  }

  get isFinal() {
    return this.phase === 'final';
  }

  halfSecondsLeft() {
    return this.q === 2 || this.q === 4 ? this.clock : this.q === 1 || this.q === 3 ? this.clock + this.quarterLen : this.clock;
  }

  margin(k: TeamKey = this.poss) {
    return this.score[k] - this.score[other(k)];
  }

  // ------------------------------------------------------------------ clock
  private runClock(sec: number, duringPlay = false) {
    const before = this.clock;
    this.clock -= sec;
    if ((this.q === 2 || this.q === 4) && !this.twoMin[this.q] && before > 120 && this.clock <= 120) {
      this.twoMin[this.q] = true;
      if (!duringPlay) this.clock = 120;
      this.clockRunning = false;
      this.addLog('Two-minute warning.', 'info');
    }
    if (this.clock < 0) this.clock = 0;
  }

  /** Clock stops after OOB only in final 2:00 of 1st half and final 5:00 of 2nd half. */
  private oobStops() {
    return (this.q === 2 && this.clock <= 120) || (this.q >= 4 && this.clock <= 300);
  }

  tempo(): 'normal' | 'hurry' | 'milk' {
    const m = this.margin();
    const late4 = this.q >= 4 && this.clock < 420;
    if ((this.q === 2 && this.clock < 130 && m >= -16) || (late4 && m < 0) || (this.q >= 4 && this.clock < 150 && m <= 0)) return 'hurry';
    if (this.q >= 4 && m > 0 && this.clock < 600) return 'milk';
    if (this.q === 2 && this.clock < 130 && m > 0 && this.ballOn < 40) return 'milk';
    return 'normal';
  }

  /** Called right before each snap (scrimmage). Applies runoff. Returns false if the period ended. */
  preSnap(): boolean {
    if (this.clockRunning && !this.untimed) {
      const tp = this.tempo();
      let run = tp === 'hurry' ? this.rng.range(12, 18) : tp === 'milk' ? this.rng.range(37, 40) : this.rng.range(29, 37);
      if (this.lastOOB) run = Math.max(4, run - 8);
      // timeouts by trailing defense late
      const dk = other(this.poss);
      const defTrailing = this.margin(dk) < 0 && this.margin(dk) >= -16;
      if (this.q >= 4 && this.clock < 150 && defTrailing && this.timeouts[dk] > 0) {
        this.timeouts[dk]--;
        this.addLog(`Timeout ${this.teamOf(dk).abbr} (${this.timeouts[dk]} left).`, 'info');
        run = 0;
      } else if (tp === 'hurry' && this.timeouts[this.poss] > 0 && this.clock < 50 && this.clock > 3) {
        this.timeouts[this.poss]--;
        this.addLog(`Timeout ${this.offense.abbr} (${this.timeouts[this.poss]} left).`, 'info');
        run = 0;
      } else if (this.q === 2 && this.clock < 45 && this.timeouts[this.poss] > 0 && this.ballOn > 55 && this.margin() >= -16) {
        this.timeouts[this.poss]--;
        this.addLog(`Timeout ${this.offense.abbr} (${this.timeouts[this.poss]} left).`, 'info');
        run = 0;
      }
      this.runClock(run);
    }
    this.untimed = false;
    this.lastOOB = false;
    if (this.clock <= 0) {
      this.endQuarter();
      return false;
    }
    return true;
  }

  private endQuarter() {
    this.addLog(`End of ${['1st', '2nd', '3rd', '4th', 'OT'][Math.min(this.q, 5) - 1]} quarter.`, 'info');
    this.clockRunning = false;
    if (this.q === 4 || this.q === 5) {
      if (this.score.home !== this.score.away || this.q === 5) {
        this.finish();
        return;
      }
      // overtime: 10 minutes, coin toss, receiver gets ball
      this.q = 5;
      this.clock = 600;
      this.timeouts = { home: 2, away: 2 };
      this.kicking = this.rng.chance(0.5) ? 'home' : 'away';
      this.poss = other(this.kicking);
      this.phase = 'kickoff';
      this.otPossDone = 0;
      this.addLog('Overtime! 10:00 period. Both teams are guaranteed a possession.', 'info');
      return;
    }
    this.q++;
    this.clock = this.quarterLen;
    this.homeDir *= -1;
    if (this.q === 3) {
      // halftime: rest, timeouts reset, other team receives
      this.timeouts = { home: 3, away: 3 };
      this.kicking = this.openingReceiver;
      this.poss = other(this.kicking);
      this.phase = 'kickoff';
      for (const p of [...this.home.roster, ...this.away.roster]) p.fatigue = 0;
      this.addLog('Halftime.', 'info');
    } else {
      for (const p of [...this.home.roster, ...this.away.roster]) p.fatigue *= 0.6;
    }
  }

  private finish() {
    this.phase = 'final';
    const h = this.home.abbr;
    const a = this.away.abbr;
    this.addLog(`FINAL: ${a} ${this.score.away} - ${h} ${this.score.home}`, 'info');
  }

  // ------------------------------------------------------------------ drive bookkeeping
  private changePossession(newBallOn: number, reason: string) {
    this.team[this.poss].top += 0; // TOP tracked per play
    this.poss = other(this.poss);
    this.ballOn = clamp(newBallOn, 1, 99);
    this.down = 1;
    this.toGo = Math.min(10, 100 - this.ballOn);
    this.ballY = FIELD_W / 2;
    this.clockRunning = false;
    this.driveStartBallOn = this.ballOn;
    this.inRedZone = false;
    this.phase = 'scrimmage';
    if (this.q === 5) this.otDriveEnded();
    void reason;
  }

  private otDriveEnded() {
    if (this.q !== 5 || this.phase === 'final') return;
    this.otPossDone++;
    if (this.otPossDone >= 2 && this.score.home !== this.score.away) this.finish();
  }

  private scoreTD(k: TeamKey) {
    this.score[k] += 6;
    this.phase = 'pat';
    if (k !== this.poss) {
      // defensive score: defense becomes kicking team for PAT
      this.poss = k;
      if (this.q === 5) {
        // defensive TD in OT ends it
        this.finish();
        return;
      }
    }
    this.ballOn = 85;
    this.ballY = FIELD_W / 2;
    this.clockRunning = false;
  }

  // ------------------------------------------------------------------ CPU coordinators
  shouldKneel(): boolean {
    const m = this.margin();
    if (m <= 0) return false;
    const oppTO = this.timeouts[other(this.poss)];
    const snaps = 5 - this.down;
    const burn = Math.max(0, snaps - oppTO) * 40 + snaps * 1.5;
    if (this.q >= 4 && this.clock <= burn) return true;
    if (this.q === 2 && this.clock <= 30 && this.ballOn < 60 && m > 0) return true;
    return false;
  }

  fourthDownCall(): 'go' | 'punt' | 'fg' {
    const t = this.offense;
    const fgDist = 100 - this.ballOn + 17;
    const k = this.kicker(this.poss);
    const range = 55 + (k.r.kickPower - 80) * 0.3 + this.windHelp(this.poss) * 0.6;
    const m = this.margin();
    const secs = this.q >= 4 ? this.clock : 9999;
    // desperation
    if (this.q >= 4 && m < 0) {
      if (m >= -3 && fgDist <= range && secs < 120) return 'fg';
      if (secs < 240 || (m < -8 && secs < 480)) return 'go';
    }
    if (this.q === 2 && this.clock < 20 && fgDist <= range) return 'fg';
    const ag = t.aggression;
    const tg = this.toGo;
    // analytics-flavored go chart
    if (tg <= 1 && this.ballOn >= 35 - ag * 25) return 'go';
    if (tg <= 2 && this.ballOn >= 50 - ag * 15 && this.ballOn < 80) return 'go';
    if (tg <= 3 && this.ballOn >= 60 && this.ballOn <= 70 && ag > 0.45) return 'go';
    if (tg <= 4 && this.ballOn >= 57 && fgDist > range && ag > 0.4) return 'go';
    if (fgDist <= range) {
      if (this.ballOn >= 95 && tg <= 2 && ag > 0.5) return 'go';
      return 'fg';
    }
    return 'punt';
  }

  private windHelp(k: TeamKey) {
    // tailwind in yd/s along attack direction
    return this.weather.windX * this.worldDir(k);
  }

  cpuOffense(): OffPlay {
    if (this.shouldKneel()) return playById('kneel');
    const tp = this.tempo();
    const t = this.offense;
    // spike to stop the clock
    if (tp === 'hurry' && this.clockRunning && this.timeouts[this.poss] === 0 && this.clock < 30 && this.clock > 4 && this.down < 3) return playById('spike');
    let pPass = t.passLean - 0.04;
    const d = this.down;
    const tg = this.toGo;
    if (d === 1) pPass += tg > 10 ? 0.15 : -0.03;
    if (d === 2) pPass += tg >= 7 ? 0.1 : tg <= 3 ? -0.18 : 0;
    if (d === 3) pPass += tg >= 4 ? 0.3 : tg <= 2 ? -0.18 : 0.1;
    if (d === 4) pPass += tg >= 3 ? 0.3 : -0.25;
    const m = this.margin();
    if (this.q >= 3) pPass += clamp(-m * 0.012, -0.25, 0.3);
    if (tp === 'hurry') pPass += 0.3;
    if (tp === 'milk') pPass -= 0.25;
    if (this.ballOn >= 96) pPass -= 0.1;
    pPass = clamp(pPass, 0.1, 0.95);
    const pass = this.rng.chance(pPass);
    const goal = this.ballOn >= 95;
    const short = tg <= 2;
    const long = tg >= 8;
    const pool = OFF_PLAYS.filter((p) => {
      if (p.type === 'kneel' || p.type === 'spike') return false;
      const isPass = p.type !== 'run';
      if (isPass !== pass) return false;
      if (goal && p.tags.includes('deep')) return false;
      if (!goal && p.formation === 'heavy' && !short) return false;
      if (p.id === 'gl_sneak' && !(short && tg <= 1)) return false;
      return true;
    });
    const w = pool.map((p) => {
      let x = 1;
      if (short && p.tags.includes('shortyard')) x += 2.5;
      if (goal && p.tags.includes('goalline')) x += 2;
      if (long && (p.tags.includes('long') || p.tags.includes('deep') || p.tags.includes('mid'))) x += 1.2;
      if (long && p.tags.includes('quick')) x *= 0.5;
      if (tp === 'hurry' && (p.tags.includes('quick') || p.tags.includes('short'))) x += 0.8;
      if (p.type === 'playaction' && d === 1) x += 0.6;
      if (p.type === 'screen' && long) x += 0.6;
      if (this.recent.includes(p.id)) x *= 0.4;
      // money downs: favor concepts with a route that settles at/just past the sticks
      if (d >= 3 && p.type !== 'run') {
        const ds = routeDepths(p);
        if (ds.some((q) => q >= tg && q <= tg + 7)) x *= 2.2;
        else if (ds.every((q) => q < tg - 1)) x *= 0.35;
      }
      return x;
    });
    const pick = this.rng.weighted(pool, w);
    this.recent.push(pick.id);
    if (this.recent.length > 4) this.recent.shift();
    return pick;
  }

  cpuDefense(offPlay: OffPlay): DefCall {
    const t = this.defense;
    const pers = formationById(offPlay.formation).personnel;
    const goal = this.ballOn >= 93;
    const short = this.toGo <= 2;
    const long = this.toGo >= 8 && this.down >= 2;
    let front: string;
    if (goal || (short && (pers === '22' || pers === '21'))) front = 'goalline';
    else if (pers === '21' || pers === '12' || pers === '22') front = 'base';
    else front = long && this.down === 3 && this.rng.chance(0.5) ? 'dime' : 'nickel';
    const pool = DEF_PLAYS.filter((d) => d.front === front);
    const w = pool.map((d) => {
      let x = 1;
      const blitz = d.blitz.length > 0;
      if (blitz) x *= t.blitzLean * 3;
      if (long && d.tags.includes('long')) x += 1;
      if (short && d.tags.includes('shortyard')) x += 1;
      if (this.tempo() === 'hurry' && this.margin(other(this.poss)) > 0 && d.tags.includes('prevent')) x += 2;
      return x;
    });
    return this.rng.weighted(pool, w);
  }

  // ------------------------------------------------------------------ penalties
  rollPresnap(): Penalty | null {
    const r = this.rng.next();
    const road = this.poss === 'away' ? 0.004 : 0;
    if (r < 0.014 + road) return { name: 'False Start', on: 'off', yards: 5, autoFirst: false, spot: 'los', replayDown: true };
    if (r < 0.018 + road) return { name: 'Delay of Game', on: 'off', yards: 5, autoFirst: false, spot: 'los', replayDown: true };
    if (r < 0.028 + road) return { name: this.rng.pick(['Neutral Zone Infraction', 'Offside', 'Encroachment']), on: 'def', yards: 5, autoFirst: false, spot: 'los', replayDown: true };
    return null;
  }

  private rollPostsnap(res: PlayResult, play: OffPlay): Penalty | null {
    const r = this.rng.next();
    const isPass = play.type !== 'run' && res.kind !== 'kneel' && res.kind !== 'spike';
    let acc = 0;
    const roll = (p: number) => r < (acc += p);
    if (res.kind === 'kneel' || res.kind === 'spike') return null;
    if (roll(isPass ? 0.026 : 0.036)) return { name: 'Offensive Holding', on: 'off', yards: 10, autoFirst: false, spot: 'los', replayDown: true };
    if (isPass && res.incomplete && res.airYards >= 5 && roll(res.deepContested ? 0.09 : 0.012))
      return { name: 'Defensive Pass Interference', on: 'def', yards: 0, autoFirst: true, spot: 'spot', spotX: res.airYards, replayDown: false };
    if (isPass && roll(0.014)) return { name: this.rng.chance(0.6) ? 'Defensive Holding' : 'Illegal Contact', on: 'def', yards: 5, autoFirst: true, spot: 'los', replayDown: false };
    if (isPass && res.pressured && roll(0.012)) return { name: 'Roughing the Passer', on: 'def', yards: 15, autoFirst: true, spot: 'end', replayDown: false };
    if (isPass && res.complete && res.airYards >= 8 && roll(0.005)) return { name: 'Offensive Pass Interference', on: 'off', yards: 10, autoFirst: false, spot: 'los', replayDown: true };
    if (roll(0.004)) return { name: this.rng.pick(['Unnecessary Roughness', 'Face Mask']), on: 'def', yards: 15, autoFirst: true, spot: 'end', replayDown: false };
    if (roll(0.002)) return { name: 'Illegal Block in the Back', on: 'off', yards: 10, autoFirst: false, spot: 'los', replayDown: true };
    return null;
  }

  /** Apply a penalty's yardage from LOS with half-the-distance rule. Returns new ballOn. */
  private enforce(pen: Penalty, los: number, endX: number): number {
    let from = pen.spot === 'end' ? endX : los;
    if (pen.spot === 'spot') return clamp(los + Math.max(1, pen.spotX ?? 1), 1, 99);
    if (pen.on === 'off') {
      const y = Math.min(pen.yards, from / 2);
      return from - y;
    }
    const y = Math.min(pen.yards, (100 - from) / 2);
    return from + y;
  }

  /** Pre-snap penalty: apply (no play). */
  applyPresnap(pen: Penalty) {
    const t = pen.on === 'off' ? this.offense : this.defense;
    const k = pen.on === 'off' ? this.poss : other(this.poss);
    const before = this.ballOn;
    this.ballOn = this.enforce(pen, this.ballOn, this.ballOn);
    const moved = this.ballOn - before;
    this.toGo -= moved;
    if (this.toGo <= 0) {
      this.down = 1;
      this.toGo = Math.min(10, 100 - this.ballOn);
    }
    this.team[k].penalties++;
    this.team[k].penYds += Math.round(Math.abs(moved));
    this.lastPenalty = pen;
    this.clockRunning = false;
    this.addLog(`PENALTY on ${t.abbr}: ${pen.name}, ${Math.abs(Math.round(moved))} yards. ${this.downText()} at ${this.ydLine()}.`, 'penalty');
  }

  // ------------------------------------------------------------------ scrimmage
  makePlay(offPlay: OffPlay, defCall: DefCall, userSide: Side | null = null): PlaySim {
    const flip = this.ballY > FIELD_W / 2 + 0.5 ? true : this.ballY < FIELD_W / 2 - 0.5 ? false : this.rng.chance(0.5);
    // strength to the field (wide) side: formation "right" is the offense's -y side
    const cfg: PlayConfig = {
      off: this.offense,
      def: this.defense,
      offPlay,
      defCall,
      los: this.ballOn,
      ballY: this.ballY,
      flip,
      weather: this.weather,
      worldDir: this.worldDir(),
      rng: this.rng.fork(),
      userSide,
      down: this.down,
      toGo: this.toGo,
    };
    return new PlaySim(cfg);
  }

  /** Apply finished play. Returns log line. */
  finishPlay(sim: PlaySim): PlayLog {
    const res = sim.result!;
    const play = sim.cfg.offPlay;
    const los = this.ballOn;
    const ok = this.poss;
    const dk = other(ok);
    const O = this.offense;
    const D = this.defense;
    const T = this.team[ok];
    const st = (p: Player, team: Team) => this.stats.line(p, team.abbr);
    const startDown = this.down;
    const startToGo = this.toGo;
    // fatigue / injuries
    for (const e of sim.ents) {
      const p = e.p;
      p.fatigue = clamp(p.fatigue + (0.028 + res.duration * 0.0035) * (1.25 - p.r.stamina / 120), 0, 1);
    }
    const onField = new Set(sim.ents.map((e) => e.p.id));
    for (const p of [...this.home.roster, ...this.away.roster]) if (!onField.has(p.id)) p.fatigue = Math.max(0, p.fatigue - 0.05);
    for (const e of sim.ents) {
      if (this.rng.chance(0.00028)) {
        e.p.injuredWeeks = this.rng.weighted([0.5, 1, 2, 4, 8], [4, 3, 2, 1, 0.5]);
        this.injuries.push(e.p);
        this.addLog(`Injury: ${e.p.pos} ${nm(e.p)} (${this.teamOf(e.side === 'off' ? ok : dk).abbr}) is down and will not return.`, 'info');
      }
    }
    // clock during the play
    this.runClock(res.duration, true);
    this.team[ok].top += res.duration + (this.clockRunning ? 25 : 0);

    // postsnap penalty
    let pen: Penalty | null = this.rollPostsnap(res, play);
    let usePen = false;
    if (pen) {
      // compare options from offense's perspective
      const resultEP = this.resultEP(res, los);
      const penBall = this.enforce(pen, los, res.endX);
      let penEP: number;
      if (pen.on === 'def') {
        const first = pen.autoFirst || penBall >= los + this.toGo;
        penEP = first ? expectedPoints(penBall, 1, 10) : expectedPoints(penBall, this.down, this.toGo - (penBall - los));
        usePen = penEP > resultEP && !res.td;
        if (res.td === 'off') usePen = false;
      } else {
        penEP = expectedPoints(penBall, this.down, this.toGo + (los - penBall));
        usePen = penEP < resultEP;
      }
      if (usePen) {
        const k = pen.on === 'off' ? ok : dk;
        this.team[k].penalties++;
        this.team[k].penYds += Math.round(Math.abs(penBall - (pen.spot === 'end' ? res.endX : los)));
        const moved = penBall - los;
        this.ballOn = penBall;
        if (pen.on === 'def' && (pen.autoFirst || penBall >= los + this.toGo)) {
          this.down = 1;
          this.toGo = Math.min(10, 100 - this.ballOn);
          T.firstDowns++;
        } else {
          this.toGo -= moved;
          if (!pen.replayDown) this.down++;
        }
        this.clockRunning = false;
        if (this.clock <= 0 && pen.on === 'def') this.untimed = true;
        const team = pen.on === 'off' ? O : D;
        const line = `${this.describe(res, play, sim)} PENALTY on ${team.abbr}: ${pen.name}, enforced. ${this.downText()} at ${this.ydLine()}.`;
        this.addLog(line, 'penalty');
        // half-distance etc handled; stats for the nullified play are not counted
        return this.log[this.log.length - 1];
      } else {
        const team = pen.on === 'off' ? O : D;
        pen = { ...pen, name: `${pen.name} on ${team.abbr} (declined)` };
      }
    }

    // ---- stats
    T.plays++;
    const yards = res.yards;
    if (res.kind === 'pass' || res.kind === 'sack') {
      const qb = res.passer;
      if (qb) {
        const l = st(qb, O);
        if (res.kind === 'sack') {
          l.sacked++;
          l.sackYds += -yards;
          T.sacks++;
          T.yards += yards;
          T.passYds += yards;
          this.team[dk].sacks += 0;
          if (res.sackedBy) st(res.sackedBy, D).sacks++;
        } else if (!res.thrownAway || true) {
          l.passAtt++;
          T.passAtt++;
          if (res.target && !res.thrownAway) st(res.target, O).targets++;
          if (res.complete && res.receiver) {
            l.passComp++;
            T.passComp++;
            const gain = res.interception ? 0 : Math.max(res.yards, -10);
            l.passYds += gain;
            T.passYds += gain;
            T.yards += gain;
            const rl = st(res.receiver, O);
            rl.rec++;
            rl.recYds += gain;
            rl.yac += res.yac;
            rl.recLong = Math.max(rl.recLong, gain);
            if (res.td === 'off') {
              rl.recTD++;
              l.passTD++;
            }
            if (gain >= 20) T.explosive++;
          }
          if (res.interception) {
            l.passInt++;
            T.ints++;
            if (res.intBy) st(res.intBy, D).ints++;
          }
        }
      }
      if (res.pbuBy) st(res.pbuBy, D).pbu++;
    } else if (res.kind === 'run' || res.kind === 'scramble') {
      if (res.rusher) {
        const l = st(res.rusher, O);
        l.rushAtt++;
        l.rushYds += yards;
        l.rushLong = Math.max(l.rushLong, yards);
        T.rushAtt++;
        T.rushYds += yards;
        T.yards += yards;
        if (res.td === 'off') l.rushTD++;
        if (yards >= 12) T.explosive++;
      }
    }
    if (res.brokenTackles && (res.rusher || res.receiver)) st((res.receiver ?? res.rusher)!, O).brokenTackles += res.brokenTackles;
    for (const tk of res.tacklers) {
      const team = D.roster.includes(tk) ? D : O;
      const l = st(tk, team);
      l.tackles++;
      if (yards < 0 && res.kind !== 'sack' && team === D) l.tfl++;
    }
    if (res.fumbleBy) {
      st(res.fumbleBy, O.roster.includes(res.fumbleBy) ? O : D).fumbles++;
      if (res.forcedBy) st(res.forcedBy, D.roster.includes(res.forcedBy) ? D : O).ff++;
      if (res.fumbleLost) {
        st(res.fumbleBy, O.roster.includes(res.fumbleBy) ? O : D).fumblesLost++;
        T.fumblesLost++;
        if (res.fumbleRecBy) st(res.fumbleRecBy, D).fr++;
      }
    }
    if (startDown === 3) T.thirdAtt++;
    if (startDown === 4) T.fourthAtt++;

    // ---- outcome
    const desc = this.describe(res, play, sim) + (pen ? ` ${pen.name}.` : '');
    const endY = clamp(res.endX === los ? this.ballY : this.endY(sim), HASH_R, HASH_L);
    let big: PlayLog['big'];
    this.lastOOB = res.outOfBounds;
    if (res.kind === 'spike' || res.incomplete || res.outOfBounds && this.oobStops()) this.clockRunning = false;
    else this.clockRunning = true;
    if (res.outOfBounds && !this.oobStops()) this.clockRunning = true;

    if (this.ballOn >= 80 && !this.inRedZone) {
      this.inRedZone = true;
      T.redZoneTrips++;
    }

    if (res.td) {
      const scorer: TeamKey = res.td === 'off' ? ok : dk;
      if (res.td === 'off' && this.inRedZone) T.redZoneTD++;
      if (res.td === 'def') {
        T.turnovers++;
        const l = res.intBy ? st(res.intBy, D) : res.fumbleRecBy ? st(res.fumbleRecBy, D) : null;
        if (l) l.defTD++;
        if (this.q === 5) this.otDriveEnded();
      }
      if (startDown === 3 && res.td === 'off') T.thirdConv++;
      if (startDown === 4 && res.td === 'off') T.fourthConv++;
      if (res.td === 'off') T.firstDowns++;
      this.addLog(`${desc} TOUCHDOWN ${this.teamOf(scorer).abbr}!`, 'td');
      this.scoreTD(scorer);
      return this.log[this.log.length - 1];
    }
    if (res.safety) {
      this.score[dk] += 2;
      this.addLog(`${desc} SAFETY! ${D.abbr} +2.`, 'safety');
      // free kick from own 20 by the team that gave up the safety
      this.kicking = ok;
      this.poss = dk;
      this.phase = 'kickoff';
      this.safetyKick = true;
      this.clockRunning = false;
      if (this.q === 5) this.finish();
      return this.log[this.log.length - 1];
    }
    if (res.turnover) {
      T.turnovers++;
      big = 'turnover';
      const newBall = res.touchback ? 20 : 100 - res.endX;
      this.addLog(`${desc}${res.touchback ? ' Touchback.' : ''}`, big);
      if (this.q === 5) this.otDriveEnded();
      if (this.phase === 'final') return this.log[this.log.length - 1];
      this.changePossessionRaw(newBall);
      return this.log[this.log.length - 1];
    }
    // normal advance
    this.ballOn = clamp(res.endX, 0.5, 99.5);
    this.ballY = endY;
    const gained = this.ballOn - los;
    if (gained >= startToGo) {
      this.down = 1;
      this.toGo = Math.min(10, 100 - this.ballOn);
      T.firstDowns++;
      if (startDown === 3) T.thirdConv++;
      if (startDown === 4) T.fourthConv++;
    } else {
      this.down++;
      this.toGo = startToGo - gained;
      if (this.down > 4) {
        this.addLog(`${desc} Turnover on downs.`, 'turnover');
        if (this.q === 5) this.otDriveEnded();
        if (this.phase === 'final') return this.log[this.log.length - 1];
        this.changePossessionRaw(100 - this.ballOn);
        return this.log[this.log.length - 1];
      }
    }
    this.addLog(desc, big);
    return this.log[this.log.length - 1];
  }

  safetyKick = false;

  private changePossessionRaw(newBallOn: number) {
    this.poss = other(this.poss);
    this.ballOn = clamp(newBallOn, 1, 99);
    this.down = 1;
    this.toGo = Math.min(10, 100 - this.ballOn);
    this.ballY = FIELD_W / 2;
    this.clockRunning = false;
    this.driveStartBallOn = this.ballOn;
    this.inRedZone = false;
    this.phase = 'scrimmage';
  }

  private endY(sim: PlaySim): number {
    const h = sim.ball.holder;
    return h ? h.pos.y : sim.ball.pos.y;
  }

  private resultEP(res: PlayResult, los: number): number {
    if (res.td === 'off') return 6.95;
    if (res.td === 'def') return -6.95;
    if (res.turnover) return -expectedPoints(100 - res.endX, 1, 10);
    if (res.safety) return -2 - 1;
    const gained = res.endX - los;
    if (gained >= this.toGo) return expectedPoints(res.endX, 1, 10);
    if (this.down === 4) return -expectedPoints(100 - res.endX, 1, 10);
    return expectedPoints(res.endX, this.down + 1, this.toGo - gained);
  }

  describe(res: PlayResult, play: OffPlay, sim: PlaySim): string {
    const y = res.yards;
    const yd = (n: number) => (n === 0 ? 'no gain' : n < 0 ? `a loss of ${-n}` : `${n} yard${n === 1 ? '' : 's'}`);
    const tk = res.tacklers.length ? ` (${res.tacklers.map(nm).join(', ')})` : '';
    const ts = res.targetSpotY ?? sim.by;
    const dir = (yy: number) => {
      const rel = (yy - sim.by) * (this.worldDir() > 0 ? 1 : 1);
      return rel > 5 ? 'left' : rel < -5 ? 'right' : 'middle';
    };
    const qb = nm(res.passer ?? sim.qb.p);
    switch (res.kind) {
      case 'kneel':
        return `${qb} kneels.`;
      case 'spike':
        return `${qb} spikes the ball to stop the clock.`;
      case 'sack':
        return `${qb} SACKED by ${nm(res.sackedBy)} for ${yd(y)}.${res.fumbleBy ? ` FUMBLE${res.fumbleLost ? `, recovered by ${this.defense.abbr} ${nm(res.fumbleRecBy)}` : ', recovered by offense'}.` : ''}`;
      case 'scramble':
        return `${qb} scrambles for ${yd(y)}${res.outOfBounds ? ', out of bounds' : tk}.`;
      case 'run': {
        const r = nm(res.rusher);
        const f = res.fumbleBy ? ` FUMBLE by ${nm(res.fumbleBy)}${res.fumbleLost ? `, recovered by ${this.defense.abbr} ${nm(res.fumbleRecBy)}` : ', offense recovers'}.` : '';
        return `${r} ${play.name.toLowerCase()} for ${yd(y)}${res.outOfBounds ? ', out of bounds' : tk}.${f}`;
      }
      case 'pass': {
        const depth = res.airYards >= 16 ? 'deep' : 'short';
        const d = dir(ts);
        if (res.thrownAway) return `${qb} throws it away under pressure.`;
        if (res.interception) return `${qb} pass ${depth} ${d} INTERCEPTED by ${nm(res.intBy)}${res.td === 'def' ? ', returned for a TOUCHDOWN' : `, returned to the ${this.ydLine(100 - res.endX, other(this.poss))}`}.`;
        if (res.incomplete) {
          const why = res.pbuBy ? `, broken up by ${nm(res.pbuBy)}` : sim.events.some((e) => e.type === 'drop') ? ', dropped' : '';
          return `${qb} pass ${depth} ${d} to ${nm(res.target)} incomplete${why}.`;
        }
        const f = res.fumbleBy ? ` FUMBLE${res.fumbleLost ? `, recovered by ${this.defense.abbr}` : ', offense recovers'}.` : '';
        return `${qb} pass ${depth} ${d} to ${nm(res.receiver)} for ${yd(y)}${res.outOfBounds ? ', out of bounds' : tk}.${f}`;
      }
    }
  }

  // ------------------------------------------------------------------ special teams
  kicker(k: TeamKey): Player {
    return this.teamOf(k).roster.filter((p) => p.pos === 'K').sort((a, b) => b.ovr - a.ovr)[0];
  }
  punter(k: TeamKey): Player {
    return this.teamOf(k).roster.filter((p) => p.pos === 'P').sort((a, b) => b.ovr - a.ovr)[0];
  }

  /** Probability a kick of `dist` yards is good. Fit to 2020s NFL by distance. */
  fgProb(dist: number, k: Player, kt: TeamKey): number {
    const eff = dist - (k.r.kickAcc - 80) * 0.35 - this.windHelp(kt) * 0.5 + Math.abs(this.weather.windY) * 0.4 + (this.weather.precip !== 'none' ? 3 : 0) + (this.weather.tempF < 32 ? 2 : 0);
    const maxR = 62 + (k.r.kickPower - 80) * 0.3 + this.windHelp(kt) * 0.8;
    // logistic shaped to: 30y ~ 97%, 40y ~ 90%, 50y ~ 76%, 55y ~ 66%
    let p = sigmoid(5.4 - 0.083 * eff + (eff < 40 ? 0 : -0.012 * (eff - 40) * (eff - 40) / 10));
    if (dist > maxR) p *= Math.max(0, 1 - (dist - maxR) / 5);
    return clamp(p, 0, 0.995);
  }

  kickoff(onside = false): SpecialResult {
    const kt = this.kicking;
    const rt = other(kt);
    const k = this.kicker(kt);
    const fromX = this.safetyKick ? 20 : 35;
    this.safetyKick = false;
    this.phase = 'scrimmage';
    const m = this.margin(kt);
    if (!onside && this.q >= 4 && m < 0 && m >= -16 && this.clock < 180) onside = true;
    this.clockRunning = false;
    if (onside) {
      const rec = this.rng.chance(0.12);
      const spot = fromX + 11;
      if (rec) {
        this.poss = kt;
        this.ballOn = spot;
        this.down = 1;
        this.toGo = 10;
        this.ballY = FIELD_W / 2;
        this.driveStartBallOn = this.ballOn;
        this.inRedZone = false;
        this.addLog(`${this.teamOf(kt).abbr} onside kick... RECOVERED by the kicking team at the ${this.ydLine()}!`, 'turnover');
      } else {
        this.poss = rt;
        this.ballOn = 100 - spot;
        this.down = 1;
        this.toGo = 10;
        this.ballY = FIELD_W / 2;
        this.driveStartBallOn = this.ballOn;
        this.inRedZone = false;
        this.addLog(`${this.teamOf(kt).abbr} onside kick recovered by ${this.teamOf(rt).abbr} at the ${this.ydLine()}.`, 'info');
      }
      this.runClock(this.rng.range(2, 4), true);
      return { desc: this.log[this.log.length - 1].desc, flight: { fromX, toX: spot, y: FIELD_W / 2, hang: 1.2, height: 2 } };
    }
    // modern dynamic kickoff: touchback at the 35, landing zone returns
    const dist = 60 + (k.r.kickPower - 75) * 0.35 + this.windHelp(kt) * 0.8 + this.rng.normal(0, 4);
    const land = fromX + dist; // in kicking team frame
    const hang = 3.9 + this.rng.normal(0, 0.3);
    let start: number;
    let desc: string;
    const kr = this.teamOf(rt).roster.filter((p) => p.pos === 'WR' || p.pos === 'RB' || p.pos === 'CB').sort((a, b) => b.r.speed + b.r.elusiveness - a.r.speed - a.r.elusiveness)[0];
    if (land >= 100 && this.rng.chance(0.75)) {
      start = 35;
      desc = `${nm(k)} kicks ${Math.round(dist)} yards into the end zone. Touchback.`;
    } else {
      const catchAt = clamp(100 - land, -5, 25);
      const ret = Math.max(0, this.rng.normal(23.5, 7) + (kr.r.speed - 85) * 0.2);
      start = clamp(Math.max(0, catchAt) + ret, 1, 99);
      // long returns / TD
      if (this.rng.chance(0.012 + (kr.r.elusiveness - 75) * 0.0004)) start += this.rng.range(20, 50);
      if (this.rng.chance(0.004)) start = 100;
      // muffed / fumbled return
      if (this.rng.chance(0.004)) {
        this.poss = kt;
        this.ballOn = 100 - clamp(start, 5, 95);
        this.down = 1;
        this.toGo = 10;
        this.ballY = FIELD_W / 2;
        this.addLog(`${nm(k)} kicks off. ${nm(kr)} FUMBLES the return! Recovered by ${this.teamOf(kt).abbr} at the ${this.ydLine()}.`, 'turnover');
        this.runClock(6, true);
        return { desc: this.log[this.log.length - 1].desc, flight: { fromX, toX: land, y: FIELD_W / 2, hang, height: 22 } };
      }
      if (start >= 100) {
        this.poss = rt;
        this.score[rt] += 6;
        this.addLog(`${nm(k)} kicks off. ${nm(kr)} returns it ALL THE WAY! TOUCHDOWN ${this.teamOf(rt).abbr}!`, 'td');
        this.stats.line(kr, this.teamOf(rt).abbr).defTD++;
        this.phase = 'pat';
        this.ballOn = 85;
        this.runClock(11, true);
        return { desc: this.log[this.log.length - 1].desc, flight: { fromX, toX: land, y: FIELD_W / 2, hang, height: 22 } };
      }
      desc = `${nm(k)} kicks ${Math.round(dist)} yards, returned by ${nm(kr)} to the ${this.teamOf(rt).abbr} ${Math.round(start) <= 50 ? Math.round(start) : 100 - Math.round(start)}.`;
      this.runClock(this.rng.range(5, 8), true);
    }
    this.poss = rt;
    this.ballOn = start;
    this.down = 1;
    this.toGo = 10;
    this.ballY = FIELD_W / 2;
    this.driveStartBallOn = this.ballOn;
    this.inRedZone = false;
    this.clockRunning = start !== 35; // returned kicks: clock keeps running
    this.addLog(desc);
    return { desc, flight: { fromX, toX: Math.min(land, 109), y: FIELD_W / 2, hang, height: 22 } };
  }

  punt(): SpecialResult {
    const kt = this.poss;
    const p = this.punter(kt);
    const T = this.team[kt];
    T.punts++;
    const los = this.ballOn;
    const l = this.stats.line(p, this.offense.abbr);
    this.runClock(this.rng.range(5, 9), true);
    if (this.rng.chance(0.006)) {
      this.addLog(`${nm(p)} punt is BLOCKED! Recovered by ${this.defense.abbr}.`, 'turnover');
      this.changePossessionRaw(100 - (los - 7));
      if (this.q === 5) this.otDriveEnded();
      return { desc: this.log[this.log.length - 1].desc };
    }
    let gross = 46.5 + (p.r.kickPower - 80) * 0.3 + this.windHelp(kt) * 1.0 + this.rng.normal(0, 6.5);
    // short-field punting: aim to pin
    const room = 100 - los;
    let land = los + gross;
    let desc: string;
    let start: number;
    if (room < 50) {
      // coffin corner style
      const target = 100 - this.rng.range(3, 15);
      land = Math.min(land, target + this.rng.normal(0, 4));
      gross = land - los;
    }
    if (land >= 100) {
      start = 20;
      desc = `${nm(p)} punts ${Math.round(100 - los)} yards into the end zone. Touchback.`;
      gross = 100 - los;
    } else {
      const r = this.rng.next();
      const catchSpot = 100 - land; // receiving team frame
      if (r < 0.38 || catchSpot < 12) {
        start = catchSpot;
        desc = `${nm(p)} punts ${Math.round(gross)} yards, ${catchSpot < 12 ? 'downed' : 'fair catch'} at the ${Math.round(catchSpot) <= 50 ? this.defense.abbr + ' ' + Math.round(catchSpot) : Math.round(100 - catchSpot)}.`;
      } else {
        let ret = Math.max(-2, this.rng.normal(9.5, 7));
        if (this.rng.chance(0.03)) ret += this.rng.range(15, 40);
        if (this.rng.chance(0.003)) ret = 200;
        start = catchSpot + ret;
        if (this.rng.chance(0.006)) {
          // muff
          this.addLog(`${nm(p)} punts ${Math.round(gross)} yards. MUFFED! ${this.offense.abbr} recovers!`, 'turnover');
          l.punts++;
          l.puntYds += Math.round(gross);
          this.ballOn = clamp(land, 1, 99);
          this.down = 1;
          this.toGo = Math.min(10, 100 - this.ballOn);
          this.ballY = FIELD_W / 2;
          return { desc: this.log[this.log.length - 1].desc, flight: { fromX: los - 14, toX: land, y: FIELD_W / 2, hang: 4.4, height: 26 } };
        }
        if (start >= 100) {
          l.punts++;
          l.puntYds += Math.round(gross);
          this.score[other(kt)] += 6;
          this.poss = other(kt);
          this.phase = 'pat';
          this.ballOn = 85;
          this.addLog(`${nm(p)} punts ${Math.round(gross)} yards... RETURNED FOR A TOUCHDOWN by ${this.defense.abbr}!`, 'td');
          if (this.q === 5) this.finish();
          return { desc: this.log[this.log.length - 1].desc, flight: { fromX: los - 14, toX: land, y: FIELD_W / 2, hang: 4.4, height: 26 } };
        }
        desc = `${nm(p)} punts ${Math.round(gross)} yards, returned ${Math.round(ret)} yards.`;
      }
    }
    l.punts++;
    l.puntYds += Math.round(gross);
    this.addLog(desc);
    if (this.q === 5) this.otDriveEnded();
    if (this.phase !== 'final') {
      this.changePossessionRaw(clamp(start, 1, 99));
      this.clockRunning = desc.includes('returned');
    }
    return { desc, flight: { fromX: los - 14, toX: Math.min(land, 105), y: FIELD_W / 2, hang: 4.3 + this.rng.normal(0, 0.3), height: 26 } };
  }

  fieldGoal(): SpecialResult {
    const kt = this.poss;
    const k = this.kicker(kt);
    const dist = Math.round(100 - this.ballOn + 17);
    const p = this.fgProb(dist, k, kt);
    const l = this.stats.line(k, this.offense.abbr);
    l.fga++;
    this.team[kt].fga++;
    this.runClock(this.rng.range(4, 6), true);
    const blocked = this.rng.chance(0.012);
    const good = !blocked && this.rng.chance(p);
    const flight = { fromX: this.ballOn - 7, toX: 110, y: FIELD_W / 2 + (good ? 0 : this.rng.chance(0.5) ? 5 : -5), hang: 2.6, height: 14, good };
    if (good) {
      l.fgm++;
      this.team[kt].fgm++;
      this.score[kt] += 3;
      this.addLog(`${nm(k)} ${dist}-yard field goal is GOOD.`, 'fg');
      this.kicking = kt;
      this.phase = 'kickoff';
      this.clockRunning = false;
      if (this.q === 5) this.otDriveEnded();
      return { desc: this.log[this.log.length - 1].desc, flight };
    }
    this.addLog(`${nm(k)} ${dist}-yard field goal is ${blocked ? 'BLOCKED' : this.rng.chance(0.5) ? 'NO GOOD, wide right' : 'NO GOOD, wide left'}.`, 'turnover');
    if (this.q === 5) this.otDriveEnded();
    if (this.phase !== 'final') this.changePossessionRaw(100 - Math.max(this.ballOn - 7, 20));
    return { desc: this.log[this.log.length - 1].desc, flight };
  }

  /** Decide 2-point try per a standard chart (CPU). */
  goForTwo(): boolean {
    const m = this.margin(); // after the 6
    const late = this.q >= 4 || (this.q === 3 && this.clock < 300);
    if (this.q === 5) return false;
    if (late && [-2, -5, -10, -13, -16, 1, 5, 12, -9].includes(m)) return true;
    return this.rng.chance(0.04 + this.offense.aggression * 0.04);
  }

  /** PAT kick (33 yards). */
  extraPoint(): SpecialResult {
    const kt = this.poss;
    const k = this.kicker(kt);
    const l = this.stats.line(k, this.offense.abbr);
    l.xpa++;
    const p = clamp(this.fgProb(33, k, kt) - 0.03, 0.8, 0.99);
    const good = this.rng.chance(p);
    if (good) {
      l.xpm++;
      this.score[kt] += 1;
    }
    this.addLog(`${nm(k)} extra point is ${good ? 'GOOD' : 'NO GOOD'}.`, good ? undefined : 'info');
    this.afterPAT(kt);
    return { desc: this.log[this.log.length - 1].desc, flight: { fromX: 78, toX: 110, y: FIELD_W / 2 + (good ? 0 : 4), hang: 2.2, height: 12, good } };
  }

  /** Two-point try: a real scrimmage play from the 2. */
  twoPointSim(offPlay: OffPlay, defCall: DefCall, userSide: Side | null = null): PlaySim {
    this.ballOn = 98;
    this.ballY = FIELD_W / 2;
    this.down = 1;
    this.toGo = 2;
    return this.makePlay(offPlay, defCall, userSide);
  }

  finishTwoPoint(sim: PlaySim) {
    const res = sim.result!;
    const kt = this.poss;
    const good = res.td === 'off';
    if (good) this.score[kt] += 2;
    if (res.td === 'def') this.score[other(kt)] += 2;
    this.addLog(`Two-point attempt: ${this.describe(res, sim.cfg.offPlay, sim)} ${good ? 'GOOD!' : 'FAILED.'}`, good ? 'td' : 'info');
    this.afterPAT(kt);
  }

  private afterPAT(kt: TeamKey) {
    this.kicking = kt;
    this.phase = 'kickoff';
    this.clockRunning = false;
    if (this.q === 5) {
      this.otDriveEnded();
    }
    if (this.clock <= 0 && (this.phase as Phase) !== 'final') this.endQuarter();
  }

  cpuTwoPointPlay(): OffPlay {
    const pool = OFF_PLAYS.filter((p) => ['trips_stick', 'dbl_slants', 'dbl_mesh', 'gl_dive', 'gl_pa', 'pistol_power', 'empty_quick'].includes(p.id));
    return this.rng.pick(pool);
  }

  // ------------------------------------------------------------------ headless driver
  /** Simulate one "action" (kickoff, PAT, or a scrimmage down) with CPU on both sides. */
  simStep(): PlayLog | null {
    if (this.phase === 'final') return null;
    if (this.phase === 'kickoff') {
      if (this.clock <= 0) {
        this.endQuarter();
        return null;
      }
      this.kickoff();
      return this.log[this.log.length - 1];
    }
    if (this.phase === 'pat') {
      if (this.goForTwo()) {
        const op = this.cpuTwoPointPlay();
        const sim = this.twoPointSim(op, this.cpuDefense(op));
        sim.runToEnd();
        this.finishTwoPoint(sim);
      } else this.extraPoint();
      return this.log[this.log.length - 1];
    }
    if (!this.preSnap()) return null;
    if (this.down === 4) {
      const call = this.fourthDownCall();
      if (call === 'punt') {
        this.punt();
        return this.log[this.log.length - 1];
      }
      if (call === 'fg') {
        this.fieldGoal();
        return this.log[this.log.length - 1];
      }
    } else if (this.q === 2 || this.q >= 4) {
      // end-of-half field goal on any down
      const fgDist = 100 - this.ballOn + 17;
      if (this.clock <= 6 && fgDist <= 56 && this.margin() >= -3 && (this.q === 2 || this.margin() >= -3)) {
        this.fieldGoal();
        return this.log[this.log.length - 1];
      }
    }
    const pre = this.rollPresnap();
    if (pre) {
      this.applyPresnap(pre);
      return this.log[this.log.length - 1];
    }
    const op = this.cpuOffense();
    const dc = this.cpuDefense(op);
    const sim = this.makePlay(op, dc);
    sim.runToEnd();
    return this.finishPlay(sim);
  }

  simToEnd(maxSteps = 2000) {
    let n = 0;
    while (this.phase !== 'final' && n++ < maxSteps) this.simStep();
  }
}

export function randomWeather(rng: Rng): Weather {
  const r = rng.next();
  const precip: Weather['precip'] = r < 0.1 ? 'rain' : r < 0.14 ? 'snow' : 'none';
  const tempF = precip === 'snow' ? rng.range(15, 32) : rng.range(35, 88);
  const wind = Math.max(0, rng.normal(3.2, 2)); // yd/s (~6.5 mph mean)
  const ang = rng.range(0, Math.PI * 2);
  return { windX: Math.cos(ang) * wind, windY: Math.sin(ang) * wind, precip, tempF };
}
