import { createLeague } from '../src/sim/league';
import { playById, defById, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const sim = new PlaySim({ off: league[4], def: league[6], offPlay: playById(process.argv[2] ?? 'ace_pa'), defCall: defById(process.argv[3] ?? 'base_c3'), los: 30, ballY: FIELD_W / 2, flip: false, weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: new Rng(Number(process.argv[4] ?? 3)) });
const show = ['QB','X','Z','TE','LCB','RCB','FS','SS','MLB','WLB','SLB'];
for (let k = 0; k < 600 && sim.phase === 'live'; k++) {
  sim.step();
  if (k % 18 === 0) console.log(`t=${sim.t.toFixed(1)} ball ${sim.ball.mode} (${sim.ball.pos.x.toFixed(1)},${sim.ball.pos.y.toFixed(1)},${sim.ball.pos.z.toFixed(1)}) | ` + sim.ents.filter(e => show.includes(e.role)).map(e => `${e.role}:${e.pos.x.toFixed(0)},${e.pos.y.toFixed(0)}${e.state==='zone'?'z':e.state==='man'?'m':''}`).join(' '));
}
console.log(sim.events.map((e) => `${e.t.toFixed(2)}:${e.type}:${e.a?.role ?? ''}${e.b ? '>' + e.b.role : ''}`).join(' '), sim.result?.yards);
for (const e of sim.def) console.log(e.role, e.state, e.zone, e.man?.role);
