import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(31);
const yac: number[] = []; const air: number[] = []; let brk = 0, firstMiss = 0;
let contactT: number[] = [];
for (let i = 0; i < 800; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'pass' || p.type === 'playaction' || p.type === 'screen'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front !== 'goalline'));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
  const r = sim.runToEnd();
  if (r.complete) {
    yac.push(r.yac); air.push(r.airYards); brk += r.brokenTackles;
    const c = sim.events.find((e) => e.type === 'catch')!; const t = sim.events.find((e) => (e.type === 'broken' || e.type === 'divemiss') && e.t > c.t);
    contactT.push(r.duration - c.t);
  }
}
const s = [...yac].sort((a,b)=>a-b); const m = (a: number[]) => a.reduce((x,y)=>x+y,0)/a.length;
console.log(`comps ${yac.length} yac mean ${m(yac).toFixed(1)} median ${s[s.length>>1]} p90 ${s[Math.floor(s.length*0.9)]} >=20: ${(100*yac.filter(x=>x>=20).length/yac.length).toFixed(1)}%  air mean ${m(air).toFixed(1)} brk/comp ${(brk/yac.length).toFixed(2)} time catch->down ${m(contactT).toFixed(2)}s`);
