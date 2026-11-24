// The look every screen shares: the palette, the type, rounded panels with depth, the selection under the cursor (a soft
// gold glow, the card lifted, a gold edge and bar: one look on every screen, in place of the old hard yellow outline),
// the menu tile, the button bar at the foot of a screen, and the wash over the stadium behind a menu.
#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include <functional>

namespace TagTheme
{
	// ---- the palette
	inline const FLinearColor Ink(0.010f, 0.013f, 0.022f, 1.f);
	inline const FLinearColor Panel(0.026f, 0.033f, 0.054f, 0.94f);
	inline const FLinearColor PanelHi(0.075f, 0.09f, 0.135f, 0.98f);
	inline const FLinearColor Line(1.f, 1.f, 1.f, 0.08f);
	inline const FLinearColor Gold(1.f, 0.80f, 0.10f);
	inline const FLinearColor GoldDeep(0.92f, 0.52f, 0.02f);
	inline const FLinearColor Text(0.94f, 0.95f, 0.98f);
	inline const FLinearColor Dim(0.6f, 0.65f, 0.72f);
	inline const FLinearColor Faint(0.36f, 0.4f, 0.47f);

	// ---- type: Roboto's faces (Black for titles, BoldCondensed for tiles and labels)
	FSlateFontInfo Font(int32 Size, const char* Weight = "Regular", int32 LetterSpacing = 0);
	const FSlateBrush* White();
	/** A rounded box (white, tinted by whoever draws it). */
	const FSlateBrush* Round(float Radius);
	/** Wall-clock seconds: the pulses keep running while the game is paused. */
	double Now();
	/** x eased out (0..1). */
	inline float EaseOut(float X) { X = FMath::Clamp(X, 0.f, 1.f); return 1.f - (1.f - X) * (1.f - X) * (1.f - X); }

	// ---- painting
	void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, const FLinearColor& C, float Radius = 0.f);
	/** Left to right (bHorizontal) or top to bottom, From to To. */
	void Gradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, const FLinearColor& From,
		const FLinearColor& To, bool bHorizontal, float Radius = 0.f);
	/** The selection under the cursor: glow, lifted card (bFill), gold edge, gold bar on the left. bActive: the column the
	 *  pad is in (else a quieter frame). Returns the next free layer. */
	int32 PaintSelection(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, bool bActive,
		bool bFill = true, float Radius = 6.f);
	/** A panel with depth: a soft drop shadow, the fill, a faint sheen at the top. */
	int32 PaintPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FVector2D& Size, const FLinearColor& Fill,
		float Radius = 8.f);

	/** A card whose selection is drawn behind its content (nothing moves when it is selected). */
	class SSelect : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SSelect) : _Selected(false), _Active(true), _Fill(FLinearColor::Transparent), _Radius(5.f), _Padding(FMargin(0)) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ATTRIBUTE(bool, Selected)
			SLATE_ATTRIBUTE(bool, Active)
			SLATE_ARGUMENT(FLinearColor, Fill)
			SLATE_ARGUMENT(float, Radius)
			SLATE_ARGUMENT(FMargin, Padding)
		SLATE_END_ARGS()
		void Construct(const FArguments& A);
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle& Style, bool bParentEnabled) const override;
	private:
		TAttribute<bool> Selected, Active;
		FLinearColor Fill;
		float Radius = 5.f;
	};

	/** A panel: rounded, with a drop shadow and a sheen; its content padded inside. */
	class SPanel : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPanel) : _Fill(Panel), _Radius(8.f), _Padding(FMargin(20)), _AccentTop(FLinearColor::Transparent), _Focused(false) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(FLinearColor, Fill)
			SLATE_ARGUMENT(float, Radius)
			SLATE_ARGUMENT(FMargin, Padding)
			/** A coloured strip across the top (a team's colour, the gold); transparent: none. */
			SLATE_ARGUMENT(FLinearColor, AccentTop)
			/** The column the pad is in: the panel glows, edged and topped in gold. */
			SLATE_ATTRIBUTE(bool, Focused)
		SLATE_END_ARGS()
		void Construct(const FArguments& A);
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle& Style, bool bParentEnabled) const override;
	private:
		FLinearColor Fill, AccentTop;
		float Radius = 8.f;
		TAttribute<bool> Focused;
	};

	/** A menu tile: focusable (the pad's d-pad / stick moves between tiles, A / cross / Enter picks), hover focuses it.
	 *  Focused, it lifts: the gold bar grows in from the left, the label steps right, a chevron slides in. */
	class STile : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(STile) : _Width(420.f), _Height(0.f), _LabelSize(20), _Accent(Gold) {}
			SLATE_ARGUMENT(FString, Label)
			SLATE_ARGUMENT(FString, Sub)
			SLATE_ARGUMENT(float, Width)
			/** 0: as tall as its text. */
			SLATE_ARGUMENT(float, Height)
			SLATE_ARGUMENT(int32, LabelSize)
			/** The value shown on the right (a setting's state); empty: none. */
			SLATE_ARGUMENT(FString, Value)
			SLATE_ARGUMENT(FLinearColor, Accent)
			SLATE_ARGUMENT(std::function<void()>, OnClick)
		SLATE_END_ARGS()
		void Construct(const FArguments& A);
		virtual bool SupportsKeyboardFocus() const override { return true; }
		virtual FReply OnKeyDown(const FGeometry& G, const FKeyEvent& E) override;
		virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override;
		virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override;
		virtual void OnMouseEnter(const FGeometry& G, const FPointerEvent& E) override;
		virtual FReply OnFocusReceived(const FGeometry& G, const FFocusEvent& E) override;
		virtual void OnFocusLost(const FFocusEvent& E) override;
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle& Style, bool bParentEnabled) const override;
		/** 0..1: how far into its focused look it is. */
		float Lift() const;
		static FName TypeName() { return FName(TEXT("STile")); }
	private:
		std::function<void()> OnClick;
		FLinearColor Accent = Gold;
		double FocusT = -10.0, BlurT = -10.0;
		bool bPressed = false;
		bool bFocused = false;
	};

	/** The button bar at the foot of a screen: the hints on a dark rounded strip. */
	TSharedRef<SWidget> HintBar(TSharedRef<SWidget> Hints);
	/** A screen's title: a small gold kicker over big Black caps, a short gold rule under them. */
	TSharedRef<SWidget> Title(const FString& Kicker, const FString& Words, int32 Size = 40);

	/** The wash over the stadium behind a menu: darker to the left (bLeft) or all round, and at the foot. */
	class SBackdrop : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBackdrop) : _LeftWeighted(false), _Strength(1.f) {}
			SLATE_ARGUMENT(bool, LeftWeighted)
			SLATE_ARGUMENT(float, Strength)
		SLATE_END_ARGS()
		void Construct(const FArguments& A) { bLeft = A._LeftWeighted; Strength = A._Strength; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16, 16); }
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle& Style, bool bParentEnabled) const override;
	private:
		bool bLeft = false;
		float Strength = 1.f;
	};
}
