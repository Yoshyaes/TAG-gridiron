// The broadcast audit (BoothScript): what the booth said, checked against what happened, beyond the play calls.
//
// What a listener catches:
//   sentiment  the analyst credits the side the play went against (an offense praised on a turnover, a failed
//              conversion, a sack or a loss; a defense praised on a conversion or a score). An analysis line's side
//              comes from its angle (Line::topic), mapped here independently of the booth's own filter.
//   two-names  the analyst names a man for the act the play credits to another (the tackle, the sack, the breakup,
//              the interception).
//   numbers    a number against the facts: the yards of a play, a run of unanswered points, the two-minute warning
//              announced after 1:55.
//   ruled-out  a man the game ruled out named as acting later (a surname he shares counts: the ear cannot tell).
//   situation  a line that does not fit the state: a down and distance on a two-point try, "barely run it" with 8+
//              runs, stakes and strategy talk once the game is decided, "the last few minutes" in the final minute.
//   back-ref   a line that refers back to something nobody said ("the pressure I was talking about", "you called it"
//              with no read before the snap, "here it is again" on a play the presentation does not replay).
//   td-spot    a touchdown called live and then a spot short of the goal in the result.
// and the timing of the broadcast (the order the listener hears things in):
//   no-result  a play or a kick with no result said before the next snap or kick (the call of a play that is over
//              comes after its own live call: a result said before it belongs to the play before).
//   late       a flag, a referee's announcement, a score or a break heard after the next snap or kick started (the
//              break's event - a timeout, the end of a quarter, halftime - came before the window; the score was not
//              made in it, nor is it the try after it).
//   break-in-play  a break line between a play's live call and its result.
//   live-name  a man named in a live call or a result who is nowhere in the play (the planned carrier when another
//              man got the ball).
//   end-zone   "end zone" in a call of a play that scored no touchdown (and is no touchback, no throw into it).
//   clock      a time said ("with 0:22 to go") more than 3 s from the game clock.
//   down-dist  arithmetic across lines: the distance said before a play, less the yards said, is not the distance
//              said after it; a "no gain" at another spot.
//   analyst-floor  a quarter in which the analyst has under 8% of the words (with 200+ words said).
// The play window runs from a snap (or a kick) to the next snap or kick.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Booth/Booth.h"
#include "Sim/Game.h"

namespace audit {

using namespace tag;

enum Cls { Sentiment, TwoNames, Numbers, RuledOut, Situation, BackRef, TdSpot, NoResult, Late, BreakInPlay, LiveName, EndZone, Clock, DownDist, AnalystFloor, kClasses };
inline const char* clsName(int c) {
  static const char* const k[] = {"sentiment", "two-names", "numbers",  "ruled-out", "situation", "back-ref",  "td-spot",      "no-result",
                                  "late",      "break-in-play", "live-name", "end-zone", "clock", "down-dist", "analyst-floor"};
  return c >= 0 && c < kClasses ? k[c] : "?";
}

inline std::string lower(const std::string& s) {
  std::string o = s;
  for (char& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}
inline bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

// the side an analysis angle credits: +1 the offense, -1 the defense (or the offense's failure), 0 neither
inline int creditOf(const std::string& topic) {
  static const char* const kOff[] = {"a_route",   "a_open",    "a_tight",  "a_yacb",   "a_yac",       "a_deep",     "a_hot",    "a_time",    "a_hitthrow",
                                     "a_pa",      "a_screen",  "a_brk",    "a_3rdconv", "a_scr",      "a_pancake",  "a_second", "a_rbrk",    "a_spin",
                                     "a_truck",   "a_stiff",   "a_juke",   "a_gap",    "a_stretch",   "a_draw",     "a_zone",   "a_short",   "a_bigrun",
                                     "a_ahead",   "a_readcov", "a_contact", "a_3rdrun", "a_tddrive",  "a_tdgen",    "a_readwin", "a_readc0w", "a_read4thy"};
  static const char* const kDef[] = {"a_intp",       "a_intd",   "a_intr",   "a_drop",    "a_pbu",    "a_hurry",      "a_over",      "a_sackmove",
                                     "a_sackblitz",  "a_cov",    "a_sackgen", "a_stuff",  "a_shortstop", "a_hit",     "a_behind",    "a_short3",
                                     "a_deftd",      "a_to",     "a_readlost", "a_readc0s", "a_read4thn"};
  // (exact angles: "a_short" is the offense's, "a_short3" and "a_shortstop" the defense's; the read follow-ups carry
  // the read's topic after their name)
  auto is = [&topic](const char* k) { return topic == k || ((!std::strcmp(k, "a_readwin") || !std::strcmp(k, "a_readlost")) && topic.rfind(k, 0) == 0); };
  if (topic == "a_sackblitz2") return -1;
  for (const char* k : kDef)
    if (is(k)) return -1;
  for (const char* k : kOff)
    if (is(k)) return 1;
  return 0;
}

inline int tierOf(const Game& g) {
  const int m = std::abs(g.score.home - g.score.away);
  if (g.q >= 4 && ((m >= 17 && g.clock < 600) || (m >= 9 && g.clock < 100) || m >= 25)) return 2;
  if ((g.q >= 3 && m >= 17) || (g.q >= 4 && m >= 9 && g.clock < 480)) return 1;
  return 0;
}

/** The timing of a broadcast (one game or a run of them): what BoothScript prints per mode. */
struct Timing {
  int plays = 0, noResult = 0;
  int refLines = 0, refLate = 0;
  int scoreLines = 0, scoreAfterKick = 0, scoreLate = 0;
  int breakLate = 0, breakInPlay = 0, flagLate = 0;
  int words = 0, colorWords = 0;
  double maxSilence = 0;
  int dropped = 0, droppedMust = 0;  // lines never said; ... of them a result, a score, a flag or a break
  void add(const Timing& o) {
    plays += o.plays;
    noResult += o.noResult;
    refLines += o.refLines;
    refLate += o.refLate;
    scoreLines += o.scoreLines;
    scoreAfterKick += o.scoreAfterKick;
    scoreLate += o.scoreLate;
    breakLate += o.breakLate;
    breakInPlay += o.breakInPlay;
    flagLate += o.flagLate;
    words += o.words;
    colorWords += o.colorWords;
    maxSilence = (std::max)(maxSilence, o.maxSilence);
    dropped += o.dropped;
    droppedMust += o.droppedMust;
  }
  void print(const char* lead) const {
    std::printf("%s plays %d, no result %d | ref late %d/%d | score after kickoff %d, score late %d (of %d) | flag late %d | break late %d, inside a play %d | "
                "analyst %.1f%% of %d words, longest silence %.0f s | dropped %d (result/score/flag/break %d)\n",
                lead, plays, noResult, refLate, refLines, scoreAfterKick, scoreLate, scoreLines, flagLate, breakLate, breakInPlay,
                100.0 * colorWords / (std::max)(1, words), words, maxSilence, dropped, droppedMust);
  }
};

class Audit {
 public:
  bool quiet = false;
  int fails[kClasses] = {};
  int nonTargetCatches = 0;  // (a diagnostic: the sim's catch by a man the throw was not aimed at, with no tip)
  Timing tm;

  void start(const Game& g) {
    g_ = &g;
    home_ = g.home;
    away_ = g.away;
    for (const Team* t : {g.home, g.away})
      for (const auto& p : t->roster) surnames_[p->last]++;
    score_ = g.score;
  }

  /** A spoken line (at the moment it starts, booth seconds t). */
  void line(const booth::Line& l, const Game& g, booth::View::Stage stage, double t) {
    readLog(g);
    trackScore(g);
    const std::string lt = lower(l.text);
    const int nWords = static_cast<int>(std::count(l.text.begin(), l.text.end(), ' ') + 1);
    // ---- the words and the analyst's share (each quarter)
    if (g.q != wordsQ_) {
      quarterFloor();
      wordsQ_ = g.q;
      qWords_ = qColor_ = 0;
    }
    tm.words += nWords;
    qWords_ += nWords;
    if (l.who == booth::Who::Color) {
      tm.colorWords += nWords;
      qColor_ += nWords;
      if (lastColorT_ >= 0) tm.maxSilence = (std::max)(tm.maxSilence, t - lastColorT_);
      lastColorT_ = t;
    }
    // ---- numbers: a run of points, the two-minute warning
    if (has(lt, "straight points") || has(lt, "unanswered") || has(lt, "points in a row") || has(lt, "scored ")) {
      const int n = firstNumber(l.text);
      if (n > 0 && (has(lt, "straight points") || has(lt, "unanswered") || has(lt, "in a row")) && n != runPts_)
        fail(Numbers, l.text, "a run of " + std::to_string(runPts_) + " points");
    }
    if (has(lt, "two-minute warning") && !has(lt, "through the two-minute") && !has(lt, "past the two-minute") && (g.q == 2 || g.q >= 4) && g.clock < 115)
      fail(Numbers, l.text, "said at " + clockStr(g.clock));
    // ---- the clock said against the game's
    if (!l.live)
      if (const int said = clockSaid(l.text); said >= 0) {
        const int now = static_cast<int>(g.clock + 0.999);
        if (std::abs(said - now) > 3) fail(Clock, l.text, "the clock reads " + clockStr(g.clock));
      }
    // ---- ruled out: a man out for the game named as acting
    if (l.live || l.kind == booth::Kind::Result || l.kind == booth::Kind::Score)
      for (const auto& [p, at] : out_)
        // (hurt on the play being called now: its own result names him)
        if (at < w_.log0 && (has(l.text, (p->first + " " + p->last).c_str()) || bareSurname(l.text, p))) fail(RuledOut, l.text, p->first + " " + p->last + " was ruled out");
    // ---- situation
    if ((l.kind == booth::Kind::Setup || l.kind == booth::Kind::Read) && g.phase == GamePhase::Pat && (downDist(lt) || has(lt, "and goal")))
      fail(Situation, l.text, "a down and distance on a try");
    if (has(lt, "barely run") && g.team[g.poss].rushAtt >= 8) fail(Situation, l.text, std::to_string(g.team[g.poss].rushAtt) + " runs");
    if (tierOf(g) >= 1 && !l.live)
      for (const char* k : {"came through when it mattered", "i like the play fake", "put it away", "this might be the game", "momentum is all", "all the momentum",
                            "that could be the one", "big conversion", "statement drive"})
        if (has(lt, k)) fail(Situation, l.text, "the game is decided");
    if (g.q >= 4 && g.clock < 60 && (has(lt, "last few minutes") || has(lt, "these last"))) fail(Situation, l.text, "the final minute");
    // ---- timing: what this window has heard
    const bool flagLead = isFlagLine(lt);
    if (l.who == booth::Who::PBP && l.live) {
      if (!w_.liveSaid && w_.open) w_.liveSaid = true;
    }
    if (l.kind == booth::Kind::Ref) {
      ++tm.refLines;
      if (w_.open && !flagInWindow(g)) {
        ++tm.refLate;
        fail(Late, l.text, "the referee after the next snap or kick");
      }
    }
    if (l.who == booth::Who::PBP && l.kind == booth::Kind::Result && flagLead && w_.open && !flagInWindow(g) && w_.liveSaid) {
      ++tm.flagLate;
      fail(Late, l.text, "a flag after the next snap or kick");
    }
    if (l.kind == booth::Kind::Break && w_.open) {
      if (w_.liveSaid && !w_.resultSaid) {
        ++tm.breakInPlay;
        fail(BreakInPlay, l.text, "between the live call and the result");
      } else if (w_.liveSaid && lastBreakLog_ >= 0 && static_cast<size_t>(lastBreakLog_) < w_.log0) {
        ++tm.breakLate;
        fail(Late, l.text, "a break after the next snap or kick");
      }
    }
    // (a result counts for this window's play when it follows the play's live call, or - no live call heard - was
    // written after the window opened: one written before it is the last play's, late)
    if (((l.who == booth::Who::PBP && (l.kind == booth::Kind::Result || l.kind == booth::Kind::Score) && !flagLead) || l.kind == booth::Kind::Ref) &&
        (w_.liveSaid || l.notBefore >= w_.t0))
      w_.resultSaid = true;
    // ---- the play window
    Said s;
    s.text = l.text;
    s.lt = lt;
    s.who = l.who;
    s.kind = l.kind;
    s.live = l.live;
    s.topic = l.topic;
    s.afterLive = w_.liveSaid;
    s.stage = stage;
    win_.push_back(s);
    if (l.kind == booth::Kind::Read && l.who == booth::Who::Color) readSince_ = true;
  }

  /** A line that left the booth's queue unsaid. */
  void dropped(const booth::Line& l) {
    ++tm.dropped;
    using K = booth::Kind;
    if (!l.live && (l.kind == K::Result || l.kind == K::Score || l.kind == K::Ref || l.kind == K::Break) && l.who == booth::Who::PBP) {
      ++tm.droppedMust;
      if (!quiet) std::printf("            [dropped %s] %s\n", booth::kindName(l.kind), l.text.c_str());
    }
  }

  /** A snap: the window of the last play closes; the next play is aimed at with this state. */
  void snap(const Game& g, double t) {
    close();
    cur_ = Play();
    cur_.down = g.down;
    cur_.toGo = g.toGo;
    cur_.los = g.ballOn;
    cur_.phase = g.phase;
    cur_.readBefore = readSince_;
    readSince_ = false;
    openWindow(g, -1, g.score, g.log.size(), t);
  }
  /** A kick (0 kickoff, 1 punt, 2 field goal, 3 extra point): closes the window and opens the kick's. scoreBefore and
   *  logBefore: the Game before it rolled the kick (a field goal's points and line are in it already). */
  void kick(const Game& g, int kind, const PerTeam<int>& scoreBefore, size_t logBefore, double t) {
    close();
    cur_ = Play();
    cur_.phase = GamePhase::Kickoff;
    readSince_ = false;
    openWindow(g, kind, scoreBefore, logBefore, t);
  }
  /** The play of the window, when its result is known. */
  void play(const PlayResult& r, const PlaySim& sim) {
    cur_.set = true;
    cur_.r = r;
    cur_.in.clear();
    // a catch by a man the throw was not aimed at, nothing touched in between
    bool tip = false;
    const Player* aimed = nullptr;
    for (const PlayEvent& e : sim.events) {
      if (e.type == "throw" && e.b) aimed = e.b->p;
      if (e.type == "pbu" || e.type == "contested_inc" || e.type == "batted") tip = true;
      if (e.a) cur_.in.insert(e.a->p);
      if (e.b) cur_.in.insert(e.b->p);
    }
    for (const Player* p : {r.passer, r.rusher, r.receiver, r.target, r.sackedBy, r.intBy, r.fumbleBy, r.fumbleRecBy, r.forcedBy, r.pbuBy})
      if (p) cur_.in.insert(p);
    for (const Player* p : r.tacklers) cur_.in.insert(p);
    // the men on the field, and the ones the ball went to (a handoff, a pitch, a throw, a catch)
    cur_.onField.clear();
    cur_.handlers.clear();
    for (const Ent* e : sim.ents)
      if (e && e->p) cur_.onField.insert(e->p);
    for (const PlayEvent& e : sim.events)
      if (e.type == "handoff" || e.type == "pitch" || e.type == "lateral" || e.type == "toss" || e.type == "throw" || e.type == "catch") {
        if (e.a) cur_.handlers.insert(e.a->p);
        if (e.b) cur_.handlers.insert(e.b->p);
      }
    for (const Player* p : {r.passer, r.rusher, r.receiver, r.target})
      if (p) cur_.handlers.insert(p);
    cur_.offTeam = nullptr;
    for (const Team* t : {home_, away_})
      if (t && ((r.passer && t->hasPlayer(r.passer)) || (r.rusher && t->hasPlayer(r.rusher)) || (sim.qb && sim.qb->p && t->hasPlayer(sim.qb->p)))) cur_.offTeam = t;
    if (r.complete && r.receiver && aimed && aimed != r.receiver && !tip) ++nonTargetCatches;
  }
  void finish() {
    close();
    w_.open = false;
    quarterFloor();
  }

  int total() const {
    int n = 0;
    for (int c = 0; c < kClasses; ++c) n += fails[c];
    return n;
  }

 private:
  struct Said {
    std::string text, lt, topic;
    booth::Who who = booth::Who::PBP;
    booth::Kind kind = booth::Kind::Result;
    bool live = false;
    bool afterLive = false;  // said after the window's first live call
    booth::View::Stage stage = booth::View::Stage::Wait;
  };
  struct Play {
    bool set = false;
    PlayResult r;
    int down = 1;
    double toGo = 10, los = 25;
    GamePhase phase = GamePhase::Scrimmage;
    bool readBefore = false;
    std::set<const Player*> in;        // every man the play's events and result name
    std::set<const Player*> onField;   // the 22
    std::set<const Player*> handlers;  // the men the ball went through
    const Team* offTeam = nullptr;
  };
  struct Window {
    bool open = false;
    int kick = -1;  // -1 a scrimmage play (or a try for two), else the kick's kind
    PerTeam<int> score0{};
    size_t log0 = 0;
    bool liveSaid = false, resultSaid = false;
    bool afterScore = false;  // the window before scored (its score line may come in this one when this is the try)
    bool tryWin = false;
    double t0 = 0;  // booth seconds it opened
  };
  const Game* g_ = nullptr;
  Play cur_;
  Window w_;
  std::vector<Said> win_;
  bool readSince_ = false;
  std::map<std::string, int> surnames_;
  const Team* home_ = nullptr;
  const Team* away_ = nullptr;
  std::map<const Player*, size_t> out_;  // a man ruled out, and the log line that said so
  size_t logSeen_ = 0;
  long lastBreakLog_ = -1;  // the last log line that is a break's event (a timeout, the warning, a quarter's end)
  PerTeam<int> score_{};
  int runTeam_ = -1, runPts_ = 0;
  int wordsQ_ = -1, qWords_ = 0, qColor_ = 0;
  double lastColorT_ = -1;
  // the down and distance the listener last heard (and the spot), for the arithmetic across lines
  int ddDown_ = 0, ddDist_ = -1;
  std::string ddSpot_;

  void fail(int c, const std::string& text, const std::string& why) {
    ++fails[c];
    if (!quiet) std::printf("            [AUDIT %s] %s  (%s)\n", clsName(c), text.c_str(), why.c_str());
  }

  void openWindow(const Game& g, int kind, const PerTeam<int>& score0, size_t log0, double t) {
    const bool prevScored = w_.open && windowScored();
    w_ = Window();
    w_.open = true;
    w_.t0 = t;
    w_.kick = kind;
    w_.score0 = score0;
    w_.log0 = log0;
    w_.afterScore = prevScored;
    w_.tryWin = kind == 3 || (kind < 0 && g.phase == GamePhase::Pat);
    ++tm.plays;
  }
  bool windowScored() const { return g_ && (g_->score.home != w_.score0.home || g_->score.away != w_.score0.away); }
  static bool isFlagLog(const PlayLog& e) {
    const std::string& d = e.desc;
    return e.big == LogTag::Penalty || has(d, "PENALTY") || has(d, "Penalty") || has(d, "(declined)") || (has(d, " kicks ") && (has(d, "out of bounds") || has(d, "short of the landing zone")));
  }
  bool flagInWindow(const Game& g) const {
    for (size_t i = w_.log0; i < g.log.size(); ++i)
      if (isFlagLog(g.log[i])) return true;
    return false;
  }
  static bool isFlagLine(const std::string& lt) {
    return has(lt, "flag") || has(lt, "laundry") || has(lt, "yellow") || has(lt, "penalty") || has(lt, "declined");
  }
  void quarterFloor() {
    if (wordsQ_ >= 1 && wordsQ_ <= 4 && qWords_ >= 200 && qColor_ * 100 < qWords_ * 8)
      fail(AnalystFloor, "Q" + std::to_string(wordsQ_), "the analyst had " + std::to_string(qColor_) + " of " + std::to_string(qWords_) + " words");
  }

  static std::string clockStr(double s) {
    const int t = static_cast<int>(s + 0.999);
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
  }
  // "with 0:22 to go", "1:24 left in the half": the seconds said, or -1
  static int clockSaid(const std::string& t) {
    for (size_t i = 0; i + 3 < t.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(t[i])) || (i > 0 && std::isdigit(static_cast<unsigned char>(t[i - 1])))) continue;
      size_t j = i;
      while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j]))) ++j;
      if (j + 2 >= t.size() || t[j] != ':' || !std::isdigit(static_cast<unsigned char>(t[j + 1])) || !std::isdigit(static_cast<unsigned char>(t[j + 2]))) continue;
      const std::string after = lower(t.substr(j + 3, 24));
      if (has(after, "left") || has(after, "to go") || has(after, "remaining") || has(after, "on the clock") || has(after, "to play"))
        return std::atoi(t.c_str() + i) * 60 + std::atoi(t.c_str() + j + 1);
    }
    return -1;
  }
  static int firstNumber(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i)
      if (std::isdigit(static_cast<unsigned char>(s[i]))) return std::atoi(s.c_str() + i);
    return -1;
  }
  static bool downDist(const std::string& lt) {
    for (const char* d : {"1st and", "2nd and", "3rd and", "4th and"})
      if (has(lt, d)) return true;
    return false;
  }
  // the last "Nth and X" in a line: down, and the distance (-1 goal, -2 inches); false when none
  static bool ddOf(const std::string& lt, int& down, int& dist, bool result) {
    bool got = false;
    for (const char* d : {"1st and ", "2nd and ", "3rd and ", "4th and "})
      for (size_t at = lt.find(d); at != std::string::npos; at = lt.find(d, at + 1)) {
        // ("short on 4th and 1", "converts 3rd and 2": the down the play was on, not the next)
        if (result && ((at >= 3 && lt.compare(at - 3, 3, "on ") == 0) || (at >= 8 && lt.compare(at - 8, 8, "convert ") == 0) ||
            (at >= 9 && lt.compare(at - 9, 9, "converts ") == 0)))
          continue;
        got = true;
        down = d[0] - '0';
        const std::string rest = lt.substr(at + 8);
        dist = rest.rfind("goal", 0) == 0 ? -1 : rest.rfind("inches", 0) == 0 ? -2 : std::atoi(rest.c_str());
      }
    return got;
  }
  // the first spot in a line ("the Austin 26" -> "austin 26", "the 40" -> "40", "midfield")
  static std::string spotOf(const std::string& t) {
    for (size_t i = 0; i < t.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(t[i])) || (i > 0 && std::isalnum(static_cast<unsigned char>(t[i - 1])))) continue;
      if (!isSpot(t, i)) continue;
      size_t a = t.rfind("the ", i);
      if (a == std::string::npos) a = i;
      else a += 4;
      size_t j = i;
      while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j]))) ++j;
      return lower(t.substr(a, j - a));
    }
    return has(lower(t), "midfield") ? std::string("midfield") : std::string();
  }
  // a man's surname said bare (not after his own or another first name)
  bool bareSurname(const std::string& text, const Player* p) const {
    for (size_t at = text.find(p->last); at != std::string::npos; at = text.find(p->last, at + 1)) {
      const size_t end = at + p->last.size();
      if (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]))) continue;
      if (at > 0 && std::isalpha(static_cast<unsigned char>(text[at - 1]))) continue;
      // preceded by a capitalized word: a first name (his, or another man's)
      size_t b = at;
      while (b > 0 && text[b - 1] == ' ') --b;
      size_t a = b;
      while (a > 0 && std::isalpha(static_cast<unsigned char>(text[a - 1]))) --a;
      if (b > a && std::isupper(static_cast<unsigned char>(text[a])) && b == at - 1) continue;
      return true;
    }
    return false;
  }
  // a man named in a line: his full name, or a surname nobody else in the game has (said as a word)
  bool named(const std::string& text, const Player* p) const {
    if (text.find(p->first + " " + p->last) != std::string::npos) return true;
    auto it = surnames_.find(p->last);
    if (it == surnames_.end() || it->second > 1) return false;
    for (size_t at = text.find(p->last); at != std::string::npos; at = text.find(p->last, at + 1)) {
      const size_t end = at + p->last.size();
      if ((end >= text.size() || !std::isalpha(static_cast<unsigned char>(text[end]))) && (at == 0 || !std::isalpha(static_cast<unsigned char>(text[at - 1])))) return true;
    }
    return false;
  }

  void readLog(const Game& g) {
    // "Injury: CB J.Smith (ABQ) is down and will not return."
    for (; logSeen_ < g.log.size(); ++logSeen_) {
      const std::string& d = g.log[logSeen_].desc;
      if (has(d, "Timeout ") || has(d, "Two-minute warning") || has(d, "End of ") || has(d, "Halftime") || has(d, "Injury: ") || has(d, "SAFETY") ||
          has(d, "Overtime"))
        lastBreakLog_ = static_cast<long>(logSeen_);
      const size_t at = d.find("Injury: ");
      if (at == std::string::npos) continue;
      const size_t a = d.find(' ', at + 8);
      const size_t par = d.find(" (", a);
      if (a == std::string::npos || par == std::string::npos) continue;
      const std::string nm = d.substr(a + 1, par - a - 1);
      const std::string ab = d.substr(par + 2, 3);
      const Team& t = g.home->abbr == ab ? *g.home : *g.away;
      for (const auto& p : t.roster)
        if (!p->first.empty() && nm == std::string(1, p->first[0]) + "." + p->last) out_.emplace(p.get(), logSeen_);
    }
  }
  void trackScore(const Game& g) {
    const int dh = g.score.home - score_.home, da = g.score.away - score_.away;
    for (const auto& [team, pts] : {std::pair<int, int>{0, dh}, {1, da}})
      if (pts > 0) {
        if (runTeam_ == team) runPts_ += pts;
        else {
          runTeam_ = team;
          runPts_ = pts;
        }
      }
    score_ = g.score;
  }

  // a number that is a yard line: "the 26", "the Austin 26", "the Oklahoma City 26"
  static bool isSpot(const std::string& t, size_t i) {
    size_t k = i;
    for (int words = 0; words < 4; ++words) {
      while (k > 0 && t[k - 1] == ' ') --k;
      size_t a = k;
      while (a > 0 && (std::isalpha(static_cast<unsigned char>(t[a - 1])) || t[a - 1] == '.')) --a;
      if (a == k) return false;
      const std::string w = t.substr(a, k - a);
      if (w == "the" || w == "The") return true;
      if (!std::isupper(static_cast<unsigned char>(w[0]))) return false;
      k = a;
    }
    return false;
  }
  // the yards a result line gives the play: numbers that are not a yard line, a down, a distance, a clock or a jersey
  static std::vector<int> yardsSaid(const std::string& t) {
    std::vector<int> out;
    for (size_t i = 0; i < t.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(t[i])) || (i > 0 && (std::isalnum(static_cast<unsigned char>(t[i - 1])) || t[i - 1] == ':'))) continue;
      size_t j = i;
      while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j]))) ++j;
      const std::string after = t.substr(j, 8), before = t.substr(i >= 12 ? i - 12 : 0, i >= 12 ? 12 : i);
      const int v = std::atoi(t.c_str() + i);
      const bool ord = after.rfind("st", 0) == 0 || after.rfind("nd", 0) == 0 || after.rfind("rd", 0) == 0 || after.rfind("th", 0) == 0;
      const bool spot = isSpot(t, i);
      const bool dist = before.size() >= 4 && before.compare(before.size() - 4, 4, "and ") == 0;
      const bool clock = after.rfind(":", 0) == 0;
      const bool num = before.find("number") != std::string::npos;
      const bool score = after.rfind("-", 0) == 0 && j + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[j + 1]));
      const bool count = after.rfind(" of", 0) == 0 || after.rfind(" catch", 0) == 0 || after.rfind(" carr", 0) == 0 || after.rfind(" play", 0) == 0 ||
                         after.rfind(" time", 0) == 0 || after.rfind(" sack", 0) == 0;
      if (!ord && !spot && !dist && !clock && !num && !score && !count) out.push_back(v);
      i = j;
    }
    return out;
  }

  void close() {
    const bool scrim = cur_.set && cur_.phase == GamePhase::Scrimmage;
    const PlayResult& r = cur_.r;
    // ---- timing: the play's result, the scores said in the window
    if (w_.open) {
      const bool scored = windowScored();
      // (a knee: the first one is called, the rest of the victory formation need not be)
      if (!w_.resultSaid && ((cur_.set && cur_.r.kind != PlayKind::Kneel) || w_.kick >= 0)) {
        ++tm.noResult;
        std::string last;
        for (const Said& s : win_)
          if (s.live && s.who == booth::Who::PBP) last = s.text;
        fail(NoResult, last.empty() ? std::string("(nothing called)") : last, w_.kick >= 0 ? "a kick with no result before the next play" : "a play with no result before the next snap");
      }
      for (const Said& s : win_)
        if (s.kind == booth::Kind::Score) {
          ++tm.scoreLines;
          // (said as the kickoff team lines up, before the kick is called: in time)
          if (scored || (w_.tryWin && w_.afterScore) || (w_.kick == 0 && !s.afterLive)) continue;
          if (w_.kick == 0) {
            ++tm.scoreAfterKick;
            fail(Late, s.text, "the score after the kickoff");
          } else {
            ++tm.scoreLate;
            fail(Late, s.text, "the score after the next snap");
          }
        }
    }
    if (scrim) {
      const double gained = r.endX - cur_.los;
      const bool conv = !r.td && !r.turnover && !r.safety && gained >= cur_.toGo - 1e-6;
      const bool good = (r.td && *r.td == Side::Off) || (cur_.down >= 3 && conv);
      const bool bad = r.turnover || r.kind == PlayKind::Sack || r.yards < 0 || (cur_.down >= 3 && !conv && !r.td) || (r.td && *r.td == Side::Def);
      const bool replay = r.td || r.interception || r.fumbleLost || r.kind == PlayKind::Sack || r.yards >= 20;
      const bool flag = g_ && flagInWindow(*g_);
      const int tdDist = static_cast<int>(100 - cur_.los + 0.5);
      bool tdCall = false;
      for (const Said& s : win_) {
        // sentiment
        if (s.kind == booth::Kind::Analysis && s.who == booth::Who::Color && !s.topic.empty()) {
          const int c = creditOf(s.topic);
          if (c > 0 && bad) fail(Sentiment, s.text, "offense credited on a play that failed (" + s.topic + ")");
          if (c < 0 && good) fail(Sentiment, s.text, "defense credited on a play that worked (" + s.topic + ")");
        }
        // two names for one act
        if (s.kind == booth::Kind::Analysis && !s.topic.empty()) {
          const Player* want = nullptr;
          if (s.topic.rfind("a_stuff", 0) == 0) want = r.tacklers.empty() ? nullptr : r.tacklers[0];
          else if (s.topic == "a_sackmove" || s.topic == "a_sackblitz") want = r.sackedBy;
          else if (s.topic == "a_pbu" || s.topic.rfind("a_readlost", 0) == 0) want = r.pbuBy;
          else if (s.topic.rfind("a_int", 0) == 0 && s.topic != "a_intback") want = r.intBy;
          // another man of the side that made the play, named for it ("Xavier Iverson" for Nolan Iverson's tackle); a
          // line about the quarterback's mistake names nobody else for the interception
          if (want)
            for (const Team* t : {home_, away_})
              if (t && t->hasPlayer(want))
                for (const auto& p : t->roster) {
                  if (p.get() == want || (p->first == want->first && p->last == want->last)) continue;  // (a namesake: the ear can't tell)
                  bool twin = false;  // (the same full name on the other side: the line may mean him)
                  for (const Team* o : {home_, away_})
                    if (o && o != t)
                      for (const auto& q : o->roster) twin = twin || (q->first == p->first && q->last == p->last);
                  if (twin) continue;
                  const bool full = s.text.find(p->first + " " + p->last) != std::string::npos;
                  const bool bare = surnames_[p->last] <= 1 && s.text.find(p->last) != std::string::npos;
                  if (full || bare) fail(TwoNames, s.text, "the play credits " + want->first + " " + want->last + ", not " + p->first + " " + p->last);
                }
        }
        // the yards of the play
        // (a flag's line - "laundry", "yellow", "repeat second down" - gives the penalty's yards, not the play's)
        bool flagLine = false;
        for (const char* k : {"flag", "penalty", "laundry", "yellow", "repeat", "automatic", "declin", "wipes", "erases", "comes back", "spot foul", "half the distance"})
          if (has(s.lt, k)) flagLine = true;
        const bool playResult = s.who == booth::Who::PBP && s.kind == booth::Kind::Result && s.afterLive && !flagLine;
        if (playResult && !has(s.lt, "return") && !has(s.lt, "interception") && !has(s.lt, "picks it off") && !r.turnover && r.kind != PlayKind::Kneel && r.kind != PlayKind::Spike)
          for (const int v : yardsSaid(s.text)) {
            const int y = r.yards;
            if (std::abs(v - std::abs(y)) > 1 && v != tdDist && !(v == 2 && has(s.lt, "two-point")))
              fail(Numbers, s.text, "the play gained " + std::to_string(y));
          }
        // back-references to a read nobody made; a replay's lead-in on a play the presentation does not replay
        if (s.kind == booth::Kind::Analysis)
          for (const char* k : {"i was talking about", "like i thought", "i told you", "you called it", "just like you said", "you saw that one", "nailed it",
                                "you had that one", "exactly what you said", "what you were talking about", "the pressure i saw before", "i thought they were coming"})
            if (has(s.lt, k) && !cur_.readBefore) fail(BackRef, s.text, "no read before the snap");
        if ((has(s.lt, "here it is again") || has(s.lt, "one more time")) && !replay && s.stage != booth::View::Stage::Replay)
          fail(BackRef, s.text, "no replay of this play");
        // a touchdown called, then a spot short of it
        if (s.live && s.who == booth::Who::PBP && (has(s.lt, "touchdown") || has(s.lt, "he's in"))) tdCall = true;
        if (tdCall && s.who == booth::Who::PBP && s.kind == booth::Kind::Result && !has(s.lt, "touchdown") && !has(s.lt, "score") && !has(s.lt, "end zone") &&
            !has(s.lt, "six") && !has(s.lt, "house") && !has(s.lt, "distance") && !has(s.lt, "flag") && !has(s.lt, "in from") && !has(s.lt, "punches it") &&
            (has(s.lt, " out to ") || has(s.lt, " down at ") || has(s.lt, " to the ")))
          fail(TdSpot, s.text, "a touchdown was called");
        // "end zone" in the call of a play that scored nothing
        if (s.who == booth::Who::PBP && (s.live || playResult) && s.afterLive && has(s.lt, "end zone") && !r.td && !r.safety && !r.touchback && !flagLine) {
          bool ok = false;
          for (const char* k : {"incomplete", "back of", "out of", "touchback", "knee", "throw", "throws", "fires", "pass", "lob", "shot", "intercept", "picked",
                                "short of", "just short", "almost", "nearly", "inches", "can't", "no catch", "knocked", "broken up", "batted", "drop", "away", "safety"})
            ok = ok || has(s.lt, k);
          if (!ok) fail(EndZone, s.text, "no touchdown on the play");
        }
        // a man named in the call who is not on the field; the live call of the handoff or the throw naming a man of
        // the offense the ball never went to (the planned back when the quarterback kept it)
        if (s.who == booth::Who::PBP && s.afterLive && (s.live || playResult) && !cur_.onField.empty() && r.kind != PlayKind::Kneel) {
          bool handling = false;
          for (const char* k : {"handoff", "hands it", "gives it to", "the carry", "with the ball", "throws", "delivers", "fires", "intended for",
                                "lets it go", "lets it fly", "airs it out", "gets it out to", "pitch", "pass, "})
            handling = handling || (s.live && has(s.lt, k));
          for (const Team* t : {home_, away_})
            if (t)
              for (const auto& p : t->roster) {
                if (!named(s.text, p.get())) continue;
                bool twin = false;  // (another man of the same name is on the field)
                for (const Player* q : cur_.onField) twin = twin || (q && q != p.get() && q->last == p->last);
                if (twin) continue;
                if (!cur_.onField.count(p.get())) fail(LiveName, s.text, p->first + " " + p->last + " is not on the field");
                else if (handling && t == cur_.offTeam && !cur_.handlers.count(p.get()) && !cur_.in.count(p.get()))
                  fail(LiveName, s.text, "the ball never went to " + p->first + " " + p->last);
              }
        }
      }
      // ---- the arithmetic across lines: the distance before, the yards, the distance after
      if (!flag && !r.turnover && !r.td && cur_.phase == GamePhase::Scrimmage)
        for (const Said& s : win_) {
          if (!(s.who == booth::Who::PBP && s.kind == booth::Kind::Result && s.afterLive)) continue;
          int dn = 0, ds = -1;
          const std::vector<int> ys = yardsSaid(s.text);
          int y = ys.size() == 1 ? ys[0] : (has(s.lt, "no gain") ? 0 : -999);
          if (y != -999 && (has(s.lt, "loss") || has(s.lt, "loses") || has(s.lt, "lost") || has(s.lt, "back to") || has(s.lt, "behind the line") ||
                            has(s.lt, "costs") || has(s.lt, "sack")))
            y = -y;
          if (ddOf(s.lt, dn, ds, true) && ddDown_ >= 1 && ddDist_ > 0 && ds > 0 && y != -999 && dn == ddDown_ + 1 && y < ddDist_ && ds != ddDist_ - y)
            fail(DownDist, s.text, std::to_string(ddDown_) + " and " + std::to_string(ddDist_) + " less " + std::to_string(y) + " yards");
          if (has(s.lt, "no gain") && !ddSpot_.empty()) {
            const std::string sp = spotOf(s.text);
            if (!sp.empty() && sp != ddSpot_) fail(DownDist, s.text, "no gain, but the ball was at the " + ddSpot_);
          }
          break;
        }
    }
    // what the listener last heard of the down, the distance and the spot (a setup, or a result's tail); a flag in the
    // window makes what was said before it history (and the down it leaves may never be said: nothing to count from)
    const bool flagged = g_ && flagInWindow(*g_);
    if (flagged) {
      ddDown_ = 0;
      ddSpot_.clear();
    }
    bool pastFlag = !flagged;
    for (const Said& s : win_) {
      if (!pastFlag) {
        pastFlag = s.kind == booth::Kind::Ref || (s.who == booth::Who::PBP && isFlagLine(s.lt));
        continue;
      }
      if (s.who != booth::Who::PBP || s.live) continue;
      if (s.kind != booth::Kind::Setup && s.kind != booth::Kind::Result && s.kind != booth::Kind::Ref) continue;
      int dn = 0, ds = -1;
      if (ddOf(s.lt, dn, ds, s.kind == booth::Kind::Result)) {
        ddDown_ = dn;
        ddDist_ = ds;
      } else if (s.kind == booth::Kind::Result && s.afterLive)
        for (const char* k : {"first down", "the chains", "new set of downs", "fresh set", "past the marker", "convert"})
          if (has(s.lt, k)) ddDown_ = 0;  // (a new set of downs, its distance not said: nothing to count from)
      if (s.kind == booth::Kind::Setup) ddSpot_ = spotOf(s.text);  // (none said: no spot to hold the next result to)
      else if (s.kind == booth::Kind::Result && s.afterLive && !isFlagLine(s.lt)) ddSpot_ = spotOf(s.text);
    }
    if (w_.kick >= 0 || (cur_.set && (cur_.r.turnover || cur_.r.td))) {
      ddDown_ = 0;  // (a new series: the next setup starts the count)
      ddSpot_.clear();
    }
    win_.clear();
    cur_.set = false;
  }
};

}  // namespace audit
