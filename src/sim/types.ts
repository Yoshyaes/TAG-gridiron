// Core data model: positions, ratings, players, teams.

export type Pos =
  | 'QB' | 'RB' | 'FB' | 'WR' | 'TE' | 'OT' | 'OG' | 'C'
  | 'DE' | 'DT' | 'LB' | 'CB' | 'S' | 'K' | 'P';

export const OFFENSE_POS: Pos[] = ['QB', 'RB', 'FB', 'WR', 'TE', 'OT', 'OG', 'C'];
export const DEFENSE_POS: Pos[] = ['DE', 'DT', 'LB', 'CB', 'S'];

/** All ratings are 25..99. Physical ones map to real-world measurables (see physics in play.ts). */
export interface Ratings {
  speed: number;        // top speed: 99 ~= 4.28 forty, 50 ~= 5.25 forty
  accel: number;        // how fast top speed is reached
  agility: number;      // lateral acceleration / change of direction
  strength: number;
  stamina: number;
  awareness: number;
  // ball carrier
  carrying: number;     // ball security
  breakTackle: number;
  elusiveness: number;
  vision: number;
  // receiving
  catching: number;
  catchInTraffic: number;
  routeRunning: number;
  release: number;
  // passing
  throwPower: number;
  accShort: number;
  accMid: number;
  accDeep: number;
  throwOnRun: number;
  poise: number;        // accuracy under pressure
  decision: number;     // read progression quality
  // blocking
  passBlock: number;
  runBlock: number;
  // defense
  passRush: number;
  blockShed: number;
  tackling: number;
  pursuit: number;
  playRec: number;
  manCov: number;
  zoneCov: number;
  press: number;
  ballSkills: number;
  // kicking
  kickPower: number;
  kickAcc: number;
}

export interface Player {
  id: number;
  first: string;
  last: string;
  pos: Pos;
  number: number;
  age: number;
  heightIn: number;
  weightLb: number;
  r: Ratings;
  ovr: number;
  /** 0 = fresh, 1 = exhausted. Persisted across plays within a game. */
  fatigue: number;
  injuredWeeks: number;
}

export interface TeamColors {
  primary: string;
  secondary: string;
  accent: string;
}

export interface Team {
  id: number;
  city: string;
  name: string;
  abbr: string;
  colors: TeamColors;
  conference: 'North' | 'South';
  division: string;
  roster: Player[];
  /** Offensive identity 0..1 (0 = run heavy, 1 = air raid). Drives CPU play calling. */
  passLean: number;
  /** Aggressiveness on 4th downs 0..1. */
  aggression: number;
  /** Defensive blitz tendency 0..1 */
  blitzLean: number;
}

export function depth(team: Team, pos: Pos): Player[] {
  return team.roster
    .filter((p) => p.pos === pos && p.injuredWeeks <= 0)
    .sort((a, b) => b.ovr - a.ovr);
}
