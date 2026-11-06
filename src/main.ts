// TAG Gridiron - entry point: menus, team select, game loop.
import './style.css';
import { createLeague, teamOvr } from './sim/league';
import { Game, randomWeather, TeamKey } from './sim/game';
import type { Weather } from './sim/play';
import { Rng } from './sim/rng';
import type { Team } from './sim/types';
import { World } from './render/world';
import { Match } from './match';
import { Input } from './input';
import { showBoxScore } from './ui/boxscore';

const ui = document.getElementById('ui')!;
const canvas = document.getElementById('view') as HTMLCanvasElement;
const input = new Input();
const league = createLeague(2027);
let world: World | null = null;
let match: Match | null = null;
let menuWorld: World | null = null;

interface Setup {
  home: Team;
  away: Team;
  user: TeamKey | null;
  weather: string;
  night: boolean;
  quarter: number;
  mode: 'play' | 'watch' | 'sim';
}

function clearUI() {
  ui.innerHTML = '';
}

function mainMenu() {
  match = null;
  clearUI();
  showMenuBackdrop();
  const s = document.createElement('div');
  s.className = 'screen';
  s.innerHTML = `
    <h1 class="title">TAG Gridiron</h1>
    <div class="subtitle">Simulation-first pro football</div>
    <div class="menu">
      <button class="btn primary" data-m="play">Play Now<small>You vs CPU. Call plays, control the QB, ball carrier or a defender.</small></button>
      <button class="btn" data-m="watch">Watch a Game<small>CPU vs CPU with full 3D broadcast. Speed up anytime.</small></button>
      <button class="btn" data-m="sim">Quick Sim<small>Simulate a full game instantly and read the box score.</small></button>
      <button class="btn" data-m="season">Season Sim<small>Run a 17-game season for all 32 teams and see standings & leaders.</small></button>
      <button class="btn" data-m="how">How It Works<small>The realism model under the hood.</small></button>
    </div>
    <div style="position:absolute;bottom:14px;color:#56627a;font-size:12px">An original football game. All teams, players and names are fictional.</div>`;
  ui.appendChild(s);
  s.querySelectorAll<HTMLButtonElement>('[data-m]').forEach((b) => {
    b.onclick = () => {
      const m = b.dataset.m!;
      if (m === 'how') return howItWorks();
      if (m === 'season') return seasonSim();
      teamSelect(m as Setup['mode']);
    };
  });
}

function showMenuBackdrop() {
  // a slowly orbiting stadium behind the menus
  if (world) {
    world.dispose();
    world = null;
  }
  if (!menuWorld) {
    menuWorld = new World(canvas, true);
    const w = randomWeather(new Rng(3));
    w.precip = 'none';
    menuWorld.build(league[0], league[1], w);
  }
}

function teamSelect(mode: Setup['mode']) {
  clearUI();
  let homeIdx = 0;
  let awayIdx = 1;
  let userSide: TeamKey = 'home';
  const s = document.createElement('div');
  s.className = 'screen';
  const card = (t: Team, i: number, sel: boolean, side: string) => {
    const o = teamOvr(t);
    return `<button class="team-card ${sel ? 'sel' : ''}" data-side="${side}" data-i="${i}"><span class="swatch" style="background:${t.colors.primary};border-color:${t.colors.secondary}"></span><span><div class="nm">${t.city} ${t.name}</div><div class="ov">OVR ${o.ovr} &middot; OFF ${o.off} &middot; DEF ${o.def}</div></span></button>`;
  };
  const render = () => {
    s.innerHTML = `
      <h1 class="title" style="font-size:40px">${mode === 'play' ? 'Play Now' : mode === 'watch' ? 'Watch a Game' : 'Quick Sim'}</h1>
      <div class="row" style="margin-top:18px">
        <div class="panel"><div class="label">Away team</div><div class="teams">${league.map((t, i) => card(t, i, i === awayIdx, 'away')).join('')}</div></div>
        <div class="panel"><div class="label">Home team</div><div class="teams">${league.map((t, i) => card(t, i, i === homeIdx, 'home')).join('')}</div></div>
      </div>
      <div class="panel" style="margin-top:14px">
        <div class="opts">
          ${mode === 'play' ? `<span>You control</span><select id="us"><option value="home" ${userSide === 'home' ? 'selected' : ''}>Home: ${league[homeIdx].abbr}</option><option value="away" ${userSide === 'away' ? 'selected' : ''}>Away: ${league[awayIdx].abbr}</option></select>` : ''}
          <span>Weather</span><select id="wx"><option value="auto">Random (realistic)</option><option value="clear">Clear</option><option value="windy">Windy</option><option value="rain">Rain</option><option value="snow">Snow</option></select>
          <span>Kickoff</span><select id="tod"><option value="night">Night (lights)</option><option value="day">Day</option></select>
          <span>Quarter length</span><select id="ql"><option value="900">15 min (authentic NFL clock)</option><option value="480">8 min</option><option value="300" ${mode === 'play' ? 'selected' : ''}>5 min</option></select>
        </div>
      </div>
      <div class="row" style="margin-top:14px"><button class="btn slim" id="back">Back</button><button class="btn slim primary" id="go">Kick Off</button></div>`;
    s.querySelectorAll<HTMLButtonElement>('.team-card').forEach((b) => {
      b.onclick = () => {
        const i = Number(b.dataset.i);
        if (b.dataset.side === 'home') {
          if (i !== awayIdx) homeIdx = i;
        } else if (i !== homeIdx) awayIdx = i;
        render();
      };
    });
    const us = s.querySelector('#us') as HTMLSelectElement | null;
    if (us) us.onchange = () => (userSide = us.value as TeamKey);
    (s.querySelector('#back') as HTMLButtonElement).onclick = mainMenu;
    (s.querySelector('#go') as HTMLButtonElement).onclick = () => {
      const wx = (s.querySelector('#wx') as HTMLSelectElement).value;
      const night = (s.querySelector('#tod') as HTMLSelectElement).value === 'night';
      const quarter = Number((s.querySelector('#ql') as HTMLSelectElement).value);
      start({ home: league[homeIdx], away: league[awayIdx], user: mode === 'play' ? userSide : null, weather: wx, night, quarter, mode });
    };
  };
  render();
  ui.appendChild(s);
}

function makeWeather(kind: string, rng: Rng): Weather {
  const w = randomWeather(rng);
  if (kind === 'clear') return { ...w, precip: 'none', windX: w.windX * 0.4, windY: w.windY * 0.4, tempF: Math.max(50, w.tempF) };
  if (kind === 'windy') {
    const a = rng.range(0, Math.PI * 2);
    return { ...w, precip: 'none', windX: Math.cos(a) * 9, windY: Math.sin(a) * 9 };
  }
  if (kind === 'rain') return { ...w, precip: 'rain', tempF: Math.max(40, Math.min(65, w.tempF)) };
  if (kind === 'snow') return { ...w, precip: 'snow', tempF: rng.range(18, 31) };
  return w;
}

function start(setup: Setup) {
  clearUI();
  const rng = new Rng(Math.floor(Math.random() * 1e9));
  const weather = makeWeather(setup.weather, rng);
  for (const t of [setup.home, setup.away]) for (const p of t.roster) p.injuredWeeks = 0;
  const game = new Game(setup.home, setup.away, { seed: rng.int(1, 1e9), quarterLen: setup.quarter, weather, userTeam: setup.user });
  if (setup.mode === 'sim') {
    game.simToEnd();
    showBoxScore(ui, game, 'Quick Sim · Final', [{ label: 'Play-by-play', fn: () => playByPlay(game) }, { label: 'Main Menu', fn: mainMenu }]);
    return;
  }
  if (menuWorld) {
    menuWorld.dispose();
    menuWorld = null;
  }
  world = new World(canvas, setup.night);
  world.build(setup.home, setup.away, weather);
  match = new Match(ui, world, input, game, setup.mode === 'watch' ? 'watch' : 'play', setup.user, mainMenu);
}

function playByPlay(g: Game) {
  const m = document.createElement('div');
  m.className = 'modal';
  m.innerHTML = `<div class="panel"><div class="label">Play-by-play</div><table>${g.log.map((l) => `<tr><td style="white-space:nowrap;color:#9aa6b8">Q${Math.min(l.q, 5)} ${Math.floor(l.clock / 60)}:${String(Math.ceil(l.clock % 60) % 60).padStart(2, '0')}</td><td style="text-align:left">${l.desc}</td><td>${l.score.away}-${l.score.home}</td></tr>`).join('')}</table><div class="row" style="justify-content:center;margin-top:12px"><button class="btn slim primary" id="pbx">Back</button></div></div>`;
  ui.appendChild(m);
  (m.querySelector('#pbx') as HTMLButtonElement).onclick = () => { m.remove(); showBoxScore(ui, g, 'Final', [{ label: 'Play-by-play', fn: () => playByPlay(g) }, { label: 'Main Menu', fn: mainMenu }]); };
}

function seasonSim() {
  clearUI();
  const s = document.createElement('div');
  s.className = 'screen';
  s.innerHTML = `<div class="panel" style="min-width:480px;text-align:center"><div class="label">Season Sim</div><div id="prog" style="font-size:22px;margin:16px">Simulating week 1...</div></div>`;
  ui.appendChild(s);
  const rng = new Rng(Math.floor(Math.random() * 1e9));
  const rec = new Map<number, { w: number; l: number; t: number; pf: number; pa: number }>();
  for (const t of league) rec.set(t.id, { w: 0, l: 0, t: 0, pf: 0, pa: 0 });
  const leaders = new Map<number, { name: string; team: string; pos: string; passYds: number; rushYds: number; recYds: number; sacks: number; ints: number; td: number }>();
  let week = 0;
  const step = () => {
    week++;
    const order = league.map((t) => t.id).sort(() => rng.next() - 0.5);
    for (let i = 0; i < order.length; i += 2) {
      const h = league[order[i]];
      const a = league[order[i + 1]];
      for (const p of [...h.roster, ...a.roster]) p.injuredWeeks = Math.max(0, p.injuredWeeks - 1);
      const g = new Game(h, a, { seed: rng.int(1, 1e9) });
      g.simToEnd();
      const rh = rec.get(h.id)!;
      const ra = rec.get(a.id)!;
      rh.pf += g.score.home; rh.pa += g.score.away; ra.pf += g.score.away; ra.pa += g.score.home;
      if (g.score.home > g.score.away) { rh.w++; ra.l++; } else if (g.score.home < g.score.away) { ra.w++; rh.l++; } else { rh.t++; ra.t++; }
      for (const l of g.stats.lines.values()) {
        let x = leaders.get(l.pid);
        if (!x) leaders.set(l.pid, (x = { name: l.name, team: l.team, pos: l.pos, passYds: 0, rushYds: 0, recYds: 0, sacks: 0, ints: 0, td: 0 }));
        x.passYds += l.passYds; x.rushYds += l.rushYds; x.recYds += l.recYds; x.sacks += l.sacks; x.ints += l.ints; x.td += l.passTD + l.rushTD + l.recTD;
      }
    }
    (s.querySelector('#prog') as HTMLElement).textContent = `Simulating week ${week + 1} of 17...`;
    if (week < 17) setTimeout(step, 0);
    else finish();
  };
  const finish = () => {
    const divs = [...new Set(league.map((t) => t.division))];
    const top = (k: 'passYds' | 'rushYds' | 'recYds' | 'sacks' | 'ints' | 'td') => [...leaders.values()].sort((a, b) => b[k] - a[k]).slice(0, 5).map((x) => `<tr><td>${x.name} <small>${x.pos} ${x.team}</small></td><td>${k === 'sacks' ? x[k].toFixed(1) : x[k]}</td></tr>`).join('');
    s.innerHTML = `<div class="panel" style="max-width:1200px;width:95vw;max-height:90vh;overflow:auto"><div class="label">Final Standings</div><div class="grid2">${divs.map((d) => `<div><h3>${d}</h3><table><tr><th>Team</th><th>W</th><th>L</th><th>T</th><th>PF</th><th>PA</th></tr>${league.filter((t) => t.division === d).sort((a, b) => rec.get(b.id)!.w - rec.get(a.id)!.w).map((t) => { const r = rec.get(t.id)!; return `<tr><td>${t.city} ${t.name}</td><td>${r.w}</td><td>${r.l}</td><td>${r.t}</td><td>${r.pf}</td><td>${r.pa}</td></tr>`; }).join('')}</table></div>`).join('')}</div>
      <div class="label" style="margin-top:20px">League Leaders</div><div class="grid2">
      <div><h3>Passing yards</h3><table>${top('passYds')}</table><h3>Rushing yards</h3><table>${top('rushYds')}</table><h3>Receiving yards</h3><table>${top('recYds')}</table></div>
      <div><h3>Sacks</h3><table>${top('sacks')}</table><h3>Interceptions</h3><table>${top('ints')}</table><h3>Total TDs</h3><table>${top('td')}</table></div></div>
      <div class="row" style="justify-content:center;margin-top:14px"><button class="btn slim primary" id="mm">Main Menu</button></div></div>`;
    (s.querySelector('#mm') as HTMLButtonElement).onclick = mainMenu;
  };
  setTimeout(step, 30);
}

function howItWorks() {
  clearUI();
  const s = document.createElement('div');
  s.className = 'screen';
  s.innerHTML = `<div class="panel" style="max-width:820px;line-height:1.55;font-size:15px">
    <div class="label">The realism model</div>
    <p><b>Every play is a physics simulation.</b> 22 athletes run at 60 Hz with mass, top speed, acceleration curves and change-of-direction limits derived from measurables (a 99-speed player runs a ~4.28 forty). Momentum decides collisions and how far a ball carrier falls forward.</p>
    <p><b>Line play is a contest, not a dice roll.</b> Blockers are assigned at the snap, linemen set a pocket, rushers win with power or finesse over time, double teams hold up, and a beaten blocker stays beaten. Pressure comes from real geometry.</p>
    <p><b>Quarterbacks read the field.</b> The QB works a progression, estimates separation at the catch point, throws with anticipation, puts touch over linebackers, feels unblocked rushers, uses a release windup that can get hit, and is less accurate under pressure, on the run, in rain, snow and cold.</p>
    <p><b>The ball flies.</b> Spiral drag, gravity and wind are integrated in 3D; the kicking game uses distance, leg strength, wind and weather.</p>
    <p><b>Coverage is real.</b> Cover 0/1/2/3/4, 2-Man and fire-zone pressures: zone defenders match threats in their area, deep defenders keep a speed-based cushion, man defenders trail with reaction lag based on their ratings, and contested catches are decided by body position.</p>
    <p><b>Full NFL rules engine.</b> Game clock rules (2:00 warning, out-of-bounds windows, runoff, timeouts), penalties with accept/decline logic using expected points, analytics-informed 4th-down and two-point decisions, modern kickoff touchbacks, overtime, fatigue rotation and injuries.</p>
    <p><b>Calibrated against the league.</b> Thousands of simulated games are compared with real per-game averages (yards per play, completion %, sack rate, yards per carry, scoring, FG%) using the built-in calibration harness (<code>npm run calibrate</code>).</p>
    <div class="row" style="justify-content:center"><button class="btn slim primary" id="bk">Back</button></div></div>`;
  ui.appendChild(s);
  (s.querySelector('#bk') as HTMLButtonElement).onclick = mainMenu;
}

// ------------------------------------------------------------------ main loop
let last = performance.now();
let orbit = 0;
function frame(now: number) {
  const dt = Math.min(0.05, (now - last) / 1000);
  last = now;
  if (match && world) {
    match.update(dt);
    world.render(dt);
  } else if (menuWorld) {
    orbit += dt * 0.05;
    menuWorld.camera.position.set(50 + Math.cos(orbit) * 75, 26, Math.sin(orbit) * 60);
    menuWorld.camera.lookAt(50, 0, 0);
    menuWorld.render(dt);
  }
  requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
mainMenu();

// expose for debugging/automation
(window as unknown as { tag: unknown }).tag = { get match() { return match; }, league };
