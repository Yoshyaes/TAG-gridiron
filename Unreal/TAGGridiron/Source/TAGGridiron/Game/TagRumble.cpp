#include "Game/TagRumble.h"

#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace
{
	/** An effect's shape at full strength: the low (heavy) motor, the high (light) one, how long, pulses and the gap
	 *  between them, and a fade over the whole length (else flat, let go over its last quarter). */
	struct FRumbleDef
	{
		float Low, High, Ms;
		int32 Pulses;
		float GapMs;
		bool bFade;
	};

	FRumbleDef DefOf(ETagRumble Fx)
	{
		switch (Fx)
		{
		case ETagRumble::Snap: return {0.f, 0.18f, 40.f, 1, 0.f, false};
		case ETagRumble::Contact: return {0.35f, 0.1f, 90.f, 1, 0.f, false};
		case ETagRumble::TackleGiven: return {0.55f, 0.25f, 120.f, 1, 0.f, false};
		case ETagRumble::TackleTaken: return {0.6f, 0.3f, 150.f, 1, 0.f, false};
		case ETagRumble::BigHitGiven: return {1.f, 0.7f, 220.f, 1, 0.f, false};
		case ETagRumble::BigHitTaken: return {0.9f, 0.8f, 250.f, 1, 0.f, false};
		case ETagRumble::SackGiven: return {0.85f, 0.55f, 200.f, 1, 0.f, false};
		case ETagRumble::SackTaken: return {0.8f, 0.6f, 200.f, 1, 0.f, false};
		case ETagRumble::Catch: return {0.f, 0.35f, 45.f, 1, 0.f, false};
		case ETagRumble::CatchTraffic: return {0.15f, 0.6f, 60.f, 1, 0.f, false};
		case ETagRumble::Fumble: return {0.7f, 0.5f, 90.f, 2, 70.f, false};
		case ETagRumble::KickStrike: return {0.1f, 0.7f, 50.f, 1, 0.f, false};
		case ETagRumble::Touchdown: return {0.45f, 0.12f, 700.f, 1, 0.f, true};
		case ETagRumble::Test: return {0.6f, 0.4f, 160.f, 1, 0.f, false};
		}
		return {0.f, 0.f, 0.f, 0, 0.f, false};
	}

	int32 GRumbleLevel = 2;
	bool GRumbleRead = false;
	const TCHAR* const kRumbleKey = TEXT("Rumble");
}

FTagRumble& FTagRumble::Get()
{
	static FTagRumble R;
	return R;
}

int32 FTagRumble::Level()
{
	if (!GRumbleRead && GConfig)
	{
		GRumbleRead = true;
		GConfig->GetInt(TEXT("TAG.Settings"), kRumbleKey, GRumbleLevel, GGameUserSettingsIni);
		GRumbleLevel = FMath::Clamp(GRumbleLevel, 0, 2);
	}
	return GRumbleLevel;
}

void FTagRumble::StepLevel()
{
	GRumbleLevel = (Level() + 2) % 3; // Normal -> Low -> Off -> Normal
	if (GConfig && !FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"))) // (the scripted walk through the menus keeps nothing)
	{
		GConfig->SetInt(TEXT("TAG.Settings"), kRumbleKey, GRumbleLevel, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	// what the new strength feels like (Off: nothing)
	Get().Play(ETagRumble::Test, 1.f, TEXT("the settings' sample"));
}

const TCHAR* FTagRumble::LevelName(int32 L)
{
	return L <= 0 ? TEXT("Off") : L == 1 ? TEXT("Low") : TEXT("Normal");
}

const TCHAR* FTagRumble::Name(ETagRumble Fx)
{
	switch (Fx)
	{
	case ETagRumble::Snap: return TEXT("snap");
	case ETagRumble::Contact: return TEXT("contact");
	case ETagRumble::TackleGiven: return TEXT("tackle_given");
	case ETagRumble::TackleTaken: return TEXT("tackle_taken");
	case ETagRumble::BigHitGiven: return TEXT("bighit_given");
	case ETagRumble::BigHitTaken: return TEXT("bighit_taken");
	case ETagRumble::SackGiven: return TEXT("sack_given");
	case ETagRumble::SackTaken: return TEXT("sack_taken");
	case ETagRumble::Catch: return TEXT("catch");
	case ETagRumble::CatchTraffic: return TEXT("catch_traffic");
	case ETagRumble::Fumble: return TEXT("fumble");
	case ETagRumble::KickStrike: return TEXT("kick");
	case ETagRumble::Touchdown: return TEXT("touchdown");
	case ETagRumble::Test: return TEXT("test");
	}
	return TEXT("?");
}

void FTagRumble::Play(ETagRumble Fx, float Scale, const FString& Why, const TCHAR* NotSent)
{
	const FRumbleDef D = DefOf(Fx);
	const int32 L = Level();
	const float Strength = FMath::Clamp(Scale, 0.f, 1.f) * (L >= 2 ? 1.f : L == 1 ? 0.5f : 0.f);
	const TCHAR* Skip = NotSent ? NotSent : L <= 0 ? TEXT("the setting is Off") : bHeld ? TEXT("paused / replay") : nullptr;
	const float TotalMs = D.Ms * D.Pulses + D.GapMs * (D.Pulses - 1);
	if (Skip)
	{
		++NumLogged;
		UE_LOG(LogTemp, Display, TEXT("TAG haptic %s %.0f ms %.2f (%s; not sent: %s)"), Name(Fx), TotalMs, Skip == NotSent ? Strength : 0.f, *Why, Skip);
		return;
	}
	++NumSent;
	UE_LOG(LogTemp, Display, TEXT("TAG haptic %s %.0f ms %.2f (%s)"), Name(Fx), TotalMs, Strength, *Why);
	for (int32 p = 0; p < D.Pulses; ++p)
	{
		FFx F;
		F.Low = D.Low * Strength;
		F.High = D.High * Strength;
		F.Dur = D.Ms / 1000.f;
		F.Delay = p * (D.Ms + D.GapMs) / 1000.f;
		F.bFade = D.bFade;
		Active.Add(F);
	}
}

void FTagRumble::StopAll()
{
	Active.Reset();
	if (APlayerController* PC = OnPC.Get()) Send(PC, 0.f, 0.f);
}

void FTagRumble::Tick(float Dt, APlayerController* PC, bool bHold)
{
	bHeld = bHold;
	if (PC != OnPC.Get())
	{
		// (a new controller: the old one's effects went with it)
		if (APlayerController* Old = OnPC.Get()) Send(Old, 0.f, 0.f);
		OnPC = PC;
		HLow = HHigh = 0;
		bLowOn = bHighOn = false;
	}
	if (bHold || Level() <= 0) Active.Reset();
	float Low = 0.f, High = 0.f;
	for (int32 i = Active.Num() - 1; i >= 0; --i)
	{
		FFx& F = Active[i];
		F.T += Dt;
		const float U = (F.T - F.Delay) / FMath::Max(0.001f, F.Dur);
		if (U >= 1.f) { Active.RemoveAtSwap(i); continue; }
		if (U < 0.f) continue; // (a later pulse, not yet)
		const float Env = F.bFade ? 1.f - U : U < 0.75f ? 1.f : (1.f - U) / 0.25f;
		Low = FMath::Max(Low, F.Low * Env);
		High = FMath::Max(High, F.High * Env);
	}
	if (PC) Send(PC, Low, High);
}

void FTagRumble::Send(APlayerController* PC, float Low, float High)
{
	// each motor one dynamic effect, restarted with its value every frame it runs (a short life, so a frame that does not
	// come - the director gone - cannot leave a motor on), stopped as it reaches nothing
	auto Motor = [PC](float V, bool bLeftLarge, uint64& Handle, bool& bRunning)
	{
		if (V > 0.005f)
		{
			Handle = PC->PlayDynamicForceFeedback(V, 0.25f, bLeftLarge, false, !bLeftLarge, false, EDynamicForceFeedbackAction::Start, Handle);
			bRunning = true;
		}
		else if (bRunning)
		{
			PC->PlayDynamicForceFeedback(0.f, 0.f, bLeftLarge, false, !bLeftLarge, false, EDynamicForceFeedbackAction::Stop, Handle);
			bRunning = false;
		}
	};
	Motor(Low, true, HLow, bLowOn);
	Motor(High, false, HHigh, bHighOn);
}
