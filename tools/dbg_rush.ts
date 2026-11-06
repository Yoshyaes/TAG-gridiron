import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(19);
let n = 0, anyWin25 = 0, pressured = 0, sacks = 0, hotThrows = 0, engagedAt05 = 0, rushers = 0, unblockedAt05 = 0;
const firstWin: number[] = [];
for (let i = 0; i < 300; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'pass'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline' && d.blitz.length === 0));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  n++;
  let win = -1;
  const wasEng = new Set<any>();
  while (sim.phase === 'live') {
    sim.step();
    if (Math.abs(sim.t - 0.6) < 0.009) for (const d of sim.def) if (d.state === 'rush') { rushers++; if (d.engaged) engagedAt05++; else unblockedAt05++; }
    for (const d of sim.def) { if (d.engaged) wasEng.add(d); else if (wasEng.has(d) && d.state === 'rush' && win < 0 && !sim.passThrown) win = sim.t; }
  }
  if (win >= 0) firstWin.push(win);
  if (win >= 0 && win < 2.5) anyWin25++;
  if (sim.pressured) pressured++;
  if (sim.result!.kind === 'sack') sacks++;
}
firstWin.sort((a,b)=>a-b);
console.log(`plays ${n} rusher won <2.5s: ${(100*anyWin25/n).toFixed(0)}%  pressured ${(100*pressured/n).toFixed(0)}% sacks ${(100*sacks/n).toFixed(1)}%`);
console.log(`at t=0.6: rushers engaged ${(100*engagedAt05/rushers).toFixed(0)}% unblocked ${(100*unblockedAt05/rushers).toFixed(0)}%  median first win ${firstWin[firstWin.length>>1]?.toFixed(2)}`);
