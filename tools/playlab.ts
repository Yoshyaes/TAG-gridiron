// Play lab: run one play many times and report outcome distributions.
// usage: tsx tools/playlab.ts [offPlayId|run|pass] [defId] [n] [trace]
import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, playById, defById, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';

const league = createLeague(2027);
const which = process.argv[2] ?? 'run';
const defId = process.argv[3] ?? 'any';
const n = Number(process.argv[4] ?? 300);
const trace = process.argv[5] === 'trace';
const rng = new Rng(11);

const offPool = which === 'run' ? OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak') : which === 'pass' ? OFF_PLAYS.filter((p) => p.type === 'pass' || p.type === 'playaction' || p.type === 'screen') : [playById(which)];
const defPool = defId === 'any' ? DEF_PLAYS.filter((d) => d.front !== 'goalline') : [defById(defId)];

const yards: number[] = [];
let comp = 0, att = 0, sacks = 0, ints = 0, fum = 0, tds = 0, inc = 0, tta = 0, brk = 0, yac = 0, air = 0, scr = 0, ta = 0, dur = 0, drops = 0, pbu = 0;
for (let i = 0; i < n; i++) {
  const off = league[rng.int(0, 31)];
  const def = league[rng.int(0, 31)];
  const op = rng.pick(offPool);
  const dc = rng.pick(defPool);
  const sim = new PlaySim({ off, def, offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  if (trace && i === 0) {
    for (let k = 0; k < 600 && sim.phase === 'live'; k++) {
      sim.step();
      if (k % 15 === 0) {
        const c = sim.ball;
        console.log(`t=${sim.t.toFixed(2)} ball=${c.mode} ${c.pos.x.toFixed(1)},${c.pos.y.toFixed(1)},${c.pos.z.toFixed(1)} holder=${c.holder?.role ?? '-'}`);
        for (const e of sim.ents) if (['QB', 'RB', 'X', 'LE', 'MLB', 'LCB', 'FS', 'LT'].includes(e.role)) console.log(`   ${e.side} ${e.role.padEnd(4)} ${e.state.padEnd(9)} pos ${e.pos.x.toFixed(1)},${e.pos.y.toFixed(1)} v ${e.speed.toFixed(1)} eng ${e.engaged?.role ?? '-'}`);
      }
    }
    console.log(sim.endReason, sim.result);
  }
  const r = sim.runToEnd();
  yards.push(r.yards);
  dur += r.duration;
  if (r.kind === 'pass') {
    att++;
    if (r.complete) { comp++; yac += r.yac; air += r.airYards; }
    if (r.incomplete) inc++;
    tta += r.timeToThrow;
  }
  if (r.kind === 'sack') sacks++;
  if (r.kind === 'scramble') scr++;
  if (r.interception) ints++;
  if (r.fumbleLost) fum++;
  if (r.td) tds++;
  if (r.thrownAway) ta++;
  if (r.events.some((e) => e.type === 'drop')) drops++;
  if (r.pbuBy) pbu++;
  brk += r.brokenTackles;
}
const mean = yards.reduce((a, b) => a + b, 0) / n;
const sorted = [...yards].sort((a, b) => a - b);
const pct = (q: number) => sorted[Math.floor(q * (n - 1))];
console.log(`plays=${n} mean=${mean.toFixed(2)} median=${pct(0.5)} p10=${pct(0.1)} p90=${pct(0.9)} max=${sorted[n - 1]} dur=${(dur / n).toFixed(1)}s`);
const hist: Record<string, number> = {};
for (const y of yards) {
  const b = y < 0 ? '<0' : y < 3 ? '0-2' : y < 5 ? '3-4' : y < 10 ? '5-9' : y < 20 ? '10-19' : '20+';
  hist[b] = (hist[b] ?? 0) + 1;
}
console.log('hist', Object.entries(hist).map(([k, v]) => `${k}:${((100 * v) / n).toFixed(0)}%`).join(' '));
if (att) console.log(`att=${att} comp%=${((100 * comp) / att).toFixed(1)} ypa=${(yards.filter((_, i) => true).reduce((a, b) => a + b, 0) / n).toFixed(1)} air/comp=${(air / Math.max(1, comp)).toFixed(1)} yac/comp=${(yac / Math.max(1, comp)).toFixed(1)} tta=${(tta / att).toFixed(2)} drops=${drops} pbu=${pbu} throwaways=${ta}`);
console.log(`sacks=${sacks} (${((100 * sacks) / n).toFixed(1)}%) scrambles=${scr} ints=${ints} fumLost=${fum} tds=${tds} brokenTackles/play=${(brk / n).toFixed(2)}`);
