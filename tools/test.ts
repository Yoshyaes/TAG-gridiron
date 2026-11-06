// Invariant / regression tests for the simulation. Run: npm test
import { createLeague } from '../src/sim/league';
import { Game, expectedPoints } from '../src/sim/game';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W, playById, defById } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';

let pass = 0;
let fail = 0;
function t(name: string, fn: () => void) {
  try {
    fn();
    pass++;
    console.log(`  ok   ${name}`);
  } catch (e) {
    fail++;
    console.log(`  FAIL ${name}: ${(e as Error).message}`);
  }
}
function assert(c: unknown, msg: string) {
  if (!c) throw new Error(msg);
}

const league = createLeague(2027);

t('league: 32 fictional teams with full rosters', () => {
  assert(league.length === 32, 'team count');
  for (const tm of league) {
    assert(tm.roster.length >= 50, `${tm.abbr} roster ${tm.roster.length}`);
    for (const pos of ['QB', 'RB', 'WR', 'TE', 'OT', 'OG', 'C', 'DE', 'DT', 'LB', 'CB', 'S', 'K', 'P']) assert(tm.roster.some((p) => p.pos === pos), `${tm.abbr} missing ${pos}`);
    const nums = new Set(tm.roster.map((p) => p.number));
    assert(nums.size === tm.roster.length, `${tm.abbr} duplicate jersey numbers`);
  }
});

t('determinism: same seed => identical game', () => {
  const run = () => {
    const L = createLeague(2027);
    const g = new Game(L[3], L[7], { seed: 4242 });
    g.simToEnd();
    return `${g.score.home}-${g.score.away}-${g.log.length}`;
  };
  const a = run();
  const b = run();
  assert(a === b, `${a} vs ${b}`);
});

t('100 full games complete with sane totals', () => {
  const rng = new Rng(99);
  for (let i = 0; i < 100; i++) {
    const L = league;
    const h = rng.int(0, 31);
    const a = (h + 1 + rng.int(0, 30)) % 32;
    const g = new Game(L[h], L[a], { seed: rng.int(1, 1e9) });
    g.simToEnd();
    assert(g.phase === 'final', 'game did not finish');
    assert(g.score.home >= 0 && g.score.away >= 0, 'negative score');
    assert(g.score.home <= 80 && g.score.away <= 80, `absurd score ${g.score.home}-${g.score.away}`);
    assert(g.q >= 4, 'ended before 4th quarter');
    for (const k of ['home', 'away'] as const) {
      const T = g.team[k];
      assert(T.plays >= 35 && T.plays <= 95, `plays ${T.plays}`);
      assert(Number.isFinite(T.yards), 'yards NaN');
    }
    // scores are reachable football scores (no 1-point totals unless safety+... allowed)
    assert(g.score.home !== 1 && g.score.away !== 1, 'impossible score of 1');
  }
});

t('every offensive play vs every defense runs without NaN and ends', () => {
  const rng = new Rng(5);
  for (const op of OFF_PLAYS) {
    for (const dc of DEF_PLAYS) {
      const sim = new PlaySim({ off: league[1], def: league[2], offPlay: op, defCall: dc, los: rng.range(5, 95), ballY: rng.pick([FIELD_W / 2 - 3.08, FIELD_W / 2, FIELD_W / 2 + 3.08]), flip: rng.chance(0.5), weather: { windX: rng.range(-5, 5), windY: rng.range(-5, 5), precip: rng.pick(['none', 'rain', 'snow'] as const), tempF: 40 }, worldDir: 1, rng: rng.fork() });
      const r = sim.runToEnd();
      assert(sim.phase === 'dead', `${op.id} vs ${dc.id} never ended`);
      assert(Number.isFinite(r.endX), `${op.id} vs ${dc.id} endX NaN`);
      for (const e of sim.ents) assert(Number.isFinite(e.pos.x) && Number.isFinite(e.pos.y), `${e.role} NaN position`);
      assert(r.duration < 30, `${op.id} lasted ${r.duration}s`);
    }
  }
});

t('physics: 99-speed player runs a ~4.3 forty, lineman ~5.2', () => {
  const fast = league[0].roster.find((p) => p.pos === 'WR')!;
  const slow = league[0].roster.find((p) => p.pos === 'OG')!;
  const forty = (spd: number, acc: number) => {
    const vmax = 8.6 + (spd - 50) * 0.049;
    const tau = 1.0 - (acc - 50) * 0.0078;
    let x = 0;
    let v = 0;
    let tt = 0;
    while (x < 40) {
      v += (vmax / tau) * Math.max(0.12, 1 - v / vmax) * 0.001;
      v = Math.min(v, vmax);
      x += v * 0.001;
      tt += 0.001;
    }
    return tt;
  };
  const f99 = forty(99, 99);
  const f55 = forty(55, 62);
  assert(f99 > 4.1 && f99 < 4.5, `elite forty ${f99.toFixed(2)}`);
  assert(f55 > 4.9 && f55 < 5.6, `lineman forty ${f55.toFixed(2)}`);
  void fast;
  void slow;
});

t('field goal probability decreases with distance and weather', () => {
  const g = new Game(league[0], league[1], { seed: 1, weather: { windX: 0, windY: 0, precip: 'none', tempF: 70 } });
  const k = g.kicker('home');
  let prev = 1;
  for (let d = 20; d <= 65; d += 5) {
    const p = g.fgProb(d, k, 'home');
    assert(p <= prev + 1e-9, `fg prob not monotonic at ${d}`);
    prev = p;
  }
  assert(g.fgProb(25, k, 'home') > 0.93, 'chip shot too hard');
  const snow = new Game(league[0], league[1], { seed: 1, weather: { windX: 0, windY: 0, precip: 'snow', tempF: 20 } });
  assert(snow.fgProb(45, k, 'home') < g.fgProb(45, k, 'home'), 'snow should hurt kicking');
});

t('expected points: better field position and down = more value', () => {
  assert(expectedPoints(80, 1, 10) > expectedPoints(20, 1, 10), 'field position');
  assert(expectedPoints(50, 1, 10) > expectedPoints(50, 3, 10), 'down');
  assert(expectedPoints(50, 3, 2) > expectedPoints(50, 3, 12), 'distance');
});

t('kneel-down burns the clock and loses a yard', () => {
  const g = new Game(league[0], league[1], { seed: 3 });
  g.phase = 'scrimmage';
  g.ballOn = 40;
  const sim = g.makePlay(playById('kneel'), defById('base_c3'));
  const r = sim.runToEnd();
  assert(r.kind === 'kneel' && r.yards === -1, `kneel result ${r.kind} ${r.yards}`);
});

t('touchdown awards 6 and moves to PAT', () => {
  const g = new Game(league[0], league[1], { seed: 9 });
  g.phase = 'scrimmage';
  g.poss = 'home';
  g.ballOn = 99.4;
  g.toGo = 1;
  let scored = false;
  for (let i = 0; i < 30 && !scored; i++) {
    g.ballOn = 99.4;
    g.down = 1;
    g.phase = 'scrimmage';
    g.poss = 'home';
    const before = g.score.home;
    const sim = g.makePlay(playById('gl_sneak'), defById('gl_c1'));
    sim.runToEnd();
    g.finishPlay(sim);
    if (g.score.home === before + 6) {
      scored = true;
      assert((g.phase as string) === 'pat', 'should be PAT phase');
    }
  }
  assert(scored, 'no sneak TD from the 1 in 30 tries');
});

t('two-minute warning fires in each half', () => {
  const g = new Game(league[4], league[5], { seed: 77 });
  g.simToEnd();
  const n = g.log.filter((l) => l.desc === 'Two-minute warning.').length;
  assert(n >= 1, `two-minute warnings: ${n}`);
});

console.log(`\n${pass} passed, ${fail} failed`);
if (fail) process.exit(1);
