// The match loading screen: drawn over the highlight reel (Content/Movies/TAG_Highlights.mp4) by the engine's movie
// player while the stadium and the teams are built (AMatchDirector::StartMatch). Tonight's matchup in the teams'
// colours, the conditions, a loading bar with a travelling light, a rotating tip and the wordmark. Pure Slate: it
// ticks on the movie player's own thread while the game thread is busy, so it touches no game object.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FLoadingTeam
{
	FString City, Name, Abbr;
	FLinearColor Color = FLinearColor::White;
};

class SLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLoadingScreen) : _bHasMovie(true) {}
		SLATE_ARGUMENT(FLoadingTeam, Away)
		SLATE_ARGUMENT(FLoadingTeam, Home)
		SLATE_ARGUMENT(FString, Conditions)
		SLATE_ARGUMENT(TArray<FString>, Tips)
		SLATE_ARGUMENT(bool, bHasMovie)  // no reel: the screen paints its own backdrop
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TArray<FString> Tips;
	double T0 = -1.0, T = 0.0;
	bool bHasMovie = true;
	FText TipText() const;
	FSlateColor TipColor() const;
	FMargin SweepPad() const;
};
