#include "Game/MatchDirector.h"

#include "Render/Athlete.h"
#include "Render/Stadium.h"
#include "Render/Coords.h"
#include "UI/GridironOverlay.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Sim/Playbook.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "AssetCompilingManager.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

using namespace TagCoords;

namespace
{
	constexpr float STEP = 1.f / 60.f;
	FString S(const std::string& s) { return FString(s.c_str()); }
	const FLinearColor Good(0.24f, 0.86f, 0.52f);
	const FLinearColor Bad(1.f, 0.35f, 0.35f);
	const FLinearColor Flag(1.f, 0.69f, 0.f);
}

AMatchDirector::AMatchDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMatchDirector::BeginPlay()
{
	Super::BeginPlay();
	League = tag::createLeague(2027);
	FAthleteAssets::Get().Load();

	Camera = GetWorld()->SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), FTransform(FRotator(-20, -90, 0), CamPos));
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->Filmback.SensorWidth = 36.f;
		C->Filmback.SensorHeight = 20.25f;
		C->CurrentFocalLength = 32.f;
		C->CurrentAperture = 5.6f;
		C->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
	}
	Ball = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity);
	Ball->SetMobility(EComponentMobility::Movable);
	Ball->GetStaticMeshComponent()->SetStaticMesh(FAthleteAssets::Get().Sphere);
	Ball->GetStaticMeshComponent()->SetMaterial(0, TagColorMat(this, FLinearColor(0.2f, 0.07f, 0.02f), 0.55f));
	Ball->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetActorScale3D(FVector(0.28f, 0.17f, 0.17f));

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetViewTarget(Camera);
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->bShowMouseCursor = true;
	}
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(SAssignNew(Overlay, SGridironOverlay).Director(this), 10);
	}
	tag::Weather W;
	W.tempF = 60;
	BuildWorld(*League[0], *League[1], W, true);
	ShowMainMenu();
	if (FParse::Param(FCommandLine::Get(), TEXT("TagAutoWatch")))
	{
		bAuto = true;
		int32 Wx = 0;
		FParse::Value(FCommandLine::Get(), TEXT("TagWeather="), Wx);
		const bool bDay = FParse::Param(FCommandLine::Get(), TEXT("TagDay"));
		StartMatch(3, 7, false, true, Wx, !bDay, 900);
	}
}

void AMatchDirector::TickAutomation(float Dt)
{
	if (!bAuto) return;
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return; // wait for shaders
#endif
	if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0) return; // and textures/meshes
	AutoT += Dt;
	static const float Times[] = {3.f, 9.f, 14.f, 20.f, 27.f, 34.f, 42.f, 50.f};
	if (AutoShot < static_cast<int32>(UE_ARRAY_COUNT(Times)) && AutoT >= Times[AutoShot])
	{
		if (AutoShot == 4) CamMode = ECamMode::Sideline;
		if (AutoShot == 6) CamMode = ECamMode::Skycam;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_auto_%02d.png"), AutoShot), false, false);
		int32 NLog = 0;
		for (auto& KV : Athletes)
		{
			if (!KV.Value->IsHidden() && NLog++ < 3) UE_LOG(LogTemp, Display, TEXT("TAG athlete %d: %s"), KV.Key, *KV.Value->DebugAnimState());
		}
		UE_LOG(LogTemp, Display, TEXT("TAG auto screenshot %d state=%d"), AutoShot, static_cast<int32>(State));
		++AutoShot;
	}
	// final shot: establishing view of the far stands (verifies crowd/stadium rendering)
	if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) && AutoT > 52.f && AutoT < 54.f)
	{
		bAutoEstablish = true;
	}
	if (bAutoEstablish && AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) && AutoT >= 53.5f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_stands.png"), false, false);
		++AutoShot;
	}
	// close-up of one athlete (verifies character, uniform and equipment fit)
	if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 1 && AutoT > 55.f)
	{
		bAutoEstablish = false;
		for (auto& KV : Athletes)
		{
			if (!KV.Value->IsHidden())
			{
				CloseupTarget = KV.Value;
				break;
			}
		}
		if (AutoT >= 57.5f)
		{
			if (const AAthlete* CA = Cast<AAthlete>(CloseupTarget)) UE_LOG(LogTemp, Display, TEXT("TAG closeup athlete: %s"), *CA->DebugAnimState());
			FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_closeup.png"), false, false);
			++AutoShot;
		}
	}
	else if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 2 && AutoT >= 59.f)
	{
		CloseupView = 1; // full body
		if (AutoT >= 60.5f)
		{
			FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_closeup_full.png"), false, false);
			++AutoShot;
		}
	}
	else if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 3 && AutoT >= 61.f)
	{
		CloseupView = 2; // from behind
		if (AutoT >= 62.5f)
		{
			FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_closeup_back.png"), false, false);
			++AutoShot;
		}
	}
	if (AutoT > 63.5f) FPlatformMisc::RequestExit(false);
}

void AMatchDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GEngine && GEngine->GameViewport && Overlay.IsValid()) GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	Super::EndPlay(Reason);
}

void AMatchDirector::BuildWorld(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight)
{
	if (Stadium) Stadium->Destroy();
	for (auto& KV : Athletes)
		if (KV.Value) KV.Value->Destroy();
	Athletes.Empty();
	Stadium = GetWorld()->SpawnActor<AStadium>(AStadium::StaticClass(), FTransform::Identity);
	Stadium->Build(Home, Away, W, bNight);
}

void AMatchDirector::ShowMainMenu()
{
	State = EMatchState::Menu;
	Sim.reset();
	Game.reset();
	ShowOnly({});
	if (Overlay) Overlay->ShowMainMenu();
}

void AMatchDirector::ShowTeamSelect(bool bUserPlays)
{
	if (Overlay) Overlay->ShowTeamSelect(bUserPlays);
}

void AMatchDirector::QuitToMenu()
{
	ShowMainMenu();
}

void AMatchDirector::StartMatch(int HomeIdx, int AwayIdx, bool bUserPlays, bool bUserHome, int WeatherKind, bool bNight, double QuarterLen)
{
	tag::Rng R(static_cast<uint32_t>(FMath::Rand()) * 2654435761u + 17u);
	tag::Weather W = tag::randomWeather(R);
	switch (WeatherKind)
	{
	case 1: W.precip = tag::Precip::None; W.windX *= 0.4; W.windY *= 0.4; W.tempF = FMath::Max(50.0, W.tempF); break;
	case 2: { W.precip = tag::Precip::None; const double A = R.range(0, 6.283); W.windX = FMath::Cos(A) * 9; W.windY = FMath::Sin(A) * 9; break; }
	case 3: W.precip = tag::Precip::Rain; W.tempF = FMath::Clamp(W.tempF, 40.0, 65.0); break;
	case 4: W.precip = tag::Precip::Snow; W.tempF = R.range(18, 31); break;
	default: break;
	}
	tag::Team& Home = *League[HomeIdx];
	tag::Team& Away = *League[AwayIdx];
	for (tag::Team* T : {&Home, &Away})
		for (auto& P : T->roster) P->injuredWeeks = 0;
	tag::GameOptions O;
	O.seed = static_cast<uint32_t>(R.next() * 4294967295.0);
	O.quarterLen = QuarterLen;
	O.weather = W;
	if (bUserPlays) O.userTeam = bUserHome ? tag::TeamKey::Home : tag::TeamKey::Away;
	Game = std::make_unique<tag::Game>(Home, Away, O);
	UserTeam = O.userTeam;
	bWatch = !bUserPlays;
	Speed = 1.f;
	LastQ = 1;
	BuildWorld(Home, Away, W, bNight);
	if (Overlay)
	{
		Overlay->ClearModal();
		Overlay->SetHudVisible(true);
	}
	CamMode = ECamMode::Broadcast;
	Next();
}

bool AMatchDirector::UserOff() const { return UserTeam.has_value() && Game && *UserTeam == Game->poss; }
bool AMatchDirector::UserDef() const { return UserTeam.has_value() && Game && *UserTeam == tag::other(Game->poss); }

// ------------------------------------------------------------------ flow
void AMatchDirector::Next()
{
	tag::Game& G = *Game;
	Labels.clear();
	if (Overlay) Overlay->SetFlag(false);
	if (G.phase == tag::GamePhase::Final) return ShowFinal();
	if (G.q == 3 && LastQ == 2)
	{
		LastQ = 3;
		State = EMatchState::Paused;
		Overlay->ShowBoxScore(TEXT("Halftime"), TEXT("Start 2nd Half"), [this] { State = EMatchState::Wait; Timer = 0; });
		return;
	}
	LastQ = G.q;
	if (G.phase == tag::GamePhase::Kickoff)
	{
		if (G.clock <= 0)
		{
			G.simStep();
			return Next();
		}
		const tag::TeamKey KT = G.kicking;
		const int D = G.worldDir(KT);
		auto KickU = Pick11(G.teamOf(KT), "K");
		auto RecvU = Pick11(G.teamOf(tag::other(KT)), "KR");
		const tag::SpecialResult R = G.kickoff();
		return StartKick(R, D, 0, KickU, RecvU, 35);
	}
	if (G.phase == tag::GamePhase::Pat)
	{
		auto Decide = [this](bool bTwo)
		{
			tag::Game& GG = *Game;
			if (!bTwo)
			{
				const int D = GG.worldDir();
				auto KickU = Pick11(GG.offense(), "K");
				auto DefU = Pick11(GG.defense(), "D");
				const tag::SpecialResult R = GG.extraPoint();
				return StartKick(R, D, 2, KickU, DefU, 85);
			}
			bTwoPoint = true;
			CallPlays(true);
		};
		if (UserOff())
		{
			State = EMatchState::Call;
			Overlay->ShowPatChoice(Decide);
			return;
		}
		return Decide(G.goForTwo());
	}
	const int QBefore = G.q;
	if (!G.preSnap())
	{
		if (G.q != QBefore && G.phase != tag::GamePhase::Final) Overlay->Banner(G.q == 5 ? TEXT("OVERTIME") : FString::Printf(TEXT("END OF Q%d"), QBefore), TEXT(""), FLinearColor::White);
		return Wait(1.2f);
	}
	if (!UserOff())
	{
		if (G.down == 4)
		{
			const tag::FourthDownCall C = G.fourthDownCall();
			if (C == tag::FourthDownCall::Punt) return DoPunt();
			if (C == tag::FourthDownCall::Fg) return DoFG();
		}
		else if ((G.q == 2 || G.q >= 4) && G.clock <= 6 && 100 - G.ballOn + 17 <= 56 && G.margin() >= -3)
			return DoFG();
	}
	bTwoPoint = false;
	CallPlays(false);
}

void AMatchDirector::CallPlays(bool bTwo)
{
	tag::Game& G = *Game;
	State = EMatchState::Call;
	Sim.reset();
	ShowOnly({});
	Dir = G.worldDir();
	IdleFocus = ToWorld(bTwo ? 98 : G.ballOn, G.ballY, Dir);
	Stadium->SetLines(ToWorld(bTwo ? 98 : G.ballOn, 0, Dir).X, bTwo ? -1 : ToWorld(FMath::Min(100.0, G.ballOn + G.toGo), 0, Dir).X);
	auto Finish = [this](const tag::OffPlay* Op, const tag::DefCall* Dc) { StartPresnap(Op, Dc); };
	if (UserOff())
	{
		Overlay->ShowPlayCall(true, G.down == 4 && !bTwo, bTwo, [this, bTwo, Finish](FCallChoice C)
		{
			tag::Game& GG = *Game;
			if (C.Kind == FCallChoice::Punt) return DoPunt();
			if (C.Kind == FCallChoice::FG) return DoFG();
			const tag::OffPlay* Op = C.Kind == FCallChoice::Off && C.Play ? C.Play : bTwo ? GG.cpuTwoPointPlay() : GG.cpuOffense();
			Finish(Op, GG.cpuDefense(*Op));
		});
	}
	else
	{
		const tag::OffPlay* Op = bTwo ? G.cpuTwoPointPlay() : G.cpuOffense();
		if (UserDef())
			Overlay->ShowPlayCall(false, false, false, [this, Op, Finish](FCallChoice C) { Finish(Op, C.Kind == FCallChoice::Def && C.Call ? C.Call : Game->cpuDefense(*Op)); });
		else
			Finish(Op, G.cpuDefense(*Op));
	}
}

void AMatchDirector::StartPresnap(const tag::OffPlay* Op, const tag::DefCall* Dc)
{
	tag::Game& G = *Game;
	if (!bTwoPoint && Op->type != tag::PlayType::Kneel && Op->type != tag::PlayType::Spike)
	{
		if (std::optional<tag::Penalty> Pen = G.rollPresnap())
		{
			G.applyPresnap(*Pen);
			Overlay->SetFlag(true);
			Overlay->Banner(TEXT("FLAG"), S(Pen->name) + TEXT("  -  ") + S((Pen->on == tag::Side::Off ? G.offense() : G.defense()).abbr), Flag);
			return Wait(2.f);
		}
	}
	std::optional<tag::Side> US;
	if (UserOff()) US = tag::Side::Off;
	else if (UserDef()) US = tag::Side::Def;
	Sim = bTwoPoint ? G.twoPointSim(*Op, *Dc, US) : G.makePlay(*Op, *Dc, US);
	Dir = G.worldDir();
	std::vector<tag::Player*> Ps;
	for (tag::Ent* E : Sim->ents) Ps.push_back(E->p);
	ShowOnly(Ps);
	CapturePrev();
	Stadium->SetLines(ToWorld(Sim->los, 0, Dir).X, bTwoPoint ? -1 : ToWorld(FMath::Min(100.0, G.ballOn + G.toGo), 0, Dir).X);
	Labels.clear();
	if (US && *US == tag::Side::Off) AssignLabels();
	State = EMatchState::Presnap;
	Timer = bWatch ? 1.1f / Speed : (US && *US == tag::Side::Off) ? 999.f : 1.6f;
	Overlay->SetHint(US && *US == tag::Side::Off ? TEXT("SPACE snap   1-5 throw (SHIFT = touch)   WASD move   C camera")
		: US ? TEXT("Defending   TAB switch player   WASD move   SPACE dive tackle")
		: TEXT("1/2/4 game speed   C camera   ESC pause"));
	SyncPlay(0.f, 1.f);
}

void AMatchDirector::AssignLabels()
{
	std::vector<tag::Ent*> Order;
	for (const std::string& R : Sim->cfg.offPlay->reads)
		if (tag::Ent* E = Sim->offRole(R))
			if (E->state == tag::EntState::Route) Order.push_back(E);
	for (tag::Ent* E : Sim->off)
		if (E->state == tag::EntState::Route && std::find(Order.begin(), Order.end(), E) == Order.end()) Order.push_back(E);
	for (size_t i = 0; i < Order.size() && i < 5; ++i) Labels[Order[i]] = FString::FromInt(static_cast<int>(i) + 1);
}

void AMatchDirector::Wait(float Sec)
{
	State = EMatchState::Wait;
	Timer = Sec;
}

void AMatchDirector::ApplyPlay()
{
	tag::Game& G = *Game;
	tag::PlaySim& P = *Sim;
	const tag::TeamKey PossBefore = G.poss;
	if (bTwoPoint)
	{
		G.finishTwoPoint(P);
		bTwoPoint = false;
	}
	else
	{
		const tag::PlayLog L = G.finishPlay(P);
		if (L.big == tag::LogTag::Penalty) Overlay->Banner(TEXT("FLAG"), TEXT("Penalty on the play"), Flag);
		else if (G.poss == PossBefore && G.phase == tag::GamePhase::Scrimmage && G.down == 1 && P.result && !P.result->td && P.result->yards > 0)
		{
			if (P.result->yards >= 20) Stadium->Hype = 0.6f;
			Overlay->Toast(TEXT("FIRST DOWN"));
		}
		else if (L.desc.find("Turnover on downs") != std::string::npos) Overlay->Banner(TEXT("TURNOVER ON DOWNS"), TEXT(""), Bad);
	}
	State = EMatchState::Wait;
	Timer = 0.f;
}

void AMatchDirector::OnPlayDead()
{
	const tag::PlayResult& R = *Sim->result;
	if (R.td)
	{
		Stadium->Hype = 1.f;
		Shake = 25.f;
		if (Sim->ball.holder)
		{
			Sim->ball.holder->anim = tag::Anim::Celebrate;
			Sim->ball.holder->animT = 0;
		}
		Overlay->Banner(TEXT("TOUCHDOWN"), TEXT(""), Good);
	}
	else if (R.interception) Overlay->Banner(TEXT("INTERCEPTED"), TEXT(""), Bad);
	else if (R.fumbleLost) Overlay->Banner(TEXT("FUMBLE"), TEXT("Defense recovers"), Bad);
	else if (R.kind == tag::PlayKind::Sack) { Overlay->Banner(TEXT("SACK"), TEXT(""), Flag); Shake = 12.f; }
	else if (R.safety) Overlay->Banner(TEXT("SAFETY"), TEXT(""), Bad);
}

void AMatchDirector::ShowFinal()
{
	State = EMatchState::Final;
	Overlay->SetHint(TEXT(""));
	const tag::Game& G = *Game;
	const FString W = G.score.home == G.score.away ? FString(TEXT("Tie game"))
		: G.score.home > G.score.away ? S(G.home->city) + TEXT(" ") + S(G.home->name) + TEXT(" win")
		: S(G.away->city) + TEXT(" ") + S(G.away->name) + TEXT(" win");
	Overlay->ShowBoxScore(TEXT("Final  -  ") + W, TEXT("Main Menu"), [this] { ShowMainMenu(); });
}

void AMatchDirector::TogglePause()
{
	if (State == EMatchState::Paused || State == EMatchState::Menu || State == EMatchState::Final) return;
	ResumeState = State;
	State = EMatchState::Paused;
	Overlay->ShowPause(
		[this] { State = ResumeState; },
		[this]
		{
			if (Sim && Sim->isLive()) Sim->runToEnd();
			if (Sim && Sim->result && (ResumeState == EMatchState::Live || ResumeState == EMatchState::Post || ResumeState == EMatchState::Presnap))
			{
				if (bTwoPoint) Game->finishTwoPoint(*Sim); else Game->finishPlay(*Sim);
			}
			Sim.reset();
			Game->simToEnd();
			ShowFinal();
		},
		[this] { ShowMainMenu(); });
}

// ------------------------------------------------------------------ special teams
std::vector<tag::Player*> AMatchDirector::Pick11(tag::Team& T, const char* RoleName)
{
	std::vector<tag::Player*> Healthy;
	for (auto& P : T.roster)
		if (P->injuredWeeks <= 0) Healthy.push_back(P.get());
	auto By = [&](std::initializer_list<tag::Pos> Ps)
	{
		std::vector<tag::Player*> Out;
		for (tag::Player* P : Healthy)
			for (tag::Pos Q : Ps)
				if (P->pos == Q) { Out.push_back(P); break; }
		std::stable_sort(Out.begin(), Out.end(), [](tag::Player* A, tag::Player* B) { return A->r.speed > B->r.speed; });
		return Out;
	};
	std::vector<tag::Player*> Out;
	const FString R(RoleName);
	if (R == TEXT("K") || R == TEXT("P"))
	{
		auto L = By({R == TEXT("K") ? tag::Pos::K : tag::Pos::P});
		if (!L.empty()) Out.push_back(L[0]);
		if (R == TEXT("P") || Game->phase != tag::GamePhase::Kickoff)
			for (tag::Player* P : By({tag::Pos::OT, tag::Pos::OG, tag::Pos::C, tag::Pos::TE})) if (Out.size() < 9) Out.push_back(P);
	}
	if (R == TEXT("KR"))
	{
		auto L = By({tag::Pos::WR, tag::Pos::CB, tag::Pos::RB});
		if (!L.empty()) Out.push_back(L[0]);
	}
	auto Rest = R == TEXT("D") ? By({tag::Pos::DE, tag::Pos::DT, tag::Pos::LB}) : By({tag::Pos::LB, tag::Pos::S, tag::Pos::CB, tag::Pos::TE, tag::Pos::WR, tag::Pos::RB, tag::Pos::FB, tag::Pos::DE});
	for (tag::Player* P : Rest)
		if (Out.size() < 11 && std::find(Out.begin(), Out.end(), P) == Out.end()) Out.push_back(P);
	return Out;
}

void AMatchDirector::DoPunt()
{
	tag::Game& G = *Game;
	const int D = G.worldDir();
	auto KickU = Pick11(G.offense(), "P");
	auto RecvU = Pick11(G.defense(), "KR");
	const double Los = G.ballOn;
	tag::SpecialResult R = G.punt();
	if (R.flight) R.flight->fromX = Los - 14;
	StartKick(R, D, 1, KickU, RecvU, Los);
}

void AMatchDirector::DoFG()
{
	tag::Game& G = *Game;
	const int D = G.worldDir();
	auto KickU = Pick11(G.offense(), "K");
	auto DefU = Pick11(G.defense(), "D");
	const double Los = G.ballOn;
	const tag::SpecialResult R = G.fieldGoal();
	StartKick(R, D, 2, KickU, DefU, Los);
}

void AMatchDirector::StartKick(const tag::SpecialResult& R, int D, int Kind, const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& RecvTeam, double Los)
{
	Sim.reset();
	Kick = std::make_unique<FKick>();
	Kick->Res = R;
	Kick->Dir = D;
	Kick->Kind = Kind;
	Kick->Kick = KickTeam;
	Kick->Recv = RecvTeam;
	tag::KickFlight F;
	if (R.flight) F = *R.flight;
	else { F.fromX = Los; F.toX = Los + 40; }
	Kick->FromX = F.fromX;
	Kick->ToX = FMath::Min(F.toX, 112.0);
	Kick->Dur = static_cast<float>(F.hang) + 1.6f;
	std::vector<tag::Player*> All = KickTeam;
	All.insert(All.end(), RecvTeam.begin(), RecvTeam.end());
	ShowOnly(All);
	Stadium->SetLines(-1, -1);
	State = EMatchState::Kick;
	Overlay->SetHint(TEXT(""));
}

void AMatchDirector::UpdateKick(float Dt)
{
	FKick& K = *Kick;
	tag::KickFlight F;
	if (K.Res.flight) F = *K.Res.flight;
	K.T += Dt;
	const float Lead = K.Kind == 0 ? 0.35f : K.Kind == 1 ? 1.0f : 0.9f;
	const float Ft = FMath::Clamp((K.T - Lead) / FMath::Max(0.1f, static_cast<float>(F.hang)), 0.f, 1.f);
	const double BallX = K.T < Lead ? K.FromX : K.FromX + (K.ToX - K.FromX) * Ft;
	const double BallZ = K.T < Lead ? 0.3 : 0.3 + 4.0 * F.height * Ft * (1 - Ft);
	Ball->SetActorLocation(ToWorld(BallX, F.y, K.Dir, BallZ));
	Ball->AddActorLocalRotation(FRotator(0, 0, 900 * Dt));
	const double W = tag::FIELD_W;
	auto Pose = [&](tag::Player* P, double X, double Y, double FaceX, const TCHAR* Anim, float Sp)
	{
		AAthlete* A = AthleteFor(P, Game->home->hasPlayer(P) ? tag::TeamKey::Home : tag::TeamKey::Away);
		A->SetActorHiddenInGame(false);
		const float Yw = FaceToYaw(FaceX > 0 ? 0 : PI, K.Dir);
		const FVector VelW = FRotator(0, Yw, 0).Vector() * Sp * YD;
		A->Drive(ToWorld(X, Y, K.Dir), Yw, VelW, 9.f, Anim, K.T, Dt, false);
		A->SetSelected(false);
		A->SetLabel(TEXT(""));
	};
	if (K.Kind == 0)
	{
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			const double Y = i == 0 ? W / 2 : 4 + (static_cast<double>(i) - 1) / 9.0 * (W - 8);
			const double X = K.FromX - (i == 0 ? 7 : 1) + FMath::Max(0.f, K.T - 0.2f) * (i == 0 ? 4 : 8.5);
			Pose(K.Kick[i], FMath::Min(X, BallX + 2), Y, 1, K.T > 0.2f ? TEXT("run") : TEXT("idle"), K.T > 0.2f ? 8.5f : 0.f);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i)
		{
			const double Y = i == 0 ? F.y : 6 + (static_cast<double>(i) - 1) / 9.0 * (W - 12);
			const double X = i == 0 ? FMath::Min(K.ToX, 100.0) : K.FromX + 12 + (i % 3) * 6 + FMath::Max(0.f, K.T - 1) * 2;
			Pose(K.Recv[i], X, Y, -1, i == 0 ? TEXT("idle") : TEXT("run"), i == 0 ? 0.f : 3.f);
		}
	}
	else if (K.Kind == 1)
	{
		const double Los = K.FromX + 14;
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			const double Y = i == 0 ? W / 2 : i <= 6 ? W / 2 + (static_cast<double>(i) - 3.5) * 1.4 : i == 7 ? 4 : i == 8 ? W - 4 : W / 2 + (static_cast<double>(i) - 9.5) * 6;
			const double X = (i == 0 ? K.FromX : Los - 0.6) + FMath::Max(0.f, K.T - 1.1f) * (i == 0 ? 1 : 8);
			Pose(K.Kick[i], X, Y, 1, K.T > 1.1f ? TEXT("run") : TEXT("idle"), K.T > 1.1f ? 8.f : 0.f);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i)
		{
			const double Y = i == 0 ? F.y : W / 2 + (static_cast<double>(i) - 5.5) * 2.2;
			const double X = i == 0 ? FMath::Min(K.ToX, 100.0) : Los + 1 - FMath::Max(0.f, K.T - 1) * 3;
			Pose(K.Recv[i], X, Y, -1, i == 0 ? TEXT("idle") : TEXT("block"), 0.f);
		}
	}
	else
	{
		const double Los = K.FromX + 7;
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			double X = Los - 0.6, Y = W / 2 + (static_cast<double>(i) - 5) * 1.3;
			const TCHAR* Anim = TEXT("block");
			float Sp = 0;
			if (i == 0) { X = K.FromX - 1.5 + FMath::Min(1.f, K.T / 0.9f) * 1.5; Y = W / 2 + 0.5; Anim = K.T > 0.9f ? TEXT("idle") : TEXT("run"); Sp = K.T < 0.9f ? 4.f : 0.f; }
			if (i == 8) { X = K.FromX; Y = W / 2 - 0.4; Anim = TEXT("idle"); }
			Pose(K.Kick[i], X, Y, 1, Anim, Sp);
		}
		for (size_t i = 0; i < K.Recv.size(); ++i) Pose(K.Recv[i], Los + 0.8, W / 2 + (static_cast<double>(i) - 5) * 1.35, -1, TEXT("block"), 0.f);
	}
	UpdateCamera(Dt, ToWorld(BallX, F.y, K.Dir));
	if (K.T > Lead + F.hang && !K.bAnnounced)
	{
		K.bAnnounced = true;
		const tag::PlayLog* L = Game->log.empty() ? nullptr : &Game->log.back();
		if (K.Kind == 2) Overlay->Banner(F.good.value_or(false) ? TEXT("IT'S GOOD") : TEXT("NO GOOD"), L ? S(L->desc) : FString(), F.good.value_or(false) ? Good : Bad);
		else if (L && L->big == tag::LogTag::Td) { Overlay->Banner(TEXT("TOUCHDOWN"), S(L->desc), Good); Stadium->Hype = 1.f; }
		else if (L && L->big == tag::LogTag::Turnover) Overlay->Banner(TEXT("TURNOVER"), S(L->desc), Bad);
	}
	if (K.T >= K.Dur)
	{
		Kick.reset();
		State = EMatchState::Wait;
		Timer = 0;
	}
}

// ------------------------------------------------------------------ world sync
AAthlete* AMatchDirector::AthleteFor(tag::Player* P, tag::TeamKey Side)
{
	if (TObjectPtr<AAthlete>* Found = Athletes.Find(P->id)) return *Found;
	AAthlete* A = GetWorld()->SpawnActor<AAthlete>(AAthlete::StaticClass(), FTransform::Identity);
	const tag::Team& T = Game ? Game->teamOf(Side) : *League[0];
	FUniform U;
	if (Side == tag::TeamKey::Home)
	{
		U.Jersey = Hex(T.colors.primary);
		U.Pants = Hex(T.colors.secondary);
		U.Helmet = Hex(T.colors.primary);
		U.Trim = Hex(T.colors.secondary);
		U.Number = Hex(T.colors.accent);
	}
	else
	{
		U.Jersey = FLinearColor(0.92f, 0.92f, 0.92f);
		U.Pants = FLinearColor(0.85f, 0.85f, 0.85f);
		U.Helmet = Hex(T.colors.primary);
		U.Trim = Hex(T.colors.primary);
		U.Number = Hex(T.colors.primary);
	}
	A->Setup(P, U);
	Athletes.Add(P->id, A);
	return A;
}

void AMatchDirector::ShowOnly(const std::vector<tag::Player*>& Ps)
{
	TSet<int32> Ids;
	for (tag::Player* P : Ps)
	{
		Ids.Add(P->id);
		AthleteFor(P, Game && Game->home->hasPlayer(P) ? tag::TeamKey::Home : tag::TeamKey::Away);
	}
	for (auto& KV : Athletes)
	{
		KV.Value->ResetPose(); // stand tackled players back up for the next snap
		KV.Value->SetActorHiddenInGame(!Ids.Contains(KV.Key));
	}
}

void AMatchDirector::CapturePrev()
{
	if (!Sim) return;
	for (tag::Ent* E : Sim->ents) Prev[E] = FVector2D(E->pos.x, E->pos.y);
}

void AMatchDirector::SyncPlay(float Dt, float Alpha)
{
	if (!Sim) return;
	const tag::TeamKey OffK = Game->poss;
	for (tag::Ent* E : Sim->ents)
	{
		const FVector2D* PV = nullptr;
		auto It = Prev.find(E);
		if (It != Prev.end()) PV = &It->second;
		const double X = PV ? FMath::Lerp(PV->X, E->pos.x, static_cast<double>(Alpha)) : E->pos.x;
		const double Y = PV ? FMath::Lerp(PV->Y, E->pos.y, static_cast<double>(Alpha)) : E->pos.y;
		AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? OffK : tag::other(OffK));
		const FVector VelW = Dir > 0 ? FVector(E->vel.x, -E->vel.y, 0) * YD : FVector(-E->vel.x, E->vel.y, 0) * YD;
		A->Drive(ToWorld(X, Y, Dir), FaceToYaw(E->face, Dir), VelW, static_cast<float>(E->vmax), FString(E->animName()), static_cast<float>(E->animT), Dt, E->isOL || E->isDL);
		A->SetSelected(E->userControlled);
		auto L = Labels.find(E);
		A->SetLabel(L != Labels.end() ? L->second : FString());
	}
	// ball
	const tag::Ball& B = Sim->ball;
	auto AthOf = [&](const tag::Ent* X) { return AthleteFor(X->p, X->side == tag::Side::Off ? OffK : tag::other(OffK)); };
	const FVector BallW = ToWorld(B.pos.x, B.pos.y, Dir, B.pos.z);
	const FVector BallVelW = Dir > 0 ? FVector(B.vel.x, -B.vel.y, B.vel.z) * YD : FVector(-B.vel.x, B.vel.y, B.vel.z) * YD;

	// football animation layer: what each player's hands, torso and eyes are doing (spec L2/L4)
	const tag::Ent* Holder = B.mode == tag::BallMode::Held ? B.holder : nullptr;
	for (tag::Ent* E : Sim->ents)
	{
		AAthlete* A = AthOf(E);
		FAthleteFootball F;
		const bool bQB = E->p->pos == tag::Pos::QB;
		F.bHasBall = Holder == E;
		F.bSecure = F.bHasBall && bQB && E->speed() < 5.5 && E->anim != tag::Anim::Run;
		if (E->anim == tag::Anim::Throw) F.ThrowT = FMath::Clamp(static_cast<float>(E->animT) / 0.45f, 0.f, 1.f);
		F.bStance = Sim->t < 0.05 && (E->isOL || E->isDL);
		const FVector Me = A->GetActorLocation();
		// catch: the intended receiver (and anyone right on the ball) gets hands to where the ball will be
		if (B.mode == tag::BallMode::Air)
		{
			const float Dist = FVector::Dist(BallW, Me + FVector(0, 0, 120));
			const bool bTarget = B.target == E;
			if ((bTarget && Dist < 900.f) || Dist < 260.f)
			{
				// hands go to the interception point (ball's closest approach to the chest), always out in
				// front of the facemask - over-the-shoulder balls are caught in front, not behind
				const FVector ChestW = A->BoneWorld(TEXT("spine_05"));
				const float V2 = FMath::Max(static_cast<float>(BallVelW.SizeSquared()), 1.f);
				const float Tc = FMath::Clamp(static_cast<float>(FVector::DotProduct(ChestW - BallW, BallVelW)) / V2, 0.f, 1.f);
				FVector Catch = A->GetActorTransform().InverseTransformPosition(BallW + BallVelW * Tc);
				Catch.X = FMath::Max(static_cast<float>(Catch.X), 32.f);       // in front of the chest
				Catch.Y = FMath::Clamp(static_cast<float>(Catch.Y), -45.f, 45.f);
				Catch.Z = FMath::Clamp(static_cast<float>(Catch.Z), 70.f, 215.f);
				F.bReach = true;
				F.ReachWorld = A->GetActorTransform().TransformPosition(Catch);
				F.ReachSpread = Catch.Z > 150.f ? 6.f : 9.f; // thumbs together high, pinkies together low
			}
			F.bLook = true;
			F.LookWorld = BallW;
		}
		// blocks: hands inside on the opponent's chest plate, drive lean
		const tag::Ent* Opp = E->engaged ? E->engaged : (!E->engagedBy.empty() ? E->engagedBy[0] : nullptr);
		if (Opp && !F.bHasBall)
		{
			AAthlete* O = AthOf(Opp);
			const FVector Chest = O->BoneWorld(TEXT("spine_04"));
			F.bReach = true;
			F.ReachWorld = Chest + (Me - Chest).GetSafeNormal2D() * 16.f;
			F.ReachSpread = 13.f;
			F.Lean = E->engaged ? 0.9f : 0.6f;
			F.bLook = true;
			F.LookWorld = O->BoneWorld(TEXT("head"));
		}
		// tackles: wrap the ball carrier at the waist
		if (Holder && Holder != E && (E->anim == tag::Anim::Tackle || E->anim == tag::Anim::Dive ||
			(E->side != Holder->side && FVector::Dist2D(Me, AthOf(Holder)->GetActorLocation()) < 230.f)))
		{
			AAthlete* H = AthOf(Holder);
			F.bReach = true;
			F.ReachWorld = H->BoneWorld(TEXT("pelvis")) + FVector(0, 0, 18);
			F.ReachSpread = 22.f;
			F.Lean = 0.8f;
		}
		if (!F.bLook)
		{
			F.bLook = true;
			if (F.bHasBall && bQB && B.target) F.LookWorld = AthOf(B.target)->BoneWorld(TEXT("head"));
			else if (Holder && Holder != E) F.LookWorld = AthOf(Holder)->BoneWorld(TEXT("spine_04"));
			else F.LookWorld = Me + A->GetActorForwardVector() * 1500.f + FVector(0, 0, 160);
		}
		A->SetFootball(F, Dt);
	}

	// automation: grab the key football moments up close (spec acceptance shots)
	if (bAuto && EventShotFrames == 0)
	{
		for (tag::Ent* E : Sim->ents)
		{
			if (!bShotThrow && E->anim == tag::Anim::Throw && E->animT > 0.14)
			{
				bShotThrow = true;
				EventShotName = TEXT("tag_auto_throw.png");
				CloseupTarget = AthOf(E);
				EventShotFrames = 3;
			}
		}
		if (!bShotCatch && B.mode == tag::BallMode::Air && B.target &&
			FVector::Dist(BallW, AthOf(B.target)->GetActorLocation()) < 380.f)
		{
			bShotCatch = true;
			EventShotName = TEXT("tag_auto_catch.png");
			CloseupTarget = AthOf(B.target);
			EventShotFrames = 3;
		}
	}
	if (EventShotFrames > 0 && --EventShotFrames == 0)
	{
		FScreenshotRequest::RequestScreenshot(EventShotName, false, false);
		const AAthlete* CA = Cast<AAthlete>(CloseupTarget);
		UE_LOG(LogTemp, Display, TEXT("TAG event shot %s ball=%s %s"), *EventShotName,
			CA ? *CA->GetActorTransform().InverseTransformPosition(BallW).ToCompactString() : TEXT("-"), CA ? *CA->DebugAnimState() : TEXT(""));
	}
	else if (EventShotFrames == 0 && CloseupTarget && AutoShot < 8)
	{
		CloseupTarget = nullptr;
	}

	if (B.mode == tag::BallMode::Held && B.holder)
	{
		AAthlete* A = AthOf(B.holder);
		FVector HoldLoc;
		FRotator HoldRot;
		if (B.holder->anim != tag::Anim::Down && A->GetBallHold(HoldLoc, HoldRot))
		{
			Ball->SetActorLocation(HoldLoc);
			Ball->SetActorRotation(HoldRot);
		}
		else
		{
			const FVector Fwd = A->GetActorForwardVector();
			const FVector Right = A->GetActorRightVector();
			const bool bDown = B.holder->anim == tag::Anim::Down;
			Ball->SetActorLocation(A->GetActorLocation() + Fwd * 16 + Right * 20 + FVector(0, 0, bDown ? 30 : 108));
			Ball->SetActorRotation(A->GetActorRotation());
		}
	}
	else
	{
		Ball->SetActorLocation(ToWorld(B.pos.x, B.pos.y, Dir, B.pos.z));
		const FVector V = Dir > 0 ? FVector(B.vel.x, -B.vel.y, B.vel.z) : FVector(-B.vel.x, B.vel.y, B.vel.z);
		if (V.SizeSquared() > 0.01)
		{
			FRotator R = V.Rotation();
			R.Roll = FMath::RadiansToDegrees(B.spin);
			Ball->SetActorRotation(R);
		}
	}
}

FVector AMatchDirector::FocusPoint() const
{
	if (!Sim) return IdleFocus;
	const tag::Ball& B = Sim->ball;
	const double X = B.holder ? B.holder->pos.x : B.pos.x;
	const double Y = B.holder ? B.holder->pos.y : B.pos.y;
	return ToWorld(X, Y, Dir);
}

void AMatchDirector::UpdateCamera(float Dt, const FVector& Focus)
{
	const double Y = YD;
	FVector T, Look;
	const double D = static_cast<double>(Dir);
	switch (CamMode)
	{
	case ECamMode::Broadcast:
		T = FVector(Focus.X - D * 17 * Y, Focus.Y * 0.45, 10.5 * Y);
		Look = FVector(Focus.X + D * 7 * Y, Focus.Y * 0.8, 0);
		break;
	case ECamMode::Sideline:
		// high sideline position in front of the lower bowl (press-box angle)
		T = FVector(Focus.X - D * 4 * Y, -(tag::FIELD_W / 2 + 12) * Y, 17 * Y);
		Look = FVector(Focus.X + D * 2 * Y, Focus.Y * 0.5, 0);
		break;
	case ECamMode::Skycam:
		T = FVector(Focus.X - D * 22 * Y, Focus.Y * 0.3, 30 * Y);
		Look = FVector(Focus.X + D * 10 * Y, Focus.Y * 0.6, 0);
		break;
	case ECamMode::Player:
	{
		FVector F = Focus;
		if (Sim && Sim->user) F = ToWorld(Sim->user->pos.x, Sim->user->pos.y, Dir);
		T = FVector(F.X - D * 7 * Y, F.Y, 3.4 * Y);
		Look = FVector(F.X + D * 8 * Y, F.Y, 1 * Y);
		break;
	}
	}
	// keep cameras over the field / in front of the stands (never inside geometry)
	T.X = FMath::Clamp(T.X, -6.0 * Y, 106.0 * Y);
	T.Y = FMath::Clamp(T.Y, -(tag::FIELD_W / 2 + 12) * Y, (tag::FIELD_W / 2 + 12) * Y);
	if (CamMode != LastCamMode)
	{
		LastCamMode = CamMode;
		CamPos = T; // cut, don't swoop, when the camera changes
		CamLook = Look;
	}
	const double K = 1.0 - FMath::Exp(-Dt * 3.2);
	CamPos = FMath::Lerp(CamPos, T, K);
	CamLook = FMath::Lerp(CamLook, Look, FMath::Min(1.0, K * 1.3));
	FVector P = CamPos;
	if (Shake > 0.1f)
	{
		P += FVector(FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake, Shake));
		Shake *= FMath::Exp(-Dt * 6.f);
	}
	if (bAutoEstablish)
	{
		P = FVector(50 * Y, -30 * Y, 22 * Y);
		CamLook = FVector(50 * Y, 70 * Y, 14 * Y);
	}
	else if (CloseupTarget)
	{
		const FVector Tgt = CloseupTarget->GetActorLocation() + FVector(0, 0, 140);
		const FVector F = CloseupTarget->GetActorForwardVector();
		const FVector R = CloseupTarget->GetActorRightVector();
		if (CloseupView == 1)
		{
			P = Tgt + F * 380.f + R * 120.f - FVector(0, 0, 30);
			CamLook = Tgt - FVector(0, 0, 60);
		}
		else if (CloseupView == 2)
		{
			P = Tgt - F * 260.f + R * 70.f;
			CamLook = Tgt - FVector(0, 0, 25);
		}
		else
		{
			P = Tgt + F * 230.f + R * 90.f + FVector(0, 0, 10);
			CamLook = Tgt - FVector(0, 0, 25);
		}
	}
	Camera->SetActorLocation(P);
	Camera->SetActorRotation((CamLook - P).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->FocusSettings.ManualFocusDistance = (CamLook - P).Size();
		C->CurrentFocalLength = CamMode == ECamMode::Sideline ? 45.f : CamMode == ECamMode::Player ? 24.f : 30.f;
	}
}

void AMatchDirector::ReadUserInput()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !Sim) return;
	tag::PlaySim& P = *Sim;
	float MX = 0, MY = 0;
	if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::Up)) MY += 1;
	if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Down)) MY -= 1;
	if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) MX += 1;
	if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) MX -= 1;
	const float GX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	const float GY = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	if (FMath::Abs(GX) + FMath::Abs(GY) > 0.2f) { MX = GX; MY = GY; }
	FVector F = CamLook - CamPos;
	F.Z = 0;
	F.Normalize();
	const FVector R(-F.Y, F.X, 0);
	FVector Wv = R * MX + F * MY;
	if (Wv.Size() > 1) Wv.Normalize();
	P.input.move = Dir > 0 ? tag::V2{Wv.X, -Wv.Y} : tag::V2{-Wv.X, Wv.Y};
	P.input.sprint = true;
	const FKey Nums[5] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five};
	const FKey Pads[5] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_RightShoulder};
	for (int i = 0; i < 5; ++i)
	{
		if (PC->WasInputKeyJustPressed(Nums[i]) || PC->WasInputKeyJustPressed(Pads[i]))
		{
			for (auto& KV : Labels)
				if (KV.second == FString::FromInt(i + 1)) P.input.throwTo = KV.first->role;
			P.input.lob = PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::Gamepad_LeftTrigger);
		}
	}
	const bool bAction = PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Gamepad_RightTrigger);
	P.input.juke = P.input.juke || bAction;
	P.input.dive = bAction;
	if (PC->WasInputKeyJustPressed(EKeys::Tab) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder)) P.input.switchReq = true;
	// auto-switch to our ball carrier after a handoff / catch
	tag::Ent* H = P.ball.holder;
	if (H && P.user && H->side == P.user->side && H != P.user && H != P.qb)
	{
		P.user->userControlled = false;
		P.user = H;
		H->userControlled = true;
		Labels.clear();
	}
	if (P.passThrown) Labels.clear();
}

// ------------------------------------------------------------------ tick
void AMatchDirector::Tick(float Dt)
{
	Super::Tick(Dt);
	TickAutomation(Dt);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const bool bEsc = PC && (PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right));
	if (State == EMatchState::Menu)
	{
		MenuOrbit += Dt * 0.05f;
		const FVector C(50 * YD, 0, 0);
		Camera->SetActorLocation(C + FVector(FMath::Cos(MenuOrbit) * 75 * YD, FMath::Sin(MenuOrbit) * 60 * YD, 26 * YD));
		Camera->SetActorRotation((C - Camera->GetActorLocation()).Rotation());
		return;
	}
	if (!Game) return;
	if (bEsc && State != EMatchState::Final) TogglePause();
	if (PC && PC->WasInputKeyJustPressed(EKeys::C)) CamMode = static_cast<ECamMode>((static_cast<uint8>(CamMode) + 1) % 4);
	if (bWatch && PC)
	{
		if (PC->WasInputKeyJustPressed(EKeys::One)) Speed = 1.f;
		if (PC->WasInputKeyJustPressed(EKeys::Two)) Speed = 2.f;
		if (PC->WasInputKeyJustPressed(EKeys::Four)) Speed = 4.f;
	}
	const float SDt = Dt * (bWatch ? Speed : 1.f);
	const bool bSpace = PC && (PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom));
	switch (State)
	{
	case EMatchState::Wait:
		Timer -= SDt;
		UpdateCamera(Dt, Sim ? FocusPoint() : IdleFocus);
		if (Sim) SyncPlay(Dt, 1.f);
		if (Timer <= 0) Next();
		break;
	case EMatchState::Presnap:
		Timer -= SDt;
		if (UserDef() && PC && PC->WasInputKeyJustPressed(EKeys::Tab)) Sim->switchUser();
		if (Timer <= 0 || (UserOff() && bSpace))
		{
			State = EMatchState::Live;
			Acc = 0;
			Overlay->SetHint(TEXT(""));
		}
		SyncPlay(Dt, 1.f);
		UpdateCamera(Dt, FocusPoint());
		break;
	case EMatchState::Live:
	{
		if (UserTeam) ReadUserInput();
		Acc += SDt;
		int N = 0;
		while (Acc >= STEP && Sim->isLive() && N++ < 12)
		{
			CapturePrev();
			Sim->step(STEP);
			Acc -= STEP;
			Sim->input.juke = false;
		}
		SyncPlay(Dt, FMath::Min(1.f, Acc / STEP));
		UpdateCamera(Dt, FocusPoint());
		Overlay->SetClockOffset(Sim->t);
		if (!Sim->isLive())
		{
			State = EMatchState::Post;
			Timer = bWatch ? 1.4f / Speed : 1.8f;
			OnPlayDead();
		}
		break;
	}
	case EMatchState::Post:
		Timer -= SDt;
		for (tag::Ent* E : Sim->ents) E->animT += Dt;
		SyncPlay(Dt, 1.f);
		UpdateCamera(Dt, FocusPoint());
		if (Timer <= 0 || (!bWatch && Timer < 1.2f && bSpace))
		{
			Overlay->SetClockOffset(0);
			ApplyPlay();
		}
		break;
	case EMatchState::Kick:
		UpdateKick(SDt);
		break;
	default:
		UpdateCamera(Dt, Sim ? FocusPoint() : IdleFocus);
		if (Sim) SyncPlay(Dt, 1.f);
		break;
	}
}
