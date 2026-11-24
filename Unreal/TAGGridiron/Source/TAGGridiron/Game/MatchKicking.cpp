// User kicking, Madden style: the kick team set and held while the user aims (left stick: the arrow on the field) and
// swings the meter (pad: right stick pulled back for the power, pushed up straight for the accuracy; keyboard / CROSS:
// held for the power, released, pressed again on the centre). The swing is handed to the sim (Game::userKick), which
// plays the kick from it - the leg and the line of a place kick, a punt's distance, hang and landing spot, a
// kickoff's - and the pack's captured kick scenes then show it.
#include "Game/MatchDirector.h"
#include "Game/TagBindings.h"

#include "Render/Athlete.h"
#include "Render/Stadium.h"
#include "Engine/StaticMeshActor.h"
#include "Render/Coords.h"
#include "UI/GridironOverlay.h"
#include "Audio/TagAudio.h"
#include "Components/SkeletalMeshComponent.h"
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
	if (bMulti) return TeamHuman(Kicking) && !bAuto && !bMultiTest;
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
	// a played field goal: the block unit's rush look picked now (a stable pick, not the game's dice), shown while he
	// aims and then rushed by the play (StartPlaceKick)
	FgUserLook = -1;
	if (Kind == 2 && tag::tune().fgPlay > 0 && Game)
	{
		const int32 H = static_cast<int32>((Game->log.size() * 7 + Game->q * 3 + Game->score.home + Game->score.away * 5) % 10);
		FgUserLook = H < 4 ? 0 : H < 6 ? 1 : H < 8 ? 2 : 3;
	}
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
	// the swing is the kicking side's: one player's controller, or in a multiplayer game his caller's pad / keys
	FTagReader In(PC);
	if (bMulti)
	{
		const int32 Kh = CallerOf(UK.Kind == 0 ? Game->kicking : Game->poss);
		In = Humans.IsValidIndex(Kh) ? Humans[Kh].Reader() : FTagReader();
	}
	// (the kick button is the player's own: Settings > Controls, kicking)
	const TagBind::FScope BindScope(TagBind::ECtx::Kicking);
	if (In)
	{
		LX = In->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
		if (In->IsInputKeyDown(EKeys::A) || In->IsInputKeyDown(EKeys::Left)) LX -= 1.f;
		if (In->IsInputKeyDown(EKeys::D) || In->IsInputKeyDown(EKeys::Right)) LX += 1.f;
		RX = In->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		RY = RightStickY(In);
		bDown = In->IsInputKeyDown(EKeys::SpaceBar) || In->IsInputKeyDown(EKeys::Gamepad_FaceButton_Bottom);
		bHit = In->WasInputKeyJustPressed(EKeys::SpaceBar) || In->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
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
		if (FMath::Abs(RY) > 0.6f) { UK.Stage = 1; UK.bPadSwing = true; UK.MeterT = 0.f; UK.NeutralT = 0.f; UK.BackSign = RY > 0.f ? 1.f : -1.f; ATagAudio::Ui(TEXT("UiTick")); }
		else if (bHit) { UK.Stage = 1; UK.bPadSwing = false; UK.MeterT = 0.f; ATagAudio::Ui(TEXT("UiTick")); }
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
		else if (!bDown) { UK.Stage = 2; UK.MeterT = 0.f; UK.Acc = -1.f; ATagAudio::Ui(TEXT("UiTick")); }
		break;
	case 2:
		UK.MeterT += Dt;
		UK.Acc = -1.f + 2.f * UK.MeterT / 0.9f;
		if (bHit || UK.Acc >= 1.f) { UK.Acc = FMath::Clamp(UK.Acc, -1.f, 1.f); bFire = true; }
		break;
	default:
		break;
	}
	if (bFire) ATagAudio::Ui(TEXT("UiLock"));
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
		Overlay->SetHint(M.bPad ? FString(TEXT("L-STICK aim   -   R-STICK pull back for power, push straight up to kick"))
								: TagBind::TranslateHint(TEXT("A / D aim   -   hold SPACE for power, release, SPACE again on the centre"), false));
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
		else if (Sim && Sim->cfg.place)
		{
			Lat = Sim->cfg.place->miss;
			How = TEXT("place kick");
		}
		UE_LOG(LogTemp, Display, TEXT("TAG user kick lands: aim %.1f deg -> %.1f yd off the middle (+ kicker's left) [%s]"), AimDeg, Lat, How);
	}
}

// ------------------------------------------------------------------ the played field goal (Sim/PlaceKick.cpp)
namespace
{
	// the pack's field goal capture (FG_Normal_Kick_v1), seconds into it: the ball leaves the snapper's hands, reaches the
	// holder's, stands on its point, the foot meets it (as MatchDirector.cpp's staging reads it). The play's operation is
	// quicker than the capture's 1.57 s: the clips are played at the rate that puts their foot on the sim's.
	constexpr float PkSnapAt = 0.10f, PkCatchAt = 0.62f, PkPlaceAt = 1.0f, PkKickAt = 1.667f;
	// where each of the three starts in the capture (m: x left, y back; the spot the ball is kicked from is y 6.55) and
	// his heading (deg, turned left of downfield)
	struct FPkPart { float X, Y, H; };
	constexpr FPkPart PkParts[3] = {{-0.003f, 0.792f, 0.f}, {-0.558f, 6.563f, 90.f}, {1.881f, 8.688f, -40.3f}};
	constexpr float PkSpotY = 6.55f;
}

void AMatchDirector::StartPlaceKick(bool bPat, const std::vector<tag::Player*>& KickTeam, const std::vector<tag::Player*>& BlockTeam, double SetT, int32 Look)
{
	tag::Game& G = *Game;
	std::optional<tag::Side> US;
	if (UserOff()) US = tag::Side::Off;
	else if (UserDef()) US = tag::Side::Def;
	if (Sim) SyncHumanLast();
	// (per-play maps keyed by the last sim's men: see StartLiveReturn)
	PostPos.clear();
	PostStay.clear();
	PostVel.clear();
	PostYaw.clear();
	if (bSetAfterBreak)
	{
		bSetAfterBreak = false;
		if (BreakLeft <= 0.f) bCutNext = true;
	}
	Kick.reset(); // (a user's aim was staged: the play starts from the same spots)
	Sim = G.placeKickSim(bPat, KickTeam, BlockTeam, US, SetT, Look);
	FgUserLook = -1;
	SetupHumans();
	bPlaceKick = true;
	FgSceneSim = 0;
	FgCallSim = 0;
	Dir = G.worldDir();
	std::vector<tag::Player*> Ps;
	for (tag::Ent* E : Sim->ents) Ps.push_back(E->p);
	ShowOnly(Ps);
	CapturePrev();
	Stadium->SetLines(-1, -1);
	Labels.clear();
	State = EMatchState::Live;
	Acc = 0;
	ResetReplayBuf();
	ReplayClock = 0.f;
	ReplayPostRec = 0.f;
	bReplayWorthy = false;
	if (US && *US == tag::Side::Def)
	{
		const TagBind::FScope HintScope(TagBind::ECtx::Defense);
		Overlay->SetHint(PadUI() ? PadHint(TEXT("Field goal block    L-STICK rush    TRIANGLE leap at the line    SQUARE lay out    CIRCLE switch"))
			: FString(TEXT("Field goal block    WASD rush    C leap at the line    SPACE lay out    TAB switch")));
	}
	else Overlay->SetHint(TEXT(""));
	const tag::PlaceKickSpec& K = *Sim->cfg.place;
	UE_LOG(LogTemp, Display, TEXT("TAG place kick: %s from the %.0f, spot %.1f, look %d, snap at %.2f s, foot %.2f s after it, launch %.1f yd/s at %.1f deg, %s"),
		bPat ? TEXT("extra point") : TEXT("field goal"), Sim->los, K.spotX, Sim->placeLook, K.setT, K.opT, K.v, K.elevDeg,
		K.good ? TEXT("good if untouched") : K.isShort ? TEXT("short") : TEXT("wide"));
	SyncPlay(0.f, 1.f);
}

bool AMatchDirector::SyncPlaceOperation(tag::Ent* E, AAthlete* A, float Dt)
{
	static const bool bNoKickScene = FParse::Param(FCommandLine::Get(), TEXT("TagNoFgScene"));
	const FAthleteAssets::FKickScene& Sc = FAthleteAssets::Get().FgScene;
	if (bNoKickScene || !Sc.Ok() || !Sim || !Sim->cfg.place || !A) return false;
	const int32 Who = E == Sim->placeSnapper ? 0 : E == Sim->placeHolder ? 1 : E == Sim->placeKicker ? 2 : -1;
	if (Who < 0) return false;
	if (FgSceneSim != Sim->serial)
	{
		FgSceneSim = Sim->serial;
		FgStage[0] = FgStage[1] = FgStage[2] = 0;
		FgHolderA = nullptr;
	}
	// a block: the ball is everyone's, the three go after it as the sim has them
	if (Sim->placeBlocked) return false;
	const tag::PlaceKickSpec& K = *Sim->cfg.place;
	UAnimSequence* Clip = Who == 0 ? Sc.Snap.Get() : Who == 1 ? Sc.Hold.Get() : Sc.Kick.Get();
	UAnimSequence* Wait = Who == 0 ? Sc.SnapWait.Get() : Who == 1 ? Sc.HoldWait.Get() : Sc.KickWait.Get();
	const float Rate = (PkKickAt - PkSnapAt) / static_cast<float>(FMath::Max(0.8, K.opT));
	const float Tc = PkSnapAt + (static_cast<float>(Sim->t) - static_cast<float>(K.setT)) * Rate; // (capture time)
	// (the snapper is a blocker once the ball is away: the sim's, off the capture's end of the snap)
	const float EndT = Who == 0 ? PkKickAt + 0.4f : Clip->GetPlayLength() - 0.05f;
	const FVector SpotW = ToWorld(K.spotX, Sim->by, Dir);
	const FVector FwdW = (ToWorld(K.spotX + 1.0, Sim->by, Dir) - SpotW).GetSafeNormal();
	const FVector LeftW = FVector::CrossProduct(FwdW, FVector::UpVector);
	const float FwdYaw = FwdW.Rotation().Yaw;
	const FPkPart& P = PkParts[Who];
	const FVector At = SpotW + LeftW * (P.X * 100.f) - FwdW * ((P.Y - PkSpotY) * 100.f);
	const float Yaw = FwdYaw - P.H;
	if (Who == 1) FgHolderA = A;
	int32& Stage = FgStage[Who];
	if (Tc < EndT)
	{
		if (Tc < 0.f && Stage == 0 && Wait) { A->BeginScene(Wait, At, Yaw, FMath::Frac(Who * 0.37f) * Wait->GetPlayLength(), true); Stage = 1; }
		if (Tc >= 0.f && Stage < 2)
		{
			A->BeginScene(Clip, At, Yaw, Tc, false);
			A->SetSceneRate(Rate);
			Stage = 2;
		}
		A->Drive(At, Yaw, FVector::ZeroVector, static_cast<float>(E->vmax), TEXT("scene"), 0.f, Dt, false);
		A->SetSelected(false);
		A->SetLabel(FString());
		return true;
	}
	if (Who == 0 || !Sim->isLive()) return false;
	// the holder and the kicker after their capture: where it left them, the sim's animation there (up from the hold,
	// the kicker's celebration on a make)
	// (the capture's last pose held - its scene clamps at the end - until the whistle: handed to the locomotion here he
	// turned and sprinted for the sim's spot off the capture's, an 18 cm pop and a run, blk3 c006)
	A->Drive(At, Yaw, FVector::ZeroVector, static_cast<float>(E->vmax), TEXT("scene"), 0.f, Dt, false);
	A->SetSelected(false);
	A->SetLabel(FString());
	return true;
}

void AMatchDirector::SyncPlaceBall()
{
	if (!Sim || !Sim->cfg.place || !Ball || Sim->placeKicked || Sim->placeBlocked) return;
	static const bool bNoKickScene = FParse::Param(FCommandLine::Get(), TEXT("TagNoFgScene"));
	if (bNoKickScene || !FAthleteAssets::Get().FgScene.Ok()) return; // (the sim's own ball: Snap / hold positions)
	const tag::PlaceKickSpec& K = *Sim->cfg.place;
	const float Rate = (PkKickAt - PkSnapAt) / static_cast<float>(FMath::Max(0.8, K.opT));
	const float Tc = PkSnapAt + (static_cast<float>(Sim->t) - static_cast<float>(K.setT)) * Rate;
	const FVector SpotW = ToWorld(K.spotX, Sim->by, Dir);
	const FVector FwdW = (ToWorld(K.spotX + 1.0, Sim->by, Dir) - SpotW).GetSafeNormal();
	const float FwdYaw = FwdW.Rotation().Yaw;
	// on the turf under the snapper, back to the holder's hands, set on the spot
	const FVector Turf = ToWorld(Sim->los - 0.15, Sim->by, Dir, 0.12);
	const FVector Tee = SpotW + FVector(0, 0, 15);
	FVector Hands = SpotW + FVector(0, 0, 50);
	if (AAthlete* H = FgHolderA.Get())
		if (USkeletalMeshComponent* M = H->Visible())
			if (M->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE && M->GetBoneIndex(TEXT("hand_r")) != INDEX_NONE)
				Hands = 0.5 * (M->GetBoneLocation(TEXT("hand_l")) + M->GetBoneLocation(TEXT("hand_r")));
	FVector B = Turf;
	FRotator R(0, FwdYaw, 0);
	if (Tc >= PkSnapAt && Tc < PkCatchAt)
	{
		// the snap: a flat spiral, 7 yd in four tenths of a second
		const float Al = (Tc - PkSnapAt) / (PkCatchAt - PkSnapAt);
		B = FMath::Lerp(Turf + FVector(0, 0, 25), Hands, Al) + FVector(0, 0, 22.f * FMath::Sin(PI * Al));
		R = FRotator(0, FwdYaw, Tc * 1400.f);
	}
	else if (Tc >= PkCatchAt)
	{
		// down onto the spot, stood on its point, the laces away
		const float Al = FMath::SmoothStep(0.f, 1.f, (Tc - PkCatchAt) / (PkPlaceAt - PkCatchAt));
		B = FMath::Lerp(Hands, Tee, Al);
		R = FRotator(82.f * Al, FwdYaw, 0);
	}
	Ball->SetActorLocationAndRotation(B, R);
}

void AMatchDirector::TickPlaceKickCalls()
{
	if (!Sim || !Sim->cfg.place || FgCallSim == Sim->serial || !Overlay) return;
	const tag::Player* Kp = Sim->placeKicker ? Sim->placeKicker->p : nullptr;
	const int32 Yd = FMath::RoundToInt(100.0 - Sim->los + 17.0);
	const FString Who = Kp ? FString::Printf(TEXT("#%d %s"), Kp->number, UTF8_TO_TCHAR(Kp->last.c_str())) : FString();
	const FString What = Sim->cfg.place->pat ? FString(TEXT("extra point")) : FString::Printf(TEXT("%d-yard field goal"), Yd);
	if (Sim->placeBlocked)
	{
		FgCallSim = Sim->serial;
		const tag::Player* Bp = Sim->placeBlockBy ? Sim->placeBlockBy->p : nullptr;
		Overlay->Toast(Bp ? FString::Printf(TEXT("BLOCKED BY #%d %s"), Bp->number, UTF8_TO_TCHAR(Bp->last.c_str())) : FString(TEXT("BLOCKED")));
		Stadium->Hype = 0.8f;
		UE_LOG(LogTemp, Display, TEXT("TAG place kick blocked: %s by %s, %.2f s after the foot, %.1f yd past the spot"), *What,
			Sim->placeBlockBy ? UTF8_TO_TCHAR(Sim->placeBlockBy->role.c_str()) : TEXT("-"), Sim->placeBlockT - Sim->placeKickT(), Sim->placeBlockX);
		return;
	}
	if (!Sim->placeCrossed && !Sim->placeShort) return;
	FgCallSim = Sim->serial;
	const bool bGood = Sim->placeGood;
	ReleaseHudScore();
	Overlay->Banner(bGood ? TEXT("IT'S GOOD") : TEXT("NO GOOD"), Who + TEXT("  ") + What + (Sim->placeTipped ? TEXT(", tipped") : TEXT("")),
		bGood ? FLinearColor(0.24f, 0.86f, 0.52f) : FLinearColor(1.f, 0.35f, 0.35f));
	UE_LOG(LogTemp, Display, TEXT("TAG place kick at the posts: across %.2f yd, height %.2f yd, called %s%s"), Sim->placeCrossY, Sim->placeCrossZ,
		bGood ? TEXT("good") : Sim->placeShort ? TEXT("short") : TEXT("no good"), Sim->placeTipped ? TEXT(" (tipped)") : TEXT(""));
}
