// The broadcast booth: two voices calling the game the way a Sunday television crew does.
//
// Play-by-play (PBP) sets each snap up, calls the action live and gives the result; the analyst (Color) breaks the
// play down, reads the defense before the snap and carries the storylines. The booth is plain C++ like the Sim (the
// headless BoothScript tool runs whole games through it), and only WATCHES the game: every tick it is handed the Game
// and a View of what the presentation is doing (the stage, the play's sim, a kick in the air) and works out for itself
// what just happened (new log lines, new play events, the whistle, a crossed yard line).
//
// What it produces is a queue of Lines for one speech channel (two men in a booth don't talk at once). The
// presentation pulls the line to start (next()), tells the booth how long it really runs (started()), and lets it go
// when it ends (finished()). Live calls are hard real time: they expire within a second, and they may cut off a
// slower line (cutCurrent()). Before each snap the booth plans the live calls the play could need (who the QB might
// throw to, who might make the sack...) and offers them with forecast(), so a speech synthesizer can have them ready;
// genericCalls() lists the calls that name nobody, to synthesize once.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Booth/BoothBanks.h"
#include "Sim/Game.h"
#include "Sim/Rng.h"

namespace tag {
namespace booth {

/** The voices: the play-by-play man, the analyst, and the referee at his microphone (a flag's announcement: the foul,
 *  the side, the number, the enforcement and the down - Booth::enableReferee). */
enum class Who { PBP, Color, Ref };
inline const char* whoName(Who w) { return w == Who::PBP ? "PBP" : w == Who::Color ? "COLOR" : "REF"; }

/** Ref: the referee's announcement (Who::Ref), in the queue like the talk so nobody speaks over it. */
enum class Kind { Open, Setup, Read, Live, Result, Analysis, Story, Score, Break, Close, Ref };
const char* kindName(Kind k);

struct Line {
  int id = 0;
  Who who = Who::PBP;
  Kind kind = Kind::Result;
  std::string text;  // the caption
  std::string say;   // what the voice reads (numbers, scores and clock in words)
  int energy = 1;    // 0 calm .. 3 the top of his voice
  int prio = 1;      // 0 filler .. 6 must be said now
  double notBefore = 0;
  double expireAt = 1e9;
  bool live = false;      // an in-play call: said now or never
  bool snapSafe = false;  // may still be said after the next snap (a score, a flag)
  double estSecs = 0;     // estimated length when spoken
  std::string tag;        // a live call's kind ("catch", "throw:<id>", "sack:<id>", ...): what PBP has said this play
  /** A live call's stand-in: the generic call that means the same and names nobody ("Caught!" for a named catch,
   *  "Sacked!" for a named sack), baked once for every game (genericCalls). The voice says it when this call's own audio
   *  is not made yet, rather than leaving the call to a silent caption. Empty: the call is generic itself, or has none. */
  std::string alt;
  int altEnergy = 1;      // the energy the stand-in is baked at
  /** What the line is about, for the broadcast graphics that follow the booth (UI/BroadcastGfx: a stat line's man
   *  "stat:<id>", a story's "qb:<id>" / "rb:<id>" / "3rd:<team>"..., a read's or a breakdown's angle, "ref" for the
   *  referee). Never part of the voice's cache key (LineKey: voice, energy, words). Empty: nothing to show. */
  std::string topic;
};

/** What the presentation is doing this frame. */
struct View {
  enum class Stage { Menu, Call, Presnap, Live, Post, Kick, Wait, Replay, Paused, Final };
  Stage stage = Stage::Menu;
  /** The play on the field (scrimmage, two-point try or live return); the last one stays here after the whistle. */
  const PlaySim* sim = nullptr;
  /** A kick that is not a live return: 0 kickoff, 1 punt, 2 field goal / extra point; -1 none. */
  int kickKind = -1;
  double kickT = 0;     // seconds since the kick sequence started
  double kickLead = 0;  // ... when the foot meets the ball
  double kickHang = 0;  // seconds in the air
  bool userGame = false;
  bool night = false;
  /** The presentation lets the booth hold the snap a few seconds (wantsHold) so a breakdown can finish. */
  bool canHold = false;
};

struct Crew {
  std::string pbpFirst = "Dan", pbpLast = "Kessler";
  std::string colorFirst = "Cal", colorLast = "Brewer";
};

class Booth {
 public:
  explicit Booth(uint32_t seed = 1, const Crew& crew = Crew());

  /** Watch one frame. dt = real seconds (the booth's own clock; it stops while the stage is Paused). */
  void tick(double dt, const Game& g, const View& v);

  // ---- the speech channel
  /** The line to start now, or nullptr (one at a time; call again after finished()). */
  const Line* next();
  /** The presentation started line id; it runs `secs` (estimated when there is no audio). */
  void started(int id, double secs);
  /** The current line ended (or was cut). */
  void finished(int id);
  /** A live call is waiting and the line playing is less urgent (or the snap came): fade it out, call finished(). */
  bool cutCurrent() const;
  bool speaking() const { return cur_.id != 0; }
  const Line& current() const { return cur_; }
  /** When the current line is due to end (booth seconds). */
  double curEnd() const { return curEnd_; }
  /** Lines the next seconds may need (queued, then the planned live calls); for speech prewarm. */
  std::vector<Line> forecast() const;
  /** A fingerprint of what forecast() would return, without building it: the same stamp, the same lines. */
  uint64_t forecastStamp() const;
  /** Every live call that names nobody (yard lines, "Touchdown!", "Dropped!"...): synthesize once, keep. */
  static std::vector<std::pair<std::string, int>> genericCalls();
  /** The generic call (and its energy) that stands in for a live call with this tag and words; empty when the call is
   *  generic itself. The fallback the call was made with is used when it is generic. */
  static std::pair<std::string, int> genericAlt(const std::string& tag, const std::string& text, const std::string& fallback);
  /** The live calls this match's regulars can get (each man's planned variants), to synthesize at the kickoff. */
  std::vector<std::pair<std::string, int>> rosterCalls(const Game& g);
  /** Hold the snap a moment: the booth is in the middle of something worth hearing (only with View::canHold). */
  bool wantsHold() const;
  /** Remember across games what the booth has said (Game/TagRecent.h: the store the game saves between matches, kept in
   *  memory by BoothScript): pick() takes the wording of a phrase bank heard least recently - in this game or an earlier
   *  one, every wording before any comes round again - and a story sentence heard lately waits its turn. A wording
   *  counts as heard when its line is said (next()), not when it is written. On by default. Live-call rot() is not
   *  touched (its three stable variants are what the voice cache holds). */
  void enableMemory(bool on) { memory_ = on; }
  bool memoryOn() const { return memory_; }
  /** The referee announces every flag himself (Who::Ref lines: "Holding, offense, number 74. 10-yard penalty. Repeat
   *  1st down."), and the play-by-play man only leads into it and says what it means. Off by default: a Ref line needs
   *  the third voice (the speech service has two; the game turns this on once it can voice and caption the referee).
   *  Off, the play-by-play man reads the flag himself, as before. */
  void enableReferee(bool on) { ref_ = on; }
  bool refereeOn() const { return ref_; }
  double now() const { return now_; }

  /** Estimated seconds a line takes to say (words at a broadcaster's pace; a live call runs faster). */
  static double estimate(const std::string& say, Who who, int energy);
  /** Caption -> spoken text: numbers, ordinals, scores and clock readings in words. */
  static std::string spoken(const std::string& text);

  const Crew& crew() const { return crew_; }

  // ---- the writer (a language model the presentation runs): the exchanges and the break packages
  struct WriterJob {
    int id = 0;
    std::string kind;    // "exchange", "quarter", "half"
    std::string system;  // the model's system prompt
    std::string user;    // the facts and the ask
    std::string facts;   // what every name and number in the answer is checked against
    int scoreSum = 0, q = 1;
    double askedAt = 0;
    TeamKey poss = TeamKey::Home;
    int playNo = 0;
  };
  void enableWriter(bool on) { writer_ = on; }
  bool writerOn() const { return writer_; }
  /** The jobs to run (each once); hand the model's text back with writerDone. */
  std::vector<WriterJob> takeWriterJobs();
  void writerDone(int id, const std::string& text, double secs);
  struct WriterStats {
    int asked = 0, used = 0, rejected = 0, stale = 0;
  };
  WriterStats writerStats() const { return {writerId_, writerUsed_, writerRejected_, writerStale_}; }

  // ---- diagnostics
  struct Stats {
    int queued = 0, spoken = 0, expired = 0, cut = 0, liveSpoken = 0;
  };
  const Stats& stats() const { return st_; }

 private:
  struct PlanEntry {
    std::string text;
    int energy = 1;
  };
  /** The live calls decided before the snap (forecast() offers them for prewarm). Keyed by event + player id. */
  struct LivePlan {
    std::map<std::string, PlanEntry> at;
  };
  struct Shed {
    const Player* d = nullptr;  // the defender who got off the block
    const Player* b = nullptr;  // the blocker he beat
    std::string move;
  };

  // ---- the queue
  Line& push(Who who, Kind kind, const std::string& text, int energy, int prio, double delay = 0, double ttl = 30);
  /** A man into the zone (his X-Factor live) or knocked out of it. */
  void onZone(const Game& g, const AbilityEvent& e);
  void dropKind(Kind k);
  void dropStale();
  void snapFlush();
  bool speakingLiveLong() const;

  // ---- what happened
  void onOpen(const Game& g, const View& v);
  void onLog(const Game& g, const PlayLog& l, size_t index);
  void onNewPlay(const Game& g, const PlaySim& s);
  void onSnap(const Game& g, const PlaySim& s);
  void onEvent(const Game& g, const PlaySim& s, const PlayEvent& e);
  void trackCarrier(const Game& g, const PlaySim& s);
  void livePocket(const Game& g, const PlaySim& s);
  void onWhistle(const Game& g, const PlaySim& s, const PlayResult& r);
  void returnResult(const Game& g, const PlaySim& s, const PlayResult& r);
  void afterApply(const Game& g, const PlaySim& s, const PlayResult& r, const PlayLog& l);
  void onKickTick(const Game& g, const View& v);
  void kickResult(const Game& g, const std::string& desc);
  /** A flag on a return (in the kick's own log line): the play-by-play man, and the referee when he speaks. */
  void returnFoul(const Game& g, const std::string& desc);
  void onPresnapEnter(const Game& g, const View& v);
  void idle(const Game& g, const View& v);
  bool airWritten(const Game& g);
  void onPenalty(const Game& g, const PlayLog& l);
  void onScored(const Game& g, TeamKey scorer, int pts, const PlayResult& r, bool say = true);
  void afterDrive(const Game& g, const std::string& how);
  void statFollow(const Game& g, const PlayResult& r);
  void onFinal(const Game& g);

  // ---- what to say
  std::string setupLine(const Game& g, bool full);
  std::optional<std::string> presnapRead(const Game& g, const PlaySim& s);
  std::string resultLine(const Game& g, const PlaySim& s, const PlayResult& r, int& energy);
  std::optional<std::string> analysis(const Game& g, const PlaySim& s, const PlayResult& r, int& energy);
  std::optional<std::string> storyLine(const Game& g, Who& who);
  std::optional<std::pair<std::string, std::string>> exchange(const Game& g);
  std::optional<std::string> quarterStory(const Game& g);
  std::optional<std::string> halftimeStory(const Game& g);
  std::string scoreLine(const Game& g, bool withTime);
  /** The score after a score, with the lead change or the tie in it ("And Austin takes the lead. Austin 14, ..."). */
  std::string scoreSay(const Game& g, TeamKey scorer, const std::string& lead);
  std::string clockPhrase(const Game& g) const;
  std::string situation(const Game& g) const;
  void buildPlan(const Game& g, const PlaySim& s);
  void live(const std::string& key, const std::string& fallback, int energy, double ttl = 0.9, const char* tag = nullptr);
  /** PBP has said a live call of this kind this play (spoken, not just queued). */
  bool said(const char* prefix) const;
  bool blitzOn(const PlaySim& s) const;
  /** 0 a game, 1 two scores late, 2 decided: the words of urgency follow it. */
  int tier(const Game& g) const;
  std::string lowerFirst(const std::string& t) const;

  // ---- names
  std::string nm(const Player* p, bool full = false);
  std::string pname(const Player* p);  // first mention of the game: full name
  std::string ln(const Player* p);
  const Team& teamOfPlayer(const Game& g, const Player* p) const;
  std::string teamRef(const Team& t);  // "Austin" / now and then "the Outlaws"
  static std::string nick(const Team& t);  // the nickname without a trailing " FC"
  /** The QB has been sharp today (60%+ completions, no interception). */
  bool qbSharp(const Game& g, const Player* qb) const;
  std::string spot(const Game& g, double ballOn, TeamKey side) const;  // "the Austin 35", "midfield"
  std::string fieldX(double x) const;  // live: "the 40" (sim frame x)
  static std::string lastName(const std::string& statName);
  std::string factSheet(const Game& g);
  void askWriter(const Game& g, const std::string& kind);
  bool writerCheck(const std::string& line, const std::string& facts) const;
  std::string lineName(const Game& g, const PlayerLine& l);
  std::string leadName(const Game& g, const PlayerLine& l);  // first and last, always

  // ---- phrase choice without repeats
  std::string pick(const std::string& bank, const std::vector<std::string>& opts);
  /** A bank's authored wordings (BoothBanks.inl) with these facts filled, chosen like pick(); "" when no wording of the
   *  bank can be said with the facts given. */
  std::string say(const std::string& bank, const Slots& s);
  /** pick() over opts whose ids (what the memory keeps) are ids: a bank's wording index, or the position in opts. */
  std::string pickFrom(const std::string& bank, const std::vector<std::string>& opts, const std::vector<int>& ids);
  /** The facts most lines name (the teams, the down before the snap, the score): s plus these, s first. */
  Slots facts(const Game& g, Slots s) const;
  /** The referee's announcement of a flag (enableReferee), queued after what is waiting. */
  void refSay(const std::string& text, double delay = 0.3);
  /** A foul in the referee's words with its side and number ("Holding, offense, number 74"), from the log's name. */
  std::string refFoul(const std::string& logName, bool offense, const Player* man) const;
  bool ref_ = false;
  /** The memory across games (enableMemory): the wordings picked and not yet said, matched to the line that says them. */
  struct PickUse {
    std::string bank;
    int idx = 0;
    std::string text;
  };
  std::deque<PickUse> pendingPicks_;
  bool memory_ = true;
  uint32_t salt_ = 1;  // (the order of wordings no game has heard yet: the booth's seed)
  void heardNow(const Line& l);
  /** A live call planned for one man: one of his three ways of saying it, in turn (stable, so the voice cache hits). */
  std::string rot(int pid, const std::vector<std::string>& opts);
  bool chance(double p) { return rng_.chance(p); }
  bool cooled(const std::string& topic, double secs);
  void mark(const std::string& topic) { topicT_[topic] = now_; }
  bool once(const std::string& topic);

  Crew crew_;
  Rng rng_;
  double now_ = 0;
  int nextId_ = 1;
  std::deque<Line> q_;
  Line cur_;
  double curEnd_ = 0;
  double lastEnd_ = -10;
  Who lastWho_ = Who::PBP;
  bool cutForSnap_ = false;
  Stats st_;
  View view_;

  // what the booth has seen
  bool opened_ = false, closed_ = false;
  size_t logSeen_ = 0;
  size_t zoneSeen_ = 0;  // (Game::abil.history read so far: the X-Factor zone calls)
  const PlaySim* sim_ = nullptr;
  double simT_ = 0;
  size_t evSeen_ = 0;
  bool snapped_ = false, whistled_ = false, appliedThisPlay_ = false;
  View::Stage stage_ = View::Stage::Menu;
  double stageT_ = 0;
  double holdUsed_ = 0, holdBudget_ = 0;
  PerTeam<int> score_{};
  TeamKey poss_ = TeamKey::Away;
  GamePhase phase_ = GamePhase::Kickoff;
  double presnapT_ = 0;
  bool presnapSaid_ = false, readSaid_ = false;
  int playNo_ = 0;
  int scrimNo_ = 0;                  // scrimmage snaps (a written exchange airs before the next one, or never)
  double lastStoryT_ = -100;
  std::string nextSetup_, nextSetupKey_;
  int nextSetupEnergy_ = 0;  // the next snap's setup, written early (forecast offers it for the voice)

  // the live play
  LivePlan plan_;
  bool calledThrow_ = false, calledCatch_ = false, calledRun_ = false, breakaway_ = false, calledPressure_ = false;
  bool calledTD_ = false, calledINT_ = false, calledSack_ = false, calledFumble_ = false, calledPbu_ = false, calledGoingToScore_ = false;
  bool calledDrop_ = false, pbuSeen_ = false, dropSeen_ = false, battedSeen_ = false;
  int handoffPid_ = -1;  // the back the play's handoff call was planned for (a handoff to another man is called generically)
  const Player* qbOnField_[2] = {nullptr, nullptr};  // each side's quarterback at its last snap (home, away): a new one is introduced
  std::string resultSaid_;     // the play's call (the breakdown does not say it back)
  std::vector<size_t> deferredLogs_;  // clock stops logged before the play's own line: heard after it (flushDeferred)
  std::vector<TeamKey> bigPlayTeam_;  // the side each of bigPlays_ was for (the close names the winner's)
  std::unordered_map<std::string, std::set<int>> usedIds_;  // a bank's wordings picked this round of the game (pickFrom)
  std::deque<Line> heldBreaks_;       // a timeout or the two-minute warning the snap overtook: said after the play's call
  bool flushingLogs_ = false;
  void flushDeferred(const Game& g);
  std::string creditFor_;      // the held breakdown a held credit belongs to (said together, or not at all)
  std::set<std::string> spokenTags_;  // the live calls PBP has said this play
  int spokenTarget_ = -1;             // the man the spoken throw call named
  std::string lastRead_;              // the analyst's read before this snap (his breakdown may come back to it)
  bool replayWorthy_ = false;         // this play gets the presentation's replay (the breakdown rides it)
  std::string downTag_;               // "First down." held from the whistle until the play is applied (no flag)
  std::string pendingAnalysis_;       // the breakdown of a play with no replay, said after the down
  std::string pendingAnalysisTopic_;  // ... its angle (Line::topic)
  std::string lastAnalysisTopic_, lastStoryTopic_;  // the angle analysis() chose, the topic storyLine() chose (Line::topic)
  int pendingAnalysisEnergy_ = 1;
  std::unordered_map<int, int> penCount_;  // flags on a man this game
  bool setupPossKnown_ = false;       // the last setup's team and quarter (the first snap of a series says whose ball)
  TeamKey setupPoss_ = TeamKey::Home;
  int setupQ_ = 1;
  bool driveAfterTry_ = false;        // the touchdown drive's summary waits for the try
  std::vector<std::string> pendingInjury_;
  const Player* openKey_ = nullptr;   // the man the analyst named in the open
  std::set<std::string> properNames_; // words that stay capitalized after "Dan, " (players, cities, teams)
  bool playAngle_ = false;
  std::string storyReply_;            // the analyst's answer to the play-by-play man's stat line
  std::string pendingResult_, pendingBare_;  // the play's call held for the officials (with / without the down)
  int pendingResultEnergy_ = 1;
  std::vector<std::pair<std::string, int>> injuryWord_;  // a man who went down: the sideline word on him, a few plays later
  double intX_ = -100;               // where the interception was made (the return's yard lines run from there)
  int lastBig_ = -1;                 // the man who just made a big play (no "can't get him going" now)
  bool creditRead_ = false;
  bool freeKick_ = false;
  bool dropDecided_ = false, skipDrop_ = false;  // this pass play gets a "drop" call, or not
  std::string creditLine_;           // "you called it", said after the held result
  bool sealed_ = false;              // a late fourth-down stop that decides it
  bool clutchSetup_ = false;
  std::string ddSaidKey_;
  int giveBack_[2] = {0, 0};         // takeaways fumbled right back (home, away): a giveaway the sim's net count misses
  int giveBackPlay_ = -1;
  int giveaways(const Game& g, const TeamLine& t) const;            // the next down and distance a result or a flag already said         // this setup is a deciding late down (said with the stakes, up a notch)
  // the writer
  bool writer_ = false, writerBusy_ = false;
  int writerId_ = 0, writerUsed_ = 0, writerRejected_ = 0, writerStale_ = 0;
  std::vector<WriterJob> writerJobs_, writerAsked_;
  std::vector<std::string> writerTopics_;  // storylines already done (the next exchange picks another)
  struct WriterReady {
    std::string kind;
    std::vector<std::pair<Who, std::string>> lines;
    int scoreSum = 0, q = 1;
    TeamKey poss = TeamKey::Home;
    int playNo = 0;
  };
  std::deque<WriterReady> writerReady_;
  const Game* writerGame_ = nullptr;
  double lastWriterAsk_ = -100;
  std::string breakFallback_;  // the template break line, said if the writer's package does not come in time
  double breakDeadline_ = -1;
  bool fourthFail_ = false;          // this play was a fourth down that came up short
  double curStart_ = 0;              // when the current line started
  int ticks_ = 0;                    // yard lines called on this run
  std::string catchRoute_;           // the route the catcher ran (the breakdown names it)
  std::set<int> bioSaid_;            // men the analyst has introduced
  bool risky4th_ = false;             // the analyst questioned this fourth-down try before the snap
  double lastColorT_ = 0;             // when the analyst was last heard
  std::vector<std::string> bigPlays_;  // the plays the close remembers            // the next kick is the free kick after a safety          // the analyst's read hit: the play-by-play man says so
  struct PreKick {
    int down = 1;
    double toGo = 10, ballOn = 25;
    int margin = 0, q = 1, tier = 0;
  } preKick_;  // the down a kick is decided on (the Game has moved past it when the kick's line arrives)            // the last breakdown was about the play itself (a replay may frame it)
  std::map<std::string, int> readSubj_;  // a read's topic -> the man it names (this snap's candidates)
  std::set<int> gaveBack_;              // defenders who intercepted one and fumbled it back
  std::string readTopic_;             // the topic of the read just made (tagged on its line)
  bool liveTalk_ = false;  // PBP has been calling the action (the result line can be shorter)
  bool fumbleCalled_ = false;
  double lastTickX_ = -1;
  double lastTickT_ = -10;
  int lastMarkSaid_ = -1;
  const Player* catchDef_ = nullptr;
  const Player* hitBy_ = nullptr;
  std::vector<const Player*> pancakes_;
  std::vector<Shed> sheds_;
  std::vector<std::string> movesSeen_;
  int downBefore_ = 1;
  double toGoBefore_ = 10, losBefore_ = 25, clockBefore_ = 900;
  int qBefore_ = 1;
  PerTeam<int> scoreBefore_{};
  TeamKey possBefore_ = TeamKey::Away;
  int adjHome_ = 0, adjAway_ = 0;  // points on the board the Game has not added yet (a touchdown at the whistle)

  // kicks
  PlayLog kickLog_;
  bool kickPending_ = false, kickFootSaid_ = false, kickLandSaid_ = false, kickSetupSaid_ = false;
  bool returnFootSaid_ = false, firstKick_ = true;
  int kickKind_ = -1;

  // the game's story so far
  struct DriveTrack {
    TeamKey team = TeamKey::Away;
    double startBallOn = 25;
    int plays = 0;
    double startClock = 900;
    int startQ = 1;
    bool active = false;
  } drive_;
  struct Streak {
    int completions = 0;  // the QB's in a row
    int incompletions = 0;
  };
  std::unordered_map<int, Streak> qbStreak_;
  std::unordered_map<int, int> mentions_;  // player id -> times named
  std::unordered_map<std::string, int> lastBySurname_;      // a shared surname -> the man it last meant
  struct SharedName {
    std::string full;
    int id;
    std::string last;
  };
  std::vector<SharedName> sharedFull_;
  std::unordered_map<std::string, int> rotUse_;              // a man's call family (its first wording) -> times said
  std::unordered_map<std::string, std::string> rotFamily_;   // a wording -> its family
  std::unordered_map<std::string, double> topicT_;
  std::set<std::string> said_;
  std::unordered_map<std::string, std::deque<int>> recentPick_;
  std::deque<std::string> recentText_;
  std::set<std::string> spokenSet_;  // every sentence of talk said this game (never twice if there is another way)
  int unanswered_ = 0;
  TeamKey unansweredBy_ = TeamKey::Home;
  int leadChanges_ = 0;
  std::optional<TeamKey> lastLeader_;
  int blitzCount_[2] = {0, 0};
  int runStreak_ = 0;
  TeamKey runStreakTeam_ = TeamKey::Home;
  TeamKey tdBy_ = TeamKey::Home;      // the touchdown the try belongs to
  std::set<std::string> shared_;     // "ABBR:Last" two teammates share: said with the first name
  std::function<std::string(const Player*)> sharedTeamOf_;
  int lastSaidTotal_ = 0;  // blitzes called by the defense: [0] away D, [1] home D
};

}  // namespace booth
}  // namespace tag
