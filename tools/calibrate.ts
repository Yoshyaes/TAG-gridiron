// Headless realism calibration: simulate many full games and compare to NFL per-team-game averages.
import { createLeague } from '../src/sim/league';
import { Game } from '../src/sim/game';
import { Rng } from '../src/sim/rng';
import { passerRating } from '../src/sim/stats';

const N = Number(process.argv[2] ?? 40);
const seed = Number(process.argv[3] ?? 7);
const rng = new Rng(seed);
const league = createLeague(2027);

// NFL 2023-2025 regular season, per team per game (approx.)
const NFL: Record<string, [number, number]> = {
  points: [22.0, 1.5],
  plays: [62.5, 2.0],
  yards: [330, 15],
  ydsPerPlay: [5.3, 0.25],
  passAtt: [33.5, 2],
  compPct: [64.5, 1.5],
  passYdsGross: [215, 12],
  ypa: [6.9, 0.35],
  rushAtt: [27, 2],
  ypc: [4.3, 0.2],
  sacks: [2.5, 0.4],
  sackRate: [6.8, 1],
  ints: [0.8, 0.2],
  fumblesLost: [0.45, 0.15],
  firstDowns: [19.5, 1.2],
  thirdPct: [39, 3],
  fgPct: [85, 4],
  punts: [4.0, 0.5],
  penalties: [6.0, 1],
  tds: [2.5, 0.3],
  explosive: [7.0, 1.5],
};

const acc: Record<string, number[]> = {};
const push = (k: string, v: number) => (acc[k] ??= []).push(v);
let ties = 0;
let ots = 0;
let homeWins = 0;
const t0 = Date.now();
const qbRatings: number[] = [];
const scores: number[] = [];

for (let g = 0; g < N; g++) {
  const hi = rng.int(0, 31);
  let ai = rng.int(0, 31);
  if (ai === hi) ai = (ai + 1) % 32;
  const game = new Game(league[hi], league[ai], { seed: rng.int(1, 1e9) });
  game.simToEnd();
  if (game.score.home === game.score.away) ties++;
  if (game.score.home > game.score.away) homeWins++;
  if (game.log.some((l) => l.desc.startsWith('Overtime'))) ots++;
  for (const k of ['home', 'away'] as const) {
    const T = game.team[k];
    const sacks = T.sacks;
    const dropbacks = T.passAtt + sacks;
    const passNet = T.passYds;
    const lines = game.stats.forTeam(game[k].abbr);
    const grossPass = lines.reduce((s, l) => s + l.passYds, 0);
    const tds = lines.reduce((s, l) => s + l.passTD + l.rushTD, 0);
    const fgm = lines.reduce((s, l) => s + l.fgm, 0);
    const fga = lines.reduce((s, l) => s + l.fga, 0);
    push('points', game.score[k]);
    scores.push(game.score[k]);
    push('plays', T.plays);
    push('yards', T.yards);
    push('ydsPerPlay', T.yards / Math.max(1, T.plays));
    push('passAtt', T.passAtt);
    push('compPct', (100 * T.passComp) / Math.max(1, T.passAtt));
    push('passYdsGross', grossPass);
    push('ypa', grossPass / Math.max(1, T.passAtt));
    push('rushAtt', T.rushAtt);
    push('ypc', T.rushYds / Math.max(1, T.rushAtt));
    push('sacks', sacks);
    push('sackRate', (100 * sacks) / Math.max(1, dropbacks));
    push('ints', T.ints);
    push('fumblesLost', T.fumblesLost);
    push('firstDowns', T.firstDowns);
    if (T.thirdAtt) push('thirdPct', (100 * T.thirdConv) / T.thirdAtt);
    if (fga) push('fgPct', (100 * fgm) / fga);
    push('punts', T.punts);
    push('penalties', T.penalties);
    push('tds', tds);
    push('explosive', T.explosive);
    void passNet;
    for (const l of lines) if (l.passAtt >= 15) qbRatings.push(passerRating(l));
  }
}

const mean = (a: number[]) => a.reduce((s, x) => s + x, 0) / Math.max(1, a.length);
console.log(`\nSimulated ${N} games in ${((Date.now() - t0) / 1000).toFixed(1)}s  (ties ${ties}, OT games ${ots}, home wins ${homeWins})`);
console.log('stat'.padEnd(14), 'SIM'.padStart(8), 'NFL'.padStart(8), '  verdict');
let off = 0;
for (const [k, [target, tol]] of Object.entries(NFL)) {
  const v = mean(acc[k] ?? []);
  const z = (v - target) / tol;
  const verdict = Math.abs(z) <= 1 ? 'ok' : Math.abs(z) <= 2 ? (z > 0 ? 'high' : 'low') : z > 0 ? 'HIGH!!' : 'LOW!!';
  if (Math.abs(z) > 1) off++;
  console.log(k.padEnd(14), v.toFixed(2).padStart(8), target.toFixed(2).padStart(8), '  ' + verdict);
}
console.log(`QB rating avg: ${mean(qbRatings).toFixed(1)} (NFL ~ 92)`);
const sd = Math.sqrt(mean(scores.map((s) => (s - mean(scores)) ** 2)));
console.log(`Points SD: ${sd.toFixed(1)} (NFL ~ 10)`);
console.log(`${off} stats outside tolerance`);
