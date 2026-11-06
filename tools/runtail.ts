import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027); const rng = new Rng(17);
const ys: number[] = []; let brk = 0;
for (let i = 0; i < 1500; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const pool = DEF_PLAYS.filter((d) => d.front !== 'goalline'); const dc = rng.weighted(pool, pool.map((d) => (d.coverage === 'c0' ? 0.12 : 1)));
  const r = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 25, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() }).runToEnd();
  ys.push(r.yards); brk += r.brokenTackles;
}
const f = (k: number) => (100 * ys.filter((y) => y >= k).length / ys.length).toFixed(1);
console.log(`ypc ${(ys.reduce((a,b)=>a+b,0)/ys.length).toFixed(2)}  10+ ${f(10)}%  20+ ${f(20)}%  40+ ${f(40)}%  TD(75) ${f(75)}%  brk/carry ${(brk/ys.length).toFixed(2)}   NFL: 10+ ~12%, 20+ ~2.7%, 40+ ~0.5%`);
