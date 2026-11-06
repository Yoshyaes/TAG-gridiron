// Box score / player stat accumulation.
import type { Player } from './types';

export interface PlayerLine {
  pid: number;
  name: string;
  pos: string;
  team: string;
  passAtt: number; passComp: number; passYds: number; passTD: number; passInt: number; sacked: number; sackYds: number;
  rushAtt: number; rushYds: number; rushTD: number; rushLong: number; fumbles: number; fumblesLost: number;
  targets: number; rec: number; recYds: number; recTD: number; recLong: number; yac: number;
  tackles: number; sacks: number; ints: number; pbu: number; ff: number; fr: number; tfl: number; defTD: number;
  fgm: number; fga: number; xpm: number; xpa: number; punts: number; puntYds: number; brokenTackles: number;
}

export interface TeamLine {
  plays: number;
  yards: number;
  passYds: number;
  rushYds: number;
  rushAtt: number;
  passAtt: number;
  passComp: number;
  firstDowns: number;
  thirdAtt: number;
  thirdConv: number;
  fourthAtt: number;
  fourthConv: number;
  penalties: number;
  penYds: number;
  turnovers: number;
  sacks: number;
  top: number; // seconds
  punts: number;
  fga: number;
  fgm: number;
  redZoneTrips: number;
  redZoneTD: number;
  explosive: number;
  ints: number;
  fumblesLost: number;
}

export const emptyTeamLine = (): TeamLine => ({
  plays: 0, yards: 0, passYds: 0, rushYds: 0, rushAtt: 0, passAtt: 0, passComp: 0, firstDowns: 0,
  thirdAtt: 0, thirdConv: 0, fourthAtt: 0, fourthConv: 0, penalties: 0, penYds: 0, turnovers: 0, sacks: 0,
  top: 0, punts: 0, fga: 0, fgm: 0, redZoneTrips: 0, redZoneTD: 0, explosive: 0, ints: 0, fumblesLost: 0,
});

export class StatBook {
  lines = new Map<number, PlayerLine>();
  line(p: Player, team: string): PlayerLine {
    let l = this.lines.get(p.id);
    if (!l) {
      l = {
        pid: p.id, name: `${p.first[0]}. ${p.last}`, pos: p.pos, team,
        passAtt: 0, passComp: 0, passYds: 0, passTD: 0, passInt: 0, sacked: 0, sackYds: 0,
        rushAtt: 0, rushYds: 0, rushTD: 0, rushLong: 0, fumbles: 0, fumblesLost: 0,
        targets: 0, rec: 0, recYds: 0, recTD: 0, recLong: 0, yac: 0,
        tackles: 0, sacks: 0, ints: 0, pbu: 0, ff: 0, fr: 0, tfl: 0, defTD: 0,
        fgm: 0, fga: 0, xpm: 0, xpa: 0, punts: 0, puntYds: 0, brokenTackles: 0,
      };
      this.lines.set(p.id, l);
    }
    return l;
  }
  forTeam(abbr: string): PlayerLine[] {
    return [...this.lines.values()].filter((l) => l.team === abbr);
  }
}

export function passerRating(l: PlayerLine): number {
  if (!l.passAtt) return 0;
  const c = (a: number) => Math.max(0, Math.min(2.375, a));
  const a = c((l.passComp / l.passAtt - 0.3) * 5);
  const b = c((l.passYds / l.passAtt - 3) * 0.25);
  const cc = c((l.passTD / l.passAtt) * 20);
  const d = c(2.375 - (l.passInt / l.passAtt) * 25);
  return ((a + b + cc + d) / 6) * 100;
}
