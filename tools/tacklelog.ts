import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027); const rng = new Rng(17);
PlaySim.tackleLog = [];
for (let i = 0; i < 1500; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 25, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() }).runToEnd();
}
const L = PlaySim.tackleLog;
const grp = (name: string, f: (x: typeof L[0]) => boolean) => { const g = L.filter(f); console.log(`${name.padEnd(28)} n=${String(g.length).padStart(5)} meanP=${(g.reduce((a,b)=>a+b.p,0)/Math.max(1,g.length)).toFixed(2)}`); };
grp('all', () => true);
grp('dive', (x) => x.dive); grp('arm (engaged)', (x) => x.arm);
grp('head-on', (x) => !x.dive && !x.arm && x.facing > 0.3); grp('side', (x) => !x.dive && !x.arm && Math.abs(x.facing) <= 0.3); grp('behind', (x) => !x.dive && !x.arm && x.facing < -0.3);
grp('downfield >10 head-on', (x) => x.dx > 10 && !x.dive && x.facing > 0.3); grp('downfield >10 side', (x) => x.dx > 10 && !x.dive && Math.abs(x.facing) <= 0.3); grp('downfield >10 behind', (x) => x.dx > 10 && !x.dive && x.facing < -0.3);
grp('carrier fast >8', (x) => x.vc > 8 && !x.dive);
