// The feel checks (Game/MatchFeel.cpp), each ending in "TAG RESULT <mode> pass=<n> fail=<n>" (Game/TagResult.h).
//
// -TagFeelTest: a user game, the calls made for him (CallPlays -> FeelTestCall), the CPU kicking for him. First the checks
// that need no play: the steering copy against the sim's own law, the Authentic / Responsive turning table, top speed
// untouched, the camera's trauma shake (smooth, gone in a second), the contact list's interface. Then, pressed into the
// player controller the way a pad presses them:
//   1-4  on offense (Trips Flood): a receiver's button tapped is a lob, held a bullet, L1 + tap a high pass, tapped twice
//        a pump fake - each windup on the first sim step after the press;
//   5    on defense against a pass, the stick held right: CIRCLE with the ball in the air takes the man first to where it
//        comes down, and the stick does not steer him for the first 0.2 s, then does;
//   6    against a run: CIRCLE takes the man first to the carrier;
//   7    CIRCLE held and the stick pushed: the man that way;
//   8    a run on offense: a hit-stop (the sim's steps held, none lost) and Responsive movement on the player's man only;
//   9    the contacts found and sounded (never over the cap, never a frame late) and the latency log's presses.
// Without the new timing (-TagThrowOnRelease) the windup checks fail: that is the A/B.
//
// -TagHitStopTest [-TagNoHitStop] -TagWeatherSeed=<n> [-TagHitStopPlays=<n>] [-TagQuarterLen=<s>]: a watched CPU game with
// a hit-stop on every big hit (anybody's: the test treats every man as a player's). After the plays its play log's digest
// is written to Saved/TagHitStop/ and compared with the other run's: the run without -TagNoHitStop must find the same
// digest as the run with it (run that one first).
#include "Game/MatchDirector.h"
#include "Game/TagLatency.h"
#include "Game/TagResult.h"
#include "Render/Athlete.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Sim/Playbook.h"
#include "UnrealClient.h"
#include <cmath>

namespace
{
	constexpr double kFeelTestStep = 1.0 / 60.0;

	struct FFeelTestRun
	{
		bool bStarted = false, bDone = false, bOffline = false;
		int32 Step = 1, Phase = 0, Tries = 0, Pass = 0, Fail = 0;
		float Clock = 0.f, PhaseT = 0.f;
		bool bSnapSent = false;
		unsigned long long SnapSerial = 0;
		float ReleaseCross = 0.f;
		unsigned long long Serial = 0;
		// the left stick the step holds (sent every frame, as a pad does; zeros once after it is let go)
		bool bStick = false, bStickSent = false;
		float StickX = 0.f, StickY = 0.f;
		TArray<TPair<FKey, float>> Releases;
		int32 Count0 = 0;
		double RealT0 = 0.0;
		float SecondTapAt = -1.f;
		bool bEarlyMoved = false, bLateMoved = false;
		int32 Steps0 = 0, Stops0 = 0, Skipped0 = 0, During0 = 0;
		double SimT0 = 0.0;
		bool bLeak = false, bChecked = false;
		float Dir7 = -1.f;
		// the throw clip switched in the windup: the switch's transition jerk on the quarterback (-TagDev measures it)
		float MaxXfj = -1.f;
		int32 XfjSeen = 0;
	};

	/** The receivers' buttons by their labels 1..5 (ReadUserInputSlot's Pads). */
	const FKey& FeelTestPad(int32 i)
	{
		static const FKey Pads[5] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
			EKeys::Gamepad_RightShoulder};
		return Pads[FMath::Clamp(i, 0, 4)];
	}
	FFeelTestRun GFeelRun;

	/** An analog key into the player controller, as a pad sends it each frame. */
	void FeelTestAxis(UWorld* World, const FKey& Key, float Value)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		FViewport* VP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
		if (PC) PC->InputKey(FInputKeyEventArgs(VP, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Key, Value, 1.f / 60.f, 1, FPlatformTime::Cycles64()));
	}

	/** Who could be had by a switch (SmartSwitchPick's rule). */
	bool FeelTestOpen(const tag::PlaySim& P, const tag::Ent* E)
	{
		return E && E->state != tag::EntState::Down && !E->engaged && E->engagedBy.empty() && E->looseDiveT < 0 && !P.inWrap(E);
	}
}

bool AMatchDirector::FeelTestCall(const tag::OffPlay*& Op, const tag::DefCall*& Dc)
{
	if (!Game) return false;
	const int32 St = GFeelRun.Step;
	// 1-4 the throws (the user's pass), 5 and 7 a CPU pass against him, 6 a CPU run, 8 on his run
	const bool bUserOff = St <= 4 || St >= 8;
	UserTeam = bUserOff ? Game->poss : tag::other(Game->poss);
	Op = tag::playById(St == 6 || St >= 8 ? "i_power" : "trips_flood");
	if (!Op) return false;
	Dc = bUserOff ? nullptr : tag::defById("base_c3");
	if (!Dc) Dc = Game->cpuDefense(*Op);
	UE_LOG(LogTemp, Display, TEXT("TAG feeltest call for step %d: the user on %s, %s"), St, bUserOff ? TEXT("offense") : TEXT("defense"), UTF8_TO_TCHAR(Op->name.c_str()));
	return true;
}

void AMatchDirector::TickFeelTest(float Dt)
{
	FFeelTestRun& T = GFeelRun;
	if (T.bDone || !Game) return;
	if (!T.bStarted)
	{
		T.bStarted = true;
		TagFeel::SetTestOverrides(0, 1, 1); // (Authentic, hit effects on, the switch assist on: whatever is saved)
		UE_LOG(LogTemp, Display, TEXT("TAG feeltest: started (%s throw timing, %s switch)"), TagFeel::ThrowOnRelease() ? TEXT("the old") : TEXT("the new"),
			TagFeel::OldSwitch() ? TEXT("the old") : TEXT("the new"));
	}
	const float RealDt = FeelRealDt(Dt);
	T.Clock += RealDt;
	T.PhaseT += RealDt;
	auto Check = [&T](bool bOk, const FString& What)
	{
		(bOk ? T.Pass : T.Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG feeltest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	auto Press = [this, &T](const FKey& K, float Hold)
	{
		PadKey(K, true);
		T.Releases.Add(TPair<FKey, float>(K, Hold));
	};
	auto LetGoAll = [this, &T]()
	{
		for (const TPair<FKey, float>& R : T.Releases) PadKey(R.Key, false);
		T.Releases.Reset();
		PadKey(EKeys::Gamepad_FaceButton_Right, false);
		PadKey(EKeys::Gamepad_LeftShoulder, false);
		T.bStick = false;
	};
	auto Next = [&T, &LetGoAll]()
	{
		LetGoAll();
		++T.Step;
		T.Phase = 0;
		T.Tries = 0;
		T.PhaseT = 0.f;
		T.bChecked = false;
	};
	auto Finish = [&]()
	{
		LetGoAll();
		TagFeel::SetTestOverrides(-1, -1, -1);
		UE_LOG(LogTemp, Display, TEXT("TAG feeltest done: %d pass, %d fail"), T.Pass, T.Fail);
		T.bDone = true;
		TagResult::Finish(TEXT("TagFeelTest"), T.Pass, T.Fail);
	};
	if (T.Clock > 840.f)
	{
		Check(false, FString::Printf(TEXT("the test ran out of time at step %d"), T.Step));
		Finish();
		return;
	}
	// the keys' releases due, the stick held
	for (int32 i = T.Releases.Num() - 1; i >= 0; --i)
		if ((T.Releases[i].Value -= RealDt) <= 0.f)
		{
			PadKey(T.Releases[i].Key, false);
			T.Releases.RemoveAt(i);
		}
	if (T.bStick || T.bStickSent)
	{
		FeelTestAxis(GetWorld(), EKeys::Gamepad_LeftX, T.bStick ? T.StickX : 0.f);
		FeelTestAxis(GetWorld(), EKeys::Gamepad_LeftY, T.bStick ? T.StickY : 0.f);
		T.bStickSent = T.bStick;
	}
	tag::PlaySim* S = Sim.get();
	// the snap: CROSS once the user's offense is set (as -TagPadPlayTest)
	// (once a play, not once until the next snap: a press that draws a pre-snap flag never goes live - the padplay stall)
	if (S && S->serial != T.SnapSerial)
	{
		T.SnapSerial = S->serial;
		T.bSnapSent = false;
	}
	if (State == EMatchState::Presnap && UserOff() && S && !S->walking() && PresnapClock > 1.2f && !T.bSnapSent)
	{
		T.bSnapSent = true;
		PadKey(EKeys::Gamepad_FaceButton_Bottom, true);
		T.ReleaseCross = 0.08f;
	}
	if (T.ReleaseCross > 0.f && (T.ReleaseCross -= RealDt) <= 0.f) PadKey(EKeys::Gamepad_FaceButton_Bottom, false);
	if (State == EMatchState::Live) T.bSnapSent = false;

	// ---- the checks that need no play (a sim for its own steering law)
	if (!T.bOffline && S)
	{
		T.bOffline = true;
		const FTagResponsiveTune Neutral;
		const FTagResponsiveTune& Rt = TagFeel::ResponsiveTune();
		{
			// the law lives in the sim (PlaySim::steerLaw); steerVel, the one the sim steps with, is held to it to the bit over
			// 2000 random men, speeds and wants, Responsive's numbers set on every man: the sim's own law for a CPU man and for
			// a player's man his stick did not drive on the step, the Responsive law capped to the sim's speed for one it did
			FRandomStream R(7);
			double Worst = 0.0, WorstCpu = 0.0, WorstIdle = 0.0, WorstMine = 0.0;
			int32 Differ = 0;
			for (int32 k = 0; k < 2000; ++k)
			{
				tag::Player Pl;
				Pl.r.speed = R.FRandRange(40.f, 99.f);
				Pl.r.accel = R.FRandRange(40.f, 99.f);
				Pl.r.agility = R.FRandRange(40.f, 99.f);
				Pl.weightLb = R.FRandRange(170.f, 330.f);
				tag::Ent E(&Pl, tag::Side::Def, "CB", tag::V2{0, 0});
				E.strafing = R.FRand() < 0.2f;
				E.catchAdjust = R.FRand() < 0.2f;
				const double A0 = R.FRandRange(0.f, 6.2832f), A1 = R.FRandRange(0.f, 6.2832f);
				const double S0 = R.FRandRange(0.f, 1.f) * E.vmax, S1 = R.FRandRange(0.f, 1.f) * E.vmax;
				const tag::V2 V0{std::cos(A0) * S0, std::sin(A0) * S0}, W{std::cos(A1) * S1, std::sin(A1) * S1};
				const tag::V2 Own = TagFeel::Steer(&E, V0, W, kFeelTestStep, Neutral, true);
				Worst = FMath::Max(Worst, tag::dist(Own, S->steerVel(&E, V0, W, kFeelTestStep)));
				E.steer = TagFeel::ToSim(Rt);
				E.stickDrove = true;
				WorstCpu = FMath::Max(WorstCpu, tag::dist(Own, S->steerVel(&E, V0, W, kFeelTestStep))); // (a CPU man: never his)
				E.userControlled = true;
				E.stickDrove = false;
				WorstIdle = FMath::Max(WorstIdle, tag::dist(Own, S->steerVel(&E, V0, W, kFeelTestStep))); // (the AI drove the step)
				E.stickDrove = true;
				tag::V2 Resp = TagFeel::Steer(&E, V0, W, kFeelTestStep, Rt, false);
				const double Top = FMath::Max(tag::len(Own), tag::len(V0)), Lr = tag::len(Resp);
				if (Lr > Top && Lr > 1e-9) Resp = tag::scale(Resp, Top / Lr);
				const tag::V2 Got = S->steerVel(&E, V0, W, kFeelTestStep);
				WorstMine = FMath::Max(WorstMine, tag::dist(Resp, Got));
				Differ += tag::dist(Got, Own) > 1e-9;
			}
			Check(Worst < 1e-12 && WorstCpu < 1e-12 && WorstIdle < 1e-12,
				FString::Printf(TEXT("the sim steps a CPU man, and a player's man on a step his stick did not drive, by its own law (PlaySim::steerLaw), Responsive set on all of them: worst of 2000 random steps %.3g / %.3g / %.3g yd/s"), Worst, WorstCpu, WorstIdle));
			Check(WorstMine < 1e-12 && Differ > 200,
				FString::Printf(TEXT("a player's man his stick drove steps by the Responsive law, capped to the sim's speed: worst %.3g yd/s, %d of 2000 steps steered differently"), WorstMine, Differ));
		}
		{
			// the turning table: the sim's own law (Authentic) against Responsive, ms at full speed (Game/TagFeel.h)
			struct FRow { double Spd, Acc, Agi; const TCHAR* Name; };
			const FRow Rows[3] = {{80, 80, 80, TEXT("80/80/80 back")}, {95, 95, 95, TEXT("95/95/95 elite")}, {60, 60, 60, TEXT("60/60/60 lineman")}};
			double AuCut[3], AuStop[3], ReCut[3], ReStop[3];
			for (int32 k = 0; k < 3; ++k)
			{
				const FRow& Rw = Rows[k];
				AuCut[k] = TagFeel::TurnMs(Rw.Spd, Rw.Acc, Rw.Agi, false, false, Neutral, true, S);
				AuStop[k] = TagFeel::TurnMs(Rw.Spd, Rw.Acc, Rw.Agi, true, false, Neutral, true, S);
				ReCut[k] = TagFeel::TurnMs(Rw.Spd, Rw.Acc, Rw.Agi, false, false, Rt, false);
				ReStop[k] = TagFeel::TurnMs(Rw.Spd, Rw.Acc, Rw.Agi, true, false, Rt, false);
				UE_LOG(LogTemp, Display, TEXT("TAG feeltest steering %s, full speed: heading 45 deg off after the stick turns 90 - Authentic %.0f ms, Responsive %.0f ms; stick reversed to a stop - Authentic %.0f ms, Responsive %.0f ms"),
					Rw.Name, AuCut[k], ReCut[k], AuStop[k], ReStop[k]);
			}
			Check(AuCut[0] >= 683.0 && AuCut[0] <= 717.0 && AuStop[0] >= 867.0 && AuStop[0] <= 950.0,
				FString::Printf(TEXT("Authentic is the sim's law as measured (80/80/80: %.0f ms to 45 deg, report 700; %.0f ms to a stop, report about 900)"), AuCut[0], AuStop[0]));
			Check(ReCut[0] >= 500.0 && ReCut[0] <= 600.0, FString::Printf(TEXT("Responsive turns the 80/80/80 back to 45 deg in %.0f ms (the target 550-600, the report's item 8)"), ReCut[0]));
			bool bAllQuicker = true;
			for (int32 k = 0; k < 3; ++k) bAllQuicker = bAllQuicker && ReCut[k] > 0.0 && ReCut[k] < AuCut[k] && ReStop[k] <= AuStop[k];
			Check(bAllQuicker, TEXT("Responsive cuts and stops sooner than Authentic for the back, the elite back and the lineman"));
			Check(ReCut[1] <= ReCut[2], FString::Printf(TEXT("Responsive: the elite back turns no slower than the lineman (%.0f / %.0f ms; Authentic %.0f / %.0f)"), ReCut[1], ReCut[2], AuCut[1], AuCut[2]));
		}
		{
			// top speed is the sim's: from a stand, straight ahead for 4 s, the same speed every step
			tag::Player Pl;
			Pl.r.speed = 90;
			Pl.r.accel = 85;
			Pl.r.agility = 88;
			Pl.weightLb = 200;
			const tag::Ent E(&Pl, tag::Side::Off, "RB", tag::V2{0, 0});
			tag::V2 Va{0, 0}, Vr{0, 0};
			double Worst = 0.0;
			for (int32 k = 0; k < 240; ++k)
			{
				Va = S->steerVel(&E, Va, tag::V2{E.vmax, 0}, kFeelTestStep);
				Vr = TagFeel::Steer(&E, Vr, tag::V2{E.vmax, 0}, kFeelTestStep, Rt, false);
				Worst = FMath::Max(Worst, FMath::Abs(tag::len(Va) - tag::len(Vr)));
			}
			Check(Worst < 1e-9, FString::Printf(TEXT("Responsive leaves speeding up and top speed alone (%.2f yd/s after 4 s both; worst gap %.3g)"), tag::len(Va), Worst));
		}
		{
			// the shake: smooth (one frame's angle follows the last's: lag-1 autocorrelation; random jitter is ~0), as big as
			// the hit, gone within a second; the touchdown's sway slow
			FTagTrauma Tr;
			Tr.Add(0.8f, false);
			TArray<float> Yaw;
			float Peak = 0.f, After = 0.f;
			for (int32 k = 0; k < 90; ++k)
			{
				FRotator Rot;
				FVector Pos;
				Tr.Tick(1.f / 60.f, 35.f, Rot, Pos);
				if (k < 36) Yaw.Add(Rot.Yaw);
				Peak = FMath::Max(Peak, FMath::Abs(Rot.Yaw));
				if (k >= 60) After = FMath::Max(After, FMath::Abs(Rot.Yaw) + FMath::Abs(Rot.Pitch) + FMath::Abs(Rot.Roll) + static_cast<float>(Pos.Size()));
			}
			double Mean = 0.0, C0 = 0.0, C1 = 0.0;
			for (float Y : Yaw) Mean += Y;
			Mean /= FMath::Max(1, Yaw.Num());
			for (int32 k = 0; k < Yaw.Num(); ++k)
			{
				C0 += (Yaw[k] - Mean) * (Yaw[k] - Mean);
				if (k > 0) C1 += (Yaw[k] - Mean) * (Yaw[k - 1] - Mean);
			}
			const double Ac = C0 > 0.0 ? C1 / C0 : 0.0;
			Check(Ac > 0.5 && Peak > 0.05f && After == 0.f,
				FString::Printf(TEXT("the camera's shake on a big hit: smooth (lag-1 autocorrelation %.2f, white noise ~0), up to %.2f deg on a 35 mm lens, gone after 1 s"), Ac, Peak));
			FTagTrauma Sw;
			Sw.Add(0.5f, true);
			float SwPeak = 0.f, SwMaxStep = 0.f, Last = 0.f;
			for (int32 k = 0; k < 120; ++k)
			{
				FRotator Rot;
				FVector Pos;
				Sw.Tick(1.f / 60.f, 35.f, Rot, Pos);
				SwPeak = FMath::Max(SwPeak, FMath::Abs(Rot.Yaw));
				if (k > 0) SwMaxStep = FMath::Max(SwMaxStep, FMath::Abs(Rot.Yaw - Last));
				Last = Rot.Yaw;
			}
			Check(SwPeak > 0.f && SwMaxStep < 0.25f * FMath::Max(SwPeak, 1e-3f) + 1e-4f,
				FString::Printf(TEXT("a touchdown's shake is a slow sway (at most %.0f%% of its %.3f deg peak in a frame)"), 100.f * SwMaxStep / FMath::Max(SwPeak, 1e-3f), SwPeak));
		}
		{
			// the contact list's interface (Game/TagContact.h): this frame's, the last 64 by cursor, a man's own push
			FTagContacts L;
			L.BeginFrame(GFrameCounter);
			for (int32 k = 0; k < 70; ++k)
			{
				FTagContact C;
				C.IdA = 1000 + k;
				C.IdB = 2000 + k;
				C.Normal = FVector(1, 0, 0);
				C.ClosingSpeed = 180.f + k * 10.f;
				C.DvA = 50.f;
				C.DvB = 80.f;
				L.Add(C);
			}
			uint64 Cursor = 0;
			int32 Got = 0;
			uint64 First = 0;
			L.ForEachSince(Cursor, [&](const FTagContact& C) { if (Got++ == 0) First = C.Serial; });
			int32 Again = 0;
			L.ForEachSince(Cursor, [&](const FTagContact&) { ++Again; });
			const FTagContact& C0 = L.ThisFrame()[0];
			const bool bMan = C0.Involves(1000) && C0.Other(1000) == 2000 && C0.DirFor(1000).Equals(FVector(-1, 0, 0)) && C0.DirFor(2000).Equals(FVector(1, 0, 0)) &&
				C0.DvFor(2000) == 80.f && C0.DvFor(5) == 0.f && C0.Strength() == 0.f && L.ThisFrame()[69].Strength() > 0.9f;
			Check(L.ThisFrame().Num() == 70 && Got == 64 && First == 7 && Cursor == 70 && Again == 0 && L.LastSerial() == 70 && bMan,
				FString::Printf(TEXT("the contact list: %d this frame, %d held for a reader (from serial %llu, cursor at %llu), %d again after it; a man's push, the other man, strength"),
					L.ThisFrame().Num(), Got, First, Cursor, Again));
		}
	}

	// ---- the played checks
	const bool bLive = State == EMatchState::Live && S;
	if (T.Phase > 0 && (!S || S->serial != T.Serial || (State != EMatchState::Live && !((T.Step == 8 || T.Step == 1) && T.Phase >= 2))))
	{
		// the play ended before the check could finish: again on the next one (5 tries)
		UE_LOG(LogTemp, Display, TEXT("TAG feeltest: step %d's play ended at phase %d, again on the next"), T.Step, T.Phase);
		LetGoAll();
		T.Phase = 0;
		if (++T.Tries >= 5)
		{
			Check(false, FString::Printf(TEXT("step %d: no play got as far as its check in 5 tries"), T.Step));
			Next();
		}
		return;
	}
	switch (T.Step)
	{
	case 1: case 2: case 3: case 4:
	{
		if (T.Phase == 2)
		{
			// the switched windup's transition jerk on him (1: a switch the joints do not show), as measured after it
			const TObjectPtr<AAthlete>* QA = S->qb && S->qb->p ? Athletes.Find(S->qb->p->id) : nullptr;
			// (the windup's own start, cut short by the switch, is reported as it happens; the switch's own 0.25 s after it: the
			// later report is the switch's)
			if (QA && *QA && (*QA)->ClipXfjN > ThrowRetimeXfjN + T.XfjSeen)
			{
				T.XfjSeen = (*QA)->ClipXfjN - ThrowRetimeXfjN;
				T.MaxXfj = (*QA)->LastClipXfj;
				UE_LOG(LogTemp, Display, TEXT("TAG feeltest: the quarterback's clip switch measured %.2f (%d since the throw was switched)"), T.MaxXfj, T.XfjSeen);
			}
			if (FPlatformTime::Seconds() - ThrowRetimeAt > 0.5)
			{
				// (xfj: the joints' jerk in the 0.25 s after a switch over the 0.25 s before; the game's clip switches run p50
				// 1.2-1.5, a pop reads 10 and over)
				if (T.XfjSeen > 0)
					Check(T.MaxXfj <= 3.f, FString::Printf(TEXT("the throw switched in the windup shows no pop: transition jerk %.2f (1 = unseen, a pop 10+; the bound 3)"), T.MaxXfj));
				else UE_LOG(LogTemp, Display, TEXT("TAG feeltest: the switched windup's jerk is not measured in this run (-TagDev measures it)"));
				Next();
			}
			break;
		}
		// the throws: the user's quarterback with the ball, the receivers' buttons up, a second into the play
		if (!bLive) break;
		const bool bQb = S->user && S->user == S->qb && S->ball.holder == S->qb && !S->passThrown && !Labels.empty() && S->t > 1.0;
		if (T.Phase == 0)
		{
			if (!bQb || !UserOff()) break;
			// (the tap's lob: from a stand, at a receiver the windup starts as the quick or the deep throw for - 6.5-10 yd, or
			// past 28 - so the lob it settles as has to switch it, AAthlete::RetimeThrow; receiver 1 if nobody is)
			FKey TapKey = EKeys::Gamepad_FaceButton_Bottom;
			bool bInRange = false;
			if (T.Step == 1)
				for (const auto& KV : Labels)
				{
					const double Dq = tag::dist(KV.first->pos, S->qb->pos);
					if ((Dq > 6.5 && Dq < 10.0) || Dq > 28.0)
					{
						TapKey = FeelTestPad(FCString::Atoi(*KV.second) - 1);
						bInRange = true;
					}
				}
			if (T.Step == 1 && (tag::len(S->qb->vel) > 2.0 || !bInRange) && S->t < 3.5) break;
			T.Serial = S->serial;
			T.Count0 = LastThrow.Count;
			T.Phase = 1;
			T.PhaseT = 0.f;
			T.SecondTapAt = -1.f;
			T.MaxXfj = -1.f;
			T.XfjSeen = 0;
			if (T.Step == 1) Press(TapKey, 0.05f);                                          // a tap
			else if (T.Step == 2) Press(EKeys::Gamepad_FaceButton_Bottom, 0.45f);        // held
			else if (T.Step == 3)
			{
				Press(EKeys::Gamepad_LeftShoulder, 0.6f);                               // L1 held: high
				Press(EKeys::Gamepad_FaceButton_Bottom, 0.05f);
			}
			else
			{
				Press(EKeys::Gamepad_FaceButton_Bottom, 0.03f);                         // tapped twice: a pump fake
				T.SecondTapAt = 0.06f; // (inside the quickest windup, 0.13 s, at 30 fps too)
			}
			break;
		}
		if (T.Step == 4)
		{
			if (T.SecondTapAt > 0.f && T.PhaseT >= T.SecondTapAt)
			{
				T.SecondTapAt = -1.f;
				Press(EKeys::Gamepad_FaceButton_Bottom, 0.04f);
			}
			if (T.PhaseT >= 0.55f && !T.bChecked)
			{
				T.bChecked = true;
				bool bPumpEv = false;
				for (const tag::PlayEvent& V : S->events) bPumpEv = bPumpEv || V.type == "pump";
				Check(LastThrow.WindupSteps == 1, FString::Printf(TEXT("the pump fake's first tap starts the windup on the first sim step after the press (%d steps, %.0f ms)"),
					LastThrow.WindupSteps, LastThrow.WindupMs));
				Check(LastThrow.bPump && bPumpEv && !S->passThrown && S->ball.holder == S->qb,
					FString::Printf(TEXT("the receiver's button tapped twice is a pump fake: the sim's pump, the ball still in his hands (%s)"), bPumpEv ? TEXT("pumped") : TEXT("no pump")));
				Next(); // (he holds on to it: the rush ends the play)
			}
			break;
		}
		if (T.Phase == 1 && LastThrow.Count > T.Count0 && LastThrow.Serial == S->serial)
		{
			static const TCHAR* const Want[] = {TEXT(""), TEXT("tapped: a lob / touch"), TEXT("held: a bullet"), TEXT("L1 + tap: a high pass")};
			const int32 WantStyle = T.Step == 1 ? 1 : T.Step == 2 ? 0 : 2;
			Check(LastThrow.WindupSteps == 1, FString::Printf(TEXT("%s - the windup on the first sim step after the press (%d steps, %.0f ms; the old timing: 9-18 steps)"),
				Want[T.Step], LastThrow.WindupSteps, LastThrow.WindupMs));
			Check(LastThrow.Style == WantStyle && LastThrow.bBallLob == (WantStyle == 1),
				FString::Printf(TEXT("%s - thrown as style %d (%s in the air), the ball away %.0f ms after the press"), Want[T.Step], LastThrow.Style,
					LastThrow.bBallLob ? TEXT("lobbed") : TEXT("driven"), LastThrow.ReleaseMs));
			// the windup's throw is the one for the pass as thrown (a lob settled in the windup is not finished as the quick or
			// the deep throw it started as)
			const bool bWantTouch = WantStyle == 1 || WantStyle == 2;
			Check(!LastThrow.bClipStand || LastThrow.bClipTouch == bWantTouch,
				FString::Printf(TEXT("%s - the windup's throw fits it: clip key %d%s%s"), Want[T.Step], LastThrow.ClipKey,
					LastThrow.bClipStand ? (LastThrow.bClipTouch ? TEXT(", the touch throw") : TEXT(", a driven throw")) : TEXT(" (on the move / under the rush: the pass type does not pick it)"),
					LastThrow.bClipSwitched ? TEXT(", switched to in the windup") : TEXT("")));
			if (LastThrow.bClipSwitched)
			{
				T.Phase = 2; // (the switch's jerk, measured over the 0.25 s after it)
				T.PhaseT = 0.f;
			}
			else
			{
				if (T.Step == 1) UE_LOG(LogTemp, Display, TEXT("TAG feeltest: the tap's windup needed no switch this time (clip key %d)"), LastThrow.ClipKey);
				Next();
			}
		}
		else if (T.PhaseT > 2.f)
		{
			Check(false, FString::Printf(TEXT("step %d: no throw came within 2 s of the press"), T.Step));
			Next();
		}
		break;
	}
	case 5:
	{
		// on defense against a pass, the stick held right from the snap; CIRCLE once the ball is in the air
		if (!bLive || !UserDef()) break;
		TagFeel::SetTestOverrides(1, 1, 1); // (Responsive from here to step 8: checked there)
		T.bStick = true;
		T.StickX = 1.f;
		T.StickY = 0.f;
		if (T.Phase == 0)
		{
			// (a pass with a beat of its flight left: the press is read a frame on, still in the air)
			if (S->ball.mode != tag::BallMode::Air || !S->user || S->t < 0.3 || S->ball.flightT - S->ball.t < 0.3) break;
			T.Serial = S->serial;
			T.Count0 = LastSwitch.Count;
			Press(EKeys::Gamepad_FaceButton_Right, 0.05f);
			T.Phase = 1;
			T.PhaseT = 0.f;
			break;
		}
		if (T.Phase == 1)
		{
			if (LastSwitch.Count > T.Count0)
			{
				const FTagSwitchNote& N = LastSwitch;
				Check(N.Mode == 1 && FeelTestOpen(*S, N.To) && N.To != N.From,
					FString::Printf(TEXT("CIRCLE with the pass in the air takes the man first to where it comes down (mode %d, %.2f s; the nearest-the-ball rule's man %.2f s%s)"), N.Mode, N.Score,
						N.OldScore, N.OldPick && !FeelTestOpen(*S, N.OldPick) ? TEXT(", and he was locked in a block") : TEXT("")));
				Check(!N.OldPick || !FeelTestOpen(*S, N.OldPick) || N.Score <= N.OldScore + 1e-9,
					FString::Printf(TEXT("... no slower to the ball than the man the old rule took (%.2f s against %.2f s)"), N.Score, N.OldScore));
				T.RealT0 = FPlatformTime::Seconds();
				T.bEarlyMoved = T.bLateMoved = false;
				T.Phase = 2;
			}
			else if (T.PhaseT > 1.f) { Check(false, TEXT("CIRCLE with the ball in the air switched nobody")); Next(); }
			break;
		}
		{
			// the stick (still held right) as the sim was given it last frame: nothing for 0.2 s, his again after 0.25
			const double Since = FPlatformTime::Seconds() - T.RealT0;
			const double Mv = tag::len(S->input.move);
			if (Since > 0.04 && Since < 0.2 && Mv > 1e-6) T.bEarlyMoved = true;
			if (Since > 0.35 && Mv > 0.3) T.bLateMoved = true;
			if (Since > 0.9 || T.bLateMoved)
			{
				Check(!T.bEarlyMoved, TEXT("the switch assist: the stick held as it was does not steer the new man for the first 0.2 s"));
				Check(T.bLateMoved, TEXT("... and steers him again once the 0.25 s are up"));
				Next();
			}
		}
		break;
	}
	case 6:
	{
		// against a run: CIRCLE once the back has it and is on his way
		if (!bLive || !UserDef()) break;
		const tag::Ent* Cr = S->ball.mode == tag::BallMode::Held ? S->ball.holder : nullptr;
		if (T.Phase == 0)
		{
			if (!Cr || Cr->side != tag::Side::Off || Cr == S->qb || Cr->state != tag::EntState::Carrier || S->meshDoneT < 0 || S->t - S->meshDoneT < 0.3) break;
			T.Serial = S->serial;
			T.Count0 = LastSwitch.Count;
			Press(EKeys::Gamepad_FaceButton_Right, 0.05f);
			T.Phase = 1;
			T.PhaseT = 0.f;
			break;
		}
		if (LastSwitch.Count > T.Count0)
		{
			const FTagSwitchNote& N = LastSwitch;
			Check(N.Mode == 2 && FeelTestOpen(*S, N.To) && N.To != N.From,
				FString::Printf(TEXT("CIRCLE on a run takes the man first to the carrier (mode %d, %.2f s; the nearest-the-ball rule's man %.2f s%s)"), N.Mode, N.Score, N.OldScore,
					N.OldPick && !FeelTestOpen(*S, N.OldPick) ? TEXT(", and he was locked in a block") : TEXT("")));
			Check(!N.OldPick || !FeelTestOpen(*S, N.OldPick) || N.Score <= N.OldScore + 1e-9,
				FString::Printf(TEXT("... no slower to the carrier than the man the old rule took (%.2f s against %.2f s)"), N.Score, N.OldScore));
			Next();
		}
		else if (T.PhaseT > 1.f) { Check(false, TEXT("CIRCLE on a run switched nobody")); Next(); }
		break;
	}
	case 7:
	{
		// CIRCLE held, the stick in the middle, then pushed: the man that way
		if (!bLive || !UserDef()) break;
		if (T.Phase == 0)
		{
			if (S->t < 0.6 || !S->user) break;
			T.Serial = S->serial;
			T.bStick = true;
			T.StickX = T.StickY = 0.f;
			PadKey(EKeys::Gamepad_FaceButton_Right, true);
			T.Phase = 1;
			T.PhaseT = 0.f;
			break;
		}
		if (T.Phase == 1 && T.PhaseT > 0.15f)
		{
			T.Count0 = LastSwitch.Count; // (the press's own switch is in)
			T.StickX = T.Dir7;
			T.Phase = 2;
			T.PhaseT = 0.f;
			break;
		}
		if (T.Phase == 2)
		{
			if (LastSwitch.Count > T.Count0 && LastSwitch.Mode == 3)
			{
				const FTagSwitchNote& N = LastSwitch;
				const tag::V2 Rel = N.To && N.From ? tag::sub(N.To->pos, N.From->pos) : tag::V2{0, 0};
				const double Ang = tag::len(Rel) > 1e-6 && tag::len(N.Stick) > 1e-6 ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(tag::dot(Rel, N.Stick) / (tag::len(Rel) * tag::len(N.Stick)), -1.0, 1.0))) : 180.0;
				Check(Ang <= 52.0, FString::Printf(TEXT("CIRCLE held with the stick pushed takes the man that way (%.0f deg off the stick)"), Ang));
				Next();
			}
			else if (T.PhaseT > 0.8f)
			{
				if (T.Dir7 < 0.f)
				{
					// (nobody that way: the other way once)
					T.Dir7 = 1.f;
					T.StickX = 0.f;
					T.Phase = 1;
					T.PhaseT = 0.f;
				}
				else { Check(false, TEXT("CIRCLE held with the stick pushed (either way) switched nobody")); Next(); }
			}
		}
		break;
	}
	case 8:
	{
		// on his run: the hit-stop (the sim's steps held, none lost), Responsive on his man only
		if (T.Phase == 0)
		{
			if (!bLive || !UserOff() || S->t < 0.8 || !S->user || !S->user->hasBall || S->user == S->qb) break;
			T.Serial = S->serial;
			T.bStick = true;
			T.StickX = 1.f;
			T.StickY = 0.4f;
			T.Steps0 = FeelSteps;
			T.SimT0 = S->t;
			T.Stops0 = HitStop.Count;
			T.Skipped0 = HitStop.Skipped;
			T.During0 = HitStop.StepsDuring;
			T.bLeak = false;
			T.RealT0 = FPlatformTime::Seconds();
			StartHitStop(4, 0.6f, TEXT("the feel test's hit-stop"));
			Check(CamTrauma.Active(), TEXT("a big hit shakes the camera (trauma added)"));
			T.Phase = 1;
			T.PhaseT = 0.f;
			break;
		}
		if (T.Phase == 1)
		{
			if (!TagFeel::HitStopOn())
			{
				Check(HitStop.Skipped == T.Skipped0 + 1 && HitStop.Count == T.Stops0, TEXT("-TagNoHitStop: the hit-stop is asked for and skipped"));
				T.Phase = 2;
				T.PhaseT = 0.f;
				break;
			}
			if (HitStop.bOn)
			{
				if (FeelSteps != T.Steps0) T.bLeak = true;
				break;
			}
			const double HeldMs = (FPlatformTime::Seconds() - T.RealT0) * 1000.0;
			Check(HitStop.Count == T.Stops0 + 1 && !T.bLeak && HitStop.StepsDuring == T.During0 && HeldMs >= 50.0 && HeldMs <= 300.0,
				FString::Printf(TEXT("the hit-stop holds the world %.0f ms (4 steps: 67) and no sim step is taken meanwhile (%d)"), HeldMs, HitStop.StepsDuring - T.During0));
			T.Phase = 2;
			T.PhaseT = 0.f;
			break;
		}
		if (T.Phase == 2 && T.PhaseT > 0.3f)
		{
			const int32 Taken = FeelSteps - T.Steps0;
			const double Gap = FMath::Abs(S->t - T.SimT0 - Taken * kFeelTestStep);
			Check(Taken > 0 && Gap < 1e-6, FString::Printf(TEXT("after it the sim steps on, every step taken (%d steps, sim clock %.4f s on, %.2g off the step count)"), Taken, S->t - T.SimT0, Gap));
			T.Phase = 3;
			break;
		}
		if (T.Phase == 3 && State != EMatchState::Live)
		{
			Check(ResponsiveSteps > 0, FString::Printf(TEXT("Responsive steered the player's man on %d sim steps"), ResponsiveSteps));
			Check(ResponsiveCpu == 0, FString::Printf(TEXT("Responsive never touched a man the CPU had (%d)"), ResponsiveCpu));
			TagFeel::SetTestOverrides(0, 1, 1);
			Next();
		}
		break;
	}
	default:
	{
		// the contacts and their sound, the latency log
		Check(CsAllContacts > 0 && static_cast<uint64>(CsAllContacts) == FTagContacts::Get().LastSerial(),
			FString::Printf(TEXT("the bodies meeting were found and every one reached the feel layer (%d, the list's last serial %llu)"), CsAllContacts, FTagContacts::Get().LastSerial()));
		Check(CsAllSounds > 0 && CsMostInWindow <= 4 && CsAllLate == 0,
			FString::Printf(TEXT("contacts sounded: %d, never more than %d in half a second (cap 4), %d a frame late"), CsAllSounds, CsMostInWindow, CsAllLate));
		FTagLatency& Lat = FTagLatency::Get();
		Lat.LogSummary(TEXT("the feel test"));
		int32 Slow = 0;
		for (const FTagLatency::FPress& Pr : Lat.Done) Slow += Pr.Stepped - Pr.Read > 0.05 || Pr.StepFrame - Pr.ReadFrame > 2 ? 1 : 0;
		Check(Lat.Done.Num() >= 4 && Slow == 0 && Lat.NumSummaries >= 1 && Lat.Fps > 0.f,
			FString::Printf(TEXT("the latency log: %d presses followed to their sim step (%d over 50 ms or 2 frames), %.0f fps, summaries logged"), Lat.Done.Num(), Slow, Lat.Fps));
		Finish();
		break;
	}
	}
}

void AMatchDirector::TickHitStopTest()
{
	static bool bOnce = false, bDoneHs = false;
	if (!bOnce)
	{
		bOnce = true;
		TagFeel::SetTestOverrides(-1, 1, -1); // (the hit effects on, whatever is saved)
	}
	if (bDoneHs || !Game) return;
	int32 Plays = 16;
	FParse::Value(FCommandLine::Get(), TEXT("TagHitStopPlays="), Plays);
	if (static_cast<int32>(Game->log.size()) < Plays && !Game->isFinal()) return;
	bDoneHs = true;
	int32 Pass = 0, Fail = 0;
	auto Check = [&Pass, &Fail](bool bOk, const FString& What)
	{
		(bOk ? Pass : Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG hitstoptest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	const bool bStops = TagFeel::HitStopOn();
	int32 Seed = -1;
	FParse::Value(FCommandLine::Get(), TEXT("TagWeatherSeed="), Seed);
	Check(Seed >= 0, FString::Printf(TEXT("a fixed seed (-TagWeatherSeed=%d): the same game both runs"), Seed));
	// the play log's digest (FNV-1a over each play's line: the quarter, the clock, who had it, the down and distance, the
	// spot, what happened, the score)
	const int32 N = FMath::Min(Plays, static_cast<int32>(Game->log.size()));
	uint64 Hash = 1469598103934665603ull;
	FString Lines;
	for (int32 i = 0; i < N; ++i)
	{
		const tag::PlayLog& Lg = Game->log[i];
		const FString Line = FString::Printf(TEXT("%d|%.2f|%d|%d|%.2f|%.2f|%s|%d-%d"), Lg.q, Lg.clock, static_cast<int32>(Lg.poss), Lg.down, Lg.toGo, Lg.ballOn,
			UTF8_TO_TCHAR(Lg.desc.c_str()), Lg.score.away, Lg.score.home);
		for (const TCHAR Ch : Line)
		{
			Hash ^= static_cast<uint64>(Ch);
			Hash *= 1099511628211ull;
		}
		Hash ^= 10ull;
		Hash *= 1099511628211ull;
		Lines += Line + TEXT("\n");
	}
	const FString Digest = FString::Printf(TEXT("%016llx"), static_cast<unsigned long long>(Hash));
	UE_LOG(LogTemp, Display, TEXT("TAG hitstoptest: %d plays, digest %s, hit-stops %d made / %d skipped, %d sim steps taken while held, %d sim steps in all"), N, *Digest, HitStop.Count,
		HitStop.Skipped, HitStop.StepsDuring, FeelSteps);
	if (bStops)
	{
		Check(HitStop.Count >= 1, FString::Printf(TEXT("the big hits were held: %d hit-stops"), HitStop.Count));
		Check(HitStop.StepsDuring == 0, FString::Printf(TEXT("no sim step was taken while the world was held (%d)"), HitStop.StepsDuring));
	}
	else Check(HitStop.Count == 0, FString::Printf(TEXT("-TagNoHitStop: no hit-stop (%d asked for and skipped)"), HitStop.Skipped));
	// this run's digest saved; the other run's compared
	const FString OutDir = FPaths::ProjectSavedDir() / TEXT("TagHitStop");
	IFileManager::Get().MakeDirectory(*OutDir, true);
	auto FileFor = [&OutDir, Seed, N](bool bOn) { return OutDir / FString::Printf(TEXT("digest_seed%d_plays%d_%s.txt"), Seed, N, bOn ? TEXT("stops") : TEXT("nostops")); };
	// (each file carries the build it came from - the game module's time - and only a same-build run is compared: another
	// build's sim may play other plays, and the last gate's file of the other half was compared against a new build's)
	const FString Build = IFileManager::Get().GetTimeStamp(*FModuleManager::Get().GetModuleFilename(TEXT("TAGGridiron"))).ToString();
	FFileHelper::SaveStringToFile(Digest + TEXT(" build ") + Build + TEXT("\n") + Lines, *FileFor(bStops));
	const FString Other = FileFor(!bStops);
	FString OtherText, OtherDigest, OtherBuild, Rest;
	if (FFileHelper::LoadFileToString(OtherText, *Other))
	{
		FString Head = OtherText;
		OtherText.Split(TEXT("\n"), &Head, &Rest);
		if (!Head.TrimStartAndEnd().Split(TEXT(" build "), &OtherDigest, &OtherBuild)) OtherDigest = Head.TrimStartAndEnd();
	}
	if (!OtherDigest.IsEmpty() && OtherBuild == Build)
	{
		Check(OtherDigest == Digest, FString::Printf(TEXT("the same plays with and without the hit-stop: digest %s, the %s run's %s (build %s)"), *Digest,
			bStops ? TEXT("-TagNoHitStop") : TEXT("hit-stop"), *OtherDigest, *Build));
		if (OtherDigest != Digest) UE_LOG(LogTemp, Display, TEXT("TAG hitstoptest: compare %s with %s line by line"), *FileFor(bStops), *Other);
	}
	else if (bStops) Check(false, FString::Printf(TEXT("no -TagNoHitStop run of this build to compare with (run it first, the same -TagWeatherSeed and -TagHitStopPlays): %s%s"), *Other,
		OtherDigest.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - it holds build %s, this is %s"), *OtherBuild, *Build)));
	else UE_LOG(LogTemp, Display, TEXT("TAG hitstoptest: the run with the hit-stop (without -TagNoHitStop) compares with this one (build %s)%s"), *Build,
		OtherDigest.IsEmpty() ? TEXT("") : TEXT("; the hit-stop file there is another build's: not compared"));
	TagFeel::SetTestOverrides(-1, -1, -1);
	TagResult::Finish(TEXT("TagHitStopTest"), Pass, Fail);
}
