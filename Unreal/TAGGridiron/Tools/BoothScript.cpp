// Headless broadcast: plays a whole CPU game through the booth on the presentation's own timeline (call, pre-snap,
// the live play at 60 Hz, the post-play beat and replays, kicks in the air, live returns) and prints the broadcast as
// a timed transcript, then a summary (talk time, lines dropped and cut, repeats).
//
//   BoothScript.exe [seed=7] [--hold] [--quiet] [--games=N] [--jsonl=path] [--teams=H,A] [--nomemory]
//     --hold : the presentation lets the booth hold the snap (a watched CPU game)
//     --jsonl: also write every spoken line as JSON (for TTS batch synthesis and listening tests)
//     --teams: every game between these two league teams (0..31, home first); otherwise a random pair a game
//     --nomemory: the booth forgets between games (Booth::enableMemory; the baseline for the cross-game repeat share)
//     --noref: the play-by-play man reads the flags himself (no referee announcements: Booth::enableReferee, the game's default)
//     --xrep[=N]: after a run of games, where the cross-game repeats come from and the N sentences heard again most
//     --game[=dwell]: the shipped director in a played game, the user on the home team (MatchDirector.cpp): the booth
//              cannot hold for its talk (View::canHold off, userGame on), only for what it always holds for; the call
//              screen takes dwell seconds (default 6); the user's offense snaps 2 s after the set without waiting for
//              the booth; against the user's defense the CPU waits 7 s and then for the booth (at most 45 s)
//     --modes: the same games in each mode - no hold (a player who skips every hold and calls at once), --hold (a
//              watched game), --game=0, --game=3, --game=6 - and the audit's timing and failures side by side
//   BoothScript.exe --banks                  the authored phrase banks and their sizes
//   BoothScript.exe --bake --jsonl=path       every generic live call and each team's regulars' calls (one pairing a team)
//   BoothScript.exe --bake-all --jsonl=path   the same for every one of the 992 pairings (the calls that name the other
//                                             side's quarterback differ by opponent): feed it to booth_tts.py --once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Booth/Booth.h"
#include "BoothAudit.h"
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
static double gDwell = -1;  // --game: seconds on the call screen (-1: not a played game)

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
  std::map<std::string, std::string> talkKind;  // a talk line's kind (the cross-game list says where a repeat comes from)
  std::map<std::string, int> wordsBy;           // words said per voice (PBP, COLOR, REF)
  std::map<std::string, int> refCount;          // the referee's announcements (not talk: counted on their own)
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
  int liveAlt = 0, liveGeneric = 0, liveBare = 0;  // live calls with a generic stand-in / generic themselves / neither
  std::map<std::string, int> bareTags;
  std::set<std::string> genericSet = [] {
    std::set<std::string> s;
    for (const auto& gc : Booth::genericCalls()) s.insert(gc.first);
    return s;
  }();
  std::set<std::string> staticSet;
  std::vector<std::string> playCalls;  // what PBP said since the snap (checked against the play's result)
  audit::Audit audit;                  // everything else said, checked against the play and the game (BoothAudit.h)
  // the talk's sentence frames: a sentence with its names and numbers masked ("N picks up # yards, out to the #."), what
  // an ear hears as the same line said again even when the man or the yard line differs
  std::set<std::string> properWords;
  std::map<std::string, int> frameCount;
  void learnNames() {
    auto addWords = [this](const std::string& s) {
      std::string w;
      for (const char c : s + " ") {
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '\'' || c == '.') w += c;
        else if (!w.empty()) {
          properWords.insert(w);
          w.clear();
        }
      }
    };
    for (const Team* t : {g.home, g.away}) {
      addWords(t->city);
      addWords(t->name);
      for (const auto& p : t->roster) {
        addWords(p->first);
        addWords(p->last);
      }
    }
    addWords(b.crew().pbpFirst + " " + b.crew().pbpLast + " " + b.crew().colorFirst + " " + b.crew().colorLast);
  }
  std::vector<std::string> frames(const std::string& text) const {
    std::vector<std::string> out;
    std::string cur, w;
    auto flushWord = [&]() {
      if (w.empty()) return;
      std::string k = w;
      while (!k.empty() && (k.back() == '.' || k.back() == '\'')) k.pop_back();
      if (k.size() > 2 && k.compare(k.size() - 2, 2, "'s") == 0) k.erase(k.size() - 2);
      cur += properWords.count(k) ? std::string("N") : w;
      w.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
      const char c = text[i];
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '\'') w += c;
      else {
        flushWord();
        if (std::isdigit(static_cast<unsigned char>(c))) {
          if (cur.empty() || cur.back() != '#') cur += '#';
        } else cur += c;
        if ((c == '.' || c == '!' || c == '?') && (i + 1 == text.size() || text[i + 1] == ' ')) {
          while (!cur.empty() && cur[0] == ' ') cur.erase(0, 1);
          if (cur.size() > 3) out.push_back(cur);
          cur.clear();
        }
      }
    }
    flushWord();
    while (!cur.empty() && cur[0] == ' ') cur.erase(0, 1);
    if (cur.size() > 3) out.push_back(cur);
    return out;
  }
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
        audit.line(*l, g, v.stage, b.now());
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
          // a call the voice can always say on a cold cache: generic itself, or with a generic stand-in (Line::alt)
          if (!l->alt.empty()) ++liveAlt;
          else if (genericSet.count(l->text)) ++liveGeneric;
          else {
            ++liveBare;
            bareTags[l->tag.substr(0, l->tag.find(':'))]++;
          }
        }
        if (!l->live && l->kind != booth::Kind::Ref)
          for (const std::string& f : frames(l->text)) frameCount[f]++;
        if (l->kind == booth::Kind::Ref) refCount[l->text]++;  // (the referee's formula is the same every time, like a live call)
        else if (!l->live) {
          talkCount[l->text]++;
          talkKind[l->text] = std::string(booth::whoName(l->who)) + " " + booth::kindName(l->kind);
        }
        wordsBy[booth::whoName(l->who)] += static_cast<int>(std::count(l->text.begin(), l->text.end(), ' ') + 1);
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
    for (const booth::Line& d : b.takeDropped()) audit.dropped(d);
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
    if (gDwell >= 0) {
      // a played game (MatchDirector Presnap): the user's offense snaps when he presses, the booth or not; the CPU
      // offense against him snaps at its timer and only once the booth lets it (BoothHold, at most 45 s here)
      const bool userOff = g.poss == TeamKey::Home;
      v.cpuOffense = !userOff;
      run(userOff ? 2.0 : 7.0);
      for (double held = 0; !userOff && b.wantsHold() && held < 45; held += 1.0 / 30) frame(1.0 / 30);
      return;
    }
    run(base);
    double held = 0;
    while (v.canHold && b.wantsHold() && held < 8) {
      frame(1.0 / 30);
      held += 1.0 / 30;
    }
  }
  // the Game before it rolls a kick (preKick): the audit's window for the kick starts there
  PerTeam<int> kickScore{};
  size_t kickLog = 0;
  void preKick() {
    kickScore = g.score;
    kickLog = g.log.size();
  }
  void livePlay(PlaySim& sim, int kickKind = 0) {
    playCalls.clear();
    if (sim.isReturn()) audit.kick(g, kickKind, kickScore, kickLog, b.now());
    else audit.snap(g, b.now());
    stage(View::Stage::Live, &sim);
    int n = 0;
    while (sim.isLive() && n++ < 60 * 40) {
      sim.step(SIM_DT);
      frame(SIM_DT);
    }
    ++plays;
  }
  void kick(const SpecialResult& r, int kind, bool pat = false) {
    audit.kick(g, pat ? 3 : kind, kickScore, kickLog, b.now());
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
  void liveReturn(std::vector<Player*> kickU, std::vector<Player*> recvU, int kickKind) {
    auto sim = g.kickReturnSim(recvU, kickU);
    stage(View::Stage::Presnap, sim.get());
    frame(1.0 / 30);
    livePlay(*sim, kickKind);
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
  int teamH = -1, teamA = -1;
  bool memory = true, xrep = false, ref = true;
  int xrepN = 60;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--hold")) hold = true;
    else if (!std::strcmp(argv[i], "--quiet")) quiet = true;
    else if (!std::strcmp(argv[i], "--nomemory")) memory = false;
    else if (!std::strncmp(argv[i], "--xrep", 6)) {
      xrep = true;
      if (argv[i][6] == '=') xrepN = std::atoi(argv[i] + 7);
    }
    else if (!std::strcmp(argv[i], "--noref")) ref = false;
    else if (!std::strncmp(argv[i], "--game", 6) && (argv[i][6] == '=' || argv[i][6] == 0)) gDwell = argv[i][6] == '=' ? std::atof(argv[i] + 7) : 6.0;
    else if (!std::strcmp(argv[i], "--modes")) {
    }
    else if (!std::strncmp(argv[i], "--teams=", 8)) {
      teamH = std::atoi(argv[i] + 8);
      const char* comma = std::strchr(argv[i] + 8, ',');
      teamA = comma ? std::atoi(comma + 1) : -1;
    } else if (!std::strncmp(argv[i], "--bake", 6)) {
    }
    else if (!std::strncmp(argv[i], "--games=", 8)) games = std::atoi(argv[i] + 8);
    else if (!std::strncmp(argv[i], "--jsonl=", 8)) jsonPath = argv[i] + 8;
    else if (!std::strncmp(argv[i], "--writer=", 9)) gWriterModel = argv[i] + 9;
    else if (!std::strncmp(argv[i], "--writerlag=", 12)) gWriterLag = std::atof(argv[i] + 12);
    else seed = static_cast<uint32_t>(std::atoi(argv[i]));
  }
  // --modes: this run again in each mode (one process each: the cross-game memory starts fresh for every mode), the
  // audit's timing line and failures by class side by side
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--modes")) {
      char exe[MAX_PATH] = {};
      GetModuleFileNameA(nullptr, exe, MAX_PATH);
      std::string rest;
      for (int k = 1; k < argc; ++k) {
        const char* a = argv[k];
        if (!std::strcmp(a, "--modes") || !std::strcmp(a, "--hold") || !std::strcmp(a, "--quiet") || !std::strncmp(a, "--jsonl=", 8) ||
            (!std::strncmp(a, "--game", 6) && (a[6] == '=' || a[6] == 0)))
          continue;
        rest += std::string(" ") + a;
      }
      const char* const modes[] = {"", " --hold", " --game=0", " --game=3", " --game=6"};
      const char* const names[] = {"no hold", "hold", "game=0", "game=3", "game=6"};
      for (int m = 0; m < 5; ++m) {
        const std::string cmd = "\"\"" + std::string(exe) + "\"" + rest + modes[m] + " --quiet\"";
        FILE* p = _popen(cmd.c_str(), "r");
        if (!p) continue;
        char buf[4096];
        while (std::fgets(buf, sizeof buf, p))
          if (!std::strncmp(buf, "TIMING", 6) || !std::strncmp(buf, "AUDIT", 5) || !std::strncmp(buf, "SUMMARY", 7)) std::printf("%-8s %s", names[m], buf);
        _pclose(p);
        std::fflush(stdout);
      }
      return 0;
    }
  // --banks: the authored phrase banks (Booth/BoothBanks.inl) and their sizes
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--banks")) {
      auto sizes = booth::bankSizes();
      std::sort(sizes.begin(), sizes.end());
      size_t total = 0;
      for (const auto& kv : sizes) {
        std::printf("%-18s %3zu\n", kv.first.c_str(), kv.second);
        total += kv.second;
      }
      std::printf("BANKS %zu banks, %zu wordings\n", sizes.size(), total);
      for (const std::string& d : booth::bankDuplicates()) std::printf("DUPLICATE bank id: %s\n", d.c_str());
      return booth::bankDuplicates().empty() ? 0 : 1;
    }
  FILE* jf = jsonPath ? std::fopen(jsonPath, "wb") : nullptr;
  // --bake: no game, just every line the voice cache can hold for good: the generic calls, then each team's regulars'
  // live calls (the league is fixed: createLeague(2027)), the default Play Now matchup (3 v 7) first.
  // --bake-all: every pairing (home and away, 32 x 31): a man's own calls are the same in every game, but a sack call
  // names the other side's quarterback and a shared surname is said in full only against the team that shares it. The
  // order is what a bake stopped part way should already have: the generics, the default matchup, every team's own
  // calls (one pairing a team), then the calls only other pairings add (a miss on those has its generic stand-in).
  for (int i = 1; i < argc; ++i)
    if (!std::strcmp(argv[i], "--bake") || !std::strcmp(argv[i], "--bake-all")) {
      const bool all = !std::strcmp(argv[i], "--bake-all");
      if (!jf) {
        std::printf("%s needs --jsonl=path\n", argv[i]);
        return 1;
      }
      League league = createLeague(2027);
      std::set<std::string> done;
      size_t wordCount = 0;
      auto emit = [&](const std::string& text, int energy) {
        const std::string say = Booth::spoken(text);
        if (!done.insert(std::to_string(energy) + "|" + say).second) return;
        wordCount += static_cast<size_t>(std::count(say.begin(), say.end(), ' ') + 1);
        std::fprintf(jf, "{\"t\":0,\"who\":\"PBP\",\"kind\":\"live\",\"energy\":%d,\"text\":\"%s\",\"say\":\"%s\"}\n", energy, Show::esc(text).c_str(),
                     Show::esc(say).c_str());
      };
      for (const auto& gc : Booth::genericCalls()) emit(gc.first, gc.second);
      const size_t generic = done.size();
      std::vector<std::pair<int, int>> pairs = {{3, 7}};
      if (all) {
        pairs.push_back({7, 3});
        for (int t = 0; t < 32; ++t) pairs.push_back({t, (t + 1) % 32});
        for (int h = 0; h < 32; ++h)
          for (int a = 0; a < 32; ++a)
            if (h != a) pairs.push_back({h, a});
      } else
        for (int t = 0; t < 32; t += 2) pairs.push_back({t, t + 1});
      size_t ownCalls = 0;
      std::set<std::pair<int, int>> seen;
      for (auto [h, a] : pairs) {
        if (!seen.insert({h, a}).second) continue;
        Game g(*league[h], *league[a]);
        Booth b(1);
        View v;
        v.stage = View::Stage::Wait;
        b.tick(0.1, g, v);  // the open: the booth learns the rosters (names two teammates share)
        for (const auto& rc : b.rosterCalls(g)) emit(rc.first, rc.second);
        if (seen.size() == 34 || (!all && seen.size() == pairs.size())) ownCalls = done.size();
      }
      std::fclose(jf);
      std::printf("baked %zu lines: %zu generic, %zu more for every team's own calls, %zu more only other pairings add (about %zu words)\n", done.size(), generic,
                  ownCalls - generic, done.size() - ownCalls, wordCount);
      return 0;
    }
  double totTalk = 0, totTime = 0;
  int totLines = 0, totRepeats = 0, totDistinct = 0, totPlays = 0, totAnalysis = 0, totExpired = 0, totCut = 0, totQueued = 0;
  // the memory across games (Booth::enableMemory, Game/TagRecent.h kept in this process): the share of the talk's
  // sentences in each game that an earlier game of the run already said
  std::set<std::string> heardBefore;
  int crossSentences = 0, crossHeard = 0;
  std::map<std::string, int> crossRep;           // --xrep: a sentence an earlier game said -> times heard again
  std::map<std::string, std::string> crossKind;  // ... and the voice and kind of the line it was in
  std::map<std::string, int> totWords;           // words per voice, every game
  int totRef = 0;                                // the referee's announcements, every game
  int totAudit[audit::kClasses] = {};            // the audit's failures by class, every game
  audit::Timing totTiming;                       // ... and its timing counts
  std::set<std::string> framesBefore;            // sentence frames an earlier game said
  int totFrames = 0, totFrameRep = 0, totFrameOld = 0, totFrameCross = 0;
  auto sentences = [](const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t k = 0; k < text.size(); ++k) {
      cur += text[k];
      const bool end = (text[k] == '.' || text[k] == '!' || text[k] == '?') && (k + 1 == text.size() || text[k + 1] == ' ');
      if (end) {
        while (!cur.empty() && cur[0] == ' ') cur.erase(0, 1);
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
      }
    }
    while (!cur.empty() && cur[0] == ' ') cur.erase(0, 1);
    if (!cur.empty()) out.push_back(cur);
    return out;
  };
  for (int gi = 0; gi < games; ++gi) {
    League league = createLeague(2027);
    Rng pickRng(seed + gi * 7919);
    int hi = pickRng.intRange(0, 31);
    int ai = pickRng.intRange(0, 31);
    if (ai == hi) ai = (ai + 1) % 32;
    if (teamH >= 0 && teamH < 32 && teamA >= 0 && teamA < 32 && teamH != teamA) {
      hi = teamH;
      ai = teamA;
    }
    GameOptions o;
    o.seed = seed + gi * 104729;
    Game g(*league[hi], *league[ai], o);
    Booth b(seed * 31 + gi);
    b.enableWriter(!gWriterModel.empty());
    b.enableMemory(memory);
    b.enableReferee(ref);
    Show sh{g, b};
    sh.quiet = quiet;
    sh.jsonl = jf;
    // a played game (--game): the director's Wait holds Next() while the booth asks (what it always holds for: the open,
    // a break, the referee, a score), the Presnap the CPU's snap; the booth's own talk never holds (canHold off)
    sh.v.canHold = hold && gDwell < 0;
    sh.v.userGame = gDwell >= 0;
    const double waitHold = gDwell >= 0 ? 45.0 : hold ? 6.0 : 0.0, kickHold = gDwell >= 0 || hold ? 45.0 : 0.0;
    b.enableDropLog(true);
    sh.v.night = (seed + gi) % 3 == 0;
    sh.audit.quiet = quiet;
    sh.audit.start(g);
    sh.learnNames();
    if (!quiet) std::printf("==== GAME %d: %s %s at %s %s ====\n", gi, g.away->city.c_str(), g.away->name.c_str(), g.home->city.c_str(), g.home->name.c_str());
    sh.stage(View::Stage::Wait, nullptr);
    sh.run(0.5);
    sh.run(2.0);
    for (double held = 0; b.wantsHold() && held < 45; held += 1.0 / 30) sh.frame(1.0 / 30);  // the kickoff waits for the open
    int guard = 0;
    bool twoMinShown[6] = {};
    while (!g.isFinal() && guard++ < 3000) {
      PlaySim* last = sh.holdSim.get();
      if (g.phase == GamePhase::Kickoff) {
        // (the director's Wait holds Next() while the booth finishes: a score, the drive, the free-kick note)
        for (double held = 0; b.wantsHold() && held < kickHold; held += 1.0 / 30) sh.frame(1.0 / 30);
        if (g.clock <= 0) {
          const int qWas = g.q;
          g.simStep();
          if (qWas == 2 && g.q == 3) {
            // halftime (MatchBroadcast BeginHalftimeShow): the show's stages, then the wide holds on the kickoff units
            // while the booth finishes (Wait: BoothHold); a player who skips sees the short beat only
            sh.run(gDwell >= 0 || hold ? 13.5 : 3.0);
            for (double held = 0; b.wantsHold() && held < kickHold; held += 1.0 / 30) sh.frame(1.0 / 30);
          }
          continue;
        }
        const TeamKey kt = g.kicking;
        auto kickU = pick11(g.teamOf(kt), g, "K");
        auto recvU = pick11(g.teamOf(other(kt)), g, "KR");
        sh.preKick();
        const SpecialResult r = g.kickoff(false, true);
        if (g.pendingReturn) sh.liveReturn(kickU, recvU, 0);
        else sh.kick(r, 0);
        continue;
      }
      if (g.phase == GamePhase::Pat) {
        // (the director's Wait after the touchdown holds Next() - the try - as before any snap)
        for (double held = 0; b.wantsHold() && held < waitHold; held += 1.0 / 30) sh.frame(1.0 / 30);
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
          sh.preKick();
          const SpecialResult r = g.extraPoint();
          sh.kick(r, 2, true);
        }
        continue;
      }
      const int qBefore = g.q;
      sh.stage(View::Stage::Wait, last);
      // the director's Wait holds Next() while the booth finishes (a watched game; the open and the breaks always)
      for (double held = 0; b.wantsHold() && held < waitHold; held += 1.0 / 30) sh.frame(1.0 / 30);
      sh.preKick();  // (a timeout the offense takes now comes before a kick decided on this down)
      if (!g.preSnap()) {
        sh.run(qBefore != g.q ? 3.0 : 1.2);
        for (double held = 0; b.wantsHold() && held < 45; held += 1.0 / 30) sh.frame(1.0 / 30);
        continue;
      }
      if ((g.q == 2 || g.q == 4) && g.twoMin[g.q] && !twoMinShown[g.q] && g.phase == GamePhase::Scrimmage) {
        // the two-minute warning (MatchDirector Next): its break before the next call, the Wait holding for the booth
        twoMinShown[g.q] = true;
        sh.stage(View::Stage::Wait, last);
        sh.run(3.0);
        for (double held = 0; b.wantsHold() && held < waitHold; held += 1.0 / 30) sh.frame(1.0 / 30);
      }
      if (g.down == 4) {
        const FourthDownCall c = g.fourthDownCall();
        if (c == FourthDownCall::Punt) {
          auto kickU = pick11(g.offense(), g, "P");
          auto recvU = pick11(g.defense(), g, "KR");
          const SpecialResult r = g.punt(true);
          if (g.pendingReturn) sh.liveReturn(kickU, recvU, 1);
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
      sh.run(gDwell >= 0 ? gDwell : 0.4);
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
      sh.audit.play(r, *sim);
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
    sh.audit.finish();
    {
      std::printf("audit:");
      for (int c = 0; c < audit::kClasses; ++c) {
        std::printf(" %s %d", audit::clsName(c), sh.audit.fails[c]);
        totAudit[c] += sh.audit.fails[c];
      }
      std::printf(" | total %d (catches by a man the throw was not aimed at, untouched: %d)\n", sh.audit.total(), sh.audit.nonTargetCatches);
      sh.audit.tm.print("timing:");
      totTiming.add(sh.audit.tm);
      // repeated frames: within this game, and heard in an earlier game of the run
      int fr = 0, frAll = 0, frOld = 0;
      for (const auto& kv : sh.frameCount) {
        frAll += kv.second;
        if (kv.second > 1) fr += kv.second - 1;
        if (framesBefore.count(kv.first)) frOld += kv.second;
      }
      totFrames += frAll;
      totFrameRep += fr;
      if (gi > 0) {
        totFrameOld += frOld;
        totFrameCross += frAll;
      }
      for (const auto& kv : sh.frameCount) framesBefore.insert(kv.first);
      std::printf("frames: %d talk sentences, repeated in this game %d (%.1f%%), heard in an earlier game %s\n", frAll, fr, 100.0 * fr / (std::max)(1, frAll),
                  gi == 0 ? "n/a" : (std::to_string(frOld) + " (" + std::to_string(static_cast<int>(1000.0 * frOld / (std::max)(1, frAll)) / 10.0).substr(0, 4) + "%)").c_str());
      if (!quiet) {
        std::vector<std::pair<int, std::string>> top;
        for (const auto& kv : sh.frameCount)
          if (kv.second > 1) top.push_back({kv.second, kv.first});
        std::sort(top.rbegin(), top.rend());
        std::printf("most repeated frames:\n");
        for (size_t i = 0; i < top.size() && i < 15; ++i) std::printf("  %2d x %s\n", top[i].first, top[i].second.c_str());
      }
    }
    if (b.writerOn()) {
      const auto ws = b.writerStats();
      std::printf("writer: asked %d, used %d, rejected %d (fact-check veto %d), stale %d; %d calls, %.0f s wall (%.1f s each)\n", ws.asked, ws.used, ws.rejected,
                  sh.writerVetoed, ws.stale, sh.writerCalls,
                  sh.writerWall, sh.writerWall / (std::max)(1, sh.writerCalls));
    }
    std::printf("predicted voiced (warm league cache): %d of %d (%.0f%%)\n", sh.predVoiced, sh.predLines, 100.0 * sh.predVoiced / (std::max)(1, sh.predLines));
    std::printf("live calls %d, prewarmable %d (%.0f%%), in the kickoff prewarm %d (%.0f%%; %zu lines)\n", sh.liveLines, sh.liveHit,
                100.0 * sh.liveHit / (std::max)(1, sh.liveLines), sh.liveStatic, 100.0 * sh.liveStatic / (std::max)(1, sh.liveLines), sh.staticSet.size());
    {
      std::string bare;
      for (const auto& kv : sh.bareTags) bare += " " + kv.first + "=" + std::to_string(kv.second);
      std::printf("live calls voiceable on a cold cache: %d of %d (%.1f%%): generic %d, named with a generic stand-in %d; neither %d%s\n",
                  sh.liveGeneric + sh.liveAlt, sh.liveLines, 100.0 * (sh.liveGeneric + sh.liveAlt) / (std::max)(1, sh.liveLines), sh.liveGeneric, sh.liveAlt, sh.liveBare,
                  bare.c_str());
    }
    std::printf("talk lines %d, repeated %d (%.1f%%)\n", talkLines, talkRepeats, 100.0 * talkRepeats / (std::max)(1, talkLines));
    {
      // the talk's sentences (every time said) that an earlier game of this run said word for word
      int sent = 0, old = 0;
      std::set<std::string> mine;
      for (const auto& kv : sh.talkCount)
        for (const std::string& s : sentences(kv.first)) {
          sent += kv.second;
          if (heardBefore.count(s)) {
            old += kv.second;
            crossRep[s] += kv.second;
            crossKind[s] = sh.talkKind[kv.first];
          }
          mine.insert(s);
        }
      if (gi > 0) {
        crossSentences += sent;
        crossHeard += old;
      }
      if (gi == 0)
        std::printf("cross-game: game 0 talk sentences %d, said in an earlier game n/a (the first game of the run)\n", sent);
      else
        std::printf("cross-game: game %d talk sentences %d, said in an earlier game %d (%.1f%%)%s\n", gi, sent, old, 100.0 * old / (std::max)(1, sent),
                    memory ? "" : " [no memory]");
      heardBefore.insert(mine.begin(), mine.end());
    }
    for (const auto& kv : sh.textCount)
      if (kv.second > 1) repeats += kv.second - 1;
    {
      int all = 0;
      for (const auto& kv : sh.wordsBy) {
        all += kv.second;
        totWords[kv.first] += kv.second;
      }
      int refLines = 0;
      for (const auto& kv : sh.refCount) refLines += kv.second;
      totRef += refLines;
      std::printf("words: analyst %d of %d (%.1f%%), referee %d (%d announcements)\n", sh.wordsBy["COLOR"], all, 100.0 * sh.wordsBy["COLOR"] / (std::max)(1, all),
                  sh.wordsBy["REF"], refLines);
    }
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
  {
    int all = 0;
    for (const auto& kv : totWords) all += kv.second;
    std::printf("WORDS analyst %.1f%% of %d, referee %d (%.1f%%) in %d announcements\n", 100.0 * totWords["COLOR"] / (std::max)(1, all), all, totWords["REF"],
                100.0 * totWords["REF"] / (std::max)(1, all), totRef);
  }
  {
    int all = 0;
    std::printf("AUDIT");
    for (int c = 0; c < audit::kClasses; ++c) {
      std::printf(" %s %d", audit::clsName(c), totAudit[c]);
      all += totAudit[c];
    }
    std::printf(" | total %d\n", all);
    const std::string mode = gDwell >= 0 ? "game=" + std::to_string(static_cast<int>(gDwell)) : hold ? std::string("hold") : std::string("no-hold");
    totTiming.print(("TIMING " + mode + " |").c_str());
    std::printf("FRAMES repeated in-game %.1f%% of %d", 100.0 * totFrameRep / (std::max)(1, totFrames), totFrames);
    if (games > 1) std::printf(", heard in an earlier game %.1f%%", 100.0 * totFrameOld / (std::max)(1, totFrameCross));
    std::printf("\n");
  }
  if (games > 1)
    std::printf("CROSS-GAME games 2..%d: %d talk sentences, %d said in an earlier game (%.1f%%), memory %s\n", games, crossSentences, crossHeard,
                100.0 * crossHeard / (std::max)(1, crossSentences), memory ? "on" : "off");
  if (xrep) {
    // where the cross-game repeats come from: by kind, then the sentences heard again most
    std::map<std::string, int> byKind;
    for (const auto& kv : crossRep) byKind[crossKind[kv.first]] += kv.second;
    std::printf("cross-game repeats by line kind:");
    for (const auto& kv : byKind) std::printf(" [%s] %d", kv.first.c_str(), kv.second);
    std::printf("\n");
    std::vector<std::pair<int, std::string>> rep;
    for (const auto& kv : crossRep) rep.push_back({kv.second, kv.first});
    std::sort(rep.rbegin(), rep.rend());
    for (size_t i = 0; i < rep.size() && i < static_cast<size_t>(xrepN); ++i) std::printf("  %3d x %-15s %s\n", rep[i].first, crossKind[rep[i].second].c_str(), rep[i].second.c_str());
  }
  return 0;
}
