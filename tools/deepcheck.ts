import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027); const rng = new Rng(17);
let shown = 0;
for (let i = 0; i < 3000 && shown < 4; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 25, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  let printed = false;
  const at10: string[] = [];
  while (sim.phase === 'live') {
    sim.step();
    const c = sim.ball.holder;
    if (c && c.side === 'off' && c.pos.x > 33 && !at10.length) for (const d of sim.def.filter((d) => d.deepRole)) at10.push(`${d.role}(${d.pos.x.toFixed(1)},${d.pos.y.toFixed(1)} ${d.state} eng=${d.engaged?.role ?? '-'} stun=${d.stun.toFixed(1)})`);
    if (!printed && c && c.side === 'off' && c.pos.x > 45) {
      printed = true; shown++;
      console.log(`\n${op.name} vs ${dc.name} carrier (${c.pos.x.toFixed(1)},${c.pos.y.toFixed(1)}) t=${sim.t.toFixed(2)}`);
      console.log('  at +8:', at10.join(' '));
      for (const d of sim.def.filter((d) => d.deepRole)) console.log(`  now ${d.role} (${d.pos.x.toFixed(1)},${d.pos.y.toFixed(1)}) ${d.state} eng=${d.engaged?.role ?? '-'} stun=${d.stun.toFixed(1)}`);
      console.log('  events:', sim.events.map((e) => `${e.t.toFixed(1)}:${e.type}:${e.b?.role ?? ''}`).join(' '));
    }
  }
}
