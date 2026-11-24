// Golden sim digest: 64 fixed games played headless, every play-by-play line hashed and compared with the committed
// copy (Tools/QA/golden_sim.txt, one digest a game, and Tools/QA/golden_sim_plays.txt, the lines themselves). An
// unrelated change that moves a football outcome, a non-deterministic sim (each game is played twice in this process)
// and compiler or flag drift all show up here. On a mismatch it names the first game and the first line that differ and
// prints both versions of that line (after the first divergence the rest of a game is chaos: only the first matters).
//   build\SimDigest.exe                     check against the committed digest (exit 0 match, 1 mismatch, 2 usage / no file,
//                                           3 the same game played twice in one process came out different,
//                                           4 a rules invariant broken - Sim/GameInvariants.h, checked on every action)
//   build\SimDigest.exe --bless "reason"    re-bless: write both files from this build (the reason goes in the header)
//   build\SimDigest.exe --set name=value    override a Tune constant first (a 1 % nudge must name the first play it moved)
//   build\SimDigest.exe --dir <folder>      where the two files are (default: ..\QA beside this exe's build folder)
//   build\SimDigest.exe --dump <game>       print that game's lines as this build plays them
// The 64 games: the 32 teams each at home once against (5i + 11) mod 32, at 15-minute and 5-minute quarters, on fixed
// seeds, each with a league of its own (createLeague(2027): injuries in one game must not carry into the next).
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Game.h"
#include "GameInvariants.h"
#include "League.h"
#include "PlaySim.h"

using namespace tag;

namespace {

constexpr int kGames = 64;

struct GameSpec {
  int home = 0, away = 0;
  double qlen = 900;
  uint32_t seed = 0;
};

GameSpec spec(int i) {
  GameSpec s;
  const int p = i % 32;
  s.home = p;
  s.away = (5 * p + 11) % 32;  // (never the home team: 4p + 11 is odd; 5 is prime to 32, so every team is away once)
  s.qlen = i < 32 ? 900 : 300;
  s.seed = 104729u + static_cast<uint32_t>(i) * 7919u;
  return s;
}

std::string fmtD(double v) {
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", v);  // (round-trips: equal text is equal bits)
  return buf;
}

/** One play-by-play line as text: the state the game logged with it and what it said. */
std::string record(const PlayLog& l) {
  std::string s = std::to_string(l.q) + " " + fmtD(l.clock) + " " + (l.poss == TeamKey::Home ? "H" : "A") + " " + std::to_string(l.down) + " " +
                  fmtD(l.toGo) + " " + fmtD(l.ballOn) + " " + std::to_string(l.score.away) + "-" + std::to_string(l.score.home) + " " +
                  (l.big == LogTag::None ? "-" : logTagName(l.big)) + " | " + l.desc;
  for (char& c : s)
    if (c == '\n' || c == '\r') c = ' ';
  return s;
}

std::string boxRecord(const Game& g) {
  std::string s = "box";
  for (TeamKey k : {TeamKey::Away, TeamKey::Home}) {
    const TeamLine& T = g.team[k];
    s += std::string(" ") + (k == TeamKey::Home ? "H" : "A");
    for (int v : {T.plays, T.yards, T.passYds, T.rushYds, T.passAtt, T.passComp, T.rushAtt, T.firstDowns, T.thirdAtt, T.thirdConv, T.penalties,
                  T.penYds, T.turnovers, T.sacks, T.punts, T.fga, T.fgm, T.ints, T.fumblesLost, g.timeouts[k]})
      s += " " + std::to_string(v);
    s += " " + fmtD(T.top);
  }
  return s;
}

uint64_t fnv(uint64_t h, const std::string& s) {
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  h ^= 0xff;  // (a record boundary: "ab" + "c" and "a" + "bc" hash apart)
  h *= 1099511628211ull;
  return h;
}

struct Played {
  GameSpec sp;
  std::string home, away;
  std::vector<std::string> lines;
  int scoreHome = 0, scoreAway = 0;
  uint64_t hash = 0;
  std::vector<std::string> broken;  // rules invariants the game broke (they do not enter the digest)
};

Played play(int i) {
  Played out;
  out.sp = spec(i);
  League league = createLeague(2027);
  GameOptions o;
  o.seed = out.sp.seed;
  o.quarterLen = out.sp.qlen;
  Game g(*league[out.sp.home], *league[out.sp.away], o);
  // (Game::simToEnd's own loop, with the rules checked after every action: Sim/GameInvariants.h)
  GameInvariants inv(g);
  for (int n = 0; g.phase != GamePhase::Final && n < 2000; ++n) {
    g.simStep();
    inv.afterStep(g);
  }
  out.broken = inv.violations;
  out.home = g.home->abbr;
  out.away = g.away->abbr;
  out.scoreHome = g.score.home;
  out.scoreAway = g.score.away;
  for (const PlayLog& l : g.log) out.lines.push_back(record(l));
  out.lines.push_back(boxRecord(g));
  uint64_t h = 1469598103934665603ull;
  for (const std::string& s : out.lines) h = fnv(h, s);
  out.hash = h;
  return out;
}

std::string hex(uint64_t h) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%016" PRIx64, h);
  return buf;
}

std::string digestLine(int i, const Played& p) {
  char buf[256];
  std::snprintf(buf, sizeof buf, "%d %u %s %s %.0f %zu %d-%d %s", i, p.sp.seed, p.home.c_str(), p.away.c_str(), p.sp.qlen, p.lines.size(), p.scoreAway,
                p.scoreHome, hex(p.hash).c_str());
  return buf;
}

struct Golden {
  std::string blessed;
  std::vector<std::string> digest;               // by game, "" if missing
  std::vector<std::vector<std::string>> lines;   // by game
};

bool readGolden(const std::filesystem::path& dir, Golden& g) {
  g.digest.assign(kGames, std::string());
  g.lines.assign(kGames, {});
  std::ifstream d(dir / "golden_sim.txt");
  if (!d) return false;
  std::string s;
  while (std::getline(d, s)) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    if (s.rfind("# blessed: ", 0) == 0) g.blessed = s.substr(11);
    if (s.empty() || s[0] == '#') continue;
    const int i = std::atoi(s.c_str());
    if (i >= 0 && i < kGames) g.digest[static_cast<size_t>(i)] = s;
  }
  std::ifstream pl(dir / "golden_sim_plays.txt");
  int cur = -1;
  while (pl && std::getline(pl, s)) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    if (s.rfind("= ", 0) == 0) {
      cur = std::atoi(s.c_str() + 2);
      continue;
    }
    if (s.empty() || s[0] == '#') continue;
    if (cur >= 0 && cur < kGames) g.lines[static_cast<size_t>(cur)].push_back(s);
  }
  return true;
}

bool writeGolden(const std::filesystem::path& dir, const std::vector<Played>& games, const std::string& reason, const std::string& sets) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream d(dir / "golden_sim.txt", std::ios::binary);
  std::ofstream pl(dir / "golden_sim_plays.txt", std::ios::binary);
  if (!d || !pl) return false;
  d << "# TAG Gridiron golden sim digest (Tools/SimDigest.cpp): 64 headless games, every play-by-play line hashed.\n";
  d << "# Check: build\\SimDigest.exe   Re-bless on purpose: build\\SimDigest.exe --bless \"<one-line reason>\"\n";
  d << "# blessed: " << reason << sets << "\n";
  d << "# game seed home away quarter_s lines away-home fnv1a64\n";
  pl << "# The play-by-play behind golden_sim.txt: per game, every log line as \"q clock poss down toGo ballOn away-home tag | desc\",\n";
  pl << "# then the box (Tools/SimDigest.cpp). Diff two blessings of it to see which plays a change moved.\n";
  for (size_t i = 0; i < games.size(); ++i) {
    d << digestLine(static_cast<int>(i), games[i]) << "\n";
    pl << "= " << i << " " << games[i].away << " at " << games[i].home << "\n";
    for (const std::string& s : games[i].lines) pl << s << "\n";
  }
  return static_cast<bool>(d) && static_cast<bool>(pl);
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path dir;
  std::string bless, sets;
  bool doBless = false;
  int dump = -1;
  for (int i = 1; i < argc; i++) {
    if ((std::strcmp(argv[i], "--bless") == 0 || std::strcmp(argv[i], "--update") == 0) && i + 1 < argc) {
      doBless = true;
      bless = argv[++i];
    } else if (std::strcmp(argv[i], "--set") == 0 && i + 1 < argc) {
      const std::string kv = argv[++i];
      const size_t eq = kv.find('=');
      if (eq == std::string::npos || !setTune(kv.substr(0, eq), std::atof(kv.c_str() + eq + 1))) {
        std::fprintf(stderr, "unknown tune %s\n", kv.c_str());
        return 2;
      }
      sets += " [--set " + kv + "]";
    } else if (std::strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
      dir = argv[++i];
    } else if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
      dump = std::atoi(argv[++i]);
    } else {
      std::fprintf(stderr,
                   "usage: SimDigest [--bless \"reason\"] [--set name=value] [--dir <folder>] [--dump <game>]\n"
                   "  (no arguments: check this build against Tools/QA/golden_sim.txt)\n");
      return 2;
    }
  }
  if (dir.empty()) {
    // (beside the build folder this exe is in: Tools\build\SimDigest.exe -> Tools\QA)
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::absolute(argv[0], ec);
    dir = exe.parent_path().parent_path() / "QA";
  }
  if (dump >= 0) {
    if (dump >= kGames) return 2;
    const Played p = play(dump);
    std::printf("%s\n", digestLine(dump, p).c_str());
    for (size_t k = 0; k < p.lines.size(); ++k) std::printf("%4zu  %s\n", k, p.lines[k].c_str());
    return 0;
  }

  // every game played twice in this process: the second must be the first exactly (no state outside the Game)
  std::vector<Played> games;
  size_t lines = 0;
  for (int i = 0; i < kGames; ++i) {
    Played a = play(i);
    const Played b = play(i);
    if (a.lines != b.lines) {
      size_t k = 0;
      while (k < a.lines.size() && k < b.lines.size() && a.lines[k] == b.lines[k]) ++k;
      std::printf("NON-DETERMINISTIC: game %d (seed %u) played twice in one process differs at line %zu\n  first : %s\n  second: %s\n", i, a.sp.seed, k,
                  k < a.lines.size() ? a.lines[k].c_str() : "(end)", k < b.lines.size() ? b.lines[k].c_str() : "(end)");
      return 3;
    }
    lines += a.lines.size();
    games.push_back(std::move(a));
  }
  // the rules invariants, checked on every action of the 64 games (the full sweep is SimTests' "rules invariants")
  size_t broken = 0;
  for (size_t i = 0; i < games.size(); ++i)
    for (const std::string& v : games[i].broken)
      if (broken++ < 5) std::printf("RULE BROKEN in game %zu: %s\n", i, v.c_str());
  if (broken) {
    std::printf("SimDigest: FAIL - %zu rules invariant violations (Sim/GameInvariants.h)\n", broken);
    return 4;
  }

  if (doBless) {
    if (bless.empty()) {
      std::fprintf(stderr, "--bless needs a one-line reason\n");
      return 2;
    }
    if (!writeGolden(dir, games, bless, sets)) {
      std::fprintf(stderr, "could not write %s\n", (dir / "golden_sim.txt").string().c_str());
      return 2;
    }
    std::printf("SimDigest: blessed %d games (%zu lines) into %s (\"%s\")\n", kGames, lines, (dir / "golden_sim.txt").string().c_str(), bless.c_str());
    return 0;
  }

  Golden gold;
  if (!readGolden(dir, gold)) {
    std::printf("SimDigest: no golden digest at %s (bless one: SimDigest --bless \"reason\")\n", (dir / "golden_sim.txt").string().c_str());
    return 2;
  }
  std::vector<int> bad;
  for (int i = 0; i < kGames; ++i)
    if (digestLine(i, games[static_cast<size_t>(i)]) != gold.digest[static_cast<size_t>(i)]) bad.push_back(i);
  if (bad.empty()) {
    std::printf("SimDigest: PASS - %d games, %zu lines, every one as blessed (%s)\n", kGames, lines, gold.blessed.c_str());
    return 0;
  }
  std::printf("SimDigest: FAIL - %zu of %d games differ from the golden digest (blessed: %s)\n", bad.size(), kGames, gold.blessed.c_str());
  std::printf("  games:");
  for (int i : bad) std::printf(" %d", i);
  std::printf("\n");
  const int i = bad[0];
  const Played& p = games[static_cast<size_t>(i)];
  const std::vector<std::string>& old = gold.lines[static_cast<size_t>(i)];
  std::printf("first divergence: game %d (seed %u, %s at %s, %.0f s quarters)\n", i, p.sp.seed, p.away.c_str(), p.home.c_str(), p.sp.qlen);
  std::printf("  golden: %s\n  now   : %s\n", gold.digest[static_cast<size_t>(i)].c_str(), digestLine(i, p).c_str());
  if (old.empty()) {
    std::printf("  (golden_sim_plays.txt has no lines for this game: the plays cannot be compared)\n");
    return 1;
  }
  size_t k = 0;
  while (k < old.size() && k < p.lines.size() && old[k] == p.lines[k]) ++k;
  if (k == old.size() && k == p.lines.size()) {
    std::printf("  (the plays file matches this build: golden_sim.txt and golden_sim_plays.txt are out of step - re-bless both)\n");
    return 1;
  }
  std::printf("first play that differs: line %zu of the game (lines before it match)\n", k);
  if (k > 0) std::printf("  before : %s\n", old[k - 1].c_str());
  std::printf("  golden : %s\n", k < old.size() ? old[k].c_str() : "(the golden game ended here)");
  std::printf("  now    : %s\n", k < p.lines.size() ? p.lines[k].c_str() : "(this build's game ended here)");
  std::printf("If the change was meant to move football outcomes: SimDigest --bless \"<why>\"\n");
  return 1;
}
