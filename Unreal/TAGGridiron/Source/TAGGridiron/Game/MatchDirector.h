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
enum class EMatchState : uint8 { Menu, Call, Presnap, Live, Post, Kick, Wait, Final, Paused };

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

	TSharedPtr<SGridironOverlay> Overlay;
	std::map<tag::Ent*, FVector2D> Prev;
	std::map<tag::Ent*, FString> Labels;
	ECamMode CamMode = ECamMode::Broadcast;
	ECamMode LastCamMode = ECamMode::Broadcast;
	FVector CamPos = FVector(4572, 6000, 2500);
	FVector CamLook = FVector(4572, 0, 0);
	FVector IdleFocus = FVector(4572, 0, 0);
	float Shake = 0.f;
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
	int32 EventShotFrames = 0;
	FString EventShotName;
	void TickAutomation(float Dt);

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
