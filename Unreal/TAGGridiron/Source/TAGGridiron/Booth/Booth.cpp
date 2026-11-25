// The broadcast booth (see Booth.h): what the two voices say and when.
//
// The flow of a down, the way a Sunday crew calls it:
//   whistle -> PBP gives the result ("Carter picks up 12, and that's a first down.")
//           -> the analyst breaks it down from what the play really was (the coverage called, the separation at the
//              catch, the block that sprang the run, the rusher's move that beat the tackle...)
//           -> a stat, a storyline, a drive summary when there is time
//   presnap -> PBP sets it up ("Third and 4 at the Fresno 38.") and the analyst may read the defense
//   snap    -> PBP calls it live: the drop, the pressure, the throw and the name, the catch, the yard lines on a
//              breakaway, the hit; the analyst is cut off if he is still talking.
// Everything said is checked against what really happened (the sim's PlayResult and events, the Game's log and stat
// book), so the booth never calls a catch the sim did not make.
#include "Booth/Booth.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <tuple>

#include "Booth/BoothBanks.inl"  // the authored phrase banks (data: compiled here, once)
#include "Game/TagRecent.h"
#include "Sim/Playbook.h"

namespace tag {
namespace booth {

namespace {

std::string bS(int v) { return std::to_string(v); }
int bRound(double v) { return static_cast<int>(std::floor(v + 0.5)); }
bool bHas(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
std::string bCap(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}

std::string bOrd(int n) {
  switch (n) {
    case 1: return "1st";
    case 2: return "2nd";
    case 3: return "3rd";
    default: return bS(n) + "th";
  }
}

const char* bOrdWord(int n) {
  static const char* const k[] = {"zero", "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth"};
  return n >= 0 && n <= 10 ? k[n] : "next";
}

const char* bCountWord(int n) {
  static const char* const k[] = {"no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve"};
  return n >= 0 && n <= 12 ? k[n] : "a lot of";
}

// how many times, the way a man says it: "once", "twice", "three times", "11 times" ("" for none: a wording of its own)
std::string bTimes(int n) {
  if (n <= 0) return std::string();
  if (n == 1) return "once";
  if (n == 2) return "twice";
  return (n <= 12 ? std::string(bCountWord(n)) : std::to_string(n)) + " times";
}

std::string bDist(double toGo, double ballOn) {
  if (ballOn + toGo >= 99.5) return "goal";
  if (toGo < 0.6) return "inches";
  return bS((std::max)(1, bRound(toGo)));
}

std::string bDownDist(int down, double toGo, double ballOn) { return bOrd(down) + " and " + bDist(toGo, ballOn); }
// one down-and-distance, as a key (the setup skips one the result or the flag already said)
std::string bDdKey(const tag::Game& g, int down, double toGo, double ballOn) {
  return std::to_string(g.q) + "|" + std::to_string(down) + "|" + std::to_string(bRound(toGo * 10)) + "|" + std::to_string(bRound(ballOn * 10)) + "|" + teamKeyName(g.poss);
}

std::string bClock(double s) {
  const int t = (std::max)(0, static_cast<int>(std::ceil(s - 1e-6)));
  char b[16];
  std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
  return b;
}

const char* bQuarterName(int q) {
  switch (q) {
    case 1: return "first quarter";
    case 2: return "second quarter";
    case 3: return "third quarter";
    case 4: return "fourth quarter";
    default: return "overtime";
  }
}

bool bTwoHigh(Coverage c) { return c == Coverage::C2 || c == Coverage::C4 || c == Coverage::C2M || c == Coverage::C2T || c == Coverage::C6; }
bool bManCov(Coverage c) { return c == Coverage::C0 || c == Coverage::C1 || c == Coverage::C2M; }

std::string bCoverageWords(Coverage c) {
  switch (c) {
    case Coverage::C0: return "Cover Zero";
    case Coverage::C1: return "man coverage with a single safety";
    case Coverage::C2: return "Cover 2";
    case Coverage::C3: return "Cover 3";
    case Coverage::C4: return "quarters coverage";
    case Coverage::C2M: return "two-man";
    case Coverage::C3Fire: return "a fire zone";
    case Coverage::C2T: return "Tampa 2";
    case Coverage::C6: return "Cover 6";
  }
  return "zone";
}

const char* bMoveWords(const std::string& m) {
  if (m == "swim") return "a swim move";
  if (m == "rip") return "a rip move";
  if (m == "club") return "a club";
  if (m == "spin") return "a spin move";
  if (m == "bull") return "a bull rush";
  if (m == "longarm") return "a long arm";
  if (m == "diprip") return "a dip and rip";
  return "a quick move";
}

bool bIsOL(const Ent* e) { return e && e->isOL; }

std::string bRoute(const std::string& r) {
  static const std::pair<const char*, const char*> k[] = {
      {"go", "go route"}, {"fade", "fade"}, {"seam", "seam route"}, {"slant", "slant"}, {"quickout", "quick out"}, {"out", "out route"}, {"in", "in route"},
      {"dig", "dig route"}, {"curl", "curl"}, {"hitch", "hitch"}, {"comeback", "comeback"}, {"post", "post"}, {"corner", "corner route"}, {"flat", "flat route"},
      {"arrow", "arrow route"}, {"wheel", "wheel route"}, {"drag", "drag"}, {"shallow", "shallow cross"}, {"cross", "crossing route"}, {"sail", "sail route"},
      {"stick", "stick route"}, {"swing", "swing pass"}, {"angle", "angle route"}, {"snag", "snag route"}, {"outnup", "out and up"}, {"slantgo", "slant and go"},
      {"bubble", "bubble screen"}, {"deepcross", "deep cross"}, {"skinny", "skinny post"}, {"deepout", "deep out"}, {"whip", "whip route"}};
  for (const auto& kv : k)
    if (r == kv.first) return kv.second;
  return std::string();
}
bool bIsDL(const Ent* e) { return e && e->isDL; }

// the small grammar a template can't know: "1 yards", "a 8-yard"
void bGrammar(std::string& x) {
  for (const char* a : {" 1 yards", " -1 yards"})
    for (size_t at = x.find(a); at != std::string::npos; at = x.find(a, at + 1)) x.replace(at, std::strlen(a), std::string(a).substr(0, std::strlen(a) - 1));
  // "1 catches", "1 carries", "one sacks": the count's noun in the singular
  static const std::pair<const char*, const char*> kOne[] = {
      {" 1 catches", " 1 catch"},     {" 1 carries", " 1 carry"},       {" 1 passes", " 1 pass"},         {" 1 runs", " 1 run"},
      {" 1 tackles", " 1 tackle"},    {" 1 sacks", " 1 sack"},          {" 1 penalties", " 1 penalty"},   {" 1 plays", " 1 play"},
      {" 1 touchdowns", " 1 touchdown"}, {" 1 interceptions", " 1 interception"}, {" one sacks", " one sack"}, {" one tackles", " one tackle"},
      {" 1 attempts", " 1 attempt"},  {" 1 points", " 1 point"},        {" one catches", " one catch"},   {" one carries", " one carry"},
      {" one timeouts", " one timeout"}, {" 1 timeouts", " 1 timeout"}};
  for (const auto& kv : kOne)
    for (size_t at = x.find(kv.first); at != std::string::npos; at = x.find(kv.first, at + 1)) x.replace(at, std::strlen(kv.first), kv.second);
  static const char* const kAn[] = {"8-", "11-", "18-", "80-", "81-", "82-", "83-", "84-", "85-", "86-", "87-", "88-", "89-", "8 ", "11 ", "18 "};
  for (const char* n : kAn)
    for (const char* art : {"A ", " a "}) {
      const std::string from = std::string(art) + n;
      std::string to = from;
      to.replace(to.find(art[0] == 'A' ? "A" : "a"), 1, art[0] == 'A' ? "An" : "an");
      for (size_t at = x.find(from); at != std::string::npos; at = x.find(from, at + to.size())) x.replace(at, from.size(), to);
    }
}

// a line and the tail that goes with it: a tail that opens with a comma joins the line's last sentence ("Carter picks
// up 12" + ", and that's a first down."); one that opens with a space is a sentence of its own
std::string bJoin(const std::string& line, const std::string& tail) {
  if (tail.empty()) return line;
  if (line.empty()) {
    // (no line to join: the tail stands alone)
    size_t a = tail.find_first_not_of(", ");
    return a == std::string::npos ? std::string() : bCap(tail.substr(a));
  }
  if (tail[0] == ',') {
    if (line.back() == '.') return line.substr(0, line.size() - 1) + tail;
    // ("What a run! 25 yards!" can't take ", first down": the tail says it on its own)
    const size_t a = tail.find_first_not_of(", ");
    return a == std::string::npos ? line : line + " " + bCap(tail.substr(a));
  }
  return line + tail;
}

// a line that says back four words in a row of another ("...will want that one back" from both men); runs with a
// number in them ("on 2nd and 10") are facts, not an echo
bool bEcho(const std::string& a, const std::string& b) {
  auto words = [](const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (const char ch : s) {
      const unsigned char u = static_cast<unsigned char>(ch);
      if (std::isalnum(u) || ch == '\'') cur += static_cast<char>(std::tolower(u));
      else if (!cur.empty()) {
        w.push_back(cur);
        cur.clear();
      }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
  };
  const std::vector<std::string> wa = words(a), wb = words(b);
  if (wa.size() < 4 || wb.size() < 4) return false;
  auto gram = [](const std::vector<std::string>& w, size_t i, std::string& out) {
    out.clear();
    for (size_t k = i; k < i + 4; ++k) {
      if (std::isdigit(static_cast<unsigned char>(w[k][0]))) return false;
      out += w[k] + " ";
    }
    return true;
  };
  std::set<std::string> gb;
  std::string gm;
  for (size_t i = 0; i + 4 <= wb.size(); ++i)
    if (gram(wb, i, gm)) gb.insert(gm);
  for (size_t i = 0; i + 4 <= wa.size(); ++i)
    if (gram(wa, i, gm) && gb.count(gm)) return true;
  return false;
}

// a sentence as a short stable id (FNV-1a 64, hex): what the memory across games keeps of a story it has heard
std::string bTextId(const std::string& s) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (const unsigned char c : s) {
    h ^= c;
    h *= 0x100000001b3ull;
  }
  char b[24];
  std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
  return b;
}

}  // namespace

// ------------------------------------------------------------------ construction, queue

Booth::Booth(uint32_t seed, const Crew& crew) : crew_(crew), rng_(seed) { salt_ = seed; }

Line& Booth::push(Who who, Kind kind, const std::string& text, int energy, int prio, double delay, double ttl) {
  Line l;
  l.id = nextId_++;
  l.who = who;
  l.kind = kind;
  l.text = text;
  if (view_.night)
    for (size_t at = l.text.find(" today"); at != std::string::npos; at = l.text.find(" today", at + 8)) l.text.replace(at, 6, " tonight");
  l.say = spoken(l.text);
  l.energy = energy;
  l.prio = prio;
  l.notBefore = now_ + delay;
  l.expireAt = now_ + delay + ttl;
  // the small grammar a template can't know: "1 yards", "a 8-yard", "that's sixth"
  bGrammar(l.text);
  l.say = spoken(l.text);
  l.estSecs = estimate(l.say, who, energy);
  recentText_.push_back(l.text);
  while (recentText_.size() > 60) recentText_.pop_front();
  if (kind != Kind::Live) spokenSet_.insert(l.text);
  st_.queued++;
  q_.push_back(l);
  return q_.back();
}

void Booth::dropKind(Kind k) {
  for (auto it = q_.begin(); it != q_.end();)
    if (it->kind == k) {
      it = q_.erase(it);
      st_.expired++;
    } else
      ++it;
}

void Booth::dropStale() {
  for (auto it = q_.begin(); it != q_.end();)
    if (it->expireAt < now_) {
      it = q_.erase(it);
      st_.expired++;
    } else
      ++it;
}

void Booth::snapFlush() {
  for (auto it = q_.begin(); it != q_.end();)
    if (!it->live && !it->snapSafe) {
      it = q_.erase(it);
      st_.expired++;
    } else
      ++it;
  // the analyst (or a story) still going at the snap gives the floor to the call; a line nearly done finishes
  if (cur_.id && !cur_.live && cur_.kind != Kind::Score && cur_.who == Who::Color && curEnd_ - now_ > 0.8) cutForSnap_ = true;
}

const Line* Booth::next() {
  dropStale();
  if (cur_.id) return nullptr;
  int best = -1;
  for (size_t i = 0; i < q_.size(); ++i)
    if (q_[i].live && q_[i].notBefore <= now_) {
      if (best < 0 || q_[i].prio > q_[best].prio) best = static_cast<int>(i);
    }
  // while the ball is live (a play, a kick in the air) only the call of the action is heard: the two-minute warning, a
  // score or a story waits for the whistle
  const bool ballLive = (sim_ && snapped_ && sim_->isLive()) || (stage_ == View::Stage::Kick && kickFootSaid_ && !kickLandSaid_);
  if (best < 0 && ballLive) return nullptr;
  // everything else strictly in order: a conversation (the toss to the analyst, his answer) never runs backwards
  if (best < 0)
    for (size_t i = 0; i < q_.size(); ++i)
      if (!q_[i].live) {
        if (q_[i].notBefore <= now_) best = static_cast<int>(i);
        break;
      }
  if (best < 0) return nullptr;
  // a question (a toss, "you called it") whose answer is gone (dropped, expired) is not asked: it would hang in the air
  if (q_[best].tag == "question" && !q_[best].live && (best + 1 >= static_cast<int>(q_.size()) || q_[best + 1].tag != "answer")) {
    q_.erase(q_.begin() + best);
    st_.expired++;
    return nullptr;
  }
  const Line& l = q_[best];
  const bool talk = l.kind == Kind::Read || l.kind == Kind::Story || l.kind == Kind::Analysis;
  const bool answer = l.tag == "answer";  // (its question was asked: the answer comes)
  // a question needs the time for its answer too (Dan's half fit, and the snap cut Cal's)
  double need = l.estSecs + 0.8;
  if (l.tag == "question")
    for (int k = best + 1; k < static_cast<int>(q_.size()) && q_[k].tag == "answer"; ++k) need += q_[k].estSecs + 0.8;
  if (talk && !l.live && !answer && stage_ == View::Stage::Presnap &&
      (view_.canHold ? holdBudget_ - holdUsed_ < need : (l.kind != Kind::Read || view_.userGame == false))) {
    if (l.tag == "question")  // (its answers go with it: the reply and the reaction)
      while (best + 1 < static_cast<int>(q_.size()) && q_[best + 1].tag == "answer") q_.erase(q_.begin() + best + 1);
    q_.erase(q_.begin() + best);  // (the snap would cut it in half)
    st_.expired++;
    return nullptr;
  }
  // a breath between two men; a man going on in the same voice barely pauses
  const double gap = l.live ? 0.0 : (l.who != lastWho_ ? 0.45 + 0.25 * ((l.id * 7) % 5) / 4.0 : 0.15);
  if (now_ < lastEnd_ + gap) return nullptr;
  cur_ = l;
  q_.erase(q_.begin() + best);
  // a question asked keeps its answers: a snap or a kick does not flush them away from it
  if (cur_.tag == "question")
    for (int k = best; k < static_cast<int>(q_.size()) && q_[k].tag == "answer"; ++k) q_[k].snapSafe = true;
  if (memory_) heardNow(cur_);
  for (const auto& sf : sharedFull_)  // (the man a shared surname now means, for the next bare "Calloway")
    if (cur_.text.find(sf.full) != std::string::npos) lastBySurname_[sf.last] = sf.id;
  if (cur_.who == Who::Color) lastColorT_ = now_;
  curStart_ = now_;
  if (cur_.kind == Kind::Read && !cur_.tag.empty()) lastRead_ = cur_.tag;  // (a read cut by the snap is still a read he made)
  if (cur_.live && !cur_.tag.empty()) {
    spokenTags_.insert(cur_.tag);
    if (cur_.tag.rfind("throw:", 0) == 0 || cur_.tag.rfind("deep:", 0) == 0) spokenTarget_ = std::atoi(cur_.tag.c_str() + cur_.tag.find(':') + 1);
  }
  curEnd_ = now_ + cur_.estSecs;
  cutForSnap_ = false;
  st_.spoken++;
  if (cur_.live) st_.liveSpoken++;
  return &cur_;
}

void Booth::started(int id, double secs) {
  if (id == cur_.id) curEnd_ = now_ + secs;
}

void Booth::finished(int id) {
  if (id != cur_.id) return;
  if (now_ < curEnd_ - 0.05) st_.cut++;
  lastEnd_ = now_;
  lastWho_ = cur_.who;
  cur_ = Line();
  cutForSnap_ = false;
}

bool Booth::cutCurrent() const {
  if (!cur_.id) return false;
  if (cutForSnap_) return true;
  // a live call takes the floor from anything less urgent, except the score, a break, the open/close or the referee
  // (nobody talks over the official's announcement: the snap waits for it, wantsHold)
  if (cur_.kind == Kind::Score || cur_.kind == Kind::Open || cur_.kind == Kind::Close || cur_.kind == Kind::Ref) return false;
  const bool ownCall = cur_.who == Who::PBP && cur_.live;
  const double played = cur_.estSecs > 0 ? (now_ - curStart_) / cur_.estSecs : 1.0;
  for (const Line& l : q_)
    if (l.live && l.notBefore <= now_ && l.prio > cur_.prio && (!ownCall || l.prio >= 7 || played >= 0.8)) return true;
  return false;
}

std::vector<Line> Booth::forecast() const {
  std::vector<Line> out;
  for (const Line& l : q_) out.push_back(l);
  for (const std::string* t : {&pendingResult_})  // (the version without the down is made only if a flag comes)
    if (!t->empty()) {
      Line l;
      l.who = Who::PBP;
      l.kind = Kind::Result;
      l.text = *t;
      l.say = spoken(*t);
      l.energy = t == &pendingResult_ ? pendingResultEnergy_ : 1;
      out.push_back(l);
    }
  if (!nextSetup_.empty()) {
    // the setup the next snap will get (at the energy it will be said at: up a notch on third and fourth down)
    for (int e : {nextSetupEnergy_}) {
      Line l;
      l.who = Who::PBP;
      l.kind = Kind::Setup;
      l.text = nextSetup_;
      l.say = spoken(nextSetup_);
      l.energy = e;
      out.push_back(l);
    }
  }
  for (const auto& kv : plan_.at) {
    Line l;
    l.who = Who::PBP;
    l.kind = Kind::Live;
    l.text = kv.second.text;
    l.say = spoken(kv.second.text);
    l.energy = kv.second.energy;
    l.live = true;
    out.push_back(l);
  }
  return out;
}

uint64_t Booth::forecastStamp() const {
  // FNV-1a 64 over everything forecast() reads, in its order (voice, energy and words: what a line's audio is made from)
  uint64_t h = 0xcbf29ce484222325ull;
  auto mix = [&h](const std::string& words, int extra) {
    for (const unsigned char c : words) {
      h ^= c;
      h *= 0x100000001b3ull;
    }
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(extra)) + 0x9e3779b97f4a7c15ull;
    h *= 0x100000001b3ull;
  };
  for (const Line& l : q_) mix(l.say, l.energy * 3 + static_cast<int>(l.who));
  mix(pendingResult_, pendingResultEnergy_);
  mix(nextSetup_, nextSetupEnergy_);
  for (const auto& kv : plan_.at) mix(kv.second.text, kv.second.energy);
  return h;
}

bool Booth::wantsHold() const {
  if (stage_ != View::Stage::Presnap && stage_ != View::Stage::Wait && stage_ != View::Stage::Call) return false;
  // the open, a quarter break, halftime, the referee's announcement: always worth the wait (the kickoff waits for the
  // crew, the snap for the official); the breakdown of a play only when the presentation lets the booth hold the snap
  auto big = [](const Line& l) { return l.kind == Kind::Open || l.kind == Kind::Break || l.kind == Kind::Close || l.kind == Kind::Ref; };
  const bool breakTalk = (cur_.id && big(cur_)) || std::any_of(q_.begin(), q_.end(), big);
  if (breakTalk || breakDeadline_ >= 0) return holdUsed_ < 45.0;
  if (holdUsed_ >= holdBudget_) return false;
  if (cur_.id && cur_.prio >= 2 && !cur_.live && curEnd_ - now_ > 0.3) return true;
  for (const Line& l : q_)
    if (!l.live && l.prio >= 2 && l.notBefore <= now_ + 0.5) return true;
  return false;
}

std::vector<std::pair<std::string, int>> Booth::genericCalls() {
  // every live call that names nobody, with the energy it is said at (the presentation synthesizes these once)
  std::vector<std::pair<std::string, int>> v;
  auto at = [&v](int e, std::initializer_list<const char*> ts) {
    for (const char* t : ts) v.push_back({t, e});
  };
  at(1, {"Incomplete.", "Falls incomplete.", "Off the mark, incomplete.", "Throws it away.", "Gets rid of it, out of bounds.", "Out of bounds, no catch.",
         "Fielded.", "Handoff.", "Throws...", "Back to throw.", "Here's the kickoff.", "Here's the punt.", "And we're underway!",
         "And the kickoff is away.", "And the punt is away.", "Here's the free kick.", "Here comes the punt team.", "Here's the field goal try."});
  at(2, {"Caught!", "Got it!", "Makes the grab!", "Hauls it in!", "And he's got it!", "Makes the catch in traffic!", "Holds on with a man draped all over him!",
         "Snags it!", "Caught on the sideline!", "Makes the grab at the sideline!", "Caught over the middle!", "Got it, over the middle!", "Dropped!", "Drops it!", "Right through his hands!", "Can't hang on!", "Broken up!", "Knocked away!", "Batted down!", "Batted at the line!",
         "Knocked down at the line of scrimmage!", "Pressure!", "Here comes the rush!", "He's got company!", "Pocket's collapsing!", "Spin move!",
         "Makes a man miss!", "Runs him over!", "Stiff arm!", "Hurdles a man!", "He takes off!", "The snap, the hold, the kick...", "It's up..."});
  at(3, {"Diving catch!", "Lays out, and makes the grab!", "What a catch!", "He's got room!", "He's loose!", "Breaks free!", "Nobody's going to catch him!",
         "Oh, big hit!", "What a shot!", "Lowers the boom!", "Fumble! Ball's loose!", "He coughs it up! Loose ball!", "The ball is out!", "Midfield!",
         "Touchdown!", "He's in! Touchdown!", "He's going to score!", "Into the end zone!", "Intercepted!", "Sacked!", "Safety!", "He's in the clear!",
         "Recovered!"});
  for (int n = 10; n <= 40; n += 10) v.push_back({"The " + bS(n) + "!", 3});
  for (int n = 1; n <= 50; ++n) {
    v.push_back({"Brought down at the " + bS(n) + ".", 1});
    v.push_back({"Taken down at the " + bS(n) + ".", 1});
    v.push_back({"Finally brought down at the " + bS(n) + "!", 2});
    v.push_back({"Run down at the " + bS(n) + "!", 2});
    v.push_back({"Wrapped up at the " + bS(n) + ".", 1});
  }
  return v;
}

std::pair<std::string, int> Booth::genericAlt(const std::string& tag, const std::string& text, const std::string& fallback) {
  static const std::unordered_map<std::string, int> kGeneric = [] {
    std::unordered_map<std::string, int> m;
    for (const auto& gc : genericCalls()) m.emplace(gc.first, gc.second);
    return m;
  }();
  if (kGeneric.count(text)) return {};  // (the call names nobody: its audio is baked with the generics)
  if (auto it = kGeneric.find(fallback); it != kGeneric.end()) return {it->first, it->second};
  // a call made from a phrase bank with a name in it ("And Austin recovers!"): the generic call of its kind
  static const std::pair<const char*, const char*> kByTag[] = {
      {"catch", "Caught!"}, {"drop", "Dropped!"},      {"pbu", "Broken up!"},        {"recover", "Recovered!"},     {"fum", "Fumble! Ball's loose!"},
      {"int:", "Intercepted!"}, {"sack", "Sacked!"},   {"press", "Pressure!"},       {"td", "Touchdown!"},          {"field", "Fielded."},
      {"handoff", "Handoff."}, {"scramble", "He takes off!"}, {"away", "Throws it away."}, {"qbdrop", "Back to throw."}, {"throw:", "Throws..."},
      {"deep:", "Throws..."}, {"break", "He's loose!"}, {"hit", "Oh, big hit!"}, {"kset:punt", "Here comes the punt team."},
      {"kset:fg", "Here's the field goal try."}};
  for (const auto& kv : kByTag)
    if (tag.rfind(kv.first, 0) == 0)
      if (auto it = kGeneric.find(kv.second); it != kGeneric.end()) return {it->first, it->second};
  return {};
}

// ------------------------------------------------------------------ phrase choice

std::string Booth::pick(const std::string& bank, const std::vector<std::string>& opts) {
  std::vector<int> ids(opts.size());
  for (size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<int>(i);
  return pickFrom(bank, opts, ids);
}

std::string Booth::say(const std::string& bank, const Slots& s) {
  // the bank's wordings this moment can say (every fact each names is known), each under its index in the bank
  const std::vector<const char*>& w = bankWordings(bank);
  std::vector<std::string> opts;
  std::vector<int> ids;
  std::string o;
  for (size_t i = 0; i < w.size(); ++i)
    if (fillWording(w[i], s, o)) {
      opts.push_back(o);
      ids.push_back(static_cast<int>(i));
    }
  return pickFrom(bank, opts, ids);
}

std::string Booth::pickFrom(const std::string& bank, const std::vector<std::string>& opts, const std::vector<int>& ids) {
  if (opts.empty()) return std::string();
  std::deque<int>& rec = recentPick_[bank];
  const size_t n = opts.size();
  const size_t keep = n <= 1 ? 0 : (std::min)(n - 1, (std::max)(static_cast<size_t>(1), n * 2 / 3));
  auto inRec = [&rec, &ids](int i) { return std::find(rec.begin(), rec.end(), ids[static_cast<size_t>(i)]) != rec.end(); };
  auto inText = [this](const std::string& s) {
    return spokenSet_.count(s) > 0 || std::find(recentText_.begin(), recentText_.end(), s) != recentText_.end();
  };
  std::vector<int> ok;
  for (size_t i = 0; i < n; ++i)
    if (!inRec(static_cast<int>(i)) && !inText(opts[i])) ok.push_back(static_cast<int>(i));
  if (ok.empty())
    for (size_t i = 0; i < n; ++i)
      if (!inRec(static_cast<int>(i))) ok.push_back(static_cast<int>(i));
  if (ok.empty())
    for (size_t i = 0; i < n; ++i) ok.push_back(static_cast<int>(i));
  int i = ok[rng_.intRange(0, static_cast<int>(ok.size()) - 1)];
  if (memory_ && ok.size() > 1) {
    // across games (Game/TagRecent.h): of the wordings this game allows, the one heard least recently - never heard
    // first - so the next game does not open with the last one's lines (NHL's rule: all of them before any comes back)
    std::vector<std::string> keys;
    keys.reserve(ok.size());
    for (int k : ok) keys.push_back(bS(ids[static_cast<size_t>(k)]));
    const int j = Recent::get().leastRecent("booth." + bank, keys, salt_);
    if (j >= 0) i = ok[j];
  }
  rec.push_back(ids[static_cast<size_t>(i)]);
  while (rec.size() > keep) rec.pop_front();
  // heard when its line is said (next()); a wording written into a line that is dropped stays unheard
  if (memory_) {
    pendingPicks_.push_back({bank, ids[static_cast<size_t>(i)], opts[i]});
    while (pendingPicks_.size() > 160) pendingPicks_.pop_front();
  }
  return opts[i];
}

Slots Booth::facts(const Game& g, Slots s) const {
  // (a wording takes the first fact of a name: the caller's own come first and win)
  const Team& O = g.offense();
  const Team& D = g.defense();
  const int m = g.margin();
  const int hi = (std::max)(g.score.home, g.score.away), lo = (std::min)(g.score.home, g.score.away);
  const Team* lead = g.score.home == g.score.away ? nullptr : g.score.home > g.score.away ? g.home : g.away;
  const Team* trail = lead == g.home ? g.away : lead == g.away ? g.home : nullptr;
  s.push_back({"off", O.city});
  s.push_back({"def", D.city});
  s.push_back({"offn", nick(O)});
  s.push_back({"defn", nick(D)});
  s.push_back({"pf", crew_.pbpFirst});
  s.push_back({"cf", crew_.colorFirst});
  s.push_back({"ddb", bDownDist(downBefore_, toGoBefore_, losBefore_)});
  s.push_back({"dd", bDownDist(g.down, g.toGo, g.ballOn)});
  s.push_back({"qn", bQuarterName(g.q)});
  s.push_back({"clock", bClock(g.clock)});
  s.push_back({"seg", g.q == 2 ? std::string("half") : g.q >= 5 ? std::string("overtime") : g.q == 4 ? std::string("game") : std::string(bQuarterName(g.q))});
  s.push_back({"leader", lead ? lead->city : std::string()});
  s.push_back({"trailer", trail ? trail->city : std::string()});
  s.push_back({"hi", bS(hi)});
  s.push_back({"lo", bS(lo)});
  s.push_back({"margin", bS(std::abs(m))});
  s.push_back({"tied", hi != lo ? std::string() : hi == 0 ? std::string("still scoreless") : "tied at " + bS(hi)});
  s.push_back({"sc", hi == lo ? (hi == 0 ? std::string("still scoreless") : "tied at " + bS(hi)) : lead->city + " " + bS(hi) + ", " + trail->city + " " + bS(lo)});
  s.push_back({"sit", situation(g)});
  return s;
}

void Booth::heardNow(const Line& l) {
  // the wordings picked for this line (matched by their words: a night game's line says "tonight" for "today", the
  // grammar fixed "a 8-yard", and a wording joined to the next one lost its full stop)
  auto in = [&l, this](const std::string& words) {
    if (words.empty()) return false;
    std::string w = words;
    bGrammar(w);
    if (view_.night)
      for (size_t at = w.find(" today"); at != std::string::npos; at = w.find(" today", at + 8)) w.replace(at, 6, " tonight");
    if (l.text.find(w) != std::string::npos) return true;
    while (!w.empty() && (w.back() == '.' || w.back() == '!' || w.back() == '?')) w.pop_back();
    if (!w.empty() && w[0] == ' ') w.erase(0, 1);
    return w.size() > 6 && l.text.find(w) != std::string::npos;
  };
  for (auto it = pendingPicks_.begin(); it != pendingPicks_.end();)
    if (in(it->text)) {
      Recent::get().use("booth." + it->bank, bS(it->idx));
      it = pendingPicks_.erase(it);
    } else
      ++it;
  // a story's sentence as said (its numbers are part of it: only a sentence that is word for word the same waits)
  if (l.kind == Kind::Story || l.kind == Kind::Break) Recent::get().use("booth.story", bTextId(l.text));
}

std::string Booth::rot(int pid, const std::vector<std::string>& opts) {
  // three of the ways to say it belong to this man, taken in turn play by play
  if (opts.empty()) return std::string();
  const size_t n = opts.size();
  const size_t k = (std::min)(n, static_cast<size_t>(3));
  const size_t base = static_cast<size_t>((pid * 2654435761u) % n);
  // the next of his three each time one is actually said (by play number, the same call came back on every third play)
  const std::string fam = opts[0];
  const std::string out = opts[(base + static_cast<size_t>(rotUse_[fam]) % k) % n];
  rotFamily_[out] = fam;
  return out;
}

bool Booth::cooled(const std::string& topic, double secs) {
  auto it = topicT_.find(topic);
  return it == topicT_.end() || now_ - it->second >= secs;
}

bool Booth::once(const std::string& topic) {
  if (said_.count(topic)) return false;
  said_.insert(topic);
  return true;
}

// ------------------------------------------------------------------ names and places

std::string Booth::nm(const Player* p, bool full) {
  if (!p) return "the ball carrier";
  if (full) return p->first + " " + p->last;
  // a surname two men in this game share (teammates or not): always the full name ("Tyrell Ramsey knocks it away from
  // Ramsey" named nobody). The match-start prewarm (rosterCalls) makes the same full-name calls.
  if (shared_.count(p->last)) return p->first + " " + p->last;
  return p->last;
}

std::string Booth::ln(const Player* p) { return p ? nm(p) : std::string(); }

// a team's giveaways as the stat crew counts them: interceptions thrown, fumbles lost, and a takeaway handed right back
// (the sim's TeamLine::turnovers is net changes of possession: an interception fumbled back counts for nobody)
int Booth::giveaways(const Game& g, const TeamLine& t) const { return t.ints + t.fumblesLost + giveBack_[&t == &g.team.home ? 0 : 1]; }

static const char* bPosWord(Pos p) {
  switch (p) {
    case Pos::QB: return "quarterback";
    case Pos::RB: return "running back";
    case Pos::FB: return "fullback";
    case Pos::WR: return "receiver";
    case Pos::TE: return "tight end";
    case Pos::OT: return "tackle";
    case Pos::OG: return "guard";
    case Pos::C: return "center";
    case Pos::DE: return "defensive end";
    case Pos::DT: return "defensive tackle";
    case Pos::LB: return "linebacker";
    case Pos::CB: return "corner";
    case Pos::S: return "safety";
    case Pos::K: return "kicker";
    case Pos::P: return "punter";
  }
  return "player";
}

std::string Booth::pname(const Player* p) {
  if (!p) return "the ball carrier";
  int& m = mentions_[p->id];
  ++m;
  if (m > 1) return nm(p);
  // the first time a man is named: his team and his job, the way a broadcast introduces him ("Tritons linebacker DeShawn Lockhart")
  if (writerGame_ && (writerGame_->home->hasPlayer(p) || writerGame_->away->hasPlayer(p)))
    return nick(teamOfPlayer(*writerGame_, p)) + " " + bPosWord(p->pos) + " " + p->first + " " + p->last;
  return p->first + " " + p->last;
}

const Team& Booth::teamOfPlayer(const Game& g, const Player* p) const { return g.home->hasPlayer(p) ? *g.home : *g.away; }

std::string Booth::nick(const Team& t) {
  std::string n = t.name;
  if (n.size() > 3 && n.compare(n.size() - 3, 3, " FC") == 0) n.erase(n.size() - 3);
  return n;
}

std::string Booth::teamRef(const Team& t) { return chance(0.3) ? "the " + nick(t) : t.city; }

std::string Booth::spot(const Game& g, double ballOn, TeamKey side) const {
  const int yl = bRound((std::max)(1.0, (std::min)(99.0, ballOn)));
  if (yl == 50) return "midfield";
  const Team& own = g.teamOf(side);
  const Team& opp = g.teamOf(other(side));
  if (yl < 50) return "the " + own.city + " " + bS(yl);
  return "the " + opp.city + " " + bS(100 - yl);
}

std::string Booth::fieldX(double x) const {
  const int yl = bRound((std::max)(1.0, (std::min)(99.0, x)));
  if (yl == 50) return "midfield";
  return "the " + bS(yl < 50 ? yl : 100 - yl);
}

std::string Booth::clockPhrase(const Game& g) const {
  const std::string c = bClock(g.clock);
  if (g.q == 2 || g.q == 4) return c + " to go in the " + (g.q == 2 ? std::string("half") : std::string("game"));
  if (g.q >= 5) return c + " left in overtime";
  return c + " left in the " + bQuarterName(g.q);
}

std::string Booth::scoreLine(const Game& g, bool withTime) {
  const int h = g.score.home + adjHome_, a = g.score.away + adjAway_;
  std::string s;
  if (h == a) s = h == 0 ? "Still scoreless" : "We're tied at " + bS(h);
  else if (h > a) s = g.home->city + " " + bS(h) + ", " + g.away->city + " " + bS(a);
  else s = g.away->city + " " + bS(a) + ", " + g.home->city + " " + bS(h);
  if (withTime && g.phase != GamePhase::Final) s += ", " + clockPhrase(g);
  return s + ".";
}

std::string Booth::situation(const Game& g) const {
  const int m = g.margin();
  if (m == 0) return "in a tie game";
  const int a = std::abs(m);
  return std::string(m > 0 ? "up " : "down ") + bS(a);
}

// ------------------------------------------------------------------ the frame

void Booth::tick(double dt, const Game& g, const View& v) {
  if (v.stage == View::Stage::Paused) return;
  view_ = v;
  writerGame_ = &g;
  if (breakDeadline_ >= 0 && (v.stage == View::Stage::Kick || v.stage == View::Stage::Live)) {
    breakFallback_.clear();  // (the second half has started: too late for the package)
    breakDeadline_ = -1;
  }
  // the writer's break package, or the template line if it did not come in time
  while (!writerReady_.empty() && writerReady_.front().kind != "exchange") {
    if (breakDeadline_ >= 0)  // (in time: the package replaces the fallback; late: it is dropped)
      for (const auto& ln : writerReady_.front().lines) push(ln.first, Kind::Break, ln.second, 1, 4, 0, 60).snapSafe = true;
    writerReady_.pop_front();
    breakFallback_.clear();
    breakDeadline_ = -1;
  }
  if (breakDeadline_ >= 0 && now_ > breakDeadline_) {
    if (!breakFallback_.empty()) push(Who::Color, Kind::Break, breakFallback_, 1, 4, 0, 40).snapSafe = true;
    breakFallback_.clear();
    breakDeadline_ = -1;
  }
  now_ += dt;
  stageT_ += dt;
  if (v.stage != stage_) {
    const View::Stage was = stage_;
    stage_ = v.stage;
    stageT_ = 0;
    if (v.stage == View::Stage::Presnap || (v.stage == View::Stage::Wait && was != View::Stage::Call)) {
      holdUsed_ = 0;
      holdBudget_ = v.canHold ? 14.0 : 0.0;
    }
  }
  if ((stage_ == View::Stage::Presnap || stage_ == View::Stage::Wait || stage_ == View::Stage::Call) && wantsHold()) holdUsed_ += dt;
  if (!opened_ && v.stage != View::Stage::Menu) {
    opened_ = true;
    score_ = g.score;
    onOpen(g, v);
  }
  // the play on the field
  if (v.sim && (v.sim != sim_ || v.sim->t + 1e-9 < simT_ || v.sim->events.size() < evSeen_)) {
    sim_ = v.sim;
    if (sim_->t > 0 && !sim_->isLive()) {
      // a play that is already over (the last play's sim, shown again after a kick): nothing new to call
      evSeen_ = sim_->events.size();
      snapped_ = whistled_ = appliedThisPlay_ = true;
    } else {
      evSeen_ = 0;
      snapped_ = whistled_ = false;
      onNewPlay(g, *sim_);
    }
  }
  if (!v.sim) sim_ = nullptr;
  // the log first (a kick's or a two-point try's line arrives with its stage; a play's at ApplyPlay)
  while (logSeen_ < g.log.size()) {
    const size_t i = logSeen_++;
    onLog(g, g.log[i], i);
  }
  while (zoneSeen_ < g.abil.history.size()) onZone(g, g.abil.history[zoneSeen_++]);
  if (sim_) {
    simT_ = sim_->t;
    if (!snapped_ && sim_->t > 0) {
      snapped_ = true;
      onSnap(g, *sim_);
    }
    if (snapped_) {
      while (evSeen_ < sim_->events.size()) onEvent(g, *sim_, sim_->events[evSeen_++]);
      if (sim_->isLive()) {
        livePocket(g, *sim_);
        trackCarrier(g, *sim_);
      }
    }
    if (snapped_ && !sim_->isLive() && !whistled_ && sim_->result) {
      whistled_ = true;
      onWhistle(g, *sim_, *sim_->result);
    }
  }
  if (v.stage == View::Stage::Kick || kickPending_ || (sim_ && sim_->isReturn())) onKickTick(g, v);
  if ((v.stage == View::Stage::Wait || v.stage == View::Stage::Call) && g.phase == GamePhase::Scrimmage)
    preKick_ = PreKick{g.down, g.toGo, g.ballOn, g.margin(), g.q, tier(g)};
  // the next setup, written as soon as the down is known (the voice gets the whole between-plays to make it); one
  // that names the clock is written at the snap instead (the runoff comes off before it)
  if ((v.stage == View::Stage::Wait || v.stage == View::Stage::Post || v.stage == View::Stage::Replay) && g.phase == GamePhase::Scrimmage) {
    const std::string key = std::to_string(g.q) + "|" + std::to_string(g.down) + "|" + std::to_string(bRound(g.toGo * 10)) + "|" +
                            std::to_string(bRound(g.ballOn * 10)) + "|" + teamKeyName(g.poss);
    const bool late = (g.q == 2 || g.q >= 4) && g.clock <= 170;
    if (key != nextSetupKey_ && !late) {
      nextSetupKey_ = key;
      nextSetup_ = setupLine(g, false);
      nextSetupEnergy_ = g.down >= 3 ? 1 : 0;
    }
  }
  if (v.stage == View::Stage::Presnap && sim_ && !sim_->isReturn() && !presnapSaid_) onPresnapEnter(g, v);
  if (v.stage == View::Stage::Presnap && sim_) presnapT_ += dt;
  if (v.stage == View::Stage::Presnap && sim_ && presnapSaid_ && !readSaid_ && presnapT_ > 0.2) {
    readSaid_ = true;
    // the read only when there is a moment for it: a user's offense taking its time, or the booth may hold the snap
    if (v.userGame || v.canHold) {
      if (auto r = presnapRead(g, *sim_)) {
        Line& rl = push(Who::Color, Kind::Read, *r, 1, 2, 0.1, 8);
        rl.tag = readTopic_;
        rl.topic = readTopic_;
      }
    }
  }
  // between the plays once the last one is applied (in Post / Replay the Game still has the old down)
  // a written exchange joins the line the moment it arrives, while the ball is dead (after a score or a change of hands the
  // booth is never quiet before the next snap: waiting for silence, they expired unaired)
  if ((stage_ == View::Stage::Wait || stage_ == View::Stage::Call || stage_ == View::Stage::Presnap) && (!sim_ || appliedThisPlay_ || !whistled_) &&
      !writerReady_.empty() && writerReady_.front().kind == "exchange")
    airWritten(g);
  if ((stage_ == View::Stage::Wait || stage_ == View::Stage::Call || stage_ == View::Stage::Presnap) && (!sim_ || appliedThisPlay_ || !whistled_)) idle(g, v);
  if (v.stage == View::Stage::Final && !closed_) {
    closed_ = true;
    onFinal(g);
  }
  poss_ = g.poss;
  phase_ = g.phase;
}

// ------------------------------------------------------------------ the open

void Booth::onOpen(const Game& g, const View& v) {
  const Team& H = *g.home;
  const Team& A = *g.away;
  const Team* hp = &H;
  const Team* ap = &A;
  sharedTeamOf_ = [hp, ap](const Player* p) { return hp->hasPlayer(p) ? hp->abbr : ap->abbr; };
  for (const Team* t : {hp, ap}) {
    properNames_.insert(t->city.substr(0, t->city.find(' ')));
    properNames_.insert(nick(*t));
    for (const auto& pl : t->roster) {
      properNames_.insert(pl->first);
      properNames_.insert(pl->last);
    }
  }
  properNames_.insert(crew_.pbpFirst);
  properNames_.insert(crew_.colorFirst);
  {
    // a surname two men in this game share (teammates or not) is said with the first name
    std::map<std::string, int> n;
    for (const Team* t : {&H, &A})
      for (const auto& p : t->roster) n[p->last]++;
    for (const auto& kv : n)
      if (kv.second > 1) shared_.insert(kv.first);
    for (const Team* t : {&H, &A})
      for (const auto& p : t->roster)
        if (shared_.count(p->last)) sharedFull_.push_back({p->first + " " + p->last, p->id, p->last});
  }
  const std::string pbp = crew_.pbpFirst + " " + crew_.pbpLast;
  const std::string col = crew_.colorFirst + " " + crew_.colorLast;
  const std::string tod = v.night ? "evening" : "afternoon";
  const Slots show = facts(g, {{"tod", tod}, {"hcity", H.city}, {"hname", H.name}, {"acity", A.city}, {"aname", A.name}, {"pbp", pbp}, {"col", col}});
  push(Who::PBP, Kind::Open, say("open1", show), 1, 4, 0.4, 60).snapSafe = true;
  push(Who::PBP, Kind::Open, say("open2", show), 1, 4, 0, 60).snapSafe = true;
  // the analyst's key to the game: the schemes, and the best player on each side of the ball
  auto best = [](const Team& t, std::initializer_list<Pos> ps) -> const Player* {
    const Player* b = nullptr;
    for (const auto& p : t.roster)
      for (Pos q : ps)
        if (p->pos == q && p->injuredWeeks <= 0 && (!b || p->ovr > b->ovr)) b = p.get();
    return b;
  };
  const Player* hq = best(H, {Pos::QB});
  const Player* aq = best(A, {Pos::QB});
  const Player* hRush = best(H, {Pos::DE, Pos::DT, Pos::LB});
  std::string key;
  openKey_ = !A.offScheme.empty() && !H.defScheme.empty() ? hRush : nullptr;  // the man the analyst names (halftime and the close come back to him)
  if (!A.offScheme.empty() && !H.defScheme.empty())
    key = say("openkey", facts(g, {{"acity", A.city}, {"aoff", A.offScheme}, {"hdef", H.defScheme}, {"aqb", aq ? nm(aq) : std::string("the quarterback")},
                                   {"rush", hRush ? hRush->first + " " + hRush->last : "the " + nick(H) + " front"}}));
  else
    key = say("openkey2", facts(g, {{"hqb", hq ? nm(hq) : std::string()}, {"aqb", aq ? nm(aq) : std::string()}}));
  push(Who::Color, Kind::Open, key, 1, 4, 0, 60).snapSafe = true;
  // the weather, when it is part of the story
  const Weather& w = g.weather;
  const int mph = bRound(std::hypot(w.windX, w.windY) * 2.045);
  const Slots wxs = facts(g, {{"hcity", H.city}, {"temp", bS(bRound(w.tempF))}, {"mph", mph >= 10 ? bS(mph) : std::string()}});
  std::string wx;
  if (w.precip == Precip::Snow) wx = say("wx_snow", wxs);
  else if (w.precip == Precip::Rain) wx = say("wx_rain", wxs);
  else if (w.tempF <= 32) wx = say("wx_cold", wxs);
  else if (mph >= 14) wx = say("wx_wind", wxs);
  if (!wx.empty()) push(Who::PBP, Kind::Open, wx, 1, 3, 0, 60).snapSafe = true;
}

// ------------------------------------------------------------------ the log (Game's own lines)

void Booth::onZone(const Game& g, const AbilityEvent& e) {
  if (!e.p) return;
  const Player& p = *e.p;
  const std::string full = p.first + " " + p.last;
  const std::string& last = p.last;
  const Team& T = teamOfPlayer(g, e.p);
  // The game's "zone" is a meter; a broadcast only knows a man who is having a day. Nothing is said when the meter
  // drops, and a hot streak is called only when his stat line has earned it (the line is said with it).
  if (!e.entered) return;
  std::string line;
  for (const PlayerLine* ln : g.stats.forTeam(T.abbr))
    if (ln->pid == p.id) {
      if (ln->passAtt >= 6 && (ln->passYds >= 150 || ln->passTD >= 2))
        line = bS(ln->passComp) + " of " + bS(ln->passAtt) + " for " + bS(ln->passYds) + " yards" +
               (ln->passTD ? std::string(", ") + bCountWord(ln->passTD) + (ln->passTD == 1 ? " touchdown" : " touchdowns") : std::string());
      else if (ln->rec >= 4 || ln->recYds >= 70) line = bS(ln->rec) + (ln->rec == 1 ? " catch" : " catches") + " for " + bS(ln->recYds) + " yards";
      else if (ln->rushYds >= 60 || (ln->rushTD >= 1 && ln->rushYds >= 30)) line = bS(ln->rushYds) + " yards on " + bS(ln->rushAtt) + " carries";
      else if (ln->sacks >= 2) line = std::string(bCountWord(ln->sacks)) + " sacks";
      else if (ln->ints >= 1) line = ln->ints == 1 ? std::string("an interception") : std::string(bCountWord(ln->ints)) + " interceptions";
      else if (ln->tackles >= 6) line = bS(ln->tackles) + " tackles";
      else if (ln->sacks >= 1 && ln->tackles >= 3) line = "a sack and " + bS(ln->tackles) + " tackles";
    }
  if (line.empty() || !cooled("hot:" + bS(p.id), 900)) return;
  mark("hot:" + bS(p.id));
  const Slots zs = facts(g, {{"full", full}, {"last", last}, {"team", T.city}, {"nick", nick(T)}, {"line", line}});
  push(Who::PBP, Kind::Story, say("zone_in", zs), 2, 4, 0.3, 25).snapSafe = true;
  // what a man this hot does to a game is said the first time (and once more, much later)
  if (!cooled("zonein:" + bS(p.id), 1500)) return;
  mark("zonein:" + bS(p.id));
  // what the man in the zone does to a game, ability by ability
  const char* bank = nullptr;
  switch (e.id) {
    case AbilityId::IceVeins: bank = "z_iceveins"; break;
    case AbilityId::RocketArm: bank = "z_rocketarm"; break;
    case AbilityId::Juggernaut: bank = "z_juggernaut"; break;
    case AbilityId::Phantom: bank = "z_phantom"; break;
    case AbilityId::Spotlight: bank = "z_spotlight"; break;
    case AbilityId::Slipstream: bank = "z_slipstream"; break;
    case AbilityId::Fortress: bank = "z_fortress"; break;
    case AbilityId::Havoc: bank = "z_havoc"; break;
    case AbilityId::BlackHole: bank = "z_blackhole"; break;
    case AbilityId::Enforcer: bank = "z_enforcer"; break;
    case AbilityId::Lockdown: bank = "z_lockdown"; break;
    case AbilityId::LastLine: bank = "z_lastline"; break;
    case AbilityId::Automatic: bank = "z_automatic"; break;
    default: break;
  }
  const std::string c = bank ? say(bank, zs) : std::string();
  if (!c.empty()) push(Who::Color, Kind::Analysis, c, 1, 3, 0.2, 30).snapSafe = true;
}

void Booth::flushDeferred(const Game& g) {
  if (deferredLogs_.empty()) return;
  const std::vector<size_t> held = deferredLogs_;
  deferredLogs_.clear();
  flushingLogs_ = true;
  for (const size_t i : held)
    if (i < g.log.size()) onLog(g, g.log[i], i);
  flushingLogs_ = false;
}

void Booth::onLog(const Game& g, const PlayLog& l, size_t index) {
  const std::string& d = l.desc;
  (void)index;
  if (bHas(d, "Coin toss")) {
    const Team& recv = g.teamOf(g.openingReceiver);
    const Team& kick = g.teamOf(other(g.openingReceiver));
    push(Who::PBP, Kind::Open,
         say("toss", facts(g, {{"kcity", kick.city}, {"kname", kick.name}, {"rcity", recv.city}, {"rname", recv.name}})), 0, 3, 0, 60)
        .snapSafe = true;
    return;
  }
  // a clock stop logged while the play's own line is still to come (the two-minute warning, a timeout): heard after the
  // play is called, not before its result
  if ((bHas(d, "Two-minute warning") || bHas(d, "Timeout ")) && !flushingLogs_ && sim_ && whistled_ && !appliedThisPlay_ && !sim_->isReturn() &&
      g.phase == GamePhase::Scrimmage) {
    deferredLogs_.push_back(index);
    return;
  }
  if (bHas(d, "Two-minute warning")) {
    push(Who::PBP, Kind::Break, say("2min", facts(g, {})), 1, 4, 0.2, 30).snapSafe = true;
    const int to = g.timeouts[g.poss];
    const int m = g.margin();
    const std::string tail = say(to >= 2 ? "2min4tail2" : "2min4tail1", facts(g, {}));
    const Slots cs = facts(g, {{"to", bCountWord(to)}, {"tos", to == 1 ? "timeout" : "timeouts"}, {"tail", tail}});
    std::string c;
    if (g.q == 4 && m < 0 && m >= -8) c = say("2min4close", cs);
    else if (g.q == 4 && m < -8 && m >= -16) c = say("2min4two", cs);
    else if (g.q == 4 && m < -16) c = say("2min4done", cs);
    else if (g.q == 4 && m > 8) c = say("2min4ahead", cs);
    else if (g.q == 4 && m > 0) c = say("2min4close_up", cs);
    else if (g.q == 2) c = say("2minq2", cs);
    if (!c.empty()) push(Who::Color, Kind::Break, c, 1, 3, 0, 30).snapSafe = true;
    return;
  }
  if (bHas(d, "Timeout ")) {
    // "Timeout ABQ (2 left)."
    const std::string ab = d.substr(8, d.find(' ', 8) - 8);
    const Team& t = g.home->abbr == ab ? *g.home : *g.away;
    const TeamKey k = &t == g.home ? TeamKey::Home : TeamKey::Away;
    const int left = g.timeouts[k];
    if (sim_ && sim_->cfg.offPlay && sim_->cfg.offPlay->type == PlayType::Kneel && &t == &g.offense() && once("odd_to" + bS(g.q)))
      push(Who::Color, Kind::Break, "I'm not sure what " + t.city + " is doing with those timeouts. They're the ones kneeling it.", 1, 3, 0.4, 15).snapSafe = true;
    const Slots ts = facts(g, {{"team", t.city}, {"nick", nick(t)}, {"left", bCountWord(left)}});
    push(Who::PBP, Kind::Break, say(left == 0 ? "timeout0" : "timeout", ts), 1, 3, 0.3, 20).snapSafe = true;
    return;
  }
  if (bHas(d, "End of ")) {
    const int qEnded = l.q;
    if (qEnded == 2 || qEnded >= 4) return;  // halftime / the final / overtime have their own calls
    push(Who::PBP, Kind::Break, say("qend", facts(g, {{"qn", bQuarterName(qEnded)}})), 1, 4, 0.5, 40).snapSafe = true;
    if (writer_) {
      if (auto st = quarterStory(g)) breakFallback_ = *st;
      breakDeadline_ = now_ + 40;
      writerBusy_ = false;  // (a break outranks an exchange still being written)
      askWriter(g, "quarter");
      return;
    }
    if (auto st = quarterStory(g)) push(Who::Color, Kind::Break, *st, 1, 3, 0, 40).snapSafe = true;
    return;
  }
  if (bHas(d, "Halftime")) {
    push(Who::PBP, Kind::Break, say("half", facts(g, {})), 1, 5, 0.6, 60).snapSafe = true;
    if (writer_) {
      if (auto st = halftimeStory(g)) breakFallback_ = *st;
      breakDeadline_ = now_ + 45;
      writerBusy_ = false;
      askWriter(g, "half");
    } else if (auto st = halftimeStory(g))
      push(Who::Color, Kind::Break, *st, 1, 4, 0, 60).snapSafe = true;
    // who gets the ball: the team that kicked off to open the game receives the second half
    const Team& second = g.teamOf(other(g.openingReceiver));
    push(Who::PBP, Kind::Break, say("half_recv", facts(g, {{"team", second.city}, {"nick", nick(second)}})), 0, 3, 0, 60).snapSafe = true;
    return;
  }
  if (bHas(d, "Overtime!")) {
    push(Who::PBP, Kind::Break, say("ot", facts(g, {})), 2, 5, 0.5, 40).snapSafe = true;
    return;
  }
  if (bHas(d, "FINAL")) return;  // onFinal (the Final stage) closes the show
  if (bHas(d, "Injury: ")) {
    // "Injury: CB J.Smith (ABQ) is down and will not return."  The play's own call comes first (said after it)
    const size_t a = d.find(' ', 8);
    const std::string logName = d.substr(a + 1, d.find(" (") - a - 1);
    const std::string ab = d.substr(d.find(" (") + 2, 3);
    const Team& t = g.home->abbr == ab ? *g.home : *g.away;
    const Player* who = nullptr;
    for (const auto& p : t.roster)
      if (!p->first.empty() && logName == std::string(1, p->first[0]) + "." + p->last) who = p.get();
    static const char* const kPos[] = {"quarterback", "running back", "fullback", "receiver", "tight end", "tackle", "guard", "center", "defensive end",
                                       "defensive tackle", "linebacker", "cornerback", "safety", "kicker", "punter"};
    const std::string full = who ? t.city + " " + kPos[static_cast<int>(who->pos)] + " " + who->first + " " + who->last : logName;
    const std::string iname = who ? nm(who) : logName;
    const Slots is = facts(g, {{"full", full}, {"name", iname}, {"team", t.city}});
    pendingInjury_.push_back(say("inj_down", is));
    injuryWord_.push_back({iname, playNo_ + 3});
    bool good = false;
    if (who)
      for (const PlayerLine* ln : g.stats.forTeam(t.abbr))
        if (ln->pid == who->id) good = ln->tackles >= 4 || ln->rec >= 3 || ln->rushYds >= 40 || ln->sacks >= 1 || ln->ints >= 1 || (ln->passAtt >= 8 && ln->passComp * 10 >= ln->passAtt * 6);
    pendingInjury_.push_back(say(good ? "injuryg" : "injury", is));
    return;
  }
  // a kick line: called on the kick's own clock (onKickTick), not when the Game rolls it
  const bool kick = bHas(d, " kicks ") || bHas(d, " kicks off") || bHas(d, " punts ") || bHas(d, " punt is BLOCKED") || bHas(d, "field goal is") ||
                    bHas(d, "extra point is") || bHas(d, "onside kick");
  if (kick && sim_ && sim_->isReturn() && whistled_) {
    // a live return's line (finishKickReturn): the booth called it from the play itself; a score in it is counted
    if (sim_->result && sim_->result->td) {
      const TeamKey scorer = *sim_->result->td == Side::Off ? g.poss : other(g.poss);
      PlayResult none;
      onScored(g, scorer, 6, none);
    }
    returnFoul(g, d);
    return;
  }
  if (kick) {
    kickLog_ = l;
    kickPending_ = true;
    kickFootSaid_ = kickLandSaid_ = false;
    kickSetupSaid_ = false;
    kickKind_ = bHas(d, "field goal") || bHas(d, "extra point") ? 2 : bHas(d, "punt") ? 1 : 0;
    // a kick that the presentation does not fly (headless, an end-of-half auto kick) is called at once
    if (view_.stage != View::Stage::Kick) onKickTick(g, view_);
    return;
  }
  if (bHas(d, "Two-point attempt")) {
    const bool good = bHas(d, "GOOD!");
    push(Who::PBP, Kind::Score, scoreSay(g, tdBy_, say(good ? "pt2_good" : "pt2_fail", facts(g, {{"team", g.teamOf(tdBy_).city}}))), good ? 2 : 1, 4, 0.4, 20).snapSafe = true;
    if (driveAfterTry_) {
      driveAfterTry_ = false;
      afterDrive(g, "td");
      drive_.active = false;
    }
    return;
  }
  if (l.big == LogTag::Penalty) {
    onPenalty(g, l);
    return;
  }
  // a scrimmage play the presentation just applied
  if (sim_ && sim_->result && whistled_ && !appliedThisPlay_) {
    appliedThisPlay_ = true;
    afterApply(g, *sim_, *sim_->result, l);
  }
}

// ---- the referee: a foul as the log names it, as he says it at his microphone, and its yardage (Sim/Game.cpp)
namespace {
struct RefFoul {
  const char* log;
  const char* words;
  int yards;  // 0: a spot foul
};
const RefFoul kRefFouls[] = {{"Offensive Holding", "Holding", 10},
                             {"Defensive Holding", "Holding", 5},
                             {"Defensive Pass Interference", "Pass interference", 0},
                             {"Offensive Pass Interference", "Pass interference", 10},
                             {"Illegal Contact", "Illegal contact", 5},
                             {"Roughing the Passer", "Personal foul, roughing the passer", 15},
                             {"Face Mask", "Personal foul, face mask", 15},
                             {"Unnecessary Roughness", "Personal foul, unnecessary roughness", 15},
                             {"Illegal Block in the Back", "Illegal block in the back", 10},
                             {"Illegal Use of Hands", "Illegal use of hands", 5},
                             {"Intentional Grounding", "Intentional grounding", 10},
                             {"False Start", "False start", 5},
                             {"Delay of Game", "Delay of game", 5},
                             {"Offside", "Offside", 5},
                             {"Encroachment", "Encroachment", 5},
                             {"Neutral Zone Infraction", "Neutral zone infraction", 5},
                             {"Illegal Formation", "Illegal formation", 5},
                             {"Illegal Shift", "Illegal shift", 5},
                             {"Too Many Men on the Field", "Too many men on the field", 5},
                             {"Holding", "Holding", 10}};
const RefFoul* bRefFoul(const std::string& logName) {
  for (const RefFoul& f : kRefFouls)
    if (logName == f.log) return &f;
  return nullptr;
}
// "10-yard penalty" (the caption keeps the digits; the voice reads "ten yard penalty")
std::string bPenYards(int n) { return bS(n) + "-yard penalty"; }
// the man the log names in "(#72 J.Smith)" on this team, or nobody
const Player* bLogMan(const Team& t, const std::string& who) {
  if (who.empty()) return nullptr;
  const int num = std::atoi(who.c_str() + 1);
  const size_t sp = who.find(' ');
  const std::string nmLog = sp == std::string::npos ? std::string() : who.substr(sp + 1);
  for (const auto& p : t.roster)
    if (p->number == num && !p->first.empty() && nmLog == std::string(1, p->first[0]) + "." + p->last) return p.get();
  return nullptr;
}
}  // namespace

std::string Booth::refFoul(const std::string& logName, bool offense, const Player* man) const {
  const RefFoul* f = bRefFoul(logName);
  std::string s;
  if (f) s = f->words;
  else {
    // (a foul the table does not know: the log's name in sentence case)
    for (const char c : logName) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    s = bCap(s.empty() ? std::string("foul") : s);
  }
  s += offense ? ", offense" : ", defense";
  if (man) s += ", number " + bS(man->number);
  return s;
}

void Booth::refSay(const std::string& text, double delay) {
  if (text.empty()) return;
  Line& rl = push(Who::Ref, Kind::Ref, text, 1, 4, delay, 25);
  rl.snapSafe = true;
  rl.topic = "ref";
}

void Booth::onPenalty(const Game& g, const PlayLog& l) {
  const std::string& d = l.desc;
  downTag_.clear();
  // "... PENALTY on ABQ: Offensive Holding (#72 J.Smith), enforced. 1st and 20 at ABQ 15."  (pre-snap: ", 5 yards.")
  const size_t at = d.find("PENALTY on ");
  if (at == std::string::npos) return;
  const size_t colon = d.find(':', at);
  const std::string ab = d.substr(at + 11, colon - at - 11);
  std::string name = d.substr(colon + 2);
  std::string who;
  const size_t par = name.find(" (#");
  if (par != std::string::npos) {
    const size_t close = name.find(')', par);
    who = name.substr(par + 2, close - par - 2);  // "#72 J.Smith"
    name = name.substr(0, par);
  } else {
    const size_t comma = name.find(',');
    if (comma != std::string::npos) name = name.substr(0, comma);
  }
  const Team& t = g.home->abbr == ab ? *g.home : *g.away;
  const Player* man = bLogMan(t, who);
  // a flag before the snap is a log line of its own (a play's line starts with the play)
  const bool presnap = at == 0;
  const bool onOff = presnap ? (&t == &g.offense()) : (&t == &g.teamOf(possBefore_));
  // the foul in the words a play-by-play man uses
  struct Say {
    const char* log;
    const char* words;
  };
  static const Say kSay[] = {{"Offensive Holding", "Holding"}, {"Defensive Holding", "Defensive holding"},
                             {"Defensive Pass Interference", "Pass interference"}, {"Offensive Pass Interference", "Offensive pass interference"},
                             {"Illegal Contact", "Illegal contact"}, {"Roughing the Passer", "Roughing the passer"}, {"Face Mask", "A face mask"},
                             {"Unnecessary Roughness", "Unnecessary roughness"}, {"Illegal Block in the Back", "An illegal block in the back"},
                             {"False Start", "False start"}, {"Delay of Game", "Delay of game"}, {"Offside", "The defense is offside"},
                             {"Encroachment", "Encroachment"}, {"Neutral Zone Infraction", "Neutral zone infraction"},
                             {"Illegal Use of Hands", "Illegal use of hands"}, {"Intentional Grounding", "Intentional grounding"},
                             {"Illegal Formation", "Illegal formation"}, {"Illegal Shift", "An illegal shift"},
                             {"Too Many Men on the Field", "Too many men on the field"}};
  std::string foul = name;
  for (const Say& k : kSay)
    if (name == k.log) foul = k.words;
  const PlayResult* r = (!presnap && sim_ && sim_->result) ? &*sim_->result : nullptr;
  // the yards the foul really moved the ball (half the distance to the goal near one), and what it was worth
  const bool dpiSpot = bHas(name, "Defensive Pass Interference");
  const bool grounding = name == "Intentional Grounding";
  const RefFoul* rf = bRefFoul(name);
  const int nominal = rf ? rf->yards : 5;
  // (a personal foul on the defense is marked from where the play ended; one on the offense, and the rest, from the line)
  const bool fromEnd = !onOff && (bHas(name, "Roughing") || bHas(name, "Face Mask") || bHas(name, "Unnecessary"));
  const double endSpot = r ? (r->turnover ? losBefore_ : r->endX) : losBefore_;
  int moved = 0;
  const size_t yp = d.find(" yards.");
  if (presnap && yp != std::string::npos) {
    size_t q = yp;
    while (q > 0 && std::isdigit(static_cast<unsigned char>(d[q - 1]))) --q;
    moved = std::atoi(d.c_str() + q);
  } else if (g.poss == possBefore_) {
    moved = bRound(std::fabs(g.ballOn - (fromEnd ? endSpot : losBefore_)));
  }
  const bool halfDist = !dpiSpot && nominal > 0 && moved > 0 && moved < nominal && !(grounding && moved > 10);
  // the down before the flag (a flag before the snap: the down the booth last saw set up)
  const int downPre = presnap ? preKick_.down : downBefore_;
  const double toGoPre = presnap ? preKick_.toGo : toGoBefore_;
  const bool firstByFoul = !onOff && g.down == 1 && (presnap ? (downPre != 1 || g.toGo > toGoPre + 0.01) : true);
  // the play the flag came in on, and what it wipes out
  // (what the play did before the flag: a stop the defense had - an incompletion, a sack, short of the line to gain)
  const bool stopped = r && !r->td && (r->incomplete || r->kind == PlayKind::Sack || r->interception || r->fumbleLost ||
                                       (!r->turnover && r->endX - losBefore_ < toGoBefore_ - 1e-6));
  std::string swingBank;
  if (r && onOff && r->td) swingBank = "pen_notd";
  else if (r && onOff && r->yards >= 10 && !r->incomplete && !r->interception && !r->turnover) swingBank = "pen_wipe";
  else if (r && !onOff && r->interception) swingBank = "pen_noint";
  else if (r && !onOff && r->fumbleLost) swingBank = "pen_nofum";
  else if (r && !onOff && stopped && downBefore_ == 4) swingBank = "pen_no4th";
  else if (r && !onOff && (r->incomplete || r->kind == PlayKind::Sack)) swingBank = "pen_bail";
  if (man && man->pos == Pos::QB && bHas(name, "Holding")) man = nullptr;  // (charged to the quarterback in the log: the line gets it)
  const Player* refMan = man;
  if (grounding && r && r->passer) refMan = r->passer;  // (the passer grounds it: the log names nobody)
  // a late hit by the defense: the log names nobody, and the man who made the tackle is the one who hit him
  if (!refMan && !onOff && bHas(name, "Unnecessary") && r && !r->tacklers.empty() && t.hasPlayer(r->tacklers[0])) refMan = r->tacklers[0];
  if (!man && refMan && refMan != (r ? r->passer : nullptr)) man = refMan;
  const std::string ddNow = bDownDist(g.down, g.toGo, g.ballOn);
  const std::string spotNow = spot(g, g.ballOn, g.poss);
  std::string play;
  if (r) {
    if (r->kind == PlayKind::Run && r->rusher) play = "the run by " + nm(r->rusher);
    else if (r->kind == PlayKind::Pass && r->complete && !r->interception && r->receiver) play = "the catch by " + nm(r->receiver);
    else if (r->kind == PlayKind::Pass && r->interception) play = "the interception";
    else if (r->kind == PlayKind::Pass && r->incomplete) play = "the incompletion";
    else if (r->kind == PlayKind::Sack) play = "the sack";
    else if (r->kind == PlayKind::Scramble) play = "the scramble";
  }
  const Slots ps = facts(g, {{"name", man ? nm(man) : std::string()}, {"num", man ? bS(man->number) : std::string()}, {"team", t.city}, {"nick", nick(t)},
                             {"play", play}, {"pyds", r && r->yards > 0 ? bS(r->yards) : std::string()}, {"dd", ddNow}, {"spot", spotNow},
                             {"foul", lowerFirst(foul)}});
  if (!pendingBare_.empty() && !presnap && pendingResult_.size()) push(Who::PBP, Kind::Result, pendingBare_, 1, 4, 0, 10);  // what happened, then the flag on it
  pendingBare_.clear();
  std::string open;
  if (presnap) open = say("flagpre", ps);
  else open = say("flag", ps);
  if (ref_) {
    // the play-by-play man sees the flag, the referee announces it, and the play-by-play man says what it means
    push(Who::PBP, Kind::Result, open, 1, 4, 0.2, 20).snapSafe = true;
    std::string ref = refFoul(name, onOff, refMan) + ".";
    if (dpiSpot) ref += " The ball will be placed at the spot of the foul.";
    else if (grounding) ref += moved > 10 ? " Spot of the foul, and loss of down." : halfDist ? " Half the distance to the goal, and loss of down." : " 10-yard penalty, and loss of down.";
    else if (halfDist) ref += fromEnd && r && std::fabs(endSpot - losBefore_) > 0.5 ? " Half the distance to the goal, enforced at the end of the play." : " Half the distance to the goal.";
    else if (fromEnd && r && std::fabs(endSpot - losBefore_) > 0.5) ref += " " + bPenYards(nominal) + ", enforced at the end of the play.";
    else ref += " " + bPenYards(moved > 0 ? moved : nominal) + ".";
    if (firstByFoul && !presnap) ref += " Automatic first down.";
    else if (firstByFoul) ref += " First down.";
    else if (grounding) ref += " " + bCap(bOrdWord(g.down)) + " down.";
    else ref += " Repeat " + std::string(bOrdWord(g.down)) + " down.";
    refSay(ref);
    std::string post;
    if (!swingBank.empty()) post = say(swingBank, ps);
    else if (chance(0.5)) post = say(firstByFoul ? "pen_first_ref" : "pen_dd", ps);
    if (!post.empty()) {
      push(Who::PBP, Kind::Result, post, 1, 4, 0, 15).snapSafe = true;
      if (post.find(ddNow) != std::string::npos) ddSaidKey_ = bDdKey(g, g.down, g.toGo, g.ballOn);
    }
  } else {
    // the play-by-play man reads the flag himself: the foul, the man, what it wipes out, the yards and the down
    std::string after;
    if (dpiSpot) {
      const int dy = g.poss == possBefore_ ? bRound(std::fabs(g.ballOn - losBefore_)) : 0;
      if (dy > 0) after = "A spot foul, " + bS(dy) + " yards";
    } else if (halfDist) {
      after = "Half the distance to the goal";
    } else {
      const int yy = presnap || grounding ? moved : nominal;
      if (yy > 0) after = bS(yy) + (yy == 1 ? " yard" : " yards");
    }
    if (grounding) after += after.empty() ? "Loss of down" : ", and loss of down";
    if (firstByFoul && !presnap) after += say("pen_first", ps);
    else if (!presnap && !grounding && g.down == downBefore_ && g.poss == possBefore_)
      after += ", and they'll repeat " + std::string(bOrdWord(g.down)) + " down. Now it's " + ddNow + ".";
    else after += ". " + bCap(ddNow) + ".";
    if (!(firstByFoul && !presnap)) ddSaidKey_ = bDdKey(g, g.down, g.toGo, g.ballOn);
    const std::string mn = man ? ", " + nm(man) + ", number " + bS(man->number) + "." : onOff && !presnap ? ", on the offensive line." : ", on " + t.city + ".";
    // (what the flag wipes out: the short form, the down and distance follows in the same line)
    const std::string swing = swingBank.empty() ? std::string() : say(swingBank + "0", ps);
    push(Who::PBP, Kind::Result, open + " " + foul + (grounding ? std::string(", on the quarterback.") : mn) + (swing.empty() ? "" : " " + swing) + " " + bCap(after), 1, 4, 0.2,
         20)
        .snapSafe = true;
  }
  dropKind(Kind::Analysis);
  pendingAnalysis_.clear();
  pendingResult_.clear();
  // the analyst: the same man again, a stop turned into a first down, or the foul itself
  int& times = man ? penCount_[man->id] : penCount_[0];
  if (man) ++times;
  std::string c;
  const Slots as = facts(g, {{"name", man ? nm(man) : std::string()}, {"team", t.city}, {"times", bCountWord(times)}, {"yds", bS(moved > 0 ? moved : nominal)}});
  if (man && times >= 2) c = say(times == 2 ? "pen_again" : "pen_again3", as);
  else if (!onOff && !presnap && g.down == 1 && downBefore_ >= 3 && stopped) c = say("pen_gift", as);
  else if (bHas(name, "Offensive Holding")) c = say("pen_hold", as);
  else if (bHas(name, "Defensive Pass Interference")) c = say("pen_dpi", as);
  else if (bHas(name, "Roughing")) c = say("pen_rough", as);
  else if (bHas(name, "False Start")) c = say("pen_fs", as);
  else if (bHas(name, "Offside") || bHas(name, "Neutral Zone") || bHas(name, "Encroachment")) c = say("pen_off", as);
  else if (bHas(name, "Face Mask")) c = say("pen_mask", as);
  else if (bHas(name, "Delay of Game")) c = say("pen_delay", as);
  else if (bHas(name, "Defensive Holding") || bHas(name, "Illegal Contact")) c = say("pen_dhold", as);
  else if (bHas(name, "Offensive Pass Interference")) c = say("pen_opi", as);
  else if (grounding) c = say("pen_ground", as);
  else if (bHas(name, "Illegal Formation") || bHas(name, "Illegal Shift")) c = say("pen_form", as);
  else if (bHas(name, "Too Many Men")) c = say("pen_12", as);
  else if (bHas(name, "Unnecessary")) c = say("pen_ur", as);
  else if (bHas(name, "Illegal Use of Hands")) c = say("pen_hands", as);
  else if (bHas(name, "Block in the Back")) c = say("pen_bib", as);
  const bool must = (man && times >= 2) || (!onOff && !presnap && g.down == 1 && downBefore_ >= 3 && stopped);
  if (!c.empty() && (must || chance(0.7))) push(Who::Color, Kind::Analysis, c, 1, 3, 0, 15);
  if (!presnap) flushDeferred(g);  // (a timeout logged before the flag's line: after it)
}

// ------------------------------------------------------------------ a new play

void Booth::onNewPlay(const Game& g, const PlaySim& s) {
  flushDeferred(g);  // (a play that was never applied: what it held goes now)
  presnapT_ = 0;
  presnapSaid_ = readSaid_ = false;
  calledThrow_ = calledCatch_ = calledRun_ = breakaway_ = calledPressure_ = liveTalk_ = fumbleCalled_ = false;
  calledTD_ = calledINT_ = calledSack_ = calledFumble_ = calledPbu_ = calledGoingToScore_ = calledDrop_ = pbuSeen_ = dropSeen_ = battedSeen_ = false;
  dropDecided_ = skipDrop_ = false;
  spokenTags_.clear();
  spokenTarget_ = -1;
  lastRead_.clear();
  intX_ = -100;
  catchRoute_.clear();
  ticks_ = 0;
  appliedThisPlay_ = false;
  returnFootSaid_ = false;
  lastTickX_ = -1;
  lastTickT_ = -10;
  lastMarkSaid_ = -1;
  catchDef_ = nullptr;
  pancakes_.clear();
  sheds_.clear();
  movesSeen_.clear();
  hitBy_ = nullptr;
  downBefore_ = g.down;
  toGoBefore_ = g.toGo;
  losBefore_ = g.ballOn;
  possBefore_ = g.poss;
  clockBefore_ = g.clock;
  qBefore_ = g.q;
  scoreBefore_ = g.score;
  adjHome_ = adjAway_ = 0;
  ++playNo_;
  if (g.phase == GamePhase::Scrimmage && !s.isReturn()) ++scrimNo_;
  buildPlan(g, s);
}

// ---- the live calls' wording (one place: the play's plan and the match-start prewarm must make the same strings)
namespace {
using Opts = std::vector<std::string>;
Opts oThrow(const std::string& qb, const std::string& r) {
  return {"Throws for " + r + "...", qb + ", looking for " + r + "...", "Throws to " + r + "...", "Fires for " + r + "...", "Looking for " + r + "...",
          "Gets it out to " + r + "...", qb + " throws, " + r + "...", "Pass, intended for " + r + "...", "Delivers for " + r + "...", "Lets it go, for " + r + "..."};
}
Opts oDeep(const std::string& qb, const std::string& r) {
  return {qb + " launches deep for " + r + "...", "Deep shot, " + r + " streaking down the field...", qb + " lets it fly for " + r + "...",
          qb + " goes deep, looking for " + r + "...", "He airs it out for " + r + "...", "Down the field for " + r + "..."};
}
Opts oHand(const std::string& r, const std::string& qb, const std::string& dir, RunStyle st) {
  if (st == RunStyle::Draw) return {"Draw play, " + r + "...", "Delayed handoff to " + r + "...", "It's a draw, " + r + " has it..."};
  if (st == RunStyle::Sneak) return {qb + " sneaks it...", qb + " keeps it on the sneak...", "Quarterback sneak..."};
  Opts o = {"Handoff to " + r + ", " + dir + "...", r + " takes the handoff, " + dir + "...", r + " gets the carry " + dir + "...", "Gives it to " + r + "...",
            r + ", " + dir + "...", "Hands it off to " + r + "...", r + " with the ball, " + dir + "..."};
  if (st == RunStyle::Stretch) o.push_back(r + ", stretching it to the outside...");
  return o;
}
Opts oDrop(const std::string& qb) { return {qb + " back to pass.", qb + ", in the pocket.", qb + " sets up.", qb + " drops back.", qb + " looks downfield."}; }
Opts oScr(const std::string& qb) { return {qb + " escapes, he's on the move!", qb + " takes off!", qb + " pulls it down and runs!", qb + " steps up and runs!"}; }
Opts oSack(const std::string& n) { return {"Sacked! " + n + " gets him!", "And he's down! " + n + " with the sack!", n + " brings him down! Sack!"}; }
Opts oSackQ(const std::string& qb, const std::string& n) {
  return {qb + " drops back, and he's sacked! " + n + " gets him!", qb + " looks, and he's down! " + n + " with the sack!", qb + " is sacked! " + n + " brings him down!"};
}
Opts oInt(const std::string& n) { return {"Intercepted! " + n + " picks it off!", "Picked off! " + n + " has it!", "And it's intercepted by " + n + "!"}; }
Opts oField(const std::string& r) { return {r + " fields it.", "Taken by " + r + ".", r + " gathers it in."}; }
Opts oPress(const std::string& n) { return {"Here comes " + n + "!", n + " is coming free!", n + " gets pressure!"}; }
Opts oAway(const std::string& qb) { return {qb + " throws it away.", qb + " has to get rid of it.", qb + " throws it out of bounds."}; }
Opts oFum(const std::string& n) { return {n + " fumbles! Ball's loose!", "And " + n + " puts it on the ground! Loose ball!", "The ball is out! " + n + " lost it!"}; }
Opts oTd(const std::string& city) { return {"Touchdown!", "He's in! Touchdown!", "Touchdown, " + city + "!"}; }
Opts oTdD(const std::string& city) { return {"Touchdown!", "Touchdown, " + city + "!"}; }
bool bIntRole(const std::string& role) {
  return role.find("CB") != std::string::npos || role == "FS" || role == "SS" || role == "NB" || role == "DB" || role.find("LB") != std::string::npos;
}
}  // namespace

std::vector<std::pair<std::string, int>> Booth::rosterCalls(const Game& g) {
  // every way this match's regulars can be called live (each man's three variants), for the speech cache before
  // the plays need them. Starters by position, the way the depth chart puts them on the field.
  std::vector<std::pair<std::string, int>> out;
  auto all = [&](int pid, const Opts& o, int e) {
    if (o.empty()) return;
    const size_t n = o.size(), base = static_cast<size_t>((pid * 2654435761u) % n);
    for (size_t k = 0; k < (std::min)(n, static_cast<size_t>(3)); ++k) out.push_back({o[(base + k) % n], e});
  };
  for (const Team* t : {g.home, g.away}) {
    auto top = [&](Pos p, size_t n) {
      std::vector<Player*> v = depth(*t, p);
      if (v.size() > n) v.resize(n);
      return v;
    };
    const std::vector<Player*> qbs = top(Pos::QB, 1);
    const std::string qb = qbs.empty() ? std::string("the quarterback") : nm(qbs[0]);
    if (!qbs.empty()) {
      all(qbs[0]->id, oDrop(qb), 1);
      all(qbs[0]->id, oScr(qb), 2);
    }
    std::vector<Player*> rec;
    for (auto [p, n] : {std::pair<Pos, size_t>{Pos::WR, 4}, {Pos::TE, 2}, {Pos::RB, 2}, {Pos::FB, 1}})
      for (Player* x : top(p, n)) rec.push_back(x);
    for (Player* x : rec) {
      all(x->id, oThrow(qb, nm(x)), 1);
      all(x->id, oDeep(qb, nm(x)), 2);
    }
    if (!qbs.empty()) all(qbs[0]->id, oAway(qb), 1);
    for (Player* x : rec) all(x->id, oFum(nm(x)), 3);
    for (Player* x : top(Pos::RB, 2))
      for (const char* dir : {"up the middle", "to the left side", "to the right side"}) all(x->id, oHand(nm(x), qb, dir, RunStyle::Zone), 1);
    const Team* ot = t == g.home ? g.away : g.home;
    const std::vector<Player*> oqbs = depth(*ot, Pos::QB);
    const std::string oqb = oqbs.empty() ? std::string("the quarterback") : nm(oqbs[0]);
    for (auto [p, n] : {std::pair<Pos, size_t>{Pos::DE, 2}, {Pos::DT, 2}, {Pos::LB, 4}})
      for (Player* x : top(p, n)) {
        all(x->id, oSack(nm(x)), 3);
        all(x->id, oSackQ(oqb, nm(x)), 3);
        all(x->id, oPress(nm(x)), 2);
      }
    for (auto [p, n] : {std::pair<Pos, size_t>{Pos::CB, 3}, {Pos::S, 2}, {Pos::LB, 3}})
      for (Player* x : top(p, n)) all(x->id, oInt(nm(x)), 3);
    for (auto [p, n] : {std::pair<Pos, size_t>{Pos::WR, 2}, {Pos::CB, 1}, {Pos::RB, 1}})
      for (Player* x : top(p, n)) all(x->id, oField(nm(x)), 1);
    all(t->id, oTd(t->city), 3);
    all(t->id, oTdD(t->city), 3);
  }
  return out;
}

void Booth::buildPlan(const Game& g, const PlaySim& s) {
  plan_.at.clear();
  handoffPid_ = -1;
  if (s.isReturn()) {
    if (s.returner) plan_.at["field"] = {rot(s.returner->p->id, oField(nm(s.returner->p))), 1};
    return;
  }
  const PlaySim& sm = s;
  const std::string qb = sm.qb ? nm(sm.qb->p) : std::string("the quarterback");
  const OffPlay* op = sm.cfg.offPlay;
  // the throw calls say who, never where (the plan is made before the snap; the ball may go anywhere)
  for (const Ent* e : sm.off) {
    if (!e->eligible || e == sm.qb) continue;
    plan_.at["throw:" + bS(e->p->id)] = {rot(e->p->id, oThrow(qb, nm(e->p))), 1};
    plan_.at["deep:" + bS(e->p->id)] = {rot(e->p->id, oDeep(qb, nm(e->p))), 2};
  }
  if (op && op->run) {
    const Ent* rb = sm.offRole(op->run->carrier.empty() ? std::string("RB") : op->run->carrier);
    if (rb && rb != sm.qb) {
      // the play's design: +y is the offense's left (a flipped formation runs the other way)
      const double aim = sm.runAimDy();
      const bool mid = std::fabs(aim) < 2.5;
      const std::string dir = mid ? std::string("up the middle") : std::string(aim > 0 ? "to the left side" : "to the right side");
      plan_.at["handoff"] = {rot(rb->p->id, oHand(nm(rb->p), qb, dir, op->run->style)), 1};
      handoffPid_ = rb->p->id;
    }
  }
  // the touchdown call, for either side of the ball
  plan_.at["td"] = {rot(g.offense().id + playNo_, oTd(g.offense().city)), 3};
  plan_.at["tdD"] = {rot(g.defense().id + playNo_, oTdD(g.defense().city)), 3};
  const int qid = sm.qb ? sm.qb->p->id : 0;
  plan_.at["drop"] = {rot(qid, oDrop(qb)), 1};
  plan_.at["scramble"] = {rot(qid, oScr(qb)), 2};
  plan_.at["away"] = {rot(qid, oAway(qb)), 1};
  for (const Ent* e : sm.off)
    if (e->eligible || e == sm.qb) plan_.at["fum:" + bS(e->p->id)] = {rot(e->p->id, oFum(nm(e->p))), 3};
  for (const Ent* d : sm.def) {
    if (bIsDL(d) || bHas(d->role, "LB")) plan_.at["press:" + bS(d->p->id)] = {rot(d->p->id, oPress(nm(d->p))), 2};
    if (bIsDL(d) || bHas(d->role, "LB")) plan_.at["sack:" + bS(d->p->id)] = {rot(d->p->id, oSack(nm(d->p))), 3};
    if (bIsDL(d) || bHas(d->role, "LB")) plan_.at["sackq:" + bS(d->p->id)] = {rot(d->p->id, oSackQ(qb, nm(d->p))), 3};
    if (bIntRole(d->role)) plan_.at["int:" + bS(d->p->id)] = {rot(d->p->id, oInt(nm(d->p))), 3};
  }
}

void Booth::live(const std::string& key, const std::string& fallback, int energy, double ttl, const char* tag) {
  auto it = plan_.at.find(key);
  const std::string text = it != plan_.at.end() ? it->second.text : fallback;
  const int en = it != plan_.at.end() ? it->second.energy : energy;
  if (text.empty()) return;
  const std::string tg = tag ? std::string(tag) : key;
  if (auto f = rotFamily_.find(text); f != rotFamily_.end()) ++rotUse_[f->second];
  const bool swing = tg.rfind("int:", 0) == 0 || tg == "fumble" || tg.rfind("fum:", 0) == 0 || tg == "recover";
  const int prio = swing ? 7 : (std::max)(en, energy) >= 3 || tg.rfind("throw:", 0) == 0 || tg.rfind("deep:", 0) == 0 ? 6 : 5;
  // a big call makes the smaller ones still waiting stale ("Breaks free!" does not wait for "Got it!")
  if (prio >= 6)
    for (auto q = q_.begin(); q != q_.end();)
      if (q->live && q->prio < prio) {
        q = q_.erase(q);
        st_.expired++;
      } else
        ++q;
  Line& l = push(Who::PBP, Kind::Live, text, (std::max)(en, energy), prio, 0, ttl);
  l.live = true;
  l.tag = tag ? std::string(tag) : key;
  // the generic call the voice can fall back on when this one's audio is not made yet (a named call on a cold matchup)
  std::tie(l.alt, l.altEnergy) = genericAlt(l.tag, l.text, fallback);
  if (l.alt.empty()) l.altEnergy = l.energy;
  liveTalk_ = true;
}

bool Booth::said(const char* prefix) const {
  for (const std::string& t : spokenTags_)
    if (t.rfind(prefix, 0) == 0) return true;
  if (cur_.live && cur_.tag.rfind(prefix, 0) == 0) return true;
  for (const Line& l : q_)  // (made on the play's last frame: it goes before the result)
    if (l.live && l.tag.rfind(prefix, 0) == 0) return true;
  return false;
}

// ------------------------------------------------------------------ the snap and the live call

void Booth::onSnap(const Game& g, const PlaySim& s) {
  snapFlush();
  if (s.isReturn()) return;
  // the analyst's word on a break the snap overtook ("two minutes before the half is a big window") is stale after
  // the play: dropped, not said after the next result
  for (auto it = q_.begin(); it != q_.end();)
    if (!it->live && it->who == Who::Color && it->kind == Kind::Break) {
      it = q_.erase(it);
      st_.expired++;
    } else
      ++it;
  const OffPlay* op = s.cfg.offPlay;
  if (op && (op->type == PlayType::Kneel || op->type == PlayType::Spike)) return;
  (void)g;
}

void Booth::onEvent(const Game& g, const PlaySim& s, const PlayEvent& e) {
  const std::string& t = e.type;
  const bool ret = s.isReturn();
  if (t == "catch" && ret) {
    live("field", "Fielded.", 1);
    return;
  }
  if (t == "handoff") {
    // (the plan names the back the play was drawn for; a handoff to somebody else gets the call that names nobody, and a
    // reverse's second handoff is not called again)
    if (e.note == "reverse") return;
    if (e.b && e.b->p && e.b->p->id != handoffPid_) live("", "Handoff.", 1, 0.9, "handoff");
    else live("handoff", "Handoff.", 1);
    calledRun_ = true;
    return;
  }
  if (t == "scramble") {
    live("scramble", "He takes off!", 2);
    return;
  }
  if (t == "throw" && e.b) {
    const double air = s.ball.targetSpot ? s.ball.targetSpot->x - s.los : (e.b->pos.x - s.los);
    calledThrow_ = true;
    live((air >= 20 ? "deep:" : "throw:") + bS(e.b->p->id), "Throws...", air >= 20 ? 2 : 1, 1.0);
    return;
  }
  if (t == "throwaway") {
    live("away", "Throws it away.", 1, 0.9, "away");
    return;
  }
  if (t == "catch" || t == "divecatch") {
    calledCatch_ = true;
    // the nearest man on him at the catch (the analyst may name him)
    double best = 1e9;
    for (const Ent* d : s.def)
      if (e.a && dist(d->pos, e.a->pos) < best) {
        best = dist(d->pos, e.a->pos);
        catchDef_ = d->p;
      }
    if (t == "divecatch") live("", pick("l_dive", {"Diving catch!", "Lays out, and makes the grab!", "What a catch!"}), 3, 0.9, "catch");
    else if (best < 1.2) live("", pick("l_traffic", {"Makes the catch in traffic!", "Holds on with a man draped all over him!", "Snags it!"}), 2, 0.9, "catch");
    else if (e.a && chance(0.5)) {
      const double side = e.a->pos.y;
      const bool sideline = side < 7.5 || side > FIELD_W - 7.5;
      const bool middle = std::fabs(side - FIELD_W / 2) < 7.0;
      live("", sideline ? pick("l_catchs", {"Caught on the sideline!", "Makes the grab at the sideline!"})
                        : middle ? pick("l_catchm", {"Caught over the middle!", "Got it, over the middle!"}) : pick("l_catch", {"Caught!", "Got it!", "Makes the grab!", "Hauls it in!", "And he's got it!"}),
           2, 1.1, "catch");
      catchRoute_ = e.a->routeName;
    } else live("", pick("l_catch", {"Caught!", "Got it!", "Makes the grab!", "Hauls it in!", "And he's got it!"}), 2, 1.1, "catch");
    if (e.a) catchRoute_ = e.a->routeName;
    return;
  }
  if (t == "drop") {
    dropSeen_ = true;  // a bobble can still be caught: called when the ball is dead on the ground
    return;
  }
  if (t == "incomplete" && dropSeen_ && !pbuSeen_) {
    live("", pick("l_drop", {"Dropped!", "Drops it!", "Right through his hands!", "Can't hang on!"}), 2, 0.9, "drop");
    return;
  }
  if (t == "pbu" || t == "contested_inc") {
    // a hand on the ball is not yet an incompletion (a tipped ball can still be caught, or picked): called when it lands
    pbuSeen_ = true;
    return;
  }
  if (t == "batted") {
    battedSeen_ = true;  // (batted at the line, it can still come down in somebody's hands: called when it lands)
    return;
  }
  if (t == "incomplete" && battedSeen_ && !calledPbu_) {
    calledPbu_ = true;
    live("", pick("l_bat", {"Batted at the line!", "Knocked down at the line of scrimmage!"}), 2, 0.9, "pbu");
    return;
  }
  if (t == "incomplete" && pbuSeen_ && !calledPbu_) {
    calledPbu_ = true;
    live("", pick("l_pbu", {"Broken up!", "Knocked away!", "Batted down!"}), 2, 0.9, "pbu");
    return;
  }
  if (t == "oobcatch") {
    live("", "Out of bounds, no catch.", 1, 0.9, "inc");
    return;
  }
  if (t == "int" && e.a) {
    intX_ = e.a->pos.x;
    calledINT_ = true;
    live("int:" + bS(e.a->p->id), "Intercepted!", 3, 1.2);
    return;
  }
  if (t == "fumble") {
    calledFumble_ = true;
    live(e.a ? "fum:" + bS(e.a->p->id) : std::string(), pick("l_fum", {"Fumble! Ball's loose!", "He coughs it up! Loose ball!", "The ball is out!"}), 3, 1.2, "fumble");
    return;
  }
  if (t == "recover" && e.a && (calledFumble_ || calledINT_)) {
    const bool def = e.a->side == Side::Def;
    const Team& tm = def ? g.defense() : g.offense();
    live("", def ? pick("l_recd", {"And " + tm.city + " recovers!", nm(e.a->p) + " falls on it for " + tm.city + "!"})
                 : pick("l_reco", {tm.city + " gets it back!", "And " + tm.city + " comes up with it!"}),
         3, 1.5, "recover");
    return;
  }
  if (t == "bighit") {
    hitBy_ = e.a ? e.a->p : nullptr;
    live("", pick("l_hit", {"Oh, big hit!", "What a shot!", "Lowers the boom!"}), 3, 0.9, "hit");
    return;
  }
  if (t == "trucked") {
    movesSeen_.push_back("truck");
    live("", "Runs him over!", 2);
    return;
  }
  if (t == "move" && e.a && e.a->hasBall) {
    movesSeen_.push_back(e.note);
    if (e.note == "spin") live("", "Spin move!", 2, 0.6);
    else if (e.note == "juke") live("", "Makes a man miss!", 2, 0.6);
    else if (e.note == "stiffarm") live("", "Stiff arm!", 2, 0.6);
    else if (e.note == "hurdle") live("", "Hurdles a man!", 2, 0.6);
    return;
  }
  if (t == "pancake" && e.a && e.b) {
    // a (the defender) put on his back by b (the blocker)
    if (e.b->side == Side::Off) pancakes_.push_back(e.b->p);
    return;
  }
  if (t == "shed" && e.a && e.b) {
    sheds_.push_back({e.a->p, e.b->p, e.note});
    return;
  }
  if (t == "tackle" && e.b && e.a) {
    // (the sim's own test: a dropback QB brought down behind the line before the throw)
    const bool sack = e.b == s.qb && e.b->pos.x < s.los + 0.5 && !s.passThrown && !s.handedOff && s.cfg.offPlay && s.cfg.offPlay->type != PlayType::Run && !s.scramble;
    if (sack) {
      calledSack_ = true;
      const bool cold = !said("qbdrop") && !said("pressure");  // (nothing said of the pocket yet)
      live((cold ? "sackq:" : "sack:") + bS(e.a->p->id), "Sacked!", 3, 1.0);
      return;
    }
    // a tackle after the PBP has been running with the play gets its spot
    const double gainNow = (e.b->pos.x - (ret ? (s.catchX > 0 ? s.catchX : e.b->pos.x) : s.los)) * (e.b->side == Side::Off ? 1 : -1);
    if (breakaway_ || ((said("catch") || ret) && gainNow >= 15)) {
      const int yl = bRound(e.b->pos.x <= 50 ? e.b->pos.x : 100 - e.b->pos.x);
      if (yl >= 1 && yl <= 50 && e.b->pos.x < 99.5)
        live("", breakaway_ ? pick("l_downb", {"Finally brought down at the " + bS(yl) + "!", "Run down at the " + bS(yl) + "!"})
                            : pick("l_down", {"Brought down at the " + bS(yl) + ".", "Taken down at the " + bS(yl) + ".", "Wrapped up at the " + bS(yl) + "."}),
             breakaway_ ? 2 : 1, 0.9, "down");
    }
    return;
  }
}

void Booth::trackCarrier(const Game& g, const PlaySim& s) {
  (void)g;
  const Ent* c = s.ball.holder;
  if (!c || s.ball.mode != BallMode::Held) return;
  const bool offBall = c->side == Side::Off;
  if (!offBall) {
    // an interception or fumble return: yard lines toward the other way
  }
  // a breakaway: the carrier past the line, moving, and nobody close in front of him
  const double gd = offBall ? 1.0 : -1.0;
  const double x = c->pos.x;
  const double from = s.isReturn() ? (s.catchX > 0 ? s.catchX : x) : !offBall && intX_ > -50 ? intX_ : s.los;
  const double gained = (x - from) * gd;
  double nearAhead = 1e9;
  const std::vector<Ent*>& defs = offBall ? s.def : s.off;
  for (const Ent* d : defs) {
    if (d->state == EntState::Down) continue;
    const double dx = (d->pos.x - x) * gd;
    if (dx > -2.0) nearAhead = (std::min)(nearAhead, dist(d->pos, c->pos));
  }
  const double sp = c->speed();
  const double toGoal = offBall ? 100 - x : x;
  // nobody in front of him and the speed to stay there: the projected gain is what earns the call
  if (!offBall && !said("int") && !said("fum") && !said("recover")) return;  // (the takeaway is called first)
  if (!breakaway_ && ((gained >= (s.isReturn() ? 20.0 : 14.0) && nearAhead > (s.isReturn() ? 6.0 : 7.0) && sp > 6.0) || (gained >= 35 && sp > 5.0))) {
    breakaway_ = true;
    if (nearAhead < 9.0 && !(gained >= 35)) {
      breakaway_ = true;  // (open-ish, not open: the yard lines may still come, the hype doesn't)
    } else if (nearAhead > 12.0 && toGoal < 35)
      live("", pick("l_gone", {"Nobody's going to catch him!", "He's in the clear!"}), 3, 0.8, "break");
    else
      live("", pick("l_room", {"He's got room!", "He's loose!", "Breaks free!"}), 3, 0.8, "break");
    lastTickT_ = now_;
  }
  if (!breakaway_) return;
  // the yard lines going by: every ten, then "he's going to score" at the end
  if (toGoal < 12 && !calledGoingToScore_ && nearAhead > 3.0) {
    calledGoingToScore_ = true;
    live("", pick("l_score", {"He's going to score!", "Into the end zone!"}), 3, 0.8, "break");
    return;
  }
  const int mark = static_cast<int>(std::floor(toGoal / 10.0)) * 10;  // the next ten-yard line toward the goal
  if (mark >= 10 && mark <= 50 && mark != lastMarkSaid_ && toGoal < mark + 1.0 && now_ - lastTickT_ > 0.9 && !speakingLiveLong() && ticks_ < 3 && (mark % 20 == 10 || toGoal < 25)) {
    ++ticks_;
    lastMarkSaid_ = mark;
    lastTickT_ = now_;
    live("", mark == 50 ? std::string("Midfield!") : "The " + bS(mark) + "!", 3, 0.5, "tick");
  }
}

void Booth::livePocket(const Game& g, const PlaySim& s) {
  (void)g;
  if (s.isReturn() || !s.cfg.offPlay || s.cfg.offPlay->type == PlayType::Run || s.handedOff || s.passThrown || s.scramble) return;
  if (s.cfg.offPlay->type == PlayType::Kneel || s.cfg.offPlay->type == PlayType::Spike) return;
  // the pocket: a long-developing play gets "back to throw", a rush getting home gets "pressure"
  if (!calledDrop_ && !dropDecided_) {
    dropDecided_ = true;
    const OffPlay* op = s.cfg.offPlay;
    // (the drop-back call ("Jessup, in the pocket.") on fewer than half the passes: on every one it was the line heard most)
    skipDrop_ = (op && (op->type == PlayType::Screen || op->hasTag("quick"))) || chance(0.55);
  }
  if (!calledDrop_ && !skipDrop_ && s.t > 0.9 && !s.pendingThrow && !s.passThrown && !cur_.id) {
    calledDrop_ = true;
    live("drop", "Back to throw.", 1, 0.4, "qbdrop");
  }
  if (!calledPressure_ && s.pressured && s.t > 0.9 && said("qbdrop")) {
    calledPressure_ = true;
    calledDrop_ = true;  // (the drop line after the pressure line reads backwards)
    const Ent* near = nullptr;
    double nd = 1e9;
    for (const Ent* d : s.def)
      if (s.qb && dist(d->pos, s.qb->pos) < nd) {
        nd = dist(d->pos, s.qb->pos);
        near = d;
      }
    if (near && nd < 4.0 && plan_.at.count("press:" + bS(near->p->id))) {
      live("press:" + bS(near->p->id), "Pressure!", 2, 0.5, "pressure");
      return;
    }
    live("", pick("l_press", {"Pressure!", "Here comes the rush!", "He's got company!", "Pocket's collapsing!"}), 2, 0.5, "pressure");
  }
}

bool Booth::speakingLiveLong() const { return cur_.id && cur_.live && curEnd_ - now_ > 0.6; }

// ------------------------------------------------------------------ the whistle

void Booth::onWhistle(const Game& g, const PlaySim& s, const PlayResult& r) {
  int energy = 1;
  // the play is over: a live call still waiting ("Got it!" behind the throw) is history now
  for (auto it = q_.begin(); it != q_.end();)
    if (it->live && it->notBefore < now_ - 0.3) {  // (a call made on the play's last frame, "Broken up!", still goes)
      it = q_.erase(it);
      st_.expired++;
    } else
      ++it;
  // a score called the instant it happens
  if (r.td) {
    const bool def = *r.td == Side::Def;
    const TeamKey scorer = def ? other(g.poss) : g.poss;
    const bool two = g.phase == GamePhase::Pat;
    if (!two) {
      (scorer == TeamKey::Home ? adjHome_ : adjAway_) = 6;
      live(scorer == g.poss ? "td" : "tdD", "Touchdown!", 3, 1.5);
    }
  } else if (r.safety) {
    live("", "Safety!", 3, 1.2);
  }
  if (s.isReturn()) {
    returnResult(g, s, r);
    return;
  }
  if (g.phase == GamePhase::Pat) return;  // the two-point try: its log line says it
  // the plays the presentation replays (MatchDirector OnPlayDead's rule)
  replayWorthy_ = r.td || r.interception || r.fumbleLost || r.kind == PlayKind::Sack || r.yards >= 20;
  downTag_.clear();
  pendingResult_.clear();
  pendingBare_.clear();
  pendingAnalysis_.clear();
  fourthFail_ = false;
  std::string bare = resultLine(g, s, r, energy);
  if (fourthFail_ && !downTag_.empty() && bare.empty()) {
    bare = bJoin(std::string(), downTag_);
    downTag_.clear();
  }
  const std::string text = bJoin(bare, downTag_);
  if (!text.empty()) {
    if (replayWorthy_) {
      Line& l = push(Who::PBP, Kind::Result, text, energy, 4, liveTalk_ ? 0.25 : 0.45, 8);
      l.snapSafe = r.td.has_value() || r.turnover;
      downTag_.clear();
    } else {
      // the officials have the play until it is applied: the call waits for them (the voice is made meanwhile)
      pendingResult_ = text;
      pendingBare_ = bare;
      pendingResultEnergy_ = energy;
      downTag_.clear();
    }
  }
  int aEnergy = 1;
  resultSaid_ = text;
  creditLine_.clear();
  creditFor_.clear();
  if (auto a = analysis(g, s, r, aEnergy)) {
    std::string t = *a;
    // the replay's lead-in: a sentence of its own, or joined to the breakdown ("Watch this again, they were in man...")
    const Slots ws = facts(g, {{"yds", r.yards > 0 ? bS(r.yards) : std::string()}});
    std::string pre;
    if (replayWorthy_ && playAngle_ && chance(0.6)) pre = say("replay", ws);
    // a credit and a toss go with the breakdown they lead into: a question and its answer (dropped together, never alone)
    bool led = false;
    if (creditRead_) {
      creditRead_ = false;
      const std::string credit = say("credit", ws);
      if (replayWorthy_) {
        push(Who::PBP, Kind::Analysis, credit, 1, 3, 0, 20).tag = "question";  // (after the result, said at the whistle)
        led = true;
      } else {
        creditLine_ = credit;  // after the held result, with the held breakdown
        creditFor_ = t;
      }
    }
    if (replayWorthy_) {
      if (playAngle_ && !r.td && chance(0.3) && cooled("toss", 240)) {
        mark("toss");
        push(Who::PBP, Kind::Analysis, say("toss_q", ws), 1, 3, 0, 20).tag = led ? "answer" : "question";
        led = true;
        pre.clear();  // (the answer opens with the play, not with "Let's take another look")
      }
      if (!pre.empty()) t = pre.size() >= 2 && pre.compare(pre.size() - 2, 2, ", ") == 0 ? pre + lowerFirst(t) : pre + t;
      Line& al = push(Who::Color, Kind::Analysis, t, aEnergy, 3, 0, 25);
      al.topic = lastAnalysisTopic_;
      if (led) al.tag = "answer";
    }
    else {
      pendingAnalysis_ = t;
      pendingAnalysisEnergy_ = aEnergy;
      pendingAnalysisTopic_ = lastAnalysisTopic_;
    }
  }
}

std::string Booth::lowerFirst(const std::string& t) const {
  // "That's ..." -> "that's ..." after "Dan, "; a name (a player, a city, a team) stays capitalized
  const size_t sp = t.find_first_of(" ,.'!?");
  const std::string w = t.substr(0, sp);
  if (w.empty() || properNames_.count(w)) return t;
  return std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(t[0])))) + t.substr(1);
}

int Booth::tier(const Game& g) const {
  // 0: a game; 1: two scores late (the words "gamble", "big conversion" are wrong now); 2: decided
  const int m = std::abs(g.score.home - g.score.away);
  if (g.q >= 4 && ((m >= 17 && g.clock < 600) || (m >= 9 && g.clock < 100) || m >= 25)) return 2;
  if ((g.q >= 3 && m >= 17) || (g.q >= 4 && m >= 9 && g.clock < 480)) return 1;
  return 0;
}

std::string Booth::resultLine(const Game& g, const PlaySim& s, const PlayResult& r, int& energy) {
  // a touchdown's yards are to the goal line (the ball carrier may finish deep in the end zone)
  const double los = losBefore_;
  const bool first = !r.td && !r.turnover && !r.safety && r.endX - losBefore_ >= toGoBefore_ - 1e-6 && r.kind != PlayKind::Spike && r.kind != PlayKind::Kneel;
  // short of the line to gain by a rounding: the yards said are one less ("good for 10" and "short" can't both be heard)
  const int y = r.td == Side::Off                                                      ? (std::max)(1, bRound(100 - losBefore_))
                : !first && r.yards > 0 && r.yards >= bRound(toGoBefore_) && !r.turnover ? (std::max)(0, bRound(toGoBefore_) - 1)
                                                                                         : r.yards;
  const bool fourth = downBefore_ == 4 && !first && !r.td && !r.turnover;
  const std::string tk = r.tacklers.empty() ? std::string() : mentions_.count(r.tacklers[0]->id) ? nm(r.tacklers[0]) : pname(r.tacklers[0]);
  const std::string qb = r.passer ? nm(r.passer) : (s.qb ? nm(s.qb->p) : std::string("the quarterback"));
  const double endX = (std::max)(0.5, (std::min)(99.5, r.endX));
  const std::string at = spot(g, endX, g.poss);
  // "a yard", "7 yards"; "for 7", "for a yard"
  auto yds = [](int n) { return n == 1 ? std::string("a yard") : bS(n) + " yards"; };
  auto forN = [](int n) { return n == 1 ? std::string("for a yard") : "for " + bS(n); };
  std::string out;
  // the down that follows: held until the play is applied (a flag may change it); a play the replay shows says it now
  const bool lastSnap = (g.q == 2 || g.q >= 4) && g.clock - r.duration <= 0.05;
  auto later = [&](const std::string& t) -> std::string {
    if (lastSnap) {
      downTag_ = g.q == 2 && !r.td ? " And that'll do it for the half." : std::string();
      return std::string();
    }
    downTag_ = t;
    return std::string();
  };
  // the down and distance the play leaves when it is short of the line to gain ("..., and that brings up 3rd and 2.")
  const double gainedX = r.endX - losBefore_;
  const double ndToGo = r.incomplete ? toGoBefore_ : toGoBefore_ - gainedX;
  const double ndBall = r.incomplete ? losBefore_ : r.endX;
  const bool ndOk = !first && !r.td && !r.turnover && !r.safety && downBefore_ <= 3 && !lastSnap && r.kind != PlayKind::Kneel && r.kind != PlayKind::Spike &&
                    ndBall > 0.5 && ndBall < 99.5;
  const std::string nd = ndOk ? bDownDist(downBefore_ + 1, ndToGo, ndBall) : std::string();
  // the facts a result can name (each wording names the ones it needs)
  // the spot is said once: not again after the live call gave it ("Brought down at the 26."), and a tail does not repeat
  // the one the result just named (rsNoSpot)
  const bool spotCalled = said("down");
  const Slots rs = facts(g, {{"qb", qb}, {"tk", tk}, {"at", spotCalled ? std::string() : at}, {"to", spotCalled ? std::string() : fieldX(endX)},
                             {"yds", y > 0 ? bS(y) : std::string()}, {"ydsw", y > 0 ? yds(y) : std::string()},
                             {"forn", y > 0 ? forN(y) : std::string()}, {"loss", y < 0 ? bS(-y) : std::string()}, {"lossw", y < 0 ? yds(-y) : std::string()}, {"nd", nd},
                             {"ord", bOrdWord(downBefore_)}});
  Slots rsNoSpot = rs;
  for (Slot& x : rsNoSpot)
    if (!std::strcmp(x.k, "at") || !std::strcmp(x.k, "to")) x.v.clear();
  const std::string toWord = fieldX(endX);
  auto tailSlots = [&]() -> const Slots& { return spotCalled || out.find(at) != std::string::npos || out.find(toWord) != std::string::npos ? rsNoSpot : rs; };
  auto with = [&rs](Slots e) {
    e.insert(e.end(), rs.begin(), rs.end());
    return e;
  };
  if (r.safety) {
    energy = 3;
    const std::string who = !r.tacklers.empty() ? nm(r.tacklers[0]) : std::string("the defense");
    return say("r_safety", with({{"who", who}}));
  }
  auto firstTag = [&]() -> std::string {
    if (!first) return std::string();
    if (downBefore_ == 3) return say(tier(g) == 0 ? "r_3rd" : "r_3rdq", tailSlots());
    if (downBefore_ == 4) return say(tier(g) == 0 ? "r_4th" : "r_4thq", tailSlots());
    if (los + y >= 100) return std::string();
    return say("r_1st", tailSlots());
  };
  auto nextDown = [&]() -> std::string {
    if (first || r.td || r.turnover) return std::string();
    if (fourth) {
      energy = (std::max)(energy, 2);
      fourthFail_ = true;
      if (g.q >= 4 && g.margin() < 0 && g.margin() >= -8 && g.clock < 240) {  // (a one-score game: down 25, a stop decides nothing)
        sealed_ = g.timeouts[g.poss] == 0 || g.clock < 45;
        return say("r_todlate", tailSlots());
      }
      return say("r_tod", tailSlots());
    }
    const double gained = r.endX - losBefore_;
    if (downBefore_ == 3 && gained > 0 && toGoBefore_ - gained < 1.5 && r.kind != PlayKind::Sack) return say("r_short3", tailSlots());
    // now and then the down it brings up, said with the play (the setup then does not say it again)
    if (!nd.empty() && downBefore_ <= 2 && chance(r.incomplete ? 0.55 : 0.4)) {
      const std::string t = say("r_nd", rs);
      if (!t.empty()) ddSaidKey_ = bDdKey(g, downBefore_ + 1, ndToGo, ndBall);
      return t;
    }
    return std::string();  // (third and fourth downs: the setup says them)
  };
  switch (r.kind) {
    case PlayKind::Kneel:
      if (!once("kneel" + bS(g.q) + bS(static_cast<int>(g.clock / 60)))) return std::string();
      return say(g.q == 2 ? "r_kneelh" : "r_kneel", rs);
    case PlayKind::Spike:
      return say("r_spike", with({{"left", bClock(g.clock - r.duration)}}));
    case PlayKind::Sack: {
      energy = 2;
      const std::string who = r.sackedBy ? pname(r.sackedBy) : tk;
      if (r.fumbleBy) {
        energy = 3;
        return say(r.fumbleLost ? "r_sfum" : "r_sfumkept", with({{"who", who}}));
      }
      if (said("sack"))
      {
        int n = 1;
        if (r.sackedBy)
          for (const PlayerLine* ln : g.stats.forTeam(g.defense().abbr))
            if (ln->pid == r.sackedBy->id) n = ln->sacks + 1;  // (this one not in the book yet)
        if (n >= 2) topicT_["sk:" + bS(r.sackedBy->id) + ":" + bS(n)] = now_;  // (the count is said here: the story would repeat it)
        if (n >= 2)
          out = say("r_sackn", with({{"who", nm(r.sackedBy)}, {"n", bS(n)}, {"nw", bCountWord(n)}, {"nth", bOrdWord(n)}}));
        else
          out = say("r_sack2", with({{"who", who}}));
      }
      else
        out = say("r_sack", with({{"who", who}}));
      return out + later(nextDown());
    }
    case PlayKind::Scramble: {
      if (y >= 10) out = say("r_scrbig", rs);
      else if (y > 0) out = say("r_scr", rs);
      else out = say(y == 0 ? "r_scr0" : "r_scrneg", rs);
      if (y >= 10) energy = 2;
      return out + later(firstTag() + nextDown());
    }
    case PlayKind::Run: {
      const std::string rb = pname(r.rusher);
      const Slots ru = with({{"rb", rb}, {"rec", r.fumbleRecBy ? nm(r.fumbleRecBy) : std::string()}});
      // the live call just said his name: the result can go straight to the yards
      const bool saidRun = said("handoff") && chance(0.55);
      if (r.fumbleBy) {
        energy = 3;
        if (r.fumbleLost) return say("r_rfum", ru);
        return say(y > 0 ? "r_rfumkept" : "r_rfumkept0", ru);
      }
      if (r.td) {
        energy = 3;
        return say(y <= 4 ? "r_rtds" : "r_rtd", ru);
      }
      if (r.safety) {
        energy = 3;
        return say("r_rsafety", ru);
      }
      if (y >= 20) {
        energy = 2;
        out = say("r_runbig", ru);
        if (!r.outOfBounds && !tk.empty() && chance(0.5) && &tailSlots() == &rs) out += say("r_runbig_tk", ru);
      } else if (y >= 5) {
        out = say(saidRun ? "r_runS" : "r_run", ru);
      } else if (y > 0) {
        out = say(saidRun && first ? "r_runshortF" : saidRun ? "r_runshortS" : "r_runshort", ru);
      } else if (y == 0) {
        out = say("r_run0", ru);
      } else {
        energy = 2;
        out = say("r_runloss", ru);
      }
      if (r.outOfBounds && y > 0 && y < 20 && chance(0.4)) out = say("r_runoob", ru);
      return out + later(firstTag() + nextDown());
    }
    case PlayKind::Pass: {
      if (r.thrownAway) {
        if (said("away")) {
          const int nd2 = downBefore_ + 1;
          if (nd2 > 4) return std::string();
          if (nd2 == 4) return say("r_away4", rs);  // (the setup gives the distance)
          ddSaidKey_ = bDdKey(g, nd2, toGoBefore_, los);
          return say("r_away2", with({{"nd", bDownDist(nd2, toGoBefore_, los)}}));
        }
        return say("r_away", rs) + later(nextDown());
      }
      if (r.interception && !r.turnover) {
        if (giveBackPlay_ != playNo_) {
          giveBackPlay_ = playNo_;
          ++giveBack_[other(g.poss) == TeamKey::Home ? 0 : 1];  // (the intercepting side put it on the ground)
        }
        energy = 3;
        const std::string who = r.intBy ? nm(r.intBy) : std::string("the defender");
        if (r.intBy) gaveBack_.insert(r.intBy->id);
        return say("r_intback", with({{"who", who}}));
      }
      if (r.interception) {
        energy = 3;
        const std::string who = r.intBy ? pname(r.intBy) : std::string("the defense");
        const Slots is = with({{"who", who}, {"intat", spot(g, 100 - endX, other(g.poss))}});
        // the man who gave one back earlier: this one he keeps
        const std::string amends = r.intBy && gaveBack_.count(r.intBy->id) && once("amends" + bS(r.intBy->id)) ? say("r_amends", is) : std::string();
        if (r.td == Side::Def) return say("r_pick6", is) + amends;
        // (downed in his own end zone: a touchback, not "returns it to the Portland 1")
        if (100 - r.endX <= 0.5) return say("r_inttb", is) + amends;
        if (!amends.empty()) return say("r_int", is) + amends;
        return say(said("int") ? "r_int2" : "r_int", is);
      }
      if (r.incomplete) {
        const std::string tg = r.target ? nm(r.target) : std::string("his receiver");
        bool drop = false;
        for (const PlayEvent& e : s.events)
          if (e.type == "drop") drop = true;
        const std::string pbF = r.pbuBy ? pname(r.pbuBy) : std::string();  // (the first time he is named: his full name)
        const Slots is = with({{"tg", tg}, {"pbu", r.pbuBy ? nm(r.pbuBy) : std::string()}, {"pbuf", pbF}});
        if (r.pbuBy && said("pbu")) out = say("r_pbu2", is);
        else if (drop && said("drop")) out = say("r_drop2", is);
        else if (r.pbuBy) out = say("r_pbu", is);
        else if (drop) out = say("r_drop", is);
        else out = say("r_inc", is);
        return out + later(nextDown());
      }
      const std::string rc = pname(r.receiver);
      const Slots ps = with({{"rc", rc}});
      const bool otherMan = (said("throw") || said("deep")) && r.receiver && spokenTarget_ >= 0 && spokenTarget_ != r.receiver->id;
      if (otherMan && !r.td && !r.fumbleBy) {
        if (y >= 25) energy = 2;
        return say(y > 0 ? "r_otherman" : "r_otherman0", ps);
      }
      if (r.fumbleBy) {
        energy = 3;
        return say(r.fumbleLost ? "r_pfum" : "r_pfumkept", ps);
      }
      if (r.td) {
        energy = 3;
        return say(said("catch") || said("throw") || said("deep") ? "r_ptd2" : "r_ptd", ps);
      }
      if (y >= 25) {
        energy = 2;
        out = say("r_pbig", ps);
      } else if ((said("throw") || said("deep")) && spokenTarget_ == (r.receiver ? r.receiver->id : -2) && y > 0) {
        out = say("r_pcomp2", ps);
      } else if (y > 0) {
        out = say("r_pcomp", ps);
      } else {
        out = say(y == 0 ? "r_pzero" : "r_ploss", ps);  // (a yard lost is a loss: "no gain" read wrong next to "3rd and 11")
      }
      if (r.outOfBounds && y > 0 && chance(0.4)) out = bJoin(out, say("r_poob", tailSlots()));
      return out + later(firstTag() + nextDown());
    }
  }
  return std::string();
}

void Booth::returnResult(const Game& g, const PlaySim& s, const PlayResult& r) {
  drive_.active = false;
  (void)s;
  const bool punt = s.cfg.kick && s.cfg.kick->punt;
  const Player* who = s.returner ? s.returner->p : nullptr;
  const std::string rn = who ? pname(who) : std::string("the returner");
  const double endX = (std::max)(0.5, (std::min)(99.5, r.endX));
  const int ry = bRound(endX - (s.catchX > 0 ? s.catchX : (s.cfg.kick ? s.cfg.kick->landX : endX)));
  const Slots rs = facts(g, {{"rn", rn}, {"ry", bS(ry)}, {"at", spot(g, endX, g.poss)}, {"to", fieldX(endX)}, {"kind", punt ? "punt" : "kickoff"}, {"team", g.offense().city},
                             {"kteam", g.defense().city}});
  if (r.td == Side::Off) {
    push(Who::PBP, Kind::Result, say("ret_td", rs), 3, 5, 0.3, 10).snapSafe = true;
    push(Who::Color, Kind::Analysis, say("ret_tdA", rs), 2, 3, 0, 20);
    return;
  }
  if (r.fumbleLost) {
    push(Who::PBP, Kind::Result, say("ret_fum", rs), 3, 5, 0.2, 10).snapSafe = true;
    return;
  }
  if (r.endX <= 0.0) {
    // (downed in his own end zone after fielding it: a touchback, the ball out at the 35 / the 20, not "the Austin 1")
    push(Who::PBP, Kind::Result, say("ret_tb", facts(g, {{"rn", rn}, {"kind", punt ? "punt" : "kickoff"}, {"team", g.offense().city}, {"yl", punt ? "20" : "35"}})), 0, 3, 0.3,
         10);
    return;
  }
  std::string out;
  const bool spotSaid = said("down");
  if (ry >= 30) out = say(endX >= 45 ? "ret_big" : "ret_big2", rs);
  else if (ry > 0 && spotSaid) out = say("ret2", rs);
  else if (ry > 0) out = say("ret", rs);
  else out = say("ret0", rs);
  push(Who::PBP, Kind::Result, out, ry >= 30 ? 2 : 1, 4, 0.3, 10);
  if (ry >= 30 && chance(0.6)) push(Who::Color, Kind::Analysis, say("ret_A", rs), 1, 2, 0, 15);
}

// ------------------------------------------------------------------ the analyst

std::optional<std::string> Booth::analysis(const Game& g, const PlaySim& s, const PlayResult& r, int& energy) {
  struct Angle {
    double score;
    std::string topic;
    std::string text;
  };
  std::vector<Angle> c;
  creditRead_ = false;  // (a credit is for this play's read, and only when the breakdown is about it: decided below)
  auto add = [&](double sc, const std::string& topic, const std::string& text, double cool = 1300) {
    if (text.empty() || !cooled(topic, cool) || spokenSet_.count(text)) return;
    // (the analyst does not say back what the play-by-play man just said: "...will want that one back" twice)
    if (bEcho(text, resultSaid_)) return;
    if (s.cfg.defCall && text.find(bCoverageWords(s.cfg.defCall->coverage)) != std::string::npos && !cooled("covname", 420)) return;
    c.push_back({sc + rng_.range(0, 0.25), topic, text});
  };
  const OffPlay* op = s.cfg.offPlay;
  const DefCall* dc = s.cfg.defCall;
  const bool blitz = dc && !dc->blitz.empty();
  const bool big = r.yards >= 20 || r.td || r.turnover;
  const std::string qb = r.passer ? nm(r.passer) : (s.qb ? nm(s.qb->p) : std::string("the quarterback"));
  std::string cov = dc ? bCoverageWords(dc->coverage) : std::string("zone");
  // a blocker the play showed: the pancake, the man who beat his block
  const Player* pancake = pancakes_.empty() ? nullptr : pancakes_.front();
  const Shed* shed = sheds_.empty() ? nullptr : &sheds_.front();
  // the facts a breakdown can name (each wording names the ones it needs)
  const Slots as = facts(g, {{"qb", qb}, {"rc", r.receiver ? nm(r.receiver) : std::string()}, {"rb", r.rusher ? nm(r.rusher) : std::string()},
                             {"cdef", catchDef_ ? nm(catchDef_) : std::string()}, {"route", bRoute(catchRoute_)}, {"cov", cov},
                             {"yds", r.yards > 0 ? bS(r.yards) : std::string()}, {"yac", r.yac >= 1 ? bS(static_cast<int>(r.yac)) : std::string()},
                             {"air", r.airYards >= 1 ? bS(static_cast<int>(r.airYards)) : std::string()},
                             {"who", r.intBy ? nm(r.intBy) : std::string("the defender")}, {"pbu", r.pbuBy ? nm(r.pbuBy) : std::string()},
                             {"sk", r.sackedBy ? nm(r.sackedBy) : std::string()}, {"dl", shed && shed->d ? nm(shed->d) : std::string()},
                             {"blk", shed && shed->b ? nm(shed->b) : std::string()}, {"move", shed ? bMoveWords(shed->move) : std::string()},
                             {"pan", pancake ? nm(pancake) : std::string()}, {"hitter", hitBy_ ? nm(hitBy_) : std::string()},
                             {"tg", (r.receiver ? r.receiver : r.target) ? nm(r.receiver ? r.receiver : r.target) : std::string()},
                             {"loss", r.yards < 0 ? bS(-r.yards) : std::string()}, {"brk", bCountWord(r.brokenTackles)},
                             {"qbnote", qb + (qbSharp(g, r.passer) ? " has been calm all day" : " came through when it mattered")}});
  auto with = [&as](Slots e) {
    e.insert(e.end(), as.begin(), as.end());
    return e;
  };
  // an angle's wordings, filled: "" when none can be said now
  auto w = [&](const char* bank) { return say(bank, as); };
  switch (r.kind) {
    case PlayKind::Pass: {
      if (r.complete && !r.interception) {
        if (!bRoute(catchRoute_).empty() && r.yards >= 6) add(1.05, "a_route", w("a_route"), 1800);
        if (r.catchSep >= 5 && r.airYards >= 6 && r.yards >= 8) add(1.0, "a_open", w(bManCov(dc->coverage) ? "a_openman" : "a_openzone"));
        if (r.catchSep >= 0 && r.catchSep < 1.2 && r.airYards >= 8 && r.yards >= 8) add(1.1, "a_tight", w("a_tight"));
        if (r.yac >= 10 && r.yac > r.yards + 0.5)  // (caught behind the line: the whole gain is his)
          add(1.0 + r.yac * 0.02, "a_yacb", w("a_yacb"));
        else if (r.yac >= 10)
          add(1.0 + r.yac * 0.02, "a_yac", w("a_yac"));
        if (r.airYards >= 25) add(1.2, "a_deep", w("a_deep"));
        if (r.throwPath == 1 && blitz && r.yards >= 5) add(1.3, "a_hot", w("a_hot"));
        if (r.timeToThrow >= 3.4 && !r.pressured && r.yards >= 8) add(0.9, "a_time", w("a_time"));
        if (r.pressured && r.yards >= 8 && r.airYards >= 15) add(1.0, "a_hitthrow", w("a_hitthrow"));
        if (op && op->type == PlayType::PlayAction && r.yards >= 8) {
          const TeamLine& otl = g.team[g.poss];
          const bool runWorks = otl.rushAtt >= 8 && otl.rushYds >= otl.rushAtt * 4.3;  // (the fake only holds them if the run has been hurting them)
          add(1.0, "a_pa", w(runWorks ? "a_pa" : "a_pa2"));
        }
        if (op && op->type == PlayType::Screen && r.yards >= 6) add(1.0, "a_screen", w("a_screen"));
        if (r.brokenTackles > 0 && r.yards >= 10) add(1.0, "a_brk", w(r.brokenTackles >= 2 ? "a_brk2" : "a_brk1"));
        if (downBefore_ == 3 && r.yards >= toGoBefore_) add(tier(g) == 0 && r.yards >= 5 ? 0.95 : 0.0, "a_3rdconv", w("a_3rdconv"));
      } else if (r.interception) {
        if (!r.turnover) add(2.2, "a_intback", w("a_intback"));
        if (r.pressured) add(1.4, "a_intp", w("a_intp"));
        if (r.airYards >= 20) add(1.2, "a_intd", w("a_intd"));
        add(1.0, "a_intr", w("a_intr"));
      } else if (r.incomplete) {
        bool drop = false;
        for (const PlayEvent& e : s.events)
          if (e.type == "drop") drop = true;
        if (drop) add(1.1, "a_drop", w("a_drop"));
        if (r.pbuBy) add(1.1, "a_pbu", w("a_pbu"));
        if (r.thrownAway) add(0.9, "a_away", w("a_away"));
        // (one cause a play: a breakup is the coverage, not the rush; a hurried throw is not a miss)
        if (r.pressured && !r.thrownAway && !drop && !r.pbuBy) add(1.0, "a_hurry", w("a_hurry"));
        if (!drop && !r.pbuBy && !r.thrownAway && !r.pressured && r.airYards >= 15) add(0.8, "a_over", w("a_over"));
      }
      break;
    }
    case PlayKind::Sack: {
      if (shed && shed->d == r.sackedBy && shed->b)
        if (once("a_sackmove_sig")) add(1.4, "a_sackmove", w("a_sackmove"));
      if (blitz) {
        bool unblocked = false;
        for (const std::string& role : dc->blitz)
          if (const Ent* e = s.defRole(role))
            if (r.sackedBy && e->p == r.sackedBy) unblocked = true;
        if (unblocked) add(1.3, "a_sackblitz", w("a_sackblitz"));
        else add(0.9, "a_sackblitz2", w("a_sackblitz2"));
      }
      if (s.t >= 3.6 && !(shed && shed->d == r.sackedBy) && !said("press") && !said("pressure")) add(1.0, "a_cov", w("a_cov"));
      if (downBefore_ <= 2 && r.yards <= -6) add(0.5, "a_sackgen", w("a_sackgen"));
      break;
    }
    case PlayKind::Run:
    case PlayKind::Scramble: {
      if (r.kind == PlayKind::Scramble && r.yards >= 6) add(1.1, "a_scr", w(dc && bManCov(dc->coverage) ? "a_scrman" : "a_scr"));
      if (r.yards >= 8 && pancake) add(1.2, "a_pancake", w("a_pancake"));
      if (r.yards >= 10 && r.olBlockDepth >= 3.5) add(1.1, "a_second", w("a_second"));
      if (r.yards <= 0 && shed && shed->d) {
        const bool tackled = std::find(r.tacklers.begin(), r.tacklers.end(), shed->d) != r.tacklers.end();
        if (tackled) add(1.2, "a_stuff", w(shed->b ? "a_stuff" : "a_stuff0"), 1500);
      }
      if (r.brokenTackles > 0 && r.yards >= 8) add(1.0, "a_rbrk", w("a_rbrk"));
      for (const std::string& m : movesSeen_) {
        if (r.yards < 5) break;  // a move that did not get him anywhere is not the story
        if (m == "spin") add(1.0, "a_spin", w("a_spin"));
        if (m == "truck") add(1.0, "a_truck", w("a_truck"));
        if (m == "stiffarm") add(1.0, "a_stiff", w("a_stiff"));
        if (m == "juke") add(0.9, "a_juke", w("a_juke"));
      }
      if (op && op->run && r.yards >= 5) {
        const RunStyle st = op->run->style;
        if (st == RunStyle::Gap) add(0.9, "a_gap", w("a_gap"));
        if (st == RunStyle::Stretch) add(0.9, "a_stretch", w("a_stretch"));
        if (st == RunStyle::Draw) add(1.0, "a_draw", w("a_draw"));
        if (st == RunStyle::Zone) add(0.7, "a_zone", w("a_zone"));
      }
      if (toGoBefore_ <= 2 && r.yards >= toGoBefore_ && (downBefore_ >= 3)) add(1.1, "a_short", w("a_short"));
      if (toGoBefore_ <= 2 && r.yards < toGoBefore_ && downBefore_ >= 3) add(1.1, "a_shortstop", w("a_shortstop"));
      if (r.yards >= 20) add(0.8, "a_bigrun", w("a_bigrun"));
      if (hitBy_) add(1.3, "a_hit", w("a_hit"));
      break;
    }
    default:
      break;
  }
  // the read he made before the snap: right or wrong, he says so
  bool dropped = false;
  for (const PlayEvent& e : s.events)
    if (e.type == "drop") dropped = true;
  const bool pressureShowed = r.pressured || r.kind == PlayKind::Sack || said("press") || said("pressure");
  if (lastRead_ == "r_blitz" && blitz && pressureShowed && !dropped && chance(0.5)) creditRead_ = true;
  if (lastRead_ == "r_blitz" && !dropped && (pressureShowed || !blitz))
    add(1.6, "a_readblitz", w(blitz ? (r.yards >= 8 ? "a_readblitz1" : "a_readblitz2") : "a_readblitz0"), 0);
  if (lastRead_ == "r_c0" && (r.yards >= 15 || r.incomplete || r.kind == PlayKind::Sack)) add(1.5, "a_readc0", w(r.yards >= 15 ? "a_readc0w" : "a_readc0s"), 0);
  if (lastRead_ == "r_4th") add(1.4, "a_read4th", w(r.yards >= toGoBefore_ ? "a_read4thy" : "a_read4thn"), 0);
  if (const size_t hashAt = lastRead_.find('#'); hashAt != std::string::npos && r.kind == PlayKind::Pass) {
    const std::string topic = lastRead_.substr(0, hashAt);
    const int subj = std::atoi(lastRead_.c_str() + hashAt + 1);
    const Player* tgt = r.receiver ? r.receiver : r.target;
    if (tgt && tgt->id == subj) {
      if (r.complete && !r.interception && r.yards >= 8) {
        creditRead_ = chance(0.6);
        add(1.7, "a_readwin" + topic, w(topic == "r_press" ? "a_readpress" : topic == "r_slot" ? "a_readslot" : "a_readmatch"), 0);
      } else if (r.incomplete && r.pbuBy) {
        add(1.6, "a_readlost" + topic, w("a_readlost"), 0);
      }
    }
  }
  // the routine down, read like a coach: staying ahead of the chains, throwing short of the sticks, a run streak
  const bool runKind = r.kind == PlayKind::Run;
  const int runsInRow = (runStreakTeam_ == possBefore_ ? runStreak_ : 0) + (runKind ? 1 : 0);
  if (runKind && downBefore_ == 1 && r.yards >= 4 && r.yards < 10) add(0.55, "a_ahead", w("a_ahead"));
  if ((runKind || r.kind == PlayKind::Sack) && downBefore_ <= 2 && r.yards <= -1) add(0.6, "a_behind", w("a_behind"));
  if (r.kind == PlayKind::Pass && r.complete && downBefore_ == 3 && toGoBefore_ >= 5 && r.yards < toGoBefore_ && !r.td) add(0.75, "a_short3", w("a_short3"));
  if (runKind && runsInRow >= 4 && r.yards >= 3) add(0.65, "a_runstreak", say("a_runstreak", with({{"runs", bCountWord(runsInRow)}})), 300);
  if (r.kind == PlayKind::Pass && r.complete && r.yards >= 6 && r.yards < 20 && dc) add(0.5, "a_readcov", w("a_readcov"));
  if (r.kind == PlayKind::Pass && r.complete && r.catchSep >= 0 && r.catchSep < 1.0 && r.yards >= 5 && !r.td) add(0.6, "a_contact", w("a_contact"));
  if (r.kind == PlayKind::Run && r.yards >= 3 && r.yards < 8 && downBefore_ == 3 && r.yards >= toGoBefore_) add(0.7, "a_3rdrun", w("a_3rdrun"));
  // (a game already decided gets no "momentum" or "statement": the score says what a touchdown or a turnover means now)
  const bool decided = tier(g) >= 2;
  if (r.td && *r.td == Side::Def) add(1.2, "a_deftd", w(decided ? "a_tdlate" : "a_deftd"));
  if (r.td && *r.td == Side::Off && r.kind != PlayKind::Sack) {
    const double drivePl = drive_.plays + 1;
    if (drivePl >= 8 && !decided) add(1.0, "a_tddrive", say("a_tddrive", with({{"plays", bS(static_cast<int>(drivePl))}})), 600);
    add(0.7, "a_tdgen", w(decided ? "a_tdlate" : "a_tdgen"));
  }
  if (r.turnover && !r.td && !decided) add(0.6, "a_to", w("a_to"), 400);
  if (c.empty()) return std::nullopt;
  if (g.q >= 4 && g.clock - r.duration <= 0.05) return std::nullopt;  // the game's last snap: the close says it
  // routine plays get the analyst some of the time; big ones nearly always
  const bool silent = now_ - lastColorT_ > 60 || (g.q >= 4 && g.clock < 150);  // a minute without him, or the end of the game
  const double want = big || silent || downBefore_ >= 3 || r.yards >= 15 ? 0.97 : (r.kind == PlayKind::Sack || r.interception) ? 0.95 : (view_.canHold || view_.userGame) ? 0.92 : 0.5;
  if (!chance(want)) return std::nullopt;
  std::sort(c.begin(), c.end(), [](const Angle& a, const Angle& b) { return a.score > b.score; });
  // "you called it" only goes with the breakdown that says the read came true
  if (creditRead_ && c[0].topic.rfind("a_readblitz", 0) != 0 && c[0].topic.rfind("a_readwin", 0) != 0) creditRead_ = false;
  mark(c[0].topic);
  lastAnalysisTopic_ = c[0].topic;
  spokenSet_.insert(c[0].text);
  if (dc && c[0].text.find(bCoverageWords(dc->coverage)) != std::string::npos) mark("covname");
  {
    // the angles about the play itself (a replay can show them); the rest is about the drive or the game
    static const char* const kPlay[] = {"a_open", "a_tight", "a_yac", "a_deep", "a_hot", "a_hitthrow", "a_pa", "a_screen", "a_brk", "a_intp", "a_intd", "a_intr",
                                        "a_sackmove", "a_sackblitz", "a_cov", "a_pancake", "a_second", "a_stuff", "a_rbrk", "a_spin", "a_truck", "a_stiff", "a_juke",
                                        "a_gap", "a_stretch", "a_draw", "a_zone", "a_bigrun", "a_hit", "a_readblitz", "a_readc0", "a_contact", "a_drop", "a_pbu"};
    playAngle_ = false;
    for (const char* k : kPlay)
      if (c[0].topic == k) playAngle_ = true;
  }
  energy = big ? 2 : 1;
  return c[0].text;
}

// ------------------------------------------------------------------ after the play is applied (the log line)

void Booth::afterApply(const Game& g, const PlaySim& s, const PlayResult& r, const PlayLog& l) {
  (void)l;
  adjHome_ = adjAway_ = 0;
  // the drive
  if (!drive_.active || drive_.team != possBefore_) {
    drive_ = DriveTrack();
    drive_.team = possBefore_;
    drive_.startBallOn = losBefore_;
    drive_.startClock = clockBefore_;
    drive_.startQ = qBefore_;
    drive_.active = true;
  }
  drive_.plays++;
  if (runStreakTeam_ != possBefore_) {
    runStreakTeam_ = possBefore_;
    runStreak_ = 0;
  }
  runStreak_ = r.kind == PlayKind::Run ? runStreak_ + 1 : 0;
  // streaks
  if (r.passer && r.kind == PlayKind::Pass) {
    Streak& st = qbStreak_[r.passer->id];
    if (r.complete && !r.interception) {
      st.completions++;
      st.incompletions = 0;
    } else {
      st.completions = 0;
      st.incompletions++;
    }
  }
  if (blitzOn(s)) {
    blitzCount_[possBefore_ == TeamKey::Home ? 1 : 0]++;
  }
  // the result (held from the whistle for the officials), then a declined flag, then the breakdown
  if (!pendingResult_.empty()) {
    push(Who::PBP, Kind::Result, pendingResult_, pendingResultEnergy_, 4, 0, 12).snapSafe = r.turnover;
    pendingResult_.clear();
  }
  if (bHas(l.desc, "(declined)")) {
    if (ref_) {
      // "... Offensive Holding (#72 J.Smith) on ABQ (declined)."
      const std::string& d = l.desc;
      const size_t dec = d.rfind(" (declined)");
      const size_t on = d.rfind(" on ", dec);
      const std::string ab = on == std::string::npos ? std::string() : d.substr(on + 4, dec - on - 4);
      const Team& t = g.home->abbr == ab ? *g.home : *g.away;
      // (the foul's name nearest the " on ABQ": the one that ends last, the longest of those - "Offensive Holding", not "Holding")
      std::string name;
      size_t nameAt = std::string::npos, nameEnd = 0;
      for (const RefFoul& f : kRefFouls) {
        const size_t p = d.rfind(f.log, on);
        if (p == std::string::npos) continue;
        const size_t e = p + std::strlen(f.log);
        if (nameAt == std::string::npos || e > nameEnd || (e == nameEnd && p < nameAt)) {
          nameAt = p;
          nameEnd = e;
          name = f.log;
        }
      }
      std::string who;
      if (nameAt != std::string::npos) {
        const size_t par = d.find(" (#", nameAt);
        if (par != std::string::npos && par < on) who = d.substr(par + 2, d.find(')', par) - par - 2);
      }
      const Player* man = bLogMan(t, who);
      if (man && man->pos == Pos::QB && bHas(name, "Holding")) man = nullptr;
      if (name == "Intentional Grounding" && r.passer) man = r.passer;
      std::string ref = refFoul(name, &t == &g.teamOf(possBefore_), man) + ". The penalty is declined.";
      if (g.poss == possBefore_ && !r.td && g.phase == GamePhase::Scrimmage) ref += g.down == 1 ? " First down." : " " + bCap(bOrdWord(g.down)) + " down.";
      refSay(ref, 0.2);
    } else
      push(Who::PBP, Kind::Result, say("declined", facts(g, {})), 1, 4, 0, 10);
  }
  // a flag that is the play's whole story: grounding on fourth down (the ball goes over), or in the end zone (a safety)
  if (bHas(l.desc, "PENALTY on ") && (bHas(l.desc, "Turnover on downs") || bHas(l.desc, "SAFETY"))) {
    const bool safety = bHas(l.desc, "SAFETY");
    const Slots gs = facts(g, {{"def", g.teamOf(other(possBefore_)).city}});
    push(Who::PBP, Kind::Result, say(safety ? "pen_safety" : "pen_tod", gs), safety ? 3 : 2, 4, 0.2, 15).snapSafe = true;
    if (ref_)
      refSay(refFoul("Intentional Grounding", true, r.passer) + (safety ? ", in the end zone. The result of the play is a safety." : ". Loss of down. The ball goes over on downs."));
    if (safety) {
      freeKick_ = true;
      PlayResult none;
      onScored(g, other(possBefore_), 2, none, true);
    }
  }
  if (risky4th_ && downBefore_ == 4 && lastRead_ == "r_4th") {
    // the analyst said he would have punted: now they both know how it went
    const bool got = r.yards >= toGoBefore_ && !r.turnover;
    const Slots fs = facts(g, {});
    push(Who::PBP, Kind::Analysis, say(got ? "r4_ok" : "r4_no", fs), 1, 3, 0, 15);
    push(Who::Color, Kind::Analysis, say(got ? "r4_oka" : "r4_noa", fs), 1, 3, 0, 15);
    pendingAnalysis_.clear();
  }
  risky4th_ = false;
  if (fourthFail_ && !sealed_ && g.q >= 4 && g.clock < 240 && possBefore_ != g.poss && std::abs(g.margin()) <= 8 && tier(g) == 0) {
    pendingAnalysis_ = say("bigstop", facts(g, {{"ahead", g.offense().city}, {"behind", g.teamOf(possBefore_).city}}));
    pendingAnalysisTopic_ = "bigstop";
  }
  fourthFail_ = false;
  if (sealed_) {
    sealed_ = false;
    pendingAnalysis_ = say("sealed", facts(g, {{"team", g.teamOf(other(possBefore_)).city}}));
    pendingAnalysisTopic_ = "sealed";
  }
  if (!pendingAnalysis_.empty()) {
    // the credit for a read goes right before the breakdown it belongs to (and only that one: a stop that replaced it
    // gets none); the credit, the breakdown and a "good eye" after it are one exchange, dropped together if at all
    const bool credit = !creditLine_.empty() && creditFor_ == pendingAnalysis_;
    if (credit) push(Who::PBP, Kind::Analysis, creditLine_, 1, 3, 0, 20).tag = "question";
    creditLine_.clear();
    creditFor_.clear();
    Line& al = push(Who::Color, Kind::Analysis, pendingAnalysis_, pendingAnalysisEnergy_, 3, 0, 20);
    al.topic = pendingAnalysisTopic_;
    // ("good eye" answers a breakdown of the play itself, not a line about the down or the game)
    const bool ack = !credit && playAngle_ && pendingAnalysisTopic_ == lastAnalysisTopic_ && chance(0.12) && cooled("ack", 300);
    al.tag = credit ? "answer" : ack ? "question" : std::string();
    pendingAnalysis_.clear();
    pendingAnalysisTopic_.clear();
    if (ack) {
      mark("ack");
      push(Who::PBP, Kind::Analysis, say("ack", facts(g, {})), 0, 2, 0, 10).tag = "answer";
    }
    // now and then the play-by-play man adds the fact that goes with it
    const Player* who = r.receiver && r.complete ? r.receiver : r.rusher;
    if (who && !r.turnover && r.yards >= 5 && cooled("stat:" + bS(who->id), 240) && chance(0.45)) {
      const PlayerLine& ln = const_cast<StatBook&>(g.stats).line(*who, teamOfPlayer(g, who).abbr);
      const bool rec = who == r.receiver;
      const int n = rec ? ln.rec : ln.rushAtt, y = rec ? ln.recYds : ln.rushYds;
      if (n >= 3) mark("stat:" + bS(who->id));  // (one stat line per man per stretch: the follow-up said it again)
      if (n >= 3)
        push(Who::PBP, Kind::Story,
             say(rec ? "addfact_rec" : "addfact_rush", facts(g, {{"name", nm(who)}, {"nth", n <= 10 ? std::string(bOrdWord(n)) : bS(n) + "th"}, {"n", bS(n)}, {"y", bS(y)}})),
             0, 2, 0, 12);
    }
  }
  // a score
  if (r.td) {
    const TeamKey scorer = *r.td == Side::Off ? possBefore_ : other(possBefore_);
    onScored(g, scorer, 6, r);
  }
  // the word from the sideline on a man who went down a few plays ago
  for (auto it = injuryWord_.begin(); it != injuryWord_.end();)
    if (playNo_ >= it->second) {
      push(Who::PBP, Kind::Story, say("inj_word", facts(g, {{"name", it->first}})), 0, 2, 0, 20);
      it = injuryWord_.erase(it);
    } else
      ++it;
  // a man hurt on the play: after the play is called (on the game's last play, after the final: the close says them)
  const bool overNow = g.phase == GamePhase::Final || (g.q >= 4 && g.clock <= 0.01 && g.score.home != g.score.away);
  if (!overNow) {
    for (size_t i = 0; i < pendingInjury_.size(); ++i)
      push(i % 2 == 0 ? Who::PBP : Who::Color, Kind::Break, pendingInjury_[i], 0, 3, 0.5, 30).snapSafe = true;
    pendingInjury_.clear();
  }
  if (r.safety) {
    freeKick_ = true;
    PlayResult none;
    onScored(g, other(possBefore_), 2, none, true);
    push(Who::PBP, Kind::Break, say("freekick", facts(g, {{"team", g.teamOf(possBefore_).city}})), 0, 3, 0, 15).snapSafe = true;
  }
  // a written exchange waiting: next in line after this play's call and breakdown (the booth is never quiet enough to
  // wait for silence: they went stale ten plays deep). Not on a score, a turnover, fourth down or the late, close snaps.
  // (a written exchange airs in the dead time it was written for: idle(), before the next snap)
  // the writer: an exchange about the game so far, for a quiet moment a few plays from now
  // (not while a written one still waits to air: they piled up and went stale, two of three calls wasted)
  const bool waiting = std::any_of(writerReady_.begin(), writerReady_.end(), [](const WriterReady& w) { return w.kind == "exchange"; });
  const bool deadTime = r.td || r.safety || g.poss != possBefore_ || g.phase != GamePhase::Scrimmage;  // (extra point, kickoff, punt, change of hands)
  if (writer_ && !writerBusy_ && !waiting && deadTime && !(g.q >= 4 && g.clock < 120) && !(g.q == 2 && g.clock < 30)) {
    lastWriterAsk_ = now_;
    askWriter(g, "exchange");
  }
  // the plays the close will remember
  if (r.td && r.yards >= 25 && bigPlays_.size() < 6) bigPlays_.push_back("The " + bS(r.yards) + "-yard touchdown" + (r.receiver ? " to " + nm(r.receiver) : r.rusher ? " run by " + nm(r.rusher) : std::string()) + ".");
  if (r.interception && r.td && r.intBy) bigPlays_.push_back("The pick six by " + nm(r.intBy) + ".");
  {
    // a fourth-down stop "sealed it" only when the offense was within one score, behind
    const int mBefore = possBefore_ == TeamKey::Home ? scoreBefore_.home - scoreBefore_.away : scoreBefore_.away - scoreBefore_.home;
    if (downBefore_ == 4 && toGoBefore_ <= 10 && !r.td && r.yards < toGoBefore_ && g.q >= 4 && g.clock < 240 && mBefore < 0 && mBefore >= -8)
      bigPlays_.push_back("And that fourth-down stop that sealed it.");
  }
  // the man, the first time he makes a play: what the viewer should know about him
  {
    const Player* who = r.td || r.yards >= 12 ? (r.receiver && r.complete ? r.receiver : r.rusher) : r.sackedBy ? r.sackedBy : r.intBy;
    if (who && !bioSaid_.count(who->id)) {
      bioSaid_.insert(who->id);
      const int ft = static_cast<int>(who->heightIn) / 12, in = static_cast<int>(who->heightIn) % 12;
      const double forty = 4.28 + (99 - who->r.speed) * (5.25 - 4.28) / 49.0;
      char f[16];
      std::snprintf(f, sizeof f, "%.2f", forty);
      std::string bio;
      // (a height as "6-4" would read as a score: "6 foot 4")
      const Slots bs = facts(g, {{"name", nm(who)}, {"age", bS(who->age)}, {"forty", f}, {"ht", bS(ft) + " foot " + bS(in)}, {"lbs", bS(static_cast<int>(who->weightLb))},
                                 {"pos", bPosWord(who->pos)}, {"team", teamOfPlayer(g, who).city}});
      if (who->age <= 22) bio = say("bio_rook", bs);
      else if (who->age >= 32) bio = say("bio_vet", bs);
      else if (who->r.speed >= 93 && who->pos != Pos::DT && who->pos != Pos::OT && r.yards >= 18) bio = say("bio_fast", bs);
      else if (who->weightLb >= 235 && (who->pos == Pos::RB || who->pos == Pos::TE || who->pos == Pos::FB)) bio = say("bio_big", bs);
      else if (who->heightIn >= 76 && who->pos == Pos::WR) bio = say("bio_tall", bs);
      if (!bio.empty() && chance(0.7)) push(Who::Color, Kind::Story, bio, 1, 2, 0, 15);
    }
  }
  // a milestone or a stat worth hearing after a good play by one man
  statFollow(g, r);
  lastBig_ = r.yards >= 15 && (r.receiver || r.rusher) ? (r.receiver ? r.receiver->id : r.rusher->id) : -1;
  // possession changed hands without a score (an interception, a fumble, a turnover on downs)
  if (g.poss != possBefore_ && !r.td) {
    drive_.active = false;
  }
  flushDeferred(g);  // (the two-minute warning or a timeout logged before this play's line: now, after its call)
}

void Booth::onScored(const Game& g, TeamKey scorer, int pts, const PlayResult& r, bool announce) {
  (void)r;
  (void)pts;
  const Team& T = g.teamOf(scorer);
  if (unansweredBy_ == scorer) unanswered_ += pts;
  else {
    unansweredBy_ = scorer;
    unanswered_ = pts;
  }
  // a touchdown's score is read after the try (the extra point / two-point line); a field goal's right away
  if (pts != 6 && announce) push(Who::PBP, Kind::Score, scoreSay(g, scorer, ""), 1, 4, 0, 25).snapSafe = true;
  else tdBy_ = scorer;
  if (unanswered_ >= 14 && chance(0.7) && cooled("unanswered", 300)) {
    mark("unanswered");
    push(Who::Color, Kind::Story, say("unanswered", facts(g, {{"n", bS(unanswered_)}, {"team", T.city}, {"nick", nick(T)}})), 1, 3, 0, 25);
  }
  if (drive_.active && drive_.team == scorer && drive_.plays >= 3) {
    if (pts == 6) driveAfterTry_ = true;  // said after the extra point (kickResult), never over the try
    else afterDrive(g, pts == 3 ? "fg" : "td");
  }
  if (pts != 6) drive_.active = false;
}

std::string Booth::scoreSay(const Game& g, TeamKey scorer, const std::string& lead) {
  const Team& T = g.teamOf(scorer);
  const int h = g.score.home, a = g.score.away;
  const std::optional<TeamKey> leader = h == a ? std::nullopt : std::optional<TeamKey>(h > a ? TeamKey::Home : TeamKey::Away);
  // the score: said with the lead in front of it ("It's good from 42, and that makes it Austin 10, Fresno 7."), or with
  // what it changed (the lead, the tie) as a sentence of its own
  const Slots ss = facts(g, {{"team", T.city}, {"nick", nick(T)}});
  const bool join = lead.size() >= 2 && lead.compare(lead.size() - 2, 2, ", ") == 0;
  std::string head = lead;
  auto sentenceLead = [&head]() {
    if (head.size() >= 2 && head.compare(head.size() - 2, 2, ", ") == 0) head = head.substr(0, head.size() - 2) + ". ";
  };
  std::string s;
  if (leader && lastLeader_ && *leader != *lastLeader_ && *leader == scorer) {
    ++leadChanges_;
    sentenceLead();
    s = head + say("sc_lead", ss);
  } else if (leader && !lastLeader_ && *leader == scorer && lastSaidTotal_ > 0) {
    sentenceLead();
    s = head + say("sc_breaktie", ss);
  } else if (!leader && h > 0) {
    sentenceLead();
    s = head + say("sc_tie", ss);
  } else {
    s = head + say(join ? "sc_plainj" : "sc_plain", ss);
  }
  lastLeader_ = leader;
  lastSaidTotal_ = h + a;
  return s;
}

void Booth::afterDrive(const Game& g, const std::string& how) {
  const int plays = drive_.plays;
  if (plays < 3) return;
  const double startAbs = (drive_.startQ - 1) * g.quarterLen + (g.quarterLen - drive_.startClock);
  const double nowAbs = (g.q - 1) * g.quarterLen + (g.quarterLen - g.clock);
  const int secs = (std::max)(0, bRound(nowAbs - startAbs));
  const int yards = how == "td" ? bRound(100 - drive_.startBallOn) : bRound(preKick_.ballOn - drive_.startBallOn);
  if (yards < 25 && how != "td") return;
  const std::string t = bS(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + bS(secs % 60);
  const Team& D = g.teamOf(drive_.team);
  push(Who::PBP, Kind::Story,
       say(how == "td" ? "drive_td" : "drive_fg", facts(g, {{"plays", bS(plays)}, {"yards", bS(yards)}, {"time", t}, {"team", D.city}, {"nick", nick(D)}})), 0, 2, 0, 30);
}

void Booth::statFollow(const Game& g, const PlayResult& r) {
  // the man's line with this play in it (the book has it now)
  const Player* who = r.td || r.yards >= 15 ? (r.receiver && r.complete ? r.receiver : r.rusher) : nullptr;
  if (!who || r.turnover) return;
  const Team& T = teamOfPlayer(g, who);
  const PlayerLine& ln = const_cast<StatBook&>(g.stats).line(*who, T.abbr);
  const bool rec = r.receiver == who;
  const int yds = rec ? ln.recYds : ln.rushYds;
  const int n = rec ? ln.rec : ln.rushAtt;
  const int td = rec ? ln.recTD : ln.rushTD;
  std::string s;
  const std::string topic = "stat:" + bS(who->id);
  const Slots fs = facts(g, {{"name", nm(who)}, {"how", rec ? "receiving" : "rushing"}, {"what", rec ? "catches" : "carries"}, {"n", bS(n)}, {"yds", bS(yds)},
                             {"tdw", bCountWord(td)}, {"more", g.q <= 3 ? "and there's still plenty of football left" : ""}, {"team", T.city}});
  if (yds >= 100 && once("100:" + bS(who->id))) s = say("st100", fs);
  else if (td >= 2 && once("td" + bS(td) + ":" + bS(who->id))) s = say("st_tds", fs);
  else if (n >= 4 && yds >= 60 && cooled(topic, 420) && chance(0.5)) s = say("st_line", fs);
  if (s.empty()) return;
  mark(topic);
  push(chance(0.6) ? Who::PBP : Who::Color, Kind::Story, s, 1, 2, 0, 20).topic = topic;
}

// ------------------------------------------------------------------ kicks

void Booth::onKickTick(const Game& g, const View& v) {
  // a live return carries its own play (onEvent / onWhistle); its kick is called here from the sim's foot
  if (!kickPending_) {
    if (v.stage == View::Stage::Live && sim_ && sim_->isReturn() && sim_->kicked && !returnFootSaid_) {
      returnFootSaid_ = true;
      snapFlush();
      const bool punt = sim_->cfg.kick && sim_->cfg.kick->punt;
      const Player* k = sim_->kickerEnt ? sim_->kickerEnt->p : nullptr;
      const std::string kn = k ? nm(k) : std::string("the kicker");
      (void)kn;
      if (punt) live("", pick("k_punt", {"Here's the punt.", "And the punt is away."}), 1, 1.5);
      else if (freeKick_) live("", "Here's the free kick.", 1, 1.5);
      else live("", firstKick_ ? "And we're underway!" : pick("k_ko", {"Here's the kickoff.", "And the kickoff is away."}), 1, 1.5);
      freeKick_ = false;
      firstKick_ = false;
    }
    return;
  }
  const std::string& d = kickLog_.desc;
  const bool presented = v.stage == View::Stage::Kick;
  const double foot = presented ? v.kickLead : 0;
  const double land = presented ? v.kickLead + v.kickHang * 0.92 : 0;
  const std::string kn = d.substr(d.find('.') + 1, d.find(' ') - d.find('.') - 1);  // "J.Smith kicks" -> "Smith"
  if (!kickSetupSaid_) {
    kickSetupSaid_ = true;
    const PreKick& k = preKick_;  // (the Game has already turned the ball over: the decision was made on this down)
    if (kickKind_ == 1 && k.down == 4) {
      Line& l = push(Who::PBP, Kind::Live, pick("k_puntset", {"Fourth and " + bDist(k.toGo, k.ballOn) + ". They'll punt it away.", "Fourth down, and here comes the punt team.",
                                                              "They'll kick it away on fourth and " + bDist(k.toGo, k.ballOn) + "."}),
                     0, 5, 0, presented ? 3.0 : 5);
      l.live = true;
      l.tag = "kset";
      std::tie(l.alt, l.altEnergy) = genericAlt("kset:punt", l.text, std::string());
    }
    const Slots kr = facts(g, {{"down", bS(-k.margin)}, {"kdd", "4th and " + bDist(k.toGo, k.ballOn)}});
    if (kickKind_ == 1 && k.down == 4 && k.toGo <= 2 && k.ballOn >= 50 && k.tier == 0) push(Who::Color, Kind::Read, say("k_puntgo", kr), 1, 4, 0, 5).snapSafe = true;
    if (kickKind_ == 2 && k.down == 4 && k.toGo <= 2 && k.ballOn >= 70 && k.q >= 3 && k.margin < -3)
      push(Who::Color, Kind::Read, say("k_fgtake", kr), 1, 4, 0, 5).snapSafe = true;
    if (kickKind_ == 2 && bHas(d, "field goal")) {
      const int dist = std::atoi(d.c_str() + d.find(' ') + 1);
      Line& l = push(Who::PBP, Kind::Live, pick("k_fgset", {"Fourth down, and they'll send out kicker " + kn + " for a " + bS(dist) + "-yard attempt.", "Kicker " + kn + " lines up from " + bS(dist) + " yards.",
                                                          "Here's " + kn + ", from " + bS(dist) + " yards."}),
                     1, 5, 0, presented ? 4.0 : 6);
      l.live = true;
      l.tag = "kset";
      std::tie(l.alt, l.altEnergy) = genericAlt("kset:fg", l.text, std::string());
    }
  }
  if (!kickFootSaid_ && (!presented || v.kickT >= foot)) {
    kickFootSaid_ = true;
    snapFlush();  // what the booth was going to say is history: the ball is in the air
    if (kickKind_ == 0 && freeKick_) live("", "Here's the free kick.", 1, 1.5);
    else if (kickKind_ == 0) live("", firstKick_ ? "And we're underway!" : pick("k_ko2", {"Here's the kickoff.", "And the kickoff is away."}), 1, 1.5);
    else if (kickKind_ == 1) live("", pick("k_punt2", {"Here's the punt.", "And the punt is away."}), 1, 1.5);
    else if (bHas(d, "field goal")) live("", pick("k_fgfoot", {"The snap, the hold, the kick...", "It's up..."}), 2, 1.5);
    if (kickKind_ == 0) freeKick_ = false;
    firstKick_ = false;
  }
  if (!kickLandSaid_ && (!presented || v.kickT >= land)) {
    kickLandSaid_ = true;
    kickPending_ = false;
    kickResult(g, d);
  }
}

void Booth::kickResult(const Game& g, const std::string& d) {
  if (!bHas(d, "extra point") && !bHas(d, "field goal")) drive_.active = false;  // a kickoff or punt: the next snap starts a new drive
  const std::string kn = d.substr(d.find('.') + 1, d.find(' ') - d.find('.') - 1);  // "J.Smith kicks" -> "Smith"
  const int dist = std::atoi(d.c_str() + d.find(' ') + 1);
  const Slots ks = facts(g, {{"kicker", kn}, {"dist", dist > 0 ? bS(dist) : std::string()}, {"at", spot(g, g.ballOn, g.poss)}, {"yl", bS(bRound(g.ballOn))},
                             {"team", g.offense().city}, {"nick", nick(g.offense())}});
  if (bHas(d, "extra point")) {
    const bool good = bHas(d, "GOOD") && !bHas(d, "NO GOOD");
    struct After {
      Booth* b;
      const Game& g;
      ~After() {
        if (b->driveAfterTry_) {
          b->driveAfterTry_ = false;
          b->afterDrive(g, "td");
          b->drive_.active = false;
        }
      }
    } after{this, g};
    if (good) push(Who::PBP, Kind::Score, scoreSay(g, tdBy_, say("k_pat", ks)), 0, 4, 0.1, 15).snapSafe = true;
    else {
      push(Who::PBP, Kind::Score, scoreSay(g, tdBy_, say("k_patmiss", ks)), 2, 4, 0.1, 15).snapSafe = true;
      push(Who::Color, Kind::Analysis, say("k_patmissA", ks), 1, 3, 0, 15);
    }
    return;
  }
  if (bHas(d, "field goal")) {
    if (bHas(d, "is GOOD")) {
      const TeamKey scorer = g.kicking;  // a good field goal: the scoring team kicks off next
      push(Who::PBP, Kind::Score, scoreSay(g, scorer, say(dist >= 50 ? "k_fggood50" : "k_fggood", ks)), dist >= 50 ? 3 : 2, 5, 0.05, 15).snapSafe = true;
      if (dist >= 50) push(Who::Color, Kind::Analysis, say("k_fglong", ks), 1, 3, 0, 15);
      PlayResult none;
      onScored(g, scorer, 3, none, false);
    } else {
      push(Who::PBP, Kind::Result, say(bHas(d, "BLOCKED") ? "k_fgblock" : bHas(d, "right") ? "k_fgright" : "k_fgleft", ks), 3, 5, 0.05, 15).snapSafe = true;
      push(Who::Color, Kind::Analysis, say(bHas(d, "BLOCKED") ? "k_fgblockA" : "k_fgmiss", ks), 1, 3, 0, 15);
      drive_.active = false;
    }
    return;
  }
  if (bHas(d, "onside")) {
    const bool kept = bHas(d, "RECOVERED by the kicking team");
    push(Who::PBP, Kind::Result, say(kept ? "k_onside_kept" : "k_onside_lost", ks), kept ? 3 : 1, 5, 0, 10).snapSafe = true;
    return;
  }
  if (bHas(d, "punt") && bHas(d, "BLOCKED")) {
    push(Who::PBP, Kind::Result, say("k_puntblock", ks), 3, 5, 0, 10).snapSafe = true;
    return;
  }
  if (bHas(d, "MUFFED")) {
    push(Who::PBP, Kind::Result, say("k_muff", ks), 3, 5, 0, 10).snapSafe = true;
    return;
  }
  if (bHas(d, "FUMBLES")) {
    push(Who::PBP, Kind::Result, say("k_retfum", ks), 3, 5, 0, 10).snapSafe = true;
    return;
  }
  if (bHas(d, "TOUCHDOWN")) {
    push(Who::PBP, Kind::Result, say("k_rettd", ks), 3, 5, 0, 10).snapSafe = true;
    return;
  }
  // a kickoff out of bounds or short of the landing zone: the receiving team's ball at its 40 (a foul: the referee's)
  if (bHas(d, " kicks ") && (bHas(d, "out of bounds") || bHas(d, "short of the landing zone"))) {
    const bool oob = bHas(d, "out of bounds");
    if (ref_)
      refSay(oob ? "The kickoff went out of bounds. The ball will be placed at the receiving team's 40-yard line."
                 : "The kickoff did not reach the landing zone. The ball will be placed at the receiving team's 40-yard line.",
             0.2);
    else
      push(Who::PBP, Kind::Result, say(oob ? "k_oob" : "k_short", ks), 1, 3, 0, 10);
    return;
  }
  if (bHas(d, "Touchback")) {
    push(Who::PBP, Kind::Result, say("k_tb", ks), 0, 3, 0, 8);
    return;
  }
  if (bHas(d, "fair catch")) {
    push(Who::PBP, Kind::Result, say("k_fc", ks), 0, 3, 0, 8);
    return;
  }
  if (bHas(d, "downed")) {
    push(Who::PBP, Kind::Result, say("k_downed", ks), 1, 3, 0, 8);
    if (g.ballOn <= 10) push(Who::Color, Kind::Analysis, say("k_pinned", ks), 1, 2, 0, 12);
    return;
  }
  if (bHas(d, "returned")) {
    push(Who::PBP, Kind::Result, say("k_ret", ks), 1, 3, 0, 8);
    returnFoul(g, d);
    return;
  }
}

// a flag on a return: the return team's block, marked from where the return ended (Game::returnFoul)
void Booth::returnFoul(const Game& g, const std::string& d) {
  // " PENALTY on ABQ: Holding, 8 yards from the end of the return."
  const size_t at = d.find("PENALTY on ");
  if (at == std::string::npos) return;
  const size_t colon = d.find(':', at);
  const size_t comma = d.find(',', colon);
  if (colon == std::string::npos || comma == std::string::npos) return;
  const std::string name = d.substr(colon + 2, comma - colon - 2);
  const int y = std::atoi(d.c_str() + comma + 2);
  const RefFoul* rf = bRefFoul(name);
  const Slots fs = facts(g, {{"foul", rf ? lowerFirst(rf->words) : std::string("a block")}, {"yds", bS(y)}, {"team", g.offense().city}});
  if (!ref_) {
    push(Who::PBP, Kind::Result, say("ret_flag", fs), 1, 3, 0, 12).snapSafe = true;
    return;
  }
  push(Who::PBP, Kind::Result, say("ret_flaglead", fs), 1, 3, 0, 12).snapSafe = true;
  const std::string words = rf ? rf->words : std::string("Illegal block");
  refSay(words + ", receiving team." +
         (y < 10 ? std::string(" Half the distance to the goal, enforced from the end of the return.") : " " + bPenYards(y) + ", enforced from the end of the return.") +
         " First down.");
}

// ------------------------------------------------------------------ before the snap

void Booth::onPresnapEnter(const Game& g, const View& v) {
  presnapSaid_ = true;
  if (g.phase == GamePhase::Pat) {
    push(Who::PBP, Kind::Setup, say("2pt", facts(g, {})), 1, 3, 0, 10);
    return;
  }
  const PlaySim* s = sim_;
  const OffPlay* op = s ? s->cfg.offPlay : nullptr;
  if (op && op->type == PlayType::Kneel) {
    if (g.q == 2) {
      if (once("kneelhalf" + bS(g.q))) push(Who::PBP, Kind::Setup, say("kneelhalf", facts(g, {})), 0, 3, 0, 6);
    } else if (once("kneelgame")) {
      push(Who::PBP, Kind::Setup, say("kneelset", facts(g, {})), 0, 3, 0, 6);
    }
    return;
  }
  // a new quarterback (the starter hurt, or pulled): introduced the first time he lines up
  if (s && s->qb && s->qb->p) {
    const Player*& was = qbOnField_[g.poss == TeamKey::Home ? 0 : 1];
    const Player* now = s->qb->p;
    if (was && was != now && once("newqb" + bS(now->id))) {
      const Team& T = g.offense();
      push(Who::PBP, Kind::Setup,
           say(was->injuredWeeks > 0 ? "qb_new_hurt" : "qb_new", facts(g, {{"full", now->first + " " + now->last}, {"old", nm(was)}, {"team", T.city}, {"nick", nick(T)}})), 1,
           3, 0, 12);
      mentions_[now->id]++;  // (introduced: the next mention is the short name)
    }
    was = now;
  }
  const bool keyDown = g.down >= 3 || g.ballOn >= 80 || ((g.q == 2 || g.q >= 4) && g.clock <= 150) || g.toGo >= 15 || !setupPossKnown_ || g.poss != setupPoss_ ||
                       g.q != setupQ_;
  const bool afterTalk = now_ - lastEnd_ < 1.0 || cur_.id;
  const std::string key = bDdKey(g, g.down, g.toGo, g.ballOn);
  // the down and distance just said by the result or the flag: the setup would only say it again
  const bool ddSaid = key == ddSaidKey_ && g.down <= 3 && !(g.q >= 4 && g.clock < 300 && std::abs(g.margin()) <= 8);
  ddSaidKey_.clear();
  const bool late = (g.q == 2 || g.q >= 4) && g.clock <= 170;
  const std::string text = (!late && g.down != 4 && key == nextSetupKey_ && !nextSetup_.empty()) ? nextSetup_ : setupLine(g, true);
  nextSetup_.clear();
  (void)keyDown;
  (void)afterTalk;
  if (!ddSaid) push(Who::PBP, Kind::Setup, text, clutchSetup_ ? 2 : g.down >= 3 ? 1 : 0, 3, 0, 12);
  clutchSetup_ = false;
  setupPossKnown_ = true;
  setupPoss_ = g.poss;
  setupQ_ = g.q;
  (void)v;
}

std::string Booth::setupLine(const Game& g, bool full) {
  const Team& O = g.offense();
  const std::string dd = bDownDist(g.down, g.toGo, g.ballOn);
  const bool takeover = !setupPossKnown_ || g.poss != setupPoss_;
  const bool newQ = setupPossKnown_ && g.q != setupQ_;
  std::string where;
  const int yl = bRound(g.ballOn);
  if (g.ballOn + g.toGo >= 99.5 || yl >= 80) where = "from the " + g.defense().city + " " + bS(100 - yl);
  else if (yl == 50) where = "at midfield";
  else if (yl < 50) where = takeover || chance(0.5) ? "from their own " + bS(yl) : "at the " + O.city + " " + bS(yl);
  else where = "at the " + g.defense().city + " " + bS(100 - yl);
  std::string s;
  const bool late = (g.q == 2 || g.q >= 4) && g.clock <= 150;
  const bool close = std::abs(g.margin()) <= 8;
  const bool clutch = g.q >= 4 && g.clock < 300 && g.margin() < 0 && g.margin() >= -8 && g.down >= 3;
  const Slots ss = facts(g, {{"where", where}, {"togo", bDist(g.toGo, g.ballOn)}, {"qbn", sim_ && sim_->qb ? nm(sim_->qb->p) : std::string()},
                             {"upby", g.margin() > 0 ? bS(g.margin()) : std::string()}, {"downby", g.margin() < 0 ? bS(-g.margin()) : std::string()}});
  if (clutch && full) {
    const int m = -g.margin();
    const int tg = (std::max)(1, bRound(g.toGo));
    const std::string need = g.down == 4 ? (g.toGo >= 3 ? bS(tg) + " yards, or this one is over." : "They have to have it. If they don't get it, this one's over.")
                             : m <= 3 && g.ballOn >= 55 ? "A field goal wins it." : m <= 3 ? "They need to get in range for a field goal." : "They need a touchdown.";
    s = bCap(dd) + " " + where + ", " + bClock(g.clock) + " to go. " + O.city + " down " + bS(m) + ", " + bCountWord(g.timeouts[g.poss]) +
        (g.timeouts[g.poss] == 1 ? " timeout left. " : " timeouts left. ") + need;
    clutchSetup_ = true;
    return s;
  }
  if (g.down == 4 && full && sim_ && sim_->cfg.offPlay && sim_->cfg.offPlay->type != PlayType::Kneel) {
    s = say("set_4go", ss);
  } else if (takeover && g.down == 1) {
    s = say("set_take", ss);
    if (g.margin() != 0 && chance(0.35)) s = bJoin(s, say("set_sit", ss));
  } else if (newQ) {
    s = say("set_newq", ss);
  } else if (late) {
    s = say("set_late", ss);
    // the timeouts that matter: the trailing team's (the leader's clock is the defense's problem)
    const TeamKey trail = g.margin() <= 0 ? g.poss : other(g.poss);
    const int to = g.timeouts[trail];
    if (g.q >= 4 && close && chance(0.6))
      s = bJoin(s, say(trail == g.poss ? "set_to_own" : "set_to_other",
                       facts(g, {{"to", bCountWord(to)}, {"tos", to == 1 ? "timeout" : "timeouts"}, {"team", g.teamOf(trail).city}})));
  } else if (g.down == 1 && g.toGo >= 10 && yl < 80 && chance(0.35)) {
    s = say("set_1st", ss);
  } else {
    s = say("set", ss);
  }
  // the formation, now and then (joined to the down and distance: "2nd and 6 at the 41, Jessup in the shotgun.")
  if (full && sim_ && sim_->qb && chance(0.35)) {
    const Formation* f = sim_->cfg.offPlay ? formationById(sim_->cfg.offPlay->formation) : nullptr;
    int rbs = 0, tes = 0, wrs = 0;
    for (const Ent* e : sim_->off) {
      if (e->p->pos == Pos::RB || e->p->pos == Pos::FB) ++rbs;
      if (e->p->pos == Pos::TE) ++tes;
      if (e->p->pos == Pos::WR) ++wrs;
    }
    const char* fm = rbs == 0                                                        ? "fm_empty"
                     : tes >= 3 || (f && formationFamily(*f) == "Goal Line")         ? "fm_heavy"
                     : f && formationFamily(*f) == "Shotgun"                        ? "fm_gun"
                     : f && formationFamily(*f) == "Pistol"                         ? "fm_pistol"
                     : f && (formationFamily(*f) == "I-Form" || bHas(formationFamily(*f), "I")) ? "fm_i"
                     : wrs >= 4                                                     ? "fm_4wide"
                                                                                    : "fm_uc";
    s = bJoin(s, say(fm, facts(g, {{"qb", nm(sim_->qb->p)}, {"tes", bCountWord(tes)}})));
  }
  // the streak that makes this snap a story
  if (full && sim_ && sim_->qb && chance(0.3)) {
    auto it = qbStreak_.find(sim_->qb->p->id);
    if (it != qbStreak_.end() && it->second.completions >= 6 && cooled("streak", 200)) {
      mark("streak");
      s = bJoin(s, say("set_streak", facts(g, {{"qb", nm(sim_->qb->p)}, {"n", bS(it->second.completions)}})));
    }
  }
  return s;
}

std::optional<std::string> Booth::presnapRead(const Game& g, const PlaySim& s) {
  const DefCall* dc = s.cfg.defCall;
  const OffPlay* op = s.cfg.offPlay;
  if (!dc || !op || op->type == PlayType::Kneel || op->type == PlayType::Spike) return std::nullopt;
  readSubj_.clear();
  const Team& D = g.defense();
  const Team& O = g.offense();
  struct C {
    double sc;
    std::string topic, text;
  };
  std::vector<C> c;
  auto add = [&](double sc, const std::string& topic, const std::string& text, double cool = 300) {
    if (!text.empty() && cooled(topic, cool) && !spokenSet_.count(text)) c.push_back({sc + rng_.range(0, 0.3), topic, text});
  };
  const bool blitz = !dc->blitz.empty();
  const bool passDown = g.down == 3 && g.toGo >= 6;
  const std::string qbn = s.qb ? nm(s.qb->p) : std::string("the quarterback");
  // the facts a read can name: the down, the men where they line up
  const Ent* fsE = s.defRole("FS");
  const Ent* ssE = s.defRole("SS");
  const Slots rs = facts(g, {{"qb", qbn}, {"togo", bDist(g.toGo, g.ballOn)}, {"fs", fsE ? nm(fsE->p) : std::string()}, {"ss", ssE ? nm(ssE->p) : std::string()}});
  auto with = [&rs](Slots e) {
    e.insert(e.end(), rs.begin(), rs.end());
    return e;
  };
  // a man the defense will send, creeping down
  if (blitz && (s.shellAdj == PlaySim::ShellAdj::ShowBlitz || chance(0.5))) {
    const Ent* b = s.defRole(dc->blitz.front());
    if (b) add(1.2, "r_blitz", say("r_blitz", with({{"b", nm(b->p)}})));
  }
  if (!blitz && s.shellAdj == PlaySim::ShellAdj::ShowBlitz) add(1.1, "r_fake", say("r_fake", rs));
  if (bTwoHigh(dc->coverage) && passDown) add(0.9, "r_2high", say("r_2high", rs));
  if (dc->coverage == Coverage::C0) add(1.3, "r_c0", say("r_c0", rs));
  // a one-on-one matchup the alignment gives away
  if (bManCov(dc->coverage)) {
    for (const Ent* e : s.off)
      if (e->p->pos == Pos::WR && e->eligible) {
        for (const Ent* d : s.def)
          if (d->man == e && (d->p->pos == Pos::CB || d->p->pos == Pos::S)) {
            const PlayerLine& ln = const_cast<StatBook&>(g.stats).line(*e->p, O.abbr);
            if (ln.rec >= 3) {
              readSubj_["r_match" + bS(e->p->id)] = e->p->id;
              add(1.0, "r_match" + bS(e->p->id), say("r_match", with({{"wr", nm(e->p)}, {"cb", nm(d->p)}, {"n", bS(ln.rec)}, {"y", bS(ln.recYds)}})), 400);
            }
          }
        break;
      }
  }
  int rbs = 0, tes = 0;
  for (const Ent* e : s.off) {
    if (e->p->pos == Pos::RB || e->p->pos == Pos::FB) ++rbs;
    if (e->p->pos == Pos::TE) ++tes;
  }
  if (rbs == 0) add(0.9, "r_empty", say("r_empty", rs));
  if (tes >= 3 && g.toGo <= 3) add(1.0, "r_heavy", say("r_heavy", rs));
  const bool risky4 = g.down == 4 && g.toGo >= 3 && g.ballOn < 50 && tier(g) == 0 && !(g.q >= 4 && g.margin() < 0);
  if (risky4) {
    add(2.2, "r_4th", say("r_4thno", rs), 0);
    risky4th_ = true;
  } else if (g.down == 4)
    add(2.0, "r_4th",
        say(tier(g) >= 2 || g.margin() < -16 ? "r_4thdone" : g.margin() <= -9 && g.q >= 4 ? "r_4thmust" : tier(g) >= 1 ? "r_4thdone" : g.ballOn >= 50 ? "r_4thgo" : "r_4thmid",
            with({{"down", bS(-g.margin())}})),
        0);
  if (g.ballOn >= 90) add(0.8, "r_goal", say("r_goal", rs));
  if (passDown && D.blitzLean > 0.33) add(0.8, "r_tend", say("r_tend", rs), 500);
  if (passDown && O.passLean > 0.6) {
    // the offense's own identity, named (a bare "they spread you out" sounded like the other team's scheme)
    const bool spread = bHas(O.offScheme, "Air Raid") || bHas(O.offScheme, "Spread") || bHas(O.offScheme, "Run and Shoot");
    add(0.6, "r_tend2" + O.abbr, say(spread ? "r_tendspread" : "r_tendpass", with({{"scheme", O.offScheme}})), 900);
  }
  // the shell: what the safeties tell you
  if (dc->coverage != Coverage::C0) {
    if (bTwoHigh(dc->coverage)) add(0.65, "r_shell2", say("r_shell2", rs), 240);
    else add(0.65, "r_shell1", say("r_shell1", rs), 240);
  }
  // the men: who is where (names make every read its own sentence)
  {
    const Ent* fs = s.defRole("FS");
    const Ent* ss = s.defRole("SS");
    if (!bTwoHigh(dc->coverage) && dc->coverage != Coverage::C0 && fs) add(0.7, "r_fs", say("r_fs", with({{"fs", nm(fs->p)}})), 600);
    if (bTwoHigh(dc->coverage) && fs && ss) add(0.7, "r_ss", say("r_ss", with({{"fs", nm(fs->p)}, {"ss", nm(ss->p)}})), 600);
    for (const Ent* d : s.def)
      if (d->man && d->man->eligible && (d->p->pos == Pos::CB) && (s.shellAdj == PlaySim::ShellAdj::Press || bManCov(dc->coverage))) {
        readSubj_["r_press"] = d->man->p->id;
        add(0.75, "r_press", say("r_press", with({{"cb", nm(d->p)}, {"wr", nm(d->man->p)}})), 600);
        break;
      }
    const Ent* slot = s.offRole("SLOT");
    const Ent* nb = s.defRole("NB");
    if (slot && nb && passDown) readSubj_["r_slot"] = slot->p->id;
    if (slot && nb && passDown) add(0.7, "r_slot", say("r_slot", with({{"slot", nm(slot->p)}, {"nb", nm(nb->p)}})), 600);
    const Ent* rb = s.offRole("RB");
    if (rb && passDown && g.toGo >= 7) add(0.65, "r_rbpro", say("r_rbpro", with({{"rb", nm(rb->p)}})), 1e9);
  }
  // personnel
  int wrs = 0;
  for (const Ent* e : s.off) wrs += e->p->pos == Pos::WR ? 1 : 0;
  if (wrs >= 4) add(0.6, "r_4wide", say("r_4wide", rs), 400);
  if (tes >= 2 && g.toGo >= 5) add(0.6, "r_2te", say("r_2te", rs), 400);
  // the situation
  if (g.down == 3 && g.toGo <= 2) add(0.8, "r_3short", say("r_3short", rs), 300);
  if (g.down == 2 && g.toGo >= 8) add(0.6, "r_2long", say("r_2long", rs), 400);
  if (g.ballOn >= 80 && g.ballOn < 90) add(0.7, "r_rz", say("r_rz", rs), 400);
  // the offense's habits so far
  {
    const TeamLine& tl = g.team[g.poss];
    if (tl.plays >= 12) {
      const int runs = tl.rushAtt, passes = tl.passAtt;
      const Slots hs = with({{"runs", bS(runs)}, {"passes", bS(passes)}, {"runst", bTimes(runs)}, {"passest", bTimes(passes)}});
      if (runs >= passes * 1.5) add(0.75, "r_runtend", say(passes == 0 ? "r_runtend0" : "r_runtend", hs), 900);
      else if (passes >= runs * 2) add(0.75, "r_passtend", say(runs == 0 ? "r_passtend0" : "r_passtend", hs), 900);
    }
  }
  if (c.empty()) return std::nullopt;
  std::sort(c.begin(), c.end(), [](const C& a, const C& b) { return a.sc > b.sc; });
  // a read on every snap is a formula: the routine looks (the shell, a man deep) about one snap in two, the ones that
  // matter (a blitz coming, Cover Zero, fourth down, a matchup) every time
  if (c[0].sc < 0.95 && chance(0.5)) return std::nullopt;
  mark(c[0].topic);
  readTopic_ = c[0].topic;
  if (auto it = readSubj_.find(c[0].topic); it != readSubj_.end()) readTopic_ += "#" + bS(it->second);
  return c[0].text;
}

bool Booth::qbSharp(const Game& g, const Player* qb) const {
  if (!qb) return false;
  for (const Team* t : {g.home, g.away})
    for (const PlayerLine* ln : g.stats.forTeam(t->abbr))
      if (ln->pid == qb->id) return ln->passAtt >= 6 && ln->passComp * 10 >= ln->passAtt * 6 && ln->passInt == 0;
  return false;
}

bool Booth::blitzOn(const PlaySim& s) const { return s.cfg.defCall && !s.cfg.defCall->blitz.empty(); }

// ------------------------------------------------------------------ between the plays

// a written exchange still about this game (same score, quarter and possession, within four plays): queued now
bool Booth::airWritten(const Game& g) {
  while (!writerReady_.empty() && writerReady_.front().kind == "exchange") {
    WriterReady r = writerReady_.front();
    writerReady_.pop_front();
    if (r.q != g.q || scrimNo_ != r.playNo) {
      ++writerStale_;
      continue;
    }
    lastStoryT_ = now_;
    for (size_t i = 0; i < r.lines.size(); ++i) {
      Line& l = push(r.lines[i].first, Kind::Story, r.lines[i].second, 1, 3, 0, 20);
      l.tag = i == 0 ? "question" : "answer";  // (the reaction rides with the answer: never cut in between)
    }
    return true;
  }
  return false;
}

void Booth::idle(const Game& g, const View& v) {
  // only into a real silence: nothing queued, nobody talking for a beat
  if (cur_.id || !q_.empty()) return;
  if (now_ - lastEnd_ < (v.userGame ? 2.0 : 1.2)) return;
  if (stage_ == View::Stage::Presnap && !(v.userGame || v.canHold)) return;
  if (stage_ != View::Stage::Live && stage_ != View::Stage::Kick && airWritten(g)) return;
  if (g.phase != GamePhase::Scrimmage) return;
  // a written exchange takes the first quiet moment (behind the 22-second story spacing, two in three went stale waiting)
  const bool written = !writerReady_.empty() && writerReady_.front().kind == "exchange";
  if (now_ - lastStoryT_ < (written ? 8 : 22)) return;
  if (airWritten(g)) return;
  if (writer_ && writerBusy_ && now_ - lastWriterAsk_ < 40) return;  // (an exchange is on its way: the templates wait for it)
  Who who = Who::Color;
  if (auto ex = exchange(g)) {
    lastStoryT_ = now_;
    push(Who::PBP, Kind::Story, ex->first, 1, 3, 0, 10).tag = "question";
    push(Who::Color, Kind::Story, ex->second, 1, 3, 0, 15).tag = "answer";
    return;
  }
  if (g.q >= 4 && g.clock < 300 && std::abs(g.margin()) <= 8 && g.down >= 3) return;  // (the deciding snaps: nothing in the way)
  // a game that is decided: talk of what it is now (the clock, the backups, a long day), now and then
  if (g.q >= 4 && tier(g) >= 2 && g.score.home != g.score.away && cooled("garbage", 240)) {
    mark("garbage");
    lastStoryT_ = now_;
    const TeamKey lk = g.score.home > g.score.away ? TeamKey::Home : TeamKey::Away;
    const Team& L = g.teamOf(lk);
    const Team& T = g.teamOf(other(lk));
    const bool pbp = chance(0.5);
    push(pbp ? Who::PBP : Who::Color, Kind::Story, say(pbp ? "s_garbage_pbp" : "s_garbage", facts(g, {{"lteam", L.city}, {"tteam", T.city}, {"lnick", nick(L)}, {"tnick", nick(T)}})), 1, 2, 0, 10);
    return;
  }
  // late in a half or a decided fourth quarter: the clock and what each side needs, not stat lines
  if (((g.q == 2 || g.q >= 4) && g.clock < 120) || (g.q >= 4 && tier(g) >= 1)) return;
  if (auto s = storyLine(g, who)) {
    lastStoryT_ = now_;
    push(who, Kind::Story, *s, 1, 2, 0, 10).topic = lastStoryTopic_;
    if (!storyReply_.empty()) {
      push(Who::Color, Kind::Story, storyReply_, 1, 1, 0, 12);
      storyReply_.clear();
    }
  }
}

std::optional<std::string> Booth::storyLine(const Game& g, Who& who) {
  struct C {
    double sc;
    std::string topic, text;
    Who who;
  };
  std::vector<C> c;
  auto add = [&](double sc, const std::string& topic, const std::string& text, Who w, double cool) {
    if (text.empty() || !cooled(topic, cool) || spokenSet_.count(text)) return;
    // a sentence an earlier game heard word for word (the weather, the building) waits behind the others
    if (memory_ && Recent::get().age("booth.story", bTextId(text)) >= 0) sc -= 0.35;
    c.push_back({sc + rng_.range(0, 0.4), topic, text, w});
  };
  const Team& O = g.offense();
  const Team& D = g.defense();
  const TeamKey ok = g.poss, dk = other(g.poss);
  const TeamLine& to = g.team[ok];
  const TeamLine& td = g.team[dk];
  const Slots base = facts(g, {});
  auto with = [&base](Slots e) {
    e.insert(e.end(), base.begin(), base.end());
    return e;
  };
  // the quarterback's day
  for (const PlayerLine* ln : g.stats.forTeam(O.abbr)) {
    if (ln->passAtt >= 8) {
      const std::string tds = ln->passTD ? std::string(bCountWord(ln->passTD)) + (ln->passTD == 1 ? " touchdown" : " touchdowns") : std::string();
      const std::string ints = ln->passInt ? std::string(bCountWord(ln->passInt)) + (ln->passInt == 1 ? " interception" : " interceptions") : std::string();
      const Slots qs = with({{"name", lineName(g, *ln)}, {"c", bS(ln->passComp)}, {"a", bS(ln->passAtt)}, {"y", bS(ln->passYds)}, {"tds", tds}, {"ints", ints},
                             {"short", ln->passComp > 0 && ln->passYds / static_cast<double>(ln->passComp) < 6.5 ? "a lot of short stuff" : ""},
                             {"clean", ints.empty() ? "no interceptions" : ""}});
      add(0.9, "qb:" + bS(ln->pid), say(tds.empty() && ints.empty() ? "s_qb" : !tds.empty() && !ints.empty() ? "s_qbti" : !tds.empty() ? "s_qbt" : "s_qbi", qs), Who::PBP, 360);
    }
    if (ln->rushAtt >= 8) {
      const double ypc = ln->rushYds / static_cast<double>((std::max)(1, ln->rushAtt));
      const bool chasing = g.margin() <= -9 && g.q >= 3;  // down two scores late, the run is not the plan any more
      const Slots rs = with({{"name", lineName(g, *ln)}, {"y", bS(ln->rushYds)}, {"n", bS(ln->rushAtt)}});
      const std::string tail = say(ypc >= 5.0 ? (chasing ? "rb_hot2" : "rb_hot") : ypc < 3.2 ? "rb_cold" : "rb_mid", rs);
      add(0.8, "rb:" + bS(ln->pid), bJoin(say("s_rb", rs), tail), Who::Color, 900);
    }
    if (ln->rec >= 4 && !(lastBig_ == ln->pid)) {
      const double ypr = ln->recYds / static_cast<double>(ln->rec);
      int most = 0;
      for (const PlayerLine* o : g.stats.forTeam(O.abbr)) most = (std::max)(most, o->rec);
      if (ln->rec < most) continue;
      const Slots ws = with({{"name", lineName(g, *ln)}, {"n", bS(ln->rec)}, {"y", bS(ln->recYds)}, {"team", O.city}});
      add(0.7, "wr:" + bS(ln->pid), say(ypr >= 9 ? "s_wrgo" : "s_wrshort", ws), ypr >= 9 ? Who::PBP : Who::Color, 420);
    }
  }
  for (const PlayerLine* ln : g.stats.forTeam(O.abbr))
    if (ln->passInt >= 2)
      add(0.95, "qbint:" + bS(ln->pid) + ":" + bS(ln->passInt),
          say(g.margin() >= 0 ? "s_qbint_ahead" : "s_qbint_behind", with({{"name", lineName(g, *ln)}, {"n", bCountWord(ln->passInt)}, {"team", O.city}})), Who::Color,
          1e9);
  for (const PlayerLine* ln : g.stats.forTeam(D.abbr)) {
    if (ln->sacks >= 2) {
      const Slots ks = with({{"name", lineName(g, *ln)}, {"n", bCountWord(ln->sacks)}, {"team", D.city}});
      add(1.0, "sk:" + bS(ln->pid) + ":" + bS(ln->sacks), say(once("sk1:" + bS(ln->pid)) ? "s_sacks1" : "s_sacks2", ks), Who::Color, 1e9);
    }
    if (ln->ints >= 1 && ln->tackles >= 3)
      add(0.6, "dbint:" + bS(ln->pid), say("s_dbint", with({{"name", lineName(g, *ln)}, {"tk", bS(ln->tackles)}, {"team", D.city}})), Who::PBP, 600);
  }
  // the teams
  if (to.thirdAtt >= 4 && (tier(g) == 0 || g.margin() < 0)) {
    const Slots ts = with({{"team", O.city}, {"c", bS(to.thirdConv)}, {"a", bS(to.thirdAtt)}});
    add(0.8, "3rd:" + O.abbr,
        bJoin(say("s_3rd", ts), say(to.thirdConv * 2 >= to.thirdAtt ? "3rd_good" : g.margin() > 0 ? "3rd_badahead" : "3rd_bad", ts)), Who::PBP, 1e9);
  }
  if (to.plays + td.plays >= 30 && std::abs(to.yards - td.yards) >= 90)
    add(0.7, "yds", say("s_yds", with({{"team", to.yards > td.yards ? O.city : D.city}, {"big", bS((std::max)(to.yards, td.yards))}, {"small", bS((std::min)(to.yards, td.yards))}})),
        Who::Color, 700);
  if (to.penalties + td.penalties >= 6) add(0.6, "pens", say("s_pens", with({{"n", bS(to.penalties + td.penalties)}})), Who::PBP, 900);
  const int toG = giveaways(g, to), tdG = giveaways(g, td);
  if (tdG >= 2 || toG >= 2) {
    const bool offBad = toG >= tdG;
    const int badMargin = offBad ? g.margin() : -g.margin();
    const int n = offBad ? toG : tdG;
    add(0.8, "tos", say(badMargin > 0 ? "s_tos_ahead" : "s_tos_behind", with({{"team", offBad ? O.city : D.city}, {"times", n == 2 ? std::string("twice") : std::string(bCountWord(n)) + " times"}})),
        Who::Color, 900);
  }
  // the weather and the building
  const Weather& w = g.weather;
  const Slots wx = with({{"temp", bS(bRound(w.tempF))}});
  if (w.precip == Precip::Snow) add(0.5, "wx", say("s_snow", wx), Who::Color, 900);
  if (w.precip == Precip::Rain) add(0.5, "wx", say("s_rain", wx), Who::Color, 900);
  if (w.tempF <= 30) add(0.4, "wx", say("s_cold", wx), Who::PBP, 1200);
  if (g.q == 4 && std::abs(g.margin()) <= 7 && g.clock < 480 && tier(g) == 0) add(1.0, "tight4", say("s_tight", base), Who::PBP, 400);
  if (g.q >= 3 && std::abs(g.margin()) >= 17) add(0.5, "blowout", say("s_blowout", with({{"team", D.city}})), Who::Color, 900);
  if (c.empty()) return std::nullopt;
  std::sort(c.begin(), c.end(), [](const C& a, const C& b) { return a.sc > b.sc; });
  mark(c[0].topic);
  lastStoryTopic_ = c[0].topic;
  who = c[0].who;
  // a quarterback's line from the play-by-play man: the analyst says what it means
  storyReply_.clear();
  if (c[0].topic.rfind("qb:", 0) == 0) {
    const int pid = std::atoi(c[0].topic.c_str() + 3);
    for (const PlayerLine* ln : g.stats.forTeam(O.abbr))
      if (ln->pid == pid && ln->passAtt > 0) {
        const double pct = ln->passComp / static_cast<double>(ln->passAtt), ypa = ln->passYds / static_cast<double>(ln->passAtt);
        const Slots qs = with({{"name", lineName(g, *ln)}, {"n", bCap(bCountWord(ln->passInt))}});
        if (ln->passInt >= 2) storyReply_ = say("qbr_int", qs);
        else if (pct >= 0.7 && ln->passInt == 0) storyReply_ = say("qbr_hot", qs);
        else if (pct < 0.5) storyReply_ = say("qbr_cold", qs);
        else if (ypa < 5.5 && pct >= 0.6) storyReply_ = say("qbr_short", qs);
      }
    if (chance(0.4)) storyReply_.clear();  // not every time
  }
  return c[0].text;
}

std::optional<std::pair<std::string, std::string>> Booth::exchange(const Game& g) {
  if (g.shouldKneel()) return std::nullopt;  // (they're kneeling it out: no talk of what the next down needs)
  const Team& O = g.offense();
  const Team& D = g.defense();
  const TeamLine& to = g.team[g.poss];
  const double ypc = to.rushYds / static_cast<double>((std::max)(1, to.rushAtt));
  const Slots xs = facts(g, {{"ypc", bS(static_cast<int>(ypc * 10) / 10)}, {"ry", bS(to.rushYds)}, {"ra", bS(to.rushAtt)}, {"team", O.city}, {"dteam", D.city},
                             {"to", bCountWord(g.timeouts[g.poss])}, {"togo", bDist(g.toGo, g.ballOn)}});
  // third and short: run it or throw it
  if (g.down == 3 && g.toGo <= 2 && tier(g) == 0 && cooled("x_3short", 400) && chance(0.6)) {
    mark("x_3short");
    return std::make_pair(say("x3sq", xs), say(ypc >= 4.2 ? "x3sa" : "x3sb", xs));
  }
  // the red zone
  if (g.ballOn >= 80 && g.down == 1 && tier(g) == 0 && cooled("x_rz", 600) && chance(0.5)) {
    mark("x_rz");
    return std::make_pair(say("xrzq", xs), say(D.blitzLean > 0.32 ? "xrza_blitz" : "xrza_zone", xs));
  }
  // third and long coming up: what does the analyst want to see
  if (g.down == 3 && g.toGo >= 7 && tier(g) == 0 && cooled("x_3long", 600) && chance(0.4)) {
    mark("x_3long");
    return std::make_pair(say("x3q", xs), say(D.blitzLean > 0.3 ? "x3a_blitz" : "x3a_zone", xs));
  }
  if (to.plays >= 20 && to.rushAtt >= 10 && once("x_run" + O.abbr) && chance(0.6)) {
    const bool behind = g.margin() <= -14;
    return std::make_pair(say("x_runq", xs), say(ypc >= 4.5 && !behind ? "x_runa_good" : behind ? "x_runa_behind" : "x_runa_bad", xs));
  }
  if (g.q == 4 && g.clock < 360 && std::abs(g.margin()) <= 8 && g.margin() < 0 && g.phase == GamePhase::Scrimmage && g.down == 1 && cooled("x_late", 900)) {
    mark("x_late");
    return std::make_pair(say("x_lateq", xs), say("x_latea", xs));
  }
  return std::nullopt;
}

std::optional<std::string> Booth::quarterStory(const Game& g) {
  const TeamLine& h = g.team.home;
  const TeamLine& a = g.team.away;
  const Team& H = *g.home;
  const Team& A = *g.away;
  const int m = g.score.home - g.score.away;
  // (the quarter that just ended: the Game is already in the next one)
  const int qEnded = (std::max)(1, g.q - 1);
  if (std::abs(h.yards - a.yards) >= 60 && once("qcontrol"))
    return say("qstory_yds", facts(g, {{"team", h.yards > a.yards ? H.city : A.city}, {"big", bS((std::max)(h.yards, a.yards))}, {"small", bS((std::min)(h.yards, a.yards))},
                                       {"qn", bQuarterName(qEnded)}}));
  if (giveaways(g, h) + giveaways(g, a) >= 2)
    return say("qstory_to", facts(g, {{"home", H.city}, {"away", A.city}, {"hto", bS(giveaways(g, h))}, {"ato", bS(giveaways(g, a))}}));
  if (std::abs(m) >= 9)
    return say("qstory_lead", facts(g, {{"team", m > 0 ? H.city : A.city}, {"lead", std::abs(m) >= 17 ? "three-score" : "two-score"}}));
  return say("qstory_even", facts(g, {{"qn", bQuarterName(qEnded)}}));
}

std::optional<std::string> Booth::halftimeStory(const Game& g) {
  const TeamLine& h = g.team.home;
  const TeamLine& a = g.team.away;
  const bool homeLead = g.score.home >= g.score.away;
  const Team& L = homeLead ? *g.home : *g.away;
  const TeamLine& lt = homeLead ? h : a;
  const TeamLine& tt = homeLead ? a : h;
  const int m = std::abs(g.score.home - g.score.away);
  // the half in a sentence: the leader moved it at will, was out-gained, finished his chances, or an even half
  const char* how = lt.yards > tt.yards + 50 ? "half_will" : tt.yards > lt.yards + 50 ? "half_outgained" : m >= 9 ? "half_finish" : "half_even";
  std::string s = say(how, facts(g, {{"team", L.city}, {"yds", bS(lt.yards)}})) + " ";
  // the open's key, revisited
  if (openKey_) {
    const PlayerLine* kl = nullptr;
    for (const Team* t : {g.home, g.away})
      for (const PlayerLine* ln : g.stats.forTeam(t->abbr))
        if (ln->pid == openKey_->id) kl = ln;
    const PlayerLine* lead = nullptr;
    for (const Team* tm : {g.home, g.away})
      for (const PlayerLine* ln : g.stats.forTeam(tm->abbr))
        if (ln->sacks >= 2 && (!lead || ln->sacks > lead->sacks)) lead = ln;
    const Slots ks = facts(g, {{"key", nm(openKey_)}, {"keyfull", openKey_->first + " " + openKey_->last}, {"who", lead ? leadName(g, *lead) : std::string()},
                               {"n", lead && lead->pid != openKey_->id ? bS(lead->sacks) : kl ? bS(kl->sacks) : std::string()}, {"tk", kl ? bS(kl->tackles) : std::string()}});
    if (lead && lead->pid != openKey_->id && (!kl || lead->sacks > kl->sacks)) s += say("hs_keywrong", ks) + " ";
    else if (kl && kl->sacks >= 2) s += say("hs_keyright", ks) + " ";
    else if (kl && kl->sacks == 1) s += say("hs_key1", ks) + " ";
    else if (kl && kl->tackles >= 4) s += say("hs_keytk", ks) + " ";
    else s += say("hs_keyquiet", ks) + " ";
  }
  if (giveaways(g, lt) + giveaways(g, tt) > 0)
    s += say("hs_to", facts(g, {{"team", giveaways(g, tt) > giveaways(g, lt) ? L.city : std::string()}, {"won", bS(giveaways(g, tt))}, {"lost", bS(giveaways(g, lt))}})) + " ";
  s += say("half2", facts(g, {}));
  return s;
}

void Booth::onFinal(const Game& g) {
  q_.clear();
  const bool homeWin = g.score.home > g.score.away;
  const Team& W = homeWin ? *g.home : *g.away;
  const Team& L = homeWin ? *g.away : *g.home;
  const int ws = (std::max)(g.score.home, g.score.away), ls = (std::min)(g.score.home, g.score.away);
  const Slots fs = facts(g, {{"wname", W.name}, {"lname", L.name}, {"wcity", W.city}, {"lcity", L.city}, {"ws", bS(ws)}, {"ls", bS(ls)}, {"hcity", g.home->city},
                             {"pbp", crew_.pbpFirst + " " + crew_.pbpLast}, {"col", crew_.colorFirst + " " + crew_.colorLast},
                             {"tod", view_.night ? "evening" : "afternoon"}});
  push(Who::PBP, Kind::Close, say(ws == ls ? "final_tie" : "final", fs), 2, 5, 0.6, 60).snapSafe = true;
  pendingInjury_.clear();  // (the game is over: a man hurt is for the postgame, not the final call)
  // the plays that decided it
  if (!bigPlays_.empty()) {
    std::string r = say("close_big", fs) + " ";
    for (size_t i = 0; i < bigPlays_.size() && i < 3; ++i) r += bigPlays_[i] + (i + 1 < (std::min)(bigPlays_.size(), static_cast<size_t>(3)) ? " " : "");
    push(Who::PBP, Kind::Close, r, 1, 4, 0, 60).snapSafe = true;
  }
  // the player of the game: the winner's best day, said by what made it (passing, running, catching, the pass rush)
  const PlayerLine* star = nullptr;
  int bestV = 0;
  std::string line;
  for (const PlayerLine* ln : g.stats.forTeam(W.abbr)) {
    struct Opt {
      int v;
      std::string s;
    };
    const std::string tds = [](int n) { return n == 1 ? std::string(" and a touchdown") : n > 1 ? " and " + bS(n) + " touchdowns" : std::string(); }(ln->passTD + ln->rushTD + ln->recTD);
    const Opt opts[] = {
        {ln->passAtt >= 10 ? ln->passYds / 2 + 25 * ln->passTD - 20 * ln->passInt : 0,
         bS(ln->passComp) + " of " + bS(ln->passAtt) + " for " + bS(ln->passYds) + " yards" + (ln->passTD ? tds : std::string())},
        {ln->rushAtt >= 8 ? ln->rushYds + 25 * ln->rushTD : 0, bS(ln->rushYds) + " yards on the ground" + (ln->rushTD ? tds : std::string())},
        {ln->rec >= 3 ? ln->recYds + 25 * ln->recTD : 0, bS(ln->rec) + " catches for " + bS(ln->recYds) + " yards" + (ln->recTD ? tds : std::string())},
        {ln->sacks >= 2 ? 45 * ln->sacks : 0, bS(ln->sacks) + " sacks, and the quarterback never got comfortable"},
        {ln->ints >= 1 ? 60 * ln->ints : 0, ln->ints == 1 ? std::string("the interception, and it changed the game") : bS(ln->ints) + " interceptions"},
    };
    for (const Opt& o : opts)
      if (o.v > bestV) {
        bestV = o.v;
        star = ln;
        line = o.s;
      }
  }
  if (star) {
    if (!line.empty())
    {
      std::string full = lastName(star->name);
      for (const auto& p : W.roster)
        if (p->id == star->pid) full = p->first + " " + p->last;
      Slots ss = fs;
      ss.insert(ss.begin(), {{"star", full}, {"line", line}});
      push(Who::Color, Kind::Close, say("close_star", ss), 1, 4, 0, 60).snapSafe = true;
    }
  }
  push(Who::PBP, Kind::Close, say("signoff", fs), 0, 4, 0, 60).snapSafe = true;
}

std::string Booth::lastName(const std::string& full) {
  // StatBook names are "F. Last"
  const size_t d = full.find('.');
  std::string s = d == std::string::npos ? full : full.substr(d + 1);
  while (!s.empty() && s[0] == ' ') s.erase(0, 1);
  return s;
}

std::string Booth::leadName(const Game& g, const PlayerLine& l) {
  for (const Team* t : {g.home, g.away})
    for (const auto& p : t->roster)
      if (p->id == l.pid) return p->first + " " + p->last;
  return lastName(l.name);
}

std::string Booth::lineName(const Game& g, const PlayerLine& l) {
  for (const Team* t : {g.home, g.away})
    for (const auto& p : t->roster)
      if (p->id == l.pid) return nm(p.get());
  return lastName(l.name);
}

}  // namespace booth
}  // namespace tag
