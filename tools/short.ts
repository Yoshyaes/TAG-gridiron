import { createLeague } from '../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W, playById } from '../src/sim/playbook';
import { PlaySim } from '../src/sim/play';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(4);
for (const id of ['gl_sneak', 'gl_dive', 'i_power', 'i_iso', 'pistol_power', 'trips_iz']) {
  let conv = 0, n = 300;
  for (let i = 0; i < n; i++) {
    const dc = rng.pick(DEF_PLAYS.filter((d) => d.front === 'goalline' || d.front === 'base'));
    const sim = new PlaySim({ off: league[rng.int(0,31)], def: league[rng.int(0,31)], offPlay: playById(id), defCall: dc, los: 50, ballY: FIELD_W / 2, flip: rng.chance(0.5), weather: { windX: 0, windY: 0, precip: 'none', tempF: 65 }, worldDir: 1, rng: rng.fork(), down: 3, toGo: 1 });
    const r = sim.runToEnd(); if (r.endX - 50 >= 1) conv++;
  }
  console.log(`${id.padEnd(14)} 3rd&1 conv ${(100*conv/n).toFixed(0)}%`);
}
