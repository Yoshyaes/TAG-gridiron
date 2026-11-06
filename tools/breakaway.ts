import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027); const rng = new Rng(17);
let n20 = 0, n40 = 0; const ahead20: number[] = [], ahead40: number[] = []; const attemptsAfter: number[] = []; const closestAhead: number[] = [];
for (let i = 0; i < 3000; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'run' && p.id !== 'gl_sneak'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 25, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  let snap: { ahead: number; closest: number; t: number } | null = null;
  while (sim.phase === 'live') {
    sim.step();
    const c = sim.ball.holder;
    if (!snap && c && c.side === 'off' && c.pos.x > 45) {
      const free = sim.def.filter((d) => d.state !== 'down' && !d.engaged && d.stun <= 0);
      const ah = free.filter((d) => d.pos.x > c.pos.x - 1);
      snap = { ahead: ah.length, closest: Math.min(99, ...ah.map((d) => Math.hypot(d.pos.x - c.pos.x, d.pos.y - c.pos.y))), t: sim.t };
    }
  }
  const r = sim.result!;
  if (snap) {
    n20++;
    const after = r.events.filter((e) => (e.type === 'broken' || e.type === 'divemiss') && e.t > snap!.t).length;
    if (r.yards >= 40) { n40++; ahead40.push(snap.ahead); attemptsAfter.push(after); closestAhead.push(snap.closest); } else ahead20.push(snap.ahead);
  }
}
const m = (a: number[]) => (a.reduce((x, y) => x + y, 0) / Math.max(1, a.length)).toFixed(2);
console.log(`reached +20: ${n20}  went 40+: ${n40} (${(100*n40/Math.max(1,n20)).toFixed(0)}%)`);
console.log(`defenders ahead at +20: stopped<40 avg ${m(ahead20)} | 40+ avg ${m(ahead40)} ; 40+ runs: missed tackles after +20 avg ${m(attemptsAfter)}, closest defender ahead ${m(closestAhead)} yds`);
