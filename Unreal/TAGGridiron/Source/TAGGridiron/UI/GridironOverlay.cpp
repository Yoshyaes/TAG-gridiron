#include "UI/GridironOverlay.h"

#include "Game/MatchDirector.h"
#include "Render/Coords.h"
#include "Sim/Playbook.h"
#include "Sim/Stats.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
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
			.Visibility_Lambda([this] { return bHud ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SBox).WidthOverride(560)
				[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(FLinearColor(0.92f, 0.94f, 0.97f)).AutoWrapText(true).Text_Lambda([this] { return TickerText(); }) ]
			]
		]
		// weather (top-right)
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(FMargin(14, 8))
			.Visibility_Lambda([this] { return bHud ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
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
				.Text_Lambda([this] { return T(BannerSub); })
				.ColorAndOpacity_Lambda([this] { return FSlateColor(FLinearColor(1, 1, 1, BannerT < 2.3f ? 1.f : 0.f)); })
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24, 0, 0)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.07f, 0.09f, 0.13f, 0.9f)).Padding(FMargin(18, 8))
				.Visibility_Lambda([this] { return ToastT < 1.3f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
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
	bModal = true;
	PendingFocus = FMath::Max(0, Was); // focused next tick, once laid out
}

void SGridironOverlay::ClearModal()
{
	ModalBox->SetContent(SNullWidget::NullWidget);
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
		if (F.IsValid() && F != ModalBox)
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
		Text(Gl == EPadGlyph::R1 ? TEXT("R1") : Gl == EPadGlyph::L1 ? TEXT("L1") : Gl == EPadGlyph::L2 ? TEXT("L2") : TEXT("R2"), FMath::RoundToInt(Size * 0.36f), Tint);
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
					[ SNew(STextBlock).Font(Font(28, "Bold")).ShadowOffset(FVector2D(1, 1)).Text_Lambda([this, K] { return Director && Director->Game ? FText::AsNumber(Director->Game->score[K]) : FText(); }) ]
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
		];
}

FText SGridironOverlay::ClockText() const
{
	if (!Director || !Director->Game) return FText();
	return T(S(tag::fmtClock(FMath::Max(0.0, Director->Game->clock - ClockOffset))));
}

FText SGridironOverlay::DownText() const
{
	if (!Director || !Director->Game) return FText();
	const tag::Game& G = *Director->Game;
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
	const auto& Log = Director->Game->log;
	FString Out;
	for (int i = static_cast<int>(Log.size()) - 1, n = 0; i >= 0 && n < 4; --i, ++n)
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

void SGridironOverlay::ShowPlayCall(bool bOffense, bool bFourthDown, bool bTwoPoint, std::function<void(FCallChoice)> Done)
{
	const tag::Game& G = *Director->Game;
	if (CallTab.IsEmpty() || (bOffense && !tag::formationById(TCHAR_TO_UTF8(*CallTab))) || (!bOffense && tag::formationById(TCHAR_TO_UTF8(*CallTab))))
		CallTab = bOffense ? S(tag::FORMATIONS()[0].id) : FString(TEXT("nickel"));
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	auto Tab = [&](const FString& Id, const FString& Label)
	{
		const bool bOn = Id == CallTab;
		Tabs->AddSlot().AutoWidth().Padding(3)
		[
			SNew(SButton).ButtonColorAndOpacity(bOn ? Accent : PanelLight).ContentPadding(FMargin(12, 6))
			.OnClicked_Lambda([this, Id, bOffense, bFourthDown, bTwoPoint, Done] { CallTab = Id; ShowPlayCall(bOffense, bFourthDown, bTwoPoint, Done); return FReply::Handled(); })
			[ SNew(STextBlock).Font(Font(11, "Bold")).ColorAndOpacity(bOn ? FLinearColor(0.05f, 0.05f, 0.05f) : FLinearColor::White).Text(T(Label)) ]
		];
	};
	if (bOffense)
		for (const tag::Formation& F : tag::FORMATIONS()) Tab(S(F.id), S(F.name) + TEXT("  ") + S(F.personnel));
	else
	{
		Tab(TEXT("base"), TEXT("Base 4-3"));
		Tab(TEXT("nickel"), TEXT("Nickel"));
		Tab(TEXT("dime"), TEXT("Dime"));
		Tab(TEXT("goalline"), TEXT("Goal Line"));
	}
	Tabs->AddSlot().AutoWidth().Padding(3)
	[
		SNew(SButton).ButtonColorAndOpacity(PanelLight).ContentPadding(FMargin(12, 6))
		.OnClicked_Lambda([this, Done] { ClearModal(); FCallChoice C; C.Kind = FCallChoice::Auto; Done(C); return FReply::Handled(); })
		[ SNew(STextBlock).Font(Font(11, "Bold")).Text(T(TEXT("Coach's Pick"))) ]
	];
	TSharedRef<SWrapBox> Cards = SNew(SWrapBox).UseAllottedSize(true);
	auto Card = [&](const FString& Kind, const FString& Name, const FString& Note, std::function<void()> Pick)
	{
		Cards->AddSlot().Padding(4)
		[
			SNew(SBox).WidthOverride(230).HeightOverride(86)
			[
				SNew(SButton).ButtonColorAndOpacity(PanelLight).ContentPadding(FMargin(12, 8))
				.OnClicked_Lambda([this, Pick] { ClearModal(); Pick(); return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Dim).Text(T(Kind.ToUpper())) ]
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(15, "Bold")).Text(T(Name)) ]
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Accent).AutoWrapText(true).Text(T(Note)) ]
				]
			]
		];
	};
	if (bOffense)
	{
		if (bFourthDown)
		{
			const tag::Player* K = G.kicker(G.poss);
			const double Dist = FMath::RoundToDouble(100 - G.ballOn + 17);
			const int Pct = FMath::RoundToInt(G.fgProb(Dist, *K, G.poss) * 100);
			Card(TEXT("Special teams"), TEXT("Punt"), S(G.punter(G.poss)->last), [Done] { FCallChoice C; C.Kind = FCallChoice::Punt; Done(C); });
			Card(TEXT("Special teams"), FString::Printf(TEXT("Field Goal (%d yd)"), static_cast<int>(Dist)), FString::Printf(TEXT("%d%% est. make  -  %s"), Pct, *S(K->last)), [Done] { FCallChoice C; C.Kind = FCallChoice::FG; Done(C); });
		}
		for (const tag::OffPlay& P : tag::OFF_PLAYS())
		{
			if (S(P.formation) != CallTab || P.type == tag::PlayType::Kneel || P.type == tag::PlayType::Spike) continue;
			const tag::OffPlay* PP = &P;
			FString Kind = P.type == tag::PlayType::Run ? TEXT("Run") : P.type == tag::PlayType::PlayAction ? TEXT("Play action") : P.type == tag::PlayType::Screen ? TEXT("Screen") : TEXT("Pass");
			if (P.hasTag("quick")) Kind += TEXT(" - quick");
			if (P.hasTag("deep")) Kind += TEXT(" - shot");
			FString Note;
			for (const std::string& R : P.reads) Note += S(R) + TEXT(" ");
			Card(Kind, S(P.name), Note.IsEmpty() ? FString() : TEXT("Reads: ") + Note, [Done, PP] { FCallChoice C; C.Kind = FCallChoice::Off; C.Play = PP; Done(C); });
		}
		Card(TEXT("Clock"), TEXT("QB Kneel"), TEXT(""), [Done] { FCallChoice C; C.Kind = FCallChoice::Off; C.Play = tag::playById("kneel"); Done(C); });
		Card(TEXT("Clock"), TEXT("Spike"), TEXT(""), [Done] { FCallChoice C; C.Kind = FCallChoice::Off; C.Play = tag::playById("spike"); Done(C); });
	}
	else
	{
		static const TMap<FString, FString> Cov = {
			{TEXT("C0"), TEXT("Cover 0 - all-out man, no deep help")}, {TEXT("C1"), TEXT("Cover 1 - man, single-high")},
			{TEXT("C2"), TEXT("Cover 2 - two deep halves")}, {TEXT("C2M"), TEXT("2-Man - man under, two deep")},
			{TEXT("C3"), TEXT("Cover 3 - three deep, four under")}, {TEXT("C3Fire"), TEXT("Fire zone - 5-man pressure")}, {TEXT("C4"), TEXT("Quarters - four deep")}};
		for (const tag::DefCall& D : tag::DEF_PLAYS())
		{
			if (FString(tag::frontName(D.front)).ToLower() != CallTab.ToLower()) continue;
			const tag::DefCall* DD = &D;
			const FString* Desc = Cov.Find(FString(tag::coverageName(D.coverage)));
			Card(D.blitz.empty() ? TEXT("Coverage") : FString::Printf(TEXT("%d-man rush"), 4 + static_cast<int>(D.blitz.size())), S(D.name), Desc ? *Desc : FString(), [Done, DD] { FCallChoice C; C.Kind = FCallChoice::Def; C.Call = DD; Done(C); });
		}
	}
	const FString Sit = bTwoPoint ? FString(TEXT("Two-point try from the 2"))
		: FString::Printf(TEXT("%s at %s  -  %s ball  -  Q%d %s"), *S(G.downText()), *S(G.ydLine()), *S(G.offense().abbr), G.q, *S(tag::fmtClock(G.clock)));
	TSharedRef<SWidget> Body = SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.025f, 0.86f)).Padding(FMargin(28, 18, 28, 120)).VAlign(VAlign_Bottom)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Font(Font(26, "Bold")).Text(T(bOffense ? (bTwoPoint ? TEXT("TWO-POINT TRY") : TEXT("CALL OFFENSE")) : TEXT("CALL DEFENSE"))) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20, 0)[ SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Dim).Text(T(Sit)) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ Tabs ]
		+ SVerticalBox::Slot().AutoHeight()[ Cards ]
	];
	bHud = true;
	ShowModal(Body, bModal); // a formation tab changed (the call screen is already up): keep the focus there
}

void SGridironOverlay::ShowPatChoice(std::function<void(bool)> Done)
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[ SNew(STextBlock).Font(Font(22, "Bold")).Text(T(TEXT("TRY AFTER TOUCHDOWN"))) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Kick Extra Point"), TEXT("33-yard attempt, ~94% league-wide"), [this, Done] { ClearModal(); Done(false); }) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Go for Two"), TEXT("Run a play from the 2, ~48% league-wide"), [this, Done] { ClearModal(); Done(true); }) ];
	ShowModal(Centered(SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Panel).Padding(20)[V], 0.4f));
}

void SGridironOverlay::ShowPause(std::function<void()> Resume, std::function<void()> SimToEnd, std::function<void()> Quit)
{
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)[ SNew(STextBlock).Font(Font(26, "Bold")).Text(T(TEXT("PAUSED"))) ];
	V->AddSlot().AutoHeight()[ MakeButton(TEXT("Resume"), TEXT(""), [this, Resume] { ClearModal(); Resume(); }) ];
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
