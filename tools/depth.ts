import { createLeague } from '../src/sim/league';
import { Game } from '../src/sim/game';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(12);
const b: Record<string, { n: number; c: number; y: number; int: number }> = {};
let att = 0, adot = 0;
for (let g = 0; g < 25; g++) {
  const game = new Game(league[rng.int(0, 31)], league[rng.int(0, 31)], { seed: rng.int(1, 1e9) });
  const orig = game.finishPlay.bind(game);
  game.finishPlay = (sim) => {
    const r = sim.result!;
    if (r.kind === 'pass' && !r.thrownAway) {
      const a = r.airYards; att++; adot += a;
      const k = a < 0 ? 'behind' : a < 10 ? '0-9' : a < 20 ? '10-19' : '20+';
      const x = (b[k] ??= { n: 0, c: 0, y: 0, int: 0 }); x.n++; if (r.complete) { x.c++; x.y += r.yards; } if (r.interception) x.int++;
    }
    return orig(sim);
  };
  game.simToEnd();
}
console.log(`aDOT ${(adot / att).toFixed(1)} (NFL ~7.8)`);
for (const [k, x] of Object.entries(b)) console.log(`${k.padEnd(7)} share ${(100 * x.n / att).toFixed(0)}%  comp ${(100 * x.c / x.n).toFixed(0)}%  yds/att ${(x.y / x.n).toFixed(1)}  int ${(100 * x.int / x.n).toFixed(1)}%`);
console.log('NFL ref: behind ~16% comp 82%; 0-9 ~48% comp 74%; 10-19 ~24% comp 56%; 20+ ~12% comp 35% (yds/att ~13)');
