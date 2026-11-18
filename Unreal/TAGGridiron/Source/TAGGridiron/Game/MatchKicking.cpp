// User kicking, Madden style: the kick team set and held while the user aims (left stick: the arrow on the field) and
// swings the meter (pad: right stick pulled back for the power, pushed up straight for the accuracy; keyboard / CROSS:
// held for the power, released, pressed again on the centre). The swing is handed to the sim (Game::userKick), which
// plays the kick from it - the leg and the line of a place kick, a punt's distance, hang and landing spot, a
// kickoff's - and the pack's captured kick scenes then show it.
#include "Game/MatchDirector.h"

#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "UI/GridironOverlay.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"

using namespace TagCoords;

bool AMatchDirector::UserKicks(tag::TeamKey Kicking) const
{
	static const bool bAutoKicks = FParse::Param(FCommandLine::Get(), TEXT("TagUserKicks"));
	if (bAutoKicks) return true;
	// (the look-dev and pad tests drive a user game with nobody at the meter: their kicks are the CPU's)
	return UserTeam.has_value() && *UserTeam == Kicking && !bAuto && !bHighlights && !bArtTest && !bPadTest && !bPauseTest && !bMoveShot;
}

void AMatchDirector::BeginUserKick(int Kind, int D, const std::vector<tag::Player*>& KickU, const std::vector<tag::Player*>& RecvU, double Los, bool bPat,
	std::function<void()> Fire)
{
	// the team is staged as the kick will be played (a stand-in result: no ball in the air yet), and held
	tag::SpecialResult Stand;
	tag::KickFlight F;
	if (Kind == 0) { F.fromX = Los; F.toX = Los + 62; F.hang = 4.0; F.height = 22; }
	else if (Kind == 1) { F.fromX = Los - 14; F.toX = Los + 42; F.hang = 4.4; F.height = 26; }
	else { F.fromX = Los - 7; F.toX = 110; F.hang = 2.4; F.height = 12; }
	Stand.flight = F;
	StartKick(Stand, D, Kind, KickU, RecvU, Los);
	if (!Kick) { Fire(); return; }
	Kick->bAwait = true;
	Kick->HoldT = Kick->SceneT0 >= 0.f ? FMath::Max(0.05f, Kick->SceneT0 - 0.35f) : 0.f;
	UK = FUserKick();
	UK.bOn = true;
	UK.Kind = Kind;
	UK.Dir = D;
	UK.Los = Los;
	UK.SpotX = Kick->FromX;
	UK.bPat = bPat;
	UK.Fire = MoveTemp(Fire);
	// automation (-TagUserKicks): a swing drawn per kick, a little off now and then
	static int32 GAutoSeq = 0;
	const int32 Sq = GAutoSeq++;
	UK.AutoPower = 0.82f + 0.2f * FMath::Frac(Sq * 0.618f);
	UK.AutoAcc = -0.35f + 0.7f * FMath::Frac(Sq * 0.414f + 0.3f);
	UK.AutoAim = Kind == 1 ? (Sq % 2 ? 14.f : -14.f) : Kind == 0 ? (Sq % 3 - 1) * 10.f : 0.f;
	bCutNext = true; // (a cut to the kicker's shoulder)
	UE_LOG(LogTemp, Display, TEXT("TAG user kick: kind %d los %.1f spot %.1f"), Kind, Los, UK.SpotX);
}

bool AMatchDirector::KickAimCamera(FVector& OutPos, FVector& OutLook) const
{
	if (!UK.bOn || !Kick || !Kick->bAwait) return false;
	const double Mid = tag::FIELD_W / 2;
	const double T = FMath::Tan(FMath::DegreesToRadians(UK.AimDeg));
	// over the kicker's shoulder, low: the field (or the posts) ahead, the arrow on it
	// (a kickoff's kicker stands at the top of his run-up, yards behind the ball: the camera behind him)
	const double Back = UK.Kind == 0 && Kick->bDynamicKo ? 0.5 + Kick->Lead * 4.6 + 4.5 : 8.5;
	OutPos = ToWorld(UK.SpotX - Back, Mid - 0.8, UK.Dir, UK.Kind == 0 ? 2.8 : 2.4);
	OutLook = ToWorld(UK.SpotX + 30, Mid + 30 * T * 0.6, UK.Dir, UK.Kind == 2 ? 3.2 : 1.0);
	return true;
}

void AMatchDirector::TickUserKick(float Dt)
{
	if (!UK.bOn || UK.Stage >= 3 || !Game) return;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	static const bool bAutoKicks = FParse::Param(FCommandLine::Get(), TEXT("TagUserKicks"));
	float LX = 0.f, RX = 0.f, RY = 0.f;
	bool bDown = false, bHit = false;
	if (PC)
	{
		LX = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
		if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left)) LX -= 1.f;
		if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) LX += 1.f;
		RX = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		RY = RightStickY(PC);
		bDown = PC->IsInputKeyDown(EKeys::SpaceBar) || PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom);
		bHit = PC->WasInputKeyJustPressed(EKeys::SpaceBar) || PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
	}
	if (bAutoKicks)
	{
		// the same swing a player makes, on the keyboard path: aim, hold, let go at the power, press at the accuracy
		UK.AutoT += Dt;
		LX = FMath::Clamp((UK.AutoAim - UK.AimDeg) * -0.3f, -1.f, 1.f) * (UK.AutoT > 0.4f ? 1.f : 0.f);
		RX = RY = 0.f;
		bHit = (UK.Stage == 0 && UK.AutoT > 1.6f) || (UK.Stage == 2 && UK.Acc >= UK.AutoAcc);
		bDown = UK.Stage == 0 ? bHit : UK.Stage == 1 ? UK.Power < UK.AutoPower : false;
	}
	const float MaxAim = UK.Kind == 2 ? 12.f : UK.Kind == 1 ? 30.f : 25.f;
	// the stick reads the way the camera faces: right on the stick aims to the kicking team's right (- in the sim)
	if (UK.Stage <= 1 && FMath::Abs(LX) > 0.15f) UK.AimDeg = FMath::Clamp(UK.AimDeg - LX * 14.f * Dt, -MaxAim, MaxAim);
	auto Swing = [](float T) { const float Ph = FMath::Fmod(T, 2.3f); return Ph < 1.15f ? Ph : 2.3f - Ph; }; // 0 -> 1.15 -> 0 ...
	bool bFire = false;
	switch (UK.Stage)
	{
	case 0:
		// the stick pulled back (down): the swing starts. (Pads report the right stick's y either way round - a DualSense
		// through GameInput came in reversed - so "back" is whichever way it was pulled, and the kick the flick the other way.)
		if (FMath::Abs(RY) > 0.6f) { UK.Stage = 1; UK.bPadSwing = true; UK.MeterT = 0.f; UK.NeutralT = 0.f; UK.BackSign = RY > 0.f ? 1.f : -1.f; }
		else if (bHit) { UK.Stage = 1; UK.bPadSwing = false; UK.MeterT = 0.f; }
		break;
	case 1:
		UK.MeterT += Dt;
		UK.Power = Swing(UK.MeterT);
		if (UK.bPadSwing)
		{
			if (RY * UK.BackSign < -0.55f)
			{
				// flicked up: its lean off straight up is the accuracy (25 degrees either way is the edge of the meter)
				UK.Acc = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(RX, FMath::Abs(RY))) / 25.f, -1.f, 1.f);
				bFire = true;
			}
			else if (RY * UK.BackSign < -0.15f)
			{
				// on its way up: the needle follows the stick's lean, so the meter shows the line being kicked
				UK.Acc = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(RX, FMath::Abs(RY))) / 25.f, -1.f, 1.f);
				UK.bLeanShown = true;
			}
			else if (RY * UK.BackSign < 0.3f)
			{
				UK.NeutralT += Dt;
				if (UK.NeutralT > 0.35f) { UK.Stage = 0; UK.Power = 0.f; } // let go of: no kick
			}
			else UK.NeutralT = 0.f;
		}
		else if (!bDown) { UK.Stage = 2; UK.MeterT = 0.f; UK.Acc = -1.f; }
		break;
	case 2:
		UK.MeterT += Dt;
		UK.Acc = -1.f + 2.f * UK.MeterT / 0.9f;
		if (bHit || UK.Acc >= 1.f) { UK.Acc = FMath::Clamp(UK.Acc, -1.f, 1.f); bFire = true; }
		break;
	default:
		break;
	}
	// the meter and the arrow
	const tag::TeamKey KT = UK.Kind == 0 ? Game->kicking : Game->poss;
	const tag::Player* Kicker = UK.Kind == 1 ? Game->punter(KT) : Game->kicker(KT);
	FKickMeterUI M;
	M.bOn = true;
	M.Stage = UK.Stage;
	M.Power = UK.Power;
	M.Acc = UK.Acc;
	M.bAccShown = UK.Stage >= 2 || bFire || UK.bLeanShown;
	M.bPad = PadUI();
	if (UK.Kind == 2) M.Title = UK.bPat ? FString(TEXT("EXTRA POINT")) : FString::Printf(TEXT("FIELD GOAL  -  %d YD"), FMath::RoundToInt(100.0 - UK.Los + 17.0));
	else M.Title = UK.Kind == 1 ? TEXT("PUNT") : TEXT("KICKOFF");
	if (Kicker) M.Title += FString::Printf(TEXT("    #%d %s  KPW %d  KAC %d"), Kicker->number, UTF8_TO_TCHAR(Kicker->last.c_str()),
		FMath::RoundToInt(Kicker->r.kickPower), FMath::RoundToInt(Kicker->r.kickAcc));
	{
		// the wind as the kicker feels it: along (with / into) and across (toward his left / right), mph
		const double Along = Game->weather.windX * Game->worldDir(KT), Across = -Game->weather.windY * Game->worldDir(KT);
		const double Mph = FMath::Sqrt(Along * Along + Across * Across) * 2.045;
		if (Mph >= 1.0)
			M.Sub = FString::Printf(TEXT("WIND %d MPH %s%s"), FMath::RoundToInt(Mph), Along > 0.5 ? TEXT("WITH") : Along < -0.5 ? TEXT("INTO") : TEXT(""),
				FMath::Abs(Across) > 0.5 ? (Across > 0 ? TEXT(" > LEFT") : TEXT(" > RIGHT")) : TEXT(""));
	}
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(M.Viewport);
	if (PC)
	{
		const double Mid = tag::FIELD_W / 2, Tn = FMath::Tan(FMath::DegreesToRadians(UK.AimDeg));
		const float Pv = UK.Stage >= 1 ? FMath::Min(UK.Power, 1.f) : 0.85f;
		const double Kp = Kicker ? Kicker->r.kickPower : 80.0;
		const double L = UK.Kind == 2 ? 110.0 - UK.SpotX : UK.Kind == 1 ? (52.0 + (Kp - 80) * 0.3) * (0.35 + 0.65 * Pv) : (67.0 + (Kp - 75) * 0.35) * (0.45 + 0.55 * Pv);
		const double H = UK.Kind == 2 ? 10.0 : 0.16 * L; // (an arrow, flatter than the real flight: the real one climbed out of the frame)
		const double Span = UK.Kind == 2 ? L * 1.55 : L;
		for (int32 i = 0; i <= 24; ++i)
		{
			const double X = UK.SpotX + L * i / 24.0, F = (X - UK.SpotX) / Span;
			FVector2D Scr;
			if (PC->ProjectWorldLocationToScreen(ToWorld(X, Mid + (X - UK.SpotX) * Tn, UK.Dir, 0.3 + 4.0 * H * F * (1.0 - F)), Scr, true)) M.Arc.Add(Scr);
		}
	}
	{
		// automation: a shot with the meter up, the accuracy needle on its way across
		static int32 GKickShots = 0;
		static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("TagUserKicks"));
		if (bShots && GKickShots < 6 && UK.Stage == 2 && UK.Acc > -0.45f && UK.Acc - 2.f * Dt / 0.9f <= -0.45f)
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_userkick_%d.png"), GKickShots++), true, false);
	}
	if (Overlay)
	{
		Overlay->SetKickMeter(M);
		Overlay->SetHint(M.bPad ? TEXT("L-STICK aim   -   R-STICK pull back for power, push straight up to kick")
								: TEXT("A / D aim   -   hold SPACE for power, release, SPACE again on the centre"));
	}
	if (bFire)
	{
		UK.Stage = 3;
		tag::Game::UserKick U;
		U.power = UK.Power;
		U.aimDeg = UK.AimDeg;
		// (the needle reads right of centre as +; the sim's line is + toward the kicking team's LEFT, as the aim is:
		// a kick missed right on the meter hooked left)
		U.acc = -UK.Acc;
		Game->userKick = U;
		UE_LOG(LogTemp, Display, TEXT("TAG user kick swing: kind %d power %.2f aim %.1f acc %.2f (%s)"), UK.Kind, UK.Power, UK.AimDeg, UK.Acc,
			UK.bPadSwing ? TEXT("stick") : TEXT("button"));
		PendingKickFire = UK.Fire; // (played next tick: it replaces the kick being updated now)
	}
}

void AMatchDirector::FirePendingKick()
{
	if (!PendingKickFire) return;
	std::function<void()> F = MoveTemp(PendingKickFire);
	PendingKickFire = nullptr;
	UK.bOn = false;
	if (Overlay)
	{
		// the meter stays up a moment with the swing on it (power and where the needle stopped), then goes
		Overlay->HoldKickResult();
		Overlay->SetHint(TEXT(""));
		FTimerHandle H;
		GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]
		{
			if (Overlay && !UK.bOn) Overlay->SetKickMeter(FKickMeterUI());
		}), 1.6f, false);
	}
	const double AimDeg = UK.AimDeg;
	const int32 KDir = UK.Dir;
	bCutNext = true; // (back to the game camera for the kick)
	Kick.reset();    // (the staged stand-in: the kick played now replaces it, or a live return takes over)
	F();
	if (Game) Game->userKick.reset(); // (whatever the kick did not read)
	// where it is going, in the kicking team's frame (+ its left), against the aim: the check that the ball goes where
	// the arrow pointed (a live return's spot is in the receiving team's frame: through the world and back)
	{
		double Lat = 0.0;
		const TCHAR* How = TEXT("none");
		if (Sim && Sim->cfg.kick && State != EMatchState::Kick)
		{
			Lat = TagCoords::FromWorld(ToWorld(Sim->cfg.kick->landX, Sim->cfg.kick->landY, Dir), KDir).y - tag::FIELD_W / 2;
			How = TEXT("live return");
		}
		else if (Kick && Kick->Res.flight && Kick->Res.flight->endY)
		{
			Lat = *Kick->Res.flight->endY - Kick->Res.flight->y;
			How = TEXT("flight");
		}
		UE_LOG(LogTemp, Display, TEXT("TAG user kick lands: aim %.1f deg -> %.1f yd off the middle (+ kicker's left) [%s]"), AimDeg, Lat, How);
	}
}
