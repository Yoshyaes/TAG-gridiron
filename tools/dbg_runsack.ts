import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(3);
const counts: Record<string, number> = {};
for (let i = 0; i < 600; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  const r = sim.runToEnd();
  if (r.kind === 'sack') {
    const rb = sim.offRoles.get(op.run!.carrier)!;
    const k = `${op.id} t=${r.duration.toFixed(1)} by=${sim.sackBy?.role} handed=${sim.handedOff} rbdist=${Math.hypot(rb.pos.x - sim.qb.pos.x, rb.pos.y - sim.qb.pos.y).toFixed(1)}`;
    console.log(k);
  }
}
