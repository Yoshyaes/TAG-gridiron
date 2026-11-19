// The local multiplayer lobby, Madden's controller select: each player who presses a button on his pad (CROSS / A) or
// the keyboard (ENTER) gets a card in the middle; he moves it left to play for the away team, right for the home team
// (or leaves it in the middle to watch). On a side with two or more players, one calls the plays: the first there,
// until another takes it (TRIANGLE / Y, keyboard C). OPTIONS / MENU (ENTER) goes on to the team select.
//
// The input is read from every device apart by the director (AMatchDirector::TickLobby, from FTagInputHub); this screen
// only draws it, and is rebuilt whenever it changes. Its root keeps the keyboard focus and swallows the keys, so nothing
// behind it acts on them.
#include "UI/GridironOverlay.h"

#include "Game/MatchDirector.h"
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
	bHud = false;
	bSelMulti = false;
	const TArray<AMatchDirector::FTagHuman>& Hs = Director->Humans;
	const float ColW = 330.f;

	auto Card = [&](int32 i) -> TSharedRef<SWidget>
	{
		const AMatchDirector::FTagHuman& H = Hs[i];
		const FLinearColor C = PlayerColor(i);
		const FString Dev = Director->HumanDevice(i);
		const FString Hint = H.Team < 0 ? (H.bPad ? TEXT("<  pick a side  >") : TEXT("A  pick a side  D")) : H.Team == 0 ? (H.bPad ? TEXT("AWAY   >") : TEXT("AWAY   D")) : (H.bPad ? TEXT("<   HOME") : TEXT("A   HOME"));
		return SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(FLinearColor(0.1f, 0.11f, 0.15f, 0.97f)).Padding(0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(C).Padding(FMargin(14, 10)).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(LFont(24, "Bold")).ColorAndOpacity(LInk).Text(LTx(AMatchDirector::HumanTag(i))) ]
			]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(FMargin(12, 6))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(LFont(13, "Bold")).ColorAndOpacity(FLinearColor::White).Text(LTx(Dev.ToUpper())) ]
				+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(LFont(10)).ColorAndOpacity(LDim).Text(LTx(Hint)) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0, 4, 0, 0)
				[
					SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(LGold).Padding(FMargin(8, 2))
					.Visibility(H.bCaller ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
					[ SNew(STextBlock).Font(LFont(10, "Bold")).ColorAndOpacity(LInk).Text(LTx(TEXT("CALLS THE PLAYS"))) ]
				]
			]
		];
	};
	auto Empty = [&](int32 i) -> TSharedRef<SWidget>
	{
		return SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.05f)).Padding(FMargin(14, 16)).HAlign(HAlign_Center)
		[ SNew(STextBlock).Font(LFont(12, "Bold")).ColorAndOpacity(PlayerColor(i) * FLinearColor(1, 1, 1, 0.55f))
			.Text(LTx(FString::Printf(TEXT("%s   PRESS  CROSS / A  OR  ENTER  TO JOIN"), *AMatchDirector::HumanTag(i)))) ];
	};
	auto Header = [&](const FString& Big, const FString& Small, const FLinearColor& C) -> TSharedRef<SWidget>
	{
		return SNew(SBox).WidthOverride(ColW)
		[
			SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(C).Padding(FMargin(12, 8)).HAlign(HAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(LFont(22, "Bold")).ColorAndOpacity(FLinearColor::White).Text(LTx(Big)) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(LFont(10)).ColorAndOpacity(LDim).Text(LTx(Small)) ]
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
	Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 10)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ Header(TEXT("AWAY"), SideNote(0), FLinearColor(0.55f, 0.12f, 0.12f, 0.95f)) ]
		+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[ Header(TEXT("WATCHING"), TEXT("NOT ON A SIDE YET"), FLinearColor(0.16f, 0.17f, 0.22f, 0.95f)) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Header(TEXT("HOME"), SideNote(1), FLinearColor(0.1f, 0.25f, 0.55f, 0.95f)) ]
	];
	for (int32 i = 0; i < 4; ++i)
	{
		const int32 Col = i < Hs.Num() ? (Hs[i].Team < 0 ? 1 : Hs[i].Team == 0 ? 0 : 2) : 1;
		auto Cell = [&](int32 C) -> TSharedRef<SWidget>
		{
			TSharedRef<SWidget> In = SNullWidget::NullWidget;
			if (C == Col) In = i < Hs.Num() ? Card(i) : Empty(i);
			return SNew(SBox).WidthOverride(ColW).HeightOverride(78.f)[ In ];
		};
		Rows->AddSlot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(0) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0)[ Cell(1) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Cell(2) ]
		];
	}
	const bool bReady = Director->LobbyReady();
	const FString Status = Hs.Num() == 0 ? FString(TEXT("Up to four players: every controller and the keyboard is its own player"))
		: !bReady ? FString(TEXT("Move to a side to play: left for the away team, right for the home team"))
		: FString(TEXT("OPTIONS / MENU  or  ENTER   -   on to the teams"));
	const FString Keys = TEXT("JOIN  CROSS / A  -  ENTER        SIDE  D-PAD  -  A / D        CALL THE PLAYS  TRIANGLE / Y  -  C        BACK  CIRCLE / B  -  BACKSPACE");

	TSharedRef<SWidget> Page = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(LFont(40, "Bold")).ColorAndOpacity(LGold).Text(LTx(TEXT("LOCAL MULTIPLAYER"))) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 22)
		[ SNew(STextBlock).Font(LFont(12)).ColorAndOpacity(LDim).Text(LTx(TEXT("UP TO 4 PLAYERS  -  CONTROLLERS AND KEYBOARD  -  EACH PLAYER CONTROLS ONE MAN AT A TIME"))) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Rows ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 6)
		[
			SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(bReady ? LGold : FLinearColor(1, 1, 1, 0.08f)).Padding(FMargin(18, 7))
			[ SNew(STextBlock).Font(LFont(14, "Bold")).ColorAndOpacity(bReady ? LInk : FLinearColor::White).Text(LTx(Status)) ]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)[ SNew(STextBlock).Font(LFont(10)).ColorAndOpacity(LDim).Text(LTx(Keys)) ];

	TSharedRef<SWidget> Root = SNew(SLobbyRoot)
	[
		SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.025f, 0.86f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[ SNew(SBorder).BorderImage(LWhite()).BorderBackgroundColor(LPanel).Padding(FMargin(28, 24))[ Page ] ]
	];
	// (no focus frame: the screen draws its own state, as the play call does)
	CallRoot = Root;
	ShowModal(Root);
}
