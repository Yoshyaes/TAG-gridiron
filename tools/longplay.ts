import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(Number(process.argv[3] ?? 5));
const type = process.argv[2] ?? 'pass';
for (let i = 0; i < 500; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => type === 'pass' ? p.type === 'pass' : p.type === 'run'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const mk = (seed: number) => new PlaySim({ off: league[3], def: league[9], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: false, weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: new Rng(seed) });
  const seed = rng.int(1, 1e9);
  const r = mk(seed).runToEnd();
  if (r.yards < 40) continue;
  console.log(`LONG PLAY ${op.name} vs ${dc.name}: ${r.yards} yds kind=${r.kind} air=${r.airYards} brk=${r.brokenTackles}`);
  const sim = mk(seed);
  let k = 0;
  while (sim.phase === 'live' && k++ < 2000) {
    sim.step();
    if (k % 30 === 0 && sim.ball.holder && sim.ball.holder !== sim.qb) {
      const c = sim.ball.holder;
      const near = sim.def.filter((d) => d.state !== 'down').sort((a, b) => Math.hypot(a.pos.x - c.pos.x, a.pos.y - c.pos.y) - Math.hypot(b.pos.x - c.pos.x, b.pos.y - c.pos.y)).slice(0, 4);
      console.log(`t=${sim.t.toFixed(1)} carrier ${c.role} (${c.pos.x.toFixed(1)},${c.pos.y.toFixed(1)}) v=${c.speed.toFixed(1)}/${c.vmax.toFixed(1)}`);
      for (const d of near) console.log(`    ${d.role.padEnd(4)} ${d.state.padEnd(8)} (${d.pos.x.toFixed(1)},${d.pos.y.toFixed(1)}) v=${d.speed.toFixed(1)}/${d.vmax.toFixed(1)} want=${Math.hypot(d.want.x,d.want.y).toFixed(1)} eng=${d.engaged?.role ?? '-'} stun=${d.stun.toFixed(2)} cd=${d.tackleCd.toFixed(2)}`);
    }
  }
  console.log(sim.events.map((e) => `${e.t.toFixed(1)}:${e.type}:${e.a?.role ?? ''}${e.b ? '>' + e.b.role : ''}`).join(' '));
  break;
}
