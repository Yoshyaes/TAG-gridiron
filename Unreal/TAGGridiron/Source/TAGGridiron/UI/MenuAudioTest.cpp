// -TagMenuAudio: a walk through everything before the kickoff, by the pad, so the menus' sound can be checked: the main
// menu, the team select (teams, options, random, swap), the abilities and the depth chart from it, Create-a-Player
// (values, a typed name, a save), the settings' volumes, the lobby (two players in, sides, the caller) and the kickoff.
// Nothing is saved. Run it with -TagAudioLog (every sound as a "TAG audio" line) and -TagAudioRec=<s> (the mix as a
// wav); "TAG menuaudio <step>" lines mark where the walk is; "TAG menuaudio done", then the game exits.
#include "UI/GridironOverlay.h"

#include "Audio/TagAudio.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/MatchDirector.h"

namespace InputTest
{
	void Send(int32 DeviceId, const FKey& K, bool bDown); // (UI/InputTest.cpp)
}

void SGridironOverlay::StartMenuAudioTest()
{
	using namespace InputTest;
	constexpr int32 P = 8401, Q = 8402; // two pads of their own
	struct FState { TArray<TFunction<bool()>> Steps; int32 I = 0; double T = 0; };
	const TSharedRef<FState> S = MakeShared<FState>();
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
	auto Do = [S](TFunction<void()> F) { S->Steps.Add([F] { F(); return true; }); };
	auto Wait = [S](double Secs)
	{
		S->Steps.Add([S] { S->T = FPlatformTime::Seconds(); return true; });
		S->Steps.Add([S, Secs] { return FPlatformTime::Seconds() - S->T >= Secs; });
	};
	auto TapOn = [Do, Wait](int32 Dev, const FKey& K, double After = 0.32)
	{
		Do([Dev, K] { Send(Dev, K, true); });
		Wait(0.05);
		Do([Dev, K] { Send(Dev, K, false); });
		Wait(After);
	};
	auto Tap = [TapOn](const FKey& K, double After = 0.32) { TapOn(8401, K, After); };
	auto Say = [Do](const TCHAR* What) { Do([What] { UE_LOG(LogTemp, Display, TEXT("TAG menuaudio %s"), What); }); };
	const FKey Up = EKeys::Gamepad_DPad_Up, Down = EKeys::Gamepad_DPad_Down, Left = EKeys::Gamepad_DPad_Left, Right = EKeys::Gamepad_DPad_Right;
	const FKey Ok = EKeys::Gamepad_FaceButton_Bottom, Back = EKeys::Gamepad_FaceButton_Right, Square = EKeys::Gamepad_FaceButton_Left, Triangle = EKeys::Gamepad_FaceButton_Top;
	const FKey L1 = EKeys::Gamepad_LeftShoulder, R1 = EKeys::Gamepad_RightShoulder, L2 = EKeys::Gamepad_LeftTrigger, R2 = EKeys::Gamepad_RightTrigger, Start = EKeys::Gamepad_Special_Right;

	// ---- the title arrives, the playlist comes in; the cursor over the tiles
	Say(TEXT("main"));
	Wait(6.0);
	for (int32 i = 0; i < 3; ++i) Tap(Down);
	for (int32 i = 0; i < 3; ++i) Tap(Up);
	Wait(0.6);
	// ---- Play Now: the team select
	Say(TEXT("teamselect"));
	Tap(Ok, 1.2);
	Tap(Left); Tap(Left);                                   // (the cursor opens on the user's team: over to the away team)
	for (int32 i = 0; i < 3; ++i) Tap(Down, 0.45);          // other teams
	Tap(Right);                                             // to the options
	Tap(Down); Tap(R1); Tap(R1); Tap(L1); Tap(Ok);          // an option stepped up, down, on
	Tap(Right);                                             // the other team
	Tap(Up, 0.45); Tap(Up, 0.45);
	Tap(Square, 1.0);                                       // random
	Tap(Triangle, 1.0);                                     // swap home / away
	// ---- its abilities
	Say(TEXT("abilities"));
	Tap(R2, 1.2);
	Tap(Down); Tap(Right);
	// each option twice: one he carries comes off and goes back on (removed, equipped); one he cannot take is refused
	for (int32 i = 0; i < 6; ++i) { Tap(Ok, 0.9); Tap(Ok, 1.1); Tap(Down, 0.3); }
	Tap(Back, 1.2);
	// ---- its depth chart
	Say(TEXT("depthchart"));
	Tap(L2, 1.2);
	Tap(Down); Tap(Right);
	Tap(Ok, 0.6); Tap(Down, 0.4); Tap(Down, 0.4); Tap(Ok, 0.8);     // a man picked up, carried two places, put down
	Tap(EKeys::Gamepad_LeftThumbstick, 0.8);                // the slot back to the coach's
	Tap(Back, 0.5); Tap(Back, 1.2);
	Say(TEXT("back to main"));
	Tap(Back, 1.5);
	// ---- Create-a-Player
	Say(TEXT("createplayer"));
	Do([W] { if (auto O = W.Pin()) O->ShowCreatePlayer(O->SelHome, [W] { if (auto O2 = W.Pin()) O2->ShowMainMenu(); }); });
	Wait(1.2);
	Tap(Ok, 1.0);                                           // a new player: the form
	Tap(Right); Tap(Right); Tap(Left);                      // the team stepped
	for (int32 i = 0; i < 4; ++i) Tap(Down, 0.25);          // down to the first name
	Tap(Ok, 0.6);                                           // type it
	for (const TCHAR C : {TEXT('T'), TEXT('a'), TEXT('g')})
	{
		Do([C] { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(C, FModifierKeysState(), 0, false)); } });
		Wait(0.16);
	}
	Do([] { Send(8401, EKeys::Enter, true); });
	Wait(0.05);
	Do([] { Send(8401, EKeys::Enter, false); });
	Wait(0.8);
	Tap(Square, 1.0);                                       // a rolled profile
	Tap(Start, 1.6);                                        // signed
	Tap(Back, 1.5);
	// ---- the settings: each volume stepped (the stadium's answers with a crowd)
	Say(TEXT("settings"));
	Do([W] { if (auto O = W.Pin()) O->ShowSettings([W] { if (auto O2 = W.Pin()) O2->ShowMainMenu(); }); });
	Wait(1.2);
	Tap(Down); Tap(Down); Tap(Up);
	// (the volume tiles' own action, not a press: the walk must not flip whatever tile the screen has there today)
	Do([] { ATagAudio::StepLevel(0); });
	Wait(0.8);
	Do([] { ATagAudio::StepLevel(2); });
	Wait(2.5);
	Do([W] { if (auto O = W.Pin()) O->ShowMainMenu(); });
	Wait(1.5);
	// ---- the lobby: two players in, a side each, one calls the plays; then the matchup and the kickoff
	Say(TEXT("lobby"));
	Do([W] { if (auto O = W.Pin()) if (O->Director) O->Director->ShowLobby(); });
	Wait(1.5);
	TapOn(P, Ok, 0.8);
	TapOn(Q, Ok, 0.8);
	TapOn(P, Start, 0.8);                                   // (nobody has a side yet: refused)
	TapOn(P, Right, 0.6);
	TapOn(Q, Left, 0.6);
	TapOn(Q, Triangle, 0.8);
	TapOn(Q, Back, 0.6);                                    // off his side...
	TapOn(Q, Back, 0.9);                                    // ...and out of the lobby
	TapOn(P, Start, 2.0);
	Say(TEXT("kickoff"));
	Tap(Down, 0.5);
	Tap(Start, 9.0);
	Do([] { UE_LOG(LogTemp, Display, TEXT("TAG menuaudio done")); FPlatformMisc::RequestExit(false); });

	UE_LOG(LogTemp, Display, TEXT("TAG menuaudio start: %d steps"), S->Steps.Num());
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
	{
		if (!S->Steps.IsValidIndex(S->I)) return false;
		static double Beat = 0.0;
		if (FPlatformTime::Seconds() - Beat > 10.0) { Beat = FPlatformTime::Seconds(); UE_LOG(LogTemp, Display, TEXT("TAG menuaudio at step %d of %d"), S->I, S->Steps.Num()); }
		if (S->Steps[S->I]()) ++S->I;
		return true;
	}));
}
