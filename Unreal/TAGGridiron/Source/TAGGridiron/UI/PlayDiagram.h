// A play drawn on a card: the formation at the snap and the play art (the same strokes the field shows), seen from
// behind the side calling it. Offense: the offense at the bottom running up the card. Defense: the defense at the bottom,
// the offense across the top, the drops and rushes coming down the card.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Game/PlayArtStrokes.h"

class SPlayDiagram : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SPlayDiagram) : _Offense(true), _FirstDown(-1) {}
		SLATE_ARGUMENT(FArtPicture, Picture)
		SLATE_ARGUMENT(bool, Offense)
		/** The first-down line (yards along the field), or < 0 for none. */
		SLATE_ARGUMENT(double, FirstDown)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(254, 150); }

private:
	FArtPicture Pic;
	bool bOffense = true;
	double FirstDown = -1;
};
