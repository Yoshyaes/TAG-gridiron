import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027); const rng = new Rng(41);
const why: Record<string, number> = {}; let n = 0, comp = 0;
const pool = DEF_PLAYS.filter((d) => d.front !== 'goalline');
for (let i = 0; i < 2000; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'pass'));
  const dc = rng.weighted(pool, pool.map((d) => (d.coverage === 'c0' ? 0.12 : 1)));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  const r = sim.runToEnd();
  if (r.kind !== 'pass' || r.thrownAway || r.airYards < 0 || r.airYards > 9) continue;
  n++;
  if (r.complete) { comp++; continue; }
  const ev = sim.events.map((e) => e.type);
  const k = r.interception ? 'int' : ev.includes('batted') ? 'batted' : ev.includes('pbu') ? 'pbu' : ev.includes('drop') ? 'drop' : ev.includes('contested_inc') ? 'contested_inc' : ev.includes('oobcatch') ? 'oob' : 'uncatchable';
  why[k] = (why[k] ?? 0) + 1;
}
console.log(`0-9 yd attempts ${n} comp ${(100*comp/n).toFixed(1)}%`);
for (const [k, v] of Object.entries(why)) console.log(`  ${k.padEnd(14)} ${(100*v/n).toFixed(1)}%`);
console.log('NFL-ish split of short incompletions: drops ~4%, PBU ~6%, uncatchable/off-target ~12%, int ~1.5%');
