// Match director: menus, match flow (play calling -> snap -> live sim -> result -> special teams),
// cameras, input and HUD. C++ port of the browser build's match controller.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <memory>
#include <map>
#include <set>
#include <string>
#include <functional>
#include "Sim/League.h"
#include "Sim/Game.h"
#include "Sim/PlaySim.h"
#include "Game/TagInput.h"
#include "Game/TagMoveBuffer.h"
#include "Game/TagContact.h"
#include "Game/TagFeel.h"
#include "MatchDirector.generated.h"

class AAthlete;
class AStadium;
class ACineCameraActor;
class SGridironOverlay;
struct FPreRow;
struct FUserMarkerUI;
class ATagBooth;
class ATagAudio;
class AStaticMeshActor;
class UAnimSequence;

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
	/** The pre-snap foul rolled for this play, called once the formation is set (a false start, an offside). */
	void CallPresnapFlag();
	int32 PlayClockShown() const { return bPlayClockOn ? FMath::Max(0, FMath::CeilToInt(PlayClockF)) : -1; }
	void HoldHudScore() { HudScore[0] = Game->score[tag::TeamKey::Home]; HudScore[1] = Game->score[tag::TeamKey::Away]; }
	void ReleaseHudScore() { HudScore[0] = HudScore[1] = -1; }
	/** The score the bug shows: the book's, held where a kick holds it, and a scrimmage score counted from the whistle
	 *  (the book takes it when the play is applied, after the post-play shots; the bug sat at 0-0 through the touchdown
	 *  celebration). Out: the down box's word for that score, or empty. */
	int32 ScoreShown(tag::TeamKey K, FString* Word = nullptr) const;
	std::unique_ptr<tag::PlaySim> Sim;
	const tag::PlaySim* EventSim = nullptr;  // sim whose events were last scanned for keyed one-shots
	size_t SeenEvents = 0;
	EMatchState State = EMatchState::Menu;
	bool bWatch = false;
	float Speed = 1.f;
	std::optional<tag::TeamKey> UserTeam;

	// ---- local multiplayer (Game/MatchMulti.cpp, UI/Lobby.cpp): up to four people on one screen, each on his own pad
	// or the keyboard. Each picks a side; each drives one man at a time; on a side with two or more, one calls the plays.
	/** What one player's controls keep between frames (a throw button held, the right stick's flick, his sprint, the
	 *  pre-snap switch). A one-player game keeps these in the director's own members; in a multiplayer game each
	 *  player's are swapped in while his input is read (SwapHuman). */
	struct FHumanCtl
	{
		int32 PadThrowIdx = -1, PadTapIdx = -1;
		double PadThrowT0 = 0.0, PadTapT = 0.0;
		bool bRStickArmed = true;
		float SprintStamina = 1.f;
		bool bSprinting = false, bSprintSpent = false;
		bool bSwitchHeld = false, bSwitchPicked = false, bSwitchStickArmed = true;
		float SwitchHeldT = 0.f;
		bool bPreMoved = false, bCarrierHint = false, bRushHint = false;
		FTagFeelCtl Feel;          // the throw's windup, the switch's grace (Game/TagFeel.h)
	};
	struct FTagHuman
	{
		int32 Dev = -1;            // FTagInputHub's device
		bool bPad = false;
		TArray<int32> Twins;       // the same pad seen through a second interface (XInput and GameInput): its buttons are his too
		int32 Team = -1;           // the lobby's pick: -1 not yet (watching), 0 away, 1 home
		bool bCaller = false;      // calls his side's plays (one per side)
		int32 Slot = -1;           // the sim's user slot he drives this play (-1 none)
		tag::Player* Last = nullptr; // the man he had: he keeps him from play to play while he is on the field
		FHumanCtl Ctl;
		FTagReader Reader() const { return FTagReader(Dev, bPad); }
	};
	TArray<FTagHuman> Humans;
	bool bMulti = false;
	/** The lobby is up (its input read from every device in Tick). */
	bool bLobby = false;
	void ShowLobby();
	void StartMultiMatch(int HomeIdx, int AwayIdx, int WeatherKind, bool bNight, double QuarterLen);
	/** The lobby can go on: someone has picked a side. */
	bool LobbyReady() const;
	/** A side with people on it (multiplayer) / the user's side (one player). */
	bool TeamHuman(tag::TeamKey K) const;
	/** The player who calls a side's plays (index into Humans), -1 for a CPU side. */
	int32 CallerOf(tag::TeamKey K) const;
	static tag::TeamKey KeyOf(int32 Team) { return Team == 1 ? tag::TeamKey::Home : tag::TeamKey::Away; }
	/** "P2", the player's place in the lobby (his colour: PlayerColor). */
	static FString HumanTag(int32 H) { return FString::Printf(TEXT("P%d"), H + 1); }
	FString HumanDevice(int32 H) const;
	/** The team select's view of the lobby: a side with players, their tags ("P1  P3"), the sides swapped (each player
	 *  goes with his team). */
	bool TeamHumanInLobby(bool bHome) const;
	FString LobbyTags(bool bHome) const;
	void LobbySwapSides();

	/** The bodies meeting this frame (Game/TagContact.h; the same as FTagContacts::Get().ThisFrame()). */
	TArrayView<const FTagContact> ContactsThisFrame() const { return FTagContacts::Get().ThisFrame(); }

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
	/** Under the loading screen (StartMatch): every man either team can field is made now, hidden and asleep
	 *  (AAthlete::SetDormant), so the first snap, punt, field goal and substitution do not spawn and dress anyone
	 *  (AthleteFor still makes a man it has not seen). The units and the depth chart's starters first, then the rest of
	 *  both rosters while the time allows. Not in the lineup / test-course modes; -TagNoPrespawn: off (A/B). */
	void PrespawnAthletes();
	/** Under the loading screen too: the clips and the play art that were loaded on their first use in play (the
	 *  pre-snap flag's captures, the hard counts, the art's actor and material), held in WarmClips. */
	void WarmMatchAssets();
	void WarmPresnapAssets();
	UAnimSequence* WarmClip(const FString& Path);
	UPROPERTY()
	TMap<FString, TObjectPtr<UAnimSequence>> WarmClips;
	void SyncPlay(float Dt, float Alpha);
	/** Locked-up blockers: the upper bodies of a clip lean in further than the hips' spacing allows (a rusher's chest
	 *  inside the blocker's shoulder); each frame the pair is eased apart until chest and helmet clear the other man. */
	void ResolveEngagedOverlap();
	/** Piles: each man on the ground is given the heads, chests and hips of the men down with him to keep his arms out of. */
	void UpdateKeepOut();
	/** After the whistle, now and then: a teammate walks over and pulls the tackled man to his feet (the pack's two-man
	 *  help-up capture). 1: the helper is on his way, 2: the two are on the capture. */
	void TickHelpUp(float Dt);
	TWeakObjectPtr<AAthlete> HelpDown, HelpBy;
	TWeakObjectPtr<AAthlete> HelpWalker; // the man pulled up: on his feet now, he walks to the huddle like the rest
	int32 HelpStage = 0;
	float HelpAge = 0.f, HelpT = 0.f, HelpYaw = 0.f, HelpSpd = 0.f;
	FVector HelpSpot = FVector::ZeroVector, HelpByPos = FVector::ZeroVector;
	bool HelpBusy(const AAthlete* A) const { return HelpStage > 0 && (A == HelpBy.Get() || (HelpStage == 2 && A == HelpDown.Get())); }
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
	/** The last play's frames (up to 300), their arrays kept for the next play's recording instead of freed at the snap. */
	TArray<FReplayFrame> ReplayPool;
	void ResetReplayBuf();
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
	std::map<const tag::Ent*, FVector> CoastV; // each man's velocity at the whistle (cm/s): he carries on, easing up, for a beat
	std::set<const tag::Ent*> PostStay;
	std::map<const tag::Ent*, FVector> PostVel; // the post-play jog's velocity per man (cm/s) // after the whistle, men left where a capture put them (for the rest of the play)
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
	/** The crew's motion (TickCrew): velocity (accelerates and arrives, no jumps to full speed), the spot he is going to
	 *  (it moves on only when the new one is 1.5 yd off it: not with every man's step), whether he is under way, and the
	 *  way he faces (turned at a man's pace). */
	TArray<FVector> CrewVel, CrewTgt;
	TArray<float> CrewYaw;
	TArray<bool> CrewGo;
	unsigned long long CrewKickSerial = 0; // (the kick play the crew was placed for)
	void TickCrew(float Dt);
	/** Where an official at Pos steps to stay Keep (cm) clear of every player over the next Look seconds (each man's
	 *  path read from his sim velocity: sidestep his line, not just back away); Urgency 0..1 = how close the nearest gets. */
	FVector OfficialDodge(const FVector& Pos, float Keep, float Look, float& Urgency);
	/** Last resort: a player inside 1.2 m pushes the official straight out (up to 6 m/s), whatever his target. */
	void OfficialPushOut(FVector& Pos, float Dt);
	/** A live kick return (the sim fields the kick and runs it back; the user returns it or covers it). */
	bool bLiveReturn = false;
	void StartLiveReturn(const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& RecvTeam);
	/** A field goal / extra point played (Sim/PlaceKick.cpp, Game/MatchKicking.cpp): the two units live from the set,
	 *  the snap, the protection and the rush, the strike; the snapper, the holder and the kicker on the pack's capture
	 *  timed to the sim's snap and foot. Look: the block unit's rush (-1 its own; a user's kick shows it while he aims). */
	bool bPlaceKick = false;
	void StartPlaceKick(bool bPat, const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& BlockTeam, double SetT, int32 Look = -1);
	/** The place kick's operation on the capture (SyncPlay); the ball in the snapper's / holder's hands before the foot. */
	bool SyncPlaceOperation(tag::Ent* E, AAthlete* A, float Dt);
	void SyncPlaceBall();
	/** The call once the ball is through, short or blocked (a banner), once per kick. */
	void TickPlaceKickCalls();
	unsigned long long FgSceneSim = 0, FgCallSim = 0;
	int32 FgStage[3] = {0, 0, 0};
	TWeakObjectPtr<AAthlete> FgHolderA;
	int32 FgUserLook = -1;  // the look shown while a user aims (the play then rushes it)
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
	/** A pre-snap foul waiting for the men to be set at the line: it was called as the play was picked, before
	 *  anyone reached the line. Called PreFlagAt seconds into the set (or as the user tries to snap). */
	std::optional<tag::Penalty> PendingPreFlag;
	float PreFlagAt = 0.f;
	bool bPreConsumed = false;   // a pre-snap control used this frame's button (it does not also snap the ball)
	UPROPERTY()
	TObjectPtr<class ATagPlayArt> PlayArt;
	void BeginPresnapUI();
	void TickPresnap(float Dt);
	void EndPresnapUI();
	void BuildPlayArt();
	void UpdateIcons();
	void RefreshPresnapPanel();
	/** The pre-snap panel's rows for the man in slot 0 (bOffense: the offense's menus, else the defense's); false: no panel. */
	bool PresnapPanelRows(bool bOffense, bool bPad, const FTagReader& PC, FString& Title, FString& Sub, TArray<FPreRow>& Rows) const;
	/** The defense's pre-snap menus (audible, coverage shell, shifts, hot route, walk + switch) for the man in slot 0 and
	 *  the reader PC. An audible is left in DefAudibleTo for the caller to make (RebuildPlay remakes the slots). */
	void PresnapDefMenus(const FTagReader& PC, float Dt);
	const tag::DefCall* DefAudibleTo = nullptr;
	void RebuildPlay(const tag::OffPlay* Op, const tag::DefCall* Dc, bool bFlip);
	/** A call at the line, shown: the quarterback's audible capture and the men who hear it (the back, the linebackers)
	 *  re-settling; a defensive call is the middle linebacker's hand signals. bSignal: a hot route / motion (the quieter hand signal). */
	void PlayAudibleCue(bool bOffense, bool bSignal = false);
	float PrevStickX = 0.f;
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
	bool bXboxPad = false; // an Xbox (Microsoft) pad is connected (FindPlayStationPad scans for both)
	TArray<FKey> PadKeys, KeyboardKeys;
	void TrackInputDevice(APlayerController* PC);
	/** A Sony pad is plugged in or paired right now (RawInput device list). */
	bool FindPlayStationPad();
	bool PadUI() const;
	/** Settings (kept in GameUserSettings.ini, [TAG.Settings]). Captions: the booth's words as text; off unless asked for. */
	bool CaptionsOn() const { return bOptCaptions; }
	void SetCaptionsOn(bool b);
	/** The right stick's up / down as the game reads it (+ = pushed up). Pads come through GameInput with it either way
	 *  round (the user's read reversed: his kick swing ran backwards), so it is a setting, on by default. */
	bool InvertRightY() const { return bOptInvertRY; }
	void SetInvertRightY(bool b);
	float RightStickY(class APlayerController* PC) const;
	float RightStickY(const FTagReader& R) const;
	bool bOptInvertRY = true;
	bool bOptCaptions = false;
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
	/** The trenches' bars over each engaged pass rush (the rusher's energy, his blocker's power) in a user game on
	 *  defense, the user's own pair larger with the move he last tried. */
	void UpdateLineBars();
	/** The screen-space marks over the men (user ring, icons, highlight cue, line bars) are projected once the frame's
	 *  camera and bodies are final: in Tick they used last frame's camera before the athletes moved, so the ring
	 *  trailed a runner by a frame (1-2 m at a few fps). */
	void OnPostActorTick(UWorld* InWorld, ELevelTick InTickType, float InDt);
	FDelegateHandle PostTickHandle;
	bool bOverlayDue = false;
	bool bRushHint = false;
	/** The aggressive catch: TRIANGLE (Y) held while the pass is in the air - the catch goes up for the spectacular one. */
	bool bAggroCatch = false;
	/** The catch started early (a beat before the ball arrives) this play, and by whom: the catch event does not restart it. */
	unsigned long long PrimedSerial = 0;
	unsigned long long HandoffSerial = 0; // (the two-man handoff started this play)
	/** The run exchange as drawn. HandoffNoneSerial: no capture fits this play's handoff (the keyed clip plays at the
	 *  exchange). HandXfer*: the ball going from the quarterback's hand into the back's arms over a few frames from the
	 *  sim's exchange (it jumped from one man's socket to the other's in a frame). Toss*: a toss in the air, from the
	 *  quarterback's hands as it left them. HandGiveSerial: the giving hand latched for this play's handoff. */
	unsigned long long HandoffNoneSerial = 0, HandXferSerial = 0, TossSerial = 0, HandGiveSerial = 0, TossClipSerial = 0;
	int32 HandoffSeq = 0;
	bool bHandGiveRight = true;
	// (the sim's exchange came before the capture's contact frame: the ball stays in the quarterback's hand until the
	// back's hands get there, 0.3 s at most)
	unsigned long long HandDeferSerial = 0;
	double HandDeferT = 0.0;
	double HandXferT = 0.0;
	TWeakObjectPtr<AAthlete> HandXferFrom;
	FVector TossHandW = FVector::ZeroVector;
	unsigned long long PaFakeSerial = 0;  // (the two-man play-action fake started this play)
	unsigned long long KickCatchSerial = 0; // (the returner's catch started before the ball came down, this play)
	/** The defender who went up for the ball in the air this play (his swat started a beat before it arrived). */
	unsigned long long SwatSerial = 0;
	const tag::Ent* SwatBy = nullptr;
	/** The pass: the throw timed from the windup this play; where the ball left the passer's hand (drawn from there). */
	unsigned long long ThrowTimedSerial = 0, ReleaseSerial = 0;
	FVector ReleaseHandW = FVector::ZeroVector, ReleaseOff = FVector::ZeroVector;
	double ReleaseOffT = -1.0;
	const tag::Ent* PrimedCatcher = nullptr;
	/** The ball against the hands that play it (SyncPlay). BallBend: what the drawn ball is off the sim's, brought
	 *  onto the hands of the man the pass's outcome was rolled for over the last 0.25 s (45 cm at most) and let go
	 *  after. Attach*: what is left between the ball's flight and his hold when the sim gives it to him, eased out
	 *  (it went from the air into his socket in one frame). BallDrawn*: the ball as drawn last frame. FatePrimed*: the
	 *  rolled outcome the captures were started for (a defender's play rolled later replaces the receiver's).
	 *  CatchCheck*: -TagCatchCheck, a log line per contact with the hands' distance from the ball's path and the
	 *  largest off-path step of the drawn ball in the 0.3 s around it. */
	FVector BallBend = FVector::ZeroVector, AttachOff = FVector::ZeroVector;
	float AttachLeft = 0.f, AttachDur = 0.f;
	FVector BallDrawnW = FVector::ZeroVector, BallDrawnVelW = FVector::ZeroVector, BallDrawnRel = FVector::ZeroVector;
	unsigned long long BallDrawnSerial = 0, DivePrimedSerial = 0, IntPrimedSerial = 0; // (Dive / Int: the layout, the one-man interception started before the ball got there)
	int32 BallDrawnMode = -1;
	const tag::Ent* BallDrawnHolder = nullptr;
	int32 FatePrimedFate = 0;
	const tag::Ent* FatePrimedBy = nullptr;
	struct FCatchCheck { FString What; const tag::Ent* Who = nullptr; int32 Num = -1; float HandPath = -1.f, HandNow = -1.f, HandMid = -1.f, HandMin = 1e9f, Bend = 0.f, Snap = 0.f, MaxStep = 0.f, BallZ = 0.f, BallSpeed = 0.f, Left = 0.15f; };
	/** -TagCatchCheck: the ball as drawn this frame against the open checks (see SyncPlay, after the ball's drawing). */
	void TickCatchCheck(float Dt, float BendWas, float SnapNow);
	TArray<FCatchCheck> CatchChecks;
	TArray<TPair<const tag::Ent*, FString>> CatchCheckNew;
	TArray<FVector2D> BallSteps; // (time s, off-path step cm) of the last 0.3 s
	double CatchCheckClock = 0.0;
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
	/** the player controller this director ticks after (its input is read fresh every frame, paused or not) */
	TWeakObjectPtr<APlayerController> InputPrereq;
	void Unpause();
	std::map<tag::Ent*, FVector2D> Prev;
	std::map<tag::Ent*, FString> Labels;
	ECamMode CamMode = ECamMode::Broadcast;
	ECamMode LastCamMode = ECamMode::Broadcast;
	float CamHoldT = 0.f; // how long the pad's VIEW / CREATE has been held (the camera changes at 0.5 s)
	/** A break in the broadcast (the end of a quarter, the two-minute warning): the score panel up and the camera on a
	 *  slow crane over the stadium while the teams change ends or the clock stops, then a cut back to the game with the
	 *  men set (it froze on the last play's men idling for a beat, then teleported them to the other end). */
	float BreakLeft = 0.f, BreakT = 0.f;
	bool bTwoMinShown[8] = {};
	bool bSetAfterBreak = false;
	/** -TagPadPlayTest (Game/MatchPadPlay.cpp): the in-game pad controls pressed into the controller and checked. */
	bool bPadPlayTest = false;
	// (SnapSerial: the play the snap press was for - a new play set up, a press of its own; FlagStage / FlagT: the check
	// that a pre-snap flag drawn by that press still leaves the next snap to be made)
	struct FPadPlay { int32 Step = 0, Pass = 0, Fail = 0, CamWas = 0, FlagStage = 0; float Clock = 0.f, HoldT = 0.f, Wait = 0.f, ReleaseCross = 0.f, FlagT = 0.f; bool bSnapSent = false; unsigned long long SnapSerial = 0; } PadPlay;
	void TickPadPlayTest(float Dt);
	void PadKey(const FKey& Key, bool bDown); // the next snap's men are put on their spots (the teams changed ends during the break)
	void BeginBreak(const FString& Title, const FString& Sub, float Secs);
	bool BreakCamera(FVector& T, FVector& Look) const;
	/** The user's tempo after the whistle (Madden's): CROSS held - hurry up (no huddle, no replay, the last play called
	 *  again at the line); TRIANGLE held - spike it. Read while the men come back from the play (Post / Wait). */
	const tag::OffPlay* LastUserPlay = nullptr;
	bool bHurryNext = false, bSpikeNext = false;
	float HurryHoldT = 0.f, SpikeHoldT = 0.f;
	void ReadTempoInput(float Dt);
	FVector CamPos = FVector(4572, 6000, 2500);
	FVector CamLook = FVector(4572, 0, 0);
	FVector IdleFocus = FVector(4572, 0, 0);
	float Shake = 0.f;
	// broadcast camera operator (UpdateCamera): critically damped moves, lens zoom to the action, handheld drift
	FVector CamVel = FVector::ZeroVector;
	FVector LookVel = FVector::ZeroVector;
	float CamFocal = 30.f;
	float CamFocalVel = 0.f;
	/** The TV game camera (ECamMode::Sideline): which fixed position it shoots from (yards from the left goal line; the
	 *  50 and both 20s), picked while the ball is dead. < 0: not picked yet. */
	float CamStationX = -1.f;
	float CamTime = 0.f;
	/** Post-play cut-in: a telephoto close shot of the player who made the play (TD, takeaway, sack, big gain). */
	UPROPERTY()
	TObjectPtr<AActor> CutInTarget;
	float CutInLeft = 0.f;
	void StartCutIn(const tag::Ent* Who, float Seconds);
	/** The cut-in's camera: placed once when the shot starts (a long lens on a tripod: it pans after the man, it does not
	 *  hop round him as he rolls over), a second camera cut to only when the first is blocked for a beat; a cut back
	 *  to the game camera at the end (not a swoop). */
	bool bCutInPlaced = false, bCutNext = false;

	// ---- the broadcast (Game/MatchBroadcast.cpp): camera cuts, replay choice, field overlays, the stands
	/** A hard cut of the view this frame (a replay in or out, a station change, a cut-in, a break's wide shot): the
	 *  renderer is told (APlayerCameraManager::SetGameCameraCutThisFrame), so TSR and motion blur do not carry the last
	 *  shot's history across it. CommitCameraFrame also catches a jump of the view nobody marked. */
	void MarkCameraCut(const TCHAR* Why);
	/** Once a frame, after every actor has ticked (OnPostActorTick): the cut signal, and the camera's numbers for the
	 *  -TagHlFull telemetry (CameraTelemetryCols). */
	void CommitCameraFrame(float Dt);
	/** The camera columns of a -TagHlFull telemetry row (",camx,...": the frame the last screenshot showed). */
	FString CameraTelemetryCols();
	bool bCamCutPending = false;
	const TCHAR* CamCutWhy = nullptr;
	struct FCamFrame
	{
		bool bValid = false, bCut = false;
		FVector Pos = FVector::ZeroVector, Fwd = FVector::ForwardVector;
		float Focal = 0.f, AngSpeed = 0.f; // (deg/s, the view direction's swing since the last frame)
		int32 Cuts = 0;
	} CamFrame;
	/** Replay-worthiness (replaces the yes/no test): every play scored from what the sim did (yards over the down's
	 *  expectation, a sack, a third / fourth down converted or stopped, the red zone, the sim's big hit, broken tackles,
	 *  a layout or a long-odds catch, late and close), against a budget of about one replay per five scrimmage plays.
	 *  Touchdowns, takeaways, safeties and blocked kicks always replay; two routine sacks in a row do not. Read from the
	 *  sim's result and the game only (nothing random, nothing measured on the drawn bodies): the same game replays the
	 *  same plays. -TagReplayOld: the old test (A/B); -TagReplayAll: every play. Sets bReplayWorthy. */
	void DecideReplay(const tag::PlayResult& R);
	/** A replay chosen by DecideReplay has started (StartReplay): it is paid for out of the budget. */
	void SpendReplay();
	void ResetReplayBudget();
	void LogReplaySummary() const;
	float ReplayCredit = 1.f, ReplayScoreNow = 0.f;
	bool bReplayForced = false;
	FString ReplayWhyNow, LastReplayWhy;
	int32 ReplayPlays = 0, ReplaysShown = 0, ReplayGap = 0, ReplayGapMax = 0;
	TMap<FString, int32> ReplayWhyCount;
	/** The field's graphics to broadcast convention, each frame: the line to gain red on fourth down, both lines on a
	 *  glowing (emissive) look that reads the same by day and night, the down and distance on the turf beside the line
	 *  before the snap, and nothing of it in a replay, a cut-in or a break's wide shot. -TagLitLines: the old lit lines. */
	void TickFieldOverlays(float Dt);
	TWeakObjectPtr<AStadium> OverlayStadium;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> LosLineMID;
	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> FdLineMID;
	UPROPERTY()
	TObjectPtr<class UTextRenderComponent> DownMark;
	int32 OverlayDown = 0;          // the down the lines were styled for (4: the line to gain is red)
	bool bOverlayHidden = false;
	FString DownMarkText;
	/** The stands move with the home crowd's mood (ATagAudio::CrowdEnergy: its roar, not every touchdown): a visitors'
	 *  score leaves them seated. Without the sound (-TagNoAudio) HomeCheer stands in, for the home side only. */
	void TickStands();
	void HomeCheer(float Level, tag::TeamKey For);
	bool bStandsUp = false;
	/** Once a frame from Tick (after the sound): the overlays and the stands. */
	void TickBroadcast(float Dt);
	/** Pre-snap: the user is walking his defender (the blocking preview is redone when he settles). */
	bool bPreMoved = false;
	float SprintStamina = 1.f; // the user's sprint (R2) stamina, 0..1, drawn under his man
	bool bSprinting = false, bSprintSpent = false;
	bool bPresnapPanelHidden = false; // R3 / F1 hides the pre-play panel
	float PrevMotionX = 0.f;   // (pre-snap motion: the left stick's last x, for a flick)
	/** Pre-snap on defense: the switch button held, the left stick picks the man that way (one a push); a tap steps
	 *  through the defense on its release. */
	bool bSwitchHeld = false, bSwitchPicked = false, bSwitchStickArmed = true;
	float SwitchHeldT = 0.f;
	/** Pre-snap on offense: the art's trigger held and the left stick pushed left / right swings the camera along the
	 *  line to see the edges of the field (-1 .. 1, the screen's left / right; PreLook is what the camera has reached). */
	float PreLookWant = 0.f, PreLook = 0.f;
	/** The officials' routes between plays: every man on the field as an obstacle (where he is, how he moves, where he is
	 *  walking to), gathered once a frame; OfficialSteer picks the heading that gets an official to his spot around
	 *  them - behind the formation, outside the huddle - instead of through them. */
	struct FOffBody { FVector P = FVector::ZeroVector, V = FVector::ZeroVector, Dest = FVector::ZeroVector; bool bDest = false; };
	TArray<FOffBody> OffBodies;
	TArray<float> CrewSteerSide;
	float UmpSteerSide = 0.f;
	void GatherOfficialBodies();
	FVector OfficialSteer(const FVector& Pos, const FVector& Target, float Pace, float& Turn, bool bCoreOk, float* OutCrowd = nullptr) const;
	/** R1 at the line: the quarterback's fake snap (a hard count), from the pack's captures. */
	void PlayFakeSnap();
	/** A pad throw (Madden): the receiver's button tapped is a touch pass, held a bullet (thrown on release, or at 0.3 s). */
	int32 PadThrowIdx = -1;
	double PadThrowT0 = 0.0;
	int32 PadTapIdx = -1;     // a receiver's button tapped: a lob, unless a second tap (a pump fake) comes in 0.22 s
	double PadTapT = 0.0;
	tag::V2 PadMovedFrom{0, 0}; // (pad test: where the walked defender started)
	FVector CutCamPos = FVector::ZeroVector, CutCamVel = FVector::ZeroVector, CutLook = FVector::ZeroVector, CutLookVel = FVector::ZeroVector;
	float CutBlockedT = 0.f;
	int32 CutRecuts = 0;
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
	void StartPairedTackle(AAthlete* Tk, AAthlete* Cr, const FVector& SimSpot, int32 Pair, bool bDive, float PileSecs = 0.f, bool bBigHit = false,
		int32 Strip = 0);
	/** Strip = 1 / 2 (sack): the tackle the sim fumbles at - one of the pack's strip captures, picked like a pair. The
	 *  capture's punch comes LastStripOut s after the contact, out of the arm bLastStripRight (-1: no strip capture fit). */
	float LastStripOut = -1.f;
	bool bLastStripRight = true;
	/** A fumble this play: the ball drawn in the carrier's arm until the capture's punch, then eased onto the sim's loose
	 *  ball (the rigid body, BallPhysics.h) over 0.25 s. */
	unsigned long long FumbleSerial = 0;
	double FumbleWT = 0.0;
	float FumbleOutIn = 0.f;
	TWeakObjectPtr<AAthlete> FumbleCarrier;
	FVector FumbleHandW = FVector::ZeroVector;
	/** The pass's outcome, rolled before it arrived (PlaySim::preRollCatch), started on film this play: a two-man
	 *  interception, the named defender's breakup, the receiver's drop. */
	unsigned long long FatePrimedSerial = 0;
	bool bIntPaired = false;
	/** A takeaway's celebrations started after the whistle this play. */
	unsigned long long TurnoverCelebSerial = 0;
	tag::Ent* TurnoverCelebMan = nullptr; // (he celebrates once he is up off the turf)
	float TurnoverWaitT = 0.f;            // (how long the shot has waited for him)
	/** The last paired tackle (a pile started at the sim's wrap is retargeted when the sim ends it). */
	TWeakObjectPtr<AAthlete> PileCarrier, PileTackler;
	FVector PileSpot = FVector::ZeroVector;
	/** A gang tackle from the pack's three-man captures (pack_gangs.json): Tk and As on Cr at once. False when none
	 *  fits where the two are (the caller pairs Tk and Cr and As piles on). */
	bool StartGangTackle(AAthlete* Tk, AAthlete* Cr, AAthlete* As, const FVector& SimSpot);
	TWeakObjectPtr<AAthlete> GangAssist;
	/** A tackle landed a beat after the catch waits until the catch has the ball secured (the catch plays, then the
	 *  hit): the fall took over on the catch's own frame and the ball just turned up in his hands as he went down. */
	struct FHeldTackle
	{
		TWeakObjectPtr<AAthlete> Tk, Cr, As;
		FVector Spot = FVector::ZeroVector;
		bool bDive = false, bBig = false, bWrap = false;
		int32 Strip = 0;
		float PileSecs = 0.f, Left = 0.f;
	};
	TArray<FHeldTackle> HeldTackles;
	const tag::Ent* LastCatcher = nullptr;
	double LastCatchT = -10.0;
	void FireHeldTackle(const FHeldTackle& H);
	TWeakObjectPtr<AAthlete> LastPileCarrier;
	FVector LastPileDir = FVector::ForwardVector;
	double LastPileT = -10.0;
	FVector LastPileSpot = FVector::ZeroVector;
	/** A second man arriving on the pile. */
	void StartPileOn(AAthlete* As, AAthlete* Cr);
	/** Bodies meeting (any two men whose torsos come within reach closing at speed): each takes his share of the
	 *  collision's velocity change by mass (restitution 0.15), handed to his body layer's springs (AAthlete::AddImpact).
	 *  A pair is struck once, then not again for 0.6 s. Logged as "TAG contact". */
	void DetectContacts();
	TMap<uint64, double> ContactUntil;
	int32 ContactsThisPlay = 0;
	float MaxJoltThisPlay = 0.f;
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
		float Lead = 0.9f;    // seconds from the start to the foot meeting the ball
		float SceneT0 = -1.f; // a field goal played as the pack's three-man capture: when its clips start (< 0: staged)
		bool bSceneOn = false; bool bDynamicKo = false; bool bPlaneLogged = false;
		bool bFairOn = false; // the returner is on the fair catch capture
		/** A blocked kick (the sim's call): one rusher gets through and lays out across the kick (FgBlockRush) - on a
		 *  field goal his capture from the snap, aimed so he leaves his feet at the ball; on a punt a run in, then
		 *  the layout. bBlockOn: his capture is playing. */
		bool bBlocked = false, bBlockOn = false;
		double Los = 0;       // the line of scrimmage (a punt played as its capture: the punt point comes from the capture)
		/** The ball's flight, solved once: (yards past FromX, height in yards) every 1/60 s from the kick, under gravity
		 *  and air drag (a kicked ball climbs fast and drops steeply; the old symmetric arc read as a lob). */
		std::vector<FVector2D> Path;
		/** A user kick being aimed: the team set and held (the clock stops at HoldT, short of the snap) until the swing. */
		bool bAwait = false;
		float HoldT = 0.f;
		/** The units in the played place kick's order (Game::orderPlaceUnits), set where its play will line them up
		 *  (PlaySim::placeAlign, the block unit in its rush look): a user's field goal aimed before its play starts. */
		bool bPlaceOrder = false;
		int32 PlaceLook = 0;
		/** The ball down and not caught (a touchback, a punt let go): it bounces on as a football does, high and off line,
		 *  and rolls to a stop (offense frame yd, yd/s). It stopped dead where it landed. */
		bool bBounce = false;
		FVector BounceP = FVector::ZeroVector, BounceV = FVector::ZeroVector;
		int32 Hops = 0;
	};
	std::unique_ptr<FKick> Kick;
	/** A user kick, Madden's way (Game/MatchKicking.cpp): the kick team staged and held, the camera over the kicker's
	 *  shoulder; the left stick aims (the arrow on the field), the swing sets the power and the accuracy - the right
	 *  stick pulled back and pushed up straight (pad), or SPACE / CROSS held for the power, released, pressed again on
	 *  the centre. Fire then plays the kick for real (the sim reads Game::userKick). -TagUserKicks swings every kick
	 *  automatically (automation). */
	struct FUserKick
	{
		bool bOn = false;
		int Kind = 0;           // 0 kickoff, 1 punt, 2 field goal / extra point
		int Stage = 0;          // 0 aim, 1 power, 2 accuracy, 3 away
		bool bPadSwing = false;
		float AimDeg = 0.f, Power = 0.f, Acc = 0.f, MeterT = 0.f, NeutralT = 0.f, AutoT = 0.f;
		float AutoPower = 1.f, AutoAcc = 0.f, AutoAim = 0.f;
		float BackSign = -1.f;  // which way this pad reads the stick pulled back
		bool bLeanShown = false; // the stick on its way up: the needle shows its lean
		double SpotX = 0, Los = 0;
		int Dir = 1;
		bool bPat = false;
		std::function<void()> Fire;
	};
	FUserKick UK;
	std::function<void()> PendingKickFire;
	bool UserKicks(tag::TeamKey Kicking) const;
	void BeginUserKick(int Kind, int D, const std::vector<tag::Player*>& KickU, const std::vector<tag::Player*>& RecvU, double Los, bool bPat, std::function<void()> Fire);
	void TickUserKick(float Dt);
	void FirePendingKick();
	bool KickAimCamera(FVector& OutPos, FVector& OutLook) const;
	// a live punt return: the punter and the snapper on the pack's punt capture until the ball is away
	unsigned long long PuntSceneSim = 0; // (the play's serial: a new sim reused the old one's address and the punt scene never reset)
	const tag::Ent* PuntSnapEnt = nullptr;
	int32 PuntStage[2] = {0, 0}; // punter, snapper: 0 not started, 1 waiting, 2 on the capture

	// the broadcast booth (Booth/TagBooth.h): two voices calling the game; fed every frame, it may hold the next
	// play a moment (the open, a break; in a watched game the breakdown of the last play)
	UPROPERTY()
	TObjectPtr<ATagBooth> Booth;
	bool bBoothNight = false;
	void FeedBooth(float Dt);
	/** The game's sound (Audio/TagAudio): the crowd, the field, the stadium, the music. It watches the game like the booth. */
	UPROPERTY()
	TObjectPtr<ATagAudio> Audio;
	void FeedAudio(float Dt);
	bool BoothHold() const;
	/** A played game: a confirm press while the booth holds the kickoff (the open, a break) lets the game go on; the
	 *  booth's hold is passed over until this time (platform seconds; the booth itself is not told and talks on). */
	double BoothSkipUntil = 0.0;

	// ---- local multiplayer (Game/MatchMulti.cpp)
	bool bMultiNext = false;     // (StartMatch: the game being started is a multiplayer one)
	void TickLobby();
	void LobbyJoin(int32 Dev);
	/** After a play's sim is made (or remade at the line): each player on a side gets his man - slot 0 the play's first
	 *  player (the offense's caller on the quarterback, else the defense's), the rest a man of their own. */
	void SetupHumans();
	/** Each player's man as he stands now (he gets him back on the next play). */
	void SyncHumanLast();
	/** One player's man, input and control state in and out of slot 0 / the director's members. */
	void SwapHuman(FTagHuman& H);
	/** The live controls of the player whose reader is CurIn (the man in slot 0 and the director's control members). */
	void ReadUserInputSlot();
	FTagReader CurIn;
	/** The reader of the player in slot 0 (one player: the controller). */
	FTagReader PrimaryReader() const;
	/** Before the snap, the players other than slot 0's: a defender walks his man and switches; an offensive player
	 *  steps through the eligible men (L1 / R1, TAB). */
	void TickMatePresnap(float Dt);
	/** Head to head, the defense's caller (not in slot 0: the quarterback is) has his own pre-snap menus: their state is
	 *  swapped with PreMode / AudibleSel / bPresnapPanelHidden while his menus run, and his panel is the left one. */
	EPre DefPreMode = EPre::Base;
	int32 DefAudibleSel = 0;
	bool bDefPanelHidden = false;
	void SwapDefMenus();
	/** -1: no defense caller apart from slot 0 (one player, co-op against the CPU, or the defense calling in slot 0). */
	int32 DefMenuHuman() const;
	/** The defense's pre-snap walk and switch (the left stick walks him, the switch button held picks by the stick). */
	void PresnapWalkSwitch(const FTagReader& PC, float Dt, bool bCanSwitch, bool bSwitchPress);
	/** A menu that one player works (a play call, the try): only his pad / keys reach it. -1: everyone's. */
	void SetMenuOwner(int32 Human);
	int32 MenuOwner = -1;
	TArray<int32> HumanDevices(int32 H) const;
	/** Every player's marker (multiplayer): ring, chevron, his tag, his sprint. */
	void UpdateHumanMarkers(class APlayerController* PC, const FVector2D& VP);
	/** -TagMultiTest: a scripted 2v2 on fake devices (joins, sides, callers, both calls, both defenses' switches, a
	 *  player on a receiver), checked and logged PASS / FAIL. */
	bool bMultiTest = false;
	int32 MultiStep = 0, MultiFails = 0, MultiPasses = 0;
	float MultiT = 0.f;
	int32 MultiOwnerDev = -1, MultiOffDev = -1, MultiDefDev = -1, MultiPlays = 0;
	bool bMultiCallOff = false;
	const tag::Ent* MultiPrevOff = nullptr;
	const tag::Ent* MultiPrevDef = nullptr;
	tag::V2 MultiWalkFrom{0, 0};
	int32 MultiDefCallDev = -1;
	const tag::DefCall* MultiPrevCall = nullptr;
	void TickMultiTest(float Dt);
	void MultiCheck(bool bOk, const FString& What);

	// ---- the feel of the controls (Game/MatchFeel.cpp)
	/** Each user slot's moves pressed a beat early (Game/TagMoveBuffer.h): taken out of the input after it is read, handed
	 *  to the sim before each step until it takes them (150 ms), and what became of them after the step. */
	FTagMoveBuf MoveBufs[4];
	void TakeMovePresses();
	void OfferBufferedMoves();
	void SettleBufferedMoves();
	/** The grey "too early" pill for slot Slot's move that ran out unused: 0..1 as it fades (What: the move). */
	float MoveRefusedA(int32 Slot, FString* What = nullptr) const;
	void MoveRefusedCue(int32 Slot, FUserMarkerUI& M) const;
	/** The pad's rumble (Game/TagRumble.h) for the events of the players' men; held while paused or in a replay. */
	void TickRumble(float Dt);
	unsigned long long RumbleSerial = 0;
	size_t RumbleSeen = 0;
	bool bRumbleTd = false, bRumbleKick = false;
	int32 RumbleWarned = -1;
	/** The pass started on the press (06 item 3): the windup begins as the receiver's button goes down, the pass type
	 *  (bullet / lob / high / low) and a pump fake are settled during it. bOldTiming (-TagThrowOnRelease): the old code
	 *  throws; this only counts the press to the windup. The human's quarterback only. */
	void FeelThrow(const FTagReader& PC, int32 ModStyle, bool bOldTiming);
	/** The switch (06 item 5), every frame of a live play: the press picks by where the ball is going (in the air: who
	 *  gets to the catch point first; a run: who gets to the carrier first; else the sim's nearest-the-ball), the button
	 *  held with the stick pushed picks the man that way, and the stick as it was does not steer the new man for 0.25 s
	 *  (Switch assist). -TagSwitchOld: the sim's switch, as before. */
	void FeelSwitch(bool bPress, bool bKeyHeld);
	/** The man the switch picks for slot 0 (nullptr: leave it to the sim's nearest-the-ball). */
	tag::Ent* SmartSwitchPick(tag::Ent* From, FTagSwitchNote& Note) const;
	/** How long E takes to get to Spot (s): his top speed, a turn if he runs the other way, getting going from a stand. */
	double ArriveSecs(const tag::Ent* E, const tag::V2& Spot) const;
	FTagFeelCtl FeelCtl;
	FTagSwitchNote LastSwitch;
	FTagThrowNote LastThrow;
	/** Responsive movement (Game/TagFeel.h): before a sim step each player's man is given his steering (the sim steers
	 *  him by it on the steps his stick drives him: PlaySim::setUserSteer); after it, the steps it changed are counted
	 *  (every man's Ent::steerBoosted over the step: a player's man, or a man the CPU had - which must never be). */
	void FeelBeforeStep();
	void FeelAfterStep();
	const tag::Ent* PreStepEnt[4] = {nullptr, nullptr, nullptr, nullptr};
	TArray<int32> PreStepBoosted;
	int32 FeelSteps = 0;              // sim steps the director has taken (the press-to-windup count)
	int32 ResponsiveSteps = 0, ResponsiveCpu = 0; // steps a player's man was steered Responsive; men the CPU had (must stay 0)
	/** The hit-stop and the camera's shake (06 item 10) and the sound of the bodies meeting (05 A8), from the contacts
	 *  DetectContacts found this frame and the sim's events. */
	void OnContactsFound();
	void StartHitStop(int32 Steps, float Trauma, const FString& Why);
	void TickHitStop();
	/** The frame's real seconds (Dt is held near 0 through a hit-stop: the rumble and the shake run on real time). */
	float FeelRealDt(float Dt) const;
	/** A shake of the camera: Old (cm, -TagOldShake's random jitter) or Trauma (0..1; bSway a touchdown's slow sway). */
	void ShakeCam(float Old, float Trauma, bool bSway);
	/** The camera's shake this frame: its position offset (returned) and rotation (OutRot). */
	FVector CameraShake(float Dt, FRotator& OutRot);
	FTagHitStop HitStop;
	FTagTrauma CamTrauma;
	/** The contact sounds: when the last ones played (platform s, the cap's window), this play's counts. */
	TArray<double> ContactSoundT;
	unsigned long long ContactSoundSerial = 0;
	int32 CsContacts = 0, CsSounds = 0, CsTackle = 0, CsFar = 0, CsAfterHit = 0, CsCap = 0, CsLate = 0;
	int32 CsAllContacts = 0, CsAllSounds = 0, CsAllLate = 0, CsMostInWindow = 0; // (since the start: the self-check's)
	uint64 ContactsHeardSerial = 0; // the last contact the sound has taken (FTagContact::Serial): each one once
	/** The windup's throw clip (SyncPlay): its key, whether it came from the standing choice (the pass type picks it there)
	 *  and the pass type it was picked for; a pass settled in the windup (a tap's lob) switches it (AAthlete::RetimeThrow). */
	int32 ThrowTimedKey = -1;
	uint64 QbStyleOpenFrame = 0; // (the frame FeelThrow last saw the player's pass type still open in its windup)
	bool bThrowTimedStand = false, bThrowTimedTouch = false;
	int32 ThrowRetimes = 0;
	unsigned long long ThrowRetimeSerial = 0;
	double ThrowRetimeAt = -1.0;  // platform s of the last switch
	int32 ThrowRetimeXfjN = 0;    // the quarterback's clip-switch measures before it (AAthlete::ClipXfjN)
	/** -TagLatencyLog (Game/TagLatency.h): the gameplay presses PC read this frame. */
	void FeelNotePresses(const FTagReader& PC);
	/** -TagFeelTest (Game/MatchFeelTest.cpp): a user game that presses the throws, the switches and the stick, checks
	 *  each, the steering law, the shake, the contact events and sounds, the hit-stop and the latency log. */
	bool bFeelTest = false;
	void TickFeelTest(float Dt);
	/** The play the feel test wants next (CallPlays): the user's side and the offense's play. */
	bool FeelTestCall(const tag::OffPlay*& Op, const tag::DefCall*& Dc);
	/** -TagHitStopTest (MatchFeelTest.cpp): a CPU game with a hit-stop on every big hit, its play log's digest saved and
	 *  compared with the other run's (-TagNoHitStop): the same seed must give the same plays. */
	bool bHitStopTest = false;
	void TickHitStopTest();

	friend class SGridironOverlay;
};
