import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(3);
for (let i = 0; i < 600; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const o = rng.int(0,31), d = rng.int(0,31), fl = rng.chance(0.5); const seedN = rng.int(1,1e9);
  const mk = () => new PlaySim({ off: league[o], def: league[d], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: fl, weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: new Rng(seedN) });
  const sim = mk();
  const r = sim.runToEnd();
  if (r.kind === 'sack' && r.duration < 0.3) {
    const s2 = mk();
    for (let k = 0; k < 14; k++) {
      s2.step();
      const qb = s2.qb;
      const near = s2.ents.filter(e => e !== qb).sort((a,b)=>Math.hypot(a.pos.x-qb.pos.x,a.pos.y-qb.pos.y)-Math.hypot(b.pos.x-qb.pos.x,b.pos.y-qb.pos.y)).slice(0,3);
      console.log(`t=${s2.t.toFixed(2)} ${s2.ball.mode} QB(${qb.pos.x.toFixed(2)},${qb.pos.y.toFixed(2)}) ` + near.map(e=>`${e.side}:${e.role}(${e.pos.x.toFixed(2)},${e.pos.y.toFixed(2)} ${e.state} eng=${e.engaged?.role??'-'})`).join(' '));
    }
    console.log(op.id, dc.id, s2.endReason, s2.events.map(e=>e.type));
    break;
  }
}
