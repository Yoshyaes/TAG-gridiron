// Fictional league generation. Every team, name and player here is original.
import { Rng, clamp } from './rng';
import type { Player, Pos, Ratings, Team } from './types';

interface TeamSeed {
  city: string;
  name: string;
  abbr: string;
  primary: string;
  secondary: string;
  accent: string;
}

export const TEAM_SEEDS: TeamSeed[] = [
  { city: 'Austin', name: 'Outlaws', abbr: 'AUS', primary: '#B4531F', secondary: '#1E1E1E', accent: '#F2E3C6' },
  { city: 'Portland', name: 'Lumberjacks', abbr: 'POR', primary: '#1F5B3A', secondary: '#C9A227', accent: '#FFFFFF' },
  { city: 'San Antonio', name: 'Vaqueros', abbr: 'SAT', primary: '#3B2F6B', secondary: '#C0C0C0', accent: '#E4572E' },
  { city: 'Oklahoma City', name: 'Twisters', abbr: 'OKC', primary: '#0E4C92', secondary: '#F7B500', accent: '#FFFFFF' },
  { city: 'Salt Lake', name: 'Summit', abbr: 'SLC', primary: '#14213D', secondary: '#9AD1D4', accent: '#FFFFFF' },
  { city: 'Columbus', name: 'Ironclads', abbr: 'CLB', primary: '#5A5A5A', secondary: '#B22222', accent: '#FFFFFF' },
  { city: 'Sacramento', name: 'Goldrush', abbr: 'SAC', primary: '#D4A017', secondary: '#2B2B2B', accent: '#FFFFFF' },
  { city: 'Memphis', name: 'Kings', abbr: 'MEM', primary: '#4B2E83', secondary: '#E8B92F', accent: '#FFFFFF' },
  { city: 'Louisville', name: 'Thoroughbreds', abbr: 'LOU', primary: '#7A0019', secondary: '#E9E3D6', accent: '#000000' },
  { city: 'Birmingham', name: 'Forge', abbr: 'BHM', primary: '#C1440E', secondary: '#2F3E46', accent: '#FFFFFF' },
  { city: 'Albuquerque', name: 'Roadrunners', abbr: 'ABQ', primary: '#00827F', secondary: '#F4A259', accent: '#FFFFFF' },
  { city: 'Omaha', name: 'Stampede', abbr: 'OMA', primary: '#2E4057', secondary: '#D1495B', accent: '#FFFFFF' },
  { city: 'Honolulu', name: 'Waves', abbr: 'HNL', primary: '#0081A7', secondary: '#FED9B7', accent: '#F07167' },
  { city: 'Hartford', name: 'Whalers FC', abbr: 'HFD', primary: '#003F5C', secondary: '#7FB800', accent: '#FFFFFF' },
  { city: 'Raleigh', name: 'Oaks', abbr: 'RAL', primary: '#2D6A4F', secondary: '#B7472A', accent: '#FFFFFF' },
  { city: 'Richmond', name: 'Ironhorses', abbr: 'RIC', primary: '#22223B', secondary: '#C9ADA7', accent: '#F2E9E4' },
  { city: 'Boise', name: 'Bison', abbr: 'BOI', primary: '#6F1D1B', secondary: '#BB9457', accent: '#FFFFFF' },
  { city: 'Anchorage', name: 'Aurora', abbr: 'ANC', primary: '#0B3954', secondary: '#7DE2D1', accent: '#FFFFFF' },
  { city: 'Tucson', name: 'Scorpions', abbr: 'TUC', primary: '#9B2226', secondary: '#E9D8A6', accent: '#001219' },
  { city: 'El Paso', name: 'Sundogs', abbr: 'ELP', primary: '#EE9B00', secondary: '#005F73', accent: '#FFFFFF' },
  { city: 'Spokane', name: 'Timberwolves', abbr: 'SPO', primary: '#344E41', secondary: '#A3B18A', accent: '#FFFFFF' },
  { city: 'Milwaukee', name: 'Brewmasters', abbr: 'MKE', primary: '#7F5539', secondary: '#EDE0D4', accent: '#1B263B' },
  { city: 'St. Louis', name: 'Archers', abbr: 'STL', primary: '#1D3557', secondary: '#E63946', accent: '#F1FAEE' },
  { city: 'San Diego', name: 'Tritons', abbr: 'SDT', primary: '#023E8A', secondary: '#48CAE4', accent: '#FFD60A' },
  { city: 'Orlando', name: 'Comets', abbr: 'ORL', primary: '#5F0F40', secondary: '#FB8B24', accent: '#FFFFFF' },
  { city: 'Brooklyn', name: 'Bridges', abbr: 'BKN', primary: '#111111', secondary: '#FFFFFF', accent: '#E07A5F' },
  { city: 'Toronto', name: 'Huskies', abbr: 'TOR', primary: '#003049', secondary: '#D62828', accent: '#FCBF49' },
  { city: 'Mexico City', name: 'Aztecas', abbr: 'MEX', primary: '#006847', secondary: '#CE1126', accent: '#FFFFFF' },
  { city: 'Virginia Beach', name: 'Mariners', abbr: 'VAB', primary: '#264653', secondary: '#2A9D8F', accent: '#E9C46A' },
  { city: 'Des Moines', name: 'Cyclones FC', abbr: 'DSM', primary: '#540B0E', secondary: '#E09F3E', accent: '#FFFFFF' },
  { city: 'Charleston', name: 'Palmettos', abbr: 'CHS', primary: '#2B2D42', secondary: '#8D99AE', accent: '#EF233C' },
  { city: 'Fresno', name: 'Raptors', abbr: 'FRE', primary: '#3A0CA3', secondary: '#4CC9F0', accent: '#FFFFFF' },
];

const FIRST = ['Marcus', 'Jalen', 'Tyrell', 'Cole', 'Brady', 'DeShawn', 'Trevor', 'Andre', 'Isaiah', 'Caleb', 'Darius', 'Jordan',
  'Malik', 'Logan', 'Xavier', 'Kendrick', 'Hunter', 'Dante', 'Elijah', 'Grant', 'Jamal', 'Tanner', 'Rashad', 'Wyatt', 'Quinton',
  'Bryce', 'Terrell', 'Garrett', 'Devonte', 'Micah', 'Nolan', 'Keon', 'Zion', 'Luke', 'Cedric', 'Mason', 'Trey', 'Amari',
  'Colby', 'Desmond', 'Reggie', 'Jaxon', 'Omar', 'Victor', 'Silas', 'Damon', 'Kobe', 'Lamar', 'Ty', 'Ezra', 'Roman', 'Jace',
  'Brock', 'Kareem', 'Felix', 'Owen', 'Javon', 'Easton', 'Mateo', 'Nate', 'Rocco', 'Shane', 'Darnell', 'Emmett', 'Hollis'];
const LAST = ['Washington', 'Carter', 'Holloway', 'McBride', 'Stanton', 'Okafor', 'Reyes', 'Whitfield', 'Branson', 'Delgado',
  'Thibodeaux', 'Kowalski', 'Ellison', 'Pruitt', 'Vance', 'Abernathy', 'Marsh', 'Nwosu', 'Calloway', 'Fitzgerald', 'Gaines',
  'Hightower', 'Ingram', 'Jefferson', 'Kingsley', 'Langford', 'Mabry', 'Northcutt', 'Oduya', 'Pemberton', 'Quarles', 'Rutledge',
  'Sorensen', 'Tolliver', 'Underwood', 'Valdez', 'Whitaker', 'Yancey', 'Zeller', 'Ashford', 'Blackwell', 'Crenshaw', 'Dunbar',
  'Estrada', 'Faulkner', 'Gentry', 'Hargrove', 'Iverson', 'Jessup', 'Kincaid', 'Lockhart', 'Merriweather', 'Nakamura', 'Ochoa',
  'Pippen', 'Ramsey', 'Sutton', 'Treadwell', 'Upshaw', 'Vickers', 'Winslow', 'Yeager', 'Bartholomew', 'Castellanos', 'Driscoll'];

/** Position archetype: mean measurables + rating means. Unlisted ratings default to "low". */
interface Archetype {
  h: [number, number];
  w: [number, number];
  r: Partial<Ratings>;
}

const BASE_LOW = 35;

const ARCH: Record<Pos, Archetype> = {
  QB: { h: [75, 2], w: [222, 10], r: { speed: 68, accel: 72, agility: 70, strength: 60, awareness: 72, throwPower: 84, accShort: 78, accMid: 74, accDeep: 70, throwOnRun: 70, poise: 72, decision: 72, carrying: 62, elusiveness: 55, vision: 60, breakTackle: 45, catching: 40 } },
  RB: { h: [70.5, 1.5], w: [214, 10], r: { speed: 88, accel: 88, agility: 86, strength: 66, carrying: 80, breakTackle: 76, elusiveness: 80, vision: 76, catching: 64, catchInTraffic: 55, routeRunning: 58, release: 55, passBlock: 48, runBlock: 40, awareness: 65 } },
  FB: { h: [72, 1], w: [245, 8], r: { speed: 72, accel: 74, agility: 66, strength: 76, carrying: 72, breakTackle: 70, elusiveness: 50, vision: 58, catching: 60, routeRunning: 45, passBlock: 64, runBlock: 74, awareness: 65 } },
  WR: { h: [73, 2.2], w: [200, 12], r: { speed: 90, accel: 88, agility: 86, strength: 55, catching: 80, catchInTraffic: 72, routeRunning: 76, release: 74, carrying: 66, breakTackle: 50, elusiveness: 72, vision: 66, awareness: 66, runBlock: 42 } },
  TE: { h: [77, 1.2], w: [252, 8], r: { speed: 79, accel: 78, agility: 72, strength: 72, catching: 74, catchInTraffic: 72, routeRunning: 66, release: 62, carrying: 66, breakTackle: 66, elusiveness: 52, vision: 55, passBlock: 64, runBlock: 68, awareness: 66 } },
  OT: { h: [78, 1], w: [315, 10], r: { speed: 56, accel: 64, agility: 60, strength: 84, passBlock: 77, runBlock: 74, awareness: 70 } },
  OG: { h: [76.5, 1], w: [315, 10], r: { speed: 52, accel: 62, agility: 56, strength: 86, passBlock: 74, runBlock: 77, awareness: 70 } },
  C: { h: [75.5, 1], w: [305, 8], r: { speed: 52, accel: 62, agility: 58, strength: 84, passBlock: 75, runBlock: 75, awareness: 74 } },
  DE: { h: [76.5, 1.2], w: [268, 12], r: { speed: 78, accel: 82, agility: 74, strength: 78, passRush: 74, blockShed: 72, tackling: 72, pursuit: 74, playRec: 66, awareness: 66, ballSkills: 40 } },
  DT: { h: [75, 1], w: [310, 12], r: { speed: 62, accel: 70, agility: 62, strength: 88, passRush: 68, blockShed: 76, tackling: 72, pursuit: 66, playRec: 66, awareness: 66, ballSkills: 35 } },
  LB: { h: [74, 1], w: [238, 8], r: { speed: 80, accel: 80, agility: 74, strength: 72, passRush: 58, blockShed: 66, tackling: 78, pursuit: 80, playRec: 72, manCov: 56, zoneCov: 66, press: 45, ballSkills: 52, awareness: 70, catching: 45 } },
  CB: { h: [71.5, 1.4], w: [192, 7], r: { speed: 91, accel: 88, agility: 87, strength: 52, tackling: 62, pursuit: 76, playRec: 70, manCov: 76, zoneCov: 72, press: 68, ballSkills: 68, catching: 60, catchInTraffic: 52, awareness: 68, blockShed: 45 } },
  S: { h: [72.5, 1.2], w: [206, 8], r: { speed: 87, accel: 85, agility: 80, strength: 62, tackling: 74, pursuit: 82, playRec: 74, manCov: 64, zoneCov: 76, press: 50, ballSkills: 66, catching: 58, catchInTraffic: 52, awareness: 72, blockShed: 50 } },
  K: { h: [72, 1.5], w: [195, 10], r: { kickPower: 84, kickAcc: 82, speed: 50, accel: 55, agility: 50, strength: 40, awareness: 60 } },
  P: { h: [74, 1.5], w: [210, 10], r: { kickPower: 82, kickAcc: 78, speed: 50, accel: 55, agility: 50, strength: 40, awareness: 60 } },
};

const RATING_KEYS: (keyof Ratings)[] = [
  'speed', 'accel', 'agility', 'strength', 'stamina', 'awareness', 'carrying', 'breakTackle', 'elusiveness', 'vision',
  'catching', 'catchInTraffic', 'routeRunning', 'release', 'throwPower', 'accShort', 'accMid', 'accDeep', 'throwOnRun',
  'poise', 'decision', 'passBlock', 'runBlock', 'passRush', 'blockShed', 'tackling', 'pursuit', 'playRec', 'manCov',
  'zoneCov', 'press', 'ballSkills', 'kickPower', 'kickAcc',
];

/** Weights used to compute an overall rating per position. */
const OVR_WEIGHTS: Record<Pos, Partial<Record<keyof Ratings, number>>> = {
  QB: { throwPower: 2, accShort: 3, accMid: 3, accDeep: 2, decision: 3, poise: 2, throwOnRun: 1, awareness: 2, speed: 0.5 },
  RB: { speed: 3, accel: 2, agility: 2, carrying: 2, breakTackle: 2, elusiveness: 2, vision: 2, catching: 1 },
  FB: { runBlock: 3, strength: 2, passBlock: 1, carrying: 1, catching: 1, speed: 1 },
  WR: { speed: 3, catching: 3, routeRunning: 3, release: 2, catchInTraffic: 2, agility: 1, accel: 1 },
  TE: { catching: 2, routeRunning: 2, runBlock: 2, passBlock: 1, speed: 2, catchInTraffic: 1, strength: 1 },
  OT: { passBlock: 3, runBlock: 2, strength: 2, agility: 1, awareness: 1 },
  OG: { passBlock: 2, runBlock: 3, strength: 2, awareness: 1 },
  C: { passBlock: 2, runBlock: 2, strength: 2, awareness: 2 },
  DE: { passRush: 3, blockShed: 2, speed: 2, accel: 2, tackling: 1, strength: 1, pursuit: 1 },
  DT: { blockShed: 3, strength: 3, passRush: 2, tackling: 1, playRec: 1 },
  LB: { tackling: 3, pursuit: 2, playRec: 2, zoneCov: 2, speed: 2, blockShed: 1, manCov: 1 },
  CB: { manCov: 3, zoneCov: 2, speed: 3, press: 1, ballSkills: 2, agility: 1, playRec: 1 },
  S: { zoneCov: 3, speed: 2, tackling: 2, pursuit: 2, playRec: 2, manCov: 1, ballSkills: 1 },
  K: { kickPower: 2, kickAcc: 3 },
  P: { kickPower: 3, kickAcc: 2 },
};

export function computeOvr(pos: Pos, r: Ratings): number {
  const w = OVR_WEIGHTS[pos];
  let s = 0;
  let t = 0;
  for (const k in w) {
    const wt = w[k as keyof Ratings]!;
    s += r[k as keyof Ratings] * wt;
    t += wt;
  }
  // stretch so that an average starter lands ~75 and elite ~90+
  return Math.round(clamp(40 + (s / t - 50) * 1.25, 30, 99));
}

let nextPlayerId = 1;

export function makePlayer(rng: Rng, pos: Pos, talent: number, number: number): Player {
  const a = ARCH[pos];
  const r = {} as Ratings;
  // talent shifts all relevant skills; individual variation on top
  for (const k of RATING_KEYS) {
    const base = a.r[k];
    if (base === undefined) {
      r[k] = Math.round(clamp(rng.normal(BASE_LOW, 6), 20, 60));
    } else {
      r[k] = Math.round(clamp(base + talent + rng.normal(0, 5), 25, 99));
    }
  }
  r.stamina = Math.round(clamp(rng.normal(80, 7), 55, 99));
  const heightIn = Math.round(rng.normal(a.h[0], a.h[1]));
  const weightLb = Math.round(rng.normal(a.w[0], a.w[1]));
  // heavier players within a position are a bit slower/stronger: physics-consistent ratings
  const wz = (weightLb - a.w[0]) / Math.max(1, a.w[1]);
  r.speed = Math.round(clamp(r.speed - wz * 1.5, 25, 99));
  r.strength = Math.round(clamp(r.strength + wz * 2, 25, 99));
  const p: Player = {
    id: nextPlayerId++,
    first: rng.pick(FIRST),
    last: rng.pick(LAST),
    pos,
    number,
    age: Math.round(clamp(rng.normal(26, 3), 21, 37)),
    heightIn,
    weightLb,
    r,
    ovr: 0,
    fatigue: 0,
    injuredWeeks: 0,
  };
  p.ovr = computeOvr(pos, r);
  return p;
}

const ROSTER_COUNTS: [Pos, number][] = [
  ['QB', 3], ['RB', 3], ['FB', 1], ['WR', 6], ['TE', 3], ['OT', 4], ['OG', 4], ['C', 2],
  ['DE', 4], ['DT', 4], ['LB', 6], ['CB', 6], ['S', 4], ['K', 1], ['P', 1],
];

const NUMBER_RANGES: Record<Pos, [number, number][]> = {
  QB: [[1, 19]], RB: [[0, 49]], FB: [[30, 49]], WR: [[0, 19], [80, 89]], TE: [[80, 89], [40, 49]],
  OT: [[60, 79]], OG: [[60, 79]], C: [[50, 79]], DE: [[90, 99], [50, 59]], DT: [[90, 99], [70, 79]],
  LB: [[40, 59], [0, 9]], CB: [[20, 39], [0, 9]], S: [[20, 49], [0, 9]], K: [[1, 19]], P: [[1, 19]],
};

function makeTeam(rng: Rng, id: number, seed: TeamSeed, idx: number): Team {
  const roster: Player[] = [];
  const used = new Set<number>();
  const teamTalent = rng.normal(0, 3.5); // franchise-level talent spread
  for (const [pos, n] of ROSTER_COUNTS) {
    for (let i = 0; i < n; i++) {
      // starters better than backups
      const tier = i === 0 ? 3 : i === 1 ? 0 : -5;
      let num = -1;
      for (let tries = 0; tries < 50 && (num < 0 || used.has(num)); tries++) {
        const [lo, hi] = rng.pick(NUMBER_RANGES[pos]);
        num = rng.int(lo, hi);
      }
      used.add(num);
      roster.push(makePlayer(rng, pos, teamTalent + tier + rng.normal(0, 2), num));
    }
  }
  return {
    id,
    city: seed.city,
    name: seed.name,
    abbr: seed.abbr,
    colors: { primary: seed.primary, secondary: seed.secondary, accent: seed.accent },
    conference: idx < 16 ? 'North' : 'South',
    division: `${idx < 16 ? 'North' : 'South'} ${['East', 'West', 'Central', 'Mountain'][Math.floor((idx % 16) / 4)]}`,
    roster,
    passLean: clamp(rng.normal(0.55, 0.08), 0.35, 0.75),
    aggression: clamp(rng.normal(0.5, 0.18), 0.1, 0.95),
    blitzLean: clamp(rng.normal(0.28, 0.08), 0.1, 0.5),
  };
}

export function createLeague(seed = 2027): Team[] {
  const rng = new Rng(seed);
  return TEAM_SEEDS.map((s, i) => makeTeam(rng, i, s, i));
}

export function teamOvr(t: Team): { off: number; def: number; ovr: number } {
  const top = (pos: Pos, n: number) => t.roster.filter((p) => p.pos === pos).sort((a, b) => b.ovr - a.ovr).slice(0, n);
  const avg = (ps: Player[]) => ps.reduce((s, p) => s + p.ovr, 0) / Math.max(1, ps.length);
  const off = avg([...top('QB', 1), ...top('QB', 1), ...top('RB', 1), ...top('WR', 3), ...top('TE', 1), ...top('OT', 2), ...top('OG', 2), ...top('C', 1)]);
  const def = avg([...top('DE', 2), ...top('DT', 2), ...top('LB', 3), ...top('CB', 3), ...top('S', 2)]);
  return { off: Math.round(off), def: Math.round(def), ovr: Math.round((off + def) / 2) };
}
