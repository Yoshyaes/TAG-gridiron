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

}  // namespace

// ------------------------------------------------------------------ construction, queue

Booth::Booth(uint32_t seed, const Crew& crew) : crew_(crew), rng_(seed) {}

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
  {
    std::string& x = l.text;
    for (const char* a : {" 1 yards", " -1 yards"})
      for (size_t at = x.find(a); at != std::string::npos; at = x.find(a, at + 1)) x.replace(at, std::strlen(a), std::string(a).substr(0, std::strlen(a) - 1));
    static const char* const kAn[] = {"8-", "11-", "18-", "80-", "81-", "82-", "83-", "84-", "85-", "86-", "87-", "88-", "89-", "8 ", "11 ", "18 "};
    for (const char* n : kAn)
      for (const char* art : {"A ", " a "}) {
        const std::string from = std::string(art) + n;
        std::string to = from;
        to.replace(to.find(art[0] == 'A' ? "A" : "a"), 1, art[0] == 'A' ? "An" : "an");
        for (size_t at = x.find(from); at != std::string::npos; at = x.find(from, at + to.size())) x.replace(at, from.size(), to);
      }
    l.say = spoken(x);
  }
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
  // everything else strictly in order: a conversation (the toss to the analyst, his answer) never runs backwards
  if (best < 0)
    for (size_t i = 0; i < q_.size(); ++i)
      if (!q_[i].live) {
        if (q_[i].notBefore <= now_) best = static_cast<int>(i);
        break;
      }
  if (best < 0) return nullptr;
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
  // a live call takes the floor from anything less urgent, except the score, a break or the open/close
  if (cur_.kind == Kind::Score || cur_.kind == Kind::Open || cur_.kind == Kind::Close) return false;
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

bool Booth::wantsHold() const {
  if (stage_ != View::Stage::Presnap && stage_ != View::Stage::Wait && stage_ != View::Stage::Call) return false;
  // the open, a quarter break, halftime: always worth the wait (the kickoff waits for the crew); the breakdown of a
  // play only when the presentation lets the booth hold the snap
  auto big = [](const Line& l) { return l.kind == Kind::Open || l.kind == Kind::Break || l.kind == Kind::Close; };
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
         "And the kickoff is away.", "And the punt is away.", "Here's the free kick."});
  at(2, {"Caught!", "Got it!", "Makes the grab!", "Hauls it in!", "And he's got it!", "Makes the catch in traffic!", "Holds on with a man draped all over him!",
         "Snags it!", "Caught on the sideline!", "Makes the grab at the sideline!", "Caught over the middle!", "Got it, over the middle!", "Dropped!", "Drops it!", "Right through his hands!", "Can't hang on!", "Broken up!", "Knocked away!", "Batted down!", "Batted at the line!",
         "Knocked down at the line of scrimmage!", "Pressure!", "Here comes the rush!", "He's got company!", "Pocket's collapsing!", "Spin move!",
         "Makes a man miss!", "Runs him over!", "Stiff arm!", "Hurdles a man!", "He takes off!", "The snap, the hold, the kick...", "It's up..."});
  at(3, {"Diving catch!", "Lays out, and makes the grab!", "What a catch!", "He's got room!", "He's loose!", "Breaks free!", "Nobody's going to catch him!",
         "Oh, big hit!", "What a shot!", "Lowers the boom!", "Fumble! Ball's loose!", "He coughs it up! Loose ball!", "The ball is out!", "Midfield!",
         "Touchdown!", "He's in! Touchdown!", "He's going to score!", "Into the end zone!", "Intercepted!", "Sacked!", "Safety!", "He's in the clear!"});
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

// ------------------------------------------------------------------ phrase choice

std::string Booth::pick(const std::string& bank, const std::vector<std::string>& opts) {
  if (opts.empty()) return std::string();
  std::deque<int>& rec = recentPick_[bank];
  const size_t n = opts.size();
  const size_t keep = n <= 1 ? 0 : (std::min)(n - 1, (std::max)(static_cast<size_t>(1), n * 2 / 3));
  auto inRec = [&rec](int i) { return std::find(rec.begin(), rec.end(), i) != rec.end(); };
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
  const int i = ok[rng_.intRange(0, static_cast<int>(ok.size()) - 1)];
  rec.push_back(i);
  while (rec.size() > keep) rec.pop_front();
  return opts[i];
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
  if (shared_.count(p->last)) {
    // a surname two men share: the full name when the other one is on the field, or when "Calloway" last meant him
    // (what a surname last meant is set when a line is spoken, not when one is written: the plan writes every man's calls)
    auto it = lastBySurname_.find(p->last);
    if (it == lastBySurname_.end() || it->second != p->id) return p->first + " " + p->last;
    if (sim_) {
      // a teammate namesake on the field (the one on the other side of the ball has a different job in the call)
      const Ent* me = nullptr;
      for (const Ent* e : sim_->ents)
        if (e->p == p) me = e;
      for (const Ent* e : sim_->ents)
        if (e->p != p && e->p->last == p->last && (!me || e->side == me->side)) return p->first + " " + p->last;
    }
  }
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
      if (auto r = presnapRead(g, *sim_)) push(Who::Color, Kind::Read, *r, 1, 2, 0.1, 8).tag = readTopic_;
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
  push(Who::PBP, Kind::Open,
       pick("open1", {"Good " + tod + ", everybody, and welcome to " + H.city + ", where the " + H.name + " host the " + A.city + " " + A.name + ".",
                      "Hello, everybody. It's a great " + tod + " for football here in " + H.city + ", the " + A.name + " in town to take on the " + H.name + ".",
                      "From " + H.city + ", good " + tod + ". The " + A.name + " and the " + H.name + ", and we are just about ready to go."}),
       1, 4, 0.4, 60)
      .snapSafe = true;
  push(Who::PBP, Kind::Open,
       pick("open2", {pbp + " here, alongside " + col + ". " + crew_.colorFirst + ", what's the key today?",
                      "I'm " + pbp + ", with " + col + " in the booth. " + crew_.colorFirst + ", where does this one get decided?"}),
       1, 4, 0, 60)
      .snapSafe = true;
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
    key = pick("openkey",
               {"For me it's " + A.city + "'s " + A.offScheme + " offense against that " + H.defScheme + " look. " +
                    (aq ? nm(aq) + " has to be patient. " : std::string()) + "And keep an eye on " + (hRush ? hRush->first + " " + hRush->last : std::string("the " + nick(H) + " front")) +
                    ". He can wreck a game plan.",
                "It starts up front. " + (hRush ? hRush->first + " " + hRush->last : std::string("That defensive front")) + " has been a problem for everybody, and " +
                    (aq ? nm(aq) : std::string("the quarterback")) + " is going to have to handle him. Get the ball out quick, and stay ahead of the chains."});
  else
    key = "It's going to come down to the quarterbacks. " + (hq ? nm(hq) : std::string("One")) + " and " + (aq ? nm(aq) : std::string("the other")) + ", who makes the fewest mistakes.";
  push(Who::Color, Kind::Open, key, 1, 4, 0, 60).snapSafe = true;
  // the weather, when it is part of the story
  const Weather& w = g.weather;
  const int mph = bRound(std::hypot(w.windX, w.windY) * 2.045);
  std::string wx;
  if (w.precip == Precip::Snow) wx = "And it is snowing here in " + H.city + ". " + bS(bRound(w.tempF)) + " degrees, footing is going to be an issue.";
  else if (w.precip == Precip::Rain) wx = "We've got rain coming down, " + bS(bRound(w.tempF)) + " degrees. Ball security is going to be at a premium.";
  else if (w.tempF <= 32) wx = "It is cold. " + bS(bRound(w.tempF)) + " degrees at kickoff" + (mph >= 10 ? ", wind at " + bS(mph) + " miles an hour." : ".");
  else if (mph >= 14) wx = "The wind is going to be a factor, gusting around " + bS(mph) + " miles an hour. Watch the kicking game.";
  if (!wx.empty()) push(Who::PBP, Kind::Open, wx, 1, 3, 0, 60).snapSafe = true;
}

// ------------------------------------------------------------------ the log (Game's own lines)

void Booth::onLog(const Game& g, const PlayLog& l, size_t index) {
  const std::string& d = l.desc;
  (void)index;
  if (bHas(d, "Coin toss")) {
    const Team& recv = g.teamOf(g.openingReceiver);
    const Team& kick = g.teamOf(other(g.openingReceiver));
    push(Who::PBP, Kind::Open,
         pick("toss", {kick.city + " won the toss and deferred, so the " + recv.name + " will get the football first.",
                       "The " + kick.name + " won the toss and chose to defer. " + recv.city + " will receive."}),
         0, 3, 0, 60)
        .snapSafe = true;
    return;
  }
  if (bHas(d, "Two-minute warning")) {
    push(Who::PBP, Kind::Break, pick("2min", {"And that brings us to the two-minute warning. " + scoreLine(g, false),
                                              "Two-minute warning. " + scoreLine(g, false)}),
         1, 4, 0.2, 30)
        .snapSafe = true;
    const Team& O = g.offense();
    const int to = g.timeouts[g.poss];
    const int m = g.margin();
    std::string c;
    if (g.q == 4 && m < 0 && m >= -8)
      c = O.city + " needs a score, and they've got " + bCountWord(to) + (to == 1 ? " timeout" : " timeouts") + " to work with. " +
          (to >= 2 ? "That's plenty. They can use the whole field." : "They have to get out of bounds and stay out of the middle.");
    else if (g.q == 4 && m < -8 && m >= -16)
      c = "It's a two-score game. " + O.city + " needs a quick touchdown, a stop or an onside kick, and then another score. They have to move fast.";
    else if (g.q == 4 && m < -16)
      c = "This one is just about decided. " + O.city + " is playing for pride now.";
    else if (g.q == 4 && m > 8)
      c = "That'll just about do it. " + O.city + " can run the clock from here.";
    else if (g.q == 4 && m > 0)
      c = "Now it's about the clock. " + O.city + " gets a first down or two and this game is over.";
    else if (g.q == 2)
      c = pick("2minq2", {"Two minutes before the half is a big window. You'd like to come away with points and still get the ball to start the third.",
                          "This is where the good teams steal points. Get into field goal range, don't do anything silly."});
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
    push(Who::PBP, Kind::Break,
         left == 0 ? pick("timeout0", {"Timeout, " + t.city + ". That's their last one.", t.city + " uses its final timeout."})
                   : pick("timeout", {"Timeout called by " + t.city + ". They have " + std::string(bCountWord(left)) + " left.",
                                      "And " + t.city + " will use a timeout. That leaves them with " + std::string(bCountWord(left)) + ".",
                                      "The " + nick(t) + " stop the clock with a timeout, " + std::string(bCountWord(left)) + " remaining."}),
         1, 3, 0.3, 20)
        .snapSafe = true;
    return;
  }
  if (bHas(d, "End of ")) {
    const int qEnded = l.q;
    if (qEnded == 2 || qEnded >= 4) return;  // halftime / the final / overtime have their own calls
    std::string s = qEnded == 1 ? "That will end the first quarter. " : qEnded == 3 ? "And that's the end of three. " : "That's the end of the quarter. ";
    push(Who::PBP, Kind::Break, s + scoreLine(g, false), 1, 4, 0.5, 40).snapSafe = true;
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
    push(Who::PBP, Kind::Break, pick("half", {"And that'll do it for the first half. " + scoreLine(g, false),
                                             "The gun sounds, and we're at the half. " + scoreLine(g, false)}),
         1, 5, 0.6, 60)
        .snapSafe = true;
    if (writer_) {
      if (auto st = halftimeStory(g)) breakFallback_ = *st;
      breakDeadline_ = now_ + 45;
      writerBusy_ = false;
      askWriter(g, "half");
    } else if (auto st = halftimeStory(g))
      push(Who::Color, Kind::Break, *st, 1, 4, 0, 60).snapSafe = true;
    // who gets the ball: the team that kicked off to open the game receives the second half
    push(Who::PBP, Kind::Break, g.teamOf(other(g.openingReceiver)).city + " will get the football to start the second half.", 0, 3, 0, 60).snapSafe = true;
    return;
  }
  if (bHas(d, "Overtime!")) {
    push(Who::PBP, Kind::Break, "We are going to overtime! Ten minutes, and both teams are guaranteed a possession.", 2, 5, 0.5, 40).snapSafe = true;
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
    pendingInjury_.push_back(pick("inj_down", {"We've got a man down. Looks like " + full + ".", "There's a player down on the field. That's " + full + ", and the trainers are on their way out.",
                                               full + " is slow to get up, and the trainers are coming out."}));
    injuryWord_.push_back({who ? nm(who) : logName, playNo_ + 3});
    bool good = false;
    if (who)
      for (const PlayerLine* ln : g.stats.forTeam(t.abbr))
        if (ln->pid == who->id) good = ln->tackles >= 4 || ln->rec >= 3 || ln->rushYds >= 40 || ln->sacks >= 1 || ln->ints >= 1 || (ln->passAtt >= 8 && ln->passComp * 10 >= ln->passAtt * 6);
    pendingInjury_.push_back(good ? pick("injuryg", {"That's a tough one. He'd been playing well, too.", "That hurts. He's been one of their best players today."})
                                  : pick("injury", {"You never like to see that. Hopefully it's nothing serious.", "You hate to see anybody go down like that."}));
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
    push(Who::PBP, Kind::Score, scoreSay(g, tdBy_, good ? "And the two-point try is good! " : "The two-point try fails. "), good ? 2 : 1, 4, 0.4, 20).snapSafe = true;
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
  const Player* man = nullptr;
  if (!who.empty()) {
    const int num = std::atoi(who.c_str() + 1);
    const size_t sp = who.find(' ');
    const std::string nmLog = sp == std::string::npos ? std::string() : who.substr(sp + 1);
    for (const auto& p : t.roster)
      if (p->number == num && !p->first.empty() && nmLog == std::string(1, p->first[0]) + "." + p->last) man = p.get();
  }
  const bool presnap = bHas(name, "False Start") || bHas(name, "Delay of Game") || bHas(name, "Offside") || bHas(name, "Encroachment") || bHas(name, "Neutral Zone");
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
                             {"Encroachment", "Encroachment"}, {"Neutral Zone Infraction", "Neutral zone infraction"}};
  std::string foul = name;
  for (const Say& k : kSay)
    if (name == k.log) foul = k.words;
  // the yards, the down it leaves
  // the foul's own yardage (the spot moved by it and the play together); pass interference is a spot foul
  int yards = 0;
  const size_t yp = d.find(" yards.");
  if (presnap && yp != std::string::npos) {
    size_t q = yp;
    while (q > 0 && std::isdigit(static_cast<unsigned char>(d[q - 1]))) --q;
    yards = std::atoi(d.c_str() + q);
  } else if (bHas(name, "Defensive Pass Interference")) {
    yards = g.poss == possBefore_ ? bRound(std::fabs(g.ballOn - losBefore_)) : 0;
  } else {
    yards = bHas(name, "Face Mask") || bHas(name, "Roughing") || bHas(name, "Unnecessary") ? 15
            : bHas(name, "Offensive Holding") || bHas(name, "Offensive Pass") || bHas(name, "Block in the Back") ? 10
                                                                                                          : 5;
  }
  const bool dpiSpot = bHas(name, "Defensive Pass Interference");
  std::string after;
  if (yards > 0) after = dpiSpot ? "A spot foul, " + bS(yards) + " yards" : bS(yards) + (yards == 1 ? " yard" : " yards");
  if (g.down == 1 && !onOff && !presnap) after += pick("pen_first", {", and an automatic first down.", ", and that's a first down."});
  else if (!presnap && g.down == downBefore_ && g.poss == possBefore_)
    after += ", and they'll repeat " + std::string(bOrdWord(g.down)) + " down. Now it's " + bDownDist(g.down, g.toGo, g.ballOn) + ".";
  else after += ". " + bCap(bDownDist(g.down, g.toGo, g.ballOn)) + ".";
  if (!(g.down == 1 && !onOff && !presnap)) ddSaidKey_ = bDdKey(g, g.down, g.toGo, g.ballOn);
  std::string open;
  if (presnap) open = pick("flagpre", {"Flag before the snap.", "They've thrown a flag before the snap.", "We've got a flag, and nobody's moving."});
  else open = once("laundry") ? std::string("We've got laundry on the field.") : pick("flag", {"Flag on the play.", "There's a flag down.", "And a flag comes in."});
  std::string swing;
  const PlayResult* r = (!presnap && sim_ && sim_->result) ? &*sim_->result : nullptr;
  if (r && onOff && r->yards >= 10 && !r->incomplete && !r->interception) swing = " That wipes out a " + bS(r->yards) + "-yard play.";
  else if (r && !onOff && r->interception) swing = " So the interception comes off the board.";
  else if (r && !onOff && (r->incomplete || r->kind == PlayKind::Sack)) swing = " That bails them out.";
  if (man && man->pos == Pos::QB && bHas(name, "Holding")) man = nullptr;  // (charged to the quarterback in the log: the line gets it)
  const std::string mn = man ? ", " + nm(man) + ", number " + bS(man->number) + "." : onOff && !presnap ? ", on the offensive line." : ", on " + t.city + ".";
  if (!pendingBare_.empty() && !presnap && pendingResult_.size()) push(Who::PBP, Kind::Result, pendingBare_, 1, 4, 0, 10);  // what happened, then the flag on it
  pendingBare_.clear();
  push(Who::PBP, Kind::Result, open + " " + foul + mn + swing + " " + after, 1, 4, 0.2, 20).snapSafe = true;
  dropKind(Kind::Analysis);
  pendingAnalysis_.clear();
  pendingResult_.clear();
  // the analyst: the same man again, a stop turned into a first down, or the foul itself
  int& times = man ? penCount_[man->id] : penCount_[0];
  if (man) ++times;
  std::string c;
  if (man && times >= 2)
    c = times == 2 ? pick("pen_again", {"That's twice now on " + nm(man) + ". He's having a rough day.", nm(man) + " again. That's the second flag on him."})
                   : nm(man) + " again. That's " + bCountWord(times) + " flags on him. They might have to get him some help.";
  else if (!onOff && !presnap && g.down == 1 && downBefore_ >= 3)
    c = pick("pen_gift", {"That's a gift. They had them stopped, and now it's a first down.", "The defense did its job, and that flag hands it right back to them."});
  else if (bHas(name, "Offensive Holding"))
    c = pick("pen_hold", {"He got beat, and when you get beat on the edge you grab. The official was right there.", "You can see it, he's got two handfuls of jersey.",
                          "That's a drive killer. Now you're behind the chains."});
  else if (bHas(name, "Defensive Pass Interference"))
    c = pick("pen_dpi", {"He never turned his head around to find the football. When you do that, you're going to get flagged.",
                         "He was beat, and he knew it, so he grabbed. That's a big chunk of yards.", "Contact before the ball got there. Easy call."});
  else if (bHas(name, "Roughing"))
    c = pick("pen_rough", {"The ball was gone, and he still drove him into the ground. You can't put your full weight on the quarterback.", "That one's going to cost them fifteen yards. He was late."});
  else if (bHas(name, "False Start"))
    c = pick("pen_fs", {"Crowd noise. When it gets this loud, the snap count is the first thing to go.", "Just jumped early. Little things like that hurt you."});
  else if (bHas(name, "Offside") || bHas(name, "Neutral Zone") || bHas(name, "Encroachment"))
    c = pick("pen_off", {"Free five yards. The hard count got him.", "He was trying to time the snap and guessed wrong."});
  else if (bHas(name, "Face Mask")) c = pick("pen_mask", {"Grabbed the face mask on the tackle. That's fifteen yards.", "Hand right on the mask. Can't do that."});
  else if (bHas(name, "Delay of Game")) c = pick("pen_delay", {"They just couldn't get it snapped in time. That's on the operation.", "Five yards for nothing."});
  else if (bHas(name, "Defensive Holding") || bHas(name, "Illegal Contact"))
    c = pick("pen_dhold", {"Contact past five yards. The automatic first down is the big part of it.", "He got his hands on him downfield. The officials see it."});
  else if (bHas(name, "Offensive Pass Interference"))
    c = pick("pen_opi", {"He pushed off to create the separation.", "Extended the arms, gave him a shove. That's offensive pass interference."});
  const bool must = (man && times >= 2) || (!onOff && !presnap && g.down == 1 && downBefore_ >= 3);
  if (!c.empty() && (must || chance(0.7))) push(Who::Color, Kind::Analysis, c, 1, 3, 0, 15);
}

// ------------------------------------------------------------------ a new play

void Booth::onNewPlay(const Game& g, const PlaySim& s) {
  presnapT_ = 0;
  presnapSaid_ = readSaid_ = false;
  calledThrow_ = calledCatch_ = calledRun_ = breakaway_ = calledPressure_ = liveTalk_ = fumbleCalled_ = false;
  calledTD_ = calledINT_ = calledSack_ = calledFumble_ = calledPbu_ = calledGoingToScore_ = calledDrop_ = pbuSeen_ = dropSeen_ = false;
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
    live("handoff", "Handoff.", 1);
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
  if (t == "pbu") {
    // a hand on the ball is not yet an incompletion (a tipped ball can still be caught, or picked): called when it lands
    pbuSeen_ = true;
    return;
  }
  if ((t == "contested_inc" || (t == "incomplete" && pbuSeen_)) && !calledPbu_) {
    calledPbu_ = true;
    live("", pick("l_pbu", {"Broken up!", "Knocked away!", "Batted down!"}), 2, 0.9, "pbu");
    return;
  }
  if (t == "batted") {
    live("", pick("l_bat", {"Batted at the line!", "Knocked down at the line of scrimmage!"}), 2, 0.9, "pbu");
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
    skipDrop_ = (op && (op->type == PlayType::Screen || op->hasTag("quick"))) || chance(0.2);
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
    bare = downTag_.substr(1);
    downTag_.clear();
  }
  const std::string text = bare.empty() ? downTag_.substr(downTag_.empty() ? 0 : 1) : bare + downTag_;
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
  if (auto a = analysis(g, s, r, aEnergy)) {
    std::string t = *a;
    if (replayWorthy_ && playAngle_ && chance(0.6))
      t = pick("replay", {"Let's take another look. ", "Here's the replay. ", "Watch this again. ", "Look at it from this angle. "}) + t;
    if (creditRead_) {
      creditRead_ = false;
      const std::string credit = pick("credit", {"You called it before the snap, " + crew_.colorFirst + ".", "Just like you said, " + crew_.colorFirst + ".",
                                                 crew_.colorFirst + ", you saw that one coming."});
      if (replayWorthy_) push(Who::PBP, Kind::Analysis, credit, 1, 3, 0, 20);  // (after the result, said at the whistle)
      else creditLine_ = credit;                                                // after the held result
    }
    if (replayWorthy_) {
      if (playAngle_ && !r.td && chance(0.3) && cooled("toss", 240)) {
        mark("toss");
        push(Who::PBP, Kind::Analysis, pick("toss", {crew_.colorFirst + ", what happened there?", "Take us through it, " + crew_.colorFirst + ".",
                                                     crew_.colorFirst + ", how does that happen?", "What did you see, " + crew_.colorFirst + "?"}),
             1, 3, 0, 20);
        // the answer opens with the play, not with "Let's take another look"
        if (t.rfind("Let's take another look. ", 0) == 0 || t.rfind("Here's the replay. ", 0) == 0 || t.rfind("Watch this again. ", 0) == 0 ||
            t.rfind("Look at it from this angle. ", 0) == 0)
          t = t.substr(t.find(". ") + 2);
      }
      push(Who::Color, Kind::Analysis, t, aEnergy, 3, 0, 25);
    }
    else {
      pendingAnalysis_ = t;
      pendingAnalysisEnergy_ = aEnergy;
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
  const Team& O = g.offense();
  const Team& D = g.defense();
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
  if (r.safety) {
    energy = 3;
    const std::string who = !r.tacklers.empty() ? nm(r.tacklers[0]) : std::string("the defense");
    return pick("r_safety", {"Brought down in the end zone by " + who + ", and that's a safety! Two points, " + D.city + ".",
                             "Safety! " + who + " gets him in the end zone. Two points for " + D.city + "."});
  }
  auto firstTag = [&]() -> std::string {
    if (!first) return std::string();
    if (downBefore_ == 3)
      return " " + (tier(g) == 0 ? pick("r_3rd", {"And that's a first down! Big conversion on third down.", "Moves the chains on third down.", "First down, and they keep the drive alive.",
                                                 "That'll do it, first down.", "And they've moved the sticks."})
                                 : pick("r_3rdq", {"That's a first down.", "Enough for the first.", "They move the chains."}));
    if (downBefore_ == 4)
      return " " + (tier(g) == 0 ? pick("r_4th", {"They pick it up on fourth down!", "And they convert on fourth down!", "Gamble pays off! First down!"})
                                 : pick("r_4thq", {"They get it on fourth down.", "That's enough on fourth down."}));
    if (los + y >= 100) return std::string();
    return " " + pick("r_1st", {"First down.", "That's a first down.", "And that'll move the chains.", "That's enough for the first.", "Moves the sticks.",
                                "First down, " + O.city + ".", "And they've got a fresh set of downs.", "Past the marker, first down."});
  };
  auto nextDown = [&]() -> std::string {
    if (first || r.td || r.turnover) return std::string();
    if (fourth) {
      energy = (std::max)(energy, 2);
      fourthFail_ = true;
      if (g.q >= 4 && g.margin() < 0 && g.clock < 240) {
        sealed_ = g.timeouts[g.poss] == 0 || g.clock < 45;
        return " " + pick("r_todlate", {"Stopped! Turnover on downs, and that should just about do it.", "They don't get it! " + D.city + " takes over, and that may be the ballgame."});
      }
      return " " + pick("r_tod", {"Short! And that'll be a turnover on downs.", "They don't get it. " + D.city + " takes over on downs.", "Stopped short! Turnover on downs."});
    }
    const double gained = r.endX - losBefore_;
    if (downBefore_ == 3 && gained > 0 && toGoBefore_ - gained < 1.5 && r.kind != PlayKind::Sack)
      return " " + pick("r_short3", {"But he's just short of the marker.", "Just short of the first down.", "And that's short of the sticks."});
    return std::string();  // (third and fourth downs: the setup says them)
  };
  switch (r.kind) {
    case PlayKind::Kneel:
      if (!once("kneel" + bS(g.q) + bS(static_cast<int>(g.clock / 60)))) return std::string();
      return g.q == 2 ? pick("r_kneelh", {qb + " takes a knee.", qb + " kneels it."})
                      : pick("r_kneel", {qb + " takes a knee.", "And " + qb + " will kneel it.", "Victory formation, " + qb + " goes down."});
    case PlayKind::Spike:
      return pick("r_spike", {qb + " spikes it to stop the clock with " + bClock(g.clock - r.duration) + " left.", "Ball spiked, the clock stops."});
    case PlayKind::Sack: {
      energy = 2;
      const std::string who = r.sackedBy ? pname(r.sackedBy) : tk;
      if (r.fumbleBy) {
        energy = 3;
        out = r.fumbleLost ? pick("r_sfum", {"Strip sack! And " + D.city + " has the football!", who + " gets the sack and the fumble, and " + D.city + " recovers!"})
                           : who + " gets the sack, the ball came out, but " + O.city + " falls on it.";
        return out;
      }
      if (said("sack"))
      {
        int n = 1;
        if (r.sackedBy)
          for (const PlayerLine* ln : g.stats.forTeam(D.abbr))
            if (ln->pid == r.sackedBy->id) n = ln->sacks + 1;  // (this one not in the book yet)
        if (n >= 2) topicT_["sk:" + bS(r.sackedBy->id) + ":" + bS(n)] = now_;  // (the count is said here: the story would repeat it)
        if (n >= 2)
          out = pick("r_sackn", {"That's sack number " + bS(n) + " for " + nm(r.sackedBy) + ". A loss of " + bS(-y) + ".", nm(r.sackedBy) + " again! That's " + std::string(bCountWord(n)) + " sacks today."});
        else
          out = pick("r_sack2", {"That's a loss of " + bS(-y) + ".", "A " + bS(-y) + "-yard loss on the play.", "Back to " + at + ".", "He loses " + yds(-y) + " on the play."});
      }
      else
        out = pick("r_sack", {who + " with the sack, a loss of " + bS(-y) + ".", qb + " is sacked by " + who + ", down at " + at + ".", who + " gets home! Sack, and a loss of " + bS(-y) + "."});
      return out + later(nextDown());
    }
    case PlayKind::Scramble: {
      std::string pre;
      if (y >= 10) pre = pick("r_scrbig", {qb + " scrambles and picks up " + bS(y) + "!", "Great improvisation by " + qb + ", " + yds(y) + " on the ground.", qb + " takes off and gets " + bS(y) + "!"});
      else if (y > 0) pre = pick("r_scr", {qb + " scrambles " + forN(y) + ".", qb + " tucks it and gets " + yds(y) + ".", qb + " runs it " + forN(y) + "."});
      else pre = qb + " tries to run, but he's dropped for " + (y == 0 ? std::string("no gain.") : "a loss of " + bS(-y) + ".");
      if (y >= 10) energy = 2;
      return pre + later(firstTag() + nextDown());
    }
    case PlayKind::Run: {
      const std::string rb = pname(r.rusher);
      // the live call just said his name: the result can go straight to the yards
      const bool saidRun = said("handoff") && chance(0.55);
      if (r.fumbleBy) {
        energy = 3;
        if (r.fumbleLost)
          return pick("r_rfum", {"And it's " + D.city + " ball! " + (r.fumbleRecBy ? nm(r.fumbleRecBy) + " came up with it." : std::string()), "Turnover! " + D.city + " takes it away."});
        return std::string("The ball came loose, but ") + O.city + " held on to it. " + (y > 0 ? "A gain of " + bS(y) + "." : y == 0 ? std::string("No gain.") : "A loss of " + bS(-y) + ".");
      }
      if (r.td) {
        energy = 3;
        if (y <= 4) return pick("r_rtds", {rb + " punches it in from the " + bS(y) + "!", rb + " pushes in from " + yds(y) + " out!", rb + " gets in from short yardage!"});
        return pick("r_rtd", {rb + " from " + bS(y) + " yards out! " + O.city + " scores!", "A " + bS(y) + "-yard touchdown run for " + rb + "!", rb + " goes " + bS(y) + " yards for the score!"});
      }
      if (r.safety) {
        energy = 3;
        return rb + " is tackled in the end zone, and that's a safety! Two points for " + D.city + ".";
      }
      if (y >= 20) {
        energy = 2;
        out = pick("r_runbig", {rb + " breaks off a " + bS(y) + "-yard run!", "What a run by " + rb + "! " + bS(y) + " yards!", rb + " rips off " + bS(y) + "!", "A " + bS(y) + "-yard gallop for " + rb + "!"});
        if (!r.outOfBounds && !tk.empty() && chance(0.5)) out += " " + tk + " finally brings him down at " + at + ".";
      } else if (y >= 5) {
        out = saidRun ? pick("r_runS", {"Picks up " + bS(y) + ".", "Gain of " + bS(y) + ".", bCap(yds(y)) + " on the play.", "Good for " + bS(y) + ".", "And he gets " + bS(y) + "."})
                   : pick("r_run", {rb + " picks up " + bS(y) + ".", rb + " " + forN(y) + ".", rb + " gains " + bS(y) + " before " + (tk.empty() ? std::string("they bring him down") : tk + " brings him down") + ".",
                                    "Good run by " + rb + ", " + yds(y) + ".", rb + " finds a crease, picks up " + bS(y) + "."});
      } else if (y > 0) {
        out = saidRun && first ? pick("r_runshortF", {"Picks up " + yds(y) + ".", "Gets " + yds(y) + "."})
              : saidRun ? pick("r_runshortS", {"Just " + yds(y) + ".", "Only " + yds(y) + " there.", "Gets " + yds(y) + " before he's wrapped up.", "Not much there, " + yds(y) + "."})
                   : pick("r_runshort", {rb + " " + forN(y) + ".", rb + " grinds out " + yds(y) + ".", "Just " + yds(y) + " on the carry for " + rb + ".",
                                         rb + " is met by " + (tk.empty() ? std::string("a crowd") : tk) + " after " + yds(y) + "."});
      } else if (y == 0) {
        out = pick("r_run0", {rb + " is stopped for no gain.", (tk.empty() ? std::string("The defense") : tk) + " stuffs " + rb + " at the line.", "Nothing there for " + rb + ".",
                              "No gain on the play. " + (tk.empty() ? std::string("They") : tk) + " got there in a hurry."});
      } else {
        energy = 2;
        out = pick("r_runloss", {(tk.empty() ? std::string("The defense") : tk) + " drops " + rb + " for a loss of " + bS(-y) + ".",
                                 rb + " is hit in the backfield, a loss of " + bS(-y) + ".", "Stopped behind the line! A " + bS(-y) + "-yard loss."});
      }
      if (r.outOfBounds && y > 0 && y < 20 && chance(0.4)) out = rb + " picks up " + bS(y) + " and steps out of bounds.";
      return out + later(firstTag() + nextDown());
    }
    case PlayKind::Pass: {
      if (r.thrownAway) {
        if (said("away")) {
          const int nd = downBefore_ + 1;
          if (nd > 4) return std::string();
          if (nd == 4) return pick("r_away4", {"Incomplete. That brings up fourth down.", "Incomplete, and it'll be fourth down."});  // (the setup gives the distance)
          ddSaidKey_ = bDdKey(g, nd, toGoBefore_, los);
          return pick("r_away2", {"Incomplete. " + bCap(bDownDist(nd, toGoBefore_, los)) + ".", "Incomplete, and that'll bring up " + bDownDist(nd, toGoBefore_, los) + "."});
        }
        return pick("r_away", {qb + " has to throw it away.", "Nothing open, and " + qb + " throws it away.", qb + " gets rid of it. Nobody there."}) + later(nextDown());
      }
      if (r.interception && !r.turnover) {
        if (giveBackPlay_ != playNo_) {
          giveBackPlay_ = playNo_;
          ++giveBack_[other(g.poss) == TeamKey::Home ? 0 : 1];  // (the intercepting side put it on the ground)
        }
        energy = 3;
        const std::string who = r.intBy ? nm(r.intBy) : std::string("the defender");
        if (r.intBy) gaveBack_.insert(r.intBy->id);
        return pick("r_intback", {who + " picks it off, but he coughs it up on the return, and " + O.city + " gets it back! What a sequence!",
                                  "Intercepted, and then fumbled! " + O.city + " falls on it and keeps the football!"});
      }
      if (r.interception) {
        energy = 3;
        const std::string who = r.intBy ? pname(r.intBy) : std::string("the defense");
        // the man who gave one back earlier: this one he keeps
        const std::string amends = r.intBy && gaveBack_.count(r.intBy->id) && once("amends" + bS(r.intBy->id))
                                       ? pick("r_amends", {" He gave one back earlier. Not this time.", " And he makes up for the one he fumbled away."}) : std::string();
        if (r.td == Side::Def)
          return pick("r_pick6", {"Pick six! " + who + " takes it all the way back!", who + " jumps the route and he's gone! Interception for a touchdown!"}) + amends;
        if (!amends.empty()) return pick("r_int", {"Intercepted by " + who + "! " + D.city + " ball.", "And it's picked off! " + who + " with the interception."}) + amends;
        out = said("int") ? pick("r_int2", {who + " with the interception, and " + D.city + " takes over at " + spot(g, 100 - endX, other(g.poss)) + ".",
                                           who + " brings it back to " + spot(g, 100 - endX, other(g.poss)) + ". A huge turnover."})
                         : pick("r_int", {"Intercepted by " + who + "! " + D.city + " ball.", "And it's picked off! " + who + " with the interception."});
        return out;
      }
      if (r.incomplete) {
        const std::string tg = r.target ? nm(r.target) : std::string("his receiver");
        bool drop = false;
        for (const PlayEvent& e : s.events)
          if (e.type == "drop") drop = true;
        if (r.pbuBy && said("pbu"))
          out = pick("r_pbu2", {nm(r.pbuBy) + " with the breakup.", "That's " + nm(r.pbuBy) + " getting a hand in there.", "Nice play by " + nm(r.pbuBy) + "."});
        else if (drop && said("drop"))
          out = pick("r_drop2", {"Incomplete. That one was right there for " + tg + ".", "Dropped by " + tg + ". He'll want that one back."});
        else if (r.pbuBy)
          out = pick("r_pbu", {"Incomplete, broken up by " + pname(r.pbuBy) + ".", "Intended for " + tg + ", but " + nm(r.pbuBy) + " gets a hand on it.",
                               "Good coverage by " + nm(r.pbuBy) + ", the pass falls incomplete."});
        else if (drop)
          out = pick("r_drop", {"Incomplete. " + tg + " couldn't hang on.", "That one's dropped by " + tg + ".", "Off the hands of " + tg + ", incomplete."});
        else
          out = pick("r_inc", {"Incomplete, intended for " + tg + ".", "The pass falls incomplete. " + tg + " was the target.", "Incomplete, looking for " + tg + ".",
                               qb + "'s pass for " + tg + " is off the mark.", "Can't connect. " + tg + " was the intended receiver."});
        return out + later(nextDown());
      }
      const std::string rc = pname(r.receiver);
      const bool otherMan = (said("throw") || said("deep")) && r.receiver && spokenTarget_ >= 0 && spokenTarget_ != r.receiver->id;
      if (otherMan && !r.td && !r.fumbleBy) {
        if (y >= 25) energy = 2;
        return pick("r_otherman", {"And it's " + rc + " who comes down with it, " + yds(y) + ".", "It ends up with " + rc + " instead. " + bCap(yds(y)) + ".",
                                   "It's " + rc + " who hauls it in, good for " + bS(y) + "."});
      }
      if (r.fumbleBy) {
        energy = 3;
        if (r.fumbleLost) return "Caught, but then stripped! " + D.city + " comes up with it!";
        return rc + " made the catch and put it on the ground, but " + O.city + " recovers.";
      }
      if (r.td) {
        energy = 3;
        return said("catch") || said("throw") || said("deep") ? pick("r_ptd2", {qb + " to " + rc + ", " + bS(y) + " yards, for the touchdown!", "A " + bS(y) + "-yard touchdown strike from " + qb + " to " + rc + "!",
                                              "That's a " + bS(y) + "-yard touchdown pass, " + qb + " to " + rc + "!"})
                            : pick("r_ptd", {qb + " finds " + rc + " in the end zone! Touchdown!", rc + " with the touchdown catch!"});
      }
      if (y >= 25) {
        energy = 2;
        out = pick("r_pbig", {qb + " to " + rc + ", a " + bS(y) + "-yard gain!", "That's a big play! " + rc + " for " + bS(y) + ".", rc + " with a " + bS(y) + "-yard catch, down to " + at + "."});
      } else if ((said("throw") || said("deep")) && spokenTarget_ == (r.receiver ? r.receiver->id : -2) && y > 0) {
        out = pick("r_pcomp2", {rc + " picks up " + bS(y) + ".", "A gain of " + bS(y) + " on the play.", rc + " with the grab, " + yds(y) + ".", "That's good for " + bS(y) + ".",
                                "Complete, " + yds(y) + ".", "Caught for " + bS(y) + "."});
      } else if (y > 0) {
        out = pick("r_pcomp", {qb + " completes it to " + rc + " " + forN(y) + ".", "Complete to " + rc + ", " + yds(y) + ".", qb + " finds " + rc + " " + forN(y) + ".",
                               "Short pass to " + rc + ", picks up " + bS(y) + "."});
      } else {
        if (y >= -1)
          out = pick("r_pzero", {rc + " makes the catch, but goes nowhere.", "Caught by " + rc + ", but " + (tk.empty() ? std::string("they're") : tk + " is") + " right there. No gain."});
        else
          out = pick("r_ploss", {rc + " makes the catch, and loses " + bS(-y) + ".", "Caught behind the line, and " + (tk.empty() ? std::string("they drop") : tk + " drops") + " him for a loss of " + bS(-y) + "."});
      }
      if (r.outOfBounds && y > 0 && chance(0.4)) out += " He gets out of bounds.";
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
  if (r.td == Side::Off) {
    push(Who::PBP, Kind::Result, pick("ret_td", {rn + " takes it all the way! What a return!", "He's gone! " + rn + " goes the distance!"}), 3, 5, 0.3, 10).snapSafe = true;
    push(Who::Color, Kind::Analysis, pick("ret_tdA", {"Look at the wall they set up. One cut, and there's nobody left.", "That's a special teams breakdown. Somebody lost his lane."}), 2,
         3, 0, 20);
    return;
  }
  if (r.fumbleLost) {
    push(Who::PBP, Kind::Result, "He's lost the football on the return! And the kicking team has it!", 3, 5, 0.2, 10).snapSafe = true;
    return;
  }
  const double endX = (std::max)(0.5, (std::min)(99.5, r.endX));
  const int ry = bRound(endX - (s.catchX > 0 ? s.catchX : (s.cfg.kick ? s.cfg.kick->landX : endX)));
  const std::string at = spot(g, endX, g.poss);
  std::string out;
  const bool spotSaid = said("down");
  if (ry >= 30)
    out = endX >= 45 ? pick("ret_big", {"A big return by " + rn + "! " + bS(ry) + " yards, and great field position.", rn + " with a " + bS(ry) + "-yard return!"})
                     : pick("ret_big2", {"A " + bS(ry) + "-yard return by " + rn + ".", rn + " gets " + bS(ry) + " on the return."});
  else if (ry > 0 && spotSaid) out = pick("ret2", {"A " + bS(ry) + "-yard return for " + rn + ".", rn + " brings it back " + bS(ry) + ".", "Good for " + bS(ry) + " on the return."});
  else if (ry > 0) out = pick("ret", {rn + " brings it back to " + at + ".", "Returned to " + at + " by " + rn + ".", rn + " gets it out to " + at + "."});
  else out = rn + " is stopped right there, at " + at + ".";
  push(Who::PBP, Kind::Result, out, ry >= 30 ? 2 : 1, 4, 0.3, 10);
  if (ry >= 30 && chance(0.6))
    push(Who::Color, Kind::Analysis, pick("ret_A", {"That flips the field. They start in great shape.", "The coverage unit got caught up in the wash, and he found the seam."}), 1, 2,
         0, 15);
  (void)punt;
}

// ------------------------------------------------------------------ the analyst

std::optional<std::string> Booth::analysis(const Game& g, const PlaySim& s, const PlayResult& r, int& energy) {
  struct Angle {
    double score;
    std::string topic;
    std::string text;
  };
  std::vector<Angle> c;
  auto add = [&](double sc, const std::string& topic, const std::string& text, double cool = 1300) {
    if (text.empty() || !cooled(topic, cool) || spokenSet_.count(text)) return;
    if (s.cfg.defCall && text.find(bCoverageWords(s.cfg.defCall->coverage)) != std::string::npos && !cooled("covname", 420)) return;
    c.push_back({sc + rng_.range(0, 0.25), topic, text});
  };
  const OffPlay* op = s.cfg.offPlay;
  const DefCall* dc = s.cfg.defCall;
  const Team& O = g.offense();
  const Team& D = g.defense();
  const bool blitz = dc && !dc->blitz.empty();
  const bool big = r.yards >= 20 || r.td || r.turnover;
  const std::string qb = r.passer ? nm(r.passer) : (s.qb ? nm(s.qb->p) : std::string("the quarterback"));
  std::string cov = dc ? bCoverageWords(dc->coverage) : std::string("zone");
  const std::string cf = crew_.pbpFirst;
  // a blocker the play showed: the pancake, the man who beat his block
  const Player* pancake = pancakes_.empty() ? nullptr : pancakes_.front();
  const Shed* shed = sheds_.empty() ? nullptr : &sheds_.front();
  switch (r.kind) {
    case PlayKind::Pass: {
      if (r.complete && !r.interception) {
        const std::string rc = nm(r.receiver);
        {
          const std::string route = bRoute(catchRoute_);
          const std::string def = catchDef_ ? nm(catchDef_) : std::string();
          if (!route.empty() && r.yards >= 6)
            add(1.05, "a_route", pick("a_route", {rc + " runs the " + route + (def.empty() ? std::string("") : " and gets " + def + " turned around") + ". That's a pro route.",
                                                  "Watch the " + route + " by " + rc + ". He sells it, breaks it off, and " + qb + " puts it right on him.",
                                                  "It's the " + route + " against " + cov + ", and that's exactly the throw that coverage gives you.",
                                                  (def.empty() ? std::string("The defender") : def) + " is in decent position, but " + rc + " wins on the " + route + ". You can't cover it much better than that."}),
                1800);
        }
        if (r.catchSep >= 5 && r.airYards >= 6 && r.yards >= 8)
          add(1.0, "a_open",
              bManCov(dc->coverage) ? pick("a_openman", {"They were in man, and " + rc + " just ran away from his guy. Look at the separation.",
                                                         "Man coverage, and " + (catchDef_ ? nm(catchDef_) : std::string("the corner")) + " never had a chance. " + rc + " was open by five yards."})
                                    : pick("a_openzone", {"That's " + cov + ", and " + rc + " sits down right in the hole. Nobody within five yards of him.",
                                                          "Somebody busted the coverage there. " + rc + " was all by himself.",
                                                          "They're playing " + cov + ", and " + qb + " finds the soft spot. That's an easy throw for him."}));
        if (r.catchSep >= 0 && r.catchSep < 1.2 && r.airYards >= 8 && r.yards >= 8)
          add(1.1, "a_tight", pick("a_tight", {"What a throw. That window was tiny, " + (catchDef_ ? nm(catchDef_) : std::string("the defender")) + " was all over him and " + qb + " put it right on him.",
                                               "Tight coverage, and it didn't matter. That's trust between a quarterback and his receiver.",
                                               "That ball had to be perfect, and it was."}));
        if (r.yac >= 10 && r.yac > r.yards + 0.5)  // (caught behind the line: the whole gain is his)
          add(1.0 + r.yac * 0.02, "a_yacb", pick("a_yacb", {"He catches it behind the line, and " + rc + " makes all of that himself.",
                                                            "That's a negative throw, and " + rc + " turns it into a positive play. That's all him."}));
        else if (r.yac >= 10)
          add(1.0 + r.yac * 0.02, "a_yac", pick("a_yac", {"It's a short throw, and " + rc + " does the rest. " + bS(r.yac) + " yards after the catch.",
                                                          "Look at him after the catch. He makes the first man miss and now it's a big play.",
                                                          "The yards after the catch, that's what separates the good ones."}));
        if (r.airYards >= 25)
          add(1.2, "a_deep", pick("a_deep", {"That ball traveled " + bS(r.airYards) + " yards in the air and hit him in stride. That's a big-time throw.",
                                             qb + " dropped it right in the bucket. You can't defend that.",
                                             "He gave his guy a chance, and " + rc + " went and got it."}));
        if (r.throwPath == 1 && blitz && r.yards >= 5)
          add(1.3, "a_hot", pick("a_hot", {"They brought pressure and " + qb + " knew exactly where to go with it. That's the hot read, and that's preparation.",
                                           "Blitz, and he beats it. Ball's out before the rush gets home. When you bring the house, that's the risk.",
                                           "He saw the blitz coming pre-snap and got it out to the vacated area. Textbook.",
                                           "Everybody's coming, so somebody's open. He found him in a hurry."}));
        if (r.timeToThrow >= 3.4 && !r.pressured && r.yards >= 8)
          add(0.9, "a_time", pick("a_time", {"Look at the time he had. You give a quarterback that long and somebody's going to come open.",
                                             "The protection was outstanding. Nobody got near him."}));
        if (r.pressured && r.yards >= 8 && r.airYards >= 15)
          add(1.0, "a_hitthrow", pick("a_hitthrow", {"He's getting hit as he lets it go, and he still delivers. That's toughness.",
                                                     "He stood in there knowing he was going to take a shot. That's a big-boy throw."}));
        if (op && op->type == PlayType::PlayAction && r.yards >= 8) {
          const TeamLine& otl = g.team[g.poss];
          const bool runWorks = otl.rushAtt >= 8 && otl.rushYds >= otl.rushAtt * 4.3;  // (the fake only holds them if the run has been hurting them)
          add(1.0, "a_pa", runWorks ? pick("a_pa", {"The play fake does it. The linebackers come up hard on the run, and that window opens right behind them.",
                                                    "Play action. When you can run the ball like they have, the fake holds everybody."})
                                    : std::string("Play action, and the linebackers still bite. One false step, and that window opens right behind them."));
        }
        if (op && op->type == PlayType::Screen && r.yards >= 6)
          add(1.0, "a_screen", pick("a_screen", {"Screen's set up beautifully. Let the rush come, then get the big guys out in front.",
                                                 "They baited the rush right into it. That's a well-timed screen."}));
        if (r.brokenTackles > 0 && r.yards >= 10)
          add(1.0, "a_brk", r.brokenTackles >= 2 ? rc + " made " + bCountWord(r.brokenTackles) + " guys miss on that play. That's not on the scheme, that's all him."
                                                 : "Watch him break that tackle. First guy has to get him down, and he didn't.");
        if (downBefore_ == 3 && r.yards >= toGoBefore_)
          add(tier(g) == 0 && r.yards >= 5 ? 0.95 : 0.0, "a_3rdconv", pick("a_3rdconv", {"That's the money down, and they won it. " + qb + (qbSharp(g, r.passer) ? " has been calm all day." : " came through when it mattered."),
                                                    "Third down's where you win football games. That was a great call against " + cov + "."}));
      } else if (r.interception) {
        const std::string who = r.intBy ? nm(r.intBy) : std::string("the defender");
        if (!r.turnover)
          add(2.2, "a_intback", pick("a_intback", {who + " makes a great play on the ball, and then he tries to do too much with it. Get down. You've got the football, protect it.",
                                                   "Two turnovers on one play. " + who + " had it, and he gave it right back. You have to finish the play."}));
        if (r.pressured) add(1.4, "a_intp", "The pressure gets there, he can't step into it, and the ball just floats. " + who + " was sitting there waiting.");
        if (r.airYards >= 20) add(1.2, "a_intd", pick("a_intd", {"He underthrew it, and " + who + " had position. You can't leave that one short.",
                                                                  "Deep ball into coverage. " + who + " read his eyes the whole way."}));
        add(1.0, "a_intr", pick("a_intr", {"He never saw " + who + ". Stared his guy down, and the defense knew where the ball was going.",
                                           "That's " + cov + ", and the quarterback thought it was something else. " + who + " jumps it.",
                                           "Bad decision. You have to throw that one away and live to fight another down."}));
      } else if (r.incomplete) {
        bool drop = false;
        for (const PlayEvent& e : s.events)
          if (e.type == "drop") drop = true;
        if (drop) add(1.1, "a_drop", pick("a_drop", {"He's got to catch that. That's right in his hands.", "The throw was there. That's a drop, plain and simple.",
                                                      "He took his eyes off it, started to run before he caught it."}));
        if (r.pbuBy) add(1.1, "a_pbu", pick("a_pbu", {"Great job by " + nm(r.pbuBy) + ". Stayed in phase, turned and found the football.",
                                                       nm(r.pbuBy) + " played that perfectly. Didn't panic, just got a hand in there at the last second."}));
        if (r.thrownAway) add(0.9, "a_away", pick("a_away", {"Smart play. Nothing there, don't force it. Live to play another down.",
                                                              "That's a veteran move. Throw it away, it's only a down.",
                                                              "No harm done. Better than taking the sack.", "Everybody was covered. He did the right thing with it."}));
        if (r.pressured && !r.thrownAway && !drop) add(1.0, "a_hurry", "The rush got there in a hurry and made him speed it up. Never got to set his feet.");
        if (!drop && !r.pbuBy && !r.thrownAway && r.airYards >= 15) add(0.8, "a_over", pick("a_over", {"Just a little too far out in front of him. He was open, too.",
                                                                                                         "They weren't on the same page. He broke it in, the ball went out."}));
      }
      break;
    }
    case PlayKind::Sack: {
      if (shed && shed->d == r.sackedBy && shed->b)
        if (once("a_sackmove_sig")) add(1.4, "a_sackmove", "Watch " + nm(shed->d) + " against " + nm(shed->b) + ". " + bCap(bMoveWords(shed->move)) + ", and he's by him clean. The quarterback never had a chance.");
      if (blitz) {
        bool unblocked = false;
        for (const std::string& role : dc->blitz)
          if (const Ent* e = s.defRole(role))
            if (r.sackedBy && e->p == r.sackedBy) unblocked = true;
        if (unblocked) add(1.3, "a_sackblitz", "They sent " + nm(r.sackedBy) + " and nobody picked him up. That's a protection breakdown. Somebody has to account for him.");
        else add(0.9, "a_sackblitz2", "They brought more than they could block. " + D.city + " has been dialing up the pressure.");
      }
      if (s.t >= 3.6 && !(shed && shed->d == r.sackedBy) && !said("press") && !said("pressure"))
        add(1.0, "a_cov", pick("a_cov", {"That's a coverage sack. He looked left, looked right, nothing there. Credit the secondary.",
                                         "He held onto it way too long. At some point you have to get rid of it."}));
      if (downBefore_ <= 2 && r.yards <= -6) add(0.5, "a_sackgen", pick("a_sackgen", {"That's a drive-killer. Second and long against this rush is a tough spot.", "Now they're way behind the chains."}));
      break;
    }
    case PlayKind::Run:
    case PlayKind::Scramble: {
      const std::string rb = nm(r.rusher);
      if (r.kind == PlayKind::Scramble && r.yards >= 6)
        add(1.1, "a_scr", pick("a_scr", {"When they play man and turn their backs, the quarterback can take off. Nobody's watching him.",
                                         "That's the dimension he gives you. The play breaks down, and he just makes something happen."}));
      if (r.yards >= 8 && pancake)
        add(1.2, "a_pancake", pick("a_pancake", {"Watch " + nm(pancake) + " on this play. He puts his man on his back, and that's the hole right there.",
                                                 "That's " + nm(pancake) + " just burying his guy. Running backs love that."}));
      if (r.yards >= 10 && r.olBlockDepth >= 3.5)
        add(1.1, "a_second", pick("a_second", {"The offensive line gets to the second level. When your guards are on the linebackers, that's a big play waiting to happen.",
                                               "Look at the push up front. They're moving people five yards off the ball.",
                                               "The center climbs to the linebacker and seals him. Easy yards.", "That hole was huge. You could drive a truck through it."}));
      if (r.yards <= 0 && shed && shed->d)
      {
        const bool tackled = std::find(r.tacklers.begin(), r.tacklers.end(), shed->d) != r.tacklers.end();
        if (tackled)
          add(1.2, "a_stuff", pick("a_stuff", {nm(shed->d) + " sheds the block" + (shed->b ? " from " + nm(shed->b) : std::string()) + " and blows it up. That's a defensive lineman winning his gap.",
                                               "Watch " + nm(shed->d) + ". He gets off the block and he's right there to make the play.",
                                               nm(shed->d) + " takes on the blocker, sheds him, and makes the tackle. Textbook."}), 1500);
      }
      if (r.brokenTackles > 0 && r.yards >= 8)
        add(1.0, "a_rbrk", pick("a_rbrk", {"That's all " + rb + ". Contact early, and he kept those legs churning.",
                                           rb + " breaks the tackle. They had him, and he wouldn't go down.",
                                           "Yards after contact. That's what makes " + rb + " special. The first guy never gets him down.",
                                           "Watch the balance on " + rb + ". Gets hit, spins off it, keeps going."}));
      for (const std::string& m : movesSeen_) {
        if (r.yards < 5) break;  // a move that did not get him anywhere is not the story
        if (m == "spin") add(1.0, "a_spin", "The spin move, right at the point of contact. The tackler ends up hugging air.");
        if (m == "truck") add(1.0, "a_truck", "He just lowered the shoulder and ran right through him. That's a grown man run.");
        if (m == "stiffarm") add(1.0, "a_stiff", "The stiff arm. Locked it out and pushed him right into the ground.");
        if (m == "juke") add(0.9, "a_juke", "One jump cut and the defender's left grabbing at nothing.");
      }
      if (op && op->run && r.yards >= 5) {
        const RunStyle st = op->run->style;
        if (st == RunStyle::Gap)
          add(0.9, "a_gap", pick("a_gap", {"That's the power scheme. They pull the guard, kick out the end, and " + rb + " follows him right through it.",
                                           "Gap scheme, double team at the point of attack, and the guard leads him up in there.",
                                           "Downhill run. Everybody blocks down, the puller kicks out, and it's a clean lane."}));
        if (st == RunStyle::Stretch)
          add(0.9, "a_stretch", pick("a_stretch", {"Outside zone. Stretch it, stretch it, and when the cutback lane opens, one cut and go.",
                                                   "They get the defense flowing sideways and " + rb + " plants and cuts back against it."}));
        if (st == RunStyle::Draw)
          add(1.0, "a_draw", pick("a_draw", {"The draw. They show pass, the linebackers bail into coverage, and there's nobody home in the middle.",
                                             "Perfect time for a draw. The rush comes upfield, and they run right by the play."}));
        if (st == RunStyle::Zone)
          add(0.7, "a_zone", pick("a_zone", {"Inside zone, and he's patient. Lets the blocks develop and picks his hole.",
                                             "He presses the line, makes the linebacker commit, and then he goes.", "Good patience. He waited for the double team to open it up."}));
      }
      if (toGoBefore_ <= 2 && r.yards >= toGoBefore_ && (downBefore_ >= 3))
        add(1.1, "a_short", "Short yardage, everybody in the stadium knows it's coming, and they still get it. That's a physical offensive line.");
      if (toGoBefore_ <= 2 && r.yards < toGoBefore_ && downBefore_ >= 3)
        add(1.1, "a_shortstop", "They sold out for the run, put eight in the box, and won the line of scrimmage. Great stop.");
      if (r.yards >= 20) add(0.8, "a_bigrun", pick("a_bigrun", {"Once he gets to the second level, it's over. Nobody's catching him from behind.",
                                                                "One missed tackle in the hole and that's what happens."}));
      if (hitBy_) add(1.3, "a_hit", "What a hit by " + nm(hitBy_) + ". He timed that perfectly. You hear that one all the way up here.");
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
    add(1.6, "a_readblitz", blitz ? (r.yards >= 8 ? pick("a_readblitz1", {"There's the pressure I was talking about, and he beat it. That's a big play against the blitz.",
                                                                          "They brought it, like I thought they would, and he made them pay."})
                                                  : pick("a_readblitz2", {"There's the pressure I was talking about, and it got there.", "They brought the extra man, and the protection never sorted it out."}))
                                  : std::string("They showed it, and then they backed out. Good disguise by the defense."), 0);
  if (lastRead_ == "r_c0" && (r.yards >= 15 || r.incomplete || r.kind == PlayKind::Sack))
    add(1.5, "a_readc0", r.yards >= 15 ? "That's what happens against Cover Zero. One man beats his guy and it's a big play." : "Cover Zero, and the rush got there before anybody could get open.", 0);
  if (lastRead_ == "r_4th")
    add(1.4, "a_read4th", r.yards >= toGoBefore_ ? std::string("And there it is. You trust your guys, and they deliver.") : std::string("You have to live with it. I'd make that call every time."), 0);
  if (const size_t hashAt = lastRead_.find('#'); hashAt != std::string::npos && r.kind == PlayKind::Pass) {
    const std::string topic = lastRead_.substr(0, hashAt);
    const int subj = std::atoi(lastRead_.c_str() + hashAt + 1);
    const Player* tgt = r.receiver ? r.receiver : r.target;
    if (tgt && tgt->id == subj) {
      const std::string tn = nm(tgt);
      if (r.complete && !r.interception && r.yards >= 8) {
        creditRead_ = chance(0.6);
        add(1.7, "a_readwin" + topic,
            topic == "r_press" ? pick("a_readpress", {"He beat the jam. Clean release off the line, and after that it's a footrace.", "They pressed him, and " + tn + " won at the line. That's the whole play."})
            : topic == "r_slot" ? pick("a_readslot", {"That's the matchup I liked. " + tn + " is too quick for him inside.", "Inside, one on one, and " + tn + " wins it again."})
                                : pick("a_readmatch", {"They went right back to " + tn + ". When it's working, you keep doing it.", "That's what I'd do. Keep feeding " + tn + " until they take him away."}),
            0);
      } else if (r.incomplete && r.pbuBy) {
        add(1.6, "a_readlost" + topic, pick("a_readlost", {"That one goes to the defense. " + nm(r.pbuBy) + " won the matchup this time.",
                                                             "They went to " + tn + ", and " + nm(r.pbuBy) + " was ready for it. Good answer by the defense."}), 0);
      }
    }
  }
  // the routine down, read like a coach: staying ahead of the chains, throwing short of the sticks, a run streak
  const bool runKind = r.kind == PlayKind::Run;
  const int runsInRow = (runStreakTeam_ == possBefore_ ? runStreak_ : 0) + (runKind ? 1 : 0);
  if (runKind && downBefore_ == 1 && r.yards >= 4 && r.yards < 10)
    add(0.55, "a_ahead", pick("a_ahead", {"That's what you want on first down. Four, five yards, stay ahead of the chains and the whole playbook is open.",
                                          "Good, efficient first-down run. Now it's second and manageable.", "You win first down like that, you're in control of the series."}));
  if ((runKind || r.kind == PlayKind::Sack) && downBefore_ <= 2 && r.yards <= -1)
    add(0.6, "a_behind", pick("a_behind", {"Now they're behind the chains. That's a tough spot against this defense.", "Negative play on early down. That's what kills drives."}));
  if (r.kind == PlayKind::Pass && r.complete && downBefore_ == 3 && toGoBefore_ >= 5 && r.yards < toGoBefore_ && !r.td)
    add(0.75, "a_short3", pick("a_short3", {"That's not going to get it. On third down you have to throw it past the sticks.",
                                            "He checked it down, and they tackled the catch. That's exactly what " + D.city + " wanted."}));
  if (runKind && runsInRow >= 4 && r.yards >= 3)
    add(0.65, "a_runstreak", "That's " + std::string(bCountWord(runsInRow)) + " straight runs. They want to lean on this front and wear it down.", 300);
  if (r.kind == PlayKind::Pass && r.complete && r.yards >= 6 && r.yards < 20 && dc)
    add(0.5, "a_readcov", pick("a_readcov", {"Good read. They're in " + cov + ", and he takes what it gives him.", "He knew where he was going with that before the snap. " + bCap(cov) + ", and that route beats it.",
                                             "Nice rhythm throw. Hitch, ball's out, catch, turn upfield."}));
  if (r.kind == PlayKind::Pass && r.complete && r.catchSep >= 0 && r.catchSep < 1.0 && r.yards >= 5 && !r.td)
    add(0.6, "a_contact", pick("a_contact", {"And he holds on through the contact. That's a strong catch.", "Took a shot as the ball got there and hung on."}));
  if (r.kind == PlayKind::Run && r.yards >= 3 && r.yards < 8 && downBefore_ == 3 && r.yards >= toGoBefore_)
    add(0.7, "a_3rdrun", "They ran it on third down and got it. That's confidence in your offensive line.");
  if (r.td && *r.td == Side::Def)
    add(1.2, "a_deftd", pick("a_deftd", {"That's a killer. The defense just took the game into its own hands.", "You can't throw that ball there. That's seven points the wrong way.",
                                         "That changes everything. The offense was moving, and now it's points the other way."}));
  if (r.td && *r.td == Side::Off && r.kind != PlayKind::Sack) {
    const double drivePl = drive_.plays + 1;
    if (drivePl >= 8) add(1.0, "a_tddrive", pick("a_tddrive", {"What a drive. They just kept grinding and finished it off.", "That's a long, patient drive, and they finish it in style."}), 600);
    add(0.7, "a_tdgen", pick("a_tdgen", {"Great execution. They had the look they wanted and they took advantage.",
                                         "That's a statement drive for " + O.city + "."}));
  }
  if (r.turnover && !r.td) {
    add(0.6, "a_to", pick("a_to", {"That's a backbreaker. You can't give a team like this extra possessions.", "Turnovers decide football games, " + cf + ". That could be the one."}), 400);
  }
  if (c.empty()) return std::nullopt;
  if (g.q >= 4 && g.clock - r.duration <= 0.05) return std::nullopt;  // the game's last snap: the close says it
  // routine plays get the analyst some of the time; big ones nearly always
  const bool silent = now_ - lastColorT_ > 60 || (g.q >= 4 && g.clock < 150);  // a minute without him, or the end of the game
  const double want = big || silent || downBefore_ >= 3 || r.yards >= 15 ? 0.97 : (r.kind == PlayKind::Sack || r.interception) ? 0.95 : (view_.canHold || view_.userGame) ? 0.92 : 0.5;
  if (!chance(want)) return std::nullopt;
  std::sort(c.begin(), c.end(), [](const Angle& a, const Angle& b) { return a.score > b.score; });
  mark(c[0].topic);
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
  (void)D;
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
  if (!creditLine_.empty()) {
    push(Who::PBP, Kind::Analysis, creditLine_, 1, 3, 0, 20);
    creditLine_.clear();
  }
  if (bHas(l.desc, "(declined)"))
    push(Who::PBP, Kind::Result, pick("declined", {"There was a flag on the play, but it's been declined.", "A flag came in on that one, but it'll be declined."}), 1, 4, 0, 10);
  if (risky4th_ && downBefore_ == 4 && lastRead_ == "r_4th") {
    // the analyst said he would have punted: now they both know how it went
    const bool got = r.yards >= toGoBefore_ && !r.turnover;
    push(Who::PBP, Kind::Analysis, got ? pick("r4_ok", {"Well, " + crew_.colorFirst + ", the gamble paid off.", "They got it, " + crew_.colorFirst + ". Maybe they knew something we didn't."})
                                       : pick("r4_no", {"And there's your point, " + crew_.colorFirst + ".", "Just what you were worried about, " + crew_.colorFirst + "."}),
         1, 3, 0, 15);
    push(Who::Color, Kind::Analysis, got ? pick("r4_oka", {"I'll take it. Sometimes you have to trust your guys. I still wouldn't have done it.", "Hey, they made me look bad. Good for them."})
                                         : pick("r4_noa", {"That's a short field for the other side now. You just can't do that in your own end.", "That's the risk. Now the defense has to bail them out."}),
         1, 3, 0, 15);
    pendingAnalysis_.clear();
  }
  risky4th_ = false;
  if (fourthFail_ && !sealed_ && g.q >= 4 && g.clock < 240 && possBefore_ != g.poss) {
    pendingAnalysis_ = "Huge stop. Now " + g.offense().city + " needs one first down to put it away, and " + g.teamOf(possBefore_).city + " has to use those timeouts.";
  }
  fourthFail_ = false;
  if (sealed_) {
    sealed_ = false;
    pendingAnalysis_ = pick("sealed", {"That's the ballgame. The defense came up big when it had to.", "What a stand. They needed one stop, and they got it.",
                                       "Game over. That defensive front won the biggest snap of the day."});
  }
  if (!pendingAnalysis_.empty()) {
    push(Who::Color, Kind::Analysis, pendingAnalysis_, pendingAnalysisEnergy_, 3, 0, 20);
    pendingAnalysis_.clear();
    if (chance(0.12) && cooled("ack", 300)) {
      mark("ack");
      push(Who::PBP, Kind::Analysis, pick("ack", {"Good eye.", "You can see it right there."}), 0, 2, 0, 10);
    }
    // now and then the play-by-play man adds the fact that goes with it
    const Player* who = r.receiver && r.complete ? r.receiver : r.rusher;
    if (who && !r.turnover && r.yards >= 5 && cooled("stat:" + bS(who->id), 240) && chance(0.45)) {
      const PlayerLine& ln = const_cast<StatBook&>(g.stats).line(*who, teamOfPlayer(g, who).abbr);
      const bool rec = who == r.receiver;
      const int n = rec ? ln.rec : ln.rushAtt, y = rec ? ln.recYds : ln.rushYds;
      if (n >= 3) mark("stat:" + bS(who->id));  // (one stat line per man per stretch: the follow-up said it again)
      if (n >= 3)
        push(Who::PBP, Kind::Story, pick("addfact", {"And that's " + nm(who) + "'s " + (n <= 10 ? std::string(bOrdWord(n)) : bS(n) + "th") + (rec ? " catch" : " carry") + ". " + bS(y) + " yards on the day.",
                                                     nm(who) + " is up to " + bS(y) + " yards on " + bS(n) + (rec ? " catches." : " carries.")}),
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
      push(Who::PBP, Kind::Story, pick("inj_word", {"Word from the sideline: " + it->first + " will not return.", "We're told " + it->first + " is out for the rest of the game."}), 0, 2, 0, 20);
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
    push(Who::PBP, Kind::Break, g.teamOf(possBefore_).city + " will have to free kick from its own 20.", 0, 3, 0, 15).snapSafe = true;
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
  if (downBefore_ == 4 && toGoBefore_ <= 10 && !r.td && r.yards < toGoBefore_ && g.q >= 4 && g.clock < 240) bigPlays_.push_back("And that fourth-down stop that sealed it.");
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
      if (who->age <= 22) bio = pick("bio_rook", {nm(who) + " is a rookie, and he's playing like he's been here for years.", "Keep an eye on " + nm(who) + ". He's just " + bS(who->age) + ", a rookie."});
      else if (who->age >= 32) bio = pick("bio_vet", {nm(who) + " is " + bS(who->age) + " years old and still doing this. That's a pro.", nm(who) + " is " + bS(who->age) + ", and he's still making plays like that one."});
      else if (who->r.speed >= 93 && who->pos != Pos::DT && who->pos != Pos::OT && r.yards >= 18)
        bio = pick("bio_fast", {nm(who) + " runs about a " + std::string(f) + " forty. Give him a crease and he's gone.", "That speed is real. " + nm(who) + " is one of the fastest players on the field."});
      else if (who->weightLb >= 235 && (who->pos == Pos::RB || who->pos == Pos::TE || who->pos == Pos::FB))
        bio = nm(who) + " is " + bS(ft) + "-" + bS(in) + ", " + bS(static_cast<int>(who->weightLb)) + " pounds. That's a load to bring down.";
      else if (who->heightIn >= 76 && who->pos == Pos::WR)
        bio = nm(who) + " is " + bS(ft) + "-" + bS(in) + ". On those contested balls, he's always open.";
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
}

void Booth::onScored(const Game& g, TeamKey scorer, int pts, const PlayResult& r, bool say) {
  (void)r;
  (void)pts;
  const Team& T = g.teamOf(scorer);
  if (unansweredBy_ == scorer) unanswered_ += pts;
  else {
    unansweredBy_ = scorer;
    unanswered_ = pts;
  }
  // a touchdown's score is read after the try (the extra point / two-point line); a field goal's right away
  if (pts != 6 && say) push(Who::PBP, Kind::Score, scoreSay(g, scorer, ""), 1, 4, 0, 25).snapSafe = true;
  else tdBy_ = scorer;
  if (unanswered_ >= 14 && chance(0.7) && cooled("unanswered", 300)) {
    mark("unanswered");
    push(Who::Color, Kind::Story, "That's " + bS(unanswered_) + " unanswered points for " + T.city + ". They've completely taken over this game.", 1, 3, 0, 25);
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
  std::string s = lead + scoreLine(g, false);
  if (leader && lastLeader_ && *leader != *lastLeader_ && *leader == scorer) {
    ++leadChanges_;
    s = lead + pick("lead", {"And " + T.city + " takes the lead. ", T.city + " goes back in front. ", "That puts " + T.city + " on top. "}) + scoreLine(g, false);
  } else if (leader && !lastLeader_ && *leader == scorer && lastSaidTotal_ > 0) {
    s = lead + T.city + " breaks the tie. " + scoreLine(g, false);
  } else if (!leader && h > 0) {
    s = lead + pick("tie", {"And we're all even. ", "Tie ballgame. "}) + scoreLine(g, false);
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
  push(Who::PBP, Kind::Story,
       pick("drive", {"That drive: " + bS(plays) + " plays, " + bS(yards) + " yards, " + t + " off the clock.",
                      bS(plays) + " plays, " + bS(yards) + " yards on the drive.", "They went " + bS(yards) + " yards in " + bS(plays) + " plays."}),
       0, 2, 0, 30);
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
  if (yds >= 100 && once("100:" + bS(who->id)))
    s = pick("st100", {"And that puts " + nm(who) + " over a hundred yards " + (rec ? "receiving" : "rushing") + " today.",
                       "That's a hundred-yard day for " + nm(who) + (g.q <= 3 ? std::string(", and there's still plenty of football left.") : std::string(".")) });
  else if (td >= 2 && once("td" + bS(td) + ":" + bS(who->id)))
    s = "That's " + std::string(bCountWord(td)) + " touchdowns for " + nm(who) + " today.";
  else if (n >= 4 && yds >= 60 && cooled(topic, 420) && chance(0.5))
    s = nm(who) + " now has " + bS(n) + (rec ? " catches for " : " carries for ") + bS(yds) + " yards.";
  if (s.empty()) return;
  mark(topic);
  push(chance(0.6) ? Who::PBP : Who::Color, Kind::Story, s, 1, 2, 0, 20);
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
    }
    if (kickKind_ == 1 && k.down == 4 && k.toGo <= 2 && k.ballOn >= 50 && k.tier == 0)
      push(Who::Color, Kind::Read, pick("k_puntgo", {"I'd have gone for it there, " + crew_.pbpFirst + ". Fourth and short, in their territory.",
                                                     "That's a conservative call. You're on their side of the field with a yard to go. I don't love it."}),
           1, 4, 0, 5)
          .snapSafe = true;
    if (kickKind_ == 2 && k.down == 4 && k.toGo <= 2 && k.ballOn >= 70 && k.q >= 3 && k.margin < -3)
      push(Who::Color, Kind::Read, "Interesting. Down " + bS(-k.margin) + ", they're taking the points. You might need seven there.", 1, 4, 0, 5).snapSafe = true;
    if (kickKind_ == 2 && bHas(d, "field goal")) {
      const int dist = std::atoi(d.c_str() + d.find(' ') + 1);
      Line& l = push(Who::PBP, Kind::Live, pick("k_fgset", {"Fourth down, and they'll send out kicker " + kn + " for a " + bS(dist) + "-yard attempt.", "Kicker " + kn + " lines up from " + bS(dist) + " yards.",
                                                          "Here's " + kn + ", from " + bS(dist) + " yards."}),
                     1, 5, 0, presented ? 4.0 : 6);
      l.live = true;
      l.tag = "kset";
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
    if (good) push(Who::PBP, Kind::Score, scoreSay(g, tdBy_, pick("k_pat", {"The extra point is good. ", "And the kick is good. ", "Extra point's good. "})), 0, 4, 0.1, 15).snapSafe = true;
    else {
      push(Who::PBP, Kind::Score, scoreSay(g, tdBy_, "And the extra point is no good! "), 2, 4, 0.1, 15).snapSafe = true;
      push(Who::Color, Kind::Analysis, "That point could loom large. You just don't miss those.", 1, 3, 0, 15);
    }
    return;
  }
  if (bHas(d, "field goal")) {
    const int dist = std::atoi(d.c_str() + d.find(' ') + 1);
    if (bHas(d, "is GOOD")) {
      const TeamKey scorer = g.kicking;  // a good field goal: the scoring team kicks off next
      push(Who::PBP, Kind::Score, scoreSay(g, scorer, dist >= 50 ? "It's good! From " + bS(dist) + " yards! " : pick("k_fggood", {"It's good! ", "Right down the middle! ", "And it's good! "})),
           dist >= 50 ? 3 : 2, 5, 0.05, 15)
          .snapSafe = true;
      if (dist >= 50) push(Who::Color, Kind::Analysis, pick("k_fglong", {"That's a big-time kick. " + bS(dist) + " yards, and it had plenty of leg.", "From " + bS(dist) + "! He hit that one pure."}), 1, 3, 0, 15);
      PlayResult none;
      onScored(g, scorer, 3, none, false);
    } else {
      const std::string how = bHas(d, "BLOCKED") ? "It's blocked!" : bHas(d, "right") ? "No good! Wide right." : "No good! It hooks wide left.";
      push(Who::PBP, Kind::Result, how, 3, 5, 0.05, 15).snapSafe = true;
      push(Who::Color, Kind::Analysis, bHas(d, "BLOCKED") ? std::string("They got penetration up the middle. The kick had no chance.")
                                                           : pick("k_fgmiss", {"He pulled it. You could see it right off his foot.", "Points left on the field, and in a game like this you can't afford it."}),
           1, 3, 0, 15);
      drive_.active = false;
    }
    return;
  }
  if (bHas(d, "onside")) {
    const bool kept = bHas(d, "RECOVERED by the kicking team");
    push(Who::PBP, Kind::Result, kept ? "Onside kick... and they've got it! The kicking team recovers!" : "Onside kick, and it's recovered by the receiving team.", kept ? 3 : 1, 5, 0, 10)
        .snapSafe = true;
    return;
  }
  if (bHas(d, "punt") && bHas(d, "BLOCKED")) {
    push(Who::PBP, Kind::Result, "Blocked! The punt is blocked!", 3, 5, 0, 10).snapSafe = true;
    return;
  }
  if (bHas(d, "MUFFED")) {
    push(Who::PBP, Kind::Result, "He muffs it! Loose ball! And the kicking team comes up with it!", 3, 5, 0, 10).snapSafe = true;
    return;
  }
  if (bHas(d, "FUMBLES")) {
    push(Who::PBP, Kind::Result, "Fumble on the return! And the kicking team has it!", 3, 5, 0, 10).snapSafe = true;
    return;
  }
  if (bHas(d, "TOUCHDOWN")) {
    push(Who::PBP, Kind::Result, pick("k_rettd", {"He's gone! All the way back for a touchdown!", "Touchdown on the return! What a play!"}), 3, 5, 0, 10).snapSafe = true;
    return;
  }
  const std::string at = spot(g, g.ballOn, g.poss);
  if (bHas(d, "Touchback")) {
    push(Who::PBP, Kind::Result, pick("k_tb", {"Into the end zone, touchback.", "That one's a touchback.", "Down in the end zone. " + g.offense().city + " starts at its own " + bS(bRound(g.ballOn)) + "."}), 0, 3, 0, 8);
    return;
  }
  if (bHas(d, "fair catch")) {
    push(Who::PBP, Kind::Result, pick("k_fc", {"Fair catch at " + at + ".", "He calls for the fair catch at " + at + "."}), 0, 3, 0, 8);
    return;
  }
  if (bHas(d, "downed")) {
    push(Who::PBP, Kind::Result, pick("k_downed", {"And it's downed at " + at + ". Great punt.", "Downed at " + at + ".", "The coverage team gets there, and it's downed at " + at + ".",
                                                   "That one dies at " + at + "."}),
         1, 3, 0, 8);
    if (g.ballOn <= 10)
      push(Who::Color, Kind::Analysis, pick("k_pinned", {"That's a weapon. Pinned them inside the ten, now they've got a long way to go.",
                                                         "Great coverage on the punt. Now they're backed up, and the defense can play aggressive.",
                                                         "Field position. That's how you win a game like this.", "That punt team doesn't get enough credit. Look where the ball is."}),
           1, 2, 0, 12);
    return;
  }
  if (bHas(d, "returned")) {
    push(Who::PBP, Kind::Result, pick("k_ret", {"Brought back to " + at + ".", "Returned out to " + at + "."}), 1, 3, 0, 8);
    return;
  }
}

// ------------------------------------------------------------------ before the snap

void Booth::onPresnapEnter(const Game& g, const View& v) {
  presnapSaid_ = true;
  if (g.phase == GamePhase::Pat) {
    push(Who::PBP, Kind::Setup, pick("2pt", {g.offense().city + " is going to go for two.", "They're staying on the field. They'll go for the two-point conversion."}), 1, 3, 0, 10);
    return;
  }
  const PlaySim* s = sim_;
  const OffPlay* op = s ? s->cfg.offPlay : nullptr;
  if (op && op->type == PlayType::Kneel) {
    if (g.q == 2) {
      if (once("kneelhalf" + bS(g.q))) push(Who::PBP, Kind::Setup, "They'll take a knee and run out the half.", 0, 3, 0, 6);
    } else if (once("kneelgame")) {
      push(Who::PBP, Kind::Setup, pick("kneelset", {"And here comes the victory formation.", "They'll take a knee, and this one is over."}), 0, 3, 0, 6);
    }
    return;
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
    s = pick("set_4go", {"Fourth and " + bDist(g.toGo, g.ballOn) + ", and they're going for it.", "They're staying on the field. Fourth and " + bDist(g.toGo, g.ballOn) + " " + where + ".",
                         O.city + " will go for it on fourth and " + bDist(g.toGo, g.ballOn) + "."});
  } else if (takeover && g.down == 1) {
    s = pick("set_take", {O.city + " takes over, " + dd + " " + where + ".", O.city + " ball, " + dd + " " + where + ".", "The " + nick(O) + " take over. " + bCap(dd) + " " + where + ".",
                          "Here comes the " + nick(O) + " offense. " + bCap(dd) + " " + where + "."});
    if (g.margin() != 0 && chance(0.6)) s += " They're " + situation(g) + ".";
  } else if (newQ) {
    s = pick("set_newq", {"And we're underway in the " + std::string(bQuarterName(g.q)) + ". " + O.city + ", " + dd + " " + where + ".",
                          "To start the " + std::string(bQuarterName(g.q)) + ", " + O.city + " with a " + dd + " " + where + "."});
  } else if (late) {
    s = pick("set_late", {bCap(dd) + " " + where + ", " + bClock(g.clock) + " to go.", bCap(dd) + ", " + bClock(g.clock) + " left, " + O.city + " " + situation(g) + "."});
    // the timeouts that matter: the trailing team's (the leader's clock is the defense's problem)
    const TeamKey trail = g.margin() <= 0 ? g.poss : other(g.poss);
    const int to = g.timeouts[trail];
    if (g.q >= 4 && close && chance(0.6))
      s += trail == g.poss ? " They have " + std::string(bCountWord(to)) + (to == 1 ? " timeout." : " timeouts.")
                           : " " + g.teamOf(trail).city + " has " + bCountWord(to) + (to == 1 ? " timeout left." : " timeouts left.");
  } else if (g.down == 1 && g.toGo >= 10 && yl < 80 && chance(0.35)) {
    s = pick("set_1st", {O.city + " with a first down " + where + ".", "First and ten " + where + ".", bCap(dd) + " " + where + "."});
  } else {
    s = pick("set", {bCap(dd) + " " + where + ".", bCap(dd) + " for " + O.city + ", " + where + ".", O.city + ", " + dd + " " + where + "."});
  }
  // the formation, now and then
  if (full && sim_ && sim_->qb && chance(0.35)) {
    const Formation* f = sim_->cfg.offPlay ? formationById(sim_->cfg.offPlay->formation) : nullptr;
    int rbs = 0, tes = 0, wrs = 0;
    for (const Ent* e : sim_->off) {
      if (e->p->pos == Pos::RB || e->p->pos == Pos::FB) ++rbs;
      if (e->p->pos == Pos::TE) ++tes;
      if (e->p->pos == Pos::WR) ++wrs;
    }
    const std::string qb = nm(sim_->qb->p);
    if (rbs == 0) s += " Empty backfield, " + qb + " alone in the gun.";
    else if (tes >= 3 || (f && formationFamily(*f) == "Goal Line")) s += " Heavy set, three tight ends.";
    else if (f && formationFamily(*f) == "Shotgun") s += " " + qb + " in the shotgun.";
    else if (f && formationFamily(*f) == "Pistol") s += " " + qb + " in the pistol.";
    else if (f && (formationFamily(*f) == "I-Form" || bHas(formationFamily(*f), "I"))) s += " " + qb + " under center, two backs in the I.";
    else if (wrs >= 4) s += " Four wide.";
    else s += " " + qb + " under center.";
  }
  // the streak that makes this snap a story
  if (full && sim_ && sim_->qb && chance(0.3)) {
    auto it = qbStreak_.find(sim_->qb->p->id);
    if (it != qbStreak_.end() && it->second.completions >= 6 && cooled("streak", 200)) {
      mark("streak");
      s += " " + nm(sim_->qb->p) + " has completed " + bS(it->second.completions) + " straight.";
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
  // a man the defense will send, creeping down
  if (blitz && (s.shellAdj == PlaySim::ShellAdj::ShowBlitz || chance(0.5))) {
    const Ent* b = s.defRole(dc->blitz.front());
    if (b) add(1.2, "r_blitz", pick("r_blitz", {"Watch " + nm(b->p) + ". He's creeping down, I think he's coming.", "They're going to bring it. Look at " + nm(b->p) + " walking up into the A gap.",
                                                "I've got a feeling " + D.city + " is sending pressure here. " + nm(b->p) + " is cheating up."}));
  }
  if (!blitz && s.shellAdj == PlaySim::ShellAdj::ShowBlitz)
    add(1.1, "r_fake", "They're showing blitz, but watch these guys bail out at the snap. They want him to panic.");
  if (bTwoHigh(dc->coverage) && passDown)
    add(0.9, "r_2high", pick("r_2high", {"Two deep safeties. They're going to make him check it down and tackle the catch.",
                                          "Look at the shell. Two high, everything in front of them. Take the underneath stuff.",
                                          "They're keeping a lid on it. Nobody's getting behind this secondary on third and long.",
                                          "Split safeties. " + D.city + " is daring them to be patient."}));
  if (dc->coverage == Coverage::C0) add(1.3, "r_c0", "Nobody deep! They're in Cover Zero, it's all man, and somebody's going to be one-on-one.");
  // a one-on-one matchup the alignment gives away
  if (bManCov(dc->coverage)) {
    for (const Ent* e : s.off)
      if (e->p->pos == Pos::WR && e->eligible) {
        for (const Ent* d : s.def)
          if (d->man == e && (d->p->pos == Pos::CB || d->p->pos == Pos::S)) {
            const PlayerLine& ln = const_cast<StatBook&>(g.stats).line(*e->p, O.abbr);
            if (ln.rec >= 3) {
              readSubj_["r_match" + bS(e->p->id)] = e->p->id;
              add(1.0, "r_match" + bS(e->p->id), nm(e->p) + " is going to see man coverage from " + nm(d->p) + ". He's already got " + bS(ln.rec) + " catches today. I'd go right back to him.", 400);
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
  if (rbs == 0) add(0.9, "r_empty", "Empty backfield. Nobody to help in protection, so " + (s.qb ? nm(s.qb->p) : std::string("the quarterback")) + " has to get it out quick. Hot reads.");
  if (tes >= 3 && g.toGo <= 3) add(1.0, "r_heavy", "Big personnel. They're telling you they're going to run it, and daring " + D.city + " to stop it.");
  const bool risky4 = g.down == 4 && g.toGo >= 3 && g.ballOn < 50 && tier(g) == 0 && !(g.q >= 4 && g.margin() < 0);
  if (risky4) {
    add(2.2, "r_4th", pick("r_4thno", {"I don't love this, " + crew_.pbpFirst + ". Fourth and " + bS(bRound(g.toGo)) + " in your own end? If you don't get it, you've handed them points.",
                                       "That's a gamble. Their own side of the field, and they need " + bS(bRound(g.toGo)) + ". I'm punting."}),
        0);
    risky4th_ = true;
  } else if (g.down == 4)
    add(2.0, "r_4th", g.margin() <= -9 && g.q >= 4 ? "They don't have a choice here. Down " + bS(-g.margin()) + ", you have to go for it."
                      : tier(g) >= 1 ? std::string("Not much to decide here. They're just playing it out.")
                                     : pick("r_4th", {"I love this decision. You're " + std::string(g.ballOn >= 50 ? "in their territory" : "near midfield") + ", you have to be aggressive.",
                                                      "This is the game right here. If they don't get it, " + D.city + " has a short field."}),
        0);
  if (g.ballOn >= 90) add(0.8, "r_goal", "Everything gets tight down here. No room to throw it over the top, so it's about winning at the line.");
  if (passDown && D.blitzLean > 0.33) add(0.8, "r_tend", D.city + " loves to bring pressure on third down. Watch for the blitz.", 500);
  if (passDown && O.passLean > 0.6) {
    // the offense's own identity, named (a bare "they spread you out" sounded like the other team's scheme)
    const bool spread = bHas(O.offScheme, "Air Raid") || bHas(O.offScheme, "Spread") || bHas(O.offScheme, "Run and Shoot");
    add(0.6, "r_tend2" + O.abbr, spread ? "This is what the " + O.offScheme + " is built for. They spread you out and make you cover everybody."
                                        : O.city + " is going to throw it here. They put it in the quarterback's hands on third and long.", 900);
  }
  // the shell: what the safeties tell you
  const std::string qbn = s.qb ? nm(s.qb->p) : std::string("the quarterback");
  if (dc->coverage != Coverage::C0) {
    if (bTwoHigh(dc->coverage))
      add(0.65, "r_shell2", pick("r_shell2", {"Two safeties deep. They're taking away the shot play, so " + qbn + " has to be patient.", "Split safeties. Everything is in front of them on this one.",
                                               "Two-high look. That's an invitation to run the football."}), 240);
    else
      add(0.65, "r_shell1", pick("r_shell1", {"Single-high safety. That's man or Cover 3, and the seams are there if he has time.", "One deep safety, eight guys near the box. They want to stop the run first.",
                                               "Middle of the field closed. Watch the outside matchups."}), 240);
  }
  // the men: who is where (names make every read its own sentence)
  {
    const Ent* fs = s.defRole("FS");
    const Ent* ss = s.defRole("SS");
    if (!bTwoHigh(dc->coverage) && dc->coverage != Coverage::C0 && fs)
      add(0.7, "r_fs", pick("r_fs", {nm(fs->p) + " is the only man deep. Everything over the top is on him.", "Watch " + nm(fs->p) + " in the middle of the field. He's the last line.",
                                      nm(fs->p) + " is sitting at about fifteen yards. Single high."}),
          600);
    if (bTwoHigh(dc->coverage) && fs && ss)
      add(0.7, "r_ss", pick("r_ss", {nm(fs->p) + " and " + nm(ss->p) + " both deep. They don't want to give up anything big.", "Look at " + nm(ss->p) + ", he's backed way off. Two-deep shell."}), 600);
    for (const Ent* d : s.def)
      if (d->man && d->man->eligible && (d->p->pos == Pos::CB) && (s.shellAdj == PlaySim::ShellAdj::Press || bManCov(dc->coverage))) {
        readSubj_["r_press"] = d->man->p->id;
        add(0.75, "r_press", pick("r_press", {nm(d->p) + " is right up in " + nm(d->man->p) + "'s face. Press man.", "Look at " + nm(d->p) + ", he's going to jam " + nm(d->man->p) + " at the line.",
                                              "It's " + nm(d->p) + " on " + nm(d->man->p) + ", one on one."}),
            600);
        break;
      }
    const Ent* slot = s.offRole("SLOT");
    const Ent* nb = s.defRole("NB");
    if (slot && nb && passDown) readSubj_["r_slot"] = slot->p->id;
    if (slot && nb && passDown) add(0.7, "r_slot", pick("r_slot", {"Watch " + nm(slot->p) + " in the slot against " + nm(nb->p) + ". That's a matchup they like.", nm(nb->p) + " has " + nm(slot->p) + " inside. Quickness on quickness."}), 600);
    const Ent* rb = s.offRole("RB");
    if (rb && passDown && g.toGo >= 7)
      add(0.65, "r_rbpro", pick("r_rbpro", {"If they blitz, " + nm(rb->p) + " has to pick it up. That's the hardest job a back has.", nm(rb->p) + " stays in to help in protection, I'd bet."}), 1e9);
  }
  // personnel
  int wrs = 0;
  for (const Ent* e : s.off) wrs += e->p->pos == Pos::WR ? 1 : 0;
  if (wrs >= 4) add(0.6, "r_4wide", pick("r_4wide", {"Four wide. They're spreading " + D.city + " out to see what the defense shows them.", "They've gone to four receivers. That's a lot of space to cover."}), 400);
  if (tes >= 2 && g.toGo >= 5) add(0.6, "r_2te", pick("r_2te", {"Two tight ends. That's a play-action look. Watch the linebackers' eyes.", "Heavy personnel on a passing down. They might be setting up the boot."}), 400);
  // the situation
  if (g.down == 3 && g.toGo <= 2) add(0.8, "r_3short", pick("r_3short", {"Third and short. Somebody has to win his one-on-one up front.", "Short yardage. Watch for the quarterback sneak.",
                                                                          "This is about the push. Whoever gets lower wins this down."}), 300);
  if (g.down == 2 && g.toGo >= 8) add(0.6, "r_2long", pick("r_2long", {"Second and long. You want half of it back here to make third down manageable.", "They need a chunk here, or it's third and long."}), 400);
  if (g.ballOn >= 80 && g.ballOn < 90) add(0.7, "r_rz", pick("r_rz", {"Red zone. The field shrinks, so the windows get tight.", "Inside the 20, everybody's within reach. This is where a big back earns his money."}), 400);
  // the offense's habits so far
  {
    const TeamLine& tl = g.team[g.poss];
    if (tl.plays >= 12) {
      const int runs = tl.rushAtt, passes = tl.passAtt;
      if (runs >= passes * 1.5)
        add(0.75, "r_runtend", O.city + " has run it " + bS(runs) + " times to " + bS(passes) + " passes. Everybody in the stadium knows what's coming, and they still have to stop it.", 900);
      else if (passes >= runs * 2)
        add(0.75, "r_passtend", O.city + " has thrown it " + bS(passes) + " times and run it just " + bS(runs) + ". They're living on " + qbn + "'s arm.", 900);
    }
  }
  if (c.empty()) return std::nullopt;
  std::sort(c.begin(), c.end(), [](const C& a, const C& b) { return a.sc > b.sc; });
  if (c[0].sc < 0.7 && chance(0.3)) return std::nullopt;
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
  // late in a half or a decided fourth quarter: the clock and what each side needs, not stat lines
  if (((g.q == 2 || g.q >= 4) && g.clock < 120) || (g.q >= 4 && tier(g) >= 1)) return;
  if (auto s = storyLine(g, who)) {
    lastStoryT_ = now_;
    push(who, Kind::Story, *s, 1, 2, 0, 10);
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
    if (!text.empty() && cooled(topic, cool) && !spokenSet_.count(text)) c.push_back({sc + rng_.range(0, 0.4), topic, text, w});
  };
  const Team& O = g.offense();
  const Team& D = g.defense();
  const TeamKey ok = g.poss, dk = other(g.poss);
  const TeamLine& to = g.team[ok];
  const TeamLine& td = g.team[dk];
  // the quarterback's day
  for (const PlayerLine* ln : g.stats.forTeam(O.abbr)) {
    if (ln->passAtt >= 8)
      add(0.9, "qb:" + bS(ln->pid),
          pick("s_qb", {lineName(g, *ln) + " is " + bS(ln->passComp) + " of " + bS(ln->passAtt) + " today for " + bS(ln->passYds) + " yards" +
                            (ln->passTD ? ", " + std::string(bCountWord(ln->passTD)) + (ln->passTD == 1 ? " touchdown" : " touchdowns") : std::string()) +
                            (ln->passInt ? ", " + std::string(bCountWord(ln->passInt)) + (ln->passInt == 1 ? " interception." : " interceptions.") : std::string(".")),
                        lineName(g, *ln) + " is now " + bS(ln->passComp) + " for " + bS(ln->passAtt) + ", " + bS(ln->passYds) + " yards" +
                            (ln->passComp > 0 && ln->passYds / static_cast<double>(ln->passComp) < 6.5 ? ". A lot of short stuff." : ".")}),
          Who::PBP, 360);
    if (ln->rushAtt >= 8) {
      const double ypc = ln->rushYds / static_cast<double>((std::max)(1, ln->rushAtt));
      const bool chasing = g.margin() <= -9 && g.q >= 3;  // down two scores late, the run is not the plan any more
      std::string tail = ypc >= 5.0 ? (chasing ? pick("rb_hot2", {"He's been their best weapon. They just don't have the time to lean on him now.", "Good numbers, but the scoreboard says they have to throw."})
                                               : pick("rb_hot", {"That's over five a pop. They have to keep feeding him.", "He's been the engine of this offense.", "Every time they hand it to him, something good happens."}))
                         : ypc < 3.2 ? pick("rb_cold", {"They just haven't been able to get him going.", "The front has been winning that battle all day.", "There's been nothing there for him."})
                                     : pick("rb_mid", {"Steady.", "Nothing flashy, but he keeps them on schedule.", "Workmanlike."});
      add(0.8, "rb:" + bS(ln->pid), lineName(g, *ln) + " has " + bS(ln->rushYds) + " yards on " + bS(ln->rushAtt) + " carries. " + tail, Who::Color, 900);
    }
    if (ln->rec >= 4 && !(lastBig_ == ln->pid)) {
      const double ypr = ln->recYds / static_cast<double>(ln->rec);
      int most = 0;
      for (const PlayerLine* o : g.stats.forTeam(O.abbr)) most = (std::max)(most, o->rec);
      if (ln->rec < most) continue;
      add(0.7, "wr:" + bS(ln->pid),
          ypr >= 9 ? lineName(g, *ln) + " has been " + O.city + "'s go-to guy. " + bS(ln->rec) + " catches, " + bS(ln->recYds) + " yards."
                   : bS(ln->rec) + " catches for " + lineName(g, *ln) + ", but only " + bS(ln->recYds) + " yards. They just can't get him going down the field.",
          ypr >= 9 ? Who::PBP : Who::Color, 420);
    }
  }
  for (const PlayerLine* ln : g.stats.forTeam(O.abbr))
    if (ln->passInt >= 2)
      add(0.95, "qbint:" + bS(ln->pid) + ":" + bS(ln->passInt),
          lineName(g, *ln) + " has thrown " + bCountWord(ln->passInt) + " interceptions today. " + (g.margin() >= 0 ? std::string("They're winning in spite of it.") : std::string("That's why they're behind.")),
          Who::Color, 1e9);
  for (const PlayerLine* ln : g.stats.forTeam(D.abbr)) {
    if (ln->sacks >= 2)
      add(1.0, "sk:" + bS(ln->pid) + ":" + bS(ln->sacks),
          once("sk1:" + bS(ln->pid)) ? lineName(g, *ln) + " already has " + bCountWord(ln->sacks) + " sacks today. He's been living in the backfield."
                                     : "That's " + std::string(bCountWord(ln->sacks)) + " sacks now for " + lineName(g, *ln) + ". They still haven't found an answer for him.",
          Who::Color, 1e9);
    if (ln->ints >= 1 && ln->tackles >= 3) add(0.6, "dbint:" + bS(ln->pid), lineName(g, *ln) + " has been everywhere. The interception, and " + bS(ln->tackles) + " tackles.", Who::PBP, 600);
  }
  // the teams
  if (to.thirdAtt >= 4 && (tier(g) == 0 || g.margin() < 0))
    add(0.8, "3rd:" + O.abbr, O.city + " is " + bS(to.thirdConv) + " of " + bS(to.thirdAtt) + " on third down. " +
                                   (to.thirdConv * 2 >= to.thirdAtt ? pick("3rd_good", {"That's how you stay on the field.", "They've been money on the money down."})
                                    : g.margin() > 0               ? std::string("They've been getting by on the other side of the ball.")
                                                                   : pick("3rd_bad", {"They've got to be better than that.", "That's why they're chasing.", "That's been the difference in this game."})),
        Who::PBP, 1e9);
  if (to.plays + td.plays >= 30 && std::abs(to.yards - td.yards) >= 90)
    add(0.7, "yds", (to.yards > td.yards ? O.city : D.city) + " has out-gained them " + bS((std::max)(to.yards, td.yards)) + " to " + bS((std::min)(to.yards, td.yards)) + ".", Who::Color,
        700);
  if (to.penalties + td.penalties >= 6) add(0.6, "pens", "Lots of flags in this one. " + bS(to.penalties + td.penalties) + " penalties so far.", Who::PBP, 900);
  const int toG = giveaways(g, to), tdG = giveaways(g, td);
  if (tdG >= 2 || toG >= 2) {
    const bool offBad = toG >= tdG;
    const int badMargin = offBad ? g.margin() : -g.margin();
    add(0.8, "tos", (offBad ? O.city : D.city) + " has turned it over " + ((offBad ? toG : tdG) == 2 ? std::string("twice") : std::string(bCountWord(offBad ? toG : tdG)) + " times") + ". " +
                        (badMargin > 0 ? std::string("They're lucky to still be in front.") : std::string("You can't win like that.")),
        Who::Color, 900);
  }
  if (blitzCount_[dk == TeamKey::Home ? 0 : 1] >= 6 || blitzCount_[ok == TeamKey::Home ? 0 : 1] >= 6) {}
  // the weather and the building
  const Weather& w = g.weather;
  if (w.precip == Precip::Snow) add(0.5, "wx", "Look at that snow coming down. Footing has to be a real issue for the receivers.", Who::Color, 900);
  if (w.precip == Precip::Rain) add(0.5, "wx", "This rain hasn't let up. Every snap, you're worried about the exchange.", Who::Color, 900);
  if (w.tempF <= 30) add(0.4, "wx", "You can see their breath out there. This is a cold one.", Who::PBP, 1200);
  if (g.q == 4 && std::abs(g.margin()) <= 7 && g.clock < 480 && tier(g) == 0)
    add(1.0, "tight4", pick("s_tight", {"This one is going to come down to the wire.", "Every snap matters now. One play could decide it."}), Who::PBP, 400);
  if (g.q >= 3 && std::abs(g.margin()) >= 17)
    add(0.5, "blowout", D.city + " has to find something here. They're running out of time to get back into it.", Who::Color, 900);
  if (c.empty()) return std::nullopt;
  std::sort(c.begin(), c.end(), [](const C& a, const C& b) { return a.sc > b.sc; });
  mark(c[0].topic);
  who = c[0].who;
  // a quarterback's line from the play-by-play man: the analyst says what it means
  storyReply_.clear();
  if (c[0].topic.rfind("qb:", 0) == 0) {
    const int pid = std::atoi(c[0].topic.c_str() + 3);
    for (const PlayerLine* ln : g.stats.forTeam(O.abbr))
      if (ln->pid == pid && ln->passAtt > 0) {
        const double pct = ln->passComp / static_cast<double>(ln->passAtt), ypa = ln->passYds / static_cast<double>(ln->passAtt);
        if (ln->passInt >= 2) storyReply_ = pick("qbr_int", {"The interceptions are what you worry about. You can't keep giving them extra possessions.", bCap(bCountWord(ln->passInt)) + " picks. He's been forcing it."});
        else if (pct >= 0.7 && ln->passInt == 0) storyReply_ = pick("qbr_hot", {"He's been surgical. Getting the ball out on time, hitting guys in stride.", "He's in a rhythm. They're not going to stop him unless they get pressure."});
        else if (pct < 0.5) storyReply_ = pick("qbr_cold", {"He's been off. Some of that's the pressure, some of it is just missing throws he usually makes.", "That's not his day. They've got to find him something easy to get him going."});
        else if (ypa < 5.5 && pct >= 0.6) storyReply_ = pick("qbr_short", {"Efficient, but it's all underneath. They're not taking any shots.", "They're keeping it short. That's fine, until you need a big play."});
      }
    if (chance(0.4)) storyReply_.clear();  // not every time
  }
  return c[0].text;
}

std::optional<std::pair<std::string, std::string>> Booth::exchange(const Game& g) {
  if (g.shouldKneel()) return std::nullopt;  // (they're kneeling it out: no talk of what the next down needs)
  const std::string cf = crew_.colorFirst;
  const Team& O = g.offense();
  const Team& D = g.defense();
  const TeamLine& to = g.team[g.poss];
  const double ypc = to.rushYds / static_cast<double>((std::max)(1, to.rushAtt));
  // third and short: run it or throw it
  if (g.down == 3 && g.toGo <= 2 && tier(g) == 0 && cooled("x_3short", 400) && chance(0.6)) {
    mark("x_3short");
    return std::make_pair(pick("x3sq", {"Run it or throw it here, " + cf + "?", "What do you call here, " + cf + "?", cf + ", third and short. What's your play?"}),
                          ypc >= 4.2 ? pick("x3sa", {"I run it. They've been getting " + bS(static_cast<int>(ypc * 10) / 10) + " a carry, and the line has been winning up front all day.",
                                                     "Hand it off. Their offensive line has earned the right to that call today."})
                                     : pick("x3sb", {"I'm throwing it. The run game hasn't been there, so play-action, quick throw to the flat, move the sticks.",
                                                     "Not the run. They've been stuffed all day. Get the ball out of his hand quick."}));
  }
  // the red zone
  if (g.ballOn >= 80 && g.down == 1 && tier(g) == 0 && cooled("x_rz", 600) && chance(0.5)) {
    mark("x_rz");
    return std::make_pair(pick("xrzq", {"How do you attack this defense down here, " + cf + "?", "Red zone, " + cf + ". What are you looking for?"}),
                          D.blitzLean > 0.32 ? std::string("They're going to bring pressure. So you want quick game, slants, the back in the flat. Make them pay for sending people.")
                                             : std::string("They'll sit in zone and squeeze the field. I'd use the tight end, he's a matchup problem for linebackers in tight space."));
  }
  // third and long coming up: what does the analyst want to see
  if (g.down == 3 && g.toGo >= 7 && tier(g) == 0 && cooled("x_3long", 600) && chance(0.4)) {
    mark("x_3long");
    return std::make_pair(pick("x3q", {"Big down here, " + cf + ".", cf + ", what are you looking for on third and long?"}),
                          D.blitzLean > 0.3 ? "They've been bringing pressure all day. If I'm the quarterback, I know where my hot read is before the snap."
                                            : "They're going to sit back and make him be patient. Take what's underneath, get to the sticks.");
  }
  if (to.plays >= 20 && to.rushAtt >= 10 && once("x_run" + O.abbr) && chance(0.6)) {
    const bool behind = g.margin() <= -14;
    return std::make_pair("How has " + O.city + "'s running game looked to you, " + cf + "?",
                          ypc >= 4.5 && !behind ? "Really good. " + bS(to.rushYds) + " yards on the ground, and that keeps the defense honest. The safeties have to come down, and that's where the shots come from."
                          : behind ? "It's been fine, " + bS(to.rushYds) + " yards, but down " + bS(-g.margin()) + " you can't lean on it anymore. They have to throw."
                                   : "Not much there. " + bS(to.rushYds) + " yards on " + bS(to.rushAtt) + " carries. But they're staying with it to keep the defense honest.");
  }
  if (g.q == 4 && g.clock < 360 && std::abs(g.margin()) <= 8 && g.margin() < 0 && g.phase == GamePhase::Scrimmage && g.down == 1 && cooled("x_late", 900)) {
    mark("x_late");
    return std::make_pair(O.city + " needs a drive here, " + cf + ".",
                          "Stay calm. You've got " + bClock(g.clock) + " and " + bCountWord(g.timeouts[g.poss]) + " timeouts, that's plenty. Don't try to get it all on one play.");
  }
  return std::nullopt;
}

std::optional<std::string> Booth::quarterStory(const Game& g) {
  const TeamLine& h = g.team.home;
  const TeamLine& a = g.team.away;
  const Team& H = *g.home;
  const Team& A = *g.away;
  const int m = g.score.home - g.score.away;
  if (std::abs(h.yards - a.yards) >= 60 && once("qcontrol"))
    return (h.yards > a.yards ? H.city : A.city) + " has controlled it so far. " + bS((std::max)(h.yards, a.yards)) + " yards of offense to " + bS((std::min)(h.yards, a.yards)) + ".";
  if (giveaways(g, h) + giveaways(g, a) >= 2)
    return std::string("The turnovers have been the story. ") + H.city + " has turned it over " + bCountWord(giveaways(g, h)) + (giveaways(g, h) == 1 ? " time, " : " times, ") + A.city + " " +
           bCountWord(giveaways(g, a)) + (giveaways(g, a) == 1 ? " time." : " times.");
  if (std::abs(m) >= 9) return (m > 0 ? H.city : A.city) + " has made the plays. That's a " + std::string(std::abs(m) >= 17 ? "three-score" : "two-score") + " lead already.";
  return pick("qstory", {"Pretty even so far. Neither team has really been able to impose its will.", "It's been a chess match. Both defenses have come to play."});
}

std::optional<std::string> Booth::halftimeStory(const Game& g) {
  const TeamLine& h = g.team.home;
  const TeamLine& a = g.team.away;
  const bool homeLead = g.score.home >= g.score.away;
  const Team& L = homeLead ? *g.home : *g.away;
  const TeamLine& lt = homeLead ? h : a;
  const TeamLine& tt = homeLead ? a : h;
  const int m = std::abs(g.score.home - g.score.away);
  std::string s = "Big picture: ";
  if (lt.yards > tt.yards + 50) s += L.city + " has moved the ball at will, " + bS(lt.yards) + " total yards. ";
  else if (tt.yards > lt.yards + 50) s += "the other side has out-gained them, but " + L.city + " has made the plays that count. ";
  else if (m >= 9) s += "the yardage is close, but " + L.city + " has turned its chances into points. ";
  else s += "this has been an even half. ";
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
    if (lead && lead->pid != openKey_->id && (!kl || lead->sacks > kl->sacks))
      s += "I said before the game " + openKey_->first + " " + openKey_->last + " would be the problem. It's been " + leadName(g, *lead) + " instead, " + bS(lead->sacks) + " sacks already. ";
    else if (kl && kl->sacks >= 2) s += "And I said before the game " + nm(openKey_) + " could wreck the game plan. " + bS(kl->sacks) + " sacks already. ";
    else if (kl && kl->sacks == 1) s += nm(openKey_) + " has a sack, and he's been a factor. ";
    else if (kl && kl->tackles >= 4) s += nm(openKey_) + " has been in on " + bS(kl->tackles) + " tackles. ";
    else s += "And they've kept " + nm(openKey_) + " quiet. I didn't expect that. ";
  }
  if (giveaways(g, lt) + giveaways(g, tt) > 0) s += "And the turnovers, " + bS(giveaways(g, tt)) + " to " + bS(giveaways(g, lt)) + ". ";
  s += pick("half2", {"Whoever wins the line of scrimmage in the third quarter wins this game.", "The adjustments at halftime are going to be huge."});
  return s;
}

void Booth::onFinal(const Game& g) {
  q_.clear();
  const bool homeWin = g.score.home > g.score.away;
  const Team& W = homeWin ? *g.home : *g.away;
  const Team& L = homeWin ? *g.away : *g.home;
  const int ws = (std::max)(g.score.home, g.score.away), ls = (std::min)(g.score.home, g.score.away);
  push(Who::PBP, Kind::Close,
       ws == ls ? "And that's the final. " + scoreLine(g, false)
                : pick("final", {"And that's going to do it! The " + W.name + " win it, " + bS(ws) + " to " + bS(ls) + ".",
                                 "The final from " + g.home->city + ": " + W.city + " " + bS(ws) + ", " + L.city + " " + bS(ls) + "."}),
       2, 5, 0.6, 60)
      .snapSafe = true;
  pendingInjury_.clear();  // (the game is over: a man hurt is for the postgame, not the final call)
  // the plays that decided it
  if (!bigPlays_.empty()) {
    std::string r = "What a game. ";
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
      push(Who::Color, Kind::Close, "The story today was " + full + ". " + line + ". He was the difference.", 1, 4, 0, 60).snapSafe = true;
    }
  }
  push(Who::PBP, Kind::Close, "For " + crew_.colorFirst + " " + crew_.colorLast + ", I'm " + crew_.pbpFirst + " " + crew_.pbpLast + ". Thanks for joining us.", 0, 4, 0, 60).snapSafe = true;
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
