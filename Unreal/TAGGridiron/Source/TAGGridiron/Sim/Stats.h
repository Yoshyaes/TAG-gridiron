// Port of src/sim/stats.ts
// Box score / player stat accumulation.
// StatBook stores lines in a std::deque so references returned by line() stay valid while new
// lines are added (the game layer holds several at once); iteration order = insertion order,
// like the TS Map.
#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "Types.h"

namespace tag {

struct PlayerLine {
  int pid = 0;
  std::string name;
  std::string pos;
  std::string team;
  int passAtt = 0, passComp = 0, passYds = 0, passTD = 0, passInt = 0, sacked = 0, sackYds = 0;
  int rushAtt = 0, rushYds = 0, rushTD = 0, rushLong = 0, fumbles = 0, fumblesLost = 0;
  int targets = 0, rec = 0, recYds = 0, recTD = 0, recLong = 0, yac = 0;
  int tackles = 0, sacks = 0, ints = 0, pbu = 0, ff = 0, fr = 0, tfl = 0, defTD = 0;
  int fgm = 0, fga = 0, xpm = 0, xpa = 0, punts = 0, puntYds = 0, brokenTackles = 0;
};

struct TeamLine {
  int plays = 0;
  int yards = 0;
  int passYds = 0;
  int rushYds = 0;
  int rushAtt = 0;
  int passAtt = 0;
  int passComp = 0;
  int firstDowns = 0;
  int thirdAtt = 0;
  int thirdConv = 0;
  int fourthAtt = 0;
  int fourthConv = 0;
  int penalties = 0;
  int penYds = 0;
  int turnovers = 0;
  int sacks = 0;
  double top = 0;  // seconds
  int punts = 0;
  int fga = 0;
  int fgm = 0;
  int redZoneTrips = 0;
  int redZoneTD = 0;
  int explosive = 0;
  int ints = 0;
  int fumblesLost = 0;
};

inline TeamLine emptyTeamLine() { return TeamLine{}; }

class StatBook {
 public:
  /** Get (creating on first use) the line for a player. Reference stays valid. */
  PlayerLine& line(const Player& p, const std::string& team);
  /** Lines of one team, in first-touched order. */
  std::vector<const PlayerLine*> forTeam(const std::string& abbr) const;
  const std::deque<PlayerLine>& all() const { return lines_; }

 private:
  std::deque<PlayerLine> lines_;
  std::unordered_map<int, size_t> index_;
};

double passerRating(const PlayerLine& l);

}  // namespace tag
