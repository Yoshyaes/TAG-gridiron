// The local multiplayer lobby, Madden's controller select: each player who presses a button on his pad (CROSS / A) or
// the keyboard (ENTER) gets a card in the middle; he moves it left to play for the away team, right for the home team
// (or leaves it in the middle to watch). On a side with two or more players, one calls the plays: the first there,
// until another takes it (TRIANGLE / Y, keyboard C). OPTIONS / MENU (ENTER) goes on to the team select.
//
// The input is read from every device apart by the director (AMatchDirector::TickLobby, from FTagInputHub); this screen
// only draws it, and is rebuilt whenever it changes. Its root keeps the keyboard focus and swallows the keys, so nothing
// behind it acts on them.
#include "UI/GridironOverlay.h"
#include "Audio/TagAudio.h"

#include "Game/MatchDirector.h"
#include "UI/TagTheme.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace TagLobby
{
	FSlateFontInfo LFont(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	const FSlateBrush* LWhite() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText LTx(const FString& S) { return FText::FromString(S); }
	const FLinearColor LGold(1.f, 0.78f, 0.22f), LInk(0.03f, 0.04f, 0.06f), LDim(0.62f, 0.66f, 0.74f), LPanel(0.06f, 0.07f, 0.1f, 0.94f);

	/** The lobby's root: holds the focus and takes every key (the director reads them per device). */
	class SLobbyRoot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SLobbyRoot) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()
		void Construct(const FArguments& A) { ChildSlot[A._Content.Widget]; }
		virtual bool SupportsKeyboardFocus() const override { return true; }
		virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override { return FReply::Handled(); }
		virtual FReply OnKeyUp(const FGeometry&, const FKeyEvent&) override { return FReply::Handled(); }
		virtual FNavigationReply OnNavigation(const FGeometry&, const FNavigationEvent&) override { return FNavigationReply::Stop(); }
	};
}

void SGridironOverlay::ShowLobby()
{
	using namespace TagLobby;
	if (!Director) return;
	ATagAudio::Screen(TEXT("Lobby"));
	bHud = false;
	bSelMulti = false;
	const TArray<AMatchDirector::FTagHuman>& Hs = Director->Humans;
	const float ColW = 330.f;

	auto Card = [&](int32 i) -> TSharedRef<SWidget>
	{
		const AMatchDirector::FTagHuman& H = Hs[i];
		const FLinearColor C = PlayerColor(i);
		const FString Dev = Director->HumanDevice(i);
		const FString Hint = H.Team < 0 ? (H.bPad ? TEXT("<  PICK A SIDE  >") : TEXT("A  PICK A SIDE  D")) : H.Team == 0 ? (H.bPad ? TEXT("AWAY   >") : TEXT("AWAY   D")) : (H.bPad ? TEXT("<   HOME") : TEXT("A   HOME"));
		return SNew(TagTheme::SPanel).Fill(FLinearColor(0.07f, 0.085f, 0.125f, 0.97f)).Radius(6.f).Padding(FMargin(0))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(C).Padding(FMargin(16, 10)).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(TagTheme::Font(26, "Black")).ColorAndOpacity(LInk).Text(LTx(AMatchDirector::HumanTag(i))) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(FMargin(14, 6))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(TagTheme::Font(16, "BoldCondensed", 40)).ColorAndOpacity(FLinearColor::White).Text(LTx(Dev.ToUpper())) ]
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(TagTheme::Font(11, "BoldCondensed", 120)).ColorAndOpacity(LDim).Text(LTx(Hint)) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 4, 0, 0)
				[
					SNew(SBorder).BorderImage(TagTheme::Round(3.f)).BorderBackgroundColor(LGold).Padding(FMargin(8, 2))
					.Visibility(H.bCaller ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
					[ SNew(STextBlock).Font(TagTheme::Font(10, "BoldCondensed", 120)).ColorAndOpacity(LInk).Text(LTx(TEXT("CALLS THE PLAYS"))) ]
				]
			]
		];
	};
	auto Empty = [&](int32 i) -> TSharedRef<SWidget>
	{
		const FLinearColor C = PlayerColor(i);
		return SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.035f)).Padding(FMargin(0)).VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
			[
				SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(C * FLinearColor(1, 1, 1, 0.22f)).Padding(FMargin(16, 10)).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(TagTheme::Font(26, "Black")).ColorAndOpacity(C * FLinearColor(1, 1, 1, 0.6f)).Text(LTx(AMatchDirector::HumanTag(i))) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(FMargin(14, 6))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(TagTheme::Font(14, "BoldCondensed", 80)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.55f)).Text(LTx(TEXT("PRESS  CROSS / A"))) ]
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(TagTheme::Font(11, "BoldCondensed", 120)).ColorAndOpacity(LDim * FLinearColor(1, 1, 1, 0.7f)).Text(LTx(TEXT("OR  ENTER  TO JOIN"))) ]
			]
		];
	};
	auto Header = [&](const FString& Big, const FString& Small, const FLinearColor& C) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(ColW)
		[
			SNew(TagTheme::SPanel).Fill(FLinearColor(C.R * 0.35f, C.G * 0.35f, C.B * 0.35f, 0.95f)).AccentTop(C).Radius(6.f).Padding(FMargin(12, 12, 12, 10))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(TagTheme::Font(26, "Black", 40)).ColorAndOpacity(FLinearColor::White).Text(LTx(Big)) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(TagTheme::Font(11, "BoldCondensed", 140)).ColorAndOpacity(FLinearColor(1, 1, 1, 0.7f)).Text(LTx(Small)) ]
			]
		];
	};
	auto Count = [&](int32 T) { int32 N = 0; for (const auto& H : Hs) N += H.Team == T ? 1 : 0; return N; };
	auto SideNote = [&](int32 T)
	{
		const int32 N = Count(T);
		return N == 0 ? FString(TEXT("CPU")) : N == 1 ? FString(TEXT("1 PLAYER")) : FString::Printf(TEXT("%d PLAYERS  -  ONE CALLS THE PLAYS"), N);
	};

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ Header(TEXT("AWAY"), SideNote(0), FLinearColor(0.85f, 0.22f, 0.2f, 1.f)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(14, 0)[ Header(TEXT("WATCHING"), TEXT("NOT ON A SIDE YET"), FLinearColor(0.42f, 0.45f, 0.52f, 1.f)) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Header(TEXT("HOME"), SideNote(1), FLinearColor(0.2f, 0.45f, 0.95f, 1.f)) ]
	];
	for (int32 i = 0; i < 4; ++i)
	{
		const int32 Col = i < Hs.Num() ? (Hs[i].Team < 0 ? 1 : Hs[i].Team == 0 ? 0 : 2) : 1;
		auto Cell = [&](int32 C) -> TSharedRef<SWidget>
		{
			TSharedRef<SWidget> In = SNullWidget::NullWidget;
			if (C == Col) In = i < Hs.Num() ? Card(i) : Empty(i);
			return SNew(SBox).WidthOverride(ColW).HeightOverride(80.f)[ In ];
		};
		Rows->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(0) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(14, 0)[ Cell(1) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(2) ]
		];
	}
	const bool bReady = Director->LobbyReady();
	const FString Status = Hs.Num() == 0 ? FString(TEXT("Up to four players: every controller and the keyboard is its own player"))
		: !bReady ? FString(TEXT("Move to a side to play: left for the away team, right for the home team"))
		: FString(TEXT("Ready  -  OPTIONS / MENU  or  ENTER  for the teams"));
	TSharedRef<SWidget> Keys = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ HintItem(EPadGlyph::Cross, TEXT("Enter"), TEXT("Join"), 22.f) ]
		+ SHorizontalBox::Slot().AutoWidth()[ HintItem(EPadGlyph::DLeftRight, TEXT("A / D"), TEXT("Side"), 22.f) ]
		+ SHorizontalBox::Slot().AutoWidth()[ HintItem(EPadGlyph::Triangle, TEXT("C"), TEXT("Call the plays"), 22.f) ]
		+ SHorizontalBox::Slot().AutoWidth()[ HintItem(EPadGlyph::Circle, TEXT("Bksp"), TEXT("Back"), 22.f) ];

	TSharedRef<SWidget> Page = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ TagTheme::Title(TEXT("Couch play  -  up to 4 players"), TEXT("Local multiplayer"), 44) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 8, 0, 24)
		[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 120)).ColorAndOpacity(LDim).Text(LTx(TEXT("CONTROLLERS AND KEYBOARD  -  EACH PLAYER CONTROLS ONE MAN AT A TIME"))) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Rows ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 20, 0, 10)
		[
			SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(bReady ? LGold : FLinearColor(1, 1, 1, 0.07f)).Padding(FMargin(20, 8))
			[ SNew(STextBlock).Font(TagTheme::Font(15, "BoldCondensed", 60)).ColorAndOpacity(bReady ? LInk : FLinearColor::White).Text(LTx(Status.ToUpper())) ]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[ TagTheme::HintBar(Keys) ];

	TSharedRef<SWidget> Root = SNew(SLobbyRoot)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(TagTheme::SBackdrop).Strength(1.2f) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[ SNew(TagTheme::SPanel).Fill(FLinearColor(0.02f, 0.026f, 0.044f, 0.9f)).Padding(FMargin(32, 28))[ Page ] ]
	];
	// (no focus frame: the screen draws its own state, as the play call does)
	CallRoot = Root;
	ShowModal(Root);
}
