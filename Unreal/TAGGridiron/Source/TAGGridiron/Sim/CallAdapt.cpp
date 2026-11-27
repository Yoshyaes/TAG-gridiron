// Adaptive play calling and the coach's suggestions (CallAdapt.h).
#include "CallAdapt.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "Game.h"
#include "PlaySim.h"
#include "Rng.h"

namespace tag {

namespace calladapt_detail {

// ---- the league's own mix and the matchups (Tools/CallAdaptCal.cpp prints these; regenerate after a playbook or
// play-calling change)
#include "CallAdaptTables.inl"

// ---- per concept (Tools/CallAdaptCal --concepts, Tools/Realism/concept_tables.py): what each concept gains against
// each kind of call beyond its own average - only the edges past two standard errors, the rest 0. The kind-level kMatch
// can point the wrong way for one concept (man coverage takes 0.06 off intermediate passes in general and gave the
// spammed China / Drive plays 0.21 a play more than Cover 3 did)
struct ConceptEdge {
  const char* key;
  double d[kFamN];
};
#include "CallAdaptConcepts.inl"

const double* conceptEdges(const std::string& key) {
  static const std::map<std::string, const double*> m = [] {
    std::map<std::string, const double*> o;
    // (a concept with no edge past the noise has no entry: its kind's matchups answer it)
    for (const ConceptEdge& e : kConceptEdge) {
      bool any = false;
      for (double v : e.d) any = any || v != 0.0;
      if (e.key[0] && any) o[e.key] = e.d;
    }
    return o;
  }();
  const auto it = m.find(key);
  return it == m.end() ? nullptr : it->second;
}

// (how strongly a score in expected points a play turns into a weight: a defensive call that takes away 0.045 more
// than the league's mix would gets ~x2 at the default gains. The offense leans less: "call what works" picks up the
// noise of a few plays, and at the defense's gain it lifted the league's yards a play by 0.1)
constexpr double kDefScale = 16.0;
constexpr double kOffScale = 5.0;
constexpr double kPassScale = 0.6;

double mulFor(double score, double scale) {
  const double mx = (std::max)(1.0, callTune().adaptMax);
  return clamp(std::exp(score * scale), 1.0 / mx, mx);
}

std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::string nstr(int v) { return std::to_string(v); }

/** "3RD & 7", "GOAL TO GO", "HURRY-UP  -  2ND & 4". */
std::string sitTag(const Game& g) {
  std::string s = upper(g.downText());
  if (g.ballOn + g.toGo >= 100 && g.ballOn >= 90) s = "GOAL TO GO";
  if (g.tempo() == Tempo::Hurry) s = "HURRY-UP  -  " + s;
  else if (g.tempo() == Tempo::Milk) s = "BURN CLOCK  -  " + s;
  return s;
}

/** The situation's words for a count line: "on 3rd & long". */
std::string onSit(int b) { return std::string("on ") + sitName(b); }

/** A small deterministic hash of the moment (the suggestions' own Rng: the game's is never drawn). */
uint32_t momentSeed(const Game& g, uint32_t salt) {
  uint32_t h = 2166136261u ^ salt;
  auto mix = [&h](uint32_t v) {
    h ^= v;
    h *= 16777619u;
  };
  mix(static_cast<uint32_t>(g.q));
  mix(static_cast<uint32_t>(std::lround(g.clock * 10)));
  mix(static_cast<uint32_t>(std::lround(g.ballOn * 10)));
  mix(static_cast<uint32_t>(g.down));
  mix(static_cast<uint32_t>(std::lround(g.toGo * 10)));
  mix(static_cast<uint32_t>(g.offense().id));
  mix(static_cast<uint32_t>(g.log.size()));
  return h ? h : 1u;
}

/** Up to n picks from items by weight, without replacement, each with a different key (concept / call name). */
template <class T, class K>
std::vector<size_t> pickDistinct(Rng& r, const std::vector<T>& items, const std::vector<double>& w, int n, K key) {
  std::vector<size_t> out;
  std::vector<double> left = w;
  std::vector<std::string> keys;
  for (int k = 0; k < n; k++) {
    double tot = 0;
    for (double x : left) tot += (std::max)(0.0, x);
    if (tot <= 0) break;
    double x = r.next() * tot;
    size_t at = items.size() - 1;
    for (size_t i = 0; i < items.size(); i++) {
      x -= (std::max)(0.0, left[i]);
      if (x <= 0) {
        at = i;
        break;
      }
    }
    out.push_back(at);
    const std::string kk = key(items[at]);
    keys.push_back(kk);
    for (size_t i = 0; i < items.size(); i++)
      if (key(items[i]) == kk) left[i] = 0;
  }
  return out;
}

}  // namespace calladapt_detail

CallTune& callTune() {
  static CallTune t;
  return t;
}

bool setCallTune(const std::string& name, double v) {
  CallTune& t = callTune();
  if (name == "adapt") return t.adapt = v, true;
  if (name == "adaptDefK") return t.adaptDefK = v, true;
  if (name == "adaptOffK") return t.adaptOffK = v, true;
  if (name == "adaptMax") return t.adaptMax = v, true;
  if (name == "adaptPass") return t.adaptPass = v, true;
  if (name == "adaptPrior") return t.adaptPrior = v, true;
  if (name == "runMix") return t.runMix = v, true;
  if (name == "adaptSpamK") return t.adaptSpamK = v, true;
  return false;
}

// ------------------------------------------------------------------ the run game's call mix
int runFamily(const OffPlay& p) {
  if (!p.run || p.type == PlayType::Kneel || p.type == PlayType::Spike) return -1;
  const RunScheme& r = *p.run;
  const Formation* f = formationById(p.formation);
  if (r.carrier == "QB" || r.read == "end" || r.style == RunStyle::Sneak || (f && f->family == "Wildcat")) return 5;
  if (r.style == RunStyle::Draw) return 3;
  if (r.style == RunStyle::Stretch) {
    const std::string& n = p.conceptName.empty() ? p.name : p.conceptName;
    if (r.jet || !p.reverse.empty() || n.find("Toss") != std::string::npos || n.find("Sweep") != std::string::npos) return 4;
    return 1;
  }
  if (r.style == RunStyle::Gap) return 2;
  return 0;
}

const char* runFamilyName(int f) {
  static const char* const k[kRunFamN] = {"inside zone", "outside zone", "gap", "draw", "toss / sweep", "QB run"};
  return f >= 0 && f < kRunFamN ? k[f] : "";
}

double runFamilyW(int f) {
  // (2026-11-25, CallAdaptCal --runs, 200 games: the CPU's designed runs were inside zone 22.8 %, outside zone 9.4,
  // gap 33.4, draw 11.2, toss / sweep 17.3, QB 5.8 - and outside zone and the tosses cross at the end 94 % of the time,
  // so 25 % of all runs went to the edge from the call alone (nflverse end share 24.3 %). Toward inside zone 30, outside
  // zone 13, gap 33, draw 8, toss / sweep 7, QB 9 (the NFL's families, approximately, with outside zone held under its
  // ~18 % since the sim's never cuts back inside)): measured 29.2 / 11.9 / 35.6 / 8.8 / 8.0 / 6.4, and by where they
  // crossed middle 23.5 -> 25.5 %, guard 20.6 -> 20.6, tackle 14.3 -> 15.9, end 41.6 -> 37.9. The rest of the edge is the
  // run game itself: gap runs end up outside 30 % of the time, outside zone and the tosses 94 %)
  static const double kW[kRunFamN] = {1.32, 1.38, 0.99, 0.71, 0.40, 1.55};
  if (callTune().runMix <= 0 || f < 0 || f >= kRunFamN) return 1.0;
  return kW[f];
}

// ------------------------------------------------------------------ kinds
int sitBucket(int down, double toGo) {
  if (down <= 1) return 0;
  if (down == 2) return toGo <= 3 ? 1 : toGo <= 6 ? 2 : 3;
  return toGo <= 2 ? 4 : toGo <= 6 ? 5 : 6;
}

const char* sitName(int b) {
  static const char* const k[kSitN] = {"1st down", "2nd & short", "2nd & medium", "2nd & long", "3rd & short", "3rd & medium", "3rd & long"};
  return b >= 0 && b < kSitN ? k[b] : "";
}

int offClass(const OffPlay& p) {
  if (p.type == PlayType::Screen) return 7;
  if (p.type == PlayType::PlayAction) return 6;
  if (p.type == PlayType::Run || p.type == PlayType::Kneel || p.type == PlayType::Spike) {
    if (p.run) {
      if (p.cat == PlayCat::RPO || !p.run->read.empty()) return 2;
      if (p.run->style == RunStyle::Stretch || p.run->jet || std::fabs(p.run->aimDy) >= 3.5) return 1;
    }
    return 0;
  }
  if (p.hasTag("quick")) return 3;
  if (p.hasTag("deep")) return 5;
  return 4;
}

const char* offClassName(int c) {
  static const char* const k[kClsN] = {"inside run", "outside run", "option", "quick pass", "intermediate pass", "deep shot", "play action", "screen"};
  return c >= 0 && c < kClsN ? k[c] : "";
}

bool isPassClass(int c) { return c >= 3; }

double matchupEpa(int cls, int fam) {
  using namespace calladapt_detail;
  return cls >= 0 && cls < kClsN && fam >= 0 && fam < kFamN ? kMatch[cls][fam] : 0.0; }

double conceptMatchup(const std::string& key, int cls, int fam) {
  using namespace calladapt_detail;
  if (fam < 0 || fam >= kFamN) return 0.0;
  if (const double* d = conceptEdges(key)) return d[fam];
  return matchupEpa(cls, fam);
}

int defFamily(const DefCall& d) {
  const int r = d.rushers();
  const bool man = d.coverage == Coverage::C0 || d.coverage == Coverage::C1 || d.coverage == Coverage::C2M;
  if (r <= 3 || d.tech == CoverTech::Prevent || d.hasTag("prevent")) return 7;
  if (r >= 5) return man ? 0 : 1;
  if (d.coverage == Coverage::C0 || d.coverage == Coverage::C1) return 2;
  if (d.coverage == Coverage::C2M) return 3;
  if (d.coverage == Coverage::C3 || d.coverage == Coverage::C3Fire) return 4;
  if (d.coverage == Coverage::C2 || d.coverage == Coverage::C2T) return 5;
  return 6;
}

const char* defFamilyName(int f) {
  static const char* const k[kFamN] = {"the blitz", "the zone blitz", "man coverage", "2-man", "Cover 3", "Cover 2", "quarters", "the drop-8 shell"};
  return f >= 0 && f < kFamN ? k[f] : "";
}

double playEpa(int down, double toGo, double los, const PlayResult& res) {
  // (Game::resultEP, from the snap's down and distance)
  double after;
  if (res.td == Side::Off) after = 6.95;
  else if (res.td == Side::Def) after = -6.95;
  else if (res.turnover) after = -expectedPoints(100 - res.endX, 1, 10);
  else if (res.safety) after = -3;
  else {
    const double gained = res.endX - los;
    if (gained >= toGo) after = expectedPoints(res.endX, 1, 10);
    else if (down >= 4) after = -expectedPoints(100 - res.endX, 1, 10);
    else after = expectedPoints(res.endX, down + 1, toGo - gained);
  }
  return after - expectedPoints(los, down, toGo);
}

bool playSuccess(int down, double toGo, const PlayResult& res) {
  if (res.td == Side::Off) return true;
  if (res.turnover || res.td == Side::Def || res.safety) return false;
  const double need = down <= 1 ? 0.4 * toGo : down == 2 ? 0.6 * toGo : toGo;
  return res.yards >= need - 1e-9;
}

// ------------------------------------------------------------------ the record
int Tendency::offCalls() const {
  int n = 0;
  for (int v : offN) n += v;
  return n;
}

int Tendency::defCalls() const {
  int n = 0;
  for (int v : defN) n += v;
  return n;
}

std::string conceptKey(const OffPlay& p) { return p.conceptName.empty() ? p.name : p.conceptName; }

void noteCall(Tendency& off, Tendency& def, int down, double toGo, const OffPlay& op, const DefCall& dc, double epa, bool succ, int yards) {
  const int b = sitBucket(down, toGo);
  const int c = offClass(op);
  const int f = defFamily(dc);
  off.offN[b]++;
  if (isPassClass(c)) off.offPass[b]++;
  off.offCls[b][c]++;
  off.formCls[op.formation][c]++;
  const std::string key = conceptKey(op);
  off.formConcept[op.formation][key]++;
  off.clsN[c]++;
  off.clsEpa[c] += epa;
  ConceptLine& cl = off.concepts[key];
  cl.cls = c;
  cl.n++;
  cl.yds += yards;
  cl.succ += succ ? 1 : 0;
  cl.epa += epa;
  cl.vsN[f]++;
  cl.vsEpa[f] += epa;
  def.defN[b]++;
  def.defFam[b][f]++;
  def.famN[f]++;
  def.famEpa[f] += epa;
}

// ------------------------------------------------------------------ the reads
DefRead readOffense(const Tendency& off, int b, const std::string& formation) {
  using namespace calladapt_detail;
  DefRead r;
  const double n0 = (std::max)(0.5, callTune().adaptPrior);
  // the offense's mix here: its calls in this situation, then out of this formation (each shrunk to the one before)
  std::array<double, kClsN> pS{};
  for (int c = 0; c < kClsN; c++) pS[c] = (off.offCls[b][c] + n0 * kPrior[b][c]) / (off.offN[b] + n0);
  const auto fc = off.formCls.find(formation);
  int nf = 0;
  if (fc != off.formCls.end())
    for (int v : fc->second) nf += v;
  r.formN = nf;
  // the concept the offense keeps coming back to from this look, the more so when it works (answered below with what
  // beats that concept itself, not its kind)
  const auto fcon = off.formConcept.find(formation);
  int spamCalls = 0, spamCls = -1;
  double best = 0;
  if (fcon != off.formConcept.end() && nf >= 3) {
    for (const auto& kv : fcon->second) {
      const auto cl = off.concepts.find(kv.first);
      if (cl == off.concepts.end()) continue;
      // (spam: four calls or more and past 30 % of the look's - a book with a few plays in a set repeats them anyway,
      // and answering that in CPU games cost the calibration: held-out 1.09 -> 1.58 at 15 % and no floor)
      if (kv.second < 4) continue;
      const double over = (std::max)(0.0, kv.second - 0.3 * nf) / (nf + n0);
      const double mean = cl->second.epa / (cl->second.n + 2.0);
      const double urg = clamp(0.5 + mean, 0.0, 2.0);
      if (over * urg > best) {
        best = over * urg;
        r.spam = kv.first;
        r.spamN = cl->second.n;
        r.spamYds = cl->second.yds;
        spamCalls = kv.second;
        spamCls = cl->second.cls;
      }
    }
  }
  // the rest of what comes out of this look, by kind (a spammed concept with measured matchups of its own left out of it:
  // its kind's average answer can be the wrong one for it; one without them stays in, its kind's answer is its answer)
  if (spamCls >= 0 && !conceptEdges(r.spam)) spamCalls = 0, spamCls = -1;
  for (int c = 0; c < kClsN; c++) {
    const double seen = fc != off.formCls.end() ? fc->second[c] - (c == spamCls ? spamCalls : 0) : 0;
    const double nRest = nf - spamCalls;
    r.expect[c] = (seen + n0 * pS[c]) / (nRest + n0);
    if (!isPassClass(c) && fc != off.formCls.end()) r.formRun += fc->second[c];
  }
  // what each kind of call takes away from that mix, beyond what it takes away from the league's
  for (int f = 0; f < kFamN; f++) {
    double s = 0;
    for (int c = 0; c < kClsN; c++) s -= (r.expect[c] - kPrior[b][c]) * kMatch[c][f];
    r.score[f] = s;
  }
  if (!r.spam.empty()) {
    const ConceptLine& cl = off.concepts.at(r.spam);
    const double mean = cl.epa / (cl.n + 2.0);
    const double k2 = 4.0;
    for (int f = 0; f < kFamN; f++) {
      // what the concept has done against this kind of call (its own results, shrunk to its measured matchups)
      const double ex = (cl.vsEpa[f] + k2 * (mean + conceptMatchup(r.spam, cl.cls, f))) / (cl.vsN[f] + k2);
      r.score[f] += callTune().adaptSpamK * best * (mean - ex);
    }
  }
  return r;
}

double defAdaptMul(const DefRead& r, const DefCall& d) {
  using namespace calladapt_detail;
  const CallTune& ct = callTune();
  if (ct.adapt <= 0) return 1.0;
  return mulFor(r.score[defFamily(d)], kDefScale * ct.adaptDefK);
}

OffRead readDefense(const Tendency& self, const Tendency& oppDef, int b) {
  using namespace calladapt_detail;
  OffRead r;
  const double n0 = (std::max)(0.5, callTune().adaptPrior);
  std::array<double, kFamN> dq{};
  for (int f = 0; f < kFamN; f++) {
    r.expect[f] = (oppDef.defFam[b][f] + n0 * kDefPrior[b][f]) / (oppDef.defN[b] + n0);
    dq[f] = r.expect[f] - kDefPrior[b][f];
  }
  // each kind of play: what it gains on the looks this defense shows more than the league does, and how it has gone
  // for this offense today (both against their averages)
  // (the run/pass lean reads the matchups only: with what has worked in it as well, the league threw 1.1 points more)
  double passS = 0, passW = 0, runS = 0, runW = 0;
  for (int c = 0; c < kClsN; c++) {
    double adv = 0;
    for (int f = 0; f < kFamN; f++) adv += dq[f] * kMatch[c][f];
    const double works = (self.clsEpa[c] + n0 * kClsEpa[c]) / (self.clsN[c] + n0) - kClsEpa[c];
    r.score[c] = adv + 0.5 * works;
    const double w = kPrior[b][c];
    (isPassClass(c) ? passS : runS) += w * adv;
    (isPassClass(c) ? passW : runW) += w;
  }
  const double d = (passW > 0 ? passS / passW : 0) - (runW > 0 ? runS / runW : 0);
  const double mx = callTune().adaptPass;
  r.passShift = clamp(d * kPassScale, -mx, mx);
  return r;
}

double offAdaptMul(const OffRead& r, const Tendency& self, const OffPlay& p) {
  using namespace calladapt_detail;
  const CallTune& ct = callTune();
  if (ct.adapt <= 0) return 1.0;
  const int c = offClass(p);
  double s = r.score[c];
  // the concept itself: what it has done today, against its kind's average
  const auto cl = self.concepts.find(conceptKey(p));
  if (cl != self.concepts.end()) s += (cl->second.epa - cl->second.n * kClsEpa[c]) / (cl->second.n + 4.0);
  return mulFor(s, kOffScale * ct.adaptOffK);
}

// ------------------------------------------------------------------ the coach
namespace calladapt_detail {

/** Why the coach likes an offensive call here. */
std::string offReason(const Game& g, const OffPlay& p, const OffRead& rd, const Tendency& self, const Tendency& opp, int b) {
  const int c = offClass(p);
  const std::string sit = sitTag(g);
  // the concept has worked today
  const auto cl = self.concepts.find(conceptKey(p));
  if (cl != self.concepts.end() && cl->second.n >= 2 && cl->second.epa / cl->second.n > 0.2)
    return sit + "  -  " + cl->first + " has " + nstr(cl->second.yds) + " yd on " + nstr(cl->second.n) + " calls today";
  // it beats what the defense keeps showing here (or all game, early on)
  {
    int bestF = -1;
    double best = 0.04;
    for (int f = 0; f < kFamN; f++) {
      const double over = rd.expect[f] - kDefPrior[b][f];
      if (over > 0.06 && kMatch[c][f] > 0.02 && over * kMatch[c][f] * 100 > best) {
        best = over * kMatch[c][f] * 100;
        bestF = f;
      }
    }
    if (bestF >= 0 && opp.defN[b] >= 3)
      return sit + "  -  beats " + defFamilyName(bestF) + ": " + nstr(opp.defFam[b][bestF]) + " of " + nstr(opp.defN[b]) + " " + onSit(b);
    if (bestF >= 0 && opp.famN[bestF] >= 4) {
      const int all = opp.defCalls();
      return sit + "  -  beats " + defFamilyName(bestF) + ": " + nstr(opp.famN[bestF]) + " of " + nstr(all) + " snaps today";
    }
  }
  const double tg = g.toGo;
  if (g.ballOn >= 95 && p.hasTag("goalline")) return sit + "  -  built for the goal line";
  if (tg <= 2 && p.hasTag("shortyard")) return sit + "  -  built for short yardage";
  if (g.tempo() == Tempo::Hurry && (p.hasTag("quick") || p.hasTag("short"))) return sit + "  -  quick throw, saves the clock";
  if (g.down >= 3 && isPassClass(c)) {
    for (const std::string& rl : p.reads) {
      const std::string* nm = p.routeFor(rl);
      const RouteDef* rt = nm ? routeByName(*nm) : nullptr;
      if (!rt || rt->pts.empty()) continue;
      double m = -1e9;
      for (const auto& q : rt->pts) m = (std::max)(m, q.first);
      if (m >= tg && m <= tg + 7) return sit + "  -  " + rl + " runs his route at the sticks";
    }
  }
  if (c == 6 && self.clsN[0] + self.clsN[1] >= 4) return sit + "  -  the run game sets up the fake";
  if (c == 6 && g.down == 1) return sit + "  -  play action on first down";
  if (c == 7 && rd.expect[0] + rd.expect[1] > kDefPrior[b][0] + kDefPrior[b][1] + 0.04) return sit + "  -  screen against the pressure";
  if (tg >= 8 && (c == 5 || p.hasTag("deep"))) return sit + "  -  a shot down the field";
  if (!isPassClass(c) && rd.expect[5] + rd.expect[6] > kDefPrior[b][5] + kDefPrior[b][6] + 0.05) return sit + "  -  two deep: a light box to run at";
  const std::string what = p.conceptName.empty() ? std::string(offClassName(c)) : p.conceptName;
  return sit + "  -  " + what + ", the coach's call here";
}

/** Why the coordinator likes a defensive call here. */
std::string defReason(const Game& g, const DefCall& d, const DefRead& rd, const Tendency& opp, int b) {
  const int f = defFamily(d);
  const std::string sit = sitTag(g);
  if (!rd.spam.empty() && rd.spamN >= 3) {
    const auto cl = opp.concepts.find(rd.spam);
    if (cl != opp.concepts.end()) {
      const double k2 = 4.0;
      const double mean = cl->second.epa / (cl->second.n + 2.0);
      const double ex = (cl->second.vsEpa[f] + k2 * (mean + conceptMatchup(rd.spam, cl->second.cls, f))) / (cl->second.vsN[f] + k2);
      if (ex < mean - 0.02) return sit + "  -  " + rd.spam + " " + nstr(rd.spamN) + " times for " + nstr(rd.spamYds) + " yd: this takes it away";
    }
  }
  if (rd.formN >= 4) {
    const int pass = rd.formN - rd.formRun;
    double runFit = 0, passFit = 0;
    for (int c = 0; c < kClsN; c++) (isPassClass(c) ? passFit : runFit) -= kMatch[c][f] * kPrior[b][c];
    if (rd.formRun * 10 >= rd.formN * 7 && runFit > 0) return sit + "  -  they run " + nstr(rd.formRun) + " of " + nstr(rd.formN) + " from this look";
    if (pass * 10 >= rd.formN * 7 && passFit > 0) return sit + "  -  they throw " + nstr(pass) + " of " + nstr(rd.formN) + " from this look";
  }
  if (opp.offN[b] >= 4) {
    const int n = opp.offN[b], ps = opp.offPass[b];
    double runFit = 0, passFit = 0;
    for (int c = 0; c < kClsN; c++) (isPassClass(c) ? passFit : runFit) -= kMatch[c][f] * kPrior[b][c];
    if (ps * 10 >= n * 7 && passFit > 0) return sit + "  -  they pass " + nstr(ps) + " of " + nstr(n) + " " + onSit(b);
    if ((n - ps) * 10 >= n * 6 && runFit > 0) return sit + "  -  they run " + nstr(n - ps) + " of " + nstr(n) + " " + onSit(b);
  }
  if (g.ballOn >= 93 || (g.toGo <= 2 && d.hasTag("shortyard"))) return sit + "  -  loads the box for short yardage";
  if (g.toGo >= 8 && g.down >= 3 && (f == 6 || f == 7 || f == 5)) return sit + "  -  keep it in front of the sticks";
  if (g.toGo >= 7 && g.down >= 3 && (f == 0 || f == 1)) return sit + "  -  bring pressure, force it out early";
  const std::string cov = d.coverLabel.empty() ? std::string(coverageName(d.coverage)) : d.coverLabel;
  return sit + "  -  " + cov + ", the coordinator's call here";
}

}  // namespace calladapt_detail

std::vector<CallSuggestion> suggestOffense(const Game& g, int n) {
  using namespace calladapt_detail;
  std::vector<CallSuggestion> out;
  if (g.phase != GamePhase::Scrimmage || n <= 0) return out;
  const double pPass = g.cpuPassProb();
  std::vector<const OffPlay*> pool[2];
  std::vector<double> w[2];
  g.cpuOffensePool(false, pool[0], w[0]);
  g.cpuOffensePool(true, pool[1], w[1]);
  // every play's chance of the CPU's call: the run/pass draw times its share of its side's pool
  std::vector<const OffPlay*> all;
  std::vector<double> pr;
  for (int s = 0; s < 2; s++) {
    double tot = 0;
    for (double x : w[s]) tot += (std::max)(0.0, x);
    if (tot <= 0) continue;
    const double side = s ? pPass : 1 - pPass;
    for (size_t i = 0; i < pool[s].size(); i++) {
      all.push_back(pool[s][i]);
      pr.push_back(side * (std::max)(0.0, w[s][i]) / tot);
    }
  }
  if (all.empty()) return out;
  // the first from the side the coach leans to, the second from the other (a run and a pass), the rest from either;
  // never two of one concept
  Rng r(momentSeed(g, 0x0ff5u));
  const bool passFirst = pPass >= 0.5;
  std::vector<double> wA = pr, wB = pr;
  for (size_t i = 0; i < all.size(); i++) {
    const bool ps = all[i]->type != PlayType::Run;
    if (ps != passFirst) wA[i] = 0;
    if (ps == passFirst) wB[i] = 0;
  }
  auto key = [](const OffPlay* p) { return conceptKey(*p); };
  std::vector<size_t> at = pickDistinct(r, all, wA, 1, key);
  {
    std::vector<double> wB2 = wB;
    for (size_t j : at)
      for (size_t i = 0; i < all.size(); i++)
        if (key(all[i]) == key(all[j])) wB2[i] = 0;
    double otherSide = 0;
    for (double x : wB2) otherSide += x;
    if (otherSide >= 0.12 || g.toGo <= 3) {  // (3rd & 9: no run unless the coach would ever call one)
      const std::vector<size_t> b2 = pickDistinct(r, all, wB2, 1, key);
      at.insert(at.end(), b2.begin(), b2.end());
    }
  }
  {
    std::vector<double> wC = pr;
    for (size_t j : at)
      for (size_t i = 0; i < all.size(); i++)
        if (key(all[i]) == key(all[j])) wC[i] = 0;
    const std::vector<size_t> c3 = pickDistinct(r, all, wC, n - static_cast<int>(at.size()), key);
    at.insert(at.end(), c3.begin(), c3.end());
  }
  const int b = sitBucket(g.down, g.toGo);
  const Tendency& self = g.tend[g.poss];
  const Tendency& opp = g.tend[other(g.poss)];
  const OffRead rd = readDefense(self, opp, b);
  for (size_t j : at) {
    if (static_cast<int>(out.size()) >= n) break;
    CallSuggestion s;
    s.off = all[j];
    s.weight = pr[j];
    s.reason = offReason(g, *all[j], rd, self, opp, b);
    out.push_back(std::move(s));
  }
  // the likeliest first (the coach's pick)
  std::stable_sort(out.begin(), out.end(), [](const CallSuggestion& a, const CallSuggestion& c) { return a.weight > c.weight; });
  return out;
}

std::vector<CallSuggestion> suggestDefense(const Game& g, const std::string& formation, int n) {
  using namespace calladapt_detail;
  std::vector<CallSuggestion> out;
  if (g.phase != GamePhase::Scrimmage || n <= 0) return out;
  Rng r(momentSeed(g, 0xdef5u));
  std::vector<const DefCall*> pool;
  std::vector<double> w;
  g.cpuDefensePool(formation, r, pool, w);
  if (pool.empty()) return out;
  double tot = 0;
  for (double x : w) tot += (std::max)(0.0, x);
  // never two of one kind of call (three Cover 3s from three sets are one suggestion)
  auto key = [](const DefCall* d) { return std::to_string(defFamily(*d)) + "|" + (d->coverLabel.empty() ? coverageName(d->coverage) : d->coverLabel); };
  const std::vector<size_t> at = pickDistinct(r, pool, w, n, key);
  const int b = sitBucket(g.down, g.toGo);
  const Tendency& opp = g.tend[g.poss];
  const DefRead rd = readOffense(opp, b, formation);
  for (size_t j : at) {
    CallSuggestion s;
    s.def = pool[j];
    s.weight = tot > 0 ? (std::max)(0.0, w[j]) / tot : 0;
    s.reason = defReason(g, *pool[j], rd, opp, b);
    out.push_back(std::move(s));
  }
  std::stable_sort(out.begin(), out.end(), [](const CallSuggestion& a, const CallSuggestion& c) { return a.weight > c.weight; });
  return out;
}

std::vector<std::string> coachRead(const Game& g, bool offense, const std::string& formation) {
  using namespace calladapt_detail;
  std::vector<std::string> out;
  if (g.phase != GamePhase::Scrimmage) return out;
  const int b = sitBucket(g.down, g.toGo);
  if (offense) {
    // the defense across: what it shows here, and how often it brings pressure
    const Tendency& opp = g.tend[other(g.poss)];
    const int all = opp.defCalls();
    if (opp.defN[b] >= 3) {
      int top = 0;
      for (int f = 1; f < kFamN; f++)
        if (opp.defFam[b][f] > opp.defFam[b][top]) top = f;
      out.push_back(std::string(sitName(b)) + ": " + defFamilyName(top) + " " + nstr(opp.defFam[b][top]) + " of " + nstr(opp.defN[b]));
    }
    if (all >= 4) {
      const int press = opp.famN[0] + opp.famN[1];
      out.push_back("pressure on " + nstr(press) + " of " + nstr(all) + " snaps");
      const int two = opp.famN[3] + opp.famN[5] + opp.famN[6];
      out.push_back("two deep on " + nstr(two) + " of " + nstr(all));
    }
    const Tendency& self = g.tend[g.poss];
    std::string bestK;
    double best = 0.25;
    int bestN = 0, bestY = 0;
    for (const auto& kv : self.concepts)
      if (kv.second.n >= 2 && kv.second.epa / kv.second.n > best) {
        best = kv.second.epa / kv.second.n;
        bestK = kv.first;
        bestN = kv.second.n;
        bestY = kv.second.yds;
      }
    if (!bestK.empty()) out.push_back("working: " + bestK + " (" + nstr(bestN) + " for " + nstr(bestY) + " yd)");
  } else {
    const Tendency& opp = g.tend[g.poss];
    if (opp.offN[b] >= 3) out.push_back(std::string(sitName(b)) + ": pass " + nstr(opp.offPass[b]) + " of " + nstr(opp.offN[b]));
    const DefRead rd = readOffense(opp, b, formation);
    if (rd.formN >= 3) out.push_back("this look: run " + nstr(rd.formRun) + " of " + nstr(rd.formN));
    if (!rd.spam.empty() && rd.spamN >= 3) out.push_back("their go-to: " + rd.spam + " (" + nstr(rd.spamN) + " for " + nstr(rd.spamYds) + " yd)");
  }
  if (out.size() > 3) out.resize(3);
  return out;
}

}  // namespace tag
