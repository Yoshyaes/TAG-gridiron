// The booth's authored phrase banks: every wording of a line the booth can say, as data (BoothBanks.inl, compiled into
// Booth.cpp), not as strings strung together in the code that decides what to say.
//
// A bank is a list of wordings for one situation ("r_1st": a first down; "a_yac": yards after the catch). A wording
// names the facts it needs in braces, filled from the sim at the moment the line is written:
//
//   "{rb} picks up {yds}, out to {to}."        -> "Carter picks up 7, out to the 41."
//   "{Dd} at {spot}, and {off} keeps it."      -> "Second and 3 at the Austin 40, ..." ({Dd}: the fact capitalized)
//
// A wording that names a fact the moment does not have (or has empty) is left out this time, so one bank can hold the
// wordings for a man named and a man not named. Booth::say() fills the bank and chooses through Booth::pick(): never
// the same words twice in a game while another way is left, and with the memory across games (Game/TagRecent.h) the
// wording heard least recently. A wording's index in its bank is its saved id, so new wordings go at the END of a bank.
//
// Adding to a bank is adding a line to its table; adding a bank is adding a table entry and one say() call. Wordings
// must be original TV football English: no real league, team, network or announcer names, and no catchphrases.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tag {
namespace booth {

/** One fact a wording may name: {k} in the wording, v the words for it ("" = not known now). */
struct Slot {
  const char* k;
  std::string v;
};
using Slots = std::vector<Slot>;

/** The wordings of a bank (empty when no table has that id). */
const std::vector<const char*>& bankWordings(const std::string& bank);

/** A wording with its facts filled in; false when it names a fact that slots does not have, or has empty. A name in
 *  capitals ({Dd}) is that fact with its first letter capitalized ({dd}). */
bool fillWording(const char* wording, const Slots& slots, std::string& out);

/** Every bank and its size (BoothScript --banks). */
std::vector<std::pair<std::string, size_t>> bankSizes();
/** Bank ids two tables both use (a mistake: the first table's wordings win). */
std::vector<std::string> bankDuplicates();

}  // namespace booth
}  // namespace tag
