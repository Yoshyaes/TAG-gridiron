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
	bool IsSony() const { return bPad && Hardware.Contains(TEXT("Hid")); }
	FString Label() const;
};

class FTagInputHub : public IInputProcessor
{
public:
	/** The hub, registered with Slate the first time it is asked for. */
	static FTagInputHub& Get();
	static void Shutdown();

	// IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
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
	/** An analog key's value (sticks with the project's 0.25 dead zone, remapped as the controller does). */
	float Axis(int32 D, const FKey& K) const;
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

private:
	int32 Touch(FInputDeviceId Id, bool bPad);
	bool Filtered(int32 D, const FKey& K) const;
	TArray<FTagDevice> Devs;
	TArray<int32> MenuOwners;
	uint64 Gen = 1;
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
	float GetInputAnalogKeyState(const FKey& K) const;
};
