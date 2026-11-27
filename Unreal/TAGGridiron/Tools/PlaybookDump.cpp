// Prints the playbooks: every scheme's formations and plays, and each team's book size.
//   build\PlaybookDump.exe            -- summary per scheme and team
//   build\PlaybookDump.exe --full     -- every play of every scheme (formation, name, type, routes)
//   build\PlaybookDump.exe --team AUS -- one team's book in full
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "League.h"
#include "Playbook.h"

using namespace tag;

namespace {
void printBook(const OffBook& b, bool full) {
  std::printf("%-22s %-14s %2zu formations %4zu plays\n", b.name.c_str(), b.scheme.c_str(), b.formations.size(), b.plays.size());
  if (!full) return;
  for (const std::string& f : b.formations) {
    const Formation* fm = formationById(f);
    const auto in = b.playsIn(f);
    std::printf("  %s [%s] (%zu)\n", fm ? fm->name.c_str() : f.c_str(), fm ? formationFamily(*fm).c_str() : "?", in.size());
    for (const OffPlay* p : in) {
      std::string r;
      for (const auto& kv : p->routes)
        if (kv.second != "block") r += kv.first + ":" + kv.second + " ";
      std::printf("    %-22s %-10s %s%s\n", p->name.c_str(), playTypeName(p->type), p->run ? "" : r.c_str(), p->run ? p->run->carrier.c_str() : "");
    }
  }
}
void printDef(const DefBook& b, bool full) {
  std::printf("%-22s %-14s %2zu sets %4zu calls\n", b.name.c_str(), b.scheme.c_str(), b.sets.size(), b.calls.size());
  if (!full) return;
  for (const std::string& s : b.sets) {
    const auto in = b.callsIn(s);
    std::printf("  %s (%zu)\n", s.c_str(), in.size());
    for (const DefCall* d : in) std::printf("    %-30s %-7s rush %d\n", d->name.c_str(), coverageName(d->coverage), d->rushers());
  }
}
}  // namespace

int main(int argc, char** argv) {
  bool full = false;
  std::string team;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--full") == 0) full = true;
    if (std::strcmp(argv[i], "--team") == 0 && i + 1 < argc) team = argv[++i];
  }
  std::printf("library: %zu formations, %zu offensive plays, %zu defensive calls\n\n", FORMATIONS().size(), OFF_PLAYS().size(), DEF_PLAYS().size());
  League league = createLeague(2027);
  if (!team.empty()) {
    for (const auto& t : league)
      if (t->abbr == team) {
        printBook(offBookFor(*t), true);
        printDef(defBookFor(*t), true);
      }
    return 0;
  }
  std::printf("schemes:\n");
  for (const std::string& s : OFF_SCHEMES()) printBook(offSchemeBook(s), full);
  for (const std::string& s : DEF_SCHEMES()) printDef(defSchemeBook(s), full);
  std::printf("\nteams:\n");
  for (const auto& t : league) {
    const OffBook& o = offBookFor(*t);
    const DefBook& d = defBookFor(*t);
    std::printf("%s %-26s pass %.2f %-14s %2zu/%3zu   blitz %.2f %-13s %zu/%zu\n", t->abbr.c_str(), (t->city + " " + t->name).c_str(), t->passLean,
                o.scheme.c_str(), o.formations.size(), o.plays.size(), t->blitzLean, d.scheme.c_str(), d.sets.size(), d.calls.size());
  }
  return 0;
}
