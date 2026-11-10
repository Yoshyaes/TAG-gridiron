#include "Game/MatchDirector.h"

#include "Render/Athlete.h"
#include "Render/Stadium.h"
#include "Render/Coords.h"
#include "Render/PlayArt.h"
#include "Game/PlayArtStrokes.h"
#include "TimerManager.h"
#include "UI/GridironOverlay.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
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
	// controllers: GameInput (GameInputWindows plugin) reads PS5 DualSense pads as standard gamepads, XInput pads too;
	// all of them drive the one player (Config/Windows/WindowsInput.ini). Note whether a Sony pad is plugged in so the
	// hints can name its buttons.
#if PLATFORM_WINDOWS
	{
		UINT N = 0;
		GetRawInputDeviceList(nullptr, &N, sizeof(RAWINPUTDEVICELIST));
		TArray<RAWINPUTDEVICELIST> L;
		L.SetNumZeroed(N);
		if (N && GetRawInputDeviceList(L.GetData(), &N, sizeof(RAWINPUTDEVICELIST)) != (UINT)-1)
			for (const RAWINPUTDEVICELIST& D : L)
			{
				if (D.dwType != RIM_TYPEHID) continue;
				UINT Len = 0;
				GetRawInputDeviceInfoW(D.hDevice, RIDI_DEVICENAME, nullptr, &Len);
				TArray<WCHAR> Name;
				Name.SetNumZeroed(Len + 1);
				GetRawInputDeviceInfoW(D.hDevice, RIDI_DEVICENAME, Name.GetData(), &Len);
				if (FString(Name.GetData()).ToUpper().Contains(TEXT("VID_054C"))) bPlayStationPad = true;
			}
	}
#endif
	UE_LOG(LogTemp, Display, TEXT("TAG controllers: PlayStation pad %s, device mapping policy %d"), bPlayStationPad ? TEXT("found") : TEXT("not found"),
		static_cast<int32>(IPlatformInputDeviceMapper::Get().GetCurrentDeviceMappingPolicy()));
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
	const FAthleteAssets& GA = FAthleteAssets::Get();
	UStaticMeshComponent* BC = Ball->GetStaticMeshComponent();
	BC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (GA.FootballMesh)
	{
		// TAG football (Tools/Art/gear_build.py): pointed tips and white laces, authored at real size
		BC->SetStaticMesh(GA.FootballMesh);
		BC->SetMaterial(0, TagColorMat(this, FLinearColor(0.16f, 0.055f, 0.018f), 0.5f));
		BC->SetMaterial(1, TagColorMat(this, FLinearColor(0.9f, 0.9f, 0.88f), 0.6f));
		Ball->SetActorScale3D(FVector::OneVector);
	}
	else
	{
		BC->SetStaticMesh(GA.Sphere);
		BC->SetMaterial(0, TagColorMat(this, FLinearColor(0.2f, 0.07f, 0.02f), 0.55f));
		Ball->SetActorScale3D(FVector(0.28f, 0.17f, 0.17f));
	}

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
	if (Overlay) Overlay->Toast(bPlayStationPad ? TEXT("PS5 controller connected") : TEXT("No PlayStation controller found (Xbox pads and keyboard work)"));
	if (FParse::Param(FCommandLine::Get(), TEXT("TagAutoWatch")))
	{
		bAuto = true;
		int32 Wx = 0;
		FParse::Value(FCommandLine::Get(), TEXT("TagWeather="), Wx);
		const bool bDay = FParse::Param(FCommandLine::Get(), TEXT("TagDay"));
		StartMatch(3, 7, false, true, Wx, !bDay, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagCallShot")))
	{
		bCallShot = true;
		StartMatch(3, 7, true, true, 0, true, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagArtTest")))
	{
		bArtTest = true;
		bPadTest = FParse::Param(FCommandLine::Get(), TEXT("TagPadTest"));
		StartMatch(3, 7, true, true, 0, true, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagMoveTest")))
	{
		bLineup = true;
		bMoveTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagTackleTest")))
	{
		bLineup = true;
		bTackleTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagLineTest")))
	{
		bLineup = true;
		bLineTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagGapTest")))
	{
		bLineup = true;
		bGapTest = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("TagLineup")))
	{
		// look-dev lineup: six athletes (home and away, every body type) in a row, shot front then back, then exit
		bLineup = true;
		StartMatch(3, 7, false, true, 0, false, 900);
	}
}

void AMatchDirector::TickLineup(float Dt)
{
	LineupT += Dt;
	if (Lineup.Num() == 0)
	{
		const tag::Pos Pick[] = {tag::Pos::OT, tag::Pos::DE, tag::Pos::LB, tag::Pos::RB, tag::Pos::WR, tag::Pos::QB};
		const tag::Pos MovePick[] = {tag::Pos::WR, tag::Pos::RB, tag::Pos::CB, tag::Pos::OT};
		const tag::Pos GapPick[] = {tag::Pos::QB, tag::Pos::RB, tag::Pos::WR, tag::Pos::LB, tag::Pos::CB};
		const tag::TeamKey GapSide[] = {tag::TeamKey::Home, tag::TeamKey::Home, tag::TeamKey::Home, tag::TeamKey::Away, tag::TeamKey::Away};
		const tag::TeamKey Side[] = {tag::TeamKey::Home, tag::TeamKey::Away, tag::TeamKey::Home, tag::TeamKey::Away, tag::TeamKey::Home, tag::TeamKey::Away};
		for (int32 i = 0; i < ((bGapTest || bTackleTest) ? 5 : bMoveTest ? 4 : 6); ++i)
		{
			const tag::TeamKey Sd = (bGapTest || bTackleTest) ? GapSide[i] : Side[i];
			const std::vector<tag::Player*> D = tag::depth(Game->teamOf(Sd), (bGapTest || bTackleTest) ? GapPick[i] : bMoveTest ? MovePick[i] : Pick[i]);
			if (!D.empty()) Lineup.Add(AthleteFor(D[0], Sd));
		}
		for (auto& KV : Athletes) KV.Value->SetActorHiddenInGame(!Lineup.Contains(KV.Value));
		if (Ball) Ball->SetActorHiddenInGame(true);
	}
	if (bMoveTest)
	{
		TickMoveTest(Dt);
		return;
	}
	if (bGapTest)
	{
		TickGapTest(Dt);
		return;
	}
	if (bLineTest)
	{
		TickLineTest(Dt);
		return;
	}
	if (bTackleTest)
	{
		TickTackleTest(Dt);
		return;
	}
	// 0-9 s pre-snap stances facing the camera, 9-13 s from behind, 13-15 s celebrating, 15-17 s contact / evasion:
	// linemen tackle, skill players juke (alternating sides, with real lateral velocity), the QB kneels
	const bool bBack = LineupT > 9.f && LineupT <= 13.f;
	const bool bCelebrate = LineupT > 13.f && LineupT <= 15.f;
	const bool bAction = LineupT > 15.f;
	const FVector Center = TagCoords::ToWorld(50, tag::FIELD_W * 0.5, 1);
	for (int32 i = 0; i < Lineup.Num(); ++i)
	{
		const FVector L = Center + FVector(0, (i - (Lineup.Num() - 1) * 0.5) * 115.0, 0);
		FAthleteFootball F;
		F.bPreSnap = !bCelebrate && !bAction;
		Lineup[i]->SetFootball(F, Dt);
		const TCHAR* LineState = bCelebrate ? TEXT("celebrate") : TEXT("idle");
		FVector Vel = FVector::ZeroVector;
		if (bAction)
		{
			LineState = i < 2 ? TEXT("tackle") : i == Lineup.Num() - 1 ? TEXT("kneel") : TEXT("juke");
			// facing -X (yaw 180): the actor's right is +Y; alternate cut sides across the skill players
			if (i >= 2 && i < Lineup.Num() - 1) Vel = FVector(-300.f, (i % 2) ? 250.f : -250.f, 0.f);
		}
		Lineup[i]->Drive(L, bBack ? 0.f : 180.f, Vel, 9.f, LineState, LineupT, Dt, false);
	}
	if (LineupT > 16.8f)
	{
		// last beat: look at the near sideline's front rows (3D hero crowd) from the field, crowd hyped (cheering)
		if (Stadium) Stadium->Hype = 1.f;
		const FVector Stand(50 * TagCoords::YD, (tag::FIELD_W / 2 + 9) * TagCoords::YD, 0);
		Camera->SetActorLocation(Stand + FVector(0, -900, 300));
		Camera->SetActorRotation(((Stand + FVector(0, 250, 250)) - Camera->GetActorLocation()).Rotation());
	}
	else if (Lineup.Num() > 3 && ((LineupT > 6.4f && LineupT < 7.6f) || (LineupT > 14.3f && LineupT < 14.9f)))
	{
		// face closeups (expressions): pre-snap focus, then the celebration
		const FVector Head = Lineup[3]->BoneWorld(TEXT("head")) + FVector(0, 0, 6);
		const FVector Fw = Lineup[3]->GetActorForwardVector();
		Camera->SetActorLocation(Head + Fw * 95.f + FVector(0, 0, 4));
		Camera->SetActorRotation((Head - Camera->GetActorLocation()).Rotation());
	}
	else
	{
		Camera->SetActorLocation(Center + FVector(-1050, 0, 150));
		Camera->SetActorRotation(((Center + FVector(0, 0, 100)) - Camera->GetActorLocation()).Rotation());
	}
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 30.f;
		C->FocusSettings.ManualFocusDistance = 1050.f;
	}
	auto Shot = [&](float At, const TCHAR* Name, int32 Idx)
	{
		if (LineupShot == Idx && LineupT >= At)
		{
			FScreenshotRequest::RequestScreenshot(Name, false, false);
			++LineupShot;
		}
	};
	Shot(7.f, TEXT("tag_lineup_face_focus.png"), 0);
	Shot(7.7f, TEXT("tag_lineup_front.png"), 1);
	Shot(12.f, TEXT("tag_lineup_back.png"), 2);
	{
		static float NextLog = 0.f;
		if (LineupT >= NextLog && Lineup.Num() > 3)
		{
			NextLog = LineupT + 0.5f;
			for (int32 i : {0, 1, 3})
				UE_LOG(LogTemp, Display, TEXT("TAG lineup t=%.2f #%d head=%s %s"), LineupT, i, *Lineup[i]->BoneWorld(TEXT("head")).ToString(), *Lineup[i]->DebugAnimState());
		}
	}
	Shot(14.1f, TEXT("tag_lineup_celebrate_a.png"), 3);
	Shot(14.5f, TEXT("tag_lineup_face_celebrate.png"), 4);
	Shot(15.25f, TEXT("tag_lineup_action_a.png"), 5);
	Shot(15.45f, TEXT("tag_lineup_action_b.png"), 6);
	Shot(16.4f, TEXT("tag_lineup_action_c.png"), 7);
	Shot(17.6f, TEXT("tag_lineup_crowd.png"), 8);
	Shot(17.8f, TEXT("tag_lineup_crowd_b.png"), 9);
	PerfT += Dt; ++PerfFrames;
	if (LineupT > 18.f)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		FPlatformMisc::RequestExit(false);
	}
}

void AMatchDirector::TickMoveTest(float Dt)
{
	// hold the clock until motion matching has its search indexes (uncooked runs build them in the background)
	FAthleteAssets& AA = FAthleteAssets::Get();
	if (AA.bMotionMatch && !AA.MotionMatchReady())
	{
		LineupT = 0.f;
		return;
	}
	const float Seg = 9.f;
	const int32 R = FMath::Clamp(static_cast<int32>(LineupT / Seg), 0, Lineup.Num() - 1);
	const float T = LineupT - R * Seg;
	const FVector Start = TagCoords::ToWorld(30, tag::FIELD_W * 0.5, 1);
	if (R != MoveRunner)
	{
		double Raw, Shown, Sec, Metres;
		int32 Steps;
		AAthlete::SlideStats(Raw, Shown, true);
		AAthlete::GaitStats(Sec, Metres, Steps, true);
		if (MoveRunner >= 0) UE_LOG(LogTemp, Display, TEXT("TAG slide r%d: animation %.0f cm, shown %.0f cm (planted-foot travel over the route)"), MoveRunner, Raw, Shown);
		double HipsM, HeadM;
		AAthlete::HeightStats(HipsM, HeadM, true);
		if (MoveRunner >= 0 && Sec > 0.5) UE_LOG(LogTemp, Display, TEXT("TAG gait r%d: %.2f steps/s, %.2f m per step at %.1f m/s over %.1f s, hips %.2f m head %.2f m (sprint reference ~4.7 steps/s, 2.1 m at 10 m/s; hips 0.90-0.99, head 1.32-1.54)"), MoveRunner, Steps / Sec, Steps > 0 ? Metres / Steps : 0.0, Metres / Sec, Sec, HipsM / Sec, HeadM / Sec);
		MoveRunner = R;
		MovePos = Start;
		MoveVel = FVector::ZeroVector;
		for (int32 i = 0; i < Lineup.Num(); ++i) Lineup[i]->SetActorHiddenInGame(i != R);
		Lineup[R]->ResetPose();
	}
	// target velocity and facing for this runner's route (yaw 0 = +X, downfield)
	FVector Target = FVector::ZeroVector;
	float Yaw = 0.f;
	if (R == 0)
	{
		// receiver: standing start, sprint 10 m/s (past the 9.1 the sped-up capture covers: stride warping), plant and
		// cut 90 deg right at 4.5 s, stop at 7 s
		if (T > 1.f && T <= 4.5f) Target = FVector(1000, 0, 0);
		else if (T > 4.5f && T <= 7.f) Target = FVector(0, -850, 0);
	}
	else if (R == 1)
	{
		// back: 7 m/s, then a 120 deg sweeping curve, stop at 7 s
		const float CurveYaw = T <= 3.f ? 0.f : FMath::Min(T - 3.f, 4.f) * 30.f;
		if (T > 1.f && T <= 7.f) Target = 750.f * FRotator(0, CurveYaw, 0).Vector();
	}
	else if (R == 3)
	{
		// tackle: pass set kick-slide (back and out at 1.9 m/s, facing downfield), anchor, drive block forward, settle
		if (T > 1.f && T <= 3.f) Target = FVector(-110, 150, 0);
		else if (T > 4.5f && T <= 6.5f) Target = FVector(220, 0, 0);
	}
	else
	{
		// defensive back: backpedal at 3.8 m/s facing downfield, plant and break forward at 4 s, stop at 7.5 s
		if (T > 1.f && T <= 4.f) Target = FVector(-380, 0, 0);
		else if (T > 4.f && T <= 7.5f) Target = FVector(850, 0, 0);
	}
	const float MaxDv = (Target.Size() > MoveVel.Size() ? 900.f : 1500.f) * Dt;
	MoveVel += (Target - MoveVel).GetClampedToMaxSize(MaxDv);
	MovePos += MoveVel * Dt;
	if (R < 2 && MoveVel.Size() > 50.f) Yaw = MoveVel.Rotation().Yaw;
	else if (R < 2) Yaw = Lineup[R]->GetActorRotation().Yaw;
	FAthleteFootball F;
	Lineup[R]->SetFootball(F, Dt);
	Lineup[R]->Drive(MovePos, Yaw, MoveVel, 10.f, MoveVel.Size() > 30.f ? TEXT("run") : TEXT("idle"), T, Dt, R == 3);

	// tracking side camera (from the -Y sideline side), 8 m off, at chest height
	Camera->SetActorLocation(MovePos + FVector(-250, -800, 170));
	Camera->SetActorRotation(((MovePos + FVector(0, 0, 95)) - Camera->GetActorLocation()).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 28.f;
		C->FocusSettings.ManualFocusDistance = 840.f;
	}
	// -TagMoveFrames=<route>,<from>,<to>: every frame of that window (foot skating shows between consecutive frames)
	{
		static FString Win = FParse::Value(FCommandLine::Get(), TEXT("TagMoveFrames="), Win, false) ? Win : FString();
		static int32 FrameNum = 0;
		TArray<FString> P;
		// a 4th field "shots" also writes a screenshot per frame (run with -UseFixedTimeStep -FPS=30: writing one stalls)
		const int32 N = Win.ParseIntoArray(P, TEXT(","));
		if (N >= 3 && R == FCString::Atoi(*P[0]) && T >= FCString::Atof(*P[1]) && T <= FCString::Atof(*P[2]))
		{
			if (N > 3) FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_frames_r%d_%03d.png"), R, FrameNum), false, false);
			UE_LOG(LogTemp, Display, TEXT("TAG frame r%d %03d t=%.3f v=%s footL=%s footR=%s %s"), R, FrameNum++, T, *MoveVel.ToCompactString(),
				*Lineup[R]->BoneWorld(TEXT("ball_l")).ToCompactString(), *Lineup[R]->BoneWorld(TEXT("ball_r")).ToCompactString(), *Lineup[R]->DebugAnimState());
		}
	}
	const float ShotT = 0.8f + (MoveShot % 27) * 0.3f;
	if (MoveShot / 27 == R && T >= ShotT)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_move_r%d_%02d.png"), R, MoveShot % 27), false, false);
		double SlRaw, SlShown, GSec, GM;
		int32 GSteps;
		AAthlete::SlideStats(SlRaw, SlShown, false);
		AAthlete::GaitStats(GSec, GM, GSteps, false);
		UE_LOG(LogTemp, Display, TEXT("TAG move r%d #%d t=%.2f v=%.0f slide=%.0f/%.0f steps=%d %s"), R, Lineup[R]->Player ? Lineup[R]->Player->number : -1, T, MoveVel.Size(), SlRaw, SlShown, GSteps, *Lineup[R]->DebugAnimState());
		++MoveShot;
	}
	PerfT += Dt; ++PerfFrames;
	if (LineupT > Seg * Lineup.Num())
	{
		double Raw, Shown, Sec, Metres;
		int32 Steps;
		AAthlete::SlideStats(Raw, Shown, true);
		AAthlete::GaitStats(Sec, Metres, Steps, true);
		UE_LOG(LogTemp, Display, TEXT("TAG slide r%d: animation %.0f cm, shown %.0f cm (planted-foot travel over the route)"), MoveRunner, Raw, Shown);
		double HipsM, HeadM;
		AAthlete::HeightStats(HipsM, HeadM, true);
		if (Sec > 0.5) UE_LOG(LogTemp, Display, TEXT("TAG gait r%d: %.2f steps/s, %.2f m per step at %.1f m/s over %.1f s, hips %.2f m head %.2f m (sprint reference ~4.7 steps/s, 2.1 m at 10 m/s; hips 0.90-0.99, head 1.32-1.54)"), MoveRunner, Steps / Sec, Steps > 0 ? Metres / Steps : 0.0, Metres / Sec, Sec, HipsM / Sec, HeadM / Sec);
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		FPlatformMisc::RequestExit(false);
	}
}

// the pairs as captured (Tools/Anim/mocap_pairs.py from the PT_* clips' jsons; both men of a pair come from one mocap
// group in a shared space, cut at the hit): the carrier's actor in the tackler's clip frame (m; x left, y back) and his
// yaw relative to the tackler (deg, + turns left); where the carrier's pelvis ends (his clip frame)
namespace
{
	struct FTacklePairDef { float Ox, Oy, Yaw, EndX, EndY, TacklerLie, CarrierLie; };
	const FTacklePairDef GPairs[FAthleteAssets::PairCount] = {
		{-0.07f, -0.62f, 151.3f, -0.99f, 1.29f, 0.45f, 1.05f},  // Drive: Tackle_D_Win_12_D1 - driven back onto his back, tackler on top
		{0.01f, -0.65f, -77.7f, -1.93f, -2.10f, 1.05f, 0.45f},  // Over: Tackle_O_Win_12_D1 - the tackler goes low, the carrier pitches over him
		{-0.05f, -0.34f, -9.8f, 1.93f, -2.39f, 0.45f, 1.05f},   // Behind: Tackle_Neu_12_D5
		{-0.00f, -0.38f, -58.5f, 1.76f, 0.73f, 0.45f, 1.05f},   // Side: Tackle_Neu_12_D3 (from his right; mirrored from his left)
		{-0.04f, -1.23f, 0.f, 0.28f, -5.08f, 0.70f, 0.90f},     // Dive: Tackle_Dive_Low_from_D5 - shoestring from behind at full speed, he slides
	};
	// clip frame (x left, y back, m) -> actor local (forward, right, cm)
	FVector ClipToActor(float X, float Y) { return FVector(-Y * 100.f, -X * 100.f, 0.f); }
}

void AMatchDirector::StartPairedTackle(AAthlete* Tk, AAthlete* Cr, const FVector& SimSpot, int32 Pair, bool bDive)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!Tk || !Cr || Tk->IsFalling() || Cr->IsFalling()) return;
	const FVector H = Cr->GetActorLocation(); // held where the hit landed
	FVector Fc = Cr->GetLastVel().GetSafeNormal2D();
	if (Fc.IsNearlyZero()) Fc = Cr->GetActorForwardVector();
	const FVector Rc(-Fc.Y, Fc.X, 0.f);       // his right
	const FVector Rel = (Tk->GetActorLocation() - H).GetSafeNormal2D();
	const float Front = FVector::DotProduct(Rel, Fc), Right = FVector::DotProduct(Rel, Rc);
	const float Yac = FVector::DotProduct(SimSpot - H, Fc);
	bool bMirror = false;
	if (Pair < 0)
	{
		if (bDive) Pair = FAthleteAssets::PairDive;
		// head on: driven back onto his back, or falling forward over the tackler - whichever lands nearer the sim's
		// spot, so the pile never slides against the fall
		else if (Front > 0.45f) Pair = FMath::Abs(Yac - 100.f) < FMath::Abs(Yac + 100.f) ? FAthleteAssets::PairOver : FAthleteAssets::PairDrive;
		else if (Front < -0.45f) Pair = FAthleteAssets::PairBehind;
		else Pair = FAthleteAssets::PairSide;
	}
	if (Pair == FAthleteAssets::PairSide) bMirror = Right < 0.f; // the tackler on his left: the mirrored pair
	UAnimationAsset* TClip = A.Side(A.PairT[Pair], bMirror);
	UAnimationAsset* CClip = A.Side(A.PairC[Pair], bMirror);
	// the pack library's two-man tackles of that kind (the placed pair is one entry among them), the fall driven by
	// the captures' root motion from the contact
	FTacklePairDef P = GPairs[Pair];
	float Start = 0.f;
	bool bRoot = false;
	{
		static int32 GPairSeq = 0;
		const int32 Kind = Pair == FAthleteAssets::PairDrive ? 0 : Pair == FAthleteAssets::PairOver ? 1 : Pair == FAthleteAssets::PairBehind ? 2 : Pair == FAthleteAssets::PairDive ? 5 : bMirror ? 4 : 3;
		const TArray<FAthleteAssets::FPackPair>& Lib = A.PackPairs[Kind];
		const int32 i = Lib.Num() ? (GPairSeq++ % (Lib.Num() + 1)) : 0;
		if (i > 0)
		{
			const FAthleteAssets::FPackPair& L = Lib[i - 1];
			TClip = L.T; CClip = L.C;
			P = {L.Ox, L.Oy, L.Yaw, L.EndX, L.EndY, L.TLie, L.CLie};
			Start = L.Start;
			bRoot = true;
			bMirror = false; // a SideL capture is already from his left
		}
	}
	if (!TClip || !CClip) return;
	const float Ox = bMirror ? -P.Ox : P.Ox, YawRel = bMirror ? -P.Yaw : P.Yaw, EndX = bMirror ? -P.EndX : P.EndX;
	const float Yc = Fc.Rotation().Yaw;
	const float Yt = Yc + YawRel;
	// the carrier lies where the clip takes him; the pile drives that onto the sim's spot (within reason)
	const FVector Travel = FRotator(0, Yc, 0).RotateVector(ClipToActor(EndX, P.EndY));
	const FVector Drive = (FVector(SimSpot.X, SimSpot.Y, H.Z) - (H + Travel)).GetClampedToMaxSize2D(150.f);
	// the pair's relation: the tackler where the pair puts him relative to the carrier. The two close the gap between
	// where they are and that relation together (each moves half of it), so neither glides far.
	const FVector Off = FRotator(0, Yt, 0).RotateVector(ClipToActor(Ox, P.Oy));
	FVector Gap = (Tk->GetActorLocation() - (H - Off));
	Gap.Z = 0.f;
	Gap = Gap.GetClampedToMaxSize2D(160.f) * 0.5f;
	const FVector CFrom = H + Gap, CTo = CFrom + FVector(Drive.X, Drive.Y, 0.f);
	const FVector TFrom = CFrom - Off, TTo = CTo - Off;
	const float DriveT = 0.45f;
	const float Jit = FMath::Frac(FPlatformTime::Seconds() * 7.13) * 0.35f; // not every pile gets up on the same beat
	Cr->BeginPairedFall(CClip, CFrom, CTo, Yc, DriveT, P.CarrierLie + Jit, true, Start, bRoot);
	Cr->PileBodyEnd = CTo + Travel; // where his body comes to rest (a second man lands on that)
	Tk->BeginPairedFall(TClip, TFrom, TTo, Yt, DriveT, P.TacklerLie, true, Start, bRoot);
	UE_LOG(LogTemp, Display, TEXT("TAG tackle pair %s%s: #%d on #%d (front %.2f right %.2f, yac %.0f cm, drive %.0f cm, close %.0f cm)"),
		*TClip->GetName(), bMirror ? TEXT(" (mirror)") : TEXT(""), Tk->Player ? Tk->Player->number : -1,
		Cr->Player ? Cr->Player->number : -1, Front, Right, Yac, Drive.Size2D(), FVector::Dist2D(Tk->GetActorLocation(), TFrom));
}

FVector AMatchDirector::HuddleSpot(bool bOff, bool bQB, int32 Slot, int32 Count, const FVector& BallW, int32 InDir, float& OutYaw) const
{
	// the huddle: the offense in a ring 7.5 yd behind the ball (1.3 m radius, the quarterback at the open side
	// nearest the line, facing his men), the defense in a looser ring 5.5 yd in front; everyone faces the middle
	const FVector Down = (ToWorld(1, 0, InDir) - ToWorld(0, 0, InDir)).GetSafeNormal();
	const FVector Left = FVector::CrossProduct(FVector::UpVector, Down);
	const FVector C = BallW + Down * (bOff ? -7.5f : 5.5f) * YD;
	FVector Target;
	if (bOff && bQB) Target = C + Down * 150.f;
	else
	{
		const int32 M = FMath::Max(Count, 1);
		const double Ang = bOff ? PI * 0.5 + PI * (Slot + 0.5) / M : 2 * PI * (Slot + 0.5) / M; // offense: the far half
		Target = C + (Down * FMath::Cos(Ang) + Left * FMath::Sin(Ang)) * (bOff ? 130.f : 190.f);
	}
	Target.Z = 0.f;
	OutYaw = (C - Target).Rotation().Yaw;
	return Target;
}

void AMatchDirector::TickPostJog(float Dt)
{
	// after the whistle (a beat for the tackle and the celebration), everyone on his feet jogs to the huddle, 4.2 yd/s
	// slowing into the last yards; men who are down, celebrating or carrying the ball stay as the sim shows them.
	// While the play's sim is up (post-play, banners) the sim's men are moved director-side (PostPos); once it is gone
	// (the play call) the Between list carries them and this drives the athletes itself.
	auto Step = [Dt](FVector& Pos, const FVector& Target) -> FVector
	{
		FVector To = Target - Pos;
		To.Z = 0.f;
		const float D = To.Size();
		if (D < 30.f) return FVector::ZeroVector;
		const float Sp = FMath::Min(420.f, 120.f + D * 1.2f);
		const FVector V = To / D * Sp;
		Pos += V * FMath::Min(Dt, D / Sp);
		return V;
	};
	if (!Sim)
	{
		if (Between.empty() || !Game) return;
		const int32 NewDir = Game->worldDir();
		int32 NOff = 0, NDef = 0;
		for (const FBetweenMan& M : Between) (M.bOff ? NOff : NDef)++;
		int32 SlotOff = 0, SlotDef = 0;
		for (FBetweenMan& M : Between)
		{
			const bool bQB = M.P->pos == tag::Pos::QB;
			const int32 Slot = M.bOff ? (bQB ? 0 : SlotOff++) : SlotDef++;
			float FaceYaw;
			const FVector Target = HuddleSpot(M.bOff, bQB, Slot, M.bOff ? FMath::Max(NOff - 1, 1) : NDef, BetweenBallW, NewDir, FaceYaw);
			const FVector V = Step(M.Pos, Target);
			AAthlete* A = AthleteFor(M.P, Game->home->hasPlayer(M.P) ? tag::TeamKey::Home : tag::TeamKey::Away);
			if (A->IsDown()) continue;
			const bool bMoving = V.Size() > 60.f;
			if (bMoving) M.Yaw = V.Rotation().Yaw;
			else M.Yaw += FMath::FindDeltaAngleDegrees(M.Yaw, FaceYaw) * FMath::Clamp(Dt * 4.f, 0.f, 1.f);
			FAthleteFootball F;
			F.bLook = true;
			F.LookWorld = bMoving ? M.Pos + V.GetSafeNormal() * 800.f + FVector(0, 0, 150) : BetweenBallW + FVector(0, 0, 120);
			A->SetFootball(F, Dt);
			A->Drive(M.Pos, M.Yaw, V, 9.f, bMoving ? TEXT("run") : TEXT("idle"), 0.f, Dt, false);
		}
		return;
	}
	if (Sim->isLive() || Sim->isReturn() || !Sim->result) return;
	const tag::Ball& B = Sim->ball;
	const FVector BallW = ToWorld(FMath::Clamp(B.pos.x, 2.0, 98.0), FMath::Clamp(B.pos.y, 10.0, tag::FIELD_W - 10.0), Dir);
	for (tag::Ent* E : Sim->ents)
	{
		AAthlete* A = AthleteFor(E->p, E->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		const bool bHolds = B.mode == tag::BallMode::Held && B.holder == E;
		if (A->IsDown() || E->anim == tag::Anim::Celebrate || E->anim == tag::Anim::Down || bHolds)
		{
			PostPos.erase(E);
			continue;
		}
		if (E->animT < 1.0) continue; // the beat after the whistle: look around, then go
		auto It = PostPos.find(E);
		if (It == PostPos.end()) It = PostPos.emplace(E, A->GetActorLocation()).first;
		const bool bOff = E->side == tag::Side::Off;
		const std::vector<tag::Ent*>& SideList = bOff ? Sim->off : Sim->def;
		const int32 N = static_cast<int32>(SideList.size());
		int32 K = static_cast<int32>(std::find(SideList.begin(), SideList.end(), E) - SideList.begin());
		const bool bQB = E->p->pos == tag::Pos::QB;
		if (bOff) for (int32 i = 0; i < K; ++i) if (SideList[i]->p->pos == tag::Pos::QB) --K; // slots skip the QB
		float FaceYaw;
		const FVector Target = HuddleSpot(bOff, bQB, K, bOff ? FMath::Max(N - 1, 1) : N, BallW, Dir, FaceYaw);
		PostYaw[E] = FaceYaw;
		Step(It->second, Target);
	}
}

void AMatchDirector::TickUmpire(float Dt)
{
	if (!Umpire || !Game) return;
	const bool bDead = Sim && !Sim->isLive() && Sim->result && !Sim->isReturn();
	const bool bCall = !Sim && !Between.empty();
	const int32 NewDir = Game->worldDir();
	const FVector Spot = ToWorld(Game->ballOn, Game->ballY, NewDir); // the next play's spot (after ApplyPlay)
	const FVector DownNew = (ToWorld(1, 0, NewDir) - ToWorld(0, 0, NewDir)).GetSafeNormal();
	const FVector Station = Spot - DownNew * 9.f * YD + FVector::CrossProduct(FVector::UpVector, DownNew) * 3.5f * YD; // 9 yd behind the ball
	FVector Target = UmpPos;
	float Jog = 380.f;
	FVector BallW = bCall ? BetweenBallW : (Sim ? ToWorld(Sim->ball.pos.x, Sim->ball.pos.y, Dir) : Spot);
	if (Sim && Sim->isReturn()) Target = ToWorld(FMath::Clamp(Game->ballOn, 5.0, 95.0), -2.5, Dir); // off the field for the kick
	else if (State == EMatchState::Live) Jog = 0.f;                                               // holds his position through the play
	else if (bDead && State == EMatchState::Post)
	{
		// at the whistle, to the dead ball
		Target = BallW;
		if (!bUmpHasBall && !bUmpSpotted && FVector::Dist2D(UmpPos, BallW) < 75.f) bUmpHasBall = true;
	}
	else if ((bDead && (State == EMatchState::Wait || State == EMatchState::Call)) || bCall)
	{
		if (!bUmpHasBall && !bUmpSpotted)
		{
			Target = BallW;
			if (FVector::Dist2D(UmpPos, BallW) < 75.f) bUmpHasBall = true;
		}
		else if (bUmpHasBall)
		{
			// carry it to the new spot and set it down
			Target = Spot;
			if (FVector::Dist2D(UmpPos, Spot) < 60.f) { bUmpHasBall = false; bUmpSpotted = true; }
		}
		else Target = Station;
	}
	else if (Sim)
	{
		// before the snap: 9 yd behind the ball in the offensive backfield, off the quarterback's line
		const FVector Down = (ToWorld(1, 0, Dir) - ToWorld(0, 0, Dir)).GetSafeNormal();
		Target = ToWorld(Sim->los, Sim->by, Dir) - Down * 9.f * YD + FVector::CrossProduct(FVector::UpVector, Down) * 3.5f * YD;
	}
	FVector To = Target - UmpPos;
	To.Z = 0.f;
	const float D = To.Size();
	FVector Vel = FVector::ZeroVector;
	if (Jog > 0.f && D > 25.f)
	{
		const float Sp = FMath::Min(Jog, 110.f + D * 1.3f);
		Vel = To / D * Sp;
		UmpPos += Vel * FMath::Min(Dt, D / Sp);
	}
	// facing: the way he goes, or the ball when he stands
	float Yaw = Umpire->GetActorRotation().Yaw;
	if (!Vel.IsNearlyZero()) Yaw = Vel.Rotation().Yaw;
	else
	{
		const FVector At = bUmpSpotted || bCall ? Spot : BallW;
		FVector F = At - UmpPos;
		F.Z = 0.f;
		if (F.Size() > 30.f) Yaw = F.Rotation().Yaw;
	}
	FAthleteFootball F;
	F.bLook = true;
	F.LookWorld = bUmpHasBall ? UmpPos + Vel.GetSafeNormal() * 800.f + FVector(0, 0, 150) : (bUmpSpotted || bCall ? Spot : BallW) + FVector(0, 0, 20);
	Umpire->SetFootball(F, Dt);
	Umpire->Drive(UmpPos, Yaw, Vel, 7.f, Vel.IsNearlyZero() ? TEXT("idle") : TEXT("run"), 0.f, Dt, false);
	// without a sim nothing else places the ball: in his hand, or spotted for the next play
	if (!Sim && Ball)
	{
		FVector HoldLoc;
		FRotator HoldRot;
		if (bUmpHasBall && Umpire->GetBallHold(HoldLoc, HoldRot)) { Ball->SetActorLocation(HoldLoc); Ball->SetActorRotation(HoldRot); }
		else if (bUmpSpotted) { Ball->SetActorLocation(Spot + FVector(0, 0, 9)); Ball->SetActorRotation(FRotator(0, NewDir > 0 ? 0 : 180, 0)); }
	}
}

void AMatchDirector::StartPileOn(AAthlete* As, AAthlete* Cr)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!As || !Cr || As->IsFalling() || !A.PileOn) return;
	// he lands across the pile: chest over the carrier's body (the carrier's anchor end, pelvis a little past it)
	const FVector Pile = Cr->IsFalling() ? Cr->PileBodyEnd : Cr->GetActorLocation();
	FVector ToPile = Pile - As->GetActorLocation();
	ToPile.Z = 0.f;
	if (!ToPile.Normalize()) ToPile = As->GetActorForwardVector();
	const FVector At = Pile - ToPile * 165.f; // the pile-on clip lands his chest 1.65 m ahead of where it starts
	As->BeginPairedFall(A.PileOn, At, At, ToPile.Rotation().Yaw, 0.3f, 0.25f, true);
}

void AMatchDirector::TickTackleTest(float Dt)
{
	// a back running at 7 m/s along +X; the tackler arrives from the rep's angle. Reps (5 s): 0 head on (no gain),
	// 1 head on (falls forward for yards), 2 from behind, 3 from his right, 4 from his left, 5 shoestring dive,
	// 6 head on with a second man piling on
	if (Lineup.Num() < 5) return;
	const float T = LineupT;
	AAthlete* Cr = Lineup[1];
	AAthlete* Tk = Lineup[3];
	AAthlete* As = Lineup[4];
	const int32 Rep = static_cast<int32>(T / 5.f);
	const float R = T - Rep * 5.f;
	if (Rep > 6)
	{
		FPlatformMisc::RequestExit(false);
		return;
	}
	static int32 LastRep = -1;
	static bool bHit = false, bPile = false;
	if (Rep != LastRep)
	{
		LastRep = Rep;
		bHit = bPile = false;
		Cr->ResetPose();
		Tk->ResetPose();
		As->ResetPose();
	}
	for (int32 i = 0; i < Lineup.Num(); ++i) Lineup[i]->SetActorHiddenInGame(!(i == 1 || i == 3 || (i == 4 && Rep == 6)));
	const FVector O = TagCoords::ToWorld(40, tag::FIELD_W * 0.5, 1);
	const float HitT = 1.2f, RunV = 700.f;
	const float Run = FMath::Min(R, HitT);
	const FVector CrP = O + FVector(-RunV * (HitT - Run), 0.f, 0.f);
	// where the tackler comes from (relative to the hit point), and how fast
	FVector From(260.f, 0.f, 0.f);
	if (Rep == 2) From = FVector(-260.f, 0.f, 0.f);
	else if (Rep == 3) From = FVector(40.f, 300.f, 0.f);
	else if (Rep == 4) From = FVector(40.f, -300.f, 0.f);
	else if (Rep == 5) From = FVector(-320.f, 60.f, 0.f);
	const float Approach = FMath::Clamp(R / HitT, 0.f, 1.f);
	const FVector TkP = O + From * (1.f - Approach) * 1.6f + From.GetSafeNormal() * 55.f;
	const FVector TkV = R < HitT ? -From.GetSafeNormal() * 600.f : FVector::ZeroVector;
	auto Foot = [&](AAthlete* Who) { FAthleteFootball F; F.bHasBall = Who == Cr; Who->SetFootball(F, Dt); };
	Foot(Cr);
	Foot(Tk);
	Foot(As);
	if (!bHit && R >= HitT)
	{
		bHit = true;
		const FVector Spot = O + FVector(Rep == 1 ? 230.f : Rep == 0 || Rep == 6 ? -40.f : 120.f, 0.f, 0.f);
		const int32 Pairs[] = {FAthleteAssets::PairDrive, FAthleteAssets::PairOver, FAthleteAssets::PairBehind,
			FAthleteAssets::PairSide, FAthleteAssets::PairSide, FAthleteAssets::PairDive, FAthleteAssets::PairDrive};
		StartPairedTackle(Tk, Cr, Spot, Pairs[Rep], Rep == 5);
	}
	if (Rep == 6 && bHit && !bPile && R >= HitT + 0.25f)
	{
		bPile = true;
		StartPileOn(As, Cr);
	}
	// sim-side positions (the falls own the bodies once they start)
	const FVector Spot = O;
	Cr->Drive(R < HitT ? CrP : Spot, 0.f, R < HitT ? FVector(RunV, 0, 0) : FVector::ZeroVector, 9.f, R < HitT ? TEXT("run") : TEXT("down"), R, Dt, false);
	Tk->Drive(R < HitT ? TkP : O + From.GetSafeNormal() * 60.f, TkV.IsNearlyZero() ? (-From).Rotation().Yaw : TkV.Rotation().Yaw, TkV, 9.f,
		R < HitT ? TEXT("run") : TEXT("tackle"), R, Dt, false);
	if (Rep == 6)
	{
		const FVector AsFrom = O + FVector(300.f, -220.f, 0.f);
		const FVector AsP = R < HitT + 0.25f ? FMath::Lerp(AsFrom + FVector(400.f, -300.f, 0.f), AsFrom, FMath::Clamp(R / (HitT + 0.25f), 0.f, 1.f)) : AsFrom;
		As->Drive(AsP, (O - AsFrom).Rotation().Yaw, R < HitT + 0.25f ? (AsFrom - (AsFrom + FVector(400.f, -300.f, 0.f))) : FVector::ZeroVector, 9.f,
			TEXT("run"), R, Dt, false);
	}
	// side on from the field side, low, 30 mm
	const FVector At = O + FVector(60.f, -720.f, 170.f), Look = O + FVector(60.f, 0.f, 60.f);
	Camera->SetActorLocation(At);
	Camera->SetActorRotation((Look - At).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 30.f;
		C->FocusSettings.ManualFocusDistance = (Look - At).Size();
	}
	static int32 Shot = 0;
	if (T >= 0.1f * Shot && Shot < 360)
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_tackletest_r%d_%03d.png"), Rep, FMath::RoundToInt(R * 100.f)), false, false), ++Shot;
}

void AMatchDirector::TickLineTest(float Dt)
{
	// a right tackle (home, +X) against the end over him (away, -X), filmed side on. Each 4 s rep: stance, get-off and
	// the punch at contact, the hand fight, then the rep's outcome:
	//   0 bull rush: the end walks him into the pocket (bull drive / anchor), finishes with the long arm
	//   1 run block won: drive / driven back, pancake (the end goes down, the tackle finishes over him)
	//   2 speed rush off the edge: dip and rip, the tackle beaten on the outside
	//   3 swim inside   4 spin   5 club   6 rip
	if (Lineup.Num() < 2) return;
	const float T = LineupT;
	AAthlete* OL = Lineup[0];
	AAthlete* DL = Lineup[1];
	for (int32 i = 0; i < Lineup.Num(); ++i) Lineup[i]->SetActorHiddenInGame(i > 1);
	const int32 Rep = static_cast<int32>(T / 4.f);
	const float R = T - Rep * 4.f;
	static const TCHAR* Moves[] = {TEXT("longarm"), TEXT(""), TEXT("diprip"), TEXT("swim"), TEXT("spin"), TEXT("club"), TEXT("rip")};
	if (Rep > 6)
	{
		FPlatformMisc::RequestExit(false);
		return;
	}
	static int32 LastRep = -1;
	static bool bResolved = false;
	if (Rep != LastRep)
	{
		LastRep = Rep;
		bResolved = false;
		OL->ResetPose();
		DL->ResetPose();
	}
	const FVector O = TagCoords::ToWorld(40, tag::FIELD_W * 0.5, 1);
	const bool bRun = Rep == 1;
	const float Wide = Rep == 2 ? 70.f : 0.f; // the speed rusher lines up wide of the tackle
	const float Snap = 0.8f, Contact = 1.15f, Resolve = 2.6f;
	// pile travel: toward the quarterback (-X) on the bull rush, off the ball (+X) on the run block
	const float PileV = Rep == 0 ? -110.f : bRun ? 150.f : 0.f;
	const float Fought = FMath::Clamp(R - (Contact + 0.6f), 0.f, Resolve - Contact - 0.6f);
	const FVector Pile = O + FVector(PileV * Fought, 0.f, 0.f);
	FVector OLp = Pile + FVector(-55.f, 0.f, 0.f), DLp = Pile + FVector(55.f, Wide, 0.f);
	FVector OLv = FVector(PileV, 0, 0), DLv = OLv;
	FString OLa = TEXT("block"), DLa = TEXT("block");
	bool bEngaged = R >= Contact && R < Resolve;
	if (R < Snap)
	{
		OLp = O + FVector(-125.f, 0, 0);
		DLp = O + FVector(125.f, Wide, 0);
		OLv = DLv = FVector::ZeroVector;
		OLa = DLa = TEXT("idle");
	}
	else if (R < Contact)
	{
		const float S = (R - Snap) / (Contact - Snap);
		OLp = FMath::Lerp(O + FVector(-125.f, 0, 0), O + FVector(-55.f, 0, 0), S);  // pass set / fire out
		DLp = FMath::Lerp(O + FVector(125.f, Wide, 0), O + FVector(55.f, Wide, 0), S);
		OLv = FVector(bRun ? 80.f : -60.f, 0, 0);
		DLv = FVector(-80.f, 0, 0);
	}
	else if (R >= Resolve)
	{
		const float After = R - Resolve;
		if (!bResolved)
		{
			bResolved = true;
			if (bRun)
			{
				OL->PlayPancakeFinish();
				DL->SetPancaked(FVector(1, 0, 0));
				UE_LOG(LogTemp, Display, TEXT("TAG line test rep %d: pancake"), Rep);
			}
			else
			{
				DL->PlayRushMove(Moves[Rep], OL);
				OL->PlayBeaten(DL);
				UE_LOG(LogTemp, Display, TEXT("TAG line test rep %d: %s"), Rep, Moves[Rep]);
			}
		}
		const FVector End = Pile;
		OLp = End + FVector(-55.f, 0.f, 0.f);
		OLv = FVector::ZeroVector;
		OLa = TEXT("idle");
		if (bRun)
		{
			DLp = End + FVector(60.f + 40.f * FMath::Min(After, 0.5f), 0.f, 0.f);
			DLv = FVector(120.f, 0, 0);
			DLa = TEXT("down");
		}
		else
		{
			DLp = End + FVector(48.f - 380.f * After, Wide + (Rep == 3 || Rep == 6 ? -90.f : 90.f) * FMath::Min(After, 0.6f), 0.f);
			DLv = FVector(-380.f, 0, 0);
			DLa = TEXT("run");
		}
	}
	// hand fight while engaged; the rep's winner after the punch has settled
	const float Push = !bEngaged || R < Contact + 0.6f ? 0.f : (Rep == 0 ? 0.85f : bRun ? -0.85f : 0.f);
	OL->SetBlockPartner(bEngaged ? DL : nullptr);
	DL->SetBlockPartner(bEngaged ? OL : nullptr);
	OL->SetLinePush(Push, false, !bRun);
	DL->SetLinePush(Push, true, !bRun);
	auto Feed = [&](AAthlete* Me, AAthlete* Him)
	{
		FAthleteFootball F;
		F.bPreSnap = R < Snap;
		if (bEngaged)
		{
			const FVector Chest = Him->BoneWorld(TEXT("spine_04"));
			F.bReach = true;
			F.ReachWorld = Chest + (Me->GetActorLocation() - Chest).GetSafeNormal2D() * 16.f;
			F.ReachSpread = 13.f;
			F.Lean = 0.9f;
			F.bLook = true;
			F.LookWorld = Him->BoneWorld(TEXT("head"));
		}
		Me->SetFootball(F, Dt);
	};
	Feed(OL, DL);
	Feed(DL, OL);
	OL->Drive(OLp, 0.f, OLv, 7.f, OLa, R, Dt, true);
	DL->Drive(DLp, 180.f, DLv, 7.f, DLa, R, Dt, true);
	// side on from the offense's right, low, a 35 mm lens on the pair
	const FVector At = O + FVector(0.f, -560.f, 150.f), Look = O + FVector(0.f, 0.f, 95.f);
	Camera->SetActorLocation(At);
	Camera->SetActorRotation((Look - At).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->CurrentFocalLength = 35.f;
		C->FocusSettings.ManualFocusDistance = (Look - At).Size();
	}
	static int32 Shot = 0;
	if (T >= 0.2f * Shot && Shot < 150)
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_linetest_%03d.png"), Shot++), false, false);
}

void AMatchDirector::TickGapTest(float Dt)
{
	if (Lineup.Num() < 5) return;
	const float T = LineupT;
	AAthlete* QB = Lineup[0];
	AAthlete* RB = Lineup[1];
	AAthlete* WR = Lineup[2];
	AAthlete* LB = Lineup[3];
	AAthlete* CB = Lineup[4];
	const FVector O = TagCoords::ToWorld(40, tag::FIELD_W * 0.5, 1); // the spot, offense going +X (yaw 0)
	auto Show = [&](std::initializer_list<AAthlete*> Who)
	{
		for (AAthlete* A : Lineup) A->SetActorHiddenInGame(true);
		for (AAthlete* A : Who) A->SetActorHiddenInGame(false);
	};
	auto Foot = [&](AAthlete* A, bool bPre, bool bUnder)
	{
		FAthleteFootball F;
		F.bPreSnap = bPre;
		F.bUnderCenter = bUnder;
		A->SetFootball(F, Dt);
	};
	auto Cam = [&](const FVector& At, const FVector& Look, float Focal)
	{
		Camera->SetActorLocation(At);
		Camera->SetActorRotation((Look - At).Rotation());
		if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
		{
			C->CurrentFocalLength = Focal;
			C->FocusSettings.ManualFocusDistance = (Look - At).Size();
		}
	};
	static bool bSnap = false, bPitch = false, bDive = false, bTackle = false;
	if (T < 6.f)
	{
		// 1) under center, the snap, the toss to the back sweeping right (-Y)
		Show({QB, RB});
		const bool bPre = T < 1.5f;
		Foot(QB, bPre, true);
		Foot(RB, bPre, false);
		QB->Drive(O, 0.f, FVector::ZeroVector, 9.f, TEXT("idle"), T, Dt, false);
		if (!bPre && !bPitch && T > 2.2f) { QB->PlayOneShot(FAthleteAssets::Get().Pitch); bPitch = true; }
		const float Rt = FMath::Max(0.f, T - 1.6f);
		const FVector RbV = Rt > 0.f ? FVector(Rt > 1.6f ? 450.f : 120.f, -650.f, 0.f) : FVector::ZeroVector;
		const FVector RbP = O + FVector(-450.f, -60.f, 0.f) + FVector(Rt > 1.6f ? 120.f * 1.6f + 450.f * (Rt - 1.6f) : 120.f * Rt, -650.f * Rt, 0.f);
		RB->Drive(RbP, RbV.IsNearlyZero() ? 0.f : RbV.Rotation().Yaw, RbV, 9.f, RbV.IsNearlyZero() ? TEXT("idle") : TEXT("run"), T, Dt, false);
		(void)bSnap;
		Cam(O + FVector(-700.f, 420.f, 230.f), O + FVector(-120.f, -120.f, 100.f), 30.f);
	}
	else if (T < 11.f)
	{
		// 2) the layout catch: a receiver at full speed, the ball just out of reach
		Show({WR});
		Foot(WR, false, false);
		const float Wt = T - 6.f;
		const float StopT = 2.4f;
		const float Run = FMath::Min(Wt, StopT);
		const FVector P = O + FVector(-1200.f + 800.f * Run, 900.f, 0.f);
		if (!bDive && Wt >= StopT) { WR->PlayDiveAttempt(); bDive = true; }
		const FVector V = Wt < StopT ? FVector(800.f, 0.f, 0.f) : FVector::ZeroVector;
		WR->Drive(P, 0.f, V, 9.f, Wt < StopT ? TEXT("run") : TEXT("catch"), Wt, Dt, false);
		Cam(P + FVector(250.f, -650.f, 140.f), P + FVector(150.f, 0.f, 70.f), 30.f);
	}
	else if (T >= 22.f && T < 38.f)
	{
		// 4) the throwing family, one every 2 s: quick, deep, fade, sidearm, touch (feet set), then on the run both ways
		Show({QB});
		Foot(QB, false, false);
		const float Tt = T - 22.f;
		const int32 Kinds[] = {FAthleteAssets::ThrowQuickK, FAthleteAssets::ThrowDeepK, FAthleteAssets::ThrowFadeK,
			FAthleteAssets::ThrowSidearmK, FAthleteAssets::ThrowTouchK, FAthleteAssets::ThrowRunRK, FAthleteAssets::ThrowRunLK};
		const int32 Idx = FMath::Clamp(static_cast<int32>(Tt / 2.f), 0, 6);
		static int32 LastThrow = -1;
		const bool bRun = Kinds[Idx] == FAthleteAssets::ThrowRunRK || Kinds[Idx] == FAthleteAssets::ThrowRunLK;
		const FVector V = bRun ? FVector(400.f, 0.f, 0.f) : FVector::ZeroVector;
		QB->Drive(O + FVector(-600.f, 0.f, 0.f), 0.f, V, 9.f, TEXT("throw"), Tt, Dt, false);
		if (Idx != LastThrow)
		{
			LastThrow = Idx;
			QB->PlayThrow(Kinds[Idx]);
			UE_LOG(LogTemp, Display, TEXT("TAG gap test: throw kind %d"), Kinds[Idx]);
		}
		const FVector At = O + FVector(-600.f, 0.f, 0.f);
		Cam(At + FVector(250.f, -520.f, 160.f), At + FVector(0.f, 0.f, 120.f), 30.f);
	}
	else if (T >= 51.5f)
	{
		// 6) the quarterback's drops, filmed from the side: under center for a beat, the five step drop at 4 m/s and
		//    the pocket; then the shotgun three step at 2.75 m/s and the pocket again
		Show({QB});
		const float Qt = T - 51.5f;
		const bool bUnderPhase = Qt < 4.5f;
		const float Lt = Qt - (bUnderPhase ? 0.f : 4.5f);
		const bool bPre = Lt < 0.5f;
		const float DropLen = bUnderPhase ? 1.5f : 0.66f, Sp = bUnderPhase ? 400.f : 275.f;
		const float Run = FMath::Clamp(Lt - 0.5f, 0.f, DropLen);
		const FVector Start = O + FVector(bUnderPhase ? 0.f : -500.f, 200.f, 0.f);
		const FVector P = Start - FVector(Sp * Run, 0.f, 0.f);
		const FVector V = (Lt >= 0.5f && Lt < 0.5f + DropLen) ? FVector(-Sp, 0.f, 0.f) : FVector::ZeroVector;
		FAthleteFootball F;
		F.bPreSnap = bPre;
		F.bUnderCenter = bUnderPhase;
		F.bHasBall = !bPre;
		F.bLive = !bPre;
		F.bLook = true;
		F.LookWorld = P + FVector(1500.f, 0.f, 160.f);
		QB->SetFootball(F, Dt);
		QB->Drive(P, 0.f, V, 9.f, V.IsNearlyZero() ? TEXT("idle") : TEXT("run"), Lt, Dt, false);
		Cam(P + FVector(-40.f, -640.f, 160.f), P + FVector(-60.f, 0.f, 100.f), 30.f);
	}
	else if (T >= 38.f)
	{
		// 5) the catching family, one every 2.2 s, each in its situation: in stride, back shoulder, one hand, scoop at
		//    speed; the high point jump ball and the comeback from a stand
		Show({WR});
		Foot(WR, false, false);
		const float Ct = T - 38.f;
		const FAthleteAssets& AA = FAthleteAssets::Get();
		UAnimationAsset* Catches[] = {AA.CatchInStride.Get(), AA.CatchBackShoulder.Get(), AA.CatchOneHand.Get(), AA.CatchScoop.Get(),
			AA.CatchHighPoint.Get(), AA.CatchComeback.Get()};
		const int32 Idx = FMath::Clamp(static_cast<int32>(Ct / 2.2f), 0, 5);
		static int32 LastCatch = -1;
		const bool bRun = Idx < 4;
		const FVector V = bRun ? FVector(650.f, 0.f, 0.f) : FVector::ZeroVector;
		const FVector At = O + FVector(-800.f, 700.f, 0.f);
		WR->Drive(At, 0.f, V, 9.f, bRun ? TEXT("run") : TEXT("catch"), Ct, Dt, false);
		if (Idx != LastCatch && Catches[Idx])
		{
			LastCatch = Idx;
			WR->PlayOneShot(Catches[Idx]);
		}
		Cam(At + FVector(260.f, -560.f, 150.f), At + FVector(0.f, 0.f, 110.f), 30.f);
	}
	else
	{
		// 3) gang tackle: the linebacker squares up and wraps, the corner comes in second, they go down in a pile,
		//    the back is hurt and stays down, then gets up slowly
		Show({RB, LB, CB});
		Foot(RB, false, false);
		Foot(LB, false, false);
		Foot(CB, false, false);
		const float Kt = T - 11.f;
		const float HitT = 1.8f;
		static FVector RbAt = FVector::ZeroVector;
		if (Kt < HitT)
		{
			RB->bForceInjury = true;
			RbAt = O + FVector(-900.f + 700.f * Kt, -300.f, 0.f);
			RB->Drive(RbAt, 0.f, FVector(700.f, 0.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
			const FVector LbAt = O + FVector(900.f - 300.f * Kt, -360.f, 0.f);
			LB->Drive(LbAt, 180.f, FVector(-300.f, 0.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
			// the corner closes from the sideline to arrive just behind the linebacker, a beat later
			const FVector CbTgt = O + FVector(-900.f + 700.f * HitT + 160.f, -300.f + 110.f, 0.f);
			const FVector CbAt = CbTgt + FVector(0.f, 420.f * (HitT + 0.25f - Kt), 0.f);
			CB->Drive(CbAt, -90.f, FVector(0.f, -420.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
		}
		else
		{
			// the sim resolves the tackle in one tick and moves the carrier forward by the yards after contact
			const FVector Spot = RbAt + FVector(140.f, 0.f, 0.f);
			RB->Drive(Spot, 0.f, FVector::ZeroVector, 9.f, TEXT("down"), Kt - HitT, Dt, false);
			if (!bTackle) { LB->BeginTackleOn(RB); bTackle = true; }
			LB->Drive(O + FVector(900.f - 300.f * HitT, -360.f, 0.f), 180.f, FVector::ZeroVector, 9.f, TEXT("tackle"), Kt - HitT, Dt, false);
			const float Ct = Kt - HitT - 0.25f;
			const FVector CbTgt = O + FVector(-900.f + 700.f * HitT + 160.f, -300.f + 110.f, 0.f);
			const FVector CbAt = CbTgt + FVector(0.f, 420.f * FMath::Max(0.f, HitT + 0.25f - Kt), 0.f);
			if (Ct < 0.f) CB->Drive(CbAt, -90.f, FVector(0.f, -420.f, 0.f), 9.f, TEXT("run"), Kt, Dt, false);
			else
			{
				if (Ct < Dt * 1.5f) CB->BeginTackleOn(RB);
				CB->Drive(CbAt, -90.f, FVector::ZeroVector, 9.f, TEXT("tackle"), Ct, Dt, false);
			}
		}
		Cam(RbAt + FVector(100.f, -900.f, 170.f), RbAt + FVector(160.f, 0.f, 60.f), 32.f);
	}
	const float ShotT = 0.33f * GapShot;
	if (T >= ShotT && GapShot < 200)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_gap_%02d.png"), GapShot), false, false);
		++GapShot;
	}
	PerfT += Dt; ++PerfFrames;
	if (T > 61.f)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		FPlatformMisc::RequestExit(false);
	}
}

static bool GHudShot = false;  // automation: one screenshot with the UI
static bool GAutoBoard = false;
static bool GAutoChains = false;
static bool GAutoCoach = false; // automation: the establishing shot turns to the end-zone video board

void AMatchDirector::TickAutomation(float Dt)
{
	if (!bAuto) return;
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return; // wait for shaders
#endif
	if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0) return; // and textures/meshes
	AutoT += Dt;
	if (GHudShot && !FScreenshotRequest::IsScreenshotRequested())
	{
		GHudShot = false;
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_hud.png"), true, false);
	}
	static const float Times[] = {3.f, 9.f, 14.f, 20.f, 27.f, 34.f, 42.f, 50.f};
	if (AutoShot < static_cast<int32>(UE_ARRAY_COUNT(Times)) && AutoT >= Times[AutoShot])
	{
		if (AutoShot == 4) CamMode = ECamMode::Sideline;
		if (AutoShot == 6) CamMode = ECamMode::Skycam;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_auto_%02d.png"), AutoShot), false, false);
		if (AutoShot == 3) GHudShot = true; // next frame: the same view with the HUD (score bug, hints)
		int32 NLog = 0;
		for (auto& KV : Athletes)
		{
			if (!KV.Value->IsHidden() && NLog++ < 3) UE_LOG(LogTemp, Display, TEXT("TAG athlete %d: %s"), KV.Key, *KV.Value->DebugAnimState());
		}
		UE_LOG(LogTemp, Display, TEXT("TAG auto screenshot %d state=%d cam %s look %s focus %s dir %d ballOn %.1f"), AutoShot, static_cast<int32>(State),
			*Camera->GetActorLocation().ToCompactString(), *CamLook.ToCompactString(), *FocusPoint().ToCompactString(), Dir, Game ? Game->ballOn : -1.0);
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
	// then the end-zone video board above the stands (verifies the bowl's end and the live score)
	if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 1 && AutoT > 53.9f && AutoT < 55.f && !GAutoBoard)
		GAutoBoard = true;
	if (GAutoBoard && !GAutoChains && AutoT >= 54.6f && AutoT < 55.f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_board.png"), false, false);
		GAutoChains = true; // then the chain crew and the down box on the far sideline
		AutoT = 54.0f;
	}
	if (GAutoChains && !GAutoCoach && AutoT >= 54.6f && AutoT < 55.f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_chains.png"), false, false);
		GAutoCoach = true; // then the home head coach in the coaching box
		AutoT = 54.0f;
	}
	if (GAutoCoach && AutoT >= 54.6f && AutoT < 55.f)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_coach.png"), false, false);
		AutoT = 55.f;
	}
	// close-up of one athlete (verifies character, uniform and equipment fit)
	if (AutoShot == static_cast<int32>(UE_ARRAY_COUNT(Times)) + 1 && AutoT > 55.f)
	{
		bAutoEstablish = false;
		GAutoBoard = false;
		GAutoChains = false;
		GAutoCoach = false;
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
	PerfT += Dt; ++PerfFrames;
	static const bool bLineCamAuto = FParse::Param(FCommandLine::Get(), TEXT("TagLineCam"));
	if (AutoT > (bLineCamAuto ? 240.f : 63.5f))
	{
		UE_LOG(LogTemp, Display, TEXT("TAG perf avg %.1f fps over %d frames"), PerfFrames / FMath::Max(PerfT, 0.001f), PerfFrames);
		FPlatformMisc::RequestExit(false);
	}
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
	SpawnSidelines(Home, Away);
}

static FUniform UniformFor(const tag::Team& T, bool bHome)
{
	FUniform U;
	if (bHome)
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
	return U;
}

void AMatchDirector::SpawnSidelines(const tag::Team& Home, const tag::Team& Away)
{
	for (AAthlete* A : Sideline)
		if (A) A->Destroy();
	Sideline.Empty();
	for (AAthlete* A : Officials)
		if (A) A->Destroy();
	Officials.Empty();
	SidelineIsHome.Empty();
	SidelineScore[0] = SidelineScore[1] = 0;
	SidelineCheerLeft[0] = SidelineCheerLeft[1] = 0.f;
	static int32 PerTeam = 14;
	FParse::Value(FCommandLine::Get(), TEXT("TagSideline="), PerTeam); // 0 turns the benches off
	if (PerTeam <= 0 || bLineup) return;
	// chain crew (orange shirts, black trousers) on the rods and the down box, and a head coach in each team's colours
	{
		OfficialPeople.clear();
		auto Staff = [&](int32 Id, double HeightIn, double WeightLb, const FLinearColor& Top, const FLinearColor& Bottom)
		{
			auto P = std::make_unique<tag::Player>();
			P->id = 900000 + Id;
			P->pos = tag::Pos::WR;
			P->heightIn = HeightIn;
			P->weightLb = WeightLb;
			P->number = 0;
			FUniform U{Top, Bottom, Top, Top, Top}; // number painted in the shirt colour: none shows
			AAthlete* A = GetWorld()->SpawnActor<AAthlete>(AAthlete::StaticClass(), FTransform::Identity);
			A->TickAfter(this);
			A->Setup(P.get(), U);
			A->SetCivilian();
			OfficialPeople.push_back(std::move(P));
			Officials.Add(A);
			return A;
		};
		const FLinearColor Orange(0.9f, 0.28f, 0.02f), Black(0.03f, 0.03f, 0.035f), Khaki(0.55f, 0.47f, 0.33f);
		for (int32 k = 0; k < 3; ++k)
			if (USceneComponent* Post = Stadium ? Stadium->CrewPost(k) : nullptr)
			{
				AAthlete* A = Staff(k, 70 + k * 2, 185 + k * 15, Orange, Black);
				A->StandOnSideline(Post->GetComponentTransform().TransformPosition(FVector(-40, 14, 0)), -90.f, 3 * k);
				A->AttachToComponent(Post, FAttachmentTransformRules::KeepWorldTransform);
			}
		AAthlete* HC = Staff(5, 72, 210, Hex(Home.colors.primary) * 0.8f, Khaki);
		HC->StandOnSideline(FVector(46 * YD, (tag::FIELD_W / 2 + 1.7) * YD, 0), -90.f, 1);
		AAthlete* AC = Staff(6, 70, 200, Hex(Away.colors.primary) * 0.8f, Khaki);
		AC->StandOnSideline(FVector(54 * YD, -(tag::FIELD_W / 2 + 1.7) * YD, 0), 90.f, 4);
		// the umpire (white cap, striped shirt reads as white at distance) works from the offensive backfield
		Umpire = Staff(7, 71, 195, FLinearColor(0.85f, 0.85f, 0.85f), Black);
		UmpPos = FVector(50 * YD, 8 * YD, 0);
		Umpire->StandOnSideline(UmpPos, -90.f, 2);
	}
	// the team areas: home on the far sideline (the broadcast side looks at it), visitors on the near one, each between
	// the 32s, standing in two loose rows just outside the 6 ft border in front of the bench
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const bool bHome = Team == 0;
		const tag::Team& T = bHome ? Home : Away;
		std::vector<tag::Player*> Bench;
		for (int32 Depth = 2; Depth < 6 && static_cast<int32>(Bench.size()) < PerTeam; ++Depth)
			for (int32 Pi = 0; Pi <= static_cast<int32>(tag::Pos::P) && static_cast<int32>(Bench.size()) < PerTeam; ++Pi)
			{
				const std::vector<tag::Player*> D = tag::depth(T, static_cast<tag::Pos>(Pi));
				if (Depth < static_cast<int32>(D.size())) Bench.push_back(D[Depth]);
			}
		const double SideSign = bHome ? 1.0 : -1.0;
		FRandomStream Rand(T.id * 31 + 7);
		const FUniform U = UniformFor(T, bHome);
		for (int32 i = 0; i < static_cast<int32>(Bench.size()); ++i)
		{
			const int32 Row = i % 3 == 2 ? 1 : 0;
			const double X = (33.0 + 34.0 * (i + Rand.FRandRange(-0.3f, 0.3f)) / FMath::Max(1, static_cast<int32>(Bench.size()) - 1)) * YD;
			const double Y = SideSign * (tag::FIELD_W / 2 + 2.4 + Row * 1.3 + Rand.FRandRange(-0.25f, 0.25f)) * YD;
			float Yaw = SideSign > 0 ? -90.f : 90.f;            // facing the field
			Yaw += Rand.FRandRange(-28.f, 28.f) + (i % 5 == 4 ? (Rand.FRand() < 0.5f ? 70.f : -70.f) : 0.f); // some talk to a teammate
			AAthlete* A = GetWorld()->SpawnActor<AAthlete>(AAthlete::StaticClass(), FTransform::Identity);
			A->TickAfter(this);
			A->Setup(Bench[i], U);
			A->StandOnSideline(FVector(X, Y, 0), Yaw, i + T.id * 5);
			Sideline.Add(A);
			SidelineIsHome.Add(bHome);
		}
	}
}

void AMatchDirector::TickSidelines(float Dt)
{
	if (!Game || Sideline.Num() == 0) return;
	// a score sets the scoring team's bench off for a few seconds (longer for a touchdown)
	const int32 Now[2] = {Game->score.home, Game->score.away};
	for (int32 k = 0; k < 2; ++k)
	{
		if (Now[k] > SidelineScore[k])
		{
			SidelineCheerLeft[k] = Now[k] - SidelineScore[k] >= 6 ? 7.f : 4.f;
			for (int32 i = 0; i < Sideline.Num(); ++i)
				if (Sideline[i] && SidelineIsHome[i] == (k == 0)) Sideline[i]->SidelineCheer(true);
		}
		else if (Now[k] < SidelineScore[k])
			SidelineCheerLeft[k] = 0.f; // new game
		SidelineScore[k] = Now[k];
		if (SidelineCheerLeft[k] > 0.f && (SidelineCheerLeft[k] -= Dt) <= 0.f)
			for (int32 i = 0; i < Sideline.Num(); ++i)
				if (Sideline[i] && SidelineIsHome[i] == (k == 0)) Sideline[i]->SidelineCheer(false);
	}
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
		const tag::SpecialResult R = G.kickoff(false, true);
		if (G.pendingReturn) return StartLiveReturn(KickU, RecvU);
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
	// the play's sim goes, the men stay where the whistle (and the jog since) left them, in the huddle or on the way
	Between.clear();
	if (Sim)
	{
		const tag::TeamKey OffWas = Sim->isReturn() ? G.poss : G.poss; // the sim's own side labels are the last play's
		for (tag::Ent* E : Sim->ents)
		{
			AAthlete* A = AthleteFor(E->p, Game->home->hasPlayer(E->p) ? tag::TeamKey::Home : tag::TeamKey::Away);
			FBetweenMan M;
			M.P = E->p;
			auto PJ = PostPos.find(E);
			M.Pos = PJ != PostPos.end() ? PJ->second : A->GetActorLocation();
			M.Pos.Z = 0.f;
			M.Yaw = A->GetActorRotation().Yaw;
			M.bOff = G.teamOf(G.poss).hasPlayer(E->p);
			Between.push_back(M);
		}
		BetweenBallW = ToWorld(Sim->ball.pos.x, Sim->ball.pos.y, Dir);
		(void)OffWas;
	}
	PostPos.clear();
	PostYaw.clear();
	Sim.reset();
	{
		std::vector<tag::Player*> Keep;
		for (const FBetweenMan& M : Between) Keep.push_back(M.P);
		ShowOnly(Keep);
	}
	Dir = G.worldDir();
	IdleFocus = ToWorld(bTwo ? 98 : G.ballOn, G.ballY, Dir);
	Stadium->SetLines(ToWorld(bTwo ? 98 : G.ballOn, 0, Dir).X, bTwo ? -1 : ToWorld(FMath::Min(100.0, G.ballOn + G.toGo), 0, Dir).X);
	{
		Stadium->SetDown(bTwo || G.phase != tag::GamePhase::Scrimmage ? 0 : G.down);
		static const TCHAR* QName[] = {TEXT("1ST"), TEXT("2ND"), TEXT("3RD"), TEXT("4TH")};
		const int32 Secs = FMath::Max(0, FMath::CeilToInt(G.clock));
		Stadium->SetScoreboard(FString::Printf(TEXT("%s %d   %s %d"), UTF8_TO_TCHAR(G.away->abbr.c_str()), G.score.away,
			UTF8_TO_TCHAR(G.home->abbr.c_str()), G.score.home),
			FString::Printf(TEXT("%s  %d:%02d"), G.q >= 1 && G.q <= 4 ? QName[G.q - 1] : TEXT("OT"), Secs / 60, Secs % 60));
	}
	auto Finish = [this](const tag::OffPlay* Op, const tag::DefCall* Dc) { StartPresnap(Op, Dc); };
	if (bArtTest && !bTwo)
	{
		// -TagArtTest: the user's side of the ball is the one still to test; the calls are made for it
		UserTeam = !bArtDidOff ? G.poss : tag::other(G.poss);
		const tag::OffPlay* Op = UserOff() ? tag::playById("trips_flood") : G.cpuOffense();
		return Finish(Op, UserOff() ? G.cpuDefense(*Op) : tag::defById("base_c3"));
	}
	if (bCallShot && !bTwo)
	{
		// -TagCallShot: the call screen itself, offense first and then defense, each shot with its cards up; the play
		// then runs on the coach's pick
		UserTeam = CallShotStep == 0 ? G.poss : tag::other(G.poss);
		const bool bOff = UserOff();
		Overlay->ShowPlayCall(bOff, false, false, [](FCallChoice) {});
		FTimerHandle H;
		GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, Finish, bOff]
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_call_%s.png"), bOff ? TEXT("off") : TEXT("def")), true, false);
			FTimerHandle H2;
			GetWorld()->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateWeakLambda(this, [this, Finish]
			{
				Overlay->ClearModal();
				if (++CallShotStep >= 2) { FPlatformMisc::RequestExit(false); return; }
				const tag::OffPlay* Op = Game->cpuOffense();
				Finish(Op, Game->cpuDefense(*Op));
			}), 0.6f, false);
		}), 3.f, false);
		return;
	}
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
	// where the last play left everyone (world), so they jog back to the new formation instead of appearing on it
	std::map<tag::Player*, FVector> From;
	for (const FBetweenMan& M : Between) From[M.P] = M.Pos;
	if (Sim)
		for (tag::Ent* E : Sim->ents)
		{
			auto PJ = PostPos.find(E);
			From[E->p] = PJ != PostPos.end() ? PJ->second : ToWorld(E->pos.x, E->pos.y, Dir);
		}
	Between.clear();
	PostPos.clear();
	PostYaw.clear();
	bUmpHasBall = false;
	bUmpSpotted = false;
	Sim = bTwoPoint ? G.twoPointSim(*Op, *Dc, US) : G.makePlay(*Op, *Dc, US);
	Dir = G.worldDir();
	double Far = 0.0;
	int32 Walkers = 0, Known = 0;
	for (tag::Ent* E : Sim->ents)
	{
		auto It = From.find(E->p);
		if (It == From.end()) continue;
		++Known;
		const tag::V2 P = TagCoords::FromWorld(It->second, Dir);
		const double D = tag::dist(P, E->pos);
		if (D > 0.3 && D < 32.0) // a man far downfield (a long touchdown) takes his spot; the rest jog in
		{
			Sim->walkFrom(E, P);
			Far = FMath::Max(Far, D);
			++Walkers;
		}
	}
	UE_LOG(LogTemp, Verbose, TEXT("TAG presnap walk: %d of %d known men walk in, farthest %.1f yd"), Walkers, Known, Far);
	if (bAuto) UE_LOG(LogTemp, Display, TEXT("TAG presnap walk: %d of %d known men walk in, farthest %.1f yd"), Walkers, Known, Far);
	std::vector<tag::Player*> Ps;
	for (tag::Ent* E : Sim->ents) Ps.push_back(E->p);
	ShowOnly(Ps);
	CapturePrev();
	Stadium->SetLines(ToWorld(Sim->los, 0, Dir).X, bTwoPoint ? -1 : ToWorld(FMath::Min(100.0, G.ballOn + G.toGo), 0, Dir).X);
	Labels.clear();
	if (US && *US == tag::Side::Off) AssignLabels();
	State = EMatchState::Presnap;
	// the user's offense snaps when ready; against the user's defense the CPU offense gives it a few seconds to
	// make its calls (and waits while a pre-snap menu is open, up to the play clock)
	Timer = bWatch ? 1.1f / Speed : (US && *US == tag::Side::Off) ? 999.f : US ? 7.f : 1.6f;
	// ...and nobody snaps it before the men jogging back have set (4.2 yd/s is their jog; at most a few seconds)
	if (!(US && *US == tag::Side::Off)) Timer = FMath::Max(Timer, static_cast<float>(FMath::Min(6.0, Far / 4.2 + 0.7)) / Speed);
	// the pre-snap panel lists the calls; the hint is for the play itself
	if (bPlayStationPad)
		Overlay->SetHint(US && *US == tag::Side::Off ? TEXT("Throw: press the receiver's button (hold L2 = touch)    L-STICK move    R2 juke    CREATE camera    OPTIONS pause")
			: US ? TEXT("Defending    L1 switch player    L-STICK move    R2 dive tackle    CREATE camera    OPTIONS pause")
			: TEXT("CREATE camera    OPTIONS pause"));
	else
		Overlay->SetHint(US && *US == tag::Side::Off ? TEXT("Throw: the receiver's key 1-5 (SHIFT = touch)   WASD move   C camera")
			: US ? TEXT("Defending   TAB switch player   WASD move   SPACE dive tackle")
			: TEXT("1/2/4 game speed   C camera   ESC pause"));
	BeginPresnapUI();
	SyncPlay(0.f, 1.f);
}

void AMatchDirector::AssignLabels()
{
	// buttons by the QB's progression (cross = the primary), then the other route runners, then the eligible men
	// kept in to block (they can be hot routed out); the blockers lose theirs at the snap (EndPresnapUI)
	Labels.clear();
	const std::vector<tag::Ent*> Order = ArtLabelOrder(*Sim);
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
	if (bLiveReturn)
	{
		bLiveReturn = false;
		const tag::PlayLog L = G.finishKickReturn(P);
		if (L.big == tag::LogTag::Turnover) Overlay->Banner(TEXT("FUMBLE"), S(L.desc), Bad);
		State = EMatchState::Wait;
		Timer = 0.f;
		return;
	}
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

void AMatchDirector::StartCutIn(const tag::Ent* Who, float Seconds)
{
	static const bool bForce = FParse::Param(FCommandLine::Get(), TEXT("TagCutIns")); // review cut-ins under automation
	if (!Who || ((bAuto || bLineup) && !bForce)) return; // automation drives its own shots
	UE_LOG(LogTemp, Display, TEXT("TAG cut-in: #%d %s for %.1f s"), Who->p->number, ANSI_TO_TCHAR(tag::posName(Who->p->pos)), Seconds);
	CutInTarget = AthleteFor(Who->p, Who->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
	CutInLeft = Seconds;
	Timer = FMath::Max(Timer, Seconds); // hold the post-play beat long enough for the shot
}

void AMatchDirector::OnPlayDead()
{
	const tag::PlayResult& R = *Sim->result;
	// the broadcast cut-in: who made the play (the ball carrier on a score, takeaway or big gain; the sacker)
	{
		const tag::Ent* Star = Sim->ball.holder;
		if (R.kind == tag::PlayKind::Sack && !R.tacklers.empty())
			for (const tag::Ent* E : Sim->ents)
				if (E->p == R.tacklers.front()) Star = E;
		static const int32 BigGain = FParse::Param(FCommandLine::Get(), TEXT("TagCutIns")) ? 5 : 20; // review: more samples
		if (R.td || R.interception || R.fumbleLost || R.kind == tag::PlayKind::Sack || R.yards >= BigGain)
			StartCutIn(Star, R.td ? 3.5f : 2.8f);
		static const bool bReplayAll = FParse::Param(FCommandLine::Get(), TEXT("TagReplayAll")); // review: replay every play
		bReplayWorthy = bReplayAll || R.td || R.interception || R.fumbleLost || (Sim->isReturn() ? R.yards >= 40 : (R.kind == tag::PlayKind::Sack || R.yards >= 20));
	}
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

void AMatchDirector::RecordReplayFrame(float Dt)
{
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoReplay"));
	if (bOff || bLineup) return;
	ReplayClock += Dt;
	if (ReplayBuf.Num() && ReplayClock - ReplayBuf.Last().T < 1.f / 30.f) return;
	if (ReplayBuf.Num() >= 30 * 40) return; // 40 s cap
	FReplayFrame& F = ReplayBuf.AddDefaulted_GetRef();
	F.T = ReplayClock;
	if (Ball)
	{
		F.Ball = Ball->GetActorTransform();
		F.bBall = !Ball->IsHidden();
	}
	TArray<FTransform> Local;
	for (const auto& KV : Athletes)
	{
		const AAthlete* A = KV.Value;
		FTransform W;
		if (!A || A->IsHidden() || !A->CaptureReplayPose(W, Local)) continue;
		FReplayAth& R = F.Ath.AddDefaulted_GetRef();
		R.Id = KV.Key;
		R.World = W;
		R.Rot.SetNumUninitialized(Local.Num());
		R.Pos.SetNumUninitialized(Local.Num());
		for (int32 i = 0; i < Local.Num(); ++i)
		{
			R.Rot[i] = FQuat4f(Local[i].GetRotation());
			R.Pos[i] = FVector3f(Local[i].GetTranslation());
		}
	}
}

bool AMatchDirector::StartReplay()
{
	if (!bReplayWorthy || ReplayBuf.Num() < 20) return false;
	bReplayWorthy = false;
	// the last 5 s of the play at a little over half speed (a long run is picked up as it breaks free)
	ReplayEnd = ReplayBuf.Last().T;
	ReplayT = FMath::Max(0.f, ReplayEnd - 5.f);
	ReplayIdx = 0;
	ReplayFocus = FVector::ZeroVector;
	State = EMatchState::Replay;
	Overlay->SetHint(bWatch ? TEXT("REPLAY") : TEXT("REPLAY      SPACE skip"));
	UE_LOG(LogTemp, Display, TEXT("TAG replay: %d frames, %.1f s of play from %.1f s"), ReplayBuf.Num(), ReplayEnd - ReplayT, ReplayT);
	return true;
}

void AMatchDirector::TickReplay(float Dt, bool bSkip)
{
	ReplayT += Dt * 0.55f;
	while (ReplayIdx + 1 < ReplayBuf.Num() && ReplayBuf[ReplayIdx + 1].T <= ReplayT) ++ReplayIdx;
	const FReplayFrame& A = ReplayBuf[ReplayIdx];
	const FReplayFrame& B = ReplayBuf[FMath::Min(ReplayIdx + 1, ReplayBuf.Num() - 1)];
	const float Al = B.T > A.T ? FMath::Clamp((ReplayT - A.T) / (B.T - A.T), 0.f, 1.f) : 0.f;
	for (const FReplayAth& RA : A.Ath)
	{
		AAthlete* Ath = Athletes.FindRef(RA.Id);
		if (!Ath) continue;
		const FReplayAth* RB = B.Ath.FindByPredicate([&](const FReplayAth& X) { return X.Id == RA.Id; });
		if (!RB || RB->Rot.Num() != RA.Rot.Num()) RB = &RA;
		ReplayScratch.SetNum(RA.Rot.Num());
		for (int32 i = 0; i < RA.Rot.Num(); ++i)
			ReplayScratch[i] = FTransform(FQuat::Slerp(FQuat(RA.Rot[i]), FQuat(RB->Rot[i]), Al).GetNormalized(),
				FMath::Lerp(FVector(RA.Pos[i]), FVector(RB->Pos[i]), Al));
		FTransform W;
		W.Blend(RA.World, RB->World, Al);
		Ath->ShowReplayPose(W, ReplayScratch);
	}
	FTransform BallT;
	BallT.Blend(A.Ball, B.Ball, Al);
	if (Ball)
	{
		Ball->SetActorTransform(BallT);
		Ball->SetActorHiddenInGame(!A.bBall);
	}
	// reverse angle from field level on the far side, a longer lens, easing after the ball
	const FVector BallAt = BallT.GetLocation();
	ReplayFocus = ReplayFocus.IsZero() ? BallAt : FMath::Lerp(ReplayFocus, BallAt, 1.f - FMath::Exp(-Dt / 0.55f * 4.f));
	const FVector P = ReplayFocus + FVector(-350.f, 1650.f, 230.f);
	const FVector Look = FVector(ReplayFocus.X, ReplayFocus.Y, 95.f);
	Camera->SetActorLocation(P);
	Camera->SetActorRotation((Look - P).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->FocusSettings.ManualFocusDistance = (Look - P).Size();
		C->CurrentFocalLength = 50.f;
	}
	static int32 GReplayShots = 0;
	if (bAuto && GReplayShots < 12 && ReplayCount == 0)
	{
		static float NextShot = 0.f;
		if (ReplayT >= NextShot || NextShot > ReplayEnd)
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_replay_%02d.png"), GReplayShots++), false, false);
			NextShot = ReplayT + 0.45f;
		}
	}
	if (ReplayT >= ReplayEnd || bSkip)
	{
		for (auto& KV : Athletes)
			if (KV.Value) KV.Value->EndReplay();
		++ReplayCount;
		Overlay->SetHint(TEXT(""));
		State = EMatchState::Post;
		Overlay->SetClockOffset(0);
		ApplyPlay();
	}
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
	tag::SpecialResult R = G.punt(true);
	if (G.pendingReturn) return StartLiveReturn(KickU, RecvU);
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

void AMatchDirector::StartLiveReturn(const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& RecvTeam)
{
	tag::Game& G = *Game;
	// the game gave the ball to the receiving team: they are the sim's offense, attacking their way
	std::optional<tag::Side> US;
	if (UserOff()) US = tag::Side::Off;
	else if (UserDef()) US = tag::Side::Def;
	Sim = G.kickReturnSim(RecvTeam, KickTeam, US);
	bLiveReturn = true;
	Dir = G.worldDir();
	std::vector<tag::Player*> Ps;
	for (tag::Ent* E : Sim->ents) Ps.push_back(E->p);
	ShowOnly(Ps);
	CapturePrev();
	Stadium->SetLines(-1, -1);
	Labels.clear();
	State = EMatchState::Live;
	Acc = 0;
	ReplayBuf.Reset();
	ReplayClock = 0.f;
	ReplayPostRec = 0.f;
	bReplayWorthy = false;
	Overlay->SetHint(US && *US == tag::Side::Off ? (bPlayStationPad ? TEXT("Returning the kick    L-STICK run    R2 juke") : TEXT("Returning the kick    WASD run    SPACE juke"))
		: US ? (bPlayStationPad ? TEXT("Kick coverage    L-STICK run    L1 switch    R2 dive tackle") : TEXT("Kick coverage    WASD run    TAB switch    SPACE dive tackle"))
		: TEXT(""));
	UE_LOG(LogTemp, Display, TEXT("TAG live %s return: land %.1f, hang %.1f s"), Sim->cfg.kick->punt ? TEXT("punt") : TEXT("kickoff"),
		Sim->cfg.kick->landX, Sim->cfg.kick->hang);
	SyncPlay(0.f, 1.f);
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
		// kick clips: AnimT is where to start in the clip so its foot contact lands on the ball leaving (Lead)
		float AnimT = K.T;
		if (FCString::Strcmp(Anim, TEXT("kickoff")) == 0) AnimT = FAthleteAssets::KickoffContact - Lead;
		else if (FCString::Strcmp(Anim, TEXT("punt")) == 0 || FCString::Strcmp(Anim, TEXT("fgkick")) == 0) AnimT = 0.f;
		A->Drive(ToWorld(X, Y, K.Dir), Yw, VelW, 9.f, Anim, AnimT, Dt, false);
		A->SetSelected(false);
		A->SetLabel(TEXT(""));
	};
	if (K.Kind == 0)
	{
		for (size_t i = 0; i < K.Kick.size(); ++i)
		{
			const double Y = i == 0 ? W / 2 : 4 + (static_cast<double>(i) - 1) / 9.0 * (W - 8);
			const double X = K.FromX - (i == 0 ? 7 : 1) + FMath::Max(0.f, K.T - 0.2f) * (i == 0 ? 4 : 8.5);
			const TCHAR* An = K.T > 0.2f ? TEXT("run") : TEXT("idle");
			if (i == 0 && K.T < Lead + 0.5f) An = TEXT("kickoff");
			Pose(K.Kick[i], FMath::Min(X, BallX + 2), Y, 1, An, K.T > 0.2f ? 8.5f : 0.f);
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
			const TCHAR* An = K.T > 1.1f ? TEXT("run") : TEXT("idle");
			if (i == 0 && K.T > Lead - FAthleteAssets::PuntContact && K.T < Lead + 0.5f) An = TEXT("punt"); // the punter
			Pose(K.Kick[i], X, Y, 1, An, K.T > 1.1f ? 8.f : 0.f);
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
			if (i == 0)
			{
				X = K.FromX - 1.5 + FMath::Min(1.f, K.T / 0.9f) * 1.5; Y = W / 2 + 0.5; Anim = K.T > 0.9f ? TEXT("idle") : TEXT("run"); Sp = K.T < 0.9f ? 4.f : 0.f;
				if (K.T > Lead - FAthleteAssets::FieldGoalContact && K.T < Lead + 0.6f) Anim = TEXT("fgkick");
			}
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
			A->TickAfter(this);
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
		FVector VelW = Dir > 0 ? FVector(E->vel.x, -E->vel.y, 0) * YD : FVector(-E->vel.x, E->vel.y, 0) * YD;
		FString AnimName(E->animName());
		float AnimT = static_cast<float>(E->animT);
		FVector Loc = ToWorld(X, Y, Dir);
		float Yaw = FaceToYaw(E->face, Dir);
		if (auto PJ = PostPos.find(E); PJ != PostPos.end())
		{
			// jogging toward the ball after the whistle (TickPostJog): the director's position and velocity
			const FVector Step = PJ->second - A->GetActorLocation();
			VelW = Dt > 1e-4f ? FVector(Step.X, Step.Y, 0.f) / Dt : FVector::ZeroVector;
			if (VelW.Size() > 60.f) { AnimName = TEXT("run"); Yaw = VelW.Rotation().Yaw; }
			else
			{
				AnimName = TEXT("idle");
				VelW = FVector::ZeroVector;
				if (auto PY = PostYaw.find(E); PY != PostYaw.end()) Yaw = PY->second;
			}
			Loc = PJ->second;
		}
		if (Sim->isReturn() && E == Sim->kickerEnt && Sim->t < Sim->cfg.kick->kickT + 0.6)
		{
			// the kick: start the clip so its foot contact lands when the sim kicks the ball
			const bool bPunt = Sim->cfg.kick->punt;
			AnimName = bPunt ? TEXT("punt") : TEXT("kickoff");
			AnimT = FMath::Max(0.f, (bPunt ? FAthleteAssets::PuntContact : FAthleteAssets::KickoffContact) - static_cast<float>(Sim->cfg.kick->kickT));
		}
		A->Drive(Loc, Yaw, VelW, static_cast<float>(E->vmax), AnimName, AnimT, Dt, E->isOL || E->isDL);
		A->SetSelected(E->userControlled);
		A->SetLabel(FString()); // receivers' buttons are drawn by the overlay (UpdateIcons)
	}
	// ball
	const tag::Ball& B = Sim->ball;
	auto AthOf = [&](const tag::Ent* X) { return AthleteFor(X->p, X->side == tag::Side::Off ? OffK : tag::other(OffK)); };
	// paired blocks: who each lineman is locked up with (the sim's engagement), for alignment
	{
		double SumD = 0.0;
		int32 NPairs = 0;
		for (tag::Ent* E : Sim->ents)
		{
			AAthlete* Me = AthOf(E);
			AAthlete* Him = E->engaged && E->anim == tag::Anim::Block ? AthOf(E->engaged) : nullptr;
			Me->SetBlockPartner(Him);
			// who is winning the rep (the engaged loops follow it); pass or run blocking from the blocker's state
			const tag::Ent* Blk = E->side == tag::Side::Off ? E : E->engaged;
			Me->SetLinePush(Him ? static_cast<float>(E->linePush) : 0.f, E->side == tag::Side::Def,
				!Blk || Blk->state != tag::EntState::RunBlock);
			if (Him) { SumD += FVector::Dist2D(Me->GetActorLocation(), Him->GetActorLocation()); ++NPairs; }
		}
		static double LastBlockLog = 0.0;
		const double NowT = GetWorld()->GetTimeSeconds();
		if (NPairs && NowT - LastBlockLog > 3.0)
		{
			LastBlockLog = NowT;
			UE_LOG(LogTemp, Verbose, TEXT("TAG blocks: %d engaged, mean chest to chest %.0f cm"), NPairs, SumD / NPairs);
		}
	}
	// paired tackles: a defender whose tackle just started squares up to the carrier he is hitting (the man down or
	// still holding the ball within 3 yd)
	for (tag::Ent* E : Sim->ents)
	{
		if (E->anim != tag::Anim::Tackle || E->animT > 0.06 || AthOf(E)->IsFalling()) continue;
		const tag::Ent* Best = nullptr;
		double BestD = 3.0;
		for (const tag::Ent* C : Sim->ents)
			if (C->side != E->side && (C->anim == tag::Anim::Down || C->hasBall) && tag::dist(C->pos, E->pos) < BestD)
			{
				Best = C;
				BestD = tag::dist(C->pos, E->pos);
			}
		if (Best)
		{
			AAthlete* Tk = AthOf(E);
			Tk->BeginTackleOn(AthOf(Best));
			if (Best->anim == tag::Anim::Down && !AthOf(Best)->IsFalling())
			{
				const FVector TvW = Dir > 0 ? FVector(E->vel.x, -E->vel.y, 0) * YD : FVector(-E->vel.x, E->vel.y, 0) * YD;
				AthOf(Best)->ApplyHit(TvW, static_cast<float>(E->p->weightLb) * 0.4536f, Tk->GetActorLocation());
			}
		}
	}
	// keyed one-shots on sim events (new events only; a new play restarts the list)
	if (Sim.get() != EventSim || Sim->events.size() < SeenEvents) { EventSim = Sim.get(); SeenEvents = 0; }
	for (; SeenEvents < Sim->events.size(); ++SeenEvents)
	{
		const tag::PlayEvent& Ev = Sim->events[SeenEvents];
		if (Ev.type == "handoff" && Ev.a)
		{
			AAthlete* Q = AthOf(Ev.a);
			// a toss on stretch runs (the back is out wide), a handoff otherwise
			const bool bPitch = Ev.b && tag::dist(Ev.a->pos, Ev.b->pos) > 2.5 && FAthleteAssets::Get().Pitch;
			// toss / hand to the side the back is on (the keyed clips go to the QB's right; mirrored to his left)
			bool bLeft = false;
			if (Q && Ev.b)
				if (AAthlete* Rb = AthOf(Ev.b)) bLeft = FVector::DotProduct(Rb->GetActorLocation() - Q->GetActorLocation(), Q->GetActorRightVector()) < 0.f;
			const FAthleteAssets& AA = FAthleteAssets::Get();
			if (Q) Q->PlayOneShot(AA.Side(bPitch ? AA.Pitch.Get() : AA.Handoff.Get(), bLeft));
		}
		else if (Ev.type == "throw" && Ev.a && Ev.b)
		{
			// the throw that fits the pass: on the run, under pressure, lofted, deep, quick, or a set-feet throw
			if (AAthlete* Q = AthOf(Ev.a))
			{
				const double D = tag::dist(Ev.a->pos, Ev.b->pos);
				const double Sp = tag::len(Ev.a->vel);
				double Rush = 99.0;
				for (const tag::Ent* O : Sim->ents)
					if (O->side != Ev.a->side) Rush = FMath::Min(Rush, tag::dist(O->pos, Ev.a->pos));
				int32 K = FAthleteAssets::ThrowSet;
				static int32 GPressSeq = 0;
				if (Sp > 2.5)
				{
					const FVector VW = Dir > 0 ? FVector(Ev.a->vel.x, -Ev.a->vel.y, 0) : FVector(-Ev.a->vel.x, Ev.a->vel.y, 0);
					K = FVector::DotProduct(VW, Q->GetActorRightVector()) > 0.f ? FAthleteAssets::ThrowRunRK : FAthleteAssets::ThrowRunLK;
				}
				else if (Rush < 1.6) K = (GPressSeq++ % 2) ? FAthleteAssets::ThrowSidearmK : FAthleteAssets::ThrowFadeK;
				else if (Sim->ball.lob) K = FAthleteAssets::ThrowTouchK;
				else if (D > 28.0) K = FAthleteAssets::ThrowDeepK;
				else if (D < 10.0) K = FAthleteAssets::ThrowQuickK;
				Q->PlayThrow(K);
				UE_LOG(LogTemp, Verbose, TEXT("TAG throw kind %d (%.0f yd, qb %.1f yd/s, rush %.1f yd%s)"), K, D, Sp, Rush, Sim->ball.lob ? TEXT(", lob") : TEXT(""));
			}
		}
		else if (Ev.type == "shed" && Ev.a && Ev.b)
		{
			// the rusher beat his blocker: his move (picked by the sim from his build and leverage) to the side he
			// went, and the beaten blocker's lunge after him
			AAthlete* Ru = AthOf(Ev.a);
			AAthlete* Bl = AthOf(Ev.b);
			if (Ru && Bl)
			{
				Ru->PlayRushMove(UTF8_TO_TCHAR(Ev.note.c_str()), Bl);
				Bl->PlayBeaten(Ru);
			}
		}
		else if (Ev.type == "tackle" && Ev.a && Ev.b)
		{
			// the sim's tackle: both men to the ground together, the carrier's body from the hit to the spot
			AAthlete* Tk = AthOf(Ev.a);
			AAthlete* Cr = AthOf(Ev.b);
			if (Tk && Cr && Tk != Cr)
				StartPairedTackle(Tk, Cr, ToWorld(Ev.b->pos.x, Ev.b->pos.y, Dir), -1, Ev.note == "dive");
		}
		else if (Ev.type == "assist" && Ev.a && Ev.b)
		{
			if (AAthlete* As = AthOf(Ev.a)) StartPileOn(As, AthOf(Ev.b));
		}
		else if (Ev.type == "pancake" && Ev.a && Ev.b)
		{
			// a run block won decisively: the defender goes down on his back (the sim's down state: hit, fall, get up)
			// and the blocker finishes over him
			if (AAthlete* Bl = AthOf(Ev.b))
			{
				Bl->PlayPancakeFinish();
				if (AAthlete* Df = AthOf(Ev.a))
				{
					// knocked onto his back away from the blocker (the carrier's head-on fall, about a metre back)
					const FVector Here = Df->GetActorLocation();
					const float Yaw = (Bl->GetActorLocation() - Here).Rotation().Yaw;
					if (UAnimationAsset* Fall = FAthleteAssets::Get().PairC[FAthleteAssets::PairDrive])
						Df->BeginPairedFall(Fall, Here, Here, Yaw, 0.3f, 0.9f, false);
				}
			}
			UE_LOG(LogTemp, Display, TEXT("TAG pancake: #%d puts #%d down"), Ev.b->p ? Ev.b->p->number : -1, Ev.a->p ? Ev.a->p->number : -1);
		}
		else if ((Ev.type == "broken") && Ev.a && Ev.b)
		{
			// broken tackle: the defender whiffs, the carrier bounces off and keeps going
			AAthlete* Cr = AthOf(Ev.a);
			AAthlete* Df = AthOf(Ev.b);
			if (Cr && Df)
			{
				Cr->PlayBrokenTackle(FVector::DotProduct(Df->GetActorLocation() - Cr->GetActorLocation(), Cr->GetActorRightVector()) > 0.f);
				Df->PlayMissedTackle(FVector::DotProduct(Cr->GetActorLocation() - Df->GetActorLocation(), Df->GetActorRightVector()) < 0.f);
			}
		}
		else if (Ev.type == "divecatch" && Ev.a)
		{
			// laid out for it: the sim ends the play at the spot, the receiver dives, secures it and gets up
			if (AAthlete* R = AthOf(Ev.a)) R->PlayDiveAttempt();
		}
		else if (Ev.type == "incomplete" && Ev.a)
		{
			// a ball just out of reach of a receiver still running at it: he lays out for it
			const tag::Ball& Bi = Sim->ball;
			const double Gap = tag::dist(tag::V2{Bi.pos.x, Bi.pos.y}, Ev.a->pos);
			if (Gap > 1.0 && Gap < 3.5 && tag::len(Ev.a->vel) > 3.0)
				if (AAthlete* R = AthOf(Ev.a)) R->PlayDiveAttempt();
		}
		else if (Ev.type == "catch" && Ev.a)
		{
			// catch type from the ball's height, the receiver's speed, the ball's direction and the sideline
			if (AAthlete* R = AthOf(Ev.a))
			{
				const tag::Ball& Bc = Sim->ball;
				const FVector BallVelC = Dir > 0 ? FVector(Bc.vel.x, -Bc.vel.y, Bc.vel.z) * YD : FVector(-Bc.vel.x, Bc.vel.y, Bc.vel.z) * YD;
				const double Yd = Ev.a->pos.y;
				FVector Side = FVector::ZeroVector;
				if (Yd < 1.5 || Yd > tag::FIELD_W - 1.5)
				{
					const FVector In = ToWorld(Ev.a->pos.x, Yd, Dir, 0), Out = ToWorld(Ev.a->pos.x, Yd < 1.5 ? Yd - 1.0 : Yd + 1.0, Dir, 0);
					Side = (Out - In).GetSafeNormal();
				}
				bool bContested = false;
				for (const tag::Ent* O : Sim->ents)
					if (O->side != Ev.a->side && tag::dist(O->pos, Ev.a->pos) < 1.3) bContested = true;
				// the sim takes momentum off the catcher as he catches it (x0.45, x0.3 contested): his speed into the catch
				const double SpeedIn = tag::len(Ev.a->vel) / (bContested ? 0.3 : 0.45);
				R->PlayCatch(static_cast<float>(Bc.pos.z * YD), static_cast<float>(SpeedIn * YD), BallVelC, Side,
					ToWorld(Bc.pos.x, Bc.pos.y, Dir, Bc.pos.z), bContested);
			}
		}
	}
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
		F.bLive = State == EMatchState::Live;
		F.bSecure = F.bHasBall && bQB && E->speed() < 5.5 && E->anim != tag::Anim::Run;
		if (Holder && Holder != E && E->side != Holder->side) F.CarrierDist = FVector::Dist2D(A->GetActorLocation(), AthOf(Holder)->GetActorLocation());
		if (E->anim == tag::Anim::Throw) F.ThrowT = FMath::Clamp(static_cast<float>(E->animT) / 0.45f, 0.f, 1.f);
		F.bStance = Sim->t < 0.05 && (E->isOL || E->isDL);
		F.bPreSnap = Sim->t < 0.05 && State == EMatchState::Presnap;
		if (bQB && F.bPreSnap)
			for (const tag::Ent* C : Sim->ents)
				if (C->role == "C") F.bUnderCenter = tag::dist(C->pos, E->pos) < 1.7; // hands under the center
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
		if (!bShotGetUp && EventShotFrames == 0)
		{
			for (auto& KV : Athletes)
			{
				if (KV.Value && KV.Value->GetUpElapsed() > 0.4f)
				{
					bShotGetUp = true;
					EventShotName = TEXT("tag_auto_getup.png");
					CloseupTarget = KV.Value;
					EventShotFrames = 3;
					break;
				}
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
	else if (bUmpHasBall && Umpire)
	{
		FVector HoldLoc;
		FRotator HoldRot;
		if (Umpire->GetBallHold(HoldLoc, HoldRot)) { Ball->SetActorLocation(HoldLoc); Ball->SetActorRotation(HoldRot); }
	}
	else if (bUmpSpotted && Game)
	{
		// spotted for the next play: on the turf at the new line, laces up, nose downfield
		const int32 NewDir = Game->worldDir();
		Ball->SetActorLocation(ToWorld(Game->ballOn, Game->ballY, NewDir) + FVector(0, 0, 9));
		Ball->SetActorRotation(FRotator(0, NewDir > 0 ? 0 : 180, 0));
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

void AMatchDirector::UpdateCamera(float Dt, const FVector& InFocus)
{
	if (bTackleTest || bLineTest) return; // the look-dev scenes frame their own shots
	const double Y = YD;
	// a dead ball keeps flying (an overthrow out of the back of the end zone, a kick into the stands): the operators
	// stay on the field, so the framing never follows it more than a few yards past the end lines and sidelines
	const FVector Focus(FMath::Clamp(InFocus.X, -8.0 * Y, 108.0 * Y), FMath::Clamp(InFocus.Y, -(tag::FIELD_W / 2 + 3) * Y, (tag::FIELD_W / 2 + 3) * Y), InFocus.Z);
	FVector T, Look;
	const double D = static_cast<double>(Dir);
	switch (CamMode)
	{
	case ECamMode::Broadcast:
	{
		// play art up: the camera rises and looks further downfield so the whole route tree fits
		const double A = State == EMatchState::Presnap && PlayArt ? static_cast<double>(PlayArt->Alpha()) : 0.0;
		T = FVector(Focus.X - D * (17 + 1.5 * A) * Y, Focus.Y * 0.45, (10.5 + 4 * A) * Y);
		Look = FVector(Focus.X + D * (7 + 3.5 * A) * Y, Focus.Y * 0.8, 0);
		break;
	}
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
	// keep cameras over the field / in front of the stands (never inside geometry); backed up against an end line
	// the broadcast camera can't get its distance, so it climbs instead to keep the backfield in the frame
	const double WantX = T.X;
	T.X = FMath::Clamp(T.X, -6.0 * Y, 106.0 * Y);
	if (CamMode == ECamMode::Broadcast) T.Z += FMath::Abs(WantX - T.X) * 0.9;
	T.Y = FMath::Clamp(T.Y, -(tag::FIELD_W / 2 + 12) * Y, (tag::FIELD_W / 2 + 12) * Y);
	if (CamMode != LastCamMode)
	{
		LastCamMode = CamMode;
		CamPos = T; // cut, don't swoop, when the camera changes
		CamLook = Look;
	}
	// lead the ball the way an operator does: frame where the play is going, not where it was
	if (Sim && CamMode != ECamMode::Player)
	{
		const tag::Ball& B = Sim->ball;
		const FVector BV = Dir > 0 ? FVector(B.vel.x, -B.vel.y, 0) * Y : FVector(-B.vel.x, B.vel.y, 0) * Y;
		Look += (BV * 0.35f).GetClampedToMaxSize(6.f * Y);
	}
	// critically damped moves (no overshoot, no linear-lerp drag): ~0.35 s to settle the dolly, ~0.25 s the pan
	auto Spring = [Dt](FVector& X, FVector& V, const FVector& Goal, float Halflife)
	{
		const float W = 1.3863f / FMath::Max(Halflife, 0.01f); // ln(4) / halflife
		const FVector J0 = X - Goal, J1 = V + J0 * W;
		const float E = FMath::Exp(-W * Dt);
		X = Goal + (J0 + J1 * Dt) * E;
		V = (V - J1 * W * Dt) * E;
	};
	Spring(CamPos, CamVel, T, 0.35f);
	Spring(CamLook, LookVel, Look, 0.22f);
	FVector P = CamPos;
	CamTime += Dt;
	// lens: zoom to the action (players within 18 yd of the focus) so a pile fills the frame and a breakaway does
	// not shrink to a speck; wider when the ball is in the air
	float WantFocal = CamMode == ECamMode::Sideline ? 45.f : CamMode == ECamMode::Player ? 24.f : 30.f;
	if (CamMode == ECamMode::Sideline || CamMode == ECamMode::Broadcast)
	{
		float Spread = 6.f * Y;
		for (const auto& KV : Athletes)
		{
			const AAthlete* O = KV.Value;
			if (!O || O->IsHidden()) continue;
			const float Dd = FVector::Dist2D(O->GetActorLocation(), Look);
			if (Dd < 18.f * Y) Spread = FMath::Max(Spread, Dd);
		}
		if (Sim && Sim->ball.mode == tag::BallMode::Air) Spread = FMath::Max(Spread, 16.f * Y);
		WantFocal = FMath::Clamp(WantFocal * (11.f * Y) / Spread, WantFocal * 0.75f, WantFocal * 1.9f);
	}
	{
		const float W = 1.3863f / 0.6f, J0 = CamFocal - WantFocal, J1 = CamFocalVel + J0 * W, E = FMath::Exp(-W * Dt);
		CamFocal = WantFocal + (J0 + J1 * Dt) * E;
		CamFocalVel = (CamFocalVel - J1 * W * Dt) * E;
	}
	// handheld drift for the field-level cameras (low frequency, a few cm; the booth and skycam stay rock steady)
	FVector Hand = FVector::ZeroVector;
	if (CamMode == ECamMode::Sideline || CamMode == ECamMode::Player)
		Hand = FVector(FMath::PerlinNoise1D(CamTime * 0.31f), FMath::PerlinNoise1D(CamTime * 0.27f + 17.f), FMath::PerlinNoise1D(CamTime * 0.23f + 41.f)) * 6.f;
	P += Hand;
	if (Shake > 0.1f)
	{
		P += FVector(FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake, Shake));
		Shake *= FMath::Exp(-Dt * 6.f);
	}
	if (bAutoEstablish && GAutoCoach)
	{
		const FVector C(46 * Y, (tag::FIELD_W / 2 + 1.7) * Y, 0);
		P = C + FVector(-5 * Y, -11 * Y, 2.2 * Y);
		CamLook = C + FVector(0, 0, 1.1 * Y);
	}
	else if (bAutoEstablish && GAutoChains && Stadium)
	{
		const FVector B = Stadium->DownBoxLocation();
		P = B + FVector(-4 * Y, -14 * Y, 3.5 * Y);
		CamLook = B + FVector(4 * Y, 0, 1.2 * Y);
	}
	else if (bAutoEstablish && GAutoBoard)
	{
		P = FVector(40 * Y, -12 * Y, 12 * Y);
		CamLook = FVector(-76 * Y, 0, 72 * Y);
	}
	else if (bAutoEstablish)
	{
		P = FVector(50 * Y, -30 * Y, 22 * Y);
		CamLook = FVector(50 * Y, 70 * Y, 14 * Y);
	}
	else if (!CloseupTarget && CutInTarget && CutInLeft > 0.f)
	{
		CutInLeft -= Dt;
		const FVector Tgt = CutInTarget->GetActorLocation() + FVector(0, 0, 120);
		const FVector F = CutInTarget->GetActorForwardVector(), R = CutInTarget->GetActorRightVector();
		// long lens from in front and a little low: compresses the background, the crowd fills the frame behind him
		P = Tgt + F * 900.f + R * 260.f - FVector(0, 0, 40) + FVector(FMath::PerlinNoise1D(CamTime * 0.4f), FMath::PerlinNoise1D(CamTime * 0.35f + 9.f), 0) * 8.f;
		CamLook = Tgt;
		static int32 GCutShots = 0;
		static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("TagCutIns"));
		if (bShots && CutInLeft < 1.6f && CutInLeft + Dt >= 1.6f && GCutShots < 6)
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_cutin_%d.png"), GCutShots++), false, false);
		if (CutInLeft <= 0.f) { CutInTarget = nullptr; CamPos = P; CamVel = FVector::ZeroVector; }
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
		// broadcast operators reframe around bodies: orbit the camera about the target until no other visible
		// athlete sits on the camera or the camera-to-target line (catches are usually contested)
		auto Blocked = [&](const FVector& C)
		{
			for (const auto& KV : Athletes)
			{
				const AAthlete* O = KV.Value;
				if (!O || O == CloseupTarget || O->IsHidden()) continue;
				const FVector Q = O->GetActorLocation() + FVector(0, 0, 100);
				if (FMath::PointDistToSegment(Q, C, Tgt) < 70.f) return true;
			}
			return false;
		};
		if (Blocked(P))
		{
			const FVector Off = P - Tgt;
			for (float Deg : {35.f, -35.f, 70.f, -70.f, 110.f, -110.f, 150.f, -150.f})
			{
				const FVector Cand = Tgt + Off.RotateAngleAxis(Deg, FVector::UpVector) + FVector(0, 0, 40);
				if (!Blocked(Cand)) { P = Cand; break; }
			}
		}
	}
	// -TagLineCam (look-dev, with -TagAutoWatch): the trenches in profile. A low camera on the sideline at the line of
	// scrimmage, framing the interior line, for the first 4 s of every scrimmage play; a frame every 0.25 s.
	static const bool bLineCam = FParse::Param(FCommandLine::Get(), TEXT("TagLineCam"));
	bool bLineShot = false;
	if (bLineCam && Sim && (State == EMatchState::Live || State == EMatchState::Presnap) && Sim->t < 4.0)
	{
		const FVector Los = ToWorld(Sim->los, Sim->by, Dir);
		P = FVector(Los.X, Los.Y - 15.5 * Y, 1.7 * Y);
		CamLook = FVector(Los.X - Dir * 1.0 * Y, Los.Y, 0.8 * Y);
		bLineShot = State == EMatchState::Live;
	}
	Camera->SetActorLocation(P);
	Camera->SetActorRotation((CamLook - P).Rotation());
	if (UCineCameraComponent* C = Camera->GetCineCameraComponent())
	{
		C->FocusSettings.ManualFocusDistance = (CamLook - P).Size();
		C->CurrentFocalLength = bLineShot || (bLineCam && State == EMatchState::Presnap) ? 50.f
			: CutInTarget && CutInLeft > 0.f && !CloseupTarget ? 85.f : CloseupTarget ? 30.f : CamFocal;
	}
	static int32 GLineShots = 0;
	static double GNextLineShot = 0.0;
	if (bLineShot && GLineShots < 400)
	{
		if (Sim->t < GNextLineShot - 1.0) GNextLineShot = 0.0; // new play
		if (Sim->t >= GNextLineShot)
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_line_%03d.png"), GLineShots++), false, false);
			GNextLineShot = Sim->t + 0.25;
		}
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
	if (bArtTest) TickArtTest(Dt);
	{
		// -TagMenuShot: screenshot the main menu with its UI (gamepad focus frame), then quit
		static const bool bMenuShot = FParse::Param(FCommandLine::Get(), TEXT("TagMenuShot"));
		static float MenuT = 0.f;
		if (bMenuShot && (MenuT += Dt) > 6.f && MenuT - Dt <= 6.f) FScreenshotRequest::RequestScreenshot(TEXT("tag_menu.png"), true, false);
		if (bMenuShot)
			if (APlayerController* MPC = GetWorld()->GetFirstPlayerController())
				for (const FKey& K : {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Enter, EKeys::SpaceBar,
					EKeys::Gamepad_Special_Right, EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightTrigger})
					if (MPC->IsInputKeyDown(K)) UE_LOG(LogTemp, Display, TEXT("TAG menu input held: %s at %.2f s"), *K.ToString(), MenuT);
		if (bMenuShot && MenuT > 8.f) FPlatformMisc::RequestExit(false);
	}
	TickSidelines(Dt);
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
	if (bLineup)
	{
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return; // wait for shaders
#endif
		TickLineup(Dt);
		return;
	}
	if (bEsc && State != EMatchState::Final) TogglePause();
	if (PC && (PC->WasInputKeyJustPressed(EKeys::C) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left))) CamMode = static_cast<ECamMode>((static_cast<uint8>(CamMode) + 1) % 4);
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
		if (Sim && !Sim->isLive()) TickPostJog(SDt);
		if (Sim) SyncPlay(Dt, 1.f);
		if (Timer <= 0) Next();
		break;
	case EMatchState::Presnap:
	{
		TickPresnap(SDt);
		{
			// automation: the formation setting up, men still walking to their spots
			static bool bSetShot = false;
			static int32 SetLogged = 0;
			if (bAuto && SetLogged < 6 && PresnapClock > 0.4f && Timer > 0.f && Timer < 100.f && PresnapClock - SDt <= 0.4f)
			{
				++SetLogged;
				int32 Walking = 0;
				for (const tag::Ent* E : Sim->ents) Walking += E->shiftTo ? 1 : 0;
				UE_LOG(LogTemp, Display, TEXT("TAG presnap at %.2f s: %d men still walking to their spots, timer %.2f"), PresnapClock, Walking, Timer);
			}
			if (bAuto && !bSetShot && PresnapClock > 0.4f && Sim->walking())
			{
				bSetShot = true;
				FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_setup.png"), false, false);
				UE_LOG(LogTemp, Display, TEXT("TAG auto screenshot setup: formation still setting at %.1f s"), PresnapClock);
			}
		}
		const bool bMenu = PreMode != EPre::Base;
		if (!(UserDef() && bMenu)) Timer -= SDt;
		PresnapClock += SDt;
		if (Timer <= 0 || (UserDef() && PresnapClock > 25.f) || (bCallShot && PresnapClock > 4.f && !Sim->walking()) ||
			(UserOff() && bSpace && !bMenu && !bPreConsumed && !Sim->walking()))
		{
			EndPresnapUI();
			State = EMatchState::Live;
			Acc = 0;
			Overlay->SetHint(TEXT(""));
			ReplayBuf.Reset();
			ReplayClock = 0.f;
			ReplayPostRec = 0.f;
			bReplayWorthy = false;
		}
		SyncPlay(Dt, 1.f);
		UpdateCamera(Dt, FocusPoint());
		break;
	}
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
		RecordReplayFrame(SDt);
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
		TickPostJog(SDt);
		SyncPlay(Dt, 1.f);
		{
			// automation: the men jogging back toward the ball after the whistle (the first scrimmage play's)
			static bool bJogShot = false;
			if (bAuto && !bJogShot && !Sim->isReturn() && Timer < 0.25f && PostPos.size() >= 6)
			{
				bJogShot = true;
				FScreenshotRequest::RequestScreenshot(TEXT("tag_auto_postjog.png"), false, false);
				UE_LOG(LogTemp, Display, TEXT("TAG auto screenshot postjog: %d men jogging"), static_cast<int32>(PostPos.size()));
			}
		}
		UpdateCamera(Dt, FocusPoint());
		if ((ReplayPostRec += SDt) < 1.3f) RecordReplayFrame(SDt); // the whistle and the moment after it
		if (Timer <= 0 || (!bWatch && Timer < 1.2f && bSpace))
		{
			if (StartReplay()) break;
			Overlay->SetClockOffset(0);
			ApplyPlay();
		}
		break;
	case EMatchState::Replay:
		TickReplay(SDt, bSpace);
		break;
	case EMatchState::Kick:
		UpdateKick(SDt);
		break;
	default:
		UpdateCamera(Dt, Sim ? FocusPoint() : IdleFocus);
		if (State == EMatchState::Call) TickPostJog(SDt);
		if (Sim) SyncPlay(Dt, 1.f);
		break;
	}
	if (State != EMatchState::Menu && State != EMatchState::Final && !bLineup) TickUmpire(Dt);
	UpdateIcons();
	if (PlayArt && State != EMatchState::Presnap) PlayArt->SetShown(false);
}
