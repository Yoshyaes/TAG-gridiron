// Adaptive play calling: what each side has called this game and what it got for it (a Tendency per team), how a CPU
// caller leans on that (both sides of the ball), and the coach's suggestions on the play-call screen.
//
//  - Tendencies are kept for every scrimmage play (Game::finishPlay), whoever called it: the CPU reads the human's calls
//    and the human's coach reads the CPU's. A defense reads only what it can see before the snap: the situation, the
//    formation, and what the offense has done so far - never the call itself.
//  - The CPU's lean is relative to the league's own mix (kPrior / kDefPrior, measured on CPU games with the lean off), so
//    a team that calls like the league gets no lean and the league's play mix stays where it is calibrated; a team that
//    shows a tendency gets answered. Every weight moves by at most CallTune::adaptMax either way, the run/pass call by
//    at most adaptPass. No RNG is drawn here (the sim stays deterministic: same seed, same game).
//  - Matchups (kMatch): how much better or worse each kind of offensive call does against each kind of defensive call
//    than its average, in expected points a play, from the sim itself (Tools/CallAdaptCal.cpp, controlled plays).
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "Playbook.h"

namespace tag {

class Game;
struct PlayResult;

struct CallTune {
  double adapt = 1;         // 1: the CPU callers lean on the tendencies; 0: off (the calls as before)
  double adaptDefK = 1.0;   // x the defense's lean (its counters to what the offense shows)
  double adaptOffK = 1.0;   // x the offense's lean (what works, and what beats the looks the defense shows)
  double adaptMax = 2.5;    // a call's weight moves at most this factor either way
  double adaptPass = 0.06;  // the run/pass call moves at most this much
  double adaptPrior = 8;    // the league's mix counts as this many calls in every estimate (shrinkage)
  double adaptSpamK = 1;    // x the defense's answer to a spammed concept (how far past its share of the look's calls,
                            // times how well it has worked, times what its measured matchups say takes it away)
  double runMix = 1;       // 1: the CPU's run calls weighed by concept family toward the NFL's mix (runFamilyW); 0: off
};
CallTune& callTune();
/** --set / -TagTune= names: adapt, adaptDefK, adaptOffK, adaptMax, adaptPass, adaptPrior, runMix. */
bool setCallTune(const std::string& name, double v);

// ---- the situations, the kinds of offensive calls, the kinds of defensive calls
constexpr int kSitN = 7;  // 1st; 2nd & 1-3 / 4-6 / 7+; 3rd-4th & 1-2 / 3-6 / 7+
constexpr int kClsN = 8;  // inside run, outside run, option / RPO, quick pass, intermediate pass, deep pass, play action, screen
constexpr int kFamN = 8;  // man pressure, zone pressure, man single high, 2-man, Cover 3, Cover 2, quarters, drop 8 / prevent
int sitBucket(int down, double toGo);
const char* sitName(int b);  // "3rd & long"
int offClass(const OffPlay& p);
const char* offClassName(int c);
int defFamily(const DefCall& d);
const char* defFamilyName(int f);  // as a coach says it: "the blitz", "Cover 3"
bool isPassClass(int c);
/** A run's concept family: 0 inside zone (and the zone RPOs), 1 outside zone / stretch, 2 gap (power, counter, duo,
 *  dive, iso, trap, lead), 3 draw, 4 toss / sweep (jet, reverse), 5 QB runs (designed keeps, sneaks, read option,
 *  wildcat); -1 not a run. */
constexpr int kRunFamN = 6;
int runFamily(const OffPlay& p);
const char* runFamilyName(int f);
/** The CPU run call's factor for a family (1 with CallTune::runMix 0). */
double runFamilyW(int f);
/** kMatch: what a kind of offensive call gains against a kind of defensive call beyond its average (EPA a play). */
double matchupEpa(int cls, int fam);
/** The same for one concept (conceptKey), measured on its own plays: only the edges past two standard errors (the rest
 *  0); a concept not in the table, or with no edge past the noise, gets its kind's (matchupEpa). */
double conceptMatchup(const std::string& key, int cls, int fam);

/** What a play was worth to the offense in expected points (the play's end less its snap), and whether it was a
 *  success (40 % of the distance on 1st down, 60 % on 2nd, all of it on 3rd and 4th). */
double playEpa(int down, double toGo, double los, const PlayResult& res);
bool playSuccess(int down, double toGo, const PlayResult& res);

/** One concept's (or one play's, for a play with no concept) line this game. */
struct ConceptLine {
  int cls = 0;
  int n = 0;
  int yds = 0;
  int succ = 0;
  double epa = 0;
  std::array<int, kFamN> vsN{};      // calls of it against each kind of defense
  std::array<double, kFamN> vsEpa{};
};

/** A team's calls this game: its offense's (by situation, by formation, by concept) and its defense's. */
struct Tendency {
  // offense
  std::array<int, kSitN> offN{};
  std::array<int, kSitN> offPass{};
  std::array<std::array<int, kClsN>, kSitN> offCls{};
  std::map<std::string, std::array<int, kClsN>> formCls;  // formation id -> calls by kind
  std::map<std::string, std::map<std::string, int>> formConcept;  // formation id -> concept -> calls
  std::array<int, kClsN> clsN{};
  std::array<double, kClsN> clsEpa{};
  std::map<std::string, ConceptLine> concepts;
  // defense
  std::array<int, kSitN> defN{};
  std::array<std::array<int, kFamN>, kSitN> defFam{};
  std::array<int, kFamN> famN{};
  std::array<double, kFamN> famEpa{};  // what the offense got against each kind of call

  int offCalls() const;
  int defCalls() const;
};
/** The concept a play is filed under (its conceptName, else its id). */
std::string conceptKey(const OffPlay& p);
/** One scrimmage play into both teams' lines. */
void noteCall(Tendency& off, Tendency& def, int down, double toGo, const OffPlay& op, const DefCall& dc, double epa, bool succ, int yards);

/** The defense's read of the offense before it calls: a score per kind of defensive call (expected points it takes
 *  away against what this offense has shown, beyond what the league's mix would) and the concept it is answering. */
struct DefRead {
  std::array<double, kFamN> score{};
  std::array<double, kClsN> expect{};  // the offense's mix expected here (shrunk to the league's)
  std::string spam;                     // the concept the offense keeps coming back to from this look ("" none)
  int spamN = 0, spamYds = 0;
  int formN = 0, formRun = 0;           // calls from this formation, of them runs
};
DefRead readOffense(const Tendency& off, int sit, const std::string& formation);
/** The CPU defense's weight factor for a call (1 with the lean off). */
double defAdaptMul(const DefRead& r, const DefCall& d);

/** The offense's read of the defense (and of its own day): a score per kind of offensive call, and the run/pass lean. */
struct OffRead {
  std::array<double, kClsN> score{};
  std::array<double, kFamN> expect{};  // the defense's mix expected here
  double passShift = 0;
};
OffRead readDefense(const Tendency& self, const Tendency& oppDef, int sit);
/** The CPU offense's weight factor for a play (1 with the lean off). */
double offAdaptMul(const OffRead& r, const Tendency& self, const OffPlay& p);

// ---- the coach's suggestions (the play-call screen): ranked by the CPU caller's own weights, picked with an Rng of
// their own seeded from the game's situation (the game's RNG is never drawn), each with a line on why
struct CallSuggestion {
  const OffPlay* off = nullptr;
  const DefCall* def = nullptr;
  std::string reason;  // "3RD & 7  -  beats the blitz: 4 of 6 on 3rd & medium"
  double weight = 0;   // the CPU caller's chance of the call
};
std::vector<CallSuggestion> suggestOffense(const Game& g, int n = 3);
/** formation: the one the offense has come out in ("" unknown). */
std::vector<CallSuggestion> suggestDefense(const Game& g, const std::string& formation, int n = 3);
/** The tendencies the coach reads out on the call screen (two or three short lines; empty early in a game). */
std::vector<std::string> coachRead(const Game& g, bool offense, const std::string& formation);

}  // namespace tag
