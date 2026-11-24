// -TagPadPlayTest: the new in-game pad controls pressed into the player controller, the way a pad presses them, in a user
// game (the play calls are made for the user, a run on offense and Cover 3 on defense). Each check reads what the sim or
// the director did with the press: R1 with the ball covers it up, R1 on defense goes for the strip, L2 held strafes,
// CROSS held after the whistle hurries up (no call screen: the last play again), TRIANGLE held spikes it, VIEW tapped
// keeps the camera and VIEW held changes it. PASS / FAIL lines, "TAG padplaytest done", then the game exits.
#include "Game/MatchDirector.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "Sim/PlaySim.h"
#include "UI/GridironOverlay.h"

void AMatchDirector::PadKey(const FKey& Key, bool bDown)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	FViewport* VP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	if (PC) PC->InputKey(FInputKeyEventArgs(VP, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Key, bDown ? IE_Pressed : IE_Released, 1.f, true, FPlatformTime::Cycles64()));
}

void AMatchDirector::TickPadPlayTest(float Dt)
{
	if (!bPadPlayTest || !Game || PadPlay.Step == 99) return;
	FPadPlay& T = PadPlay;
	T.Clock += Dt;
	auto Check = [&](bool bOk, const TCHAR* What)
	{
		(bOk ? T.Pass : T.Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG padplaytest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), What);
	};
	auto Done = [&]()
	{
		UE_LOG(LogTemp, Display, TEXT("TAG padplaytest done: %d pass, %d fail"), T.Pass, T.Fail);
		FPlatformMisc::RequestExit(false);
	};
	if (T.Clock > 600.f) { Check(false, TEXT("the test ran out of time")); Done(); T.Clock = -1e9f; return; }
	tag::PlaySim* S = Sim.get();
	// the snap: CROSS once the men are set (the user's offense)
	if (State == EMatchState::Presnap && UserOff() && S && !S->walking() && PresnapClock > 1.2f && !T.bSnapSent)
	{
		T.bSnapSent = true;
		PadKey(EKeys::Gamepad_FaceButton_Bottom, true);
		T.ReleaseCross = 0.08f;
	}
	if (T.ReleaseCross > 0.f && (T.ReleaseCross -= Dt) <= 0.f) PadKey(EKeys::Gamepad_FaceButton_Bottom, false);
	if (State == EMatchState::Live) T.bSnapSent = false;

	switch (T.Step)
	{
	case 0: // R1 with the ball: the carrier covers up
		if (State == EMatchState::Live && S && UserOff() && S->user && S->user->hasBall && S->user != S->qb)
		{
			if (T.HoldT == 0.f) PadKey(EKeys::Gamepad_RightShoulder, true);
			if ((T.HoldT += Dt) > 0.3f)
			{
				Check(S->user->protecting, TEXT("R1 held with the ball covers it up (Ent::protecting)"));
				PadKey(EKeys::Gamepad_RightShoulder, false);
				T.HoldT = 0.f;
				T.Step = 1;
			}
		}
		break;
	case 1: // CROSS held after the whistle: the hurry-up
		if (State == EMatchState::Post && UserOff() && S && S->result && !S->result->td && !S->result->turnover)
		{
			if (T.HoldT == 0.f) PadKey(EKeys::Gamepad_FaceButton_Bottom, true);
			if ((T.HoldT += Dt) > 0.5f)
			{
				Check(bHurryNext, TEXT("CROSS held after the whistle asks for the hurry-up"));
				PadKey(EKeys::Gamepad_FaceButton_Bottom, false);
				T.HoldT = 0.f;
				T.Step = 2;
			}
		}
		break;
	case 2: // ... and the next snap comes up with no call screen, the same play
		if (State == EMatchState::Presnap && S)
		{
			Check(!Overlay->HasModal() && S->cfg.offPlay == LastUserPlay, TEXT("the hurry-up goes to the line with the last play, no call screen"));
			T.Step = 3;
		}
		break;
	case 3: // TRIANGLE held after the whistle: the spike
		if (State == EMatchState::Post && UserOff() && S && S->result && !S->result->td && !S->result->turnover && Game->phase == tag::GamePhase::Scrimmage)
		{
			if (T.HoldT == 0.f) PadKey(EKeys::Gamepad_FaceButton_Top, true);
			if ((T.HoldT += Dt) > 0.5f)
			{
				Check(bSpikeNext, TEXT("TRIANGLE held after the whistle asks for the spike"));
				PadKey(EKeys::Gamepad_FaceButton_Top, false);
				T.HoldT = 0.f;
				T.Step = 4;
			}
		}
		break;
	case 4:
		if (State == EMatchState::Presnap && S)
		{
			Check(S->cfg.offPlay && S->cfg.offPlay->type == tag::PlayType::Spike, TEXT("the spike is the next play"));
			T.Step = 5;
		}
		break;
	case 5: // VIEW tapped: the camera stays
		if (State == EMatchState::Presnap || State == EMatchState::Live || State == EMatchState::Post)
		{
			if (T.HoldT == 0.f) { T.CamWas = static_cast<int32>(CamMode); PadKey(EKeys::Gamepad_Special_Left, true); }
			if ((T.HoldT += Dt) > 0.1f)
			{
				PadKey(EKeys::Gamepad_Special_Left, false);
				T.HoldT = 0.f;
				T.Wait = 0.3f;
				T.Step = 6;
			}
		}
		break;
	case 6:
		if ((T.Wait -= Dt) <= 0.f)
		{
			Check(static_cast<int32>(CamMode) == T.CamWas, TEXT("VIEW tapped keeps the camera"));
			T.Step = 7;
		}
		break;
	case 7: // VIEW held: the camera changes
		if (T.HoldT == 0.f) { T.CamWas = static_cast<int32>(CamMode); PadKey(EKeys::Gamepad_Special_Left, true); }
		if ((T.HoldT += Dt) > 0.7f)
		{
			PadKey(EKeys::Gamepad_Special_Left, false);
			Check(static_cast<int32>(CamMode) != T.CamWas, TEXT("VIEW held half a second changes the camera"));
			CamMode = static_cast<ECamMode>(T.CamWas); // (back to the camera the test started on)
			T.HoldT = 0.f;
			T.Step = 8;
			UserTeam = tag::other(Game->poss); // the user goes over to the defense for the next snap
		}
		break;
	case 8: // on defense: R1 goes for the strip
		if (State == EMatchState::Live && S && UserDef() && S->user && S->t > 0.5)
		{
			if (T.HoldT == 0.f) PadKey(EKeys::Gamepad_RightShoulder, true);
			if ((T.HoldT += Dt) > 0.1f)
			{
				PadKey(EKeys::Gamepad_RightShoulder, false);
				Check(S->user->stripT > 0.0, TEXT("R1 on defense goes for the ball (Ent::stripT)"));
				T.HoldT = 0.f;
				T.Step = 9;
			}
		}
		break;
	case 9: // L2 held: the strafe
		if (State == EMatchState::Live && S && UserDef() && S->user)
		{
			if (T.HoldT == 0.f) PadKey(EKeys::Gamepad_LeftTrigger, true);
			if ((T.HoldT += Dt) > 0.3f)
			{
				Check(S->user->strafing, TEXT("L2 held on defense strafes (Ent::strafing)"));
				PadKey(EKeys::Gamepad_LeftTrigger, false);
				T.HoldT = 0.f;
				T.Step = 10;
			}
		}
		else if (State != EMatchState::Live && T.Step == 9 && S && !S->isLive() && S->result) { Check(false, TEXT("the play ended before L2 was held")); T.Step = 10; }
		break;
	default:
		Done();
		T.Step = 99;
		T.Clock = -1e9f;
		break;
	}
}
