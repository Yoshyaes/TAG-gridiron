// Verification-only: every OFF_PLAY x DEF_PLAY x {AI, user offense, user defense} with scripted UserInput,
// under reference math; prints result fields + exact (hex) final state of every entity and the ball.
// Usage (from repo root): npx tsx Unreal/TAGGridiron/Tools/Verify/scenario_dump.ts
import "./refmath_setup";
import { createLeague } from '../../../../src/sim/league';
import { OFF_PLAYS, DEF_PLAYS, FIELD_W } from '../../../../src/sim/playbook';
import { PlaySim } from '../../../../src/sim/play';
import { Rng } from '../../../../src/sim/rng';
const f64 = new Float64Array(1); const u64 = new BigUint64Array(f64.buffer);
const hx = (v: number) => { f64[0] = v; return u64[0].toString(16); };
const league = createLeague(2027);
const rng = new Rng(5);
const out: string[] = [];
for (const op of OFF_PLAYS) for (const dc of DEF_PLAYS) for (const mode of [0, 1, 2]) {
  const sim = new PlaySim({ off: league[1], def: league[2], offPlay: op, defCall: dc, los: rng.range(5, 95), ballY: rng.pick([FIELD_W / 2 - 3.08, FIELD_W / 2, FIELD_W / 2 + 3.08]), flip: rng.chance(0.5), weather: { windX: rng.range(-5, 5), windY: rng.range(-5, 5), precip: rng.pick(['none', 'rain', 'snow'] as const), tempF: 40 }, worldDir: 1, rng: rng.fork(), userSide: mode === 1 ? 'off' : mode === 2 ? 'def' : null, down: 3, toGo: 6 });
  let k = 0;
  while (sim.phase === 'live' && k < 2400) {
    const I = sim.input;
    if (mode === 1) {
      I.move = k < 30 ? { x: 0, y: 0 } : k < 90 ? { x: -0.3, y: 0.5 } : { x: 1, y: 0.2 };
      I.sprint = k >= 90; I.lob = (k % 2) === 0;
      if (k === 75) I.throwTo = 'X';
      if (k === 100 || k === 160) I.juke = true;
      if (k === 140) I.switchReq = true;
    } else if (mode === 2 && sim.user) {
      const u = sim.user; const dx = sim.ball.pos.x - u.pos.x, dy = sim.ball.pos.y - u.pos.y; const l = Math.hypot(dx, dy) || 1;
      I.move = { x: dx / l, y: dy / l }; I.sprint = true; I.dive = k % 20 === 0;
      if (k === 50 || k === 120) I.switchReq = true;
    }
    sim.step(); k++;
  }
  if (sim.phase === 'live') sim.endPlay('tackle');
  const r = sim.result!;
  let line = `${op.id} ${dc.id} ${mode} ${sim.endReason} ${r.kind} ${hx(r.endX)} ${r.yards} ${r.complete} ${r.interception} ${r.turnover} ${r.td} ${hx(r.duration)} ${r.tacklers.map(p => p.id).join(',')} ${r.passer?.id} ${r.rusher?.id} ${r.receiver?.id} ${r.target?.id} ${r.airYards} ${r.yac} ${r.brokenTackles} ${r.thrownAway} ${r.pressured} ${hx(r.timeToThrow)} ${r.targetSpotY === undefined ? 'u' : hx(r.targetSpotY)} ev:${r.events.map(e => e.type + '@' + Math.round(e.t * 60)).join(',')} user:${sim.user ? sim.user.role : '-'}`;
  for (const e of sim.ents) line += ` ${hx(e.pos.x)}/${hx(e.pos.y)}/${hx(e.face)}/${e.anim}/${e.state}`;
  line += ` ball:${sim.ball.mode}/${hx(sim.ball.pos.x)}/${hx(sim.ball.pos.y)}/${hx(sim.ball.pos.z)}`;
  out.push(line);
}
console.log(out.join('\n'));
