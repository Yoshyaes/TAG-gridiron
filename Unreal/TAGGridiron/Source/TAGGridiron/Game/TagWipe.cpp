#include "Game/TagWipe.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "UI/TagTheme.h"

namespace
{
	constexpr int32 kWipeSlats = 9;
	constexpr float kWipeStagger = 0.07f; // each slat starts this much of the move after the one before it
}

void STagWipe::Start(float InSecs, const FString& InWord)
{
	bOn = true;
	bReleased = false;
	Phase = 0;
	T = 0.f;
	HeldT = 0.f;
	HeldFrames = 0;
	Secs = FMath::Max(0.05f, InSecs);
	Word = InWord;
}

void STagWipe::Advance(float RealDt)
{
	if (!bOn) return;
	const float Dt = FMath::Clamp(RealDt, 0.f, 0.1f); // (a hitch does not jump the slats across the screen)
	if (Phase == 0)
	{
		T += Dt;
		if (T >= Secs) { Phase = 1; T = 0.f; HeldT = 0.f; HeldFrames = 0; }
	}
	else if (Phase == 1)
	{
		HeldT += Dt;
		// (covered: the frame the director cuts on, and the one after it, are drawn under the slats)
		if (bReleased && ++HeldFrames > 2 && HeldT >= 0.04f) { Phase = 2; T = 0.f; }
	}
	else
	{
		T += Dt;
		if (T >= Secs) { bOn = false; Phase = 0; }
	}
}

float STagWipe::Progress() const
{
	if (!bOn) return 0.f;
	return Phase == 0 ? 0.45f * T / Secs : Phase == 1 ? 0.5f : 0.55f + 0.45f * T / Secs;
}

float STagWipe::Slat(int32 i, int32 N) const
{
	if (Phase == 1) return 1.f;
	const float Span = 1.f - (N - 1) * kWipeStagger;
	const float U = FMath::Clamp((T / Secs - i * kWipeStagger) / FMath::Max(Span, 0.05f), 0.f, 1.f);
	const float E = U * U * (3.f - 2.f * U);
	return Phase == 0 ? E : 1.f - E;
}

int32 STagWipe::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!bOn) return Layer;
	const FVector2D Size = G.GetLocalSize();
	const double W = Size.X / kWipeSlats;
	float Least = 1.f;
	for (int32 i = 0; i < kWipeSlats; ++i)
	{
		const float F = Slat(i, kWipeSlats);
		Least = FMath::Min(Least, F);
		if (F <= 0.f) continue;
		const double H = Size.Y * F;
		// down from the top coming in, back up to it going out (the gold bottom edge leads both ways), each slat a hair
		// wider than its share so no seam of picture shows between them
		const FLinearColor Fill = i % 2 ? FLinearColor(0.018f, 0.023f, 0.038f) : TagTheme::Ink;
		TagTheme::Box(Out, Layer, G, FVector2D(i * W - 1.0, 0.0), FVector2D(W + 2.0, H), Fill);
		if (F < 1.f) TagTheme::Box(Out, Layer + 1, G, FVector2D(i * W - 1.0, FMath::Max(0.0, H - 6.0)), FVector2D(W + 2.0, FMath::Min(6.0, H)), TagTheme::Gold);
	}
	// the mark, once the slats are (nearly) all down
	if (Least > 0.6f && FSlateApplication::IsInitialized())
	{
		const float A = FMath::Clamp((Least - 0.6f) / 0.4f, 0.f, 1.f);
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const int32 Big = FMath::Max(24, FMath::RoundToInt(Size.Y * 0.11));
		const FSlateFontInfo Mark = TagTheme::Font(Big, "Black", 120);
		const FString MarkText = TEXT("TAG");
		const FVector2D MS = Measure->Measure(MarkText, Mark);
		const FVector2D At((Size.X - MS.X) * 0.5, Size.Y * 0.5 - MS.Y * 0.62);
		FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(MS), FSlateLayoutTransform(FVector2f(At))), MarkText, Mark, ESlateDrawEffect::None,
			TagTheme::Text.CopyWithNewOpacity(A));
		const double Rule = MS.X * 0.9;
		TagTheme::Box(Out, Layer + 2, G, FVector2D((Size.X - Rule) * 0.5, At.Y + MS.Y + 6.0), FVector2D(Rule, 5.0), TagTheme::Gold.CopyWithNewOpacity(A));
		if (!Word.IsEmpty())
		{
			const FSlateFontInfo Small = TagTheme::Font(FMath::Max(12, Big / 4), "Bold", 400);
			const FVector2D WS = Measure->Measure(Word, Small);
			FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(WS), FSlateLayoutTransform(FVector2f((Size.X - WS.X) * 0.5, At.Y + MS.Y + 20.0))),
				Word, Small, ESlateDrawEffect::None, TagTheme::Gold.CopyWithNewOpacity(A));
		}
	}
	return Layer + 3;
}
