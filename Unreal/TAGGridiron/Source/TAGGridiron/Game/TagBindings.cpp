#include "Game/TagBindings.h"

#include "UI/GridironOverlay.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace TagBind
{
namespace
{
	const TCHAR* Section = TEXT("TAG.Controls");

	/** Logical -> physical, the pad's and the keyboard's together (their keys never meet). Only moved keys are kept. */
	TMap<FKey, FKey> GMap[NumCtx];
	bool GLoaded = false;
	ECtx GCur = ECtx::None;
	int32 GStyle = 0;

	bool Valid(ECtx C) { return static_cast<int32>(C) < NumCtx; }
	int32 Ix(ECtx C) { return static_cast<int32>(C); }

	/** -TagControlsTest runs on a layout of its own: nothing read from or written to the player's. */
	bool NoSave()
	{
		static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagControlsTest")) || FParse::Param(FCommandLine::Get(), TEXT("TagUiShots"));
		return b;
	}

	void Save()
	{
		if (!GConfig || NoSave()) return;
		for (int32 c = 0; c < NumCtx; ++c)
		{
			FString V;
			for (const TPair<FKey, FKey>& P : GMap[c])
			{
				if (!V.IsEmpty()) V += TEXT(",");
				V += P.Key.GetFName().ToString() + TEXT(">") + P.Value.GetFName().ToString();
			}
			GConfig->SetString(Section, *FString::Printf(TEXT("Ctx%d"), c), *V, GGameUserSettingsIni);
		}
		GConfig->SetInt(Section, TEXT("GlyphStyle"), GStyle, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	void Ensure() { if (!GLoaded) Load(); }

	/** A map must stay a permutation: any key moved away has another moved onto it. A hand-edited ini that breaks
	 *  that is thrown away (the defaults are always playable). */
	bool IsPermutation(const TMap<FKey, FKey>& M)
	{
		TSet<FKey> From, To;
		for (const TPair<FKey, FKey>& P : M) { From.Add(P.Key); To.Add(P.Value); }
		return From.Num() == M.Num() && To.Num() == M.Num() && From.Includes(To) && To.Includes(From);
	}

	const FKey KNone;
}

// ------------------------------------------------------------------ what every button does
const TArray<FCtxInfo>& Contexts()
{
	static const TArray<FCtxInfo> C = {
		{ECtx::Carrier, TEXT("OFFENSE"), TEXT("Ball Carrier"), TEXT("With the ball in his hands: the runner's moves.")},
		{ECtx::Passing, TEXT("OFFENSE"), TEXT("Passing"), TEXT("The quarterback until the ball is gone: a button for each receiver.")},
		{ECtx::Receiving, TEXT("OFFENSE"), TEXT("Receiving"), TEXT("On offense without the ball: the catch.")},
		{ECtx::Defense, TEXT("DEFENSE"), TEXT("Defense"), TEXT("After the snap: tackles, the hit stick, switching and the pass rush.")},
		{ECtx::PreOff, TEXT("PRE-SNAP"), TEXT("Offense"), TEXT("At the line before the snap: hot routes, audibles, motion and protection.")},
		{ECtx::PreDef, TEXT("PRE-SNAP"), TEXT("Defense"), TEXT("Before the snap on defense: audibles, coverage, line and linebacker shifts.")},
		{ECtx::Kicking, TEXT("SPECIAL TEAMS"), TEXT("Kicking"), TEXT("Field goals, punts and kickoffs: the kick meter.")},
	};
	return C;
}

const FCtxInfo& Info(ECtx C)
{
	return Contexts()[FMath::Clamp(Ix(C), 0, NumCtx - 1)];
}

const TArray<FAction>& Actions(ECtx C)
{
	// (built on first use: the keys are the InputCore module's)
	static TArray<FAction> A[NumCtx];
	static bool bBuilt = false;
	if (!bBuilt)
	{
		bBuilt = true;
		using K = EKeys;
		A[Ix(ECtx::Carrier)] = {
			{TEXT("Sprint (hold)"), K::Gamepad_RightTrigger, KNone, TEXT("On a stamina bar; the keyboard always runs flat out")},
			{TEXT("Stiff arm"), K::Gamepad_FaceButton_Bottom, K::G, TEXT("")},
			{TEXT("Spin"), K::Gamepad_FaceButton_Right, K::R, TEXT("")},
			{TEXT("Hurdle"), K::Gamepad_FaceButton_Top, K::V, TEXT("")},
			{TEXT("Truck"), KNone, K::F, TEXT("")},
			{TEXT("Juke left"), KNone, K::Q, TEXT("")},
			{TEXT("Juke right"), KNone, K::E, TEXT("")},
			{TEXT("Juke"), KNone, K::SpaceBar, TEXT("")},
			{TEXT("Cover up the ball (hold)"), K::Gamepad_RightShoulder, K::X, TEXT("Far harder to strip; no moves while it is covered")},
			{TEXT("Switch player"), K::Gamepad_LeftShoulder, K::Tab, TEXT("")},
		};
		A[Ix(ECtx::Passing)] = {
			{TEXT("Throw to receiver 1"), K::Gamepad_FaceButton_Bottom, K::One, TEXT("Hold: bullet  -  tap: lob  -  tap twice: pump fake")},
			{TEXT("Throw to receiver 2"), K::Gamepad_FaceButton_Right, K::Two, TEXT("")},
			{TEXT("Throw to receiver 3"), K::Gamepad_FaceButton_Left, K::Three, TEXT("")},
			{TEXT("Throw to receiver 4"), K::Gamepad_FaceButton_Top, K::Four, TEXT("")},
			{TEXT("Throw to receiver 5"), K::Gamepad_RightShoulder, K::Five, TEXT("")},
			{TEXT("High pass (hold)"), K::Gamepad_LeftShoulder, K::LeftControl, TEXT("")},
			{TEXT("Low pass (hold)"), K::Gamepad_LeftTrigger, K::LeftAlt, TEXT("")},
			{TEXT("Lob (hold)"), KNone, K::LeftShift, TEXT("")},
			{TEXT("Throw it away"), K::Gamepad_RightThumbstick, K::T, TEXT("")},
			{TEXT("Pump fake"), KNone, K::P, TEXT("")},
			{TEXT("Sprint (hold)"), K::Gamepad_RightTrigger, KNone, TEXT("")},
			{TEXT("Juke (scrambling)"), KNone, K::SpaceBar, TEXT("")},
			{TEXT("Truck (scrambling)"), KNone, K::F, TEXT("")},
			{TEXT("Stiff arm (scrambling)"), KNone, K::G, TEXT("")},
			{TEXT("Juke left (scrambling)"), KNone, K::Q, TEXT("")},
			{TEXT("Juke right (scrambling)"), KNone, K::E, TEXT("")},
			{TEXT("Switch player"), KNone, K::Tab, TEXT("")},
		};
		A[Ix(ECtx::Receiving)] = {
			{TEXT("Aggressive catch (hold)"), K::Gamepad_FaceButton_Top, K::Y, TEXT("Held while the ball is in the air")},
			{TEXT("Sprint (hold)"), K::Gamepad_RightTrigger, KNone, TEXT("")},
			{TEXT("Juke"), KNone, K::SpaceBar, TEXT("")},
			{TEXT("Switch player"), K::Gamepad_LeftShoulder, K::Tab, TEXT("")},
		};
		A[Ix(ECtx::Defense)] = {
			{TEXT("Sprint  /  speed rush"), K::Gamepad_RightTrigger, KNone, TEXT("Engaged with a blocker it is the speed rush")},
			{TEXT("Dive tackle"), K::Gamepad_FaceButton_Left, K::SpaceBar, TEXT("")},
			{TEXT("Switch player"), K::Gamepad_FaceButton_Right, K::Tab, TEXT("To the man nearest the ball")},
			{TEXT("Switch player (2)"), K::Gamepad_LeftShoulder, KNone, TEXT("")},
			{TEXT("Strip the ball"), K::Gamepad_RightShoulder, K::X, TEXT("Goes for the ball: fewer tackles, far more fumbles")},
			{TEXT("Strafe (hold)"), K::Gamepad_LeftTrigger, K::LeftShift, TEXT("Square to the play, eyes on the ball")},
			{TEXT("Hit stick"), KNone, K::F, TEXT("")},
			{TEXT("Cut tackle"), KNone, K::V, TEXT("")},
			{TEXT("Finesse rush move"), KNone, K::J, TEXT("A / D a side: swim; S back: spin; else rip")},
			{TEXT("Power rush move"), KNone, K::K, TEXT("A / D a side: club; S back: long arm; else bull")},
		};
		A[Ix(ECtx::PreOff)] = {
			{TEXT("Snap  /  select"), K::Gamepad_FaceButton_Bottom, K::SpaceBar, TEXT("")},
			{TEXT("Motion  /  another man"), K::Gamepad_FaceButton_Right, K::BackSpace, TEXT("")},
			{TEXT("Audible  /  flip play"), K::Gamepad_FaceButton_Left, K::J, TEXT("")},
			{TEXT("Hot route"), K::Gamepad_FaceButton_Top, K::H, TEXT("")},
			{TEXT("Pass protection  /  block"), K::Gamepad_LeftShoulder, K::Tab, TEXT("")},
			{TEXT("Fake snap  /  post & wheel"), K::Gamepad_RightShoulder, K::N, TEXT("")},
			{TEXT("Show play art (hold)  /  back"), K::Gamepad_LeftTrigger, K::Q, TEXT("")},
			{TEXT("Look down the line (hold)"), K::Gamepad_RightTrigger, KNone, TEXT("With the left stick")},
			{TEXT("Hide the controls panel"), K::Gamepad_RightThumbstick, K::F1, TEXT("")},
			{TEXT("Hot route: deep  /  menus up"), K::Gamepad_DPad_Up, K::Up, TEXT("")},
			{TEXT("Hot route: short  /  menus down"), K::Gamepad_DPad_Down, K::Down, TEXT("")},
			{TEXT("Hot route: left  /  slide left"), K::Gamepad_DPad_Left, K::Left, TEXT("")},
			{TEXT("Hot route: right  /  slide right"), K::Gamepad_DPad_Right, K::Right, TEXT("")},
			{TEXT("Flip the run (one way)"), KNone, K::LeftBracket, TEXT("")},
			{TEXT("Flip the run (the other)"), KNone, K::RightBracket, TEXT("")},
			{TEXT("Pick receiver 1"), KNone, K::One, TEXT("Hot route / motion: the receiver's own key")},
			{TEXT("Pick receiver 2"), KNone, K::Two, TEXT("")},
			{TEXT("Pick receiver 3"), KNone, K::Three, TEXT("")},
			{TEXT("Pick receiver 4"), KNone, K::Four, TEXT("")},
			{TEXT("Pick receiver 5"), KNone, K::Five, TEXT("")},
		};
		A[Ix(ECtx::PreDef)] = {
			{TEXT("Defensive hot route  /  select"), K::Gamepad_FaceButton_Bottom, K::SpaceBar, TEXT("")},
			{TEXT("Switch player (hold + stick: pick)"), K::Gamepad_FaceButton_Right, K::BackSpace, TEXT("")},
			{TEXT("Audible"), K::Gamepad_FaceButton_Left, K::J, TEXT("")},
			{TEXT("Coverage audibles"), K::Gamepad_FaceButton_Top, K::H, TEXT("")},
			{TEXT("Switch player"), K::Gamepad_LeftShoulder, K::Tab, TEXT("")},
			{TEXT("Show play art (hold)"), K::Gamepad_LeftTrigger, K::Q, TEXT("")},
			{TEXT("Hide the controls panel"), K::Gamepad_RightThumbstick, K::F1, TEXT("")},
			{TEXT("D-line shift  /  pinch"), K::Gamepad_DPad_Up, K::Up, TEXT("")},
			{TEXT("Spread (in the shift menus)"), K::Gamepad_DPad_Down, K::Down, TEXT("")},
			{TEXT("Linebacker shift  /  shift left"), K::Gamepad_DPad_Left, K::Left, TEXT("")},
			{TEXT("Coverage  /  shift right"), K::Gamepad_DPad_Right, K::Right, TEXT("")},
		};
		A[Ix(ECtx::Kicking)] = {
			{TEXT("Kick: start  /  power  /  accuracy"), K::Gamepad_FaceButton_Bottom, K::SpaceBar, TEXT("Or the right stick: pull back, push straight up")},
		};
	}
	return A[FMath::Clamp(Ix(C), 0, NumCtx - 1)];
}

const TArray<FFixed>& Fixed(ECtx C, bool bPad)
{
	static TArray<FFixed> P[NumCtx], Kb[NumCtx];
	static bool bBuilt = false;
	if (!bBuilt)
	{
		bBuilt = true;
		for (int32 c = 0; c < NumCtx; ++c)
		{
			P[c].Add({TEXT("MENU"), TEXT("Pause")});
			P[c].Add({TEXT("VIEW"), TEXT("Camera (hold)")});
			Kb[c].Add({TEXT("ESC"), TEXT("Pause")});
			Kb[c].Add({TEXT("C"), TEXT("Camera")});
		}
		P[Ix(ECtx::Carrier)].Append({{TEXT("LS"), TEXT("Run")}, {TEXT("RS"), TEXT("Up: truck  -  side / back: juke")}});
		P[Ix(ECtx::Passing)].Append({{TEXT("LS"), TEXT("Drop back  /  scramble")}, {TEXT("RS"), TEXT("Scrambling: truck / juke")}});
		P[Ix(ECtx::Receiving)].Append({{TEXT("LS"), TEXT("Run")}});
		P[Ix(ECtx::Defense)].Append({{TEXT("LS"), TEXT("Move  /  engaged: the rush move")}, {TEXT("RS"), TEXT("Up: hit stick  -  down: cut  -  rush moves")}});
		P[Ix(ECtx::PreOff)].Append({{TEXT("LS"), TEXT("Motion left / right  -  look down the line")}, {TEXT("RS"), TEXT("Flip the run's direction")}});
		P[Ix(ECtx::PreDef)].Append({{TEXT("LS"), TEXT("Move your player")}});
		P[Ix(ECtx::Kicking)].Append({{TEXT("LS"), TEXT("Aim")}, {TEXT("RS"), TEXT("Pull back for power, push up to kick")}});
		for (int32 c = 0; c < NumCtx; ++c)
			Kb[c].Add({TEXT("WASD"), c == Ix(ECtx::Kicking) ? TEXT("Aim (A / D)") : c >= Ix(ECtx::PreOff) ? TEXT("Move your player") : TEXT("Run (the arrows too)")});
	}
	const int32 I = FMath::Clamp(Ix(C), 0, NumCtx - 1);
	return bPad ? P[I] : Kb[I];
}

const TArray<FKey>& PadButtons()
{
	static TArray<FKey> B;
	if (B.Num() == 0)
		B = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
			EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftTrigger, EKeys::Gamepad_RightTrigger,
			EKeys::Gamepad_LeftThumbstick, EKeys::Gamepad_RightThumbstick,
			EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right};
	return B;
}

bool CanBind(ECtx C, const FKey& K)
{
	if (!K.IsValid() || K.IsMouseButton() || K.IsAnalog() || K.IsTouch()) return false;
	if (K.IsGamepadKey()) return PadButtons().Contains(K);
	// kept: pause, the camera, the movement keys (the arrows move in the play; before the snap they are the D-pad)
	if (K == EKeys::Escape || K == EKeys::C || K == EKeys::W || K == EKeys::A || K == EKeys::S || K == EKeys::D) return false;
	const bool bPre = C == ECtx::PreOff || C == ECtx::PreDef;
	if (bPre && K == EKeys::Enter) return false;  // (ENTER selects in every pre-snap menu)
	if (!bPre && (K == EKeys::Up || K == EKeys::Down || K == EKeys::Left || K == EKeys::Right)) return false;
	if (K == EKeys::LeftCommand || K == EKeys::RightCommand) return false;
	return true;
}

// ------------------------------------------------------------------ the maps
FKey Phys(ECtx C, const FKey& L)
{
	if (!Valid(C)) return L;
	Ensure();
	const FKey* P = GMap[Ix(C)].Find(L);
	return P ? *P : L;
}

FKey Logical(ECtx C, const FKey& P)
{
	if (!Valid(C)) return P;
	Ensure();
	for (const TPair<FKey, FKey>& M : GMap[Ix(C)])
		if (M.Value == P) return M.Key;
	return P;
}

FKey Assign(ECtx C, const FKey& L, const FKey& P)
{
	if (!Valid(C) || !L.IsValid() || !P.IsValid()) return FKey();
	Ensure();
	TMap<FKey, FKey>& M = GMap[Ix(C)];
	const FKey Was = Phys(C, L);
	if (Was == P) return FKey();
	const FKey Other = Logical(C, P);  // the logical button now on P
	M.Add(L, P);
	M.Add(Other, Was);
	for (auto It = M.CreateIterator(); It; ++It)
		if (It.Key() == It.Value()) It.RemoveCurrent();
	Save();
	UE_LOG(LogTemp, Display, TEXT("TAG controls: %s: %s now on %s (%s moved to %s)"), *Info(C).Name, *L.ToString(), *P.ToString(), *Other.ToString(), *Was.ToString());
	return Other;
}

void Reset(ECtx C, bool bPad)
{
	if (!Valid(C)) return;
	Ensure();
	for (auto It = GMap[Ix(C)].CreateIterator(); It; ++It)
		if (It.Key().IsGamepadKey() == bPad) It.RemoveCurrent();
	Save();
}

void ResetAll()
{
	Ensure();
	for (int32 c = 0; c < NumCtx; ++c) GMap[c].Reset();
	Save();
}

bool IsDefault(ECtx C, bool bPad)
{
	if (!Valid(C)) return true;
	Ensure();
	for (const TPair<FKey, FKey>& P : GMap[Ix(C)])
		if (P.Key.IsGamepadKey() == bPad) return false;
	return true;
}

void Load()
{
	GLoaded = true;
	for (int32 c = 0; c < NumCtx; ++c) GMap[c].Reset();
	if (!GConfig || NoSave()) return;
	GConfig->GetInt(Section, TEXT("GlyphStyle"), GStyle, GGameUserSettingsIni);
	GStyle = FMath::Clamp(GStyle, 0, 2);
	for (int32 c = 0; c < NumCtx; ++c)
	{
		FString V;
		if (!GConfig->GetString(Section, *FString::Printf(TEXT("Ctx%d"), c), V, GGameUserSettingsIni) || V.IsEmpty()) continue;
		TArray<FString> Pairs;
		V.ParseIntoArray(Pairs, TEXT(","));
		TMap<FKey, FKey> M;
		for (const FString& Pr : Pairs)
		{
			FString A, B;
			if (!Pr.Split(TEXT(">"), &A, &B)) continue;
			const FKey KA(*A), KB(*B);
			if (KA.IsValid() && KB.IsValid() && KA.IsGamepadKey() == KB.IsGamepadKey()) M.Add(KA, KB);
		}
		if (IsPermutation(M)) GMap[c] = M;
		else UE_LOG(LogTemp, Warning, TEXT("TAG controls: the saved %s layout does not add up; the defaults instead"), *Info(static_cast<ECtx>(c)).Name);
	}
}

ECtx Current() { return GCur; }
FScope::FScope(ECtx C) : Prev(GCur) { GCur = C; }
FScope::~FScope() { GCur = Prev; }
FKey Phys(const FKey& L) { return Phys(GCur, L); }

// ------------------------------------------------------------------ names and glyphs
FKey PadKeyOf(EPadGlyph G)
{
	switch (G)
	{
	case EPadGlyph::Cross: return EKeys::Gamepad_FaceButton_Bottom;
	case EPadGlyph::Circle: return EKeys::Gamepad_FaceButton_Right;
	case EPadGlyph::Square: return EKeys::Gamepad_FaceButton_Left;
	case EPadGlyph::Triangle: return EKeys::Gamepad_FaceButton_Top;
	case EPadGlyph::R1: return EKeys::Gamepad_RightShoulder;
	case EPadGlyph::L1: return EKeys::Gamepad_LeftShoulder;
	case EPadGlyph::R2: return EKeys::Gamepad_RightTrigger;
	case EPadGlyph::L2: return EKeys::Gamepad_LeftTrigger;
	case EPadGlyph::DUp: return EKeys::Gamepad_DPad_Up;
	case EPadGlyph::DDown: return EKeys::Gamepad_DPad_Down;
	case EPadGlyph::DLeft: return EKeys::Gamepad_DPad_Left;
	case EPadGlyph::DRight: return EKeys::Gamepad_DPad_Right;
	case EPadGlyph::L3: return EKeys::Gamepad_LeftThumbstick;
	case EPadGlyph::R3: return EKeys::Gamepad_RightThumbstick;
	default: return FKey();
	}
}

EPadGlyph GlyphOf(const FKey& K)
{
	if (K == EKeys::Gamepad_FaceButton_Bottom) return EPadGlyph::Cross;
	if (K == EKeys::Gamepad_FaceButton_Right) return EPadGlyph::Circle;
	if (K == EKeys::Gamepad_FaceButton_Left) return EPadGlyph::Square;
	if (K == EKeys::Gamepad_FaceButton_Top) return EPadGlyph::Triangle;
	if (K == EKeys::Gamepad_RightShoulder) return EPadGlyph::R1;
	if (K == EKeys::Gamepad_LeftShoulder) return EPadGlyph::L1;
	if (K == EKeys::Gamepad_RightTrigger) return EPadGlyph::R2;
	if (K == EKeys::Gamepad_LeftTrigger) return EPadGlyph::L2;
	if (K == EKeys::Gamepad_DPad_Up) return EPadGlyph::DUp;
	if (K == EKeys::Gamepad_DPad_Down) return EPadGlyph::DDown;
	if (K == EKeys::Gamepad_DPad_Left) return EPadGlyph::DLeft;
	if (K == EKeys::Gamepad_DPad_Right) return EPadGlyph::DRight;
	if (K == EKeys::Gamepad_LeftThumbstick) return EPadGlyph::L3;
	if (K == EKeys::Gamepad_RightThumbstick) return EPadGlyph::R3;
	return EPadGlyph::None;
}

EPadGlyph PhysGlyph(ECtx C, EPadGlyph G)
{
	if (!Valid(C)) return G;
	// the D-pad's pairs ("move" hints) stay as they are unless both arms went elsewhere together
	if (G == EPadGlyph::DUpDown || G == EPadGlyph::DLeftRight || G == EPadGlyph::None) return G;
	const FKey K = PadKeyOf(G);
	if (!K.IsValid()) return G;
	const EPadGlyph P = GlyphOf(Phys(C, K));
	return P == EPadGlyph::None ? G : P;
}

EPadGlyph PhysGlyph(EPadGlyph G) { return PhysGlyph(GCur, G); }

FString PadName(const FKey& K, bool bSony)
{
	struct FN { const FKey* Key; const TCHAR* Ps; const TCHAR* Xb; };
	static const FN N[] = {
		{&EKeys::Gamepad_FaceButton_Bottom, TEXT("CROSS"), TEXT("A")}, {&EKeys::Gamepad_FaceButton_Right, TEXT("CIRCLE"), TEXT("B")},
		{&EKeys::Gamepad_FaceButton_Left, TEXT("SQUARE"), TEXT("X")}, {&EKeys::Gamepad_FaceButton_Top, TEXT("TRIANGLE"), TEXT("Y")},
		{&EKeys::Gamepad_LeftShoulder, TEXT("L1"), TEXT("LB")}, {&EKeys::Gamepad_RightShoulder, TEXT("R1"), TEXT("RB")},
		{&EKeys::Gamepad_LeftTrigger, TEXT("L2"), TEXT("LT")}, {&EKeys::Gamepad_RightTrigger, TEXT("R2"), TEXT("RT")},
		{&EKeys::Gamepad_LeftThumbstick, TEXT("L3"), TEXT("LS")}, {&EKeys::Gamepad_RightThumbstick, TEXT("R3"), TEXT("RS")},
		{&EKeys::Gamepad_DPad_Up, TEXT("D-PAD UP"), TEXT("D-PAD UP")}, {&EKeys::Gamepad_DPad_Down, TEXT("D-PAD DOWN"), TEXT("D-PAD DOWN")},
		{&EKeys::Gamepad_DPad_Left, TEXT("D-PAD LEFT"), TEXT("D-PAD LEFT")}, {&EKeys::Gamepad_DPad_Right, TEXT("D-PAD RIGHT"), TEXT("D-PAD RIGHT")},
		{&EKeys::Gamepad_Special_Right, TEXT("OPTIONS"), TEXT("MENU")}, {&EKeys::Gamepad_Special_Left, TEXT("CREATE"), TEXT("VIEW")},
	};
	for (const FN& E : N)
		if (*E.Key == K) return bSony ? E.Ps : E.Xb;
	return K.GetDisplayName().ToString().ToUpper();
}

namespace
{
	struct FLabel { const FKey* Key; const TCHAR* Label; };
	const TArray<FLabel>& Labels()
	{
		static const TArray<FLabel> L = {
			{&EKeys::SpaceBar, TEXT("SPACE")}, {&EKeys::BackSpace, TEXT("BKSP")}, {&EKeys::Tab, TEXT("TAB")}, {&EKeys::Enter, TEXT("ENTER")},
			{&EKeys::Escape, TEXT("ESC")}, {&EKeys::LeftShift, TEXT("SHIFT")}, {&EKeys::LeftControl, TEXT("CTRL")}, {&EKeys::LeftAlt, TEXT("ALT")},
			{&EKeys::RightShift, TEXT("R-SHIFT")}, {&EKeys::RightControl, TEXT("R-CTRL")}, {&EKeys::RightAlt, TEXT("R-ALT")},
			{&EKeys::Zero, TEXT("0")}, {&EKeys::One, TEXT("1")}, {&EKeys::Two, TEXT("2")}, {&EKeys::Three, TEXT("3")}, {&EKeys::Four, TEXT("4")},
			{&EKeys::Five, TEXT("5")}, {&EKeys::Six, TEXT("6")}, {&EKeys::Seven, TEXT("7")}, {&EKeys::Eight, TEXT("8")}, {&EKeys::Nine, TEXT("9")},
			{&EKeys::Up, TEXT("↑")}, {&EKeys::Down, TEXT("↓")}, {&EKeys::Left, TEXT("←")}, {&EKeys::Right, TEXT("→")},
			{&EKeys::LeftBracket, TEXT("[")}, {&EKeys::RightBracket, TEXT("]")}, {&EKeys::Semicolon, TEXT(";")}, {&EKeys::Apostrophe, TEXT("'")},
			{&EKeys::Comma, TEXT(",")}, {&EKeys::Period, TEXT(".")}, {&EKeys::Slash, TEXT("/")}, {&EKeys::Backslash, TEXT("\\")},
			{&EKeys::Hyphen, TEXT("-")}, {&EKeys::Equals, TEXT("=")}, {&EKeys::Tilde, TEXT("`")}, {&EKeys::CapsLock, TEXT("CAPS")},
			{&EKeys::Insert, TEXT("INS")}, {&EKeys::Delete, TEXT("DEL")}, {&EKeys::Home, TEXT("HOME")}, {&EKeys::End, TEXT("END")},
			{&EKeys::PageUp, TEXT("PGUP")}, {&EKeys::PageDown, TEXT("PGDN")},
		};
		return L;
	}
}

FString KeyLabel(const FKey& K)
{
	for (const FLabel& L : Labels())
		if (*L.Key == K) return L.Label;
	if (K.IsGamepadKey()) return PadName(K, true);
	// letters and F-keys are their own names; the numpad and the rest by the engine's short name
	const FString N = K.GetFName().ToString();
	if (N.Len() <= 3) return N.ToUpper();
	return K.GetDisplayName(false).ToString().ToUpper();
}

FKey KeyFromLabel(const FString& Label)
{
	for (const FLabel& L : Labels())
		if (Label == L.Label) return *L.Key;
	if (Label.Len() >= 1 && Label.Len() <= 3)
	{
		const FKey K(*Label);
		if (K.IsValid() && !K.IsGamepadKey()) return K;
	}
	return FKey();
}

FString DefaultKbdLabel(EPadGlyph G)
{
	switch (G)
	{
	case EPadGlyph::Cross: return TEXT("SPACE");
	case EPadGlyph::Circle: return TEXT("BKSP");
	case EPadGlyph::Square: return TEXT("J");
	case EPadGlyph::Triangle: return TEXT("H");
	case EPadGlyph::R1: return TEXT("N");
	case EPadGlyph::L1: return TEXT("TAB");
	case EPadGlyph::R2: return TEXT("Q");
	case EPadGlyph::L2: return TEXT("BKSP");
	case EPadGlyph::DUp: return TEXT("↑");
	case EPadGlyph::DDown: return TEXT("↓");
	case EPadGlyph::DLeft: return TEXT("←");
	case EPadGlyph::DRight: return TEXT("→");
	default: return FString();
	}
}

FString PhysKeyLabel(ECtx C, const FString& Label)
{
	if (!Valid(C)) return Label;
	const FKey K = KeyFromLabel(Label);
	if (!K.IsValid()) return Label;
	const FKey P = Phys(C, K);
	return P == K ? Label : KeyLabel(P);
}

FString PhysKeyLabel(const FString& Label) { return PhysKeyLabel(GCur, Label); }

FString TranslateHint(const FString& Hint, bool bPad)
{
	if (!Valid(GCur) || IsDefault(GCur, bPad)) return Hint;
	// word by word (a word: letters, digits, '-'), each button word renamed once - never a renamed word renamed again
	static const TCHAR* PadWords[] = {TEXT("CROSS"), TEXT("CIRCLE"), TEXT("SQUARE"), TEXT("TRIANGLE"), TEXT("L1"), TEXT("R1"), TEXT("L2"), TEXT("R2"), TEXT("L3"), TEXT("R3")};
	auto PadWordKey = [](const FString& W) -> FKey
	{
		for (const FKey& K : PadButtons())
			if (PadName(K, true) == W) return K;
		return FKey();
	};
	FString Out, Word;
	auto Flush = [&]()
	{
		if (Word.IsEmpty()) return;
		FString R = Word;
		if (bPad)
		{
			for (const TCHAR* P : PadWords)
				if (Word == P)
				{
					const FKey K = PadWordKey(Word);
					if (K.IsValid()) R = PadName(Phys(K), true);
					break;
				}
		}
		else if (Word == Word.ToUpper() && Word != TEXT("-"))
			R = PhysKeyLabel(Word);
		Out += R;
		Word.Reset();
	};
	for (const TCHAR Ch : Hint)
	{
		if (FChar::IsAlnum(Ch) || (Ch == TEXT('-') && !Word.IsEmpty())) Word.AppendChar(Ch);
		else { Flush(); Out.AppendChar(Ch); }
	}
	Flush();
	return Out;
}

// ------------------------------------------------------------------ the controller drawn
int32 GlyphStyle() { Ensure(); return GStyle; }

void SetGlyphStyle(int32 S)
{
	Ensure();
	GStyle = FMath::Clamp(S, 0, 2);
	Save();
}

bool Sony(bool bSonyDetected)
{
	const int32 S = GlyphStyle();
	return S == 2 || (S == 0 && bSonyDetected);
}
}
