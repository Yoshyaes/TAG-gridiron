// Match director: menus, match flow (play calling -> snap -> live sim -> result -> special teams),
// cameras, input and HUD. C++ port of the browser build's match controller.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <memory>
#include <map>
#include <string>
#include "Sim/League.h"
#include "Sim/Game.h"
#include "Sim/PlaySim.h"
#include "MatchDirector.generated.h"

class AAthlete;
class AStadium;
class ACineCameraActor;
class SGridironOverlay;
class AStaticMeshActor;

UENUM()
enum class EMatchState : uint8 { Menu, Call, Presnap, Live, Post, Kick, Wait, Final, Paused, Replay };

UENUM()
enum class ECamMode : uint8 { Broadcast, Sideline, Skycam, Player };

UCLASS()
class TAGGRIDIRON_API AMatchDirector : public AActor
{
	GENERATED_BODY()

public:
	AMatchDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// ---- menu / setup (called from UI)
	void ShowMainMenu();
	void ShowTeamSelect(bool bUserPlays);
	void StartMatch(int HomeIdx, int AwayIdx, bool bUserPlays, bool bUserHome, int WeatherKind, bool bNight, double QuarterLen);
	void QuitToMenu();

	// ---- shared state read by the HUD
	tag::League League;
	std::unique_ptr<tag::Game> Game;
	/** What the scorebug's down box shows while a kick or try is on screen: the sim has already moved the game on (the next
	 *  phase, down and spot) before the play is animated. Empty = read the game live (scrimmage presnap and play). */
	FString HudDown;
	/** The score the scorebug shows while a canned kick is on screen (the sim has already added the points before the ball
	 *  is in the air: an extra point read 10-0 at the snap of the try). -1 = read the game live. Index: tag::TeamKey. */
	int32 HudScore[2] = {-1, -1};
	/** Between plays: the sim takes the whole runoff off the game clock at the play call (Game::preSnap); the scorebug
	 *  winds it down instead, over the call and the pre-snap (HudRunoffLeft game seconds still to come off), and shows
	 *  the 40 s play clock running with it (PlayClockUse = the seconds the offense uses before the snap). */
	double HudRunoff = 0.0, HudRunoffLeft = 0.0;
	/** From the whistle to the call the clock already runs on the scorebug (HudPreRun game seconds, at most 12 and only a
	 *  running clock); the call's runoff then takes up from there. The play clock (PlayClockF) runs from 40 at the whistle
	 *  and reaches its snap value (40 - the seconds the offense uses) by the snap. */
	double HudPreRun = 0.0;
	bool bPreRun = false;
	float PlayClockF = 40.f, PlayClockSnap = 10.f, PlayClockRate = 1.f;
	bool bPlayClockOn = false;
	/** A human plays: both clocks run in real seconds from the whistle (the play clock down to 0, then delay of game on
	 *  the user's offense), and at the snap the sim's game clock is set to what the scorebug showed (SyncRealClock).
	 *  CPU-only games keep the fast wind-down above. bClockLive: the game clock runs between these plays. */
	bool bRealClock = false, bClockLive = false;
	void SyncRealClock();
	void DelayOfGame();
	int32 PlayClockShown() const { return bPlayClockOn ? FMath::Max(0, FMath::CeilToInt(PlayClockF)) : -1; }
	void HoldHudScore() { HudScore[0] = Game->score[tag::TeamKey::Home]; HudScore[1] = Game->score[tag::TeamKey::Away]; }
	void ReleaseHudScore() { HudScore[0] = HudScore[1] = -1; }
	std::unique_ptr<tag::PlaySim> Sim;
	const tag::PlaySim* EventSim = nullptr;  // sim whose events were last scanned for keyed one-shots
	size_t SeenEvents = 0;
	EMatchState State = EMatchState::Menu;
	bool bWatch = false;
	float Speed = 1.f;
	std::optional<tag::TeamKey> UserTeam;

private:
	// flow
	void Next();
	void CallPlays(bool bTwoPoint);
	void StartPresnap(const tag::OffPlay* Op, const tag::DefCall* Dc);
	void ApplyPlay();
	void OnPlayDead();
	void DoPunt();
	void DoFG();
	void StartKick(const tag::SpecialResult& R, int Dir, int Kind, const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& RecvTeam, double Los);
	void UpdateKick(float Dt);
	void Wait(float Sec);
	void ShowFinal();
	void TogglePause();
	std::vector<tag::Player*> Pick11(tag::Team& T, const char* RoleName);
	bool UserOff() const;
	bool UserDef() const;

	// world
	AAthlete* AthleteFor(tag::Player* P, tag::TeamKey Side);
	void ShowOnly(const std::vector<tag::Player*>& Ps);
	void SyncPlay(float Dt, float Alpha);
	/** Locked-up blockers: the upper bodies of a clip lean in further than the hips' spacing allows (a rusher's chest
	 *  inside the blocker's shoulder); each frame the pair is eased apart until chest and helmet clear the other man. */
	void ResolveEngagedOverlap();
	void CapturePrev();
	void UpdateCamera(float Dt, const FVector& Focus);
	FVector FocusPoint() const;
	void ReadUserInput();
	void AssignLabels();
	void BuildWorld(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight);

	UPROPERTY()
	TObjectPtr<AStadium> Stadium;

	UPROPERTY()
	TObjectPtr<ACineCameraActor> Camera;

	UPROPERTY()
	TObjectPtr<AStaticMeshActor> Ball;

	UPROPERTY()
	TMap<int32, TObjectPtr<AAthlete>> Athletes;

	/** Instant replay: every visible player's shown pose and the ball, sampled at 30 Hz from the snap to just after
	 *  the whistle, played back in slow motion from a field-level reverse angle after a big play. */
	struct FReplayAth { int32 Id = 0; FTransform World; TArray<FQuat4f> Rot; TArray<FVector3f> Pos; };
	struct FReplayFrame { float T = 0.f; FTransform Ball; bool bBall = false; TArray<FReplayAth> Ath; };
	TArray<FReplayFrame> ReplayBuf;
	float ReplayClock = 0.f, ReplayPostRec = 0.f, ReplayT = 0.f, ReplayEnd = 0.f;
	int32 ReplayIdx = 0, ReplayCount = 0;
	bool bReplayWorthy = false;
	FVector ReplayFocus = FVector::ZeroVector;
	TArray<FTransform> ReplayScratch;
	void RecordReplayFrame(float Dt);
	bool StartReplay();
	void TickReplay(float Dt, bool bSkip);

	/** Bench players standing in each team's area (deep backups, so they never meet themselves on the field). */
	UPROPERTY()
	TArray<TObjectPtr<AAthlete>> Sideline;
	TArray<bool> SidelineIsHome;
	/** Chain crew (holding the rods and the down box) and the head coaches. */
	UPROPERTY()
	TArray<TObjectPtr<AAthlete>> Officials;
	std::vector<std::unique_ptr<tag::Player>> OfficialPeople;
	void SpawnSidelines(const tag::Team& Home, const tag::Team& Away);
	void TickSidelines(float Dt);
	/** After the whistle the men jog toward the ball (the offense gathering behind it, the defense in front) until
	 *  the next play is called; the pre-snap walk then takes them on to the formation from there. Director-side
	 *  positions (world, cm), the dead sim's own are left alone. */
	std::map<tag::Ent*, FVector> PostPos;
	std::map<tag::Ent*, float> PostYaw;
	TMap<const void*, float> PostSpd; // each man's jog speed after the whistle (cm/s): he speeds up, he does not start at a jog
	float PostAge = 0.f;                  // seconds since the whistle  // where each man faces once he has arrived (the huddle looks in)
	void TickPostJog(float Dt);
	/** The men between plays once the play's sim is gone (the play call): where each stands, which way he faces,
	 *  whether he is on the next offense; and where the dead ball lay. StartPresnap walks them in from here. */
	struct FBetweenMan { tag::Player* P = nullptr; FVector Pos = FVector::ZeroVector; float Yaw = 0.f; bool bOff = false; };
	std::vector<FBetweenMan> Between;
	FVector BetweenBallW = FVector::ZeroVector;
	/** Each man's huddle spot and the facing into it (bOff: the next offense), around the dead ball's spot. */
	FVector HuddleSpot(bool bOff, bool bQB, int32 Slot, int32 Count, const FVector& BallW, int32 InDir, float& OutYaw) const;
	/** The umpire: stands in the offensive backfield before the snap, jogs to the dead ball at the whistle, carries it
	 *  to the new spot and sets it down, then goes back to his position. */
	UPROPERTY()
	TObjectPtr<AAthlete> Umpire;
	FVector UmpPos = FVector::ZeroVector;
	bool bUmpHasBall = false, bUmpSpotted = false;
	void TickUmpire(float Dt);
	/** The rest of the crew (NFL mechanics; the referee above): 0 umpire in the offensive backfield opposite the referee,
	 *  1 down judge and 2 line judge on the sidelines at the line, 3 side judge and 4 field judge 20 yd deep on the
	 *  sidelines, 5 back judge 25 yd deep in the middle (deep men on the goal line / end line in the red zone). Wings
	 *  follow the ball along their sideline and mark its spot at the whistle, deep men stay ahead of the deepest player,
	 *  everyone steps around players in every phase. */
	UPROPERTY()
	TArray<TObjectPtr<AAthlete>> Crew;
	TArray<FVector> CrewPos;
	void TickCrew(float Dt);
	/** Where an official at Pos steps to stay Keep (cm) clear of every player over the next Look seconds (each man's
	 *  path read from his sim velocity: sidestep his line, not just back away); Urgency 0..1 = how close the nearest gets. */
	FVector OfficialDodge(const FVector& Pos, float Keep, float Look, float& Urgency);
	/** Last resort: a player inside 1.2 m pushes the official straight out (up to 6 m/s), whatever his target. */
	void OfficialPushOut(FVector& Pos, float Dt);
	/** A live kick return (the sim fields the kick and runs it back; the user returns it or covers it). */
	bool bLiveReturn = false;
	void StartLiveReturn(const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& RecvTeam);
	int32 SidelineScore[2] = {0, 0};
	float SidelineCheerLeft[2] = {0.f, 0.f};

	// ---- pre-snap (MatchPresnap.cpp): play art on the field (hold R2), hot routes, audibles and flip, protection
	// slides, motion; on defense coverage audibles, d-line / linebacker shifts, the coverage shell and assignments
	enum class EPre : uint8 { Base, HotPick, HotRoute, Audible, Protect, MotionPick, Motion, DLine, LBs, Shell, Assign };
	EPre PreMode = EPre::Base;
	tag::Ent* PreEnt = nullptr;  // the receiver being hot routed / sent in motion
	int32 AudibleSel = 0;
	float ArtAuto = 0.f;         // the art shows itself for a moment as the play comes in
	float PresnapClock = 0.f;
	/** Seconds every man has been set (nobody walking to his spot): the CPU snaps only after a full second, as the rules require. */
	float SetClock = 0.f;
	bool bPreConsumed = false;   // a pre-snap control used this frame's button (it does not also snap the ball)
	UPROPERTY()
	TObjectPtr<class ATagPlayArt> PlayArt;
	void BeginPresnapUI();
	void TickPresnap(float Dt);
	void EndPresnapUI();
	void BuildPlayArt();
	void UpdateIcons();
	void RefreshPresnapPanel();
	void RebuildPlay(const tag::OffPlay* Op, const tag::DefCall* Dc, bool bFlip);
	/** +1 if the screen's right is the offense's left (+y), else -1 (D-pad left / right are screen directions). */
	int32 ScreenRightSign() const;
	std::vector<const tag::OffPlay*> AudibleOff() const;
	std::vector<const tag::DefCall*> AudibleDef() const;
	int32 LabelOf(const tag::Ent* E) const;
	/** -TagArtTest: a user game that drives every pre-snap call (offense, then defense) and screenshots each. */
	bool bArtTest = false;
	/** -TagCallShot: a user game that screenshots the play-call screens (offense, then defense) with their cards up. */
	bool bCallShot = false;
	int32 CallShotStep = 0;
	bool bArtDidOff = false, bArtDidDef = false, bArtLiveShot = false, bArtDrawShot = false;
	float ArtT = 0.f, ArtExitT = 0.f;
	int32 ArtStep = 0;
	void TickArtTest(float Dt);
	/** -TagArtTest -TagPadTest: the same, but every call made by injecting the gamepad's keys (R2 held, triangle,
	 *  the D-pad...) through the player controller, each result checked and logged PASS / FAIL. */
	bool bPadTest = false;
	int32 PadFails = 0, PadPasses = 0;
	TArray<TPair<FKey, float>> PadHeld;  // keys down and when to let go
	void PadPress(const FKey& K, float Hold = 0.1f);
	void PadCheck(bool bOk, const TCHAR* What);
	void TickPadTest(float Dt);

	TSharedPtr<SGridironOverlay> Overlay;
	bool bPlayStationPad = false; // a DualSense was found through RawInput (hints name its buttons)
	/** The device the player used last: a pad (its buttons are drawn and named) or the keyboard (its keys). */
	bool bPadActive = false;
	TArray<FKey> PadKeys, KeyboardKeys;
	void TrackInputDevice(APlayerController* PC);
	bool PadUI() const { return bPadActive; }
	/** The highlight stick: the right stick has to come back to the middle before the next flick counts. */
	bool bRStickArmed = true;
	/** -TagMoveShot (look-dev): the user runs the ball every play and takes the highlighted move when it shows;
	 *  screenshots of the cue (tag_hl_*.png) and of each move (tag_mv_*.png), then quits. */
	bool bMoveShot = false;
	int32 MoveShotsHl = 0, MoveShotsMv = 0, MoveShotPlays = 0;
	bool bMoveShotHlThisPlay = false;
	/** The live hint is the ball carrier's (his moves) since he got the ball this play. */
	bool bCarrierHint = false;
	/** The highlight over the user's ball carrier: the move that suits the man about to tackle him (UpdateIcons). */
	void UpdateHighlight();
	/** A hint written with PlayStation names, renamed for an Xbox-style pad when that is what is plugged in. */
	FString PadHint(const TCHAR* PS) const;
	/** The match loading screen: the highlight reel under UI/LoadingScreen while BuildWorld runs (the engine's movie
	 *  player draws it on its own thread). On when a user starts a match from the menu, off for automation. */
	bool BeginLoadingScreen(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight);
	/** -TagHighlights: a CPU game filmed play by play (a screenshot every frame of every live play, post-whistle and
	 *  replay, under Saved/Screenshots/<platform>/Highlights) for Tools/Art/build_highlights.py to cut the reel from. */
	bool bHighlights = false;
	int32 HlClip = 0, HlFrame = 0;
	bool bHlInClip = false, bHlReplayLogged = false;
	double HlStart = 0.0;
	void TickHighlights(float Dt);
	/** -TagHighlights -TagHlFull [-TagHlDir=<dir>] [-TagQuarterLen=<s>]: the whole game to the final whistle (no clip
	 *  or time cap), the pre-snap and kicks filmed too, and per frame a row per man (sim state + the shown body's
	 *  bones and the clip driving it) in <dir>/../telemetry/c<clip>.csv, the play's events in ev<clip>.csv. */
	bool bHlFull = false;
	bool bHlHeld = false;
	bool bHlSkipClip = false;
	FString HlTele;
	void HlTelemetry(int32 Frame);
	void HlFlush();
	/** -TagPauseTest (a -TagPlayNow user game): once the play call is up, Esc; checks the world is held (bone
	 *  positions a second apart), Esc again; checks the play call is back. Screenshots each step, PASS / FAIL lines. */
	/** -TagSoak=<seconds> (-TagQuarterLen=<s>): a CPU game left running that long, then exit (the pack library's
	 *  per-quarter draws, memory, errors over a whole game). */
	float SoakSecs = 0.f;
	double SoakStart = 0.0;
	bool bPauseTest = false;
	bool bPlayNowPending = false;  // -TagPlayNow: the match starts once the startup movie is over (the loading screen then shows)
	int32 PauseStep = 0;
	double PauseT = 0.0;
	FVector PauseProbe[3];
	void TickPauseTest();
	bool bKickCatchLogged = false;
	/** The pause menu holds the whole world (engine pause); Esc / OPTIONS again, or Resume, lets it go. */
	bool bEnginePaused = false;
	void Unpause();
	std::map<tag::Ent*, FVector2D> Prev;
	std::map<tag::Ent*, FString> Labels;
	ECamMode CamMode = ECamMode::Broadcast;
	ECamMode LastCamMode = ECamMode::Broadcast;
	FVector CamPos = FVector(4572, 6000, 2500);
	FVector CamLook = FVector(4572, 0, 0);
	FVector IdleFocus = FVector(4572, 0, 0);
	float Shake = 0.f;
	// broadcast camera operator (UpdateCamera): critically damped moves, lens zoom to the action, handheld drift
	FVector CamVel = FVector::ZeroVector;
	FVector LookVel = FVector::ZeroVector;
	float CamFocal = 30.f;
	float CamFocalVel = 0.f;
	float CamTime = 0.f;
	/** Post-play cut-in: a telephoto close shot of the player who made the play (TD, takeaway, sack, big gain). */
	UPROPERTY()
	TObjectPtr<AActor> CutInTarget;
	float CutInLeft = 0.f;
	void StartCutIn(const tag::Ent* Who, float Seconds);
	float Timer = 0.f;
	float Acc = 0.f;
	int Dir = 1;
	bool bTwoPoint = false;
	int LastQ = 1;
	int32 PackQ = 1;  // the quarter the pack library's current draw is for (FAthleteAssets::ReseedPack each new one)
	EMatchState ResumeState = EMatchState::Menu;
	float MenuOrbit = 0.f;
	// automation (-TagAutoWatch): start a CPU game and capture screenshots, then exit
	bool bAuto = false;
	float AutoT = 0.f;
	int32 AutoShot = 0;
	bool bAutoEstablish = false;
	UPROPERTY()
	TObjectPtr<AActor> CloseupTarget;
	int32 CloseupView = 0;
	bool bShotThrow = false;
	bool bShotCatch = false;
	bool bShotGetUp = false;
	int32 EventShotFrames = 0;
	FString EventShotName;
	void TickAutomation(float Dt);
	/** -TagLineup: look-dev lineup of six athletes, front and back shots. */
	void TickLineup(float Dt);
	bool bLineup = false;
	/** -TagMoveTest: locomotion test course (sprint + 90 deg cut + stop, curve + stop, backpedal + break), one runner at a
	 *  time with a tracking side camera and a screenshot every 0.3 s (motion matching look-dev; -TagNoMM shows the blend spaces for A/B). */
	void TickMoveTest(float Dt);
	/** -TagGapTest: the situations that only come up now and then, played on demand and shot every 0.33 s: under
	 *  center snap + pitch, a layout catch, a paired gang tackle with the pile and an injured carrier. */
	void TickGapTest(float Dt);
	bool bGapTest = false;
	/** -TagLineTest: one tackle against one end, every trench interaction in turn, filmed side on. */
	bool bLineTest = false;
	void TickLineTest(float Dt);
	/** -TagTackleTest: every paired tackle in turn, filmed side on. */
	bool bTackleTest = false;
	void TickTackleTest(float Dt);
	/** -TagTackleCam [-TagTcDir=<dir>] [-TagTcClips=<n>] [-TagQuarterLen=<s>]: a CPU game where every defender closing
	 *  on the ball carrier starts a close, low side-on clip (frames t<clip>_<frame>.png) that runs to 3 s past the
	 *  tackle, and per frame the bones of every man near the ball (<dir>/../tcdata/t<clip>.csv) and the sim's events
	 *  (tev<clip>.csv), for Tools/Anim/tackle_check.py (body overlap, hands on the carrier, pops, limbs through the turf). */
	bool bTackleCam = false;
	int32 TcClip = 0, TcFrame = 0, TcMaxClips = 16;
	float TcT = 0.f, TcAfter = -1.f;
	size_t TcEv0 = 0;
	TWeakObjectPtr<AAthlete> TcCarrier;
	FVector TcSide = FVector::ZeroVector, TcFocus = FVector::ZeroVector;
	FString TcTele;
	void TickTackleCam(float Dt);
	void TcRecord();
	void TcFlush();
	/** Pair a tackle: Tk tackles Cr, the sim put the carrier down at SimSpot (world). Pair -1 picks from the hit
	 *  (angle, yards after contact); both men fall together, anchored in the pair's authored relation. */
	void StartPairedTackle(AAthlete* Tk, AAthlete* Cr, const FVector& SimSpot, int32 Pair, bool bDive, float PileSecs = 0.f, bool bBigHit = false);
	/** The last paired tackle (a pile started at the sim's wrap is retargeted when the sim ends it). */
	TWeakObjectPtr<AAthlete> PileCarrier, PileTackler;
	FVector PileSpot = FVector::ZeroVector;
	/** A gang tackle from the pack's three-man captures (pack_gangs.json): Tk and As on Cr at once. False when none
	 *  fits where the two are (the caller pairs Tk and Cr and As piles on). */
	bool StartGangTackle(AAthlete* Tk, AAthlete* Cr, AAthlete* As, const FVector& SimSpot);
	TWeakObjectPtr<AAthlete> GangAssist;
	TWeakObjectPtr<AAthlete> LastPileCarrier;
	FVector LastPileDir = FVector::ForwardVector;
	double LastPileT = -10.0;
	FVector LastPileSpot = FVector::ZeroVector;
	/** A second man arriving on the pile. */
	void StartPileOn(AAthlete* As, AAthlete* Cr);
	int32 GapShot = 0;
	bool bMoveTest = false;
	FVector MovePos = FVector::ZeroVector;
	FVector MoveVel = FVector::ZeroVector;
	int32 MoveRunner = -1;
	int32 MoveShot = 0;
	float PerfT = 0.f;      // automation frame-time sampling
	int32 PerfFrames = 0;
	float LineupT = 0.f;
	bool bLineupFloatCheck = false; // -TagLineup: the floating-people check ran (once, at the crowd shot)
	int32 LineupShot = 0;
	UPROPERTY()
	TArray<TObjectPtr<class AAthlete>> Lineup;

	struct FKick
	{
		tag::SpecialResult Res;
		int Dir = 1;
		float T = 0;
		float Dur = 3;
		int Kind = 0; // 0 kickoff, 1 punt, 2 fg/pat
		std::vector<tag::Player*> Kick;
		std::vector<tag::Player*> Recv;
		double FromX = 35;
		double ToX = 75;
		bool bAnnounced = false;
		/** The ball's flight, solved once: (yards past FromX, height in yards) every 1/60 s from the kick, under gravity
		 *  and air drag (a kicked ball climbs fast and drops steeply; the old symmetric arc read as a lob). */
		std::vector<FVector2D> Path;
	};
	std::unique_ptr<FKick> Kick;

	friend class SGridironOverlay;
};
