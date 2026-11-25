// The TAG wipe (Research/NextLevel/04 A2): the broadcast's own mark across the whole screen in and out of a replay, so
// the cut to the replay camera, the cut back and the bodies' return to their live poses happen under it. Staggered
// slats drop over the picture (ink and the gold edge), the TAG wordmark sits on them while the screen is covered, and
// they lift off the new picture. The director drives it (Game/MatchBroadcast.cpp): Start, then each frame Advance; it
// cuts once Covered() and then Release()s it, and the slats lift no sooner than two frames after that, so the cut and
// the frame after it are drawn under the cover. No network's look: the game's own colours and name.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class STagWipe : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(STagWipe) {}
	SLATE_END_ARGS()
	void Construct(const FArguments&) { SetVisibility(EVisibility::HitTestInvisible); } // (drawn, never in the way of a click)

	/** Begin a wipe: the slats come down over InSecs (then hold until Release, then lift over InSecs). Word: the line
	 *  under the mark ("REPLAY", or empty). */
	void Start(float InSecs = 0.15f, const FString& InWord = FString());
	/** Each frame, real seconds (a watched game's 2x / 4x does not hurry it). */
	void Advance(float RealDt);
	/** The cut has been made: the slats may lift (two frames on at the soonest). */
	void Release() { bReleased = true; }
	/** Covering the screen now (the frame to cut on). */
	bool Covered() const { return bOn && Phase == 1; }
	bool IsOn() const { return bOn; }
	/** 0..1 over the whole wipe (down, held, up), for the graphics that sit over it. */
	float Progress() const;
	/** Stop at once (a match ending under it). */
	void Stop() { bOn = false; Phase = 0; }
	/** Seconds since the screen was first covered in this wipe (-1 not yet). */
	float CoveredFor() const { return Phase >= 1 ? HeldT : -1.f; }

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16, 16); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;

private:
	bool bOn = false, bReleased = false;
	int32 Phase = 0;           // 0 coming down, 1 covering, 2 lifting
	float T = 0.f, Secs = 0.15f, HeldT = 0.f;
	int32 HeldFrames = 0;
	FString Word;
	/** How far slat i of N is over the picture (0..1). */
	float Slat(int32 i, int32 N) const;
};
