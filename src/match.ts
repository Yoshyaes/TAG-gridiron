// Match controller: drives the game flow between play calling, live simulation, results and special teams.
import * as THREE from 'three';
import { Game, TeamKey, other, SpecialResult } from './sim/game';
import type { PlaySim, Ent } from './sim/play';
import type { OffPlay, DefCall } from './sim/playbook';
import { FIELD_W } from './sim/playbook';
import type { Player, Team } from './sim/types';
import { World, toWorld } from './render/world';
import { Hud } from './ui/hud';
import { showPlayCall, CallChoice } from './ui/playcall';
import { showBoxScore } from './ui/boxscore';
import { Input } from './input';

type State = 'idle' | 'call' | 'presnap' | 'live' | 'post' | 'kick' | 'wait' | 'final' | 'paused';

interface KickAnim {
  res: SpecialResult;
  dir: number;
  t: number;
  dur: number;
  kind: 'kickoff' | 'punt' | 'fg';
  kickTeam: Player[];
  recvTeam: Player[];
  fromX: number;
  toX: number;
}

const STEP = 1 / 60;

export class Match {
  state: State = 'idle';
  sim: PlaySim | null = null;
  hud: Hud;
  private acc = 0;
  private timer = 0;
  private kick: KickAnim | null = null;
  private twoPoint = false;
  private lastQ = 1;
  speed = 1;
  private labels = new Map<Ent, string>();
  private resumeState: State = 'idle';
  private overlay: HTMLElement | null = null;
  private dir = 1;
  private presnapFlag = false;
  private kickT = 0;
  private idleFocus = new THREE.Vector3(50, 0, 0);

  constructor(
    private ui: HTMLElement,
    private world: World,
    private input: Input,
    public game: Game,
    public mode: 'play' | 'watch',
    public userTeam: TeamKey | null,
    private onExit: () => void,
  ) {
    this.hud = new Hud(ui, game);
    this.hud.update();
    this.world.camMode = 'broadcast';
    if (mode === 'watch') this.makeSpeedButtons();
    this.next();
  }

  private makeSpeedButtons() {
    const d = document.createElement('div');
    d.className = 'speed';
    for (const s of [1, 2, 4]) {
      const b = document.createElement('button');
      b.className = 'btn slim';
      b.textContent = `${s}x`;
      b.onclick = () => (this.speed = s);
      d.appendChild(b);
    }
    const sim = document.createElement('button');
    sim.className = 'btn slim';
    sim.textContent = 'Sim to end';
    sim.onclick = () => {
      this.game.simToEnd();
      this.state = 'idle';
      this.next();
    };
    d.appendChild(sim);
    this.hud.root.appendChild(d);
    d.style.pointerEvents = 'auto';
  }

  get g() {
    return this.game;
  }
  private userOff() {
    return this.userTeam !== null && this.userTeam === this.g.poss;
  }
  private userDef() {
    return this.userTeam !== null && this.userTeam === other(this.g.poss);
  }

  // ------------------------------------------------------------------ flow
  next(): void {
    const g = this.g;
    this.hud.update();
    this.world.drawRoutes(null, 1);
    this.labels.clear();
    if (g.phase === 'final') return this.showFinal();
    if (g.q === 3 && this.lastQ === 2) {
      this.lastQ = 3;
      this.state = 'paused';
      showBoxScore(this.ui, g, 'Halftime', [{ label: 'Start 2nd Half', fn: () => { this.state = 'idle'; this.next(); } }]);
      return;
    }
    this.lastQ = g.q;
    if (g.phase === 'kickoff') {
      if (g.clock <= 0) {
        g.simStep();
        return this.next();
      }
      const kt = g.kicking;
      const dir = g.worldDir(kt);
      const kick = this.pick11(this.g.teamOf(kt), 'K');
      const recv = this.pick11(this.g.teamOf(other(kt)), 'KR');
      const res = g.kickoff();
      return this.startKick(res, dir, 'kickoff', kick, recv);
    }
    if (g.phase === 'pat') {
      const decide = (two: boolean) => {
        if (!two) {
          const dir = g.worldDir();
          const kick = this.pick11(g.offense, 'K');
          const recv = this.pick11(g.defense, 'D');
          const res = g.extraPoint();
          return this.startKick(res, dir, 'fg', kick, recv);
        }
        this.twoPoint = true;
        this.callPlays(true);
      };
      if (this.userOff()) return this.patChoice(decide);
      return decide(g.goForTwo());
    }
    // ---- scrimmage
    const qBefore = g.q;
    if (!g.preSnap()) {
      if (g.q !== qBefore && (g.phase as string) !== 'final') this.hud.banner(g.q === 5 ? 'Overtime' : `End of Q${qBefore}`);
      return this.wait(1.2);
    }
    this.hud.update();
    // CPU 4th down / end-of-half decisions
    if (!this.userOff()) {
      if (g.down === 4) {
        const call = g.fourthDownCall();
        if (call === 'punt') return this.doPunt();
        if (call === 'fg') return this.doFG();
      } else if ((g.q === 2 || g.q >= 4) && g.clock <= 6 && 100 - g.ballOn + 17 <= 56 && g.margin() >= -3) {
        return this.doFG();
      }
    }
    this.twoPoint = false;
    this.callPlays(false);
  }

  private patChoice(decide: (two: boolean) => void) {
    this.state = 'call';
    const d = document.createElement('div');
    d.className = 'modal';
    d.style.background = 'rgba(3,5,9,0.5)';
    d.innerHTML = `<div class="panel pausebox"><div class="label">Try after touchdown</div><div class="menu"><button class="btn" id="k1">Kick Extra Point<small>33-yard attempt, ~94% league-wide</small></button><button class="btn" id="k2">Go for Two<small>Run a play from the 2, ~48% league-wide</small></button></div></div>`;
    this.ui.appendChild(d);
    (d.querySelector('#k1') as HTMLButtonElement).onclick = () => { d.remove(); decide(false); };
    (d.querySelector('#k2') as HTMLButtonElement).onclick = () => { d.remove(); decide(true); };
  }

  private callPlays(twoPoint: boolean) {
    const g = this.g;
    this.state = 'call';
    this.sim = null;
    this.world.showPlayers([]);
    this.dir = g.worldDir();
    this.idleFocus = toWorld(twoPoint ? 98 : g.ballOn, g.ballY, this.dir);
    this.world.setLines(twoPoint ? 98 : g.ballOn, twoPoint ? null : g.ballOn + g.toGo, g.worldDir());
    const finish = (op: OffPlay, dc: DefCall) => this.startPresnap(op, dc);
    if (this.userOff()) {
      showPlayCall(this.ui, g, 'off', { fourthDown: g.down === 4 && !twoPoint, twoPoint }, (c: CallChoice) => {
        if (c.kind === 'punt') return this.doPunt();
        if (c.kind === 'fg') return this.doFG();
        const op = c.kind === 'off' ? c.play : twoPoint ? g.cpuTwoPointPlay() : g.cpuOffense();
        finish(op, g.cpuDefense(op));
      });
    } else {
      const op = twoPoint ? g.cpuTwoPointPlay() : g.cpuOffense();
      if (this.userDef()) {
        showPlayCall(this.ui, g, 'def', {}, (c: CallChoice) => finish(op, c.kind === 'def' ? c.call : g.cpuDefense(op)));
      } else finish(op, g.cpuDefense(op));
    }
  }

  private startPresnap(op: OffPlay, dc: DefCall) {
    const g = this.g;
    this.presnapFlag = false;
    if (!this.twoPoint && op.type !== 'kneel' && op.type !== 'spike') {
      const pen = g.rollPresnap();
      if (pen) {
        g.applyPresnap(pen);
        this.hud.update(true);
        this.hud.banner('FLAG', `${pen.name} · ${pen.on === 'off' ? g.offense.abbr : g.defense.abbr}`, '#ffb000');
        return this.wait(2.0);
      }
    }
    const userSide = this.userOff() ? 'off' : this.userDef() ? 'def' : null;
    this.sim = this.twoPoint ? g.twoPointSim(op, dc, userSide) : g.makePlay(op, dc, userSide);
    this.dir = g.worldDir();
    this.world.showPlayers(this.sim.ents.map((e) => e.p));
    this.world.capturePrev(this.sim);
    this.world.setLines(this.sim.los, this.twoPoint ? null : g.ballOn + g.toGo, this.dir);
    if (userSide === 'off') this.world.drawRoutes(this.sim, this.dir);
    this.labels.clear();
    if (userSide === 'off') this.assignLabels();
    this.state = 'presnap';
    this.timer = this.mode === 'watch' ? 1.1 / this.speed : userSide === 'off' ? 999 : 1.6;
    this.hud.hint(userSide === 'off' ? `<b>SPACE</b> snap &middot; <b>1-5</b> throw (hold <b>SHIFT</b> = touch) &middot; <b>WASD</b> move &middot; <b>C</b> camera` : userSide === 'def' ? `Defending &middot; <b>TAB</b> switch player &middot; <b>WASD</b> move &middot; <b>SPACE</b> dive tackle` : null);
    this.hud.update();
  }

  private assignLabels() {
    const s = this.sim!;
    const order = (s.cfg.offPlay.reads ?? []).map((r) => s.offRoles.get(r)).filter((e): e is Ent => !!e && e.state === 'route');
    for (const e of s.off) if (e.state === 'route' && !order.includes(e)) order.push(e);
    order.slice(0, 5).forEach((e, i) => this.labels.set(e, String(i + 1)));
  }

  private wait(sec: number) {
    this.state = 'wait';
    this.timer = sec;
  }

  // ------------------------------------------------------------------ special teams
  private pick11(t: Team, role: 'K' | 'P' | 'KR' | 'D'): Player[] {
    const healthy = t.roster.filter((p) => p.injuredWeeks <= 0);
    const by = (pos: string[]) => healthy.filter((p) => pos.includes(p.pos)).sort((a, b) => b.r.speed - a.r.speed);
    let lead: Player[] = [];
    if (role === 'K') lead = by(['K']).slice(0, 1);
    if (role === 'P') lead = by(['P']).slice(0, 1);
    if (role === 'KR') lead = by(['WR', 'CB', 'RB']).slice(0, 1);
    const rest = role === 'D' ? by(['DE', 'DT', 'LB']) : by(['LB', 'S', 'CB', 'TE', 'WR', 'RB', 'FB', 'DE']);
    const out = [...lead];
    for (const p of rest) if (out.length < 11 && !out.includes(p)) out.push(p);
    if (role === 'K' || role === 'P') {
      // field goal / punt units need linemen up front
      const ol = by(['OT', 'OG', 'C', 'TE']);
      if (role === 'K' && this.g.phase !== 'kickoff') return [...lead, ...ol.slice(0, 7), ...by(['QB']).slice(1, 2), ...by(['TE', 'LB']).slice(0, 2)].slice(0, 11);
      if (role === 'P') return [...lead, ...ol.slice(0, 6), ...by(['LB', 'S', 'CB']).slice(0, 4)].slice(0, 11);
    }
    return out.slice(0, 11);
  }

  private doPunt() {
    const g = this.g;
    const dir = g.worldDir();
    const kick = this.pick11(g.offense, 'P');
    const recv = this.pick11(g.defense, 'KR');
    const los = g.ballOn;
    const res = g.punt();
    if (res.flight) res.flight.fromX = los - 14;
    this.startKick(res, dir, 'punt', kick, recv, los);
  }

  private doFG() {
    const g = this.g;
    const dir = g.worldDir();
    const kick = this.pick11(g.offense, 'K');
    const recv = this.pick11(g.defense, 'D');
    const los = g.ballOn;
    const res = g.fieldGoal();
    this.startKick(res, dir, 'fg', kick, recv, los);
  }

  private startKick(res: SpecialResult, dir: number, kind: KickAnim['kind'], kickTeam: Player[], recvTeam: Player[], los?: number) {
    const f = res.flight ?? { fromX: los ?? 35, toX: (los ?? 35) + 40, y: FIELD_W / 2, hang: 2, height: 10 };
    this.world.showPlayers([...kickTeam, ...recvTeam]);
    this.world.hideLines();
    this.kick = { res, dir, t: 0, dur: f.hang + 1.4, kind, kickTeam, recvTeam, fromX: f.fromX, toX: Math.min(f.toX, 112) };
    this.kickT = 0;
    this.state = 'kick';
    this.hud.hint(null);
  }

  private updateKick(dt: number) {
    const k = this.kick!;
    const f = k.res.flight ?? { fromX: k.fromX, toX: k.toX, y: FIELD_W / 2, hang: 2, height: 10 };
    k.t += dt;
    const lead = k.kind === 'kickoff' ? 0.35 : k.kind === 'punt' ? 1.0 : 0.9;
    const ft = Math.max(0, Math.min(1, (k.t - lead) / f.hang));
    const ballX = k.t < lead ? (k.kind === 'kickoff' ? f.fromX : f.fromX + 7 * (k.t / lead) * (k.kind === 'punt' ? 0 : 1)) : f.fromX + (k.toX - f.fromX) * ft;
    this.world.setBallFlight(k.t < lead ? ballX : f.fromX, k.toX, f.y, f.height, k.t < lead ? 0 : ft, k.dir);
    if (k.kind === 'fg' && k.t < lead) this.world.setBallFlight(f.fromX, f.fromX, f.y, 0, 0, k.dir);
    // choreography
    const W = FIELD_W;
    if (k.kind === 'kickoff') {
      k.kickTeam.forEach((p, i) => {
        const y = i === 0 ? W / 2 : 4 + ((i - 1) / 9) * (W - 8);
        const x = f.fromX - (i === 0 ? 7 : 1) + Math.max(0, k.t - 0.2) * (i === 0 ? 4 : 8.5);
        this.world.pose(p, Math.min(x, ballX + 2), y, k.dir, 1, 0, k.t > 0.2 ? 'run' : 'idle', k.t > 0.2 ? 8.5 : 0, dt, k.t);
      });
      k.recvTeam.forEach((p, i) => {
        const land = Math.min(k.toX, 100);
        const y = i === 0 ? f.y : 6 + ((i - 1) / 9) * (W - 12);
        const x = i === 0 ? land : f.fromX + 12 + (i % 3) * 6 + Math.max(0, k.t - 1) * 2;
        this.world.pose(p, x, y, k.dir, -1, 0, i === 0 ? 'idle' : 'run', i === 0 ? 0 : 3, dt, k.t);
      });
    } else if (k.kind === 'punt') {
      const los = f.fromX + 14;
      k.kickTeam.forEach((p, i) => {
        const y = i === 0 ? W / 2 : i <= 6 ? W / 2 + (i - 3.5) * 1.4 : i === 7 ? 4 : i === 8 ? W - 4 : W / 2 + (i - 9.5) * 6;
        const baseX = i === 0 ? f.fromX : los - 0.6;
        const x = baseX + Math.max(0, k.t - 1.1) * (i === 0 ? 1 : 8);
        this.world.pose(p, x, y, k.dir, 1, 0, k.t > 1.1 ? 'run' : 'idle', k.t > 1.1 ? 8 : 0, dt, k.t);
      });
      k.recvTeam.forEach((p, i) => {
        const y = i === 0 ? f.y : W / 2 + (i - 5.5) * 2.2;
        const x = i === 0 ? Math.min(k.toX, 100) : los + 1 - Math.max(0, k.t - 1) * 3;
        this.world.pose(p, x, y, k.dir, -1, 0, i === 0 ? 'idle' : 'block', 0, dt, k.t);
      });
    } else {
      const los = f.fromX + 7;
      k.kickTeam.forEach((p, i) => {
        let x = los - 0.6;
        let y = W / 2 + (i - 5) * 1.3;
        let anim = 'block';
        if (i === 0) { x = f.fromX - 1.5 + Math.min(1, k.t / 0.9) * 1.5; y = W / 2 + 0.5; anim = k.t > 0.9 ? 'idle' : 'run'; }
        if (i === 8) { x = f.fromX; y = W / 2 - 0.4; anim = 'kneel'; }
        this.world.pose(p, x, y, k.dir, 1, 0, anim, i === 0 && k.t < 0.9 ? 4 : 0, dt, k.t);
      });
      k.recvTeam.forEach((p, i) => {
        const y = W / 2 + (i - 5) * 1.35;
        this.world.pose(p, los + 0.8, y, k.dir, -1, 0, 'block', 0, dt, k.t);
      });
    }
    const focus = this.world.ballWorld();
    this.world.updateCamera(dt, new THREE.Vector3(focus.x, 0, focus.z), k.dir, null);
    if (k.t > lead + f.hang && !this.kickT) {
      this.kickT = 1;
      const last = this.g.log[this.g.log.length - 1];
      this.hud.update();
      if (k.kind === 'fg') this.hud.banner(f.good ? 'IT\'S GOOD' : 'NO GOOD', last?.desc ?? '', f.good ? '#3ddc84' : '#ff5a5a');
      else if (last?.big === 'td') this.hud.banner('TOUCHDOWN', last.desc, '#3ddc84');
      else if (last?.big === 'turnover') this.hud.banner('TURNOVER', last.desc, '#ff5a5a');
    }
    if (k.t >= k.dur / Math.max(1, this.speed > 1 ? 1 : 1)) {
      this.kick = null;
      this.state = 'idle';
      this.next();
    }
  }

  // ------------------------------------------------------------------ live play
  private readUserInput() {
    const s = this.sim!;
    const inp = this.input;
    const mv = inp.move();
    // camera-relative -> world -> offense frame
    const cam = this.world.camera;
    const fwd = new THREE.Vector3();
    cam.getWorldDirection(fwd);
    fwd.y = 0;
    fwd.normalize();
    const right = new THREE.Vector3(-fwd.z, 0, fwd.x);
    const wv = right.multiplyScalar(mv.x).add(fwd.multiplyScalar(mv.y));
    const d = this.dir;
    s.input.move = d > 0 ? { x: wv.x, y: -wv.z } : { x: -wv.x, y: wv.z };
    s.input.sprint = inp.sprint() || true;
    for (let i = 1; i <= 5; i++) {
      if (inp.take(`Digit${i}`)) {
        for (const [e, l] of this.labels) if (l === String(i)) s.input.throwTo = e.role as any;
        s.input.lob = inp.down.has('ShiftLeft') || inp.down.has('ShiftRight');
      }
    }
    if (inp.take('Space')) {
      s.input.juke = true;
      s.input.dive = true;
    } else s.input.dive = false;
    if (inp.take('Tab')) s.input.switchReq = true;
    // auto-switch to our ball carrier
    const h = s.ball.holder;
    if (h && s.user && h.side === s.user.side && h !== s.user && h !== s.qb) {
      s.user.userControlled = false;
      s.user = h;
      h.userControlled = true;
      this.labels.clear();
    }
    if (s.passThrown) this.labels.clear();
  }

  update(dt: number) {
    const inp = this.input;
    inp.pollPad();
    if (inp.take('Escape') && this.state !== 'final') {
      if (this.state === 'paused') return;
      this.pause();
    }
    if (inp.take('KeyC')) {
      const modes = ['broadcast', 'sideline', 'skycam', 'player'] as const;
      this.world.camMode = modes[(modes.indexOf(this.world.camMode) + 1) % modes.length];
      this.toast(`Camera: ${this.world.camMode}`);
    }
    if (inp.take('KeyB') && this.state !== 'paused' && this.state !== 'final') {
      this.resumeState = this.state;
      this.state = 'paused';
      showBoxScore(this.ui, this.g, 'Box Score', [{ label: 'Resume', fn: () => (this.state = this.resumeState) }]);
    }
    const sdt = dt * (this.mode === 'watch' ? this.speed : 1);
    switch (this.state) {
      case 'wait':
        this.timer -= sdt;
        if (this.timer <= 0 || (this.mode === 'play' && inp.take('Space'))) {
          this.state = 'idle';
          this.next();
        }
        break;
      case 'presnap': {
        const s = this.sim!;
        this.timer -= sdt;
        if (this.userDef() && inp.take('Tab')) s.switchUser();
        if (this.timer <= 0 || (this.userOff() && inp.take('Space'))) {
          this.state = 'live';
          this.acc = 0;
          this.world.drawRoutes(null, 1);
          this.hud.hint(null);
          this.hud.keys(this.userTeam ? `<b>WASD</b> move<br><b>1-5</b> pass target<br><b>SHIFT+#</b> touch pass<br><b>SPACE</b> juke / dive<br><b>TAB</b> switch (D)<br><b>C</b> camera &middot; <b>B</b> box` : null);
        }
        this.world.syncPlay(s, this.dir, 1, dt, this.labels);
        this.focusCamera(dt);
        break;
      }
      case 'live': {
        const s = this.sim!;
        if (this.userTeam) this.readUserInput();
        this.acc += sdt;
        let n = 0;
        while (this.acc >= STEP && s.phase === 'live' && n++ < 12) {
          this.world.capturePrev(s);
          s.step(STEP);
          this.acc -= STEP;
          s.input.juke = false;
        }
        this.world.syncPlay(s, this.dir, Math.min(1, this.acc / STEP), dt, this.labels);
        this.focusCamera(dt);
        this.hud.update(false, this.g.clockRunning || true ? s.t : 0);
        if (s.phase === 'dead') {
          this.state = 'post';
          this.timer = this.mode === 'watch' ? 1.4 / this.speed : 1.8;
          this.onPlayDead();
        }
        break;
      }
      case 'post': {
        const s = this.sim!;
        this.timer -= sdt;
        // keep animating bodies on the ground / celebrations
        for (const e of s.ents) e.animT += dt;
        this.world.syncPlay(s, this.dir, 1, dt, this.labels);
        this.focusCamera(dt);
        if (this.timer <= 0 || (this.mode === 'play' && this.timer < 1.2 && inp.take('Space'))) this.applyPlay();
        break;
      }
      case 'kick':
        this.updateKick(sdt);
        break;
      default:
        if (this.sim) {
          this.world.syncPlay(this.sim, this.dir, 1, dt, this.labels);
          this.focusCamera(dt);
        } else this.world.updateCamera(dt, this.idleFocus, this.dir, null);
    }
    inp.endFrame();
  }

  private focusCamera(dt: number) {
    const s = this.sim!;
    const b = s.ball;
    const fx = b.holder ? b.holder.pos.x : b.pos.x;
    const fy = b.holder ? b.holder.pos.y : b.pos.y;
    const focus = toWorld(fx, fy, this.dir);
    const up = s.user ? toWorld(s.user.pos.x, s.user.pos.y, this.dir) : null;
    this.world.updateCamera(dt, focus, this.dir, up);
  }

  /** Immediate reactions when the whistle blows (before stats apply). */
  private onPlayDead() {
    const s = this.sim!;
    const r = s.result!;
    if (r.td) {
      this.world.crowdHype = 1;
      this.world.shake = 0.3;
      const holder = s.ball.holder;
      if (holder) { holder.anim = 'celebrate'; holder.animT = 0; }
      this.hud.banner('TOUCHDOWN', '', '#3ddc84');
    } else if (r.interception) this.hud.banner('INTERCEPTED', '', '#ff5a5a');
    else if (r.fumbleLost) this.hud.banner('FUMBLE', 'Defense recovers', '#ff5a5a');
    else if (r.kind === 'sack') { this.hud.banner('SACK', '', '#ffb000'); this.world.shake = 0.15; }
    else if (r.safety) this.hud.banner('SAFETY', '', '#ff5a5a');
  }

  private applyPlay() {
    const s = this.sim!;
    const g = this.g;
    const downBefore = g.down;
    const possBefore = g.poss;
    if (this.twoPoint) {
      g.finishTwoPoint(s);
      this.twoPoint = false;
    } else {
      const log = g.finishPlay(s);
      if (log.big === 'penalty') this.hud.banner('FLAG', log.desc.split('PENALTY')[1]?.split(',')[0] ?? '', '#ffb000');
      else if (g.poss === possBefore && g.phase === 'scrimmage' && g.down === 1 && !s.result!.td && s.result!.yards > 0 && downBefore >= 1) {
        if (s.result!.yards >= 20) this.world.crowdHype = 0.6;
        this.toast('First down');
      } else if (log.desc.includes('Turnover on downs')) this.hud.banner('TURNOVER ON DOWNS', '', '#ff5a5a');
    }
    this.hud.update();
    this.state = 'idle';
    this.next();
  }

  private toast(msg: string) {
    const t = document.createElement('div');
    t.className = 'toast';
    t.textContent = msg;
    this.hud.root.appendChild(t);
    setTimeout(() => t.remove(), 1300);
  }

  private pause() {
    this.resumeState = this.state;
    this.state = 'paused';
    const d = document.createElement('div');
    d.className = 'modal';
    d.innerHTML = `<div class="panel pausebox"><div class="label">Paused</div><div class="menu">
      <button class="btn" id="p1">Resume</button>
      <button class="btn" id="p2">Box Score</button>
      <button class="btn" id="p3">Sim to End of Game</button>
      <button class="btn" id="p4">Quit to Main Menu</button></div></div>`;
    this.ui.appendChild(d);
    this.overlay = d;
    const q = (id: string) => d.querySelector(id) as HTMLButtonElement;
    q('#p1').onclick = () => { d.remove(); this.state = this.resumeState; };
    q('#p2').onclick = () => { d.remove(); showBoxScore(this.ui, this.g, 'Box Score', [{ label: 'Resume', fn: () => (this.state = this.resumeState) }]); };
    q('#p3').onclick = () => {
      d.remove();
      if (this.sim && this.sim.phase === 'live') this.sim.runToEnd();
      if (this.sim && this.resumeState !== 'idle' && this.resumeState !== 'call' && this.resumeState !== 'kick' && this.resumeState !== 'wait' && this.sim.result && this.g.phase === 'scrimmage') {
        try { this.g.finishPlay(this.sim); } catch { /* ignore */ }
      }
      this.g.simToEnd();
      this.showFinal();
    };
    q('#p4').onclick = () => { d.remove(); this.destroy(); this.onExit(); };
  }

  private showFinal() {
    this.state = 'final';
    this.hud.update();
    this.hud.hint(null);
    this.hud.keys(null);
    const w = this.g.score.home === this.g.score.away ? 'Tie Game' : this.g.score.home > this.g.score.away ? `${this.g.home.city} ${this.g.home.name} win` : `${this.g.away.city} ${this.g.away.name} win`;
    showBoxScore(this.ui, this.g, `Final · ${w}`, [{ label: 'Main Menu', fn: () => { this.destroy(); this.onExit(); } }]);
  }

  destroy() {
    this.hud.destroy();
    this.overlay?.remove();
    this.ui.querySelectorAll('.callwrap,.modal').forEach((e) => e.remove());
  }
}
