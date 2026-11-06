// Box score / team stats modal.
import type { Game } from '../sim/game';
import { passerRating } from '../sim/stats';

export function boxScoreHTML(g: Game): string {
  const A = g.away;
  const H = g.home;
  const ta = g.team.away;
  const th = g.team.home;
  const top = (s: number) => `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, '0')}`;
  const rows: [string, string, string][] = [
    ['Total yards', String(ta.yards), String(th.yards)],
    ['Plays', String(ta.plays), String(th.plays)],
    ['Yards / play', (ta.yards / Math.max(1, ta.plays)).toFixed(1), (th.yards / Math.max(1, th.plays)).toFixed(1)],
    ['Passing', `${ta.passComp}/${ta.passAtt}, ${ta.passYds}`, `${th.passComp}/${th.passAtt}, ${th.passYds}`],
    ['Rushing', `${ta.rushAtt}-${ta.rushYds}`, `${th.rushAtt}-${th.rushYds}`],
    ['First downs', String(ta.firstDowns), String(th.firstDowns)],
    ['3rd down', `${ta.thirdConv}/${ta.thirdAtt}`, `${th.thirdConv}/${th.thirdAtt}`],
    ['4th down', `${ta.fourthConv}/${ta.fourthAtt}`, `${th.fourthConv}/${th.fourthAtt}`],
    ['Red zone', `${ta.redZoneTD}/${ta.redZoneTrips}`, `${th.redZoneTD}/${th.redZoneTrips}`],
    ['Sacks allowed', String(ta.sacks), String(th.sacks)],
    ['Turnovers', String(ta.turnovers), String(th.turnovers)],
    ['Penalties', `${ta.penalties}-${ta.penYds}`, `${th.penalties}-${th.penYds}`],
    ['Time of poss.', top(ta.top), top(th.top)],
  ];
  const teamTable = `<table><tr><th></th><th>${A.abbr}</th><th>${H.abbr}</th></tr>${rows.map((r) => `<tr><td>${r[0]}</td><td>${r[1]}</td><td>${r[2]}</td></tr>`).join('')}</table>`;
  const ind = (abbr: string) => {
    const ls = g.stats.forTeam(abbr);
    const pass = ls.filter((l) => l.passAtt > 0).sort((a, b) => b.passAtt - a.passAtt);
    const rush = ls.filter((l) => l.rushAtt > 0).sort((a, b) => b.rushYds - a.rushYds);
    const rec = ls.filter((l) => l.targets > 0).sort((a, b) => b.recYds - a.recYds);
    const def = ls.filter((l) => l.tackles + l.sacks + l.ints + l.pbu > 0).sort((a, b) => b.tackles + b.sacks * 2 + b.ints * 3 - a.tackles - a.sacks * 2 - a.ints * 3).slice(0, 6);
    const kick = ls.filter((l) => l.fga + l.xpa + l.punts > 0);
    let h = `<h3>${abbr} Passing</h3><table><tr><th>Player</th><th>C/ATT</th><th>YDS</th><th>TD</th><th>INT</th><th>SCK</th><th>RTG</th></tr>`;
    h += pass.map((l) => `<tr><td>${l.name}</td><td>${l.passComp}/${l.passAtt}</td><td>${l.passYds}</td><td>${l.passTD}</td><td>${l.passInt}</td><td>${l.sacked}</td><td>${passerRating(l).toFixed(1)}</td></tr>`).join('');
    h += `</table><h3>Rushing</h3><table><tr><th>Player</th><th>CAR</th><th>YDS</th><th>AVG</th><th>TD</th><th>LNG</th></tr>`;
    h += rush.map((l) => `<tr><td>${l.name} <small>${l.pos}</small></td><td>${l.rushAtt}</td><td>${l.rushYds}</td><td>${(l.rushYds / l.rushAtt).toFixed(1)}</td><td>${l.rushTD}</td><td>${l.rushLong}</td></tr>`).join('');
    h += `</table><h3>Receiving</h3><table><tr><th>Player</th><th>REC/TGT</th><th>YDS</th><th>TD</th><th>LNG</th><th>YAC</th></tr>`;
    h += rec.map((l) => `<tr><td>${l.name} <small>${l.pos}</small></td><td>${l.rec}/${l.targets}</td><td>${l.recYds}</td><td>${l.recTD}</td><td>${l.recLong}</td><td>${l.yac}</td></tr>`).join('');
    h += `</table><h3>Defense</h3><table><tr><th>Player</th><th>TKL</th><th>TFL</th><th>SCK</th><th>INT</th><th>PD</th><th>FF</th></tr>`;
    h += def.map((l) => `<tr><td>${l.name} <small>${l.pos}</small></td><td>${l.tackles}</td><td>${l.tfl}</td><td>${l.sacks}</td><td>${l.ints}</td><td>${l.pbu}</td><td>${l.ff}</td></tr>`).join('');
    h += `</table><h3>Kicking</h3><table><tr><th>Player</th><th>FG</th><th>XP</th><th>PUNTS</th><th>AVG</th></tr>`;
    h += kick.map((l) => `<tr><td>${l.name}</td><td>${l.fgm}/${l.fga}</td><td>${l.xpm}/${l.xpa}</td><td>${l.punts}</td><td>${l.punts ? (l.puntYds / l.punts).toFixed(1) : '-'}</td></tr>`).join('');
    return h + '</table>';
  };
  return `<div class="final-score"><span style="color:${A.colors.primary === '#111111' ? '#ddd' : A.colors.primary}">${A.city} ${A.name} ${g.score.away}</span><span>${g.score.home} ${H.city} ${H.name}</span></div>
    ${teamTable}<div class="grid2"><div>${ind(A.abbr)}</div><div>${ind(H.abbr)}</div></div>`;
}

export function showBoxScore(ui: HTMLElement, g: Game, title: string, buttons: { label: string; fn: () => void }[]) {
  const m = document.createElement('div');
  m.className = 'modal';
  m.innerHTML = `<div class="panel"><div class="label">${title}</div>${boxScoreHTML(g)}<div class="row" style="justify-content:center;margin-top:16px" id="bxb"></div></div>`;
  ui.appendChild(m);
  const row = m.querySelector('#bxb')!;
  for (const b of buttons) {
    const btn = document.createElement('button');
    btn.className = 'btn slim primary';
    btn.textContent = b.label;
    btn.onclick = () => {
      m.remove();
      b.fn();
    };
    row.appendChild(btn);
  }
  return m;
}
