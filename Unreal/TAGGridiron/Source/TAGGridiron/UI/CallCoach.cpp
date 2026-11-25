// The coach on the play-call screen (GridironOverlay.h): the SUGGESTED tab (two or three plays the coach would call
// here, each with a line on why: the situation and what this game's tendencies say, Sim/CallAdapt.h), the coach's read
// of the tendencies down the left, TRIANGLE (keyboard H) calling the top suggestion, and the call timer: tied to the
// play clock (the offense's call ends with time left to get set and snap), and when it runs out the coach's pick is
// called (on 4th down his punt / kick / go). -TagCallCoachTest checks it in a game ("TAG RESULT TagCallCoachTest").
// -TagNoCallTimer turns the timer off.
#include "UI/GridironOverlay.h"

#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Input/Events.h"
#include "Game/MatchDirector.h"
#include "Game/TagResult.h"
#include "Misc/CommandLine.h"
#include "Sim/CallAdapt.h"
#include "Sim/Game.h"
#include "Sim/Playbook.h"
#include "UI/TagTheme.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

/** The coach's state for the call screen up (GridironOverlay.h: Coach). */
struct FCallCoach
{
	struct FSugg
	{
		FCallChoice Choice;
		FString Reason;
	};
	TArray<FSugg> Sugg;
	TArray<FString> Read;
	// the timer: 0 the offense's (the play clock less Reserve), 1 the defense's (the same), 2 a countdown of its own
	bool bArmed = false;
	int32 Mode = 2;
	float Reserve = 12.f;
	float Window = 25.f;
	float Elapsed = 0.f;
	float Total = 1.f;
	double OpenT = 0.0;
	// how the last call was made ("button", "coach", "timer") and what it was (the test reads them)
	FString PickHow = TEXT("button");
	FString LastHow;
	FCallChoice LastPick;
	int32 Opens = 0;
};

namespace
{
	FString CoachS(const std::string& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	/** The screens and tests that hold the call screen up on purpose (their pictures, their key walks): no timer. */
	bool CoachTimerOff()
	{
		static const bool bOff = [] {
			const TCHAR* C = FCommandLine::Get();
			for (const TCHAR* F : {TEXT("TagNoCallTimer"), TEXT("TagCallTour"), TEXT("TagUiShots"), TEXT("TagInputTest"), TEXT("TagCallShot"), TEXT("TagControlsTest")})
				if (FParse::Param(C, F)) return true;
			return false;
		}();
		return bOff;
	}

	bool SameChoice(const FCallChoice& A, const FCallChoice& B) { return A.Kind == B.Kind && A.Play == B.Play && A.Call == B.Call; }

	FString ChoiceName(const FCallChoice& C)
	{
		switch (C.Kind)
		{
		case FCallChoice::Punt: return TEXT("Punt");
		case FCallChoice::FG: return TEXT("Field Goal");
		case FCallChoice::Off: return C.Play ? CoachS(C.Play->name) : FString();
		case FCallChoice::Def: return C.Call ? CoachS(C.Call->name) : FString();
		default: return FString();
		}
	}
}

void SGridironOverlay::SetCallOffenseForm(const FString& FormationId) { CallOffForm = FormationId; }

void SGridironOverlay::CallCoachOpen()
{
	if (!Coach.IsValid()) Coach = MakeShared<FCallCoach>();
	FCallCoach& K = *Coach;
	K.Sugg.Reset();
	K.Read.Reset();
	K.bArmed = false;
	K.Elapsed = 0.f;
	K.PickHow = TEXT("button");
	K.OpenT = FPlatformTime::Seconds();
	++K.Opens;
	if (!Director || !Director->Game) return;
	const tag::Game& G = *Director->Game;
	const std::string Form = TCHAR_TO_UTF8(*CallOffForm);
	if (!bCallTwo && G.phase == tag::GamePhase::Scrimmage)
	{
		// (the suggestions draw an Rng of their own, seeded from the moment: the game's is never touched)
		const std::vector<tag::CallSuggestion> Sg = bCallOffense ? tag::suggestOffense(G, 3) : tag::suggestDefense(G, Form, 3);
		for (const tag::CallSuggestion& S : Sg)
		{
			FCallCoach::FSugg& O = K.Sugg.AddDefaulted_GetRef();
			O.Choice.Kind = S.off ? FCallChoice::Off : FCallChoice::Def;
			O.Choice.Play = S.off;
			O.Choice.Call = S.def;
			O.Reason = CoachS(S.reason);
		}
		for (const std::string& L : tag::coachRead(G, bCallOffense, Form)) K.Read.Add(CoachS(L));
	}
	// the SUGGESTED tab first (after the kicks on 4th down), and the screen opens on it (4th down: on the kicks)
	if (K.Sugg.Num())
	{
		const bool bKicks = bCallOffense && bCallFourth;
		CallTabs.Insert(TPair<FString, FString>(CallCoachTab(), TEXT("SUGGESTED")), bKicks ? FMath::Min(1, CallTabs.Num()) : 0);
		if (!bKicks)
		{
			CallTab = CallCoachTab();
			CallPage = 0;
		}
	}
	// the call timer: the offense's ends with ~12 s on the play clock (time to break the huddle, get set and snap; at
	// least 6 s of call when the screen comes up late), the defense's with 8 s (a CPU offense is waiting at the line); a
	// multiplayer defense (the play clock holds at 10 for him) or a game without the real clock counts its own
	K.bArmed = !CoachTimerOff() && CallDone != nullptr;
	const bool bClock = Director->bRealClock && Director->bPlayClockOn;
	if (bClock && bCallOffense)
	{
		K.Mode = 0;
		K.Reserve = FMath::Clamp(Director->PlayClockF - 6.f, 4.f, 12.f);
	}
	else if (bClock && !Director->bMulti)
	{
		K.Mode = 1;
		K.Reserve = FMath::Clamp(Director->PlayClockF - 6.f, 3.f, 8.f);
	}
	else
	{
		K.Mode = 2;
		K.Window = bCallOffense ? 25.f : 15.f;
	}
	K.Total = FMath::Max(1.f, CallTimerLeft());
	FString Line;
	for (const FCallCoach::FSugg& S : K.Sugg) Line += FString::Printf(TEXT("  [%s | %s]"), *ChoiceName(S.Choice), *S.Reason);
	UE_LOG(LogTemp, Display, TEXT("TAG coach %s: %d suggestions, timer %s %.1f s:%s"), bCallOffense ? TEXT("OFF") : TEXT("DEF"), K.Sugg.Num(),
		K.bArmed ? TEXT("on") : TEXT("off"), K.bArmed ? K.Total : 0.f, *Line);
}

bool SGridironOverlay::CallCoachCards()
{
	if (CallTab != CallCoachTab() || !Coach.IsValid()) return false;
	for (const FCallCoach::FSugg& S : Coach->Sugg)
	{
		FCallCard& Cd = CallCards.AddDefaulted_GetRef();
		Cd.Choice = S.Choice;
		Cd.Op = S.Choice.Play;
		Cd.Dc = S.Choice.Call;
		Cd.Note = S.Reason;
		Cd.Name = ChoiceName(S.Choice);
		Cd.Kind = Cd.Op ? CoachS(tag::playCatName(Cd.Op->cat)) : FString(TEXT("Defense"));
		if (Cd.Dc)
		{
			const tag::DefCall& D = *Cd.Dc;
			const FString Cover = D.coverLabel.empty() ? CoachS(tag::coverageName(D.coverage)).ToUpper() : CoachS(D.coverLabel);
			const FString Press = D.pressure.empty() || D.pressure == "Base" ? FString() : TEXT("  -  ") + CoachS(D.pressure);
			Cd.Concept = FString::Printf(TEXT("%s%s  -  %d rush"), *Cover, *Press, D.rushers());
			Cd.Form = D.frontLabel.empty() ? CoachS(tag::defSet(D)) : CoachS(D.frontLabel);
		}
		else if (Cd.Op)
			if (const tag::Formation* F = tag::formationById(Cd.Op->formation)) Cd.Form = CoachS(F->name);
	}
	return true;
}

TSharedRef<SWidget> SGridironOverlay::CallCoachRead() const
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	const TArray<FString> None = {bCallOffense ? FString(TEXT("No tendencies yet: the picks follow the down, the distance and the clock."))
		: FString(TEXT("No tendencies yet: the calls follow the down, the distance and their personnel."))};
	const TArray<FString>& Lines = Coach.IsValid() && Coach->Read.Num() ? Coach->Read : None;
	for (const FString& L : Lines)
		V->AddSlot().AutoHeight().Padding(4, 0, 0, 6)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 6, 8, 0)
			[ SNew(SBox).WidthOverride(5).HeightOverride(5)[ SNew(SBorder).BorderImage(TagTheme::Round(2.5f)).BorderBackgroundColor(TagTheme::Gold) ] ]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[ SNew(STextBlock).Font(TagTheme::Font(12)).ColorAndOpacity(TagTheme::Dim).AutoWrapText(true).Text(FText::FromString(L)) ]
		];
	V->AddSlot().AutoHeight().Padding(4, 6, 0, 0)
	[ SNew(STextBlock).Font(TagTheme::Font(10)).ColorAndOpacity(TagTheme::Faint).AutoWrapText(true).Text(FText::FromString(TEXT("L1 / R1 for the whole playbook."))) ];
	return V;
}

bool SGridironOverlay::CallTimerOn() const { return Coach.IsValid() && Coach->bArmed && CallDone != nullptr; }

float SGridironOverlay::CallTimerLeft() const
{
	if (!Coach.IsValid() || !Coach->bArmed || !Director) return -1.f;
	const FCallCoach& K = *Coach;
	if (K.Mode != 2 && Director->bPlayClockOn) return Director->PlayClockF - K.Reserve;
	return K.Window - K.Elapsed;
}

TSharedRef<SWidget> SGridironOverlay::CallTimerWidget() const
{
	constexpr float BarW = 92.f;
	return SNew(SBorder).BorderImage(TagTheme::Round(4.f)).BorderBackgroundColor(FLinearColor(0.08f, 0.10f, 0.14f, 1.f)).Padding(FMargin(14, 4, 14, 6))
		.Visibility_Lambda([this] { return CallTimerOn() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[ SNew(STextBlock).Font(TagTheme::Font(9, "Bold", 60)).ColorAndOpacity(TagTheme::Dim).Text(FText::FromString(TEXT("CALL"))) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(TagTheme::Font(24, "Bold"))
				.Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT(":%02d"), FMath::Max(0, FMath::CeilToInt(CallTimerLeft())))); })
				.ColorAndOpacity_Lambda([this] { return FSlateColor(CallTimerLeft() <= 5.f ? FLinearColor(1.f, 0.25f, 0.2f) : TagTheme::Gold); })
			]
			// the time left as a bar that runs down to the coach's pick
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 3, 0, 0)
			[
				SNew(SBox).WidthOverride(BarW).HeightOverride(3)
				[
					SNew(SBorder).BorderImage(TagTheme::Round(1.5f)).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.08f)).Padding(0).HAlign(HAlign_Left)
					[
						SNew(SBox).HeightOverride(3)
						.WidthOverride_Lambda([this, BarW] { return FOptionalSize(BarW * FMath::Clamp(CallTimerLeft() / FMath::Max(1.f, Coach.IsValid() ? Coach->Total : 1.f), 0.f, 1.f)); })
						[ SNew(SBorder).BorderImage(TagTheme::Round(1.5f)).BorderBackgroundColor_Lambda([this] { return FSlateColor(CallTimerLeft() <= 5.f ? FLinearColor(1.f, 0.25f, 0.2f) : TagTheme::Gold); }) ]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3, 0, 0)
			[ SNew(STextBlock).Font(TagTheme::Font(8, "Bold", 40)).ColorAndOpacity(TagTheme::Faint).Text(FText::FromString(TEXT("THEN COACH'S PICK"))) ]
		];
}

void SGridironOverlay::CallTimerTick(float Dt)
{
	if (!CallTimerOn()) return;
	Coach->Elapsed += Dt;
	if (CallTimerLeft() > 0.f) return;
	// time: the coach's pick, said on screen
	const FCallChoice C = CallCoachPick();
	const FString Name = ChoiceName(C);
	Coach->PickHow = TEXT("timer");
	Toast(Name.IsEmpty() ? FString(TEXT("COACH'S CALL")) : TEXT("COACH'S CALL  -  ") + Name);
	CallPick(C);
}

FCallChoice SGridironOverlay::CallCoachPick() const
{
	// 4th down: the coach's own decision (punt, kick, or go with his top play)
	if (bCallOffense && bCallFourth && Director && Director->Game)
	{
		const tag::FourthDownCall Fd = Director->Game->fourthDownCall();
		FCallChoice K;
		if (Fd == tag::FourthDownCall::Punt) { K.Kind = FCallChoice::Punt; return K; }
		if (Fd == tag::FourthDownCall::Fg) { K.Kind = FCallChoice::FG; return K; }
	}
	if (Coach.IsValid() && Coach->Sugg.Num()) return Coach->Sugg[0].Choice;
	return FCallChoice(); // (none: the CPU caller's own draw)
}

FString SGridironOverlay::CallCoachPickName() const
{
	const FString N = ChoiceName(CallCoachPick());
	return N.IsEmpty() ? FString(bCallOffense ? TEXT("Coach calls it") : TEXT("Coordinator calls it")) : N;
}

void SGridironOverlay::CallCoachPress()
{
	if (Coach.IsValid()) Coach->PickHow = TEXT("coach");
	CallPick(CallCoachPick());
}

void SGridironOverlay::CallCoachPicked(const FCallChoice& C)
{
	if (!Coach.IsValid()) return;
	FCallCoach& K = *Coach;
	const double Secs = FPlatformTime::Seconds() - K.OpenT;
	bool bSugg = false;
	for (const FCallCoach::FSugg& S : K.Sugg) bSugg = bSugg || SameChoice(S.Choice, C);
	UE_LOG(LogTemp, Display, TEXT("TAG call time %.1f s (%s, %s%s): %s"), Secs, *K.PickHow, bCallOffense ? TEXT("offense") : TEXT("defense"),
		bSugg ? TEXT(", suggested") : TEXT(""), *ChoiceName(C));
	K.LastHow = K.PickHow;
	K.LastPick = C;
	K.PickHow = TEXT("button");
	K.bArmed = false;
}

// -TagCallCoachTest: a game's call screens: the first opens on the suggestions (two or three, each from the book and
// with a reason, the timer running, the game's RNG untouched by them) and TRIANGLE calls the top one; the next one is
// left alone, its timer brought near its end, and the coach's pick must be called by it before the play clock runs
// out (no delay of game). "TAG coachtest PASS / FAIL" lines, then "TAG RESULT TagCallCoachTest pass=n fail=n". Run it
// with -game -RenderOffscreen -TagNoLoad -TagNoBooth -TagAudioSilent -LiveCoding=0 (and -TagTimeout=300).
void SGridironOverlay::StartCallCoachTest()
{
	constexpr int32 Pad = 8501; // (a pad of its own, under the hub's automation range)
	struct FState
	{
		TArray<TFunction<bool()>> Steps;
		int32 I = 0, Wait = 0, Pass = 0, Fail = 0, N = 0, Dog = 0;
		double StepT = 0;
		FCallChoice Top;
	};
	const TSharedRef<FState> S = MakeShared<FState>();
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
	auto Send = [](const FKey& K, bool bDown)
	{
		if (!FSlateApplication::IsInitialized()) return;
		const FKeyEvent E(K, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(Pad), false, 0, 0, TOptional<int32>(0));
		if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(E);
		else FSlateApplication::Get().ProcessKeyUpEvent(E);
	};
	auto Axis = [](const FKey& K, float V)
	{
		if (!FSlateApplication::IsInitialized()) return;
		const FAnalogInputEvent E(K, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(Pad), false, 0, 0, V, TOptional<int32>(0));
		FSlateApplication::Get().ProcessAnalogInputEvent(E);
	};
	auto Check = [S](bool bOk, const FString& What)
	{
		(bOk ? S->Pass : S->Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG coachtest %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	auto Do = [S](TFunction<void()> F) { S->Steps.Add([F] { F(); return true; }); };
	auto Frames = [S](int32 N) { S->Steps.Add([S, N] { if (++S->Wait < N) return false; S->Wait = 0; return true; }); };
	auto Shot = [S, Do](const FString& Name)
	{
		Do([S, Name]
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_coach_%02d_%s.png"), S->N++, *Name), true, false);
			UE_LOG(LogTemp, Display, TEXT("TAG coachtest shot %s"), *Name);
		});
	};
	// on to the next call screen: a kickoff of ours on the right stick's swing, a snap / a return on CROSS now and then
	// (never while a screen is up)
	auto UntilCall = [S, W, Send, Axis](double Secs)
	{
		S->Steps.Add([S] { S->StepT = FPlatformTime::Seconds(); S->Wait = 0; return true; });
		S->Steps.Add([S, W, Send, Axis, Secs]
		{
			const TSharedPtr<SGridironOverlay> O = W.Pin();
			if ((O && O->IsPlayCall() && O->ModalContent == O->CallRoot) || FPlatformTime::Seconds() - S->StepT > Secs) { Axis(EKeys::Gamepad_RightY, 0.f); S->Wait = 0; return true; }
			const int32 F = ++S->Wait % 120, Sign = (S->Wait / 120) % 2 ? 1 : -1;
			Axis(EKeys::Gamepad_RightY, F < 40 ? -1.f * Sign : F < 48 ? 1.f * Sign : 0.f);
			if (S->Wait % 45 == 0 && O && !O->HasModal()) { Send(EKeys::Gamepad_FaceButton_Bottom, true); Send(EKeys::Gamepad_FaceButton_Bottom, false); }
			return false;
		});
	};
	auto Dogs = [](const tag::Game& G)
	{
		int32 N = 0;
		for (const tag::PlayLog& L : G.log) N += L.desc.find("Delay of Game") != std::string::npos ? 1 : 0;
		return N;
	};

	Frames(60);
	Do([W] { if (auto O = W.Pin()) if (O->Director) O->Director->StartMatch(O->SelHome, O->SelAway, true, false, 1, false, 300); });
	UntilCall(240.0);
	Frames(30);
	// ---- 1. the screen opens on the suggestions
	Do([W, S, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || !O->Director->Game || !O->IsPlayCall()) { Check(false, TEXT("a call screen came up")); return; }
		const tag::Game& G = *O->Director->Game;
		Check(O->CallTab == CallCoachTab() || (O->bCallOffense && O->bCallFourth), FString::Printf(TEXT("the %s call opens on SUGGESTED (tab %s)"), O->bCallOffense ? TEXT("offense's") : TEXT("defense's"), *O->CallTab));
		Check(O->CallCards.Num() >= 2 && O->CallCards.Num() <= 3, FString::Printf(TEXT("two or three suggestions (%d)"), O->CallCards.Num()));
		for (const FCallCard& Cd : O->CallCards)
		{
			const bool bBook = (Cd.Op && tag::offBookFor(G.offense()).has(Cd.Op)) || (Cd.Dc && tag::defBookFor(G.defense()).has(Cd.Dc));
			Check(bBook && !Cd.Note.IsEmpty(), FString::Printf(TEXT("from the book, with a reason: %s  -  \"%s\""), *Cd.Name, *Cd.Note));
		}
		// the same moment suggests the same plays, and the game's RNG is not drawn for them
		const uint32 Before = G.rng.state();
		const std::vector<tag::CallSuggestion> Again = O->bCallOffense ? tag::suggestOffense(G, 3) : tag::suggestDefense(G, TCHAR_TO_UTF8(*O->CallOffForm), 3);
		bool bSame = static_cast<int32>(Again.size()) == O->Coach->Sugg.Num();
		for (int32 i = 0; bSame && i < O->Coach->Sugg.Num(); ++i)
			bSame = Again[i].off == O->Coach->Sugg[i].Choice.Play && Again[i].def == O->Coach->Sugg[i].Choice.Call;
		Check(bSame && G.rng.state() == Before, TEXT("the suggestions are repeatable and leave the game's RNG alone"));
		Check(O->CallTimerOn() && O->CallTimerLeft() > 3.f, FString::Printf(TEXT("the call timer runs (%.1f s left, mode %d)"), O->CallTimerLeft(), O->Coach->Mode));
		S->Top = O->CallCoachPick();
	});
	Shot(TEXT("open"));
	Frames(10);
	// ---- 2. TRIANGLE: the coach's pick, the top suggestion
	Do([Send] { Send(EKeys::Gamepad_FaceButton_Top, true); });
	Frames(3);
	Do([Send] { Send(EKeys::Gamepad_FaceButton_Top, false); });
	Frames(10);
	Do([W, S, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Coach.IsValid()) { Check(false, TEXT("the coach")); return; }
		Check(O->Coach->LastHow == TEXT("coach") && SameChoice(O->Coach->LastPick, S->Top), FString::Printf(TEXT("triangle calls the coach's pick (%s, %s)"), *O->Coach->LastHow, *ChoiceName(O->Coach->LastPick)));
	});
	// ---- 3. the next call screen, left alone: its timer brought near its end calls the coach's pick in time
	UntilCall(240.0);
	Frames(20);
	Do([W, S, Check, Dogs]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || !O->Director->Game || !O->IsPlayCall() || !O->Coach.IsValid()) { Check(false, TEXT("a second call screen came up")); return; }
		S->Top = O->CallCoachPick();
		S->Dog = Dogs(*O->Director->Game);
		FCallCoach& K = *O->Coach;
		if (K.Mode == 2 || !O->Director->bPlayClockOn) K.Elapsed = K.Window - 2.5f;
		else O->Director->PlayClockF = K.Reserve + 2.5f;
	});
	Frames(4);
	Do([W, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		const float L = O ? O->CallTimerLeft() : -1.f;
		Check(O && O->IsPlayCall() && L > 0.f && L <= 2.6f, FString::Printf(TEXT("the timer shows the last seconds (%.1f)"), L));
	});
	Shot(TEXT("timer"));
	S->Steps.Add([S] { S->StepT = FPlatformTime::Seconds(); return true; });
	S->Steps.Add([S, W] { const TSharedPtr<SGridironOverlay> O = W.Pin(); return !O || !O->IsPlayCall() || FPlatformTime::Seconds() - S->StepT > 8.0; });
	Do([W, S, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Coach.IsValid()) { Check(false, TEXT("the coach")); return; }
		Check(O->Coach->LastHow == TEXT("timer") && SameChoice(O->Coach->LastPick, S->Top), FString::Printf(TEXT("time up: the coach's pick is called (%s, %s)"), *O->Coach->LastHow, *ChoiceName(O->Coach->LastPick)));
	});
	Frames(150);
	Do([W, S, Check, Dogs]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		const int32 Now = O && O->Director && O->Director->Game ? Dogs(*O->Director->Game) : -1;
		Check(Now == S->Dog, FString::Printf(TEXT("no delay of game after the timer's call (%d -> %d)"), S->Dog, Now));
	});
	Do([S]
	{
		UE_LOG(LogTemp, Display, TEXT("TAG coachtest done: %d pass, %d fail"), S->Pass, S->Fail);
		TagResult::Finish(TEXT("TagCallCoachTest"), S->Pass, S->Fail);
	});

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
	{
		if (!S->Steps.IsValidIndex(S->I)) return false;
		if (S->Steps[S->I]()) ++S->I;
		return true;
	}));
}
