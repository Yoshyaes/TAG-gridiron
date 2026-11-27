#include "Game/TagInput.h"
#include "Game/TagBindings.h"

#include "Framework/Application/NavigationConfig.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GenericPlatform/InputDeviceRegistry.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

namespace
{
	TSharedPtr<FTagInputHub> GHub;
	bool GHubRegistered = false;

	bool IsLeftStick(const FKey& K) { return K == EKeys::Gamepad_LeftX || K == EKeys::Gamepad_LeftY; }
	bool IsStickX(const FKey& K) { return K == EKeys::Gamepad_LeftX || K == EKeys::Gamepad_RightX; }

	/** The stick's dead zone and outer edge (TagInput::Stick), read once. */
	struct FStickCfg
	{
		float Dz = 0.15f, Outer = 0.95f;
		bool bAxial = false;
		FStickCfg()
		{
			if (GConfig)
			{
				GConfig->GetFloat(TEXT("TAG.Settings"), TEXT("StickDeadZone"), Dz, GGameUserSettingsIni);
				GConfig->GetFloat(TEXT("TAG.Settings"), TEXT("StickOuter"), Outer, GGameUserSettingsIni);
			}
			const TCHAR* Cmd = FCommandLine::Get();
			FParse::Value(Cmd, TEXT("TagStickDZ="), Dz);
			FParse::Value(Cmd, TEXT("TagStickOuter="), Outer);
			bAxial = FParse::Param(Cmd, TEXT("TagAxialStick"));
			Dz = FMath::Clamp(Dz, 0.f, 0.6f);
			Outer = FMath::Clamp(Outer, Dz + 0.1f, 1.f);
			UE_LOG(LogTemp, Display, TEXT("TAG input stick: %s dead zone %.2f, full at %.2f"), bAxial ? TEXT("per-axis (-TagAxialStick)") : TEXT("radial"), bAxial ? 0.25f : Dz, bAxial ? 1.f : Outer);
		}
	};
	const FStickCfg& StickCfg()
	{
		static const FStickCfg C;
		return C;
	}

	/** How close together two interfaces' copies of one press come (a frame or two apart at worst). */
	constexpr double TwinWindow = 0.08;
	/** Two presses are one press's two copies: within TwinWindow, or within two of the hub's frames (a slow frame - the
	 *  menus streaming in at 15-25 fps - put a frame's copies 130 ms apart, and one press moved the menu twice), but
	 *  never more than a quarter second apart (two people pressing the same button). */
	bool CloseCopies(double Dt, uint64 GenGap) { return Dt <= TwinWindow || (GenGap <= 2 && Dt <= 0.25); }

	/** Menu navigation from keys only. Slate's default also navigates from the stick's analog value, while the platform
	 *  sends the same push as a Gamepad_LeftStick_* key: the menus took one push of the stick twice (a key and a
	 *  navigation) and skipped a row. The stick's keys navigate instead, once a push (and repeat as a held key does). */
	class FTagNavigationConfig : public FNavigationConfig
	{
	public:
		FTagNavigationConfig()
		{
			bAnalogNavigation = false;
			KeyEventRules.Emplace(EKeys::Gamepad_LeftStick_Left, EUINavigation::Left);
			KeyEventRules.Emplace(EKeys::Gamepad_LeftStick_Right, EUINavigation::Right);
			KeyEventRules.Emplace(EKeys::Gamepad_LeftStick_Up, EUINavigation::Up);
			KeyEventRules.Emplace(EKeys::Gamepad_LeftStick_Down, EUINavigation::Down);
		}
	};
}

float TagInput::StickDeadZone() { return StickCfg().Dz; }
float TagInput::StickOuter() { return StickCfg().Outer; }

bool TagInput::IsStickAxis(const FKey& K)
{
	return K == EKeys::Gamepad_LeftX || K == EKeys::Gamepad_LeftY || K == EKeys::Gamepad_RightX || K == EKeys::Gamepad_RightY;
}

FVector2D TagInput::Stick(const FVector2D& Raw, float Dz, float Outer)
{
	const FStickCfg& C = StickCfg();
	if (C.bAxial && Dz < 0.f)
	{
		// the old reading: each axis on its own against 0.25, remapped from there (Config/DefaultInput.ini's dead zone)
		auto Ax = [](float V) { const float A = FMath::Abs(V); return A < 0.25f ? 0.f : FMath::Sign(V) * FMath::Min(1.f, (A - 0.25f) / 0.75f); };
		return FVector2D(Ax(static_cast<float>(Raw.X)), Ax(static_cast<float>(Raw.Y)));
	}
	if (Dz < 0.f) Dz = C.Dz;
	if (Outer < 0.f) Outer = C.Outer;
	const double L = Raw.Size();
	if (L <= Dz || L < UE_KINDA_SMALL_NUMBER) return FVector2D::ZeroVector;
	const double M = FMath::Min(1.0, (L - Dz) / FMath::Max(0.01, static_cast<double>(Outer - Dz)));
	return Raw * (M / L);
}

FString FTagDevice::Label() const
{
	if (!bPad) return TEXT("Keyboard");
	if (IsSony()) return TEXT("PlayStation controller");
	if (Hardware.Contains(TEXT("Xbox")) || Hardware.Contains(TEXT("XInput"))) return TEXT("Xbox controller");
	return TEXT("Controller");
}

FTagInputHub& FTagInputHub::Get()
{
	if (!GHub.IsValid()) GHub = MakeShared<FTagInputHub>();
	if (!GHubRegistered && FSlateApplication::IsInitialized())
	{
		// first in line: it sees every key before a menu or the game takes it
		FSlateApplication::Get().RegisterInputPreProcessor(GHub, 0);
		if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoInputDedupe"))) FSlateApplication::Get().SetNavigationConfig(MakeShared<FTagNavigationConfig>());
		GHubRegistered = true;
	}
	return *GHub;
}

void FTagInputHub::Shutdown()
{
	if (GHubRegistered && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(GHub);
	GHubRegistered = false;
}

int32 FTagInputHub::Find(FInputDeviceId Id, bool bPad) const
{
	return Devs.IndexOfByPredicate([&](const FTagDevice& D) { return D.Id == Id && D.bPad == bPad; });
}

int32 FTagInputHub::Touch(FInputDeviceId Id, bool bPad)
{
	int32 I = Find(Id, bPad);
	if (I >= 0)
	{
		// (a pad's interface can be registered after its first event: look again until it is known)
		if (bPad && Devs[I].Hardware.IsEmpty() && Id.GetId() < 9000)
			if (const TOptional<FInputDeviceDescriptor> Desc = FInputDeviceRegistry::FindDescriptor(Id))
				Devs[I].Hardware = Desc->InputDeviceName.ToString() + TEXT("/") + Desc->HardwareDeviceIdentifier.ToString();
		return I;
	}
	FTagDevice D;
	D.Id = Id;
	D.bPad = bPad;
	if (bPad)
		if (const TOptional<FInputDeviceDescriptor> Desc = FInputDeviceRegistry::FindDescriptor(Id))
			D.Hardware = Desc->InputDeviceName.ToString() + TEXT("/") + Desc->HardwareDeviceIdentifier.ToString();
	I = Devs.Add(D);
	UE_LOG(LogTemp, Display, TEXT("TAG input device %d: id %d, %s (%s)"), I, Id.GetId(), *Devs[I].Label(), *Devs[I].Hardware);
	return I;
}

bool FTagInputHub::Filtered(int32 D, const FKey& K) const
{
	if (MenuOwners.Num() == 0 || MenuOwners.Contains(D)) return false;
	if (K == EKeys::Escape || K == EKeys::Gamepad_Special_Right) return false; // (anyone may pause)
	return true;
}

bool FTagInputHub::SecondCopy(int32 D, const FKey& K)
{
	FTagDevice& Dv = Devs[D];
	if (!Dv.bPad || Dv.Id.GetId() >= 9000) return false; // (the automation's made-up devices are all their own)
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoInputDedupe")); // (the old way, to compare)
	if (bOff) return false;
	const double Now = FPlatformTime::Seconds();
	if (Dv.TwinOf >= 0)
	{
		// its pad's own copy may still be on the way: if it never comes, this is a pad of its own (Tick)
		const double* T = Devs[Dv.TwinOf].PressTime.Find(K);
		const uint64* G = Devs[Dv.TwinOf].PressGen.Find(K);
		if (!T || !G || !CloseCopies(Now - *T, Gen - *G)) Pending.Add({D, K, Now, Gen});
		++NumSwallowed;
		return true;
	}
	for (int32 o = 0; o < Devs.Num(); ++o)
	{
		const FTagDevice& O = Devs[o];
		if (o == D || !O.bPad || O.TwinOf >= 0 || O.Id.GetId() >= 9000) continue;
		const FString A = Dv.Interface(), B = O.Interface();
		if (!A.IsEmpty() && A == B) continue; // (two pads on one interface are two pads)
		const double* T = O.PressTime.Find(K);
		const uint64* G = O.PressGen.Find(K);
		if (!T || !G || !CloseCopies(Now - *T, Gen - *G)) continue;
		// the same button a moment ago on another interface's pad: the same press, a second time
		++NumSwallowed;
		const uint64 Pair = (static_cast<uint64>(FMath::Min(D, o)) << 32) | static_cast<uint64>(FMath::Max(D, o));
		if (++PairHits.FindOrAdd(Pair) >= 2)
		{
			Dv.TwinOf = o;
			UE_LOG(LogTemp, Display, TEXT("TAG input device %d (%s) is device %d (%s) again: its buttons go to the hub only"), D, *Dv.Hardware, o, *O.Hardware);
		}
		return true;
	}
	return false;
}

void FTagInputHub::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	const double Now = FPlatformTime::Seconds();
	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		const FPend P = Pending[i];
		if (!Devs.IsValidIndex(P.D) || Devs[P.D].TwinOf < 0) { Pending.RemoveAt(i); continue; }
		FTagDevice& Dv = Devs[P.D];
		const double* T = Devs[Dv.TwinOf].PressTime.Find(P.K);
		const uint64* G = Devs[Dv.TwinOf].PressGen.Find(P.K);
		if (T && G && (FMath::Abs(*T - P.T) <= TwinWindow * 2 || CloseCopies(FMath::Abs(*T - P.T), *G > P.Gen ? *G - P.Gen : P.Gen - *G)))
		{
			Pending.RemoveAt(i);
			continue;
		}
		if (Now - P.T < 0.25 || Gen - P.Gen < 3) continue; // (its pad's copy has had a quarter second and three frames)
		// a press its "pad" never made: a second pad after all
		UE_LOG(LogTemp, Display, TEXT("TAG input device %d pressed %s alone: a pad of its own, not device %d again"), P.D, *P.K.ToString(), Dv.TwinOf);
		PairHits.Remove((static_cast<uint64>(FMath::Min(P.D, Dv.TwinOf)) << 32) | static_cast<uint64>(FMath::Max(P.D, Dv.TwinOf)));
		Dv.TwinOf = -1;
		Pending.RemoveAt(i);
	}
}

bool FTagInputHub::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (K.IsMouseButton()) return false;
	const int32 D = Touch(E.GetInputDeviceId(), K.IsGamepadKey());
	bool bSwallow = false;
	if (!E.IsRepeat() && !Devs[D].Down.Contains(K))
	{
		bSwallow = SecondCopy(D, K);
		FTagDevice& Dv = Devs[D];
		Dv.Down.Add(K);
		Dv.PressGen.Add(K, Gen);
		Dv.PressTime.Add(K, FPlatformTime::Seconds());
		Dv.LastInput = FPlatformTime::Seconds();
		if (bSwallow) Dv.Swallowed.Add(K);
	}
	else bSwallow = Devs[D].TwinOf >= 0 || Devs[D].Swallowed.Contains(K); // (a second copy's repeats)
	// a second copy of a press is kept here (a player's own reader still sees his pad) but goes no further
	return bSwallow || Filtered(D, K);
}

bool FTagInputHub::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& E)
{
	const FKey K = E.GetKey();
	if (K.IsMouseButton()) return false;
	const int32 D = Touch(E.GetInputDeviceId(), K.IsGamepadKey());
	FTagDevice& Dv = Devs[D];
	if (Dv.Down.Remove(K) > 0) Dv.ReleaseGen.Add(K, Gen);
	// a release always goes through (nothing is left held down behind a menu), but a swallowed press's release is
	// swallowed with it
	return Dv.Swallowed.Remove(K) > 0;
}

bool FTagInputHub::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& E)
{
	const FKey K = E.GetKey();
	const int32 D = Touch(E.GetInputDeviceId(), true);
	FTagDevice& Dv = Devs[D];
	Dv.Axis.Add(K, E.GetAnalogValue());
	if (FMath::Abs(E.GetAnalogValue()) > 0.5f) Dv.LastInput = FPlatformTime::Seconds();
	return Dv.TwinOf >= 0 || Filtered(D, K); // (a twin's sticks are its pad's: one copy is enough)
}

bool FTagInputHub::Down(int32 D, const FKey& K) const { return Devs.IsValidIndex(D) && Devs[D].Down.Contains(K); }

bool FTagInputHub::Pressed(int32 D, const FKey& K) const
{
	if (!Devs.IsValidIndex(D)) return false;
	const uint64* G = Devs[D].PressGen.Find(K);
	return G && *G == Gen;
}

bool FTagInputHub::Released(int32 D, const FKey& K) const
{
	if (!Devs.IsValidIndex(D)) return false;
	const uint64* G = Devs[D].ReleaseGen.Find(K);
	return G && *G == Gen;
}

float FTagInputHub::Axis(int32 D, const FKey& K) const
{
	if (!Devs.IsValidIndex(D)) return 0.f;
	if (TagInput::IsStickAxis(K))
	{
		// the whole stick through the radial dead zone (an axis read on its own snapped diagonals onto the axes)
		const FVector2D S = Stick(D, IsLeftStick(K));
		return static_cast<float>(IsStickX(K) ? S.X : S.Y);
	}
	const float* V = Devs[D].Axis.Find(K);
	return V ? *V : 0.f;
}

FVector2D FTagInputHub::Stick(int32 D, bool bLeft) const
{
	if (!Devs.IsValidIndex(D)) return FVector2D::ZeroVector;
	const TMap<FKey, float>& A = Devs[D].Axis;
	const float* X = A.Find(bLeft ? EKeys::Gamepad_LeftX : EKeys::Gamepad_RightX);
	const float* Y = A.Find(bLeft ? EKeys::Gamepad_LeftY : EKeys::Gamepad_RightY);
	return TagInput::Stick(FVector2D(X ? *X : 0.f, Y ? *Y : 0.f));
}

TArray<int32> FTagInputHub::PressedThisFrame() const
{
	TArray<int32> Out;
	for (int32 i = 0; i < Devs.Num(); ++i)
		for (const TPair<FKey, uint64>& P : Devs[i].PressGen)
			if (P.Value == Gen) { Out.Add(i); break; }
	return Out;
}

double FTagInputHub::PressTime(int32 D, const FKey& K) const
{
	if (!Devs.IsValidIndex(D)) return -1.0;
	const double* T = Devs[D].PressTime.Find(K);
	return T ? *T : -1.0;
}

int32 FTagInputHub::AddFake(bool bPad, const FString& Name)
{
	FTagDevice D;
	D.Id = FInputDeviceId::CreateFromInternalId(9000 + Devs.Num());
	D.bPad = bPad;
	D.Hardware = Name;
	return Devs.Add(D);
}

void FTagInputHub::Inject(int32 D, const FKey& K, bool bDown)
{
	if (!Devs.IsValidIndex(D)) return;
	FTagDevice& Dv = Devs[D];
	if (bDown && !Dv.Down.Contains(K))
	{
		Dv.Down.Add(K);
		Dv.PressGen.Add(K, Gen);
		Dv.PressTime.Add(K, FPlatformTime::Seconds());
		Dv.LastInput = FPlatformTime::Seconds();
	}
	else if (!bDown && Dv.Down.Remove(K) > 0) Dv.ReleaseGen.Add(K, Gen);
}

void FTagInputHub::SetAxis(int32 D, const FKey& K, float V)
{
	if (Devs.IsValidIndex(D)) Devs[D].Axis.Add(K, V);
}

// ------------------------------------------------------------------ the reader
bool FTagReader::IsPad() const
{
	return bPad; // (the one-player reader is given the device he used last)
}

bool FTagReader::IsInputKeyDown(const FKey& InK) const
{
	const FKey K = TagBind::Phys(InK); // (the button the player put this one's action on: Settings > Controls)
	if (PC) return PC->IsInputKeyDown(K);
	return Dev >= 0 && K.IsGamepadKey() == bPad && FTagInputHub::Get().Down(Dev, K);
}

bool FTagReader::WasInputKeyJustPressed(const FKey& InK) const
{
	const FKey K = TagBind::Phys(InK); // (the button the player put this one's action on: Settings > Controls)
	if (PC) return PC->WasInputKeyJustPressed(K);
	return Dev >= 0 && K.IsGamepadKey() == bPad && FTagInputHub::Get().Pressed(Dev, K);
}

bool FTagReader::WasInputKeyJustReleased(const FKey& InK) const
{
	const FKey K = TagBind::Phys(InK); // (the button the player put this one's action on: Settings > Controls)
	if (PC) return PC->WasInputKeyJustReleased(K);
	return Dev >= 0 && K.IsGamepadKey() == bPad && FTagInputHub::Get().Released(Dev, K);
}

float FTagReader::GetInputAnalogKeyState(const FKey& K) const
{
	if (TagInput::IsStickAxis(K))
	{
		const FVector2D S = Stick(IsLeftStick(K));
		return static_cast<float>(IsStickX(K) ? S.X : S.Y);
	}
	if (PC) return PC->GetInputAnalogKeyState(K);
	return Dev >= 0 && bPad ? FTagInputHub::Get().Axis(Dev, K) : 0.f;
}

FVector2D FTagReader::Stick(bool bLeft) const
{
	if (PC)
	{
		// the controller's raw values (its own value has Config/DefaultInput.ini's per-axis dead zone in it, kept there for
		// the few reads that go to the controller directly: which device the player is on, TrackInputDevice)
		const UPlayerInput* In = PC->PlayerInput;
		if (!In) return FVector2D::ZeroVector;
		const FVector2D Raw(In->GetRawKeyValue(bLeft ? EKeys::Gamepad_LeftX : EKeys::Gamepad_RightX), In->GetRawKeyValue(bLeft ? EKeys::Gamepad_LeftY : EKeys::Gamepad_RightY));
		return TagInput::Stick(Raw);
	}
	return Dev >= 0 && bPad ? FTagInputHub::Get().Stick(Dev, bLeft) : FVector2D::ZeroVector;
}
