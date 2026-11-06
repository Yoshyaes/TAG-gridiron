import { createLeague } from '../src/sim/league';
import { Game } from '../src/sim/game';
import { Rng } from '../src/sim/rng';
const league = createLeague(2027); const rng = new Rng(21);
let trips = 0, td = 0, fga = 0, drives = 0, gl = 0, glTD = 0;
const startPos: number[] = [];
for (let g = 0; g < 40; g++) {
  const game = new Game(league[rng.int(0, 31)], league[rng.int(0, 31)], { seed: rng.int(1, 1e9) });
  const orig = game.finishPlay.bind(game);
  game.finishPlay = (sim) => { if (game.ballOn >= 95 && game.down === 1) { gl++; } const r = orig(sim); return r; };
  game.simToEnd();
  for (const k of ['home', 'away'] as const) { trips += game.team[k].redZoneTrips; td += game.team[k].redZoneTD; fga += game.team[k].fga; }
  for (const l of game.log) if (/returned by|Touchback|fair catch|downed/.test(l.desc)) drives++;
}
console.log(`red zone trips/team-game ${(trips / 80).toFixed(2)} (NFL ~3.3)  RZ TD% ${(100 * td / trips).toFixed(0)}% (NFL ~56%)  FGA/team-game ${(fga / 80).toFixed(2)} (NFL ~1.8)`);
