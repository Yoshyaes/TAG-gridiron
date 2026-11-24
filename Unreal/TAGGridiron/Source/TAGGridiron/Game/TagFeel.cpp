// The feel options, the Responsive steering law and the camera's trauma shake (Game/TagFeel.h).
#include "Game/TagFeel.h"

#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include <algorithm>
#include <cmath>

namespace
{
	const TCHAR* const kFeelSection = TEXT("TAG.Settings");

	/** One saved on / off setting (read once, then kept). */
	struct FFeelSetting
	{
		const TCHAR* Key;
		bool bDefault;
		bool bRead = false;
		bool bValue = false;
		bool Get()
		{
			if (!bRead)
			{
				bRead = true;
				bValue = bDefault;
				if (GConfig) GConfig->GetBool(kFeelSection, Key, bValue, GGameUserSettingsIni);
			}
			return bValue;
		}
		void Set(bool b)
		{
			bRead = true;
			bValue = b;
			if (GConfig && !FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"))) // (the scripted walk through the menus keeps nothing)
			{
				GConfig->SetBool(kFeelSection, Key, bValue, GGameUserSettingsIni);
				GConfig->Flush(false, GGameUserSettingsIni);
			}
		}
	};
	FFeelSetting GFeelResponsive{TEXT("MovementResponsive"), false};
	FFeelSetting GFeelHitFx{TEXT("HitEffects"), true};
	FFeelSetting GFeelSwitchAssist{TEXT("SwitchAssist"), true};

	bool FeelFlag(const TCHAR* Name) { return FParse::Param(FCommandLine::Get(), Name); }

	/** The self-check's overrides (-1: the saved setting). */
	int32 GFeelTestResponsive = -1, GFeelTestHitFx = -1, GFeelTestSwitchAssist = -1;

	/** -TagMovement=responsive|authentic (-1: not given). */
	int32 FeelMovementFlag()
	{
		static const int32 V = []
		{
			FString M;
			if (!FParse::Value(FCommandLine::Get(), TEXT("TagMovement="), M)) return -1;
			return M.Equals(TEXT("responsive"), ESearchCase::IgnoreCase) ? 1 : 0;
		}();
		return V;
	}
}

bool TagFeel::Responsive()
{
	if (GFeelTestResponsive >= 0) return GFeelTestResponsive > 0;
	if (FeelMovementFlag() >= 0) return FeelMovementFlag() > 0;
	return GFeelResponsive.Get();
}

void TagFeel::ToggleMovement() { GFeelResponsive.Set(!GFeelResponsive.Get()); }
const TCHAR* TagFeel::MovementName() { return Responsive() ? TEXT("Responsive") : TEXT("Authentic"); }
bool TagFeel::HitFx() { return GFeelTestHitFx >= 0 ? GFeelTestHitFx > 0 : GFeelHitFx.Get(); }
void TagFeel::ToggleHitFx() { GFeelHitFx.Set(!GFeelHitFx.Get()); }
bool TagFeel::SwitchAssist() { return GFeelTestSwitchAssist >= 0 ? GFeelTestSwitchAssist > 0 : GFeelSwitchAssist.Get(); }
void TagFeel::ToggleSwitchAssist() { GFeelSwitchAssist.Set(!GFeelSwitchAssist.Get()); }

void TagFeel::SetTestOverrides(int32 InResponsive, int32 InHitFx, int32 InSwitchAssist)
{
	GFeelTestResponsive = InResponsive;
	GFeelTestHitFx = InHitFx;
	GFeelTestSwitchAssist = InSwitchAssist;
}

bool TagFeel::ThrowOnRelease()
{
	static const bool b = FeelFlag(TEXT("TagThrowOnRelease"));
	return b;
}

bool TagFeel::OldSwitch()
{
	static const bool b = FeelFlag(TEXT("TagSwitchOld"));
	return b;
}

bool TagFeel::HitStopOn()
{
	static const bool bOff = FeelFlag(TEXT("TagNoHitStop"));
	return !bOff && HitFx();
}

bool TagFeel::OldShake()
{
	static const bool b = FeelFlag(TEXT("TagOldShake"));
	return b;
}

const FTagResponsiveTune& TagFeel::ResponsiveTune()
{
	static FTagResponsiveTune T;
	static bool bRead = false;
	if (!bRead)
	{
		bRead = true;
		FString Spec;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagResponsive="), Spec, false))
		{
			TArray<FString> Parts;
			Spec.ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts)
			{
				FString K, V;
				if (!P.Split(TEXT("="), &K, &V)) continue;
				const double D = FCString::Atod(*V);
				bool bKnown = true;
				if (K == TEXT("LatMul")) T.LatMul = D;
				else if (K == TEXT("BrakeMul")) T.BrakeMul = D;
				else if (K == TEXT("LatAgiAtSpeed")) T.LatAgiAtSpeed = D;
				else if (K == TEXT("GripMul")) T.GripMul = D;
				else if (K == TEXT("GripAgi")) T.GripAgi = D;
				else bKnown = false;
				UE_LOG(LogTemp, Display, TEXT("TAG responsive %s = %s (%s)"), *K, *V, bKnown ? TEXT("set") : TEXT("unknown"));
			}
		}
	}
	return T;
}

tag::SteerBoost TagFeel::ToSim(const FTagResponsiveTune& T)
{
	tag::SteerBoost B;
	B.on = true;
	B.latMul = T.LatMul;
	B.brakeMul = T.BrakeMul;
	B.latAgiAtSpeed = T.LatAgiAtSpeed;
	B.gripMul = T.GripMul;
	B.gripAgi = T.GripAgi;
	return B;
}

tag::V2 TagFeel::Steer(const tag::Ent* E, const tag::V2& Vel, const tag::V2& Want, double Dt, const FTagResponsiveTune& T, bool bNeutral)
{
	// the sim's own law (PlaySim::steerLaw: one copy, in the sim), with Responsive's numbers unless bNeutral
	const tag::SteerBoost B = ToSim(T);
	return tag::PlaySim::steerLaw(E, Vel, Want, Dt, bNeutral ? nullptr : &B);
}

double TagFeel::TurnMs(double Speed, double Accel, double Agility, bool bStop, bool bHalf, const FTagResponsiveTune& T, bool bNeutral, const tag::PlaySim* Sim)
{
	tag::Player Pl;
	Pl.r.speed = Speed;
	Pl.r.accel = Accel;
	Pl.r.agility = Agility;
	Pl.weightLb = 210;
	const tag::Ent E(&Pl, tag::Side::Off, "RB", tag::V2{0, 0});
	constexpr double Dt = 1.0 / 60;
	tag::V2 V{E.vmax * (bHalf ? 0.5 : 1.0), 0};
	const tag::V2 Want = bStop ? tag::V2{-E.vmax, 0} : tag::V2{0, E.vmax};
	for (int32 i = 1; i <= 600; ++i)
	{
		V = Sim ? Sim->steerVel(&E, V, Want, Dt) : Steer(&E, V, Want, Dt, T, bNeutral);
		const double Sp = tag::len(V);
		if (Sp > E.vmax) V = tag::scale(V, E.vmax / Sp); // (PlaySim::move's cap)
		if (bStop ? V.x <= 0 : std::atan2(V.y, V.x) >= tag::kPi / 4) return i * Dt * 1000.0;
	}
	return -1.0;
}

// ------------------------------------------------------------------ the camera's trauma
void FTagTrauma::Add(float Amount, bool bSway)
{
	if (Amount <= 0.f) return;
	const float Was = Trauma;
	Trauma = FMath::Clamp(Trauma + Amount, 0.f, 1.f);
	// (what is on now: the sway's share of it, by amount)
	Sway = FMath::Clamp((Was * Sway + Amount * (bSway ? 1.f : 0.f)) / FMath::Max(Was + Amount, 1e-4f), 0.f, 1.f);
}

void FTagTrauma::Tick(float RealDt, float FocalMm, FRotator& OutRot, FVector& OutPos)
{
	OutRot = FRotator::ZeroRotator;
	OutPos = FVector::ZeroVector;
	if (Trauma <= 1e-3f)
	{
		Trauma = 0.f;
		Sway = 0.f;
		return;
	}
	T += RealDt;
	const float Sh = Trauma * Trauma;
	// a hit's quick shake (12 Hz noise), a touchdown's slow sway (2.2 Hz); the angle per lens (35 mm: 0.9 deg at full
	// trauma; a 200 mm lens 0.16 deg - the same share of the picture)
	const float Freq = FMath::Lerp(12.f, 2.2f, Sway);
	const float MaxDeg = FMath::Clamp(0.9f * 35.f / FMath::Max(FocalMm, 10.f), 0.08f, 1.2f) * FMath::Lerp(1.f, 0.6f, Sway);
	const float X = static_cast<float>(FMath::Fmod(T * Freq, 4096.0));
	OutRot.Pitch = MaxDeg * Sh * FMath::PerlinNoise1D(X + 11.3f);
	OutRot.Yaw = MaxDeg * Sh * FMath::PerlinNoise1D(X + 37.7f);
	OutRot.Roll = 0.6f * MaxDeg * Sh * FMath::PerlinNoise1D(X + 71.1f);
	OutPos = FVector(FMath::PerlinNoise1D(X + 5.1f), FMath::PerlinNoise1D(X + 23.9f), FMath::PerlinNoise1D(X + 51.3f)) * (4.f * Sh);
	Trauma = FMath::Max(0.f, Trauma - RealDt * FMath::Lerp(1.5f, 0.55f, Sway));
}
