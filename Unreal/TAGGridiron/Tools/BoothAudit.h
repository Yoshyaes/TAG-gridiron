// The broadcast audit (BoothScript): what the booth said, checked against what happened, beyond the play calls.
//
// Seven classes of mistake a listener catches and the old fact check did not:
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
//              with no read before the snap, "here it is again").
//   td-spot    a touchdown called live and then a spot short of the goal in the result.
// The play window runs from a snap to the next snap or kick: its result, its breakdown, what follows them.
#pragma once

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

enum Cls { Sentiment, TwoNames, Numbers, RuledOut, Situation, BackRef, TdSpot, kClasses };
inline const char* clsName(int c) {
  static const char* const k[] = {"sentiment", "two-names", "numbers", "ruled-out", "situation", "back-ref", "td-spot"};
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

class Audit {
 public:
  bool quiet = false;
  int fails[kClasses] = {};
  int nonTargetCatches = 0;  // (a diagnostic: the sim's catch by a man the throw was not aimed at, with no tip)

  void start(const Game& g) {
    home_ = g.home;
    away_ = g.away;
    for (const Team* t : {g.home, g.away})
      for (const auto& p : t->roster) surnames_[p->last]++;
    score_ = g.score;
  }

  /** A spoken line (at the moment it starts). */
  void line(const booth::Line& l, const Game& g, booth::View::Stage stage) {
    readLog(g);
    trackScore(g);
    const std::string lt = lower(l.text);
    // ---- numbers: a run of points, the two-minute warning
    if (has(lt, "straight points") || has(lt, "unanswered") || has(lt, "points in a row") || has(lt, "scored ")) {
      const int n = firstNumber(l.text);
      if (n > 0 && (has(lt, "straight points") || has(lt, "unanswered") || has(lt, "in a row")) && n != runPts_)
        fail(Numbers, l.text, "a run of " + std::to_string(runPts_) + " points");
    }
    if (has(lt, "two-minute warning") && !has(lt, "through the two-minute") && !has(lt, "past the two-minute") && (g.q == 2 || g.q >= 4) && g.clock < 115)
      fail(Numbers, l.text, "said at " + clockStr(g.clock));
    // ---- ruled out: a man out for the game named as acting
    if (l.live || l.kind == booth::Kind::Result || l.kind == booth::Kind::Score)
      for (const Player* p : out_)
        if (has(l.text, (p->first + " " + p->last).c_str()) || bareSurname(l.text, p)) fail(RuledOut, l.text, p->first + " " + p->last + " was ruled out");
    // ---- situation
    if ((l.kind == booth::Kind::Setup || l.kind == booth::Kind::Read) && g.phase == GamePhase::Pat && (downDist(lt) || has(lt, "and goal")))
      fail(Situation, l.text, "a down and distance on a try");
    if (has(lt, "barely run") && g.team[g.poss].rushAtt >= 8) fail(Situation, l.text, std::to_string(g.team[g.poss].rushAtt) + " runs");
    if (tierOf(g) >= 1 && !l.live)
      for (const char* k : {"came through when it mattered", "i like the play fake", "put it away", "this might be the game", "momentum is all", "all the momentum",
                            "that could be the one", "big conversion", "statement drive"})
        if (has(lt, k)) fail(Situation, l.text, "the game is decided");
    if (g.q >= 4 && g.clock < 60 && (has(lt, "last few minutes") || has(lt, "these last"))) fail(Situation, l.text, "the final minute");
    // ---- back-references that need nothing earlier
    if (has(lt, "here it is again") || has(lt, "one more time")) fail(BackRef, l.text, "no earlier look");
    // ---- the play window
    Said s;
    s.text = l.text;
    s.lt = lt;
    s.who = l.who;
    s.kind = l.kind;
    s.live = l.live;
    s.topic = l.topic;
    win_.push_back(s);
    if (l.kind == booth::Kind::Read && l.who == booth::Who::Color) readSince_ = true;
    (void)stage;
  }

  /** A snap: the window of the last play closes; the next play is aimed at with this state. */
  void snap(const Game& g) {
    close();
    cur_ = Play();
    cur_.down = g.down;
    cur_.toGo = g.toGo;
    cur_.los = g.ballOn;
    cur_.phase = g.phase;
    cur_.readBefore = readSince_;
    readSince_ = false;
  }
  /** A kick (no scrimmage play): closes the window. */
  void kick() {
    close();
    cur_ = Play();
    readSince_ = false;
  }
  /** The play of the window, when its result is known. */
  void play(const PlayResult& r, const PlaySim& sim) {
    cur_.set = true;
    cur_.r = r;
    // a catch by a man the throw was not aimed at, nothing touched in between
    bool tip = false;
    const Player* aimed = nullptr;
    for (const PlayEvent& e : sim.events) {
      if (e.type == "throw" && e.b) aimed = e.b->p;
      if (e.type == "pbu" || e.type == "contested_inc" || e.type == "batted") tip = true;
    }
    if (r.complete && r.receiver && aimed && aimed != r.receiver && !tip) ++nonTargetCatches;
  }
  void finish() { close(); }

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
  };
  struct Play {
    bool set = false;
    PlayResult r;
    int down = 1;
    double toGo = 10, los = 25;
    GamePhase phase = GamePhase::Scrimmage;
    bool readBefore = false;
  };
  Play cur_;
  std::vector<Said> win_;
  bool readSince_ = false;
  std::map<std::string, int> surnames_;
  const Team* home_ = nullptr;
  const Team* away_ = nullptr;
  std::set<const Player*> out_;
  size_t logSeen_ = 0;
  PerTeam<int> score_{};
  int runTeam_ = -1, runPts_ = 0;

  void fail(int c, const std::string& text, const std::string& why) {
    ++fails[c];
    if (!quiet) std::printf("            [AUDIT %s] %s  (%s)\n", clsName(c), text.c_str(), why.c_str());
  }

  static std::string clockStr(double s) {
    const int t = static_cast<int>(s + 0.999);
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
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

  void readLog(const Game& g) {
    // "Injury: CB J.Smith (ABQ) is down and will not return."
    for (; logSeen_ < g.log.size(); ++logSeen_) {
      const std::string& d = g.log[logSeen_].desc;
      const size_t at = d.find("Injury: ");
      if (at == std::string::npos) continue;
      const size_t a = d.find(' ', at + 8);
      const size_t par = d.find(" (", a);
      if (a == std::string::npos || par == std::string::npos) continue;
      const std::string nm = d.substr(a + 1, par - a - 1);
      const std::string ab = d.substr(par + 2, 3);
      const Team& t = g.home->abbr == ab ? *g.home : *g.away;
      for (const auto& p : t.roster)
        if (!p->first.empty() && nm == std::string(1, p->first[0]) + "." + p->last) out_.insert(p.get());
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
    if (cur_.set && cur_.phase == GamePhase::Scrimmage) {
      const PlayResult& r = cur_.r;
      const double gained = r.endX - cur_.los;
      const bool conv = !r.td && !r.turnover && !r.safety && gained >= cur_.toGo - 1e-6;
      const bool good = (r.td && *r.td == Side::Off) || (cur_.down >= 3 && conv);
      const bool bad = r.turnover || r.kind == PlayKind::Sack || r.yards < 0 || (cur_.down >= 3 && !conv && !r.td) || (r.td && *r.td == Side::Def);
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
        if (s.who == booth::Who::PBP && s.kind == booth::Kind::Result && !flagLine && !has(s.lt, "return") && !has(s.lt, "interception") &&
            !has(s.lt, "picks it off") && !r.turnover && r.kind != PlayKind::Kneel && r.kind != PlayKind::Spike)
          for (const int v : yardsSaid(s.text)) {
            const int y = r.yards;
            if (v != std::abs(y) && v != tdDist && v != std::abs(y) - 1 && !(v == 1 && std::abs(y) <= 1) && !(v == 2 && has(s.lt, "two-point")))
              fail(Numbers, s.text, "the play gained " + std::to_string(y));
          }
        // back-references to a read nobody made
        if (s.kind == booth::Kind::Analysis)
          for (const char* k : {"i was talking about", "like i thought", "i told you", "you called it", "just like you said", "you saw that one", "nailed it",
                                "you had that one", "exactly what you said", "what you were talking about", "the pressure i saw before", "i thought they were coming"})
            if (has(s.lt, k) && !cur_.readBefore) fail(BackRef, s.text, "no read before the snap");
        // a touchdown called, then a spot short of it
        if (s.live && s.who == booth::Who::PBP && (has(s.lt, "touchdown") || has(s.lt, "he's in"))) tdCall = true;
        if (tdCall && s.who == booth::Who::PBP && s.kind == booth::Kind::Result && !has(s.lt, "touchdown") && !has(s.lt, "score") && !has(s.lt, "end zone") &&
            !has(s.lt, "six") && !has(s.lt, "house") && !has(s.lt, "distance") && !has(s.lt, "flag") && !has(s.lt, "in from") && !has(s.lt, "punches it") &&
            (has(s.lt, " out to ") || has(s.lt, " down at ") || has(s.lt, " to the ")))
          fail(TdSpot, s.text, "a touchdown was called");
      }
    }
    win_.clear();
    cur_.set = false;
  }
};

}  // namespace audit
