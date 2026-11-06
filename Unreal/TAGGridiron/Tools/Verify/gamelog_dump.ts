// Verification-only: runs the calibrate.ts game sequence (league 2027, Rng(7)) under reference math
// and prints every PlayLog entry with exact (hex) clock / toGo / ballOn. Diffed against gamelog_dump.cpp.
// Usage (from repo root): npx tsx Unreal/TAGGridiron/Tools/Verify/gamelog_dump.ts [games]
import './refmath_setup';
import { createLeague } from '../../../../src/sim/league';
import { Game } from '../../../../src/sim/game';
import { Rng } from '../../../../src/sim/rng';

const f64 = new Float64Array(1);
const u64 = new BigUint64Array(f64.buffer);
const hx = (v: number) => { f64[0] = v; return u64[0].toString(16); };

const N = Number(process.argv[2] ?? 3);
const rng = new Rng(7);
const league = createLeague(2027);
const out: string[] = [];
for (let g = 0; g < N; g++) {
  const hi = rng.int(0, 31);
  let ai = rng.int(0, 31);
  if (ai === hi) ai = (ai + 1) % 32;
  const game = new Game(league[hi], league[ai], { seed: rng.int(1, 1e9) });
  game.simToEnd();
  out.push(`GAME ${g} ${game.away.abbr} ${game.score.away} ${game.home.abbr} ${game.score.home} log ${game.log.length}`);
  for (const l of game.log) out.push(`${l.q} ${hx(l.clock)} ${l.poss} ${l.down} ${hx(l.toGo)} ${hx(l.ballOn)} ${l.big ?? ''} ${l.score.home}-${l.score.away} | ${l.desc}`);
}
console.log(out.join('\n'));
