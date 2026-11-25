// -TagInputTest: one button press that reaches the game twice (an Xbox pad is read through XInput and GameInput both,
// a frame or so apart) must move a menu once. The check sends each press from two made-up devices a frame apart, the
// way the pad does, and walks the main menu, the team select and a play call; also the stick (its key and its analog
// value, which Slate used to take as a second push). First the sticks' radial dead zone: heading and length read back
// at a dozen angles and lengths, through a pad of the hub's and through the controller ("stick ..." lines). PASS / FAIL
// lines, "TAG inputtest done", then the game exits.
#include "UI/GridironOverlay.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Game/MatchDirector.h"
#include "Game/TagInput.h"
#include "Game/TagResult.h"
#include "UI/LoadingScreen.h"

namespace InputTest
{
	void Send(int32 DeviceId, const FKey& K, bool bDown)
	{
		if (!FSlateApplication::IsInitialized()) return;
		const FKeyEvent E(K, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(DeviceId), false, 0, 0, TOptional<int32>(0));
		if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(E);
		else FSlateApplication::Get().ProcessKeyUpEvent(E);
	}
	void Axis(int32 DeviceId, const FKey& K, float V)
	{
		if (!FSlateApplication::IsInitialized()) return;
		const FAnalogInputEvent E(K, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(DeviceId), false, 0, 0, V, TOptional<int32>(0));
		FSlateApplication::Get().ProcessAnalogInputEvent(E);
	}
}

void SGridironOverlay::StartInputTest()
{
	using namespace InputTest;
	// the pad's two copies: made-up ids under the hub's automation range (9000+), so it treats them as real pads
	constexpr int32 A = 8201, B = 8202;
	constexpr int32 C = 8203; // (a pad of its own: the stick check and the kick meter's presses)
	struct FState
	{
		TArray<TFunction<bool()>> Steps;
		int32 I = 0, Wait = 0, Pass = 0, Fail = 0;
		int32 Mark = 0;
		FString MarkS;
		double StepT = 0;
	};
	const TSharedRef<FState> S = MakeShared<FState>();
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
	auto Check = [S](bool bOk, const FString& What)
	{
		(bOk ? S->Pass : S->Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG inputtest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	auto Do = [S](TFunction<void()> F) { S->Steps.Add([F] { F(); return true; }); };
	auto Frames = [S](int32 N) { S->Steps.Add([S, N] { if (++S->Wait < N) return false; S->Wait = 0; return true; }); };
	auto Until = [S](TFunction<bool()> F) { S->Steps.Add(F); };
	// one press of K, arriving twice: the second copy a frame after the first (bFlip: the other device first)
	auto Twice = [&](const FKey& K, bool bFlip)
	{
		const int32 P = bFlip ? B : A, Q = bFlip ? A : B;
		Do([P, K] { Send(P, K, true); });
		Frames(1);
		Do([Q, K] { Send(Q, K, true); });
		Frames(4);
		Do([P, K] { Send(P, K, false); });
		Frames(1);
		Do([Q, K] { Send(Q, K, false); });
		Frames(4);
	};
	auto Shot = [Do](const TCHAR* Name) { Do([Name] { FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_input_%s.png"), Name), true, false); }); };

	// ---- the main menu: a button's focus moves one a press
	Frames(40);
	Do([W, S] { if (auto O = W.Pin()) { O->ShowMainMenu(); } });
	Frames(10);
	Do([W, S] { if (auto O = W.Pin()) S->Mark = O->FocusedIndex(); });
	Twice(EKeys::Gamepad_DPad_Down, false);
	Do([W, S, Check] { if (auto O = W.Pin()) Check(O->FocusedIndex() == S->Mark + 1, FString::Printf(TEXT("main menu: d-pad down sent twice moves the focus one (%d -> %d)"), S->Mark, O->FocusedIndex())); });
	Shot(TEXT("menu"));

	// ---- the team select: columns and rows
	Do([W] { if (auto O = W.Pin()) O->ShowTeamSelect(true); });
	Frames(10);
	Do([W, S] { if (auto O = W.Pin()) S->Mark = O->SelCol; });
	Twice(EKeys::Gamepad_DPad_Left, true);
	Do([W, S, Check] { if (auto O = W.Pin()) Check(O->SelCol == S->Mark - 1, FString::Printf(TEXT("team select: d-pad left sent twice moves one column (%d -> %d)"), S->Mark, O->SelCol)); });
	// the stick on the options column: its key and its analog value together, as a pad sends them (one push, one row)
	Do([W, S] { if (auto O = W.Pin()) S->Mark = O->SelRow; });
	Do([] { Axis(C, EKeys::Gamepad_LeftY, -1.f); Send(C, EKeys::Gamepad_LeftStick_Down, true); });
	Frames(1);
	Do([] { Axis(C, EKeys::Gamepad_LeftY, -1.f); });
	Frames(1);
	Do([] { Axis(C, EKeys::Gamepad_LeftY, -1.f); });
	Frames(2);
	Do([] { Axis(C, EKeys::Gamepad_LeftY, 0.f); Send(C, EKeys::Gamepad_LeftStick_Down, false); });
	Frames(4);
	Do([W, S, Check] { if (auto O = W.Pin()) Check(O->SelRow == S->Mark + 1, FString::Printf(TEXT("team select: one push of the stick moves one row (%d -> %d)"), S->Mark, O->SelRow)); });
	Twice(EKeys::Gamepad_DPad_Down, false);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->SelRow == 2, FString::Printf(TEXT("team select: d-pad down sent twice moves one row (row %d)"), O->SelRow)); });
	Twice(EKeys::Gamepad_DPad_Down, true);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->SelRow == 3, FString::Printf(TEXT("team select: again, the other copy first (row %d)"), O->SelRow)); });
	Do([Check]
	{
		const FTagInputHub& H = FTagInputHub::Get();
		const int32 DA = H.Find(FInputDeviceId::CreateFromInternalId(A), true), DB = H.Find(FInputDeviceId::CreateFromInternalId(B), true);
		const bool bTwin = (H.Dev(DA) && H.Dev(DA)->TwinOf == DB) || (H.Dev(DB) && H.Dev(DB)->TwinOf == DA);
		Check(bTwin, FString::Printf(TEXT("the hub knows the two devices are one pad (%d copies swallowed)"), H.SwallowedCount()));
	});
	Shot(TEXT("teamselect"));

	// ---- the sticks' radial dead zone (TagInput::Stick; after the menus' checks, which time their two copies of a press
	// to the frame): the way the stick points is the way the man goes at every angle,
	// its length the speed (a full diagonal as fast as a full push up), nothing inside the dead zone. Read through a
	// player's own pad (the hub) and through the controller (the one-player game), each a raw value set and read back.
	Do([Check]
	{
		FTagInputHub& H = FTagInputHub::Get();
		const int32 D = H.AddFake(true, TEXT("StickTest"));
		const FTagReader R(D, true);
		const float Dz = TagInput::StickDeadZone(), Outer = TagInput::StickOuter();
		struct FCase { float Deg, Len; };
		// full pushes on a round gate, a square gate's diagonal (longer than 1), part pushes, one inside the dead zone
		const FCase Cases[] = {{0.f, 1.f}, {10.f, 1.f}, {20.f, 1.f}, {30.f, 1.f}, {45.f, 1.f}, {60.f, 1.f}, {80.f, 1.f}, {135.f, 1.f}, {200.f, 1.f}, {290.f, 1.f},
			{45.f, 1.06f}, {30.f, 0.55f}, {100.f, 0.4f}, {70.f, Dz * 0.8f}};
		for (const FCase& Cs : Cases)
		{
			const float Rad = FMath::DegreesToRadians(Cs.Deg);
			const FVector2D Raw(FMath::Cos(Rad) * Cs.Len, FMath::Sin(Rad) * Cs.Len);
			H.SetAxis(D, EKeys::Gamepad_LeftX, static_cast<float>(Raw.X));
			H.SetAxis(D, EKeys::Gamepad_LeftY, static_cast<float>(Raw.Y));
			const FVector2D V(R.GetInputAnalogKeyState(EKeys::Gamepad_LeftX), R.GetInputAnalogKeyState(EKeys::Gamepad_LeftY));
			const float WantLen = FMath::Clamp((Cs.Len - Dz) / (Outer - Dz), 0.f, 1.f);
			const float Len = static_cast<float>(V.Size());
			float Head = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(V.Y), static_cast<float>(V.X)));
			if (Head < 0.f) Head += 360.f;
			const float HeadErr = FMath::Abs(FMath::FindDeltaAngleDegrees(Head, Cs.Deg));
			// (what the old per-axis 0.25 dead zone made of the same push, for the record)
			auto Old = [](double Ax) { const double M = FMath::Abs(Ax); return M < 0.25 ? 0.0 : FMath::Sign(Ax) * FMath::Min(1.0, (M - 0.25) / 0.75); };
			const double OX = Old(Raw.X), OY = Old(Raw.Y);
			double OldHead = FMath::RadiansToDegrees(FMath::Atan2(OY, OX));
			if (OldHead < 0.0) OldHead += 360.0;
			const bool bOk = WantLen <= 0.f ? Len == 0.f : (FMath::Abs(Len - WantLen) < 0.01f && HeadErr < 1.f);
			Check(bOk, FString::Printf(TEXT("stick (pad) %3.0f deg at %.2f: heading %.1f deg, length %.2f (want %.2f); the per-axis dead zone read %.1f deg at %.2f"),
				Cs.Deg, Cs.Len, Len > 0.f ? Head : 0.f, Len, WantLen, OX == 0.0 && OY == 0.0 ? 0.0 : OldHead, FMath::Sqrt(OX * OX + OY * OY)));
		}
		H.SetAxis(D, EKeys::Gamepad_LeftX, 0.f);
		H.SetAxis(D, EKeys::Gamepad_LeftY, 0.f);
		// a full diagonal is as fast as a full push up (the sim's speed goes with the stick's length)
		const FVector2D Up = TagInput::Stick(FVector2D(0.0, 1.0)), Diag = TagInput::Stick(FVector2D(0.7071, 0.7071));
		Check(FMath::Abs(Up.Size() - Diag.Size()) < 0.01, FString::Printf(TEXT("stick: a full diagonal's length %.2f is a full push up's %.2f"), Diag.Size(), Up.Size()));
	});
	// the same through the controller: a raw value put into its input each frame (as a pad sends it), read back before
	// the controller takes the next one (the engine's own per-axis dead zone is not used). Leave the pads alone while it runs.
	auto CtlStick = [W](float Deg)
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		APlayerController* PC = O && O->Director && O->Director->GetWorld() ? O->Director->GetWorld()->GetFirstPlayerController() : nullptr;
		if (!PC) return;
		FViewport* VP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
		const FInputDeviceId Dv = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		const float Rad = FMath::DegreesToRadians(Deg);
		const float LX = Deg < 0.f ? 0.f : FMath::Cos(Rad), LY = Deg < 0.f ? 0.f : FMath::Sin(Rad);
		PC->InputKey(FInputKeyEventArgs(VP, Dv, EKeys::Gamepad_LeftX, LX, 1.f / 60.f, 1, FPlatformTime::Cycles64()));
		PC->InputKey(FInputKeyEventArgs(VP, Dv, EKeys::Gamepad_LeftY, LY, 1.f / 60.f, 1, FPlatformTime::Cycles64()));
	};
	Do([CtlStick] { CtlStick(20.f); });
	Frames(1);
	Do([CtlStick] { CtlStick(20.f); });
	Frames(1);
	Do([W, Check, CtlStick]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		APlayerController* PC = O && O->Director && O->Director->GetWorld() ? O->Director->GetWorld()->GetFirstPlayerController() : nullptr;
		if (!PC) { Check(false, TEXT("stick (controller): no player controller")); return; }
		const FTagReader R(PC);
		const FVector2D V = R.Stick(true);
		const float Head = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(V.Y), static_cast<float>(V.X)));
		Check(FMath::Abs(Head - 20.f) < 1.f && FMath::Abs(V.Size() - 1.0) < 0.01, FString::Printf(TEXT("stick (controller) 20 deg at 1.00: heading %.1f deg, length %.2f (the per-axis dead zone read 7.6 deg)"), Head, V.Size()));
		CtlStick(-1.f); // (let go)
	});


	// ---- a play call: formation families (R1), formations (d-pad up / down), pages (d-pad left / right)
	Do([W] { if (auto O = W.Pin()) if (O->Director) O->Director->StartMatch(O->SelHome, O->SelAway, true, true, 1, false, 300); });
	Do([S] { S->StepT = FPlatformTime::Seconds(); });
	Until([W, S, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (O && O->IsPlayCall())
		{
			S->Wait = 0;
			Axis(C, EKeys::Gamepad_RightY, 0.f);
			if (APlayerController* PC0 = O->Director && O->Director->GetWorld() ? O->Director->GetWorld()->GetFirstPlayerController() : nullptr)
			{
				FViewport* VP0 = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
				PC0->InputKey(FInputKeyEventArgs(VP0, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), EKeys::Gamepad_RightY, 0.f, 1.f / 60.f, 1, FPlatformTime::Cycles64()));
				PC0->InputKey(FInputKeyEventArgs(VP0, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), EKeys::Gamepad_FaceButton_Bottom, IE_Released, 0.f, false, FPlatformTime::Cycles64()));
			}
			return true;
		}
		// (a kickoff of ours: the right stick's swing, both ways round as -TagUiShots does; the return: cross now and
		// then on a pad of its own until the call is up. The kick meter reads the player controller in a one-player game,
		// and a made-up device's events reach only Slate's widgets and the hub, never the controller - the engine maps no
		// player to the device: the swing and the cross go to the controller too, as the pad tests press them.)
		const int32 F = ++S->Wait % 120, Sign = (S->Wait / 120) % 2 ? 1 : -1;
		const float RY = F < 40 ? -1.f * Sign : F < 48 ? 1.f * Sign : 0.f;
		Axis(C, EKeys::Gamepad_RightY, RY);
		APlayerController* KPC = O && O->Director && O->Director->GetWorld() ? O->Director->GetWorld()->GetFirstPlayerController() : nullptr;
		FViewport* KVP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
		const FInputDeviceId KDv = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		if (KPC) KPC->InputKey(FInputKeyEventArgs(KVP, KDv, EKeys::Gamepad_RightY, RY, 1.f / 60.f, 1, FPlatformTime::Cycles64()));
		if (S->Wait % 45 == 0)
		{
			Send(C, EKeys::Gamepad_FaceButton_Bottom, true);
			Send(C, EKeys::Gamepad_FaceButton_Bottom, false);
			if (KPC) KPC->InputKey(FInputKeyEventArgs(KVP, KDv, EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, 1.f, false, FPlatformTime::Cycles64()));
		}
		if (S->Wait % 45 == 2 && KPC) KPC->InputKey(FInputKeyEventArgs(KVP, KDv, EKeys::Gamepad_FaceButton_Bottom, IE_Released, 0.f, false, FPlatformTime::Cycles64()));
		if (S->Wait % 240 == 0 && O && O->Director)
		{
			// (what the game is waiting on, every few seconds: the state, a kick meter's stage, the stick as the game reads it)
			const AMatchDirector& D = *O->Director;
			APlayerController* PC = D.GetWorld() ? D.GetWorld()->GetFirstPlayerController() : nullptr;
			const FVector2D RS = PC ? FTagReader(PC).Stick(false) : FVector2D::ZeroVector;
			UE_LOG(LogTemp, Display, TEXT("TAG inputtest waiting for the play call: state %d, kick meter %d stage %d, right stick (%.2f, %.2f) cooked y %.2f, modal %d"),
				static_cast<int32>(D.State), D.UK.bOn ? 1 : 0, D.UK.Stage, RS.X, RS.Y, PC ? PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY) : 0.f, O->HasModal() ? 1 : 0);
		}
		if (FPlatformTime::Seconds() - S->StepT > 150) { Check(false, TEXT("no play call within 150 s")); S->I = S->Steps.Num() - 2; return false; }
		return false;
	});
	Frames(30);
	auto TabAt = [](const SGridironOverlay& O) { return O.CallTabs.IndexOfByPredicate([&O](const TPair<FString, FString>& T) { return T.Key == O.CallTab; }); };
	Do([W, S, TabAt] { if (auto O = W.Pin()) S->Mark = TabAt(*O); });
	Twice(EKeys::Gamepad_RightShoulder, false);
	Do([W, S, Check, TabAt]
	{
		if (auto O = W.Pin())
		{
			const int32 N = FMath::Max(1, O->CallTabs.Num()), Now = TabAt(*O);
			Check(Now == (S->Mark + 1) % N, FString::Printf(TEXT("play call: R1 sent twice moves one formation family (%d -> %d of %d)"), S->Mark, Now, N));
		}
	});
	Do([W, S] { if (auto O = W.Pin()) { S->Mark = O->CallSets.IndexOfByKey(O->CallSet); S->MarkS = O->CallTab; } });
	Twice(EKeys::Gamepad_DPad_Down, true);
	Do([W, S, Check]
	{
		if (auto O = W.Pin())
		{
			const int32 N = O->CallSets.Num(), Now = O->CallSets.IndexOfByKey(O->CallSet);
			if (N > 1) Check(Now == (S->Mark + 1) % N, FString::Printf(TEXT("play call: d-pad down sent twice moves one formation (%d -> %d of %d)"), S->Mark, Now, N));
			else UE_LOG(LogTemp, Display, TEXT("TAG inputtest: (family %s has one formation: no step to check)"), *S->MarkS);
		}
	});
	Do([W, S] { if (auto O = W.Pin()) S->Mark = O->CallPage; });
	Twice(EKeys::Gamepad_DPad_Right, false);
	Do([W, S, Check]
	{
		if (auto O = W.Pin())
		{
			const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(O->CallCards.Num(), 3));
			Check(O->CallPage == (S->Mark + 1) % Pages, FString::Printf(TEXT("play call: d-pad right sent twice turns one page (%d -> %d of %d)"), S->Mark, O->CallPage, Pages));
		}
	});
	Shot(TEXT("playcall"));
	// a press only the "twin" makes: a pad of its own after all (the hub lets it go)
	Do([S]
	{
		const FTagInputHub& H = FTagInputHub::Get();
		const int32 DA = H.Find(FInputDeviceId::CreateFromInternalId(A), true);
		const int32 Twin = H.Dev(DA) && H.Dev(DA)->TwinOf >= 0 ? A : B;
		Send(Twin, EKeys::Gamepad_DPad_Left, true);
		S->StepT = FPlatformTime::Seconds();
	});
	// (the hub gives the pad's own copy a quarter second to come: wait that out in time, not frames - 30 frames at 175 fps
	// were 0.19 s, and the check ran before the hub had decided)
	Until([S] { return FPlatformTime::Seconds() - S->StepT > 0.45; });
	Frames(2);
	Do([] { Send(A, EKeys::Gamepad_DPad_Left, false); Send(B, EKeys::Gamepad_DPad_Left, false); });
	Do([Check]
	{
		const FTagInputHub& H = FTagInputHub::Get();
		const int32 DA = H.Find(FInputDeviceId::CreateFromInternalId(A), true), DB = H.Find(FInputDeviceId::CreateFromInternalId(B), true);
		Check(H.Dev(DA) && H.Dev(DB) && H.Dev(DA)->TwinOf < 0 && H.Dev(DB)->TwinOf < 0, TEXT("a press the other device never made: two pads after all"));
	});
	// the pre-snap look (the HUD, the receivers' icons, the audible panel): a play called on a pad of its own
	Frames(20);
	Do([] { Send(C, EKeys::Gamepad_FaceButton_Bottom, true); });
	Frames(3);
	Do([] { Send(C, EKeys::Gamepad_FaceButton_Bottom, false); });
	Do([S] { S->StepT = FPlatformTime::Seconds(); });
	Until([W, S] { const TSharedPtr<SGridironOverlay> O = W.Pin(); return (O && !O->HasModal() && FPlatformTime::Seconds() - S->StepT > 6.0) || FPlatformTime::Seconds() - S->StepT > 20.0; });
	Shot(TEXT("presnap"));
	Do([] { Send(C, EKeys::Gamepad_FaceButton_Left, true); });
	Frames(3);
	Do([] { Send(C, EKeys::Gamepad_FaceButton_Left, false); });
	Frames(40);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->PreRows.Num() > 0, FString::Printf(TEXT("square opens the pre-snap panel (%d rows)"), O->PreRows.Num())); });
	Shot(TEXT("presnap_panel"));
	Do([S]
	{
		UE_LOG(LogTemp, Display, TEXT("TAG inputtest done: %d pass, %d fail"), S->Pass, S->Fail);
		TagResult::Finish(TEXT("TagInputTest"), S->Pass, S->Fail); // (the result line and the exit status, Game/TagResult.h)
	});

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
	{
		while (S->Steps.IsValidIndex(S->I))
		{
			if (!S->Steps[S->I]()) return true;
			++S->I;
			// (one input step a frame: the copies must land in different frames, as a real pad's do)
			return true;
		}
		return false;
	}));
}

// -TagUiShots: a picture of every menu screen (Saved/Screenshots/.../tag_ui_NN_name.png, with the UI), then the game exits:
// the main menu (the cursor moved), the settings, the team select, then a game: the play call, the pause menu, the box
// score, the try choice. Run it with -RenderOffscreen -TagNoLoad.
void SGridironOverlay::StartUiShots()
{
	using namespace InputTest;
	constexpr int32 P = 8301; // (a pad of its own)
	struct FState { TArray<TFunction<bool()>> Steps; int32 I = 0, Wait = 0, N = 0; double StepT = 0; };
	const TSharedRef<FState> S = MakeShared<FState>();
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
	auto Do = [S](TFunction<void()> F) { S->Steps.Add([F] { F(); return true; }); };
	auto Frames = [S](int32 N) { S->Steps.Add([S, N] { if (++S->Wait < N) return false; S->Wait = 0; return true; }); };
	auto Tap = [Do, Frames](const FKey& K) { Do([K] { Send(P, K, true); }); Frames(2); Do([K] { Send(P, K, false); }); Frames(12); };
	auto Shot = [S, Do](const FString& Name)
	{
		Do([S, Name]
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_ui_%02d_%s.png"), S->N++, *Name), true, false);
			UE_LOG(LogTemp, Display, TEXT("TAG ui shot %s"), *Name);
		});
	};
	auto UntilT = [S](TFunction<bool()> F, double Secs)
	{
		S->Steps.Add([S] { S->StepT = FPlatformTime::Seconds(); return true; });
		S->Steps.Add([S, F, Secs] { return F() || FPlatformTime::Seconds() - S->StepT > Secs; });
	};

	Frames(60);
	Shot(TEXT("menu"));
	Tap(EKeys::Gamepad_DPad_Down);
	Tap(EKeys::Gamepad_DPad_Down);
	Frames(20);
	Shot(TEXT("menu_watch"));
	Do([W] { if (auto O = W.Pin()) O->ShowSettings([W] { if (auto O2 = W.Pin()) O2->ShowMainMenu(); }); });
	Frames(30);
	Tap(EKeys::Gamepad_DPad_Down);
	Frames(20);
	Shot(TEXT("settings"));
	Do([W] { if (auto O = W.Pin()) O->ShowTeamSelect(true); });
	Frames(40);
	Shot(TEXT("teamselect"));
	Do([W] { if (auto O = W.Pin()) O->ShowCreatePlayer(O->SelHome, [W] { if (auto O2 = W.Pin()) O2->ShowMainMenu(); }); });
	Frames(40);
	Shot(TEXT("cap_hub"));
	Do([W] { if (auto O = W.Pin()) O->ShowAbilities(O->SelHome, [W] { if (auto O2 = W.Pin()) O2->ShowMainMenu(); }); });
	Frames(40);
	Shot(TEXT("abilities"));
	Do([W] { if (auto O = W.Pin()) O->ShowDepthChart(O->SelHome, [W] { if (auto O2 = W.Pin()) O2->ShowMainMenu(); }); });
	Frames(40);
	Shot(TEXT("depth_chart"));
	// the match loading screen (the movie player draws it outside the viewport in a real start: here it is put in the
	// overlay for its picture, on its own night backdrop, as when there is no reel)
	Do([W]
	{
		if (auto O = W.Pin())
		{
			FLoadingTeam A{TEXT("Portland"), TEXT("Lumberjacks"), TEXT("POR"), FLinearColor(0.05f, 0.35f, 0.15f)};
			FLoadingTeam H{TEXT("Austin"), TEXT("Outlaws"), TEXT("AUS"), FLinearColor(0.75f, 0.3f, 0.05f)};
			O->ShowModal(SNew(SLoadingScreen).Away(A).Home(H).Conditions(TEXT("Clear, 71F, wind 2 mph  -  night game"))
				.Tips({TEXT("Hold the receiver's button for a bullet, tap it for a lob, tap twice to pump fake.")}).bHasMovie(false));
		}
	});
	Frames(40);
	Shot(TEXT("loading"));
	Do([W] { if (auto O = W.Pin()) O->ShowLobby(); });
	Frames(40);
	Shot(TEXT("lobby"));
	Do([W] { if (auto O = W.Pin()) if (O->Director) O->Director->StartMatch(O->SelHome, O->SelAway, true, false, 1, false, 300); });
	UntilT([W, S]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (O && O->IsPlayCall()) { S->Wait = 0; Axis(P, EKeys::Gamepad_RightY, 0.f); return true; }
		// a kickoff of ours: the right stick's swing (pulled back, then pushed up; both ways round, the stick may be
		// inverted in the settings); a return: cross now and then
		const int32 F = ++S->Wait % 120, Sign = (S->Wait / 120) % 2 ? 1 : -1;
		Axis(P, EKeys::Gamepad_RightY, F < 40 ? -1.f * Sign : F < 48 ? 1.f * Sign : 0.f);
		if (S->Wait % 45 == 0) { Send(P, EKeys::Gamepad_FaceButton_Bottom, true); Send(P, EKeys::Gamepad_FaceButton_Bottom, false); }
		return false;
	}, 200);
	Frames(40);
	Shot(TEXT("playcall"));
	// (the screens themselves: a kickoff of ours may still be waiting on the kick meter)
	Do([W] { if (auto O = W.Pin()) O->ShowPause([] {}, [] {}, [] {}); });
	Frames(40);
	Shot(TEXT("pause"));
	Do([W] { if (auto O = W.Pin()) O->ShowBoxScore(TEXT("Box Score"), TEXT("Resume"), [] {}); });
	Frames(40);
	Shot(TEXT("boxscore"));
	Do([W] { if (auto O = W.Pin()) O->ShowPatChoice([](bool) {}); });
	Frames(40);
	Shot(TEXT("try_choice"));
	Do([] { UE_LOG(LogTemp, Display, TEXT("TAG ui shots done")); FPlatformMisc::RequestExit(false); });

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
	{
		if (!S->Steps.IsValidIndex(S->I)) return false;
		if (S->Steps[S->I]()) ++S->I;
		return true;
	}));
}

// -TagCallTour: pictures of the play-call screens (Saved/Screenshots/.../tag_call_NN_name.png, with the UI), then the
// game exits: the team select (its signature play's card), then in a game the offense's call - every formation family
// (R1), a formation stepped to (d-pad down), the whole family in one list, each play type tab (R2), pages turned
// (d-pad right), the shotgun's passes (the deep routes) and the goal line set - and the defense's: its fronts, a set,
// the whole front, its type tabs, a page. Each shot's log line names what is on it (the tab, the set, the type, the
// page and its three plays). Run it with -RenderOffscreen -ForceRes -ResX= -ResY= -TagNoLoad -LiveCoding=0
// -TagAudioSilent; the screens themselves (no play is run: the tour shows them over the first call and exits).
void SGridironOverlay::StartCallTour()
{
	using namespace InputTest;
	constexpr int32 P = 8401; // (a pad of its own)
	struct FState { TArray<TFunction<bool()>> Steps; int32 I = 0, Wait = 0, N = 0; double StepT = 0; };
	const TSharedRef<FState> S = MakeShared<FState>();
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
	auto Do = [S](TFunction<void()> F) { S->Steps.Add([F] { F(); return true; }); };
	auto Frames = [S](int32 N) { S->Steps.Add([S, N] { if (++S->Wait < N) return false; S->Wait = 0; return true; }); };
	auto Tap = [Do, Frames](const FKey& K) { Do([K] { Send(P, K, true); }); Frames(2); Do([K] { Send(P, K, false); }); Frames(16); };
	auto Shot = [S, Do, Frames, W](const FString& Name)
	{
		Do([S, Name, W]
		{
			FString What;
			if (const TSharedPtr<SGridironOverlay> O = W.Pin())
			{
				What = FString::Printf(TEXT("%s  tab=%s  set=%s  type=%s  page=%d/%d  cards=%d  avail=%.0fx%.0f :"), O->bCallOffense ? TEXT("OFF") : TEXT("DEF"), *O->CallTab, *O->CallSet,
					O->CallFilter().IsEmpty() ? TEXT("ALL") : *O->CallFilter(), O->CallPage + 1, FMath::Max(1, FMath::DivideAndRoundUp(O->CallCards.Num(), CallPerPage)),
					O->CallCards.Num(), O->CallBuiltAvail.X, O->CallBuiltAvail.Y);
				for (int32 k = 0; k < CallPerPage; ++k)
					if (O->CallCards.IsValidIndex(O->CallPage * CallPerPage + k))
					{
						const FCallCard& Cd = O->CallCards[O->CallPage * CallPerPage + k];
						What += FString::Printf(TEXT("  [%s %s | %s]"), *Cd.Cat, *Cd.Name, *Cd.Concept);
					}
			}
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_call_%02d_%s.png"), S->N, *Name), true, false);
			UE_LOG(LogTemp, Display, TEXT("TAG calltour shot %02d %s: %s"), S->N, *Name, *What);
			++S->N;
		});
		Frames(12);
	};
	auto UntilT = [S](TFunction<bool()> F, double Secs)
	{
		S->Steps.Add([S] { S->StepT = FPlatformTime::Seconds(); return true; });
		S->Steps.Add([S, F, Secs] { return F() || FPlatformTime::Seconds() - S->StepT > Secs; });
	};
	// a tab by its id (a family / front), its first set, every type, the first page
	auto ToTab = [Do, W](const FString& Id)
	{
		Do([W, Id]
		{
			if (const TSharedPtr<SGridironOverlay> O = W.Pin())
				if (O->CallTabs.ContainsByPredicate([&Id](const TPair<FString, FString>& Tb) { return Tb.Key == Id; }))
				{
					O->CallTab = Id;
					O->CallSet.Reset();
					O->CallFilter().Reset();
					O->CallPage = 0;
					O->BuildPlayCall();
				}
		});
	};

	Frames(60);
	// the team select: the signature play's card (the same painter as the call screen's)
	Do([W] { if (auto O = W.Pin()) O->ShowTeamSelect(true); });
	Frames(40);
	Shot(TEXT("teamselect"));
	// a game; its first call screen (a kickoff of ours: the right stick's swing; a return: cross now and then)
	Do([W] { if (auto O = W.Pin()) if (O->Director) O->Director->StartMatch(O->SelHome, O->SelAway, true, false, 1, false, 300); });
	UntilT([W, S]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (O && O->IsPlayCall()) { S->Wait = 0; Axis(P, EKeys::Gamepad_RightY, 0.f); return true; }
		const int32 F = ++S->Wait % 120, Sign = (S->Wait / 120) % 2 ? 1 : -1;
		Axis(P, EKeys::Gamepad_RightY, F < 40 ? -1.f * Sign : F < 48 ? 1.f * Sign : 0.f);
		if (S->Wait % 45 == 0) { Send(P, EKeys::Gamepad_FaceButton_Bottom, true); Send(P, EKeys::Gamepad_FaceButton_Bottom, false); }
		return false;
	}, 200);
	Frames(30);

	// ---- the offense's call (the team with the ball's book), whoever's call came up
	Do([W] { if (auto O = W.Pin()) { O->CallFilters[0].Reset(); O->CallFilters[1].Reset(); O->ShowPlayCall(true, false, false, [](FCallChoice) {}); } });
	Frames(30);
	Shot(TEXT("off_open"));
	for (int32 i = 0; i < 5; ++i)
	{
		Tap(EKeys::Gamepad_RightShoulder);
		Shot(FString::Printf(TEXT("off_family%d"), i + 1));
	}
	// the first family again: a formation stepped to, the whole family, the type tabs, pages
	Do([W] { if (auto O = W.Pin()) if (O->CallTabs.Num()) { const int32 F0 = O->CallTabs[0].Key == CallCoachTab() && O->CallTabs.Num() > 1 ? 1 : 0; O->CallTab = O->CallTabs[F0].Key; O->CallSet.Reset(); O->CallPage = 0; O->BuildPlayCall(); } }); // (the first family: past the SUGGESTED tab)
	Frames(10);
	Tap(EKeys::Gamepad_DPad_Down);
	Shot(TEXT("off_formation2"));
	Do([W] { if (auto O = W.Pin()) { O->CallSet = CallAllSet(); O->CallPage = 0; O->BuildPlayCall(); } });
	Frames(10);
	Shot(TEXT("off_family_all"));
	for (int32 i = 0; i < 6; ++i)
	{
		Tap(EKeys::Gamepad_RightTrigger);
		Shot(FString::Printf(TEXT("off_type%d"), i + 1));
	}
	Do([W] { if (auto O = W.Pin()) { O->CallFilter().Reset(); O->CallPage = 0; O->BuildPlayCall(); } });
	Frames(10);
	Tap(EKeys::Gamepad_DPad_Right);
	Shot(TEXT("off_page2"));
	Tap(EKeys::Gamepad_RightStick_Right);
	Shot(TEXT("off_page_jump5"));
	// the shotgun's passes (the deep routes: four verticals, posts, wheels), every page of them up to six
	ToTab(TEXT("Shotgun"));
	Do([W] { if (auto O = W.Pin()) { O->CallSet = CallAllSet(); O->CallFilter() = TEXT("PASS"); O->CallPage = 0; O->BuildPlayCall(); } });
	Frames(10);
	Shot(TEXT("off_gun_pass1"));
	for (int32 i = 0; i < 5; ++i)
	{
		Tap(EKeys::Gamepad_DPad_Right);
		Shot(FString::Printf(TEXT("off_gun_pass%d"), i + 2));
	}
	// runs from under center (the tailback's path from seven deep) and the goal line set (a compact board)
	// (the book's under-center family: I-Form if it has one, else Singleback, else Pistol)
	Do([W]
	{
		if (auto O = W.Pin())
			for (const TCHAR* Fam : {TEXT("I-Form"), TEXT("Singleback"), TEXT("Pistol")})
				if (O->CallTabs.ContainsByPredicate([Fam](const TPair<FString, FString>& Tb) { return Tb.Key == Fam; }))
				{
					O->CallTab = Fam;
					O->CallSet.Reset();
					O->CallFilter() = TEXT("RUN");
					O->CallPage = 0;
					O->BuildPlayCall();
					break;
				}
	});
	Frames(10);
	Shot(TEXT("off_undercenter_run"));
	ToTab(TEXT("Goal Line"));
	Frames(10);
	Shot(TEXT("off_goalline"));

	// ---- the defense's call (the other side's book), with what the offense has come out in
	Do([W]
	{
		if (auto O = W.Pin())
		{
			O->SetCallOffenseLook(TEXT("OFFENSE IN  SHOTGUN  -  Trips     11 PERSONNEL  (1 RB, 1 TE, 3 WR)"));
			O->ShowPlayCall(false, false, false, [](FCallChoice) {});
		}
	});
	Frames(30);
	Shot(TEXT("def_open"));
	for (int32 i = 0; i < 5; ++i)
	{
		Tap(EKeys::Gamepad_RightShoulder);
		Shot(FString::Printf(TEXT("def_front%d"), i + 1));
	}
	Tap(EKeys::Gamepad_DPad_Down);
	Shot(TEXT("def_set2"));
	Do([W] { if (auto O = W.Pin()) { O->CallSet = CallAllSet(); O->CallPage = 0; O->BuildPlayCall(); } });
	Frames(10);
	Shot(TEXT("def_front_all"));
	for (int32 i = 0; i < 4; ++i)
	{
		Tap(EKeys::Gamepad_RightTrigger);
		Shot(FString::Printf(TEXT("def_type%d"), i + 1));
	}
	Tap(EKeys::Gamepad_DPad_Right);
	Shot(TEXT("def_page2"));
	Do([] { UE_LOG(LogTemp, Display, TEXT("TAG calltour done")); FPlatformMisc::RequestExit(false); });

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
	{
		if (!S->Steps.IsValidIndex(S->I)) return false;
		if (S->Steps[S->I]()) ++S->I;
		return true;
	}));
}
