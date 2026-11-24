#include "UI/TagTheme.h"

#include "Audio/TagAudio.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace TagTheme
{
	FSlateFontInfo Font(int32 Size, const char* Weight, int32 LetterSpacing)
	{
		FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(Weight, Size);
		F.LetterSpacing = LetterSpacing;
		return F;
	}

	const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }

	const FSlateBrush* Round(float Radius)
	{
		static TMap<int32, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
		const int32 Key = FMath::RoundToInt(Radius * 4.f);
		if (const TSharedPtr<FSlateRoundedBoxBrush>* Hit = Cache.Find(Key)) return Hit->Get();
		TSharedPtr<FSlateRoundedBoxBrush> B = MakeShared<FSlateRoundedBoxBrush>(FLinearColor::White, Radius);
		Cache.Add(Key, B);
		return B.Get();
	}

	double Now() { return FPlatformTime::Seconds(); }

	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, const FLinearColor& C, float Radius)
	{
		if (Size.X <= 0 || Size.Y <= 0 || C.A <= 0.f) return;
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TL))), Radius > 0.f ? Round(Radius) : White(),
			ESlateDrawEffect::None, C);
	}

	void Gradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, const FLinearColor& From,
		const FLinearColor& To, bool bHorizontal, float Radius)
	{
		if (Size.X <= 0 || Size.Y <= 0) return;
		TArray<FSlateGradientStop> Stops;
		Stops.Add(FSlateGradientStop(FVector2f(0.f, 0.f), From));
		Stops.Add(FSlateGradientStop(FVector2f(Size), To));
		FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(TL))), Stops,
			bHorizontal ? Orient_Vertical : Orient_Horizontal, ESlateDrawEffect::None, FVector4f(Radius));
	}

	int32 PaintSelection(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, bool bActive,
		bool bFill, float Radius)
	{
		// a slow breath on the edge, so the eye finds the cursor without it flashing
		const float Pulse = 0.82f + 0.18f * FMath::Sin(static_cast<float>(Now()) * 3.4f);
		if (bActive)
		{
			// the glow: three soft rings outside the card
			for (int32 k = 3; k >= 1; --k)
			{
				const float E = 2.f + k * 3.f;
				Box(Out, Layer, G, TL - FVector2D(E, E), Size + FVector2D(2 * E, 2 * E), Gold * FLinearColor(1, 1, 1, 0.045f * Pulse), Radius + E);
			}
		}
		// the edge (a gold box under the card, the card inset over it)
		const float Ew = bActive ? 2.f : 1.5f;
		Box(Out, Layer + 1, G, TL - FVector2D(Ew, Ew), Size + FVector2D(2 * Ew, 2 * Ew), Gold * FLinearColor(1, 1, 1, bActive ? 0.95f * Pulse : 0.38f), Radius + Ew);
		if (bFill)
		{
			Box(Out, Layer + 2, G, TL, Size, PanelHi, Radius);
			// a gold sheen from the left, the bar on the left edge
			Gradient(Out, Layer + 3, G, TL, FVector2D(Size.X * 0.7, Size.Y), Gold * FLinearColor(1, 1, 1, bActive ? 0.16f : 0.07f), FLinearColor(Gold.R, Gold.G, Gold.B, 0.f), true, Radius);
			if (bActive) Box(Out, Layer + 4, G, TL + FVector2D(0, 3), FVector2D(4, FMath::Max(0.0, Size.Y - 6)), Gold, 2.f);
		}
		return Layer + 5;
	}

	int32 PaintPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, const FLinearColor& Fill, float Radius)
	{
		// the drop shadow: soft and a little low, as a card lifted off the field
		Box(Out, Layer, G, TL + FVector2D(-6, 2), Size + FVector2D(12, 14), FLinearColor(0, 0, 0, 0.16f * Fill.A), Radius + 6);
		Box(Out, Layer, G, TL + FVector2D(-2, 2), Size + FVector2D(4, 6), FLinearColor(0, 0, 0, 0.22f * Fill.A), Radius + 2);
		Box(Out, Layer + 1, G, TL, Size, Fill, Radius);
		// a faint sheen over the top third and a hairline at the top edge
		Gradient(Out, Layer + 2, G, TL, FVector2D(Size.X, FMath::Min(140.0, Size.Y * 0.35)), FLinearColor(1, 1, 1, 0.045f), FLinearColor(1, 1, 1, 0.f), false, Radius);
		Box(Out, Layer + 2, G, TL + FVector2D(Radius, 0), FVector2D(Size.X - 2 * Radius, 1), FLinearColor(1, 1, 1, 0.07f));
		return Layer + 3;
	}

	// ------------------------------------------------------------------ SSelect
	void SSelect::Construct(const FArguments& A)
	{
		Selected = A._Selected;
		Active = A._Active;
		Fill = A._Fill;
		Radius = A._Radius;
		ChildSlot.Padding(A._Padding)[ A._Content.Widget ];
	}

	int32 SSelect::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		if (Selected.Get()) Layer = PaintSelection(Out, Layer, G, FVector2D::ZeroVector, Sz, Active.Get(), true, Radius);
		else if (Fill.A > 0.f) { Box(Out, Layer, G, FVector2D::ZeroVector, Sz, Fill, Radius); ++Layer; }
		return SCompoundWidget::OnPaint(Args, G, Cull, Out, Layer + 1, Style, bParentEnabled);
	}

	// ------------------------------------------------------------------ SPanel
	void SPanel::Construct(const FArguments& A)
	{
		Fill = A._Fill;
		Radius = A._Radius;
		AccentTop = A._AccentTop;
		Focused = A._Focused;
		ChildSlot.Padding(A._Padding)[ A._Content.Widget ];
	}

	int32 SPanel::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const bool bFocus = Focused.Get();
		if (bFocus) Layer = PaintSelection(Out, Layer, G, FVector2D::ZeroVector, Sz, true, false, Radius);
		Layer = PaintPanel(Out, Layer, G, FVector2D::ZeroVector, Sz, Fill, Radius);
		const FLinearColor Top = bFocus ? Gold : AccentTop;
		if (Top.A > 0.f) Box(Out, Layer++, G, FVector2D::ZeroVector, FVector2D(Sz.X, bFocus ? 4.f : 3.f), Top, 2.f);
		return SCompoundWidget::OnPaint(Args, G, Cull, Out, Layer + 1, Style, bParentEnabled);
	}

	// ------------------------------------------------------------------ STile
	namespace { double GLastTileBlur = -10.0; }

	void STile::Construct(const FArguments& A)
	{
		OnClick = A._OnClick;
		Accent = A._Accent;
		const bool bSub = !A._Sub.IsEmpty();
		TSharedRef<SVerticalBox> Words = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(Font(A._LabelSize, "BoldCondensed", 40)).Text(FText::FromString(A._Label.ToUpper()))
				.ColorAndOpacity_Lambda([this] { return FSlateColor(FMath::Lerp(FLinearColor(0.86f, 0.88f, 0.92f), FLinearColor::White, Lift())); })
				.ShadowOffset(FVector2D(0, 1)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f))
			];
		if (bSub)
			Words->AddSlot().AutoHeight().Padding(0, 1, 0, 0)
			[
				SNew(STextBlock).Font(Font(11)).AutoWrapText(true).Text(FText::FromString(A._Sub))
				.ColorAndOpacity_Lambda([this] { return FSlateColor(FMath::Lerp(Dim * FLinearColor(1, 1, 1, 0.85f), FLinearColor(0.82f, 0.85f, 0.9f), Lift())); })
			];
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Words ];
		if (!A._Value.IsEmpty())
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
			[ SNew(STextBlock).Font(Font(15, "BoldCondensed", 30)).ColorAndOpacity(Gold).Text(FText::FromString(A._Value.ToUpper())) ];
		ChildSlot
		[
			SNew(SBox).WidthOverride(A._Width).MinDesiredHeight(A._Height > 0.f ? A._Height : 0.f).VAlign(VAlign_Center)
			.Padding_Lambda([this] { const float L = Lift(); return FMargin(22.f + 8.f * L, 9.f, 40.f - 4.f * L, 10.f); })
			[ Row ]
		];
	}

	float STile::Lift() const
	{
		const double T = Now();
		return bFocused ? EaseOut(static_cast<float>((T - FocusT) / 0.14)) : 1.f - EaseOut(static_cast<float>((T - BlurT) / 0.12));
	}

	FReply STile::OnKeyDown(const FGeometry& G, const FKeyEvent& E)
	{
		if (FSlateApplication::Get().GetNavigationActionFromKey(E) == EUINavigationAction::Accept)
		{
			if (!E.IsRepeat() && OnClick)
			{
				ATagAudio::Ui(TEXT("UiSelect"));
				const std::function<void()> F = OnClick; // (the click may rebuild the screen, and this tile with it)
				F();
			}
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	FReply STile::OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
	{
		if (E.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
		bPressed = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	FReply STile::OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E)
	{
		if (E.GetEffectingButton() != EKeys::LeftMouseButton || !bPressed) return FReply::Unhandled();
		bPressed = false;
		const bool bInside = G.IsUnderLocation(E.GetScreenSpacePosition());
		FReply R = FReply::Handled().ReleaseMouseCapture();
		if (bInside && OnClick)
		{
			ATagAudio::Ui(TEXT("UiSelect"));
			const std::function<void()> F = OnClick;
			F();
		}
		return R;
	}

	void STile::OnMouseEnter(const FGeometry& G, const FPointerEvent& E)
	{
		SCompoundWidget::OnMouseEnter(G, E);
		if (!bFocused) FSlateApplication::Get().SetAllUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	FReply STile::OnFocusReceived(const FGeometry& G, const FFocusEvent& E)
	{
		if (!bFocused)
		{
			// a click as the cursor goes from tile to tile (not for the focus a screen gives its first tile as it opens)
			if (Now() - GLastTileBlur < 0.25) ATagAudio::Ui(TEXT("UiMove"), 0.7f);
			bFocused = true;
			FocusT = Now();
		}
		return FReply::Handled();
	}

	void STile::OnFocusLost(const FFocusEvent& E)
	{
		if (bFocused)
		{
			bFocused = false;
			BlurT = Now();
			GLastTileBlur = BlurT;
		}
	}

	int32 STile::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const float L = Lift();
		const float R = 5.f;
		// the resting tile: dark glass
		Box(Out, Layer, G, FVector2D::ZeroVector, Sz, FMath::Lerp(FLinearColor(0.02f, 0.025f, 0.04f, 0.72f), FLinearColor(0.035f, 0.042f, 0.065f, 0.0f), L), R);
		if (L > 0.001f)
		{
			// focused: the soft glow, the gold edge, the lifted card, the sheen and the bar (they grow in with L)
			const float Pulse = 0.85f + 0.15f * FMath::Sin(static_cast<float>(Now()) * 3.4f);
			for (int32 k = 3; k >= 1; --k)
			{
				const float E = 2.f + k * 3.f;
				Box(Out, Layer + 1, G, -FVector2D(E, E), Sz + FVector2D(2 * E, 2 * E), Accent * FLinearColor(1, 1, 1, 0.05f * L * Pulse), R + E);
			}
			Box(Out, Layer + 2, G, -FVector2D(2, 2), Sz + FVector2D(4, 4), Accent * FLinearColor(1, 1, 1, 0.9f * L * Pulse), R + 2);
			Box(Out, Layer + 3, G, FVector2D::ZeroVector, Sz, PanelHi * FLinearColor(1, 1, 1, L), R);
			Gradient(Out, Layer + 4, G, FVector2D::ZeroVector, FVector2D(Sz.X * 0.75, Sz.Y), Accent * FLinearColor(1, 1, 1, 0.2f * L), FLinearColor(Accent.R, Accent.G, Accent.B, 0.f), true, R);
			Box(Out, Layer + 5, G, FVector2D(0, 4), FVector2D(6.f * L, Sz.Y - 8), Accent, 2.f);
			// the chevron, sliding in from the left
			const float Cx = Sz.X - 24.f - 8.f * (1.f - L), Cy = Sz.Y * 0.5f, H = 7.f;
			TArray<FVector2f> P = {FVector2f(Cx - H * 0.6f, Cy - H), FVector2f(Cx + H * 0.4f, Cy), FVector2f(Cx - H * 0.6f, Cy + H)};
			FSlateDrawElement::MakeLines(Out, Layer + 6, G.ToPaintGeometry(), P, ESlateDrawEffect::None, Accent * FLinearColor(1, 1, 1, L), true, 3.f);
		}
		else
			// resting: a hairline at the left edge so the column reads as one list
			Box(Out, Layer + 1, G, FVector2D(0, 6), FVector2D(2, Sz.Y - 12), FLinearColor(1, 1, 1, 0.12f));
		return SCompoundWidget::OnPaint(Args, G, Cull, Out, Layer + 7, Style, bParentEnabled);
	}

	// ------------------------------------------------------------------ pieces
	TSharedRef<SWidget> HintBar(TSharedRef<SWidget> Hints)
	{
		return SNew(SBorder).BorderImage(Round(8.f)).BorderBackgroundColor(FLinearColor(0.008f, 0.01f, 0.018f, 0.82f)).Padding(FMargin(20, 8, 6, 8))[ Hints ];
	}

	TSharedRef<SWidget> Title(const FString& Kicker, const FString& Words, int32 Size)
	{
		TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
		if (!Kicker.IsEmpty())
			V->AddSlot().AutoHeight().Padding(2, 0, 0, 0)[ SNew(STextBlock).Font(Font(12, "BoldCondensed", 260)).ColorAndOpacity(Gold).Text(FText::FromString(Kicker.ToUpper())) ];
		V->AddSlot().AutoHeight()
		[
			SNew(STextBlock).Font(Font(Size, "Black", 10)).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(Words.ToUpper()))
			.ShadowOffset(FVector2D(0, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.55f))
		];
		V->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(2, 4, 0, 0)[ SNew(SBox).WidthOverride(64).HeightOverride(4)[ SNew(SBorder).BorderImage(Round(2.f)).BorderBackgroundColor(Gold) ] ];
		return V;
	}

	int32 SBackdrop::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const
	{
		const FVector2D Sz = FVector2D(G.GetLocalSize());
		const float S = Strength;
		auto Stops = [&](const TArray<TPair<float, float>>& At, bool bAlongX)
		{
			TArray<FSlateGradientStop> St;
			for (const TPair<float, float>& P : At)
				St.Add(FSlateGradientStop(bAlongX ? FVector2f(Sz.X * P.Key, 0.f) : FVector2f(0.f, Sz.Y * P.Key), FLinearColor(Ink.R, Ink.G, Ink.B, FMath::Clamp(P.Value * S, 0.f, 1.f))));
			FSlateDrawElement::MakeGradient(Out, Layer++, G.ToPaintGeometry(), St, bAlongX ? Orient_Vertical : Orient_Horizontal);
		};
		if (bLeft) Stops({{0.f, 0.94f}, {0.3f, 0.82f}, {0.52f, 0.42f}, {0.78f, 0.16f}, {1.f, 0.22f}}, true);
		else Stops({{0.f, 0.62f}, {0.5f, 0.5f}, {1.f, 0.62f}}, true);
		// the top and the foot darker (the button bar sits on the foot)
		Stops({{0.f, 0.55f}, {0.22f, 0.f}, {0.7f, 0.f}, {1.f, 0.8f}}, false);
		return Layer;
	}
}
