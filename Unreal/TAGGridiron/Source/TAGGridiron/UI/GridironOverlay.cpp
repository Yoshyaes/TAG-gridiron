#include "UI/GridironOverlay.h"

#include "Game/MatchDirector.h"
#include "Game/PlayArtStrokes.h"
#include "UI/PlayDiagram.h"
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
#include "Widgets/Text/STextBlock.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"

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
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(FMargin(14, 10))
			.Visibility_Lambda([this] { return bHud && !bBroadcastOnly && !bModal ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SBox).WidthOverride(560)
				[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(0.92f, 0.94f, 0.97f)).AutoWrapText(true).Text_Lambda([this] { return TickerText(); }) ]
			]
		]
		// weather (top-right)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(FMargin(14, 8))
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
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.07f, 0.09f, 0.13f, 0.9f)).Padding(FMargin(18, 8))
				.Visibility_Lambda([this] { return !IsStatLine() && ToastT < (ToastText.Len() > 14 ? 3.2f : 1.3f) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				[ SNew(STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(Accent).Text_Lambda([this] { return T(ToastText); }) ]
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
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(FMargin(16, 8))
			.Visibility_Lambda([this] { return (bHud && !HintText.IsEmpty() && !bModal) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[ SNew(STextBlock).Font(Font(13)).ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.92f)).Text_Lambda([this] { return T(HintText); }) ]
		]
		// scorebug
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 0, 28))
		[ MakeScoreBug() ]
		// modal content
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[ SAssignNew(ModalBox, SBox).Visibility_Lambda([this] { return bModal ? EVisibility::Visible : EVisibility::Collapsed; }) ]
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
	if (!bModal) Layer = PaintKickMeter(OutDrawElements, Layer + 1, AllottedGeometry);
	// the focused menu button (gamepad / keyboard navigation) gets a gold frame
	if (bModal && FSlateApplication::IsInitialized())
	{
		const TSharedPtr<SWidget> F = FSlateApplication::Get().GetUserFocusedWidget(0);
		if (F.IsValid() && F != ModalBox && F != CallRoot) // (the play call is driven by its buttons, not a focus frame)
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
				const float Tk = 3.f;
				const FSlateBrush* B = FCoreStyle::Get().GetBrush("WhiteBrush");
				auto Edge = [&](const FVector2D& At, const FVector2D& Sz)
				{
					FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1,
						AllottedGeometry.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(TL + At))), B, ESlateDrawEffect::None, Accent);
				};
				Edge(FVector2D(-Tk, -Tk), FVector2D(Size.X + 2 * Tk, Tk));
				Edge(FVector2D(-Tk, Size.Y), FVector2D(Size.X + 2 * Tk, Tk));
				Edge(FVector2D(-Tk, 0), FVector2D(Tk, Size.Y));
				Edge(FVector2D(Size.X, 0), FVector2D(Tk, Size.Y));
				++Layer;
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
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(C - Sz * 0.5))), White(),
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
		Box(FVector2D(Size * 1.36f + 3, Size * 0.8f + 3), Tint * FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
		Box(FVector2D(Size * 1.36f, Size * 0.8f), Dark);
		Text(bSonyPad ? (Gl == EPadGlyph::R1 ? TEXT("R1") : Gl == EPadGlyph::L1 ? TEXT("L1") : Gl == EPadGlyph::L2 ? TEXT("L2") : TEXT("R2"))
			: (Gl == EPadGlyph::R1 ? TEXT("RB") : Gl == EPadGlyph::L1 ? TEXT("LB") : Gl == EPadGlyph::L2 ? TEXT("LT") : TEXT("RT")), FMath::RoundToInt(Size * 0.36f), Tint);
		break;
	default:
	{
		// the D-pad, the pressed arm lit
		const FLinearColor Arm(0.32f, 0.34f, 0.4f, 1.f);
		const float A = R * 0.86f, W = R * 0.52f;
		Lines({P2(-A, 0), P2(A, 0)}, Arm, W, 0);
		Lines({P2(0, -A), P2(0, A)}, Arm, W, 0);
		const FVector2f D = Gl == EPadGlyph::DUp ? FVector2f(0, -1) : Gl == EPadGlyph::DDown ? FVector2f(0, 1) : Gl == EPadGlyph::DLeft ? FVector2f(-1, 0) : FVector2f(1, 0);
		Lines({P2(D.X * R * 0.18f, D.Y * R * 0.18f), P2(D.X * A, D.Y * A)}, Tint, W, 1);
		break;
	}
	}
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
		FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(Pill.X, 3.f), FSlateLayoutTransform(FVector2f(TL + FVector2D(0, Pill.Y - 3)))), White(),
			ESlateDrawEffect::None, Accent);
		PaintGlyph(Out, Layer + 3, G, TL + FVector2D(8 + Gs * 0.5, Pill.Y * 0.5), Gs, Highlight.Glyph, Highlight.Key,
			Highlight.Glyph != EPadGlyph::None && bIconPad, FLinearColor::White);
		FSlateDrawElement::MakeText(Out, Layer + 6, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(TL + FVector2D(8 + Gs + 8, (Pill.Y - M.Y) * 0.5)))),
			Highlight.Move, F, ESlateDrawEffect::None, FLinearColor::White);
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
			const float Sz = 30.f;
			if (I.bPrimary || I.bPicked)
			{
				TArray<FVector2f> P;
				for (int32 k = 0; k <= 32; ++k)
					P.Add(FVector2f(C.X + FMath::Cos(2.f * PI * k / 32) * (Sz * 0.62f), C.Y + FMath::Sin(2.f * PI * k / 32) * (Sz * 0.62f)));
				FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), P, ESlateDrawEffect::None, I.bPicked ? FLinearColor::White : Accent, true, 3.f);
			}
			PaintGlyph(Out, Layer, G, C, Sz, Face[FMath::Clamp(I.Button, 0, 4)], FString::FromInt(I.Button + 1), bIconPad, ButtonColor(I.Button));
			if (I.bHot)
			{
				const FSlateFontInfo F = Font(9, "Bold");
				const FVector2D M = Measure(TEXT("HOT"), F);
				FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(C + FVector2D(-M.X * 0.5, Sz * 0.58)))),
					FString(TEXT("HOT")), F, ESlateDrawEffect::None, Accent);
			}
		}
	}
	if (PreRows.Num() == 0) return Layer + 4;
	// the pre-snap panel, bottom right (over the icons)
	Layer += 6;
	const float W = 480.f, RowH = 34.f, Head = PreSub.IsEmpty() ? 44.f : 62.f;
	const float H = Head + PreRows.Num() * RowH + 12.f;
	const FVector2D TL(Local.X - W - 30.f, Local.Y - H - 128.f);
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(TL))), White(), ESlateDrawEffect::None, Panel);
	FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(4.f, H), FSlateLayoutTransform(FVector2f(TL))), White(), ESlateDrawEffect::None, Accent);
	FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W - 30, 24), FSlateLayoutTransform(FVector2f(TL + FVector2D(20, 12)))), PreTitle,
		Font(15, "Bold"), ESlateDrawEffect::None, Accent);
	if (!PreSub.IsEmpty())
		FSlateDrawElement::MakeText(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W - 30, 18), FSlateLayoutTransform(FVector2f(TL + FVector2D(20, 36)))), PreSub,
			Font(11), ESlateDrawEffect::None, Dim);
	for (int32 i = 0; i < PreRows.Num(); ++i)
	{
		const FPreRow& Rw = PreRows[i];
		const FVector2D RowTL = TL + FVector2D(8, Head + i * RowH);
		if (Rw.bOn)
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W - 16, RowH - 4), FSlateLayoutTransform(FVector2f(RowTL))), White(),
				ESlateDrawEffect::None, FLinearColor(1.f, 0.83f, 0.f, 0.13f));
		PaintGlyph(Out, Layer + 1, G, RowTL + FVector2D(30, (RowH - 4) * 0.5f), 26.f, Rw.Glyph, Rw.Key, bPrePad, Rw.Tint);
		FSlateDrawElement::MakeText(Out, Layer + 4, G.ToPaintGeometry(FVector2f(W - 90, RowH), FSlateLayoutTransform(FVector2f(RowTL + FVector2D(64, 6)))), Rw.Label,
			Font(13, Rw.bOn ? "Bold" : "Regular"), ESlateDrawEffect::None, Rw.bOn ? Accent : FLinearColor(0.9f, 0.92f, 0.95f));
	}
	return Layer + 6;
}

void SGridironOverlay::Banner(const FString& Text, const FString& Sub, const FLinearColor& Color)
{
	BannerText = Text;
	BannerSub = Sub;
	BannerColor = Color;
	BannerT = 0.f;
}

void SGridironOverlay::Toast(const FString& Text)
{
	ToastText = Text;
	ToastT = 0.f;
}

TSharedRef<SWidget> SGridironOverlay::MakeButton(const FString& Label, const FString& Sub, std::function<void()> OnClick, float Width)
{
	return SNew(SBox).WidthOverride(Width).Padding(FMargin(0, 4))
	[
		SNew(SButton)
		.ButtonColorAndOpacity(PanelLight)
		.ContentPadding(FMargin(16, 10))
		.OnClicked_Lambda([OnClick] { OnClick(); return FReply::Handled(); })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(FLinearColor::White).Text(T(Label)) ]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).AutoWrapText(true).Text(T(Sub)).Visibility(Sub.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible) ]
		]
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
static TSharedRef<SWidget> Centered(TSharedRef<SWidget> Inner, float BgAlpha = 0.82f)
{
	return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.025f, BgAlpha)).HAlign(HAlign_Center).VAlign(VAlign_Center)[Inner];
}

void SGridironOverlay::ShowMainMenu()
{
	bHud = false;
	TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(Font(72, "Bold")).ColorAndOpacity(Accent).ShadowOffset(FVector2D(3, 3)).Text(T(TEXT("TAG GRIDIRON"))) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 36)[ SNew(STextBlock).Font(Font(14)).ColorAndOpacity(Dim).Text(T(TEXT("SIMULATION-FIRST PRO FOOTBALL"))) ];
	Menu->AddSlot().AutoHeight().HAlign(HAlign_Center)[ MakeButton(TEXT("Play Now"), TEXT("You vs CPU. Call plays, control the QB, ball carrier or any defender."), [this] { ShowTeamSelect(true); }) ];
	Menu->AddSlot().AutoHeight().HAlign(HAlign_Center)[ MakeButton(TEXT("Watch a Game"), TEXT("CPU vs CPU broadcast. Speed up with 1/2/4."), [this] { ShowTeamSelect(false); }) ];
	Menu->AddSlot().AutoHeight().HAlign(HAlign_Center)[ MakeButton(TEXT("Settings"), TEXT("Commentary captions."), [this] { ShowSettings([this] { ShowMainMenu(); }); }) ];
	Menu->AddSlot().AutoHeight().HAlign(HAlign_Center)[ MakeButton(TEXT("Quit"), TEXT(""), [] { FPlatformMisc::RequestExit(false); }) ];
	Menu->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 30, 0, 0)[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text(T(TEXT("An original football game. All teams, players and names are fictional."))) ];
	ShowModal(Centered(Menu, 0.35f));
	// -TagTeamSelect: straight on to the Play Now team select, once (screen checks)
	static bool bTeamSelectShot = FParse::Param(FCommandLine::Get(), TEXT("TagTeamSelect"));
	if (bTeamSelectShot)
	{
		bTeamSelectShot = false;
		ShowTeamSelect(true);
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
			const bool bPad = Owner && Owner->PadGlyphs();
			if (bPad || Key.Len() <= 1 || !FSlateApplication::IsInitialized()) return FVector2D(Size + 4, Size + 4);
			const FVector2D M = Measure(Key, Font(FMath::RoundToInt(Size * 0.42f), "Bold"));
			return FVector2D(FMath::Max<double>(Size, M.X + 12) + 4, Size + 4);
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
}

bool SGridironOverlay::PadGlyphs() const { return Director && Director->PadUI(); }

void SGridironOverlay::ShowPlayCall(bool bOffense, bool bFourthDown, bool bTwoPoint, std::function<void(FCallChoice)> Done)
{
	bCallOffense = bOffense;
	bCallFourth = bFourthDown;
	bCallTwo = bTwoPoint;
	CallDone = Done;
	CallPage = 0;
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
	CallRoot.Reset();
	ClearModal();
	if (Done) Done(C);
}

bool SGridironOverlay::CallKey(const FKey& K, bool bRepeat)
{
	if (!CallDone) return false;
	auto Is = [&K](std::initializer_list<FKey> Ks) { for (const FKey& X : Ks) if (K == X) return true; return false; };
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(CallCards.Num(), 3));
	int32 Slot = -1;
	if (Is({EKeys::Gamepad_FaceButton_Left, EKeys::One, EKeys::NumPadOne})) Slot = 0;
	else if (Is({EKeys::Gamepad_FaceButton_Bottom, EKeys::Two, EKeys::NumPadTwo, EKeys::SpaceBar, EKeys::Enter})) Slot = 1;
	else if (Is({EKeys::Gamepad_FaceButton_Right, EKeys::Three, EKeys::NumPadThree})) Slot = 2;
	if (Slot >= 0)
	{
		if (bRepeat) return true;
		const int32 I = CallPage * 3 + Slot;
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
			BuildPlayCall();
		}
		return true;
	}
	const bool bPrevPage = Is({EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A});
	const bool bNextPage = Is({EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D});
	if (bPrevPage || bNextPage)
	{
		if (bRepeat) return true;
		if (Pages > 1)
		{
			CallPage = (CallPage + (bNextPage ? 1 : -1) + Pages) % Pages;
			BuildPlayCall();
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
	auto Add = [this](const FString& Kind, const FString& Name, const FString& Note, const tag::OffPlay* Op, const tag::DefCall* Dc, FCallChoice C)
	{
		FCallCard& K = CallCards.AddDefaulted_GetRef();
		K.Kind = Kind;
		K.Name = Name;
		K.Note = Note;
		K.Op = Op;
		K.Dc = Dc;
		K.Choice = C;
	};
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
		// the family's formations in the book, the one shown, and its plays
		const tag::OffBook& Book = tag::offBookFor(G.offense());
		CallSets.Reset();
		for (const std::string& Fid : Book.formations)
			if (const tag::Formation* F = tag::formationById(Fid); F && S(tag::formationFamily(*F)) == CallTab) CallSets.Add(S(Fid));
		if (!CallSets.Contains(CallSet)) CallSet = CallSets.Num() ? CallSets[0] : FString();
		for (const tag::OffPlay* PP : Book.playsIn(TCHAR_TO_UTF8(*CallSet)))
		{
			const tag::OffPlay& P = *PP;
			if (P.type == tag::PlayType::Kneel || P.type == tag::PlayType::Spike) continue;
			FString Kind = P.type == tag::PlayType::Run ? TEXT("Run") : P.type == tag::PlayType::PlayAction ? TEXT("Play action") : P.type == tag::PlayType::Screen ? TEXT("Screen") : TEXT("Pass");
			if (P.hasTag("quick")) Kind += TEXT("  -  quick");
			if (P.hasTag("deep")) Kind += TEXT("  -  shot");
			FString Note;
			for (const std::string& R : P.reads) Note += (Note.IsEmpty() ? TEXT("") : TEXT("  ")) + S(R);
			FCallChoice C; C.Kind = FCallChoice::Off; C.Play = &P;
			Add(Kind, S(P.name), Note.IsEmpty() ? FString() : TEXT("Reads  ") + Note, &P, nullptr, C);
		}
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
		if (!CallSets.Contains(CallSet)) CallSet = CallSets.Num() ? CallSets[0] : FString();
		for (const tag::DefCall* DP : Book.callsIn(TCHAR_TO_UTF8(*CallSet)))
		{
			const tag::DefCall& D = *DP;
			const FString* Desc = Cov.Find(FString(tag::coverageName(D.coverage)));
			FCallChoice C; C.Kind = FCallChoice::Def; C.Call = &D;
			Add(D.rushers() <= 4 ? FString::Printf(TEXT("Coverage  -  %d rush"), D.rushers()) : FString::Printf(TEXT("%d-man pressure"), D.rushers()), S(D.name),
				Desc ? *Desc : FString(), nullptr, &D, C);
		}
	}
	const int32 Pages = FMath::Max(1, FMath::DivideAndRoundUp(CallCards.Num(), 3));
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
	if (!CallSet.IsEmpty() && CallTab != TEXT("special") && CallTab != TEXT("clock"))
	{
		// the formation (set) shown, and where it sits in its family
		const tag::Formation* F = bOffense ? tag::formationById(TCHAR_TO_UTF8(*CallSet)) : nullptr;
		TabLabel = (F ? S(F->name) : CallSet).ToUpper();
		if (CallSets.Num() > 1) TabLabel += FString::Printf(TEXT("   %d / %d"), CallSets.IndexOfByKey(CallSet) + 1, CallSets.Num());
	}
	const FString BookLine = bOffense ? FString::Printf(TEXT("%s PLAYBOOK  -  %s"), *S(tag::offBookFor(G.offense()).name).ToUpper(), *S(tag::offBookFor(G.offense()).scheme).ToUpper())
		: FString::Printf(TEXT("%s PLAYBOOK  -  %s"), *S(tag::defBookFor(G.defense()).name).ToUpper(), *S(tag::defBookFor(G.defense()).scheme).ToUpper());
	const FString Sit = bTwoPoint ? FString(TEXT("TWO-POINT TRY  -  FROM THE 2"))
		: FString::Printf(TEXT("%s  -  %s"), *S(G.downText()).ToUpper(), *S(G.ydLine()));
	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Accent)
				.Text(T(FString(bOffense ? (bTwoPoint ? TEXT("TWO-POINT TRY  -  CALL OFFENSE") : TEXT("CALL OFFENSE")) : TEXT("CALL DEFENSE")) + TEXT("     ") + BookLine)) ]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(30, "Bold")).ColorAndOpacity(White90).Text(T(TabLabel)) ]
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
	for (const TPair<FString, FString>& Tb : CallTabs)
	{
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
	Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)[ Glyph(EPadGlyph::R1, TEXT("E"), 30.f) ];

	// three plays, each over its button
	const double Los = bTwoPoint ? 98.0 : G.ballOn;
	const double FirstDown = bTwoPoint ? -1.0 : FMath::Min(100.0, G.ballOn + G.toGo);
	constexpr float ArtW = 430.f, ArtH = 236.f;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	auto Arrow = [&](EPadGlyph Gl, const TCHAR* Key, bool bShow) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(56).VAlign(VAlign_Center).HAlign(HAlign_Center)
			[ bShow ? Glyph(Gl, Key, 34.f) : SNullWidget::NullWidget ];
	};
	Row->AddSlot().AutoWidth()[ Arrow(EPadGlyph::DLeft, TEXT("←"), Pages > 1) ];
	for (int32 k = 0; k < 3; ++k)
	{
		const int32 I = CallPage * 3 + k;
		if (!CallCards.IsValidIndex(I))
		{
			Row->AddSlot().AutoWidth().Padding(8, 0)[ SNew(SBox).WidthOverride(ArtW + 16) ];
			continue;
		}
		const FCallCard& Cd = CallCards[I];
		TSharedRef<SWidget> Art = SNullWidget::NullWidget;
		if (Cd.Op || Cd.Dc)
		{
			FArtPicture Pic;
			BuildCardPicture(*Director->Game, Cd.Op, Cd.Dc, bOffense, Los, G.ballY, Pic);
			if (!Pic.Marks.IsEmpty()) Art = SNew(SPlayDiagram).Picture(Pic).Offense(bOffense).FirstDown(FirstDown);
		}
		if (Art == SNullWidget::NullWidget)
			Art = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.025f, 0.032f, 0.045f, 1.f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(Font(26, "Bold")).ColorAndOpacity(FLinearColor(1, 1, 1, 0.22f)).Text(T(Cd.Kind.ToUpper())) ];
		const FCallChoice Choice = Cd.Choice;
		Row->AddSlot().AutoWidth().Padding(8, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(CardEdge).ContentPadding(FMargin(2))
				.OnClicked_Lambda([this, Choice] { CallPick(Choice); return FReply::Handled(); })
				[
					SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Card).Padding(FMargin(6, 6, 6, 10))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).WidthOverride(ArtW).HeightOverride(ArtH)[ Art ] ]
						+ SVerticalBox::Slot().AutoHeight().Padding(6, 8, 6, 0)[ SNew(STextBlock).Font(Font(10, "Bold")).ColorAndOpacity(Dim).Text(T(Cd.Kind.ToUpper())) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(6, 0)[ SNew(STextBlock).Font(Font(19, "Bold")).ColorAndOpacity(White90).Text(T(Cd.Name)) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(6, 2, 6, 0)
						[ SNew(SBox).HeightOverride(16)[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Accent).Text(T(Cd.Note)) ] ]
					]
				]
			]
			// the button that calls it, under the card
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)[ Glyph(CallButtons[k], CallKeys[k], 38.f) ]
		];
	}
	Row->AddSlot().AutoWidth()[ Arrow(EPadGlyph::DRight, TEXT("→"), Pages > 1) ];

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
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::DUp, TEXT("\u2191")}, {EPadGlyph::DDown, TEXT("\u2193")}}, bOffense ? TEXT("FORMATION") : TEXT("SET")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::DLeft, TEXT("←")}, {EPadGlyph::DRight, TEXT("→")}}, FString::Printf(TEXT("MORE PLAYS  %d / %d"), CallPage + 1, Pages)) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::Triangle, TEXT("H")}}, TEXT("COACH'S PICK")) ];

	TSharedRef<SWidget> CallPanel = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.012f, 0.016f, 0.026f, 0.93f)).Padding(FMargin(40, 16, 40, 14))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[ Header ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[ Tabs ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Row ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[ Foot ]
	];
	TSharedRef<SWidget> Root = SAssignNew(CallRoot, SCallRoot).OnKey([this](const FKey& K, bool bRep) { return CallKey(K, bRep); })
	[
		SNew(SVerticalBox)
		// the field stays in view over the top (lightly shaded), the call across the bottom above the scorebug
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
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[ SNew(STextBlock).Font(Font(22, "Bold")).Text(T(TEXT("TRY AFTER TOUCHDOWN"))) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Kick Extra Point"), TEXT("33-yard attempt, ~94% league-wide"), [this, Done] { ClearModal(); Done(false); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Go for Two"), TEXT("Run a play from the 2, ~48% league-wide"), [this, Done] { ClearModal(); Done(true); }) ];
	ShowModal(Centered(SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(20)[V], 0.4f));
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
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Accent).Text(T(TEXT("GAME PAUSED"))) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 2)[ SNew(STextBlock).Font(Font(30, "Bold")).ColorAndOpacity(FLinearColor::White).Text(T(Score)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Dim).Text(T(Situation)) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)[ SNew(SBox).HeightOverride(3).WidthOverride(400)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Accent) ] ];
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
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Sim to End of Game"), TEXT(""), [this, SimToEnd] { ClearModal(); SimToEnd(); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Quit to Main Menu"), TEXT(""), [this, Quit] { ClearModal(); Quit(); }) ];
	V->AddSlot().AutoHeight().Padding(0, 14, 0, 0)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Dim)
		.Text(T(bSonyPad ? TEXT("OPTIONS  resume") : TEXT("MENU / ESC  resume"))) ];
	// the frozen picture behind it, blurred and darkened (the field stays readable as a backdrop, the menu card on
	// the left, as a broadcast game's pause is laid out)
	ShowModal(SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBackgroundBlur).BlurStrength(14.f) ]
		+ SOverlay::Slot()[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.03f, 0.55f)) ]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(96, 0, 0, 0))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(5)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Accent) ] ]
			+ SHorizontalBox::Slot().AutoWidth()
			[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.025f, 0.03f, 0.05f, 0.92f)).Padding(FMargin(28, 24))[V] ]
		]);
}

void SGridironOverlay::ShowSettings(std::function<void()> Back)
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[ SNew(STextBlock).Font(Font(26, "Bold")).Text(T(TEXT("SETTINGS"))) ];
	const bool bCc = Director && Director->CaptionsOn();
	V->AddSlot().AutoHeight()[ MakeButton(FString::Printf(TEXT("Commentary captions: %s"), bCc ? TEXT("On") : TEXT("Off")),
		TEXT("The booth's words as text at the bottom of the screen."), [this, Back, bCc] { if (Director) Director->SetCaptionsOn(!bCc); ShowSettings(Back); }) ];
	const bool bInv = Director && Director->InvertRightY();
	V->AddSlot().AutoHeight()[ MakeButton(FString::Printf(TEXT("Right stick up / down: %s"), bInv ? TEXT("Inverted") : TEXT("Standard")),
		TEXT("If the hit stick, truck and kick swing go the wrong way on your controller, flip this."), [this, Back, bInv] { if (Director) Director->SetInvertRightY(!bInv); ShowSettings(Back); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Back"), TEXT(""), [Back] { Back(); }) ];
	ShowModal(Centered(SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(20)[V], 0.6f));
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
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(0, 1);
	Grid->AddSlot(1, 0).Padding(16, 2)[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Dim).Text(T(S(G.away->abbr))) ];
	Grid->AddSlot(2, 0).Padding(16, 2)[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Dim).Text(T(S(G.home->abbr))) ];
	for (int i = 0; i < Rows.Num(); ++i)
	{
		Grid->AddSlot(0, i + 1).Padding(0, 2)[ SNew(STextBlock).Font(Font(12)).Text(T(Rows[i].L)) ];
		Grid->AddSlot(1, i + 1).Padding(16, 2).HAlign(HAlign_Right)[ SNew(STextBlock).Font(Font(12, "Bold")).Text(T(Rows[i].A)) ];
		Grid->AddSlot(2, i + 1).Padding(16, 2).HAlign(HAlign_Right)[ SNew(STextBlock).Font(Font(12, "Bold")).Text(T(Rows[i].H)) ];
	}
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
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(Dim).Text(T(Title.ToUpper())) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 14)
		[ SNew(STextBlock).Font(Font(34, "Bold")).Text(T(FString::Printf(TEXT("%s %s %d   -   %d %s %s"), *S(G.away->city), *S(G.away->name), G.score[tag::TeamKey::Away], G.score[tag::TeamKey::Home], *S(G.home->city), *S(G.home->name)))) ]
		+ SVerticalBox::Slot().AutoHeight()[ Grid ]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 12, 0)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.92f)).Text(T(S(G.away->abbr) + TEXT(" leaders\n") + Leaders(*G.away))) ]
			+ SHorizontalBox::Slot().FillWidth(1)[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(FLinearColor(0.85f, 0.88f, 0.92f)).Text(T(S(G.home->abbr) + TEXT(" leaders\n") + Leaders(*G.home))) ]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 16, 0, 0)[ MakeButton(Button, TEXT(""), [this, Done] { ClearModal(); Done(); }, 220) ];
	ShowModal(Centered(SNew(SBox).WidthOverride(900)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(24)[V] ], 0.75f));
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
	const float W = 460.f, H = 118.f;
	const FVector2D TL((Local.X - W) * 0.5f, Local.Y - H - 150.f); // (above the score bug and the hint line)
	Box(TL, FVector2D(W, H), FLinearColor(0.02f, 0.025f, 0.035f, 0.88f), Layer + 2);
	Box(TL + FVector2D(0, H - 3), FVector2D(W, 3), Accent, Layer + 3);
	const FSlateFontInfo Ft = Font(14, "Bold"), Fs = Font(10);
	FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(W - 28, 20), FSlateLayoutTransform(FVector2f(TL + FVector2D(14, 8)))), K.Title, Ft, ESlateDrawEffect::None, FLinearColor::White);
	if (!K.Sub.IsEmpty())
	{
		const FVector2D M = Measure(K.Sub, Fs);
		FSlateDrawElement::MakeText(Out, Layer + 3, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(TL + FVector2D(W - 14 - M.X, 12)))), K.Sub, Fs, ESlateDrawEffect::None, Dim);
	}
	// power: 0..1.15 across, the last 13% red
	const FVector2D PB = TL + FVector2D(14, 40);
	const float PW = W - 28, Red = PW * (1.f / 1.15f);
	Box(PB, FVector2D(PW, 20), FLinearColor(0.12f, 0.13f, 0.16f, 1.f), Layer + 3);
	Box(PB + FVector2D(Red, 0), FVector2D(PW - Red, 20), FLinearColor(0.45f, 0.06f, 0.05f, 1.f), Layer + 3);
	Box(PB + FVector2D(Red * 0.9f, 0), FVector2D(Red * 0.1f, 20), FLinearColor(0.07f, 0.3f, 0.12f, 1.f), Layer + 3);
	const float Fill = FMath::Clamp(K.Power / 1.15f, 0.f, 1.f) * PW;
	const FLinearColor FillC = K.Power > 1.f ? FLinearColor(1.f, 0.22f, 0.15f) : K.Power > 0.9f ? FLinearColor(0.3f, 1.f, 0.45f) : FLinearColor(1.f, 1.f, 1.f, 0.92f);
	Box(PB + FVector2D(0, 4), FVector2D(Fill, 12), FillC, Layer + 4);
	FSlateDrawElement::MakeText(Out, Layer + 4, G.ToPaintGeometry(FVector2f(60, 14), FSlateLayoutTransform(FVector2f(PB + FVector2D(0, -15)))), FString(TEXT("POWER")), Fs, ESlateDrawEffect::None, Dim);
	// accuracy: the needle across, the centre band green
	const FVector2D AB = TL + FVector2D(14, 84);
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
	if (UserMarker.bOn && UserMarker.Ring.Num() > 2)
	{
		const FVector2D Local = G.GetLocalSize();
		const FVector2D Sc(Local.X / FMath::Max(1.0, UserMarker.Viewport.X), Local.Y / FMath::Max(1.0, UserMarker.Viewport.Y));
		const float Pulse = 0.8f + 0.2f * FMath::Sin(FPlatformTime::Seconds() * 5.0);
		const FLinearColor Glow(0.25f, 0.85f, 1.f, 0.35f * Pulse), Core(0.45f, 0.95f, 1.f, 0.95f);
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
		if (!UserMarker.Number.IsEmpty())
		{
			const FSlateFontInfo F = Font(12, "Bold");
			const FVector2D M = Measure(UserMarker.Number, F);
			FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(H + FVector2f(-M.X * 0.5f, -Hh - 4.f - M.Y))), UserMarker.Number, F, ESlateDrawEffect::None, Core);
		}
		Layer += 3;
	}
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
