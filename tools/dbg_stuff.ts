import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim, Ent } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(21);
const agg: Record<string, number> = {};
const inc = (k: string) => (agg[k] = (agg[k] ?? 0) + 1);
let n = 0;
for (let i = 0; i < 500; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  const blocked = new Set<Ent>();
  let handT = -1, losT = -1;
  while (sim.phase === 'live') { sim.step(); for (const d of sim.def) if (d.engagedBy.length) blocked.add(d); if (sim.handedOff && handT < 0) handT = sim.t; }
  const r = sim.result!;
  if (r.yards < 0) {
    n++;
    const tk = sim.tacklers[0];
    inc(`tackler:${tk?.role ?? 'none'}`);
    inc(`tacklerEverBlocked:${tk ? blocked.has(tk) : 'na'}`);
    inc(`brokenFirst:${r.brokenTackles > 0}`);
    inc(`play:${op.id}`);
    inc(`dur:${Math.floor(r.duration)}`);
    inc(`handedOff:${sim.handedOff}`);
  }
}
console.log('negative runs', n); console.log(Object.entries(agg).sort().map(([k, v]) => `${k}=${v}`).join('  '));
