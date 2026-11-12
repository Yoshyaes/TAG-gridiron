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
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

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
			.Visibility_Lambda([this] { return bHud && !bBroadcastOnly ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SBox).WidthOverride(560)
				[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(0.92f, 0.94f, 0.97f)).AutoWrapText(true).Text_Lambda([this] { return TickerText(); }) ]
			]
		]
		// weather (top-right)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(FMargin(14, 8))
			.Visibility_Lambda([this] { return bHud && !bBroadcastOnly ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
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
				.Visibility_Lambda([this] { return ToastT < (ToastText.Len() > 14 ? 3.2f : 1.3f) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }) // a stat line stays long enough to read
				[ SNew(STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(Accent).Text_Lambda([this] { return T(ToastText); }) ]
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
	BannerT += Dt;
	ToastT += Dt;
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
	switch (Gl)
	{
	case EPadGlyph::Cross:
	case EPadGlyph::Circle:
	case EPadGlyph::Square:
	case EPadGlyph::Triangle:
		Lines(Poly(R * 0.5f, 28, 0.f), Dark, R, 0);  // the button (a ring as thick as its radius is a disc)
		Lines(Poly(R - 0.8f, 28, 0.f), Tint * FLinearColor(1, 1, 1, 0.45f), 1.4f);
		if (!bSonyPad)
		{
			// an Xbox-style pad: the face buttons by their letters
			Text(Gl == EPadGlyph::Cross ? TEXT("A") : Gl == EPadGlyph::Circle ? TEXT("B") : Gl == EPadGlyph::Square ? TEXT("X") : TEXT("Y"), FMath::RoundToInt(Size * 0.46f), Tint);
			break;
		}
		if (Gl == EPadGlyph::Cross)
		{
			Lines({P2(-S, -S), P2(S, S)}, Tint, Th);
			Lines({P2(-S, S), P2(S, -S)}, Tint, Th);
		}
		else if (Gl == EPadGlyph::Circle) Lines(Poly(S * 1.02f, 24, 0.f), Tint, Th);
		else if (Gl == EPadGlyph::Square) Lines({P2(-S * 0.92f, -S * 0.92f), P2(S * 0.92f, -S * 0.92f), P2(S * 0.92f, S * 0.92f), P2(-S * 0.92f, S * 0.92f), P2(-S * 0.92f, -S * 0.92f)}, Tint, Th);
		else Lines(Poly(S * 1.12f, 3, -PI * 0.5f), Tint, Th);  // apex up
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
					[ SNew(STextBlock).Font(Font(28, "Bold")).ShadowOffset(FVector2D(1, 1)).Text_Lambda([this, K] { return Director && Director->Game ? FText::AsNumber(Director->HudScore[K == tag::TeamKey::Home ? 0 : 1] >= 0 ? Director->HudScore[K == tag::TeamKey::Home ? 0 : 1] : Director->Game->score[K]) : FText(); }) ]
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
	Menu->AddSlot().AutoHeight().HAlign(HAlign_Center)[ MakeButton(TEXT("Quit"), TEXT(""), [] { FPlatformMisc::RequestExit(false); }) ];
	Menu->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 30, 0, 0)[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text(T(TEXT("An original football game. All teams, players and names are fictional."))) ];
	ShowModal(Centered(Menu, 0.35f));
}

void SGridironOverlay::ShowTeamSelect(bool bUserPlays)
{
	static const TCHAR* WxNames[] = {TEXT("Random (realistic)"), TEXT("Clear"), TEXT("Windy"), TEXT("Rain"), TEXT("Snow")};
	static const double Quarters[] = {900, 300, 480};
	static const TCHAR* QNames[] = {TEXT("15 min (authentic clock)"), TEXT("5 min"), TEXT("8 min")};
	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
	Root->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)[ SNew(STextBlock).Font(Font(34, "Bold")).Text(T(bUserPlays ? TEXT("PLAY NOW") : TEXT("WATCH A GAME"))) ];
	auto Grid = [this, bUserPlays](bool bHome) -> TSharedRef<SWidget>
	{
		TSharedRef<SWrapBox> W = SNew(SWrapBox).PreferredSize(720);
		for (int i = 0; i < static_cast<int>(Director->League.size()); ++i)
		{
			const tag::Team& Tm = *Director->League[i];
			const tag::TeamOvr O = tag::teamOvr(Tm);
			const bool bSel = bHome ? i == SelHome : i == SelAway;
			W->AddSlot().Padding(3)
			[
				SNew(SBox).WidthOverride(172)
				[
					SNew(SButton).ButtonColorAndOpacity(bSel ? FLinearColor(0.35f, 0.3f, 0.05f) : PanelLight).ContentPadding(FMargin(8, 5))
					.OnClicked_Lambda([this, i, bHome, bUserPlays] { if (bHome) { if (i != SelAway) SelHome = i; } else if (i != SelHome) SelAway = i; ShowTeamSelect(bUserPlays); return FReply::Handled(); })
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
						[ SNew(SBox).WidthOverride(18).HeightOverride(18)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Hex(Tm.colors.primary)) ] ]
						+ SHorizontalBox::Slot().FillWidth(1)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(10, "Bold")).Text(T(S(Tm.city) + TEXT(" ") + S(Tm.name))) ]
							+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(8)).ColorAndOpacity(Dim).Text(T(FString::Printf(TEXT("OVR %d  OFF %d  DEF %d"), O.ovr, O.off, O.def))) ]
						]
					]
				]
			];
		}
		return SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[ SNew(STextBlock).Font(Font(11, "Bold")).ColorAndOpacity(Dim).Text(T(bHome ? TEXT("HOME TEAM") : TEXT("AWAY TEAM"))) ]
			+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).MaxDesiredHeight(560)[ SNew(SScrollBox) + SScrollBox::Slot()[ W ] ] ]
		];
	};
	Root->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(6)[ Grid(false) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6)[ Grid(true) ]
	];
	auto Opt = [this](const FString& Label, const FString& Value, std::function<void()> Cycle) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)[ SNew(SBox).WidthOverride(140)[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Dim).Text(T(Label)) ] ]
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).ButtonColorAndOpacity(PanelLight).ContentPadding(FMargin(12, 4)).OnClicked_Lambda([Cycle] { Cycle(); return FReply::Handled(); })[ SNew(STextBlock).Font(Font(12, "Bold")).Text(T(Value)) ] ];
	};
	TSharedRef<SVerticalBox> Opts = SNew(SVerticalBox);
	if (bUserPlays)
		Opts->AddSlot().AutoHeight().Padding(2)[ Opt(TEXT("You control"), bSelUserHome ? TEXT("Home team") : TEXT("Away team"), [this, bUserPlays] { bSelUserHome = !bSelUserHome; ShowTeamSelect(bUserPlays); }) ];
	Opts->AddSlot().AutoHeight().Padding(2)[ Opt(TEXT("Weather"), WxNames[SelWeather], [this, bUserPlays] { SelWeather = (SelWeather + 1) % 5; ShowTeamSelect(bUserPlays); }) ];
	Opts->AddSlot().AutoHeight().Padding(2)[ Opt(TEXT("Kickoff"), bSelNight ? TEXT("Night (lights)") : TEXT("Day"), [this, bUserPlays] { bSelNight = !bSelNight; ShowTeamSelect(bUserPlays); }) ];
	Opts->AddSlot().AutoHeight().Padding(2)[ Opt(TEXT("Quarter length"), QNames[SelQuarter], [this, bUserPlays] { SelQuarter = (SelQuarter + 1) % 3; ShowTeamSelect(bUserPlays); }) ];
	Root->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(12)[ Opts ] ];
	Root->AddSlot().AutoHeight().HAlign(HAlign_Center)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(6)[ MakeButton(TEXT("Back"), TEXT(""), [this] { ShowMainMenu(); }, 160) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6)[ MakeButton(TEXT("Kick Off"), TEXT(""), [this, bUserPlays] { Director->StartMatch(SelHome, SelAway, bUserPlays, bSelUserHome, SelWeather, bSelNight, Quarters[SelQuarter]); }, 200) ]
	];
	ShowModal(Centered(Root, 0.7f), true);
}

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
	if (bOffense)
	{
		// fourth down: the kicks first (and open on them); the clock plays at the end
		if (bFourthDown) CallTabs.Emplace(TEXT("special"), TEXT("SPECIAL TEAMS"));
		for (const tag::Formation& F : tag::FORMATIONS()) CallTabs.Emplace(S(F.id), S(F.name).ToUpper());
		if (!bTwoPoint) CallTabs.Emplace(TEXT("clock"), TEXT("CLOCK"));
	}
	else
	{
		CallTabs.Emplace(TEXT("base"), TEXT("BASE 4-3"));
		CallTabs.Emplace(TEXT("nickel"), TEXT("NICKEL"));
		CallTabs.Emplace(TEXT("dime"), TEXT("DIME"));
		CallTabs.Emplace(TEXT("goalline"), TEXT("GOAL LINE"));
	}
	const bool bKnown = CallTabs.ContainsByPredicate([this](const TPair<FString, FString>& Tb) { return Tb.Key == CallTab; });
	if (bFourthDown && bOffense) CallTab = TEXT("special");
	else if (!bKnown || CallTab == TEXT("special") || CallTab == TEXT("clock")) CallTab = bOffense ? CallTabs[0].Key : FString(TEXT("nickel"));
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
		const int32 At = CallTabs.IndexOfByPredicate([this](const TPair<FString, FString>& Tb) { return Tb.Key == CallTab; });
		const int32 N = CallTabs.Num();
		if (N > 0)
		{
			CallTab = CallTabs[((At < 0 ? 0 : At) + (bNextTab ? 1 : -1) + N) % N].Key;
			CallPage = 0;
			BuildPlayCall();
		}
		return true;
	}
	const bool bPrevPage = Is({EKeys::Gamepad_DPad_Left, EKeys::Left, EKeys::A});
	const bool bNextPage = Is({EKeys::Gamepad_DPad_Right, EKeys::Right, EKeys::D});
	if (bPrevPage || bNextPage)
	{
		if (Pages > 1)
		{
			CallPage = (CallPage + (bNextPage ? 1 : -1) + Pages) % Pages;
			BuildPlayCall();
		}
		return true;
	}
	// the d-pad / arrows up and down do nothing here (and must not reach the game)
	return Is({EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Up, EKeys::Down, EKeys::W, EKeys::S});
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
		for (const tag::OffPlay& P : tag::OFF_PLAYS())
		{
			if (S(P.formation) != CallTab || P.type == tag::PlayType::Kneel || P.type == tag::PlayType::Spike) continue;
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
			{TEXT("C3"), TEXT("Cover 3  -  three deep, four under")}, {TEXT("C3Fire"), TEXT("Fire zone  -  5-man pressure")}, {TEXT("C4"), TEXT("Quarters  -  four deep")}};
		for (const tag::DefCall& D : tag::DEF_PLAYS())
		{
			if (FString(tag::frontName(D.front)).ToLower() != CallTab.ToLower()) continue;
			const FString* Desc = Cov.Find(FString(tag::coverageName(D.coverage)));
			FCallChoice C; C.Kind = FCallChoice::Def; C.Call = &D;
			Add(D.blitz.empty() ? TEXT("Coverage") : FString::Printf(TEXT("%d-man pressure"), 4 + static_cast<int>(D.blitz.size())), S(D.name), Desc ? *Desc : FString(), nullptr, &D, C);
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
	const FString TabLabel = [&]() -> FString { for (const TPair<FString, FString>& Tb : CallTabs) if (Tb.Key == CallTab) return Tb.Value; return FString(); }();
	const FString Sit = bTwoPoint ? FString(TEXT("TWO-POINT TRY  -  FROM THE 2"))
		: FString::Printf(TEXT("%s  -  %s"), *S(G.downText()).ToUpper(), *S(G.ydLine()));
	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(Accent)
				.Text(T(bOffense ? (bTwoPoint ? TEXT("TWO-POINT TRY  -  CALL OFFENSE") : TEXT("CALL OFFENSE")) : TEXT("CALL DEFENSE"))) ]
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
			.OnClicked_Lambda([this, Id] { CallTab = Id; CallPage = 0; BuildPlayCall(); return FReply::Handled(); })
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
		+ SHorizontalBox::Slot().AutoWidth()[ Legend({{EPadGlyph::L1, TEXT("Q")}, {EPadGlyph::R1, TEXT("E")}}, bOffense ? TEXT("FORMATION") : TEXT("FRONT")) ]
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
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[ SNew(STextBlock).Font(Font(26, "Bold")).Text(T(TEXT("PAUSED"))) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Resume"), TEXT(""), [this, Resume] { ClearModal(); Resume(); }) ]; // the director restores the suspended screen
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Box Score"), TEXT(""), [this, Resume] { ShowBoxScore(TEXT("Box Score"), TEXT("Resume"), [Resume] { Resume(); }); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Sim to End of Game"), TEXT(""), [this, SimToEnd] { ClearModal(); SimToEnd(); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Quit to Main Menu"), TEXT(""), [this, Quit] { ClearModal(); Quit(); }) ];
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
