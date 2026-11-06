import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(19);
const reasons: Record<string, number> = {};
for (let i = 0; i < 100; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'pass'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline' && d.blitz.length === 0));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  const prev = new Map<any, any>();
  while (sim.phase === 'live' && !sim.passThrown) {
    sim.step();
    for (const d of sim.def) {
      const was = prev.get(d);
      if (was && !d.engaged && d.state === 'rush') {
        const k = `t${Math.floor(sim.t*2)/2} blockerStun=${was.stun > 0.3} blockerState=${was.state} by=${was.role}`;
        reasons[k] = (reasons[k] ?? 0) + 1;
      }
      prev.set(d, d.engaged);
    }
  }
}
console.log(Object.entries(reasons).sort().map(([k,v])=>`${v}  ${k}`).join('\n'));
