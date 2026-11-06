import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(4);
const toGo = Number(process.argv[2] ?? 6);
let conv = 0, n = 0, sacks = 0, inc = 0, comp = 0, shortC = 0;
const tgtDepth: number[] = [];
for (let i = 0; i < 500; i++) {
  const op = rng.pick(OFF_PLAYS.filter((p) => p.type === 'pass'));
  const dc = rng.pick(DEF_PLAYS.filter((d) => d.front === 'nickel' || d.front === 'dime'));
  const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: dc, los: 40, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork(), down: 3, toGo });
  const r = sim.runToEnd(); n++;
  if (r.yards >= toGo) conv++;
  if (r.kind === 'sack') sacks++;
  if (r.incomplete) inc++;
  if (r.complete) { comp++; if (r.yards < toGo) shortC++; }
  if (r.kind === 'pass' && !r.thrownAway) tgtDepth.push(r.airYards);
}
tgtDepth.sort((a,b)=>a-b);
console.log(`3rd & ${toGo}: conv ${(100*conv/n).toFixed(0)}% sacks ${(100*sacks/n).toFixed(0)}% inc ${(100*inc/n).toFixed(0)}% comp ${(100*comp/n).toFixed(0)}% (short of sticks ${(100*shortC/Math.max(1,comp)).toFixed(0)}%) median target depth ${tgtDepth[tgtDepth.length>>1]}`);
