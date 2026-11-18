// The depth chart and Create-a-Player (engine independent; implemented in League.cpp so every tool that links the
// league links these too).
//
// Depth chart: Madden's chart, slot for slot - the eleven a side by their spots (LT and RT, LE and RE, the three
// backers, the two safeties), the specialists (K, P, the kickoff specialist, the long snapper, both returners) and
// the situational subs (3rd down back, power back, slot receiver, the pass-rush ends and tackle, the sub linebackers,
// the slot corner, the nose tackle). Each slot is an ordered list of men; anyone may be listed at any slot (a corner
// at free safety plays free safety).
//
// The sim reads a slot's order only once the user has set it (Team::chart[slot] non-empty): an untouched slot is the
// coach's - the old pick, best man by OVR with the fatigue rotation - so a league nobody has touched plays exactly as
// it did. A set slot's starters start; the rotation still spells a worn-down starter with the man below him.
#pragma once

#include <string>
#include <vector>

#include "Types.h"

namespace tag {

enum class DSlot : uint8_t {
  // offense
  QB, HB, FB, WR, TE, LT, LG, C, RG, RT,
  // defense
  LE, RE, DT, LOLB, MLB, ROLB, CB, FS, SS,
  // special teams
  K, P, KOS, LS, KR, PR,
  // situational
  TDRB, PWHB, SWR, RLE, RRE, RDT, SUBLB, SLCB, NT,
  Count
};
constexpr int DSLOTS = static_cast<int>(DSlot::Count);

enum class DGroup : uint8_t { Offense, Defense, Special, Situational };

struct DSlotInfo {
  DSlot slot;
  const char* abbr;     // "LOLB"
  const char* name;     // "Left Outside Linebacker"
  DGroup group;
  Pos natural;          // the position the slot is rated as (a man's OVR there: computeOvr(natural, his ratings))
  int starters;         // how many of the list start ("WR": 3)
  const char* when;     // when the slot plays ("3rd down", "Nickel and dime", "Every snap")
};

const DSlotInfo& dslotInfo(DSlot s);
const char* dgroupName(DGroup g);
/** "LOLB" -> DSlot::LOLB (false: no such slot). */
bool dslotByAbbr(const std::string& abbr, DSlot& out);

/** The coach's order for a slot: the men of its position(s), the slot's own share first (LT the best tackle, RT the
 *  second...), then the rest by the slot's rating. What the screen shows for a slot nobody has set. */
std::vector<Player*> autoChart(const Team& t, DSlot s);
/** The slot as it stands: the user's order (the men still on the roster) if set, else the coach's. */
std::vector<Player*> chartOrder(const Team& t, DSlot s);
/** Whether the user has set the slot. */
inline bool chartSet(const Team& t, DSlot s) {
  const size_t i = static_cast<size_t>(s);
  return i < t.chart.size() && !t.chart[i].empty();
}
/** Sets a slot's order (the ids of `order`); an empty order gives the slot back to the coach. */
void setChart(Team& t, DSlot s, const std::vector<Player*>& order);
void clearChart(Team& t, DSlot s);
void clearAllCharts(Team& t);
/** A man's rating at a slot (his ratings through the slot's position formula). */
int slotOvr(const Player& p, DSlot s);
/** The starters a slot lists (the first dslotInfo(s).starters healthy men of chartOrder). */
std::vector<Player*> chartStarters(const Team& t, DSlot s);
/** A new man (a signing, a created player): put at the bottom of every set slot of his position, so the user's order
 *  stands and he is on it. */
void chartAddPlayer(Team& t, const Player& p);
/** A man leaving the team: off every list. */
void chartRemovePlayer(Team& t, const Player& p);

/** The sim's pick for a formation role, by the chart: the slot(s) the role reads in this situation, most specific
 *  first ("RB" on 3rd down: the 3rd down back's slot, then the halfback's). Only set slots are listed (an empty
 *  result: the old pick). `front` is the defense's Front as an int (-1 offense). */
std::vector<DSlot> roleSlots(const Team& t, const std::string& role, int front, double down, double toGo, double los);
/** Specialists by the chart: the kicker on field goals and tries, the kickoff man (KOS, else K), the punter, the
 *  returners, the long snapper. nullptr: nobody set (the caller's old pick). */
Player* chartSpecialist(const Team& t, DSlot s);

// ------------------------------------------------------------------ Create-a-Player
/** A position's archetypes (Madden's "Field General", "Strong Arm"...): each leans the position's template. */
struct CapArchetype {
  const char* name;
  const char* blurb;
  std::vector<std::pair<double Ratings::*, double>> lean;  // added to the position template
  double heightIn, weightLb;                               // his frame
};
const std::vector<CapArchetype>& capArchetypes(Pos pos);
/** The ratings a created man starts from: his position's template, the archetype's lean, at a talent level (0 a
 *  league average starter; +10 an elite one, -10 a backup), no randomness. */
Ratings capRatings(Pos pos, int archetype, double talent);
/** Every rating by name (the screen's rows, the save's keys): abbr ("SPD"), name ("Speed"), the member. */
struct RatingDef {
  const char* abbr;
  const char* name;
  double Ratings::*m;
  const char* group;  // "Physical", "Ball carrier", ...
};
const std::vector<RatingDef>& ratingDefs();
/** The ratings that matter most at a position (the chart's columns, the card's key ratings), most important first. */
std::vector<const RatingDef*> keyRatings(Pos pos, size_t n = 6);
/** A depth chart slot's key ratings: its own formula's (a 3rd down back: catching, routes, pass block), else its
 *  position's. */
std::vector<const RatingDef*> slotKeyRatings(DSlot s, size_t n = 6);
/** A jersey number for a position nobody on the team wears (the rules' ranges first), -1 if none is free. */
int freeNumber(const Team& t, Pos pos, int prefer = -1);
bool numberFree(const Team& t, int number, const Player* except = nullptr);
/** League rules for a position's numbers (0..99 ranges). */
bool numberLegal(Pos pos, int number);
constexpr size_t ROSTER_MAX = 53;
/** The league's name pools (the generator's), for a created man's name. */
const std::vector<std::string>& firstNamePool();
const std::vector<std::string>& lastNamePool();

}  // namespace tag
