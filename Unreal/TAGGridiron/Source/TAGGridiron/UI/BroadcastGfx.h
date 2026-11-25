// The broadcast's graphics (Research/NextLevel/04 A1, A6, A9; the card for A5's pregame open): the score bug and the
// states that slide out of it (FLAG, TIMEOUT, RED ZONE, TWO-MINUTE, TOUCHDOWN, FIRST DOWN, FIELD GOAL, SAFETY), the
// lower third of a big play, the sim-truth stat tags on a replay, the drive summary and the third-down graphic, and the
// pregame matchup card.
//
// One family of type (Roboto, the engine's own, Apache 2.0: Black for the numbers, Bold Condensed for the words). The bug
// keeps the hierarchy the network graphics leads describe: the score and the clock first, the down and distance in a
// neutral cell (white on dark: never yellow, which is the flag's colour and only the flag's), the timeouts as pips under
// each team, the play clock only in its last 10 s. Every graphic is drawn in one leaf widget's paint (under the menus,
// over the HUD's text): boxes, the team marks and text that is shaped once when it changes (never in paint), every
// move timed on the overlay's game clock with eased curves (the same on any frame rate).
//
// Each graphic takes its data as a plain struct (below) and has one call that shows it (FBroadcastGfx::Show*). Without
// anyone calling them the game still gets them: Pump (every frame, from SGridironOverlay::Tick) builds the bug from the
// book (Game: scores as shown, timeouts, the down, the clocks), fires the bug's states from what changes in it and from
// the director's Banner / Toast calls, tracks each drive for its summary, puts the third-down graphic up between the
// plays and the stat tags on a replay. So they work with the commentary off.
//
// -TagGfxShots (StartGfxShots, UI/BroadcastGfx.cpp): every state on a flat field colour and over a real frame, written to
// Saved/Screenshots/<platform>/tag_gfx_<w>x<h>_NN_<name>.png, with the checks (every text in its box at 720p, 1080p and
// 1440p, every team's mark loads, the stat tag picker), ending in "TAG RESULT TagGfxShots" (Game/TagResult.h).
#pragma once

#include "CoreMinimal.h"
#include "Fonts/ShapedTextFwd.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Sim/League.h"
#include "Sim/Game.h" // (tag::PlayLog: PenaltyFromLog)

class SGridironOverlay;
class AMatchDirector;
class SWidget;
struct FSlateBrush;
struct FGfxCanvas;
namespace tag { class Game; struct PlayResult; struct Ball; }

/** The type the graphics use (Roboto's Black and Bold Condensed at fixed sizes): an index into one table. */
enum class EGfxFont : uint8
{
	BugAbbr, BugScore, BugClock, BugPeriod, BugDown, BugSpot, PlayClock, AlertKind, AlertDetail,
	L3Name, L3Sub, L3Stats, TagLabel, TagValue, Kicker, DrvResult, ColNum, ColLabel, ThdDown, ThdLine,
	CardCity, CardName, CardNum, CardLabel, CardLine, CardRow, CardChip, CardVs, PotgNum, PotgStat, PotgName, BoxLine, Count
};

/** A line of text shaped for the screen once (and again only when the words, the face or the screen's scale change);
 *  the paint draws the shaped glyphs. Cut: the same text cut to a width with an ellipsis (a box it does not fit). */
struct FGfxText
{
	FString Str;
	int32 Font = -1;
	float Scale = 0.f;
	FShapedGlyphSequencePtr Full, Cut;
	FVector2D FullSize = FVector2D::ZeroVector, CutSize = FVector2D::ZeroVector; // slate units
	float CutW = -1.f;
	/** Shape S (when it, the face or the scale changed); false when there is no renderer to shape with. */
	bool Shape(const FString& S, EGfxFont InFont, float InScale);
	/** The glyphs to draw in a box MaxW wide (<= 0: any width) and their size: the full text, else the cut one. */
	const FShapedGlyphSequencePtr& For(float MaxW, FVector2D& OutSize);
};

// ------------------------------------------------------------------ the data each graphic shows

/** One team on the score bug (0 the visitors, on the left; 1 the home team). */
struct FGfxBugSide
{
	FString Abbr;                  // "POR"
	int32 Score = 0;
	int32 Timeouts = 3;            // left
	int32 TimeoutsMax = 3;         // 3 (2 in overtime)
	FLinearColor Color = FLinearColor(0.2f, 0.2f, 0.25f);   // the team's colour, lifted to show on the dark bug
	FLinearColor Color2 = FLinearColor::White;              // its second colour
	const FSlateBrush* Logo = nullptr;                      // the team's mark (RosterUI::Logo), null: none
	bool bBall = false;            // has the ball
};

/** The score bug: always on in a game. */
struct FGfxScoreBug
{
	FGfxBugSide Side[2];
	FString Period;                // "1ST" .. "4TH", "OT", "FINAL"
	FString Clock;                 // "12:34" (empty: none, as at the final)
	FString Down;                  // "3RD & 7", "1ST & GOAL", "KICKOFF", "EXTRA POINT", "TOUCHDOWN"
	FString Spot;                  // "POR 34" (empty: none)
	int32 PlayClock = -1;          // seconds; shown only at 10 and under (-1: off)
};

/** What slides out of the top of the bug for a few seconds. In order of importance (a more important one takes the
 *  slot from a lesser one; a lesser one waits its turn). */
enum class EGfxAlert : uint8 { None, FirstDown, FieldGoal, RedZone, TwoMinute, Timeout, Safety, Flag, Touchdown };

struct FGfxAlert
{
	EGfxAlert Kind = EGfxAlert::None;
	FString Text;                  // "FLAG", "TIMEOUT", "RED ZONE"
	FString Detail;                // "OFFENSIVE HOLDING  ·  #72 J. SMITH  ·  POR" (empty: none)
	int32 Side = -1;               // the team it is for (its colour; a timeout drains its pip), -1 none
	float Secs = 3.5f;             // out this long (a flag also stays while the flag is down: SGridironOverlay::SetFlag)
};

/** A tracking number from the sim ("CATCH PROBABILITY  24%"). Field / Raw: where it came from and its exact value (the log
 *  line each tag writes, for checking the screen against the play's telemetry). */
struct FGfxStatTag
{
	FString Label;                 // "CATCH PROBABILITY"
	FString Value;                 // "24%", "0.6 YD", "21.3 MPH"
	FString Field;                 // "Ball::fateP*(1-fatePbu)", "PlayResult::airYards"
	double Raw = 0.0;
	float Score = 0.f;             // how remarkable against the league's norms (1 = just worth showing)
};

/** The tags for one replay: one, two when the second is remarkable too. */
struct FGfxStatTags
{
	FGfxStatTag Tag[2];
	int32 Num = 0;
	float Delay = 0.9f;            // seconds after the call before the first comes in (the replay's key moment)
	float Secs = 4.2f;             // on screen
};

/** A big play's lower third: the man and his game so far, his team's colour and mark. */
struct FGfxLowerThird
{
	int32 Side = 0;                // his team on the bug (0 visitors, 1 home)
	FString Name;                  // "#89  R. OCHOA"
	FString Sub;                   // "WR  ·  PORTLAND"
	FString Stats;                 // "2 REC  ·  25 YDS  ·  1 TD"
	bool bTag = false;             // a tracking number beside it
	FGfxStatTag Tag;
	float Secs = 4.5f;
};

/** A drive, summed up after a score or a long one. */
struct FGfxDriveSummary
{
	int32 Side = 0;
	FString Result;                // "TOUCHDOWN", "FIELD GOAL", "PUNT", "INTERCEPTION", "TURNOVER ON DOWNS"
	FString Start;                 // "OWN 25", "OPP 40", "MIDFIELD"
	int32 Plays = 0;
	int32 Yards = 0;
	int32 Seconds = 0;             // game clock used
	float Secs = 6.f;
};

/** Third (or fourth) down, before the snap: the down and the offense's conversions today. */
struct FGfxThirdDown
{
	int32 Side = 0;
	FString Down;                  // "3RD & 8"
	FString Kicker;                // "3RD DOWN TODAY"
	FString Line;                  // "4 OF 9  ·  44%", "FIRST OF THE GAME"
	float Secs = 5.f;
};

/** The pregame matchup card. */
struct FGfxMatchupPlayer
{
	FString Pos;                   // "QB"
	FString Name;                  // "#12  M. RIVERS"
	int32 Ovr = 0;
	FString Grade;                 // "X-FACTOR", "SUPERSTAR", "STAR" or empty
};
struct FGfxMatchupTeam
{
	FString City, Name, Abbr;      // "PORTLAND", "LUMBERJACKS", "POR"
	FString Scheme;                // "WEST COAST  ·  4-3 COVER 3"
	FLinearColor Color = FLinearColor(0.2f, 0.2f, 0.25f), Color2 = FLinearColor::White;
	const FSlateBrush* Logo = nullptr;
	int32 Ovr = 0, Off = 0, Def = 0;
	int32 OvrRank = 0, OffRank = 0, DefRank = 0; // in the league (0: not known)
	FGfxMatchupPlayer Stars[3];
	int32 NumStars = 0;
};
struct FGfxMatchup
{
	FGfxMatchupTeam Side[2];       // 0 the visitors (left), 1 the home team
	FString Kicker;                // "TODAY'S MATCHUP"
	FString Conditions;            // "CLEAR, 71F, WIND 2 MPH  ·  NIGHT"
};

/** A team as the halftime / final box and the player of the game show it. */
struct FGfxTeamId
{
	FString City, Name, Abbr;      // "PORTLAND", "LUMBERJACKS", "POR"
	FLinearColor Color = FLinearColor(0.2f, 0.2f, 0.25f), Color2 = FLinearColor::White;
	const FSlateBrush* Logo = nullptr;
};

/** A team's numbers for the box (from the book: Game::team, Game::log, Game::score). */
struct FGfxTeamTotals
{
	int32 Score = 0;
	int32 ByQ[5] = {-1, -1, -1, -1, -1}; // points in each period, 1st .. 4th and overtime (-1: not played)
	int32 Yards = 0, PassYds = 0, RushYds = 0, FirstDowns = 0, ThirdConv = 0, ThirdAtt = 0, Turnovers = 0, TopSecs = 0, Sacks = 0, Penalties = 0, PenYds = 0;
};

/** One of a team's leaders on the box: "PASSING", "#12  M. RIVERS", "14/22  ·  186 YDS  ·  2 TD". */
struct FGfxLeader
{
	bool bOn = false;
	FString Role, Name, Line;
};

/** The box: the score by period, the team comparison, each side's leaders (passing, rushing, receiving, defense). */
struct FGfxBoxStats
{
	FString Kicker;                // "HALFTIME", "FINAL", "FINAL  ·  OVERTIME"
	FString Sub;                   // the right of the head strip ("PORTLAND LEADS 14-10")
	FGfxTeamId Team[2];            // 0 the visitors, 1 the home team
	FGfxTeamTotals Tot[2];
	int32 NumQ = 2;                // periods in the table (2 at the half, 4 at the end, 5 with overtime)
	FGfxLeader Leaders[2][4];
};
struct FHalftimeStats : FGfxBoxStats {};
struct FFinalStats : FGfxBoxStats {};

/** A big number on the player of the game card ("241" over "PASS YDS"). */
struct FGfxBigStat
{
	FString Value, Label;
};

/** The player of the game: picked by GamePoints (fantasy-style scoring of everything he did; x1.2 on the winning side). */
struct FPlayerOfGame
{
	bool bValid = false;
	int32 Side = 0;
	FGfxTeamId Team;
	FString Name;                  // "MARCUS RIVERS"
	FString Number;                // "#12"
	FString Pos;                   // "QUARTERBACK"
	FString Grade;                 // "X-FACTOR" or empty
	FGfxBigStat Stats[4];
	int32 NumStats = 0;
	FString Line;                  // his whole game ("18/27  ·  241 YDS  ·  3 TD  ·  6 CAR  ·  34 YDS")
	FString Why;                   // "31.6 GAME POINTS  ·  WINNING SIDE"
	double Points = 0.0;
};

/** A flag's panel (Research/NextLevel/05 A11): "HOLDING  ·  OFFENSE #74  ·  10 YARDS  ·  REPEAT 1ST DOWN". */
struct FGfxPenalty
{
	int32 Side = -1;               // the team flagged (its colour)
	FString Foul;                  // "HOLDING"
	FString Team;                  // "POR"
	FString Who;                   // "OFFENSE" / "DEFENSE"
	FString Number;                // "#74" (empty: nobody named)
	FString Yards;                 // "10 YARDS", "HALF THE DISTANCE"
	FString Result;                // "REPEAT 1ST DOWN", "AUTOMATIC 1ST DOWN", "LOSS OF DOWN"
	float Secs = 5.5f;
};

/** The down, distance, spot and possession before a play (a flag's panel works out what it did from it). */
struct FGfxSnapState
{
	bool bValid = false;
	int32 Down = 1;
	double ToGo = 10.0, BallOn = 25.0;
	int32 PossSide = 0;
};

/** The two teams side by side on one number (the booth's team comparisons: total yards, penalties, giveaways). */
struct FGfxTeamStat
{
	FString Label;                 // "TOTAL YARDS"
	FString Val[2];                // visitors, home: "312", "4-35"
	double Num[2] = {0.0, 0.0};    // (the bars)
	float Secs = 5.f;
};

/** The booth's line on the air (ATagBooth): what it is about, so the graphic for it comes up as it is said.
 *  Topic is the booth's story key: "qb:<pid>", "rb:<pid>", "wr:<pid>", "stat:<pid>", "sk:<pid>:<n>", "dbint:<pid>",
 *  "qbint:<pid>:<n>", "3rd:<ABBR>", "yds", "pens", "tos"; a referee's line has bRef. */
struct FGfxBoothCue
{
	int32 Id = 0;                  // the booth's line id (one graphic a line)
	int32 Kind = -1;               // tag::booth::Kind as a number (6 Story, 7 Score, 10 Ref)
	bool bRef = false;
	FString Topic;
	FString Text;                  // the caption (the log)
	float Secs = 4.f;              // how long it runs (the graphic stays a beat longer)
};

// ------------------------------------------------------------------ the graphics

class FBroadcastGfx : public TSharedFromThis<FBroadcastGfx>
{
public:
	explicit FBroadcastGfx(SGridironOverlay* InOwner);

	// ---- show this (each returns at once; the graphic animates itself in, holds and goes)
	/** The bug's data as given (a scripted bug: the game's own is built by Pump unless SetScripted). */
	void SetScoreBug(const FGfxScoreBug& B);
	const FGfxScoreBug& ScoreBug() const { return Bug; }
	void ShowAlert(const FGfxAlert& A);
	void ShowStatTags(const FGfxStatTags& T);
	void HideStatTags();
	void ShowLowerThird(const FGfxLowerThird& L);
	void ShowDriveSummary(const FGfxDriveSummary& D);
	void ShowThirdDown(const FGfxThirdDown& T);
	/** Secs <= 0: up until HideMatchupCard. */
	void ShowMatchupCard(const FGfxMatchup& M, float Secs = 0.f);
	void HideMatchupCard();
	bool MatchupCardUp() const { return CardUp(0); }
	// the halftime and postgame show's cards (04 A10), in the matchup card's place (one full-screen card at a time).
	// Secs <= 0: up until hidden.
	void ShowHalftimeStats(const FHalftimeStats& S, float Secs = 0.f);
	void HideHalftimeStats() { HideCard(1); }
	bool HalftimeStatsUp() const { return CardUp(1); }
	void ShowPlayerOfGame(const FPlayerOfGame& P, float Secs = 0.f);
	void HidePlayerOfGame() { HideCard(2); }
	bool PlayerOfGameUp() const { return CardUp(2); }
	void ShowFinalStats(const FFinalStats& S, float Secs = 0.f);
	void HideFinalStats() { HideCard(3); }
	bool FinalStatsUp() const { return CardUp(3); }
	/** A flag's panel in the lower-third band (the referee's numbers: foul, side, number, yards, what it does). */
	void ShowPenalty(const FGfxPenalty& P);
	/** The two teams on one number, in the band (a booth comparison). */
	void ShowTeamStat(const FGfxTeamStat& S);
	/** The booth's line just went on the air: the graphic that goes with it, now (a player's line, the third-down rate,
	 *  a team comparison, the drive with a score call, the flag's panel with the referee). Not over a live play, not a
	 *  second time what the game already showed by itself. True when a graphic came up. */
	bool OnBoothLine(const FGfxBoothCue& Cue);

	// ---- the data, from the sim and the league
	/** The most remarkable tracking numbers of a play (at most two; none when nothing stands out against the league):
	 *  catch probability (Ball::fateP after the breakup chance, shown under 50%), separation at the catch, air yards,
	 *  yards after the catch, time to throw (quick or long), time to a sack, broken tackles, the carrier's top speed. */
	static FGfxStatTags PickStatTags(const tag::PlayResult& R, const tag::Ball& B, float TopMph);
	/** The card for a game: names, marks, colours, ratings (and their league rank when L is given), schemes, each side's
	 *  three best (X-Factors and Superstars first). */
	static FGfxMatchup MatchupFor(const tag::Team& Away, const tag::Team& Home, const tag::League* L, const FString& Conditions);
	/** The box at the half and at the end, from the book: points by period, the team totals, each side's leaders. */
	static FHalftimeStats HalftimeStatsFor(const tag::Game& G);
	static FFinalStats FinalStatsFor(const tag::Game& G);
	/** The player of the game: the most GamePoints (1 a 25 passing yards, 4 a passing touchdown, -2 an interception, 1 a
	 *  10 rushing or receiving yards, 6 a touchdown, 0.5 a catch, -2 a lost fumble; on defense 1 a tackle, 1.5 a tackle
	 *  for loss or a breakup, 4 a sack, 5 an interception, 3 a forced or recovered fumble, 6 a defensive touchdown; 3 a
	 *  field goal, 1 an extra point), x1.2 on the winning side. */
	static FPlayerOfGame PlayerOfGameFor(const tag::Game& G);
	/** A flag's panel from the book's penalty line (Game::applyPresnap / finishPlay: "PENALTY on POR: Offensive Holding
	 *  (#74 J.Smith), enforced. ...") and the down before it. False: the line is not an enforced penalty. */
	static bool PenaltyFromLog(const tag::Game& G, const tag::PlayLog& L, const FGfxSnapState& Before, FGfxPenalty& Out);
	/** The score bug for the game as it is now (what Pump shows). */
	static FGfxScoreBug BugFromGame(const AMatchDirector& D, double ClockOffset);
	/** A team's side of the bug: its abbreviation, colours and mark (no score, timeouts or ball). */
	static FGfxBugSide BugSideFor(const tag::Team& T);
	/** The top speed (mph) the sim had this man at with the ball in the play just run (0: none). */
	float CarrierTopMph(const tag::Player* P) const;
	/** The game puts the stat tags on a replay by itself as it starts (on by default; off once the director calls
	 *  ShowStatTags at the replay's key moment itself). */
	void SetAutoReplayTags(bool b) { bAutoReplayTags = b; }

	// ---- driven by the overlay (UI/GridironOverlay.cpp)
	/** Every frame: the clock, the bug from the game, the states and graphics the game's changes call for. */
	void Pump(float Dt);
	/** The director's banner: true when the bug carries it instead (a flag); a touchdown, a two-minute warning and the
	 *  rest are also slid out of the bug, the banner still shown. */
	bool OnBanner(const FString& Words, const FString& Sub);
	/** The director's toast: true when a graphic carries it instead (a big play's stat line, the first down). */
	bool OnToast(const FString& Words);
	/** The widget the graphics are painted in (a leaf filling the overlay, under the menus). */
	TSharedRef<SWidget> MakeLayer();
	int32 Paint(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& G);
	/** -TagOldBug: the old widget bug instead (A/B). */
	static bool OldBug();
	/** The pregame open is on (AMatchDirector::PregameNow): the HUD's chips and toasts wait for it (never when scripted). */
	bool OpenOn() const;
	/** The open still holds the score bug back: a TV open brings it in at the kickoff, once the units' beat is over. */
	bool OpenHidesBug() const;
	/** A toast held back while the open is on (shown a moment after it ends): true when it was held. */
	bool HoldToast(const FString& Words);
	/** A graphic that wants the screen's lower part or all of it is up (a card, a lower third, a drive summary, the
	 *  third-down graphic): the hint line stands down. */
	bool CoverUp() const { return BandKind != 0 || CardT0 >= 0.0; }
	/** The full-screen matchup card is on screen (in, held or going): the markers over the men stand down. */
	bool CardOnScreen() const { return CardT0 >= 0.0; }

	// ---- -TagGfxShots
	/** Scripted: Pump leaves the bug and the graphics to the script (no game state, no banner or toast hooks). */
	void SetScripted(bool b) { bScripted = b; }
	/** Everything but the bug and the card taken down at once (the state, its queue, the band, the tags). */
	void ClearGraphics();
	/** A flat field colour behind everything the graphics draw (the plain-background pictures). */
	void SetBackdrop(bool b) { bBackdrop = b; }
	/** Every graphic up now laid out at the scale of each of 720p, 1080p and 1440p: each text against its box. Returns the
	 *  number of texts checked; a line per text that does not fit goes into Fails. */
	int32 CheckFits(TArray<FString>& Fails);
	/** The card's and the bug's words for every team in the league against their boxes at the three scales. */
	int32 CheckLeagueFits(const tag::League& L, TArray<FString>& Fails);
	/** Nothing animating (every graphic in or out). */
	bool Settled() const;
	double Now() const { return Clock; }

private:
	void PaintAll(FGfxCanvas& C);
	void PaintBackdrop(FGfxCanvas& C);
	void PaintBug(FGfxCanvas& C, const FVector2D& BugTL);
	void PaintAlert(FGfxCanvas& C, const FVector2D& BugTL);
	void PaintBand(FGfxCanvas& C, const FVector2D& BugTL);
	void PaintTags(FGfxCanvas& C, const FVector2D& BugTL);
	void PaintCard(FGfxCanvas& C);
	void PaintBox(FGfxCanvas& C, float In);
	void PaintPotg(FGfxCanvas& C, float In);
	void ShowCard(int32 Kind, float Secs);
	void HideCard(int32 Kind);
	bool CardUp(int32 Kind) const { return CardT0 >= 0.0 && !bCardOut && CardKind == Kind; }
	static int32 BandPri(int32 Kind);
	/** A key shown lately (a player's line, a team number): the booth's graphic is not shown twice. */
	bool ShownLately(const FString& Key, double Secs) const;
	void NoteShown(const FString& Key);

	void StartAlert(const FGfxAlert& A);
	void BeginBand(int32 Kind, float Secs);
	bool BandFree(int32 Kind) const;
	void ResetGame(const tag::Game* InGame);
	void PumpGame(const AMatchDirector& D, const tag::Game& InGame);
	void TrackDrive(const tag::Game& InGame);
	FLinearColor SideColor(int32 S) const;

	SGridironOverlay* Owner = nullptr;
	double Clock = 0.0;               // the overlay's game clock (s): every animation is timed on it
	bool bScripted = false, bBackdrop = false, bAutoReplayTags = true;

	// the bug
	FGfxScoreBug Bug;
	double BugT0 = -10.0;             // when it came on
	double ScoreT[2] = {-10.0, -10.0};// a score went up (its cell flashes)
	float PlayClockAmt = 0.f;         // the play clock's cell, 0 in .. 1 out
	int32 DrainSide = -1, DrainPip = -1;
	double DrainT0 = -10.0;
	FGfxText TxAbbr[2], TxScore[2], TxPeriod, TxClock, TxDown, TxSpot, TxPlayClock;
	FString ScoreStr[2];
	FString PlayClockStr;
	int32 PlayClockShownFor = -2;

	// the state sliding out of it
	FGfxAlert Alert;
	double AlertT0 = -10.0, AlertOutT0 = -10.0;
	bool bAlertOut = false;
	TArray<FGfxAlert> AlertQueue;
	FGfxText TxAlert, TxAlertDetail;

	// the lower-third band (one at a time: 1 a big play's lower third, 2 a drive summary, 3 the third-down graphic)
	int32 BandKind = 0;
	double BandT0 = -10.0, BandOutT0 = -10.0;
	float BandSecs = 0.f;
	bool bBandOut = false;
	FGfxLowerThird L3;
	FGfxDriveSummary Drive;
	FGfxThirdDown Third;
	FGfxText TxL3Name, TxL3Sub, TxL3Stats, TxL3TagLab, TxL3TagVal;
	FGfxText TxDrvKicker, TxDrvResult, TxDrvNum[3], TxDrvLab[3];
	FGfxText TxThdDown, TxThdKicker, TxThdLine;
	FString DrvKicker, DrvNum[3];

	// the stat tags (a replay)
	FGfxStatTags Tags;
	double TagsT0 = -10.0, TagsOutT0 = -10.0;
	bool bTagsOn = false, bTagsOut = false;
	FGfxText TxTagLab[2], TxTagVal[2];

	// the matchup card
	FGfxMatchup Card;
	double CardT0 = -10.0, CardOutT0 = -10.0;
	float CardSecs = 0.f;
	bool bCardOut = false;
	struct FCardText { FGfxText City, Name, Num[3], Lab[3], Scheme, Stars, Pos[3], Row[3], Ovr[3], Grade[3]; };
	FCardText TxCard[2];
	FGfxText TxCardKicker, TxCardCond, TxCardVs;
	FString CardNum[2][3], CardLab[2][3], CardOvr[2][3];
	int32 CardKind = 0;               // 0 the matchup, 1 the halftime box, 2 the player of the game, 3 the final box
	// the box (halftime / final): the table's numbers and the comparison's, as words once
	FGfxBoxStats Box;
	FString BoxQ[2][6];               // each period's points and the total
	FString BoxVal[9][2];             // the comparison's nine rows, each side
	double BoxNum[9][2] = {};
	FGfxText TxBoxTeam[2], TxBoxQ[2][6], TxBoxQHead[6], TxBoxLab[9], TxBoxVal[9][2], TxBoxLead[2][4][3];
	// the player of the game
	FPlayerOfGame Potg;
	FGfxText TxPotgKicker, TxPotgName, TxPotgNum, TxPotgPos, TxPotgGrade, TxPotgStatV[4], TxPotgStatL[4], TxPotgLine, TxPotgWhy;

	// the flag's panel (band 4) and the booth's team number (band 5)
	FGfxPenalty Pen;
	FString PenSub;
	FGfxText TxPenFlag, TxPenFoul, TxPenSub, TxPenYds, TxPenRes;
	FGfxTeamStat TeamStat;
	FGfxText TxTsLab, TxTsVal[2], TxTsAbbr[2];
	bool bL3Booth = false;            // the lower third up is a booth line's (it goes at the snap like the game's graphics)
	// a flag waiting for the referee's line (the booth's referee voice on), and the down before each play
	FGfxPenalty PendingPen;
	bool bPenPending = false;
	double PenPendingT = 0.0;
	FGfxSnapState SnapBefore;
	int32 SnapLogN = -1;
	// the booth: its line last seen on the air, the keys shown lately
	int32 LastBoothId = 0;
	TArray<TPair<FString, double>> ShownKeys;

	// what the game was last frame (Pump: what changed calls for a graphic)
	const tag::Game* LastGame = nullptr;
	int32 SigQ = -1, SigClock = -1, SigScore[2] = {-1, -1}, SigTo[2] = {-1, -1}, SigPoss = -1, SigPhase = -1, SigDown = -1, SigToGo = -1, SigBall = -1, SigState = -1;
	int32 SigPlayClock = -2;
	FString SigHud;
	int32 SeenScore[2] = {-1, -1}, SeenTo[2] = {-1, -1};
	int32 LastStateI = -1;
	bool bLastHud = false;
	// the pregame open: was it on last frame, when it ended, the toast held back for after it
	bool bLastOpen = false;
	double OpenEndT = -10.0;
	FString HeldToast;
	// the drive being played (its summary when it ends)
	struct FDriveTrack
	{
		bool bOn = false;
		int32 Id = 0, Poss = 0, StartPlays = 0, Score = 0, OppScore = 0, Q = 1;
		double StartBallOn = 25.0, LastBallOn = 25.0, StartSecs = 0.0;
		int32 LogStart = 0;
	} DriveT;
	double GameSecs = 0.0, LastClockV = -1.0;
	int32 LastQ = -1;
	bool bDrivePending = false;
	double DrivePendingUntil = 0.0;
	FGfxDriveSummary DrivePending;
	int32 RedZoneDrive = -1;
	int32 ThirdShownLog = -1;
	// the carrier's top speed this play (yd/s from the sim's velocity, kept per man who had the ball)
	unsigned long long SpeedSerial = 0;
	const tag::Player* SpeedWho[4] = {nullptr, nullptr, nullptr, nullptr};
	float SpeedMph[4] = {0.f, 0.f, 0.f, 0.f};
};
