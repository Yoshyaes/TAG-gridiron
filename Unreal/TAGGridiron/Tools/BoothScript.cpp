// Headless broadcast: plays a whole CPU game through the booth on the presentation's own timeline (call, pre-snap,
// the live play at 60 Hz, the post-play beat and replays, kicks in the air, live returns) and prints the broadcast as
// a timed transcript, then a summary (talk time, lines dropped and cut, repeats).
//
//   BoothScript.exe [seed=7] [--hold] [--quiet] [--games=N] [--jsonl=path]
//     --hold : the presentation lets the booth hold the snap (a watched CPU game)
//     --jsonl: also write every spoken line as JSON (for TTS batch synthesis and listening tests)
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Booth/Booth.h"
#include "Sim/Game.h"
#include "Sim/League.h"

#include <chrono>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// --writer=<model>: the booth's writer jobs go to a local Ollama (http://127.0.0.1:11434/api/chat), or with
// --writer=claude:claude-opus-5-5 to the Claude API (needs ANTHROPIC_API_KEY); each answer is
// delivered --writerlag seconds of booth time after the ask (the in-game CPU latency of the model, ~25 s for 14B)
static std::string gWriterModel;
static double gWriterLag = 25;

static std::string jsonEsc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      o += '\\';
      o += static_cast<char>(c);
    } else if (c == '\n') o += "\\n";
    else if (c == '\r') {
    } else if (c == '\t') o += ' ';
    else if (c < 32) o += ' ';
    else o += static_cast<char>(c);
  }
  return o;
}

static std::string ollamaChat(const std::string& model, const std::string& sys, const std::string& user) {
  const std::string body = "{\"model\":\"" + jsonEsc(model) + "\",\"stream\":false,\"keep_alive\":\"10m\",\"options\":{\"temperature\":0.75,\"top_p\":0.9,\"num_predict\":200},"
                           "\"messages\":[{\"role\":\"system\",\"content\":\"" + jsonEsc(sys) + "\"},{\"role\":\"user\",\"content\":\"" + jsonEsc(user) + "\"}]}";
  std::string resp;
  HINTERNET ses = WinHttpOpen(L"BoothScript/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) return resp;
  WinHttpSetTimeouts(ses, 5000, 5000, 300000, 300000);
  HINTERNET con = WinHttpConnect(ses, L"127.0.0.1", 11434, 0);
  HINTERNET req = con ? WinHttpOpenRequest(con, L"POST", L"/api/chat", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
  if (req && WinHttpSendRequest(req, L"Content-Type: application/json\r\n", static_cast<DWORD>(-1), const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                                static_cast<DWORD>(body.size()), 0) &&
      WinHttpReceiveResponse(req, nullptr)) {
    DWORD n = 0;
    while (WinHttpQueryDataAvailable(req, &n) && n > 0) {
      std::string chunk(n, '\0');
      DWORD got = 0;
      if (!WinHttpReadData(req, chunk.data(), n, &got)) break;
      resp.append(chunk.data(), got);
    }
  }
  if (req) WinHttpCloseHandle(req);
  if (con) WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  // "content":"..." of the message, unescaped
  const size_t at = resp.find("\"content\":\"");
  if (at == std::string::npos) return std::string();
  std::string out;
  for (size_t i = at + 11; i < resp.size(); ++i) {
    const char c = resp[i];
    if (c == '"') break;
    if (c == '\\' && i + 1 < resp.size()) {
      const char e = resp[++i];
      if (e == 'n') out += '\n';
      else if (e == 't') out += ' ';
      else if (e == 'u' && i + 4 < resp.size()) {
        const int cp = std::stoi(resp.substr(i + 1, 4), nullptr, 16);
        i += 4;
        if (cp == 0x2019 || cp == 0x2018) out += '\'';
        else if (cp == 0x201c || cp == 0x201d) out += '"';
        else if (cp == 0x2014 || cp == 0x2013) out += ", ";
        else if (cp < 128) out += static_cast<char>(cp);
      } else out += e;
    } else out += c;
  }
  return out;
}

// --writer=claude:<model>: the job goes to Tools/Booth/booth_writer.py (Claude API through the anthropic SDK, with
// its own fact check), run in the booth venv. Returns '' when it vetoes the piece or the call fails.
static std::string claudeWrite(const std::string& model, const std::string& sys, const std::string& user, const std::string& facts) {
  char exe[MAX_PATH];
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  std::string tools = exe;
  tools = tools.substr(0, tools.find_last_of("\\/"));  // Tools/build
  tools = tools.substr(0, tools.find_last_of("\\/"));  // Tools
  char tmp[MAX_PATH];
  GetTempPathA(MAX_PATH, tmp);
  const std::string job = std::string(tmp) + "booth_job.json", out = std::string(tmp) + "booth_job.txt";
  if (FILE* f = std::fopen(job.c_str(), "wb")) {
    const std::string body = "{\"id\":0,\"system\":\"" + jsonEsc(sys) + "\",\"user\":\"" + jsonEsc(user) + "\",\"facts\":\"" + jsonEsc(facts) + "\"}";
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
  }
  std::remove(out.c_str());
  const std::string cmd = "\"\"" + tools + "\\Booth\\.venv\\Scripts\\python.exe\" \"" + tools + "\\Booth\\booth_writer.py\" --model " + model + " \"" + job + "\" \"" + out +
                          "\" >nul\"";
  std::system(cmd.c_str());
  std::string text;
  if (FILE* f = std::fopen(out.c_str(), "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
  }
  return text;
}

using namespace tag;
using booth::Booth;
using booth::View;

namespace {

std::vector<Player*> pick11(Team& t, const Game& g, const std::string& role) {
  std::vector<Player*> healthy;
  for (auto& p : t.roster)
    if (p->injuredWeeks <= 0) healthy.push_back(p.get());
  auto by = [&](std::initializer_list<Pos> ps) {
    std::vector<Player*> out;
    for (Player* p : healthy)
      for (Pos q : ps)
        if (p->pos == q) {
          out.push_back(p);
          break;
        }
    std::stable_sort(out.begin(), out.end(), [](Player* a, Player* b) { return a->r.speed > b->r.speed; });
    return out;
  };
  std::vector<Player*> out;
  if (role == "K" || role == "P") {
    auto l = by({role == "K" ? Pos::K : Pos::P});
    if (!l.empty()) out.push_back(l[0]);
    if (role == "P" || g.phase != GamePhase::Kickoff)
      for (Player* p : by({Pos::OT, Pos::OG, Pos::C, Pos::TE}))
        if (out.size() < 9) out.push_back(p);
  }
  if (role == "KR") {
    auto l = by({Pos::WR, Pos::CB, Pos::RB});
    if (!l.empty()) out.push_back(l[0]);
  }
  auto rest = role == "D" ? by({Pos::DE, Pos::DT, Pos::LB}) : by({Pos::LB, Pos::S, Pos::CB, Pos::TE, Pos::WR, Pos::RB, Pos::FB, Pos::DE});
  for (Player* p : rest)
    if (out.size() < 11 && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
  return out;
}

struct Show {
  Game& g;
  Booth& b;
  View v;
  bool quiet = false;
  FILE* jsonl = nullptr;
  double talk = 0;
  int lines = 0;
  std::map<std::string, int> textCount;
  std::map<std::string, int> kindCount;
  std::map<std::string, int> talkCount;  // the non-live lines (a repeated sentence of talk is what an ear notices)
  int plays = 0, playsWithAnalysis = 0;
  int lastAnalysisPlay = -1;

  static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
      if (c == '"' || c == '\\') o += '\\';
      o += c;
    }
    return o;
  }

  std::set<std::string> forecastSeen;  // every text offered for prewarm before it was needed
  int liveLines = 0, liveHit = 0, frameNo = 0, liveStatic = 0;
  std::set<std::string> staticSet;
  std::vector<std::string> playCalls;  // what PBP said since the snap (checked against the play's result)
  int checked = 0, contradictions = 0;
  std::map<std::string, double> firstSeen;  // when a line was first offered to the voice (queued or forecast)
  int predVoiced = 0, predLines = 0;
  void factCheck(const PlayResult& r) {
    auto said = [&](std::initializer_list<const char*> ws) {
      for (const std::string& t : playCalls)
        for (const char* w : ws)
          if (t.find(w) != std::string::npos) return t;
      return std::string();
    };
    ++checked;
    std::string bad;
    const bool caught = r.complete && !r.interception;
    if (std::string t = said({"Broken up", "Knocked away", "Batted down", "Dropped", "Drops it", "through his hands", "hang on", "Incomplete", "incomplete"}); !t.empty() && caught)
      bad = "called an incompletion (" + t + ") on a catch";
    if (std::string t = said({"Caught!", "Got it!", "Makes the grab", "Hauls it in", "he's got it", "Snags it", "Diving catch", "What a catch", "Makes the catch"}); !t.empty() && r.incomplete)
      bad = "called a catch (" + t + ") on an incompletion";
    if (std::string t = said({"Sacked", "Sack!", "with the sack"}); !t.empty() && r.kind != PlayKind::Sack) bad = "called a sack (" + t + ") on a " + playKindName(r.kind);
    if (std::string t = said({"Touchdown"}); !t.empty() && !r.td) bad = "called a touchdown (" + t + ") on no score";
    if (std::string t = said({"Intercepted", "Picked off", "intercepted"}); !t.empty() && !r.interception) bad = "called an interception (" + t + ") on none";
    if (std::string t = said({"Fumble", "coughs it up", "ball is out"}); !t.empty() && !r.fumbleBy) bad = "called a fumble (" + t + ") on none";
    if (!bad.empty()) {
      ++contradictions;
      if (!quiet) std::printf("            [FACT CHECK] %s\n", bad.c_str());
    }
    playCalls.clear();
  }  // what the match-start prewarm covers (generic + roster calls)
  struct Pending {
    int id;
    double due;
    std::string text;
    double secs;
  };
  std::vector<Pending> writerPending;
  double writerWall = 0;
  int writerVetoed = 0;
  int writerCalls = 0;
  void frame(double dt) {
    b.tick(dt, g, v);
    if (!gWriterModel.empty()) {
      for (const Booth::WriterJob& j : b.takeWriterJobs()) {
        const auto t0 = std::chrono::steady_clock::now();
        const bool claude = gWriterModel.rfind("claude", 0) == 0;
        std::string text = claude ? claudeWrite(gWriterModel, j.system, j.user, j.facts) : ollamaChat(gWriterModel, j.system, j.user);
        // the same model checks its own lines against the facts (a wrong count or a wrong team never airs)
        const std::string verdict = claude ? std::string(text.empty() ? "NO" : "YES")  // (booth_writer.py ran its own check)
                                           : ollamaChat(gWriterModel, "You are a strict fact checker for a TV sports broadcast. Answer with one word: YES or NO.",
                                                        j.facts + "\n\nBroadcast lines:\n" + text +
                                                            "\n\nIs every statement in these lines supported by the facts above (right player, right team, right counts, "
                                                            "events that really happened, nothing about seasons or history)? Answer YES or NO.");
        if (verdict.find("YES") == std::string::npos) {
          ++writerVetoed;
          text.clear();
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        writerWall += secs;
        ++writerCalls;
        writerPending.push_back({j.id, b.now() + gWriterLag, text, secs});
        if (!quiet) std::printf("            [writer %s, %.1f s]\n", j.kind.c_str(), secs);
      }
      for (auto it = writerPending.begin(); it != writerPending.end();)
        if (b.now() >= it->due) {
          b.writerDone(it->id, it->text, it->secs);
          it = writerPending.erase(it);
        } else
          ++it;
    }
    if (forecastSeen.empty())
      for (const auto& gc : Booth::genericCalls()) forecastSeen.insert(gc.first);
    if (staticSet.empty() && b.now() > 0.1) {
      for (const auto& gc : Booth::genericCalls()) staticSet.insert(gc.first);
      for (const auto& rc : b.rosterCalls(g)) staticSet.insert(rc.first);
    }
    if (++frameNo % 3 == 0)
      for (const booth::Line& l : b.forecast()) {
        forecastSeen.insert(l.text);
        firstSeen.emplace(l.text, b.now());
      }
    // the speech channel, played at the booth's estimated length
    if (b.speaking() && b.cutCurrent()) {
      if (!quiet) std::printf("            [cut]\n");
      b.finished(b.current().id);
    }
    if (b.speaking() && b.now() >= b.curEnd()) b.finished(b.current().id);
    if (!b.speaking()) {
      if (const booth::Line* l = b.next()) {
        b.started(l->id, l->estSecs);
        talk += l->estSecs;
        ++lines;
        textCount[l->text]++;
        if (l->who == booth::Who::PBP && (l->live || l->kind == booth::Kind::Result)) playCalls.push_back(l->text);
        {
          // would the voice have it? cached for good (generic / roster), or offered long enough ahead to be made
          // (2 s under game load) plus the wait the presentation allows this kind
          ++predLines;
          auto it = firstSeen.find(l->text);
          const double lead = it == firstSeen.end() ? 0.0 : b.now() - it->second;
          using K = booth::Kind;
          const double allow = l->live ? 0.3 : l->kind == K::Result ? 3.5 : l->kind == K::Setup ? 3.0 : l->kind == K::Analysis || l->kind == K::Read ? 3.5 : 5.0;
          if (staticSet.count(l->text) || lead + allow >= 2.0 + 0.25 * l->estSecs) ++predVoiced;
        }
        if (l->live) {
          ++liveLines;
          if (forecastSeen.count(l->text)) ++liveHit;
          if (staticSet.count(l->text)) ++liveStatic;
        }
        if (!l->live) talkCount[l->text]++;
        kindCount[booth::kindName(l->kind)]++;
        if (l->kind == booth::Kind::Analysis && lastAnalysisPlay != plays) {
          lastAnalysisPlay = plays;
          ++playsWithAnalysis;
        }
        if (!quiet) {
          char clk[16];
          const int c = static_cast<int>(g.clock + 0.999);
          std::snprintf(clk, sizeof clk, "%d:%02d", c / 60, c % 60);
          std::printf("%7.1f Q%d %5s %-5s %-8s e%d | %s\n", b.now(), g.q, clk, booth::whoName(l->who), booth::kindName(l->kind), l->energy, l->text.c_str());
        }
        if (jsonl)
          std::fprintf(jsonl, "{\"t\":%.2f,\"who\":\"%s\",\"kind\":\"%s\",\"energy\":%d,\"text\":\"%s\",\"say\":\"%s\"}\n", b.now(), booth::whoName(l->who),
                       booth::kindName(l->kind), l->energy, esc(l->text).c_str(), esc(l->say).c_str());
      }
    }
  }
  void run(double secs, double dt = 1.0 / 30) {
    for (double t = 0; t < secs; t += dt) frame(dt);
  }
  void stage(View::Stage s, const PlaySim* sim) {
    v.stage = s;
    v.sim = sim;
  }
  /** Hold the snap while the booth asks (as the director does with canHold), at most a few seconds. */
  void presnap(double base) {
    run(base);
    double held = 0;
    while (v.canHold && b.wantsHold() && held < 8) {
      frame(1.0 / 30);
      held += 1.0 / 30;
    }
  }
  void livePlay(PlaySim& sim) {
    playCalls.clear();
    stage(View::Stage::Live, &sim);
    int n = 0;
    while (sim.isLive() && n++ < 60 * 40) {
      sim.step(SIM_DT);
      frame(SIM_DT);
    }
    ++plays;
  }
  void kick(const SpecialResult& r, int kind) {
    v.kickKind = kind;
    v.kickT = 0;
    v.kickLead = kind == 0 ? 0.35 : kind == 1 ? 1.0 : 0.9;
    v.kickHang = r.flight ? r.flight->hang : 2.5;
    v.stage = View::Stage::Kick;
    v.sim = nullptr;
    const double dur = v.kickHang + 1.6;
    for (double t = 0; t < dur; t += 1.0 / 30) {
      v.kickT = t;
      frame(1.0 / 30);
    }
    v.kickKind = -1;
    ++plays;
    holdSim.reset();  // (the director drops the last play's sim when a kick starts)
    stage(View::Stage::Wait, nullptr);
    run(1.5);
  }
  void liveReturn(std::vector<Player*> kickU, std::vector<Player*> recvU) {
    auto sim = g.kickReturnSim(recvU, kickU);
    stage(View::Stage::Presnap, sim.get());
    frame(1.0 / 30);
    livePlay(*sim);
    stage(View::Stage::Post, sim.get());
    run(1.8);
    g.finishKickReturn(*sim);
    stage(View::Stage::Wait, sim.get());
    run(0.8);
    holdSim = std::move(sim);
  }
  std::unique_ptr<PlaySim> holdSim;  // the last play's sim stays alive into the next call (as in the director)
};

}  // namespace

int main(int argc, char** argv) {
  uint32_t seed = 7;
  bool hold = false, quiet = false;
  int games = 1;
  const char* jsonPath = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--hold")) hold = true;
    else if (!std::strcmp(argv[i], "--quiet")) quiet = true;
    else if (!std::strncmp(argv[i], "--games=", 8)) games = std::atoi(argv[i] + 8);
    else if (!std::strncmp(argv[i], "--jsonl=", 8)) jsonPath = argv[i] + 8;
    else if (!std::strncmp(argv[i], "--writer=", 9)) gWriterModel = argv[i] + 9;
    else if (!std::strncmp(argv[i], "--writerlag=", 12)) gWriterLag = std::atof(argv[i] + 12);
    else seed = static_cast<uint32_t>(std::atoi(argv[i]));
  }
  FILE* jf = jsonPath ? std::fopen(jsonPath, "wb") : nullptr;
  // --bake: no game, just every line the voice cache can hold for good: the generic calls, then each team's regulars'
  // live calls (the league is fixed: createLeague(2027)), the default Play Now matchup (3 v 7) first
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--bake")) {
      if (!jf) {
        std::printf("--bake needs --jsonl=path\n");
        return 1;
      }
      League league = createLeague(2027);
      std::set<std::string> done;
      auto emit = [&](const std::string& text, int energy) {
        const std::string say = Booth::spoken(text);
        if (!done.insert(std::to_string(energy) + "|" + say).second) return;
        std::fprintf(jf, "{\"t\":0,\"who\":\"PBP\",\"kind\":\"live\",\"energy\":%d,\"text\":\"%s\",\"say\":\"%s\"}\n", energy, Show::esc(text).c_str(),
                     Show::esc(say).c_str());
      };
      for (const auto& gc : Booth::genericCalls()) emit(gc.first, gc.second);
      std::vector<std::pair<int, int>> pairs = {{3, 7}};
      for (int t = 0; t < 32; t += 2) pairs.push_back({t, t + 1});
      for (auto [h, a] : pairs) {
        Game g(*league[h], *league[a]);
        Booth b(1);
        View v;
        v.stage = View::Stage::Wait;
        b.tick(0.1, g, v);  // the open: the booth learns the rosters (names two teammates share)
        for (const auto& rc : b.rosterCalls(g)) emit(rc.first, rc.second);
      }
      std::fclose(jf);
      std::printf("baked %zu lines\n", done.size());
      return 0;
    }
  double totTalk = 0, totTime = 0;
  int totLines = 0, totRepeats = 0, totDistinct = 0, totPlays = 0, totAnalysis = 0, totExpired = 0, totCut = 0, totQueued = 0;
  for (int gi = 0; gi < games; ++gi) {
    League league = createLeague(2027);
    Rng pickRng(seed + gi * 7919);
    const int hi = pickRng.intRange(0, 31);
    int ai = pickRng.intRange(0, 31);
    if (ai == hi) ai = (ai + 1) % 32;
    GameOptions o;
    o.seed = seed + gi * 104729;
    Game g(*league[hi], *league[ai], o);
    Booth b(seed * 31 + gi);
    b.enableWriter(!gWriterModel.empty());
    Show sh{g, b};
    sh.quiet = quiet;
    sh.jsonl = jf;
    sh.v.canHold = hold;
    sh.v.night = (seed + gi) % 3 == 0;
    if (!quiet) std::printf("==== GAME %d: %s %s at %s %s ====\n", gi, g.away->city.c_str(), g.away->name.c_str(), g.home->city.c_str(), g.home->name.c_str());
    sh.stage(View::Stage::Wait, nullptr);
    sh.run(0.5);
    sh.run(2.0);
    for (double held = 0; b.wantsHold() && held < 45; held += 1.0 / 30) sh.frame(1.0 / 30);  // the kickoff waits for the open
    int guard = 0;
    while (!g.isFinal() && guard++ < 3000) {
      PlaySim* last = sh.holdSim.get();
      if (g.phase == GamePhase::Kickoff) {
        // (the director's Wait holds Next() while the booth finishes: a score, the drive, the free-kick note)
        for (double held = 0; b.wantsHold() && held < (hold ? 45.0 : 0.0); held += 1.0 / 30) sh.frame(1.0 / 30);
        if (g.clock <= 0) {
          g.simStep();
          continue;
        }
        const TeamKey kt = g.kicking;
        auto kickU = pick11(g.teamOf(kt), g, "K");
        auto recvU = pick11(g.teamOf(other(kt)), g, "KR");
        const SpecialResult r = g.kickoff(false, true);
        if (g.pendingReturn) sh.liveReturn(kickU, recvU);
        else sh.kick(r, 0);
        continue;
      }
      if (g.phase == GamePhase::Pat) {
        if (g.goForTwo()) {
          const OffPlay* op = g.cpuTwoPointPlay();
          const DefCall* dc = g.cpuDefense(*op);
          auto sim = g.twoPointSim(*op, *dc);
          sh.stage(View::Stage::Presnap, sim.get());
          sh.presnap(2.5);
          sh.livePlay(*sim);
          sh.stage(View::Stage::Post, sim.get());
          sh.run(1.8);
          g.finishTwoPoint(*sim);
          sh.stage(View::Stage::Wait, sim.get());
          sh.run(1.0);
          sh.holdSim = std::move(sim);
        } else {
          const SpecialResult r = g.extraPoint();
          sh.kick(r, 2);
        }
        continue;
      }
      const int qBefore = g.q;
      sh.stage(View::Stage::Wait, last);
      // the director's Wait holds Next() while the booth finishes (a watched game; the open and the breaks always)
      for (double held = 0; b.wantsHold() && held < (hold ? 6.0 : 0.0); held += 1.0 / 30) sh.frame(1.0 / 30);
      if (!g.preSnap()) {
        sh.run(qBefore != g.q ? 3.0 : 1.2);
        for (double held = 0; b.wantsHold() && held < 45; held += 1.0 / 30) sh.frame(1.0 / 30);
        continue;
      }
      if (g.down == 4) {
        const FourthDownCall c = g.fourthDownCall();
        if (c == FourthDownCall::Punt) {
          auto kickU = pick11(g.offense(), g, "P");
          auto recvU = pick11(g.defense(), g, "KR");
          const SpecialResult r = g.punt(true);
          if (g.pendingReturn) sh.liveReturn(kickU, recvU);
          else sh.kick(r, 1);
          continue;
        }
        if (c == FourthDownCall::Fg) {
          const SpecialResult r = g.fieldGoal();
          sh.kick(r, 2);
          continue;
        }
      } else if ((g.q == 2 || g.q >= 4) && g.clock <= 6 && 100 - g.ballOn + 17 <= 56 && g.margin() >= -3) {
        const SpecialResult r = g.fieldGoal();
        sh.kick(r, 2);
        continue;
      }
      if (auto pen = g.rollPresnap()) {
        g.applyPresnap(*pen);
        sh.run(2.5);
        continue;
      }
      sh.stage(View::Stage::Call, last);
      sh.run(0.4);
      const OffPlay* op = g.cpuOffense();
      const DefCall* dc = g.cpuDefense(*op);
      auto sim = g.makePlay(*op, *dc);
      sh.stage(View::Stage::Presnap, sim.get());
      sh.presnap(2.6);
      sh.livePlay(*sim);
      const PlayResult& r = *sim->result;
      sh.stage(View::Stage::Post, sim.get());
      sh.run(1.8);
      const bool replay = r.td || r.interception || r.fumbleLost || r.kind == PlayKind::Sack || r.yards >= 20;
      if (replay) {
        sh.stage(View::Stage::Replay, sim.get());
        sh.run(5.0);
      }
      sh.factCheck(r);
      g.finishPlay(*sim);
      sh.stage(View::Stage::Wait, sim.get());
      sh.run(0.6);
      sh.holdSim = std::move(sim);
    }
    sh.stage(View::Stage::Final, nullptr);
    sh.run(40);
    const booth::Booth::Stats& st = b.stats();
    int repeats = 0;
    int talkLines = 0, talkRepeats = 0;
    for (const auto& kv : sh.talkCount) {
      talkLines += kv.second;
      if (kv.second > 1) talkRepeats += kv.second - 1;
    }
    std::printf("fact check: %d plays, %d contradictions\n", sh.checked, sh.contradictions);
    if (b.writerOn()) {
      const auto ws = b.writerStats();
      std::printf("writer: asked %d, used %d, rejected %d (fact-check veto %d), stale %d; %d calls, %.0f s wall (%.1f s each)\n", ws.asked, ws.used, ws.rejected,
                  sh.writerVetoed, ws.stale, sh.writerCalls,
                  sh.writerWall, sh.writerWall / (std::max)(1, sh.writerCalls));
    }
    std::printf("predicted voiced (warm league cache): %d of %d (%.0f%%)\n", sh.predVoiced, sh.predLines, 100.0 * sh.predVoiced / (std::max)(1, sh.predLines));
    std::printf("live calls %d, prewarmable %d (%.0f%%), in the kickoff prewarm %d (%.0f%%; %zu lines)\n", sh.liveLines, sh.liveHit,
                100.0 * sh.liveHit / (std::max)(1, sh.liveLines), sh.liveStatic, 100.0 * sh.liveStatic / (std::max)(1, sh.liveLines), sh.staticSet.size());
    std::printf("talk lines %d, repeated %d (%.1f%%)\n", talkLines, talkRepeats, 100.0 * talkRepeats / (std::max)(1, talkLines));
    for (const auto& kv : sh.textCount)
      if (kv.second > 1) repeats += kv.second - 1;
    totTalk += sh.talk;
    totTime += b.now();
    totLines += sh.lines;
    totRepeats += repeats;
    totDistinct += static_cast<int>(sh.textCount.size());
    totPlays += sh.plays;
    totAnalysis += sh.playsWithAnalysis;
    totExpired += st.expired;
    totCut += st.cut;
    totQueued += st.queued;
    if (!quiet) {
      std::printf("==== FINAL %s %d, %s %d ====\n", g.away->abbr.c_str(), g.score.away, g.home->abbr.c_str(), g.score.home);
      std::printf("lines by kind:");
      for (const auto& kv : sh.kindCount) std::printf(" %s %d", kv.first.c_str(), kv.second);
      std::printf("\nmost repeated:\n");
      std::vector<std::pair<int, std::string>> rep;
      for (const auto& kv : sh.talkCount)
        if (kv.second > 1) rep.push_back({kv.second, kv.first});
      std::sort(rep.rbegin(), rep.rend());
      for (size_t i = 0; i < rep.size() && i < 15; ++i) std::printf("  %2d x %s\n", rep[i].first, rep[i].second.c_str());
    }
  }
  if (jf) std::fclose(jf);
  std::printf("SUMMARY games %d | broadcast %.0f s, talk %.0f s (%.0f%%) | lines %d, distinct %d, repeats %d (%.1f%%) | plays %d, analysis on %d (%.0f%%) | queued %d, dropped %d, cut %d\n",
              games, totTime, totTalk, 100 * totTalk / (std::max)(1.0, totTime), totLines, totDistinct, totRepeats, 100.0 * totRepeats / (std::max)(1, totLines), totPlays, totAnalysis,
              100.0 * totAnalysis / (std::max)(1, totPlays), totQueued, totExpired, totCut);
  return 0;
}
