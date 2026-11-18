// The booth's writer: a language model (a local Ollama model, run by the presentation) writes the talk that has time to
// be written -- the two men's exchanges about the game so far, the quarter and halftime packages -- from a snapshot of
// the game's own numbers. Booth asks (takeWriterJobs), the presentation runs the job and hands the text back
// (writerDone); the booth checks every name and number against the snapshot, drops what fails or what a new score has
// made stale, and says the rest in the next dead time. The play-by-play's live calls and the breakdown of each play
// stay templated (they must be instant and are built from the sim's events).
#include "Booth/Booth.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Sim/League.h"

namespace tag {
namespace booth {

namespace {
std::string wS(int v) { return std::to_string(v); }
std::string wClock(double s) {
  const int t = (std::max)(0, static_cast<int>(std::ceil(s - 1e-6)));
  char b[16];
  std::snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
  return b;
}
}  // namespace

std::string Booth::factSheet(const Game& g) {
  // what the booth knows right now, in plain sentences (the writer may use these facts and no others)
  const Team& H = *g.home;
  const Team& A = *g.away;
  std::string f;
  f += "Game: the " + A.city + " " + nick(A) + " (visitors) at the " + H.city + " " + nick(H) + " (home). ";
  f += "Score: " + A.city + " " + wS(g.score.away) + ", " + H.city + " " + wS(g.score.home) + ". ";
  static const char* const kQ[] = {"first", "second", "third", "fourth", "overtime"};
  f += "It is the " + std::string(kQ[(std::min)(g.q, 5) - 1]) + (g.q <= 4 ? " quarter" : std::string()) + " with " + wClock(g.clock) + " left. ";
  if (g.phase == GamePhase::Scrimmage)
    f += g.offense().city + " has the ball, " + std::to_string(g.down) + (g.down == 1 ? "st" : g.down == 2 ? "nd" : g.down == 3 ? "rd" : "th") + " and " +
         wS((std::max)(1, static_cast<int>(std::lround(g.toGo)))) + ". ";
  for (const Team* t : {&A, &H}) {
    const TeamLine& tl = g.team[t == &H ? TeamKey::Home : TeamKey::Away];
    f += t->city + " team: " + wS(tl.yards) + " total yards, " + wS(tl.rushYds) + " rushing on " + wS(tl.rushAtt) + " carries, " + wS(tl.passYds) + " passing, " +
         wS(tl.thirdConv) + " of " + wS(tl.thirdAtt) + " on third down, " + wS(giveaways(g, tl)) + " turnovers (interceptions thrown plus fumbles lost), " + wS(tl.sacks) + " sacks allowed, " + wS(tl.penalties) +
         " penalties. Offense: " + t->offScheme + ". Defense: " + t->defScheme + ". ";
    // the men: the quarterback, the top runner and receiver, the defenders who have made plays
    const PlayerLine *qb = nullptr, *rb = nullptr, *wr = nullptr;
    std::vector<const PlayerLine*> def;
    for (const PlayerLine* ln : g.stats.forTeam(t->abbr)) {
      if (ln->passAtt > 0 && (!qb || ln->passAtt > qb->passAtt)) qb = ln;
      if (ln->rushAtt > 0 && ln->passAtt == 0 && (!rb || ln->rushYds > rb->rushYds)) rb = ln;
      if (ln->rec > 0 && (!wr || ln->recYds > wr->recYds)) wr = ln;
      if (ln->sacks > 0 || ln->ints > 0 || ln->tackles >= 5) def.push_back(ln);
    }
    auto who = [&](const PlayerLine* ln) { return t->city + "'s " + leadName(g, *ln); };
    if (qb)
      f += "QB " + who(qb) + ": " + wS(qb->passComp) + " of " + wS(qb->passAtt) + ", " + wS(qb->passYds) + " yards, " + wS(qb->passTD) + " TD, " + wS(qb->passInt) + " INT, sacked " +
           wS(qb->sacked) + " times. ";
    if (rb) f += "Top runner " + who(rb) + ": " + wS(rb->rushAtt) + " carries, " + wS(rb->rushYds) + " yards, " + wS(rb->rushTD) + " TD. ";
    if (wr) f += "Top receiver " + who(wr) + ": " + wS(wr->rec) + " catches, " + wS(wr->recYds) + " yards, " + wS(wr->recTD) + " TD. ";
    for (const PlayerLine* d : def) {
      f += t->city + " defender " + leadName(g, *d) + ": " + wS(d->tackles) + " tackles";
      if (d->sacks) f += ", " + wS(d->sacks) + " sacks";
      if (d->ints) f += ", " + wS(d->ints) + " interceptions";
      f += ". ";
    }
  }
  // the last few plays, as the game's own log has them
  const size_t n = g.log.size();
  std::string recent;
  for (size_t i = n > 6 ? n - 6 : 0; i < n; ++i)
    if (g.log[i].big != LogTag::Info) recent += "- " + g.log[i].desc + "\n";
  if (!recent.empty()) f += "\nRecent plays (oldest first):\n" + recent;
  return f;
}

void Booth::askWriter(const Game& g, const std::string& kind) {
  if (!writer_ || writerBusy_) return;
  WriterJob j;
  j.id = ++writerId_;
  j.kind = kind;
  j.scoreSum = g.score.home * 100 + g.score.away;
  j.q = g.q;
  j.poss = g.poss;
  j.playNo = scrimNo_;
  j.askedAt = now_;
  j.system =
      "You write live television dialogue for the two-man booth of a network NFL broadcast. DAN KESSLER is the play-by-play announcer; CAL BREWER "
      "is the analyst, a former NFL quarterback. Write how real announcers talk on a Sunday afternoon: conversational, specific, a little personality, "
      "no cliches, short sentences meant to be heard. STRICT RULES: use only the facts given to you; never invent a player, a number, a stat, a team, a "
      "coach, a record or an event; this league has NO history, so talk only about tonight's game (never season, year, career, record, last week); "
      "every player belongs to the team the facts name, so never put a player on the other team's side; refer to players by last name; numbers must be "
      "exactly as given; never name a play type, blitz or coverage the facts do not show (no play action, screens or blitzes unless listed); "
      "a quarterback HAS THROWN interceptions and a defender HAS them, so say which; one event counts once (a pick thrown and the same pick made "
      "are one thing, not two); no quotation marks, no stage directions, no emojis.";
  const std::string facts = factSheet(g);
  std::string avoid;
  for (const std::string& t : writerTopics_) avoid += "- " + t + "\n";
  if (kind == "exchange")
    j.user = facts + "\nA drive just ended (a score, a punt or a turnover), and the booth has the break before the next snap. Write a short exchange about the game as "
                     "it stands: DAN raises one storyline from the facts (the drive that just ended, a player's day, a trend, a matchup, what a team needs), quoting the "
                     "exact number from the facts, and using a man's first name too when another player in the facts has the same last name; CAL answers with something NEW "
                     "the facts support: the football reason why (technique, scheme, matchup) or what they should do about it. Never repeat DAN's point, never hedge "
                     "with probably. Then DAN reacts to what CAL said, the way partners do: a quick follow-up, a bit of pushback, or a fact that backs him up "
                     "(under 12 words, not a question, never just 'good point'). 3 lines, the first two under 22 words, spoken style: contractions, short sentences, "
                     "no semicolons or dashes. Do not describe the last play again. Do not mention the clock, the down or the distance (they will be stale). " +
             (avoid.empty() ? std::string() : "Pick a different storyline than these, already discussed:\n" + avoid) +
             "\nFormat exactly:\nDAN: ...\nCAL: ...\nDAN: ...";
  else if (kind == "half")
    j.user = facts + "\nIt is halftime, and the score was just read. Write the booth's halftime segment: DAN gives one key number of the half (not the score), CAL gives the two things "
                     "that have decided the half and one adjustment he expects, DAN adds a short line on what to watch in the second half. 3 lines, each under 35 words."
                     "\nFormat exactly:\nDAN: ...\nCAL: ...\nDAN: ...";
  else if (kind == "quarter")
    j.user = facts + "\nThe quarter just ended and the score was just read. Write a quick break: DAN gives one key number (not the score), CAL gives the story of the quarter in one or two sentences. "
                     "2 lines, each under 30 words.\nFormat exactly:\nDAN: ...\nCAL: ...";
  j.facts = facts;
  writerBusy_ = true;
  writerJobs_.push_back(j);
  writerAsked_.push_back(j);
  while (writerAsked_.size() > 8) writerAsked_.erase(writerAsked_.begin());
}

std::vector<Booth::WriterJob> Booth::takeWriterJobs() {
  std::vector<WriterJob> v;
  v.swap(writerJobs_);
  return v;
}

bool Booth::writerCheck(const std::string& line, const std::string& facts) const {
  // every number must be in the facts; every name-looking word must be someone in this game (or one of the crew)
  if (line.size() < 8 || line.size() > 260) return false;
  if (line.find('"') != std::string::npos || line.find('*') != std::string::npos || line.find('(') != std::string::npos) return false;
  for (size_t i = 0; i < line.size();) {
    if (std::isdigit(static_cast<unsigned char>(line[i]))) {
      size_t j = i;
      while (j < line.size() && (std::isdigit(static_cast<unsigned char>(line[j])) || line[j] == ':')) ++j;
      const std::string num = line.substr(i, j - i);
      // (whole-number match in the facts: "1" must not ride along inside "14")
      bool found = false;
      for (size_t at = facts.find(num); at != std::string::npos; at = facts.find(num, at + 1)) {
        const bool l = at == 0 || !std::isdigit(static_cast<unsigned char>(facts[at - 1]));
        const bool r = at + num.size() >= facts.size() || !std::isdigit(static_cast<unsigned char>(facts[at + num.size()]));
        if (l && r) found = true;
      }
      if (!found) return false;
      i = j;
    } else
      ++i;
  }
  // no history exists in this league
  {
    std::string low = line;
    for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* b : {"season", "this year", "last year", "career", "all year", "last week", "record", "historically", "coach "})
      if (low.find(b) != std::string::npos) return false;
    // a play type the analyst names must be one the facts show (Haiku gave the run game "play action" nobody ran)
    std::string lowFacts = facts;
    for (char& c : lowFacts) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* pt : {"play action", "play-action", "bootleg", "boots", "screen", "blitz", "rpo", "read option", "zone blocking", "man coverage", "cover 0",
                           "cover 1", "cover 2", "cover 3", "cover 4", "cover 6", "trick play", "flea flicker", "reverse"})
      if (low.find(pt) != std::string::npos && lowFacts.find(pt) == std::string::npos) return false;
  }
  // a sentence about one man and one team: the team must be his
  if (writerGame_) {
    size_t st = 0;
    while (st < line.size()) {
      size_t en = line.find_first_of(".!?", st);
      if (en == std::string::npos) en = line.size();
      const std::string sent = line.substr(st, en - st);
      st = en + 1;
      std::vector<const Team*> teams;
      for (const Team* tm : {writerGame_->home, writerGame_->away})
        if (sent.find(tm->city) != std::string::npos || sent.find(nick(*tm)) != std::string::npos) teams.push_back(tm);
      std::vector<const Player*> men;
      for (const Team* tm : {writerGame_->home, writerGame_->away})
        for (const auto& pl : tm->roster) {
          const size_t at = sent.find(pl->last);
          if (at == std::string::npos) continue;
          const bool shared = shared_.count(pl->last) > 0;
          if (shared && sent.find(pl->first + " " + pl->last) == std::string::npos) continue;  // (which one: unknown)
          men.push_back(pl.get());
        }
      if (teams.size() == 1 && men.size() == 1 && !teams[0]->hasPlayer(men[0]) && sent.find("against") == std::string::npos) return false;
    }
  }
  // a capitalized word that is a name in the league but not in this game is an invention
  static const std::set<std::string> kLeague = [] {
    std::set<std::string> s;
    League lg = createLeague(2027);
    for (const auto& t : lg) {
      s.insert(t->city.substr(0, t->city.find(' ')));
      s.insert(t->name.substr(0, t->name.find(' ')));
      for (const auto& p : t->roster) {
        s.insert(p->last);
        s.insert(p->first);
      }
    }
    return s;
  }();
  static const char* const kNfl[] = {"Chargers", "Cowboys", "Patriots", "Packers", "Bears", "Giants", "Jets", "Eagles", "Steelers", "Chiefs", "Raiders", "Broncos",
                                     "Ravens", "Bengals", "Browns", "Texans", "Colts", "Jaguars", "Titans", "Bills", "Dolphins", "Saints", "Falcons", "Panthers",
                                     "Buccaneers", "Rams", "Seahawks", "Cardinals", "49ers", "Niners", "Vikings", "Lions", "Commanders", "Brady", "Mahomes"};
  std::string w;
  for (size_t i = 0; i <= line.size(); ++i) {
    const char c = i < line.size() ? line[i] : ' ';
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '-') {
      w += c;
      continue;
    }
    if (!w.empty() && std::isupper(static_cast<unsigned char>(w[0]))) {
      for (const char* x : kNfl)
        if (w == x && !properNames_.count(w)) return false;
      if (kLeague.count(w) && !properNames_.count(w)) return false;
    }
    w.clear();
  }
  return true;
}

void Booth::writerDone(int id, const std::string& text, double secs) {
  writerBusy_ = false;
  (void)secs;
  const WriterJob* j = nullptr;
  for (const WriterJob& x : writerAsked_)
    if (x.id == id) j = &x;
  if (!j) return;
  // stale: the score moved or the quarter turned since the facts were taken (an exchange about a game that has changed)
  // (the score is not compared: the extra point lands between the ask and the answer)
  if (j->kind == "exchange" && ((writerGame_ && writerGame_->q != j->q) || scrimNo_ != j->playNo)) {  // (a snap since, or a new quarter: the extra point moving the score is not stale)
    ++writerStale_;
    return;
  }
  // "DAN: ..." / "CAL: ..." lines, each checked
  std::vector<std::pair<Who, std::string>> lines;
  size_t at = 0;
  while (at < text.size()) {
    size_t e = text.find('\n', at);
    if (e == std::string::npos) e = text.size();
    std::string ln = text.substr(at, e - at);
    at = e + 1;
    while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
    size_t k = 0;
    while (k < ln.size() && (ln[k] == ' ' || ln[k] == '-' || ln[k] == '(')) ++k;
    ln = ln.substr(k);
    Who who;
    if (ln.rfind("DAN:", 0) == 0 || ln.rfind("DAN KESSLER:", 0) == 0) who = Who::PBP;
    else if (ln.rfind("CAL:", 0) == 0 || ln.rfind("CAL BREWER:", 0) == 0) who = Who::Color;
    else continue;
    std::string body = ln.substr(ln.find(':') + 1);
    while (!body.empty() && body[0] == ' ') body.erase(0, 1);
    // spoken punctuation: curly quotes straight, semicolons and dashes broken into sentences
    for (const auto& pr : std::vector<std::pair<std::string, std::string>>{{"\xE2\x80\x99", "'"}, {"\xE2\x80\x98", "'"}, {"\xE2\x80\x94", ", "}, {"\xE2\x80\x93", ", "}, {"; ", ". "}, {" - ", ", "}})
      for (size_t q = body.find(pr.first); q != std::string::npos; q = body.find(pr.first, q + pr.second.size())) body.replace(q, pr.first.size(), pr.second);
    for (size_t q = body.find(". "); q != std::string::npos; q = body.find(". ", q + 2))
      if (q + 2 < body.size()) body[q + 2] = static_cast<char>(std::toupper(static_cast<unsigned char>(body[q + 2])));
    int words = 0;
    for (size_t q = 0; q < body.size(); ++q) words += (body[q] == ' ') ? 1 : 0;
    if (words > (j->kind == "exchange" ? 26 : 40) || body.find("robably") != std::string::npos) {
      ++writerRejected_;
      return;
    }
    while (!body.empty() && (body.back() == ')' )) body.pop_back();
    if (!writerCheck(body, j->facts)) {
      ++writerRejected_;
      return;  // one bad line and the whole exchange goes (half an exchange is worse than none)
    }
    lines.push_back({who, body});
  }
  // an exchange: the question, the answer, and the play-by-play man's short reaction (a long third line is dropped, not the exchange)
  if (j->kind == "exchange" && lines.size() > 3) lines.resize(3);
  if (j->kind == "exchange" && lines.size() == 3 &&
      (lines[2].first != Who::PBP || std::count(lines[2].second.begin(), lines[2].second.end(), ' ') > 14))
    lines.resize(2);
  if (lines.size() < 2 || lines.front().first != Who::PBP || lines[1].first != Who::Color) {
    ++writerRejected_;
    return;
  }
  if (j->kind == "exchange") {
    // the analyst must add, not echo: most of his words already in the question is an echo
    auto toks = [](const std::string& x) {
      std::set<std::string> o;
      std::string w;
      for (char c : x + " ") {
        if (std::isalpha(static_cast<unsigned char>(c))) w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else {
          if (w.size() > 3) o.insert(w);
          w.clear();
        }
      }
      return o;
    };
    const auto a = toks(lines[0].second), b = toks(lines[1].second);
    int shared = 0;
    for (const auto& x : b) shared += a.count(x) ? 1 : 0;
    if (!b.empty() && shared * 2 > static_cast<int>(b.size())) {
      ++writerRejected_;
      return;
    }
  }
  ++writerUsed_;
  writerTopics_.push_back(lines.front().second.substr(0, 90));
  while (writerTopics_.size() > 6) writerTopics_.erase(writerTopics_.begin());
  WriterReady r;
  r.kind = j->kind;
  r.lines = lines;
  r.scoreSum = j->scoreSum;
  r.q = j->q;
  r.poss = j->poss;
  r.playNo = j->playNo;
  writerReady_.push_back(r);
}

}  // namespace booth
}  // namespace tag
