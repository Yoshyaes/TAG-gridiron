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
	void Banner(const FString& Text, const FString& Sub, const FLinearColor& Color);
	void Toast(const FString& Text);
	void SetHint(const FString& Text) { HintText = Text; }
	void SetHudVisible(bool b) { bHud = b; }
	void SetClockOffset(double s) { ClockOffset = s; }
	void SetFlag(bool b) { bFlag = b; }

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
	bool bHud = false;
	bool bFlag = false;
	double ClockOffset = 0;
	FString HintText;
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
