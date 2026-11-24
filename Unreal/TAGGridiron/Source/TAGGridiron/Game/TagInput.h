// Per-device input for local multiplayer. Every pad and the keyboard reach the one player controller (all devices are
// mapped to the primary platform user, Config/Windows/WindowsInput.ini), so the controller cannot tell two players
// apart. FTagInputHub sits in front of Slate (an input pre-processor) and keeps each device's own buttons and sticks;
// FTagReader reads one player's device(s) the way the game reads the controller (down / just pressed / analog), or
// the controller itself in a one-player game, so the same control code serves both.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"

class APlayerController;

namespace TagInput
{
	/** A thumbstick as the game reads it: a scaled radial dead zone. The stick's length (not each axis on its own) is
	 *  measured against the dead zone and rescaled from it to the outer edge, (|v| - Dz) / (Outer - Dz) clamped to 1, and
	 *  its direction is kept as it is. A per-axis dead zone (the engine's, 0.25 on each axis) snapped every push within
	 *  14.5 deg of up / down / left / right onto it, and gave a full diagonal only 86 % of a straight push's speed.
	 *  Dz < 0 / Outer < 0: the project's values (GameUserSettings [TAG.Settings] StickDeadZone / StickOuter, 0.15 / 0.95;
	 *  -TagStickDZ= / -TagStickOuter= override them). -TagAxialStick: the old per-axis reading, to compare. */
	FVector2D Stick(const FVector2D& Raw, float Dz = -1.f, float Outer = -1.f);
	/** The dead zone and outer edge Stick uses by default. */
	float StickDeadZone();
	float StickOuter();
	bool IsStickAxis(const FKey& K);
}

/** One input device as the hub knows it: a pad (its gamepad keys) or the keyboard (every other key). A pad seen twice
 *  (an Xbox pad comes through XInput and GameInput both) is two devices: the lobby marks the second a twin. */
struct FTagDevice
{
	FInputDeviceId Id;
	bool bPad = false;
	TSet<FKey> Down;
	TMap<FKey, uint64> PressGen, ReleaseGen;
	TMap<FKey, double> PressTime;
	TMap<FKey, float> Axis;
	double LastInput = 0.0;
	/** What the engine calls it ("XInputController", "XboxOne", "Hid"...): the lobby's name for it. */
	FString Hardware;
	/** This pad is another pad seen again through a second interface (an Xbox pad: XInput and GameInput): its buttons are
	 *  kept here but never passed on to the menus and the game, which would take every press twice. -1: its own pad. */
	int32 TwinOf = -1;
	/** Keys whose press was swallowed (a second copy): their release is swallowed too. */
	TSet<FKey> Swallowed;
	bool IsSony() const { return bPad && Hardware.Contains(TEXT("Hid")); }
	/** The interface it came through ("XInputInterface", "GameInput"; empty if not known yet). */
	FString Interface() const { FString L; return Hardware.Split(TEXT("/"), &L, nullptr) ? L : Hardware; }
	FString Label() const;
};

class FTagInputHub : public IInputProcessor
{
public:
	/** The hub, registered with Slate the first time it is asked for. */
	static FTagInputHub& Get();
	static void Shutdown();

	// IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("TagInputHub"); }

	/** The device (index into Devices()) for an event's device and kind of key; -1 if never seen. */
	int32 Find(FInputDeviceId Id, bool bPad) const;
	const TArray<FTagDevice>& Devices() const { return Devs; }
	const FTagDevice* Dev(int32 Index) const { return Devs.IsValidIndex(Index) ? &Devs[Index] : nullptr; }
	bool Down(int32 D, const FKey& K) const;
	bool Pressed(int32 D, const FKey& K) const;
	bool Released(int32 D, const FKey& K) const;
	/** An analog key's value (a stick's axis read with both of its axes: TagInput::Stick's radial dead zone). */
	float Axis(int32 D, const FKey& K) const;
	/** Device D's left (or right) stick, raw from the device through TagInput::Stick. */
	FVector2D Stick(int32 D, bool bLeft) const;
	/** The devices that pressed a button since the last frame ended. */
	TArray<int32> PressedThisFrame() const;
	/** When device D last pressed K (seconds, platform time; < 0 never). */
	double PressTime(int32 D, const FKey& K) const;
	/** Called once a frame, after everything has read it (the director's tick): "just pressed" moves on. */
	void EndFrame() { ++Gen; }
	/** A menu that belongs to some players (a play call): the other devices' buttons go nowhere - not to the menu, not to
	 *  the game - except the pause keys. Empty: everyone's. */
	void SetMenuOwners(const TArray<int32>& Owners) { MenuOwners = Owners; }
	/** Synthetic input (automation): a press / release of K on device D, as if it came from the device. */
	void Inject(int32 D, const FKey& K, bool bDown);
	/** Automation: a fake device (pad or keyboard) the tests drive with Inject. */
	int32 AddFake(bool bPad, const FString& Name);
	void SetAxis(int32 D, const FKey& K, float V);

	/** How many second copies were swallowed (the -TagInputTest check). */
	int32 SwallowedCount() const { return NumSwallowed; }

private:
	int32 Touch(FInputDeviceId Id, bool bPad);
	bool Filtered(int32 D, const FKey& K) const;
	/** A press of K on pad D that is a second copy of one another interface's pad just sent: swallow it. */
	bool SecondCopy(int32 D, const FKey& K);
	TArray<FTagDevice> Devs;
	TArray<int32> MenuOwners;
	uint64 Gen = 1;
	/** Pairs of pads (lo << 32 | hi) seen pressing together, and how often. */
	TMap<uint64, int32> PairHits;
	/** A twin's presses waiting for its pad's own (none: it is a pad of its own after all). */
	struct FPend { int32 D; FKey K; double T; uint64 Gen; };
	TArray<FPend> Pending;
	int32 NumSwallowed = 0;
};

/** One player's controls: the device(s) he plays on (multiplayer) or the player controller (one player). */
struct FTagReader
{
	APlayerController* PC = nullptr;
	int32 Dev = -1;
	bool bPad = false;
	FTagReader() = default;
	explicit FTagReader(APlayerController* InPC) : PC(InPC) {}
	FTagReader(int32 InDev, bool bInPad) : Dev(InDev), bPad(bInPad) {}
	bool Valid() const { return PC != nullptr || Dev >= 0; }
	/** (reads like the controller it stands in for: `R->IsInputKeyDown(K)`, `if (R)`) */
	explicit operator bool() const { return Valid(); }
	const FTagReader* operator->() const { return this; }
	/** This player plays on a pad (his hints and glyphs are the pad's; R2 is his sprint). */
	bool IsPad() const;
	bool IsInputKeyDown(const FKey& K) const;
	bool WasInputKeyJustPressed(const FKey& K) const;
	bool WasInputKeyJustReleased(const FKey& K) const;
	/** A stick's axis comes from the whole stick (Stick: the radial dead zone), the controller's raw value or the hub's. */
	float GetInputAnalogKeyState(const FKey& K) const;
	/** The left (or right) stick, both axes, through TagInput::Stick. */
	FVector2D Stick(bool bLeft) const;
};
