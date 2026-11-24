// The team select screen (Play Now / Watch a Game), laid out the way Madden's is: the two teams side by side, each with
// its mark, its ratings (overall, offense, defense, special teams and every position group, ranked in the league), its
// star players, how it plays (its schemes and its tendencies) and its playbook (a signature play drawn as the play call
// draws it); between them the tale of the tape, the projection and the game options.
//
// The pad drives it without a focus frame (like the play call): the D-pad / stick moves between the away team, the
// options and the home team; L1 / R1 (Q / E) change the team (or the option) under it; square picks a random team,
// triangle swaps the sides, cross / OPTIONS kicks off, circle goes back. The mouse can click everything.
#include "UI/GridironOverlay.h"
#include "UI/TagTheme.h"

#include "Game/MatchDirector.h"
#include "Audio/TagAudio.h"
#include "Game/PlayArtStrokes.h"
#include "UI/PlayDiagram.h"
#include "Render/Coords.h"
#include "Sim/League.h"
#include "Sim/Playbook.h"
#include "Sim/PlaySim.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include <algorithm>
#include <cmath>

// the ability icons (UI/AbilityScreen.cpp)
namespace TagAbilityArt { const FSlateBrush* Icon(const tag::AbilityDef& A); }

// (a named namespace, not an anonymous one: in a unity build this file shares a translation unit with the overlay's)
namespace TeamSel
{
	const FLinearColor Ink(0.012f, 0.016f, 0.026f, 1.f);
	const FLinearColor Card(0.035f, 0.045f, 0.068f, 0.94f);
	const FLinearColor CardHi(0.06f, 0.075f, 0.11f, 0.97f);
	const FLinearColor Gold(1.f, 0.83f, 0.f);
	const FLinearColor Dim(0.58f, 0.63f, 0.71f);
	const FLinearColor Faint(0.36f, 0.4f, 0.47f);
	const FLinearColor White90(0.93f, 0.95f, 0.98f);

	FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	const FSlateBrush* WhiteBrush() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	FText Tx(const FString& S) { return FText::FromString(S); }
	FString Str(const std::string& s) { return FString(s.c_str()); }
	FLinearColor Col(const std::string& H) { return TagCoords::Hex(H); }
	FLinearColor Col(const TCHAR* H) { return FLinearColor(FColor::FromHex(H)); }
	FVector2D Measure(const FString& S, const FSlateFontInfo& F)
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F);
	}
	/** The rating's colour, Madden's traffic light: green for elite, lime, amber, red. */
	FLinearColor Tier(double V)
	{
		return V >= 88 ? Col(TEXT("2FD47E")) : V >= 80 ? Col(TEXT("A6D93B")) : V >= 72 ? Col(TEXT("F2C230")) : Col(TEXT("F0703C"));
	}
	/** A team colour that shows on the dark panels (Brooklyn's near-black lifted toward grey, the rest left alone). */
	FLinearColor Vivid(const FLinearColor& C)
	{
		const float L = C.GetLuminance();
		return L < 0.12f ? FMath::Lerp(C, FLinearColor(0.8f, 0.82f, 0.86f), FMath::Clamp(0.45f - L * 3.f, 0.f, 0.45f)) : C;
	}
	/** Text that reads on the team's colour: near black on a light primary, white on a dark one. */
	FLinearColor OnColor(const FLinearColor& Bg)
	{
		const FColor C = Bg.ToFColor(true);  // judged in display (sRGB) terms
		return (0.299f * C.R + 0.587f * C.G + 0.114f * C.B) / 255.f > 0.62f ? FLinearColor(0.02f, 0.02f, 0.03f) : FLinearColor::White;
	}
	FString Ordinal(int32 N)
	{
		const int32 M = N % 100;
		const TCHAR* Suf = (M >= 11 && M <= 13) ? TEXT("th") : N % 10 == 1 ? TEXT("st") : N % 10 == 2 ? TEXT("nd") : N % 10 == 3 ? TEXT("rd") : TEXT("th");
		return FString::Printf(TEXT("%d%s"), N, Suf);
	}

	/** The team's mark (Tools/Art/gen_ui_team_logos.py: Content/Slate/TeamLogos/<ABBR>.png), or null when it is missing. */
	const FSlateBrush* Logo(const tag::Team& T)
	{
		static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> Cache;
		const FString Abbr = Str(T.abbr);
		if (const TSharedPtr<FSlateDynamicImageBrush>* Hit = Cache.Find(Abbr)) return Hit->Get();
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Slate/TeamLogos") / (Abbr + TEXT(".png")));
		// (a dynamic brush: Slate loads it from the file the first time it is drawn; a plain image brush only finds
		// what a registered style set loaded at startup)
		TSharedPtr<FSlateDynamicImageBrush>& B = Cache.Add(Abbr);
		if (IFileManager::Get().FileExists(*Path)) B = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(256, 256));
		return B.Get();
	}

	// ------------------------------------------------------------------ the numbers
	/** A position group's rating: its starters' average overall (the depth chart's top n at each position). */
	struct FUnitDef
	{
		const TCHAR* Short;
		const TCHAR* Long;
		std::vector<std::pair<tag::Pos, int>> Picks;
	};
	const std::vector<FUnitDef>& Units()
	{
		using P = tag::Pos;
		static const std::vector<FUnitDef> U = {
			{TEXT("QB"), TEXT("Quarterback"), {{P::QB, 1}}},
			{TEXT("RB"), TEXT("Backfield"), {{P::RB, 1}}},
			{TEXT("WR"), TEXT("Receivers"), {{P::WR, 3}}},
			{TEXT("TE"), TEXT("Tight Ends"), {{P::TE, 1}}},
			{TEXT("OL"), TEXT("Offensive Line"), {{P::OT, 2}, {P::OG, 2}, {P::C, 1}}},
			{TEXT("DL"), TEXT("Defensive Line"), {{P::DE, 2}, {P::DT, 2}}},
			{TEXT("LB"), TEXT("Linebackers"), {{P::LB, 3}}},
			{TEXT("CB"), TEXT("Cornerbacks"), {{P::CB, 3}}},
			{TEXT("S"), TEXT("Safeties"), {{P::S, 2}}},
			{TEXT("K/P"), TEXT("Kicking Game"), {{P::K, 1}, {P::P, 1}}},
		};
		return U;
	}
	constexpr int32 NumUnits = 10;
	enum EUnit { UQB, URB, UWR, UTE, UOL, UDL, ULB, UCB, US, UKP };

	double UnitRating(const tag::Team& T, const FUnitDef& U)
	{
		double Sum = 0;
		int N = 0;
		for (const auto& [Pos, Count] : U.Picks)
		{
			const std::vector<tag::Player*> D = tag::depth(T, Pos);
			for (int i = 0; i < Count && i < static_cast<int>(D.size()); ++i)
			{
				Sum += D[i]->ovr;
				++N;
			}
		}
		return N ? Sum / N : 0.0;
	}

	/** Everything the screen shows about a team, and where it stands in the league. */
	struct FInfo
	{
		const tag::Team* Tm = nullptr;
		tag::TeamOvr O{0, 0, 0};
		double Unit[NumUnits] = {};
		int32 UnitRank[NumUnits] = {};
		int32 OvrRank = 0, OffRank = 0, DefRank = 0;
		// the tale of the tape's composites
		double Passing = 0, Rushing = 0, PassRush = 0, Coverage = 0;
	};

	TArray<FInfo> LeagueInfo(const tag::League& L)
	{
		TArray<FInfo> All;
		for (const auto& T : L)
		{
			FInfo I;
			I.Tm = T.get();
			I.O = tag::teamOvr(*T);
			for (int32 u = 0; u < NumUnits; ++u) I.Unit[u] = UnitRating(*T, Units()[u]);
			I.Passing = (2 * I.Unit[UQB] + I.Unit[UWR] + I.Unit[UTE]) / 4;
			I.Rushing = (I.Unit[URB] + I.Unit[UOL]) / 2;
			I.PassRush = 0.7 * I.Unit[UDL] + 0.3 * I.Unit[ULB];
			I.Coverage = (1.5 * I.Unit[UCB] + I.Unit[US]) / 2.5;
			All.Add(I);
		}
		for (FInfo& I : All)
		{
			I.OvrRank = I.OffRank = I.DefRank = 1;
			for (int32 u = 0; u < NumUnits; ++u) I.UnitRank[u] = 1;
			for (const FInfo& J : All)
			{
				// ties share the better rank (the overall is a whole number: compare it before rounding the units)
				I.OvrRank += J.O.ovr > I.O.ovr;
				I.OffRank += J.O.off > I.O.off;
				I.DefRank += J.O.def > I.O.def;
				for (int32 u = 0; u < NumUnits; ++u) I.UnitRank[u] += J.Unit[u] > I.Unit[u] + 1e-9;
			}
		}
		return All;
	}

	/** A player's two best ratings among the ones his position is judged by ("SPD 94", "CTH 91"). */
	TArray<TPair<FString, int32>> KeyRatings(const tag::Player& P)
	{
		using R = tag::Ratings;
		struct FA { const TCHAR* Abbr; double R::*M; };
		static const std::vector<FA> QB = {{TEXT("THP"), &R::throwPower}, {TEXT("SAC"), &R::accShort}, {TEXT("MAC"), &R::accMid}, {TEXT("DAC"), &R::accDeep}, {TEXT("AWR"), &R::awareness}, {TEXT("TOR"), &R::throwOnRun}};
		static const std::vector<FA> RB = {{TEXT("SPD"), &R::speed}, {TEXT("ACC"), &R::accel}, {TEXT("BTK"), &R::breakTackle}, {TEXT("ELU"), &R::elusiveness}, {TEXT("CAR"), &R::carrying}, {TEXT("BCV"), &R::vision}};
		static const std::vector<FA> FB = {{TEXT("RBK"), &R::runBlock}, {TEXT("BTK"), &R::breakTackle}, {TEXT("STR"), &R::strength}, {TEXT("CAR"), &R::carrying}};
		static const std::vector<FA> WR = {{TEXT("SPD"), &R::speed}, {TEXT("CTH"), &R::catching}, {TEXT("RTE"), &R::routeRunning}, {TEXT("CIT"), &R::catchInTraffic}, {TEXT("REL"), &R::release}};
		static const std::vector<FA> TE = {{TEXT("CTH"), &R::catching}, {TEXT("RBK"), &R::runBlock}, {TEXT("PBK"), &R::passBlock}, {TEXT("RTE"), &R::routeRunning}, {TEXT("CIT"), &R::catchInTraffic}};
		static const std::vector<FA> OL = {{TEXT("PBK"), &R::passBlock}, {TEXT("RBK"), &R::runBlock}, {TEXT("STR"), &R::strength}, {TEXT("AWR"), &R::awareness}};
		static const std::vector<FA> DL = {{TEXT("PRSH"), &R::passRush}, {TEXT("BSH"), &R::blockShed}, {TEXT("STR"), &R::strength}, {TEXT("TAK"), &R::tackling}, {TEXT("PUR"), &R::pursuit}};
		static const std::vector<FA> LB = {{TEXT("TAK"), &R::tackling}, {TEXT("PUR"), &R::pursuit}, {TEXT("PRC"), &R::playRec}, {TEXT("ZCV"), &R::zoneCov}, {TEXT("BSH"), &R::blockShed}, {TEXT("PRSH"), &R::passRush}};
		static const std::vector<FA> CB = {{TEXT("MCV"), &R::manCov}, {TEXT("ZCV"), &R::zoneCov}, {TEXT("SPD"), &R::speed}, {TEXT("PRS"), &R::press}, {TEXT("BSK"), &R::ballSkills}};
		static const std::vector<FA> SF = {{TEXT("ZCV"), &R::zoneCov}, {TEXT("PUR"), &R::pursuit}, {TEXT("TAK"), &R::tackling}, {TEXT("PRC"), &R::playRec}, {TEXT("SPD"), &R::speed}, {TEXT("BSK"), &R::ballSkills}};
		static const std::vector<FA> KP = {{TEXT("KPW"), &R::kickPower}, {TEXT("KAC"), &R::kickAcc}};
		using Ps = tag::Pos;
		const std::vector<FA>* Set = &KP;
		switch (P.pos)
		{
		case Ps::QB: Set = &QB; break;
		case Ps::RB: Set = &RB; break;
		case Ps::FB: Set = &FB; break;
		case Ps::WR: Set = &WR; break;
		case Ps::TE: Set = &TE; break;
		case Ps::OT: case Ps::OG: case Ps::C: Set = &OL; break;
		case Ps::DE: case Ps::DT: Set = &DL; break;
		case Ps::LB: Set = &LB; break;
		case Ps::CB: Set = &CB; break;
		case Ps::S: Set = &SF; break;
		default: break;
		}
		std::vector<FA> V = *Set;
		std::stable_sort(V.begin(), V.end(), [&P](const FA& A, const FA& B) { return P.r.*A.M > P.r.*B.M; });
		TArray<TPair<FString, int32>> Out;
		for (int i = 0; i < 2 && i < static_cast<int>(V.size()); ++i) Out.Emplace(V[i].Abbr, FMath::RoundToInt(P.r.*V[i].M));
		return Out;
	}

	/** The team's five best players (kickers aside), best first. */
	std::vector<const tag::Player*> Stars(const tag::Team& T)
	{
		std::vector<const tag::Player*> V;
		for (const auto& P : T.roster)
			if (P->injuredWeeks <= 0 && P->pos != tag::Pos::K && P->pos != tag::Pos::P) V.push_back(P.get());
		std::stable_sort(V.begin(), V.end(), [](const tag::Player* A, const tag::Player* B) { return A->ovr > B->ovr; });
		if (V.size() > 5) V.resize(5);
		return V;
	}

	// ------------------------------------------------------------------ how the team plays
	const TCHAR* OffSchemeNote(const std::string& S)
	{
		if (S == "Power Run") return TEXT("Downhill gap runs behind pulling guards; play action off the hammer.");
		if (S == "Zone Run") return TEXT("Outside zone and stretch, with boots and nakeds off the same look.");
		if (S == "Pro Style") return TEXT("Under center and gun alike: a balanced run game and dropback concepts.");
		if (S == "West Coast") return TEXT("Timing throws that live on yards after the catch: slants, drives, flats.");
		if (S == "Vertical") return TEXT("Seven-step shots down the field from max protection: posts, gos, digs.");
		if (S == "Spread") return TEXT("Three and four wide from the gun, spacing the field for quick reads.");
		if (S == "Run and Shoot") return TEXT("Four wides whose routes adjust to the coverage on the move.");
		if (S == "Air Raid") return TEXT("Mesh, four verticals and Y-cross from the gun, forty throws a game.");
		return TEXT("");
	}
	const TCHAR* DefSchemeNote(const std::string& S)
	{
		if (S == "Tampa 2") return TEXT("Two deep, the Mike runs the middle hole; keep it in front and rally.");
		if (S == "4-3 Quarters") return TEXT("Four-deep match coverage whose safeties fit the run.");
		if (S == "4-3 Cover 3") return TEXT("Single high: eight in the box, three-deep zone behind it.");
		if (S == "3-4 Multiple") return TEXT("Two-gap front, disguised shells, the outside backers rush.");
		if (S == "3-4 Pressure") return TEXT("Zone and fire blitzes from everywhere; zero on the money downs.");
		return TEXT("");
	}
	/** The offense's identity from how often it throws (Team::passLean runs about 0.35 .. 0.75). */
	FString OffIdentity(double PassLean)
	{
		return PassLean < 0.46 ? TEXT("Ground and Pound") : PassLean < 0.52 ? TEXT("Run First") : PassLean < 0.58 ? TEXT("Balanced")
			: PassLean < 0.64 ? TEXT("Pass First") : TEXT("Air It Out");
	}
	FString DefIdentity(double Blitz)
	{
		return Blitz < 0.21 ? TEXT("Bend, Don't Break") : Blitz < 0.27 ? TEXT("Sit in Coverage") : Blitz < 0.33 ? TEXT("Balanced")
			: Blitz < 0.39 ? TEXT("Pressure") : TEXT("Blitz Heavy");
	}
	FString FourthIdentity(double Aggr)
	{
		return Aggr < 0.32 ? TEXT("Punts it away") : Aggr < 0.5 ? TEXT("By the book") : Aggr < 0.68 ? TEXT("Calculated risks") : TEXT("Goes for it");
	}
	const char* SchemeCode(const std::string& S)
	{
		static const std::pair<const char*, const char*> M[] = {{"West Coast", "WC"}, {"Spread", "SP"}, {"Air Raid", "AR"}, {"Pro Style", "PS"},
			{"Power Run", "PR"}, {"Zone Run", "ZR"}, {"Vertical", "VT"}, {"Run and Shoot", "RS"}};
		for (const auto& [N, C] : M)
			if (S == N) return C;
		return "";
	}

	/** The book's signature play: among the every-down plays built for its scheme (not the stock ones every book has),
	 *  those the fewest other schemes run, a run for the run-first books and a throw for the rest; the team's name picks
	 *  one of the near-best (so two books of a scheme show different plays). */
	const tag::OffPlay* SignaturePlay(const tag::Team& T, const tag::OffBook& B)
	{
		const std::string Code = SchemeCode(B.scheme);
		const bool bRun = T.offScheme == "Power Run" || T.offScheme == "Zone Run";
		uint32 H = 2166136261u;
		for (const char C : T.abbr) H = (H ^ static_cast<uint8>(C)) * 16777619u;
		std::vector<std::pair<double, const tag::OffPlay*>> Scored;
		for (const tag::OffPlay* P : B.plays)
		{
			if (P->type == tag::PlayType::Kneel || P->type == tag::PlayType::Spike) continue;
			// (a signature is an every-down play, not a goal-line or short-yardage one)
			if (P->hasTag("goalline") || P->hasTag("shortyard") || P->formation == "heavy") continue;
			const bool bIsRun = P->type == tag::PlayType::Run;
			const int32 Codes = P->styles.empty() ? 8 : static_cast<int32>((P->styles.size() + 1) / 3);
			Scored.emplace_back((P->stock ? -20.0 : 0.0) - Codes * 2.0 + (bIsRun == bRun ? 6.0 : 0.0)
				+ (!Code.empty() && P->styles.find(Code) != std::string::npos ? 3.0 : 0.0), P);
		}
		if (Scored.empty()) return B.plays.empty() ? nullptr : B.plays[0];
		double Top = -1e9;
		for (const auto& [Sc, P] : Scored) Top = FMath::Max(Top, Sc);
		std::vector<const tag::OffPlay*> Near;
		for (const auto& [Sc, P] : Scored)
			if (Sc >= Top - 2.0) Near.push_back(P);
		const tag::OffPlay* Best = Near[H % Near.size()];
		return Best;
	}

	/** The play drawn as a play-call card draws it: a fresh pre-snap sim of it at the 30 against a stock Cover 3 (no
	 *  game needed, no randomness spent). */
	void PlayPicture(tag::Team& Off, tag::Team& Def, const tag::OffPlay* Op, FArtPicture& Out)
	{
		Out.Strokes.Reset();
		Out.Marks.Reset();
		const tag::DefCall* Dc = tag::defById("base_c3");
		if (!Dc && !tag::DEF_PLAYS().empty()) Dc = &tag::DEF_PLAYS()[0];
		if (!Op || !Dc) return;
		tag::PlayConfig Cfg;
		Cfg.off = &Off;
		Cfg.def = &Def;
		Cfg.offPlay = Op;
		Cfg.defCall = Dc;
		Cfg.los = 30;
		Cfg.ballY = tag::FIELD_W / 2;
		Cfg.rng = tag::Rng(7u);
		Cfg.down = 1.0;
		Cfg.toGo = 10.0;
		const tag::PlaySim Sim(Cfg);
		const std::vector<tag::Ent*> Order = ArtLabelOrder(Sim);
		BuildArtPicture(Sim, true, nullptr, nullptr, [&Order](const tag::Ent* E)
		{
			const auto It = std::find(Order.begin(), Order.end(), E);
			const int32 I = It == Order.end() ? -1 : static_cast<int32>(It - Order.begin());
			return I < 5 ? I : -1;
		}, Out, ArtCardDepthCap);
	}

	// ------------------------------------------------------------------ widgets
	/** The screen's root: the one focusable widget, so every pad button and key comes to the screen. */
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

	/** A controller button (or its key) drawn by the overlay's glyph painter. */
	class SGlyph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGlyph) : _Owner(nullptr), _Glyph(EPadGlyph::None), _Size(24.f) {}
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
			return Owner ? Owner->GlyphExtent(Glyph, Key, Size, Owner->PadGlyphs()) : FVector2D(Size + 4, Size + 4);
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
		float Size = 24.f;
	};

	/** A rating in a ring: the arc fills with the rating, in its colour, the number in the middle. */
	class SRing : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRing) : _Value(0), _Size(64.f), _Thick(5.f), _FontSize(22) {}
			SLATE_ARGUMENT(int32, Value)
			SLATE_ARGUMENT(float, Size)
			SLATE_ARGUMENT(float, Thick)
			SLATE_ARGUMENT(int32, FontSize)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Value = A._Value;
			Size = A._Size;
			Thick = A._Thick;
			FontSize = A._FontSize;
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size, Size); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			const FVector2f C(Size * 0.5f, Size * 0.5f);
			const float R = Size * 0.5f - Thick * 0.5f - 1.f;
			auto Arc = [&](float From, float To, const FLinearColor& Cl, int32 L)
			{
				TArray<FVector2f> P;
				const int32 N = FMath::Max(2, FMath::CeilToInt((To - From) * 72));
				for (int32 i = 0; i <= N; ++i)
				{
					const float A = (From + (To - From) * i / N) * 2.f * PI - PI * 0.5f;
					P.Add(C + FVector2f(FMath::Cos(A), FMath::Sin(A)) * R);
				}
				FSlateDrawElement::MakeLines(Out, Layer + L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, Cl, true, Thick);
			};
			// the disc, the track, then the rating's share of it (50 .. 99 fills the ring)
			const float Fill = FMath::Clamp((Value - 50) / 49.f, 0.04f, 1.f);
			const FLinearColor Cl = Tier(Value);
			Arc(0.f, 1.f, FLinearColor(1, 1, 1, 0.1f), 0);
			Arc(0.f, Fill, Cl, 1);
			const FSlateFontInfo F = Font(FontSize, "Bold");
			const FString S = FString::FromInt(Value);
			const FVector2D M = Measure(S, F);
			FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(C - FVector2f(M) * 0.5f)), S, F,
				ESlateDrawEffect::None, FLinearColor::White);
			return Layer + 3;
		}
	private:
		int32 Value = 0;
		float Size = 64.f, Thick = 5.f;
		int32 FontSize = 22;
	};

	/** A rating bar: a dim track, the rating's share of it in its colour (Mirror: grows from the right). */
	class SBar : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBar) : _Value(0), _Height(6.f), _Mirror(false), _Color(FLinearColor::White), _bTier(true) {}
			SLATE_ARGUMENT(double, Value)
			SLATE_ARGUMENT(float, Height)
			SLATE_ARGUMENT(bool, Mirror)
			SLATE_ARGUMENT(FLinearColor, Color)
			SLATE_ARGUMENT(bool, bTier)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Value = A._Value;
			Height = A._Height;
			bMirror = A._Mirror;
			Color = A._bTier ? Tier(A._Value) : A._Color;
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(40, Height); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			const FVector2f Sz = FVector2f(G.GetLocalSize());
			const float H = FMath::Min(Height, Sz.Y);
			const float Y = (Sz.Y - H) * 0.5f;
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Sz.X, H), FSlateLayoutTransform(FVector2f(0, Y))), WhiteBrush(),
				ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.08f));
			// 40 .. 99 spans the bar (a 60 shows a third, a 90 most of it)
			const float W = Sz.X * FMath::Clamp(static_cast<float>((Value - 40) / 59.0), 0.02f, 1.f);
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(bMirror ? Sz.X - W : 0, Y))), WhiteBrush(),
				ESlateDrawEffect::None, Color);
			return Layer + 2;
		}
	private:
		double Value = 0;
		float Height = 6.f;
		bool bMirror = false;
		FLinearColor Color = FLinearColor::White;
	};

	/** A tendency slider: a track between two words and a marker where the team sits (0 .. 1). */
	class SMeter : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SMeter) : _Value(0.5), _Color(FLinearColor::White) {}
			SLATE_ARGUMENT(double, Value)
			SLATE_ARGUMENT(FLinearColor, Color)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Value = FMath::Clamp(A._Value, 0.0, 1.0);
			Color = A._Color;
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(120, 14); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			const FVector2f Sz = FVector2f(G.GetLocalSize());
			const float Y = Sz.Y * 0.5f;
			// the track, its five notches, the filled run to the marker, the marker
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(Sz.X, 4), FSlateLayoutTransform(FVector2f(0, Y - 2))), WhiteBrush(),
				ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.1f));
			for (int32 i = 0; i <= 4; ++i)
				FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(1, 8), FSlateLayoutTransform(FVector2f(FMath::Min(Sz.X - 1, Sz.X * i / 4.f), Y - 4))), WhiteBrush(),
					ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.16f));
			const float X = Sz.X * static_cast<float>(Value);
			FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(X, 4), FSlateLayoutTransform(FVector2f(0, Y - 2))), WhiteBrush(),
				ESlateDrawEffect::None, Color * FLinearColor(1, 1, 1, 0.55f));
			FSlateDrawElement::MakeBox(Out, Layer + 2, G.ToPaintGeometry(FVector2f(8, Sz.Y), FSlateLayoutTransform(FVector2f(FMath::Clamp(X - 4, 0.f, Sz.X - 8), 0))), WhiteBrush(),
				ESlateDrawEffect::None, Color);
			return Layer + 3;
		}
	private:
		double Value = 0.5;
		FLinearColor Color = FLinearColor::White;
	};

	/** The backdrop: each team's colour washing in from its own side of the screen over the dimmed stadium. */
	class SWash : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWash) {}
			SLATE_ARGUMENT(FLinearColor, Left)
			SLATE_ARGUMENT(FLinearColor, Right)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Left = A._Left;
			Right = A._Right;
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100, 100); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer,
			const FWidgetStyle&, bool) const override
		{
			const FVector2f Sz = FVector2f(G.GetLocalSize());
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(), WhiteBrush(), ESlateDrawEffect::None, FLinearColor(0.008f, 0.01f, 0.018f, 0.86f));
			auto A = [](FLinearColor C, float Al) { C.A = Al; return C; };
			TArray<FSlateGradientStop> S;
			S.Add(FSlateGradientStop(FVector2f(0, 0), A(Left, 0.55f)));
			S.Add(FSlateGradientStop(FVector2f(Sz.X * 0.30f, 0), A(Left, 0.12f)));
			S.Add(FSlateGradientStop(FVector2f(Sz.X * 0.5f, 0), FLinearColor(0, 0, 0, 0)));
			S.Add(FSlateGradientStop(FVector2f(Sz.X * 0.70f, 0), A(Right, 0.12f)));
			S.Add(FSlateGradientStop(FVector2f(Sz.X, 0), A(Right, 0.55f)));
			FSlateDrawElement::MakeGradient(Out, Layer + 1, G.ToPaintGeometry(), S, Orient_Vertical);
			// a darker band behind the title and the button bar
			TArray<FSlateGradientStop> V;
			V.Add(FSlateGradientStop(FVector2f(0, 0), FLinearColor(0, 0, 0, 0.55f)));
			V.Add(FSlateGradientStop(FVector2f(0, Sz.Y * 0.1f), FLinearColor(0, 0, 0, 0)));
			V.Add(FSlateGradientStop(FVector2f(0, Sz.Y * 0.9f), FLinearColor(0, 0, 0, 0)));
			V.Add(FSlateGradientStop(FVector2f(0, Sz.Y), FLinearColor(0, 0, 0, 0.6f)));
			FSlateDrawElement::MakeGradient(Out, Layer + 2, G.ToPaintGeometry(), V, Orient_Horizontal);
			return Layer + 3;
		}
	private:
		FLinearColor Left, Right;
	};

	/** A flat click target (no button art to tint): the content draws itself. */
	TSharedRef<SWidget> Click(TSharedRef<SWidget> Content, std::function<void()> OnClick)
	{
		return SNew(SButton).IsFocusable(false).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
			.OnClicked_Lambda([OnClick] { ATagAudio::Ui(TEXT("UiSelect"), 0.8f); OnClick(); return FReply::Handled(); })[ Content ];
	}
	TSharedRef<SWidget> Label(const FString& S, int32 Size = 10, const FLinearColor& C = Dim, const char* W = "Bold")
	{
		return SNew(STextBlock).Font(Font(Size, W)).ColorAndOpacity(C).Text(Tx(S));
	}
	/** A section's heading: small caps in the dim colour over a hairline. */
	TSharedRef<SWidget> Heading(const FString& S)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Font(Font(9, "Bold")).ColorAndOpacity(Faint).Text(Tx(S.ToUpper())) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 6)[ SNew(SBox).HeightOverride(1)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.08f)) ] ];
	}
	/** The team's mark on its colour (or its initial, if the mark is missing). */
	TSharedRef<SWidget> Mark(const tag::Team& T, float Size, bool bBackdrop = true)
	{
		const FSlateBrush* B = Logo(T);
		const FLinearColor Pri = Col(T.colors.primary);
		TSharedRef<SWidget> Inner = SNullWidget::NullWidget;
		if (B) Inner = SNew(SImage).Image(B);
		else Inner = SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[ SNew(STextBlock).Font(Font(FMath::RoundToInt(Size * 0.4f), "Bold")).ColorAndOpacity(OnColor(Pri)).Text(Tx(Str(T.name).Left(1))) ];
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
			[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bBackdrop ? Pri : FLinearColor::Transparent).Padding(Size * 0.06f)[ Inner ] ];
	}
}

namespace TeamSel
{
	/** The screen's key handler as last built (the -TagTeamSelect check presses keys through it). */
	std::function<bool(const FKey&)> GKey;

	/** -TagTeamSelect: a scripted visit, a picture after each step (Saved/Screenshots/.../tag_teamselect_NN.png, with the
	 *  UI), then the game exits. Run it with -RenderOffscreen: nothing touches the desktop. */
	void StartShotScript(TWeakPtr<SGridironOverlay> Overlay)
	{
		struct FStep { TArray<FKey> Keys; const TCHAR* Name; bool bWatch; };
		static TArray<FStep> Steps = {
			{{}, TEXT("open"), false},
			{{EKeys::E}, TEXT("home_next"), false},
			{{EKeys::Left, EKeys::Left}, TEXT("away_focus"), false},
			{{EKeys::Q}, TEXT("away_prev"), false},
			{{EKeys::Right, EKeys::Down, EKeys::Down}, TEXT("options_row"), false},
			{{EKeys::E}, TEXT("option_changed"), false},
			{{EKeys::T}, TEXT("swapped"), false},
			{{EKeys::Down, EKeys::Down, EKeys::Down}, TEXT("kickoff_row"), false},
			{{EKeys::Right, EKeys::R}, TEXT("random_home"), false},
			{{}, TEXT("watch"), true},
		};
		static int32 I = -2;  // two beats for the first layout
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Overlay](float) -> bool
		{
			const TSharedPtr<SGridironOverlay> O = Overlay.Pin();
			if (!O.IsValid()) return false;
			if (++I < 0) return true;
			if (I >= Steps.Num())
			{
				UE_LOG(LogTemp, Display, TEXT("TAG team select shots done"));
				FPlatformMisc::RequestExit(false);
				return false;
			}
			const FStep& S = Steps[I];
			if (S.bWatch) O->ShowTeamSelect(false);
			for (const FKey& K : S.Keys)
				if (GKey) GKey(K);
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_teamselect_%02d_%s.png"), I, S.Name), true, false);
			UE_LOG(LogTemp, Display, TEXT("TAG team select shot %d %s"), I, S.Name);
			return true;
		}), 1.5f);
	}
}

using namespace TeamSel;

// ------------------------------------------------------------------ the screen
void SGridironOverlay::ShowTeamSelect(bool bUserPlays)
{
	if (!Director || Director->League.size() < 2) return;
	ATagAudio::Screen(TEXT("TeamSelect"));
	static const TCHAR* WxNames[] = {TEXT("Random (realistic)"), TEXT("Clear"), TEXT("Windy"), TEXT("Rain"), TEXT("Snow")};
	static const double Quarters[] = {900, 300, 480};
	static const TCHAR* QNames[] = {TEXT("15 min"), TEXT("5 min"), TEXT("8 min")};
	const int32 NTeams = static_cast<int32>(Director->League.size());
	SelHome = FMath::Clamp(SelHome, 0, NTeams - 1);
	SelAway = FMath::Clamp(SelAway, 0, NTeams - 1);
	if (SelAway == SelHome) SelAway = (SelHome + 1) % NTeams;
	const bool bFresh = !bModal || !CallRoot.IsValid() || ModalContent != CallRoot;  // (not this screen rebuilt)
	if (bFresh || bSelUserPlays != bUserPlays)
	{
		// a fresh visit: the pad starts on the user's own team (on the home team when watching)
		bSelUserPlays = bUserPlays;
		SelCol = bUserPlays && !bSelUserHome ? 0 : 2;
		SelRow = 0;
	}
	SelCol = FMath::Clamp(SelCol, 0, 2);

	const TArray<FInfo> All = LeagueInfo(Director->League);
	const FInfo& Away = All[SelAway];
	const FInfo& Home = All[SelHome];
	UE_LOG(LogTemp, Display, TEXT("TAG team select up: %s (%d) at %s (%d), pad on column %d row %d"), *Str(Away.Tm->abbr), Away.O.ovr,
		*Str(Home.Tm->abbr), Home.O.ovr, SelCol, SelRow);

	// the options column's rows (the last one is the kickoff)
	struct FOpt { FString Label, Value; std::function<void(int32)> Step; };
	TArray<FOpt> Opts;
	if (bUserPlays && !bSelMulti)
		Opts.Add({TEXT("You control"), bSelUserHome ? Str(Home.Tm->abbr) + TEXT("  (home)") : Str(Away.Tm->abbr) + TEXT("  (away)"), [this](int32) { bSelUserHome = !bSelUserHome; }});
	Opts.Add({TEXT("Weather"), WxNames[SelWeather], [this](int32 D) { SelWeather = (SelWeather + D + 5) % 5; }});
	Opts.Add({TEXT("Kickoff"), bSelNight ? TEXT("Night (lights)") : TEXT("Day"), [this](int32) { bSelNight = !bSelNight; }});
	Opts.Add({TEXT("Quarter length"), QNames[SelQuarter], [this](int32 D) { SelQuarter = (SelQuarter + D + 3) % 3; }});
	const int32 NRows = Opts.Num() + 1;
	SelRow = FMath::Clamp(SelRow, 0, NRows - 1);

	auto Rebuild = [this, bUserPlays] { ShowTeamSelect(bUserPlays); };
	auto Step = [this, NTeams](bool bHome, int32 D)
	{
		int32& Sel = bHome ? SelHome : SelAway;
		const int32 Other = bHome ? SelAway : SelHome;
		Sel = (Sel + D + NTeams) % NTeams;
		if (Sel == Other) Sel = (Sel + D + NTeams) % NTeams;
	};
	auto Kick = [this, bUserPlays]
	{
		// (a multiplayer game: the sides were picked in the lobby)
		if (bSelMulti) Director->StartMultiMatch(SelHome, SelAway, SelWeather, bSelNight, Quarters[SelQuarter]);
		else Director->StartMatch(SelHome, SelAway, bUserPlays, bSelUserHome, SelWeather, bSelNight, Quarters[SelQuarter]);
	};
	auto Random = [this, NTeams](bool bHome)
	{
		int32& Sel = bHome ? SelHome : SelAway;
		const int32 Other = bHome ? SelAway : SelHome;
		if (NTeams < 3) return;
		int32 Pick = Sel;
		while (Pick == Sel || Pick == Other) Pick = FMath::RandRange(0, NTeams - 1);
		Sel = Pick;
	};

	// ---- one team's side
	auto TeamPanel = [&](bool bHome) -> TSharedRef<SWidget>
	{
		const FInfo& I = bHome ? Home : Away;
		const FInfo& Opp = bHome ? Away : Home;
		const tag::Team& Tm = *I.Tm;
		const FLinearColor Pri = Col(Tm.colors.primary);
		const FLinearColor Sec = Col(Tm.colors.secondary);
		const FLinearColor OnPri = OnColor(Pri);
		const bool bFocus = SelCol == (bHome ? 2 : 0);
		const bool bUsers = bSelMulti ? Director->TeamHumanInLobby(bHome) : bUserPlays && bSelUserHome == bHome;
		const int32 St = FMath::RoundToInt(I.Unit[UKP]);
		const int32 Col0 = bHome ? 2 : 0;

		// the header: arrows, the mark, the name, the overall
		auto Arrow = [&](bool bNext) -> TSharedRef<SWidget>
		{
			return SNew(SButton).IsFocusable(false).ButtonColorAndOpacity(FLinearColor(0, 0, 0, 0.18f)).ContentPadding(FMargin(6, 4))
				.OnClicked_Lambda([this, Step, bHome, bNext, Col0, Rebuild] { ATagAudio::Ui(TEXT("UiTeam")); Step(bHome, bNext ? 1 : -1); SelCol = Col0; Rebuild(); return FReply::Handled(); })
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(Font(20, "Bold")).ColorAndOpacity(OnPri).Text(Tx(bNext ? TEXT(">") : TEXT("<"))) ]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(SGlyph).Owner(this).Glyph(bNext ? EPadGlyph::R1 : EPadGlyph::L1).Key(bNext ? TEXT("E") : TEXT("Q")).Size(20.f) ]
				];
		};
		const FString Side = bHome ? TEXT("HOME") : TEXT("AWAY");
		FString Who = bUserPlays ? (bUsers ? TEXT("YOU") : TEXT("CPU")) : TEXT("CPU");
		if (bSelMulti && bUsers) Who = Director->LobbyTags(bHome); // "P1  P2"
		const bool bYou = bUsers;
		TSharedRef<SWidget> Header = SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Pri).Padding(FMargin(8, 10, 8, 10))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Arrow(false) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 12, 0)[ Mark(Tm, 92.f, false) ]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.35f)).Padding(FMargin(7, 1))[ Label(Side, 9, FLinearColor::White) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
					[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bYou ? Gold : FLinearColor(1, 1, 1, 0.2f)).Padding(FMargin(7, 1))
						[ Label(Who, 9, bYou ? Ink : FLinearColor::White) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
					[ Label(FString::Printf(TEXT("%s CONFERENCE  ·  %s"), *Str(Tm.conference).ToUpper(), *Str(Tm.division).ToUpper()), 9, OnPri * FLinearColor(1, 1, 1, 0.75f)) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[ SNew(STextBlock).Font(Font(15)).ColorAndOpacity(OnPri * FLinearColor(1, 1, 1, 0.85f)).Text(Tx(Str(Tm.city).ToUpper())) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, -4, 0, 0)[ SNew(STextBlock).Font(Font(30, "Bold")).ColorAndOpacity(OnPri).Text(Tx(Str(Tm.name).ToUpper())) ]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(0, 0, 0, 0.45f)).Padding(FMargin(10, 6))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(SRing).Value(I.O.ovr).Size(74.f).Thick(6.f).FontSize(26) ]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3, 0, 0)[ Label(FString::Printf(TEXT("OVR  ·  %s"), *Ordinal(I.OvrRank)), 9, FLinearColor::White) ]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Arrow(true) ]
		];

		// the ratings: offense, defense, special teams, then every position group with its rank
		auto Small = [&](const FString& Name, int32 V, int32 Rank) -> TSharedRef<SWidget>
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SRing).Value(V).Size(50.f).Thick(4.f).FontSize(16) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Label(Name, 11, White90) ]
					+ SVerticalBox::Slot().AutoHeight()[ Label(Rank > 0 ? Ordinal(Rank) + TEXT(" in league") : FString(), 9, Dim, "Regular") ]
				];
		};
		int32 StRank = 1;
		for (const FInfo& J : All) StRank += J.Unit[UKP] > I.Unit[UKP] + 1e-9;
		TSharedRef<SWidget> Rings = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[ Small(TEXT("OFFENSE"), I.O.off, I.OffRank) ]
			+ SHorizontalBox::Slot().FillWidth(1)[ Small(TEXT("DEFENSE"), I.O.def, I.DefRank) ]
			+ SHorizontalBox::Slot().FillWidth(1)[ Small(TEXT("SPECIAL TEAMS"), St, StRank) ];

		auto UnitRow = [&](int32 u) -> TSharedRef<SWidget>
		{
			const double V = I.Unit[u];
			const int32 R = I.UnitRank[u];
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(34)[ Label(Units()[u].Short, 10, White90) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(104)[ Label(Units()[u].Long, 9, Dim, "Regular") ] ]
				+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ SNew(SBox).HeightOverride(14)[ SNew(SBar).Value(V).Height(6.f) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[ SNew(SBox).WidthOverride(22).HAlign(HAlign_Right)[ Label(FString::FromInt(FMath::RoundToInt(V)), 11, Tier(V)) ] ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBox).WidthOverride(38).HAlign(HAlign_Right)[ Label(Ordinal(R), 9, R <= 8 ? Col(TEXT("2FD47E")) : R >= 25 ? Col(TEXT("F0703C")) : Faint) ] ];
		};
		TSharedRef<SVerticalBox> UnitsL = SNew(SVerticalBox), UnitsR = SNew(SVerticalBox);
		for (int32 u = 0; u < NumUnits; ++u)
			(u < 5 ? UnitsL : UnitsR)->AddSlot().AutoHeight().Padding(0, 1)[ UnitRow(u) ];

		// strengths and weaknesses: the groups in the league's top eight and bottom eight
		TArray<int32> Strong, Weak;
		for (int32 u = 0; u < NumUnits; ++u)
		{
			if (I.UnitRank[u] <= 8) Strong.Add(u);
			if (I.UnitRank[u] >= 25) Weak.Add(u);
		}
		Strong.Sort([&](int32 A, int32 B) { return I.UnitRank[A] < I.UnitRank[B]; });
		Weak.Sort([&](int32 A, int32 B) { return I.UnitRank[A] > I.UnitRank[B]; });
		TSharedRef<SWrapBox> Chips = SNew(SWrapBox).UseAllottedSize(true);
		auto Chip = [&](const FString& S, const FLinearColor& C)
		{
			Chips->AddSlot().Padding(0, 0, 6, 0)
			[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(C * FLinearColor(1, 1, 1, 0.18f)).Padding(FMargin(8, 2))[ Label(S, 9, C) ] ];
		};
		for (int32 k = 0; k < Strong.Num() && k < 3; ++k) Chip(FString::Printf(TEXT("+ %s"), Units()[Strong[k]].Long), Col(TEXT("2FD47E")));
		for (int32 k = 0; k < Weak.Num() && k < 2; ++k) Chip(FString::Printf(TEXT("- %s"), Units()[Weak[k]].Long), Col(TEXT("F0703C")));
		if (Strong.IsEmpty() && Weak.IsEmpty()) Chip(TEXT("No glaring holes, no standout unit"), Dim);

		// the star players
		TSharedRef<SHorizontalBox> StarRow = SNew(SHorizontalBox);
		for (const tag::Player* P : Stars(Tm))
		{
			const TArray<TPair<FString, int32>> K = KeyRatings(*P);
			FString Keys;
			for (const auto& KV : K) Keys += (Keys.IsEmpty() ? TEXT("") : TEXT("   ")) + FString::Printf(TEXT("%s %d"), *KV.Key, KV.Value);
			// his grade (Sim/Abilities.h: his standing at his position league-wide) and what he carries
			const FString Tierword = P->grade == tag::StarGrade::None ? FString(TEXT("STARTER")) : FString(UTF8_TO_TCHAR(tag::starGradeName(P->grade))).ToUpper();
			const FLinearColor TierC = P->grade == tag::StarGrade::XFactor ? Col(TEXT("FF6A1A")) : P->grade == tag::StarGrade::Superstar ? Col(TEXT("B58CFF"))
				: P->grade == tag::StarGrade::Star ? Col(TEXT("72B8FF")) : Faint;
			TSharedRef<SHorizontalBox> Carry = SNew(SHorizontalBox);
			auto Pip = [&](const tag::AbilityDef& A)
			{
				if (const FSlateBrush* Ic = TagAbilityArt::Icon(A))
				{
					Carry->AddSlot().AutoWidth().Padding(0, 0, 2, 0)
					[ SNew(SBox).WidthOverride(22).HeightOverride(22).ToolTipText(Tx(Str(A.name)))[ SNew(SImage).Image(Ic) ] ];
					return;
				}
				const FLinearColor C = A.tier == tag::AbilityTier::XFactor ? Col(TEXT("FF6A1A")) : Gold;
				Carry->AddSlot().AutoWidth().Padding(0, 0, 3, 0)
				[ SNew(SBox).WidthOverride(22).HeightOverride(14).ToolTipText(Tx(Str(A.name)))
					[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(C).Padding(0).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Label(UTF8_TO_TCHAR(A.icon), 7, Ink) ] ] ];
			};
			if (P->xFactor != tag::AbilityId::None) Pip(tag::abilityDef(P->xFactor));
			for (tag::AbilityId Id : P->abilities) Pip(tag::abilityDef(Id));
			const int32 TeamIdx = bHome ? SelHome : SelAway;
			StarRow->AddSlot().FillWidth(1).Padding(0, 0, 6, 0)
			[
				Click(SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(CardHi).Padding(0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).HeightOverride(3)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Tier(P->ovr)) ] ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 5, 8, 0))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
						[ Label(FString::Printf(TEXT("%s  #%d"), ANSI_TO_TCHAR(tag::posName(P->pos)), P->number), 10, Dim) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(FString::FromInt(P->ovr), 18, Tier(P->ovr)) ]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 0, 8, 0))
					[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(Dim).Text(Tx(Str(P->first))).Clipping(EWidgetClipping::ClipToBounds) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, -2, 8, 0))
					[ SNew(STextBlock).Font(Font(13, "Bold")).ColorAndOpacity(White90).Text(Tx(Str(P->last).ToUpper())).Clipping(EWidgetClipping::ClipToBounds) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 3, 8, 0))[ Label(Keys, 9, White90 * FLinearColor(1, 1, 1, 0.8f), "Regular") ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 2, 8, 0))[ Label(Tierword, 8, TierC) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(8, 2, 8, 7))[ SNew(SBox).HeightOverride(14)[ Carry ] ]
				], [this, TeamIdx, bUserPlays] { ShowAbilities(TeamIdx, [this, bUserPlays] { ShowTeamSelect(bUserPlays); }); })
			];
		}

		// how it plays: the schemes, its identity and three tendencies
		auto Tend = [&](const FString& L, const FString& R, double V, const FString& Word) -> TSharedRef<SWidget>
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1)[ Label(L, 8, Faint) ]
					+ SHorizontalBox::Slot().AutoWidth()[ Label(Word, 9, White90) ]
					+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right)[ Label(R, 8, Faint) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)[ SNew(SBox).HeightOverride(12)[ SNew(SMeter).Value(V).Color(Gold) ] ];
		};
		auto SchemeBlock = [&](const FString& Side, const std::string& Scheme, const FString& Ident, const TCHAR* Note) -> TSharedRef<SWidget>
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(30)[ Label(Side, 9, Faint) ] ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(Str(Scheme), 13, White90) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
					[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Gold * FLinearColor(1, 1, 1, 0.16f)).Padding(FMargin(6, 1))[ Label(Ident.ToUpper(), 8, Gold) ] ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(30, 1, 0, 0)[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Dim).WrapTextAt(300.f).Text(Tx(Note)) ];
		};
		const double PassV = FMath::Clamp((Tm.passLean - 0.35) / 0.4, 0.0, 1.0);
		const double AggrV = FMath::Clamp((Tm.aggression - 0.1) / 0.85, 0.0, 1.0);
		const double BlitzV = FMath::Clamp((Tm.blitzLean - 0.1) / 0.4, 0.0, 1.0);
		TSharedRef<SWidget> Style = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Heading(TEXT("Playstyle")) ]
			+ SVerticalBox::Slot().AutoHeight()[ SchemeBlock(TEXT("OFF"), Tm.offScheme, OffIdentity(Tm.passLean), OffSchemeNote(Tm.offScheme)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[ SchemeBlock(TEXT("DEF"), Tm.defScheme, DefIdentity(Tm.blitzLean), DefSchemeNote(Tm.defScheme)) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[ Tend(TEXT("RUN"), TEXT("PASS"), PassV, FString::Printf(TEXT("%d%% pass calls"), FMath::RoundToInt(Tm.passLean * 100))) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)[ Tend(TEXT("COVER"), TEXT("BLITZ"), BlitzV, FString::Printf(TEXT("Blitzes %d%%"), FMath::RoundToInt(Tm.blitzLean * 100))) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)[ Tend(TEXT("SAFE"), TEXT("AGGRESSIVE"), AggrV, FString::Printf(TEXT("4th down: %s"), *FourthIdentity(Tm.aggression))) ];

		// the playbook: its size, its formation groups, its signature play drawn
		const tag::OffBook& OB = tag::offBookFor(Tm);
		const tag::DefBook& DB = tag::defBookFor(Tm);
		TArray<FString> Families;
		for (const std::string& F : OB.formations)
			if (const tag::Formation* Fm = tag::formationById(F)) Families.AddUnique(Str(tag::formationFamily(*Fm)));
		FString FamText;
		for (int32 k = 0; k < Families.Num() && k < 6; ++k) FamText += (k ? TEXT("  ·  ") : TEXT("")) + Families[k];
		if (Families.Num() > 6) FamText += FString::Printf(TEXT("  +%d"), Families.Num() - 6);
		const tag::OffPlay* Sig = SignaturePlay(Tm, OB);
		TSharedRef<SWidget> Art = SNullWidget::NullWidget;
		FString SigName, SigForm;
		if (Sig)
		{
			FArtPicture Pic;
			PlayPicture(*Director->League[bHome ? SelHome : SelAway], *Director->League[bHome ? SelAway : SelHome], Sig, Pic);
			if (!Pic.Marks.IsEmpty()) Art = SNew(SPlayDiagram).Picture(Pic).Offense(true).FirstDown(-1.0);
			SigName = Str(Sig->name);
			if (const tag::Formation* Fm = tag::formationById(Sig->formation)) SigForm = Str(Fm->name);
		}
		const FString BaseFront = DB.base == tag::Front::Base34 ? TEXT("3-4") : TEXT("4-3");
		TSharedRef<SWidget> Book = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Heading(TEXT("Playbook")) ]
			+ SVerticalBox::Slot().AutoHeight()[ Label(Str(Tm.name) + TEXT(" Playbook"), 13, White90) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
			[ Label(FString::Printf(TEXT("%d formations  ·  %d plays      %s base  ·  %d calls"), static_cast<int32>(OB.formations.size()), static_cast<int32>(OB.plays.size()),
				*BaseFront, static_cast<int32>(DB.calls.size())), 9, Dim, "Regular") ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 6)[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(Faint).WrapTextAt(296.f).Text(Tx(FamText)) ]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.08f)).Padding(1)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).WidthOverride(300).HeightOverride(150)[ Art ] ]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Ink).Padding(FMargin(8, 4))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(TEXT("SIGNATURE"), 8, Gold) ]
							+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
							[ SNew(STextBlock).Font(Font(11, "Bold")).ColorAndOpacity(White90).Text(Tx(SigName)).Clipping(EWidgetClipping::ClipToBounds) ]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[ Label(SigForm, 9, Dim, "Regular") ]
						]
					]
				]
			];

		// every team, a click away
		TSharedRef<SWrapBox> Strip = SNew(SWrapBox).PreferredSize(660).UseAllottedSize(true);
		for (int32 k = 0; k < NTeams; ++k)
		{
			const tag::Team& T = *Director->League[k];
			const bool bSel = k == (bHome ? SelHome : SelAway);
			const bool bTaken = k == (bHome ? SelAway : SelHome);
			Strip->AddSlot().Padding(1)
			[
				SNew(SBox).WidthOverride(36).HeightOverride(36).ToolTipText(Tx(Str(T.city) + TEXT(" ") + Str(T.name)))
				[
					Click(SNew(SBorder).BorderImage(TagTheme::Round(5.f)).BorderBackgroundColor(bSel ? Gold : FLinearColor::Transparent).Padding(2)
						[ SNew(SBox).RenderOpacity(bTaken ? 0.25f : 1.f)[ Mark(T, 32.f) ] ],
						[this, k, bHome, Col0, Rebuild] { int32& S = bHome ? SelHome : SelAway; if (k != (bHome ? SelAway : SelHome)) S = k; SelCol = Col0; Rebuild(); })
				]
			];
		}

		return SNew(SBox).WidthOverride(700.f)
		[
			SNew(TagTheme::SPanel).Fill(Card).Focused(bFocus).Radius(6.f).Padding(FMargin(0))
			[
				SNew(SBox).Padding(0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ Header ]
					+ SVerticalBox::Slot().AutoHeight()[ SNew(SBox).HeightOverride(4)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Sec) ] ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 12, 16, 0))[ Rings ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 10, 16, 0))[ Heading(TEXT("Team ratings")) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 0, 16, 0))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 14, 0)[ UnitsL ]
						+ SHorizontalBox::Slot().FillWidth(1).Padding(14, 0, 0, 0)[ UnitsR ]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 7, 16, 0))[ Chips ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 10, 16, 0))[ Heading(TEXT("Star players  -  R2 / F: Superstars & X-Factors")) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 0, 10, 0))[ StarRow ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 10, 16, 0))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 16, 0)[ Style ]
						+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(302)[ Book ] ]
					]
					+ SVerticalBox::Slot().FillHeight(1)[ SNullWidget::NullWidget ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(16, 10, 16, 0))[ Heading(TEXT("Choose a team")) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(14, 0, 14, 12))[ Strip ]
				]
			]
		];
	};

	// ---- the middle: the tale of the tape, the projection, the options, the kickoff
	auto TapeRow = [&](const FString& Name, double A, double H) -> TSharedRef<SWidget>
	{
		const int32 Ai = FMath::RoundToInt(A), Hi = FMath::RoundToInt(H);
		const FLinearColor Ac = Ai > Hi ? Gold : Dim, Hc = Hi > Ai ? Gold : Dim;
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(34)[ Label(FString::FromInt(Ai), 13, Ac) ] ]
				+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Label(Name, 9, Faint) ]
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(34).HAlign(HAlign_Right)[ Label(FString::FromInt(Hi), 13, Hc) ] ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 2, 0)[ SNew(SBox).HeightOverride(5)[ SNew(SBar).Value(A).Height(5.f).Mirror(true).bTier(false).Color(Vivid(Col(Away.Tm->colors.primary))) ] ]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(2, 0, 0, 0)[ SNew(SBox).HeightOverride(5)[ SNew(SBar).Value(H).Height(5.f).bTier(false).Color(Vivid(Col(Home.Tm->colors.primary))) ] ]
			];
	};
	TSharedRef<SVerticalBox> Tape = SNew(SVerticalBox);
	auto AddTape = [&](const FString& N, double A, double H) { Tape->AddSlot().AutoHeight().Padding(0, 3)[ TapeRow(N, A, H) ]; };
	AddTape(TEXT("OVERALL"), Away.O.ovr, Home.O.ovr);
	AddTape(TEXT("OFFENSE"), Away.O.off, Home.O.off);
	AddTape(TEXT("DEFENSE"), Away.O.def, Home.O.def);
	AddTape(TEXT("PASSING"), Away.Passing, Home.Passing);
	AddTape(TEXT("RUSHING"), Away.Rushing, Home.Rushing);
	AddTape(TEXT("PASS RUSH"), Away.PassRush, Home.PassRush);
	AddTape(TEXT("COVERAGE"), Away.Coverage, Home.Coverage);
	AddTape(TEXT("SPECIAL TEAMS"), Away.Unit[UKP], Home.Unit[UKP]);

	// a projection from the ratings: a point of overall is about a point on the field, the home side gets a field goal's
	// half; a 3-point favourite wins about 59 times in 100
	const double Spread = (Home.O.ovr - Away.O.ovr) * 1.1 + ((Home.O.off - Away.O.def) - (Away.O.off - Home.O.def)) * 0.35 + 1.5;
	const double HomeWin = 1.0 / (1.0 + FMath::Exp(-Spread / 8.0));
	const double Line = FMath::RoundToDouble(FMath::Abs(Spread) * 2.0) / 2.0;
	const FString Fav = Spread >= 0 ? Str(Home.Tm->abbr) : Str(Away.Tm->abbr);
	const FString LineText = Line < 0.5 ? FString(TEXT("PICK'EM")) : FString::Printf(TEXT("%s  -%s"), *Fav, *FString::SanitizeFloat(Line, 1).Replace(TEXT(".0"), TEXT("")));
	const int32 HomePct = FMath::Clamp(FMath::RoundToInt(HomeWin * 100), 1, 99);
	TSharedRef<SWidget> Proj = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1)[ Label(TEXT("LINE"), 9, Faint) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Label(LineText, 13, White90) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Label(FString::Printf(TEXT("%s %d%%"), *Str(Away.Tm->abbr), 100 - HomePct), 10, White90) ]
			+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center)[ Label(TEXT("WIN PROBABILITY"), 8, Faint) ]
			+ SHorizontalBox::Slot().AutoWidth()[ Label(FString::Printf(TEXT("%d%% %s"), HomePct, *Str(Home.Tm->abbr)), 10, White90) ]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(static_cast<float>(100 - HomePct))[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Vivid(Col(Away.Tm->colors.primary))) ]
				+ SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(2) ]
				+ SHorizontalBox::Slot().FillWidth(static_cast<float>(HomePct))[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Vivid(Col(Home.Tm->colors.primary))) ]
			]
		];

	// keys to the game: each side's pass and run attack against the other's defense (pass D = coverage and rush, run
	// D = the front seven), the edge in rating points to whichever side has it
	TSharedRef<SVerticalBox> Keys = SNew(SVerticalBox);
	auto KeyRow = [&](const FInfo& O, const FInfo& D, bool bPass)
	{
		const double Atk = bPass ? O.Passing : O.Rushing;
		const double Def = bPass ? (D.Coverage + D.PassRush) / 2 : (D.Unit[UDL] + D.Unit[ULB]) / 2;
		const int32 Edge = FMath::RoundToInt(Atk - Def);
		const FInfo& By = Edge >= 0 ? O : D;
		const FString EdgeText = Edge == 0 ? FString(TEXT("EVEN")) : FString::Printf(TEXT("%s +%d"), *Str(By.Tm->abbr), FMath::Abs(Edge));
		Keys->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBox).WidthOverride(4).HeightOverride(16)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Vivid(Col(O.Tm->colors.primary))) ] ]
			+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[ Label(FString::Printf(TEXT("%s %s  vs  %s %s"), *Str(O.Tm->abbr), bPass ? TEXT("air attack") : TEXT("ground game"), *Str(D.Tm->abbr), bPass ? TEXT("pass D") : TEXT("run D")), 10, Dim, "Regular") ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(EdgeText, 11, Edge == 0 ? Dim : FMath::Abs(Edge) >= 4 ? Gold : White90) ]
		];
	};
	KeyRow(Away, Home, true);
	KeyRow(Away, Home, false);
	KeyRow(Home, Away, true);
	KeyRow(Home, Away, false);

	TSharedRef<SVerticalBox> OptBox = SNew(SVerticalBox);
	for (int32 r = 0; r < Opts.Num(); ++r)
	{
		const bool bOn = SelCol == 1 && SelRow == r;
		std::function<void(int32)> Fn = Opts[r].Step;
		OptBox->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton).IsFocusable(false).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
			.OnClicked_Lambda([this, r, Fn, Rebuild] { ATagAudio::Ui(TEXT("UiValue")); SelCol = 1; SelRow = r; Fn(1); Rebuild(); return FReply::Handled(); })
			[
				SNew(TagTheme::SSelect).Selected(bOn).Fill(FLinearColor(1, 1, 1, 0.045f)).Padding(FMargin(12, 7))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[ Label(Opts[r].Label, 10, bOn ? White90 : Dim, "Regular") ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(bOn ? FString(TEXT("<  ")) : FString(), 10, Gold) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(Opts[r].Value, 11, bOn ? Gold : White90) ]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Label(bOn ? FString(TEXT("  >")) : FString(), 10, Gold) ]
				]
			]
		];
	}
	const bool bKickOn = SelCol == 1 && SelRow == Opts.Num();
	TSharedRef<SWidget> KickBtn = Click(SNew(SBorder).BorderImage(TagTheme::Round(6.f)).BorderBackgroundColor(bKickOn ? Gold : Gold * FLinearColor(0.8f, 0.8f, 0.8f, 1.f)).Padding(FMargin(14, 10))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(STextBlock).Font(Font(18, "Bold")).ColorAndOpacity(Ink).Text(Tx(TEXT("KICK OFF"))) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Ink).Padding(2)[ SNew(SGlyph).Owner(this).Glyph(EPadGlyph::Cross).Key(TEXT("Enter")).Size(22.f) ] ]
		], [Kick] { Kick(); });
	TSharedRef<SWidget> Middle = SNew(SBox).WidthOverride(360)
	[
		SNew(TagTheme::SPanel).Fill(Card).Focused(SelCol == 1).Radius(6.f).Padding(FMargin(0))
		[
			SNew(SBox).Padding(FMargin(16, 12))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Mark(*Away.Tm, 54.f) ]
					+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ SNew(STextBlock).Font(Font(30, "Bold")).ColorAndOpacity(Gold).Text(Tx(TEXT("VS"))) ]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[ Label(FString::Printf(TEXT("%s  at  %s"), *Str(Away.Tm->abbr), *Str(Home.Tm->abbr)), 10, Dim) ]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Mark(*Home.Tm, 54.f) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[ Heading(TEXT("Tale of the tape")) ]
				+ SVerticalBox::Slot().AutoHeight()[ Tape ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[ Heading(TEXT("Projection")) ]
				+ SVerticalBox::Slot().AutoHeight()[ Proj ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[ Heading(TEXT("Keys to the game")) ]
				+ SVerticalBox::Slot().AutoHeight()[ Keys ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)[ Heading(TEXT("Game options")) ]
				+ SVerticalBox::Slot().AutoHeight()[ OptBox ]
				+ SVerticalBox::Slot().FillHeight(1)[ SNullWidget::NullWidget ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(bKickOn ? FLinearColor::White : FLinearColor::Transparent).Padding(2)[ KickBtn ] ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
				[
					Click(SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.06f)).Padding(FMargin(12, 6))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Center).VAlign(VAlign_Center)[ Label(TEXT("BACK"), 12, Dim) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SGlyph).Owner(this).Glyph(EPadGlyph::Circle).Key(TEXT("Esc")).Size(20.f) ]
					], [this] { ShowMainMenu(); })
				]
			]
		]
	];

	// ---- the title bar and the button bar
	auto Hint = [this](EPadGlyph G, const TCHAR* Key, const TCHAR* What) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ SNew(SGlyph).Owner(this).Glyph(G).Key(Key).Size(22.f) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(*What ? 6.f : 1.f, 0, *What ? 20.f : 1.f, 0)[ Label(What, 11, White90, "Regular") ];
	};
	TSharedRef<SWidget> Hints = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::DLeftRight, TEXT("←→"), TEXT("Away / Options / Home")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L1, TEXT("Q"), TEXT("")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R1, TEXT("E"), TEXT("Change team")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Square, TEXT("R"), TEXT("Random team")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Triangle, TEXT("T"), TEXT("Swap home / away")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::L2, TEXT("G"), TEXT("Depth chart")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::R2, TEXT("F"), TEXT("Abilities")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Cross, TEXT("Enter"), TEXT("Kick off")) ]
		+ SHorizontalBox::Slot().AutoWidth()[ Hint(EPadGlyph::Circle, TEXT("Esc"), TEXT("Back")) ];

	TSharedRef<SWidget> Page = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 0, 0, 12))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
			[ SNew(STextBlock).Font(Font(30, "Bold")).ColorAndOpacity(FLinearColor::White).Text(Tx(bSelMulti ? TEXT("MULTIPLAYER") : bUserPlays ? TEXT("PLAY NOW") : TEXT("WATCH A GAME"))) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(14, 0, 0, 5)
			[ Label(bSelMulti ? TEXT("LOCAL  ·  PICK THE TEAMS  (THE SIDES ARE THE LOBBY'S)") : bUserPlays ? TEXT("EXHIBITION  ·  SELECT YOUR MATCHUP") : TEXT("CPU VS CPU BROADCAST  ·  SELECT THE MATCHUP"), 11, Dim) ]
			+ SHorizontalBox::Slot().FillWidth(1)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 0, 5)
			[ Label(FString::Printf(TEXT("%d TEAMS  ·  RATINGS RANKED LEAGUE-WIDE"), NTeams), 9, Faint) ]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)[ TeamPanel(false) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill).Padding(14, 0)[ Middle ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)[ TeamPanel(true) ]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0, 14, 0, 0))[ TagTheme::HintBar(Hints) ];

	// ---- the pad and the keys
	const int32 NOpts = Opts.Num();
	TArray<std::function<void(int32)>> OptSteps;
	OptSteps.Reserve(8); // (a TArray moves its items bytewise when it grows, which a std::function does not survive)
	for (const FOpt& O : Opts) OptSteps.Add(O.Step);
	auto OnKey = [this, Rebuild, Step, Kick, Random, NRows, NOpts, OptSteps](const FKey& K) -> bool
	{
		const bool bLeft = K == EKeys::Gamepad_DPad_Left || K == EKeys::Gamepad_LeftStick_Left || K == EKeys::Left || K == EKeys::A;
		const bool bRight = K == EKeys::Gamepad_DPad_Right || K == EKeys::Gamepad_LeftStick_Right || K == EKeys::Right || K == EKeys::D;
		const bool bUp = K == EKeys::Gamepad_DPad_Up || K == EKeys::Gamepad_LeftStick_Up || K == EKeys::Up || K == EKeys::W;
		const bool bDown = K == EKeys::Gamepad_DPad_Down || K == EKeys::Gamepad_LeftStick_Down || K == EKeys::Down || K == EKeys::S;
		const bool bPrev = K == EKeys::Gamepad_LeftShoulder || K == EKeys::Q;
		const bool bNext = K == EKeys::Gamepad_RightShoulder || K == EKeys::E;
		// the cursor clicks; an option stepped ticks up or down; another team swishes in
		if (bLeft || bRight) { ATagAudio::Ui(TEXT("UiMove")); SelCol = FMath::Clamp(SelCol + (bRight ? 1 : -1), 0, 2); Rebuild(); return true; }
		if (SelCol == 1)
		{
			if (bUp || bDown) { ATagAudio::Ui(TEXT("UiMove")); SelRow = FMath::Clamp(SelRow + (bDown ? 1 : -1), 0, NRows - 1); Rebuild(); return true; }
			if ((bPrev || bNext) && SelRow < NOpts) { ATagAudio::Ui(TEXT("UiValue"), 1.f, bNext ? 1.06f : 0.94f); OptSteps[SelRow](bNext ? 1 : -1); Rebuild(); return true; }
		}
		else if (bUp || bDown || bPrev || bNext)
		{
			ATagAudio::Ui(TEXT("UiTeam"));
			Step(SelCol == 2, (bDown || bNext) ? 1 : -1);
			Rebuild();
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Bottom || K == EKeys::Enter || K == EKeys::SpaceBar)
		{
			ATagAudio::Ui(SelCol == 1 && SelRow < NOpts ? TEXT("UiValue") : TEXT("UiSelect"));
			if (SelCol == 1 && SelRow < NOpts) { OptSteps[SelRow](1); Rebuild(); }
			else Kick();
			return true;
		}
		if (K == EKeys::Gamepad_Special_Right) { ATagAudio::Ui(TEXT("UiSelect")); Kick(); return true; }
		if (K == EKeys::Gamepad_RightTrigger || K == EKeys::F)
		{
			// the Superstars & X-Factors screen for the team under the pad (from the options: the user's own, or home)
			ATagAudio::Ui(TEXT("UiTab"));
			const bool bHomeT = SelCol == 2 || (SelCol == 1 && (!bSelUserPlays || bSelUserHome));
			const bool bUserGame = bSelUserPlays;
			ShowAbilities(bHomeT ? SelHome : SelAway, [this, bUserGame] { ShowTeamSelect(bUserGame); });
			return true;
		}
		if (K == EKeys::Gamepad_LeftTrigger || K == EKeys::G)
		{
			// the depth chart of the team under the pad
			ATagAudio::Ui(TEXT("UiTab"));
			const bool bHomeT = SelCol == 2 || (SelCol == 1 && (!bSelUserPlays || bSelUserHome));
			const bool bUserGame = bSelUserPlays;
			ShowDepthChart(bHomeT ? SelHome : SelAway, [this, bUserGame] { ShowTeamSelect(bUserGame); });
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Right || K == EKeys::Escape || K == EKeys::BackSpace)
		{
			ATagAudio::Ui(TEXT("UiBack"));
			if (bSelMulti) Director->ShowLobby(); // (back to the players' sides)
			else ShowMainMenu();
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Left || K == EKeys::R)
		{
			ATagAudio::Ui(TEXT("UiRandom"));
			if (SelCol != 2) Random(false);
			if (SelCol != 0) Random(true);
			Rebuild();
			return true;
		}
		if (K == EKeys::Gamepad_FaceButton_Top || K == EKeys::T)
		{
			// the teams change ends; the user stays with their team and the pad with the team it was on
			ATagAudio::Ui(TEXT("UiSwapSides"));
			Swap(SelHome, SelAway);
			bSelUserHome = !bSelUserHome;
			if (bSelMulti) Director->LobbySwapSides(); // (every player stays with his team)
			if (SelCol != 1) SelCol = 2 - SelCol;
			Rebuild();
			return true;
		}
		return false;
	};

	TSharedRef<SWidget> Root = SNew(SRoot).OnKey(OnKey)
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SWash).Left(Col(Away.Tm->colors.primary)).Right(Col(Home.Tm->colors.primary)) ]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[ SNew(SBox).Padding(FMargin(16, 12))[ Page ] ]
	];
	// (the overlay frames the focused widget in gold; this screen draws its own selection, as the play call does)
	CallRoot = Root;
	ShowModal(Root, true);
	GKey = OnKey;
	static bool bShots = FParse::Param(FCommandLine::Get(), TEXT("TagTeamSelect"));
	if (bShots)
	{
		bShots = false;
		StartShotScript(StaticCastSharedRef<SGridironOverlay>(AsShared()));
	}
}
