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
	std::map<tag::Ent*, float> PostYaw;  // where each man faces once he has arrived (the huddle looks in)
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
	/** Pair a tackle: Tk tackles Cr, the sim put the carrier down at SimSpot (world). Pair -1 picks from the hit
	 *  (angle, yards after contact); both men fall together, anchored in the pair's authored relation. */
	void StartPairedTackle(AAthlete* Tk, AAthlete* Cr, const FVector& SimSpot, int32 Pair, bool bDive);
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
	};
	std::unique_ptr<FKick> Kick;

	friend class SGridironOverlay;
};
