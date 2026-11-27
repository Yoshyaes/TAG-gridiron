// The pad's rumble (Settings > Vibration: Off / Low / Normal, GameUserSettings [TAG.Settings] Rumble). A few named
// effects, each the two motors (the heavy low one and the light high one) for a length: the hits given and taken, the
// sack, the catch, the fumble, the kick, the snap, a touchdown. The director plays them for the events of the men the
// players have (Game/MatchFeel.cpp, AMatchDirector::TickRumble) - never for the CPU's own, never while the game is paused
// or a replay runs - and every one is logged: "TAG haptic <effect> <ms> ms <strength> (<why>)".
//
// The motors go out through the player controller's dynamic force feedback. The engine sends that to the pads of the
// platform user, and every device is mapped to the one user (Config/Windows/WindowsInput.ini, so that one controller
// reads every pad): XInput gives it to the pad used last, GameInput to every pad it has. One player's pad cannot be
// picked out of several, so a multiplayer game rumbles only when one player is on a pad (TickRumble).
//
// Channels: LeftLarge carries the low motor, RightLarge the high one. GameInput reads those two as its low and high
// frequency motors (its LeftSmall / RightSmall are the trigger motors, never touched here); XInput puts the larger of the
// two on its heavy motor (it drives its light motor from the Small channels), so on that path a light tick is felt in
// the heavy motor.
#pragma once

#include "CoreMinimal.h"

class APlayerController;

enum class ETagRumble : uint8
{
	Snap,          // the ball in the quarterback's hands: a faint tick
	Contact,       // a wrap, a broken tackle, a block
	TackleGiven,
	TackleTaken,
	BigHitGiven,   // the hit stick landed, a truck
	BigHitTaken,
	SackGiven,
	SackTaken,
	Catch,
	CatchTraffic,  // a catch with a defender on him, an interception
	Fumble,        // two pulses
	KickStrike,    // the kick meter's swing locked
	Touchdown,     // a long low roll
	Test,          // the settings screen's sample
};

class FTagRumble
{
public:
	static FTagRumble& Get();
	/** The setting: 0 off, 1 low (half strength), 2 normal. */
	static int32 Level();
	/** Normal -> Low -> Off -> Normal (the settings button), saved; a sample is played at the new strength. */
	static void StepLevel();
	static const TCHAR* LevelName(int32 L);
	static const TCHAR* Name(ETagRumble Fx);

	/** Play Fx at Scale (0..1 of its full strength: how hard the hit was) for Why (the log line). NotSent: the reason
	 *  it is logged but not sent to the pad (the player is on the keyboard, two players on pads); null: sent. */
	void Play(ETagRumble Fx, float Scale, const FString& Why, const TCHAR* NotSent = nullptr);
	/** Each frame: the motors as the effects now playing have them. bHold (the game paused, a replay): every effect
	 *  stops at once and nothing new plays. */
	void Tick(float Dt, APlayerController* PC, bool bHold);
	void StopAll();
	/** Effects sent to the pad / logged only, since the start (the tests read them). */
	int32 NumSent = 0, NumLogged = 0;

private:
	struct FFx
	{
		float Low = 0.f, High = 0.f, Dur = 0.1f, T = 0.f, Delay = 0.f;
		bool bFade = false;
	};
	TArray<FFx> Active;
	bool bHeld = false;
	// the two motors' dynamic force feedback on the controller (handles; 0 none yet) and whether each is running
	uint64 HLow = 0, HHigh = 0;
	bool bLowOn = false, bHighOn = false;
	TWeakObjectPtr<APlayerController> OnPC;
	void Send(APlayerController* PC, float Low, float High);
};
