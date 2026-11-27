// The depth chart (Madden's): every slot a team fills - the eleven a side by their spots, the specialists and the
// situational subs - in order, and who is listed at each.
//
// Left: the slots of a group (Offense, Defense, Special Teams, Situational; L1 / R1 the group), each with its
// starter. Middle: the slot's chart - depth, name, his rating at the slot and the slot's key ratings, the starters
// above the line. Cross picks a man up; up / down carries him; cross puts him down (the order is saved at once).
// Square shows the whole roster under the chart so a man from any position can be added (a corner at free safety);
// triangle sorts the slot by the rating at the slot; L3 gives the slot back to the coach, R3 the whole team. Right:
// the man's card and the starting lineup, the slot's spot lit. L2 / R2 another team; circle goes back.
//
// The chart drives the game (Sim/Roster.h): a slot the user set is the order the sim fills it in; an untouched slot is
// the coach's (best OVR first, which is what the screen shows for it).
#include "UI/RosterUI.h"

#include "Audio/TagAudio.h"
#include "Game/MatchDirector.h"
#include "Sim/Abilities.h"
#include "Sim/League.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SNullWidget.h"
#include "Misc/CommandLine.h"
#include <algorithm>

using namespace RosterUI;

namespace DepthUI
{
	int32 TeamIdx = -1;
	int32 SlotI = 0;       // the DSlot under the cursor
	int32 Col = 0;         // 0 the slots, 1 the chart
	int32 Row = 0;         // the chart's row
	bool bAll = false;     // the whole roster under the chart
	int32 HeldId = -1;     // the man picked up (carried with up / down)
	std::function<void()> Back;
	FString Msg;
	FLinearColor MsgColor = Dim;
	void Say(const FString& S, const FLinearColor& C) { Msg = S; MsgColor = C; }

	tag::DSlot Slot() { return static_cast<tag::DSlot>(FMath::Clamp(SlotI, 0, tag::DSLOTS - 1)); }
	TArray<tag::DSlot> GroupSlots(tag::DGroup G)
	{
		TArray<tag::DSlot> V;
		for (int32 i = 0; i < tag::DSLOTS; ++i)
			if (tag::dslotInfo(static_cast<tag::DSlot>(i)).group == G) V.Add(static_cast<tag::DSlot>(i));
		return V;
	}
	FLinearColor GradeColor(tag::StarGrade G)
	{
		switch (G)
		{
		case tag::StarGrade::XFactor: return FLinearColor(FColor::FromHex(TEXT("FF6A1A")));
		case tag::StarGrade::Superstar: return FLinearColor(FColor::FromHex(TEXT("B58CFF")));
		case tag::StarGrade::Star: return FLinearColor(FColor::FromHex(TEXT("72B8FF")));
		default: return Faint;
		}
	}
	const TCHAR* GradeShort(tag::StarGrade G)
	{
		switch (G)
		{
		case tag::StarGrade::XFactor: return TEXT("XF");
		case tag::StarGrade::Superstar: return TEXT("SS");
		case tag::StarGrade::Star: return TEXT("ST");
		default: return TEXT("");
		}
	}
	/** The chart's rows: the slot's list, then (show all) every other man, best at the slot first. */
	struct FRows
	{
		std::vector<tag::Player*> Listed, Others;
		int32 Num() const { return static_cast<int32>(Listed.size() + Others.size()); }
		tag::Player* At(int32 i) const
		{
			if (i < 0) return nullptr;
			if (i < static_cast<int32>(Listed.size())) return Listed[static_cast<size_t>(i)];
			i -= static_cast<int32>(Listed.size());
			return i < static_cast<int32>(Others.size()) ? Others[static_cast<size_t>(i)] : nullptr;
		}
	};
	FRows RowsOf(const tag::Team& T, tag::DSlot S, bool bAllMen)
	{
		FRows R;
		R.Listed = tag::chartOrder(T, S);
		if (bAllMen)
		{
			for (const auto& P : T.roster)
				if (std::find(R.Listed.begin(), R.Listed.end(), P.get()) == R.Listed.end()) R.Others.push_back(P.get());
			std::stable_sort(R.Others.begin(), R.Others.end(), [S](const tag::Player* A, const tag::Player* B) { return tag::slotOvr(*A, S) > tag::slotOvr(*B, S); });
		}
		return R;
	}
	/** The slots where a man starts (his starters' spots), but the one on screen. */
	FString StartsAt(const tag::Team& T, const tag::Player& P, tag::DSlot Except)
	{
		FString S;
		for (int32 i = 0; i < tag::DSLOTS; ++i)
		{
			const tag::DSlot D = static_cast<tag::DSlot>(i);
			if (D == Except) continue;
			const std::vector<tag::Player*> St = tag::chartStarters(T, D);
			if (std::find(St.begin(), St.end(), &P) != St.end())
				S += (S.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(ANSI_TO_TCHAR(tag::dslotInfo(D).abbr));
		}
		return S;
	}
	int32 SetCount(const tag::Team& T)
	{
		int32 N = 0;
		for (int32 i = 0; i < tag::DSLOTS; ++i) N += tag::chartSet(T, static_cast<tag::DSlot>(i));
		return N;
	}
}

using namespace DepthUI;

void SGridironOverlay::ShowDepthChart(int32 InTeam, std::function<void()> InBack, int32 FocusSlot, int32 FocusPlayerId)
{
	if (!Director || Director->League.empty()) return;
	const int32 NTeams = static_cast<int32>(Director->League.size());
	ATagAudio::Screen(TEXT("DepthChart"));
	InTeam = FMath::Clamp(InTeam, 0, NTeams - 1);
	if (InTeam != TeamIdx || InBack)
	{
		if (InTeam != TeamIdx) { Row = 0; HeldId = -1; }
		Msg.Reset();
	}
	if (InBack)
	{
		// a fresh visit: the chart column off, nothing held
		HeldId = -1;
		bAll = false;
		Col = 0;
	}
	TeamIdx = InTeam;
	if (InBack) Back = InBack;
	tag::Team& Tm = *Director->League[TeamIdx];
	if (FocusSlot >= 0 && FocusSlot < tag::DSLOTS)
	{
		SlotI = FocusSlot;
		Col = FocusPlayerId >= 0 ? 1 : 0;
		Row = 0;
		const std::vector<tag::Player*> L = tag::chartOrder(Tm, Slot());
		for (size_t i = 0; i < L.size(); ++i)
			if (L[i]->id == FocusPlayerId) Row = static_cast<int32>(i);
	}
	const tag::DSlot S = Slot();
	const tag::DSlotInfo& Info = tag::dslotInfo(S);
	const FRows Rows = RowsOf(Tm, S, bAll);
	Row = FMath::Clamp(Row, 0, FMath::Max(0, Rows.Num() - 1));
	const bool bSet = tag::chartSet(Tm, S);
	const FLinearColor Pri = TeamInk(Tm);
	auto Rebuild = [this] { ShowDepthChart(TeamIdx, nullptr); };
	auto Pick = [this, Rebuild](int32 Slot_, int32 Col_, int32 Row_) { SlotI = Slot_; Col = Col_; Row = Row_; if (Col_ == 0) HeldId = -1; Rebuild(); };

	// ---- the left column: the group's tabs and its slots
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	for (int32 g = 0; g < 4; ++g)
	{
		const tag::DGroup G = static_cast<tag::DGroup>(g);
		const bool bOn = G == Info.group;
		const TArray<tag::DSlot> Gs = GroupSlots(G);
		Tabs->AddSlot().FillWidth(1).Padding(0, 0, g < 3 ? 3 : 0, 0)
		[
			Click(SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bOn ? Gold : FLinearColor(1, 1, 1, 0.06f)).Padding(FMargin(4, 5)).HAlign(HAlign_Center)
				[ Label(G == tag::DGroup::Special ? FString(TEXT("SPECIAL")) : FString(UTF8_TO_TCHAR(tag::dgroupName(G))).ToUpper(), 8, bOn ? Ink : Dim) ],
				[Pick, Gs] { Pick(static_cast<int32>(Gs[0]), 0, 0); })
		];
	}
	TSharedRef<SVerticalBox> SlotList = SNew(SVerticalBox);
	for (tag::DSlot D : GroupSlots(Info.group))
	{
		const tag::DSlotInfo& I = tag::dslotInfo(D);
		const bool bSel = D == S;
		const std::vector<tag::Player*> St = tag::chartStarters(Tm, D);
		FString Who = St.empty() ? FString(TEXT("-")) : Short(*St[0]);
		if (St.size() > 1) Who += FString::Printf(TEXT("  +%d"), static_cast<int32>(St.size()) - 1);
		const int32 O = St.empty() ? 0 : tag::slotOvr(*St[0], D);
		const bool bMine = tag::chartSet(Tm, D);
		SlotList->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[
			Click(Frame(bSel, Col == 0,
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(4)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bSel ? Gold : bMine ? Pri : FLinearColor(1, 1, 1, 0.05f)) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 6, 0, 6)[ SNew(SBox).WidthOverride(56)[ Label(ANSI_TO_TCHAR(I.abbr), 12, bSel ? Gold : White90) ] ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Clip(Who.ToUpper(), 10, St.empty() ? Faint : Dim) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[ bMine ? Chip(TEXT("SET"), Pri * FLinearColor(1, 1, 1, 0.3f), White90, 7) : StaticCastSharedRef<SWidget>(SNew(SBox)) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 10, 0)[ SNew(SBox).WidthOverride(26).HAlign(HAlign_Right)[ Label(O ? FString::FromInt(O) : FString(), 12, OvrColor(O)) ] ]),
				[Pick, D] { Pick(static_cast<int32>(D), 0, 0); })
		];
	}

	// ---- the middle: the slot's chart
	const std::vector<const tag::RatingDef*> Keys = tag::slotKeyRatings(S, 6);
	TSharedRef<SHorizontalBox> Head = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(40)[ Label(TEXT("DEPTH"), 8, Faint) ] ]
		+ SHorizontalBox::Slot().FillWidth(1)[ Label(TEXT("PLAYER"), 8, Faint) ]
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(56)[ Label(TEXT("POS"), 8, Faint) ] ]
		+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(56).HAlign(HAlign_Center)[ Label(FString::Printf(TEXT("%s OVR"), ANSI_TO_TCHAR(Info.abbr)), 7, Gold) ] ];
	for (const tag::RatingDef* K : Keys)
		Head->AddSlot().AutoWidth()[ SNew(SBox).WidthOverride(42).HAlign(HAlign_Center)[ Label(ANSI_TO_TCHAR(K->abbr), 8, Faint) ] ];
	Head->AddSlot().AutoWidth()[ SNew(SBox).WidthOverride(38).HAlign(HAlign_Center)[ Label(TEXT("AGE"), 8, Faint) ] ];
	Head->AddSlot().AutoWidth()[ SNew(SBox).WidthOverride(76)[ Label(TEXT(""), 8, Faint) ] ];

	TSharedRef<SVerticalBox> Chart = SNew(SVerticalBox);
	const int32 Window = 15;
	const int32 First = FMath::Clamp(Row - Window / 2, 0, FMath::Max(0, Rows.Num() - Window));
	const int32 NListed = static_cast<int32>(Rows.Listed.size());
	int32 Healthy = 0;
	for (int32 i = 0; i < NListed && i < First; ++i) Healthy += Rows.Listed[static_cast<size_t>(i)]->injuredWeeks <= 0;
	bool bLine = false, bOthersHead = false;
	for (int32 i = First; i < Rows.Num() && i < First + Window; ++i)
	{
		tag::Player& P = *Rows.At(i);
		const bool bListed = i < NListed;
		const bool bSel = Col == 1 && i == Row;
		const bool bHeld = P.id == HeldId;
		const bool bHurt = P.injuredWeeks > 0;
		const bool bStarter = bListed && !bHurt && Healthy < Info.starters;
		if (bListed && !bHurt) ++Healthy;
		if (bListed && !bStarter && !bLine && i > 0)
		{
			bLine = true;
			Chart->AddSlot().AutoHeight().Padding(0, 2, 0, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(TEXT("BACKUPS"), 7, Faint) ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6, 0, 0, 0)[ Fill(FLinearColor(1, 1, 1, 0.1f)) ]
			];
		}
		if (!bListed && !bOthersHead)
		{
			bOthersHead = true;
			Chart->AddSlot().AutoHeight().Padding(0, 8, 0, 4)[ Heading(TEXT("The rest of the roster  ·  best at the slot first  ·  cross adds him")) ];
		}
		const int32 SO = tag::slotOvr(P, S);
		const bool bOut = P.pos != Info.natural;
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(4)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bHeld ? Good : bStarter ? Gold : FLinearColor::Transparent) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
			[ SNew(SBox).WidthOverride(30)[ Label(bListed ? FString::FromInt(i + 1) : FString(TEXT("+")), 12, bStarter ? Gold : bListed ? Dim : Good) ] ]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 5)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(34)[ Label(FString::Printf(TEXT("#%d"), P.number), 10, Faint) ] ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Clip(Full(P).ToUpper(), 11, bHurt ? Faint : White90) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(56)[ Label(bOut ? FString::Printf(TEXT("%s %d"), *Pos(P.pos), P.ovr) : Pos(P.pos), 10, bOut ? Warn : Dim) ] ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(56).HAlign(HAlign_Center)[ Label(FString::FromInt(SO), 14, OvrColor(SO)) ] ];
		for (const tag::RatingDef* K : Keys)
		{
			const int32 V = FMath::RoundToInt(P.r.*(K->m));
			Line->AddSlot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(42).HAlign(HAlign_Center)[ Label(FString::FromInt(V), 10, OvrColor(V), "Regular") ] ];
		}
		Line->AddSlot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(38).HAlign(HAlign_Center)[ Label(FString::FromInt(P.age), 10, Faint, "Regular") ] ];
		TSharedRef<SHorizontalBox> Tags = SNew(SHorizontalBox);
		if (bHurt) Tags->AddSlot().AutoWidth().Padding(0, 0, 3, 0)[ Chip(TEXT("INJ"), Warn, Ink, 7) ];
		if (P.created) Tags->AddSlot().AutoWidth().Padding(0, 0, 3, 0)[ Chip(TEXT("CAP"), Cap, Ink, 7) ];
		if (P.grade != tag::StarGrade::None) Tags->AddSlot().AutoWidth().Padding(0, 0, 3, 0)[ Chip(GradeShort(P.grade), GradeColor(P.grade), Ink, 7) ];
		if (bHeld) Tags->AddSlot().AutoWidth()[ Chip(TEXT("MOVING"), Good, Ink, 7) ];
		Line->AddSlot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(76).Padding(FMargin(4, 0, 6, 0))[ Tags ] ];
		Chart->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
		[
			Click(Frame(bSel || bHeld, Col == 1, Line, bListed ? Card : FLinearColor(0.025f, 0.03f, 0.045f, 0.9f)),
				[Pick, i] { Pick(SlotI, 1, i); })
		];
	}
	if (Rows.Num() > Window)
		Chart->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[ Label(FString::Printf(TEXT("%d - %d of %d  ·  up / down for more"), First + 1, FMath::Min(First + Window, Rows.Num()), Rows.Num()), 9, Faint) ];

	// ---- the right: the man's card
	tag::Player* Me = Col == 1 ? Rows.At(Row) : nullptr;
	if (!Me)
	{
		const std::vector<tag::Player*> St = tag::chartStarters(Tm, S);
		Me = !St.empty() ? St[0] : Rows.At(0);
	}
	TSharedRef<SWidget> CardW = SNew(SBox);
	if (Me)
	{
		const tag::Player& P = *Me;
		const int32 SO = tag::slotOvr(P, S);
		TSharedRef<SVerticalBox> Bars = SNew(SVerticalBox);
		std::vector<const tag::RatingDef*> Show = tag::slotKeyRatings(S, 8);
		for (const tag::RatingDef* K : tag::keyRatings(P.pos, 4))
			if (std::find(Show.begin(), Show.end(), K) == Show.end()) Show.push_back(K);
		if (Show.size() > 10) Show.resize(10);
		for (const tag::RatingDef* K : Show)
		{
			const double V = P.r.*(K->m);
			Bars->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(150)[ Label(UTF8_TO_TCHAR(K->name), 9, Dim, "Regular") ] ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Bar(V, 300.f, 7.f) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ SNew(SBox).WidthOverride(26).HAlign(HAlign_Right)[ Label(FString::FromInt(FMath::RoundToInt(V)), 11, OvrColor(FMath::RoundToInt(V))) ] ]
			];
		}
		const FString Starts = StartsAt(Tm, P, S);
		FString Arch;
		if (P.archetype >= 0)
		{
			const auto& A = tag::capArchetypes(P.pos);
			if (P.archetype < static_cast<int32>(A.size())) Arch = FString(TEXT("  ·  ")) + UTF8_TO_TCHAR(A[static_cast<size_t>(P.archetype)].name);
		}
		TSharedRef<SHorizontalBox> Badges = SNew(SHorizontalBox);
		if (P.grade != tag::StarGrade::None) Badges->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[ Chip(FString(UTF8_TO_TCHAR(tag::starGradeName(P.grade))).ToUpper(), GradeColor(P.grade), Ink, 8) ];
		if (P.created) Badges->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[ Chip(TEXT("CREATED PLAYER"), Cap, Ink, 8) ];
		if (P.injuredWeeks > 0) Badges->AddSlot().AutoWidth()[ Chip(TEXT("INJURED"), Warn, Ink, 8) ];
		CardW = SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(CardHi).Padding(0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Fill(Pri, 3.f) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 10, 16, 6))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Label(FString::Printf(TEXT("#%d  ·  %s%s  ·  %s"), P.number, *Pos(P.pos), *Arch, *Str(Tm.abbr)), 10, Dim) ]
					+ SVerticalBox::Slot().AutoHeight()[ Label(Str(P.first), 12, Dim, "Regular") ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, -3, 0, 0)[ Clip(Str(P.last).ToUpper(), 24, White90) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[ Label(FString::Printf(TEXT("%s  ·  %d LBS  ·  AGE %d"), *Height(P.heightIn), FMath::RoundToInt(P.weightLb), P.age), 9, Faint) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)[ Badges ]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(10, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Label(FString::FromInt(SO), 34, OvrColor(SO)) ]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, -6, 0, 0)[ Label(FString::Printf(TEXT("AT %s"), ANSI_TO_TCHAR(Info.abbr)), 8, Faint) ]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 0)
					[ Label(SO != P.ovr || P.pos != Info.natural ? FString::Printf(TEXT("%d AS %s"), P.ovr, *Pos(P.pos)) : FString(TEXT("OVR")), 8, Faint) ]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 0, 16, 6))
			[ Label(Starts.IsEmpty() ? FString(TEXT("Starts nowhere else")) : FString(TEXT("Also starts at  ")) + Starts, 9, Starts.IsEmpty() ? Faint : Gold, "Regular") ]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 2, 16, 12))[ Bars ]
		];
	}

	// ---- the starting lineup: the group's spots, the slot on screen lit
	auto Spot = [&Tm, S, Pick](tag::DSlot D, int32 K) -> TSharedRef<SWidget>
	{
		const std::vector<tag::Player*> St = tag::chartStarters(Tm, D);
		const tag::Player* P = K < static_cast<int32>(St.size()) ? St[static_cast<size_t>(K)] : nullptr;
		const bool bLit = D == S;
		FString Abbr = ANSI_TO_TCHAR(tag::dslotInfo(D).abbr);
		if (tag::dslotInfo(D).starters > 1) Abbr += FString::FromInt(K + 1);
		const int32 O = P ? tag::slotOvr(*P, D) : 0;
		return Click(SNew(SBox).WidthOverride(68).HeightOverride(40)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bLit ? Gold : FLinearColor(0.02f, 0.03f, 0.04f, 0.88f)).Padding(FMargin(4, 2))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1)[ Label(Abbr, 8, bLit ? Ink : Gold) ]
						+ SHorizontalBox::Slot().AutoWidth()[ Label(O ? FString::FromInt(O) : FString(), 8, bLit ? Ink : OvrColor(O)) ]
					]
					+ SVerticalBox::Slot().AutoHeight()[ Clip(P ? Str(P->last).ToUpper() : FString(TEXT("-")), 8, bLit ? Ink : White90, "Regular") ]
				]
			], [Pick, D] { Pick(static_cast<int32>(D), 0, 0); });
	};
	TSharedRef<SCanvas> Field = SNew(SCanvas);
	auto Put = [&Field](float X, float Y, TSharedRef<SWidget> W) { Field->AddSlot().Position(FVector2D(X, Y)).Size(FVector2D(68, 40))[ W ]; };
	using DS = tag::DSlot;
	const tag::DGroup G = Info.group;
	const bool bOffView = G == tag::DGroup::Offense || S == DS::TDRB || S == DS::PWHB || S == DS::SWR;
	FString FieldTitle;
	if (G == tag::DGroup::Special)
	{
		FieldTitle = TEXT("Special teams");
		const DS Sp[] = {DS::K, DS::P, DS::KOS, DS::LS, DS::KR, DS::PR};
		for (int32 i = 0; i < 6; ++i) Put(40.f + (i % 3) * 200.f, 26.f + (i / 3) * 84.f, Spot(Sp[i], 0));
	}
	else if (bOffView)
	{
		FieldTitle = TEXT("Starting offense  ·  11 personnel");
		const float L = 18.f;
		Put(4, L, Spot(DS::WR, 0));
		Put(96, L, Spot(DS::LT, 0));
		Put(166, L, Spot(DS::LG, 0));
		Put(236, L, Spot(DS::C, 0));
		Put(306, L, Spot(DS::RG, 0));
		Put(376, L, Spot(DS::RT, 0));
		Put(446, L, Spot(DS::TE, 0));
		Put(526, L, Spot(DS::WR, 1));
		Put(40, L + 58, tag::chartSet(Tm, DS::SWR) || S == DS::SWR ? Spot(DS::SWR, 0) : Spot(DS::WR, 2));
		Put(236, L + 58, Spot(DS::QB, 0));
		Put(236, L + 116, S == DS::TDRB || S == DS::PWHB ? Spot(S, 0) : Spot(DS::HB, 0));
		Put(526, L + 116, Spot(DS::FB, 0));
	}
	else
	{
		FieldTitle = TEXT("Starting defense  ·  4-3 base");
		const float L = 18.f;
		Put(4, L, Spot(DS::CB, 0));
		Put(126, L, S == DS::RLE ? Spot(DS::RLE, 0) : Spot(DS::LE, 0));
		Put(206, L, S == DS::NT ? Spot(DS::NT, 0) : Spot(DS::DT, 0));
		Put(286, L, S == DS::RDT ? Spot(DS::RDT, 0) : Spot(DS::DT, 1));
		Put(366, L, S == DS::RRE ? Spot(DS::RRE, 0) : Spot(DS::RE, 0));
		Put(526, L, Spot(DS::CB, 1));
		Put(60, L + 58, Spot(DS::SLCB, 0));
		Put(156, L + 58, S == DS::SUBLB ? Spot(DS::SUBLB, 0) : Spot(DS::LOLB, 0));
		Put(266, L + 58, S == DS::SUBLB ? Spot(DS::SUBLB, 1) : Spot(DS::MLB, 0));
		Put(376, L + 58, Spot(DS::ROLB, 0));
		Put(186, L + 116, Spot(DS::FS, 0));
		Put(346, L + 116, Spot(DS::SS, 0));
	}
	TSharedRef<SWidget> FieldW = SNew(SBox).WidthOverride(600).HeightOverride(200)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.04f, 0.16f, 0.07f, 0.85f)) ]
			+ SOverlay::Slot().VAlign(VAlign_Top).Padding(0, 20.f, 0, 0)
			[ G == tag::DGroup::Special ? StaticCastSharedRef<SWidget>(SNew(SBox)) : Fill(FLinearColor(1, 1, 1, 0.35f), 2.f) ]
			+ SOverlay::Slot().Padding(FMargin(0, 6, 0, 0))[ Field ]
		];

	// ---- the header numbers
	const tag::TeamOvr TO = tag::teamOvr(Tm);
	const int32 NSet = SetCount(Tm);
	const FString Status = bSet ? FString(TEXT("YOUR ORDER")) : FString(TEXT("COACH'S ORDER"));
	TSharedRef<SWidget> LogoW = SNew(SBox).WidthOverride(52).HeightOverride(52)
		[ Logo(Tm) ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Logo(Tm))) : StaticCastSharedRef<SWidget>(SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Pri)) ];

	const bool bPad = PadGlyphs();
	auto Hint = [this](EPadGlyph Gl, const FString& Key, const TCHAR* What) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SPadGlyph).Owner(this).Glyph(Gl).Key(Key).Size(22.f) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(*What ? 6.f : 1.f, 0, *What ? 18.f : 1.f, 0)[ Label(What, 10, White90, "Regular") ];
	};
	const bool bHeld = HeldId >= 0;
	TSharedRef<SWidget> Hints = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DUpDown, TEXT("↑↓"), bHeld ? TEXT("Carry") : TEXT("Move")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DLeftRight, TEXT("←→"), TEXT("Slots / Chart")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L1, TEXT("Q"), TEXT("")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R1, TEXT("E"), TEXT("Group")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L2, TEXT("Z"), TEXT("")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R2, TEXT("C"), TEXT("Team")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Cross, TEXT("Enter"), bHeld ? TEXT("Put down") : TEXT("Pick up")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Square, TEXT("R"), bAll ? TEXT("Chart only") : TEXT("Whole roster")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Triangle, TEXT("T"), TEXT("Best at slot")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(bPad ? EPadGlyph::L3 : EPadGlyph::None, bPad ? TEXT("L3") : TEXT("X"), TEXT("Coach's slot")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(bPad ? EPadGlyph::R3 : EPadGlyph::None, bPad ? TEXT("R3") : TEXT("V"), TEXT("Coach's team")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Circle, TEXT("Esc"), TEXT("Done")) ];

	TSharedRef<SWidget> Page = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 0, 0, 10))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ LogoW ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(12, 0, 0, 0)[ Label(TEXT("DEPTH CHART"), 30, FLinearColor::White) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(14, 0, 0, 5)
			[ Label(FString::Printf(TEXT("%s %s"), *Str(Tm.city).ToUpper(), *Str(Tm.name).ToUpper()), 13, Pri) ]
			+ SHorizontalBox::Slot().FillWidth(1)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 0, 5)
			[
				Label(FString::Printf(TEXT("OVR %d  ·  OFF %d  ·  DEF %d  ·  ROSTER %d / %d  ·  %d SLOT%s SET"), TO.ovr, TO.off, TO.def,
					static_cast<int32>(Tm.roster.size()), static_cast<int32>(tag::ROSTER_MAX), NSet, NSet == 1 ? TEXT("") : TEXT("S")), 10, Dim)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(360)
				[
					SNew(TagTheme::SPanel).Fill(Card).Radius(6.f).AccentTop(Col == 0 ? Gold : FLinearColor(1, 1, 1, 0.1f)).Padding(FMargin(0))
					[
						SNew(SBox).Padding(FMargin(10, 10))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ Tabs ]
							+ SVerticalBox::Slot().AutoHeight()[ SlotList ]
						]
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(860)
				[
					SNew(TagTheme::SPanel).Fill(Card).Radius(6.f).AccentTop(Col == 1 ? Gold : FLinearColor(1, 1, 1, 0.1f)).Padding(FMargin(0))
					[
						SNew(SBox).Padding(FMargin(12, 10))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[ Label(FString(UTF8_TO_TCHAR(Info.name)).ToUpper(), 18, White90) ]
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(10, 0, 0, 3)[ Label(ANSI_TO_TCHAR(Info.abbr), 11, Gold) ]
								+ SHorizontalBox::Slot().FillWidth(1)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
								[ Chip(Status, bSet ? Pri * FLinearColor(1, 1, 1, 0.45f) : FLinearColor(1, 1, 1, 0.08f), bSet ? White90 : Dim, 8) ]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)
							[ Label(FString::Printf(TEXT("%s  ·  %d starter%s  ·  rated as %s"), UTF8_TO_TCHAR(Info.when), Info.starters, Info.starters == 1 ? TEXT("") : TEXT("s"), *Pos(Info.natural)), 9, Faint, "Regular") ]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[ Head ]
							+ SVerticalBox::Slot().AutoHeight()[ Chart ]
						]
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(12, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(624)
				[
					SNew(TagTheme::SPanel).Fill(Card).Radius(6.f).Padding(FMargin(12, 10))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ CardW ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[ Heading(FieldTitle) ]
						+ SVerticalBox::Slot().AutoHeight()[ FieldW ]
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, 10, 0, 0))
		[ SNew(SBox).HeightOverride(18)[ Label(Msg.IsEmpty() ? FString(TEXT("Set slots are the order the game plays them in; an untouched slot is the coach's (best OVR first).")) : Msg, 11, Msg.IsEmpty() ? Faint : MsgColor) ] ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, 6, 0, 0))[ TagTheme::HintBar(Hints) ];

	// ---- the keys
	auto OnKey = [this, Rebuild](const FKey& K) -> bool
	{
		if (!Director || Director->League.empty()) return false;
		const FNav N(K);
		tag::Team& T = *Director->League[TeamIdx];
		const tag::DSlot Sl = Slot();
		const tag::DSlotInfo& In = tag::dslotInfo(Sl);
		FRows R = RowsOf(T, Sl, bAll);
		// (a man being carried slides a place; nothing else moves while he is held)
		if (HeldId >= 0) { if (N.bUp || N.bDown) ATagAudio::Ui(TEXT("UiSwap")); }
		else if (N.Moves()) ATagAudio::Ui(TEXT("UiMove"));
		auto Commit = [&](const std::vector<tag::Player*>& Order)
		{
			tag::setChart(T, Sl, Order);
			SaveChart(T);
		};
		if (N.bLeft || N.bRight)
		{
			if (HeldId >= 0) return true;
			Col = N.bRight ? 1 : 0;
			if (Col == 1) Row = FMath::Clamp(Row, 0, FMath::Max(0, R.Num() - 1));
			Rebuild();
			return true;
		}
		if (N.bPrev || N.bNext)
		{
			// another group: its first slot
			if (HeldId >= 0) return true;
			const int32 NextG = (static_cast<int32>(In.group) + (N.bNext ? 1 : 3)) % 4;
			SlotI = static_cast<int32>(GroupSlots(static_cast<tag::DGroup>(NextG))[0]);
			Row = 0;
			Rebuild();
			return true;
		}
		if (N.bPrevT || N.bNextT)
		{
			if (HeldId >= 0) return true;
			ATagAudio::Ui(TEXT("UiTab"));
			const int32 NT = static_cast<int32>(Director->League.size());
			TeamIdx = (TeamIdx + (N.bNextT ? 1 : NT - 1)) % NT;
			Row = 0;
			Msg.Reset();
			Rebuild();
			return true;
		}
		if (N.bUp || N.bDown)
		{
			const int32 Dir = N.bDown ? 1 : -1;
			if (Col == 0)
			{
				// through every slot: the end of a group runs into the next
				SlotI = (SlotI + Dir + tag::DSLOTS) % tag::DSLOTS;
				Row = 0;
			}
			else if (HeldId >= 0)
			{
				// carrying: he trades places with the man above / below
				std::vector<tag::Player*> L = R.Listed;
				auto It = std::find_if(L.begin(), L.end(), [](const tag::Player* P) { return P->id == HeldId; });
				if (It != L.end())
				{
					const int32 I = static_cast<int32>(It - L.begin());
					const int32 J = I + Dir;
					if (J >= 0 && J < static_cast<int32>(L.size()))
					{
						std::swap(L[static_cast<size_t>(I)], L[static_cast<size_t>(J)]);
						Commit(L);
						Row = J;
					}
				}
			}
			else Row = FMath::Clamp(Row + Dir, 0, FMath::Max(0, R.Num() - 1));
			Rebuild();
			return true;
		}
		if (N.bOk)
		{
			if (Col == 0) { ATagAudio::Ui(TEXT("UiTab")); Col = 1; Row = 0; Rebuild(); return true; }
			tag::Player* P = R.At(Row);
			if (!P) return true;
			if (HeldId >= 0)
			{
				ATagAudio::Ui(TEXT("UiDrop"));
				HeldId = -1;
				Say(FString::Printf(TEXT("%s: %s is %s %d"), *Str(T.abbr), *Short(*P), ANSI_TO_TCHAR(In.abbr), Row + 1), Good);
			}
			else if (Row >= static_cast<int32>(R.Listed.size()))
			{
				// a man from the rest of the roster onto the slot (at the bottom, picked up to carry where he goes)
				std::vector<tag::Player*> L = R.Listed;
				L.push_back(P);
				Commit(L);
				bAll = false;
				Row = static_cast<int32>(L.size()) - 1;
				HeldId = P->id;
				ATagAudio::Ui(TEXT("UiPick"));
				Say(FString::Printf(TEXT("%s added at %s: carry him up"), *Short(*P), ANSI_TO_TCHAR(In.abbr)), Good);
			}
			else
			{
				ATagAudio::Ui(TEXT("UiPick"));
				HeldId = P->id;
				Say(FString::Printf(TEXT("Moving %s: up / down, cross to put him down"), *Short(*P)), Gold);
			}
			UE_LOG(LogTemp, Display, TEXT("TAG depth chart: %s %s -> %s"), *Str(T.abbr), ANSI_TO_TCHAR(In.abbr), *Str([&] {
				std::string Order;
				for (const tag::Player* Q : tag::chartOrder(T, Sl)) Order += (Order.empty() ? "" : ",") + std::to_string(Q->number);
				return Order;
			}()));
			Rebuild();
			return true;
		}
		if (N.bBack || N.bStart)
		{
			ATagAudio::Ui(TEXT("UiBack"));
			if (HeldId >= 0) { HeldId = -1; Rebuild(); return true; }
			if (bAll) { bAll = false; Rebuild(); return true; }
			if (Col == 1 && N.bBack) { Col = 0; Rebuild(); return true; }
			if (Back) { const std::function<void()> B = Back; Back = nullptr; B(); }
			else ShowMainMenu();
			return true;
		}
		if (HeldId >= 0) return true;
		if (N.bSquare)
		{
			ATagAudio::Ui(TEXT("UiTab"));
			bAll = !bAll;
			Col = 1;
			if (bAll) Row = static_cast<int32>(R.Listed.size());
			else Row = FMath::Min(Row, static_cast<int32>(R.Listed.size()) - 1);
			Rebuild();
			return true;
		}
		if (N.bTriangle)
		{
			ATagAudio::Ui(TEXT("UiTab"));
			std::vector<tag::Player*> L = R.Listed;
			std::stable_sort(L.begin(), L.end(), [Sl](const tag::Player* A, const tag::Player* B)
			{
				if ((A->injuredWeeks > 0) != (B->injuredWeeks > 0)) return B->injuredWeeks > 0;
				return tag::slotOvr(*A, Sl) > tag::slotOvr(*B, Sl);
			});
			Commit(L);
			Say(FString::Printf(TEXT("%s sorted by the rating at %s"), ANSI_TO_TCHAR(In.abbr), ANSI_TO_TCHAR(In.abbr)), Gold);
			Rebuild();
			return true;
		}
		if (N.bL3)
		{
			ATagAudio::Ui(TEXT("UiDelete"));
			tag::clearChart(T, Sl);
			SaveChart(T);
			Say(FString::Printf(TEXT("%s is the coach's again"), ANSI_TO_TCHAR(In.abbr)), Dim);
			Rebuild();
			return true;
		}
		if (N.bR3)
		{
			ATagAudio::Ui(TEXT("UiDelete"));
			tag::clearAllCharts(T);
			SaveChart(T);
			Say(FString::Printf(TEXT("%s: the whole depth chart is the coach's again"), *Str(T.abbr)), Dim);
			Rebuild();
			return true;
		}
		return false;
	};

	TSharedRef<SWidget> Root = SNew(SKeyRoot).OnKey(OnKey)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.01f, 0.012f, 0.02f, 0.92f)) ]
		+ SOverlay::Slot().VAlign(VAlign_Top)[ SNew(SBox).HeightOverride(6)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Pri) ] ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(SBox).Padding(FMargin(16, 12))[ Page ] ]
	];
	CallRoot = Root;  // (the screen draws its own selection: no focus frame)
	ShowModal(Root, true);
}
