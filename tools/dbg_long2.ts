import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(77);
let shown = 0;
for (let i = 0; i < 400 && shown < 3; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline' && d.coverage !== 'c0'));
  const o = rng.int(0,31), d = rng.int(0,31), fl = rng.chance(0.5), sd = rng.int(1,1e9);
  const mk = () => new PlaySim({ off: league[o], def: league[d], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: fl, weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: new Rng(sd) });
  if (mk().runToEnd().yards < 60) continue;
  shown++;
  const sim = mk();
  let printed = false;
  while (sim.phase === 'live') {
    sim.step();
    const c = sim.ball.holder;
    if (!printed && c && c.side === 'off' && c.pos.x > 37) {
      printed = true;
      console.log(`\n${op.name} vs ${dc.name} t=${sim.t.toFixed(2)} carrier (${c.pos.x.toFixed(1)},${c.pos.y.toFixed(1)}) v=${c.speed.toFixed(1)}`);
      for (const e of sim.def) console.log(`  ${e.role.padEnd(4)} ${e.state.padEnd(8)} (${e.pos.x.toFixed(1)},${e.pos.y.toFixed(1)}) d=${Math.hypot(e.pos.x-c.pos.x,e.pos.y-c.pos.y).toFixed(1)} eng=${e.engaged?.role ?? '-'}${e.engaged ? ' engT=' + e.engaged.engT.toFixed(1) : ''} stun=${e.stun.toFixed(1)} v=${e.speed.toFixed(1)}`);
    }
  }
  console.log(sim.events.map((e) => `${e.t.toFixed(1)}:${e.type}:${e.b?.role ?? ''}`).join(' '));
}
