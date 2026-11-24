// -TagInputTest: one button press that reaches the game twice (an Xbox pad is read through XInput and GameInput both,
// a frame or so apart) must move a menu once. The check sends each press from two made-up devices a frame apart, the
// way the pad does, and walks the main menu, the team select and a play call; also the stick (its key and its analog
// value, which Slate used to take as a second push). PASS / FAIL lines, "TAG inputtest done", then the game exits.
#include "UI/GridironOverlay.h"

#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/MatchDirector.h"
#include "Game/TagInput.h"
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

	// ---- a play call: formation families (R1), formations (d-pad up / down), pages (d-pad left / right)
	Do([W] { if (auto O = W.Pin()) if (O->Director) O->Director->StartMatch(O->SelHome, O->SelAway, true, true, 1, false, 300); });
	Do([S] { S->StepT = FPlatformTime::Seconds(); });
	Until([W, S, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (O && O->IsPlayCall()) { S->Wait = 0; Axis(C, EKeys::Gamepad_RightY, 0.f); return true; }
		// (a kickoff of ours: the right stick's swing, both ways round as -TagUiShots does; the return: cross now and
		// then on a pad of its own until the call is up)
		const int32 F = ++S->Wait % 120, Sign = (S->Wait / 120) % 2 ? 1 : -1;
		Axis(C, EKeys::Gamepad_RightY, F < 40 ? -1.f * Sign : F < 48 ? 1.f * Sign : 0.f);
		if (S->Wait % 45 == 0) { Send(C, EKeys::Gamepad_FaceButton_Bottom, true); Send(C, EKeys::Gamepad_FaceButton_Bottom, false); }
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
	Do([]
	{
		const FTagInputHub& H = FTagInputHub::Get();
		const int32 DA = H.Find(FInputDeviceId::CreateFromInternalId(A), true);
		const int32 Twin = H.Dev(DA) && H.Dev(DA)->TwinOf >= 0 ? A : B;
		Send(Twin, EKeys::Gamepad_DPad_Left, true);
	});
	Frames(30);
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
		FPlatformMisc::RequestExit(false);
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
	Do([W] { if (auto O = W.Pin()) if (O->CallTabs.Num()) { O->CallTab = O->CallTabs[0].Key; O->CallSet.Reset(); O->CallPage = 0; O->BuildPlayCall(); } });
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
