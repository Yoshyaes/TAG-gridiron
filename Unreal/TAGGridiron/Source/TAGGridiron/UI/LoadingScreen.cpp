#include "UI/LoadingScreen.h"

#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"

namespace
{
	const FLinearColor Accent(1.f, 0.83f, 0.f);
	const FLinearColor Dim(0.62f, 0.67f, 0.74f);
	const FLinearColor Ink(0.035f, 0.045f, 0.07f);
	const float BarW = 340.f, BarH = 6.f, SweepW = 90.f;

	FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	const FSlateBrush* White() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText Tx(const FString& S) { return FText::FromString(S); }

	TSharedRef<SWidget> TeamLine(const FLoadingTeam& Tm, bool bRight)
	{
		TSharedRef<SWidget> Swatch = SNew(SBox).WidthOverride(14).HeightOverride(46).Padding(0)
			[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Tm.Color).Padding(0) ];
		TSharedRef<SWidget> Words = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(bRight ? HAlign_Right : HAlign_Left)
			[ SNew(STextBlock).Font(Font(13, "Bold")).ColorAndOpacity(Dim).Text(Tx(Tm.City.ToUpper())) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(bRight ? HAlign_Right : HAlign_Left)
			[ SNew(STextBlock).Font(Font(30, "Bold")).ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(2, 2)).Text(Tx(Tm.Name.ToUpper())) ];
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		if (bRight)
		{
			H->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Words ];
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[ Swatch ];
		}
		else
		{
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)[ Swatch ];
			H->AddSlot().AutoWidth().VAlign(VAlign_Center)[ Words ];
		}
		return H;
	}
}

void SLoadingScreen::Construct(const FArguments& InArgs)
{
	Tips = InArgs._Tips;
	bHasMovie = InArgs._bHasMovie;
	const FLoadingTeam Away = InArgs._Away, Home = InArgs._Home;
	ChildSlot
	[
		SNew(SOverlay)
		// the matchup, top centre
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0, 40, 0, 0)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(Ink.R, Ink.G, Ink.B, 0.78f)).Padding(FMargin(28, 14))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 8)
				[ SNew(STextBlock).Font(Font(11, "Bold")).ColorAndOpacity(Accent).Text(Tx(TEXT("TONIGHT'S MATCHUP"))) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ TeamLine(Away, true) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(26, 0)
					[ SNew(STextBlock).Font(Font(16, "Bold")).ColorAndOpacity(Dim).Text(Tx(TEXT("AT"))) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ TeamLine(Home, false) ]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 0)
				[ SNew(STextBlock).Font(Font(12)).ColorAndOpacity(Dim).Text(Tx(InArgs._Conditions)) ]
			]
		]
		// the bottom band: the tip, the loading bar, the wordmark
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
		[
			SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(Ink.R, Ink.G, Ink.B, 0.86f)).Padding(FMargin(48, 22, 48, 26))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(10, "Bold")).ColorAndOpacity(Accent).Text(Tx(TEXT("TIP"))) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
					[ SNew(SBox).MaxDesiredWidth(760)[ SNew(STextBlock).Font(Font(15)).AutoWrapText(true).ColorAndOpacity_Lambda([this] { return TipColor(); }).Text_Lambda([this] { return TipText(); }) ] ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(40, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(FLinearColor::White).Text(Tx(TEXT("LOADING THE STADIUM"))) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
					[
						SNew(SBox).WidthOverride(BarW).HeightOverride(BarH)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.14f)).Padding(0) ]
							+ SOverlay::Slot().HAlign(HAlign_Left)
							[ SNew(SBox).Padding_Lambda([this] { return SweepPad(); })[ SNew(SBox).WidthOverride(SweepW)[ SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Accent).Padding(0) ] ] ]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 8, 0, 0)
					[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text(Tx(bHasMovie ? TEXT("HIGHLIGHTS PLAY WHILE THE TEAMS TAKE THE FIELD") : TEXT("THE TEAMS ARE TAKING THE FIELD"))) ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(48, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[ SNew(STextBlock).Font(Font(26, "Bold")).ColorAndOpacity(Accent).ShadowOffset(FVector2D(2, 2)).Text(Tx(TEXT("TAG GRIDIRON"))) ]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Dim).Text(Tx(TEXT("SIMULATION-FIRST PRO FOOTBALL"))) ]
				]
			]
		]
	];
}

void SLoadingScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (T0 < 0) T0 = InCurrentTime;
	T = InCurrentTime - T0;
}

FText SLoadingScreen::TipText() const
{
	if (Tips.Num() == 0) return FText();
	return Tx(Tips[static_cast<int32>(T / 4.5) % Tips.Num()]);
}

FSlateColor SLoadingScreen::TipColor() const
{
	// each tip fades in and out over its 4.5 s
	const float P = FMath::Fmod(static_cast<float>(T), 4.5f);
	const float A = FMath::Clamp(FMath::Min(P / 0.4f, (4.5f - P) / 0.4f), 0.f, 1.f);
	return FSlateColor(FLinearColor(0.92f, 0.94f, 0.97f, A));
}

FMargin SLoadingScreen::SweepPad() const
{
	const float X = (FMath::Sin(static_cast<float>(T) * 2.4f - PI * 0.5f) * 0.5f + 0.5f) * (BarW - SweepW);
	return FMargin(X, 0, 0, 0);
}

int32 SLoadingScreen::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	if (!bHasMovie)
	{
		// no reel yet: a dark stadium-night backdrop with a soft pool of light where the matchup sits
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), White(), ESlateDrawEffect::None, FLinearColor(0.012f, 0.016f, 0.028f, 1.f));
		const FVector2D Sz = G.GetLocalSize();
		for (int32 i = 0; i < 6; ++i)
		{
			const float R = 260.f + i * 150.f;
			TArray<FVector2f> P;
			for (int32 k = 0; k <= 48; ++k)
				P.Add(FVector2f(Sz.X * 0.5f + FMath::Cos(2.f * PI * k / 48) * R, Sz.Y * 0.36f + FMath::Sin(2.f * PI * k / 48) * R * 0.55f));
			FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), P, ESlateDrawEffect::None, FLinearColor(0.2f, 0.26f, 0.4f, 0.05f), true, 120.f);
		}
		Layer += 2;
	}
	return SCompoundWidget::OnPaint(Args, G, Cull, Out, Layer, Style, bParentEnabled);
}
