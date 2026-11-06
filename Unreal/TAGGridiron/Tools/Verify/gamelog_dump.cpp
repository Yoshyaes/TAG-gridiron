// Verification-only: C++ twin of gamelog_dump.ts. Build with RefMathFI.h forced-included (see verify_vs_ts.bat).
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Game.h"
#include "League.h"

using namespace tag;

static std::string hx(double v) {
  unsigned long long u;
  std::memcpy(&u, &v, 8);
  char b[32];
  std::snprintf(b, sizeof b, "%llx", u);
  return b;
}

int main(int argc, char** argv) {
  _setmode(_fileno(stdout), _O_BINARY);  // LF line endings, like node
  const int N = argc > 1 ? std::atoi(argv[1]) : 3;
  Rng rng(7);
  League league = createLeague(2027);
  bool first = true;
  for (int g = 0; g < N; g++) {
    const int hi = rng.intRange(0, 31);
    int ai = rng.intRange(0, 31);
    if (ai == hi) ai = (ai + 1) % 32;
    GameOptions o;
    o.seed = static_cast<uint32_t>(rng.intRange(1, 1000000000));
    Game game(*league[hi], *league[ai], o);
    game.simToEnd();
    std::printf("%sGAME %d %s %d %s %d log %zu", first ? "" : "\n", g, game.away->abbr.c_str(), game.score.away, game.home->abbr.c_str(),
                game.score.home, game.log.size());
    first = false;
    for (const PlayLog& l : game.log)
      std::printf("\n%d %s %s %d %s %s %s %d-%d | %s", l.q, hx(l.clock).c_str(), teamKeyName(l.poss), l.down, hx(l.toGo).c_str(),
                  hx(l.ballOn).c_str(), logTagName(l.big), l.score.home, l.score.away, l.desc.c_str());
  }
  std::printf("\n");
  return 0;
}
