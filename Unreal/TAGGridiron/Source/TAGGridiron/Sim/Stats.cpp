// Port of src/sim/stats.ts
#include "Stats.h"

#include <algorithm>

namespace tag {

PlayerLine& StatBook::line(const Player& p, const std::string& team) {
  auto it = index_.find(p.id);
  if (it != index_.end()) return lines_[it->second];
  PlayerLine l;
  l.pid = p.id;
  l.name = std::string(1, p.first.empty() ? '?' : p.first[0]) + ". " + p.last;
  l.pos = posName(p.pos);
  l.team = team;
  index_.emplace(p.id, lines_.size());
  lines_.push_back(l);
  return lines_.back();
}

std::vector<const PlayerLine*> StatBook::forTeam(const std::string& abbr) const {
  std::vector<const PlayerLine*> out;
  for (const PlayerLine& l : lines_)
    if (l.team == abbr) out.push_back(&l);
  return out;
}

double passerRating(const PlayerLine& l) {
  if (!l.passAtt) return 0;
  auto c = [](double a) { return (std::max)(0.0, (std::min)(2.375, a)); };
  const double att = l.passAtt;
  const double a = c((l.passComp / att - 0.3) * 5);
  const double b = c((l.passYds / att - 3) * 0.25);
  const double cc = c((l.passTD / att) * 20);
  const double d = c(2.375 - (l.passInt / att) * 25);
  return ((a + b + cc + d) / 6) * 100;
}

}  // namespace tag
