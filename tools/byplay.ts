import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(5);
const type = process.argv[2] ?? 'run';
const n = Number(process.argv[3] ?? 150);
const plays = OFF_PLAYS.filter((p) => type === 'run' ? p.type === 'run' : p.type !== 'run' && p.type !== 'kneel' && p.type !== 'spike');
const defs = DEF_PLAYS.filter((d) => d.front !== 'goalline');
for (const op of plays) {
  const ys: number[] = []; let neg = 0, big = 0, sk = 0, comp = 0, att = 0, inc = 0, int = 0;
  for (let i = 0; i < n; i++) {
    const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: op, defCall: rng.pick(defs), los: 30, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork() });
    const r = sim.runToEnd();
    ys.push(r.yards); if (r.yards < 0) neg++; if (r.yards >= 20) big++; if (r.kind === 'sack') sk++;
    if (r.kind === 'pass') { att++; if (r.complete) comp++; if (r.incomplete) inc++; if (r.interception) int++; }
  }
  const m = ys.reduce((a, b) => a + b, 0) / n; ys.sort((a,b)=>a-b);
  console.log(`${op.id.padEnd(16)} mean ${m.toFixed(1).padStart(5)} med ${String(ys[n>>1]).padStart(3)} neg ${(100*neg/n).toFixed(0).padStart(3)}% 20+ ${(100*big/n).toFixed(0).padStart(3)}% sack ${(100*sk/n).toFixed(0).padStart(3)}%` + (att ? ` comp ${(100*comp/att).toFixed(0)}% int ${int}` : ''));
}
