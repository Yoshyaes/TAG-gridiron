// Superstars & X-Factors: the loadout screen, the saved loadouts, and the zone in the game (badges over the X-Factor
// men before the snap, the banner when one gets in the zone, the toast when he is knocked out). See Sim/Abilities.h for
// what the abilities are and do.
//
// The screen (from the team select: R2 / F on a team): the team's graded players down the left (X-Factors, Superstars,
// Stars, best first); on the right the man's card (his grade, his slots, his AP) and every ability his position can
// carry - the X-Factors first, then the Superstar abilities - each one equipped, open, or locked with the reason
// ("Needs DAC 76", "Needs Superstar grade"). Up / down moves in the column, left / right changes column, L1 / R1 the
// man; cross equips or removes, square gives him the coach's picks, triangle the whole team; circle goes back. Every
// change is saved at once (GameUserSettings.ini [TAG.Abilities]: one line a team, by jersey number), and loaded over
// the coach's picks when the game starts.
#include "UI/GridironOverlay.h"

#include "Game/MatchDirector.h"
#include "Audio/TagAudio.h"
#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "Sim/Abilities.h"
#include "Sim/League.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include <algorithm>

namespace TagAbilityArt
{
	/** The ability's icon (Tools/Art/gen_ability_icons.py: Content/Slate/AbilityIcons/<key>.png), or null when it is
	 *  missing (the two-letter badge stands in). */
	const FSlateBrush* Icon(const tag::AbilityDef& A)
	{
		static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> Cache;
		const FString Key = UTF8_TO_TCHAR(A.key);
		if (const TSharedPtr<FSlateDynamicImageBrush>* Hit = Cache.Find(Key)) return Hit->Get();
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Slate/AbilityIcons") / (Key + TEXT(".png")));
		// (a dynamic brush: Slate loads it from the file the first time it is drawn)
		TSharedPtr<FSlateDynamicImageBrush>& B = Cache.Add(Key);
		if (!Key.IsEmpty() && IFileManager::Get().FileExists(*Path)) B = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(128, 128));
		return B.Get();
	}
}

// (a named namespace: in a unity build this file shares a translation unit with the other UI files)
namespace AbilUI
{
	const FLinearColor Ink(0.012f, 0.016f, 0.026f, 1.f);
	const FLinearColor Card(0.035f, 0.045f, 0.068f, 0.95f);
	const FLinearColor CardHi(0.06f, 0.075f, 0.11f, 0.97f);
	const FLinearColor Gold(1.f, 0.83f, 0.f);
	const FLinearColor Dim(0.58f, 0.63f, 0.71f);
	const FLinearColor Faint(0.36f, 0.4f, 0.47f);
	const FLinearColor White90(0.93f, 0.95f, 0.98f);
	const FLinearColor Lock(0.94f, 0.44f, 0.24f);
	const FLinearColor Open(0.18f, 0.83f, 0.49f);
	const TCHAR* Section = TEXT("TAG.Abilities");

	FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	const FSlateBrush* WhiteBrush() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText Tx(const FString& S) { return FText::FromString(S); }
	FString Str(const std::string& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	FLinearColor Hex(const TCHAR* H) { return FLinearColor(FColor::FromHex(H)); }

	/** Each grade's colour: the X-Factor's flame, the Superstar's violet, the Star's blue. */
	FLinearColor GradeColor(tag::StarGrade G)
	{
		switch (G)
		{
		case tag::StarGrade::XFactor: return Hex(TEXT("FF6A1A"));
		case tag::StarGrade::Superstar: return Hex(TEXT("B58CFF"));
		case tag::StarGrade::Star: return Hex(TEXT("72B8FF"));
		default: return Faint;
		}
	}
	FString GradeWord(tag::StarGrade G) { return FString(UTF8_TO_TCHAR(tag::starGradeName(G))).ToUpper(); }
	FString Name(const tag::Player& P)
	{
		return FString::Printf(TEXT("%s. %s"), *Str(P.first.substr(0, 1)), *Str(P.last));
	}
	FString TagOf(const tag::Player& P)
	{
		return FString::Printf(TEXT("#%d %s %s"), P.number, ANSI_TO_TCHAR(tag::posName(P.pos)), *Name(P));
	}

	TSharedRef<SWidget> Label(const FString& S, int32 Size = 10, const FLinearColor& C = Dim, const char* W = "Bold")
	{
		return SNew(STextBlock).Font(Font(Size, W)).ColorAndOpacity(C).Text(Tx(S));
	}
	TSharedRef<SWidget> Fill(const FLinearColor& C, float H = 1.f)
	{
		return SNew(SBox).HeightOverride(H)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(C) ];
	}
	TSharedRef<SWidget> Heading(const FString& S)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Label(S.ToUpper(), 9, Faint) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 6)[ Fill(FLinearColor(1, 1, 1, 0.08f)) ];
	}
	TSharedRef<SWidget> Click(TSharedRef<SWidget> Content, std::function<void()> OnClick)
	{
		return SNew(SButton).IsFocusable(false).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
			.OnClicked_Lambda([OnClick] { OnClick(); return FReply::Handled(); })[ Content ];
	}
	/** An ability's badge: its icon (dimmed when it is not equipped, greyed out when he cannot carry it); its two
	 *  letters on the grade colour when the icon is missing. */
	TSharedRef<SWidget> Badge(const tag::AbilityDef& A, bool bOn, float Size = 22.f, bool bLocked = true)
	{
		if (const FSlateBrush* Ic = TagAbilityArt::Icon(A))
			return SNew(SBox).WidthOverride(Size).HeightOverride(Size).ToolTipText(Tx(Str(A.name)))
				[ SNew(SImage).Image(Ic).ColorAndOpacity(bOn ? FLinearColor::White : bLocked ? FLinearColor(0.4f, 0.42f, 0.48f, 0.7f) : FLinearColor(0.78f, 0.8f, 0.85f, 0.95f)) ];
		Size *= 0.7f;
		const FLinearColor C = A.tier == tag::AbilityTier::XFactor ? GradeColor(tag::StarGrade::XFactor) : Gold;
		return SNew(SBox).WidthOverride(Size + 6).HeightOverride(Size)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bOn ? C : C * FLinearColor(1, 1, 1, 0.18f)).Padding(0)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[ Label(UTF8_TO_TCHAR(A.icon), FMath::RoundToInt(Size * 0.42f), bOn ? Ink : C) ]
			];
	}

	/** The screen's root: the one focusable widget (every key comes to the screen). */
	class SRoot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRoot) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(std::function<bool(const FKey&)>, OnKey)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			OnKey = A._OnKey;
			ChildSlot[A._Content.Widget];
		}
		virtual bool SupportsKeyboardFocus() const override { return true; }
		virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override { return OnKey && OnKey(E.GetKey()) ? FReply::Handled() : FReply::Unhandled(); }
		virtual FNavigationReply OnNavigation(const FGeometry&, const FNavigationEvent& E) override
		{
			if (OnKey)
				switch (E.GetNavigationType())
				{
				case EUINavigation::Left: OnKey(EKeys::Gamepad_DPad_Left); break;
				case EUINavigation::Right: OnKey(EKeys::Gamepad_DPad_Right); break;
				case EUINavigation::Up: OnKey(EKeys::Gamepad_DPad_Up); break;
				case EUINavigation::Down: OnKey(EKeys::Gamepad_DPad_Down); break;
				default: break;
				}
			return FNavigationReply::Stop();
		}
	private:
		std::function<bool(const FKey&)> OnKey;
	};

	/** A pad button / key, drawn by the overlay's painter. */
	class SGlyph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGlyph) : _Owner(nullptr), _Glyph(EPadGlyph::None), _Size(22.f) {}
			SLATE_ARGUMENT(const SGridironOverlay*, Owner)
			SLATE_ARGUMENT(EPadGlyph, Glyph)
			SLATE_ARGUMENT(FString, Key)
			SLATE_ARGUMENT(float, Size)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Owner = A._Owner;
			Glyph = A._Glyph;
			Key = A._Key;
			Size = A._Size;
		}
		virtual FVector2D ComputeDesiredSize(float) const override
		{
			const bool bPad = Owner && Owner->PadGlyphs();
			if (bPad || Key.Len() <= 1 || !FSlateApplication::IsInitialized()) return FVector2D(Size + 4, Size + 4);
			const FVector2D M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Key, Font(FMath::RoundToInt(Size * 0.42f), "Bold"));
			return FVector2D(FMath::Max<double>(Size, M.X + 12) + 4, Size + 4);
		}
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			if (Owner) Owner->PaintGlyph(Out, Layer, G, FVector2D(G.GetLocalSize()) * 0.5, Size, Glyph, Key, Owner->PadGlyphs(), FLinearColor::White);
			return Layer + 4;
		}
	private:
		const SGridironOverlay* Owner = nullptr;
		EPadGlyph Glyph = EPadGlyph::None;
		FString Key;
		float Size = 22.f;
	};

	/** The team's graded men, X-Factors first, best first. */
	std::vector<tag::Player*> Graded(tag::Team& T)
	{
		std::vector<tag::Player*> V;
		for (auto& P : T.roster)
			if (P->grade != tag::StarGrade::None) V.push_back(P.get());
		std::stable_sort(V.begin(), V.end(), [](const tag::Player* A, const tag::Player* B)
		{
			return A->grade != B->grade ? static_cast<int>(A->grade) > static_cast<int>(B->grade) : A->ovr > B->ovr;
		});
		return V;
	}
	/** What a man could carry, in the order the screen lists it: X-Factors, then the Superstar abilities. */
	std::vector<const tag::AbilityDef*> Options(const tag::Player& P)
	{
		std::vector<const tag::AbilityDef*> O = tag::abilitiesFor(P.pos, tag::AbilityTier::XFactor);
		for (const tag::AbilityDef* A : tag::abilitiesFor(P.pos, tag::AbilityTier::Superstar)) O.push_back(A);
		return O;
	}

	// ---- saving
	FString TeamLine(const tag::Team& T)
	{
		FString S;
		for (const auto& P : T.roster)
		{
			if (P->grade == tag::StarGrade::None) continue;
			if (!S.IsEmpty()) S += TEXT("|");
			S += FString::Printf(TEXT("%d=%s"), P->number, *Str(tag::loadoutString(*P)));
		}
		return S;
	}
	/** The scripted screen checks never touch the user's saved loadouts. */
	bool NoSave() { static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagAbilityShots")); return b; }
	void Save(const tag::Team& T)
	{
		if (!GConfig || NoSave()) return;
		GConfig->SetString(Section, *Str(T.abbr), *TeamLine(T), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	void Forget(const tag::Team& T)
	{
		if (!GConfig || NoSave()) return;
		GConfig->RemoveKey(Section, *Str(T.abbr), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	// ---- in the game: the events waiting for the banner, the badges over the X-Factor men
	struct FZoneMsg
	{
		bool bIn = true;
		FString Title, Sub;
	};
	TArray<FZoneMsg> Queue;
	struct FBadge
	{
		FVector2D Screen;
		FString Icon;
		const FSlateBrush* Brush = nullptr;
		bool bActive = false;
		int32 Progress = 0, Need = 0;
	};
	TArray<FBadge> Badges;
	FVector2D BadgeVp(1920, 1080);
	// the screen's status line (the overlay's toast is hidden under a menu)
	FString Msg;
	FLinearColor MsgColor = Dim;
	void Say(const FString& S, const FLinearColor& C) { Msg = S; MsgColor = C; }
}

using namespace AbilUI;

// ------------------------------------------------------------------ the saved loadouts
void SGridironOverlay::LoadAbilityLoadouts(AMatchDirector* D)
{
	if (!D || !GConfig) return;
	int32 Teams = 0, Men = 0;
	for (auto& T : D->League)
	{
		FString Line;
		if (!GConfig->GetString(Section, *Str(T->abbr), Line, GGameUserSettingsIni) || Line.IsEmpty()) continue;
		TArray<FString> Parts;
		Line.ParseIntoArray(Parts, TEXT("|"));
		for (const FString& Part : Parts)
		{
			FString Num, Load;
			if (!Part.Split(TEXT("="), &Num, &Load)) continue;
			const int32 N = FCString::Atoi(*Num);
			for (auto& P : T->roster)
				if (P->number == N && P->grade != tag::StarGrade::None)
				{
					tag::applyLoadoutString(*P, Load == TEXT("none") ? std::string() : std::string(TCHAR_TO_UTF8(*Load)));
					++Men;
				}
		}
		++Teams;
	}
	UE_LOG(LogTemp, Display, TEXT("TAG abilities: %d saved team loadouts (%d players) over the coach's picks"), Teams, Men);
}

// ------------------------------------------------------------------ the screen
void SGridironOverlay::ShowAbilities(int32 TeamIdx, std::function<void()> Back)
{
	if (!Director || Director->League.empty()) return;
	const int32 NTeams = static_cast<int32>(Director->League.size());
	TeamIdx = FMath::Clamp(TeamIdx, 0, NTeams - 1);
	if (TeamIdx != AbTeam || !AbBack)
	{
		AbRow = AbPick = AbCol = 0;
		Msg.Reset();
	}
	AbTeam = TeamIdx;
	if (Back) AbBack = Back;
	tag::Team& Tm = *Director->League[AbTeam];
	const std::vector<tag::Player*> Men = Graded(Tm);
	if (Men.empty()) return;
	AbRow = FMath::Clamp(AbRow, 0, static_cast<int32>(Men.size()) - 1);
	tag::Player& P = *Men[AbRow];
	const std::vector<const tag::AbilityDef*> Opts = Options(P);
	AbPick = FMath::Clamp(AbPick, 0, FMath::Max(0, static_cast<int32>(Opts.size()) - 1));
	const FLinearColor Pri = TagCoords::Hex(Tm.colors.primary);
	auto Rebuild = [this] { ShowAbilities(AbTeam, nullptr); };

	// ---- the left column: the graded men
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	const int32 Window = 17;
	const int32 First = FMath::Clamp(AbRow - Window / 2, 0, FMath::Max(0, static_cast<int32>(Men.size()) - Window));
	for (int32 i = First; i < static_cast<int32>(Men.size()) && i < First + Window; ++i)
	{
		const tag::Player& M = *Men[i];
		const bool bSel = i == AbRow;
		const FLinearColor Gc = GradeColor(M.grade);
		TSharedRef<SHorizontalBox> Carried = SNew(SHorizontalBox);
		if (M.xFactor != tag::AbilityId::None) Carried->AddSlot().AutoWidth().Padding(0, 0, 3, 0)[ Badge(tag::abilityDef(M.xFactor), true, 24.f) ];
		for (tag::AbilityId Id : M.abilities) Carried->AddSlot().AutoWidth().Padding(0, 0, 3, 0)[ Badge(tag::abilityDef(Id), true, 24.f) ];
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
		[
			Click(SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bSel ? (AbCol == 0 ? Gold : Gold * FLinearColor(1, 1, 1, 0.45f)) : FLinearColor::Transparent).Padding(bSel ? 2.f : 0.f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bSel ? CardHi : Card).Padding(FMargin(0))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(4)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Gc) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 5, 0, 5)
					[ SNew(SBox).WidthOverride(72)[ Label(GradeWord(M.grade), 8, Gc) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(64)[ Label(FString::Printf(TEXT("%s #%d"), ANSI_TO_TCHAR(tag::posName(M.pos)), M.number), 10, Dim) ] ]
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(12, "Bold")).ColorAndOpacity(White90).Text(Tx(Name(M).ToUpper())).Clipping(EWidgetClipping::ClipToBounds) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)[ Carried ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 10, 0)[ SNew(SBox).WidthOverride(26).HAlign(HAlign_Right)[ Label(FString::FromInt(M.ovr), 14, White90) ] ]
				]
			], [this, i, Rebuild] { AbRow = i; AbCol = 0; AbPick = 0; Rebuild(); })
		];
	}
	if (static_cast<int32>(Men.size()) > Window)
		List->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[ Label(FString::Printf(TEXT("%d - %d of %d"), First + 1, FMath::Min(First + Window, static_cast<int32>(Men.size())), static_cast<int32>(Men.size())), 9, Faint) ];

	// ---- the man's card: grade, slots, AP
	const int32 Slots = tag::gradeSlots(P.grade), Used = static_cast<int32>(P.abilities.size());
	const int32 AP = tag::gradeAP(P.grade), APUsed = tag::abilityAPUsed(P);
	const FLinearColor Gc = GradeColor(P.grade);
	TSharedRef<SHorizontalBox> SlotRow = SNew(SHorizontalBox);
	for (int32 s = 0; s < 3; ++s)
	{
		const bool bHas = s < Used;
		const bool bOpen = s < Slots;
		TSharedRef<SWidget> In = bHas ? Badge(tag::abilityDef(P.abilities[s]), true, 40.f)
			: SNew(SBox).WidthOverride(40).HeightOverride(40)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(1, 1, 1, bOpen ? 0.1f : 0.03f))
				.HAlign(HAlign_Center).VAlign(VAlign_Center)[ Label(bOpen ? TEXT("+") : TEXT("-"), 12, bOpen ? Dim : Faint) ] ];
		SlotRow->AddSlot().AutoWidth().Padding(0, 0, 6, 0)[ In ];
	}
	TSharedRef<SWidget> XfSlot = P.grade == tag::StarGrade::XFactor
		? (P.xFactor != tag::AbilityId::None ? Badge(tag::abilityDef(P.xFactor), true, 40.f)
			: SNew(SBox).WidthOverride(40).HeightOverride(40)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Gc * FLinearColor(1, 1, 1, 0.2f)).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Label(TEXT("+"), 12, Gc) ] ])
		: StaticCastSharedRef<SWidget>(Label(TEXT("X-Factor grade only"), 9, Faint, "Regular"));
	TSharedRef<SHorizontalBox> APBar = SNew(SHorizontalBox);
	for (int32 a = 0; a < 7; ++a)
		APBar->AddSlot().AutoWidth().Padding(0, 0, 3, 0)
		[
			SNew(SBox).WidthOverride(18).HeightOverride(8)
			[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(a < APUsed ? Gold : a < AP ? FLinearColor(1, 1, 1, 0.16f) : FLinearColor(1, 1, 1, 0.03f)) ]
		];
	TSharedRef<SWidget> CardW = SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(CardHi).Padding(0)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[ Fill(Gc, 3.f) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 10, 16, 12))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ Label(FString::Printf(TEXT("%s  #%d  ·  %s"), ANSI_TO_TCHAR(tag::posName(P.pos)), P.number, *Str(Tm.abbr)), 10, Dim) ]
				+ SVerticalBox::Slot().AutoHeight()[ Label(Str(P.first), 12, Dim, "Regular") ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, -3, 0, 0)[ Label(Str(P.last).ToUpper(), 24, White90) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Gc).Padding(FMargin(8, 2))[ Label(GradeWord(P.grade), 10, Ink) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
					[ Label(P.grade == tag::StarGrade::XFactor ? TEXT("Top 3% at his position") : P.grade == tag::StarGrade::Superstar ? TEXT("Top 10% at his position") : TEXT("Top 22% at his position"), 9, Faint, "Regular") ]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(16, 0, 18, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Label(FString::FromInt(P.ovr), 34, White90) ]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, -6, 0, 0)[ Label(TEXT("OVR"), 9, Faint) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ Label(FString::Printf(TEXT("SUPERSTAR SLOTS  %d / %d"), Used, Slots), 9, Faint) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[ SlotRow ]
				+ SVerticalBox::Slot().AutoHeight()[ Label(FString::Printf(TEXT("ABILITY POINTS  %d / %d"), APUsed, AP), 9, Faint) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[ APBar ]
				+ SVerticalBox::Slot().AutoHeight()[ Label(TEXT("X-FACTOR"), 9, Faint) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ XfSlot ]
			]
		]
	];

	// ---- the abilities he could carry
	TSharedRef<SVerticalBox> Abil = SNew(SVerticalBox);
	const int32 AWin = 7;
	const int32 AFirst = FMath::Clamp(AbPick - AWin / 2, 0, FMath::Max(0, static_cast<int32>(Opts.size()) - AWin));
	bool bXfHead = false, bSsHead = false;
	for (int32 i = AFirst; i < static_cast<int32>(Opts.size()) && i < AFirst + AWin; ++i)
	{
		const tag::AbilityDef& A = *Opts[i];
		const bool bXf = A.tier == tag::AbilityTier::XFactor;
		if (bXf && !bXfHead) { bXfHead = true; Abil->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[ Heading(TEXT("X-Factor  ·  earned in the game, live until defeated")) ]; }
		if (!bXf && !bSsHead) { bSsHead = true; Abil->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[ Heading(TEXT("Superstar abilities  ·  always on")) ]; }
		const bool bOn = tag::hasAbility(P, A.id);
		const std::string Why = tag::abilityLock(P, A);
		const bool bLocked = !Why.empty();
		const bool bSel = AbCol == 1 && i == AbPick;
		const FLinearColor C = bXf ? GradeColor(tag::StarGrade::XFactor) : Gold;
		const FString Status = bOn ? TEXT("EQUIPPED") : bLocked ? Str(Why).ToUpper() : bXf ? TEXT("AVAILABLE") : FString::Printf(TEXT("%d AP"), A.cost);
		TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(Str(A.name).ToUpper(), 13, bLocked ? Dim : White90) ]
				+ SHorizontalBox::Slot().FillWidth(1)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bOn ? C : bLocked ? Lock * FLinearColor(1, 1, 1, 0.16f) : FLinearColor(1, 1, 1, 0.07f)).Padding(FMargin(7, 1))
					[ Label(Status, 8, bOn ? Ink : bLocked ? Lock : Open) ] ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(bLocked ? Faint : Dim).AutoWrapText(true).Text(Tx(Str(A.blurb))) ];
		if (bXf)
		{
			Body->AddSlot().AutoHeight().Padding(0, 3, 0, 0)[ Label(FString(TEXT("IN THE ZONE:  ")) + Str(A.trigger), 9, C * FLinearColor(1, 1, 1, bLocked ? 0.5f : 1.f), "Regular") ];
			Body->AddSlot().AutoHeight()[ Label(FString(TEXT("KNOCKED OUT:  ")) + Str(A.defeat), 9, Faint, "Regular") ];
		}
		FString Req;
		for (const tag::AbilityReq& R : A.req) Req += (Req.IsEmpty() ? TEXT("") : TEXT("   ")) + FString::Printf(TEXT("%s %d"), UTF8_TO_TCHAR(R.abbr), R.min);
		Body->AddSlot().AutoHeight().Padding(0, 2, 0, 0)
		[ Label(FString::Printf(TEXT("NEEDS  %s   ·   %s+"), *Req, *GradeWord(A.minGrade)), 8, Faint) ];
		Abil->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			Click(SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bSel ? Gold : FLinearColor::Transparent).Padding(bSel ? 2.f : 0.f)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bSel ? CardHi : Card).Padding(FMargin(0))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 6, 10, 6)[ Badge(A, bOn, 44.f, bLocked) ]
					+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 6, 12, 6)[ Body ]
				]
			], [this, i, Rebuild] { AbCol = 1; AbPick = i; Rebuild(); })
		];
	}
	if (static_cast<int32>(Opts.size()) > AWin)
		Abil->AddSlot().AutoHeight()[ Label(FString::Printf(TEXT("%d - %d of %d  ·  up / down for more"), AFirst + 1, FMath::Min(AFirst + AWin, static_cast<int32>(Opts.size())), static_cast<int32>(Opts.size())), 9, Faint) ];

	// ---- the team's totals
	int32 NXf = 0, NSs = 0, NSt = 0, NZ = 0;
	for (const tag::Player* M : Men)
	{
		NXf += M->grade == tag::StarGrade::XFactor;
		NSs += M->grade == tag::StarGrade::Superstar;
		NSt += M->grade == tag::StarGrade::Star;
		NZ += M->xFactor != tag::AbilityId::None;
	}

	auto Hint = [this](EPadGlyph G, const TCHAR* Key, const TCHAR* What) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(AbilUI::SGlyph).Owner(this).Glyph(G).Key(Key).Size(22.f) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 18, 0)[ Label(What, 11, White90, "Regular") ];
	};
	TSharedRef<SWidget> Hints = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DUp, TEXT("↑↓"), TEXT("Move")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DRight, TEXT("←→"), TEXT("Players / Abilities")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L1, TEXT("Q"), TEXT("")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R1, TEXT("E"), TEXT("Player")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L2, TEXT("Z"), TEXT("")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R2, TEXT("C"), TEXT("Team")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Cross, TEXT("Enter"), TEXT("Equip / remove")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Square, TEXT("R"), TEXT("Coach's picks")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Triangle, TEXT("T"), TEXT("Whole team")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Circle, TEXT("Esc"), TEXT("Done")) ];

	TSharedRef<SWidget> Page = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 0, 0, 10))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[ Label(TEXT("SUPERSTARS & X-FACTORS"), 30, FLinearColor::White) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(14, 0, 0, 5)
			[ Label(FString::Printf(TEXT("%s %s"), *Str(Tm.city).ToUpper(), *Str(Tm.name).ToUpper()), 13, Pri.GetLuminance() < 0.12f ? Dim : Pri) ]
			+ SHorizontalBox::Slot().FillWidth(1)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 0, 5)
			[ Label(FString::Printf(TEXT("%d X-FACTORS (%d EQUIPPED)  ·  %d SUPERSTARS  ·  %d STARS"), NXf, NZ, NSs, NSt), 10, Dim) ]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(560)
				[
					SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(AbCol == 0 ? Gold * FLinearColor(1, 1, 1, 0.5f) : FLinearColor(1, 1, 1, 0.07f)).Padding(1)
					[
						SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Card).Padding(FMargin(12, 10))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[ Heading(TEXT("Graded players  ·  by their standing at their position league-wide")) ]
							+ SVerticalBox::Slot().AutoHeight()[ List ]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
							[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Faint).AutoWrapText(true)
								.Text(Tx(TEXT("STAR: 1 slot, 3 AP.  SUPERSTAR: 2 slots, 5 AP.  X-FACTOR: 3 slots, 7 AP and an X-Factor. Superstar abilities are always on (some in their moment); an X-Factor is earned in the game and stays live until its defeat."))) ]
						]
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(14, 0, 0, 0)
			[
				SNew(SBox).WidthOverride(760)
				[
					SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(AbCol == 1 ? Gold * FLinearColor(1, 1, 1, 0.5f) : FLinearColor(1, 1, 1, 0.07f)).Padding(1)
					[
						SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Card).Padding(FMargin(12, 10))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[ CardW ]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[ Abil ]
						]
					]
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, 10, 0, 0))
		[ SNew(SBox).HeightOverride(18)[ Label(Msg.IsEmpty() ? FString(TEXT("Cross equips or removes the ability under the cursor; locked ones say what they need.")) : Msg, 11, Msg.IsEmpty() ? Faint : MsgColor) ] ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, 6, 0, 0))[ Hints ];

	// ---- the pad and the keys
	const int32 NMen = static_cast<int32>(Men.size());
	const int32 NOpts = static_cast<int32>(Opts.size());
	auto OnKey = [this, Rebuild, NMen, NOpts](const FKey& K) -> bool
	{
		if (!Director || Director->League.empty()) return false;
		tag::Team& T = *Director->League[AbTeam];
		const std::vector<tag::Player*> Ms = Graded(T);
		if (Ms.empty()) return false;
		tag::Player& Me = *Ms[FMath::Clamp(AbRow, 0, NMen - 1)];
		const bool bLeft = K == EKeys::Gamepad_DPad_Left || K == EKeys::Gamepad_LeftStick_Left || K == EKeys::Left || K == EKeys::A;
		const bool bRight = K == EKeys::Gamepad_DPad_Right || K == EKeys::Gamepad_LeftStick_Right || K == EKeys::Right || K == EKeys::D;
		const bool bUp = K == EKeys::Gamepad_DPad_Up || K == EKeys::Gamepad_LeftStick_Up || K == EKeys::Up || K == EKeys::W;
		const bool bDown = K == EKeys::Gamepad_DPad_Down || K == EKeys::Gamepad_LeftStick_Down || K == EKeys::Down || K == EKeys::S;
		const bool bPrev = K == EKeys::Gamepad_LeftShoulder || K == EKeys::Q;
		const bool bNext = K == EKeys::Gamepad_RightShoulder || K == EKeys::E;
		if (bLeft || bRight || bUp || bDown || bPrev || bNext) ATagAudio::Ui(TEXT("UiMove"));
		if (bLeft || bRight) { AbCol = bRight ? 1 : 0; Rebuild(); return true; }
		if (K == EKeys::Gamepad_LeftTrigger || K == EKeys::Gamepad_RightTrigger || K == EKeys::Z || K == EKeys::C)
		{
			// another team (L2 / R2)
			ATagAudio::Ui(TEXT("UiTab"));
			const int32 N = static_cast<int32>(Director->League.size());
			const int32 D = (K == EKeys::Gamepad_RightTrigger || K == EKeys::C) ? 1 : -1;
			AbTeam = (AbTeam + D + N) % N;
			AbRow = AbPick = AbCol = 0;
			Rebuild();
			return true;
		}
		if (bPrev || bNext) { AbRow = (AbRow + (bNext ? 1 : -1) + NMen) % NMen; AbPick = 0; Rebuild(); return true; }
		if (bUp || bDown)
		{
			if (AbCol == 0) { AbRow = FMath::Clamp(AbRow + (bDown ? 1 : -1), 0, NMen - 1); AbPick = 0; }
			else AbPick = FMath::Clamp(AbPick + (bDown ? 1 : -1), 0, FMath::Max(0, NOpts - 1));
			Rebuild();
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Bottom || K == EKeys::Enter || K == EKeys::SpaceBar)
		{
			if (AbCol == 0) { ATagAudio::Ui(TEXT("UiTab")); AbCol = 1; AbPick = 0; Rebuild(); return true; }
			const std::vector<const tag::AbilityDef*> O = Options(Me);
			if (AbPick < 0 || AbPick >= static_cast<int32>(O.size())) return true;
			const tag::AbilityDef& A = *O[AbPick];
			std::string Why;
			if (tag::hasAbility(Me, A.id))
			{
				tag::unequipAbility(Me, A.id);
				ATagAudio::Ui(TEXT("UiBack"));
				Say(FString::Printf(TEXT("%s: %s removed"), *Name(Me), *Str(A.name)), Dim);
			}
			else if (tag::equipAbility(Me, A.id, &Why))
			{
				ATagAudio::Ui(TEXT("UiSelect"));
				Say(FString::Printf(TEXT("%s: %s equipped"), *Name(Me), *Str(A.name)), Open);
			}
			else
			{
				ATagAudio::Ui(TEXT("UiBack"));
				Say(FString::Printf(TEXT("%s: %s"), *Str(A.name), *Str(Why)), Lock);
			}
			Save(T);
			UE_LOG(LogTemp, Display, TEXT("TAG abilities: %s %s -> %s"), *Str(T.abbr), *TagOf(Me), *Str(tag::loadoutString(Me)));
			Rebuild();
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Left || K == EKeys::R)
		{
			ATagAudio::Ui(TEXT("UiTab"));
			tag::autoLoadout(Me);
			Save(T);
			Say(FString::Printf(TEXT("%s: the coach's picks"), *Name(Me)), Gold);
			Rebuild();
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Top || K == EKeys::T)
		{
			ATagAudio::Ui(TEXT("UiTab"));
			for (auto& Pl : T.roster) tag::autoLoadout(*Pl);
			Forget(T);
			Say(FString::Printf(TEXT("%s: every loadout back to the coach's picks"), *Str(T.abbr)), Gold);
			Rebuild();
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Right || K == EKeys::Escape || K == EKeys::BackSpace || K == EKeys::Gamepad_Special_Right)
		{
			ATagAudio::Ui(TEXT("UiBack"));
			if (AbBack) { const std::function<void()> B = AbBack; AbBack = nullptr; B(); }
			else ShowMainMenu();
			return true;
		}
		return false;
	};

	TSharedRef<SWidget> Root = SNew(AbilUI::SRoot).OnKey(OnKey)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0.01f, 0.012f, 0.02f, 0.9f)) ]
		+ SOverlay::Slot().VAlign(VAlign_Top)[ SNew(SBox).HeightOverride(6)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Pri) ] ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(SBox).Padding(FMargin(16, 12))[ Page ] ]
	];
	CallRoot = Root;  // (the screen draws its own selection: no focus frame)
	ShowModal(Root, true);

	// -TagAbilityShots: a scripted visit (a picture per step, with the UI), then the game exits
	static bool bShots = FParse::Param(FCommandLine::Get(), TEXT("TagAbilityShots"));
	if (bShots)
	{
		bShots = false;
		TWeakPtr<SGridironOverlay> Weak = StaticCastSharedRef<SGridironOverlay>(AsShared());
		struct FStep { TArray<FKey> Keys; const TCHAR* Name; };
		static TArray<FStep> Steps = {
			{{}, TEXT("open")},
			{{EKeys::Down, EKeys::Down}, TEXT("third_man")},
			{{EKeys::Right}, TEXT("abilities")},
			{{EKeys::Down, EKeys::Down, EKeys::Down}, TEXT("ability_4")},
			{{EKeys::Enter}, TEXT("toggled")},
			{{EKeys::Left, EKeys::Up, EKeys::Up}, TEXT("top_man")},
			{{EKeys::Right}, TEXT("xfactors")},
			{{EKeys::R}, TEXT("coach_picks")},
		};
		static int32 I = -2;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float) -> bool
		{
			const TSharedPtr<SGridironOverlay> O = Weak.Pin();
			if (!O.IsValid()) return false;
			if (++I < 0) return true;
			if (I >= Steps.Num())
			{
				UE_LOG(LogTemp, Display, TEXT("TAG ability shots done"));
				FPlatformMisc::RequestExit(false);
				return false;
			}
			// (the keys go through the screen as it is now: each rebuild makes a new handler)
			for (const FKey& K : Steps[I].Keys)
				if (const TSharedPtr<SWidget> R = O->CallRoot)
				{
					FKeyEvent E(K, FModifierKeysState(), 0, false, 0, 0);
					StaticCastSharedPtr<AbilUI::SRoot>(R)->OnKeyDown(FGeometry(), E);
				}
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_abilities_%02d_%s.png"), I, Steps[I].Name), true, false);
			UE_LOG(LogTemp, Display, TEXT("TAG ability shot %d %s"), I, Steps[I].Name);
			return true;
		}), 1.5f);
	}
}

// ------------------------------------------------------------------ in the game
void SGridironOverlay::PumpAbilities()
{
	if (!Director) return;
	tag::Game* G = Director->Game.get();
	// look-dev: -TagZoneAll starts every X-Factor man of a new game in the zone; -TagZoneShots films the badges and the
	// banners (Saved/Screenshots/.../tag_zone_*.png, with the UI)
	static const bool bZoneAll = FParse::Param(FCommandLine::Get(), TEXT("TagZoneAll"));
	static const bool bZoneShots = FParse::Param(FCommandLine::Get(), TEXT("TagZoneShots"));
	static const tag::Game* SeenGame = nullptr;
	static int32 BadgeShots = 0, BannerShots = 0;
	static double LastShot = 0.0, BannerAt = -1.0;
	if (G && G != SeenGame)
	{
		SeenGame = G;
		int32 Xf = 0;
		for (tag::Team* T : {G->home, G->away})
			for (auto& P : T->roster)
				if (P->xFactor != tag::AbilityId::None)
				{
					++Xf;
					if (bZoneAll) G->abil.zones[P->id].active = true;
					UE_LOG(LogTemp, Display, TEXT("TAG X-Factor: %s %s - %s"), *Str(T->abbr), *TagOf(*P), *Str(tag::abilityDef(P->xFactor).name));
				}
		UE_LOG(LogTemp, Display, TEXT("TAG abilities in this game: %d X-Factors%s"), Xf, bZoneAll ? TEXT(" (all in the zone: -TagZoneAll)") : TEXT(""));
	}
	const double Now = FPlatformTime::Seconds();
	if (bZoneShots && BannerAt > 0 && Now > BannerAt)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_zone_banner_%02d.png"), BannerShots++), true, false);
		BannerAt = -1.0;
	}
	// the zone events: queued, shown one at a time between the other banners
	if (G && !G->abil.events.empty())
	{
		for (const tag::AbilityEvent& E : G->abil.events)
		{
			if (!E.p) continue;
			const tag::AbilityDef& A = tag::abilityDef(E.id);
			FZoneMsg M;
			M.bIn = E.entered;
			if (E.entered)
			{
				M.Title = TEXT("IN THE ZONE");
				M.Sub = FString::Printf(TEXT("%s (%s)  ·  %s"), *TagOf(*E.p), *Str(E.team), *Str(A.name).ToUpper());
			}
			else M.Title = FString::Printf(TEXT("%s knocked out of the zone (%s)"), *TagOf(*E.p), *Str(E.why));
			UE_LOG(LogTemp, Display, TEXT("TAG zone %s: %s %s - %s (%s)"), E.entered ? TEXT("IN") : TEXT("OUT"), *Str(E.team), *TagOf(*E.p), *Str(A.name), *Str(E.why));
			Queue.Add(M);
		}
		G->abil.events.clear();
	}
	if (!G) Queue.Reset();
	if (Queue.Num() && !bModal && BannerT > 2.2f && ToastT > 1.6f)
	{
		const FZoneMsg M = Queue[0];
		Queue.RemoveAt(0);
		if (M.bIn) Banner(M.Title, M.Sub, GradeColor(tag::StarGrade::XFactor));
		else Toast(M.Title);
		if (bZoneShots && BannerShots < 8) BannerAt = Now + 0.6;
	}
	// the badges over the X-Factor men before the snap: the flame lit when he is in the zone, his pips toward it if not
	Badges.Reset();
	const tag::PlaySim* S = Director->Sim.get();
	if (!G || !S || Director->State != EMatchState::Presnap || !G->abilOn()) return;
	APlayerController* PC = Director->GetWorld() ? Director->GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(BadgeVp);
	for (const tag::Ent* E : S->ents)
	{
		if (E->p->xFactor == tag::AbilityId::None) continue;
		AAthlete* At = Director->AthleteFor(E->p, E->side == tag::Side::Off ? G->poss : tag::other(G->poss));
		if (!At || At->IsHidden()) continue;
		FVector2D Scr;
		if (!PC->ProjectWorldLocationToScreen(At->GetActorLocation() + FVector(0, 0, 232), Scr, true)) continue;
		const tag::AbilityDef& A = tag::abilityDef(E->p->xFactor);
		const tag::ZoneState* Z = G->abil.zone(*E->p);
		FBadge B;
		B.Screen = Scr;
		B.Icon = UTF8_TO_TCHAR(A.icon);
		B.Brush = TagAbilityArt::Icon(A);
		B.bActive = Z && Z->active;
		B.Progress = Z ? Z->progress : 0;
		B.Need = A.need;
		Badges.Add(B);
	}
	if (bZoneShots && Badges.Num() && BadgeShots < 6 && Now - LastShot > 6.0)
	{
		LastShot = Now;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_zone_badges_%02d.png"), BadgeShots++), true, false);
		UE_LOG(LogTemp, Display, TEXT("TAG zone badge shot %d: %d badges"), BadgeShots - 1, Badges.Num());
	}
}

int32 SGridironOverlay::PaintAbilities(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
{
	if (Badges.IsEmpty() || BadgeVp.X <= 0 || BadgeVp.Y <= 0) return Layer;
	const FVector2D Local = G.GetLocalSize();
	const FVector2D Scale(Local.X / BadgeVp.X, Local.Y / BadgeVp.Y);
	const FSlateFontInfo F = Font(12, "Bold");
	const FLinearColor Flame = GradeColor(tag::StarGrade::XFactor);
	const float Pulse = 0.7f + 0.3f * FMath::Sin(FPlatformTime::Seconds() * 6.0);
	for (const FBadge& B : Badges)
	{
		const FVector2D C = B.Screen * Scale;
		const FVector2D Sz = B.Brush ? FVector2D(B.bActive ? 44 : 36, B.bActive ? 44 : 36) : FVector2D(38, 24);
		const FVector2D TL = C - Sz * 0.5;
		if (B.Brush)
		{
			// his X-Factor's icon: a pulsing glow behind it when he is in the zone, greyed until he gets there
			if (B.bActive)
			{
				const FVector2D Gs = Sz * (1.12f + 0.1f * Pulse);
				FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Gs), FSlateLayoutTransform(FVector2f(C - Gs * 0.5))), B.Brush,
					ESlateDrawEffect::None, Flame * FLinearColor(1.4f, 1.4f, 1.4f, 0.45f * Pulse));
			}
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(TL))), B.Brush, ESlateDrawEffect::None,
				B.bActive ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.55f, 0.9f));
		}
		else
		{
			if (B.bActive)
			{
				// a glow behind a lit badge
				const FVector2D Gs = Sz + FVector2D(8, 8) * Pulse;
				FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Gs), FSlateLayoutTransform(FVector2f(C - Gs * 0.5))), WhiteBrush(),
					ESlateDrawEffect::None, Flame * FLinearColor(1, 1, 1, 0.35f * Pulse));
			}
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(TL))), WhiteBrush(), ESlateDrawEffect::None,
				B.bActive ? Flame : FLinearColor(0.02f, 0.025f, 0.035f, 0.85f));
			const FVector2D M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(B.Icon, F);
			FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(C - M * 0.5))), B.Icon, F,
				ESlateDrawEffect::None, B.bActive ? Ink : Flame);
		}
		if (!B.bActive && B.Need > 0)
		{
			// his pips toward the zone under the badge
			const float W = 9.f, Gap = 3.f;
			const float Total = B.Need * W + (B.Need - 1) * Gap;
			for (int32 k = 0; k < B.Need; ++k)
			{
				const FVector2D P(C.X - Total * 0.5 + k * (W + Gap), TL.Y + Sz.Y + 3);
				FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W, 4.f), FSlateLayoutTransform(FVector2f(P))), WhiteBrush(),
					ESlateDrawEffect::None, k < B.Progress ? Flame : FLinearColor(1, 1, 1, 0.25f));
			}
		}
	}
	return Layer + 4;
}
