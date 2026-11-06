import { createLeague } from '../src/sim/league';
import { playById, defById, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const sim = new PlaySim({ off: league[1], def: league[2], offPlay: playById(process.argv[2] ?? 'ace_iz'), defCall: defById('base_c3'), los: 30, ballY: FIELD_W / 2, flip: false, weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: new Rng(Number(process.argv[3] ?? 1)) });
for (const e of sim.off) if (e.blockTarget) console.log('assign', e.role, '->', e.blockTarget.role);
for (let k = 0; k < 90 && sim.phase === 'live'; k++) {
  sim.step();
  if (k % 6 === 0) console.log(`t=${sim.t.toFixed(2)} ball=${sim.ball.mode} holder=${sim.ball.holder?.role ?? '-'} QB(${sim.qb.pos.x.toFixed(1)},${sim.qb.pos.y.toFixed(1)}) RB(${sim.offRoles.get('RB')!.pos.x.toFixed(1)},${sim.offRoles.get('RB')!.pos.y.toFixed(1)}) ` + sim.def.filter(d => ['LDT','RDT','LE','RE'].includes(d.role)).map(d => `${d.role}(${d.pos.x.toFixed(1)},${d.pos.y.toFixed(1)} ${d.engaged ? 'E:' + d.engaged.role : d.state})`).join(' '));
}
console.log(sim.endReason, sim.result?.kind, sim.result?.yards);
