// The broadcast's presentation as plain data (Game/MatchBroadcast.cpp makes it; anybody may read it): the pace setting
// (Settings > Presentation), and what is on screen right now - the replay (its angle, its subject, its key moment), the
// between-play shot (its kind and subject), the pregame open (its stage, the matchup) and the wipe. The on-screen
// graphics (score bug, lower thirds, stat tags, the matchup card) read these through AMatchDirector's ReplayNow(),
// ShotNow(), PregameNow() and WipeNow(); nothing here draws.
#pragma once

#include "CoreMinimal.h"

namespace tag { struct Player; struct Team; }

// ------------------------------------------------------------------ the pace (Research/NextLevel/04 A8)
/** How much of the broadcast plays between the snaps. Auto: Full when watching a CPU game, Standard when playing. */
enum class ETagPace : uint8 { Auto, Full, Standard, Quick };

namespace TagPace
{
	/** The saved choice (GameUserSettings.ini [TAG.Settings] Presentation; Auto until one is picked). */
	ETagPace Setting();
	/** Auto -> Full -> Standard -> Quick -> Auto (the settings row), saved. */
	void Step();
	/** The pace in force: the setting, Auto resolved (bWatch: a CPU game watched), -TagPace=full|standard|quick over both. */
	ETagPace Current(bool bWatch);
	const TCHAR* Name(ETagPace P);
	/** The settings row's line under the name. */
	const TCHAR* Blurb(ETagPace P);
}

// ------------------------------------------------------------------ what is on screen
/** The replay's camera angles (04 A2). */
enum class ETagReplayAngle : uint8 { HighEndZone, TightIso, Pylon, Reverse };

struct FTagReplayView
{
	bool bOn = false;            // a replay is on screen (its wipes included)
	int32 Angle = 0;             // which of the package's angles is playing (0, 1)
	int32 NumAngles = 0;         // 1, or 2 on a touchdown or a takeaway
	ETagReplayAngle Type = ETagReplayAngle::Reverse;
	const TCHAR* AngleName = TEXT("");    // "high end zone", "tight iso", "pylon", "reverse"
	FString Why;                 // the replay's reason (MatchBroadcast's DecideReplay: "touchdown", "sack", "catch", ...)
	const tag::Player* Subject = nullptr; // the key man (the receiver at the catch, the rusher on a sack, the carrier)
	int32 SubjectId = -1;        // his tag::Player id (-1 none)
	bool bSubjectHome = false;   // his team is the home team
	const TCHAR* KeyKind = TEXT("");      // "catch", "hit", "release", "run", "takeaway", "score", "kick"
	float KeyIn = 0.f;           // seconds of play until the key moment (negative once it is past)
	bool bAtKey = false;         // the slow-motion part around the key moment (a stat tag's moment)
	float Rate = 1.f;            // the playback speed now (1 real time; the ramp eases to ~0.35 through the key)
	int32 Serial = 0;            // a new replay, a new number
};

/** The between-play shots (04 A3). */
enum class ETagShot : uint8 { None, Hero, Opponent, Bench, Coach, Crowd, Official, Wide, OpenCrane, OpenCard, OpenTeams, ShowCrane };

struct FTagShotView
{
	bool bOn = false;
	ETagShot Type = ETagShot::None;
	const TCHAR* TypeName = TEXT("");     // "hero", "opponent", "bench", "coach", "crowd", "official", "wide", "open crane", "show crane" ...
	FString Variant;             // which framing ("front", "low", "left section" ...)
	const tag::Player* Subject = nullptr; // the man in the shot (a player or a head coach; null for the bench, the crowd, a wide)
	int32 SubjectId = -1;
	int32 Team = -1;             // whose: 1 home, 0 away, -1 nobody's
	float Elapsed = 0.f, Left = 0.f;
	int32 Serial = 0;            // a new shot, a new number
};

/** The pregame open (04 A5): its stage, and the matchup the card shows. */
struct FTagMatchStar
{
	const tag::Player* Player = nullptr; // the side's top man: an X-Factor, else a Superstar, else the best overall
	FString Name;                // "J. Smith"
	FString Pos;                 // "QB"
	FString Grade;               // "X-Factor", "Superstar", "Star" or ""
	FString Ability;             // his X-Factor's name (else his first Superstar ability's), "" none
};

struct FTagPregameView
{
	bool bOn = false;
	int32 Stage = 0;             // 0 none, 1 the crane over the stadium, 2 the matchup card, 3 both teams on the field
	bool bCardUp = false;        // the matchup card belongs on screen now
	bool bLong = false;          // the long version (Full pace) or the short one
	float StageT = 0.f, StageLeft = 0.f;
	const tag::Team* Home = nullptr;
	const tag::Team* Away = nullptr;
	FTagMatchStar HomeStar, AwayStar;
	FString Weather;             // the game's weather line ("Clear, 64 F")
	bool bNight = false;
};

/** The halftime show (04 A10): the teams leaving the field, the first half's stats, the half's moments (the leading
 *  bench, the crowd), the second-half kickoff units. */
struct FTagHalftimeView
{
	bool bOn = false;
	int32 Stage = 0;             // 0 none, 1 the teams leaving, 2 the first half's stats, 3 the moments, 4 the kickoff units
	const TCHAR* StageName = TEXT("");
	bool bStatsUp = false;       // the first half's stats belong on screen now (ShowHalftimeStats was called)
	bool bLong = false;          // the long version (Full pace)
	float StageT = 0.f, StageLeft = 0.f;
	int32 Serial = 0;
};

/** The postgame show (04 A10): the winners' celebration, the final score, the player of the game, the final stats; then
 *  the box score menu as before. */
struct FTagPostgameView
{
	bool bOn = false;
	int32 Stage = 0;             // 0 none, 1 the celebration, 2 the final score, 3 the player of the game, 4 the final stats
	const TCHAR* StageName = TEXT("");
	int32 Winner = -1;           // 1 home, 0 away, -1 a tie
	const tag::Player* PlayerOfGame = nullptr;
	int32 PlayerOfGameTeam = -1; // 1 home, 0 away
	FString PlayerOfGameLine;    // "24/31, 288 YDS, 3 TD"
	bool bPlayerUp = false, bStatsUp = false;
	bool bLong = false;
	float StageT = 0.f, StageLeft = 0.f;
	int32 Serial = 0;
};

/** The TAG wipe in and out of a replay (drawn by Game/TagWipe). Progress 0..1; Covered: the screen is fully hidden. */
struct FTagWipeView
{
	bool bOn = false;
	float Progress = 0.f;
	bool bCovered = false;
};
