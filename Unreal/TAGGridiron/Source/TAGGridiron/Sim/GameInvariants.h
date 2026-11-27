// Per-play rules invariants for a Game (header only, no engine dependency).
// The rules, clock and game-flow long tail - what a studio's QA keeps finding after ship (a timeout on a safety punt, a
// declined penalty that restarts the clock) - checked after every action of a game: snapshot the game, take one action
// (Game::simStep, or a kickoff / play / try driven by hand), then afterStep() compares the two states and the log lines
// the action wrote. Each broken rule is one line in `violations`, named by the rule, with the clock and the lines.
//
//   GameInvariants inv(game);
//   while (!game.isFinal()) { game.simStep(); inv.afterStep(game); }
//
// What it holds the game to (see afterStep for the details):
//   score     - only goes up, by 1 (a kicked try), 2 (a safety or a two-point try), 3 (a field goal) or 6 (a
//               touchdown), one team at a time, each with the log tag that says why
//   try       - a try (extra point / two-point) only right after a touchdown, by the team that scored it
//   downs     - on a scrimmage down: down 1-4, toGo > 0 and no farther than the goal, ballOn inside the field; a down
//               that goes up keeps the line to gain, a new series is 1st and min(10, to the goal) and has a reason
//   clock     - never runs backwards within a quarter; a new quarter starts at its full length (OT: 10:00)
//   timeouts  - 0-3 (2 in OT), down by one only with a "Timeout" line, back to 3 at the half
//   ball      - changes hands only for a reason (a kickoff, a turnover, a punt, a score, a safety, the half)
//   safety    - the team scored on free-kicks next, and the free kick is spent on that kick (never carried into a half)
//   warning   - one two-minute warning a half, in the 2nd and 4th quarters only
//   untimed   - a snap at 0:00 only after a defensive foul accepted at 0:00, and such a foul always gets its down
//   overtime  - ends only when its rules allow it (both teams have had the ball, a safety or a return score, time)
//   final     - no snap after the final whistle; the game ends tied only at the end of overtime
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Game.h"

namespace tag {

class GameInvariants {
 public:
  struct Snap {
    int q = 1;
    double clock = 0;
    GamePhase phase = GamePhase::Kickoff;
    TeamKey poss = TeamKey::Away;
    TeamKey kicking = TeamKey::Home;
    int down = 1;
    double toGo = 10;
    double ballOn = 25;
    PerTeam<int> score;
    PerTeam<int> timeouts;
    bool untimed = false;
    bool safetyKick = false;
    int otPossDone = 0;
    size_t logN = 0;
  };
  static Snap snap(const Game& g) {
    Snap s;
    s.q = g.q;
    s.clock = g.clock;
    s.phase = g.phase;
    s.poss = g.poss;
    s.kicking = g.kicking;
    s.down = g.down;
    s.toGo = g.toGo;
    s.ballOn = g.ballOn;
    s.score = g.score;
    s.timeouts = g.timeouts;
    s.untimed = g.untimed;
    s.safetyKick = g.safetyKick;
    s.otPossDone = g.otPossDone;
    s.logN = g.log.size();
    return s;
  }

  explicit GameInvariants(const Game& g) : prev_(snap(g)) {}

  /** Each broken rule: "<rule>: <what> [Qq m:ss] <the action's log lines>". */
  std::vector<std::string> violations;
  /** Re-take the snapshot without checking (after a test has set the game's state by hand). */
  void reset(const Game& g) { prev_ = snap(g); }

  /** Check one action of the game against the state before it. Returns the number of rules it broke. */
  int afterStep(const Game& g) {
    const Snap p = prev_;
    const Snap c = snap(g);
    prev_ = c;
    const size_t before = violations.size();
    lines_.clear();
    for (size_t i = p.logN; i < g.log.size(); ++i) lines_.push_back(&g.log[i]);
    const bool tied = c.score.home == c.score.away;

    // ---- final: nothing happens after the final whistle
    if (p.phase == GamePhase::Final) {
      if (c.logN != p.logN || c.score.home != p.score.home || c.score.away != p.score.away || c.q != p.q) fail(g, p, "final", "the game went on after the final whistle");
      return static_cast<int>(violations.size() - before);
    }
    if (c.phase == GamePhase::Final) {
      if (c.q < 4) fail(g, p, "final", "the game ended in quarter " + std::to_string(c.q));
      if (tied && c.q != 5) fail(g, p, "final", "the game ended tied before overtime");
      if (tied && c.q == 5 && c.clock > 0) fail(g, p, "overtime", "overtime ended tied with time on the clock");
    }

    // ---- score
    int scorer = -1;
    for (TeamKey k : {TeamKey::Home, TeamKey::Away}) {
      const int d = c.score[k] - p.score[k];
      if (d == 0) continue;
      if (d < 0) fail(g, p, "score", std::string(teamKeyName(k)) + " score went down by " + std::to_string(-d));
      if (d != 1 && d != 2 && d != 3 && d != 6) fail(g, p, "score", std::string(teamKeyName(k)) + " scored " + std::to_string(d) + " points in one action");
      if (scorer >= 0) fail(g, p, "score", "both teams scored in one action");
      scorer = static_cast<int>(k);
      const bool wasTry = p.phase == GamePhase::Pat;
      if (wasTry && d != 1 && d != 2) fail(g, p, "score", std::to_string(d) + " points on a try");
      if (d == 6 && !tagged(LogTag::Td)) fail(g, p, "score", "6 points with no touchdown line");
      if (d == 3 && !(tagged(LogTag::Fg) && p.phase == GamePhase::Scrimmage)) fail(g, p, "score", "3 points with no field goal line");
      if (d == 2 && !wasTry && !tagged(LogTag::Safety)) fail(g, p, "score", "2 points with no safety line");
      if (d == 1 && !(wasTry && k == p.poss && says("extra point is GOOD"))) fail(g, p, "score", "1 point that was not a good extra point by the scoring team");
    }
    const TeamKey scoredBy = scorer == static_cast<int>(TeamKey::Home) ? TeamKey::Home : TeamKey::Away;

    // ---- try: only after a touchdown, by its team
    if (c.phase == GamePhase::Pat && p.phase != GamePhase::Pat && !(scorer >= 0 && scoredBy == c.poss && c.score[c.poss] - p.score[c.poss] == 6))
      fail(g, p, "try", "a try with no touchdown before it (or by the other team)");
    if (p.phase == GamePhase::Pat && c.phase == GamePhase::Pat) fail(g, p, "try", "a second try after one touchdown");
    if (p.phase == GamePhase::Pat && c.poss != p.poss && !says("Halftime.") && !says("Overtime!")) fail(g, p, "ball", "the ball changed hands on a try");

    // ---- clock and quarters
    if (c.clock < 0) fail(g, p, "clock", "the clock is below zero");
    if (c.q == p.q) {
      if (c.clock > p.clock + 1e-9) fail(g, p, "clock", "the clock ran backwards from " + fmtClock(p.clock) + " to " + fmtClock(c.clock));
    } else {
      if (c.q != p.q + 1 || c.q > 5) fail(g, p, "clock", "quarter " + std::to_string(p.q) + " went to " + std::to_string(c.q));
      // (the runoff before the snap that never came ran it out in this same action: the end is logged at 0:00)
      for (const PlayLog* l : lines_)
        if (l->desc.rfind("End of ", 0) == 0 && l->clock > 0) fail(g, p, "clock", "a quarter ended with " + fmtClock(l->clock) + " left");
      const double full = c.q == 5 ? 600 : g.quarterLen;
      if (c.clock != full) fail(g, p, "clock", "quarter " + std::to_string(c.q) + " started at " + fmtClock(c.clock));
    }

    // ---- timeouts
    for (TeamKey k : {TeamKey::Home, TeamKey::Away}) {
      const int cap = c.q == 5 ? 2 : 3;
      if (c.timeouts[k] < 0 || c.timeouts[k] > cap) fail(g, p, "timeouts", std::string(teamKeyName(k)) + " has " + std::to_string(c.timeouts[k]) + " timeouts");
      const bool reset = (p.q == 2 && c.q == 3) || (p.q == 4 && c.q == 5);
      if (reset) {
        if (c.timeouts[k] != cap) fail(g, p, "timeouts", std::string(teamKeyName(k)) + " timeouts not reset to " + std::to_string(cap) + " at the " + (c.q == 3 ? "half" : "start of overtime"));
        continue;
      }
      int called = 0;
      const std::string who = "Timeout " + g.teamOf(k).abbr + " (";
      for (const PlayLog* l : lines_) called += l->desc.rfind(who, 0) == 0;
      if (p.timeouts[k] - c.timeouts[k] != called)
        fail(g, p, "timeouts", std::string(teamKeyName(k)) + " timeouts " + std::to_string(p.timeouts[k]) + " -> " + std::to_string(c.timeouts[k]) + " with " +
                                   std::to_string(called) + " timeout lines");
    }

    // ---- the half: the team that received the opening kickoff kicks the second half
    if (p.q == 2 && c.q == 3 && !(c.phase == GamePhase::Kickoff && c.kicking == g.openingReceiver && c.poss == other(c.kicking)))
      fail(g, p, "ball", "the second half did not start with the opening receiver kicking off");
    if ((c.q == 3 && p.q == 2) || (c.q == 5 && p.q == 4))
      if (c.safetyKick) fail(g, p, "safety", "a safety's free kick carried over into a new half / overtime");

    // ---- the ball changes hands only for a reason
    if (c.poss != p.poss && p.phase != GamePhase::Pat) {
      const bool kick = p.phase == GamePhase::Kickoff;
      const bool why = kick || tagged(LogTag::Turnover) || tagged(LogTag::Td) || tagged(LogTag::Safety) || says(" punts ") || says("Halftime.") || says("Overtime!");
      if (!why) fail(g, p, "ball", "possession changed with no turnover, kick, score or safety");
    }
    if (p.phase == GamePhase::Kickoff && c.phase != GamePhase::Kickoff && c.q == p.q && c.phase != GamePhase::Final) {
      // a kickoff: the receiving team has it, unless the kicking team got it back (an onside kick, a muff)
      if (c.poss != other(p.kicking) && !tagged(LogTag::Turnover)) fail(g, p, "ball", "after the kickoff the kicking team has the ball with no recovery");
      if (c.safetyKick) fail(g, p, "safety", "the free kick after a safety was not spent on the kickoff");
    }

    // ---- a safety: the team scored on kicks
    if (tagged(LogTag::Safety) && c.phase != GamePhase::Final) {
      if (!(c.phase == GamePhase::Kickoff && c.safetyKick && scorer >= 0 && c.kicking == other(scoredBy) && c.poss == scoredBy))
        fail(g, p, "safety", "after a safety the team scored on does not free-kick from its 20");
    }
    if (c.safetyKick && c.phase != GamePhase::Kickoff && c.phase != GamePhase::Final) fail(g, p, "safety", "a pending safety free kick outside a kickoff");

    // ---- scrimmage downs
    if (c.phase == GamePhase::Scrimmage) {
      if (c.down < 1 || c.down > 4) fail(g, p, "downs", "down " + std::to_string(c.down));
      if (!(c.toGo > 0)) fail(g, p, "downs", "toGo " + num(c.toGo));
      if (!(c.ballOn > 0 && c.ballOn < 100)) fail(g, p, "downs", "ballOn " + num(c.ballOn));
      if (c.ballOn + c.toGo > 100 + 1e-6) fail(g, p, "downs", "the line to gain is past the goal line (ballOn " + num(c.ballOn) + ", toGo " + num(c.toGo) + ")");
      const double Lp = p.ballOn + p.toGo, Lc = c.ballOn + c.toGo;
      const bool same = std::fabs(Lc - Lp) < 1e-6;
      const bool newSeries = !(p.phase == GamePhase::Scrimmage && p.poss == c.poss) || (c.down == 1 && (p.down != 1 || !same));
      if (newSeries) {
        if (c.down != 1 || std::fabs(c.toGo - (std::min)(10.0, 100 - c.ballOn)) > 1e-9)
          fail(g, p, "downs", "a new series is " + std::to_string(c.down) + " & " + num(c.toGo) + " at " + num(c.ballOn) + ", not 1st & min(10, to the goal)");
        const bool kept = p.phase == GamePhase::Scrimmage && p.poss == c.poss;
        if (kept && !(c.ballOn >= Lp - 1e-6 || tagged(LogTag::Penalty) || says("FIRST DOWN") || says("MUFFED")))
          fail(g, p, "downs", "a first down short of the line to gain (" + num(c.ballOn) + " < " + num(Lp) + ") with no penalty");
      } else if (c.down == p.down + 1 || c.down == p.down) {
        if (!same) fail(g, p, "downs", "the line to gain moved within a series (" + num(Lp) + " -> " + num(Lc) + ")");
      } else {
        fail(g, p, "downs", "down " + std::to_string(p.down) + " went to " + std::to_string(c.down));
      }
    }

    // ---- the two-minute warning: once a half, in the 2nd and 4th quarters
    for (const PlayLog* l : lines_)
      if (l->desc == "Two-minute warning.") {
        if (l->q != 2 && l->q != 4) fail(g, p, "warning", "a two-minute warning in quarter " + std::to_string(l->q));
        else if (++warn_[l->q] > 1) fail(g, p, "warning", "a second two-minute warning in quarter " + std::to_string(l->q));
      }

    // ---- the untimed down
    const bool quarterOver = says("End of ");
    if (p.phase == GamePhase::Scrimmage && p.clock <= 0 && c.q == p.q && !lines_.empty() && !quarterOver && !p.untimed)
      fail(g, p, "untimed", "a down snapped at 0:00 with no defensive foul before it");
    if (p.untimed && quarterOver) fail(g, p, "untimed", "a defensive foul accepted at 0:00 and the period ended without its untimed down");
    if (c.untimed) {
      const PlayLog& l = g.log.back();
      if (!(c.clock <= 0 && l.big == LogTag::Penalty && l.desc.find("PENALTY on " + g.teamOf(other(c.poss)).abbr + ":") != std::string::npos))
        fail(g, p, "untimed", "an untimed down owed with no accepted defensive foul at 0:00");
    }

    // ---- overtime
    if (c.q == 5 && c.phase == GamePhase::Final && !tied && c.otPossDone < 2 && c.clock > 0) {
      const bool returnScore = scorer >= 0 && scoredBy != p.poss && tagged(LogTag::Td);  // (a defensive or return touchdown)
      if (!tagged(LogTag::Safety) && !returnScore) fail(g, p, "overtime", "overtime ended before both teams had the ball");
    }
    if (c.q == 5 && (c.phase == GamePhase::Scrimmage || c.phase == GamePhase::Kickoff) && c.otPossDone >= 2 && !tied)
      fail(g, p, "overtime", "overtime went on after both teams had the ball and one led");
    return static_cast<int>(violations.size() - before);
  }

 private:
  Snap prev_;
  std::vector<const PlayLog*> lines_;
  int warn_[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  bool tagged(LogTag t) const {
    for (const PlayLog* l : lines_)
      if (l->big == t) return true;
    return false;
  }
  bool says(const char* s) const {
    for (const PlayLog* l : lines_)
      if (l->desc.find(s) != std::string::npos) return true;
    return false;
  }
  static std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
  }
  void fail(const Game& g, const Snap& p, const char* rule, const std::string& what) {
    std::string s = std::string(rule) + ": " + what + " [Q" + std::to_string(p.q) + " " + fmtClock(p.clock) + " " + gamePhaseName(p.phase) + ", " +
                    g.away->abbr + " at " + g.home->abbr + "]";
    for (const PlayLog* l : lines_) s += " | " + l->desc;
    violations.push_back(s);
  }
};

}  // namespace tag
