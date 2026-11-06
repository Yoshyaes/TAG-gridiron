// Port of src/sim/league.ts
// Fictional league generation. Every team, name and player here is original.
// RNG call order matches the TS exactly, so createLeague(2027) yields the same league.
#include "League.h"

#include <set>
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
    {"Hartford", "Whalers FC", "HFD", "#003F5C", "#7FB800", "#FFFFFF"},
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
  const double teamTalent = rng.normal(0, 2.4);  // franchise-level talent spread
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

int computeOvr(Pos pos, const Ratings& r) {
  const Weights& w = ovrWeights(pos);
  double s = 0;
  double t = 0;
  for (const auto& kv : w) {
    s += r.*(kv.first) * kv.second;
    t += kv.second;
  }
  // stretch so that an average starter lands ~75 and elite ~90+
  return jsRoundI(clamp(40 + (s / t - 50) * 1.25, 30, 99));
}

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

}  // namespace tag
