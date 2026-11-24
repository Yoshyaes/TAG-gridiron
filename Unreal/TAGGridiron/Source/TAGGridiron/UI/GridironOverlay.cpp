#include "UI/GridironOverlay.h"

#include "Game/MatchDirector.h"
#include "Audio/TagAudio.h"
#include "Booth/TagBooth.h"
#include "Game/TagRumble.h"
#include "Game/PlayArtStrokes.h"
#include "UI/PlayDiagram.h"
#include "UI/RosterUI.h"
#include "UI/TagTheme.h"
#include "Game/TagBindings.h"
#include "Render/Coords.h"
#include "Sim/Playbook.h"
#include "Sim/Stats.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Input/Events.h"
#include "Input/NavigationReply.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include <concepts>
#include <string>

using namespace TagCoords;

namespace
{
	const FLinearColor Panel(0.035f, 0.045f, 0.07f, 0.93f);
	const FLinearColor PanelLight(0.07f, 0.09f, 0.13f, 0.95f);
	const FLinearColor Accent(1.f, 0.83f, 0.f);
	const FLinearColor Dim(0.6f, 0.65f, 0.72f);

	FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText T(const FString& S) { return FText::FromString(S); }
	FString S(const std::string& s) { return FString(s.c_str()); }
}

void SGridironOverlay::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	ChildSlot
	[
		SNew(SOverlay)
		// ticker (top-left)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(24)
		[
			SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(Panel).Padding(FMargin(14, 10))
			.Visibility_Lambda([this] { return bHud && !bBroadcastOnly && !bModal && !TickerText().IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SBox).WidthOverride(560)
				[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(0.92f, 0.94f, 0.97f)).AutoWrapText(true).Text_Lambda([this] { return TickerText(); }) ]
			]
		]
		// weather (top-right)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)
		[
			SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(Panel).Padding(FMargin(14, 8))
			.Visibility_Lambda([this] { return bHud && !bBroadcastOnly && !bModal ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Dim).Text_Lambda([this] { return WeatherText(); }) ]
		]
		// banner
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0, 180, 0, 0))
		[
			SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Font(64, "Bold")).ShadowOffset(FVector2D(3, 3))
				.ShadowColorAndOpacity_Lambda([this] { const float A = BannerT < 0.25f ? BannerT / 0.25f : BannerT > 1.8f ? FMath::Max(0.f, 1.f - (BannerT - 1.8f) / 0.5f) : 1.f; return FLinearColor(0, 0, 0, 0.8f * A); })
				.Text_Lambda([this] { return T(BannerText); })
				.ColorAndOpacity_Lambda([this] { const float A = BannerT < 0.25f ? BannerT / 0.25f : BannerT > 1.8f ? FMath::Max(0.f, 1.f - (BannerT - 1.8f) / 0.5f) : 1.f; FLinearColor C = BannerColor; C.A = A; return FSlateColor(C); })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Font(18, "Bold")).ShadowOffset(FVector2D(2, 2))
				// the shadow goes with the text (it stayed behind as black text over the field after the banner faded)
				.ShadowColorAndOpacity_Lambda([this] { return FLinearColor(0, 0, 0, BannerT < 2.3f ? 0.8f : 0.f); })
				.Text_Lambda([this] { return T(BannerSub); })
				.ColorAndOpacity_Lambda([this] { return FSlateColor(FLinearColor(1, 1, 1, BannerT < 2.3f ? 1.f : 0.f)); })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24, 0, 0)
			[
				SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(FLinearColor(0.03f, 0.04f, 0.065f, 0.92f)).Padding(FMargin(0))
				.Visibility_Lambda([this] { return !bModal && !IsStatLine() && ToastT < (ToastText.Len() > 14 ? 3.2f : 1.3f) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(5)[ SNew(SBorder).BorderImage(TagTheme::Round(2.f)).BorderBackgroundColor(Accent) ] ]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(16, 8, 20, 8))
					[ SNew(STextBlock).Font(TagTheme::Font(18, "BoldCondensed", 40)).ColorAndOpacity(Accent).Text_Lambda([this] { return T(ToastText.ToUpper()); }) ]
				]
			]
		]
		// a big play's stat line: the broadcast's lower third, above the score bar (over the field it sat on the
		// player's helmet in the close-up that follows a score)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 0, 74))
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this] { return IsStatLine() && ToastT < 4.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.96f, 0.96f, 0.97f, 0.97f)).Padding(FMargin(16, 7))
				[ SNew(STextBlock).Font(Font(17, "Bold")).ColorAndOpacity(FLinearColor(0.06f, 0.07f, 0.1f)).Text_Lambda([this] { FString N, St; ToastText.Split(TEXT("   "), &N, &St); return T(N); }) ]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.07f, 0.09f, 0.13f, 0.95f)).Padding(FMargin(16, 7))
				[ SNew(STextBlock).Font(Font(17, "Bold")).ColorAndOpacity(Accent).Text_Lambda([this] { FString N, St; ToastText.Split(TEXT("   "), &N, &St); return T(St.ToUpper()); }) ]
			]
		]
		// hint
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 0, 110))
		[
			SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(Panel).Padding(FMargin(16, 8))
			.Visibility_Lambda([this] { return (bHud && !HintText.IsEmpty() && !bModal) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[ SNew(STextBlock).Font(Font(13)).ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.92f)).Text_Lambda([this] { return T(HintText); }) ]
		]
		// scorebug
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 0, 28))
		[ MakeScoreBug() ]
		// modal content
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[ SAssignNew(ModalBox, SBox).Visibility_Lambda([this] { return bModal ? EVisibility::Visible : EVisibility::Collapsed; }) ]
		// over a menu the toast is a notification card at the top right (it sat on the menu's middle)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0, 36, 40, 0))
		[
			SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(FLinearColor(0.03f, 0.04f, 0.065f, 0.94f)).Padding(FMargin(0))
			.Visibility_Lambda([this] { return bModal && !IsStatLine() && ToastT < 3.2f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(5)[ SNew(SBorder).BorderImage(TagTheme::Round(2.f)).BorderBackgroundColor(Accent) ] ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(14, 10, 20, 10))
				[ SNew(STextBlock).Font(TagTheme::Font(15, "BoldCondensed", 40)).ColorAndOpacity(FLinearColor::White).Text_Lambda([this] { return T(ToastText.ToUpper()); }) ]
			]
		]
	];
}

void SGridironOverlay::Tick(const FGeometry& G, const double Now, const float Dt)
{
	SCompoundWidget::Tick(G, Now, Dt);
	// on the game's clock, not the wall's: a fixed-step film (rendered slower than real time) showed a 3.2 s stat line
	// for half a second; in play the two are the same
	// (once per engine frame: the widget can be ticked more than once a frame, and the stat line left at 2 s)
	static uint64 LastFrame = 0;
	const float GameDt = LastFrame == GFrameCounter ? 0.f : FMath::Min(static_cast<float>(FApp::GetDeltaTime()), 0.1f);
	LastFrame = GFrameCounter;
	BannerT += GameDt;
	ToastT += GameDt;
	PumpAbilities();  // (the zone: banners, the badges over the X-Factor men)
	// the play call sizes its cards to the screen: a window resized (or the first layout, if it was built before the
	// overlay had a size) lays it out again
	if (IsPlayCall() && CallRoot.IsValid() && ModalContent == CallRoot) // (not under the pause menu)
	{
		const FVector2D Now2 = G.GetLocalSize();
		if (Now2.X >= 640.0 && Now2.Y >= 360.0 && !Now2.Equals(CallBuiltAvail, 2.0)) BuildPlayCall();
	}
	// a screen coming in: it fades up and rises 16 px into place
	if (ModalBox.IsValid())
	{
		const float E = TagTheme::EaseOut(static_cast<float>((TagTheme::Now() - ModalInT) / 0.22));
		ModalBox->SetRenderOpacity(0.15f + 0.85f * E);
		ModalBox->SetRenderTransform(FSlateRenderTransform(FVector2f(0.f, 16.f * (1.f - E))));
	}
	// a menu just opened (laid out by now): give its button focus so the gamepad can drive it
	if (PendingFocus >= 0 && bModal && ModalBox.IsValid() && FSlateApplication::IsInitialized())
	{
		TArray<TSharedRef<SWidget>> All;
		Focusables(ModalBox.ToSharedRef(), All);
		if (All.Num() > 0)
			FSlateApplication::Get().SetAllUserFocus(All[FMath::Clamp(PendingFocus, 0, All.Num() - 1)], EFocusCause::Navigation);
		PendingFocus = -1;
	}
	// keep the menu focused if a click on the game view took it away (a pad can't click back in)
	if (bModal && ModalBox.IsValid() && FSlateApplication::IsInitialized() && FocusedIndex() < 0)
	{
		TArray<TSharedRef<SWidget>> All;
		Focusables(ModalBox.ToSharedRef(), All);
		if (All.Num() > 0) FSlateApplication::Get().SetAllUserFocus(All[0], EFocusCause::Navigation);
	}
}

void SGridironOverlay::Focusables(const TSharedRef<SWidget>& W, TArray<TSharedRef<SWidget>>& Out)
{
	if (W->GetVisibility().IsVisible() || W->GetVisibility() == EVisibility::SelfHitTestInvisible)
	{
		if (W->SupportsKeyboardFocus()) Out.Add(W);
		FChildren* C = W->GetChildren();
		for (int32 i = 0; C && i < C->Num(); ++i) Focusables(C->GetChildAt(i), Out);
	}
}

int32 SGridironOverlay::FocusedIndex() const
{
	if (!ModalBox.IsValid() || !FSlateApplication::IsInitialized()) return -1;
	const TSharedPtr<SWidget> F = FSlateApplication::Get().GetUserFocusedWidget(0);
	if (!F.IsValid()) return -1;
	TArray<TSharedRef<SWidget>> All;
	Focusables(ModalBox.ToSharedRef(), All);
	return All.IndexOfByPredicate([&](const TSharedRef<SWidget>& X) { return X == F.ToSharedRef(); });
}

void SGridironOverlay::ShowModal(TSharedRef<SWidget> W, bool bKeepFocus)
{
	const int32 Was = bKeepFocus ? FocusedIndex() : -1;
	// a new screen comes in (fades up); the same screen rebuilt in place (a selection moved) just changes
	if (!bModal || !ModalContent.IsValid() || ModalContent->GetType() != W->GetType()) ModalInT = TagTheme::Now();
	ModalBox->SetContent(W);
	ModalContent = W;
	bModal = true;
	PendingFocus = FMath::Max(0, Was); // focused next tick, once laid out
}

void SGridironOverlay::ClearModal()
{
	ModalBox->SetContent(SNullWidget::NullWidget);
	ModalContent.Reset();
	bModal = false;
	PendingFocus = -1;
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
}

int32 SGridironOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	// pre-snap: the receivers' icons and the adjustments panel
	if (bHud && !bModal) Layer = PaintPresnap(OutDrawElements, Layer + 1, AllottedGeometry);
	if (bHud && !bModal) Layer = PaintLineBars(OutDrawElements, Layer + 1, AllottedGeometry);
	if (bHud && !bModal && !bBroadcastOnly) Layer = PaintAbilities(OutDrawElements, Layer + 1, AllottedGeometry);
	if (!bModal) Layer = PaintKickMeter(OutDrawElements, Layer + 1, AllottedGeometry);
	// the focused menu button (gamepad / keyboard navigation) gets a gold frame
	if (bModal && FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetUserFocusedWidget(0);
		// (the play call is driven by its buttons, not a focus frame; a menu tile draws its own)
		if (F.IsValid() && F != ModalBox && F != CallRoot && !F->GetTypeAsString().Contains(TEXT("STile")))
		{
			FWidgetPath Path;
			if (FSlateApplication::Get().GeneratePathToWidgetUnchecked(F.ToSharedRef(), Path) && Path.IsValid())
			{
				// the focused widget's rectangle in this overlay's own space (its path geometry is window absolute)
				// (a generated path is in desktop space: take the window's position out to get paint space)
				const FGeometry& G = Path.Widgets.Last().Geometry;
				const FVector2D Win = Path.Widgets[0].Geometry.GetAbsolutePosition();
				const FVector2D TL = AllottedGeometry.AbsoluteToLocal(G.GetAbsolutePosition() - Win);
				const FVector2D BR = AllottedGeometry.AbsoluteToLocal(G.GetAbsolutePosition() + G.GetAbsoluteSize() - Win);
				const FVector2D Size = BR - TL;
				Layer = TagTheme::PaintSelection(OutDrawElements, Layer + 1, AllottedGeometry, TL, Size, true, false, 4.f);
			}
		}
	}
	return Layer;
}

namespace
{
	const TCHAR* DefaultKey(EPadGlyph G)
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
		case EPadGlyph::DUpDown: return TEXT("↑↓");
		case EPadGlyph::DLeftRight: return TEXT("←→");
		default: return TEXT("");
		}
	}
	FVector2D Measure(const FString& S, const FSlateFontInfo& F)
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F);
	}
}

void SGridironOverlay::PaintGlyph(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& C, float Size, EPadGlyph Gl,
	const FString& Key, bool bPad, const FLinearColor& Tint, float Alpha) const
{
	if (Gl == EPadGlyph::None && Key.IsEmpty()) return;
	auto Col = [Alpha](FLinearColor X) { X.A *= Alpha; return X; };
	auto Lines = [&](const TArray<FVector2f>& P, const FLinearColor& Cl, float Th, int32 L = 1)
	{
		FSlateDrawElement::MakeLines(Out, Layer + L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, Col(Cl), true, Th);
	};
	auto Poly = [&](float R, int32 N, float Rot)
	{
		TArray<FVector2f> P;
		for (int32 i = 0; i <= N; ++i)
		{
			const float A = Rot + 2.f * PI * i / N;
			P.Add(FVector2f(C.X + FMath::Cos(A) * R, C.Y + FMath::Sin(A) * R));
		}
		return P;
	};
	auto Text = [&](const FString& S, int32 Pt, const FLinearColor& Cl)
	{
		const FSlateFontInfo F = Font(Pt, "Bold");
		const FVector2D M = Measure(S, F);
		FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(C - M * 0.5))), S, F,
			ESlateDrawEffect::None, Col(Cl));
	};
	auto Box = [&](const FVector2D& Sz, const FLinearColor& Cl)
	{
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(C - Sz * 0.5))), TagTheme::Round(FMath::Min(5.f, Size * 0.18f)),
			ESlateDrawEffect::None, Col(Cl));
	};
	const float R = Size * 0.5f;
	const FLinearColor Dark(0.03f, 0.035f, 0.05f, 0.94f);
	if (!bPad || Gl == EPadGlyph::None)
	{
		// keyboard: the key on a keycap
		const FString K = Key.IsEmpty() ? FString(DefaultKey(Gl)) : Key;
		const FVector2D M = Measure(K, Font(FMath::RoundToInt(Size * 0.42f), "Bold"));
		const FVector2D Sz(FMath::Max<double>(Size, M.X + 12), Size * 0.86f);
		Box(Sz + FVector2D(3, 3), Tint * FLinearColor(0.7f, 0.7f, 0.7f, 1.f));
		Box(Sz, Dark);
		Text(K, FMath::RoundToInt(Size * 0.42f), Tint);
		return;
	}
	const float Th = FMath::Max(2.f, Size * 0.085f);
	const float S = R * 0.46f;
	auto P2 = [&](float X, float Y) { return FVector2f(C.X + X, C.Y + Y); };
	// the face buttons in their own colours (an untinted glyph: a receiver's icon keeps his route colour): an Xbox pad's
	// green A, red B, blue X, yellow Y; a PlayStation pad's blue cross, red circle, pink square, green triangle
	const FLinearColor FaceInk = !Tint.Equals(FLinearColor::White, 0.01f) ? Tint : bSonyPad
		? (Gl == EPadGlyph::Cross ? FLinearColor(0.45f, 0.62f, 1.f) : Gl == EPadGlyph::Circle ? FLinearColor(1.f, 0.38f, 0.38f) : Gl == EPadGlyph::Square ? FLinearColor(1.f, 0.55f, 0.85f) : FLinearColor(0.3f, 0.9f, 0.7f))
		: (Gl == EPadGlyph::Cross ? FLinearColor(0.35f, 0.85f, 0.3f) : Gl == EPadGlyph::Circle ? FLinearColor(1.f, 0.3f, 0.28f) : Gl == EPadGlyph::Square ? FLinearColor(0.3f, 0.6f, 1.f) : FLinearColor(1.f, 0.85f, 0.2f));
	switch (Gl)
	{
	case EPadGlyph::Cross:
	case EPadGlyph::Circle:
	case EPadGlyph::Square:
	case EPadGlyph::Triangle:
		Lines(Poly(R * 0.5f, 28, 0.f), Dark, R, 0);  // the button (a ring as thick as its radius is a disc)
		Lines(Poly(R - 0.8f, 28, 0.f), FaceInk * FLinearColor(1, 1, 1, 0.45f), 1.4f);
		if (!bSonyPad)
		{
			// an Xbox-style pad: the face buttons by their letters
			Text(Gl == EPadGlyph::Cross ? TEXT("A") : Gl == EPadGlyph::Circle ? TEXT("B") : Gl == EPadGlyph::Square ? TEXT("X") : TEXT("Y"), FMath::RoundToInt(Size * 0.46f), FaceInk);
			break;
		}
		if (Gl == EPadGlyph::Cross)
		{
			Lines({P2(-S, -S), P2(S, S)}, FaceInk, Th);
			Lines({P2(-S, S), P2(S, -S)}, FaceInk, Th);
		}
		else if (Gl == EPadGlyph::Circle) Lines(Poly(S * 1.02f, 24, 0.f), FaceInk, Th);
		else if (Gl == EPadGlyph::Square) Lines({P2(-S * 0.92f, -S * 0.92f), P2(S * 0.92f, -S * 0.92f), P2(S * 0.92f, S * 0.92f), P2(-S * 0.92f, S * 0.92f), P2(-S * 0.92f, -S * 0.92f)}, FaceInk, Th);
		else Lines(Poly(S * 1.12f, 3, -PI * 0.5f), FaceInk, Th);  // apex up
		break;
	case EPadGlyph::R1:
	case EPadGlyph::L1:
	case EPadGlyph::R2:
	case EPadGlyph::L2:
	{
		// a shoulder button / trigger: a pill (a trigger's top edge rounder), its name on it
		const bool bTrig = Gl == EPadGlyph::R2 || Gl == EPadGlyph::L2;
		const FVector2D Sz(Size * 1.36f, Size * 0.8f);
		const float Rad = Sz.Y * (bTrig ? 0.42f : 0.3f);
		auto RBox = [&](const FVector2D& S2, const FLinearColor& Cl, int32 L)
		{
			FSlateDrawElement::MakeBox(Out, Layer + L, G.ToPaintGeometry(FVector2f(S2), FSlateLayoutTransform(FVector2f(C - S2 * 0.5))), TagTheme::Round(Rad),
				ESlateDrawEffect::None, Col(Cl));
		};
		RBox(Sz + FVector2D(3, 3), Tint * FLinearColor(0.62f, 0.64f, 0.7f, 0.95f), 0);
		RBox(Sz, FLinearColor(0.045f, 0.05f, 0.07f, 0.97f), 1);
		Text(bSonyPad ? (Gl == EPadGlyph::R1 ? TEXT("R1") : Gl == EPadGlyph::L1 ? TEXT("L1") : Gl == EPadGlyph::L2 ? TEXT("L2") : TEXT("R2"))
			: (Gl == EPadGlyph::R1 ? TEXT("RB") : Gl == EPadGlyph::L1 ? TEXT("LB") : Gl == EPadGlyph::L2 ? TEXT("LT") : TEXT("RT")), FMath::RoundToInt(Size * 0.36f), Tint);
		break;
	}
	case EPadGlyph::L3:
	case EPadGlyph::R3:
		// a stick click: the stick's cap, ringed, its name on it
		Lines(Poly(R * 0.5f, 28, 0.f), Dark, R, 0);
		Lines(Poly(R - 0.8f, 28, 0.f), Tint * FLinearColor(1, 1, 1, 0.55f), 1.6f);
		Lines(Poly(R * 0.62f, 24, 0.f), Tint * FLinearColor(1, 1, 1, 0.22f), 1.f);
		Text(bSonyPad ? (Gl == EPadGlyph::L3 ? TEXT("L3") : TEXT("R3")) : (Gl == EPadGlyph::L3 ? TEXT("LS") : TEXT("RS")), FMath::RoundToInt(Size * 0.32f), Tint);
		break;
	default:
	{
		// the D-pad on its dark plate: the arms grey, the pressed arm (or pair of arms) lit, with its arrowhead
		const FLinearColor Arm(0.3f, 0.32f, 0.38f, 1.f);
		const float A = R * 0.88f, W = R * 0.5f;
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Size, Size), FSlateLayoutTransform(FVector2f(C - FVector2D(R, R)))), TagTheme::Round(R * 0.45f),
			ESlateDrawEffect::None, Col(FLinearColor(0.03f, 0.035f, 0.05f, 0.85f)));
		Lines({P2(-A, 0), P2(A, 0)}, Arm, W, 1);
		Lines({P2(0, -A), P2(0, A)}, Arm, W, 1);
		TArray<FVector2f> Lit;
		if (Gl == EPadGlyph::DUp || Gl == EPadGlyph::DUpDown) Lit.Add(FVector2f(0, -1));
		if (Gl == EPadGlyph::DDown || Gl == EPadGlyph::DUpDown) Lit.Add(FVector2f(0, 1));
		if (Gl == EPadGlyph::DLeft || Gl == EPadGlyph::DLeftRight) Lit.Add(FVector2f(-1, 0));
		if (Gl == EPadGlyph::DRight || Gl == EPadGlyph::DLeftRight) Lit.Add(FVector2f(1, 0));
		for (const FVector2f& D : Lit)
		{
			Lines({P2(D.X * R * 0.16f, D.Y * R * 0.16f), P2(D.X * A, D.Y * A)}, Tint, W, 2);
			// the arrowhead, dark on the lit arm
			const FVector2f Tip = P2(D.X * A * 0.86f, D.Y * A * 0.86f), Bk = P2(D.X * A * 0.42f, D.Y * A * 0.42f);
			const FVector2f Side(-D.Y * W * 0.32f, D.X * W * 0.32f);
			Lines({Bk + Side, Tip, Bk - Side}, Dark, FMath::Max(1.5f, W * 0.2f), 3);
		}
		break;
	}
	}
}

FVector2D SGridironOverlay::GlyphExtent(EPadGlyph Gl, const FString& Key, float Size, bool bPad) const
{
	if (bPad && Gl != EPadGlyph::None)
	{
		const bool bShoulder = Gl == EPadGlyph::R1 || Gl == EPadGlyph::L1 || Gl == EPadGlyph::R2 || Gl == EPadGlyph::L2;
		return FVector2D((bShoulder ? Size * 1.36f + 3.f : Size) + 6.f, Size + 4.f);
	}
	const FString K = Key.IsEmpty() ? FString(DefaultKey(Gl)) : Key;
	if (K.Len() <= 1 || !FSlateApplication::IsInitialized()) return FVector2D(Size + 6.f, Size + 4.f);
	const FVector2D M = Measure(K, Font(FMath::RoundToInt(Size * 0.42f), "Bold"));
	return FVector2D(FMath::Max<double>(Size, M.X + 12) + 7.f, Size + 4.f);
}

int32 SGridironOverlay::PaintPresnap(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
{
	const FVector2D Local = G.GetLocalSize();
	// the highlight stick: a gold ring at the feet of the man about to make the tackle, and over the ball carrier the
	// move that beats him
	if (Highlight.bOn && IconViewport.X > 0 && IconViewport.Y > 0)
	{
		const FVector2D Scale(Local.X / IconViewport.X, Local.Y / IconViewport.Y);
		const FVector2D T = Highlight.Threat * Scale;
		const float Rr = Highlight.ThreatR * Scale.X;
		const float Pulse = 0.75f + 0.25f * FMath::Sin(FPlatformTime::Seconds() * 9.0);
		TArray<FVector2f> P;
		for (int32 k = 0; k <= 40; ++k)
			P.Add(FVector2f(T.X + FMath::Cos(2.f * PI * k / 40) * Rr, T.Y + FMath::Sin(2.f * PI * k / 40) * Rr * 0.38f));
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), P, ESlateDrawEffect::None, Accent * FLinearColor(1, 1, 1, Pulse), true, 3.f);
		const FVector2D C = Highlight.Carrier * Scale;
		const FSlateFontInfo F = Font(13, "Bold");
		const FVector2D M = Measure(Highlight.Move, F);
		const float Gs = 30.f;
		const FVector2D Pill(Gs + 14 + M.X + 16, 38);
		const FVector2D TL = C - FVector2D(Pill.X * 0.5, Pill.Y * 0.5);
		FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(Pill), FSlateLayoutTransform(FVector2f(TL))), White(), ESlateDrawEffect::None,
			FLinearColor(0.02f, 0.025f, 0.035f, 0.88f));
		// (a move pressed too early just ran out unused: the pill goes grey a moment, Game/TagMoveBuffer.h)
		const float Grey = FMath::Clamp(Highlight.RefusedA, 0.f, 1.f);
		const FLinearColor Bar = FMath::Lerp(Accent, FLinearColor(0.45f, 0.46f, 0.5f), Grey);
		const FLinearColor Ink = FMath::Lerp(FLinearColor::White, FLinearColor(0.6f, 0.62f, 0.66f), Grey);
		FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(Pill.X, 3.f), FSlateLayoutTransform(FVector2f(TL + FVector2D(0, Pill.Y - 3)))), White(),
			ESlateDrawEffect::None, Bar);
		PaintGlyph(Out, Layer + 3, G, TL + FVector2D(8 + Gs * 0.5, Pill.Y * 0.5), Gs, Highlight.Glyph, Highlight.Key,
			Highlight.Glyph != EPadGlyph::None && bIconPad, Ink);
		FSlateDrawElement::MakeText(Out, Layer + 6, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(TL + FVector2D(8 + Gs + 8, (Pill.Y - M.Y) * 0.5)))),
			Highlight.Move, F, ESlateDrawEffect::None, Ink);
		Layer += 8;
	}
	// receivers' button icons over their heads
	if (Icons.Num() && IconViewport.X > 0 && IconViewport.Y > 0)
	{
		const FVector2D Scale(Local.X / IconViewport.X, Local.Y / IconViewport.Y);
		static const EPadGlyph Face[5] = {EPadGlyph::Cross, EPadGlyph::Circle, EPadGlyph::Square, EPadGlyph::Triangle, EPadGlyph::R1};
		for (const FArtIcon& I : Icons)
		{
			const FVector2D C = I.Screen * Scale;
			const float Sz = 38.f; // (Madden's over-the-head buttons read at a glance; at 30 they were specks over a deep receiver)
			if (I.bPrimary || I.bPicked)
			{
				TArray<FVector2f> P;
				for (int32 k = 0; k <= 32; ++k)
					P.Add(FVector2f(C.X + FMath::Cos(2.f * PI * k / 32) * (Sz * 0.62f), C.Y + FMath::Sin(2.f * PI * k / 32) * (Sz * 0.62f)));
				FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), P, ESlateDrawEffect::None, I.bPicked ? FLinearColor::White : Accent, true, 3.f);
			}
			// (on the button the player put that receiver on: Settings > Controls, passing in the play, the pre-snap
			// offense before it)
			const TagBind::ECtx IcCtx = Director && Director->State == EMatchState::Presnap ? TagBind::ECtx::PreOff : TagBind::ECtx::Passing;
			PaintGlyph(Out, Layer, G, C, Sz, TagBind::PhysGlyph(IcCtx, Face[FMath::Clamp(I.Button, 0, 4)]), TagBind::PhysKeyLabel(IcCtx, FString::FromInt(I.Button + 1)), bIconPad, ButtonColor(I.Button));
			if (I.bHot)
			{
				const FSlateFontInfo F = Font(9, "Bold");
				const FVector2D M = Measure(TEXT("HOT"), F);
				FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(C + FVector2D(-M.X * 0.5, Sz * 0.58)))),
					FString(TEXT("HOT")), F, ESlateDrawEffect::None, Accent);
			}
		}
	}
	if (PreRows.Num() == 0 && DefPreRows.Num() == 0) return Layer + 4;
	// the pre-snap panel, bottom right (over the icons); head to head the defense's caller has his own, bottom left
	Layer += 6;
	auto PaintPanel = [&](const FString& Title, const FString& Sub, const TArray<FPreRow>& Rows, bool bPad, const FString& Who, const FLinearColor& WhoC, bool bLeft)
	{
		const float W = 480.f, RowH = 34.f, Head = (Sub.IsEmpty() ? 44.f : 62.f) + (Who.IsEmpty() ? 0.f : 6.f);
		const float SubY = Who.IsEmpty() ? 36.f : 42.f; // (below the tag chip)
		const float H = Head + Rows.Num() * RowH + 12.f;
		const FVector2D TL(bLeft ? 30.f : Local.X - W - 30.f, Local.Y - H - 128.f);
		TagTheme::PaintPanel(Out, Layer - 3, G, TL, FVector2D(W, H), Panel, 7.f);
		TagTheme::Box(Out, Layer + 1, G, TL + FVector2D(0, 6), FVector2D(4.f, H - 12), Who.IsEmpty() ? Accent : WhoC, 2.f);
		float TitleX = 20.f;
		if (!Who.IsEmpty())
		{
			// whose panel it is: his tag on a chip in his colour
			const FSlateFontInfo WF = Font(12, "Bold");
			const FVector2D M = Measure(Who, WF);
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(M.X + 14.f, 24.f), FSlateLayoutTransform(FVector2f(TL + FVector2D(18, 11)))), White(),
				ESlateDrawEffect::None, WhoC);
			FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(TL + FVector2D(25, 14)))), Who, WF,
				ESlateDrawEffect::None, FLinearColor(0.03f, 0.04f, 0.06f));
			TitleX += M.X + 24.f;
		}
		FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W - 30 - TitleX, 24), FSlateLayoutTransform(FVector2f(TL + FVector2D(TitleX, 12)))), Title,
			Font(15, "Bold"), ESlateDrawEffect::None, Accent);
		if (!Sub.IsEmpty())
			FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W - 30, 18), FSlateLayoutTransform(FVector2f(TL + FVector2D(20, SubY)))), Sub,
				Font(11), ESlateDrawEffect::None, Dim);
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const FPreRow& Rw = Rows[i];
			const FVector2D RowTL = TL + FVector2D(8, Head + i * RowH);
			if (Rw.bOn)
				TagTheme::Gradient(Out, Layer + 1, G, RowTL, FVector2D(W - 16, RowH - 4), FLinearColor(1.f, 0.83f, 0.f, 0.22f), FLinearColor(1.f, 0.83f, 0.f, 0.04f), true, 4.f);
			// (a wide keycap - "L-STICK" - moves its label over instead of running past the panel's edge into it)
			const float GW = static_cast<float>(GlyphExtent(Rw.Glyph, Rw.Key, 26.f, bPad && Rw.Glyph != EPadGlyph::None).X);
			const float GX = FMath::Max(30.f, 10.f + GW * 0.5f), LabelX = FMath::Max(64.f, GX + GW * 0.5f + 10.f);
			PaintGlyph(Out, Layer + 1, G, RowTL + FVector2D(GX, (RowH - 4) * 0.5f), 26.f, Rw.Glyph, Rw.Key, bPad, Rw.Tint);
			FSlateDrawElement::MakeText(Out, Layer + 4, G.ToPaintGeometry(FVector2f(W - 26 - LabelX, RowH), FSlateLayoutTransform(FVector2f(RowTL + FVector2D(LabelX, 6)))), Rw.Label,
				Font(13, Rw.bOn ? "Bold" : "Regular"), ESlateDrawEffect::None, Rw.bOn ? Accent : FLinearColor(0.9f, 0.92f, 0.95f));
		}
	};
	if (PreRows.Num() > 0) PaintPanel(PreTitle, PreSub, PreRows, bPrePad, PreWho, PreWhoColor, false);
	if (DefPreRows.Num() > 0) PaintPanel(DefPreTitle, DefPreSub, DefPreRows, bDefPrePad, DefPreWho, DefPreWhoColor, true);
	return Layer + 6;
}

void SGridironOverlay::Banner(const FString& Text, const FString& Sub, const FLinearColor& Color)
{
	BannerText = Text;
	BannerSub = Sub;
	BannerColor = Color;
	BannerT = 0.f;
	ATagAudio::Ui(TEXT("UiWhoosh"), 0.55f);
}

void SGridironOverlay::Toast(const FString& Text)
{
	ToastText = Text;
	ToastT = 0.f;
	ATagAudio::Ui(TEXT("UiToast"), 0.7f);
}

TSharedRef<SWidget> SGridironOverlay::MakeButton(const FString& Label, const FString& Sub, std::function<void()> OnClick, float Width)
{
	// a menu tile (UI/TagTheme.h): "Name: Value" labels show their value on the right
	FString L = Label, V;
	if (Label.Split(TEXT(": "), &L, &V) && V.Len() > 18) { L = Label; V.Reset(); }
	return SNew(SBox).Padding(FMargin(0, 3))
	[
		SNew(TagTheme::STile).Label(L).Value(V).Sub(Sub).Width(Width).LabelSize(Sub.IsEmpty() ? 19 : 18).OnClick(OnClick)
	];
}

TSharedRef<SWidget> SGridironOverlay::MakeScoreBug()
{
	auto TeamCell = [this](tag::TeamKey K) -> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(White()).Padding(FMargin(16, 6))
			.BorderBackgroundColor_Lambda([this, K] { return Director && Director->Game ? FSlateColor(Hex(Director->Game->teamOf(K).colors.primary)) : FSlateColor(Panel); })
			[
				SNew(SBox).WidthOverride(150)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(20, "Bold")).ShadowOffset(FVector2D(1, 1)).Text_Lambda([this, K] { return Director && Director->Game ? T(S(Director->Game->teamOf(K).abbr)) : FText(); }) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
					[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Accent).Text_Lambda([this, K] { return Director && Director->Game && Director->Game->poss == K && !Director->Game->isFinal() ? T(TEXT("●")) : FText(); }) ]
					+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(28, "Bold")).ShadowOffset(FVector2D(1, 1)).Text_Lambda([this, K] { return Director && Director->Game ? FText::AsNumber(Director->ScoreShown(K)) : FText(); }) ]
				]
			];
	};
	return SNew(SHorizontalBox)
		.Visibility_Lambda([this] { return bHud ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SHorizontalBox::Slot().AutoWidth()[ TeamCell(tag::TeamKey::Away) ]
		+ SHorizontalBox::Slot().AutoWidth()[ TeamCell(tag::TeamKey::Home) ]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(FMargin(18, 4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(Font(22, "Bold")).Text_Lambda([this] { return ClockText(); }) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text_Lambda([this] { if (!Director || !Director->Game) return FText(); const int Q = Director->Game->q; return T(Q <= 4 ? FString::Printf(TEXT("Q%d"), Q) : FString(TEXT("OT"))); }) ]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBorder).BorderImage(White()).Padding(FMargin(18, 4))
			.BorderBackgroundColor_Lambda([this] { return FSlateColor(bFlag ? FLinearColor(1.f, 0.69f, 0.f) : Accent); })
			[
				SNew(SBox).MinDesiredWidth(190).VAlign(VAlign_Center).HAlign(HAlign_Center)
				[ SNew(STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(FLinearColor(0.05f, 0.05f, 0.05f)).Text_Lambda([this] { return DownText(); }) ]
			]
		]
		// the play clock (40 s from the whistle), as broadcasts show it beside the down: red in its last 5 seconds
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBorder).BorderImage(White()).Padding(FMargin(10, 4)).BorderBackgroundColor(FLinearColor(0.07f, 0.08f, 0.1f))
			.Visibility_Lambda([this] { return Director && Director->PlayClockShown() >= 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SBox).MinDesiredWidth(44).VAlign(VAlign_Center).HAlign(HAlign_Center)
				[ SNew(STextBlock).Font(Font(16, "Bold"))
					.ColorAndOpacity_Lambda([this] { return FSlateColor(Director && Director->PlayClockShown() >= 0 && Director->PlayClockShown() <= 5 ? FLinearColor(1.f, 0.25f, 0.2f) : FLinearColor::White); })
					.Text_Lambda([this] { return Director ? FText::FromString(FString::Printf(TEXT(":%02d"), FMath::Max(0, Director->PlayClockShown()))) : FText(); }) ]
			]
		];
}

FText SGridironOverlay::ClockText() const
{
	if (!Director || !Director->Game) return FText();
	return T(S(tag::fmtClock(FMath::Max(0.0, Director->Game->clock + Director->HudRunoffLeft - Director->HudPreRun - ClockOffset))));
}

FText SGridironOverlay::DownText() const
{
	if (!Director || !Director->Game) return FText();
	const tag::Game& G = *Director->Game;
	if (!Director->HudDown.IsEmpty() && G.phase != tag::GamePhase::Final) return T(Director->HudDown);
	FString Word;
	Director->ScoreShown(tag::TeamKey::Home, &Word);
	if (!Word.IsEmpty()) return T(Word);
	switch (G.phase)
	{
	case tag::GamePhase::Final: return T(TEXT("FINAL"));
	case tag::GamePhase::Kickoff: return T(TEXT("KICKOFF"));
	case tag::GamePhase::Pat: return T(TEXT("TRY"));
	default: return T(S(G.downText()) + TEXT("  ·  ") + S(G.ydLine()));
	}
}

FText SGridironOverlay::TickerText() const
{
	if (!Director || !Director->Game) return FText();
	// a broadcast shows the play-by-play between plays, never over a live snap or a kick (a canned kick's result is
	// already in the log before it is animated)
	const EMatchState St = Director->State;
	if (St == EMatchState::Presnap || St == EMatchState::Live || St == EMatchState::Kick || St == EMatchState::Replay) return FText();
	const auto& Log = Director->Game->log;
	FString Out;
	for (int i = static_cast<int>(Log.size()) - 1, n = 0; i >= 0 && n < 2; --i, ++n)
	{
		if (n) Out += TEXT("\n");
		Out += S(Log[i].desc);
	}
	return T(Out);
}

FText SGridironOverlay::WeatherText() const
{
	if (!Director || !Director->Game) return FText();
	return T(S(Director->Game->weatherText()));
}

// ------------------------------------------------------------------ screens

void SGridironOverlay::ShowMainMenu()
{
	bHud = false;
	bSelMulti = false;
	ATagAudio::Screen(TEXT("Main"));
	// the menu (Madden's front end): the title and the tiles on the left over a darkened stadium, a card on the right
	// that tells what the tile under the cursor is, the button bar at the foot
	struct FItem { FString Label, Kicker, Desc; TArray<FString> Points; std::function<void()> Go; bool bGapBefore = false; };
	const TSharedRef<TArray<FItem>> Items = MakeShared<TArray<FItem>>();
	Items->Reserve(16); // (a TArray moves its items bytewise when it grows, which a std::function does not survive)
	Items->Add({TEXT("Play Now"), TEXT("Exhibition"), TEXT("You against the CPU. Call every play, then take the quarterback, the ball carrier or any man on defense."),
		{TEXT("Madden-style play call: formations, families, coach's pick"), TEXT("Hot routes, audibles, line shifts before the snap"), TEXT("Hit stick, truck, spin, stiff arm and the kick meter")},
		[this] { ShowTeamSelect(true); }});
	Items->Add({TEXT("Multiplayer"), TEXT("Couch play"), TEXT("Up to four players on one screen, pads and keyboard together. Pick sides, share a team and choose who calls the plays."),
		{TEXT("Every pad and the keyboard read on their own"), TEXT("Head to head or co-op against the CPU"), TEXT("Each player keeps his own man, marker and sprint")},
		[this] { if (Director) Director->ShowLobby(); }});
	Items->Add({TEXT("Watch a Game"), TEXT("Broadcast"), TEXT("CPU against CPU, shown the way TV shows it: the sideline camera, the booth, the replays. Speed it up with 1 / 2 / 4."),
		{TEXT("Two-man booth with live commentary"), TEXT("TV game camera and replays"), TEXT("Every team, every scheme")},
		[this] { ShowTeamSelect(false); }});
	Items->Add({TEXT("Superstars & X-Factors"), TEXT("Team"), TEXT("Pick the abilities your stars carry into the game. X-Factors earn the zone in the game and keep it until they are knocked out."),
		{TEXT("Graded by their standing at their position"), TEXT("Slots and ability points by grade"), TEXT("Loadouts saved for every game")},
		[this] { ShowAbilities(SelHome, [this] { ShowMainMenu(); }); }, true});
	Items->Add({TEXT("Depth Chart"), TEXT("Team"), TEXT("Every slot on every team: starters, specialists and situational subs. Your order is the one the game plays."),
		{TEXT("34 slots a team"), TEXT("Coach's order wherever you leave a slot alone"), TEXT("Change it at the pause menu mid-game")},
		[this] { ShowDepthChart(SelHome, [this] { ShowMainMenu(); }); }});
	Items->Add({TEXT("Create a Player"), TEXT("Team"), TEXT("Build a player at any position, from his archetype to every rating, and sign him to any team."),
		{TEXT("Archetypes for every position"), TEXT("A 3D turntable of the man you build"), TEXT("He plays, grades and saves like anyone")},
		[this] { ShowCreatePlayer(SelHome, [this] { ShowMainMenu(); }); }});
	Items->Add({TEXT("Controls"), TEXT("Options"), TEXT("Every button on offense, on defense and before the snap, shown on the controller you have plugged in. Put any action on any button."),
		{TEXT("A DualSense or an Xbox pad, drawn as it is in your hands"), TEXT("Your own layout for each part of the game, keyboard too"), TEXT("The game's prompts name the buttons you chose")},
		[this] { ShowControls([this] { ShowMainMenu(); }); }, true});
	Items->Add({TEXT("Settings"), TEXT("Options"), TEXT("Commentary captions, the right stick's up and down, and the volume of the music and the stadium."),
		{}, [this] { ShowSettings([this] { ShowMainMenu(); }); }});
	Items->Add({TEXT("Quit"), TEXT(""), TEXT("Back to the desktop."), {}, [] { FPlatformMisc::RequestExit(false); }});

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (const FItem& It : *Items)
	{
		if (It.bGapBefore) List->AddSlot().AutoHeight()[ SNew(SBox).HeightOverride(14) ];
		const std::function<void()> Go = It.Go;
		List->AddSlot().AutoHeight().Padding(0, 3)[ SNew(TagTheme::STile).Label(It.Label).Width(440).Height(50).LabelSize(21).OnClick(Go) ];
	}
	// the card: what the tile under the cursor is (it fades over when the cursor moves)
	MenuCardIdx = -1;
	auto CardIdx = [this, Items]() { const int32 I = FocusedIndex(); return Items->IsValidIndex(I) ? I : 0; };
	auto CardFade = [this, CardIdx]()
	{
		const int32 I = CardIdx();
		if (I != MenuCardIdx) { MenuCardIdx = I; MenuCardT = TagTheme::Now(); }
		return TagTheme::EaseOut(static_cast<float>((TagTheme::Now() - MenuCardT) / 0.2));
	};
	TSharedRef<SVerticalBox> Points = SNew(SVerticalBox);
	for (int32 p = 0; p < 3; ++p)
		Points->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([Items, CardIdx, p] { return (*Items)[CardIdx()].Points.IsValidIndex(p) ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
			[ SNew(SBox).WidthOverride(8).HeightOverride(8)[ SNew(SBorder).BorderImage(TagTheme::Round(4.f)).BorderBackgroundColor(TagTheme::Gold) ] ]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Font(14)).AutoWrapText(true)
				.ColorAndOpacity_Lambda([CardFade] { return FSlateColor(FLinearColor(0.86f, 0.88f, 0.92f, CardFade())); })
				.Text_Lambda([Items, CardIdx, p] { const FItem& It = (*Items)[CardIdx()]; return T(It.Points.IsValidIndex(p) ? It.Points[p] : FString()); })
			]
		];
	TSharedRef<SWidget> Card = SNew(SBox).WidthOverride(560)
	[
		SNew(TagTheme::SPanel).Fill(FLinearColor(0.02f, 0.026f, 0.044f, 0.86f)).AccentTop(TagTheme::Gold).Padding(FMargin(34, 30, 34, 32))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 260))
				.ColorAndOpacity_Lambda([CardFade] { return FSlateColor(TagTheme::Gold * FLinearColor(1, 1, 1, CardFade())); })
				.Text_Lambda([Items, CardIdx] { return T((*Items)[CardIdx()].Kicker.ToUpper()); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 10)
			[
				SNew(SBox).Padding_Lambda([CardFade] { return FMargin(14.f * (1.f - CardFade()), 0, 0, 0); })
				[
					SNew(STextBlock).Font(TagTheme::Font(40, "Black", 10))
					.ColorAndOpacity_Lambda([CardFade] { return FSlateColor(FLinearColor(1, 1, 1, CardFade())); })
					.Text_Lambda([Items, CardIdx] { return T((*Items)[CardIdx()].Label.ToUpper()); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(Font(16)).AutoWrapText(true).LineHeightPercentage(1.1f)
				.ColorAndOpacity_Lambda([CardFade] { return FSlateColor(FLinearColor(0.93f, 0.94f, 0.97f, CardFade())); })
				.Text_Lambda([Items, CardIdx] { return T((*Items)[CardIdx()].Desc); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[ Points ]
		]
	];
	// the title lockup
	TSharedRef<SWidget> Lockup = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 0, 0, 0)
		[ SNew(STextBlock).Font(TagTheme::Font(14, "BoldCondensed", 420)).ColorAndOpacity(TagTheme::Gold).Text(T(TEXT("SIMULATION-FIRST PRO FOOTBALL"))) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, -6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[ SNew(STextBlock).Font(TagTheme::Font(92, "Black", -10)).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(0, 4)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f)).Text(T(TEXT("TAG "))) ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ SNew(STextBlock).Font(TagTheme::Font(92, "Black", -10)).ColorAndOpacity(TagTheme::Gold).ShadowOffset(FVector2D(0, 4)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.5f)).Text(T(TEXT("GRIDIRON"))) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(4, 2, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(96).HeightOverride(5)[ SNew(SBorder).BorderImage(TagTheme::Round(2.f)).BorderBackgroundColor(TagTheme::Gold) ] ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[ SNew(SBox).WidthOverride(28).HeightOverride(5)[ SNew(SBorder).BorderImage(TagTheme::Round(2.f)).BorderBackgroundColor(TagTheme::Gold * FLinearColor(1, 1, 1, 0.45f)) ] ]
		];
	// the button bar
	TSharedRef<SWidget> Footer = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.006f, 0.008f, 0.014f, 0.86f)).Padding(FMargin(110, 14, 110, 14))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ HintItem(EPadGlyph::DUpDown, TEXT("↑↓"), TEXT("Move")) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ HintItem(EPadGlyph::Cross, TEXT("Enter"), TEXT("Select")) ]
		+ SHorizontalBox::Slot().FillWidth(1)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(TagTheme::Faint).Text(T(TEXT("An original football game. All teams, players and names are fictional."))) ]
	];
	ShowModal(SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(TagTheme::SBackdrop).LeftWeighted(true) ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Fill).Padding(FMargin(110, 84, 0, 96))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Lockup ]
			+ SVerticalBox::Slot().FillHeight(1).VAlign(VAlign_Center).Padding(0, 24, 0, 0)[ List ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(FMargin(0, 120, 110, 40))[ Card ]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)[ Footer ]);
	// -TagTeamSelect: straight on to the Play Now team select, once (screen checks)
	static bool bTeamSelectShot = FParse::Param(FCommandLine::Get(), TEXT("TagTeamSelect"));
	if (bTeamSelectShot)
	{
		bTeamSelectShot = false;
		ShowTeamSelect(true);
	}
	// -TagInputTest: every press sent twice, as an Xbox pad's comes, must move the menus once (UI/InputTest.cpp)
	static bool bUiShots = FParse::Param(FCommandLine::Get(), TEXT("TagUiShots"));
	if (bUiShots)
	{
		bUiShots = false;
		StartUiShots();
	}
	// -TagCallTour: the play-call screens walked and pictured (families, formations, type tabs, pages; the defense's;
	// the team select's signature play), then the game exits (UI/InputTest.cpp)
	static bool bCallTour = FParse::Param(FCommandLine::Get(), TEXT("TagCallTour"));
	if (bCallTour)
	{
		bCallTour = false;
		StartCallTour();
	}
	// -TagControlsTest: the controls screen and a remapped button in a game (UI/ControlsScreen.cpp)
	static bool bControlsTest = FParse::Param(FCommandLine::Get(), TEXT("TagControlsTest"));
	if (bControlsTest)
	{
		bControlsTest = false;
		StartControlsTest();
	}
	static bool bMenuAudio = FParse::Param(FCommandLine::Get(), TEXT("TagMenuAudio"));
	if (bMenuAudio)
	{
		bMenuAudio = false;
		StartMenuAudioTest();
	}
	static bool bInputTest = FParse::Param(FCommandLine::Get(), TEXT("TagInputTest"));
	if (bInputTest)
	{
		bInputTest = false;
		StartInputTest();
	}
	// -TagRosterShots: the depth chart and Create-a-Player, scripted (pictures, nothing saved), then the game exits
	static bool bRosterShot = FParse::Param(FCommandLine::Get(), TEXT("TagRosterShots"));
	if (bRosterShot)
	{
		bRosterShot = false;
		StartRosterShots();
	}
	// -TagAbilityShots: straight on to the Superstars & X-Factors screen (its own scripted shots, then the game exits)
	static bool bAbilityShot = FParse::Param(FCommandLine::Get(), TEXT("TagAbilityShots"));
	if (bAbilityShot)
	{
		bAbilityShot = false;
		ShowAbilities(SelHome, [this] { ShowMainMenu(); });
	}
}

// ShowTeamSelect: UI/TeamSelect.cpp

namespace
{
	/** The play call's root: the one focusable widget on the screen, so every pad button comes to the call (the cards are
	 *  called by their buttons, not by moving a focus frame over them). Keys it does not use (OPTIONS, Esc) go on to the
	 *  game, which pauses. */
	class SCallRoot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCallRoot) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(std::function<bool(const FKey&, bool)>, OnKey)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			OnKey = A._OnKey;
			ChildSlot[A._Content.Widget];
		}
		virtual bool SupportsKeyboardFocus() const override { return true; }
		virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
		{
			return OnKey && OnKey(E.GetKey(), E.IsRepeat()) ? FReply::Handled() : FReply::Unhandled();
		}
		virtual FNavigationReply OnNavigation(const FGeometry&, const FNavigationEvent& E) override
		{
			// the stick pages like the d-pad; nothing else moves a focus
			if (OnKey && E.GetNavigationType() == EUINavigation::Left) OnKey(EKeys::Gamepad_DPad_Left, false);
			if (OnKey && E.GetNavigationType() == EUINavigation::Right) OnKey(EKeys::Gamepad_DPad_Right, false);
			return FNavigationReply::Stop();
		}
	private:
		std::function<bool(const FKey&, bool)> OnKey;
	};

	/** A controller button (or, for keyboard play, its key) as a widget, drawn by the overlay's glyph painter. */
	class SCallGlyph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCallGlyph) : _Owner(nullptr), _Glyph(EPadGlyph::None), _Size(30.f), _Tint(FLinearColor::White) {}
			SLATE_ARGUMENT(const SGridironOverlay*, Owner)
			SLATE_ARGUMENT(EPadGlyph, Glyph)
			SLATE_ARGUMENT(FString, Key)
			SLATE_ARGUMENT(float, Size)
			SLATE_ARGUMENT(FLinearColor, Tint)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Owner = A._Owner;
			Glyph = A._Glyph;
			Key = A._Key;
			Size = A._Size;
			Tint = A._Tint;
		}
		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return Owner ? Owner->GlyphExtent(Glyph, Key, Size, Owner->PadGlyphs()) : FVector2D(Size + 4, Size + 4);
		}
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			if (Owner) Owner->PaintGlyph(Out, Layer, G, FVector2D(G.GetLocalSize()) * 0.5, Size, Glyph, Key, Owner->PadGlyphs(), Tint);
			return Layer + 4;
		}
	private:
		const SGridironOverlay* Owner = nullptr;
		EPadGlyph Glyph = EPadGlyph::None;
		FString Key;
		float Size = 30.f;
		FLinearColor Tint = FLinearColor::White;
	};

	const EPadGlyph CallButtons[3] = {EPadGlyph::Square, EPadGlyph::Cross, EPadGlyph::Circle};
	const TCHAR* CallKeys[3] = {TEXT("1"), TEXT("2"), TEXT("3")};

	// ---- what the call screen says about a play: its type tab (Madden's "Play Type"), its concept line
	/** A string field the playbook may carry on its plays (the concept a play was built from, its category): read when
	 *  it is there, so the screen picks up the playbook's own words without waiting on them. */
	template <class TPlay>
	FString PlayConceptField(const TPlay& P)
	{
		if constexpr (requires { { P.conceptName } -> std::convertible_to<std::string>; }) return FString(UTF8_TO_TCHAR(std::string(P.conceptName).c_str()));
		else if constexpr (requires { { P.concept_ } -> std::convertible_to<std::string>; }) return FString(UTF8_TO_TCHAR(std::string(P.concept_).c_str()));
		else if constexpr (requires { { P.conceptId } -> std::convertible_to<std::string>; }) return FString(UTF8_TO_TCHAR(std::string(P.conceptId).c_str()));
		else return FString();
	}
	template <class TPlay>
	FString PlayCategoryField(const TPlay& P)
	{
		if constexpr (requires { { P.category } -> std::convertible_to<std::string>; }) return FString(UTF8_TO_TCHAR(std::string(P.category).c_str())).ToLower();
		else return FString();
	}

	/** An offensive play's type tab: "RUN", "PASS", "PA", "SCREEN", "RPO", "TRICK". The playbook's own words for it
	 *  first (a category, a "trick" / "rpo" / "screen" / "playaction" tag, the play type's name), then its type. */
	FString OffCategory(const tag::OffPlay& P)
	{
		// the playbook's own kind of play (OffPlay::cat); its default, Pass, is checked against the play below (a
		// book built before the kinds were set reads every play as a pass)
		switch (P.cat)
		{
		case tag::PlayCat::RPO: return TEXT("RPO");
		case tag::PlayCat::Trick: return TEXT("TRICK");
		case tag::PlayCat::Screen: return TEXT("SCREEN");
		case tag::PlayCat::PlayAction: return TEXT("PA");
		case tag::PlayCat::Run: return TEXT("RUN");
		default: break;
		}
		const FString Type = FString(UTF8_TO_TCHAR(tag::playTypeName(P.type))).ToLower() + TEXT(" ") + PlayCategoryField(P);
		const FString Id = FString(UTF8_TO_TCHAR(P.id.c_str())).ToLower();
		if (P.hasTag("trick") || Type.Contains(TEXT("trick")) || Id.Contains(TEXT("trick")) || Id.Contains(TEXT("flea")) || Id.Contains(TEXT("reverse"))
			|| Id.Contains(TEXT("philly")) || Id.Contains(TEXT("hbpass"))) return TEXT("TRICK");
		if (P.hasTag("rpo") || Type.Contains(TEXT("rpo")) || Id.Contains(TEXT("rpo"))) return TEXT("RPO");
		if (P.type == tag::PlayType::Screen || P.hasTag("screen") || Type.Contains(TEXT("screen"))) return TEXT("SCREEN");
		if (P.type == tag::PlayType::PlayAction || P.hasTag("playaction") || Type.Contains(TEXT("playaction")) || Type.Contains(TEXT("play action"))) return TEXT("PA");
		if (P.type == tag::PlayType::Run || P.run.has_value() || Type.Contains(TEXT("run")) || Type.Contains(TEXT("option"))) return TEXT("RUN");
		return TEXT("PASS");
	}

	/** The concept line on an offensive card ("INSIDE ZONE", "QUICK GAME", "SHOT"): the playbook's concept when it names
	 *  one the play's name does not already say, else the run's scheme or the pass's depth. */
	FString OffConcept(const tag::OffPlay& P, const FString& Cat)
	{
		const FString Named = PlayConceptField(P);
		if (!Named.IsEmpty() && !Named.Equals(FString(UTF8_TO_TCHAR(P.name.c_str())), ESearchCase::IgnoreCase)) return Named;
		FString Out;
		if (P.run && (Cat == TEXT("RUN") || Cat == TEXT("RPO")))
		{
			const double Wide = FMath::Abs(P.run->aimDy);
			switch (P.run->style)
			{
			case tag::RunStyle::Zone: Out = Wide >= 3.0 ? TEXT("Outside zone") : TEXT("Inside zone"); break;
			case tag::RunStyle::Gap: Out = TEXT("Gap scheme"); break;
			case tag::RunStyle::Stretch: Out = TEXT("Stretch"); break;
			case tag::RunStyle::Draw: Out = TEXT("Draw"); break;
			case tag::RunStyle::Sneak: Out = TEXT("Sneak"); break;
			default: break;
			}
			const FString Car = FString(UTF8_TO_TCHAR(P.run->carrier.c_str()));
			if (!Car.IsEmpty() && Car != TEXT("RB") && Car != TEXT("HB")) Out += TEXT("  -  ") + Car;
		}
		else if (Cat == TEXT("SCREEN")) Out = TEXT("Screen");
		else
		{
			if (P.hasTag("deep")) Out = TEXT("Shot");
			else if (P.hasTag("quick")) Out = TEXT("Quick game");
			else if (P.hasTag("mid")) Out = TEXT("Intermediate");
			else if (P.hasTag("short")) Out = TEXT("Short");
			if (Cat == TEXT("PA") && FString(UTF8_TO_TCHAR(P.name.c_str())).Contains(TEXT("Boot"))) Out = TEXT("Boot") + FString(Out.IsEmpty() ? TEXT("") : TEXT("  -  ")) + Out;
		}
		if (P.hasTag("goalline")) Out += FString(Out.IsEmpty() ? TEXT("") : TEXT("  -  ")) + TEXT("Short yardage");
		return Out;
	}

	bool ManCoverage(tag::Coverage C)
	{
		const FString N = FString(UTF8_TO_TCHAR(tag::coverageName(C))).ToLower();
		return N == TEXT("c0") || N == TEXT("c1") || N == TEXT("c2m");
	}
	/** A defensive call's type tab: "BLITZ" (a man blitz, a DB blitz, a mug; five or more at the snap), "PRESSURE" (a
	 *  zone blitz or a simulated pressure), "PREVENT", else its coverage's "MAN" or "ZONE". The playbook's own word for
	 *  its pressure (DefCall::pressure) first. */
	FString DefCategory(const tag::DefCall& D)
	{
		const FString Pr = FString(UTF8_TO_TCHAR(D.pressure.c_str())).ToLower();
		if (Pr.Contains(TEXT("prevent")) || D.tech == tag::CoverTech::Prevent) return TEXT("PREVENT");
		if (Pr.Contains(TEXT("zone blitz")) || Pr.Contains(TEXT("sim"))) return TEXT("PRESSURE");
		if (Pr.Contains(TEXT("blitz")) || Pr.Contains(TEXT("mug"))) return TEXT("BLITZ");
		if (Pr.IsEmpty() && D.rushers() >= 5) return D.coverage == tag::Coverage::C3Fire ? TEXT("PRESSURE") : TEXT("BLITZ");
		return ManCoverage(D.coverage) ? TEXT("MAN") : TEXT("ZONE");
	}
	FString CoverageShort(tag::Coverage C)
	{
		static const TMap<FString, FString> Names = {
			{TEXT("c0"), TEXT("Cover 0")}, {TEXT("c1"), TEXT("Cover 1")}, {TEXT("c2"), TEXT("Cover 2")}, {TEXT("c2m"), TEXT("2-Man")},
			{TEXT("c3"), TEXT("Cover 3")}, {TEXT("c3fire"), TEXT("Fire zone")}, {TEXT("c4"), TEXT("Quarters")}, {TEXT("c2t"), TEXT("Tampa 2")}, {TEXT("c6"), TEXT("Cover 6")}};
		const FString* N = Names.Find(FString(UTF8_TO_TCHAR(tag::coverageName(C))).ToLower());
		return N ? *N : FString(UTF8_TO_TCHAR(tag::coverageName(C))).ToUpper();
	}

	/** A defensive call's name without its set ("3-4 Odd Cover 1 Robber" in the 3-4 Odd set: "Cover 1 Robber"): the
	 *  header and the set list already say the set. */
	FString DefShortName(const tag::DefCall& D)
	{
		const FString Name = FString(UTF8_TO_TCHAR(D.name.c_str()));
		for (const std::string& Pre : {tag::defSet(D), D.frontLabel, std::string(tag::frontFamily(D.front))})
		{
			const FString P = FString(UTF8_TO_TCHAR(Pre.c_str())) + TEXT(" ");
			if (P.Len() > 1 && Name.StartsWith(P) && Name.Len() > P.Len()) return Name.RightChop(P.Len());
		}
		return Name;
	}

	/** A type tab's colour (its chip, the card's type badge). */
	FLinearColor CatColor(const FString& C)
	{
		if (C == TEXT("RUN")) return FLinearColor(0.30f, 0.85f, 0.42f);
		if (C == TEXT("PASS")) return FLinearColor(0.38f, 0.64f, 1.f);
		if (C == TEXT("PA")) return FLinearColor(0.74f, 0.50f, 1.f);
		if (C == TEXT("SCREEN")) return FLinearColor(0.20f, 0.85f, 0.88f);
		if (C == TEXT("RPO")) return FLinearColor(1.f, 0.60f, 0.20f);
		if (C == TEXT("TRICK")) return FLinearColor(1.f, 0.38f, 0.64f);
		if (C == TEXT("ZONE")) return FLinearColor(0.34f, 0.62f, 1.f);
		if (C == TEXT("MAN")) return FLinearColor(1.f, 0.42f, 0.40f);
		if (C == TEXT("BLITZ")) return FLinearColor(1.f, 0.56f, 0.14f);
		if (C == TEXT("PRESSURE")) return FLinearColor(1.f, 0.80f, 0.22f);
		if (C == TEXT("PREVENT")) return FLinearColor(0.62f, 0.72f, 0.84f);
		return FLinearColor(0.6f, 0.65f, 0.72f);
	}
	FString CatLabel(const FString& C)
	{
		if (C.IsEmpty()) return TEXT("ALL");
		if (C == TEXT("PA")) return TEXT("PLAY ACTION");
		if (C == TEXT("PRESSURE")) return TEXT("ZONE PRESSURE");
		return C;
	}

	/** "11 PERSONNEL  ·  1 RB  1 TE  3 WR": the formation's men by position (its slots' first choices). */
	FString PersonnelText(const tag::Formation& F)
	{
		int32 Rb = 0, Te = 0, Wr = 0;
		for (const tag::FormationSlot& Sl : F.slots)
		{
			if (Sl.pos.empty()) continue;
			const tag::Pos Ps = Sl.pos.front();
			if (Ps == tag::Pos::RB || Ps == tag::Pos::FB) ++Rb;
			else if (Ps == tag::Pos::TE) ++Te;
			else if (Ps == tag::Pos::WR) ++Wr;
		}
		return FString::Printf(TEXT("%s PERSONNEL  ·  %d RB  %d TE  %d WR"), UTF8_TO_TCHAR(F.personnel.c_str()), Rb, Te, Wr);
	}
}

/** The call screen's card pictures by play and call (the ball does not move while the screen is up). */
struct FCallPicCache
{
	TMap<FString, FArtPicture> Pics;
};

bool SGridironOverlay::PadGlyphs() const { return Director && Director->PadUI(); }

TSharedRef<SWidget> SGridironOverlay::GlyphWidget(EPadGlyph Gl, const FString& Key, float Size, const FLinearColor& Tint) const
{
	return SNew(SCallGlyph).Owner(this).Glyph(Gl).Key(Key).Size(Size).Tint(Tint);
}

TSharedRef<SWidget> SGridironOverlay::HintItem(EPadGlyph Gl, const FString& Key, const FString& What, float Size) const
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ GlyphWidget(Gl, Key, Size) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 20, 0)
		[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 30)).ColorAndOpacity(FLinearColor(0.88f, 0.9f, 0.94f)).Text(T(What.ToUpper())) ];
}

void SGridironOverlay::ShowPlayCall(bool bOffense, bool bFourthDown, bool bTwoPoint, std::function<void(FCallChoice)> Done)
{
	bCallOffense = bOffense;
	bCallFourth = bFourthDown;
	bCallTwo = bTwoPoint;
	CallDone = Done;
	CallPage = 0;
	CallPics = MakeShared<FCallPicCache>(); // (a new snap: the ball has moved)
	CallTabs.Reset();
	const tag::Game& G = *Director->Game;
	auto AddTab = [this](const FString& Id, const FString& Label)
	{
		if (!CallTabs.ContainsByPredicate([&Id](const TPair<FString, FString>& Tb) { return Tb.Key == Id; })) CallTabs.Emplace(Id, Label);
	};
	if (bOffense)
	{
		// the team's own playbook by formation family; fourth down: the kicks first (and open on them); the clock
		// plays at the end
		if (bFourthDown) CallTabs.Emplace(TEXT("special"), TEXT("SPECIAL TEAMS"));
		for (const std::string& Fid : tag::offBookFor(G.offense()).formations)
			if (const tag::Formation* F = tag::formationById(Fid))
			{
				const FString Fam = S(tag::formationFamily(*F));
				AddTab(Fam, Fam.ToUpper());
			}
		if (!bTwoPoint) CallTabs.Emplace(TEXT("clock"), TEXT("CLOCK"));
	}
	else
	{
		for (const tag::DefCall* D : tag::defBookFor(G.defense()).calls)
		{
			const FString Fam = S(tag::frontFamily(D->front));
			AddTab(Fam, Fam.ToUpper());
		}
	}
	const bool bKnown = CallTabs.ContainsByPredicate([this](const TPair<FString, FString>& Tb) { return Tb.Key == CallTab; });
	if (bFourthDown && bOffense) CallTab = TEXT("special");
	else if (!bKnown || CallTab == TEXT("special") || CallTab == TEXT("clock"))
	{
		// defense opens on its nickel (the most common call), the offense on its first family
		CallTab = CallTabs.Num() ? CallTabs[0].Key : FString();
		if (!bOffense && CallTabs.ContainsByPredicate([](const TPair<FString, FString>& Tb) { return Tb.Key == TEXT("Nickel"); })) CallTab = TEXT("Nickel");
	}
	BuildPlayCall();
}

void SGridironOverlay::CallPick(FCallChoice C)
{
	std::function<void(FCallChoice)> Done = CallDone;
	CallDone = nullptr;
	ATagAudio::Ui(TEXT("UiSelect"));
	// what this side called (R3 on its next call screen calls it again)
	if (C.Kind == FCallChoice::Off && C.Play && C.Play->type != tag::PlayType::Kneel && C.Play->type != tag::PlayType::Spike) CallLastOff = C.Play;
	if (C.Kind == FCallChoice::Def && C.Call) CallLastDef = C.Call;
	CallRoot.Reset();
	CallPics.Reset();
	ClearModal();
	if (Done) Done(C);
}

FCallChoice SGridironOverlay::CallRepeat() const
{
	FCallChoice C; // (Auto: nothing to repeat)
	if (!Director || !Director->Game) return C;
	const tag::Game& G = *Director->Game;
	if (bCallOffense)
	{
		// this screen's last offensive call, else the director's (the hurry-up's) - if it is in the book of the team
		// with the ball (a multiplayer game: the other side's offense called last)
		const tag::OffBook& Book = tag::offBookFor(G.offense());
		const tag::OffPlay* P = CallLastOff && Book.has(CallLastOff) ? CallLastOff : Director->LastUserPlay;
		if (P && Book.has(P) && P->type != tag::PlayType::Kneel && P->type != tag::PlayType::Spike)
		{
			C.Kind = FCallChoice::Off;
			C.Play = P;
		}
	}
	else if (CallLastDef && tag::defBookFor(G.defense()).has(CallLastDef))
	{
		C.Kind = FCallChoice::Def;
		C.Call = CallLastDef;
	}
	return C;
}

bool SGridironOverlay::CallKey(const FKey& K, bool bRepeat)
{
	if (!CallDone) return false;
	auto Is = [&K](std::initializer_list<FKey> Ks) { for (const FKey& X : Ks) if (K == X) return true; return false; };
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(CallCards.Num(), CallPerPage));
	// R3 (keyboard R): the last play called again
	if (Is({EKeys::Gamepad_RightThumbstick, EKeys::R}))
	{
		if (!bRepeat)
		{
			const FCallChoice Again = CallRepeat();
			if (Again.Kind != FCallChoice::Auto) CallPick(Again);
		}
		return true;
	}
	// L2 / R2 (keyboard Z / C): the play type tabs (all, run, pass, play action, screen, ...; the defense's zone, man,
	// blitz)
	const bool bPrevCat = Is({EKeys::Gamepad_LeftTrigger, EKeys::Z});
	const bool bNextCat = Is({EKeys::Gamepad_RightTrigger, EKeys::C});
	if (bPrevCat || bNextCat)
	{
		if (bRepeat) return true;
		const int32 N = CallCats.Num();
		if (N > 1)
		{
			const int32 At = FMath::Max(0, CallCats.IndexOfByKey(CallFilter()));
			CallFilter() = CallCats[(At + (bNextCat ? 1 : -1) + N) % N];
			CallPage = 0;
			ATagAudio::Ui(TEXT("UiTab"));
			BuildPlayCall();
		}
		return true;
	}
	int32 Slot = -1;
	if (Is({EKeys::Gamepad_FaceButton_Left, EKeys::One, EKeys::NumPadOne})) Slot = 0;
	else if (Is({EKeys::Gamepad_FaceButton_Bottom, EKeys::Two, EKeys::NumPadTwo, EKeys::SpaceBar, EKeys::Enter})) Slot = 1;
	else if (Is({EKeys::Gamepad_FaceButton_Right, EKeys::Three, EKeys::NumPadThree})) Slot = 2;
	if (Slot >= 0)
	{
		if (bRepeat) return true;
		const int32 I = CallPage * CallPerPage + Slot;
		if (CallCards.IsValidIndex(I)) CallPick(CallCards[I].Choice);
		return true;
	}
	if (Is({EKeys::Gamepad_FaceButton_Top, EKeys::H}))
	{
		if (!bRepeat) CallPick(FCallChoice());
		return true;
	}
	const bool bPrevTab = Is({EKeys::Gamepad_LeftShoulder, EKeys::Q});
	const bool bNextTab = Is({EKeys::Gamepad_RightShoulder, EKeys::E});
	if (bPrevTab || bNextTab)
	{
		if (bRepeat) return true; // one formation family a press: a held shoulder button repeats after 0.2 s (it skipped one)
		const int32 At = CallTabs.IndexOfByPredicate([this](const TPair<FString, FString>& Tb) { return Tb.Key == CallTab; });
		const int32 N = CallTabs.Num();
		if (N > 0)
		{
			CallTab = CallTabs[((At < 0 ? 0 : At) + (bNextTab ? 1 : -1) + N) % N].Key;
			CallPage = 0;
			CallSet.Reset();
			ATagAudio::Ui(TEXT("UiTab"));
			BuildPlayCall();
		}
		return true;
	}
	const bool bPrevPage = Is({EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A});
	const bool bNextPage = Is({EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D});
	if (bPrevPage || bNextPage)
	{
		// a press turns one page (round from the last to the first); held, the pages run on to the end and stop there
		if (Pages > 1)
		{
			const int32 To = bRepeat ? FMath::Clamp(CallPage + (bNextPage ? 1 : -1), 0, Pages - 1) : (CallPage + (bNextPage ? 1 : -1) + Pages) % Pages;
			if (To != CallPage)
			{
				CallPage = To;
				ATagAudio::Ui(TEXT("UiMove"));
				BuildPlayCall();
			}
		}
		return true;
	}
	// the right stick left / right (keyboard Page Up / Page Down): five pages at a time through a big list
	const bool bJumpBack = Is({EKeys::Gamepad_RightStick_Left, EKeys::PageUp});
	const bool bJumpOn = Is({EKeys::Gamepad_RightStick_Right, EKeys::PageDown});
	if (bJumpBack || bJumpOn)
	{
		if (Pages > 1)
		{
			const int32 To = FMath::Clamp(CallPage + (bJumpOn ? 5 : -5), 0, Pages - 1);
			if (To != CallPage)
			{
				CallPage = To;
				ATagAudio::Ui(TEXT("UiMove"));
				BuildPlayCall();
			}
		}
		return true;
	}
	// up and down: the formations (sets) of the family
	const bool bPrevSet = Is({EKeys::Gamepad_DPad_Up, EKeys::Up, EKeys::W});
	const bool bNextSet = Is({EKeys::Gamepad_DPad_Down, EKeys::Down, EKeys::S});
	if (bPrevSet || bNextSet)
	{
		if (bRepeat) return true;
		const int32 N = CallSets.Num();
		if (N > 1)
		{
			const int32 At = FMath::Max(0, CallSets.IndexOfByKey(CallSet));
			CallSet = CallSets[(At + (bNextSet ? 1 : -1) + N) % N];
			ATagAudio::Ui(TEXT("UiMove"));
			CallPage = 0;
			BuildPlayCall();
		}
		return true;
	}
	return false;
}

void SGridironOverlay::BuildPlayCall()
{
	const tag::Game& G = *Director->Game;
	const bool bOffense = bCallOffense, bTwoPoint = bCallTwo;
	// the plays of the tab
	CallCards.Reset();
	CallCats.Reset();
	auto Add = [this](const FString& Kind, const FString& Name, const FString& Note, const tag::OffPlay* Op, const tag::DefCall* Dc, FCallChoice C) -> FCallCard&
	{
		FCallCard& K = CallCards.AddDefaulted_GetRef();
		K.Kind = Kind;
		K.Name = Name;
		K.Note = Note;
		K.Op = Op;
		K.Dc = Dc;
		K.Choice = C;
		return K;
	};
	const bool bSpecial = CallTab == TEXT("special") || CallTab == TEXT("clock");
	// the formations (sets) of the tab's family and, per type tab, how many plays the family has (which tabs there
	// are) and how many of them are listed (their counts); each formation's count under the type tab
	TMap<FString, int32> CatShown;
	TSet<FString> CatInFamily;
	TArray<int32> SetCount;
	int32 ShownAll = 0, NForms = 0;
	// (the scope: one formation, or every one in the family - CallAllSet)
	struct FScoped { const tag::OffPlay* Op = nullptr; const tag::DefCall* Dc = nullptr; int32 Set = 0; FString Cat; };
	TArray<FScoped> Scoped;
	if (bOffense && CallTab == TEXT("special"))
	{
		const tag::Player* K = G.kicker(G.poss);
		const double Dist = FMath::RoundToDouble(100 - G.ballOn + 17);
		const int Pct = FMath::RoundToInt(G.fgProb(Dist, *K, G.poss) * 100);
		FCallChoice P; P.Kind = FCallChoice::Punt;
		FCallChoice F; F.Kind = FCallChoice::FG;
		Add(TEXT("Punt"), TEXT("Punt"), TEXT("P ") + S(G.punter(G.poss)->last), nullptr, nullptr, P);
		Add(TEXT("Field goal"), FString::Printf(TEXT("Field Goal  %d yd"), static_cast<int>(Dist)), FString::Printf(TEXT("%d%% est. make  -  K %s"), Pct, *S(K->last)), nullptr, nullptr, F);
	}
	else if (bOffense && CallTab == TEXT("clock"))
	{
		FCallChoice Kn; Kn.Kind = FCallChoice::Off; Kn.Play = tag::playById("kneel");
		FCallChoice Sp; Sp.Kind = FCallChoice::Off; Sp.Play = tag::playById("spike");
		Add(TEXT("Clock"), TEXT("QB Kneel"), TEXT("Victory formation"), nullptr, nullptr, Kn);
		Add(TEXT("Clock"), TEXT("Spike"), TEXT("Stops the clock, costs a down"), nullptr, nullptr, Sp);
	}
	else if (bOffense)
	{
		// the family's formations in the book (and, with two or more, the whole family as one list), the one shown,
		// and its plays under the type tab
		const tag::OffBook& Book = tag::offBookFor(G.offense());
		CallSets.Reset();
		for (const std::string& Fid : Book.formations)
			if (const tag::Formation* F = tag::formationById(Fid); F && S(tag::formationFamily(*F)) == CallTab) CallSets.Add(S(Fid));
		NForms = CallSets.Num();
		if (NForms > 1) CallSets.Add(CallAllSet());
		if (!CallSets.Contains(CallSet)) CallSet = CallSets.Num() ? CallSets[0] : FString();
		const bool bAll = CallSet == CallAllSet();
		for (int32 f = 0; f < NForms; ++f)
			for (const tag::OffPlay* PP : Book.playsIn(TCHAR_TO_UTF8(*CallSets[f])))
			{
				if (PP->type == tag::PlayType::Kneel || PP->type == tag::PlayType::Spike) continue;
				FScoped& Sc = Scoped.AddDefaulted_GetRef();
				Sc.Op = PP;
				Sc.Set = f;
				Sc.Cat = OffCategory(*PP);
				CatInFamily.Add(Sc.Cat);
			}
		for (const TCHAR* Cn : {TEXT("RUN"), TEXT("PASS"), TEXT("PA"), TEXT("SCREEN"), TEXT("RPO"), TEXT("TRICK")})
			if (CatInFamily.Contains(Cn)) CallCats.Add(Cn);
		if (!CallFilter().IsEmpty() && !CallCats.Contains(CallFilter())) CallFilter().Reset(); // (a family without it: all)
		SetCount.SetNumZeroed(CallSets.Num());
		// (the whole family lists each play once - its first formation, the others counted on the card: "Mesh" from
		// sixteen shotgun sets was sixteen cards, and the family ninety pages)
		TMap<FString, int32> Seen;
		TSet<FString> Uniq;
		for (const FScoped& Sc : Scoped)
		{
			const bool bPass = CallFilter().IsEmpty() || CallFilter() == Sc.Cat;
			const FString Same = Sc.Cat + TEXT("|") + S(Sc.Op->name);
			if (bPass) ++SetCount[Sc.Set];
			if (bPass) Uniq.Add(Same);
			if (!bAll && CallSets[Sc.Set] != CallSet) continue;
			if (bAll)
			{
				if (const int32* Was = Seen.Find(Same))
				{
					if (CallCards.IsValidIndex(*Was)) ++CallCards[*Was].Alt;
					continue;
				}
				Seen.Add(Same, bPass ? CallCards.Num() : INDEX_NONE);
			}
			CatShown.FindOrAdd(Sc.Cat)++;
			++ShownAll;
			if (!bPass) continue;
			const tag::OffPlay& P = *Sc.Op;
			// the note: the playbook's line on the play, else the quarterback's reads
			FString Note;
			for (const std::string& R : P.reads) Note += (Note.IsEmpty() ? TEXT("") : TEXT("  ")) + S(R);
			Note = !P.desc.empty() ? S(P.desc) : Note.IsEmpty() ? FString() : TEXT("READS  ") + Note;
			FCallChoice C; C.Kind = FCallChoice::Off; C.Play = &P;
			FCallCard& Cd = Add(Sc.Cat, S(P.name), Note, &P, nullptr, C);
			Cd.Cat = Sc.Cat;
			Cd.Concept = OffConcept(P, Sc.Cat);
			if (const tag::Formation* Fm = tag::formationById(P.formation)) Cd.Form = S(Fm->name);
		}
		if (NForms > 1) SetCount.Last() = Uniq.Num();
	}
	else
	{
		static const TMap<FString, FString> Cov = {
			{TEXT("C0"), TEXT("Cover 0  -  all-out man, no deep help")}, {TEXT("C1"), TEXT("Cover 1  -  man, single high")},
			{TEXT("C2"), TEXT("Cover 2  -  two deep halves")}, {TEXT("C2M"), TEXT("2-Man  -  man under, two deep")},
			{TEXT("C3"), TEXT("Cover 3  -  three deep, four under")}, {TEXT("C3Fire"), TEXT("Fire zone  -  3 deep, 3 under, pressure")}, {TEXT("C4"), TEXT("Quarters  -  four deep")},
			{TEXT("C2T"), TEXT("Tampa 2  -  the Mike runs the middle")}, {TEXT("C6"), TEXT("Cover 6  -  quarters left, half right")}};
		const tag::DefBook& Book = tag::defBookFor(G.defense());
		CallSets.Reset();
		for (const std::string& St : Book.sets)
		{
			const std::vector<const tag::DefCall*> In = Book.callsIn(St);
			if (!In.empty() && S(tag::frontFamily(In[0]->front)) == CallTab) CallSets.Add(S(St));
		}
		NForms = CallSets.Num();
		if (NForms > 1) CallSets.Add(CallAllSet());
		if (!CallSets.Contains(CallSet)) CallSet = CallSets.Num() ? CallSets[0] : FString();
		const bool bAll = CallSet == CallAllSet();
		for (int32 f = 0; f < NForms; ++f)
			for (const tag::DefCall* DP : Book.callsIn(TCHAR_TO_UTF8(*CallSets[f])))
			{
				FScoped& Sc = Scoped.AddDefaulted_GetRef();
				Sc.Dc = DP;
				Sc.Set = f;
				Sc.Cat = DefCategory(*DP);
				CatInFamily.Add(Sc.Cat);
			}
		for (const TCHAR* Cn : {TEXT("ZONE"), TEXT("MAN"), TEXT("PRESSURE"), TEXT("BLITZ"), TEXT("PREVENT")})
			if (CatInFamily.Contains(Cn)) CallCats.Add(Cn);
		if (!CallFilter().IsEmpty() && !CallCats.Contains(CallFilter())) CallFilter().Reset();
		SetCount.SetNumZeroed(CallSets.Num());
		TMap<FString, int32> Seen;
		TSet<FString> Uniq;
		for (const FScoped& Sc : Scoped)
		{
			const bool bPass = CallFilter().IsEmpty() || CallFilter() == Sc.Cat;
			const FString Same = Sc.Cat + TEXT("|") + DefShortName(*Sc.Dc);
			if (bPass) ++SetCount[Sc.Set];
			if (bPass) Uniq.Add(Same);
			if (!bAll && CallSets[Sc.Set] != CallSet) continue;
			if (bAll)
			{
				if (const int32* Was = Seen.Find(Same))
				{
					if (CallCards.IsValidIndex(*Was)) ++CallCards[*Was].Alt;
					continue;
				}
				Seen.Add(Same, bPass ? CallCards.Num() : INDEX_NONE);
			}
			CatShown.FindOrAdd(Sc.Cat)++;
			++ShownAll;
			if (!bPass) continue;
			const tag::DefCall& D = *Sc.Dc;
			const FString* Desc = Cov.Find(FString(tag::coverageName(D.coverage)));
			FCallChoice C; C.Kind = FCallChoice::Def; C.Call = &D;
			// (the playbook's own words where it has them: the coverage as called, the pressure, a line on the call)
			FCallCard& Cd = Add(D.rushers() <= 4 ? FString::Printf(TEXT("Coverage  -  %d rush"), D.rushers()) : FString::Printf(TEXT("%d-man pressure"), D.rushers()), DefShortName(D),
				!D.desc.empty() ? S(D.desc) : Desc ? *Desc : FString(), nullptr, &D, C);
			Cd.Cat = Sc.Cat;
			const FString Cover = D.coverLabel.empty() ? CoverageShort(D.coverage) : S(D.coverLabel);
			const FString Press = D.pressure.empty() || D.pressure == "Base" ? FString() : TEXT("  -  ") + S(D.pressure);
			Cd.Concept = FString::Printf(TEXT("%s%s  -  %d rush"), *Cover, *Press, D.rushers());
			Cd.Form = D.frontLabel.empty() ? S(tag::defSet(D)) : S(D.frontLabel);
		}
		if (NForms > 1) SetCount.Last() = Uniq.Num();
	}
	// the type tabs: ALL first (only when there is more than one kind to tell apart)
	if (CallCats.Num() > 0) CallCats.Insert(FString(), 0);
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(CallCards.Num(), CallPerPage));
	CallPage = FMath::Clamp(CallPage, 0, Pages - 1);

	const FLinearColor White90(0.93f, 0.95f, 0.98f);
	// (linear colours: 0.016 is the near-black of the broadcast's cards, 0.055 already reads mid grey)
	const FLinearColor Card(0.012f, 0.015f, 0.022f, 0.98f);
	const FLinearColor CardEdge(0.05f, 0.06f, 0.08f, 1.f);
	auto Glyph = [this](EPadGlyph Gl, const FString& Key, float Size) -> TSharedRef<SWidget>
	{
		return SNew(SCallGlyph).Owner(this).Glyph(Gl).Key(Key).Size(Size);
	};

	// the header: what is being called, the formation, and the situation with the clocks running
	FString TabLabel = [&]() -> FString { for (const TPair<FString, FString>& Tb : CallTabs) if (Tb.Key == CallTab) return Tb.Value; return FString(); }();
	// under the title: the formation's personnel (and the plays listed), or how many formations the family list holds
	FString FormLine, FormDesc;
	const bool bAllView = CallSet == CallAllSet();
	if (!CallSet.IsEmpty() && !bSpecial)
	{
		// the formation (set) shown
		if (bAllView)
		{
			TabLabel = TEXT("ALL ") + TabLabel;
			FormLine = FString::Printf(TEXT("%d %s  ·  %d %s"), NForms, bOffense ? TEXT("FORMATIONS") : TEXT("SETS"), ShownAll, bOffense ? TEXT("PLAYS") : TEXT("CALLS"));
		}
		else
		{
			const tag::Formation* F = bOffense ? tag::formationById(TCHAR_TO_UTF8(*CallSet)) : nullptr;
			TabLabel = (F ? S(F->name) : CallSet).ToUpper();
			if (F) FormDesc = S(F->desc);
			FormLine = F ? PersonnelText(*F) + TEXT("  ·  ") : FString();
			FormLine += FString::Printf(TEXT("%d %s"), ShownAll, bOffense ? TEXT("PLAYS") : TEXT("CALLS"));
		}
	}
	const FString BookLine = bOffense ? FString::Printf(TEXT("%s PLAYBOOK  -  %s"), *S(tag::offBookFor(G.offense()).name).ToUpper(), *S(tag::offBookFor(G.offense()).scheme).ToUpper())
		: FString::Printf(TEXT("%s PLAYBOOK  -  %s"), *S(tag::defBookFor(G.defense()).name).ToUpper(), *S(tag::defBookFor(G.defense()).scheme).ToUpper());
	const FString Sit = bTwoPoint ? FString(TEXT("TWO-POINT TRY  -  FROM THE 2"))
		: FString::Printf(TEXT("%s  -  %s"), *S(G.downText()).ToUpper(), *S(G.ydLine()));
	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SVerticalBox)
			// a multiplayer game: whose call this is (only his pad / keys work this screen)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 4)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(CallWhoColor).Padding(FMargin(10, 3))
				.Visibility(CallWho.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
				[ SNew(STextBlock).Font(Font(14, "Bold")).ColorAndOpacity(FLinearColor(0.03f, 0.04f, 0.06f)).Text(T(CallWho)) ]
			]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Accent)
				.Text(T(FString(bOffense ? (bTwoPoint ? TEXT("TWO-POINT TRY  -  CALL OFFENSE") : TEXT("CALL OFFENSE")) : TEXT("CALL DEFENSE")) + TEXT("     ") + BookLine)) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[ SNew(STextBlock).Font(Font(30, "Bold")).ColorAndOpacity(White90).Text(T(TabLabel)) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(18, 0, 0, 6)
				[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 40)).ColorAndOpacity(Dim).Text(T(FormLine)) ]
			]
			// the playbook's line on the formation
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
			[
				SNew(STextBlock).Font(Font(11)).ColorAndOpacity(TagTheme::Faint).Text(T(FormDesc))
				.Visibility(FormDesc.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
			]
			// calling defense: what the offense has come out in
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 2)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.10f, 0.08f, 0.02f, 0.92f)).Padding(FMargin(10, 4))
				.Visibility(!bOffense && !CallOffLook.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
				[ SNew(STextBlock).Font(Font(14, "Bold")).ColorAndOpacity(Accent).Text(T(CallOffLook)) ]
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[ SNullWidget::NullWidget ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 24, 4)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[ SNew(STextBlock).Font(Font(18, "Bold")).ColorAndOpacity(White90).Text(T(Sit)) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Dim).Text_Lambda([this]
				{
					const tag::Game& GG = *Director->Game;
					return T(FString::Printf(TEXT("%s %d  -  %s %d     Q%d  %s"), *S(GG.away->abbr), GG.score.away, *S(GG.home->abbr), GG.score.home,
						FMath::Min(GG.q, 5), *ClockText().ToString()));
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.08f, 0.10f, 0.14f, 1.f)).Padding(FMargin(16, 4))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(Font(9, "Bold")).ColorAndOpacity(Dim).Text(T(TEXT("PLAY CLOCK"))) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Font(Font(28, "Bold"))
					.Text_Lambda([this] { const int32 P = Director->PlayClockShown(); return T(P < 0 ? FString(TEXT("--")) : FString::Printf(TEXT(":%02d"), P)); })
					.ColorAndOpacity_Lambda([this] { const int32 P = Director->PlayClockShown(); return FSlateColor(P >= 0 && P <= 5 ? FLinearColor(1.f, 0.25f, 0.2f) : FLinearColor::White); })
				]
			]
		];

	// the formation tabs between their shoulder buttons; the one shown is white with a gold bar under it
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[ Glyph(EPadGlyph::L1, TEXT("Q"), 30.f) ];
	// (a book with more families than the row holds shows a window of them round the one up, the rest counted at the
	// ends: the shoulder buttons still step through all of them)
	const double RowW = FMath::Max(640.0, static_cast<double>(GetTickSpaceGeometry().GetLocalSize().X)) - 2.0 * 34.0 - 140.0;
	int32 TabFirst = 0, TabLast = CallTabs.Num() - 1;
	{
		auto TabW = [](const FString& L) { return 30.0 + 10.5 * L.Len(); };
		double Used = 0;
		for (const TPair<FString, FString>& Tb : CallTabs) Used += TabW(Tb.Value);
		if (Used > RowW && CallTabs.Num() > 1)
		{
			const int32 At = FMath::Max(0, CallTabs.IndexOfByPredicate([this](const TPair<FString, FString>& Tb) { return Tb.Key == CallTab; }));
			TabFirst = TabLast = At;
			Used = TabW(CallTabs[At].Value) + 120.0;
			for (bool bGrew = true; bGrew;)
			{
				bGrew = false;
				if (TabLast + 1 < CallTabs.Num() && Used + TabW(CallTabs[TabLast + 1].Value) <= RowW) { Used += TabW(CallTabs[++TabLast].Value); bGrew = true; }
				if (TabFirst > 0 && Used + TabW(CallTabs[TabFirst - 1].Value) <= RowW) { Used += TabW(CallTabs[--TabFirst].Value); bGrew = true; }
			}
		}
	}
	if (TabFirst > 0)
		Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 8, 0)[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(TagTheme::Faint).Text(T(FString::Printf(TEXT("‹ %d"), TabFirst))) ];
	for (int32 Ti = TabFirst; Ti <= TabLast && CallTabs.IsValidIndex(Ti); ++Ti)
	{
		const TPair<FString, FString>& Tb = CallTabs[Ti];
		const bool bOn = Tb.Key == CallTab;
		const FString Id = Tb.Key;
		Tabs->AddSlot().AutoWidth().Padding(2, 0)
		[
			SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor(1, 1, 1, bOn ? 0.06f : 0.f)).ContentPadding(FMargin(14, 6, 14, 0))
			.OnClicked_Lambda([this, Id] { CallTab = Id; CallPage = 0; CallSet.Reset(); BuildPlayCall(); return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(13, "Bold")).ColorAndOpacity(bOn ? White90 : Dim).Text(T(Tb.Value)) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
				[ SNew(SBox).HeightOverride(3)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(bOn ? Accent : FLinearColor::Transparent) ] ]
			]
		];
	}
	if (TabLast < CallTabs.Num() - 1)
		Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 4, 0)[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(TagTheme::Faint).Text(T(FString::Printf(TEXT("%d ›"), CallTabs.Num() - 1 - TabLast))) ];
	Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)[ Glyph(EPadGlyph::R1, TEXT("E"), 30.f) ];

	// ---- the sizes: the cards as large as the screen allows, their art about 1.25 : 1 (a spread play's width by its
	// backfield and its deep routes); what height is left over shows the stadium above the call. (In Slate units: the
	// overlay's own size, after the DPI scale - 1920 x 1080 at 1080p and 1440p alike, narrower on a 16:10 screen.)
	FVector2D Avail = GetTickSpaceGeometry().GetLocalSize();
	if (Avail.X < 640.0 || Avail.Y < 360.0) Avail = FVector2D(1920.0, 1080.0);
	CallBuiltAvail = Avail;
	constexpr float PanelPadX = 34.f, LeftColW = 270.f, ColGap = 20.f, ArrowW = 44.f, CardPad = 8.f;
	const bool bTypes = CallCats.Num() > 1;
	const bool bStrip = Pages > 1;
	const float CardW = FMath::Max(160.f, (static_cast<float>(Avail.X) - 2.f * PanelPadX - LeftColW - ColGap - 2.f * ArrowW - 6.f * CardPad) / CallPerPage);
	const float HeaderH = 64.f + (CallWho.IsEmpty() ? 0.f : 28.f) + (!bOffense && !CallOffLook.IsEmpty() ? 32.f : 0.f) + (FormDesc.IsEmpty() ? 0.f : 18.f);
	// (top to bottom: the stadium's least, the scorebug's room, the panel's padding, the header, the family tabs, the
	// type tabs, a card's name block and its button, the page strip, the button bar)
	constexpr float CardInfoH = 146.f;
	const float FixedH = 16.f + 104.f + 26.f + HeaderH + 8.f + 46.f + (bTypes ? 44.f : 0.f) + CardInfoH + (bStrip ? 84.f : 0.f) + 40.f;
	const float ArtH = FMath::Clamp(FMath::Min(CardW / 1.25f, static_cast<float>(Avail.Y) - FixedH), 140.f, 620.f);

	// ---- the type tabs (L2 / R2): every kind the family has, each with how many of its plays are listed
	TSharedRef<SHorizontalBox> Types = SNew(SHorizontalBox);
	if (bTypes)
	{
		Types->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[ Glyph(EPadGlyph::L2, TEXT("Z"), 26.f) ];
		for (const FString& Cn : CallCats)
		{
			const bool bOn = Cn == CallFilter();
			const int32 Num = Cn.IsEmpty() ? ShownAll : CatShown.FindRef(Cn);
			const FLinearColor Cc = Cn.IsEmpty() ? Accent : CatColor(Cn);
			const FLinearColor Fg = bOn ? FLinearColor(0.02f, 0.025f, 0.04f) : Num > 0 ? Cc : TagTheme::Faint;
			Types->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)
			[
				SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor::Transparent).ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, Cn] { CallFilter() = Cn; CallPage = 0; ATagAudio::Ui(TEXT("UiTab")); BuildPlayCall(); return FReply::Handled(); })
				[
					SNew(SBorder).BorderImage(TagTheme::Round(4.f)).BorderBackgroundColor(bOn ? FLinearColor(Cc.R, Cc.G, Cc.B, 0.95f) : FLinearColor(1.f, 1.f, 1.f, 0.05f))
					.Padding(FMargin(12, 5, 12, 5))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 40)).ColorAndOpacity(Fg).Text(T(CatLabel(Cn))) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
						[ SNew(STextBlock).Font(TagTheme::Font(12, "Bold")).ColorAndOpacity(Fg * FLinearColor(1, 1, 1, bOn ? 0.75f : 0.7f)).Text(T(FString::FromInt(Num))) ]
					]
				]
			];
		}
		Types->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ Glyph(EPadGlyph::R2, TEXT("C"), 26.f) ];
	}

	// ---- the cards: three a page, each over its button - the art fitted to the card (UI/PlayDiagram.cpp), the play's
	// type badge and concept, its name, its reads (and in the family list its formation); the pointer lights a card's
	// edge gold, and the play called last carries a tag
	const double Los = bTwoPoint ? 98.0 : G.ballOn;
	const double FirstDown = bTwoPoint ? -1.0 : FMath::Min(100.0, G.ballOn + G.toGo);
	const FCallChoice Rep = CallRepeat();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	auto Arrow = [&](EPadGlyph Gl, const TCHAR* Key, bool bShow) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(ArrowW).VAlign(VAlign_Center).HAlign(HAlign_Center)
			[ bShow ? Glyph(Gl, Key, 32.f) : SNullWidget::NullWidget ];
	};
	Row->AddSlot().AutoWidth()[ Arrow(EPadGlyph::DLeft, TEXT("←"), Pages > 1) ];
	if (CallCards.IsEmpty())
	{
		// (a formation with none of this type: say so, and how to find some)
		const FString Why = CallFilter().IsEmpty() ? FString(TEXT("NO PLAYS HERE"))
			: FString::Printf(TEXT("NO %s %s IN %s"), *CatLabel(CallFilter()), bOffense ? TEXT("PLAYS") : TEXT("CALLS"), *TabLabel);
		Row->AddSlot().FillWidth(1.f).Padding(CardPad, 0)
		[
			SNew(SBox).HeightOverride(ArtH + CardInfoH - 50.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(TagTheme::Font(22, "BoldCondensed", 40)).ColorAndOpacity(TagTheme::Dim).Text(T(Why)) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
				[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(TagTheme::Faint).Text(T(bOffense ? TEXT("D-PAD UP / DOWN FOR ANOTHER FORMATION  ·  L2 / R2 FOR ANOTHER TYPE") : TEXT("D-PAD UP / DOWN FOR ANOTHER SET  ·  L2 / R2 FOR ANOTHER TYPE"))) ]
			]
		];
	}
	for (int32 k = 0; k < CallPerPage && !CallCards.IsEmpty(); ++k)
	{
		const int32 I = CallPage * CallPerPage + k;
		if (!CallCards.IsValidIndex(I))
		{
			Row->AddSlot().FillWidth(1.f).Padding(CardPad, 0)[ SNew(SBox).HeightOverride(ArtH) ];
			continue;
		}
		const FCallCard& Cd = CallCards[I];
		TSharedRef<SWidget> Art = SNullWidget::NullWidget;
		if (Cd.Op || Cd.Dc)
		{
			// (the picture once per play while the screen is up: a page turned back costs no sim)
			const FString Key = FString::Printf(TEXT("%p|%p|%d"), Cd.Op, Cd.Dc, bOffense ? 1 : 0);
			FArtPicture Pic;
			if (const FArtPicture* Hit = CallPics.IsValid() ? CallPics->Pics.Find(Key) : nullptr) Pic = *Hit;
			else
			{
				BuildCardPicture(*Director->Game, Cd.Op, Cd.Dc, bOffense, Los, G.ballY, Pic);
				if (CallPics.IsValid()) CallPics->Pics.Add(Key, Pic);
			}
			if (!Pic.Marks.IsEmpty()) Art = SNew(SPlayDiagram).Picture(Pic).Offense(bOffense).FirstDown(FirstDown);
		}
		if (Art == SNullWidget::NullWidget)
			Art = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.025f, 0.032f, 0.045f, 1.f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(Font(26, "Bold")).ColorAndOpacity(FLinearColor(1, 1, 1, 0.22f)).Text(T(Cd.Kind.ToUpper())) ];
		const FCallChoice Choice = Cd.Choice;
		const bool bLastCalled = (Cd.Op && Cd.Op == Rep.Play) || (Cd.Dc && Cd.Dc == Rep.Call);
		const FLinearColor Cc = CatColor(Cd.Cat);
		// (the whole family's list: the formation it is called from, and how many more run it)
		const FString FormTag = Cd.Form.ToUpper() + (Cd.Alt > 0 ? FString::Printf(TEXT(" +%d"), Cd.Alt) : FString());
		const FString NoteLine = bAllView && !Cd.Form.IsEmpty() ? FormTag + (Cd.Note.IsEmpty() ? FString() : TEXT("  ·  ") + Cd.Note) : Cd.Note;
		// (the card's edge: gold under the pointer)
		const TSharedRef<TWeakPtr<SButton>> Hold = MakeShared<TWeakPtr<SButton>>();
		TSharedPtr<SButton> Btn;
		Row->AddSlot().FillWidth(1.f).Padding(CardPad, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(Btn, SButton).IsFocusable(false).ContentPadding(FMargin(2))
				.ButtonColorAndOpacity_Lambda([Hold, CardEdge]
				{
					const TSharedPtr<SButton> B = Hold->Pin();
					return FSlateColor(B.IsValid() && B->IsHovered() ? Accent : CardEdge);
				})
				.OnClicked_Lambda([this, Choice] { CallPick(Choice); return FReply::Handled(); })
				[
					SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Card).Padding(FMargin(0))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).HeightOverride(ArtH)[ Art ] ]
						// the type badge and the concept, the last-called tag on the right
						+ SVerticalBox::Slot().AutoHeight().Padding(12, 9, 12, 0)
						[
							SNew(SBox).HeightOverride(20)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[
									SNew(SBorder).BorderImage(TagTheme::Round(3.f)).BorderBackgroundColor(Cc).Padding(FMargin(7, 1, 7, 1))
									.Visibility(Cd.Cat.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
									[ SNew(STextBlock).Font(TagTheme::Font(11, "BoldCondensed", 60)).ColorAndOpacity(FLinearColor(0.02f, 0.025f, 0.04f)).Text(T(Cd.Cat)) ]
								]
								+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(9, 0, 0, 0)
								[ SNew(STextBlock).Font(TagTheme::Font(12, "BoldCondensed", 40)).ColorAndOpacity(Dim).Text(T(Cd.Concept.ToUpper())).Clipping(EWidgetClipping::ClipToBounds) ]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
								[
									SNew(STextBlock).Font(TagTheme::Font(10, "BoldCondensed", 80)).ColorAndOpacity(Accent).Text(T(TEXT("LAST CALLED")))
									.Visibility(bLastCalled ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(12, 3, 12, 0)
						[ SNew(STextBlock).Font(Font(21, "Bold")).ColorAndOpacity(White90).Text(T(Cd.Name)).Clipping(EWidgetClipping::ClipToBounds) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(12, 2, 12, 10)
						[ SNew(SBox).HeightOverride(16)[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Accent).Text(T(NoteLine)).Clipping(EWidgetClipping::ClipToBounds) ] ]
					]
				]
			]
			// the button that calls it, under the card
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[ Glyph(CallButtons[k], CallKeys[k], 38.f) ]
		];
		*Hold = Btn;
	}
	Row->AddSlot().AutoWidth()[ Arrow(EPadGlyph::DRight, TEXT("→"), Pages > 1) ];

	// ---- the pages (D-pad left / right): a window of them, each with its three plays named, the one up in gold; a
	// click turns to it
	TSharedRef<SHorizontalBox> Strip = SNew(SHorizontalBox);
	if (bStrip)
	{
		// (as many page boxes as fit beside the "PAGE n / N" count)
		const float MainW = static_cast<float>(Avail.X) - 2.f * PanelPadX - LeftColW - ColGap;
		const int32 Win = FMath::Clamp(static_cast<int32>((MainW - 2.f * ArrowW - 330.f) / 202.f), 2, 6);
		const int32 First = FMath::Clamp(CallPage - Win / 2, 0, FMath::Max(0, Pages - Win));
		Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(ArrowW, 0, 6, 0)
		[ SNew(STextBlock).Font(Font(14, "Bold")).ColorAndOpacity(TagTheme::Faint).Text(T(First > 0 ? TEXT("…") : TEXT(" "))) ];
		for (int32 Pg = First; Pg < Pages && Pg < First + Win; ++Pg)
		{
			const bool bOn = Pg == CallPage;
			TSharedRef<SVerticalBox> Names = SNew(SVerticalBox);
			for (int32 j = 0; j < CallPerPage; ++j)
			{
				const int32 I = Pg * CallPerPage + j;
				if (!CallCards.IsValidIndex(I)) break;
				Names->AddSlot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[ SNew(SBox).WidthOverride(6).HeightOverride(6)[ SNew(SBorder).BorderImage(TagTheme::Round(3.f)).BorderBackgroundColor(CatColor(CallCards[I].Cat)) ] ]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(10, bOn ? "Bold" : "Regular")).ColorAndOpacity(bOn ? White90 : Dim).Text(T(CallCards[I].Name)).Clipping(EWidgetClipping::ClipToBounds) ]
				];
			}
			Strip->AddSlot().AutoWidth().Padding(3, 0)
			[
				SNew(SBox).WidthOverride(196)
				[
					SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor::Transparent).ContentPadding(FMargin(0))
					.OnClicked_Lambda([this, Pg] { CallPage = Pg; ATagAudio::Ui(TEXT("UiMove")); BuildPlayCall(); return FReply::Handled(); })
					[
						SNew(TagTheme::SSelect).Selected(bOn).Fill(FLinearColor(1.f, 1.f, 1.f, 0.035f)).Padding(FMargin(10, 5, 8, 6))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)
							[ SNew(STextBlock).Font(TagTheme::Font(10, "BoldCondensed", 80)).ColorAndOpacity(bOn ? Accent : TagTheme::Faint).Text(T(FString::Printf(TEXT("PAGE %d"), Pg + 1))) ]
							+ SVerticalBox::Slot().AutoHeight()[ Names ]
						]
					]
				]
			];
		}
		Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
		[ SNew(STextBlock).Font(Font(14, "Bold")).ColorAndOpacity(TagTheme::Faint).Text(T(First + Win < Pages ? TEXT("…") : TEXT(" "))) ];
	}
	Strip->AddSlot().FillWidth(1.f)[ SNullWidget::NullWidget ];
	Strip->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, ArrowW, 0)
	[
		SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 40)).ColorAndOpacity(Dim)
		.Text(T(FString::Printf(TEXT("PAGE %d / %d   ·   %d %s"), CallPage + 1, Pages, CallCards.Num(), bOffense ? TEXT("PLAYS") : TEXT("CALLS"))))
		.Visibility(bSpecial ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
	];

	// ---- the left column: the family's formations (sets) with their personnel and how many plays each lists, the one
	// shown in gold (D-pad up / down; a click picks one); under them the repeat call (R3) and the coach's pick (triangle)
	TSharedRef<SVerticalBox> Forms = SNew(SVerticalBox);
	Forms->AddSlot().AutoHeight().Padding(4, 0, 0, 6)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[ SNew(STextBlock).Font(TagTheme::Font(12, "BoldCondensed", 80)).ColorAndOpacity(Dim).Text(T(bSpecial ? TEXT("SITUATION") : bOffense ? TEXT("FORMATIONS") : TEXT("SETS"))) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ CallSets.Num() > 1 && !bSpecial ? Glyph(EPadGlyph::DUpDown, TEXT("↑↓"), 22.f) : SNullWidget::NullWidget ]
	];
	if (bSpecial)
	{
		Forms->AddSlot().AutoHeight().Padding(4, 0)
		[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(TagTheme::Faint).AutoWrapText(true).Text(T(CallTab == TEXT("special") ? TEXT("Punt it away or try the kick.") : TEXT("Kneel to run the clock, spike to stop it."))) ];
	}
	else
	{
		const int32 At = FMath::Max(0, CallSets.IndexOfByKey(CallSet));
		// (as many rows as the cards' height leaves room for beside them, the two boxes under them)
		const int32 MaxRows = FMath::Clamp(static_cast<int32>((ArtH + CardInfoH + (bTypes ? 44.f : 0.f) + (bStrip ? 84.f : 0.f) - 200.f) / 36.f), 4, 14);
		const int32 FirstRow = FMath::Clamp(At - MaxRows / 2, 0, FMath::Max(0, CallSets.Num() - MaxRows));
		if (FirstRow > 0)
			Forms->AddSlot().AutoHeight().Padding(12, 0, 0, 2)[ SNew(STextBlock).Font(Font(10, "Bold")).ColorAndOpacity(TagTheme::Faint).Text(T(FString::Printf(TEXT("▲  %d MORE"), FirstRow))) ];
		for (int32 Ri = FirstRow; Ri < CallSets.Num() && Ri < FirstRow + MaxRows; ++Ri)
		{
			const FString Id = CallSets[Ri];
			const bool bOn = Id == CallSet;
			const bool bAllRow = Id == CallAllSet();
			const tag::Formation* Fm = bOffense && !bAllRow ? tag::formationById(TCHAR_TO_UTF8(*Id)) : nullptr;
			FString RowName = bAllRow ? FString::Printf(TEXT("All %s"), *CallTab) : Fm ? S(Fm->name) : Id;
			// (the family is the tab: "Singleback Trey Open" under SINGLEBACK is "Trey Open", "Gun Bunch" under
			// SHOTGUN "Bunch", "3-4 Okie" under 3-4 "Okie")
			if (!bAllRow)
				for (const FString& Pre : {CallTab + TEXT(" "), FString(TEXT("Gun ")), FString(TEXT("Singleback "))})
					if (RowName.StartsWith(Pre) && RowName.Len() > Pre.Len()) { RowName = RowName.RightChop(Pre.Len()); break; }
			const FString Pers = Fm ? S(Fm->personnel) : FString();
			const int32 Num = SetCount.IsValidIndex(Ri) ? SetCount[Ri] : 0;
			Forms->AddSlot().AutoHeight().Padding(0, 1)
			[
				SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor::Transparent).ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, Id] { CallSet = Id; CallPage = 0; ATagAudio::Ui(TEXT("UiMove")); BuildPlayCall(); return FReply::Handled(); })
				[
					SNew(TagTheme::SSelect).Selected(bOn).Padding(FMargin(12, 7, 10, 7))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(Font(14, "Bold")).ColorAndOpacity(bOn ? White90 : bAllRow ? Accent * FLinearColor(1, 1, 1, 0.8f) : Dim).Text(T(RowName)).Clipping(EWidgetClipping::ClipToBounds) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
						[ SNew(STextBlock).Font(TagTheme::Font(11, "BoldCondensed", 40)).ColorAndOpacity(TagTheme::Faint).Text(T(Pers)) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
						[ SNew(SBox).WidthOverride(26).HAlign(HAlign_Right)[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Num > 0 ? (bOn ? Accent : Dim) : TagTheme::Faint).Text(T(FString::FromInt(Num))) ] ]
					]
				]
			];
		}
		if (FirstRow + MaxRows < CallSets.Num())
			Forms->AddSlot().AutoHeight().Padding(12, 2, 0, 0)[ SNew(STextBlock).Font(Font(10, "Bold")).ColorAndOpacity(TagTheme::Faint).Text(T(FString::Printf(TEXT("▼  %d MORE"), CallSets.Num() - FirstRow - MaxRows))) ];
	}
	auto SideBox = [&](EPadGlyph Gl, const TCHAR* Key, const FString& Kicker, const FString& Line, const FString& Sub, FCallChoice Pick) -> TSharedRef<SWidget>
	{
		return SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor::Transparent).ContentPadding(FMargin(0))
			.OnClicked_Lambda([this, Pick] { CallPick(Pick); return FReply::Handled(); })
			[
				SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(PanelLight).Padding(FMargin(12, 8, 12, 8))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ Glyph(Gl, Key, 30.f) ]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(TagTheme::Font(10, "BoldCondensed", 80)).ColorAndOpacity(Accent).Text(T(Kicker)) ]
						+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(14, "Bold")).ColorAndOpacity(White90).Text(T(Line)).Clipping(EWidgetClipping::ClipToBounds) ]
						+ SVerticalBox::Slot().AutoHeight()
						[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text(T(Sub)).Clipping(EWidgetClipping::ClipToBounds).Visibility(Sub.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible) ]
					]
				]
			];
	};
	FString RepName, RepSub;
	if (Rep.Play)
	{
		RepName = S(Rep.Play->name);
		if (const tag::Formation* Fm = tag::formationById(Rep.Play->formation)) RepSub = S(Fm->name);
	}
	else if (Rep.Call)
	{
		RepName = S(Rep.Call->name);
		RepSub = S(tag::defSet(*Rep.Call));
	}
	TSharedRef<SWidget> LeftCol = SNew(SBox).WidthOverride(LeftColW)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ Forms ]
		+ SVerticalBox::Slot().FillHeight(1.f)[ SNullWidget::NullWidget ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(SBox).Visibility(RepName.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
			[ SideBox(EPadGlyph::R3, TEXT("R"), bOffense ? TEXT("LAST PLAY") : TEXT("LAST CALL"), RepName, RepSub, Rep) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
		[ SideBox(EPadGlyph::Triangle, TEXT("H"), TEXT("COACH'S PICK"), bOffense ? TEXT("Coach calls it") : TEXT("Coordinator calls it"), FString(), FCallChoice()) ]
	];

	// the legend: what the buttons do
	auto Legend = [&](std::initializer_list<std::pair<EPadGlyph, const TCHAR*>> Gs, const FString& Label) -> TSharedRef<SWidget>
	{
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		for (const std::pair<EPadGlyph, const TCHAR*>& Gk : Gs) H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)[ Glyph(Gk.first, Gk.second, 24.f) ];
		H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 26, 0)[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Dim).Text(T(Label)) ];
		return H;
	};
	TSharedRef<SHorizontalBox> Foot = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::Square, TEXT("1")}, {EPadGlyph::Cross, TEXT("2")}, {EPadGlyph::Circle, TEXT("3")}}, TEXT("CALL PLAY")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::L1, TEXT("Q")}, {EPadGlyph::R1, TEXT("E")}}, bOffense ? TEXT("FAMILY") : TEXT("FRONT")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::DUpDown, TEXT("\u2191\u2193")}}, bOffense ? TEXT("FORMATION") : TEXT("SET")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::DLeftRight, TEXT("←→")}}, FString::Printf(TEXT("MORE PLAYS  %d / %d"), CallPage + 1, Pages)) ];
	if (Pages > 5) Foot->AddSlot().AutoWidth()[ Legend({{EPadGlyph::R3, TEXT("PgDn")}}, TEXT("R-STICK ←→  5 PAGES")) ];
	if (bTypes) Foot->AddSlot().AutoWidth()[ Legend({{EPadGlyph::L2, TEXT("Z")}, {EPadGlyph::R2, TEXT("C")}}, TEXT("PLAY TYPE")) ];
	if (!RepName.IsEmpty()) Foot->AddSlot().AutoWidth()[ Legend({{EPadGlyph::R3, TEXT("R")}}, bOffense ? TEXT("LAST PLAY") : TEXT("LAST CALL")) ];
	Foot->AddSlot().AutoWidth()[ Legend({{EPadGlyph::Triangle, TEXT("H")}}, TEXT("COACH'S PICK")) ];

	// the call: the header and the family tabs across the top; the formations down the left; the type tabs, the three
	// cards between the page arrows and the page strip on the right; the buttons along the foot
	TSharedRef<SWidget> Main = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(ArrowW + CardPad, 0, 0, 12)[ Types ]
		+ SVerticalBox::Slot().AutoHeight()[ Row ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[ Strip ];
	TSharedRef<SWidget> CallPanel = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.012f, 0.016f, 0.026f, 0.95f)).Padding(FMargin(PanelPadX, 14, PanelPadX, 12))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ Header ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[ Tabs ]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ LeftCol ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(ColGap, 0, 0, 0)[ Main ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[ Foot ]
	];
	TSharedRef<SWidget> Root = SAssignNew(CallRoot, SCallRoot).OnKey([this](const FKey& K, bool bRep) { return CallKey(K, bRep); })
	[
		SNew(SVerticalBox)
		// the field stays in view over the top (lightly shaded) in what height the cards leave, the call across the rest
		// above the scorebug
		+ SVerticalBox::Slot().FillHeight(1.f)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.18f)) ]
		+ SVerticalBox::Slot().AutoHeight()[ CallPanel ]
		+ SVerticalBox::Slot().AutoHeight()[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.18f)).Padding(0)[ SNew(SBox).HeightOverride(104) ] ] // (the scorebug)
	];
	bHud = true;
	ShowModal(Root, false);
}

void SGridironOverlay::ShowPatChoice(std::function<void(bool)> Done)
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)[ TagTheme::Title(TEXT("Touchdown"), TEXT("Try after touchdown"), 32) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Kick Extra Point"), TEXT("33-yard attempt, ~94% league-wide"), [this, Done] { ClearModal(); Done(false); }, 480) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Go for Two"), TEXT("Run a play from the 2, ~48% league-wide"), [this, Done] { ClearModal(); Done(true); }, 480) ];
	ShowModal(SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(TagTheme::SBackdrop).Strength(0.6f) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(TagTheme::SPanel).AccentTop(TagTheme::Gold).Padding(FMargin(32, 28))[V] ]);
}

void SGridironOverlay::RestoreSuspended()
{
	if (Suspended.IsValid())
	{
		ShowModal(Suspended.ToSharedRef());
		PendingFocus = FMath::Max(0, SuspendedFocus);
		Suspended.Reset();
	}
	else ClearModal();
}

void SGridironOverlay::ShowPause(std::function<void()> Resume, std::function<void()> SimToEnd, std::function<void()> Quit)
{
	// the screen the pause comes over (the play call, the try choice) waits underneath and comes back with Resume
	Suspended = bModal ? ModalContent : nullptr;
	SuspendedFocus = bModal ? FocusedIndex() : -1;
	ATagAudio::Screen(TEXT("Pause"));
	// the game as it stands: the score, the quarter and clock, the down
	FString Score, Situation;
	if (Director && Director->Game)
	{
		const tag::Game& G = *Director->Game;
		Score = FString::Printf(TEXT("%s  %d   -   %s  %d"), *S(G.away->abbr), Director->ScoreShown(tag::TeamKey::Away), *S(G.home->abbr), Director->ScoreShown(tag::TeamKey::Home));
		const int32 Sec = FMath::Max(0, static_cast<int32>(G.clock));
		const FString Qt = G.q >= 5 ? FString(TEXT("OVERTIME")) : FString::Printf(TEXT("%s QUARTER"), G.q == 1 ? TEXT("1ST") : G.q == 2 ? TEXT("2ND") : G.q == 3 ? TEXT("3RD") : TEXT("4TH"));
		Situation = FString::Printf(TEXT("%s   %d:%02d   -   %s  %s"), *Qt, Sec / 60, Sec % 60, UTF8_TO_TCHAR(G.downText().c_str()), UTF8_TO_TCHAR(G.ydLine().c_str()));
	}
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 300)).ColorAndOpacity(TagTheme::Gold).Text(T(TEXT("GAME PAUSED"))) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[ SNew(STextBlock).Font(TagTheme::Font(34, "Black", 10)).ColorAndOpacity(FLinearColor::White).Text(T(Score)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 60)).ColorAndOpacity(TagTheme::Dim).Text(T(Situation)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(96).HeightOverride(4)[ SNew(SBorder).BorderImage(TagTheme::Round(2.f)).BorderBackgroundColor(TagTheme::Gold) ] ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[ SNew(SBox).WidthOverride(24).HeightOverride(4)[ SNew(SBorder).BorderImage(TagTheme::Round(2.f)).BorderBackgroundColor(TagTheme::Gold * FLinearColor(1, 1, 1, 0.45f)) ] ]
		];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Resume"), TEXT(""), [this, Resume] { ClearModal(); Resume(); }) ]; // the director restores the suspended screen
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Box Score"), TEXT(""), [this, Resume] { ShowBoxScore(TEXT("Box Score"), TEXT("Resume"), [Resume] { Resume(); }); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Settings"), TEXT(""), [this, Resume, SimToEnd, Quit]
	{
		ShowSettings([this, Resume, SimToEnd, Quit]
		{
			// back to the pause menu, over the same suspended screen
			const TSharedPtr<SWidget> Keep = Suspended;
			const int32 KeepFocus = SuspendedFocus;
			ShowPause(Resume, SimToEnd, Quit);
			Suspended = Keep;
			SuspendedFocus = KeepFocus;
		});
	}) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Controls"), TEXT(""), [this, Resume, SimToEnd, Quit]
	{
		ShowControls([this, Resume, SimToEnd, Quit]
		{
			const TSharedPtr<SWidget> Keep = Suspended;
			const int32 KeepFocus = SuspendedFocus;
			ShowPause(Resume, SimToEnd, Quit);
			Suspended = Keep;
			SuspendedFocus = KeepFocus;
		});
	}) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Depth Chart"), TEXT(""), [this, Resume, SimToEnd, Quit]
	{
		// the user's team (a change takes the field from the next snap); back to the pause menu
		int32 Ti = bSelUserHome ? SelHome : SelAway;
		if (Director && Director->Game)
			for (int32 i = 0; i < static_cast<int32>(Director->League.size()); ++i)
				if (Director->League[i].get() == (bSelUserHome ? Director->Game->home : Director->Game->away)) Ti = i;
		ShowDepthChart(Ti, [this, Resume, SimToEnd, Quit]
		{
			const TSharedPtr<SWidget> Keep = Suspended;
			const int32 KeepFocus = SuspendedFocus;
			ShowPause(Resume, SimToEnd, Quit);
			Suspended = Keep;
			SuspendedFocus = KeepFocus;
		});
	}) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Sim to End of Game"), TEXT(""), [this, SimToEnd] { ClearModal(); SimToEnd(); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Quit to Main Menu"), TEXT(""), [this, Quit] { ClearModal(); Quit(); }) ];
	// the frozen picture behind it, blurred and darkened (the field stays readable as a backdrop, the menu card on
	// the left, as a broadcast game's pause is laid out), the button bar at the foot
	TSharedRef<SWidget> Footer = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.006f, 0.008f, 0.014f, 0.86f)).Padding(FMargin(96, 14, 96, 14))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ HintItem(EPadGlyph::DUpDown, TEXT("↑↓"), TEXT("Move")) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ HintItem(EPadGlyph::Cross, TEXT("Enter"), TEXT("Select")) ]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ HintItem(EPadGlyph::None, PadGlyphs() ? (bSonyPad ? TEXT("OPTIONS") : TEXT("MENU")) : TEXT("Esc"), TEXT("Resume")) ]
	];
	ShowModal(SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBackgroundBlur).BlurStrength(14.f) ]
		+ SOverlay::Slot()[ SNew(TagTheme::SBackdrop).LeftWeighted(true).Strength(0.8f) ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(96, 0, 0, 40))
		[ SNew(TagTheme::SPanel).Fill(FLinearColor(0.02f, 0.026f, 0.044f, 0.9f)).AccentTop(TagTheme::Gold).Padding(FMargin(30, 26))[V] ]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)[ Footer ]);
}

void SGridironOverlay::ShowSettings(std::function<void()> Back)
{
	ATagAudio::Screen(TEXT("Settings"));
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)[ TagTheme::Title(TEXT("Options"), TEXT("Settings"), 34) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Controls"), TEXT("Every button in the game on your controller; move any action to any button."),
		[this, Back] { ShowControls([this, Back] { ShowSettings(Back); }); }) ];
	const bool bCc = Director && Director->CaptionsOn();
	V->AddSlot().AutoHeight()[ MakeButton(FString::Printf(TEXT("Commentary captions: %s"), bCc ? TEXT("On") : TEXT("Off")),
		TEXT("The booth's words as text at the bottom of the screen."), [this, Back, bCc] { if (Director) Director->SetCaptionsOn(!bCc); ShowSettings(Back); }) ];
	const bool bInv = Director && Director->InvertRightY();
	V->AddSlot().AutoHeight()[ MakeButton(FString::Printf(TEXT("Right stick up / down: %s"), bInv ? TEXT("Inverted") : TEXT("Standard")),
		TEXT("If the hit stick, truck and kick swing go the wrong way on your controller, flip this."), [this, Back, bInv] { if (Director) Director->SetInvertRightY(!bInv); ShowSettings(Back); }) ];
	// the pad's rumble (Game/TagRumble.h): Normal, Low, Off
	V->AddSlot().AutoHeight()[ MakeButton(FString::Printf(TEXT("Vibration: %s"), FTagRumble::LevelName(FTagRumble::Level())),
		TEXT("The controller's rumble on hits, catches, kicks and scores."), [this, Back] { FTagRumble::StepLevel(); ShowSettings(Back); }) ];
	static const TCHAR* const VolName[3] = {TEXT("Volume"), TEXT("Music volume"), TEXT("Stadium volume")};
	static const TCHAR* const VolSub[3] = {TEXT("Everything."), TEXT("The broadcast theme and the menu music."), TEXT("The crowd, the field, the stadium announcer.")};
	for (int32 W = 0; W < 3; ++W)
		V->AddSlot().AutoHeight()[ MakeButton(FString::Printf(TEXT("%s: %d%%"), VolName[W], ATagAudio::Level(W) * 25), VolSub[W],
			[this, Back, W] { ATagAudio::StepLevel(W); ShowSettings(Back); }) ];
	// the booth's two voices (Booth/TagBooth.h), under Volume as the others are
	V->AddSlot().AutoHeight()[ MakeButton(FString::Printf(TEXT("Commentary volume: %d%%"), ATagBooth::CommentaryLevel() * 25), TEXT("The two voices in the booth."),
		[this, Back] { ATagBooth::StepCommentaryLevel(); ShowSettings(Back); }) ];
	V->AddSlot().AutoHeight().Padding(0, 10, 0, 0)[ MakeButton(TEXT("Back"), TEXT(""), [Back] { Back(); }) ];
	ShowModal(SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(TagTheme::SBackdrop).Strength(0.9f) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(TagTheme::SPanel).AccentTop(TagTheme::Gold).Padding(FMargin(32, 28))[V] ]);
}

void SGridironOverlay::ShowBoxScore(const FString& Title, const FString& Button, std::function<void()> Done)
{
	const tag::Game& G = *Director->Game;
	const tag::TeamLine& A = G.team[tag::TeamKey::Away];
	const tag::TeamLine& H = G.team[tag::TeamKey::Home];
	auto Top = [](double s) { return FString::Printf(TEXT("%d:%02d"), static_cast<int>(s) / 60, static_cast<int>(s) % 60); };
	struct Row { FString L, A, H; };
	TArray<Row> Rows = {
		{TEXT("Total yards"), FString::FromInt(A.yards), FString::FromInt(H.yards)},
		{TEXT("Plays"), FString::FromInt(A.plays), FString::FromInt(H.plays)},
		{TEXT("Passing"), FString::Printf(TEXT("%d/%d, %d"), A.passComp, A.passAtt, A.passYds), FString::Printf(TEXT("%d/%d, %d"), H.passComp, H.passAtt, H.passYds)},
		{TEXT("Rushing"), FString::Printf(TEXT("%d-%d"), A.rushAtt, A.rushYds), FString::Printf(TEXT("%d-%d"), H.rushAtt, H.rushYds)},
		{TEXT("First downs"), FString::FromInt(A.firstDowns), FString::FromInt(H.firstDowns)},
		{TEXT("3rd down"), FString::Printf(TEXT("%d/%d"), A.thirdConv, A.thirdAtt), FString::Printf(TEXT("%d/%d"), H.thirdConv, H.thirdAtt)},
		{TEXT("Sacks allowed"), FString::FromInt(A.sacks), FString::FromInt(H.sacks)},
		{TEXT("Turnovers"), FString::FromInt(A.turnovers), FString::FromInt(H.turnovers)},
		{TEXT("Penalties"), FString::Printf(TEXT("%d-%d"), A.penalties, A.penYds), FString::Printf(TEXT("%d-%d"), H.penalties, H.penYds)},
		{TEXT("Time of poss."), Top(A.top), Top(H.top)},
	};
	// the broadcast's stat table: the away team's number, the stat, the home team's number; banded rows
	TSharedRef<SVerticalBox> Grid = SNew(SVerticalBox);
	for (int i = 0; i < Rows.Num(); ++i)
		Grid->AddSlot().AutoHeight()
		[
			SNew(SBorder).BorderImage(TagTheme::Round(3.f)).BorderBackgroundColor(FLinearColor(1, 1, 1, i % 2 ? 0.f : 0.035f)).Padding(FMargin(18, 5))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Left)[ SNew(STextBlock).Font(TagTheme::Font(16, "BoldCondensed", 20)).ColorAndOpacity(FLinearColor::White).Text(T(Rows[i].A)) ]
				+ SHorizontalBox::Slot().FillWidth(1.4f).HAlign(HAlign_Center)[ SNew(STextBlock).Font(TagTheme::Font(12, "BoldCondensed", 160)).ColorAndOpacity(TagTheme::Dim).Text(T(Rows[i].L.ToUpper())) ]
				+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right)[ SNew(STextBlock).Font(TagTheme::Font(16, "BoldCondensed", 20)).ColorAndOpacity(FLinearColor::White).Text(T(Rows[i].H)) ]
			]
		];
	// individual leaders
	auto Leaders = [&](const tag::Team& Tm) -> FString
	{
		FString Out;
		const auto Lines = G.stats.forTeam(Tm.abbr);
		const tag::PlayerLine* QB = nullptr; const tag::PlayerLine* RB = nullptr; const tag::PlayerLine* WR = nullptr; const tag::PlayerLine* DF = nullptr;
		for (const tag::PlayerLine* L : Lines)
		{
			if (L->passAtt && (!QB || L->passAtt > QB->passAtt)) QB = L;
			if (L->rushAtt && (!RB || L->rushYds > RB->rushYds)) RB = L;
			if (L->rec && (!WR || L->recYds > WR->recYds)) WR = L;
			if ((L->tackles + L->sacks * 2) && (!DF || L->tackles + L->sacks * 2 > DF->tackles + DF->sacks * 2)) DF = L;
		}
		if (QB) Out += FString::Printf(TEXT("%s  %d/%d, %d yds, %d TD, %d INT\n"), *S(QB->name), QB->passComp, QB->passAtt, QB->passYds, QB->passTD, QB->passInt);
		if (RB) Out += FString::Printf(TEXT("%s  %d car, %d yds, %d TD\n"), *S(RB->name), RB->rushAtt, RB->rushYds, RB->rushTD);
		if (WR) Out += FString::Printf(TEXT("%s  %d rec, %d yds, %d TD\n"), *S(WR->name), WR->rec, WR->recYds, WR->recTD);
		if (DF) Out += FString::Printf(TEXT("%s  %d tkl, %.1f sck, %d INT"), *S(DF->name), DF->tackles, static_cast<double>(DF->sacks), DF->ints);
		return Out;
	};
	// each team: its mark on its colour, its name, its score (the winner's score in white, the loser's dimmed at a final)
	const bool bFinal = G.isFinal();
	auto TeamBlock = [&](const tag::Team& Tm, int32 Pts, int32 OppPts, bool bRight) -> TSharedRef<SWidget>
	{
		const FLinearColor Pri = RosterUI::TeamInk(Tm);
		const FSlateBrush* LogoB = RosterUI::Logo(Tm);
		TSharedRef<SWidget> Mark = SNew(SBox).WidthOverride(76).HeightOverride(76)
			[ LogoB ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(LogoB)) : StaticCastSharedRef<SWidget>(SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(Pri)) ];
		TSharedRef<SWidget> Name = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(TagTheme::Font(14, "BoldCondensed", 120)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.75f)).Text(T(S(Tm.city).ToUpper())) ]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(TagTheme::Font(28, "Black")).ColorAndOpacity(FLinearColor::White).Text(T(S(Tm.name).ToUpper())) ];
		TSharedRef<SWidget> Score = SNew(STextBlock).Font(TagTheme::Font(58, "Black")).Text(FText::AsNumber(Pts))
			.ColorAndOpacity(bFinal && Pts < OppPts ? FLinearColor(1, 1, 1, 0.55f) : FLinearColor::White).ShadowOffset(FVector2D(0, 2)).ShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.4f));
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		if (!bRight)
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Mark ];
			Row->AddSlot().FillWidth(1).VAlign(VAlign_Center).Padding(14, 0)[ Name ];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Score ];
		}
		else
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Score ];
			Row->AddSlot().FillWidth(1).VAlign(VAlign_Center).HAlign(HAlign_Right).Padding(14, 0)[ Name ];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Mark ];
		}
		return SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(Pri * FLinearColor(0.78f, 0.78f, 0.78f, 0.95f)).Padding(FMargin(16, 10))[ Row ];
	};
	auto LeaderCol = [&](const tag::Team& Tm) -> TSharedRef<SWidget>
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[ SNew(SBox).WidthOverride(4).HeightOverride(16)[ SNew(SBorder).BorderImage(TagTheme::Round(2.f)).BorderBackgroundColor(RosterUI::TeamInk(Tm)) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 160)).ColorAndOpacity(TagTheme::Gold).Text(T(S(Tm.abbr) + TEXT("  LEADERS"))) ]
			]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(13)).LineHeightPercentage(1.15f).ColorAndOpacity(FLinearColor(0.88f, 0.9f, 0.94f)).Text(T(Leaders(Tm))) ];
	};
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
		[ SNew(STextBlock).Font(TagTheme::Font(14, "BoldCondensed", 360)).ColorAndOpacity(TagTheme::Gold).Text(T(Title.ToUpper())) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)[ TeamBlock(*G.away, G.score[tag::TeamKey::Away], G.score[tag::TeamKey::Home], false) ]
			+ SHorizontalBox::Slot().FillWidth(1).Padding(6, 0, 0, 0)[ TeamBlock(*G.home, G.score[tag::TeamKey::Home], G.score[tag::TeamKey::Away], true) ]
		]
		+ SVerticalBox::Slot().AutoHeight()[ Grid ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)[ RosterUI::Fill(FLinearColor(1, 1, 1, 0.08f)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 16, 0)[ LeaderCol(*G.away) ]
			+ SHorizontalBox::Slot().FillWidth(1)[ LeaderCol(*G.home) ]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 0)[ MakeButton(Button, TEXT(""), [this, Done] { ClearModal(); Done(); }, 300) ];
	ShowModal(SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(TagTheme::SBackdrop).Strength(1.1f) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[ SNew(SBox).WidthOverride(1000)[ SNew(TagTheme::SPanel).AccentTop(TagTheme::Gold).Padding(FMargin(28, 24))[V] ] ]);
}

int32 SGridironOverlay::PaintKickMeter(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
{
	const FKickMeterUI& K = KickMeter;
	if (!K.bOn) return Layer;
	const FVector2D Local = G.GetLocalSize();
	const FVector2D Sc(Local.X / FMath::Max(1.0, K.Viewport.X), Local.Y / FMath::Max(1.0, K.Viewport.Y));
	auto Box = [&](const FVector2D& TL, const FVector2D& Sz, const FLinearColor& C, int32 L)
	{
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(TL))), White(), ESlateDrawEffect::None, C);
	};
	// the aim arrow: the flight the kick is lined up on, gold, with a head where it comes down / reaches the posts
	if (K.Arc.Num() > 1 && K.Stage < 3)
	{
		TArray<FVector2f> P;
		for (const FVector2D& A : K.Arc) P.Add(FVector2f(A * Sc));
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), P, ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.45f), true, 7.f);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), P, ESlateDrawEffect::None, Accent, true, 3.5f);
		const FVector2f E = P.Last(), B = P[FMath::Max(0, P.Num() - 3)];
		const FVector2f D = (E - B).GetSafeNormal(), N(-D.Y, D.X);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), {E - D * 18.f + N * 10.f, E, E - D * 18.f - N * 10.f}, ESlateDrawEffect::None, Accent, true, 3.5f);
	}
	// the meter: bottom centre
	// (the title on its own line, the kind of kick under it: side by side they ran into each other)
	const float W = 540.f, H = 146.f;
	const FVector2D TL((Local.X - W) * 0.5f, Local.Y - H - 150.f); // (above the score bug and the hint line)
	TagTheme::PaintPanel(Out, Layer + 1, G, TL, FVector2D(W, H), FLinearColor(0.02f, 0.025f, 0.04f, 0.9f), 7.f);
	TagTheme::Box(Out, Layer + 3, G, TL + FVector2D(0, H - 4), FVector2D(W, 4), Accent, 2.f);
	const FSlateFontInfo Ft = TagTheme::Font(17, "BoldCondensed", 40), Fs = TagTheme::Font(10, "BoldCondensed", 120);
	FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(W - 28, 22), FSlateLayoutTransform(FVector2f(TL + FVector2D(16, 9)))), K.Title.ToUpper(), Ft, ESlateDrawEffect::None, FLinearColor::White);
	if (!K.Sub.IsEmpty())
		FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(W - 28, 16), FSlateLayoutTransform(FVector2f(TL + FVector2D(16, 33)))), K.Sub.ToUpper(), Fs, ESlateDrawEffect::None, Accent);
	// power: 0..1.15 across, the last 13% red
	const FVector2D PB = TL + FVector2D(16, 70);
	const float PW = W - 32, Red = PW * (1.f / 1.15f);
	Box(PB, FVector2D(PW, 20), FLinearColor(0.12f, 0.13f, 0.16f, 1.f), Layer + 3);
	Box(PB + FVector2D(Red, 0), FVector2D(PW - Red, 20), FLinearColor(0.45f, 0.06f, 0.05f, 1.f), Layer + 3);
	Box(PB + FVector2D(Red * 0.9f, 0), FVector2D(Red * 0.1f, 20), FLinearColor(0.07f, 0.3f, 0.12f, 1.f), Layer + 3);
	const float Fill = FMath::Clamp(K.Power / 1.15f, 0.f, 1.f) * PW;
	const FLinearColor FillC = K.Power > 1.f ? FLinearColor(1.f, 0.22f, 0.15f) : K.Power > 0.9f ? FLinearColor(0.3f, 1.f, 0.45f) : FLinearColor(1.f, 1.f, 1.f, 0.92f);
	Box(PB + FVector2D(0, 4), FVector2D(Fill, 12), FillC, Layer + 4);
	FSlateDrawElement::MakeText(Out, Layer + 4, G.ToPaintGeometry(FVector2f(60, 14), FSlateLayoutTransform(FVector2f(PB + FVector2D(0, -15)))), FString(TEXT("POWER")), Fs, ESlateDrawEffect::None, Dim);
	// accuracy: the needle across, the centre band green
	const FVector2D AB = TL + FVector2D(16, 116);
	Box(AB, FVector2D(PW, 12), FLinearColor(0.12f, 0.13f, 0.16f, 1.f), Layer + 3);
	Box(AB + FVector2D(PW * 0.5f - PW * 0.075f, 0), FVector2D(PW * 0.15f, 12), FLinearColor(0.07f, 0.35f, 0.14f, 1.f), Layer + 3);
	FSlateDrawElement::MakeText(Out, Layer + 4, G.ToPaintGeometry(FVector2f(90, 14), FSlateLayoutTransform(FVector2f(AB + FVector2D(0, -15)))), FString(TEXT("ACCURACY")), Fs, ESlateDrawEffect::None, Dim);
	if (K.bAccShown || K.Stage >= 2)
	{
		const float X = AB.X + (FMath::Clamp(K.Acc, -1.f, 1.f) * 0.5f + 0.5f) * PW;
		Box(FVector2D(X - 2, AB.Y - 5), FVector2D(4, 22), FMath::Abs(K.Acc) < 0.15f ? FLinearColor(0.3f, 1.f, 0.45f) : FLinearColor::White, Layer + 5);
		if (K.Stage >= 3)
		{
			const float A = FMath::Abs(K.Acc);
			const FString R = A < 0.15f ? TEXT("PERFECT") : A < 0.4f ? (K.Acc < 0 ? TEXT("SLIGHTLY LEFT") : TEXT("SLIGHTLY RIGHT"))
				: (K.Acc < 0 ? TEXT("PULLED LEFT") : TEXT("PUSHED RIGHT"));
			const FVector2D M = Measure(R, Fs);
			FSlateDrawElement::MakeText(Out, Layer + 5, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(AB + FVector2D(PW - M.X, -15)))), R, Fs,
				ESlateDrawEffect::None, A < 0.15f ? FLinearColor(0.3f, 1.f, 0.45f) : A < 0.4f ? FLinearColor::White : FLinearColor(1.f, 0.45f, 0.35f));
		}
	}
	return Layer + 6;
}

int32 SGridironOverlay::PaintLineBars(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
{
	// the user's man: a glowing ring at his feet with a notch the way he faces, and a chevron with his number over him
	// (in a multiplayer game each player's, in his colour, with his tag over the number and his own sprint bar)
	for (const FUserMarkerUI& UserMarker : UserMarkers)
	{
		if (!UserMarker.bOn || UserMarker.Ring.Num() <= 2) continue;
		const FVector2D Local = G.GetLocalSize();
		const FVector2D Sc(Local.X / FMath::Max(1.0, UserMarker.Viewport.X), Local.Y / FMath::Max(1.0, UserMarker.Viewport.Y));
		const float Pulse = 0.8f + 0.2f * FMath::Sin(FPlatformTime::Seconds() * 5.0);
		const FLinearColor Core(UserMarker.Color.R, UserMarker.Color.G, UserMarker.Color.B, 0.95f);
		const FLinearColor Glow(UserMarker.Color.R * 0.6f, UserMarker.Color.G * 0.9f, UserMarker.Color.B, 0.35f * Pulse);
		TArray<FVector2f> R, N;
		for (const FVector2D& P : UserMarker.Ring) R.Add(FVector2f(P * Sc));
		for (const FVector2D& P : UserMarker.Notch) N.Add(FVector2f(P * Sc));
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), R, ESlateDrawEffect::None, Glow, true, 9.f);
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), R, ESlateDrawEffect::None, Core, true, 3.f);
		if (N.Num() == 3)
		{
			const FVector2f First = N[0]; // (copied: adding an element of the array to itself asserts)
			N.Add(First);
			FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), N, ESlateDrawEffect::None, Core, true, 3.f);
		}
		const FVector2f H = FVector2f(UserMarker.Head * Sc);
		const float W = 11.f, Hh = 12.f;
		FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), {H + FVector2f(-W, -Hh), H, H + FVector2f(W, -Hh), H + FVector2f(-W, -Hh)}, ESlateDrawEffect::None, Core, true, 3.f);
		float Up = Hh + 4.f;
		if (!UserMarker.Number.IsEmpty())
		{
			const FSlateFontInfo F = Font(12, "Bold");
			const FVector2D M = Measure(UserMarker.Number, F);
			FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(H + FVector2f(-M.X * 0.5f, -Up - M.Y))), UserMarker.Number, F, ESlateDrawEffect::None, Core);
			Up += M.Y + 2.f;
		}
		if (!UserMarker.Tag.IsEmpty())
		{
			// the player's tag on a chip of his colour
			const FSlateFontInfo F = Font(13, "Bold");
			const FVector2D M = Measure(UserMarker.Tag, F);
			const FVector2f TL = H + FVector2f(-M.X * 0.5f - 6.f, -Up - M.Y - 4.f);
			FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M.X + 12.f, M.Y + 4.f), FSlateLayoutTransform(TL)), White(), ESlateDrawEffect::None, Core);
			FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(TL + FVector2f(6.f, 2.f))), UserMarker.Tag, F, ESlateDrawEffect::None, FLinearColor(0.03f, 0.04f, 0.06f));
		}
		if (UserMarker.RefusedA > 0.f && !UserMarker.Refused.IsEmpty())
		{
			// a move pressed too early that ran out unused (Game/TagMoveBuffer.h): a grey pill over him, fading
			const float Fade = FMath::Clamp(UserMarker.RefusedA, 0.f, 1.f);
			const FSlateFontInfo F = Font(12, "Bold");
			const FVector2D M = Measure(UserMarker.Refused, F);
			const float Above = Up + (UserMarker.Tag.IsEmpty() ? 4.f : 30.f);
			const FVector2f TL = H + FVector2f(-M.X * 0.5f - 8.f, -Above - M.Y - 6.f);
			FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M.X + 16.f, M.Y + 6.f), FSlateLayoutTransform(TL)), White(), ESlateDrawEffect::None,
				FLinearColor(0.16f, 0.17f, 0.19f, 0.88f * Fade));
			FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(TL + FVector2f(8.f, 3.f))), UserMarker.Refused, F, ESlateDrawEffect::None,
				FLinearColor(0.72f, 0.74f, 0.78f, Fade));
		}
		if (UserMarker.StaminaV >= 0.f)
		{
			const FVector2D C = UserMarker.StaminaAt * Sc;
			const float BW = 64.f, BH = 6.f;
			const FVector2D TL(C.X - BW * 0.5f, C.Y + 6.f);
			const float V = UserMarker.StaminaV;
			const FLinearColor Fill = V < 0.2f ? FLinearColor(1.f, 0.25f, 0.2f) : V < 0.45f ? FLinearColor(1.f, 0.72f, 0.15f) : Core;
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(BW + 4, BH + 4), FSlateLayoutTransform(FVector2f(TL - FVector2D(2, 2)))), White(), ESlateDrawEffect::None, FLinearColor(0, 0, 0, 0.6f));
			FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(BW * FMath::Clamp(V, 0.f, 1.f), BH), FSlateLayoutTransform(FVector2f(TL))), White(), ESlateDrawEffect::None,
				Fill * FLinearColor(1, 1, 1, UserMarker.bSprint ? 1.f : 0.75f));
		}
	}
	Layer += 4;
	// the user's sprint stamina: a short bar under his man (green, amber when low, red run dry), brighter while he sprints
	if (StaminaV >= 0.f)
	{
		const FVector2D Local = G.GetLocalSize();
		const FVector2D C = StaminaAt * FVector2D(Local.X / FMath::Max(1.0, StaminaVp.X), Local.Y / FMath::Max(1.0, StaminaVp.Y));
		const float W = 64.f, H = 6.f;
		const FVector2D TL(C.X - W * 0.5f, C.Y + 6.f);
		auto Bx = [&](const FVector2D& P, const FVector2D& Sz, const FLinearColor& Cl, int32 L)
		{
			FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(P))), White(), ESlateDrawEffect::None, Cl);
		};
		const FLinearColor Fill = StaminaV < 0.2f ? FLinearColor(1.f, 0.25f, 0.2f) : StaminaV < 0.45f ? FLinearColor(1.f, 0.72f, 0.15f) : FLinearColor(0.35f, 0.95f, 0.45f);
		Bx(TL - FVector2D(2, 2), FVector2D(W + 4, H + 4), FLinearColor(0, 0, 0, 0.6f), Layer);
		Bx(TL, FVector2D(W, H), FLinearColor(0.12f, 0.13f, 0.16f, 0.9f), Layer + 1);
		Bx(TL, FVector2D(W * FMath::Clamp(StaminaV, 0.f, 1.f), H), Fill * FLinearColor(1, 1, 1, bStaminaSprint ? 1.f : 0.75f), Layer + 2);
		Layer += 3;
	}
	if (!LineBars.Num()) return Layer;
	const FVector2D Local = G.GetLocalSize();
	const FVector2D Sc(Local.X / FMath::Max(1.0, LineViewport.X), Local.Y / FMath::Max(1.0, LineViewport.Y));
	auto Box = [&](const FVector2D& TL, const FVector2D& Sz, const FLinearColor& C, int32 L)
	{
		FSlateDrawElement::MakeBox(Out, L, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(TL))), White(), ESlateDrawEffect::None, C);
	};
	const FSlateFontInfo Fm = Font(10, "Bold");
	for (const FLineBarUI& B : LineBars)
	{
		const FVector2D C = B.Screen * Sc;
		const float W = B.bUser ? 92.f : 56.f, H = B.bUser ? 7.f : 5.f;
		// the rusher's energy (blue, top) and the blocker's power (orange, below), a tick between for who is winning
		const FVector2D T0(C.X - W * 0.5f, C.Y - H * 2.f - 4.f);
		Box(T0 - FVector2D(2, 2), FVector2D(W + 4, H * 2.f + 7.f), FLinearColor(0, 0, 0, 0.55f), Layer);
		Box(T0, FVector2D(W, H), FLinearColor(0.1f, 0.12f, 0.18f, 0.9f), Layer + 1);
		Box(T0, FVector2D(W * FMath::Clamp(B.Energy, 0.f, 1.f), H), FLinearColor(0.25f, 0.65f, 1.f), Layer + 2);
		const FVector2D T1 = T0 + FVector2D(0, H + 3.f);
		Box(T1, FVector2D(W, H), FLinearColor(0.18f, 0.12f, 0.08f, 0.9f), Layer + 1);
		Box(T1, FVector2D(W * FMath::Clamp(B.Power, 0.f, 1.f), H), FLinearColor(1.f, 0.55f, 0.15f), Layer + 2);
		const float Ex = T0.X + (FMath::Clamp(B.Edge, -1.f, 1.f) * 0.5f + 0.5f) * W;
		Box(FVector2D(Ex - 1.5f, T0.Y - 3.f), FVector2D(3.f, H * 2.f + 9.f), FLinearColor::White, Layer + 3);
		if (!B.Move.IsEmpty())
		{
			const FVector2D M = Measure(B.Move, Fm);
			const FVector2D Mt(C.X - M.X * 0.5f, T0.Y - M.Y - 6.f);
			Box(Mt - FVector2D(5, 2), FVector2D(M.X + 10, M.Y + 4), B.bMoveWon ? FLinearColor(0.1f, 0.45f, 0.18f, 0.9f) : FLinearColor(0.45f, 0.08f, 0.06f, 0.9f), Layer + 1);
			FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(Mt))), B.Move, Fm, ESlateDrawEffect::None, FLinearColor::White);
		}
	}
	return Layer + 4;
}
