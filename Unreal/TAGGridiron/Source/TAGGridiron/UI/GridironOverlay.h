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

	// screens
	void ShowMainMenu();
	void ShowTeamSelect(bool bUserPlays);
	void ShowPlayCall(bool bOffense, bool bFourthDown, bool bTwoPoint, std::function<void(FCallChoice)> Done);
	void ShowPatChoice(std::function<void(bool)> Done);
	void ShowPause(std::function<void()> Resume, std::function<void()> SimToEnd, std::function<void()> Quit);
	void ShowBoxScore(const FString& Title, const FString& Button, std::function<void()> Done);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
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
	FVector2D IconViewport = FVector2D(1920, 1080);
	bool bIconPad = false;
	/** A controller button (or keyboard key) centred at C, Size across, in this widget's local space. */
	void PaintGlyph(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& C, float Size, EPadGlyph Gl,
		const FString& Key, bool bPad, const FLinearColor& Tint, float Alpha = 1.f) const;
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
	FString CallTab;
	// gamepad navigation: focus the menu's button (by its order in the menu) once the menu is laid out
	int32 PendingFocus = -1;
	static void Focusables(const TSharedRef<SWidget>& W, TArray<TSharedRef<SWidget>>& Out);
	int32 FocusedIndex() const;
};
