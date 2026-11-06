import { createLeague } from '../src/sim/league';
import { Game } from '../src/sim/game';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027);
const rng = new Rng(13);
const b: Record<string, { n: number; c: number; run: number; sack: number; inc: number; ta: number }> = {};
let pre = 0, specials = 0;
for (let g = 0; g < 30; g++) {
  const game = new Game(league[rng.int(0, 31)], league[rng.int(0, 31)], { seed: rng.int(1, 1e9) });
  const orig = game.finishPlay.bind(game);
  game.finishPlay = (sim) => {
    if (game.down === 3) {
      const tg = game.toGo; const r = sim.result!;
      const k = tg <= 2 ? '1-2' : tg <= 4 ? '3-4' : tg <= 6 ? '5-6' : tg <= 9 ? '7-9' : '10+';
      const x = (b[k] ??= { n: 0, c: 0, run: 0, sack: 0, inc: 0, ta: 0 });
      x.n++; if (r.endX - sim.los >= tg || r.td === 'off') x.c++; if (r.kind === 'run') x.run++; if (r.kind === 'sack') x.sack++; if (r.incomplete) x.inc++; if (r.thrownAway) x.ta++;
    }
    return orig(sim);
  };
  game.simToEnd();
}
for (const k of ['1-2', '3-4', '5-6', '7-9', '10+']) { const x = b[k]; if (!x) continue; console.log(`3rd&${k.padEnd(4)} n=${String(x.n).padStart(3)} conv ${(100*x.c/x.n).toFixed(0)}% run ${(100*x.run/x.n).toFixed(0)}% sack ${(100*x.sack/x.n).toFixed(0)}% inc ${(100*x.inc/x.n).toFixed(0)}% throwaway ${(100*x.ta/x.n).toFixed(0)}%`); }
console.log('NFL: 1-2 ~62%, 3-4 ~50%, 5-6 ~42%, 7-9 ~33%, 10+ ~20%');
