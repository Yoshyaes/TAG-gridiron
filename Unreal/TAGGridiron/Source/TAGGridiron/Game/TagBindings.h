// The player's button layout (Settings > Controls): what every button does in each part of the game, and which
// button the player has put it on. The game's input code asks for the buttons it was written with (CROSS stiff
// arms, SQUARE dives...): those are the logical buttons. TagBind keeps, for each context (ball carrier, passing,
// receiving, defense, the two pre-snaps, kicking) and for the pad and the keyboard, a map of logical button ->
// the physical one the player chose. Moving an action onto a button swaps it with whatever was there, so each map
// is a permutation and nothing is ever left without a button. FTagReader reads every button through the map of
// the context in force (a TagBind::FScope the director opens around each part's input code); outside any scope
// (menus, the play call) the buttons are what they say. Saved at once to GameUserSettings.ini [TAG.Controls].
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

enum class EPadGlyph : uint8;

namespace TagBind
{
	enum class ECtx : uint8 { Carrier, Passing, Receiving, Defense, PreOff, PreDef, Kicking, Count, None = 255 };
	constexpr int32 NumCtx = static_cast<int32>(ECtx::Count);

	/** One thing a button does in a context: on its default (logical) pad button and keyboard key (either may be
	 *  invalid: not on that device). */
	struct FAction
	{
		FString Name;
		FKey Pad;
		FKey Kbd;
		FString Note;
	};
	/** A control that is not a button and stays where it is (the sticks, pause, the camera): Where names it ("LS",
	 *  "RS", "MENU", "VIEW", "WASD", ...). */
	struct FFixed
	{
		FString Where;
		FString Name;
	};
	struct FCtxInfo
	{
		ECtx Ctx;
		FString Group;  // "OFFENSE"
		FString Name;   // "Ball Carrier"
		FString Desc;
	};
	const TArray<FCtxInfo>& Contexts();
	const FCtxInfo& Info(ECtx C);
	const TArray<FAction>& Actions(ECtx C);
	const TArray<FFixed>& Fixed(ECtx C, bool bPad);

	/** The fourteen pad buttons a player can put an action on (face, shoulders, triggers, stick clicks, D-pad). */
	const TArray<FKey>& PadButtons();
	/** A key the player may put an action on in context C (a keyboard key not kept for moving, the camera or pause). */
	bool CanBind(ECtx C, const FKey& K);

	/** The physical button for a logical one in context C (None: the logical one). */
	FKey Phys(ECtx C, const FKey& Logical);
	/** The logical button a physical one stands for in context C. */
	FKey Logical(ECtx C, const FKey& Physical);
	/** Put the action on logical button L onto physical button P (swapped with what was on P). Returns the logical
	 *  button whose action moved to make room (invalid: none). Saves. */
	FKey Assign(ECtx C, const FKey& L, const FKey& P);
	/** The context's pad (bPad) or keyboard map back to the defaults. Saves. */
	void Reset(ECtx C, bool bPad);
	void ResetAll();
	bool IsDefault(ECtx C, bool bPad);
	/** Read the saved layout (once, at startup; the first use loads it too). */
	void Load();

	/** The context the input code is reading in (FScope); None outside the game's controls. */
	ECtx Current();
	struct FScope
	{
		explicit FScope(ECtx C);
		~FScope();
		FScope(const FScope&) = delete;
		FScope& operator=(const FScope&) = delete;
	private:
		ECtx Prev;
	};
	/** Phys in the current context. */
	FKey Phys(const FKey& Logical);

	// ---- names and glyphs
	FKey PadKeyOf(EPadGlyph G);
	EPadGlyph GlyphOf(const FKey& PadKey);
	/** A glyph as the current context has it (the button the player put that logical button's action on). */
	EPadGlyph PhysGlyph(EPadGlyph G);
	EPadGlyph PhysGlyph(ECtx C, EPadGlyph G);
	/** A pad button's name: PlayStation ("CROSS", "R1", "L3") or Xbox ("A", "RB", "LS"). */
	FString PadName(const FKey& K, bool bSony);
	/** A keyboard key's name as the hints write it ("SPACE", "BKSP", "TAB", "1", "F"). */
	FString KeyLabel(const FKey& K);
	/** The key a hint's name stands for (KeyLabel's inverse; invalid if none). */
	FKey KeyFromLabel(const FString& Label);
	/** The keyboard key a pre-snap panel glyph stands for when no key is written (SPACE for CROSS, ...). */
	FString DefaultKbdLabel(EPadGlyph G);
	/** A key named in a hint, renamed for the current context's layout ("F" -> "Z" if the player moved it). */
	FString PhysKeyLabel(const FString& Label);
	FString PhysKeyLabel(ECtx C, const FString& Label);
	/** A hint written with PlayStation button words (CROSS, L1...) or keyboard keys (bPad false: F, SPACE...): each
	 *  button renamed to the one the player put it on, in the current context. */
	FString TranslateHint(const FString& Hint, bool bPad);

	// ---- the controller drawn
	/** 0 the pad plugged in, 1 always Xbox, 2 always PlayStation. Saved. */
	int32 GlyphStyle();
	void SetGlyphStyle(int32 S);
	/** Whether to draw PlayStation buttons, given whether a Sony pad was found. */
	bool Sony(bool bSonyDetected);
}
