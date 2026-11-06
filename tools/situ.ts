import { createLeague } from '../src/sim/league';
import { Game } from '../src/sim/game';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(8);
const third: number[] = []; let early = 0, earlySucc = 0, runE = 0, runS = 0, passE = 0, passS = 0;
for (let g = 0; g < 25; g++) {
  const game = new Game(league[rng.int(0, 31)], league[rng.int(0, 31)], { seed: rng.int(1, 1e9) });
  const orig = game.finishPlay.bind(game);
  game.finishPlay = (sim) => {
    const d = game.down, tg = game.toGo; const r = sim.result!;
    if (d === 3) third.push(tg);
    if (d <= 2 && r.kind !== 'kneel' && r.kind !== 'spike') {
      early++; const need = d === 1 ? 0.4 * tg : 0.6 * tg; const ok = r.yards >= need && !r.turnover;
      if (ok) earlySucc++;
      if (r.kind === 'run') { runE++; if (ok) runS++; } else { passE++; if (ok) passS++; }
    }
    return orig(sim);
  };
  game.simToEnd();
}
third.sort((a, b) => a - b);
const avg = third.reduce((a, b) => a + b, 0) / third.length;
console.log(`3rd down avg to go ${avg.toFixed(1)} (NFL ~7.3) median ${third[third.length >> 1]}  short(<=3) ${(100 * third.filter((x) => x <= 3).length / third.length).toFixed(0)}% long(>=7) ${(100 * third.filter((x) => x >= 7).length / third.length).toFixed(0)}%`);
console.log(`early-down success ${(100 * earlySucc / early).toFixed(0)}% (NFL ~47%)  run ${(100 * runS / runE).toFixed(0)}% pass ${(100 * passS / passE).toFixed(0)}%  passRate ${(100*passE/early).toFixed(0)}%`);
