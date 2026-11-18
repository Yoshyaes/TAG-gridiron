// Code-built Slate HUD + menus for TAG Gridiron.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include <functional>

class AMatchDirector;
class SBox;
namespace tag { struct OffPlay; struct DefCall; }

struct FCallChoice
{
	enum EKind { Off, Def, Punt, FG, Auto } Kind = Auto;
	const tag::OffPlay* Play = nullptr;
	const tag::DefCall* Call = nullptr;
};

/** Controller glyphs (drawn as shapes; keyboard players see the key instead). */
enum class EPadGlyph : uint8 { None, Cross, Circle, Square, Triangle, R1, L1, R2, L2, DUp, DDown, DLeft, DRight };

/** A row of the pre-snap panel: the button, what it does, and whether it is the current choice. */
struct FPreRow
{
	EPadGlyph Glyph = EPadGlyph::None;
	FString Key;     // keyboard key ("" = the glyph's default key)
	FString Label;
	bool bOn = false;
	FLinearColor Tint = FLinearColor::White;  // the glyph's colour when it stands for a receiver
};

/** A receiver's button icon over his head (screen position in viewport pixels). */
struct FArtIcon
{
	FVector2D Screen = FVector2D::ZeroVector;
	int32 Button = 0;  // 0..4: cross, circle, square, triangle, R1 (keys 1..5)
	bool bPrimary = false;
	bool bHot = false;  // hot routed
	bool bPicked = false;  // being hot routed / motioned right now
};

/** The highlight stick's cue: the man about to tackle the user's ball carrier (a ring at his feet) and the move that
 *  beats him over the carrier (its button and its name). Screen positions in viewport pixels. */
struct FHighlightCue
{
	bool bOn = false;
	FVector2D Carrier = FVector2D::ZeroVector;
	FVector2D Threat = FVector2D::ZeroVector;
	float ThreatR = 40.f;  // the ring's radius on screen
	FString Move;          // "TRUCK"
	EPadGlyph Glyph = EPadGlyph::None;  // a face button, or None for a right-stick flick (Key says which way)
	FString Key;
};

/** The kick meter (a user kick, Madden style): the aim arrow's flight on screen, the power swing, the accuracy needle. */
struct FKickMeterUI
{
	bool bOn = false;
	int32 Stage = 0;        // 0 aiming, 1 power (the swing), 2 accuracy (keyboard / face button), 3 away
	float Power = 0.f;      // 0..1.15 (past 1.0 the red: no farther, only wilder)
	float Acc = 0.f;        // -1..1, 0 dead centre
	bool bAccShown = false;
	TArray<FVector2D> Arc;  // the aim arrow: the ball's flight projected (viewport px)
	FVector2D Viewport = FVector2D(1920, 1080);
	FString Title;          // "FIELD GOAL  -  47 YD"
	FString Sub;            // wind
	bool bPad = false;
};

/** Engaged in the trenches: a pass rusher's energy (his moves cost it) and his blocker's power, over their heads. */
struct FLineBarUI
{
	FVector2D Screen = FVector2D::ZeroVector; // above the pair (viewport px)
	float Energy = 1.f;     // the rusher: 0..1
	float Power = 1.f;      // the blocker: 0..1
	float Edge = 0.f;       // who is winning the rep, -1 blocker .. +1 rusher
	bool bUser = false;     // the user's man (drawn larger, with the move buttons)
	FString Move;           // the last move tried ("SWIM"), shown a moment
	bool bMoveWon = false;
};

/** The man the user controls (Madden's marker): a ring at his feet, a notch the way he faces, a chevron over his head. */
struct FUserMarkerUI
{
	bool bOn = false;
	TArray<FVector2D> Ring, Notch;
	FVector2D Head = FVector2D::ZeroVector;
	FVector2D Viewport = FVector2D(1920, 1080);
	FString Number;
};

/** The receivers' button colours: their icons and their routes in the play art share them. */
inline FLinearColor ButtonColor(int32 Button)
{
	static const FLinearColor C[5] = {
		FLinearColor(0.36f, 0.62f, 1.f), FLinearColor(1.f, 0.30f, 0.33f), FLinearColor(1.f, 0.47f, 0.84f),
		FLinearColor(0.26f, 0.92f, 0.62f), FLinearColor(0.86f, 0.87f, 0.92f)};
	return C[FMath::Clamp(Button, 0, 4)];
}

class SGridironOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGridironOverlay) {}
		SLATE_ARGUMENT(AMatchDirector*, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** bKeepFocus: the same screen rebuilt (a selection changed): keep the focus on the same button. */
	void ShowModal(TSharedRef<SWidget> W, bool bKeepFocus = false);
	void ClearModal();
	bool HasModal() const { return bModal; }
	/** The screen the pause menu came over (a play call, the try choice): back as it was, or nothing. */
	void RestoreSuspended();
	void DropSuspended() { Suspended.Reset(); }
	/** Which pad's buttons the glyphs are: Sony's shapes, else Xbox letters. */
	void SetPadKind(bool bSony) { bSonyPad = bSony; }
	void Banner(const FString& Text, const FString& Sub, const FLinearColor& Color);
	void Toast(const FString& Text);
	/** The toast is a big play's stat line ("#89 R. Ochoa   2 rec, 25 yds, 1 TD"): shown as a lower third. */
	bool IsStatLine() const { return ToastText.Contains(TEXT("   ")) && ToastText.Contains(TEXT(" yds")); }
	void SetHint(const FString& Text) { HintText = Text; }
	void SetHudVisible(bool b) { bHud = b; }
	/** Broadcast only: the score bug without the ticker and the weather panel (the highlight reel). */
	void SetBroadcastOnly(bool b) { bBroadcastOnly = b; }
	void SetClockOffset(double s) { ClockOffset = s; }
	void SetFlag(bool b) { bFlag = b; }
	/** The pre-snap panel (hot routes, audibles, protection, shifts): empty rows hide it. */
	void SetPresnapPanel(const FString& Title, const FString& Sub, const TArray<FPreRow>& Rows, bool bPad)
	{
		PreTitle = Title;
		PreSub = Sub;
		PreRows = Rows;
		bPrePad = bPad;
	}
	void ClearPresnapPanel() { PreRows.Reset(); }
	void SetIcons(const TArray<FArtIcon>& In, const FVector2D& ViewportSize, bool bPad)
	{
		Icons = In;
		IconViewport = ViewportSize;
		bIconPad = bPad;
	}
	void SetHighlight(const FHighlightCue& C) { Highlight = C; }
	void SetKickMeter(const FKickMeterUI& K) { KickMeter = K; }
	/** The swing made: the meter stays up with it (the arrow gone: the camera has cut away). */
	void HoldKickResult() { KickMeter.Arc.Reset(); KickMeter.Stage = 3; KickMeter.bAccShown = true; }
	void SetUserMarker(const FUserMarkerUI& M) { UserMarker = M; }
	/** The user's sprint stamina under his man (Screen: viewport px; V < 0 hides it). */
	void SetStamina(const FVector2D& Screen, const FVector2D& Viewport, float V, bool bSprint) { StaminaAt = Screen; StaminaVp = Viewport; StaminaV = V; bStaminaSprint = bSprint; }
	void SetLineBars(const TArray<FLineBarUI>& B, const FVector2D& Viewport) { LineBars = B; LineViewport = Viewport; }

	// screens
	void ShowMainMenu();
	void ShowTeamSelect(bool bUserPlays);
	void ShowPlayCall(bool bOffense, bool bFourthDown, bool bTwoPoint, std::function<void(FCallChoice)> Done);
	void ShowPatChoice(std::function<void(bool)> Done);
	void ShowPause(std::function<void()> Resume, std::function<void()> SimToEnd, std::function<void()> Quit);
	/** The settings screen (commentary captions, ...); Back returns to whatever opened it. */
	void ShowSettings(std::function<void()> Back);
	void ShowBoxScore(const FString& Title, const FString& Button, std::function<void()> Done);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	/** A controller button (or keyboard key) centred at C, Size across, in G's local space (the glyph widgets use it too). */
	void PaintGlyph(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& C, float Size, EPadGlyph Gl,
		const FString& Key, bool bPad, const FLinearColor& Tint, float Alpha = 1.f) const;
	bool PadGlyphs() const;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TSharedRef<SWidget> MakeButton(const FString& Label, const FString& Sub, std::function<void()> OnClick, float Width = 420.f);
	TSharedRef<SWidget> MakeScoreBug();
	FText ClockText() const;
	FText DownText() const;
	FText TickerText() const;
	FText WeatherText() const;

	AMatchDirector* Director = nullptr;
	TSharedPtr<SBox> ModalBox;
	bool bModal = false;
	TSharedPtr<SWidget> ModalContent, Suspended;
	int32 SuspendedFocus = -1;
	bool bSonyPad = false;
	bool bHud = false;
	bool bBroadcastOnly = false;
	bool bFlag = false;
	double ClockOffset = 0;
	FString HintText;
	FString PreTitle, PreSub;
	TArray<FPreRow> PreRows;
	bool bPrePad = false;
	TArray<FArtIcon> Icons;
	FHighlightCue Highlight;
	FKickMeterUI KickMeter;
	FUserMarkerUI UserMarker;
	FVector2D StaminaAt = FVector2D::ZeroVector, StaminaVp = FVector2D(1920, 1080);
	float StaminaV = -1.f;
	bool bStaminaSprint = false;
	TArray<FLineBarUI> LineBars;
	FVector2D LineViewport = FVector2D(1920, 1080);
	int32 PaintKickMeter(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	int32 PaintLineBars(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	FVector2D IconViewport = FVector2D(1920, 1080);
	bool bIconPad = false;
	int32 PaintPresnap(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	FString BannerText;
	FString BannerSub;
	FLinearColor BannerColor = FLinearColor::White;
	float BannerT = 99.f;
	FString ToastText;
	float ToastT = 99.f;
	// team select state
	int SelHome = 0;
	int SelAway = 1;
	bool bSelUserHome = true;
	int SelWeather = 0;
	bool bSelNight = true;
	int SelQuarter = 1;
	// (UI/TeamSelect.cpp) the column the pad is on (0 the away team, 1 the game options, 2 the home team), the options
	// row, and which screen it is (Play Now or Watch a Game: a fresh visit puts the pad on the user's team)
	int32 SelCol = 2;
	int32 SelRow = 0;
	bool bSelUserPlays = false;
	FString CallTab;
	/** The formation (offense: its id) or set (defense: its name) shown inside the tab's family. */
	FString CallSet;
	/** The formations / sets of the current tab, in the book's order (D-pad up / down steps through them). */
	TArray<FString> CallSets;
	// the play call (Madden's layout): the team's playbook by formation family (L1 / R1), the formations of the family
	// (D-pad up / down), three plays a page called by their face buttons (square, cross, circle), the pages by D-pad
	// left / right, the coach's pick on triangle
	struct FCallCard
	{
		FString Kind, Name, Note;
		const tag::OffPlay* Op = nullptr;
		const tag::DefCall* Dc = nullptr;
		FCallChoice Choice;
	};
	TArray<FCallCard> CallCards;
	TArray<TPair<FString, FString>> CallTabs; // id, label
	int32 CallPage = 0;
	bool bCallOffense = false, bCallFourth = false, bCallTwo = false;
	std::function<void(FCallChoice)> CallDone;
	TSharedPtr<SWidget> CallRoot;
	void BuildPlayCall();
	bool CallKey(const FKey& K, bool bRepeat);
	void CallPick(FCallChoice C);
	// gamepad navigation: focus the menu's button (by its order in the menu) once the menu is laid out
	int32 PendingFocus = -1;
	static void Focusables(const TSharedRef<SWidget>& W, TArray<TSharedRef<SWidget>>& Out);
	int32 FocusedIndex() const;
};
