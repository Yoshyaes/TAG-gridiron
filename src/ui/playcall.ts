// Play-calling overlay with auto-generated play diagrams.
import { OFF_PLAYS, DEF_PLAYS, FORMATIONS, OffPlay, DefCall, FIELD_W } from '../sim/playbook';
import { PlaySim } from '../sim/play';
import type { Game } from '../sim/game';
import { Rng } from '../sim/rng';

export type CallChoice =
  | { kind: 'off'; play: OffPlay }
  | { kind: 'def'; call: DefCall }
  | { kind: 'punt' }
  | { kind: 'fg' }
  | { kind: 'auto' };

function diagram(game: Game, play: OffPlay | null, call: DefCall | null): string {
  // render using a throwaway sim so the diagram matches real alignments
  const off = play ?? OFF_PLAYS.find((p) => p.id === 'dbl_curls')!;
  const def = call ?? DEF_PLAYS[0];
  const sim = new PlaySim({
    off: game.offense, def: game.defense, offPlay: off, defCall: def, los: 50, ballY: FIELD_W / 2, flip: false,
    weather: { windX: 0, windY: 0, precip: 'none', tempF: 70 }, worldDir: 1, rng: new Rng(1),
  });
  // view: x (downfield) is up; y (+ = offense left) is screen left
  const X = (y: number) => ((FIELD_W - y) / FIELD_W) * 200;
  const Y = (x: number) => 50 - (x - 50) * 2.2;
  let s = `<svg viewBox="0 0 200 64" preserveAspectRatio="none"><line x1="0" x2="200" y1="${Y(50)}" y2="${Y(50)}" stroke="#7fb3ff" stroke-width="0.6"/>`;
  if (play) {
    for (const e of sim.off) {
      const cx = X(e.pos.y);
      const cy = Y(e.pos.x);
      if (e.route.length) {
        const pts = [e.pos, ...e.route].map((p) => `${X(p.y).toFixed(1)},${Math.max(1, Y(p.x)).toFixed(1)}`).join(' ');
        s += `<polyline points="${pts}" fill="none" stroke="${e.settle ? '#ffd400' : '#fff'}" stroke-width="1.1"/>`;
      }
      s += `<circle cx="${cx}" cy="${cy}" r="2.2" fill="${e.role === 'QB' ? '#ffd400' : '#fff'}"/>`;
    }
    if (play.run) {
      const rb = sim.offRoles.get(play.run.carrier)!;
      const aimY = sim.by + play.run.aimDy;
      s += `<polyline points="${X(rb.pos.y)},${Y(rb.pos.x)} ${X(aimY)},${Y(50.5)} ${X(aimY)},${Y(56)}" fill="none" stroke="#ff8a3d" stroke-width="1.4"/>`;
    }
  } else if (call) {
    for (const e of sim.def) {
      const cx = X(e.pos.y);
      const cy = Y(e.pos.x);
      let col = '#ff6b6b';
      if (e.state === 'rush') col = '#ff3b3b';
      if (e.state === 'zone' && e.zoneSpot) {
        const zx = X(e.zoneSpot.y);
        const zy = Math.max(3, Y(e.zoneSpot.x));
        const deep = /^(deep|third|half|q)/.test(e.zone ?? '');
        s += `<ellipse cx="${zx}" cy="${zy}" rx="${deep ? 22 : 13}" ry="${deep ? 5 : 4}" fill="${deep ? 'rgba(80,140,255,0.35)' : 'rgba(255,212,0,0.3)'}"/><line x1="${cx}" y1="${cy}" x2="${zx}" y2="${zy}" stroke="rgba(255,255,255,0.4)" stroke-width="0.5"/>`;
        col = '#7fb3ff';
      }
      if (e.state === 'man' && e.man) {
        s += `<line x1="${cx}" y1="${cy}" x2="${X(e.man.pos.y)}" y2="${Y(e.man.pos.x)}" stroke="#c38bff" stroke-width="0.8" stroke-dasharray="2 1"/>`;
        col = '#c38bff';
      }
      if (e.state === 'rush') s += `<line x1="${cx}" y1="${cy}" x2="${X(sim.by)}" y2="${Y(46)}" stroke="#ff3b3b" stroke-width="0.6"/>`;
      s += `<text x="${cx}" y="${cy + 1.6}" font-size="5" text-anchor="middle" fill="${col}" font-weight="900">x</text>`;
    }
    for (const e of sim.off) s += `<circle cx="${X(e.pos.y)}" cy="${Y(e.pos.x)}" r="1.5" fill="rgba(255,255,255,0.5)"/>`;
  }
  return s + '</svg>';
}

const COV_DESC: Record<string, string> = {
  c0: 'Cover 0 · all-out man, no deep help',
  c1: 'Cover 1 · man, single-high FS',
  c2: 'Cover 2 · 2 deep halves, 5 under',
  c2m: '2-Man · man under, 2 deep',
  c3: 'Cover 3 · 3 deep, 4 under',
  c3fire: 'Fire Zone · 5-man pressure, 3 deep 3 under',
  c4: 'Quarters · 4 deep',
};

export function showPlayCall(ui: HTMLElement, game: Game, side: 'off' | 'def', opts: { fourthDown?: boolean; twoPoint?: boolean }, done: (c: CallChoice) => void) {
  const wrap = document.createElement('div');
  wrap.className = 'callwrap';
  ui.appendChild(wrap);
  let tab = side === 'off' ? FORMATIONS[0].id : 'nickel';
  const close = (c: CallChoice) => {
    window.removeEventListener('keydown', onKey);
    wrap.remove();
    done(c);
  };
  const onKey = (e: KeyboardEvent) => {
    if (e.code === 'KeyR') close({ kind: 'auto' });
    if (e.code.startsWith('Digit')) {
      const n = Number(e.code.slice(5)) - 1;
      const btn = wrap.querySelectorAll<HTMLButtonElement>('.card')[n];
      btn?.click();
    }
  };
  window.addEventListener('keydown', onKey);
  const render = () => {
    const sit = `${game.downText()} at ${game.ydLine()} · ${game.offense.abbr} ball · Q${Math.min(game.q, 5)} ${Math.floor(game.clock / 60)}:${String(Math.ceil(game.clock % 60) % 60).padStart(2, '0')}`;
    let html = `<div class="callhead"><h2>${side === 'off' ? (opts.twoPoint ? 'Two-Point Try' : 'Call Offense') : 'Call Defense'}</h2><span class="sit">${opts.twoPoint ? 'Ball at the 2' : sit}</span><span class="sit">R = Coach's pick · 1-9 = select</span></div>`;
    html += '<div class="tabs">';
    if (side === 'off') {
      for (const f of FORMATIONS) html += `<button class="tab ${tab === f.id ? 'on' : ''}" data-tab="${f.id}">${f.name} <small style="opacity:.7">${f.personnel}</small></button>`;
    } else {
      for (const f of ['base', 'nickel', 'dime', 'goalline']) html += `<button class="tab ${tab === f ? 'on' : ''}" data-tab="${f}">${f === 'goalline' ? 'Goal Line' : f[0].toUpperCase() + f.slice(1)}</button>`;
    }
    html += `<button class="tab" data-auto="1">Coach's Pick</button></div><div class="cards">`;
    if (side === 'off') {
      if (opts.fourthDown) {
        const k = game.kicker(game.poss);
        const dist = Math.round(100 - game.ballOn + 17);
        const pct = Math.round(game.fgProb(dist, k, game.poss) * 100);
        html += `<button class="card special" data-special="punt"><span class="ty">Special Teams</span><span class="nm">Punt</span><span class="pct">${game.punter(game.poss).last}, ${game.punter(game.poss).r.kickPower} PWR</span></button>`;
        html += `<button class="card special" data-special="fg"><span class="ty">Special Teams</span><span class="nm">Field Goal (${dist} yd)</span><span class="pct">${pct}% est. make · ${k.last}</span></button>`;
      }
      for (const p of OFF_PLAYS.filter((p) => p.formation === tab)) {
        html += `<button class="card" data-play="${p.id}"><span class="ty">${p.type === 'playaction' ? 'Play Action' : p.type}${p.tags.includes('quick') ? ' · quick' : ''}${p.tags.includes('deep') ? ' · shot' : ''}</span><span class="nm">${p.name}</span>${diagram(game, p, null)}</button>`;
      }
    } else {
      for (const d of DEF_PLAYS.filter((d) => d.front === tab)) {
        html += `<button class="card" data-def="${d.id}"><span class="ty">${COV_DESC[d.coverage]}${d.blitz.length ? ` · ${4 + d.blitz.length}-man rush` : ''}</span><span class="nm">${d.name}</span>${diagram(game, null, d)}</button>`;
      }
    }
    html += '</div>';
    wrap.innerHTML = html;
    wrap.querySelectorAll<HTMLButtonElement>('[data-tab]').forEach((b) => (b.onclick = () => { tab = b.dataset.tab!; render(); }));
    wrap.querySelectorAll<HTMLButtonElement>('[data-auto]').forEach((b) => (b.onclick = () => close({ kind: 'auto' })));
    wrap.querySelectorAll<HTMLButtonElement>('[data-play]').forEach((b) => (b.onclick = () => close({ kind: 'off', play: OFF_PLAYS.find((p) => p.id === b.dataset.play)! })));
    wrap.querySelectorAll<HTMLButtonElement>('[data-def]').forEach((b) => (b.onclick = () => close({ kind: 'def', call: DEF_PLAYS.find((p) => p.id === b.dataset.def)! })));
    wrap.querySelectorAll<HTMLButtonElement>('[data-special]').forEach((b) => (b.onclick = () => close({ kind: b.dataset.special as 'punt' | 'fg' })));
  };
  render();
}
