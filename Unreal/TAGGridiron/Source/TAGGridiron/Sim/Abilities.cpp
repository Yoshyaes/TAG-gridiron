// Superstar and X-Factor abilities: the catalog, who can carry what, the coach's picks, the zone (see Abilities.h).
#include "Abilities.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "PlaySim.h"

namespace tag {

namespace {

constexpr uint32_t PB(Pos p) { return 1u << static_cast<uint32_t>(p); }
template <class... P>
constexpr uint32_t PM(P... p) {
  return (PB(p) | ...);
}

using R = Ratings;
using A = AbilityId;
using S = StarGrade;
constexpr AbilityTier SS = AbilityTier::Superstar;
constexpr AbilityTier XF = AbilityTier::XFactor;

AbilityDef sup(A id, const char* key, const char* name, const char* blurb, uint32_t pos, S grade, int cost, std::vector<AbilityReq> req,
               const char* icon) {
  AbilityDef d;
  d.id = id;
  d.key = key;
  d.name = name;
  d.blurb = blurb;
  d.tier = SS;
  d.posMask = pos;
  d.minGrade = grade;
  d.cost = cost;
  d.req = std::move(req);
  d.icon = icon;
  return d;
}
AbilityDef xf(A id, const char* key, const char* name, const char* zoneFx, uint32_t pos, std::vector<AbilityReq> req, int need, const char* trigger,
              const char* defeat, const char* icon) {
  AbilityDef d;
  d.id = id;
  d.key = key;
  d.name = name;
  d.blurb = zoneFx;
  d.zoneFx = zoneFx;
  d.tier = XF;
  d.posMask = pos;
  d.minGrade = S::XFactor;
  d.cost = 0;
  d.req = std::move(req);
  d.need = need;
  d.trigger = trigger;
  d.defeat = defeat;
  d.icon = icon;
  return d;
}

std::vector<AbilityDef> buildDefs() {
  using P = Pos;
  const uint32_t CARRY = PM(P::QB, P::RB, P::FB, P::WR, P::TE);
  const uint32_t OL = PM(P::OT, P::OG, P::C);
  const uint32_t DL = PM(P::DE, P::DT);
  const uint32_t DEF = PM(P::DE, P::DT, P::LB, P::CB, P::S);
  std::vector<AbilityDef> v;
  v.push_back(AbilityDef{});  // None
  // ---- quarterback
  v.push_back(sup(A::PocketPoise, "pocket_poise", "Pocket Poise", "Hangs in with a rusher in his face: pressure costs him little accuracy.",
                  PB(P::QB), S::Star, 2, {{&R::poise, 76, "POI"}}, "PP"));
  v.push_back(sup(A::LaserLine, "laser_line", "Laser Line", "Short and intermediate throws arrive on the numbers.", PB(P::QB), S::Star, 2,
                  {{&R::accShort, 82, "SAC"}}, "LL"));
  v.push_back(sup(A::MoonballTouch, "moonball_touch", "Moonball Touch", "Deep balls dropped in the bucket, with a few more yards of arm.",
                  PB(P::QB), S::Superstar, 3, {{&R::accDeep, 76, "DAC"}, {&R::throwPower, 86, "THP"}}, "MT"));
  v.push_back(sup(A::OffScript, "off_script", "Off Script", "Plays off schedule: throws on the run as if set, and slips the rusher who has him.", PB(P::QB), S::Star, 1,
                  {{&R::throwOnRun, 74, "TOR"}}, "OS"));
  v.push_back(sup(A::FilmJunkie, "film_junkie", "Film Junkie", "Reads the coverage before the snap: quicker, sharper progressions.",
                  PB(P::QB), S::Superstar, 2, {{&R::decision, 76, "DEC"}, {&R::awareness, 74, "AWR"}}, "FJ"));
  v.push_back(sup(A::TwoMinuteMaestro, "two_minute_maestro", "Two-Minute Maestro",
                  "In the last two minutes of a half, or a close fourth quarter: sharper on every throw.", PB(P::QB), S::Star, 1,
                  {{&R::awareness, 72, "AWR"}}, "2M"));
  v.push_back(sup(A::EscapeHatch, "escape_hatch", "Escape Hatch", "Slippery in the pocket: a rusher who gets to him often comes away with nothing.",
                  PB(P::QB), S::Star, 1, {{&R::agility, 70, "AGI"}}, "EH"));
  // ---- ball carriers
  v.push_back(sup(A::BatteringRam, "battering_ram", "Battering Ram", "Runs through the man in front of him and keeps the pile moving.",
                  PM(P::RB, P::FB, P::TE), S::Star, 2, {{&R::breakTackle, 80, "BTK"}, {&R::strength, 66, "STR"}}, "BR"));
  v.push_back(sup(A::AnkleTaker, "ankle_taker", "Ankle Taker", "Tacklers from the side grab air; his moves leave men on the turf.",
                  PM(P::RB, P::WR), S::Star, 2, {{&R::elusiveness, 84, "ELU"}}, "AT"));
  v.push_back(sup(A::ViseGrip, "vise_grip", "Vise Grip", "Ball security: fumbles less than half as often.", CARRY, S::Star, 1,
                  {{&R::carrying, 80, "CAR"}}, "VG"));
  v.push_back(sup(A::HomeRunHitter, "home_run_hitter", "Home Run Hitter", "An extra gear with the ball in his hands.", PM(P::RB, P::WR),
                  S::Superstar, 3, {{&R::speed, 92, "SPD"}}, "HR"));
  v.push_back(sup(A::SecondEffort, "second_effort", "Second Effort", "Tears out of tackles in a moving pile and falls forward.",
                  PM(P::RB, P::FB, P::TE), S::Star, 1, {{&R::breakTackle, 74, "BTK"}}, "SE"));
  v.push_back(sup(A::ReceivingBack, "receiving_back", "Receiving Back", "A back who runs routes and catches like a receiver.",
                  PM(P::RB, P::FB), S::Star, 1, {{&R::catching, 66, "CTH"}}, "RB"));
  // ---- receivers / tight ends
  v.push_back(sup(A::GlueHands, "glue_hands", "Glue Hands", "Catches what reaches him.", PM(P::WR, P::TE, P::RB), S::Star, 2,
                  {{&R::catching, 82, "CTH"}}, "GH"));
  v.push_back(sup(A::JumpBallAce, "jump_ball_ace", "Jump Ball Ace", "Wins the contested ball with a man draped on him.", PM(P::WR, P::TE),
                  S::Superstar, 3, {{&R::catchInTraffic, 78, "CIT"}}, "JB"));
  v.push_back(sup(A::RouteArtist, "route_artist", "Route Artist", "Sells every break: the man on him reads it late.", PM(P::WR, P::TE),
                  S::Star, 2, {{&R::routeRunning, 80, "RTE"}}, "RA"));
  v.push_back(sup(A::JamBreaker, "jam_breaker", "Jam Breaker", "Beats press coverage off the line.", PM(P::WR, P::TE), S::Star, 1,
                  {{&R::release, 76, "REL"}}, "JM"));
  v.push_back(sup(A::CatchAndGo, "catch_and_go", "Catch and Go", "Catches in stride and keeps his speed: yards after the catch.",
                  PM(P::WR, P::TE, P::RB), S::Star, 2, {{&R::speed, 88, "SPD"}}, "CG"));
  v.push_back(sup(A::RedZoneThreat, "red_zone_threat", "Red Zone Threat", "Inside the 20 he wins in traffic.", PM(P::WR, P::TE), S::Star, 1,
                  {{&R::catchInTraffic, 72, "CIT"}}, "RZ"));
  v.push_back(sup(A::ChainMover, "chain_mover", "Chain Mover", "On third and fourth down he gets open and holds on.",
                  PM(P::WR, P::TE, P::RB), S::Star, 1, {{&R::routeRunning, 74, "RTE"}}, "CM"));
  // ---- blockers
  v.push_back(sup(A::BrickWall, "brick_wall", "Brick Wall", "Pass protection that holds: rushers on him shed far less often.",
                  OL | PB(P::TE), S::Star, 2, {{&R::passBlock, 76, "PBK"}}, "BW"));
  v.push_back(sup(A::RoadGrader, "road_grader", "Road Grader", "Moves men off the ball in the run game.", OL | PM(P::TE, P::FB), S::Star, 2,
                  {{&R::runBlock, 76, "RBK"}}, "RG"));
  v.push_back(sup(A::Anchor, "anchor", "Anchor", "Bull rushes die on him.", OL, S::Superstar, 2, {{&R::strength, 86, "STR"}}, "AN"));
  v.push_back(sup(A::QuickFeet, "quick_feet", "Quick Feet", "Mirrors speed and finesse rushers.", OL, S::Star, 1, {{&R::agility, 58, "AGI"}},
                  "QF"));
  v.push_back(sup(A::Climber, "climber", "Climber", "Gets off the double team to the linebacker and stays on him.", OL | PM(P::TE, P::FB), S::Star, 1,
                  {{&R::runBlock, 70, "RBK"}}, "CL"));
  v.push_back(sup(A::SafetyValve, "safety_valve", "Safety Valve", "The quarterback's outlet: sure hands over the middle, in traffic or not.",
                  PM(P::TE, P::RB, P::FB), S::Star, 1, {{&R::catching, 68, "CTH"}}, "SV"));
  // ---- the front
  v.push_back(sup(A::EdgeBender, "edge_bender", "Edge Bender", "Bends the corner: wins pass-rush reps far more often.", PM(P::DE, P::LB),
                  S::Star, 2, {{&R::passRush, 76, "PRSH"}, {&R::speed, 76, "SPD"}}, "EB"));
  v.push_back(sup(A::BullCharge, "bull_charge", "Bull Charge", "Power at the point: drives blockers back, power moves land.", DL, S::Star, 2,
                  {{&R::strength, 84, "STR"}}, "BC"));
  v.push_back(sup(A::GapPlugger, "gap_plugger", "Gap Plugger", "Sheds run blocks and fills his gap.", DL | PB(P::LB), S::Star, 2,
                  {{&R::blockShed, 76, "BSH"}}, "GP"));
  v.push_back(sup(A::Motor, "motor", "Motor", "Never stops: rush energy comes back twice as fast, chases every play.", DL | PB(P::LB), S::Star, 1,
                  {{&R::stamina, 80, "STA"}}, "MO"));
  v.push_back(sup(A::Finisher, "finisher", "Finisher", "Comes off the block when the quarterback is in reach, and brings him down.",
                  DL | PB(P::LB), S::Superstar, 2, {{&R::passRush, 72, "PRSH"}, {&R::tackling, 72, "TAK"}}, "FN"));
  v.push_back(sup(A::GetOff, "get_off", "Get-Off", "First off the ball at the snap: the blocker is beaten to his set.", DL | PB(P::LB), S::Star, 1,
                  {{&R::accel, 80, "ACC"}}, "GO"));
  // ---- tacklers / coverage
  v.push_back(sup(A::SureTackler, "sure_tackler", "Sure Tackler", "Wraps up: misses fewer tackles.", DEF, S::Star, 2, {{&R::tackling, 78, "TAK"}},
                  "ST"));
  v.push_back(sup(A::BallPunch, "ball_punch", "Ball Punch", "Goes for the ball on every tackle: forces fumbles.", DEF, S::Star, 1,
                  {{&R::tackling, 70, "TAK"}}, "BP"));
  v.push_back(sup(A::Thumper, "thumper", "Thumper", "Stops the carrier where he stands: no yards after contact.", PM(P::LB, P::S),
                  S::Superstar, 3, {{&R::tackling, 80, "TAK"}, {&R::strength, 70, "STR"}}, "TH"));
  v.push_back(sup(A::SidelineToSideline, "sideline_to_sideline", "Sideline to Sideline", "Pursuit angles and closing speed.",
                  PM(P::LB, P::S, P::DE), S::Star, 2, {{&R::pursuit, 80, "PUR"}}, "SS"));
  v.push_back(sup(A::Diagnoser, "diagnoser", "Diagnoser", "Reads run or pass at the snap and is first to react.", PM(P::LB, P::S, P::CB),
                  S::Star, 1, {{&R::playRec, 74, "PRC"}}, "DG"));
  v.push_back(sup(A::Blanket, "blanket", "Blanket", "Mirrors his man through every break in man coverage.", PM(P::CB, P::S, P::LB), S::Star, 2,
                  {{&R::manCov, 76, "MCV"}}, "BL"));
  v.push_back(sup(A::ZoneReader, "zone_reader", "Zone Reader", "Breaks on the ball out of his zone before the throw is out.",
                  PM(P::CB, P::S, P::LB), S::Star, 2, {{&R::zoneCov, 76, "ZCV"}}, "ZR"));
  v.push_back(sup(A::PickArtist, "pick_artist", "Pick Artist", "Turns breakups into interceptions.", PM(P::CB, P::S, P::LB), S::Superstar, 3,
                  {{&R::ballSkills, 70, "BSK"}, {&R::catching, 58, "CTH"}}, "PA"));
  v.push_back(sup(A::SwatTeam, "swat_team", "Swat Team", "Gets a hand in on contested throws.", PM(P::CB, P::S), S::Star, 2,
                  {{&R::ballSkills, 68, "BSK"}}, "SW"));
  v.push_back(sup(A::PressMaster, "press_master", "Press Master", "Wins the jam at the line.", PB(P::CB), S::Star, 1, {{&R::press, 68, "PRS"}},
                  "PM"));
  v.push_back(sup(A::DeepRange, "deep_range", "Deep Range", "Covers ground over the top: faster breaks in deep zones.", PM(P::S, P::CB),
                  S::Superstar, 2, {{&R::speed, 86, "SPD"}, {&R::zoneCov, 74, "ZCV"}}, "DR"));
  v.push_back(sup(A::ClosingSpeed, "closing_speed", "Closing Speed", "Takes away the space: the catch is made with him already there.",
                  PM(P::CB, P::S, P::LB), S::Star, 1, {{&R::speed, 88, "SPD"}}, "CS"));
  // ---- kickers / punters
  v.push_back(sup(A::BigLeg, "big_leg", "Big Leg", "More distance on every kick.", PM(P::K, P::P), S::Star, 2, {{&R::kickPower, 84, "KPW"}}, "BG"));
  v.push_back(sup(A::SteadyFoot, "steady_foot", "Steady Foot", "Straighter kicks.", PM(P::K, P::P), S::Star, 2, {{&R::kickAcc, 80, "KAC"}}, "SF"));
  v.push_back(sup(A::IceWater, "ice_water", "Ice Water", "In a close fourth quarter his kicks go straight.", PB(P::K), S::Superstar, 2,
                  {{&R::kickAcc, 78, "KAC"}}, "IW"));
  v.push_back(sup(A::PinPoint, "pin_point", "Pin Point", "Punts placed where he aims them.", PB(P::P), S::Star, 1, {{&R::kickAcc, 76, "KAC"}}, "PN"));
  // ---- X-Factors
  v.push_back(xf(A::IceVeins, "ice_veins", "Ice Veins", "Pressure means nothing: pinpoint on every throw, from the pocket or on the run.",
                 PB(P::QB), {{&R::poise, 78, "POI"}, {&R::accMid, 76, "MAC"}}, 2, "2 straight completions of 15+ yards",
                 "Sacked or intercepted", "IV"));
  v.push_back(xf(A::RocketArm, "rocket_arm", "Rocket Arm", "Deep shots on a rope: more arm, deep balls placed like short ones.", PB(P::QB),
                 {{&R::throwPower, 88, "THP"}, {&R::accDeep, 74, "DAC"}}, 2, "2 completions of 20+ air yards",
                 "An interception, or 2 straight incompletions", "RK"));
  v.push_back(xf(A::Juggernaut, "juggernaut", "Juggernaut", "Tacklers in front of him bounce off; every pile goes forward.", PM(P::RB, P::FB),
                 {{&R::breakTackle, 82, "BTK"}, {&R::strength, 68, "STR"}}, 3, "3 runs of 5+ yards",
                 "A fumble, or 2 straight runs of 1 yard or less", "JG"));
  v.push_back(xf(A::Phantom, "phantom", "Phantom", "Untouchable in space: arm tackles and angles grab nothing.", PM(P::RB, P::WR),
                 {{&R::elusiveness, 84, "ELU"}, {&R::agility, 86, "AGI"}}, 2, "2 broken tackles", "Tackled for a loss, or a fumble",
                 "PH"));
  v.push_back(xf(A::Spotlight, "spotlight", "Spotlight", "Every 50-50 ball is his.", PM(P::WR, P::TE), {{&R::catchInTraffic, 78, "CIT"}, {&R::catching, 80, "CTH"}},
                 2, "2 catches of 15+ yards", "An interception thrown his way, or 2 straight incompletions to him", "SL"));
  v.push_back(xf(A::Slipstream, "slipstream", "Slipstream", "Open on every route and gone after the catch.", PM(P::WR, P::TE, P::RB),
                 {{&R::routeRunning, 78, "RTE"}, {&R::speed, 88, "SPD"}}, 3, "3 catches", "2 straight incompletions to him, or a fumble",
                 "SP"));
  v.push_back(xf(A::Fortress, "fortress", "Fortress", "Nobody gets past him: rushers on him all but never shed.", OL,
                 {{&R::passBlock, 80, "PBK"}, {&R::strength, 84, "STR"}}, 4, "4 straight dropbacks without a sack", "A sack", "FT"));
  v.push_back(xf(A::Havoc, "havoc", "Havoc", "Wins nearly every rep: moves land, blockers can't hold him, quarterbacks go down.",
                 DL | PB(P::LB), {{&R::passRush, 78, "PRSH"}}, 2, "A sack (2 pips) or a tackle for loss (1 pip)", "A 20+ yard play by the offense",
                 "HV"));
  v.push_back(xf(A::BlackHole, "black_hole", "Black Hole", "Runs at him go nowhere: sheds every run block and makes the stop.", DL,
                 {{&R::blockShed, 80, "BSH"}, {&R::strength, 86, "STR"}}, 2, "2 tackles on runs of 2 yards or less", "A run of 10+ yards",
                 "BH"));
  v.push_back(xf(A::Enforcer, "enforcer", "Enforcer", "Every tackle a collision: no yards after contact, balls come loose.", PM(P::LB, P::S),
                 {{&R::tackling, 82, "TAK"}}, 3, "3 tackles", "A broken tackle on him, or a touchdown allowed", "EN"));
  v.push_back(xf(A::Lockdown, "lockdown", "Lockdown", "His man is never open, and balls thrown there get knocked away.", PB(P::CB),
                 {{&R::manCov, 80, "MCV"}, {&R::speed, 90, "SPD"}}, 2, "2 incompletions thrown at the man he covers (or his breakups)",
                 "A catch of 15+ yards or a touchdown on him", "LD"));
  v.push_back(xf(A::LastLine, "last_line", "Last Line", "Erases the deep middle: breaks on everything and catches what he gets to.",
                 PM(P::S, P::CB), {{&R::zoneCov, 80, "ZCV"}, {&R::ballSkills, 68, "BSK"}}, 2, "A takeaway (2 pips) or a breakup (1 pip)",
                 "A pass play of 30+ yards", "LS"));
  v.push_back(xf(A::Automatic, "automatic", "Automatic", "Kicks split the uprights from anywhere in range.", PB(P::K), {{&R::kickAcc, 84, "KAC"}}, 2,
                 "2 straight field goals made", "A missed kick", "AU"));
  return v;
}

double ratingOf(const Player& p, double Ratings::*k) { return p.r.*k; }

/** How well his ratings fit an ability: the mean margin over its requirements. */
double fitScore(const Player& p, const AbilityDef& a) {
  if (a.req.empty()) return 0;
  double s = 0;
  for (const AbilityReq& r : a.req) s += ratingOf(p, r.key) - r.min;
  return s / static_cast<double>(a.req.size());
}

/** Position groups the grade is ranked in. */
int gradeGroup(Pos p) {
  switch (p) {
    case Pos::QB: return 0;
    case Pos::RB: case Pos::FB: return 1;
    case Pos::WR: return 2;
    case Pos::TE: return 3;
    case Pos::OT: return 4;
    case Pos::OG: case Pos::C: return 5;
    case Pos::DE: return 6;
    case Pos::DT: return 7;
    case Pos::LB: return 8;
    case Pos::CB: return 9;
    case Pos::S: return 10;
    case Pos::K: return 11;
    case Pos::P: return 12;
  }
  return 0;
}

void addR(Ratings& a, double Ratings::*k, double v) { a.*k += v; }

}  // namespace

const char* starGradeName(StarGrade g) {
  switch (g) {
    case StarGrade::Star: return "Star";
    case StarGrade::Superstar: return "Superstar";
    case StarGrade::XFactor: return "X-Factor";
    default: return "";
  }
}
int gradeSlots(StarGrade g) { return static_cast<int>(g); }
int gradeAP(StarGrade g) {
  switch (g) {
    case StarGrade::Star: return 3;
    case StarGrade::Superstar: return 5;
    case StarGrade::XFactor: return 7;
    default: return 0;
  }
}

const std::vector<AbilityDef>& abilityDefs() {
  // (indexed by id: abilityDef() reads v[id], so the catalog is put in id order whatever order it was written in)
  static const std::vector<AbilityDef> v = [] {
    std::vector<AbilityDef> d = buildDefs();
    std::stable_sort(d.begin(), d.end(), [](const AbilityDef& x, const AbilityDef& y) { return x.id < y.id; });
    return d;
  }();
  return v;
}
const AbilityDef& abilityDef(AbilityId id) {
  const auto& v = abilityDefs();
  const size_t i = static_cast<size_t>(id);
  return i < v.size() ? v[i] : v[0];
}
const AbilityDef* abilityByKey(const std::string& key) {
  for (const AbilityDef& d : abilityDefs())
    if (d.id != AbilityId::None && key == d.key) return &d;
  return nullptr;
}
std::vector<const AbilityDef*> abilitiesFor(Pos pos, AbilityTier tier) {
  std::vector<const AbilityDef*> out;
  for (const AbilityDef& d : abilityDefs())
    if (d.id != AbilityId::None && d.tier == tier && (d.posMask & PB(pos))) out.push_back(&d);
  return out;
}

std::string abilityLock(const Player& p, const AbilityDef& a) {
  if (a.id == AbilityId::None) return "Nothing";
  if (!(a.posMask & PB(p.pos))) return std::string("Not for a ") + posName(p.pos);
  if (static_cast<int>(p.grade) < static_cast<int>(a.minGrade))
    return std::string("Needs ") + starGradeName(a.minGrade) + " grade";
  for (const AbilityReq& r : a.req)
    if (ratingOf(p, r.key) < r.min) return std::string("Needs ") + r.abbr + " " + std::to_string(r.min);
  return "";
}

int abilityAPUsed(const Player& p) {
  int s = 0;
  for (AbilityId id : p.abilities) s += abilityDef(id).cost;
  return s;
}

bool hasAbility(const Player& p, AbilityId id) {
  if (id == AbilityId::None) return false;
  return p.xFactor == id || std::find(p.abilities.begin(), p.abilities.end(), id) != p.abilities.end();
}

bool equipAbility(Player& p, AbilityId id, std::string* why) {
  const AbilityDef& a = abilityDef(id);
  auto fail = [why](const std::string& w) {
    if (why) *why = w;
    return false;
  };
  if (a.id == AbilityId::None) return fail("Nothing to equip");
  const std::string lock = abilityLock(p, a);
  if (!lock.empty()) return fail(lock);
  if (hasAbility(p, id)) return fail("Already equipped");
  if (a.tier == AbilityTier::XFactor) {
    p.xFactor = id;
    return true;
  }
  if (static_cast<int>(p.abilities.size()) >= gradeSlots(p.grade)) return fail("No open slot: remove one first");
  if (abilityAPUsed(p) + a.cost > gradeAP(p.grade))
    return fail("Needs " + std::to_string(a.cost) + " AP, " + std::to_string(gradeAP(p.grade) - abilityAPUsed(p)) + " left");
  p.abilities.push_back(id);
  return true;
}

bool unequipAbility(Player& p, AbilityId id) {
  if (p.xFactor == id && id != AbilityId::None) {
    p.xFactor = AbilityId::None;
    return true;
  }
  const auto it = std::find(p.abilities.begin(), p.abilities.end(), id);
  if (it == p.abilities.end()) return false;
  p.abilities.erase(it);
  return true;
}

void autoLoadout(Player& p) {
  p.abilities.clear();
  p.xFactor = AbilityId::None;
  if (p.grade == StarGrade::None) return;
  // the X-Factor his ratings fit best
  double bestXf = -1e9;
  for (const AbilityDef* a : abilitiesFor(p.pos, AbilityTier::XFactor))
    if (abilityLock(p, *a).empty() && fitScore(p, *a) > bestXf) {
      bestXf = fitScore(p, *a);
      p.xFactor = a->id;
    }
  // the Superstar abilities: the strongest fits first (an ability's AP is what it is worth), within his slots and AP
  std::vector<std::pair<double, const AbilityDef*>> cand;
  for (const AbilityDef* a : abilitiesFor(p.pos, AbilityTier::Superstar))
    if (abilityLock(p, *a).empty()) cand.emplace_back(fitScore(p, *a) + a->cost * 4.0, a);
  std::stable_sort(cand.begin(), cand.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
  for (const auto& c : cand) equipAbility(p, c.second->id);
}

void validateLoadout(Player& p) {
  if (p.xFactor != AbilityId::None && !abilityLock(p, abilityDef(p.xFactor)).empty()) p.xFactor = AbilityId::None;
  std::vector<AbilityId> keep;
  int ap = 0;
  for (AbilityId id : p.abilities) {
    const AbilityDef& a = abilityDef(id);
    if (a.tier != AbilityTier::Superstar || !abilityLock(p, a).empty()) continue;
    if (std::find(keep.begin(), keep.end(), id) != keep.end()) continue;
    if (static_cast<int>(keep.size()) >= gradeSlots(p.grade) || ap + a.cost > gradeAP(p.grade)) continue;
    keep.push_back(id);
    ap += a.cost;
  }
  p.abilities = keep;
}

void gradeLeague(std::vector<Team*>& teams) {
  std::map<int, std::vector<Player*>> groups;
  for (Team* t : teams)
    for (auto& p : t->roster) groups[gradeGroup(p->pos)].push_back(p.get());
  std::map<const Player*, double> standing;  // 0 = the best at his position in the league, 1 = the worst
  for (auto& kv : groups) {
    std::vector<Player*>& v = kv.second;
    std::stable_sort(v.begin(), v.end(), [](const Player* a, const Player* b) { return a->ovr != b->ovr ? a->ovr > b->ovr : a->id < b->id; });
    const double n = static_cast<double>(v.size());
    const size_t xfN = static_cast<size_t>((std::max)(1.0, std::round(n * 0.03)));
    const size_t ssN = static_cast<size_t>((std::max)(2.0, std::round(n * 0.10)));
    const size_t stN = static_cast<size_t>((std::max)(4.0, std::round(n * 0.22)));
    for (size_t i = 0; i < v.size(); ++i) {
      v[i]->grade = i < xfN ? StarGrade::XFactor : i < ssN ? StarGrade::Superstar : i < stN ? StarGrade::Star : StarGrade::None;
      standing[v[i]] = static_cast<double>(i) / n;
    }
  }
  // every team has its stars: at least five graded men (its best by their standing at their positions) and a Superstar
  for (Team* t : teams) {
    auto best = [&](auto pred) {
      Player* b = nullptr;
      for (auto& p : t->roster)
        if (pred(*p) && (!b || standing[p.get()] < standing[b])) b = p.get();
      return b;
    };
    int graded = 0, top = 0;
    for (auto& p : t->roster) {
      graded += p->grade != StarGrade::None;
      top += static_cast<int>(p->grade) >= static_cast<int>(StarGrade::Superstar);
    }
    for (; graded < 5; graded++) {
      Player* p = best([](const Player& q) { return q.grade == StarGrade::None; });
      if (!p) break;
      p->grade = StarGrade::Star;
    }
    if (top == 0)
      if (Player* p = best([](const Player& q) { return q.grade == StarGrade::Star; })) p->grade = StarGrade::Superstar;
    // and no more than three X-Factors (the rest of a loaded roster are Superstars): the zone is a few men's
    std::vector<Player*> xf;
    for (auto& p : t->roster)
      if (p->grade == StarGrade::XFactor) xf.push_back(p.get());
    std::stable_sort(xf.begin(), xf.end(), [&](const Player* a, const Player* b) { return standing[a] < standing[b]; });
    for (size_t i = 3; i < xf.size(); i++) xf[i]->grade = StarGrade::Superstar;
  }
  for (Team* t : teams)
    for (auto& p : t->roster) autoLoadout(*p);
}

std::string loadoutString(const Player& p) {
  std::string s;
  if (p.xFactor != AbilityId::None) s += std::string("xf=") + abilityDef(p.xFactor).key;
  if (!p.abilities.empty()) {
    if (!s.empty()) s += ";";
    s += "ss=";
    for (size_t i = 0; i < p.abilities.size(); ++i) {
      if (i) s += ",";
      s += abilityDef(p.abilities[i]).key;
    }
  }
  return s.empty() ? std::string("none") : s;
}

void applyLoadoutString(Player& p, const std::string& s) {
  p.abilities.clear();
  p.xFactor = AbilityId::None;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ';')) {
    const size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = part.substr(0, eq);
    std::stringstream vs(part.substr(eq + 1));
    std::string key;
    while (std::getline(vs, key, ',')) {
      const AbilityDef* a = abilityByKey(key);
      if (!a) continue;
      if ((k == "xf") == (a->tier == AbilityTier::XFactor)) equipAbility(p, a->id);
    }
  }
}

// ------------------------------------------------------------------ in the play
bool clutchTime(const AbilitySituation& s) { return s.q >= 4 && s.clock <= 300 && std::abs(s.margin) <= 8; }
bool twoMinute(const AbilitySituation& s) { return (s.q == 2 || s.q >= 4) && s.clock <= 120; }

void addRatings(Ratings& r, const Ratings& d) {
  static double Ratings::*const K[] = {
      &R::speed, &R::accel, &R::agility, &R::strength, &R::stamina, &R::awareness, &R::carrying, &R::breakTackle,
      &R::elusiveness, &R::vision, &R::catching, &R::catchInTraffic, &R::routeRunning, &R::release, &R::throwPower,
      &R::accShort, &R::accMid, &R::accDeep, &R::throwOnRun, &R::poise, &R::decision, &R::passBlock, &R::runBlock,
      &R::passRush, &R::blockShed, &R::tackling, &R::pursuit, &R::playRec, &R::manCov, &R::zoneCov, &R::press,
      &R::ballSkills, &R::kickPower, &R::kickAcc};
  for (auto k : K)
    if (d.*k != 0) r.*k = (std::min)(110.0, r.*k + d.*k);
}

bool AbilityCtx::inZone(int id) const { return std::find(zoneIds.begin(), zoneIds.end(), id) != zoneIds.end(); }

void abilityFx(const Player& p, const AbilitySituation& s, bool inZone, AbilityFx& fx) {
  fx = AbilityFx{};
  auto on = [&fx]() { fx.any = true; };
  for (AbilityId id : p.abilities) {
    switch (id) {
      case A::PocketPoise: fx.pressAcc *= 0.35; fx.ballSafe += 0.3; addR(fx.add, &R::poise, 8); on(); break;
      case A::LaserLine: fx.shortAcc *= 0.88; addR(fx.add, &R::accShort, 3); on(); break;
      case A::MoonballTouch: fx.deepAcc *= 0.8; fx.armAdd += 3; addR(fx.add, &R::accDeep, 4); on(); break;
      case A::OffScript: fx.runAcc *= 0.5; fx.qbEscape -= 0.5; addR(fx.add, &R::throwOnRun, 8); on(); break;
      case A::FilmJunkie: addR(fx.add, &R::decision, 8); addR(fx.add, &R::awareness, 6); fx.ballSafe += 0.35; on(); break;
      case A::TwoMinuteMaestro:
        if (s.offense && (twoMinute(s) || clutchTime(s))) {
          for (auto k : {&R::accShort, &R::accMid, &R::accDeep}) addR(fx.add, k, 5);
          addR(fx.add, &R::decision, 6);
          addR(fx.add, &R::poise, 6);
          on();
        }
        break;
      case A::EscapeHatch: fx.qbEscape -= 1.3; on(); break;
      case A::BatteringRam: fx.powerAdj -= 0.3; fx.driveAdd += 0.2; on(); break;
      case A::AnkleTaker: fx.elusAdj -= 0.28; fx.moveAdj -= 0.3; on(); break;
      case A::ViseGrip: fx.fumble *= 0.45; on(); break;
      case A::HomeRunHitter: fx.topSpeed += 0.02; addR(fx.add, &R::accel, 3); on(); break;
      case A::SecondEffort: fx.wrapBreak += 0.06; fx.driveAdd += 0.25; on(); break;
      case A::ReceivingBack: addR(fx.add, &R::catching, 7); addR(fx.add, &R::routeRunning, 6); on(); break;
      case A::GlueHands: addR(fx.add, &R::catching, 8); fx.contested += 3; on(); break;
      case A::JumpBallAce: fx.contested += 14; fx.winBall += 0.4; addR(fx.add, &R::catchInTraffic, 8); on(); break;
      case A::RouteArtist: fx.sepLag += 0.08; addR(fx.add, &R::routeRunning, 4); on(); break;
      case A::JamBreaker: addR(fx.add, &R::release, 20); fx.sepLag += 0.04; on(); break;
      case A::CatchAndGo: fx.catchKeep += 0.12; on(); break;
      case A::RedZoneThreat:
        if (s.offense && s.los >= 80) {
          addR(fx.add, &R::catchInTraffic, 14);
          addR(fx.add, &R::catching, 6);
          fx.contested += 6;
          fx.winBall += 0.3;
          on();
        }
        break;
      case A::ChainMover:
        if (s.offense && s.down >= 3) {
          addR(fx.add, &R::catching, 10);
          addR(fx.add, &R::routeRunning, 8);
          fx.sepLag += 0.08;
          on();
        }
        break;
      case A::BrickWall: fx.holdShed *= 0.85; addR(fx.add, &R::passBlock, 2); on(); break;
      case A::RoadGrader: fx.runHold *= 0.88; addR(fx.add, &R::runBlock, 3); on(); break;
      case A::Anchor: addR(fx.add, &R::strength, 6); on(); break;
      case A::QuickFeet: addR(fx.add, &R::agility, 8); addR(fx.add, &R::passBlock, 2); on(); break;
      case A::Climber: fx.runHold *= 0.95; addR(fx.add, &R::runBlock, 3); addR(fx.add, &R::speed, 2); on(); break;
      case A::SafetyValve: addR(fx.add, &R::catching, 5); addR(fx.add, &R::catchInTraffic, 4); on(); break;
      case A::EdgeBender: fx.shed *= 1.3; addR(fx.add, &R::accel, 3); on(); break;
      case A::BullCharge: addR(fx.add, &R::strength, 8); fx.moveWin += 0.3; on(); break;
      case A::GapPlugger: fx.runShed *= 1.6; addR(fx.add, &R::blockShed, 4); on(); break;
      case A::Motor: fx.energyRegen *= 2.0; fx.shed *= 1.06; fx.runShed *= 1.1; addR(fx.add, &R::pursuit, 6); on(); break;
      case A::Finisher: fx.sackArm *= 1.4; fx.sackFinish += 0.5; addR(fx.add, &R::passRush, 3); on(); break;
      case A::SureTackler: fx.tackleAdj += 0.7; on(); break;
      case A::BallPunch: fx.hitFumble *= 1.8; on(); break;
      case A::Thumper: fx.tackleAdj += 0.35; fx.stopDrive += 0.7; fx.hitFumble *= 1.3; on(); break;
      case A::SidelineToSideline: addR(fx.add, &R::pursuit, 8); addR(fx.add, &R::speed, 3); on(); break;
      case A::Diagnoser: addR(fx.add, &R::playRec, 10); addR(fx.add, &R::awareness, 6); on(); break;
      case A::Blanket: fx.coverLag *= 0.7; addR(fx.add, &R::manCov, 3); on(); break;
      case A::ZoneReader: fx.zoneLag *= 0.65; addR(fx.add, &R::zoneCov, 3); on(); break;
      case A::PickArtist: fx.intAdj += 0.55; addR(fx.add, &R::catching, 6); on(); break;
      case A::SwatTeam: fx.pbu += 0.55; addR(fx.add, &R::ballSkills, 4); on(); break;
      case A::PressMaster: addR(fx.add, &R::press, 12); on(); break;
      case A::DeepRange: fx.zoneLag *= 0.85; addR(fx.add, &R::speed, 4); on(); break;
      case A::ClosingSpeed: fx.zoneLag *= 0.92; fx.coverLag *= 0.95; addR(fx.add, &R::pursuit, 6); addR(fx.add, &R::speed, 2); on(); break;
      case A::GetOff: fx.shed *= 1.08; addR(fx.add, &R::accel, 8); on(); break;
      default: break;  // (kick abilities: kickRatings)
    }
  }
  if (inZone) {
    switch (p.xFactor) {
      case A::IceVeins:
        fx.shortAcc *= 0.65; fx.deepAcc *= 0.7; fx.pressAcc *= 0.1; fx.runAcc *= 0.3;
        addR(fx.add, &R::decision, 10); addR(fx.add, &R::poise, 15); fx.ballSafe += 0.8; on(); break;
      case A::RocketArm: addR(fx.add, &R::throwPower, 8); fx.deepAcc *= 0.6; fx.armAdd += 8; on(); break;
      case A::Juggernaut:
        fx.powerAdj -= 0.7; fx.driveAdd += 0.5; fx.wrapBreak += 0.1;
        addR(fx.add, &R::breakTackle, 6); addR(fx.add, &R::strength, 6); on(); break;
      case A::Phantom: fx.elusAdj -= 0.9; fx.moveAdj -= 0.5; addR(fx.add, &R::elusiveness, 10); addR(fx.add, &R::agility, 6); on(); break;
      case A::Spotlight: fx.contested += 25; fx.winBall += 1.2; addR(fx.add, &R::catchInTraffic, 12); addR(fx.add, &R::catching, 12); on(); break;
      case A::Slipstream:
        fx.sepLag += 0.3; fx.catchKeep += 0.3; fx.topSpeed += 0.04;
        addR(fx.add, &R::routeRunning, 10); addR(fx.add, &R::release, 10); on(); break;
      case A::Fortress: fx.holdShed *= 0.3; fx.runHold *= 0.5; addR(fx.add, &R::passBlock, 8); on(); break;
      case A::Havoc:
        fx.shed *= 2.0; fx.moveWin += 1.2; fx.sackArm *= 1.8; fx.sackFinish += 0.8; fx.energyRegen *= 2.5;
        addR(fx.add, &R::passRush, 10); on(); break;
      case A::BlackHole: fx.runShed *= 2.4; fx.tackleAdj += 0.6; addR(fx.add, &R::blockShed, 12); on(); break;
      case A::Enforcer:
        fx.tackleAdj += 0.8; fx.stopDrive += 0.9; fx.hitFumble *= 2.5;
        addR(fx.add, &R::tackling, 8); addR(fx.add, &R::pursuit, 6); on(); break;
      case A::Lockdown: fx.coverLag *= 0.45; fx.pbu += 1.0; fx.intAdj += 0.4; addR(fx.add, &R::manCov, 10); addR(fx.add, &R::press, 8); on(); break;
      case A::LastLine: fx.zoneLag *= 0.5; fx.intAdj += 0.8; fx.pbu += 0.5; addR(fx.add, &R::speed, 4); addR(fx.add, &R::zoneCov, 10); on(); break;
      default: break;
    }
  }
}

Ratings kickRatings(const Player& k, bool punt, const AbilitySituation& s, bool inZone) {
  Ratings r = k.r;
  for (AbilityId id : k.abilities) {
    if (id == A::BigLeg) r.kickPower += 6;
    if (id == A::SteadyFoot) r.kickAcc += 6;
    if (id == A::IceWater && !punt && clutchTime(s)) r.kickAcc += 12;
    if (id == A::PinPoint && punt) r.kickAcc += 10;
  }
  if (inZone && k.xFactor == A::Automatic && !punt) {
    r.kickAcc += 15;
    r.kickPower += 5;
  }
  r.kickAcc = (std::min)(r.kickAcc, 110.0);
  r.kickPower = (std::min)(r.kickPower, 110.0);
  return r;
}

// ------------------------------------------------------------------ the zone
const ZoneState* AbilityState::zone(const Player& p) const {
  const auto it = zones.find(p.id);
  return it == zones.end() ? nullptr : &it->second;
}
bool AbilityState::active(const Player& p) const {
  const ZoneState* z = zone(p);
  return z && z->active && p.xFactor != AbilityId::None;
}
std::vector<int> AbilityState::activeIds() const {
  std::vector<int> out;
  for (const auto& kv : zones)
    if (kv.second.active) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

namespace {
/** One step of a man's zone: gain (pips toward the trigger), a defeat (out of the zone), a streak broken (the count
 *  starts again: "straight" triggers), a strike (two-strike defeats). */
struct ZoneStep {
  int gain = 0;
  bool defeat = false;
  bool streakBroken = false;
  bool strike = false;
  bool strikeClear = false;
  std::string why;
};

void stepZone(AbilityState& st, const Player& p, const ZoneStep& z, const std::string& abbr) {
  const AbilityDef& a = abilityDef(p.xFactor);
  ZoneState& s = st.zones[p.id];
  if (z.strikeClear) s.defeats = 0;
  if (s.active) {
    bool out = z.defeat;
    if (z.strike && ++s.defeats >= 2) out = true;
    if (out) {
      s.active = false;
      s.progress = 0;
      s.defeats = 0;
      st.events.push_back(AbilityEvent{&p, a.id, false, abbr, z.why});
      st.history.push_back(st.events.back());
    }
    return;
  }
  if (z.defeat || z.streakBroken) s.progress = 0;
  if (z.strike) {
    if (++s.defeats >= 2) {
      s.progress = 0;
      s.defeats = 0;
    }
  }
  s.progress += z.gain;
  if (s.progress >= a.need && a.need > 0) {
    s.active = true;
    s.progress = a.need;
    s.defeats = 0;
    s.entered++;
    st.events.push_back(AbilityEvent{&p, a.id, true, abbr, a.trigger});
    st.history.push_back(st.events.back());
  }
}
}  // namespace

void AbilityState::afterPlay(const PlaySim& sim, const PlayResult& res, const std::string& offAbbr, const std::string& defAbbr) {
  const bool dropback = res.kind == PlayKind::Pass || res.kind == PlayKind::Sack || res.passer != nullptr;
  const bool sack = res.kind == PlayKind::Sack;
  const bool isRun = res.kind == PlayKind::Run || res.kind == PlayKind::Scramble;
  const bool passPlay = res.kind == PlayKind::Pass;
  // who covered the target: the man defender on him, else the defender nearest him at the end
  const Ent* tgtEnt = nullptr;
  for (const Ent* e : sim.off)
    if (res.target && e->p == res.target) tgtEnt = e;
  const Ent* cover = nullptr;
  if (tgtEnt) {
    double bd = 1e9;
    for (const Ent* d : sim.def) {
      if (d->man == tgtEnt) {
        cover = d;
        break;
      }
      const double dd = dist(d->pos, tgtEnt->pos);
      if (dd < bd && dd < 4.0) {
        bd = dd;
        cover = d;
      }
    }
  }
  auto tackled = [&res](const Player* p) { return std::find(res.tacklers.begin(), res.tacklers.end(), p) != res.tacklers.end(); };
  auto brokenBy = [&sim](const Player* p, bool asCarrier) {
    int n = 0;
    for (const PlayEvent& e : sim.events)
      if ((e.type == "broken" || e.type == "divemiss") && e.a && e.b && (asCarrier ? e.a->p : e.b->p) == p) n++;
    return n;
  };
  for (const Ent* e : sim.ents) {
    const Player& p = *e->p;
    if (p.xFactor == AbilityId::None) continue;
    ZoneStep z;
    const bool off = e->side == Side::Off;
    switch (p.xFactor) {
      case A::IceVeins:
        if (res.passer != &p && !(sack && sim.qb == e)) continue;
        if (sack || res.interception) { z.defeat = true; z.why = sack ? "sacked" : "intercepted"; }
        else if (res.complete && res.yards >= 15) z.gain = 1;
        else z.streakBroken = true;
        break;
      case A::RocketArm:
        if (res.passer != &p) continue;
        if (res.interception) { z.defeat = true; z.why = "intercepted"; }
        else if (res.incomplete) { z.strike = true; z.why = "2 straight incompletions"; }
        else if (res.complete) { z.strikeClear = true; if (res.airYards >= 20) z.gain = 1; }
        break;
      case A::Juggernaut:
        if (res.rusher != &p || !isRun) continue;
        if (res.fumbleLost && res.fumbleBy == &p) { z.defeat = true; z.why = "fumbled"; }
        else if (res.yards <= 1) { z.strike = true; z.why = "stuffed twice"; }
        else { z.strikeClear = true; if (res.yards >= 5) z.gain = 1; }
        break;
      case A::Phantom: {
        const bool carried = res.rusher == &p || (res.complete && res.receiver == &p);
        if (!carried) continue;
        z.gain = brokenBy(&p, true);
        if ((res.fumbleLost && res.fumbleBy == &p) || res.yards < 0) { z.defeat = true; z.why = res.yards < 0 ? "tackled for a loss" : "fumbled"; }
        break;
      }
      case A::Spotlight:
        if (res.target != &p) continue;
        if (res.interception) { z.defeat = true; z.why = "intercepted"; }
        else if (res.incomplete) { z.strike = true; z.why = "2 straight incompletions"; }
        else if (res.complete && res.receiver == &p) { z.strikeClear = true; if (res.yards >= 15) z.gain = 1; }
        break;
      case A::Slipstream:
        if (res.target != &p) continue;
        if (res.fumbleLost && res.fumbleBy == &p) { z.defeat = true; z.why = "fumbled"; }
        else if (res.incomplete || res.interception) { z.strike = true; z.why = "2 straight incompletions"; }
        else if (res.complete && res.receiver == &p) { z.strikeClear = true; z.gain = 1; }
        break;
      case A::Fortress:
        if (!off || !dropback) continue;
        if (sack) { z.defeat = true; z.streakBroken = true; z.why = "a sack"; }
        else z.gain = 1;
        break;
      case A::Havoc:
        if (off) continue;
        if (res.yards >= 20 && !res.turnover) { z.defeat = true; z.why = "a 20+ yard play"; }
        else if (sack && (res.sackedBy == &p || tackled(&p))) z.gain = 2;
        else if (isRun && res.yards < 0 && tackled(&p)) z.gain = 1;
        break;
      case A::BlackHole:
        if (off) continue;
        if (isRun && res.yards >= 10) { z.defeat = true; z.why = "a 10+ yard run"; }
        else if (isRun && res.yards <= 2 && tackled(&p)) z.gain = 1;
        break;
      case A::Enforcer:
        if (off) continue;
        if (brokenBy(&p, false) > 0) { z.defeat = true; z.why = "a broken tackle"; }
        else if (res.td == Side::Off) { z.defeat = true; z.why = "a touchdown allowed"; }
        else if (tackled(&p)) z.gain = 1;
        break;
      case A::Lockdown:
        if (off || !passPlay) continue;
        if (cover == e && (res.td == Side::Off || (res.complete && res.yards >= 15))) { z.defeat = true; z.why = res.td ? "a touchdown on him" : "a big catch on him"; }
        else if (res.intBy == &p || res.pbuBy == &p || (cover == e && (res.incomplete || res.interception))) z.gain = 1;
        break;
      case A::LastLine:
        if (off) continue;
        if (passPlay && res.complete && res.yards >= 30) { z.defeat = true; z.why = "a 30+ yard pass"; }
        else if (res.intBy == &p || (res.fumbleLost && res.forcedBy == &p)) z.gain = 2;
        else if (res.pbuBy == &p) z.gain = 1;
        break;
      default: continue;
    }
    if (!z.gain && !z.defeat && !z.streakBroken && !z.strike && !z.strikeClear) continue;
    stepZone(*this, p, z, off ? offAbbr : defAbbr);
  }
}

void AbilityState::afterKick(const Player& k, bool made, bool fieldGoal, const std::string& abbr) {
  if (k.xFactor != A::Automatic) return;
  ZoneStep z;
  if (made) {
    if (!fieldGoal) return;
    z.gain = 1;
  }
  else {
    z.defeat = true;
    z.streakBroken = true;
    z.why = "a missed kick";
  }
  stepZone(*this, k, z, abbr);
}

}  // namespace tag
