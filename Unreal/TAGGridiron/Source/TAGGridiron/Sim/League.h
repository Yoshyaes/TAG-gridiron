// Port of src/sim/league.ts
// Fictional league generation. Every team, name and player here is original.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Rng.h"
#include "Types.h"

namespace tag {

struct TeamSeed {
  const char* city;
  const char* name;
  const char* abbr;
  const char* primary;
  const char* secondary;
  const char* accent;
};

/** The 32 franchises, in league order. */
const std::vector<TeamSeed>& teamSeeds();

/** League storage: Team addresses (and their Player addresses) are stable. */
using League = std::vector<std::unique_ptr<Team>>;

int computeOvr(Pos pos, const Ratings& r);
std::unique_ptr<Player> makePlayer(Rng& rng, Pos pos, double talent, int number);
League createLeague(uint32_t seed = 2027);

struct TeamOvr {
  int off;
  int def;
  int ovr;
};
TeamOvr teamOvr(const Team& t);

}  // namespace tag
