// Port of src/sim/league.ts
// Fictional league generation. Every team, name and player here is original.
// RNG call order matches the TS exactly, so createLeague(2027) yields the same league (names, measurables; the
// franchise talent spread is half the TS one, so some ratings differ by a point or two).
#include "League.h"
#include "Roster.h"

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <set>

#include "Abilities.h"
#include "Playbook.h"
#include <utility>

namespace tag {

namespace {

using RMember = double Ratings::*;

const std::vector<TeamSeed> kTeamSeeds = {
    {"Austin", "Outlaws", "AUS", "#B4531F", "#1E1E1E", "#F2E3C6"},
    {"Portland", "Lumberjacks", "POR", "#1F5B3A", "#C9A227", "#FFFFFF"},
    {"San Antonio", "Vaqueros", "SAT", "#3B2F6B", "#C0C0C0", "#E4572E"},
    {"Oklahoma City", "Twisters", "OKC", "#0E4C92", "#F7B500", "#FFFFFF"},
    {"Salt Lake", "Summit", "SLC", "#14213D", "#9AD1D4", "#FFFFFF"},
    {"Columbus", "Ironclads", "CLB", "#5A5A5A", "#B22222", "#FFFFFF"},
    {"Sacramento", "Goldrush", "SAC", "#D4A017", "#2B2B2B", "#FFFFFF"},
    {"Memphis", "Kings", "MEM", "#4B2E83", "#E8B92F", "#FFFFFF"},
    {"Louisville", "Thoroughbreds", "LOU", "#7A0019", "#E9E3D6", "#000000"},
    {"Birmingham", "Forge", "BHM", "#C1440E", "#2F3E46", "#FFFFFF"},
    {"Albuquerque", "Roadrunners", "ABQ", "#00827F", "#F4A259", "#FFFFFF"},
    {"Omaha", "Stampede", "OMA", "#2E4057", "#D1495B", "#FFFFFF"},
    {"Honolulu", "Waves", "HNL", "#0081A7", "#FED9B7", "#F07167"},
    {"Hartford", "Kestrels", "HFD", "#003F5C", "#7FB800", "#FFFFFF"},
    {"Raleigh", "Oaks", "RAL", "#2D6A4F", "#B7472A", "#FFFFFF"},
    {"Richmond", "Ironhorses", "RIC", "#22223B", "#C9ADA7", "#F2E9E4"},
    {"Boise", "Bison", "BOI", "#6F1D1B", "#BB9457", "#FFFFFF"},
    {"Anchorage", "Aurora", "ANC", "#0B3954", "#7DE2D1", "#FFFFFF"},
    {"Tucson", "Scorpions", "TUC", "#9B2226", "#E9D8A6", "#001219"},
    {"El Paso", "Sundogs", "ELP", "#EE9B00", "#005F73", "#FFFFFF"},
    {"Spokane", "Timberwolves", "SPO", "#344E41", "#A3B18A", "#FFFFFF"},
    {"Milwaukee", "Brewmasters", "MKE", "#7F5539", "#EDE0D4", "#1B263B"},
    {"St. Louis", "Archers", "STL", "#1D3557", "#E63946", "#F1FAEE"},
    {"San Diego", "Tritons", "SDT", "#023E8A", "#48CAE4", "#FFD60A"},
    {"Orlando", "Comets", "ORL", "#5F0F40", "#FB8B24", "#FFFFFF"},
    {"Brooklyn", "Bridges", "BKN", "#111111", "#FFFFFF", "#E07A5F"},
    {"Toronto", "Huskies", "TOR", "#003049", "#D62828", "#FCBF49"},
    {"Mexico City", "Aztecas", "MEX", "#006847", "#CE1126", "#FFFFFF"},
    {"Virginia Beach", "Mariners", "VAB", "#264653", "#2A9D8F", "#E9C46A"},
    {"Des Moines", "Cyclones FC", "DSM", "#540B0E", "#E09F3E", "#FFFFFF"},
    {"Charleston", "Palmettos", "CHS", "#2B2D42", "#8D99AE", "#EF233C"},
    {"Fresno", "Raptors", "FRE", "#3A0CA3", "#4CC9F0", "#FFFFFF"},
};

const std::vector<std::string> FIRST = {
    "Marcus", "Jalen", "Tyrell", "Cole", "Brady", "DeShawn", "Trevor", "Andre", "Isaiah", "Caleb", "Darius", "Jordan",
    "Malik", "Logan", "Xavier", "Kendrick", "Hunter", "Dante", "Elijah", "Grant", "Jamal", "Tanner", "Rashad", "Wyatt", "Quinton",
    "Bryce", "Terrell", "Garrett", "Devonte", "Micah", "Nolan", "Keon", "Zion", "Luke", "Cedric", "Mason", "Trey", "Amari",
    "Colby", "Desmond", "Reggie", "Jaxon", "Omar", "Victor", "Silas", "Damon", "Kobe", "Lamar", "Ty", "Ezra", "Roman", "Jace",
    "Brock", "Kareem", "Felix", "Owen", "Javon", "Easton", "Mateo", "Nate", "Rocco", "Shane", "Darnell", "Emmett", "Hollis"};
const std::vector<std::string> LAST = {
    "Washington", "Carter", "Holloway", "McBride", "Stanton", "Okafor", "Reyes", "Whitfield", "Branson", "Delgado",
    "Thibodeaux", "Kowalski", "Ellison", "Pruitt", "Vance", "Abernathy", "Marsh", "Nwosu", "Calloway", "Fitzgerald", "Gaines",
    "Hightower", "Ingram", "Jefferson", "Kingsley", "Langford", "Mabry", "Northcutt", "Oduya", "Pemberton", "Quarles", "Rutledge",
    "Sorensen", "Tolliver", "Underwood", "Valdez", "Whitaker", "Yancey", "Zeller", "Ashford", "Blackwell", "Crenshaw", "Dunbar",
    "Estrada", "Faulkner", "Gentry", "Hargrove", "Iverson", "Jessup", "Kincaid", "Lockhart", "Merriweather", "Nakamura", "Ochoa",
    "Pippen", "Ramsey", "Sutton", "Treadwell", "Upshaw", "Vickers", "Winslow", "Yeager", "Bartholomew", "Castellanos", "Driscoll"};

/** Position archetype: mean measurables + rating means. Unlisted ratings default to "low". */
struct Archetype {
  double h[2];
  double w[2];
  std::vector<std::pair<RMember, double>> r;
};

constexpr double BASE_LOW = 35;

const Archetype& arch(Pos pos) {
  static const Archetype QB{{75, 2}, {222, 10}, {{&Ratings::speed, 68}, {&Ratings::accel, 72}, {&Ratings::agility, 70}, {&Ratings::strength, 60}, {&Ratings::awareness, 72}, {&Ratings::throwPower, 84}, {&Ratings::accShort, 78}, {&Ratings::accMid, 74}, {&Ratings::accDeep, 70}, {&Ratings::throwOnRun, 70}, {&Ratings::poise, 72}, {&Ratings::decision, 72}, {&Ratings::carrying, 62}, {&Ratings::elusiveness, 55}, {&Ratings::vision, 60}, {&Ratings::breakTackle, 45}, {&Ratings::catching, 40}}};
  static const Archetype RB{{70.5, 1.5}, {214, 10}, {{&Ratings::speed, 88}, {&Ratings::accel, 88}, {&Ratings::agility, 86}, {&Ratings::strength, 66}, {&Ratings::carrying, 80}, {&Ratings::breakTackle, 76}, {&Ratings::elusiveness, 80}, {&Ratings::vision, 76}, {&Ratings::catching, 64}, {&Ratings::catchInTraffic, 55}, {&Ratings::routeRunning, 58}, {&Ratings::release, 55}, {&Ratings::passBlock, 48}, {&Ratings::runBlock, 40}, {&Ratings::awareness, 65}}};
  static const Archetype FB{{72, 1}, {245, 8}, {{&Ratings::speed, 72}, {&Ratings::accel, 74}, {&Ratings::agility, 66}, {&Ratings::strength, 76}, {&Ratings::carrying, 72}, {&Ratings::breakTackle, 70}, {&Ratings::elusiveness, 50}, {&Ratings::vision, 58}, {&Ratings::catching, 60}, {&Ratings::routeRunning, 45}, {&Ratings::passBlock, 64}, {&Ratings::runBlock, 74}, {&Ratings::awareness, 65}}};
  static const Archetype WR{{73, 2.2}, {200, 12}, {{&Ratings::speed, 90}, {&Ratings::accel, 88}, {&Ratings::agility, 86}, {&Ratings::strength, 55}, {&Ratings::catching, 80}, {&Ratings::catchInTraffic, 72}, {&Ratings::routeRunning, 76}, {&Ratings::release, 74}, {&Ratings::carrying, 66}, {&Ratings::breakTackle, 50}, {&Ratings::elusiveness, 72}, {&Ratings::vision, 66}, {&Ratings::awareness, 66}, {&Ratings::runBlock, 42}}};
  static const Archetype TE{{77, 1.2}, {252, 8}, {{&Ratings::speed, 79}, {&Ratings::accel, 78}, {&Ratings::agility, 72}, {&Ratings::strength, 72}, {&Ratings::catching, 74}, {&Ratings::catchInTraffic, 72}, {&Ratings::routeRunning, 66}, {&Ratings::release, 62}, {&Ratings::carrying, 66}, {&Ratings::breakTackle, 66}, {&Ratings::elusiveness, 52}, {&Ratings::vision, 55}, {&Ratings::passBlock, 64}, {&Ratings::runBlock, 68}, {&Ratings::awareness, 66}}};
  static const Archetype OT{{78, 1}, {315, 10}, {{&Ratings::speed, 56}, {&Ratings::accel, 64}, {&Ratings::agility, 60}, {&Ratings::strength, 84}, {&Ratings::passBlock, 77}, {&Ratings::runBlock, 74}, {&Ratings::awareness, 70}}};
  static const Archetype OG{{76.5, 1}, {315, 10}, {{&Ratings::speed, 52}, {&Ratings::accel, 62}, {&Ratings::agility, 56}, {&Ratings::strength, 86}, {&Ratings::passBlock, 74}, {&Ratings::runBlock, 77}, {&Ratings::awareness, 70}}};
  static const Archetype C{{75.5, 1}, {305, 8}, {{&Ratings::speed, 52}, {&Ratings::accel, 62}, {&Ratings::agility, 58}, {&Ratings::strength, 84}, {&Ratings::passBlock, 75}, {&Ratings::runBlock, 75}, {&Ratings::awareness, 74}}};
  static const Archetype DE{{76.5, 1.2}, {268, 12}, {{&Ratings::speed, 78}, {&Ratings::accel, 82}, {&Ratings::agility, 74}, {&Ratings::strength, 78}, {&Ratings::passRush, 74}, {&Ratings::blockShed, 72}, {&Ratings::tackling, 72}, {&Ratings::pursuit, 74}, {&Ratings::playRec, 66}, {&Ratings::awareness, 66}, {&Ratings::ballSkills, 40}}};
  static const Archetype DT{{75, 1}, {310, 12}, {{&Ratings::speed, 62}, {&Ratings::accel, 70}, {&Ratings::agility, 62}, {&Ratings::strength, 88}, {&Ratings::passRush, 68}, {&Ratings::blockShed, 76}, {&Ratings::tackling, 72}, {&Ratings::pursuit, 66}, {&Ratings::playRec, 66}, {&Ratings::awareness, 66}, {&Ratings::ballSkills, 35}}};
  static const Archetype LB{{74, 1}, {238, 8}, {{&Ratings::speed, 80}, {&Ratings::accel, 80}, {&Ratings::agility, 74}, {&Ratings::strength, 72}, {&Ratings::passRush, 58}, {&Ratings::blockShed, 66}, {&Ratings::tackling, 78}, {&Ratings::pursuit, 80}, {&Ratings::playRec, 72}, {&Ratings::manCov, 56}, {&Ratings::zoneCov, 66}, {&Ratings::press, 45}, {&Ratings::ballSkills, 52}, {&Ratings::awareness, 70}, {&Ratings::catching, 45}}};
  static const Archetype CB{{71.5, 1.4}, {192, 7}, {{&Ratings::speed, 91}, {&Ratings::accel, 88}, {&Ratings::agility, 87}, {&Ratings::strength, 52}, {&Ratings::tackling, 62}, {&Ratings::pursuit, 76}, {&Ratings::playRec, 70}, {&Ratings::manCov, 76}, {&Ratings::zoneCov, 72}, {&Ratings::press, 68}, {&Ratings::ballSkills, 68}, {&Ratings::catching, 60}, {&Ratings::catchInTraffic, 52}, {&Ratings::awareness, 68}, {&Ratings::blockShed, 45}}};
  static const Archetype S{{72.5, 1.2}, {206, 8}, {{&Ratings::speed, 87}, {&Ratings::accel, 85}, {&Ratings::agility, 80}, {&Ratings::strength, 62}, {&Ratings::tackling, 74}, {&Ratings::pursuit, 82}, {&Ratings::playRec, 74}, {&Ratings::manCov, 64}, {&Ratings::zoneCov, 76}, {&Ratings::press, 50}, {&Ratings::ballSkills, 66}, {&Ratings::catching, 58}, {&Ratings::catchInTraffic, 52}, {&Ratings::awareness, 72}, {&Ratings::blockShed, 50}}};
  static const Archetype K{{72, 1.5}, {195, 10}, {{&Ratings::kickPower, 84}, {&Ratings::kickAcc, 82}, {&Ratings::speed, 50}, {&Ratings::accel, 55}, {&Ratings::agility, 50}, {&Ratings::strength, 40}, {&Ratings::awareness, 60}}};
  static const Archetype P{{74, 1.5}, {210, 10}, {{&Ratings::kickPower, 82}, {&Ratings::kickAcc, 78}, {&Ratings::speed, 50}, {&Ratings::accel, 55}, {&Ratings::agility, 50}, {&Ratings::strength, 40}, {&Ratings::awareness, 60}}};
  switch (pos) {
    case Pos::QB: return QB;
    case Pos::RB: return RB;
    case Pos::FB: return FB;
    case Pos::WR: return WR;
    case Pos::TE: return TE;
    case Pos::OT: return OT;
    case Pos::OG: return OG;
    case Pos::C: return C;
    case Pos::DE: return DE;
    case Pos::DT: return DT;
    case Pos::LB: return LB;
    case Pos::CB: return CB;
    case Pos::S: return S;
    case Pos::K: return K;
    case Pos::P: return P;
  }
  return QB;
}

const RMember RATING_KEYS[] = {
    &Ratings::speed, &Ratings::accel, &Ratings::agility, &Ratings::strength, &Ratings::stamina, &Ratings::awareness,
    &Ratings::carrying, &Ratings::breakTackle, &Ratings::elusiveness, &Ratings::vision, &Ratings::catching,
    &Ratings::catchInTraffic, &Ratings::routeRunning, &Ratings::release, &Ratings::throwPower, &Ratings::accShort,
    &Ratings::accMid, &Ratings::accDeep, &Ratings::throwOnRun, &Ratings::poise, &Ratings::decision, &Ratings::passBlock,
    &Ratings::runBlock, &Ratings::passRush, &Ratings::blockShed, &Ratings::tackling, &Ratings::pursuit, &Ratings::playRec,
    &Ratings::manCov, &Ratings::zoneCov, &Ratings::press, &Ratings::ballSkills, &Ratings::kickPower, &Ratings::kickAcc,
};

using Weights = std::vector<std::pair<RMember, double>>;

/** Weights used to compute an overall rating per position (same key order as the TS literals). */
const Weights& ovrWeights(Pos pos) {
  static const Weights QB{{&Ratings::throwPower, 2}, {&Ratings::accShort, 3}, {&Ratings::accMid, 3}, {&Ratings::accDeep, 2}, {&Ratings::decision, 3}, {&Ratings::poise, 2}, {&Ratings::throwOnRun, 1}, {&Ratings::awareness, 2}, {&Ratings::speed, 0.5}};
  static const Weights RB{{&Ratings::speed, 3}, {&Ratings::accel, 2}, {&Ratings::agility, 2}, {&Ratings::carrying, 2}, {&Ratings::breakTackle, 2}, {&Ratings::elusiveness, 2}, {&Ratings::vision, 2}, {&Ratings::catching, 1}};
  static const Weights FB{{&Ratings::runBlock, 3}, {&Ratings::strength, 2}, {&Ratings::passBlock, 1}, {&Ratings::carrying, 1}, {&Ratings::catching, 1}, {&Ratings::speed, 1}};
  static const Weights WR{{&Ratings::speed, 3}, {&Ratings::catching, 3}, {&Ratings::routeRunning, 3}, {&Ratings::release, 2}, {&Ratings::catchInTraffic, 2}, {&Ratings::agility, 1}, {&Ratings::accel, 1}};
  static const Weights TE{{&Ratings::catching, 2}, {&Ratings::routeRunning, 2}, {&Ratings::runBlock, 2}, {&Ratings::passBlock, 1}, {&Ratings::speed, 2}, {&Ratings::catchInTraffic, 1}, {&Ratings::strength, 1}};
  static const Weights OT{{&Ratings::passBlock, 3}, {&Ratings::runBlock, 2}, {&Ratings::strength, 2}, {&Ratings::agility, 1}, {&Ratings::awareness, 1}};
  static const Weights OG{{&Ratings::passBlock, 2}, {&Ratings::runBlock, 3}, {&Ratings::strength, 2}, {&Ratings::awareness, 1}};
  static const Weights C{{&Ratings::passBlock, 2}, {&Ratings::runBlock, 2}, {&Ratings::strength, 2}, {&Ratings::awareness, 2}};
  static const Weights DE{{&Ratings::passRush, 3}, {&Ratings::blockShed, 2}, {&Ratings::speed, 2}, {&Ratings::accel, 2}, {&Ratings::tackling, 1}, {&Ratings::strength, 1}, {&Ratings::pursuit, 1}};
  static const Weights DT{{&Ratings::blockShed, 3}, {&Ratings::strength, 3}, {&Ratings::passRush, 2}, {&Ratings::tackling, 1}, {&Ratings::playRec, 1}};
  static const Weights LB{{&Ratings::tackling, 3}, {&Ratings::pursuit, 2}, {&Ratings::playRec, 2}, {&Ratings::zoneCov, 2}, {&Ratings::speed, 2}, {&Ratings::blockShed, 1}, {&Ratings::manCov, 1}};
  static const Weights CB{{&Ratings::manCov, 3}, {&Ratings::zoneCov, 2}, {&Ratings::speed, 3}, {&Ratings::press, 1}, {&Ratings::ballSkills, 2}, {&Ratings::agility, 1}, {&Ratings::playRec, 1}};
  static const Weights S{{&Ratings::zoneCov, 3}, {&Ratings::speed, 2}, {&Ratings::tackling, 2}, {&Ratings::pursuit, 2}, {&Ratings::playRec, 2}, {&Ratings::manCov, 1}, {&Ratings::ballSkills, 1}};
  static const Weights K{{&Ratings::kickPower, 2}, {&Ratings::kickAcc, 3}};
  static const Weights P{{&Ratings::kickPower, 3}, {&Ratings::kickAcc, 2}};
  switch (pos) {
    case Pos::QB: return QB;
    case Pos::RB: return RB;
    case Pos::FB: return FB;
    case Pos::WR: return WR;
    case Pos::TE: return TE;
    case Pos::OT: return OT;
    case Pos::OG: return OG;
    case Pos::C: return C;
    case Pos::DE: return DE;
    case Pos::DT: return DT;
    case Pos::LB: return LB;
    case Pos::CB: return CB;
    case Pos::S: return S;
    case Pos::K: return K;
    case Pos::P: return P;
  }
  return QB;
}

int nextPlayerId = 1;

const std::vector<std::pair<Pos, int>> ROSTER_COUNTS = {
    {Pos::QB, 3}, {Pos::RB, 3}, {Pos::FB, 1}, {Pos::WR, 6}, {Pos::TE, 3}, {Pos::OT, 4}, {Pos::OG, 4}, {Pos::C, 2},
    {Pos::DE, 4}, {Pos::DT, 4}, {Pos::LB, 6}, {Pos::CB, 6}, {Pos::S, 4}, {Pos::K, 1}, {Pos::P, 1},
};

using Ranges = std::vector<std::pair<int, int>>;
const Ranges& numberRanges(Pos pos) {
  static const Ranges QB{{1, 19}}, RB{{0, 49}}, FB{{30, 49}}, WR{{0, 19}, {80, 89}}, TE{{80, 89}, {40, 49}},
      OT{{60, 79}}, OG{{60, 79}}, C{{50, 79}}, DE{{90, 99}, {50, 59}}, DT{{90, 99}, {70, 79}},
      LB{{40, 59}, {0, 9}}, CB{{20, 39}, {0, 9}}, S{{20, 49}, {0, 9}}, K{{1, 19}}, P{{1, 19}};
  switch (pos) {
    case Pos::QB: return QB;
    case Pos::RB: return RB;
    case Pos::FB: return FB;
    case Pos::WR: return WR;
    case Pos::TE: return TE;
    case Pos::OT: return OT;
    case Pos::OG: return OG;
    case Pos::C: return C;
    case Pos::DE: return DE;
    case Pos::DT: return DT;
    case Pos::LB: return LB;
    case Pos::CB: return CB;
    case Pos::S: return S;
    case Pos::K: return K;
    case Pos::P: return P;
  }
  return QB;
}

std::unique_ptr<Team> makeTeam(Rng& rng, int id, const TeamSeed& seed, int idx) {
  auto team = std::make_unique<Team>();
  std::set<int> used;
  // franchise-level talent spread: team overalls 72-77. At 2.4 (overalls 69-79) the best team won by 35 a game and the
  // worst lost by 18 - point differentials twice the NFL's (+12 to -10) and a score spread of 12 points against ~10
  // (the sim turns a rating gap into points faster than a Sunday does). TAG_TEAM_SPREAD scales it for calibration.
  static const double kSpread = std::getenv("TAG_TEAM_SPREAD") ? std::atof(std::getenv("TAG_TEAM_SPREAD")) : 1.0;
  const double teamTalent = rng.normal(0, 1.2) * kSpread;
  for (const auto& pc : ROSTER_COUNTS) {
    const Pos pos = pc.first;
    for (int i = 0; i < pc.second; i++) {
      // starters better than backups
      const double tier = i == 0 ? 3 : i == 1 ? 0 : -5;
      int num = -1;
      for (int tries = 0; tries < 50 && (num < 0 || used.count(num)); tries++) {
        const auto& range = rng.pick(numberRanges(pos));
        num = rng.intRange(range.first, range.second);
      }
      used.insert(num);
      const double talent = teamTalent + tier + rng.normal(0, 2);
      team->roster.push_back(makePlayer(rng, pos, talent, num));
    }
  }
  team->id = id;
  team->city = seed.city;
  team->name = seed.name;
  team->abbr = seed.abbr;
  team->colors = TeamColors{seed.primary, seed.secondary, seed.accent};
  team->conference = idx < 16 ? "North" : "South";
  static const char* const kDiv[] = {"East", "West", "Central", "Mountain"};
  team->division = std::string(idx < 16 ? "North" : "South") + " " + kDiv[(idx % 16) / 4];
  team->passLean = clamp(rng.normal(0.55, 0.08), 0.35, 0.75);
  team->aggression = clamp(rng.normal(0.5, 0.18), 0.1, 0.95);
  team->blitzLean = clamp(rng.normal(0.28, 0.08), 0.1, 0.5);
  return team;
}

}  // namespace

const std::vector<TeamSeed>& teamSeeds() { return kTeamSeeds; }

namespace {
int weightedOvr(const Weights& w, const Ratings& r) {
  double s = 0;
  double t = 0;
  for (const auto& kv : w) {
    s += r.*(kv.first) * kv.second;
    t += kv.second;
  }
  // stretch so that an average starter lands ~75 and elite ~90+
  return jsRoundI(clamp(40 + (s / t - 50) * 1.25, 30, 99));
}
}  // namespace

int computeOvr(Pos pos, const Ratings& r) { return weightedOvr(ovrWeights(pos), r); }

std::unique_ptr<Player> makePlayer(Rng& rng, Pos pos, double talent, int number) {
  const Archetype& a = arch(pos);
  Ratings r;
  // talent shifts all relevant skills; individual variation on top
  for (RMember k : RATING_KEYS) {
    const std::pair<RMember, double>* base = nullptr;
    for (const auto& kv : a.r)
      if (kv.first == k) base = &kv;
    if (!base) {
      r.*k = jsRound(clamp(rng.normal(BASE_LOW, 6), 20, 60));
    } else {
      r.*k = jsRound(clamp(base->second + talent + rng.normal(0, 5), 25, 99));
    }
  }
  r.stamina = jsRound(clamp(rng.normal(80, 7), 55, 99));
  const double heightIn = jsRound(rng.normal(a.h[0], a.h[1]));
  const double weightLb = jsRound(rng.normal(a.w[0], a.w[1]));
  // heavier players within a position are a bit slower/stronger: physics-consistent ratings
  const double wz = (weightLb - a.w[0]) / (std::max)(1.0, a.w[1]);
  r.speed = jsRound(clamp(r.speed - wz * 1.5, 25, 99));
  r.strength = jsRound(clamp(r.strength + wz * 2, 25, 99));
  auto p = std::make_unique<Player>();
  p->id = nextPlayerId++;
  p->first = rng.pick(FIRST);
  p->last = rng.pick(LAST);
  p->pos = pos;
  p->number = number;
  p->age = jsRoundI(clamp(rng.normal(26, 3), 21, 37));
  p->heightIn = heightIn;
  p->weightLb = weightLb;
  p->r = r;
  p->ovr = computeOvr(pos, r);
  p->fatigue = 0;
  p->injuredWeeks = 0;
  return p;
}

League createLeague(uint32_t seed) {
  Rng rng(seed);
  League out;
  out.reserve(kTeamSeeds.size());
  for (size_t i = 0; i < kTeamSeeds.size(); i++)
    out.push_back(makeTeam(rng, static_cast<int>(i), kTeamSeeds[i], static_cast<int>(i)));
  // the playbooks follow the identity the draw gave each team (no RNG, so the league itself is unchanged): ranked by
  // how much they throw, the run-first teams get the power and zone books and the pass-first ones the spread, the
  // run and shoot and the air raid; ranked by how often they bring pressure, the sit-back defenses play Tampa 2 and
  // quarters and the blitzing ones the 3-4 pressure book
  {
    std::vector<size_t> ix(out.size());
    std::iota(ix.begin(), ix.end(), size_t{0});
    std::stable_sort(ix.begin(), ix.end(), [&out](size_t a, size_t b) { return out[a]->passLean < out[b]->passLean; });
    static const char* const kOffOrder[] = {"Power Run", "Zone Run", "Pro Style", "West Coast", "Vertical", "Spread", "Run and Shoot", "Air Raid"};
    for (size_t k = 0; k < ix.size(); k++) out[ix[k]]->offScheme = kOffOrder[(k * 8) / ix.size()];
    std::stable_sort(ix.begin(), ix.end(), [&out](size_t a, size_t b) { return out[a]->blitzLean < out[b]->blitzLean; });
    static const char* const kDefOrder[] = {"Tampa 2", "4-3 Quarters", "4-3 Cover 3", "3-4 Multiple", "3-4 Pressure"};
    for (size_t k = 0; k < ix.size(); k++) out[ix[k]]->defScheme = kDefOrder[(k * 5) / ix.size()];
  }
  // the stars: every man graded against his position league-wide, and the coach's ability picks (no RNG either)
  {
    std::vector<Team*> teams;
    for (auto& t : out) teams.push_back(t.get());
    gradeLeague(teams);
  }
  return out;
}

TeamOvr teamOvr(const Team& t) {
  auto top = [&](Pos pos, size_t n) {
    std::vector<const Player*> v;
    for (const auto& p : t.roster)
      if (p->pos == pos) v.push_back(p.get());
    std::stable_sort(v.begin(), v.end(), [](const Player* a, const Player* b) { return a->ovr > b->ovr; });
    if (v.size() > n) v.resize(n);
    return v;
  };
  auto avg = [](std::initializer_list<std::vector<const Player*>> groups) {
    double s = 0;
    size_t n = 0;
    for (const auto& g : groups)
      for (const Player* p : g) {
        s += p->ovr;
        n++;
      }
    return s / static_cast<double>((std::max)(static_cast<size_t>(1), n));
  };
  const double off = avg({top(Pos::QB, 1), top(Pos::QB, 1), top(Pos::RB, 1), top(Pos::WR, 3), top(Pos::TE, 1),
                          top(Pos::OT, 2), top(Pos::OG, 2), top(Pos::C, 1)});
  const double def = avg({top(Pos::DE, 2), top(Pos::DT, 2), top(Pos::LB, 3), top(Pos::CB, 3), top(Pos::S, 2)});
  return TeamOvr{jsRoundI(off), jsRoundI(def), jsRoundI((off + def) / 2)};
}

// ================================================================== the depth chart (Roster.h)
namespace {

const DSlotInfo kSlots[DSLOTS] = {
    {DSlot::QB, "QB", "Quarterback", DGroup::Offense, Pos::QB, 1, "Every snap"},
    {DSlot::HB, "HB", "Halfback", DGroup::Offense, Pos::RB, 1, "Every snap with a back"},
    {DSlot::FB, "FB", "Fullback", DGroup::Offense, Pos::FB, 1, "Sets with a fullback (21, 22 personnel)"},
    {DSlot::WR, "WR", "Wide Receiver", DGroup::Offense, Pos::WR, 3, "X, Z, then the third and fourth receivers"},
    {DSlot::TE, "TE", "Tight End", DGroup::Offense, Pos::TE, 2, "TE1 in most sets; TE2 in 12 and 13 personnel"},
    {DSlot::LT, "LT", "Left Tackle", DGroup::Offense, Pos::OT, 1, "Every snap"},
    {DSlot::LG, "LG", "Left Guard", DGroup::Offense, Pos::OG, 1, "Every snap"},
    {DSlot::C, "C", "Center", DGroup::Offense, Pos::C, 1, "Every snap"},
    {DSlot::RG, "RG", "Right Guard", DGroup::Offense, Pos::OG, 1, "Every snap"},
    {DSlot::RT, "RT", "Right Tackle", DGroup::Offense, Pos::OT, 1, "Every snap"},
    {DSlot::LE, "LE", "Left End", DGroup::Defense, Pos::DE, 1, "Every front"},
    {DSlot::RE, "RE", "Right End", DGroup::Defense, Pos::DE, 1, "Every front"},
    {DSlot::DT, "DT", "Defensive Tackle", DGroup::Defense, Pos::DT, 2, "Both tackles in the 4-3; the nose in the 3-4"},
    {DSlot::LOLB, "LOLB", "Left Outside Linebacker", DGroup::Defense, Pos::LB, 1, "The Will in the 4-3, an edge in the 3-4"},
    {DSlot::MLB, "MLB", "Middle Linebacker", DGroup::Defense, Pos::LB, 2, "The Mike; both inside backers in the 3-4"},
    {DSlot::ROLB, "ROLB", "Right Outside Linebacker", DGroup::Defense, Pos::LB, 1, "The Sam in the 4-3, an edge in the 3-4"},
    {DSlot::CB, "CB", "Cornerback", DGroup::Defense, Pos::CB, 2, "Both outside corners, then the nickel and dime"},
    {DSlot::FS, "FS", "Free Safety", DGroup::Defense, Pos::S, 1, "Every front"},
    {DSlot::SS, "SS", "Strong Safety", DGroup::Defense, Pos::S, 1, "Every front"},
    {DSlot::K, "K", "Kicker", DGroup::Special, Pos::K, 1, "Field goals and tries"},
    {DSlot::P, "P", "Punter", DGroup::Special, Pos::P, 1, "Punts"},
    {DSlot::KOS, "KOS", "Kickoff Specialist", DGroup::Special, Pos::K, 1, "Kickoffs (unset: the kicker)"},
    {DSlot::LS, "LS", "Long Snapper", DGroup::Special, Pos::C, 1, "Snaps on punts, field goals and tries"},
    {DSlot::KR, "KR", "Kick Returner", DGroup::Special, Pos::WR, 1, "Kickoffs"},
    {DSlot::PR, "PR", "Punt Returner", DGroup::Special, Pos::WR, 1, "Punts"},
    {DSlot::TDRB, "3DRB", "Third Down Back", DGroup::Situational, Pos::RB, 1, "3rd down"},
    {DSlot::PWHB, "PWHB", "Power Back", DGroup::Situational, Pos::RB, 1, "2 yards or less to go, and the goal line"},
    {DSlot::SWR, "SWR", "Slot Receiver", DGroup::Situational, Pos::WR, 1, "The slot in three-wide sets"},
    {DSlot::RLE, "RLE", "Rush Left End", DGroup::Situational, Pos::DE, 1, "Passing downs (3rd and 7+, dime)"},
    {DSlot::RRE, "RRE", "Rush Right End", DGroup::Situational, Pos::DE, 1, "Passing downs (3rd and 7+, dime)"},
    {DSlot::RDT, "RDT", "Rush Tackle", DGroup::Situational, Pos::DT, 1, "Passing downs (3rd and 7+, dime)"},
    {DSlot::SUBLB, "SUBLB", "Sub Linebacker", DGroup::Situational, Pos::LB, 2, "Nickel and dime"},
    {DSlot::SLCB, "SLCB", "Slot Cornerback", DGroup::Situational, Pos::CB, 1, "Nickel and dime: the nickel back"},
    {DSlot::NT, "NT", "Nose Tackle", DGroup::Situational, Pos::DT, 1, "The 3-4's nose"},
};

/** The rating a slot is judged by: its position's formula, or (specialists, subs) its own. */
const Weights* slotWeights(DSlot s) {
  static const Weights KR{{&Ratings::speed, 3}, {&Ratings::accel, 2}, {&Ratings::agility, 2}, {&Ratings::elusiveness, 2}, {&Ratings::vision, 1}, {&Ratings::carrying, 1}};
  static const Weights PR{{&Ratings::speed, 2}, {&Ratings::agility, 3}, {&Ratings::elusiveness, 2}, {&Ratings::catching, 2}, {&Ratings::carrying, 1}, {&Ratings::accel, 1}};
  static const Weights KOS{{&Ratings::kickPower, 4}, {&Ratings::kickAcc, 1}};
  static const Weights LS{{&Ratings::awareness, 3}, {&Ratings::passBlock, 2}, {&Ratings::strength, 1}};
  static const Weights TDRB{{&Ratings::catching, 3}, {&Ratings::routeRunning, 2}, {&Ratings::passBlock, 2}, {&Ratings::elusiveness, 1}, {&Ratings::speed, 1}, {&Ratings::carrying, 1}};
  static const Weights PWHB{{&Ratings::breakTackle, 3}, {&Ratings::strength, 2}, {&Ratings::carrying, 2}, {&Ratings::accel, 1}, {&Ratings::vision, 1}};
  static const Weights SWR{{&Ratings::routeRunning, 3}, {&Ratings::agility, 2}, {&Ratings::catchInTraffic, 2}, {&Ratings::catching, 2}, {&Ratings::accel, 1}, {&Ratings::release, 1}};
  static const Weights RUSH{{&Ratings::passRush, 4}, {&Ratings::speed, 2}, {&Ratings::accel, 2}, {&Ratings::blockShed, 1}};
  static const Weights RDT{{&Ratings::passRush, 4}, {&Ratings::blockShed, 2}, {&Ratings::accel, 1}, {&Ratings::strength, 1}};
  static const Weights SUBLB{{&Ratings::zoneCov, 3}, {&Ratings::manCov, 2}, {&Ratings::speed, 2}, {&Ratings::pursuit, 2}, {&Ratings::tackling, 1}, {&Ratings::playRec, 1}};
  static const Weights SLCB{{&Ratings::manCov, 3}, {&Ratings::agility, 2}, {&Ratings::zoneCov, 2}, {&Ratings::accel, 1}, {&Ratings::speed, 1}, {&Ratings::tackling, 1}};
  static const Weights NT{{&Ratings::strength, 3}, {&Ratings::blockShed, 3}, {&Ratings::tackling, 1}, {&Ratings::playRec, 1}};
  switch (s) {
    case DSlot::KR: return &KR;
    case DSlot::PR: return &PR;
    case DSlot::KOS: return &KOS;
    case DSlot::LS: return &LS;
    case DSlot::TDRB: return &TDRB;
    case DSlot::PWHB: return &PWHB;
    case DSlot::SWR: return &SWR;
    case DSlot::RLE: case DSlot::RRE: return &RUSH;
    case DSlot::RDT: return &RDT;
    case DSlot::SUBLB: return &SUBLB;
    case DSlot::SLCB: return &SLCB;
    case DSlot::NT: return &NT;
    default: return nullptr;
  }
}

/** The positions a slot's coach's list is drawn from (the first is the slot's own). */
std::vector<Pos> slotPool(DSlot s) {
  switch (s) {
    case DSlot::FB: return {Pos::FB, Pos::RB};
    case DSlot::PWHB: return {Pos::RB, Pos::FB};
    case DSlot::LS: return {Pos::C, Pos::OG, Pos::OT, Pos::TE};
    case DSlot::KR: case DSlot::PR: return {Pos::WR, Pos::CB, Pos::RB};
    case DSlot::LOLB: case DSlot::ROLB: return {Pos::LB, Pos::DE};
    case DSlot::RLE: case DSlot::RRE: return {Pos::DE, Pos::LB};
    case DSlot::SUBLB: return {Pos::LB, Pos::S};
    case DSlot::SLCB: return {Pos::CB, Pos::S};
    default: return {kSlots[static_cast<int>(s)].natural};
  }
}

std::vector<Player*> menAt(const Team& t, Pos pos, bool bySpeed = false) {
  std::vector<Player*> v;
  for (const auto& p : t.roster)
    if (p->pos == pos) v.push_back(p.get());
  std::stable_sort(v.begin(), v.end(), [bySpeed](const Player* a, const Player* b) {
    return bySpeed ? a->r.speed > b->r.speed : a->ovr > b->ovr;
  });
  return v;
}

void appendUnique(std::vector<Player*>& out, const std::vector<Player*>& in) {
  for (Player* p : in)
    if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
}

/** Slots that split one position's men by rank (LT gets the best tackle, RT the second...): the slot's share is
 *  ranks k, k + n, k + 2n... of the position, then the rest of it. */
std::vector<Player*> shared(const Team& t, Pos pos, int k, int n) {
  const std::vector<Player*> all = menAt(t, pos);
  std::vector<Player*> out;
  for (size_t i = static_cast<size_t>(k); i < all.size(); i += static_cast<size_t>(n)) out.push_back(all[i]);
  appendUnique(out, all);
  return out;
}

const std::vector<int>* setList(const Team& t, DSlot s) {
  const size_t i = static_cast<size_t>(s);
  return i < t.chart.size() && !t.chart[i].empty() ? &t.chart[i] : nullptr;
}

Player* byId(const Team& t, int id) {
  for (const auto& p : t.roster)
    if (p->id == id) return p.get();
  return nullptr;
}

}  // namespace

const DSlotInfo& dslotInfo(DSlot s) { return kSlots[(std::min)(static_cast<int>(s), DSLOTS - 1)]; }

const char* dgroupName(DGroup g) {
  switch (g) {
    case DGroup::Offense: return "Offense";
    case DGroup::Defense: return "Defense";
    case DGroup::Special: return "Special Teams";
    case DGroup::Situational: return "Situational";
  }
  return "";
}

bool dslotByAbbr(const std::string& abbr, DSlot& out) {
  for (const DSlotInfo& i : kSlots)
    if (abbr == i.abbr) {
      out = i.slot;
      return true;
    }
  return false;
}

int slotOvr(const Player& p, DSlot s) {
  if (const Weights* w = slotWeights(s)) return weightedOvr(*w, p.r);
  return computeOvr(dslotInfo(s).natural, p.r);
}

std::vector<Player*> autoChart(const Team& t, DSlot s) {
  // what the sim does with a slot nobody has set (PlaySim::pickFrom's old pick), so the screen shows the truth: the
  // formation's roles take the position's men best first, in the role order (LT before RT, LE before RE, the Will,
  // Mike, Sam, FS before SS); the situational slots are the base slots' men in the order they come in
  std::vector<Player*> out;
  switch (s) {
    case DSlot::LT: out = shared(t, Pos::OT, 0, 2); break;
    case DSlot::RT: out = shared(t, Pos::OT, 1, 2); break;
    case DSlot::LG: out = shared(t, Pos::OG, 0, 2); break;
    case DSlot::RG: out = shared(t, Pos::OG, 1, 2); break;
    case DSlot::LE: case DSlot::RLE: out = shared(t, Pos::DE, 0, 2); break;
    case DSlot::RE: case DSlot::RRE: out = shared(t, Pos::DE, 1, 2); break;
    case DSlot::LOLB: out = shared(t, Pos::LB, 0, 3); break;
    case DSlot::MLB: out = shared(t, Pos::LB, 1, 3); break;
    case DSlot::ROLB: out = shared(t, Pos::LB, 2, 3); break;
    case DSlot::FS: out = shared(t, Pos::S, 0, 2); break;
    case DSlot::SS: out = shared(t, Pos::S, 1, 2); break;
    case DSlot::SWR: {
      // the third receiver (X and Z take the first two)
      const std::vector<Player*> w = menAt(t, Pos::WR);
      for (size_t i = 2; i < w.size(); i++) out.push_back(w[i]);
      appendUnique(out, w);
      break;
    }
    case DSlot::SLCB: {
      const std::vector<Player*> c = menAt(t, Pos::CB);
      for (size_t i = 2; i < c.size(); i++) out.push_back(c[i]);
      appendUnique(out, c);
      break;
    }
    case DSlot::KR: case DSlot::PR:
      // (the return unit's man: the fastest of the receivers, corners and backs)
      for (Pos q : slotPool(s)) appendUnique(out, menAt(t, q, true));
      std::stable_sort(out.begin(), out.end(), [](const Player* a, const Player* b) { return a->r.speed > b->r.speed; });
      return out;
    case DSlot::LS:
      // (the snapper: the center, then the line)
      for (Pos q : slotPool(s)) appendUnique(out, menAt(t, q));
      return out;
    default:
      out = menAt(t, slotPool(s)[0]);
      break;
  }
  // the pool's other positions under the slot's own men, by their rating there
  const std::vector<Pos> pool = slotPool(s);
  std::vector<Player*> rest;
  for (size_t i = 1; i < pool.size(); i++) appendUnique(rest, menAt(t, pool[i]));
  std::stable_sort(rest.begin(), rest.end(), [s](const Player* a, const Player* b) { return slotOvr(*a, s) > slotOvr(*b, s); });
  appendUnique(out, rest);
  return out;
}

std::vector<Player*> chartOrder(const Team& t, DSlot s) {
  const std::vector<int>* l = setList(t, s);
  if (!l) return autoChart(t, s);
  std::vector<Player*> out;
  for (int id : *l)
    if (Player* p = byId(t, id))
      if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
  return out;
}

void setChart(Team& t, DSlot s, const std::vector<Player*>& order) {
  if (t.chart.size() < static_cast<size_t>(DSLOTS)) t.chart.resize(DSLOTS);
  std::vector<int>& l = t.chart[static_cast<size_t>(s)];
  l.clear();
  for (const Player* p : order)
    if (p && std::find(l.begin(), l.end(), p->id) == l.end()) l.push_back(p->id);
}

void clearChart(Team& t, DSlot s) {
  if (static_cast<size_t>(s) < t.chart.size()) t.chart[static_cast<size_t>(s)].clear();
}

void clearAllCharts(Team& t) { t.chart.clear(); }

std::vector<Player*> chartStarters(const Team& t, DSlot s) {
  std::vector<Player*> out;
  const int n = dslotInfo(s).starters;
  for (Player* p : chartOrder(t, s))
    if (p->injuredWeeks <= 0 && static_cast<int>(out.size()) < n) out.push_back(p);
  return out;
}

void chartAddPlayer(Team& t, const Player& p) {
  for (int i = 0; i < DSLOTS && static_cast<size_t>(i) < t.chart.size(); i++) {
    std::vector<int>& l = t.chart[static_cast<size_t>(i)];
    if (l.empty()) continue;
    const std::vector<Pos> pool = slotPool(static_cast<DSlot>(i));
    if (std::find(pool.begin(), pool.end(), p.pos) == pool.end()) continue;
    if (std::find(l.begin(), l.end(), p.id) == l.end()) l.push_back(p.id);
  }
}

void chartRemovePlayer(Team& t, const Player& p) {
  for (std::vector<int>& l : t.chart) l.erase(std::remove(l.begin(), l.end(), p.id), l.end());
}

std::vector<DSlot> roleSlots(const Team& t, const std::string& role, int front, double down, double toGo, double los) {
  if (t.chart.empty()) return {};
  std::vector<DSlot> c;
  const bool shortYd = toGo <= 2 || los >= 97;
  const Front f = front >= 0 ? static_cast<Front>(front) : Front::Base;
  const bool sub = front >= 0 && (f == Front::Nickel || f == Front::Dime);
  const bool passing = front >= 0 && ((down >= 3 && toGo >= 7) || f == Front::Dime);
  if (front < 0) {
    if (role == "QB") c = {DSlot::QB};
    else if (role == "RB") {
      if (shortYd) c.push_back(DSlot::PWHB);
      else if (down >= 3) c.push_back(DSlot::TDRB);
      c.push_back(DSlot::HB);
    } else if (role == "FB") c = {DSlot::FB};
    else if (role == "X" || role == "Z" || role == "SLOT2") c = {DSlot::WR};
    else if (role == "SLOT") c = {DSlot::SWR, DSlot::WR};
    else if (role == "TE" || role == "TE2") c = {DSlot::TE};
    else if (role == "LT") c = {DSlot::LT};
    else if (role == "LG") c = {DSlot::LG};
    else if (role == "C") c = {DSlot::C};
    else if (role == "RG") c = {DSlot::RG};
    else if (role == "RT") c = {DSlot::RT};
  } else {
    if (role == "LE") {
      if (passing) c.push_back(DSlot::RLE);
      c.push_back(DSlot::LE);
    } else if (role == "RE") {
      if (passing) c.push_back(DSlot::RRE);
      c.push_back(DSlot::RE);
    } else if (role == "LDT") {
      if (f == Front::Base34) c.push_back(DSlot::NT);
      c.push_back(DSlot::DT);
    } else if (role == "RDT") {
      if (passing) c.push_back(DSlot::RDT);
      c.push_back(DSlot::DT);
    } else if (role == "WLB") {
      if (sub) c = {DSlot::SUBLB, DSlot::LOLB};
      else if (f == Front::Base34) c = {DSlot::MLB};
      else c = {DSlot::LOLB};
    } else if (role == "MLB") {
      if (sub) c.push_back(DSlot::SUBLB);
      c.push_back(DSlot::MLB);
    } else if (role == "SLB") c = {DSlot::ROLB};
    else if (role == "LOLB") c = {DSlot::LOLB};
    else if (role == "ROLB") c = {DSlot::ROLB};
    else if (role == "NB") c = {DSlot::SLCB, DSlot::CB};
    else if (role == "DB" || role == "LCB" || role == "RCB") c = {DSlot::CB};
    else if (role == "FS") c = {DSlot::FS};
    else if (role == "SS") c = {DSlot::SS};
  }
  std::vector<DSlot> out;
  for (DSlot s : c)
    if (chartSet(t, s)) out.push_back(s);
  return out;
}

Player* chartSpecialist(const Team& t, DSlot s) {
  if (!chartSet(t, s)) return nullptr;
  for (Player* p : chartOrder(t, s))
    if (p->injuredWeeks <= 0) return p;
  return nullptr;
}

std::vector<Player*> depth(const Team& team, Pos pos) {
  std::vector<Player*> out;
  // the chart's order where it is set: the position's slots, their lists' men in turn (LT1, RT1, LT2...)
  std::vector<DSlot> slots;
  switch (pos) {
    case Pos::QB: slots = {DSlot::QB}; break;
    case Pos::RB: slots = {DSlot::HB}; break;
    case Pos::FB: slots = {DSlot::FB}; break;
    case Pos::WR: slots = {DSlot::WR}; break;
    case Pos::TE: slots = {DSlot::TE}; break;
    case Pos::OT: slots = {DSlot::LT, DSlot::RT}; break;
    case Pos::OG: slots = {DSlot::LG, DSlot::RG}; break;
    case Pos::C: slots = {DSlot::C}; break;
    case Pos::DE: slots = {DSlot::LE, DSlot::RE}; break;
    case Pos::DT: slots = {DSlot::DT}; break;
    case Pos::LB: slots = {DSlot::LOLB, DSlot::MLB, DSlot::ROLB}; break;
    case Pos::CB: slots = {DSlot::CB}; break;
    case Pos::S: slots = {DSlot::FS, DSlot::SS}; break;
    case Pos::K: slots = {DSlot::K}; break;
    case Pos::P: slots = {DSlot::P}; break;
  }
  bool any = false;
  for (DSlot s : slots) any = any || chartSet(team, s);
  if (any) {
    std::vector<std::vector<Player*>> lists;
    for (DSlot s : slots) {
      std::vector<Player*> l;
      for (Player* p : chartOrder(team, s))
        if (p->injuredWeeks <= 0) l.push_back(p);
      lists.push_back(l);
    }
    for (size_t k = 0;; k++) {
      bool more = false;
      for (const auto& l : lists)
        if (k < l.size()) {
          more = true;
          if (std::find(out.begin(), out.end(), l[k]) == out.end()) out.push_back(l[k]);
        }
      if (!more) break;
    }
  }
  std::vector<Player*> rest;
  for (const auto& p : team.roster)
    if (p->pos == pos && p->injuredWeeks <= 0 && std::find(out.begin(), out.end(), p.get()) == out.end()) rest.push_back(p.get());
  std::stable_sort(rest.begin(), rest.end(), [](const Player* a, const Player* b) { return a->ovr > b->ovr; });
  out.insert(out.end(), rest.begin(), rest.end());
  return out;
}

// ================================================================== Create-a-Player (Roster.h)
const std::vector<CapArchetype>& capArchetypes(Pos pos) {
  using R = Ratings;
  static const std::vector<CapArchetype> QB = {
      {"Field General", "Reads the whole field and puts it where it has to be.", {{&R::accShort, 6}, {&R::accMid, 5}, {&R::decision, 8}, {&R::awareness, 8}, {&R::poise, 4}, {&R::speed, -6}, {&R::throwOnRun, -2}}, 76, 225},
      {"Strong Arm", "Throws it through windows nobody else can, and deep.", {{&R::throwPower, 10}, {&R::accDeep, 7}, {&R::accShort, -3}, {&R::decision, -2}}, 77, 232},
      {"Improviser", "Extends the play and throws on the move.", {{&R::throwOnRun, 9}, {&R::poise, 6}, {&R::agility, 5}, {&R::elusiveness, 6}, {&R::accDeep, -2}}, 74, 215},
      {"Scrambler", "A runner first: the pocket is a suggestion.", {{&R::speed, 16}, {&R::accel, 12}, {&R::agility, 10}, {&R::elusiveness, 14}, {&R::carrying, 6}, {&R::vision, 8}, {&R::throwPower, -3}, {&R::accMid, -4}, {&R::decision, -4}}, 74, 212},
  };
  static const std::vector<CapArchetype> RB = {
      {"Power Back", "Runs through arm tackles; the short-yardage hammer.", {{&R::breakTackle, 10}, {&R::strength, 10}, {&R::carrying, 6}, {&R::speed, -4}, {&R::agility, -4}, {&R::elusiveness, -6}}, 71, 228},
      {"Elusive Back", "Makes the first man miss, and the second.", {{&R::elusiveness, 10}, {&R::agility, 8}, {&R::accel, 6}, {&R::breakTackle, -6}, {&R::strength, -6}}, 69.5, 200},
      {"Receiving Back", "A route runner out of the backfield; picks up the blitz.", {{&R::catching, 14}, {&R::routeRunning, 12}, {&R::release, 8}, {&R::catchInTraffic, 8}, {&R::passBlock, 6}, {&R::breakTackle, -4}}, 70, 205},
  };
  static const std::vector<CapArchetype> FB = {
      {"Blocking", "Leads the way through the hole.", {{&R::runBlock, 8}, {&R::passBlock, 6}, {&R::strength, 5}, {&R::catching, -6}}, 72, 250},
      {"Utility", "Catches it in the flat and carries it at the goal line.", {{&R::catching, 8}, {&R::carrying, 6}, {&R::routeRunning, 8}, {&R::speed, 3}, {&R::runBlock, -5}}, 72, 238},
  };
  static const std::vector<CapArchetype> WR = {
      {"Deep Threat", "Takes the top off the defense.", {{&R::speed, 6}, {&R::accel, 4}, {&R::release, 4}, {&R::catchInTraffic, -4}, {&R::routeRunning, -2}}, 72, 188},
      {"Route Runner", "Separates at the top of every route.", {{&R::routeRunning, 10}, {&R::agility, 4}, {&R::catching, 4}, {&R::speed, -2}}, 72.5, 195},
      {"Physical", "Wins at the catch point and through contact.", {{&R::catchInTraffic, 10}, {&R::strength, 12}, {&R::breakTackle, 10}, {&R::release, 6}, {&R::speed, -4}, {&R::agility, -4}}, 75, 215},
      {"Slot", "Quick in and out of breaks over the middle.", {{&R::agility, 8}, {&R::accel, 6}, {&R::routeRunning, 6}, {&R::elusiveness, 6}, {&R::catchInTraffic, 4}, {&R::strength, -6}}, 70.5, 185},
  };
  static const std::vector<CapArchetype> TE = {
      {"Vertical Threat", "A receiver in a tight end's frame.", {{&R::speed, 6}, {&R::catching, 5}, {&R::routeRunning, 6}, {&R::runBlock, -8}, {&R::passBlock, -6}}, 77, 245},
      {"Possession", "Sure hands on third down.", {{&R::catching, 8}, {&R::catchInTraffic, 8}, {&R::routeRunning, 4}, {&R::speed, -3}}, 77, 250},
      {"Blocking", "A sixth lineman.", {{&R::runBlock, 12}, {&R::passBlock, 10}, {&R::strength, 8}, {&R::catching, -8}, {&R::routeRunning, -8}, {&R::speed, -4}}, 77.5, 262},
  };
  static const std::vector<CapArchetype> OT = {
      {"Pass Protector", "Mirrors the edge rusher.", {{&R::passBlock, 8}, {&R::agility, 5}, {&R::awareness, 4}, {&R::runBlock, -4}, {&R::strength, -2}}, 78, 315},
      {"Power", "Moves men off the ball.", {{&R::runBlock, 8}, {&R::strength, 8}, {&R::passBlock, -3}, {&R::agility, -5}}, 78.5, 325},
      {"Agile", "Gets to the second level on the move.", {{&R::agility, 8}, {&R::accel, 6}, {&R::speed, 5}, {&R::passBlock, 3}, {&R::strength, -6}}, 77.5, 305},
  };
  static const std::vector<CapArchetype> OG = {
      {"Pass Protector", "Holds the pocket's middle.", {{&R::passBlock, 8}, {&R::awareness, 5}, {&R::runBlock, -4}}, 76.5, 315},
      {"Power", "A mauler in the run game.", {{&R::runBlock, 8}, {&R::strength, 8}, {&R::passBlock, -3}, {&R::agility, -5}}, 76, 330},
      {"Agile", "Pulls and leads on the perimeter.", {{&R::agility, 8}, {&R::accel, 6}, {&R::speed, 5}, {&R::strength, -6}}, 76, 305},
  };
  static const std::vector<CapArchetype> C = {
      {"Pass Protector", "Calls the protections and holds the middle.", {{&R::passBlock, 8}, {&R::awareness, 6}, {&R::runBlock, -4}}, 75.5, 305},
      {"Power", "Drives the nose tackle back.", {{&R::runBlock, 8}, {&R::strength, 8}, {&R::agility, -5}}, 75.5, 315},
      {"Agile", "Reaches and climbs to the backers.", {{&R::agility, 8}, {&R::accel, 6}, {&R::speed, 4}, {&R::strength, -6}}, 75, 298},
  };
  static const std::vector<CapArchetype> DE = {
      {"Speed Rusher", "Bends the edge.", {{&R::speed, 8}, {&R::accel, 8}, {&R::passRush, 6}, {&R::strength, -6}, {&R::blockShed, -3}}, 76, 255},
      {"Power Rusher", "Walks the tackle back into the quarterback.", {{&R::strength, 10}, {&R::blockShed, 6}, {&R::passRush, 4}, {&R::speed, -5}}, 76.5, 280},
      {"Run Stopper", "Sets the edge and sheds to the ball.", {{&R::blockShed, 8}, {&R::tackling, 8}, {&R::playRec, 6}, {&R::strength, 6}, {&R::passRush, -6}, {&R::speed, -4}}, 76.5, 275},
  };
  static const std::vector<CapArchetype> DT = {
      {"Power Rusher", "Collapses the pocket from the inside.", {{&R::strength, 6}, {&R::passRush, 6}, {&R::blockShed, 4}}, 75, 305},
      {"Run Stopper", "Eats double teams.", {{&R::blockShed, 8}, {&R::strength, 8}, {&R::tackling, 6}, {&R::passRush, -8}, {&R::speed, -4}}, 75, 325},
      {"Speed Rusher", "Shoots the gap.", {{&R::speed, 8}, {&R::accel, 8}, {&R::passRush, 8}, {&R::strength, -8}, {&R::blockShed, -4}}, 75, 290},
  };
  static const std::vector<CapArchetype> LB = {
      {"Field General", "Reads it first and gets everybody lined up.", {{&R::playRec, 10}, {&R::awareness, 10}, {&R::tackling, 4}, {&R::zoneCov, 4}, {&R::speed, -3}}, 74, 240},
      {"Pass Coverage", "Runs with backs and tight ends.", {{&R::zoneCov, 10}, {&R::manCov, 12}, {&R::speed, 5}, {&R::agility, 4}, {&R::blockShed, -6}, {&R::strength, -6}}, 73.5, 228},
      {"Run Stopper", "Downhill, through the guard, to the ball.", {{&R::tackling, 8}, {&R::blockShed, 10}, {&R::strength, 8}, {&R::pursuit, 3}, {&R::manCov, -8}, {&R::zoneCov, -6}, {&R::speed, -4}}, 74, 248},
      {"Power Rusher", "An edge rusher standing up.", {{&R::passRush, 18}, {&R::blockShed, 6}, {&R::accel, 5}, {&R::strength, 4}, {&R::zoneCov, -8}, {&R::manCov, -8}}, 75, 250},
  };
  static const std::vector<CapArchetype> CB = {
      {"Man to Man", "Presses and carries the receiver everywhere.", {{&R::manCov, 8}, {&R::press, 10}, {&R::speed, 2}, {&R::zoneCov, -4}}, 72, 195},
      {"Zone", "Reads the quarterback and breaks on the ball.", {{&R::zoneCov, 10}, {&R::playRec, 8}, {&R::ballSkills, 6}, {&R::manCov, -4}, {&R::press, -6}}, 71.5, 192},
      {"Slot", "Quick enough for the slot, tough enough to tackle there.", {{&R::agility, 6}, {&R::accel, 6}, {&R::manCov, 4}, {&R::tackling, 4}, {&R::speed, -2}, {&R::press, -4}}, 70, 186},
  };
  static const std::vector<CapArchetype> S = {
      {"Zone", "A centerfielder.", {{&R::zoneCov, 8}, {&R::playRec, 8}, {&R::ballSkills, 8}, {&R::tackling, -4}}, 72.5, 200},
      {"Hybrid", "Covers the tight end and comes down into the box.", {{&R::manCov, 8}, {&R::zoneCov, 3}, {&R::speed, 3}, {&R::agility, 3}}, 72.5, 205},
      {"Run Support", "An extra linebacker in the box.", {{&R::tackling, 10}, {&R::pursuit, 6}, {&R::strength, 8}, {&R::blockShed, 6}, {&R::zoneCov, -6}, {&R::manCov, -6}}, 73, 215},
  };
  static const std::vector<CapArchetype> K = {
      {"Power", "Range from beyond 55.", {{&R::kickPower, 10}, {&R::kickAcc, -4}}, 72, 200},
      {"Accurate", "Automatic inside 45.", {{&R::kickAcc, 8}, {&R::kickPower, -4}}, 71.5, 190},
  };
  static const std::vector<CapArchetype> P = {
      {"Power", "Booms it and flips the field.", {{&R::kickPower, 10}, {&R::kickAcc, -4}}, 74, 215},
      {"Accurate", "Pins them inside the 10.", {{&R::kickAcc, 8}, {&R::kickPower, -4}}, 73.5, 205},
  };
  switch (pos) {
    case Pos::QB: return QB;
    case Pos::RB: return RB;
    case Pos::FB: return FB;
    case Pos::WR: return WR;
    case Pos::TE: return TE;
    case Pos::OT: return OT;
    case Pos::OG: return OG;
    case Pos::C: return C;
    case Pos::DE: return DE;
    case Pos::DT: return DT;
    case Pos::LB: return LB;
    case Pos::CB: return CB;
    case Pos::S: return S;
    case Pos::K: return K;
    case Pos::P: return P;
  }
  return QB;
}

Ratings capRatings(Pos pos, int archetype, double talent) {
  const Archetype& a = arch(pos);
  const std::vector<CapArchetype>& arches = capArchetypes(pos);
  const CapArchetype* ca = archetype >= 0 && archetype < static_cast<int>(arches.size()) ? &arches[static_cast<size_t>(archetype)] : nullptr;
  Ratings r;
  for (RMember k : RATING_KEYS) {
    double v = BASE_LOW;
    bool tmpl = false;
    for (const auto& kv : a.r)
      if (kv.first == k) {
        v = kv.second;
        tmpl = true;
      }
    if (tmpl) v += talent;
    if (ca)
      for (const auto& kv : ca->lean)
        if (kv.first == k) v += kv.second * (tmpl ? 1.0 : 0.5);
    r.*k = jsRound(clamp(v, 25, 99));
  }
  r.stamina = jsRound(clamp(82 + talent * 0.5, 55, 99));
  return r;
}

const std::vector<RatingDef>& ratingDefs() {
  using R = Ratings;
  static const std::vector<RatingDef> d = {
      {"SPD", "Speed", &R::speed, "Physical"}, {"ACC", "Acceleration", &R::accel, "Physical"}, {"AGI", "Agility", &R::agility, "Physical"},
      {"STR", "Strength", &R::strength, "Physical"}, {"STA", "Stamina", &R::stamina, "Physical"}, {"AWR", "Awareness", &R::awareness, "Physical"},
      {"CAR", "Carrying", &R::carrying, "Ball Carrier"}, {"BTK", "Break Tackle", &R::breakTackle, "Ball Carrier"},
      {"ELU", "Elusiveness", &R::elusiveness, "Ball Carrier"}, {"BCV", "Ball Carrier Vision", &R::vision, "Ball Carrier"},
      {"CTH", "Catching", &R::catching, "Receiving"}, {"CIT", "Catch in Traffic", &R::catchInTraffic, "Receiving"},
      {"RTE", "Route Running", &R::routeRunning, "Receiving"}, {"REL", "Release", &R::release, "Receiving"},
      {"THP", "Throw Power", &R::throwPower, "Passing"}, {"SAC", "Short Accuracy", &R::accShort, "Passing"},
      {"MAC", "Medium Accuracy", &R::accMid, "Passing"}, {"DAC", "Deep Accuracy", &R::accDeep, "Passing"},
      {"TOR", "Throw on the Run", &R::throwOnRun, "Passing"}, {"POI", "Poise Under Pressure", &R::poise, "Passing"},
      {"DEC", "Decision Making", &R::decision, "Passing"}, {"PBK", "Pass Block", &R::passBlock, "Blocking"},
      {"RBK", "Run Block", &R::runBlock, "Blocking"}, {"PRS", "Pass Rush", &R::passRush, "Defense"},
      {"BSH", "Block Shedding", &R::blockShed, "Defense"}, {"TAK", "Tackling", &R::tackling, "Defense"},
      {"PUR", "Pursuit", &R::pursuit, "Defense"}, {"PRC", "Play Recognition", &R::playRec, "Defense"},
      {"MCV", "Man Coverage", &R::manCov, "Defense"}, {"ZCV", "Zone Coverage", &R::zoneCov, "Defense"},
      {"PRE", "Press", &R::press, "Defense"}, {"BSK", "Ball Skills", &R::ballSkills, "Defense"},
      {"KPW", "Kick Power", &R::kickPower, "Kicking"}, {"KAC", "Kick Accuracy", &R::kickAcc, "Kicking"},
  };
  return d;
}

std::vector<const RatingDef*> keyRatings(Pos pos, size_t n) {
  // the OVR formula's ratings, heaviest first, then speed / awareness / strength to fill
  std::vector<std::pair<RMember, double>> w(ovrWeights(pos).begin(), ovrWeights(pos).end());
  std::stable_sort(w.begin(), w.end(), [](const std::pair<RMember, double>& a, const std::pair<RMember, double>& b) { return a.second > b.second; });
  std::vector<const RatingDef*> out;
  auto add = [&out](RMember m) {
    for (const RatingDef& d : ratingDefs())
      if (d.m == m && std::find(out.begin(), out.end(), &d) == out.end()) out.push_back(&d);
  };
  for (const auto& kv : w) add(kv.first);
  add(&Ratings::speed);
  add(&Ratings::awareness);
  add(&Ratings::strength);
  if (out.size() > n) out.resize(n);
  return out;
}

std::vector<const RatingDef*> slotKeyRatings(DSlot s, size_t n) {
  const Weights* w = slotWeights(s);
  if (!w) return keyRatings(dslotInfo(s).natural, n);
  std::vector<std::pair<RMember, double>> v(w->begin(), w->end());
  std::stable_sort(v.begin(), v.end(), [](const std::pair<RMember, double>& a, const std::pair<RMember, double>& b) { return a.second > b.second; });
  std::vector<const RatingDef*> out;
  auto add = [&out](RMember m) {
    for (const RatingDef& d : ratingDefs())
      if (d.m == m && std::find(out.begin(), out.end(), &d) == out.end()) out.push_back(&d);
  };
  for (const auto& kv : v) add(kv.first);
  for (const RatingDef* d : keyRatings(dslotInfo(s).natural, n)) add(d->m);
  if (out.size() > n) out.resize(n);
  return out;
}

const std::vector<std::string>& firstNamePool() { return FIRST; }
const std::vector<std::string>& lastNamePool() { return LAST; }

bool numberFree(const Team& t, int number, const Player* except) {
  for (const auto& p : t.roster)
    if (p.get() != except && p->number == number) return false;
  return true;
}

bool numberLegal(Pos pos, int number) {
  for (const auto& r : numberRanges(pos))
    if (number >= r.first && number <= r.second) return true;
  return false;
}

int freeNumber(const Team& t, Pos pos, int prefer) {
  if (prefer >= 0 && prefer <= 99 && numberFree(t, prefer)) return prefer;
  for (const auto& r : numberRanges(pos))
    for (int n = r.first; n <= r.second; n++)
      if (numberFree(t, n)) return n;
  for (int n = 0; n <= 99; n++)
    if (numberFree(t, n)) return n;
  return -1;
}

}  // namespace tag
