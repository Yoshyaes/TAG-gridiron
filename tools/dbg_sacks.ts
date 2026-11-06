import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim, Ent } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(9);
const agg: Record<string, number> = {};
const inc = (k: string) => (agg[k] = (agg[k] ?? 0) + 1);
for (let i = 0; i < 400; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'pass' || p.type === 'playaction'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  // instrument: record state of sacker at the time
  let wasEngaged = false; let blockedEver = new Set<Ent>();
  while (sim.phase === 'live') {
    for (const d of sim.def) if (d.engagedBy.length) blockedEver.add(d);
    sim.step();
  }
  const r = sim.result!;
  if (r.kind === 'sack') {
    const s = sim.sackBy!;
    inc(`role:${s.role}`);
    inc(`everBlocked:${blockedEver.has(s)}`);
    inc(`t:${Math.min(6, Math.floor(r.duration))}`);
    inc(`pending:${!!sim.pendingThrow}`);
    inc(`drop:${sim.dropDone}`);
  }
}
console.log(Object.entries(agg).sort().map(([k, v]) => `${k}=${v}`).join('\n'));
