// The broadcast around the game (AMatchDirector, MatchDirector.h "the broadcast"): the view's hard cuts told to the
// renderer and recorded for the telemetry, which plays earn a replay, the field's graphics as TV draws them, and the
// stands moving with the home crowd's mood. Small hooks in MatchDirector.cpp call in here (Research/NextLevel/04 A4,
// A7; 05 A2; 07 A5).
#include "Game/MatchDirector.h"

#include "Audio/TagAudio.h"
#include "Camera/PlayerCameraManager.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "Render/Stadium.h"

using namespace TagCoords;

// ------------------------------------------------------------------ camera cuts (07 A5)
void AMatchDirector::MarkCameraCut(const TCHAR* Why)
{
	bCamCutPending = true;
	if (!CamCutWhy) CamCutWhy = Why;
}

void AMatchDirector::CommitCameraFrame(float Dt)
{
	if (!Camera || !GetWorld()) return;
	const FVector P = Camera->GetActorLocation();
	const FVector F = Camera->GetActorForwardVector();
	const UCineCameraComponent* Lens = Camera->GetCineCameraComponent();
	const float Focal = Lens ? Lens->CurrentFocalLength : 0.f;
	bool bCut = bCamCutPending;
	float Ang = 0.f;
	FString Jump;
	if (CamFrame.bValid)
	{
		// a cut nobody marked (a look-dev shot, a test's framing, a path still to be marked): one frame's jump past what
		// any operator's move does - the dolly at ~12 m/s, a whip pan well under 400 deg/s, a zoom far slower than e^3/s
		const float SafeDt = FMath::Max(Dt, 1e-3f);
		const float Moved = static_cast<float>(FVector::Dist(P, CamFrame.Pos));
		Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(F, CamFrame.Fwd)), -1.f, 1.f)));
		const float Zoom = Focal > 0.f && CamFrame.Focal > 0.f ? FMath::Abs(FMath::Loge(Focal / CamFrame.Focal)) : 0.f;
		const bool bJump = (Moved > 300.f && Moved / SafeDt > 3000.f) || (Ang > 12.f && Ang / SafeDt > 400.f) || (Zoom > 0.3f && Zoom / SafeDt > 3.f);
		if (bJump && !bCut)
		{
			bCut = true;
			Jump = FString::Printf(TEXT("jump %.0f cm, %.0f deg, lens %.0f -> %.0f mm"), Moved, Ang, CamFrame.Focal, Focal);
		}
	}
	if (bCut)
	{
		// the frame after a cut starts its temporal history (TSR, motion blur's vectors) afresh, as the renderer is told here
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APlayerController* PC = It->Get(); PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		++CamFrame.Cuts;
		UE_LOG(LogTemp, Display, TEXT("TAG camera cut: %s (state %d, cam %d%s)"), CamCutWhy ? CamCutWhy : *Jump, static_cast<int32>(State), static_cast<int32>(CamMode),
			bHighlights ? *FString::Printf(TEXT(", clip %d frame %d"), HlClip, HlFrame - 1) : TEXT(""));
	}
	CamFrame.AngSpeed = CamFrame.bValid && Dt > 1e-4f ? Ang / Dt : 0.f;
	CamFrame.bCut = bCut;
	CamFrame.Pos = P;
	CamFrame.Fwd = F;
	CamFrame.Focal = Focal;
	CamFrame.bValid = true;
	bCamCutPending = false;
	CamCutWhy = nullptr;
}

FString AMatchDirector::CameraTelemetryCols()
{
	// camx..camz the lens (cm), camfx..camfz the way it looks, camfoc the focal length (mm), camang the view's swing
	// since the frame before (deg/s; a cut frame's is the cut's), camcut 1 on a cut frame, camnear the nearest head or
	// pelvis of any man on the field to the lens (cm; under 60 the lens is in a helmet or a body), camhold the ball
	// carrier on screen (1 / 0; -1 nobody holds it). The frame the last screenshot showed: CommitCameraFrame's numbers
	// and the bodies as they were drawn.
	const FVector L = CamFrame.Pos;
	float Near = NAN;
	for (const auto& KV : Athletes)
	{
		const AAthlete* O = KV.Value;
		if (!O || O->IsHidden()) continue;
		for (const TCHAR* Bn : {TEXT("head"), TEXT("pelvis")})
		{
			const float D = static_cast<float>(FVector::Dist(O->BoneWorld(Bn), L));
			if (FMath::IsNaN(Near) || D < Near) Near = D;
		}
	}
	int32 Hold = -1;
	if (Sim && Game && Sim->ball.mode == tag::BallMode::Held && Sim->ball.holder)
	{
		Hold = 0;
		const tag::Ent* Hd = Sim->ball.holder;
		const AAthlete* HA = AthleteFor(Hd->p, Hd->side == tag::Side::Off ? Game->poss : tag::other(Game->poss));
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		FVector2D VP(0.0, 0.0);
		if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(VP);
		FVector2D S2;
		if (HA && !HA->IsHidden() && PC && VP.X > 0.0 && VP.Y > 0.0 && PC->ProjectWorldLocationToScreen(HA->GetActorLocation() + FVector(0, 0, 90), S2, false))
			Hold = S2.X >= 0.0 && S2.Y >= 0.0 && S2.X <= VP.X && S2.Y <= VP.Y ? 1 : 0;
	}
	return FString::Printf(TEXT(",%.1f,%.1f,%.1f,%.4f,%.4f,%.4f,%.1f,%.1f,%d,%.1f,%d"), L.X, L.Y, L.Z, CamFrame.Fwd.X, CamFrame.Fwd.Y, CamFrame.Fwd.Z, CamFrame.Focal,
		CamFrame.AngSpeed, CamFrame.bCut ? 1 : 0, Near, Hold);
}

// ------------------------------------------------------------------ replay-worthiness (04 A4)
void AMatchDirector::ResetReplayBudget()
{
	ReplayCredit = 1.f;
	ReplayScoreNow = 0.f;
	bReplayForced = false;
	ReplayWhyNow.Reset();
	LastReplayWhy.Reset();
	ReplayPlays = ReplaysShown = ReplayGap = ReplayGapMax = 0;
	ReplayWhyCount.Reset();
}

void AMatchDirector::DecideReplay(const tag::PlayResult& R)
{
	static const bool bReplayAll = FParse::Param(FCommandLine::Get(), TEXT("TagReplayAll")); // review: replay every play
	static const bool bOldRule = FParse::Param(FCommandLine::Get(), TEXT("TagReplayOld"));  // A/B: the yes/no test this replaced
	// the budget: Rate of a replay earned per scrimmage play (one per five), two at most banked; a play needs a score of
	// Need with a replay in hand, more as the budget runs down (-TagReplayRate=, -TagReplayNeed=: for tuning)
	static float Rate = FParse::Value(FCommandLine::Get(), TEXT("TagReplayRate="), Rate) ? Rate : 0.2f;
	static float NeedBase = FParse::Value(FCommandLine::Get(), TEXT("TagReplayNeed="), NeedBase) ? NeedBase : 1.f;
	bReplayWorthy = false;
	bReplayForced = false;
	ReplayScoreNow = 0.f;
	ReplayWhyNow.Reset();
	if (!Sim || !Game) return;
	const tag::Game& G = *Game; // (the play is not in the book yet: the down, the distance, the spot and the score before it)
	const bool bPlace = Sim->isPlaceKick(), bRet = Sim->isReturn();
	if (bReplayAll || bOldRule)
	{
		bReplayWorthy = bReplayAll || R.td || R.interception || R.fumbleLost || (bPlace ? Sim->placeBlocked
			: bRet ? R.yards >= 40 : (R.kind == tag::PlayKind::Sack || R.yards >= 20));
		bReplayForced = true;
		ReplayWhyNow = bReplayAll ? TEXT("all") : TEXT("old rule");
		return;
	}
	// what TV always shows again
	const TCHAR* Forced = R.td ? TEXT("touchdown") : (R.interception || R.fumbleLost) ? TEXT("takeaway") : R.safety ? TEXT("safety")
		: (bPlace && Sim->placeBlocked) ? TEXT("blocked kick") : nullptr;
	const bool bScrim = !bPlace && !bRet && !bTwoPoint && R.kind != tag::PlayKind::Kneel && R.kind != tag::PlayKind::Spike;
	if (bScrim)
	{
		++ReplayPlays;
		++ReplayGap;
		ReplayCredit = FMath::Min(2.f, ReplayCredit + Rate);
	}
	// the parts (about 1 is a replay on its own)
	float Gain = 0.f, Sack = 0.f, Down = 0.f, Zone = 0.f, Hit = 0.f, Catch = 0.f, Late = 1.f;
	const int32 Dn = FMath::Clamp(G.down, 1, 4);
	const double ToGo = G.toGo;
	if (bRet)
		Gain = R.yards >= 40 ? 1.f + (R.yards - 40) / 30.f : FMath::Max(0.f, (R.yards - 20) / 25.f) * 0.6f;
	else if (bScrim)
	{
		// yards past what the down usually gives (first down ~4, second about 60% of the distance, third and fourth the
		// distance), the explosive play on top
		const double Exp = Dn == 1 ? 4.0 : Dn == 2 ? FMath::Clamp(ToGo * 0.6, 2.0, 6.0) : ToGo;
		if (!R.turnover && R.yards > 0)
			Gain = static_cast<float>(FMath::Max(0.0, R.yards - Exp) / 16.0) + (R.yards >= 20 ? 0.35f : 0.f) + (R.yards >= 40 ? 0.3f : 0.f);
		// a sack (the one that ends a drive, on third or fourth down, more)
		if (R.kind == tag::PlayKind::Sack) Sack = Dn >= 3 ? 1.f : 0.7f;
		// the money downs: a third down converted (a long one more), a fourth down either way
		const bool bMade = !R.turnover && R.yards >= ToGo - 1e-6;
		if (Dn == 4) Down = 0.95f;
		else if (Dn == 3) Down = bMade ? 0.55f + (ToGo >= 7.0 ? 0.25f : 0.f) : R.kind == tag::PlayKind::Sack ? 0.f : 0.15f;
		if (G.ballOn >= 80.0 && Gain + Sack + Down > 0.f) Zone = 0.15f;
	}
	// the sim's big hit (the hit stick: a man laid out), broken tackles
	for (const tag::PlayEvent& Ev : Sim->events)
		if (Ev.type == "bighit") { Hit = 0.6f; break; }
	Hit += FMath::Min(0.5f, 0.2f * R.brokenTackles);
	// the catch: a layout, long odds (the odds the sim rolled it with), a contested one
	if (R.complete)
	{
		const tag::Ball& B = Sim->ball;
		if (B.fateDive) Catch += 0.45f;
		if (B.fateP >= 0.0 && B.fateP < 0.4) Catch += 0.3f + static_cast<float>(0.4 - B.fateP) * 1.5f;
		if (R.catchSep >= 0.0 && R.catchSep < 0.8) Catch += 0.15f;
	}
	// late and close: everything counts for more
	if (G.q >= 4 && G.clock <= 300.0 && FMath::Abs(G.score.home - G.score.away) <= 8) Late = 1.3f;
	const float Score = (Gain + Sack + Down + Zone + Hit + Catch) * Late;
	// the budget: with a replay in hand a score of Need is enough; spent, a play has to be that much better
	float Need = NeedBase + FMath::Max(0.f, 1.f - ReplayCredit) * 0.8f;
	if (R.kind == tag::PlayKind::Sack && LastReplayWhy == TEXT("sack")) Need += 0.8f; // (no two routine sacks back to back)
	const TCHAR* Top = TEXT("gain");
	float TopV = Gain;
	auto Consider = [&Top, &TopV](float V, const TCHAR* Name) { if (V > TopV) { TopV = V; Top = Name; } };
	Consider(Sack, TEXT("sack"));
	Consider(Down, Dn == 4 ? TEXT("fourth down") : TEXT("third down"));
	Consider(Zone, TEXT("red zone"));
	Consider(Hit, TEXT("hit"));
	Consider(Catch, TEXT("catch"));
	if (bRet) Top = TEXT("return");
	bReplayForced = Forced != nullptr;
	bReplayWorthy = bReplayForced || Score >= Need;
	ReplayScoreNow = Score;
	ReplayWhyNow = Forced ? Forced : Top;
	UE_LOG(LogTemp, Display, TEXT("TAG replay score %.2f need %.2f credit %.2f (gain %.2f sack %.2f down %.2f zone %.2f hit %.2f catch %.2f late x%.1f; jolt %.0f cm/s) -> %s, %s; %d replays in %d plays"),
		Score, Need, ReplayCredit, Gain, Sack, Down, Zone, Hit, Catch, Late, MaxJoltThisPlay, bReplayWorthy ? TEXT("replay") : TEXT("skip"), *ReplayWhyNow, ReplaysShown, ReplayPlays);
}

void AMatchDirector::SpendReplay()
{
	++ReplaysShown;
	ReplayGapMax = FMath::Max(ReplayGapMax, ReplayGap);
	ReplayGap = 0;
	ReplayCredit = FMath::Max(-1.f, ReplayCredit - 1.f);
	ReplayWhyCount.FindOrAdd(ReplayWhyNow)++;
	LastReplayWhy = ReplayWhyNow;
	UE_LOG(LogTemp, Display, TEXT("TAG replay shown: %s (score %.2f%s), %d replays in %d scrimmage plays, credit %.2f"), *ReplayWhyNow, ReplayScoreNow,
		bReplayForced ? TEXT(", always") : TEXT(""), ReplaysShown, ReplayPlays, ReplayCredit);
}

void AMatchDirector::LogReplaySummary() const
{
	FString By;
	for (const TPair<FString, int32>& KV : ReplayWhyCount)
		By += FString::Printf(TEXT(" %s %d (%.0f%%)"), *KV.Key, KV.Value, ReplaysShown ? 100.0 * KV.Value / ReplaysShown : 0.0);
	UE_LOG(LogTemp, Display, TEXT("TAG replay summary: %d replays in %d scrimmage plays (one per %.1f), longest run without one %d plays; by reason:%s"),
		ReplaysShown, ReplayPlays, ReplaysShown ? static_cast<double>(ReplayPlays) / ReplaysShown : 0.0, FMath::Max(ReplayGapMax, ReplayGap), *By);
}

// ------------------------------------------------------------------ the field's graphics (04 A7)
void AMatchDirector::TickFieldOverlays(float Dt)
{
	if (!Stadium || !Game || !Camera) return;
	UStaticMeshComponent* LosC = Stadium->OverlayLine(0);
	UStaticMeshComponent* FdC = Stadium->OverlayLine(1);
	if (!LosC || !FdC) return;
	static const bool bLit = FParse::Param(FCommandLine::Get(), TEXT("TagLitLines"));    // A/B: the old lit lines
	static const bool bNoMark = FParse::Param(FCommandLine::Get(), TEXT("TagNoDownMark"));
	// the glow by day and by night (the night's exposure is held brighter: the ribbon boards run 3 at night, 5 by day)
	static float GlowDay = FParse::Value(FCommandLine::Get(), TEXT("TagLineGlow="), GlowDay) ? GlowDay : 1.6f;
	static float GlowNight = FParse::Value(FCommandLine::Get(), TEXT("TagLineGlowNight="), GlowNight) ? GlowNight : 0.9f;
	const FLinearColor Blue(0.1f, 0.35f, 1.f), Yellow(1.f, 0.82f, 0.f), Red(1.f, 0.07f, 0.04f);
	const tag::Game& G = *Game;
	if (OverlayStadium.Get() != Stadium.Get())
	{
		// a new stadium (every match builds its own): its two lines on the emissive panel material (M_TagLED as a solid
		// colour, its lit part turned down): a broadcast graphic, not paint lit by the sun or the floodlights
		OverlayStadium = Stadium.Get();
		LosLineMID = FdLineMID = nullptr;
		OverlayDown = -1;
		bOverlayHidden = false;
		UMaterialInterface* Base = FAthleteAssets::Get().LEDMat;
		if (!bLit && Base)
		{
			auto Make = [&](const FLinearColor& C)
			{
				UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, Stadium.Get());
				M->SetVectorParameterValue(TEXT("Color"), C);
				M->SetVectorParameterValue(TEXT("ColorB"), C);
				M->SetScalarParameterValue(TEXT("Count"), 0.f);
				M->SetScalarParameterValue(TEXT("Speed"), 0.f);
				M->SetScalarParameterValue(TEXT("Diffuse"), 0.2f);
				M->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
				M->SetScalarParameterValue(TEXT("Intensity"), bBoothNight ? GlowNight : GlowDay);
				return M;
			};
			LosLineMID = Make(Blue);
			FdLineMID = Make(Yellow);
			LosC->SetMaterial(0, LosLineMID);
			FdC->SetMaterial(0, FdLineMID);
		}
	}
	// the line to gain is red on fourth down (styled as the lines are put down: the play call, the pre-snap)
	if (State == EMatchState::Call || State == EMatchState::Presnap)
	{
		const int32 Want = G.phase == tag::GamePhase::Scrimmage && !bTwoPoint ? G.down : 0;
		if ((Want == 4) != (OverlayDown == 4) || OverlayDown < 0)
		{
			const FLinearColor C = Want == 4 ? Red : Yellow;
			if (FdLineMID)
			{
				FdLineMID->SetVectorParameterValue(TEXT("Color"), C);
				FdLineMID->SetVectorParameterValue(TEXT("ColorB"), C);
			}
			else FdC->SetMaterial(0, TagColorMat(Stadium.Get(), C, 0.3f));
		}
		OverlayDown = Want;
	}
	// nothing of it in a replay (the lines mark the next play, not the one on the film), a cut-in, a close shot or a
	// break's wide shot. (Hidden in game: SetLines' own showing and hiding is left as it is.)
	const bool bHide = State == EMatchState::Replay || (CutInTarget && CutInLeft > 0.f) || CloseupTarget || BreakLeft > 0.f || bSetAfterBreak;
	if (bHide != bOverlayHidden)
	{
		bOverlayHidden = bHide;
		LosC->SetHiddenInGame(bHide);
		FdC->SetHiddenInGame(bHide);
	}
	// the down and distance on the turf beside the line before the snap ("3RD & 7"), gone at the snap
	const bool bMark = !bNoMark && !bHide && State == EMatchState::Presnap && Sim && !Sim->cfg.kick && !Sim->cfg.place && !bTwoPoint &&
		G.phase == tag::GamePhase::Scrimmage;
	if (bMark && !DownMark)
	{
		DownMark = NewObject<UTextRenderComponent>(this);
		DownMark->SetupAttachment(RootComponent);
		DownMark->SetHorizontalAlignment(EHTA_Center);
		DownMark->SetVerticalAlignment(EVRTA_TextCenter);
		DownMark->SetWorldSize(130.f);
		DownMark->SetTextRenderColor(FColor(245, 245, 238));
		DownMark->SetCastShadow(false);
		DownMark->SetVisibility(false);
		DownMark->RegisterComponent();
	}
	if (!DownMark) return;
	if (bMark)
	{
		auto Ord = [](int32 D) -> const TCHAR* { return D <= 1 ? TEXT("1ST") : D == 2 ? TEXT("2ND") : D == 3 ? TEXT("3RD") : TEXT("4TH"); };
		const bool bGoal = G.ballOn + G.toGo >= 100.0 - 1e-6;
		const FString Txt = FString::Printf(TEXT("%s & %s"), Ord(G.down), bGoal ? TEXT("GOAL") : G.toGo < 0.95 ? TEXT("INCHES") : *FString::FromInt(FMath::RoundToInt(G.toGo)));
		if (Txt != DownMarkText)
		{
			DownMarkText = Txt;
			DownMark->SetText(FText::FromString(Txt));
		}
		// on the offense's side of the line, toward the wide side of the field (clear of the ball and the interior line),
		// lying on the turf and turned to read upright from the camera
		const double Y = YD;
		const FVector LosW = ToWorld(Sim->los, Sim->by, Dir);
		const double Wide = LosW.Y > 0.0 ? -1.0 : 1.0;
		const double Lim = (tag::FIELD_W / 2 - 4) * Y;
		const FVector At(LosW.X - Dir * 2.5 * Y, FMath::Clamp(LosW.Y + Wide * 13.0 * Y, -Lim, Lim), 1.5);
		FVector Away = At - Camera->GetActorLocation();
		Away.Z = 0.0;
		Away = Away.GetSafeNormal();
		if (Away.IsNearlyZero()) Away = FVector(static_cast<double>(Dir), 0.0, 0.0);
		// (a text render faces its +X and stands up its +Z: faced to the sky, its top away from the lens)
		DownMark->SetWorldLocationAndRotation(At, FRotationMatrix::MakeFromXZ(FVector::UpVector, Away).Rotator());
	}
	if (DownMark->IsVisible() != bMark) DownMark->SetVisibility(bMark);
}

// ------------------------------------------------------------------ the stands (05 A2)
void AMatchDirector::HomeCheer(float Level, tag::TeamKey For)
{
	// (no sound, so no crowd mood to follow: the home side's own big moments only)
	if (!Audio && Stadium && For == tag::TeamKey::Home) Stadium->Hype = FMath::Max(Stadium->Hype, Level);
}

void AMatchDirector::TickStands()
{
	if (!Stadium || !Audio || !Game || State == EMatchState::Menu || bLineup) return;
	// the fans on their feet when the building roars, seated when it groans (the stands' thresholds: up past 0.5, down
	// under 0.15 - Stadium's UpdateHeroCrowd / UpdateMassFans)
	Stadium->Hype = Audio->CrowdEnergy();
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("TagAudioLog"));
	const bool bUp = bStandsUp ? Stadium->Hype > 0.15f : Stadium->Hype > 0.5f;
	if (bUp != bStandsUp)
	{
		bStandsUp = bUp;
		if (bLog) UE_LOG(LogTemp, Display, TEXT("TAG stands %s: hype %.2f (q%d %.0f, %d-%d)"), bUp ? TEXT("up") : TEXT("down"), Stadium->Hype, Game->q, Game->clock,
			Game->score.away, Game->score.home);
	}
}

void AMatchDirector::TickBroadcast(float Dt)
{
	TickFieldOverlays(Dt);
	TickStands();
}
