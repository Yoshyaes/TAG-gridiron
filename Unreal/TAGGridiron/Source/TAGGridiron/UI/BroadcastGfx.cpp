// The broadcast's graphics (UI/BroadcastGfx.h): the score bug and its states, the lower third and the stat tags, the
// drive summary and the third-down graphic, the matchup card; what in the game calls for each (Pump); -TagGfxShots.
#include "UI/BroadcastGfx.h"

#include "UI/GridironOverlay.h"
#include "UI/RosterUI.h"
#include "UI/TagTheme.h"
#include "Game/MatchDirector.h"
#include "Game/TagResult.h"
#include "Booth/TagBooth.h"
#include "Sim/Abilities.h"
#include "Sim/Game.h"
#include "Sim/League.h"
#include "Sim/PlaySim.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Fonts/FontCache.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "UnrealClient.h"
#include "Widgets/SLeafWidget.h"
#include <algorithm>
#include <string>
#include <vector>

// Two switches for what other parts of the game are adding (set to 1 once they are there):
//  - TAG_GFX_SHOW_VIEWS: AMatchDirector::HalftimeNow() / PostgameNow() (Game/TagBroadcast.h; each with a bOn): the
//    halftime and postgame shows hold the HUD's chips, toasts, hint and the score bug back as the pregame open does.
//  - TAG_GFX_BOOTH_ONAIR: ATagBooth::OnAir() (the line on the air, null when none), ATagBooth::RefereeOn(), and
//    tag::booth::Line::topic (the story's key): the graphics that go with the booth's lines (OnBoothLine), and a flag's
//    panel held for the referee's own line.
#ifndef TAG_GFX_SHOW_VIEWS
#define TAG_GFX_SHOW_VIEWS 1
#endif
#ifndef TAG_GFX_BOOTH_ONAIR
#define TAG_GFX_BOOTH_ONAIR 1
#endif

/** Where the graphics draw this pass: the paint's element list, or none (the fit check lays everything out and measures
 *  each text against its box, at the scale a resolution's DPI rule gives). Positions in slate units. */
struct FGfxCanvas
{
	FSlateWindowElementList* Out = nullptr;
	const FGeometry* Geo = nullptr;
	float Scale = 1.f;
	FVector2D Size = FVector2D(1920.0, 1080.0);
	int32 Layer = 0;
	float Alpha = 1.f;
	FVector2D Off = FVector2D::ZeroVector;
	TArray<FString>* Fails = nullptr;
	const TCHAR* Res = TEXT("");
	int32 Checked = 0;
};

// (a named namespace, not an anonymous one: in a unity build this file shares a translation unit with the overlay's)
namespace TagGfx
{
	// ---- the palette (linear). The bug's own look is dark glass and white type with each team's colour at its edge;
	// yellow is kept for the flag alone (the old bug's gold down box went amber for a flag: two yellows a shade apart)
	const FLinearColor Ink(0.004f, 0.006f, 0.013f, 1.f);
	const FLinearColor Cell(0.011f, 0.014f, 0.028f, 1.f);
	const FLinearColor Hair(1.f, 1.f, 1.f, 0.10f);
	const FLinearColor Paper(0.97f, 0.98f, 1.f, 1.f);
	const FLinearColor Muted(0.42f, 0.47f, 0.56f, 1.f);
	const FLinearColor Soft(0.70f, 0.75f, 0.83f, 1.f);
	const FLinearColor Track(0.02f, 0.55f, 1.f, 1.f);       // the tracking numbers' edge: a cool blue
	const FLinearColor FlagYellow(1.f, 0.76f, 0.f, 1.f);    // the flag, and only the flag
	const FLinearColor ZoneRed(0.78f, 0.02f, 0.025f, 1.f);
	const FLinearColor InkText(0.01f, 0.012f, 0.02f, 1.f);
	const FLinearColor DarkSub(0.16f, 0.18f, 0.23f, 1.f);   // a sub line on the white name plate

	// ---- the layout (slate units: the overlay is 1920 x 1080 of them at any 16:9 resolution)
	constexpr float BugH = 56.f, TeamW = 232.f, ClockW = 168.f, DownW = 196.f, BugW = TeamW * 2.f + ClockW + DownW, BugBottom = 30.f;
	constexpr float LogoW = 56.f, PcW = 60.f, TabH = 34.f, BandH = 74.f, BandGap = 12.f;
	constexpr float CardHalfW = 700.f, CardGap = 24.f, CardH = 600.f, CardHeadH = 46.f;

	const FSlateFontInfo& FontOf(EGfxFont F)
	{
		static TArray<FSlateFontInfo> Table;
		if (Table.Num() == 0)
		{
			struct FDef { int32 Size; const char* Weight; int32 Spacing; };
			// (in EGfxFont's order)
			static const FDef Defs[] = {
				{21, "BoldCondensed", 40}, {30, "Black", 0}, {23, "Black", 0}, {14, "BoldCondensed", 60}, {20, "BoldCondensed", 30}, {12, "BoldCondensed", 80},
				{19, "Black", 0}, {16, "Black", 60}, {15, "BoldCondensed", 40},
				{22, "Black", 10}, {12, "BoldCondensed", 120}, {19, "BoldCondensed", 40}, {11, "BoldCondensed", 140}, {24, "Black", 0}, {12, "BoldCondensed", 120},
				{22, "Black", 10}, {24, "Black", 0}, {10, "BoldCondensed", 160}, {24, "Black", 0}, {18, "BoldCondensed", 40},
				{19, "BoldCondensed", 220}, {44, "Black", 0}, {30, "Black", 0}, {11, "BoldCondensed", 160}, {14, "BoldCondensed", 80}, {17, "BoldCondensed", 30},
				{10, "BoldCondensed", 120}, {30, "Black", 0}, {88, "Black", 0}, {40, "Black", 0}, {40, "Black", 0}, {14, "BoldCondensed", 20}};
			static_assert(static_cast<int32>(UE_ARRAY_COUNT(Defs)) == static_cast<int32>(EGfxFont::Count), "a font for every EGfxFont");
			for (const FDef& D : Defs) Table.Add(TagTheme::Font(D.Size, D.Weight, D.Spacing));
		}
		return Table[FMath::Clamp(static_cast<int32>(F), 0, Table.Num() - 1)];
	}

	// ---- timing: every move eased on the clock's seconds (the same on any frame rate)
	inline float Clamp01(float X) { return FMath::Clamp(X, 0.f, 1.f); }
	inline float Out3(float X) { X = Clamp01(X); return 1.f - (1.f - X) * (1.f - X) * (1.f - X); }
	inline float In2(float X) { X = Clamp01(X); return X * X; }
	inline float Phase(double Now, double T0, float Dur) { return Dur > 0.f ? Clamp01(static_cast<float>((Now - T0) / Dur)) : 1.f; }
	/** Shown 0..1: eased in over InDur from T0, and (bOut) eased out over OutDur from OutT0. */
	inline float InOut(double Now, double T0, float InDur, bool bOut, double OutT0, float OutDur)
	{
		const float In = Out3(Phase(Now, T0, InDur));
		return bOut ? In * (1.f - In2(Phase(Now, OutT0, OutDur))) : In;
	}

	// ---- colour and words
	FLinearColor FromHex(const std::string& H)
	{
		FString S(UTF8_TO_TCHAR(H.c_str()));
		S.RemoveFromStart(TEXT("#"));
		return FLinearColor(FColor::FromHex(S));
	}
	/** A team colour that shows on the dark bug (a near-black one gives way to its second colour). */
	FLinearColor Vivid(const FLinearColor& P, const FLinearColor& P2)
	{
		if (P.GetLuminance() >= 0.02f) return P;
		return P2.GetLuminance() >= 0.03f ? P2 : FLinearColor(0.30f, 0.32f, 0.36f);
	}
	/** Type that reads on a colour: near black on a light one, white on a dark one (judged in display terms). */
	FLinearColor OnColor(const FLinearColor& Bg)
	{
		const FColor C = Bg.ToFColor(true);
		return (0.299f * C.R + 0.587f * C.G + 0.114f * C.B) / 255.f > 0.6f ? InkText : Paper;
	}
	FLinearColor WithA(const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, C.A * A); }
	FString Up(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())).ToUpper(); }
	const FString& Dot()
	{
		static const FString D(TEXT("  ·  "));
		return D;
	}
	FString Ordinal(int32 N)
	{
		static const TCHAR* const W[] = {TEXT("1ST"), TEXT("2ND"), TEXT("3RD"), TEXT("4TH")};
		return N >= 1 && N <= 4 ? FString(W[N - 1]) : FString::Printf(TEXT("%dTH"), N);
	}
	FString Mmss(int32 Secs)
	{
		Secs = FMath::Max(0, Secs);
		return FString::Printf(TEXT("%d:%02d"), Secs / 60, Secs % 60);
	}
	/** Where a drive started ("OWN 25", "OPP 40", "MIDFIELD"). */
	FString StartWord(double BallOn)
	{
		const int32 B = FMath::RoundToInt(BallOn);
		return B == 50 ? FString(TEXT("MIDFIELD")) : B < 50 ? FString::Printf(TEXT("OWN %d"), B) : FString::Printf(TEXT("OPP %d"), 100 - B);
	}
	/** The bug's index for a team (0 the visitors, 1 the home team) and back. */
	inline int32 SideOf(tag::TeamKey K) { return K == tag::TeamKey::Home ? 1 : 0; }
	inline tag::TeamKey KeyOf(int32 S) { return S == 1 ? tag::TeamKey::Home : tag::TeamKey::Away; }

	// ---- the type: shaped once, drawn as shaped glyphs
	FShapedGlyphSequencePtr ShapeAt(const FString& S, EGfxFont F, float Scale, FVector2D& OutSize)
	{
		OutSize = FVector2D::ZeroVector;
		if (S.IsEmpty() || Scale <= 0.f || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer()) return nullptr;
		const TSharedRef<FSlateFontCache> Fc = FSlateApplication::Get().GetRenderer()->GetFontCache();
		const FShapedGlyphSequenceRef R = Fc->ShapeUnidirectionalText(S, FontOf(F), Scale, TextBiDi::ETextDirection::LeftToRight, ETextShapingMethod::KerningOnly);
		OutSize = FVector2D(R->GetMeasuredWidth() / Scale, R->GetMaxTextHeight() / Scale);
		return R;
	}

	// ---- drawing (nothing drawn when the canvas is the fit check's)
	void Rect(FGfxCanvas& C, const FVector2D& TL, const FVector2D& Sz, const FLinearColor& Col, int32 DL, float Radius = 0.f)
	{
		if (!C.Out || Sz.X <= 0.0 || Sz.Y <= 0.0) return;
		const float A = Col.A * C.Alpha;
		if (A <= 0.003f) return;
		FSlateDrawElement::MakeBox(*C.Out, C.Layer + DL, C.Geo->ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(TL + C.Off))),
			Radius > 0.f ? TagTheme::Round(Radius) : TagTheme::White(), ESlateDrawEffect::None, FLinearColor(Col.R, Col.G, Col.B, A));
	}
	void Image(FGfxCanvas& C, const FSlateBrush* B, const FVector2D& TL, const FVector2D& Sz, int32 DL, float A = 1.f)
	{
		if (!C.Out || !B || A * C.Alpha <= 0.003f) return;
		FSlateDrawElement::MakeBox(*C.Out, C.Layer + DL, C.Geo->ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(TL + C.Off))), B,
			ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, A * C.Alpha));
	}
	void Clip(FGfxCanvas& C, const FVector2D& TL, const FVector2D& Sz)
	{
		if (C.Out) C.Out->PushClip(FSlateClippingZone(C.Geo->ToPaintGeometry(FVector2f(FVector2D(FMath::Max(1.0, Sz.X), FMath::Max(1.0, Sz.Y))), FSlateLayoutTransform(FVector2f(TL + C.Off)))));
	}
	void Unclip(FGfxCanvas& C)
	{
		if (C.Out) C.Out->PopClip();
	}
	/** The full text's size at the canvas's scale (laying out a box around it). */
	FVector2D Measure(FGfxCanvas& C, FGfxText& T, const FString& S, EGfxFont F)
	{
		if (S.IsEmpty()) return FVector2D::ZeroVector;
		if (C.Out)
		{
			T.Shape(S, F, C.Scale);
			return T.FullSize;
		}
		FVector2D Sz;
		ShapeAt(S, F, C.Scale, Sz);
		return Sz;
	}
	/** Text in a box (Align 0 left, 1 centre, 2 right; centred top to bottom). A text wider than its box is drawn cut with
	 *  an ellipsis, and the fit check reports it. Returns the size drawn. */
	FVector2D Text(FGfxCanvas& C, FGfxText& T, const FString& S, EGfxFont F, const FVector2D& TL, const FVector2D& Box, int32 Align, const FLinearColor& Col, int32 DL)
	{
		if (S.IsEmpty()) return FVector2D::ZeroVector;
		if (!C.Out)
		{
			FVector2D Sz;
			ShapeAt(S, F, C.Scale, Sz);
			++C.Checked;
			if (C.Fails && Sz.X > Box.X + 0.5)
				C.Fails->Add(FString::Printf(TEXT("%s \"%s\" %.0f wide in a %.0f box"), C.Res, *S, Sz.X, Box.X));
			return Sz;
		}
		if (!T.Shape(S, F, C.Scale)) return FVector2D::ZeroVector;
		FVector2D Sz;
		const FShapedGlyphSequencePtr& Glyphs = T.For(static_cast<float>(Box.X), Sz);
		if (!Glyphs.IsValid()) return Sz;
		const float A = Col.A * C.Alpha;
		if (A <= 0.003f) return Sz;
		double X = Align == 0 ? TL.X : Align == 1 ? TL.X + (Box.X - Sz.X) * 0.5 : TL.X + Box.X - Sz.X;
		double Y = TL.Y + (Box.Y - Sz.Y) * 0.5;
		// (on whole pixels: type between them goes soft)
		X = FMath::RoundToDouble((X + C.Off.X) * C.Scale) / C.Scale - C.Off.X;
		Y = FMath::RoundToDouble((Y + C.Off.Y) * C.Scale) / C.Scale - C.Off.Y;
		FSlateDrawElement::MakeShapedText(*C.Out, C.Layer + DL, C.Geo->ToPaintGeometry(FVector2f(Sz), FSlateLayoutTransform(FVector2f(FVector2D(X, Y) + C.Off))),
			Glyphs.ToSharedRef(), ESlateDrawEffect::None, FLinearColor(Col.R, Col.G, Col.B, A), FLinearColor::Transparent);
		return Sz;
	}

	/** A tracking number's plate: its label small over its value, a blue edge. Its width (laid out before it is drawn). */
	float TagPlateW(FGfxCanvas& C, FGfxText& Lab, FGfxText& Val, const FGfxStatTag& T)
	{
		return static_cast<float>(FMath::Max(Measure(C, Lab, T.Label, EGfxFont::TagLabel).X, Measure(C, Val, T.Value, EGfxFont::TagValue).X)) + 44.f;
	}
	void TagPlate(FGfxCanvas& C, FGfxText& Lab, FGfxText& Val, const FGfxStatTag& T, const FVector2D& TL, float W, float H, int32 DL)
	{
		Rect(C, TL + FVector2D(0, 3), FVector2D(W, H), FLinearColor(0, 0, 0, 0.3f), DL);
		Rect(C, TL, FVector2D(W, H), WithA(Ink, 0.94f), DL + 1);
		Rect(C, TL, FVector2D(4, H), Track, DL + 2);
		Text(C, Lab, T.Label, EGfxFont::TagLabel, TL + FVector2D(22, 8), FVector2D(W - 30, 16), 0, Soft, DL + 3);
		Text(C, Val, T.Value, EGfxFont::TagValue, TL + FVector2D(22, 24), FVector2D(W - 30, H - 28), 0, Paper, DL + 3);
	}

	/** The picker (FBroadcastGfx::PickStatTags), leaving out one field (a number already on screen). */
	FGfxStatTags Pick(const tag::PlayResult& R, const tag::Ball& Bl, float TopMph, const FString* Skip)
	{
		TArray<FGfxStatTag, TInlineAllocator<8>> Cands;
		auto Add = [&Cands, Skip](float Score, const TCHAR* Label, const FString& Value, const TCHAR* Field, double Raw)
		{
			if (Score < 1.f || (Skip && Skip->Equals(Field))) return;
			FGfxStatTag T;
			T.Label = Label;
			T.Value = Value;
			T.Field = Field;
			T.Raw = Raw;
			T.Score = Score;
			Cands.Add(T);
		};
		const bool bPass = R.kind == tag::PlayKind::Pass;
		const bool bCaught = bPass && R.complete && !R.interception && R.receiver != nullptr;
		// the odds the sim rolled the catch with: his catch chance once a defender's breakup chance has passed
		if (bCaught && Bl.fateP >= 0.0)
		{
			const double P = Bl.fateP * (1.0 - FMath::Clamp(Bl.fatePbu, 0.0, 1.0));
			if (P < 0.5) Add(1.f + static_cast<float>((0.5 - P) / 0.15), TEXT("CATCH PROBABILITY"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(P * 100.0)), TEXT("Ball::fateP*(1-fatePbu)"), P);
		}
		// separation at the catch (NFL tight window: under a yard; wide open: past five and a half)
		if (bCaught && R.catchSep >= 0.0)
		{
			if (R.catchSep < 1.0)
				Add(1.f + static_cast<float>((1.0 - R.catchSep) / 0.4), TEXT("SEPARATION AT THE CATCH"), FString::Printf(TEXT("%.1f YD"), R.catchSep), TEXT("PlayResult::catchSep"), R.catchSep);
			else if (R.catchSep > 5.5)
				Add(1.f + static_cast<float>((R.catchSep - 5.5) / 2.0), TEXT("SEPARATION AT THE CATCH"), FString::Printf(TEXT("%.1f YD"), R.catchSep), TEXT("PlayResult::catchSep"), R.catchSep);
		}
		if (bCaught && R.airYards >= 20)
			Add(1.f + (R.airYards - 20) / 10.f, TEXT("AIR YARDS"), FString::Printf(TEXT("%d YD"), R.airYards), TEXT("PlayResult::airYards"), R.airYards);
		if (bCaught && R.yac >= 10)
			Add(1.f + (R.yac - 10) / 8.f, TEXT("YARDS AFTER THE CATCH"), FString::Printf(TEXT("%d YD"), R.yac), TEXT("PlayResult::yac"), R.yac);
		// the throw: out quick on a big gain, or held a long time (the league's is about 2.8 s)
		if (bPass && !R.thrownAway && R.timeToThrow > 0.0)
		{
			if (R.timeToThrow < 2.3 && bCaught && R.yards >= 15)
				Add(1.f + static_cast<float>((2.3 - R.timeToThrow) / 0.3), TEXT("TIME TO THROW"), FString::Printf(TEXT("%.2f S"), R.timeToThrow), TEXT("PlayResult::timeToThrow"), R.timeToThrow);
			else if (R.timeToThrow > 4.2)
				Add(1.f + static_cast<float>((R.timeToThrow - 4.2) / 0.8), TEXT("TIME TO THROW"), FString::Printf(TEXT("%.2f S"), R.timeToThrow), TEXT("PlayResult::timeToThrow"), R.timeToThrow);
		}
		// a sack: the play's time to it (no throw: timeToThrow is the play's own length)
		if (R.kind == tag::PlayKind::Sack && R.timeToThrow > 0.0 && R.timeToThrow < 3.0)
			Add(1.f + static_cast<float>((3.0 - R.timeToThrow) / 0.4), TEXT("TIME TO SACK"), FString::Printf(TEXT("%.2f S"), R.timeToThrow), TEXT("PlayResult::timeToThrow"), R.timeToThrow);
		if (R.brokenTackles >= 2)
			Add(1.f + (R.brokenTackles - 2) * 0.8f, TEXT("BROKEN TACKLES"), FString::FromInt(R.brokenTackles), TEXT("PlayResult::brokenTackles"), R.brokenTackles);
		if (TopMph >= 20.f && (bCaught || R.kind == tag::PlayKind::Run || R.kind == tag::PlayKind::Scramble))
			Add(1.f + (TopMph - 20.f) / 1.2f, TEXT("BALL CARRIER TOP SPEED"), FString::Printf(TEXT("%.1f MPH"), TopMph), TEXT("Ent::vel (his most with the ball)"), TopMph);
		Cands.Sort([](const FGfxStatTag& X, const FGfxStatTag& Y) { return X.Score > Y.Score; });
		FGfxStatTags Out;
		if (Cands.Num() > 0)
		{
			Out.Tag[0] = Cands[0];
			Out.Num = 1;
		}
		// a second only when it stands out too (and is another number)
		for (int32 i = 1; i < Cands.Num() && Out.Num < 2; ++i)
			if (Cands[i].Score >= 1.6f && !Cands[i].Field.Equals(Cands[0].Field))
			{
				Out.Tag[1] = Cands[i];
				Out.Num = 2;
			}
		return Out;
	}

	void LogTags(const TCHAR* Where, const FGfxStatTag* T, int32 N)
	{
		for (int32 i = 0; i < N; ++i)
			UE_LOG(LogTemp, Display, TEXT("TAG gfx stat tag (%s): %s %s  [%s = %.3f, score %.2f]"), Where, *T[i].Label, *T[i].Value, *T[i].Field, T[i].Raw, T[i].Score);
	}

	/** The word for how a drive ended, from the book's last lines since it started (a score is known from the score). */
	FString DriveEnd(const tag::Game& G, int32 LogStart)
	{
		for (int32 i = static_cast<int32>(G.log.size()) - 1; i >= 0 && i >= LogStart; --i)
		{
			const tag::PlayLog& L = G.log[i];
			if (L.big == tag::LogTag::Info) continue;
			const FString Ds(UTF8_TO_TCHAR(L.desc.c_str()));
			if (Ds.Contains(TEXT("Turnover on downs"))) return TEXT("TURNOVER ON DOWNS");
			if (L.big == tag::LogTag::Safety || Ds.Contains(TEXT("SAFETY"), ESearchCase::CaseSensitive)) return TEXT("SAFETY");
			if (Ds.Contains(TEXT("punt"))) return TEXT("PUNT");
			if (Ds.Contains(TEXT("intercept"))) return TEXT("INTERCEPTION");
			if (Ds.Contains(TEXT("fumble"))) return TEXT("FUMBLE");
			if (Ds.Contains(TEXT("field goal"))) return TEXT("MISSED FIELD GOAL");
			if (L.big == tag::LogTag::Turnover) return TEXT("TURNOVER");
			break;
		}
		return TEXT("TURNOVER");
	}

	// ---- the book's numbers in the graphics' words (the box, the player of the game, the booth's lines)
	const tag::Player* PlayerById(const tag::Game& G, int32 Pid)
	{
		const tag::Team* Ts[2] = {G.away, G.home};
		for (const tag::Team* T : Ts)
			if (T)
				for (const auto& P : T->roster)
					if (P && P->id == Pid) return P.get();
		return nullptr;
	}
	const tag::PlayerLine* LineOf(const tag::Game& G, int32 Pid)
	{
		for (const tag::PlayerLine& L : G.stats.all())
			if (L.pid == Pid) return &L;
		return nullptr;
	}
	FGfxTeamId TeamIdFor(const tag::Team& T)
	{
		FGfxTeamId I;
		I.City = Up(T.city);
		I.Name = Up(T.name);
		I.Abbr = Up(T.abbr);
		const FLinearColor P = FromHex(T.colors.primary), P2 = FromHex(T.colors.secondary);
		I.Color = Vivid(P, P2);
		I.Color2 = P2;
		I.Logo = RosterUI::Logo(T);
		return I;
	}
	/** "#12  M. RIVERS" (the book's name, his number from the roster). */
	FString NumName(const tag::Game& G, const tag::PlayerLine& L)
	{
		const tag::Player* P = PlayerById(G, L.pid);
		const FString Nm = FString(UTF8_TO_TCHAR(L.name.c_str())).ToUpper();
		return P ? FString::Printf(TEXT("#%d  %s"), P->number, *Nm) : Nm;
	}
	FString PassLine(const tag::PlayerLine& L)
	{
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("%d/%d"), L.passComp, L.passAtt));
		Parts.Add(FString::Printf(TEXT("%d YDS"), L.passYds));
		if (L.passTD > 0) Parts.Add(FString::Printf(TEXT("%d TD"), L.passTD));
		if (L.passInt > 0) Parts.Add(FString::Printf(TEXT("%d INT"), L.passInt));
		return FString::Join(Parts, *Dot());
	}
	FString RushLine(const tag::PlayerLine& L)
	{
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("%d CAR"), L.rushAtt));
		Parts.Add(FString::Printf(TEXT("%d YDS"), L.rushYds));
		if (L.rushAtt > 0) Parts.Add(FString::Printf(TEXT("%.1f AVG"), L.rushYds / static_cast<double>(L.rushAtt)));
		if (L.rushTD > 0) Parts.Add(FString::Printf(TEXT("%d TD"), L.rushTD));
		return FString::Join(Parts, *Dot());
	}
	FString RecLine(const tag::PlayerLine& L)
	{
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("%d REC"), L.rec));
		Parts.Add(FString::Printf(TEXT("%d YDS"), L.recYds));
		if (L.recTD > 0) Parts.Add(FString::Printf(TEXT("%d TD"), L.recTD));
		return FString::Join(Parts, *Dot());
	}
	FString DefLine(const tag::PlayerLine& L)
	{
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("%d TKL"), L.tackles));
		if (L.sacks > 0) Parts.Add(FString::Printf(TEXT("%d SK"), L.sacks));
		if (L.ints > 0) Parts.Add(FString::Printf(TEXT("%d INT"), L.ints));
		if (L.ff > 0) Parts.Add(FString::Printf(TEXT("%d FF"), L.ff));
		if (L.pbu > 0) Parts.Add(FString::Printf(TEXT("%d PD"), L.pbu));
		return FString::Join(Parts, *Dot());
	}
	/** A defender's day in points (the box's defensive leader; the player of the game's defensive part). */
	double DefScore(const tag::PlayerLine& L)
	{
		return L.tackles + 4.0 * L.sacks + 5.0 * L.ints + 3.0 * (L.ff + L.fr) + 1.5 * (L.pbu + L.tfl) + 6.0 * L.defTD;
	}
	/** Everything a man did, in fantasy-style points (FBroadcastGfx::PlayerOfGameFor). */
	double GamePoints(const tag::PlayerLine& L)
	{
		return L.passYds / 25.0 + 4.0 * L.passTD - 2.0 * L.passInt + (L.rushYds + L.recYds) / 10.0 + 6.0 * (L.rushTD + L.recTD) + 0.5 * L.rec - 2.0 * L.fumblesLost +
			DefScore(L) + 3.0 * L.fgm + L.xpm;
	}
	/** Each period's points (-1 for one not played): the score at its end (the book's last line in it; the game's own
	 *  for the period on now) less the one before. */
	void PointsByQ(const tag::Game& G, int32 NumQ, FGfxTeamTotals* Tot)
	{
		for (int32 s = 0; s < 2; ++s)
		{
			int32 Prev = 0;
			for (int32 p = 1; p <= 5; ++p)
			{
				if (p > NumQ || p > G.q) { Tot[s].ByQ[p - 1] = -1; continue; }
				int32 End = -1;
				if (p >= G.q) End = s == 1 ? G.score.home : G.score.away;
				else
					for (const tag::PlayLog& L : G.log)
						if (L.q == p) End = s == 1 ? L.score.home : L.score.away;
				if (End < 0) End = Prev;
				Tot[s].ByQ[p - 1] = FMath::Max(0, End - Prev);
				Prev = End;
			}
		}
	}
	/** Each side's leaders: the passer, the runner and the receiver with the most yards, the defender with the most
	 *  DefScore. */
	void LeadersFor(const tag::Game& G, const tag::Team& T, FGfxLeader* Out)
	{
		const tag::PlayerLine* Best[4] = {nullptr, nullptr, nullptr, nullptr};
		for (const tag::PlayerLine* L : G.stats.forTeam(T.abbr))
		{
			if (L->passAtt > 0 && (!Best[0] || L->passYds > Best[0]->passYds)) Best[0] = L;
			if (L->rushAtt > 0 && (!Best[1] || L->rushYds > Best[1]->rushYds)) Best[1] = L;
			if (L->rec > 0 && (!Best[2] || L->recYds > Best[2]->recYds)) Best[2] = L;
			if (DefScore(*L) > 0.0 && (!Best[3] || DefScore(*L) > DefScore(*Best[3]))) Best[3] = L;
		}
		static const TCHAR* const Roles[4] = {TEXT("PASSING"), TEXT("RUSHING"), TEXT("RECEIVING"), TEXT("DEFENSE")};
		for (int32 i = 0; i < 4; ++i)
		{
			Out[i] = FGfxLeader();
			Out[i].Role = Roles[i];
			if (!Best[i]) continue;
			Out[i].bOn = true;
			Out[i].Name = NumName(G, *Best[i]);
			Out[i].Line = i == 0 ? PassLine(*Best[i]) : i == 1 ? RushLine(*Best[i]) : i == 2 ? RecLine(*Best[i]) : DefLine(*Best[i]);
		}
	}
	FGfxBoxStats BoxFor(const tag::Game& G, bool bFinal)
	{
		FGfxBoxStats B;
		const tag::Team* Ts[2] = {G.away, G.home};
		B.NumQ = bFinal ? (G.q >= 5 ? 5 : 4) : 2;
		for (int32 s = 0; s < 2; ++s)
		{
			if (!Ts[s]) return B;
			const tag::TeamKey K = s == 1 ? tag::TeamKey::Home : tag::TeamKey::Away;
			const tag::TeamLine& T = G.team[K];
			const tag::TeamLine& O = G.team[tag::other(K)];
			B.Team[s] = TeamIdFor(*Ts[s]);
			FGfxTeamTotals& Tt = B.Tot[s];
			Tt.Score = G.score[K];
			Tt.Yards = T.yards;
			Tt.PassYds = T.passYds;
			Tt.RushYds = T.rushYds;
			Tt.FirstDowns = T.firstDowns;
			Tt.ThirdConv = T.thirdConv;
			Tt.ThirdAtt = T.thirdAtt;
			Tt.Turnovers = T.ints + T.fumblesLost;
			Tt.TopSecs = FMath::RoundToInt(T.top);
			Tt.Sacks = O.sacks; // (TeamLine::sacks is the times its passer went down: the other side's are this side's sacks)
			Tt.Penalties = T.penalties;
			Tt.PenYds = T.penYds;
			LeadersFor(G, *Ts[s], B.Leaders[s]);
		}
		PointsByQ(G, B.NumQ, B.Tot);
		const int32 M = B.Tot[0].Score - B.Tot[1].Score;
		const FString& Lead = M > 0 ? B.Team[0].City : B.Team[1].City;
		const int32 Hi = FMath::Max(B.Tot[0].Score, B.Tot[1].Score), Lo = FMath::Min(B.Tot[0].Score, B.Tot[1].Score);
		B.Kicker = bFinal ? (B.NumQ >= 5 ? FString(TEXT("FINAL")) + Dot() + TEXT("OVERTIME") : FString(TEXT("FINAL"))) : FString(TEXT("HALFTIME"));
		B.Sub = M == 0 ? FString::Printf(TEXT("TIED %d-%d"), Hi, Lo) : FString::Printf(TEXT("%s %s %d-%d"), *Lead, bFinal ? TEXT("WINS") : TEXT("LEADS"), Hi, Lo);
		return B;
	}
	const TCHAR* PosWord(tag::Pos P)
	{
		switch (P)
		{
		case tag::Pos::QB: return TEXT("QUARTERBACK");
		case tag::Pos::RB: return TEXT("RUNNING BACK");
		case tag::Pos::FB: return TEXT("FULLBACK");
		case tag::Pos::WR: return TEXT("WIDE RECEIVER");
		case tag::Pos::TE: return TEXT("TIGHT END");
		case tag::Pos::OT: return TEXT("TACKLE");
		case tag::Pos::OG: return TEXT("GUARD");
		case tag::Pos::C: return TEXT("CENTER");
		case tag::Pos::DE: return TEXT("DEFENSIVE END");
		case tag::Pos::DT: return TEXT("DEFENSIVE TACKLE");
		case tag::Pos::LB: return TEXT("LINEBACKER");
		case tag::Pos::CB: return TEXT("CORNERBACK");
		case tag::Pos::S: return TEXT("SAFETY");
		case tag::Pos::K: return TEXT("KICKER");
		case tag::Pos::P: return TEXT("PUNTER");
		}
		return TEXT("");
	}
	/** The booth's player lower third: the man's line of the kind the booth is talking about ("qb", "rb", "wr",
	 *  "stat" his best, "sk" / "dbint" his defense). False: the book has no line for him. */
	bool BoothLineL3(const tag::Game& G, int32 Pid, const FString& Kind, FGfxLowerThird& Out)
	{
		const tag::PlayerLine* L = LineOf(G, Pid);
		if (!L) return false;
		const bool bHome = G.home && L->team == G.home->abbr;
		const tag::Team* T = bHome ? G.home : G.away;
		Out = FGfxLowerThird();
		Out.Side = bHome ? 1 : 0;
		Out.Name = NumName(G, *L);
		Out.Sub = FString(UTF8_TO_TCHAR(L->pos.c_str())).ToUpper() + (T ? Dot() + Up(T->city) : FString());
		if (Kind == TEXT("qb") || Kind == TEXT("qbint")) Out.Stats = PassLine(*L);
		else if (Kind == TEXT("rb")) Out.Stats = RushLine(*L);
		else if (Kind == TEXT("wr")) Out.Stats = RecLine(*L);
		else if (Kind == TEXT("sk") || Kind == TEXT("dbint")) Out.Stats = DefLine(*L);
		else Out.Stats = L->rec > 0 && L->recYds >= L->rushYds ? RecLine(*L) : L->rushAtt > 0 ? RushLine(*L) : L->passAtt > 0 ? PassLine(*L) : DefLine(*L);
		return true;
	}

	// ---- a flag's words (the referee's: the foul as he says it, its yards as the rule book has them)
	struct FFoulWords { const TCHAR* Log; const TCHAR* Words; int32 Yards; bool bAutoFirst; };
	const FFoulWords* FoulWords(const FString& LogName)
	{
		static const FFoulWords Table[] = {
			{TEXT("Offensive Holding"), TEXT("HOLDING"), 10, false}, {TEXT("Holding"), TEXT("HOLDING"), 10, false},
			{TEXT("Defensive Holding"), TEXT("DEFENSIVE HOLDING"), 5, true}, {TEXT("Defensive Pass Interference"), TEXT("PASS INTERFERENCE"), 0, true},
			{TEXT("Offensive Pass Interference"), TEXT("OFFENSIVE PASS INTERFERENCE"), 10, false}, {TEXT("Illegal Contact"), TEXT("ILLEGAL CONTACT"), 5, true},
			{TEXT("Roughing the Passer"), TEXT("ROUGHING THE PASSER"), 15, true}, {TEXT("Face Mask"), TEXT("FACE MASK"), 15, true},
			{TEXT("Unnecessary Roughness"), TEXT("UNNECESSARY ROUGHNESS"), 15, true}, {TEXT("Illegal Block in the Back"), TEXT("ILLEGAL BLOCK IN THE BACK"), 10, false},
			{TEXT("Illegal Use of Hands"), TEXT("ILLEGAL USE OF HANDS"), 10, false}, {TEXT("Intentional Grounding"), TEXT("INTENTIONAL GROUNDING"), 10, false},
			{TEXT("False Start"), TEXT("FALSE START"), 5, false}, {TEXT("Delay of Game"), TEXT("DELAY OF GAME"), 5, false}, {TEXT("Offside"), TEXT("OFFSIDE"), 5, false},
			{TEXT("Encroachment"), TEXT("ENCROACHMENT"), 5, false}, {TEXT("Neutral Zone Infraction"), TEXT("NEUTRAL ZONE INFRACTION"), 5, false},
			{TEXT("Illegal Formation"), TEXT("ILLEGAL FORMATION"), 5, false}, {TEXT("Illegal Shift"), TEXT("ILLEGAL SHIFT"), 5, false},
			{TEXT("Too Many Men on the Field"), TEXT("TOO MANY MEN ON THE FIELD"), 5, false}};
		for (const FFoulWords& F : Table)
			if (LogName.Equals(F.Log)) return &F;
		return nullptr;
	}
	FString DownWord(int32 Down)
	{
		return Ordinal(FMath::Clamp(Down, 1, 4)) + TEXT(" DOWN");
	}

	/** The leaf widget the graphics paint in (it fills the overlay; nothing in it takes the mouse). */
	class SGfxLayer : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGfxLayer) {}
		SLATE_END_ARGS()
		void Construct(const FArguments& InArgs, TWeakPtr<FBroadcastGfx> InGfx)
		{
			Gfx = InGfx;
			SetVisibility(EVisibility::HitTestInvisible);
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const TSharedPtr<FBroadcastGfx> P = Gfx.Pin();
			return P.IsValid() ? P->Paint(OutDrawElements, LayerId, AllottedGeometry) : LayerId;
		}
	private:
		TWeakPtr<FBroadcastGfx> Gfx;
	};
}

// The helpers by name in each function below: a block's own declarations, so that a name another file of a unity build
// put into the global namespace ("using namespace X;" at file scope, an anonymous namespace) never makes a call
// ambiguous (a "using namespace TagGfx;" in the block would only add these alongside those).
#define TAG_GFX_NAMES \
	using TagGfx::Ink; using TagGfx::Cell; using TagGfx::Hair; using TagGfx::Paper; using TagGfx::Muted; using TagGfx::Soft; using TagGfx::Track; \
	using TagGfx::FlagYellow; using TagGfx::ZoneRed; using TagGfx::InkText; using TagGfx::DarkSub; \
	using TagGfx::BugH; using TagGfx::TeamW; using TagGfx::ClockW; using TagGfx::DownW; using TagGfx::BugW; using TagGfx::BugBottom; \
	using TagGfx::LogoW; using TagGfx::PcW; using TagGfx::TabH; using TagGfx::BandH; using TagGfx::BandGap; \
	using TagGfx::CardHalfW; using TagGfx::CardGap; using TagGfx::CardH; using TagGfx::CardHeadH; \
	using TagGfx::Out3; using TagGfx::In2; using TagGfx::Phase; using TagGfx::InOut; using TagGfx::FromHex; using TagGfx::Vivid; \
	using TagGfx::OnColor; using TagGfx::WithA; using TagGfx::Up; using TagGfx::Dot; using TagGfx::Ordinal; using TagGfx::Mmss; \
	using TagGfx::StartWord; using TagGfx::SideOf; using TagGfx::KeyOf; using TagGfx::Rect; using TagGfx::Image; using TagGfx::Clip; \
	using TagGfx::Unclip; using TagGfx::Measure; using TagGfx::Text; using TagGfx::TagPlateW; using TagGfx::TagPlate; using TagGfx::Pick; \
	using TagGfx::LogTags; using TagGfx::DriveEnd; \
	using TagGfx::PlayerById; using TagGfx::LineOf; using TagGfx::TeamIdFor; using TagGfx::NumName; using TagGfx::PassLine; using TagGfx::RushLine; \
	using TagGfx::RecLine; using TagGfx::DefLine; using TagGfx::DefScore; using TagGfx::GamePoints; using TagGfx::PointsByQ; using TagGfx::LeadersFor; \
	using TagGfx::BoxFor; using TagGfx::PosWord; using TagGfx::BoothLineL3; using TagGfx::FFoulWords; using TagGfx::FoulWords; using TagGfx::DownWord

// ------------------------------------------------------------------ FGfxText
bool FGfxText::Shape(const FString& S, EGfxFont InFont, float InScale)
{
	if (Full.IsValid() && Font == static_cast<int32>(InFont) && Scale == InScale && Str.Equals(S, ESearchCase::CaseSensitive)) return true;
	Str = S;
	Font = static_cast<int32>(InFont);
	Scale = InScale;
	Cut.Reset();
	CutW = -1.f;
	CutSize = FVector2D::ZeroVector;
	Full = TagGfx::ShapeAt(S, InFont, InScale, FullSize);
	return Full.IsValid();
}

const FShapedGlyphSequencePtr& FGfxText::For(float MaxW, FVector2D& OutSize)
{
	if (MaxW <= 0.f || FullSize.X <= MaxW + 0.5f || Str.Len() < 2)
	{
		OutSize = FullSize;
		return Full;
	}
	if (!Cut.IsValid() || CutW != MaxW)
	{
		// (once, when it changes: the longest start of it that fits with an ellipsis)
		CutW = MaxW;
		for (int32 N = Str.Len() - 1; N >= 1; --N)
		{
			Cut = TagGfx::ShapeAt(Str.Left(N).TrimEnd() + TEXT("…"), static_cast<EGfxFont>(Font), Scale, CutSize);
			if (CutSize.X <= MaxW + 0.5f) break;
		}
		if (!Cut.IsValid())
		{
			Cut = Full;
			CutSize = FullSize;
		}
	}
	OutSize = CutSize;
	return Cut;
}

// ------------------------------------------------------------------ FBroadcastGfx
FBroadcastGfx::FBroadcastGfx(SGridironOverlay* InOwner) : Owner(InOwner)
{
	AlertQueue.Reserve(4);
	PlayClockStr = TEXT(":10");
	// the engine's map warnings drawn over the game's picture (UnrealEngine.cpp DrawMapWarnings: "Preparing Animation
	// Sequences (1)" while the anim compiler streams a clip in, texture streaming notes): off in the game, where only
	// the broadcast's graphics belong on screen (the log still has them); -TagMapWarnings keeps them, the editor too
	if (GEngine && !GIsEditor && !FParse::Param(FCommandLine::Get(), TEXT("TagMapWarnings"))) GEngine->bSuppressMapWarnings = true;
}

FBroadcastGfx& SGridironOverlay::Gfx() const
{
	return *GfxPtr;
}

ATagBooth* SGridironOverlay::GfxBooth() const
{
	return Director ? Director->Booth.Get() : nullptr;
}

bool FBroadcastGfx::OldBug()
{
	static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagOldBug"));
	return b;
}

bool FBroadcastGfx::OpenOn() const
{
	if (bScripted || !Owner || !Owner->Director) return false;
	const AMatchDirector& D = *Owner->Director;
#if TAG_GFX_SHOW_VIEWS
	if (D.HalftimeNow().bOn || D.PostgameNow().bOn) return true; // (the halftime and postgame shows hold them too)
#endif
	return D.PregameNow().bOn;
}

bool FBroadcastGfx::OpenHidesBug() const
{
	if (!OpenOn()) return false;
	// (the crane, the card and the units' beat; the TV wide the open holds after them shows it: StageLeft 0 on the last.
	// A halftime or postgame show: its cards have the score)
	const FTagPregameView& P = Owner->Director->PregameNow();
	if (!P.bOn) return true;
	return P.Stage < 3 || P.StageLeft > 0.f;
}

bool FBroadcastGfx::HoldToast(const FString& Words)
{
	if (!OpenOn()) return false;
	HeldToast = Words;
	return true;
}

TSharedRef<SWidget> FBroadcastGfx::MakeLayer()
{
	return SNew(TagGfx::SGfxLayer, TWeakPtr<FBroadcastGfx>(AsShared()));
}

FLinearColor FBroadcastGfx::SideColor(int32 S) const
{
	return S >= 0 && S < 2 ? Bug.Side[S].Color : TagGfx::Paper;
}

// ---- show this
void FBroadcastGfx::SetScoreBug(const FGfxScoreBug& B)
{
	for (int32 s = 0; s < 2; ++s)
	{
		// (a score that went up flashes its cell; a new game's teams do not)
		if (B.Side[s].Score > Bug.Side[s].Score && B.Side[s].Abbr.Equals(Bug.Side[s].Abbr)) ScoreT[s] = Clock;
		if (B.Side[s].Score != Bug.Side[s].Score || ScoreStr[s].IsEmpty()) ScoreStr[s] = FString::FromInt(B.Side[s].Score);
	}
	// the play clock's last ten: the cell keeps its last number while it slides away
	if (B.PlayClock >= 0 && B.PlayClock <= 10 && B.PlayClock != PlayClockShownFor)
	{
		PlayClockShownFor = B.PlayClock;
		PlayClockStr = FString::Printf(TEXT(":%02d"), B.PlayClock);
	}
	Bug = B;
}

void FBroadcastGfx::ShowAlert(const FGfxAlert& A)
{
	if (A.Kind == EGfxAlert::None) return;
	// a timeout: the pip it used drains (the bug's count already has it gone)
	if (A.Kind == EGfxAlert::Timeout && A.Side >= 0 && A.Side < 2)
	{
		DrainSide = A.Side;
		DrainPip = FMath::Clamp(Bug.Side[A.Side].Timeouts, 0, 2);
		DrainT0 = Clock;
	}
	if (Alert.Kind == EGfxAlert::None || A.Kind >= Alert.Kind)
	{
		StartAlert(A);
		return;
	}
	// a lesser one waits for the slot (a first down is only worth it at once)
	if (A.Kind != EGfxAlert::FirstDown && AlertQueue.Num() < 3) AlertQueue.Add(A);
}

void FBroadcastGfx::StartAlert(const FGfxAlert& A)
{
	Alert = A;
	AlertT0 = Clock;
	bAlertOut = false;
	UE_LOG(LogTemp, Display, TEXT("TAG gfx bug state: %s%s%s"), *A.Text, A.Detail.IsEmpty() ? TEXT("") : TEXT("  "), *A.Detail);
}

void FBroadcastGfx::BeginBand(int32 Kind, float Secs)
{
	BandKind = Kind;
	BandT0 = Clock;
	BandSecs = Secs;
	bBandOut = false;
}

bool FBroadcastGfx::BandFree(int32 Kind) const
{
	// (one at a time: a flag's panel, then a big play's lower third, a booth's team number, a drive summary, the
	// third-down graphic)
	return BandKind == 0 || bBandOut || BandPri(Kind) <= BandPri(BandKind);
}

int32 FBroadcastGfx::BandPri(int32 Kind)
{
	// (1 lower third, 2 drive summary, 3 third down, 4 the flag's panel, 5 a team number)
	return Kind == 4 ? 0 : Kind == 1 ? 1 : Kind == 5 ? 2 : Kind == 2 ? 3 : 4;
}

void FBroadcastGfx::ShowStatTags(const FGfxStatTags& T)
{
	if (T.Num <= 0) return;
	Tags = T;
	Tags.Num = FMath::Min(T.Num, 2);
	TagsT0 = Clock;
	bTagsOn = true;
	bTagsOut = false;
	TagGfx::LogTags(TEXT("replay"), Tags.Tag, Tags.Num);
}

void FBroadcastGfx::HideStatTags()
{
	if (bTagsOn && !bTagsOut)
	{
		bTagsOut = true;
		TagsOutT0 = Clock;
	}
}

void FBroadcastGfx::ShowLowerThird(const FGfxLowerThird& L)
{
	L3 = L;
	bL3Booth = false;
	BeginBand(1, L.Secs);
	if (L.bTag) TagGfx::LogTags(TEXT("lower third"), &L.Tag, 1);
}

void FBroadcastGfx::ShowDriveSummary(const FGfxDriveSummary& D)
{
	TAG_GFX_NAMES;
	Drive = D;
	const FString& Abbr = D.Side >= 0 && D.Side < 2 ? Bug.Side[D.Side].Abbr : FString();
	DrvKicker = FString(TEXT("DRIVE SUMMARY")) + (Abbr.IsEmpty() ? FString() : Dot() + Abbr) + (D.Start.IsEmpty() ? FString() : Dot() + TEXT("FROM ") + D.Start);
	DrvNum[0] = FString::FromInt(D.Plays);
	DrvNum[1] = FString::FromInt(D.Yards);
	DrvNum[2] = Mmss(D.Seconds);
	BeginBand(2, D.Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx drive summary: %s %s, %d plays, %d yd, %s"), *Abbr, *D.Result, D.Plays, D.Yards, *DrvNum[2]);
}

void FBroadcastGfx::ShowThirdDown(const FGfxThirdDown& T)
{
	Third = T;
	BeginBand(3, T.Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx down: %s  %s %s"), *T.Down, *T.Kicker, *T.Line);
}

void FBroadcastGfx::ShowMatchupCard(const FGfxMatchup& M, float Secs)
{
	Card = M;
	// the coin toss is the open's (the book's "Coin toss: POR wins and defers. ..." line, up in the head strip; the HUD's
	// ticker waits for the open to end)
	if (Owner && Owner->Director && Owner->Director->Game)
		for (const tag::PlayLog& L : Owner->Director->Game->log)
		{
			const FString Ds(UTF8_TO_TCHAR(L.desc.c_str()));
			if (!Ds.StartsWith(TEXT("Coin toss: "))) continue;
			FString Toss = Ds.Mid(11), Head, Rest;
			if (Toss.Split(TEXT(". "), &Head, &Rest)) Toss = Head;
			Toss.TrimEndInline();
			Toss.RemoveFromEnd(TEXT("."));
			Card.Kicker = M.Kicker + TagGfx::Dot() + TEXT("COIN TOSS: ") + Toss.ToUpper();
			break;
		}
	for (int32 s = 0; s < 2; ++s)
	{
		const FGfxMatchupTeam& T = M.Side[s];
		const int32 Vals[3] = {T.Ovr, T.Off, T.Def}, Ranks[3] = {T.OvrRank, T.OffRank, T.DefRank};
		static const TCHAR* const Names[3] = {TEXT("OVERALL"), TEXT("OFFENSE"), TEXT("DEFENSE")};
		for (int32 i = 0; i < 3; ++i)
		{
			CardNum[s][i] = FString::FromInt(Vals[i]);
			CardLab[s][i] = Ranks[i] > 0 ? FString::Printf(TEXT("%s  #%d"), Names[i], Ranks[i]) : FString(Names[i]);
			CardOvr[s][i] = i < T.NumStars ? FString::FromInt(T.Stars[i].Ovr) : FString();
		}
	}
	ShowCard(0, Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx matchup card: %s at %s"), *M.Side[0].Abbr, *M.Side[1].Abbr);
}

void FBroadcastGfx::HideMatchupCard()
{
	HideCard(0);
}

void FBroadcastGfx::ShowCard(int32 Kind, float Secs)
{
	CardKind = Kind;
	CardT0 = Clock;
	CardSecs = Secs;
	bCardOut = false;
}

void FBroadcastGfx::HideCard(int32 Kind)
{
	if (CardT0 >= 0.0 && !bCardOut && CardKind == Kind)
	{
		bCardOut = true;
		CardOutT0 = Clock;
	}
}

// ---- the halftime and postgame cards (04 A10)
namespace TagGfx
{
	/** The box's numbers as words, once (the table's points and totals; the comparison's nine rows and their bars). */
	void BoxWords(const FGfxBoxStats& B, FString (&Q)[2][6], FString (&Val)[9][2], double (&Num)[9][2])
	{
		for (int32 s = 0; s < 2; ++s)
		{
			const FGfxTeamTotals& T = B.Tot[s];
			for (int32 p = 0; p < 5; ++p) Q[s][p] = p < B.NumQ ? (T.ByQ[p] >= 0 ? FString::FromInt(T.ByQ[p]) : FString(TEXT("-"))) : FString();
			Q[s][5] = FString::FromInt(T.Score);
			Val[0][s] = FString::FromInt(T.Yards);
			Num[0][s] = T.Yards;
			Val[1][s] = FString::FromInt(T.PassYds);
			Num[1][s] = T.PassYds;
			Val[2][s] = FString::FromInt(T.RushYds);
			Num[2][s] = T.RushYds;
			Val[3][s] = FString::FromInt(T.FirstDowns);
			Num[3][s] = T.FirstDowns;
			Val[4][s] = FString::Printf(TEXT("%d-%d"), T.ThirdConv, T.ThirdAtt);
			Num[4][s] = T.ThirdAtt > 0 ? T.ThirdConv / static_cast<double>(T.ThirdAtt) : 0.0;
			Val[5][s] = FString::FromInt(T.Turnovers);
			Num[5][s] = T.Turnovers;
			Val[6][s] = Mmss(T.TopSecs);
			Num[6][s] = T.TopSecs;
			Val[7][s] = FString::FromInt(T.Sacks);
			Num[7][s] = T.Sacks;
			Val[8][s] = FString::Printf(TEXT("%d-%d"), T.Penalties, T.PenYds);
			Num[8][s] = T.PenYds;
		}
	}
	const TCHAR* const BoxLabels[9] = {TEXT("TOTAL YARDS"), TEXT("PASSING"), TEXT("RUSHING"), TEXT("FIRST DOWNS"), TEXT("3RD DOWN"), TEXT("TURNOVERS"),
		TEXT("POSSESSION"), TEXT("SACKS"), TEXT("PENALTIES")};
	/** The player of the game card's name box: a full name too wide for it gives way to the initial ("M. WASHINGTON"). */
	constexpr float PotgNameW = 740.f;
}

void FBroadcastGfx::ShowHalftimeStats(const FHalftimeStats& S, float Secs)
{
	Box = S;
	TagGfx::BoxWords(Box, BoxQ, BoxVal, BoxNum);
	ShowCard(1, Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx halftime box: %s %d - %s %d"), *Box.Team[0].Abbr, Box.Tot[0].Score, *Box.Team[1].Abbr, Box.Tot[1].Score);
}

void FBroadcastGfx::ShowFinalStats(const FFinalStats& S, float Secs)
{
	Box = S;
	TagGfx::BoxWords(Box, BoxQ, BoxVal, BoxNum);
	ShowCard(3, Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx final box: %s %d - %s %d"), *Box.Team[0].Abbr, Box.Tot[0].Score, *Box.Team[1].Abbr, Box.Tot[1].Score);
}

void FBroadcastGfx::ShowPlayerOfGame(const FPlayerOfGame& P, float Secs)
{
	Potg = P;
	FVector2D Sz;
	TagGfx::ShapeAt(Potg.Name, EGfxFont::PotgName, 1.f, Sz);
	FString First, Last;
	if (Sz.X > TagGfx::PotgNameW && Potg.Name.Split(TEXT(" "), &First, &Last) && First.Len() > 0) Potg.Name = First.Left(1) + TEXT(". ") + Last;
	ShowCard(2, Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx player of the game: %s %s (%s), %.1f game points: %s"), *Potg.Number, *Potg.Name, *Potg.Team.Abbr, Potg.Points, *Potg.Line);
}

FHalftimeStats FBroadcastGfx::HalftimeStatsFor(const tag::Game& G)
{
	FHalftimeStats S;
	static_cast<FGfxBoxStats&>(S) = TagGfx::BoxFor(G, false);
	return S;
}

FFinalStats FBroadcastGfx::FinalStatsFor(const tag::Game& G)
{
	FFinalStats S;
	static_cast<FGfxBoxStats&>(S) = TagGfx::BoxFor(G, true);
	return S;
}

FPlayerOfGame FBroadcastGfx::PlayerOfGameFor(const tag::Game& G)
{
	TAG_GFX_NAMES;
	FPlayerOfGame Out;
	if (!G.home || !G.away) return Out;
	const int32 Margin = G.score.home - G.score.away;
	const std::string* Win = Margin > 0 ? &G.home->abbr : Margin < 0 ? &G.away->abbr : nullptr;
	const tag::PlayerLine* Best = nullptr;
	double BestPts = 0.0;
	for (const tag::PlayerLine& Each : G.stats.all())
	{
		const double Pts = GamePoints(Each) * (Win && Each.team == *Win ? 1.2 : 1.0);
		if (Pts > BestPts)
		{
			BestPts = Pts;
			Best = &Each;
		}
	}
	if (!Best) return Out;
	const tag::PlayerLine& Ln = *Best;
	const tag::Player* P = PlayerById(G, Ln.pid);
	const bool bHome = Ln.team == G.home->abbr;
	const bool bWinner = Win && Ln.team == *Win;
	Out.bValid = true;
	Out.Side = bHome ? 1 : 0;
	Out.Team = TeamIdFor(bHome ? *G.home : *G.away);
	Out.Points = BestPts;
	Out.Name = P ? Up(P->first + " " + P->last) : FString(UTF8_TO_TCHAR(Ln.name.c_str())).ToUpper();
	Out.Number = P ? FString::Printf(TEXT("#%d"), P->number) : FString();
	Out.Pos = P ? FString(PosWord(P->pos)) : FString(UTF8_TO_TCHAR(Ln.pos.c_str()));
	Out.Grade = P && P->grade != tag::StarGrade::None ? Up(tag::starGradeName(P->grade)) : FString();
	// his four numbers: from the part of the game most of his points came from
	const double PassP = Ln.passYds / 25.0 + 4.0 * Ln.passTD - 2.0 * Ln.passInt;
	const double RushP = Ln.rushYds / 10.0 + 6.0 * Ln.rushTD;
	const double RecP = Ln.recYds / 10.0 + 6.0 * Ln.recTD + 0.5 * Ln.rec;
	const double DefP = DefScore(Ln);
	const double KickP = 3.0 * Ln.fgm + Ln.xpm;
	const double Top = FMath::Max(FMath::Max(PassP, RushP), FMath::Max(FMath::Max(RecP, DefP), KickP));
	auto Stat = [&Out](const FString& Value, const TCHAR* Label)
	{
		if (Out.NumStats >= 4) return;
		Out.Stats[Out.NumStats].Value = Value;
		Out.Stats[Out.NumStats].Label = Label;
		++Out.NumStats;
	};
	if (Top == PassP)
	{
		Stat(FString::Printf(TEXT("%d/%d"), Ln.passComp, Ln.passAtt), TEXT("COMP / ATT"));
		Stat(FString::FromInt(Ln.passYds), TEXT("PASS YARDS"));
		Stat(FString::FromInt(Ln.passTD), TEXT("TOUCHDOWNS"));
		Stat(FString::Printf(TEXT("%.1f"), tag::passerRating(Ln)), TEXT("RATING"));
	}
	else if (Top == RushP)
	{
		Stat(FString::FromInt(Ln.rushYds), TEXT("RUSH YARDS"));
		Stat(FString::FromInt(Ln.rushAtt), TEXT("CARRIES"));
		Stat(FString::Printf(TEXT("%.1f"), Ln.rushAtt > 0 ? Ln.rushYds / static_cast<double>(Ln.rushAtt) : 0.0), TEXT("PER CARRY"));
		Stat(FString::FromInt(Ln.rushTD + Ln.recTD), TEXT("TOUCHDOWNS"));
	}
	else if (Top == RecP)
	{
		Stat(FString::FromInt(Ln.rec), TEXT("CATCHES"));
		Stat(FString::FromInt(Ln.recYds), TEXT("REC YARDS"));
		Stat(FString::FromInt(Ln.recLong), TEXT("LONGEST"));
		Stat(FString::FromInt(Ln.recTD + Ln.rushTD), TEXT("TOUCHDOWNS"));
	}
	else if (Top == DefP)
	{
		Stat(FString::FromInt(Ln.tackles), TEXT("TACKLES"));
		Stat(FString::FromInt(Ln.sacks), TEXT("SACKS"));
		Stat(FString::FromInt(Ln.ints), TEXT("INTERCEPTIONS"));
		Stat(FString::FromInt(Ln.ff), TEXT("FORCED FUMBLES"));
	}
	else
	{
		Stat(FString::Printf(TEXT("%d/%d"), Ln.fgm, Ln.fga), TEXT("FIELD GOALS"));
		Stat(FString::Printf(TEXT("%d/%d"), Ln.xpm, Ln.xpa), TEXT("EXTRA POINTS"));
		Stat(FString::FromInt(3 * Ln.fgm + Ln.xpm), TEXT("POINTS"));
	}
	// his whole game, every part of it he played
	TArray<FString> Parts;
	if (Ln.passAtt > 0) Parts.Add(PassLine(Ln));
	if (Ln.rushAtt > 0) Parts.Add(RushLine(Ln));
	if (Ln.rec > 0) Parts.Add(RecLine(Ln));
	if (DefP > 0.0 && (Top == DefP || Ln.sacks + Ln.ints + Ln.ff > 0)) Parts.Add(DefLine(Ln));
	if (Ln.fga + Ln.xpa > 0) Parts.Add(FString::Printf(TEXT("FG %d/%d  XP %d/%d"), Ln.fgm, Ln.fga, Ln.xpm, Ln.xpa));
	Out.Line = FString::Join(Parts, TEXT("     "));
	Out.Why = FString::Printf(TEXT("%.1f GAME POINTS"), BestPts) + (bWinner ? Dot() + TEXT("WINNING SIDE") : FString());
	return Out;
}

// ---- the flag's panel (05 A11) and the booth's graphics (04 A9)
bool FBroadcastGfx::PenaltyFromLog(const tag::Game& G, const tag::PlayLog& L, const FGfxSnapState& Before, FGfxPenalty& Out)
{
	TAG_GFX_NAMES;
	if (L.big != tag::LogTag::Penalty) return false; // (a declined flag is the play's own line; a fifth down a turnover)
	const FString Ds(UTF8_TO_TCHAR(L.desc.c_str()));
	const int32 At = Ds.Find(TEXT("PENALTY on "), ESearchCase::CaseSensitive);
	if (At == INDEX_NONE) return false;
	const int32 Colon = Ds.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
	if (Colon == INDEX_NONE) return false;
	const bool bPre = At == 0; // (before the snap the flag is its own line; after it, it follows the play's words)
	const FString Abbr = Ds.Mid(At + 11, Colon - At - 11);
	FString Name = Ds.Mid(Colon + 2), Num;
	// "Offensive Holding (#74 J.Smith), enforced. ..." / "Delay of Game, 5 yards. ..."
	const int32 Par = Name.Find(TEXT(" (#"), ESearchCase::CaseSensitive);
	if (Par != INDEX_NONE)
	{
		const int32 Close = Name.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Par);
		const FString Man = Name.Mid(Par + 2, Close == INDEX_NONE ? 4 : Close - Par - 2);
		FString Rest;
		if (!Man.Split(TEXT(" "), &Num, &Rest)) Num = Man;
		Name.LeftInline(Par);
	}
	else
	{
		const int32 Comma = Name.Find(TEXT(","), ESearchCase::CaseSensitive);
		if (Comma != INDEX_NONE) Name.LeftInline(Comma);
	}
	Name.TrimStartAndEndInline();
	const bool bHome = G.home && Abbr.Equals(UTF8_TO_TCHAR(G.home->abbr.c_str()));
	const int32 Flagged = bHome ? 1 : 0;
	const bool bOff = Before.bValid ? Flagged == Before.PossSide : Flagged == SideOf(L.poss);
	const FFoulWords* Fw = FoulWords(Name);
	Out = FGfxPenalty();
	Out.Side = Flagged;
	Out.Foul = Fw ? FString(Fw->Words) : Name.ToUpper();
	Out.Team = Abbr.ToUpper();
	Out.Who = bOff ? TEXT("OFFENSE") : TEXT("DEFENSE");
	Out.Number = Num;
	// the yards: as the line has them (before the snap), else what the ball moved from the line of scrimmage
	int32 Moved = -1;
	if (bPre)
	{
		const int32 Y = Ds.Find(TEXT(" yards."), ESearchCase::CaseSensitive);
		if (Y != INDEX_NONE)
		{
			int32 Q = Y;
			while (Q > 0 && FChar::IsDigit(Ds[Q - 1])) --Q;
			Moved = FCString::Atoi(*Ds.Mid(Q, Y - Q));
		}
	}
	else if (Before.bValid && SideOf(G.poss) == Before.PossSide)
		Moved = FMath::RoundToInt(FMath::Abs(G.ballOn - Before.BallOn));
	const int32 Nominal = Fw ? Fw->Yards : 0;
	Out.Yards = Moved > 0 && Nominal > 0 && Moved < Nominal ? FString(TEXT("HALF THE DISTANCE"))
		: Moved > 0 ? FString::Printf(TEXT("%d YARDS"), Moved) : Nominal > 0 ? FString::Printf(TEXT("%d YARDS"), Nominal) : FString(TEXT("SPOT FOUL"));
	// what it does to the down (the down before it: Before)
	if (!Before.bValid) Out.Result = Up(G.downText());
	else if (bOff)
		Out.Result = G.down == Before.Down ? TEXT("REPEAT ") + DownWord(G.down) : G.down == Before.Down + 1 ? FString(TEXT("LOSS OF DOWN")) : Up(G.downText());
	else if (G.down == 1 && (Before.Down != 1 || G.toGo > Before.ToGo + 0.01 || (Fw && Fw->bAutoFirst)))
		Out.Result = Fw && Fw->bAutoFirst ? FString(TEXT("AUTOMATIC 1ST DOWN")) : FString(TEXT("1ST DOWN"));
	else Out.Result = TEXT("REPEAT ") + DownWord(G.down);
	return true;
}

void FBroadcastGfx::ShowPenalty(const FGfxPenalty& P)
{
	Pen = P;
	PenSub = P.Who + (P.Number.IsEmpty() ? FString() : FString(TEXT(" ")) + P.Number) + (P.Team.IsEmpty() ? FString() : TagGfx::Dot() + P.Team);
	BeginBand(4, P.Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx flag panel: %s  %s  %s  %s"), *P.Foul, *PenSub, *P.Yards, *P.Result);
}

void FBroadcastGfx::ShowTeamStat(const FGfxTeamStat& S)
{
	TeamStat = S;
	BeginBand(5, S.Secs);
	UE_LOG(LogTemp, Display, TEXT("TAG gfx team number: %s %s / %s"), *S.Label, *S.Val[0], *S.Val[1]);
}

bool FBroadcastGfx::ShownLately(const FString& Key, double Secs) const
{
	for (const TPair<FString, double>& K : ShownKeys)
		if (K.Key.Equals(Key) && Clock - K.Value < Secs) return true;
	return false;
}

void FBroadcastGfx::NoteShown(const FString& Key)
{
	for (TPair<FString, double>& K : ShownKeys)
		if (K.Key.Equals(Key))
		{
			K.Value = Clock;
			return;
		}
	if (ShownKeys.Num() >= 24) ShownKeys.RemoveAt(0);
	ShownKeys.Emplace(Key, Clock);
}

bool FBroadcastGfx::OnBoothLine(const FGfxBoothCue& Cue)
{
	TAG_GFX_NAMES;
	const AMatchDirector* D = Owner ? Owner->Director : nullptr;
	// never over a live play or a kick in the air, nor under a menu
	if (D && (D->State == EMatchState::Live || D->State == EMatchState::Kick)) return false;
	if (Owner && Owner->bModal) return false;
	const float Secs = FMath::Max(4.f, Cue.Secs + 1.f);
	// the referee at his microphone: the flag's panel he is reading
	if (Cue.bRef || Cue.Kind == 10)
	{
		if (!bPenPending) return false;
		bPenPending = false;
		ShowPenalty(PendingPen);
		return true;
	}
	// a score's call: the drive that got it
	if (Cue.Kind == 7)
	{
		if (!bDrivePending || !BandFree(2)) return false;
		ShowDriveSummary(DrivePending);
		bDrivePending = false;
		return true;
	}
	if (Cue.Topic.IsEmpty() || !D || !D->Game) return false;
	const tag::Game& G = *D->Game;
	FString Head, Rest;
	if (!Cue.Topic.Split(TEXT(":"), &Head, &Rest)) Head = Cue.Topic;
	// a man's day: his line as a lower third (once in a while: the game's own big-play one counts)
	if (Head == TEXT("qb") || Head == TEXT("rb") || Head == TEXT("wr") || Head == TEXT("stat") || Head == TEXT("sk") || Head == TEXT("dbint") || Head == TEXT("qbint"))
	{
		const int32 Pid = FCString::Atoi(*Rest);
		const FString Key = FString::Printf(TEXT("pl:%d"), Pid);
		if (ShownLately(Key, 90.0) || !BandFree(1)) return false;
		FGfxLowerThird L;
		if (!BoothLineL3(G, Pid, Head, L)) return false;
		L.Secs = Secs;
		ShowLowerThird(L);
		bL3Booth = true;
		NoteShown(Key);
		return true;
	}
	// a team's third downs (not when the game's own third-down graphic is up for this down)
	if (Head == TEXT("3rd"))
	{
		const FString Key = TEXT("3rd:") + Rest;
		if (ShownLately(Key, 300.0) || ThirdShownLog == static_cast<int32>(G.log.size()) || !BandFree(3)) return false;
		const bool bHome = G.home && Rest.Equals(UTF8_TO_TCHAR(G.home->abbr.c_str()));
		const tag::TeamLine& Tl = G.team[bHome ? tag::TeamKey::Home : tag::TeamKey::Away];
		FGfxThirdDown T;
		T.Side = bHome ? 1 : 0;
		T.Down = TEXT("3RD DOWN");
		T.Kicker = Rest.ToUpper() + TEXT(" TODAY");
		T.Line = Tl.thirdAtt > 0 ? FString::Printf(TEXT("%d OF %d"), Tl.thirdConv, Tl.thirdAtt) + Dot() + FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.f * Tl.thirdConv / Tl.thirdAtt))
			: FString(TEXT("NONE YET"));
		T.Secs = Secs;
		ShowThirdDown(T);
		NoteShown(Key);
		return true;
	}
	// the two teams on one number
	if (Head == TEXT("yds") || Head == TEXT("pens") || Head == TEXT("tos"))
	{
		const FString Key = TEXT("ts:") + Head;
		if (ShownLately(Key, 600.0) || !BandFree(5)) return false;
		const tag::TeamLine* Lines[2] = {&G.team.away, &G.team.home};
		FGfxTeamStat S;
		S.Secs = Secs;
		S.Label = Head == TEXT("yds") ? TEXT("TOTAL YARDS") : Head == TEXT("pens") ? TEXT("PENALTIES") : TEXT("TURNOVERS");
		for (int32 s = 0; s < 2; ++s)
		{
			const tag::TeamLine& Tl = *Lines[s];
			if (Head == TEXT("yds")) { S.Val[s] = FString::FromInt(Tl.yards); S.Num[s] = Tl.yards; }
			else if (Head == TEXT("pens")) { S.Val[s] = FString::Printf(TEXT("%d-%d"), Tl.penalties, Tl.penYds); S.Num[s] = Tl.penYds; }
			else { S.Val[s] = FString::FromInt(Tl.ints + Tl.fumblesLost); S.Num[s] = Tl.ints + Tl.fumblesLost; }
		}
		ShowTeamStat(S);
		NoteShown(Key);
		return true;
	}
	return false;
}

void FBroadcastGfx::ClearGraphics()
{
	Alert = FGfxAlert();
	bAlertOut = false;
	AlertQueue.Reset();
	BandKind = 0;
	bBandOut = false;
	bTagsOn = bTagsOut = false;
	DrainSide = -1;
	bPenPending = false;
	bL3Booth = false;
}

float FBroadcastGfx::CarrierTopMph(const tag::Player* P) const
{
	for (int32 i = 0; P && i < 4; ++i)
		if (SpeedWho[i] == P) return SpeedMph[i];
	return 0.f;
}

bool FBroadcastGfx::Settled() const
{
	if (Clock - BugT0 < 0.45) return false;
	if (Alert.Kind != EGfxAlert::None && (bAlertOut || Clock - AlertT0 < 0.32)) return false;
	if (BandKind != 0 && (bBandOut || Clock - BandT0 < 0.5)) return false;
	if (bTagsOn && (bTagsOut || Clock - (TagsT0 + Tags.Delay + 0.3 * (Tags.Num - 1)) < 0.4)) return false;
	if (CardT0 >= 0.0 && (bCardOut || Clock - CardT0 < 0.7)) return false;
	if (Clock - DrainT0 < 1.5 || Clock - ScoreT[0] < 1.2 || Clock - ScoreT[1] < 1.2) return false;
	return PlayClockAmt <= 0.f || PlayClockAmt >= 1.f;
}

// ---- the data
FGfxBugSide FBroadcastGfx::BugSideFor(const tag::Team& T)
{
	TAG_GFX_NAMES;
	FGfxBugSide S;
	S.Abbr = Up(T.abbr);
	const FLinearColor P = FromHex(T.colors.primary), P2 = FromHex(T.colors.secondary);
	S.Color = Vivid(P, P2);
	S.Color2 = P2;
	S.Logo = RosterUI::Logo(T);
	return S;
}

FGfxScoreBug FBroadcastGfx::BugFromGame(const AMatchDirector& D, double ClockOffset)
{
	TAG_GFX_NAMES;
	FGfxScoreBug B;
	if (!D.Game) return B;
	const tag::Game& G = *D.Game;
	const bool bFinal = G.isFinal();
	for (int32 s = 0; s < 2; ++s)
	{
		const tag::TeamKey K = KeyOf(s);
		FGfxBugSide& Sd = B.Side[s];
		Sd = BugSideFor(G.teamOf(K));
		Sd.Score = D.ScoreShown(K);
		Sd.TimeoutsMax = G.q >= 5 ? 2 : 3;
		Sd.Timeouts = FMath::Clamp(G.timeouts[K], 0, Sd.TimeoutsMax);
		Sd.bBall = !bFinal && G.poss == K;
	}
	B.Period = bFinal ? FString(TEXT("FINAL")) : G.q <= 4 ? Ordinal(G.q) : FString(TEXT("OT"));
	if (!bFinal) B.Clock = FString(UTF8_TO_TCHAR(tag::fmtClock(FMath::Max(0.0, G.clock + D.HudRunoffLeft - D.HudPreRun - ClockOffset)).c_str()));
	// the down cell (as the old bug's DownText): what a kick or a try shows, a score's word from the whistle, else the
	// down and the spot
	FString Word;
	D.ScoreShown(tag::TeamKey::Home, &Word);
	if (bFinal)
	{
		const int32 M = G.score.away - G.score.home;
		B.Down = M == 0 ? FString(TEXT("TIE GAME")) : (M > 0 ? B.Side[0].Abbr : B.Side[1].Abbr) + TEXT(" WIN");
	}
	else if (!D.HudDown.IsEmpty()) B.Down = D.HudDown.ToUpper();
	else if (!Word.IsEmpty()) B.Down = Word.ToUpper();
	else if (G.phase == tag::GamePhase::Kickoff) B.Down = TEXT("KICKOFF");
	else if (G.phase == tag::GamePhase::Pat) B.Down = TEXT("TRY");
	else
	{
		B.Down = Up(G.downText());
		B.Spot = Up(G.ydLine());
	}
	B.PlayClock = D.PlayClockShown();
	return B;
}

FGfxStatTags FBroadcastGfx::PickStatTags(const tag::PlayResult& R, const tag::Ball& B, float TopMph)
{
	return TagGfx::Pick(R, B, TopMph, nullptr);
}

FGfxMatchup FBroadcastGfx::MatchupFor(const tag::Team& Away, const tag::Team& Home, const tag::League* L, const FString& Conditions)
{
	TAG_GFX_NAMES;
	FGfxMatchup M;
	M.Kicker = TEXT("TODAY'S MATCHUP");
	M.Conditions = Conditions.ToUpper();
	const tag::Team* Ts[2] = {&Away, &Home};
	for (int32 s = 0; s < 2; ++s)
	{
		const tag::Team& T = *Ts[s];
		FGfxMatchupTeam& Mt = M.Side[s];
		const FGfxBugSide Look = BugSideFor(T);
		Mt.City = Up(T.city);
		Mt.Name = Up(T.name);
		Mt.Abbr = Look.Abbr;
		Mt.Color = Look.Color;
		Mt.Color2 = Look.Color2;
		Mt.Logo = Look.Logo;
		Mt.Scheme = T.offScheme.empty() ? Up(T.defScheme) : T.defScheme.empty() ? Up(T.offScheme) : Up(T.offScheme) + Dot() + Up(T.defScheme);
		const tag::TeamOvr O = tag::teamOvr(T);
		Mt.Ovr = O.ovr;
		Mt.Off = O.off;
		Mt.Def = O.def;
		if (L)
		{
			// (no season is played: the league's ratings, ranked, stand in for the records)
			Mt.OvrRank = Mt.OffRank = Mt.DefRank = 1;
			for (const auto& Other : *L)
			{
				if (!Other) continue;
				const tag::TeamOvr Q = tag::teamOvr(*Other);
				Mt.OvrRank += Q.ovr > O.ovr;
				Mt.OffRank += Q.off > O.off;
				Mt.DefRank += Q.def > O.def;
			}
		}
		// the three to watch: X-Factors and Superstars first, then by overall (the kickers aside)
		std::vector<const tag::Player*> V;
		for (const auto& P : T.roster)
			if (P && P->injuredWeeks <= 0 && P->pos != tag::Pos::K && P->pos != tag::Pos::P) V.push_back(P.get());
		std::stable_sort(V.begin(), V.end(), [](const tag::Player* X, const tag::Player* Y) { return X->grade != Y->grade ? X->grade > Y->grade : X->ovr > Y->ovr; });
		Mt.NumStars = 0;
		for (size_t i = 0; i < V.size() && Mt.NumStars < 3; ++i)
		{
			const tag::Player& P = *V[i];
			FGfxMatchupPlayer& St = Mt.Stars[Mt.NumStars++];
			St.Pos = ANSI_TO_TCHAR(tag::posName(P.pos));
			St.Name = FString::Printf(TEXT("#%d  %s. %s"), P.number, *Up(P.first.substr(0, 1)), *Up(P.last));
			St.Ovr = P.ovr;
			St.Grade = P.grade == tag::StarGrade::None ? FString() : Up(tag::starGradeName(P.grade));
		}
	}
	return M;
}

// ---- driven by the overlay
bool FBroadcastGfx::OnBanner(const FString& Words, const FString& Sub)
{
	TAG_GFX_NAMES;
	if (bScripted || OldBug()) return false;
	if (Words.Equals(TEXT("FLAG")))
	{
		// the flag is the bug's: its state, in flag yellow (no banner over the field); its panel in the band - the foul,
		// the side, the number, the yards and the down - with the referee's own line when he has a voice, else now
		FGfxPenalty P;
		const AMatchDirector* D = Owner ? Owner->Director : nullptr;
		const bool bPanel = D && D->Game && !D->Game->log.empty() && PenaltyFromLog(*D->Game, D->Game->log.back(), SnapBefore, P);
		FGfxAlert A;
		A.Kind = EGfxAlert::Flag;
		A.Text = TEXT("FLAG");
		A.Detail = bPanel ? FString() : Sub.ToUpper().Replace(TEXT("  -  "), *Dot());
		A.Secs = 5.f;
		ShowAlert(A);
		if (bPanel)
		{
#if TAG_GFX_BOOTH_ONAIR
			const ATagBooth* Bo = Owner->GfxBooth();
			const bool bRefVoice = Bo && Bo->RefereeOn();
#else
			const bool bRefVoice = false;
#endif
			if (bRefVoice)
			{
				PendingPen = P;
				bPenPending = true;
				PenPendingT = Clock;
			}
			else ShowPenalty(P);
		}
		return true;
	}
	if (Words.Equals(TEXT("TWO-MINUTE WARNING")))
	{
		FGfxAlert A;
		A.Kind = EGfxAlert::TwoMinute;
		A.Text = TEXT("TWO-MINUTE WARNING");
		A.Secs = 4.5f;
		ShowAlert(A);
	}
	return false;
}

bool FBroadcastGfx::OnToast(const FString& Words)
{
	TAG_GFX_NAMES;
	if (bScripted || OldBug()) return false;
	if (Words.Equals(TEXT("FIRST DOWN")))
	{
		// (the bug's one line, in place of the toast over the field)
		FGfxAlert A;
		A.Kind = EGfxAlert::FirstDown;
		A.Text = TEXT("FIRST DOWN");
		A.Secs = 2.2f;
		ShowAlert(A);
		return true;
	}
	// a big play's stat line ("#89 R. Ochoa   2 rec, 25 yds, 1 TD": MatchDirector OnPlayDead): the lower third
	FString Who, Line;
	if (!Words.Contains(TEXT(" yds")) || !Words.Split(TEXT("   "), &Who, &Line)) return false;
	FGfxLowerThird L;
	FString Num, Nm;
	L.Name = Who.Split(TEXT(" "), &Num, &Nm) ? Num + TEXT("  ") + Nm.ToUpper() : Who.ToUpper();
	L.Stats = Line.ToUpper().Replace(TEXT(", "), *Dot());
	const AMatchDirector* D = Owner ? Owner->Director : nullptr;
	if (D && D->Game)
	{
		const tag::Game& G = *D->Game;
		L.Side = SideOf(G.poss);
		const tag::Player* P = nullptr;
		if (D->Sim && D->Sim->result)
		{
			const tag::PlayResult& R = *D->Sim->result;
			P = R.complete && R.receiver ? R.receiver : R.rusher;
			const FGfxStatTags Ts = Pick(R, D->Sim->ball, CarrierTopMph(P), nullptr);
			if (Ts.Num > 0)
			{
				L.bTag = true;
				L.Tag = Ts.Tag[0];
			}
		}
		L.Sub = (P ? FString(ANSI_TO_TCHAR(tag::posName(P->pos))) + Dot() : FString()) + Up(G.teamOf(G.poss).city);
		if (P) NoteShown(FString::Printf(TEXT("pl:%d"), P->id)); // (the booth's line on him a moment later brings no second one)
	}
	ShowLowerThird(L);
	return true;
}

void FBroadcastGfx::ResetGame(const tag::Game* InGame)
{
	LastGame = InGame;
	SigQ = SigClock = SigPoss = SigPhase = SigDown = SigToGo = SigBall = SigState = -1;
	SigPlayClock = -2;
	SigScore[0] = SigScore[1] = SigTo[0] = SigTo[1] = -1;
	SigHud.Reset();
	SeenScore[0] = SeenScore[1] = SeenTo[0] = SeenTo[1] = -1;
	DriveT = FDriveTrack();
	GameSecs = 0.0;
	LastClockV = -1.0;
	LastQ = -1;
	bDrivePending = false;
	RedZoneDrive = -1;
	ThirdShownLog = -1;
	ScoreT[0] = ScoreT[1] = -10.0;
	SnapBefore = FGfxSnapState();
	SnapLogN = -1;
	LastBoothId = 0;
	ShownKeys.Reset();
	ClearGraphics();
}

void FBroadcastGfx::Pump(float Dt)
{
	TAG_GFX_NAMES;
	Clock += Dt;
	// the pregame open: a toast already up goes under it and comes back after (a controller notice), as do the ones
	// asked for during it (HoldToast), a moment after the open hands over to the kickoff
	const bool bOpenNow = OpenOn();
	if (bOpenNow && !bLastOpen && Owner && Owner->ToastT < 3.2f && !Owner->IsStatLine() && !Owner->ToastText.IsEmpty())
	{
		HeldToast = Owner->ToastText;
		Owner->ToastT = 99.f;
	}
	if (!bOpenNow && bLastOpen) OpenEndT = Clock;
	bLastOpen = bOpenNow;
	if (!bOpenNow && !HeldToast.IsEmpty() && Clock - OpenEndT > 1.5 && Owner)
	{
		const FString Was = HeldToast;
		HeldToast.Reset();
		Owner->Toast(Was);
	}
	// the bug coming on with the HUD (at the kickoff, after an open), and the play clock's cell
	const bool bHudNow = Owner && Owner->bHud && !OpenHidesBug();
	if (bHudNow && !bLastHud) BugT0 = Clock;
	bLastHud = bHudNow;
	PlayClockAmt = FMath::FInterpConstantTo(PlayClockAmt, Bug.PlayClock >= 0 && Bug.PlayClock <= 10 ? 1.f : 0.f, Dt, 4.f);
	// the state out of the bug: its time (a flag also while the flag is down), then the next waiting
	if (Alert.Kind != EGfxAlert::None)
	{
		const bool bHeld = Alert.Kind == EGfxAlert::Flag && Owner && Owner->bFlag && Clock - AlertT0 < 14.0;
		if (!bAlertOut && Clock - AlertT0 > Alert.Secs && !bHeld)
		{
			bAlertOut = true;
			AlertOutT0 = Clock;
		}
		if (bAlertOut && Clock - AlertOutT0 > 0.24)
		{
			Alert = FGfxAlert();
			bAlertOut = false;
		}
	}
	if (Alert.Kind == EGfxAlert::None && AlertQueue.Num() > 0)
	{
		const FGfxAlert Nx = AlertQueue[0];
		AlertQueue.RemoveAt(0);
		StartAlert(Nx);
	}
	if (BandKind != 0)
	{
		if (!bBandOut && Clock - BandT0 > BandSecs)
		{
			bBandOut = true;
			BandOutT0 = Clock;
		}
		if (bBandOut && Clock - BandOutT0 > 0.32)
		{
			BandKind = 0;
			bBandOut = false;
		}
	}
	if (bTagsOn)
	{
		if (!bTagsOut && Clock - TagsT0 > Tags.Delay + Tags.Secs + 0.3 * (Tags.Num - 1)) HideStatTags();
		if (bTagsOut && Clock - TagsOutT0 > 0.27) bTagsOn = bTagsOut = false;
	}
	if (CardT0 >= 0.0)
	{
		if (!bCardOut && CardSecs > 0.f && Clock - CardT0 > CardSecs) HideCard(CardKind);
		if (bCardOut && Clock - CardOutT0 > 0.37)
		{
			CardT0 = -10.0;
			bCardOut = false;
		}
	}
	if (bScripted || !Owner || !Owner->Director) return;
	const AMatchDirector& D = *Owner->Director;
	const tag::Game* G = D.Game.get();
	if (G != LastGame) ResetGame(G);
	if (G) PumpGame(D, *G);
}

void FBroadcastGfx::PumpGame(const AMatchDirector& D, const tag::Game& G)
{
	TAG_GFX_NAMES;
	// ---- the bug, built again only when something it shows has changed (the clock: once a second)
	const double ShownClock = FMath::Max(0.0, G.clock + D.HudRunoffLeft - D.HudPreRun - Owner->ClockOffset);
	const int32 St = static_cast<int32>(D.State);
	const int32 Sc[2] = {D.ScoreShown(tag::TeamKey::Away), D.ScoreShown(tag::TeamKey::Home)};
	const int32 To[2] = {G.timeouts.away, G.timeouts.home};
	const int32 Pc = D.PlayClockShown();
	const int32 PcSig = Pc >= 0 && Pc <= 10 ? Pc : -1;
	if (G.q != SigQ || FMath::FloorToInt(ShownClock) != SigClock || Sc[0] != SigScore[0] || Sc[1] != SigScore[1] || To[0] != SigTo[0] || To[1] != SigTo[1] ||
		static_cast<int32>(G.poss) != SigPoss || static_cast<int32>(G.phase) != SigPhase || G.down != SigDown || FMath::RoundToInt(G.toGo) != SigToGo ||
		FMath::RoundToInt(G.ballOn) != SigBall || St != SigState || PcSig != SigPlayClock || !SigHud.Equals(D.HudDown, ESearchCase::CaseSensitive))
	{
		SigQ = G.q;
		SigClock = FMath::FloorToInt(ShownClock);
		SigScore[0] = Sc[0];
		SigScore[1] = Sc[1];
		SigTo[0] = To[0];
		SigTo[1] = To[1];
		SigPoss = static_cast<int32>(G.poss);
		SigPhase = static_cast<int32>(G.phase);
		SigDown = G.down;
		SigToGo = FMath::RoundToInt(G.toGo);
		SigBall = FMath::RoundToInt(G.ballOn);
		SigState = St;
		SigPlayClock = PcSig;
		if (!SigHud.Equals(D.HudDown, ESearchCase::CaseSensitive)) SigHud = D.HudDown;
		SetScoreBug(BugFromGame(D, Owner->ClockOffset));
	}
	// ---- the bug's states from what changed: a score (as the bug shows it: a touchdown from the whistle), a timeout
	for (int32 s = 0; s < 2; ++s)
	{
		if (SeenScore[s] >= 0 && Sc[s] > SeenScore[s])
		{
			const int32 Pts = Sc[s] - SeenScore[s];
			FGfxAlert A;
			A.Side = s;
			A.Detail = Bug.Side[s].Abbr;
			if (Pts >= 6) { A.Kind = EGfxAlert::Touchdown; A.Text = TEXT("TOUCHDOWN"); A.Secs = 4.5f; }
			else if (Pts == 3) { A.Kind = EGfxAlert::FieldGoal; A.Text = TEXT("FIELD GOAL"); }
			else if (Pts == 2 && G.safetyKick) { A.Kind = EGfxAlert::Safety; A.Text = TEXT("SAFETY"); }
			else if (Pts == 2) { A.Kind = EGfxAlert::FieldGoal; A.Text = TEXT("2-POINT CONVERSION"); }
			ShowAlert(A);
		}
		SeenScore[s] = Sc[s];
		// (one fewer in the same period: overtime's reset to two is not a timeout. LastQ: the period last frame, TrackDrive)
		if (SeenTo[s] >= 0 && To[s] < SeenTo[s] && LastQ == G.q)
		{
			FGfxAlert A;
			A.Kind = EGfxAlert::Timeout;
			A.Text = TEXT("TIMEOUT");
			A.Side = s;
			A.Detail = Bug.Side[s].Abbr + Dot() + (To[s] == 0 ? FString(TEXT("NONE LEFT")) : FString::Printf(TEXT("%d LEFT"), To[s]));
			A.Secs = 4.f;
			ShowAlert(A);
		}
		SeenTo[s] = To[s];
	}
	TrackDrive(G);
	const bool bDead = D.State == EMatchState::Wait || D.State == EMatchState::Call || D.State == EMatchState::Presnap;
	const bool bModalUp = Owner->bModal;
	// ---- the red zone: once a drive, the first dead ball inside the 20
	if (bDead && G.phase == tag::GamePhase::Scrimmage && G.ballOn >= 80.0 && DriveT.bOn && RedZoneDrive != DriveT.Id && SideOf(G.poss) == DriveT.Poss)
	{
		RedZoneDrive = DriveT.Id;
		const tag::TeamLine& Tl = G.team[G.poss];
		FGfxAlert A;
		A.Kind = EGfxAlert::RedZone;
		A.Text = TEXT("RED ZONE");
		A.Side = DriveT.Poss;
		A.Detail = Tl.redZoneTrips > 0 ? FString::Printf(TEXT("%s TODAY  %d TD IN %d TRIP%s"), *Bug.Side[DriveT.Poss].Abbr, Tl.redZoneTD, Tl.redZoneTrips, Tl.redZoneTrips == 1 ? TEXT("") : TEXT("S"))
			: Bug.Side[DriveT.Poss].Abbr + TEXT("'S FIRST TRIP TODAY");
		ShowAlert(A);
	}
	// ---- a drive's summary, at the first dead ball the band is free for
	if (bDrivePending)
	{
		if (Clock > DrivePendingUntil) bDrivePending = false;
		else if (bDead && !bModalUp && BandFree(2))
		{
			ShowDriveSummary(DrivePending);
			bDrivePending = false;
		}
	}
	// ---- third down between the plays (fourth only once the offense is set to go for it): the conversions today
	const int32 LogN = static_cast<int32>(G.log.size());
	const bool bDownTime = G.phase == tag::GamePhase::Scrimmage && ((G.down == 3 && bDead) || (G.down == 4 && D.State == EMatchState::Presnap));
	if (bDownTime && ThirdShownLog != LogN && !bModalUp && !bDrivePending && BandFree(3))
	{
		ThirdShownLog = LogN;
		const tag::TeamLine& Tl = G.team[G.poss];
		const int32 Att = G.down == 3 ? Tl.thirdAtt : Tl.fourthAtt, Conv = G.down == 3 ? Tl.thirdConv : Tl.fourthConv;
		FGfxThirdDown T;
		T.Side = SideOf(G.poss);
		T.Down = Up(G.downText());
		T.Kicker = Bug.Side[T.Side].Abbr + (G.down == 3 ? TEXT(" ON 3RD DOWN TODAY") : TEXT(" ON 4TH DOWN TODAY"));
		T.Line = Att > 0 ? FString::Printf(TEXT("%d OF %d"), Conv, Att) + Dot() + FString::Printf(TEXT("%d%%"), FMath::RoundToInt(100.f * Conv / Att)) : FString(TEXT("FIRST OF THE GAME"));
		ShowThirdDown(T);
		NoteShown(TEXT("3rd:") + Bug.Side[T.Side].Abbr); // (the booth's own third-down line does not bring it up again)
	}
	// the game-state graphics go at the snap (the booth's with them; a big play's lower third has its own time)
	if (BandKind != 0 && (BandKind != 1 || bL3Booth) && !bBandOut && D.State == EMatchState::Live)
	{
		bBandOut = true;
		BandOutT0 = Clock;
	}
	// the down before each play (a flag's panel reads what the flag did from it): taken while the book has no new line
	if (G.phase == tag::GamePhase::Scrimmage && LogN == SnapLogN)
	{
		SnapBefore.bValid = true;
		SnapBefore.Down = G.down;
		SnapBefore.ToGo = G.toGo;
		SnapBefore.BallOn = G.ballOn;
		SnapBefore.PossSide = SideOf(G.poss);
	}
	SnapLogN = LogN;
	// a flag's panel held for the referee who did not come in 4 s: now, unless the next play is on
	if (bPenPending && Clock - PenPendingT > 4.0)
	{
		bPenPending = false;
		if (D.State != EMatchState::Live && D.State != EMatchState::Kick) ShowPenalty(PendingPen);
	}
#if TAG_GFX_BOOTH_ONAIR
	// the booth's line on the air: its graphic as it starts (OnBoothLine)
	if (ATagBooth* Bo = Owner->GfxBooth())
		if (const tag::booth::Line* On = Bo->OnAir())
			if (On->id != LastBoothId)
			{
				LastBoothId = On->id;
				FGfxBoothCue Cue;
				Cue.Id = On->id;
				Cue.Kind = static_cast<int32>(On->kind);
				Cue.bRef = On->who == tag::booth::Who::Ref;
				Cue.Topic = UTF8_TO_TCHAR(On->topic.c_str());
				Cue.Text = UTF8_TO_TCHAR(On->text.c_str());
				Cue.Secs = static_cast<float>(On->estSecs);
				const bool bGfx = OnBoothLine(Cue);
				if (bGfx || !Cue.Topic.IsEmpty())
					UE_LOG(LogTemp, Display, TEXT("TAG gfx booth line %d [%s] %s: %s"), Cue.Id, *Cue.Topic, bGfx ? TEXT("graphic up") : TEXT("no graphic"), *Cue.Text);
			}
#endif
	// ---- the carrier's top speed (the sim's velocity of whoever has the ball, the most each man reached)
	if (D.State == EMatchState::Live && D.Sim)
	{
		if (D.Sim->serial != SpeedSerial)
		{
			SpeedSerial = D.Sim->serial;
			for (int32 i = 0; i < 4; ++i) { SpeedWho[i] = nullptr; SpeedMph[i] = 0.f; }
		}
		const tag::Ent* H = D.Sim->ball.holder;
		if (H && H->p && H->side == tag::Side::Off)
		{
			const float Mph = static_cast<float>(FMath::Sqrt(H->vel.x * H->vel.x + H->vel.y * H->vel.y) * 2.04545);
			int32 Slot = -1;
			for (int32 i = 0; i < 4 && Slot < 0; ++i)
				if (SpeedWho[i] == H->p || !SpeedWho[i]) Slot = i;
			if (Slot >= 0)
			{
				SpeedWho[Slot] = H->p;
				SpeedMph[Slot] = FMath::Max(SpeedMph[Slot], Mph);
			}
		}
	}
	// ---- a replay: its tags (a number the lower third already shows is left to it)
	if (D.State == EMatchState::Replay && LastStateI != static_cast<int32>(EMatchState::Replay) && bAutoReplayTags && D.Sim && D.Sim->result)
	{
		const tag::PlayResult& R = *D.Sim->result;
		const tag::Player* Car = R.complete && R.receiver ? R.receiver : R.rusher;
		const FString* Skip = BandKind == 1 && !bBandOut && L3.bTag ? &L3.Tag.Field : nullptr;
		const FGfxStatTags T = Pick(R, D.Sim->ball, CarrierTopMph(Car), Skip);
		if (T.Num > 0) ShowStatTags(T);
		else UE_LOG(LogTemp, Display, TEXT("TAG gfx stat tag (replay): none notable"));
	}
	if (D.State != EMatchState::Replay && LastStateI == static_cast<int32>(EMatchState::Replay)) HideStatTags();
	LastStateI = St;
}

void FBroadcastGfx::TrackDrive(const tag::Game& G)
{
	TAG_GFX_NAMES;
	// the game's seconds gone (the clock counted down, period by period)
	if (LastQ < 0)
	{
		LastQ = G.q;
		LastClockV = G.clock;
	}
	const bool bNewHalf = G.q != LastQ && (G.q == 3 || G.q >= 5);
	GameSecs += G.q == LastQ ? FMath::Max(0.0, LastClockV - G.clock) : FMath::Max(0.0, LastClockV);
	LastQ = G.q;
	LastClockV = G.clock;
	if (DriveT.bOn)
	{
		const tag::TeamKey K = KeyOf(DriveT.Poss);
		const int32 Mine = G.score[K], Theirs = G.score[tag::other(K)];
		FString Result;
		int32 Yards = FMath::RoundToInt(DriveT.LastBallOn - DriveT.StartBallOn);
		bool bEnd = false, bScored = false;
		if (Mine > DriveT.Score)
		{
			bEnd = bScored = true;
			const int32 Pts = Mine - DriveT.Score;
			Result = Pts >= 6 ? TEXT("TOUCHDOWN") : Pts == 3 ? TEXT("FIELD GOAL") : TEXT("SCORE");
			if (Pts >= 6) Yards = FMath::RoundToInt(100.0 - DriveT.StartBallOn);
		}
		else if (bNewHalf || G.isFinal())
		{
			// (before the change of possession: the second half's kickoff goes to whoever it goes to)
			bEnd = true;
			Result = G.isFinal() ? TEXT("END OF GAME") : TEXT("END OF HALF");
		}
		else if (Theirs > DriveT.OppScore || SideOf(G.poss) != DriveT.Poss)
		{
			bEnd = true;
			Result = DriveEnd(G, DriveT.LogStart);
		}
		else if (G.phase == tag::GamePhase::Scrimmage) DriveT.LastBallOn = G.ballOn;
		if (bEnd)
		{
			DriveT.bOn = false;
			FGfxDriveSummary S;
			S.Side = DriveT.Poss;
			S.Result = Result;
			S.Start = StartWord(DriveT.StartBallOn);
			S.Plays = FMath::Max(0, G.team[K].plays - DriveT.StartPlays);
			S.Yards = Yards;
			S.Seconds = FMath::RoundToInt(GameSecs - DriveT.StartSecs);
			// after every score, and a long drive that came up empty (8 plays, 4 minutes or 45 yards)
			const bool bLong = S.Plays >= 8 || S.Seconds >= 240 || S.Yards >= 45;
			UE_LOG(LogTemp, Display, TEXT("TAG gfx drive ended: %s %s, %d plays, %d yd, %d s%s"), *Bug.Side[FMath::Clamp(S.Side, 0, 1)].Abbr, *S.Result, S.Plays, S.Yards, S.Seconds,
				bScored || bLong ? TEXT(" (summary)") : TEXT(""));
			if ((bScored || bLong) && S.Plays > 0)
			{
				DrivePending = S;
				bDrivePending = true;
				DrivePendingUntil = Clock + 45.0;
			}
		}
	}
	if (!DriveT.bOn && G.phase == tag::GamePhase::Scrimmage && !G.isFinal())
	{
		DriveT.bOn = true;
		++DriveT.Id;
		DriveT.Poss = SideOf(G.poss);
		DriveT.StartBallOn = G.driveStartBallOn;
		DriveT.LastBallOn = G.ballOn;
		DriveT.StartSecs = GameSecs;
		DriveT.StartPlays = G.team[G.poss].plays;
		DriveT.Score = G.score[G.poss];
		DriveT.OppScore = G.score[tag::other(G.poss)];
		DriveT.LogStart = static_cast<int32>(G.log.size());
		DriveT.Q = G.q;
	}
}

// ------------------------------------------------------------------ paint
int32 FBroadcastGfx::Paint(FSlateWindowElementList& Out, int32 LayerId, const FGeometry& G)
{
	FGfxCanvas C;
	C.Out = &Out;
	C.Geo = &G;
	C.Scale = G.GetAccumulatedLayoutTransform().GetScale();
	C.Size = FVector2D(G.GetLocalSize());
	C.Layer = LayerId;
	PaintAll(C);
	return LayerId + 48;
}

void FBroadcastGfx::PaintAll(FGfxCanvas& C)
{
	TAG_GFX_NAMES;
	const int32 Base = C.Layer;
	const bool bCheck = C.Out == nullptr; // (the fit check lays out everything that is up)
	const bool bHudOn = bCheck || (Owner && Owner->bHud);
	const bool bModalUp = !bCheck && Owner && Owner->bModal;
	const FVector2D BugTL(FMath::RoundToDouble((C.Size.X - BugW) * 0.5), C.Size.Y - BugBottom - BugH);
	if (bBackdrop && !bCheck)
	{
		C.Layer = Base;
		PaintBackdrop(C);
	}
	if (bHudOn && !bModalUp)
	{
		C.Layer = Base + 2;
		PaintBand(C, BugTL);
		C.Layer = Base + 14;
		PaintTags(C, BugTL);
	}
	if (bHudOn && !OldBug() && (bCheck || !OpenHidesBug()))
	{
		C.Layer = Base + 22;
		PaintBug(C, BugTL);
	}
	if (!bModalUp)
	{
		C.Layer = Base + 36;
		PaintCard(C);
	}
	C.Layer = Base;
}

void FBroadcastGfx::PaintBackdrop(FGfxCanvas& C)
{
	TAG_GFX_NAMES;
	// a mown field: two greens in bands (the plain background the -TagGfxShots pictures are judged on)
	const int32 N = 12;
	const double W = C.Size.X / N;
	for (int32 i = 0; i < N; ++i)
		Rect(C, FVector2D(i * W, 0.0), FVector2D(W + 1.0, C.Size.Y), i % 2 ? FLinearColor(0.030f, 0.135f, 0.034f) : FLinearColor(0.024f, 0.112f, 0.028f), 0);
}

void FBroadcastGfx::PaintBug(FGfxCanvas& C, const FVector2D& BugTL0)
{
	TAG_GFX_NAMES;
	const bool bCheck = C.Out == nullptr;
	const float Vis = bCheck ? 1.f : Out3(Phase(Clock, BugT0, 0.4f));
	if (Vis <= 0.001f) return;
	const FVector2D TL = BugTL0 + FVector2D(0.0, FMath::RoundToDouble(14.0 * (1.0 - Vis)));
	const float A0 = C.Alpha;
	C.Alpha = A0 * Vis;
	// the play clock's cell, behind the bug's right end: out in the last 10 s, red in the last 5
	const float Pc = Out3(PlayClockAmt);
	if (Pc > 0.002f || bCheck)
	{
		const FVector2D P(TL.X + BugW - PcW + (PcW + 2.0) * (bCheck ? 1.f : Pc), TL.Y + 6.0);
		Rect(C, P, FVector2D(PcW, BugH - 12.0), Cell, 0);
		Rect(C, P + FVector2D(PcW - 3.0, 0.0), FVector2D(3.0, BugH - 12.0), Bug.PlayClock >= 0 && Bug.PlayClock <= 5 ? ZoneRed : Hair, 1);
		Text(C, TxPlayClock, PlayClockStr, EGfxFont::PlayClock, P, FVector2D(PcW, BugH - 12.0), 1,
			Bug.PlayClock >= 0 && Bug.PlayClock <= 5 ? FLinearColor(1.f, 0.25f, 0.2f) : Paper, 2);
	}
	// the state tab behind the top edge
	PaintAlert(C, TL);
	// the bar: a soft shadow, the dark base, a hairline along the top
	Rect(C, TL + FVector2D(-6.0, -3.0), FVector2D(BugW + 12.0, BugH + 12.0), FLinearColor(0, 0, 0, 0.16f), 3, 6.f);
	Rect(C, TL + FVector2D(0.0, 2.0), FVector2D(BugW, BugH + 2.0), FLinearColor(0, 0, 0, 0.35f), 3);
	Rect(C, TL, FVector2D(BugW, BugH), Ink, 4);
	for (int32 s = 0; s < 2; ++s)
	{
		const FGfxBugSide& Sd = Bug.Side[s];
		const double X = TL.X + s * TeamW;
		// the team: its colour behind its mark, a wash of it over the cell and its edge along the foot
		Rect(C, FVector2D(X, TL.Y), FVector2D(LogoW, BugH), Sd.Color, 5);
		Rect(C, FVector2D(X + LogoW, TL.Y), FVector2D(TeamW - LogoW, BugH), WithA(Sd.Color, 0.16f), 5);
		Rect(C, FVector2D(X + LogoW, TL.Y + BugH - 3.0), FVector2D(TeamW - LogoW, 3.0), Sd.Color, 6);
		Image(C, Sd.Logo, FVector2D(X + 5.0, TL.Y + 5.0), FVector2D(46.0, 46.0), 6);
		const FVector2D AbS = Text(C, TxAbbr[s], Sd.Abbr, EGfxFont::BugAbbr, FVector2D(X + LogoW + 12.0, TL.Y + 4.0), FVector2D(96.0, 32.0), 0, Paper, 8);
		// the ball: a small white football after the name of the team that has it
		if (Sd.bBall)
		{
			const FVector2D Fb(X + LogoW + 12.0 + AbS.X + 9.0, TL.Y + 15.0);
			Rect(C, Fb, FVector2D(17.0, 10.0), Paper, 8, 5.f);
			Rect(C, Fb + FVector2D(5.0, 4.5), FVector2D(7.0, 1.0), Sd.Color, 9);
		}
		// the timeouts: a pip each, the one just used draining
		for (int32 i = 0; i < Sd.TimeoutsMax; ++i)
		{
			const FVector2D Pp(X + LogoW + 13.0 + i * 23.0, TL.Y + 41.0);
			Rect(C, Pp, FVector2D(18.0, 4.0), FLinearColor(1, 1, 1, 0.16f), 7);
			if (i < Sd.Timeouts) Rect(C, Pp, FVector2D(18.0, 4.0), Paper, 8);
			else if (s == DrainSide && i == DrainPip && Clock - DrainT0 < 1.4)
			{
				// (it flashes, then empties from the right)
				const float K = Out3(Phase(Clock, DrainT0 + 0.3, 0.9f));
				Rect(C, Pp + FVector2D(-2.0, -2.0), FVector2D(22.0, 8.0), WithA(Paper, 0.35f * (1.f - Phase(Clock, DrainT0, 0.6f))), 7, 2.f);
				Rect(C, Pp, FVector2D(18.0 * (1.0 - K), 4.0), Paper, 8);
			}
		}
		// the score, and a flash over it when it goes up
		Text(C, TxScore[s], ScoreStr[s], EGfxFont::BugScore, FVector2D(X + TeamW - 78.0, TL.Y), FVector2D(66.0, BugH), 2, Paper, 8);
		const float Fl = 1.f - Phase(Clock, ScoreT[s], 1.1f);
		if (Fl > 0.f) Rect(C, FVector2D(X + TeamW - 88.0, TL.Y), FVector2D(88.0, BugH), WithA(Paper, 0.5f * Fl * Fl), 9);
		Rect(C, FVector2D(X + TeamW - 1.0, TL.Y + 8.0), FVector2D(1.0, BugH - 16.0), Hair, 7);
	}
	// the clock: the period small, the time large (the final: its word alone)
	double X = TL.X + 2.0 * TeamW;
	Rect(C, FVector2D(X, TL.Y), FVector2D(ClockW, BugH), Cell, 5);
	if (Bug.Clock.IsEmpty()) Text(C, TxPeriod, Bug.Period, EGfxFont::BugDown, FVector2D(X, TL.Y), FVector2D(ClockW, BugH), 1, Paper, 8);
	else
	{
		Text(C, TxPeriod, Bug.Period, EGfxFont::BugPeriod, FVector2D(X + 14.0, TL.Y), FVector2D(40.0, BugH), 0, Soft, 8);
		Text(C, TxClock, Bug.Clock, EGfxFont::BugClock, FVector2D(X + 52.0, TL.Y), FVector2D(ClockW - 64.0, BugH), 2, Paper, 8);
	}
	// the down and distance: white on dark (a flag puts flag yellow along its top while the state is out)
	X += ClockW;
	Rect(C, FVector2D(X, TL.Y + 8.0), FVector2D(1.0, BugH - 16.0), Hair, 7);
	if (Bug.Spot.IsEmpty()) Text(C, TxDown, Bug.Down, EGfxFont::BugDown, FVector2D(X + 16.0, TL.Y), FVector2D(DownW - 30.0, BugH), 0, Paper, 8);
	else
	{
		Text(C, TxDown, Bug.Down, EGfxFont::BugDown, FVector2D(X + 16.0, TL.Y + 4.0), FVector2D(DownW - 30.0, 30.0), 0, Paper, 8);
		Text(C, TxSpot, Bug.Spot, EGfxFont::BugSpot, FVector2D(X + 16.0, TL.Y + 33.0), FVector2D(DownW - 30.0, 17.0), 0, Soft, 8);
	}
	if (Alert.Kind == EGfxAlert::Flag && !bCheck)
		Rect(C, FVector2D(X, TL.Y), FVector2D(DownW, 3.0), WithA(FlagYellow, InOut(Clock, AlertT0, 0.28f, bAlertOut, AlertOutT0, 0.22f)), 9);
	Rect(C, TL, FVector2D(BugW, 1.0), FLinearColor(1, 1, 1, 0.12f), 9);
	C.Alpha = A0;
}

void FBroadcastGfx::PaintAlert(FGfxCanvas& C, const FVector2D& BugTL)
{
	TAG_GFX_NAMES;
	if (Alert.Kind == EGfxAlert::None) return;
	const bool bCheck = C.Out == nullptr;
	const float Vis = bCheck ? 1.f : InOut(Clock, AlertT0, 0.28f, bAlertOut, AlertOutT0, 0.22f);
	if (Vis <= 0.001f) return;
	FLinearColor Bg = Paper, Fg = InkText;
	switch (Alert.Kind)
	{
	case EGfxAlert::Flag: Bg = FlagYellow; Fg = InkText; break;
	case EGfxAlert::RedZone: Bg = ZoneRed; Fg = Paper; break;
	case EGfxAlert::TwoMinute:
	case EGfxAlert::FirstDown: Bg = Paper; Fg = InkText; break;
	default: Bg = SideColor(Alert.Side); Fg = OnColor(Bg); break;
	}
	const FVector2D Ks = Measure(C, TxAlert, Alert.Text, EGfxFont::AlertKind);
	const FVector2D Ds = Measure(C, TxAlertDetail, Alert.Detail, EGfxFont::AlertDetail);
	const double KindW = Ks.X + 30.0;
	const double DetW = Alert.Detail.IsEmpty() ? 0.0 : FMath::Min(Ds.X + 34.0, static_cast<double>(BugW) - KindW);
	const double W = KindW + DetW;
	const FVector2D TL(FMath::RoundToDouble(BugTL.X + (BugW - W) * 0.5), BugTL.Y - TabH * Vis);
	// (it rises from behind the bug's top edge: nothing of it below that edge)
	Clip(C, FVector2D(BugTL.X - 300.0, BugTL.Y - TabH - 8.0), FVector2D(BugW + 600.0, TabH + 8.0));
	Rect(C, TL, FVector2D(KindW, TabH), Bg, 0);
	Text(C, TxAlert, Alert.Text, EGfxFont::AlertKind, TL, FVector2D(KindW, TabH), 1, Fg, 1);
	if (DetW > 0.0)
	{
		Rect(C, TL + FVector2D(KindW, 0.0), FVector2D(DetW, TabH), Ink, 0);
		Rect(C, TL + FVector2D(KindW, 0.0), FVector2D(DetW, 2.0), WithA(Bg, 0.9f), 1);
		Text(C, TxAlertDetail, Alert.Detail, EGfxFont::AlertDetail, TL + FVector2D(KindW + 16.0, 0.0), FVector2D(DetW - 32.0, TabH), 0, Paper, 1);
	}
	Unclip(C);
}

void FBroadcastGfx::PaintBand(FGfxCanvas& C, const FVector2D& BugTL)
{
	TAG_GFX_NAMES;
	if (BandKind == 0) return;
	const bool bCheck = C.Out == nullptr;
	const float Vis = bCheck ? 1.f : InOut(Clock, BandT0, 0.4f, bBandOut, BandOutT0, 0.3f);
	if (Vis <= 0.001f) return;
	// in: a wipe from the left, the words a beat behind it; out: a fade and a drop
	const float Wipe = bCheck ? 1.f : Out3(Phase(Clock, BandT0, 0.42f));
	const float TextA = bCheck ? 1.f : Out3(Phase(Clock, BandT0 + 0.15, 0.25f));
	const float OutK = bBandOut && !bCheck ? In2(Phase(Clock, BandOutT0, 0.3f)) : 0.f;
	const float A0 = C.Alpha;
	const FVector2D Off0 = C.Off;
	C.Alpha = A0 * (1.f - OutK);
	C.Off.Y += 12.0 * OutK;
	const double Y = BugTL.Y - TabH - BandGap - BandH;
	if (BandKind == 1)
	{
		// a big play's lower third: his team's mark, his name on a white plate, his game so far, a tracking number
		const FVector2D Ns = Measure(C, TxL3Name, L3.Name, EGfxFont::L3Name), Ss = Measure(C, TxL3Sub, L3.Sub, EGfxFont::L3Sub);
		const FVector2D Ls = Measure(C, TxL3Stats, L3.Stats, EGfxFont::L3Stats);
		const double NameW = FMath::Clamp(FMath::Max(Ns.X, Ss.X) + 44.0, 240.0, 560.0);
		const double StatW = FMath::Clamp(Ls.X + 44.0, 160.0, 560.0);
		const double TagW = L3.bTag ? TagPlateW(C, TxL3TagLab, TxL3TagVal, L3.Tag) : 0.0;
		const double W = BandH + NameW + StatW + (L3.bTag ? 10.0 + TagW : 0.0);
		const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), Y);
		Clip(C, TL - FVector2D(0.0, 4.0), FVector2D(W * Wipe, BandH + 8.0));
		const FLinearColor Tc = SideColor(L3.Side);
		Rect(C, TL + FVector2D(0.0, 3.0), FVector2D(BandH + NameW + StatW, BandH), FLinearColor(0, 0, 0, 0.3f), 0);
		Rect(C, TL, FVector2D(BandH, BandH), Tc, 1);
		Image(C, L3.Side >= 0 && L3.Side < 2 ? Bug.Side[L3.Side].Logo : nullptr, TL + FVector2D(7.0, 7.0), FVector2D(BandH - 14.0, BandH - 14.0), 2);
		Rect(C, TL + FVector2D(BandH, 0.0), FVector2D(NameW, BandH), Paper, 1);
		Rect(C, TL + FVector2D(BandH + NameW, 0.0), FVector2D(StatW, BandH), Ink, 1);
		Rect(C, TL + FVector2D(BandH + NameW, BandH - 4.0), FVector2D(StatW, 4.0), Tc, 2);
		const float A1 = C.Alpha;
		C.Alpha = A1 * TextA;
		Text(C, TxL3Name, L3.Name, EGfxFont::L3Name, TL + FVector2D(BandH + 22.0, 7.0), FVector2D(NameW - 40.0, 36.0), 0, InkText, 3);
		Text(C, TxL3Sub, L3.Sub, EGfxFont::L3Sub, TL + FVector2D(BandH + 22.0, 44.0), FVector2D(NameW - 40.0, 20.0), 0, DarkSub, 3);
		Text(C, TxL3Stats, L3.Stats, EGfxFont::L3Stats, TL + FVector2D(BandH + NameW + 22.0, 0.0), FVector2D(StatW - 40.0, BandH - 4.0), 0, Paper, 3);
		if (L3.bTag) TagPlate(C, TxL3TagLab, TxL3TagVal, L3.Tag, TL + FVector2D(BandH + NameW + StatW + 10.0, 6.0), static_cast<float>(TagW), BandH - 12.f, 1);
		C.Alpha = A1;
		Unclip(C);
	}
	else if (BandKind == 2)
	{
		// the drive: the team, how it ended, and plays / yards / time in columns
		static const FString Labs[3] = {TEXT("PLAYS"), TEXT("YARDS"), TEXT("TIME")};
		const double MainW = 400.0, ColW = 96.0;
		const double W = BandH + MainW + 3.0 * ColW;
		const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), Y);
		Clip(C, TL - FVector2D(0.0, 4.0), FVector2D(W * Wipe, BandH + 8.0));
		const FLinearColor Tc = SideColor(Drive.Side);
		Rect(C, TL + FVector2D(0.0, 3.0), FVector2D(W, BandH), FLinearColor(0, 0, 0, 0.3f), 0);
		Rect(C, TL, FVector2D(BandH, BandH), Tc, 1);
		Image(C, Drive.Side >= 0 && Drive.Side < 2 ? Bug.Side[Drive.Side].Logo : nullptr, TL + FVector2D(7.0, 7.0), FVector2D(BandH - 14.0, BandH - 14.0), 2);
		Rect(C, TL + FVector2D(BandH, 0.0), FVector2D(MainW, BandH), Ink, 1);
		Rect(C, TL + FVector2D(BandH + MainW, 0.0), FVector2D(3.0 * ColW, BandH), Cell, 1);
		Rect(C, TL + FVector2D(BandH, BandH - 4.0), FVector2D(W - BandH, 4.0), Tc, 2);
		const float A1 = C.Alpha;
		C.Alpha = A1 * TextA;
		Text(C, TxDrvKicker, DrvKicker, EGfxFont::Kicker, TL + FVector2D(BandH + 20.0, 9.0), FVector2D(MainW - 36.0, 18.0), 0, Track, 3);
		Text(C, TxDrvResult, Drive.Result, EGfxFont::DrvResult, TL + FVector2D(BandH + 20.0, 28.0), FVector2D(MainW - 36.0, 38.0), 0, Paper, 3);
		for (int32 i = 0; i < 3; ++i)
		{
			const double Cx = TL.X + BandH + MainW + i * ColW;
			if (i > 0) Rect(C, FVector2D(Cx, TL.Y + 12.0), FVector2D(1.0, BandH - 28.0), Hair, 2);
			Text(C, TxDrvNum[i], DrvNum[i], EGfxFont::ColNum, FVector2D(Cx, TL.Y + 6.0), FVector2D(ColW, 40.0), 1, Paper, 3);
			Text(C, TxDrvLab[i], Labs[i], EGfxFont::ColLabel, FVector2D(Cx, TL.Y + 46.0), FVector2D(ColW, 16.0), 1, Soft, 3);
		}
		C.Alpha = A1;
		Unclip(C);
	}
	else if (BandKind == 3)
	{
		// the down in the offense's colour, its conversions today beside it
		const double DownBoxW = 230.0, MainW = 320.0;
		const double W = DownBoxW + MainW;
		const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), Y + 10.0);
		const double H = BandH - 10.0;
		Clip(C, TL - FVector2D(0.0, 4.0), FVector2D(W * Wipe, H + 8.0));
		const FLinearColor Tc = SideColor(Third.Side);
		Rect(C, TL + FVector2D(0.0, 3.0), FVector2D(W, H), FLinearColor(0, 0, 0, 0.3f), 0);
		Rect(C, TL, FVector2D(DownBoxW, H), Tc, 1);
		Rect(C, TL + FVector2D(DownBoxW, 0.0), FVector2D(MainW, H), Ink, 1);
		const float A1 = C.Alpha;
		C.Alpha = A1 * TextA;
		Text(C, TxThdDown, Third.Down, EGfxFont::ThdDown, TL + FVector2D(12.0, 0.0), FVector2D(DownBoxW - 24.0, H), 1, OnColor(Tc), 3);
		Text(C, TxThdKicker, Third.Kicker, EGfxFont::Kicker, TL + FVector2D(DownBoxW + 18.0, 8.0), FVector2D(MainW - 32.0, 18.0), 0, Soft, 3);
		Text(C, TxThdLine, Third.Line, EGfxFont::ThdLine, TL + FVector2D(DownBoxW + 18.0, 27.0), FVector2D(MainW - 32.0, 30.0), 0, Paper, 3);
		C.Alpha = A1;
		Unclip(C);
	}
	else if (BandKind == 4)
	{
		// the flag (05 A11): FLAG in flag yellow, the foul and who, the yards, what it does to the down
		static const FString FlagWord(TEXT("FLAG"));
		const double FlagW = 104.0;
		const FVector2D Fs = Measure(C, TxPenFoul, Pen.Foul, EGfxFont::L3Name), Ss = Measure(C, TxPenSub, PenSub, EGfxFont::L3Sub);
		const FVector2D Ys = Measure(C, TxPenYds, Pen.Yards, EGfxFont::L3Stats), Rs = Measure(C, TxPenRes, Pen.Result, EGfxFont::L3Stats);
		const double MainW = FMath::Clamp(FMath::Max(Fs.X, Ss.X) + 44.0, 240.0, 600.0);
		const double YdsW = FMath::Clamp(Ys.X + 40.0, 130.0, 320.0);
		const double ResW = FMath::Clamp(Rs.X + 40.0, 180.0, 420.0);
		const double W = FlagW + MainW + YdsW + ResW;
		const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), Y);
		Clip(C, TL - FVector2D(0.0, 4.0), FVector2D(W * Wipe, BandH + 8.0));
		Rect(C, TL + FVector2D(0.0, 3.0), FVector2D(W, BandH), FLinearColor(0, 0, 0, 0.3f), 0);
		Rect(C, TL, FVector2D(FlagW, BandH), FlagYellow, 1);
		Rect(C, TL + FVector2D(FlagW, 0.0), FVector2D(MainW, BandH), Ink, 1);
		Rect(C, TL + FVector2D(FlagW + MainW, 0.0), FVector2D(YdsW, BandH), Cell, 1);
		Rect(C, TL + FVector2D(FlagW + MainW + YdsW, 0.0), FVector2D(ResW, BandH), Paper, 1);
		Rect(C, TL + FVector2D(FlagW, BandH - 4.0), FVector2D(MainW, 4.0), SideColor(Pen.Side), 2);
		const float A1 = C.Alpha;
		C.Alpha = A1 * TextA;
		Text(C, TxPenFlag, FlagWord, EGfxFont::AlertKind, TL, FVector2D(FlagW, BandH), 1, InkText, 3);
		Text(C, TxPenFoul, Pen.Foul, EGfxFont::L3Name, TL + FVector2D(FlagW + 22.0, 7.0), FVector2D(MainW - 40.0, 36.0), 0, Paper, 3);
		Text(C, TxPenSub, PenSub, EGfxFont::L3Sub, TL + FVector2D(FlagW + 22.0, 44.0), FVector2D(MainW - 40.0, 20.0), 0, Soft, 3);
		Text(C, TxPenYds, Pen.Yards, EGfxFont::L3Stats, TL + FVector2D(FlagW + MainW + 20.0, 0.0), FVector2D(YdsW - 40.0, BandH), 1, Paper, 3);
		Text(C, TxPenRes, Pen.Result, EGfxFont::L3Stats, TL + FVector2D(FlagW + MainW + YdsW + 20.0, 0.0), FVector2D(ResW - 40.0, BandH), 1, InkText, 3);
		C.Alpha = A1;
		Unclip(C);
	}
	else if (BandKind == 5)
	{
		// the two teams on one number: each side's mark and value, the label and a bar of their shares between them
		const double LogoBoxW = BandH, ValW = 170.0, MidW = 320.0;
		const double W = 2.0 * (LogoBoxW + ValW) + MidW;
		const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), Y);
		Clip(C, TL - FVector2D(0.0, 4.0), FVector2D(W * Wipe, BandH + 8.0));
		Rect(C, TL + FVector2D(0.0, 3.0), FVector2D(W, BandH), FLinearColor(0, 0, 0, 0.3f), 0);
		const double Mx = TL.X + LogoBoxW + ValW;
		Rect(C, FVector2D(Mx, TL.Y), FVector2D(MidW, BandH), Cell, 1);
		const double Total = FMath::Max(1e-6, TeamStat.Num[0] + TeamStat.Num[1]);
		const double Share0 = TeamStat.Num[0] + TeamStat.Num[1] > 0.0 ? TeamStat.Num[0] / Total : 0.5;
		Rect(C, FVector2D(Mx + 24.0, TL.Y + 46.0), FVector2D((MidW - 48.0) * Share0, 8.0), SideColor(0), 2);
		Rect(C, FVector2D(Mx + 24.0 + (MidW - 48.0) * Share0, TL.Y + 46.0), FVector2D((MidW - 48.0) * (1.0 - Share0), 8.0), SideColor(1), 2);
		for (int32 s = 0; s < 2; ++s)
		{
			const double Lx = s == 0 ? TL.X : TL.X + W - LogoBoxW;
			const double Vx = s == 0 ? TL.X + LogoBoxW : Mx + MidW;
			Rect(C, FVector2D(Lx, TL.Y), FVector2D(LogoBoxW, BandH), SideColor(s), 1);
			Image(C, Bug.Side[s].Logo, FVector2D(Lx + 7.0, TL.Y + 7.0), FVector2D(LogoBoxW - 14.0, BandH - 14.0), 2);
			Rect(C, FVector2D(Vx, TL.Y), FVector2D(ValW, BandH), Ink, 1);
		}
		const float A1 = C.Alpha;
		C.Alpha = A1 * TextA;
		Text(C, TxTsLab, TeamStat.Label, EGfxFont::Kicker, FVector2D(Mx + 16.0, TL.Y + 10.0), FVector2D(MidW - 32.0, 24.0), 1, Paper, 3);
		for (int32 s = 0; s < 2; ++s)
		{
			const double Vx = s == 0 ? TL.X + LogoBoxW : Mx + MidW;
			Text(C, TxTsVal[s], TeamStat.Val[s], EGfxFont::ColNum, FVector2D(Vx + 10.0, TL.Y + 4.0), FVector2D(ValW - 20.0, 44.0), 1, Paper, 3);
			Text(C, TxTsAbbr[s], Bug.Side[s].Abbr, EGfxFont::L3Sub, FVector2D(Vx + 10.0, TL.Y + 48.0), FVector2D(ValW - 20.0, 18.0), 1, Soft, 3);
		}
		C.Alpha = A1;
		Unclip(C);
	}
	C.Alpha = A0;
	C.Off = Off0;
}

void FBroadcastGfx::PaintTags(FGfxCanvas& C, const FVector2D& BugTL)
{
	TAG_GFX_NAMES;
	if (!bTagsOn || Tags.Num <= 0) return;
	const bool bCheck = C.Out == nullptr;
	const float A0 = C.Alpha;
	const float H = 62.f;
	const double Bottom = BugTL.Y - TabH - BandGap;
	for (int32 i = 0; i < Tags.Num && i < 2; ++i)
	{
		// (bottom left, the first lowest; each slides in from the left, the second a beat after the first)
		const double T0 = TagsT0 + Tags.Delay + 0.3 * i;
		const float Vis = bCheck ? 1.f : (Clock < T0 ? 0.f : InOut(Clock, T0, 0.32f, bTagsOut, TagsOutT0, 0.25f));
		if (Vis <= 0.001f) continue;
		C.Alpha = A0 * Vis;
		const float W = TagPlateW(C, TxTagLab[i], TxTagVal[i], Tags.Tag[i]);
		const FVector2D TL(72.0 - 28.0 * (1.0 - Vis), Bottom - H - i * (H + 10.0));
		TagPlate(C, TxTagLab[i], TxTagVal[i], Tags.Tag[i], TL, W, H, 0);
	}
	C.Alpha = A0;
}

void FBroadcastGfx::PaintCard(FGfxCanvas& C)
{
	TAG_GFX_NAMES;
	if (CardT0 < 0.0) return;
	const bool bCheck = C.Out == nullptr;
	const float In = bCheck ? 1.f : Out3(Phase(Clock, CardT0, 0.55f));
	const float OutK = bCardOut && !bCheck ? In2(Phase(Clock, CardOutT0, 0.35f)) : 0.f;
	if (OutK >= 1.f) return;
	if (CardKind != 0)
	{
		// the halftime / final box and the player of the game (the matchup card's frame and motion)
		const float A0k = C.Alpha;
		C.Alpha = A0k * (1.f - OutK);
		if (CardKind == 2) PaintPotg(C, In);
		else PaintBox(C, In);
		C.Alpha = A0k;
		return;
	}
	static const FString KeyWord(TEXT("THREE TO WATCH")), VsWord(TEXT("VS"));
	const double W = CardHalfW * 2.0 + CardGap;
	const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), FMath::RoundToDouble((C.Size.Y - (CardH + CardHeadH + 6.0)) * 0.5 - 40.0));
	const float A0 = C.Alpha;
	C.Alpha = A0 * (1.f - OutK);
	// the head strip: the kicker and the conditions
	const float HeadA = bCheck ? 1.f : Out3(Phase(Clock, CardT0 + 0.25, 0.3f));
	Rect(C, TL, FVector2D(W, CardHeadH), WithA(Ink, 0.94f * HeadA), 0);
	Rect(C, TL + FVector2D(0.0, CardHeadH - 2.0), FVector2D(W, 2.0), WithA(Track, HeadA), 1);
	Text(C, TxCardKicker, Card.Kicker, EGfxFont::Kicker, TL + FVector2D(24.0, 0.0), FVector2D(W * 0.5 - 24.0, CardHeadH), 0, WithA(Paper, HeadA), 2);
	Text(C, TxCardCond, Card.Conditions, EGfxFont::CardLine, TL + FVector2D(W * 0.5, 0.0), FVector2D(W * 0.5 - 24.0, CardHeadH), 2, WithA(Soft, HeadA), 2);
	for (int32 s = 0; s < 2; ++s)
	{
		const FGfxMatchupTeam& Tm = Card.Side[s];
		FCardText& Tx = TxCard[s];
		// each half slides in from its own side
		const double Slide = (1.0 - In) * 140.0 * (s == 0 ? -1.0 : 1.0);
		const FVector2D P(TL.X + s * (CardHalfW + CardGap) + Slide, TL.Y + CardHeadH + 6.0);
		const float A1 = C.Alpha;
		C.Alpha = A1 * In;
		Rect(C, P + FVector2D(0.0, 6.0), FVector2D(CardHalfW, CardH), FLinearColor(0, 0, 0, 0.3f), 1);
		Rect(C, P, FVector2D(CardHalfW, CardH), WithA(Ink, 0.95f), 2);
		// the team's colour washing down from the top (stacked bands: no gradient element to build each frame)
		for (int32 b = 0; b < 8; ++b)
			Rect(C, P + FVector2D(0.0, b * 34.0), FVector2D(CardHalfW, 34.0), WithA(Tm.Color, 0.6f * (1.f - b / 8.f)), 3);
		Rect(C, P, FVector2D(CardHalfW, 5.0), Tm.Color, 4);
		Image(C, Tm.Logo, P + FVector2D((CardHalfW - 170.0) * 0.5, 22.0), FVector2D(170.0, 170.0), 4);
		Text(C, Tx.City, Tm.City, EGfxFont::CardCity, P + FVector2D(30.0, 198.0), FVector2D(CardHalfW - 60.0, 30.0), 1, FLinearColor(0.88f, 0.91f, 0.96f), 5);
		Text(C, Tx.Name, Tm.Name, EGfxFont::CardName, P + FVector2D(30.0, 226.0), FVector2D(CardHalfW - 60.0, 66.0), 1, Paper, 5);
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector2D Q(P.X + CardHalfW * 0.5 + (i - 1) * 150.0 - 70.0, P.Y + 300.0);
			Text(C, Tx.Num[i], CardNum[s][i], EGfxFont::CardNum, Q, FVector2D(140.0, 44.0), 1, Paper, 5);
			Text(C, Tx.Lab[i], CardLab[s][i], EGfxFont::CardLabel, Q + FVector2D(0.0, 44.0), FVector2D(140.0, 18.0), 1, Soft, 5);
		}
		Text(C, Tx.Scheme, Tm.Scheme, EGfxFont::CardLine, P + FVector2D(30.0, 374.0), FVector2D(CardHalfW - 60.0, 24.0), 1, Soft, 5);
		Rect(C, P + FVector2D(30.0, 410.0), FVector2D(CardHalfW - 60.0, 1.0), Hair, 4);
		Text(C, Tx.Stars, KeyWord, EGfxFont::Kicker, P + FVector2D(30.0, 420.0), FVector2D(CardHalfW - 60.0, 20.0), 0, Track, 5);
		for (int32 r = 0; r < Tm.NumStars && r < 3; ++r)
		{
			const FGfxMatchupPlayer& St = Tm.Stars[r];
			const FVector2D Row(P.X + 30.0, P.Y + 448.0 + r * 44.0);
			Rect(C, Row, FVector2D(CardHalfW - 60.0, 38.0), FLinearColor(1, 1, 1, 0.05f), 4);
			Rect(C, Row, FVector2D(52.0, 38.0), WithA(Tm.Color, 0.85f), 5);
			Text(C, Tx.Pos[r], St.Pos, EGfxFont::CardChip, Row, FVector2D(52.0, 38.0), 1, OnColor(Tm.Color), 6);
			Text(C, Tx.Row[r], St.Name, EGfxFont::CardRow, Row + FVector2D(66.0, 0.0), FVector2D(330.0, 38.0), 0, Paper, 6);
			Text(C, Tx.Ovr[r], CardOvr[s][r], EGfxFont::CardRow, Row + FVector2D(400.0, 0.0), FVector2D(50.0, 38.0), 2, Paper, 6);
			if (!St.Grade.IsEmpty())
			{
				const FLinearColor Gc = St.Grade.Contains(TEXT("X-")) ? FLinearColor(1.f, 0.15f, 0.01f) : St.Grade.Contains(TEXT("SUPER")) ? FLinearColor(0.46f, 0.26f, 1.f)
					: FLinearColor(0.17f, 0.48f, 1.f);
				const FVector2D Chip(Row.X + CardHalfW - 60.0 - 132.0, Row.Y + 9.0);
				Rect(C, Chip, FVector2D(122.0, 20.0), Gc, 5, 3.f);
				Text(C, Tx.Grade[r], St.Grade, EGfxFont::CardChip, Chip, FVector2D(122.0, 20.0), 1, Paper, 6);
			}
		}
		C.Alpha = A1;
	}
	// the VS plate between the marks
	const float VsA = bCheck ? 1.f : Out3(Phase(Clock, CardT0 + 0.35, 0.3f));
	const FVector2D Vc(TL.X + CardHalfW + CardGap * 0.5, TL.Y + CardHeadH + 6.0 + 107.0);
	Rect(C, Vc - FVector2D(45.0, 45.0), FVector2D(90.0, 90.0), WithA(Paper, 0.9f * VsA), 7, 45.f);
	Rect(C, Vc - FVector2D(41.0, 41.0), FVector2D(82.0, 82.0), WithA(Ink, VsA), 8, 41.f);
	Text(C, TxCardVs, VsWord, EGfxFont::CardVs, Vc - FVector2D(41.0, 41.0), FVector2D(82.0, 82.0), 1, WithA(Paper, VsA), 9);
	C.Alpha = A0;
}

void FBroadcastGfx::PaintBox(FGfxCanvas& C, float In)
{
	TAG_GFX_NAMES;
	const bool bCheck = C.Out == nullptr;
	static const FString QHead[6] = {TEXT("1ST"), TEXT("2ND"), TEXT("3RD"), TEXT("4TH"), TEXT("OT"), TEXT("TOTAL")};
	static const FString Dash(TEXT("-"));
	static const TArray<FString> Labs = []()
	{
		TArray<FString> Out;
		for (const TCHAR* Word : TagGfx::BoxLabels) Out.Add(Word);
		return Out;
	}();
	const double W = CardHalfW * 2.0 + CardGap;
	const double TableH = 150.0, CompH = 300.0, LeadH = 196.0, Gap = 12.0;
	const double H = CardHeadH + 6.0 + TableH + Gap + CompH + Gap + LeadH;
	const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), FMath::RoundToDouble((C.Size.Y - H) * 0.5 - 20.0));
	// the head strip: HALFTIME / FINAL, and who leads or won
	const float HeadA = bCheck ? 1.f : Out3(Phase(Clock, CardT0 + 0.2, 0.3f));
	Rect(C, TL, FVector2D(W, CardHeadH), WithA(Ink, 0.94f * HeadA), 0);
	Rect(C, TL + FVector2D(0.0, CardHeadH - 2.0), FVector2D(W, 2.0), WithA(Track, HeadA), 1);
	Text(C, TxCardKicker, Box.Kicker, EGfxFont::Kicker, TL + FVector2D(24.0, 0.0), FVector2D(W * 0.5 - 24.0, CardHeadH), 0, WithA(Paper, HeadA), 2);
	Text(C, TxCardCond, Box.Sub, EGfxFont::CardLine, TL + FVector2D(W * 0.5, 0.0), FVector2D(W * 0.5 - 24.0, CardHeadH), 2, WithA(Soft, HeadA), 2);
	// the panels rise into place as they fade up
	const float A1 = C.Alpha;
	C.Alpha = A1 * In;
	const double Rise = FMath::RoundToDouble((1.0 - In) * 30.0);
	const int32 Winner = Box.Tot[0].Score == Box.Tot[1].Score ? -1 : Box.Tot[0].Score > Box.Tot[1].Score ? 0 : 1;
	// ---- the score by period
	const FVector2D T0(TL.X, TL.Y + CardHeadH + 6.0 + Rise);
	const int32 NQ = FMath::Clamp(Box.NumQ, 1, 5);
	const double QX = 640.0, QW = 110.0, TotW = 150.0, TotX = QX + NQ * QW + 20.0;
	Rect(C, T0, FVector2D(W, TableH), WithA(Ink, 0.95f), 2);
	for (int32 p = 0; p < NQ; ++p) Text(C, TxBoxQHead[p], QHead[p], EGfxFont::ColLabel, FVector2D(T0.X + QX + p * QW, T0.Y + 6.0), FVector2D(QW, 22.0), 1, Soft, 3);
	Text(C, TxBoxQHead[5], QHead[5], EGfxFont::ColLabel, FVector2D(T0.X + TotX, T0.Y + 6.0), FVector2D(TotW, 22.0), 1, Soft, 3);
	for (int32 s = 0; s < 2; ++s)
	{
		const FGfxTeamId& Tm = Box.Team[s];
		const double RY = T0.Y + 34.0 + s * 56.0;
		Rect(C, FVector2D(T0.X + 8.0, RY), FVector2D(W - 16.0, 50.0), WithA(Tm.Color, 0.32f), 3);
		Rect(C, FVector2D(T0.X + 8.0, RY), FVector2D(6.0, 50.0), Tm.Color, 4);
		Image(C, Tm.Logo, FVector2D(T0.X + 24.0, RY + 3.0), FVector2D(44.0, 44.0), 4);
		Text(C, TxBoxTeam[s], Tm.Name, EGfxFont::L3Name, FVector2D(T0.X + 82.0, RY), FVector2D(QX - 100.0, 50.0), 0, Paper, 5);
		for (int32 p = 0; p < NQ; ++p) Text(C, TxBoxQ[s][p], BoxQ[s][p], EGfxFont::ColNum, FVector2D(T0.X + QX + p * QW, RY), FVector2D(QW, 50.0), 1, Soft, 5);
		Text(C, TxBoxQ[s][5], BoxQ[s][5], EGfxFont::ColNum, FVector2D(T0.X + TotX, RY), FVector2D(TotW, 50.0), 1, Winner == 1 - s ? Soft : Paper, 5);
	}
	// ---- the two teams, number by number: each side's value by the label, its share as a bar outward from it
	const FVector2D C0(TL.X, T0.Y + TableH + Gap);
	Rect(C, C0, FVector2D(W, CompH), WithA(Ink, 0.95f), 2);
	const double Cx = C0.X + W * 0.5, BarMax = 330.0;
	for (int32 r = 0; r < 9; ++r)
	{
		const double RY = C0.Y + 6.0 + r * 32.0;
		if (r % 2 == 0) Rect(C, FVector2D(C0.X + 8.0, RY), FVector2D(W - 16.0, 32.0), FLinearColor(1, 1, 1, 0.03f), 3);
		Text(C, TxBoxLab[r], Labs[r], EGfxFont::CardLabel, FVector2D(Cx - 130.0, RY), FVector2D(260.0, 32.0), 1, Soft, 4);
		Text(C, TxBoxVal[r][0], BoxVal[r][0], EGfxFont::CardRow, FVector2D(Cx - 300.0, RY), FVector2D(150.0, 32.0), 2, Paper, 4);
		Text(C, TxBoxVal[r][1], BoxVal[r][1], EGfxFont::CardRow, FVector2D(Cx + 150.0, RY), FVector2D(150.0, 32.0), 0, Paper, 4);
		const double Sum = BoxNum[r][0] + BoxNum[r][1];
		const double Sh0 = Sum > 0.0 ? BoxNum[r][0] / Sum : 0.0, Sh1 = Sum > 0.0 ? BoxNum[r][1] / Sum : 0.0;
		Rect(C, FVector2D(Cx - 320.0 - BarMax * Sh0, RY + 13.0), FVector2D(BarMax * Sh0, 6.0), Box.Team[0].Color, 4);
		Rect(C, FVector2D(Cx + 320.0, RY + 13.0), FVector2D(BarMax * Sh1, 6.0), Box.Team[1].Color, 4);
	}
	// ---- each side's leaders
	const FVector2D L0(TL.X, C0.Y + CompH + Gap);
	for (int32 s = 0; s < 2; ++s)
	{
		const FGfxTeamId& Tm = Box.Team[s];
		const double X = L0.X + s * (CardHalfW + CardGap);
		Rect(C, FVector2D(X, L0.Y), FVector2D(CardHalfW, LeadH), WithA(Ink, 0.95f), 2);
		Rect(C, FVector2D(X, L0.Y), FVector2D(CardHalfW, 4.0), Tm.Color, 3);
		for (int32 i = 0; i < 4; ++i)
		{
			const FGfxLeader& Ld = Box.Leaders[s][i];
			const double RY = L0.Y + 10.0 + i * 46.0;
			Rect(C, FVector2D(X + 16.0, RY + 6.0), FVector2D(92.0, 20.0), WithA(Tm.Color, 0.85f), 3, 3.f);
			Text(C, TxBoxLead[s][i][0], Ld.Role, EGfxFont::CardChip, FVector2D(X + 16.0, RY + 6.0), FVector2D(92.0, 20.0), 1, OnColor(Tm.Color), 4);
			Text(C, TxBoxLead[s][i][1], Ld.bOn ? Ld.Name : Dash, EGfxFont::CardRow, FVector2D(X + 124.0, RY), FVector2D(CardHalfW - 140.0, 26.0), 0, Ld.bOn ? Paper : Soft, 4);
			if (Ld.bOn) Text(C, TxBoxLead[s][i][2], Ld.Line, EGfxFont::BoxLine, FVector2D(X + 124.0, RY + 24.0), FVector2D(CardHalfW - 140.0, 20.0), 0, Soft, 4);
		}
	}
	C.Alpha = A1;
}

void FBroadcastGfx::PaintPotg(FGfxCanvas& C, float In)
{
	TAG_GFX_NAMES;
	const bool bCheck = C.Out == nullptr;
	static const FString Title(TEXT("PLAYER OF THE GAME"));
	const double W = 1180.0, BodyH = 420.0, LeftW = 360.0;
	const double H = CardHeadH + 6.0 + BodyH;
	const FVector2D TL(FMath::RoundToDouble((C.Size.X - W) * 0.5), FMath::RoundToDouble((C.Size.Y - H) * 0.5 - 20.0));
	const FLinearColor Tc = Potg.Team.Color;
	// the head strip: the title, and why him
	const float HeadA = bCheck ? 1.f : Out3(Phase(Clock, CardT0 + 0.2, 0.3f));
	Rect(C, TL, FVector2D(W, CardHeadH), WithA(Ink, 0.94f * HeadA), 0);
	Rect(C, TL + FVector2D(0.0, CardHeadH - 2.0), FVector2D(W, 2.0), WithA(Track, HeadA), 1);
	Text(C, TxCardKicker, Title, EGfxFont::Kicker, TL + FVector2D(24.0, 0.0), FVector2D(W * 0.5 - 24.0, CardHeadH), 0, WithA(Paper, HeadA), 2);
	Text(C, TxPotgWhy, Potg.Why, EGfxFont::CardLine, TL + FVector2D(W * 0.5, 0.0), FVector2D(W * 0.5 - 24.0, CardHeadH), 2, WithA(Soft, HeadA), 2);
	const float A1 = C.Alpha;
	C.Alpha = A1 * In;
	const FVector2D B0(TL.X, TL.Y + CardHeadH + 6.0 + FMath::RoundToDouble((1.0 - In) * 30.0));
	Rect(C, B0 + FVector2D(0.0, 6.0), FVector2D(W, BodyH), FLinearColor(0, 0, 0, 0.3f), 1);
	Rect(C, B0, FVector2D(W, BodyH), WithA(Ink, 0.95f), 2);
	// the left: his team's colour, its mark, his number, his grade
	Rect(C, B0, FVector2D(LeftW, BodyH), Tc, 3);
	Rect(C, B0 + FVector2D(0.0, BodyH * 0.55), FVector2D(LeftW, BodyH * 0.45), FLinearColor(0, 0, 0, 0.22f), 4);
	Image(C, Potg.Team.Logo, B0 + FVector2D((LeftW - 150.0) * 0.5, 24.0), FVector2D(150.0, 150.0), 5);
	Text(C, TxPotgNum, Potg.Number, EGfxFont::PotgNum, B0 + FVector2D(0.0, 186.0), FVector2D(LeftW, 130.0), 1, OnColor(Tc), 5);
	if (!Potg.Grade.IsEmpty())
	{
		const FLinearColor Gc = Potg.Grade.Contains(TEXT("X-")) ? FLinearColor(1.f, 0.15f, 0.01f) : Potg.Grade.Contains(TEXT("SUPER")) ? FLinearColor(0.46f, 0.26f, 1.f)
			: FLinearColor(0.17f, 0.48f, 1.f);
		const FVector2D Chip(B0.X + (LeftW - 150.0) * 0.5, B0.Y + 340.0);
		Rect(C, Chip, FVector2D(150.0, 24.0), Gc, 5, 3.f);
		Text(C, TxPotgGrade, Potg.Grade, EGfxFont::CardChip, Chip, FVector2D(150.0, 24.0), 1, Paper, 6);
	}
	// the right: what he plays, his name, his team; his four numbers; his whole game
	const double Rx = B0.X + LeftW + 40.0, RW = W - LeftW - 80.0;
	Text(C, TxPotgPos, Potg.Pos, EGfxFont::CardCity, FVector2D(Rx, B0.Y + 26.0), FVector2D(RW, 30.0), 0, FLinearColor(0.88f, 0.91f, 0.96f), 5);
	Text(C, TxPotgName, Potg.Name, EGfxFont::PotgName, FVector2D(Rx, B0.Y + 56.0), FVector2D(RW, 62.0), 0, Paper, 5);
	Text(C, TxPotgKicker, Potg.Team.Name, EGfxFont::Kicker, FVector2D(Rx, B0.Y + 122.0), FVector2D(RW, 22.0), 0, Track, 5);
	Rect(C, FVector2D(Rx, B0.Y + 160.0), FVector2D(RW, 1.0), Hair, 4);
	const double SW = RW / 4.0;
	for (int32 i = 0; i < Potg.NumStats && i < 4; ++i)
	{
		const double Sx = Rx + i * SW;
		Text(C, TxPotgStatV[i], Potg.Stats[i].Value, EGfxFont::PotgStat, FVector2D(Sx, B0.Y + 174.0), FVector2D(SW - 12.0, 60.0), 0, Paper, 5);
		Text(C, TxPotgStatL[i], Potg.Stats[i].Label, EGfxFont::CardLabel, FVector2D(Sx, B0.Y + 236.0), FVector2D(SW - 12.0, 18.0), 0, Soft, 5);
	}
	Rect(C, FVector2D(Rx, B0.Y + 276.0), FVector2D(RW, 1.0), Hair, 4);
	Text(C, TxPotgLine, Potg.Line, EGfxFont::BoxLine, FVector2D(Rx, B0.Y + 290.0), FVector2D(RW, 24.0), 0, Soft, 5);
	C.Alpha = A1;
}

// ------------------------------------------------------------------ the fit check
int32 FBroadcastGfx::CheckFits(TArray<FString>& Fails)
{
	static const FIntPoint Res[3] = {FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440)};
	int32 N = 0;
	for (const FIntPoint& R : Res)
	{
		const float S = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(R);
		if (S <= 0.f) continue;
		const FString Name = FString::Printf(TEXT("%dp"), R.Y);
		FGfxCanvas C;
		C.Scale = S;
		C.Size = FVector2D(R.X / S, R.Y / S);
		C.Fails = &Fails;
		C.Res = *Name;
		PaintAll(C);
		N += C.Checked;
	}
	return N;
}

int32 FBroadcastGfx::CheckLeagueFits(const tag::League& L, TArray<FString>& Fails)
{
	TAG_GFX_NAMES;
	static const FIntPoint Res[3] = {FIntPoint(1280, 720), FIntPoint(1920, 1080), FIntPoint(2560, 1440)};
	int32 N = 0;
	for (const auto& T : L)
	{
		if (!T) continue;
		const FGfxMatchup M = MatchupFor(*T, *T, &L, FString());
		const FGfxMatchupTeam& Mt = M.Side[0];
		for (const FIntPoint& R : Res)
		{
			const float S = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(R);
			if (S <= 0.f) continue;
			const FString Name = FString::Printf(TEXT("%dp %s"), R.Y, *Mt.Abbr);
			FGfxCanvas C;
			C.Scale = S;
			C.Fails = &Fails;
			C.Res = *Name;
			FGfxText Scratch;
			// (the boxes PaintCard and PaintBug put them in)
			Text(C, Scratch, Mt.City, EGfxFont::CardCity, FVector2D::ZeroVector, FVector2D(CardHalfW - 60.0, 30.0), 1, Paper, 0);
			Text(C, Scratch, Mt.Name, EGfxFont::CardName, FVector2D::ZeroVector, FVector2D(CardHalfW - 60.0, 66.0), 1, Paper, 0);
			Text(C, Scratch, Mt.Scheme, EGfxFont::CardLine, FVector2D::ZeroVector, FVector2D(CardHalfW - 60.0, 24.0), 1, Paper, 0);
			for (int32 r = 0; r < Mt.NumStars; ++r) Text(C, Scratch, Mt.Stars[r].Name, EGfxFont::CardRow, FVector2D::ZeroVector, FVector2D(330.0, 38.0), 0, Paper, 0);
			Text(C, Scratch, Mt.Abbr, EGfxFont::BugAbbr, FVector2D::ZeroVector, FVector2D(96.0, 32.0), 0, Paper, 0);
			// the box's team rows and the player of the game's team line (PaintBox, PaintPotg)
			Text(C, Scratch, Mt.Name, EGfxFont::L3Name, FVector2D::ZeroVector, FVector2D(640.0 - 100.0, 50.0), 0, Paper, 0);
			Text(C, Scratch, Mt.Name, EGfxFont::Kicker, FVector2D::ZeroVector, FVector2D(1180.0 - 360.0 - 80.0, 22.0), 0, Paper, 0);
			// the down box's longest words, the lower third's city line
			Text(C, Scratch, Mt.Abbr + TEXT(" WIN"), EGfxFont::BugDown, FVector2D::ZeroVector, FVector2D(DownW - 30.0, 30.0), 0, Paper, 0);
			Text(C, Scratch, FString(TEXT("WR")) + Dot() + Mt.City, EGfxFont::L3Sub, FVector2D::ZeroVector, FVector2D(560.0 - 40.0, 20.0), 0, Paper, 0);
			N += C.Checked;
		}
	}
	return N;
}

// ------------------------------------------------------------------ -TagGfxShots
// Every state of the bug and each graphic, on a flat field colour and then over a real frame of a watched game (held
// with the engine paused), each picture with its fit check; every team's mark; the stat tag picker on made-up plays.
// Pictures: Saved/Screenshots/<platform>/tag_gfx_<w>x<h>_NN_<name>.png. Run with -RenderOffscreen -ForceRes -ResX= -ResY=
// -TagNoLoad -LiveCoding=0 (and -TagNoBooth: the booth's open holds the kickoff); "TAG RESULT TagGfxShots" ends it.
void SGridironOverlay::StartGfxShots()
{
	TAG_GFX_NAMES;
	struct FState { TArray<TFunction<bool()>> Steps; int32 I = 0, Wait = 0, N = 0, Pass = 0, Fail = 0; double StepT = 0; FString Res; };
	const TSharedRef<FState> S = MakeShared<FState>();
	S->Steps.Reserve(256);
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
	auto Do = [S](TFunction<void()> F) { S->Steps.Add([F] { F(); return true; }); };
	auto Frames = [S](int32 N) { S->Steps.Add([S, N] { if (++S->Wait < N) return false; S->Wait = 0; return true; }); };
	auto Check = [S](bool bOk, const FString& What)
	{
		(bOk ? S->Pass : S->Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG gfx %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	auto UntilT = [S](TFunction<bool()> F, double Secs)
	{
		S->Steps.Add([S] { S->StepT = FPlatformTime::Seconds(); return true; });
		S->Steps.Add([S, F, Secs] { return F() || FPlatformTime::Seconds() - S->StepT > Secs; });
	};
	// a picture of what is up once it has come in (at least MinSecs; bSettle: and every move has finished), its texts
	// against their boxes at the three resolutions
	auto Shot = [S, W, Check, Frames](const FString& Name, double MinSecs, bool bSettle)
	{
		S->Steps.Add([S] { S->StepT = FPlatformTime::Seconds(); return true; });
		S->Steps.Add([S, W, MinSecs, bSettle]
		{
			const TSharedPtr<SGridironOverlay> O = W.Pin();
			const double T = FPlatformTime::Seconds() - S->StepT;
			return !O || T > 8.0 || (T >= MinSecs && (!bSettle || (O->GfxPtr && O->GfxPtr->Settled())));
		});
		S->Steps.Add([S, W, Name, Check]
		{
			const TSharedPtr<SGridironOverlay> O = W.Pin();
			if (!O || !O->GfxPtr) return true;
			const FString File = FString::Printf(TEXT("tag_gfx_%s_%02d_%s.png"), *S->Res, S->N++, *Name);
			FScreenshotRequest::RequestScreenshot(File, true, false);
			TArray<FString> Fails;
			const int32 Texts = O->GfxPtr->CheckFits(Fails);
			Check(Texts > 0 && Fails.Num() == 0, FString::Printf(TEXT("%s: %d texts fit their boxes at 720p / 1080p / 1440p%s%s"), *Name, Texts,
				Fails.Num() ? TEXT(" - ") : TEXT(""), *FString::Join(Fails, TEXT("; "))));
			UE_LOG(LogTemp, Display, TEXT("TAG gfx shot %s"), *File);
			return true;
		});
		Frames(4); // (the picture is taken as the frame ends)
	};
	auto GfxOf = [W]() -> FBroadcastGfx* { const TSharedPtr<SGridironOverlay> O = W.Pin(); return O && O->GfxPtr ? O->GfxPtr.Get() : nullptr; };
	// the scripted bug: the menu's two teams, the second quarter
	auto BaseBug = [W]() -> FGfxScoreBug
	{
		FGfxScoreBug B;
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || O->Director->League.size() < 2) return B;
		const tag::League& L = O->Director->League;
		const int32 A = FMath::Clamp(O->SelAway, 0, static_cast<int32>(L.size()) - 1), H = FMath::Clamp(O->SelHome, 0, static_cast<int32>(L.size()) - 1);
		B.Side[0] = FBroadcastGfx::BugSideFor(*L[A]);
		B.Side[1] = FBroadcastGfx::BugSideFor(*L[H]);
		B.Side[0].Score = 14;
		B.Side[1].Score = 10;
		B.Side[0].Timeouts = 2;
		B.Side[0].bBall = true;
		B.Period = TEXT("2ND");
		B.Clock = TEXT("4:37");
		B.Down = TEXT("1ST & 10");
		B.Spot = B.Side[1].Abbr + TEXT(" 42");
		return B;
	};
	auto Alert = [GfxOf](EGfxAlert K, const FString& Words, const FString& Detail, int32 Side, float Secs)
	{
		if (FBroadcastGfx* X = GfxOf())
		{
			X->ClearGraphics();
			FGfxAlert A;
			A.Kind = K;
			A.Text = Words;
			A.Detail = Detail;
			A.Side = Side;
			A.Secs = Secs;
			X->ShowAlert(A);
		}
	};
	// the league's longest name on the lower third (the worst case for its plate)
	auto LongL3 = [W]() -> FGfxLowerThird
	{
		FGfxLowerThird L;
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		const tag::Player* Best = nullptr;
		const tag::Team* BestT = nullptr;
		if (O && O->Director)
			for (const auto& T : O->Director->League)
				for (const auto& P : T->roster)
					if (P && (!Best || P->last.size() > Best->last.size())) { Best = P.get(); BestT = T.get(); }
		L.Name = Best ? FString::Printf(TEXT("#%d  %s. %s"), Best->number, *Up(Best->first.substr(0, 1)), *Up(Best->last)) : FString(TEXT("#89  R. OCHOA"));
		L.Sub = (Best ? FString(ANSI_TO_TCHAR(tag::posName(Best->pos))) : FString(TEXT("WR"))) + Dot() + (BestT ? Up(BestT->city) : FString(TEXT("PORTLAND")));
		L.Stats = FString(TEXT("6 REC")) + Dot() + TEXT("112 YDS") + Dot() + TEXT("2 TD");
		L.bTag = true;
		L.Tag.Label = TEXT("YARDS AFTER THE CATCH");
		L.Tag.Value = TEXT("27 YD");
		L.Tag.Field = TEXT("PlayResult::yac");
		L.Tag.Raw = 27;
		L.Secs = 60.f;
		return L;
	};
	auto TwoTags = [](bool bTwo) -> FGfxStatTags
	{
		FGfxStatTags T;
		T.Tag[0].Label = bTwo ? TEXT("SEPARATION AT THE CATCH") : TEXT("CATCH PROBABILITY");
		T.Tag[0].Value = bTwo ? TEXT("0.4 YD") : TEXT("24%");
		T.Tag[0].Field = TEXT("script");
		T.Num = 1;
		if (bTwo)
		{
			T.Tag[1].Label = TEXT("AIR YARDS");
			T.Tag[1].Value = TEXT("43 YD");
			T.Tag[1].Field = TEXT("script");
			T.Num = 2;
		}
		T.Delay = 0.1f;
		T.Secs = 60.f;
		return T;
	};
	auto DriveOf = [](int32 Side, const TCHAR* Result, int32 Plays, int32 Yards, int32 Secs, const TCHAR* Start) -> FGfxDriveSummary
	{
		FGfxDriveSummary D;
		D.Side = Side;
		D.Result = Result;
		D.Plays = Plays;
		D.Yards = Yards;
		D.Seconds = Secs;
		D.Start = Start;
		D.Secs = 60.f;
		return D;
	};
	auto DownOf = [](int32 Side, const FString& Abbr, const TCHAR* Down, const TCHAR* Line, bool bFourth) -> FGfxThirdDown
	{
		FGfxThirdDown T;
		T.Side = Side;
		T.Down = Down;
		T.Kicker = Abbr + (bFourth ? TEXT(" ON 4TH DOWN TODAY") : TEXT(" ON 3RD DOWN TODAY"));
		T.Line = Line;
		T.Secs = 60.f;
		return T;
	};
	auto CardOf = [W](int32 A, int32 H) -> FGfxMatchup
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || O->Director->League.size() < 2) return FGfxMatchup();
		const tag::League& L = O->Director->League;
		A = FMath::Clamp(A, 0, static_cast<int32>(L.size()) - 1);
		H = FMath::Clamp(H, 0, static_cast<int32>(L.size()) - 1);
		return FBroadcastGfx::MatchupFor(*L[A], *L[H], &L, FString(TEXT("Clear, 71F, wind 2 mph")) + Dot() + TEXT("Night"));
	};
	// the two teams with the longest names on one card
	auto LongCard = [W, CardOf]() -> FGfxMatchup
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		int32 A = 0, H = 1;
		if (O && O->Director)
		{
			const tag::League& L = O->Director->League;
			size_t La = 0, Lh = 0;
			for (int32 i = 0; i < static_cast<int32>(L.size()); ++i)
			{
				const size_t Len = L[i]->name.size() + L[i]->city.size();
				if (Len > La) { Lh = La; H = A; La = Len; A = i; }
				else if (Len > Lh) { Lh = Len; H = i; }
			}
		}
		return CardOf(A, H);
	};

	Frames(60);
	Do([W, S, BaseBug]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->GfxPtr) return;
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			const FIntPoint Vp = GEngine->GameViewport->Viewport->GetSizeXY();
			S->Res = FString::Printf(TEXT("%dx%d"), Vp.X, Vp.Y);
		}
		if (S->Res.IsEmpty()) S->Res = TEXT("res");
		UE_LOG(LogTemp, Display, TEXT("TAG gfx shots at %s (slate scale %.3f)"), *S->Res, O->GetTickSpaceGeometry().GetAccumulatedLayoutTransform().GetScale());
		O->ClearModal();
		O->bHud = true;
		O->GfxPtr->SetScripted(true);
		O->GfxPtr->SetBackdrop(true);
		O->GfxPtr->ClearGraphics();
		O->GfxPtr->SetScoreBug(BaseBug());
	});
	// ---- every team's mark: the brush is made and Slate loads its image (the renderer's own load, by the name the
	// dynamic brush draws it by: RosterUI::Logo)
	Do([W, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director) { Check(false, TEXT("no director: no league to check the marks of")); return; }
		FSlateRenderer* Rr = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
		for (const auto& T : O->Director->League)
		{
			const FString Abbr = Up(T->abbr);
			const FSlateBrush* Br = RosterUI::Logo(*T);
			const FIntPoint Px = Br && Rr ? Rr->GenerateDynamicImageResource(Br->GetResourceName()) : FIntPoint::ZeroValue;
			Check(Br && Px.X >= 64 && Px.Y >= 64, FString::Printf(TEXT("%s's mark loads (%s, %dx%d)"), *Abbr, Br ? *Br->GetResourceName().ToString() : TEXT("no brush: the file is missing"),
				Px.X, Px.Y));
		}
	});
	// ---- every team's words on the card and the bug, at the three resolutions
	Do([W, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || !O->GfxPtr) return;
		TArray<FString> Fails;
		const int32 N = O->GfxPtr->CheckLeagueFits(O->Director->League, Fails);
		Check(N > 0 && Fails.Num() == 0, FString::Printf(TEXT("every team's names fit the card and the bug: %d texts%s%s"), N, Fails.Num() ? TEXT(" - ") : TEXT(""),
			*FString::Join(Fails, TEXT("; "))));
	});
	// ---- the stat tag picker on made-up plays: only real numbers, only remarkable ones, never an unset field
	Do([W, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		const tag::Player* Rec = O && O->Director && O->Director->League.size() && O->Director->League[0]->roster.size() ? O->Director->League[0]->roster[0].get() : nullptr;
		{
			tag::PlayResult R;
			R.kind = tag::PlayKind::Pass;
			R.complete = true;
			R.receiver = const_cast<tag::Player*>(Rec);
			R.airYards = 8;
			R.yac = 3;
			R.yards = 11;
			R.catchSep = 2.0;
			R.timeToThrow = 2.7;
			tag::Ball B;
			B.fateP = 0.30;
			B.fatePbu = 0.20;
			const FGfxStatTags T = FBroadcastGfx::PickStatTags(R, B, 0.f);
			Check(T.Num == 1 && T.Tag[0].Label == TEXT("CATCH PROBABILITY") && T.Tag[0].Value == TEXT("24%"),
				FString::Printf(TEXT("a contested catch: one tag, catch probability 24%% (got %d: %s %s)"), T.Num, T.Num ? *T.Tag[0].Label : TEXT("-"), T.Num ? *T.Tag[0].Value : TEXT("-")));
		}
		{
			tag::PlayResult R;
			R.kind = tag::PlayKind::Pass;
			R.complete = true;
			R.receiver = const_cast<tag::Player*>(Rec);
			R.airYards = 6;
			R.yac = 2;
			R.yards = 8;
			R.catchSep = 2.5;
			R.timeToThrow = 2.8;
			tag::Ball B;
			B.fateP = 0.92;
			B.fatePbu = 0.0;
			const FGfxStatTags T = FBroadcastGfx::PickStatTags(R, B, 15.f);
			Check(T.Num == 0, FString::Printf(TEXT("a routine catch: no tag (got %d)"), T.Num));
		}
		{
			tag::PlayResult R;
			R.kind = tag::PlayKind::Run;
			R.yards = 4;
			R.catchSep = -1.0;
			tag::Ball B; // (fateP unset: -1)
			const FGfxStatTags T = FBroadcastGfx::PickStatTags(R, B, 0.f);
			Check(T.Num == 0, FString::Printf(TEXT("a run with every pass field unset (-1): no tag (got %d)"), T.Num));
		}
		{
			tag::PlayResult R;
			R.kind = tag::PlayKind::Pass;
			R.complete = true;
			R.receiver = const_cast<tag::Player*>(Rec);
			R.airYards = 44;
			R.yac = 16;
			R.yards = 60;
			R.catchSep = 0.4;
			R.timeToThrow = 3.1;
			tag::Ball B;
			B.fateP = 0.71;
			B.fatePbu = 0.10;
			const FGfxStatTags T = FBroadcastGfx::PickStatTags(R, B, 21.4f);
			Check(T.Num == 2 && !T.Tag[0].Field.Equals(T.Tag[1].Field), FString::Printf(TEXT("a deep tight-window catch: two tags (got %d: %s / %s)"), T.Num,
				T.Num > 0 ? *T.Tag[0].Label : TEXT("-"), T.Num > 1 ? *T.Tag[1].Label : TEXT("-")));
		}
		{
			tag::PlayResult R;
			R.kind = tag::PlayKind::Sack;
			R.timeToThrow = 2.1;
			tag::Ball B;
			const FGfxStatTags T = FBroadcastGfx::PickStatTags(R, B, 0.f);
			Check(T.Num == 1 && T.Tag[0].Label == TEXT("TIME TO SACK") && T.Tag[0].Value == TEXT("2.10 S"),
				FString::Printf(TEXT("a quick sack: time to sack 2.10 s (got %d: %s)"), T.Num, T.Num ? *T.Tag[0].Value : TEXT("-")));
		}
	});

	// ---- the bug and its states, on the field colour
	Shot(TEXT("bug"), 0.6, true);
	Do([GfxOf, BaseBug] { if (FBroadcastGfx* X = GfxOf()) { FGfxScoreBug B = BaseBug(); B.Down = TEXT("3RD & 7"); B.Spot = B.Side[1].Abbr + TEXT(" 34"); B.PlayClock = 8; X->SetScoreBug(B); } });
	Shot(TEXT("bug_playclock"), 0.5, true);
	Do([GfxOf, BaseBug] { if (FBroadcastGfx* X = GfxOf()) { FGfxScoreBug B = BaseBug(); B.Down = TEXT("3RD & 7"); B.Spot = B.Side[1].Abbr + TEXT(" 34"); B.PlayClock = 4; X->SetScoreBug(B); } });
	Shot(TEXT("bug_playclock_red"), 0.3, true);
	Do([GfxOf, BaseBug] { if (FBroadcastGfx* X = GfxOf()) X->SetScoreBug(BaseBug()); });
	// (the longest call the director gives: "<team>: <foul> (#<n> <name>)", ApplyPlay)
	Do([Alert] { Alert(EGfxAlert::Flag, TEXT("FLAG"), TEXT("CHS: DEFENSIVE PASS INTERFERENCE (#24 K.OKONKWO-BRANDT)"), -1, 60.f); });
	Shot(TEXT("flag"), 0.5, true);
	Do([GfxOf, BaseBug, Alert]
	{
		if (FBroadcastGfx* X = GfxOf())
		{
			FGfxScoreBug B = BaseBug();
			B.Side[1].Timeouts = 1;
			X->SetScoreBug(B);
			Alert(EGfxAlert::Timeout, TEXT("TIMEOUT"), B.Side[1].Abbr + Dot() + TEXT("1 LEFT"), 1, 60.f);
		}
	});
	Shot(TEXT("timeout_drain"), 0.55, false);
	Shot(TEXT("timeout"), 0.2, true);
	Do([GfxOf, Alert] { if (FBroadcastGfx* X = GfxOf()) Alert(EGfxAlert::RedZone, TEXT("RED ZONE"), X->ScoreBug().Side[0].Abbr + TEXT(" TODAY  2 TD IN 3 TRIPS"), 0, 60.f); });
	Shot(TEXT("redzone"), 0.5, true);
	Do([Alert] { Alert(EGfxAlert::TwoMinute, TEXT("TWO-MINUTE WARNING"), FString(), -1, 60.f); });
	Shot(TEXT("twominute"), 0.5, true);
	Do([GfxOf, BaseBug, Alert]
	{
		if (FBroadcastGfx* X = GfxOf())
		{
			FGfxScoreBug B = BaseBug();
			B.Side[0].Score = 20;
			B.Down = TEXT("TOUCHDOWN");
			B.Spot.Reset();
			X->SetScoreBug(B);
			Alert(EGfxAlert::Touchdown, TEXT("TOUCHDOWN"), B.Side[0].Abbr, 0, 60.f);
		}
	});
	Shot(TEXT("touchdown_flash"), 0.35, false);
	Shot(TEXT("touchdown"), 0.2, true);
	Do([GfxOf, BaseBug, Alert] { if (FBroadcastGfx* X = GfxOf()) { X->SetScoreBug(BaseBug()); Alert(EGfxAlert::FirstDown, TEXT("FIRST DOWN"), FString(), -1, 60.f); } });
	Shot(TEXT("firstdown"), 0.5, true);
	Do([GfxOf, BaseBug, Alert]
	{
		if (FBroadcastGfx* X = GfxOf())
		{
			FGfxScoreBug B = BaseBug();
			B.Side[1].Score = 13;
			B.Down = TEXT("KICKOFF");
			B.Spot.Reset();
			B.Side[0].bBall = false;
			B.Side[1].bBall = true;
			X->SetScoreBug(B);
			Alert(EGfxAlert::FieldGoal, TEXT("FIELD GOAL"), B.Side[1].Abbr, 1, 60.f);
		}
	});
	Shot(TEXT("fieldgoal"), 0.5, true);
	Do([GfxOf, BaseBug]
	{
		if (FBroadcastGfx* X = GfxOf())
		{
			X->ClearGraphics();
			FGfxScoreBug B = BaseBug();
			B.Side[0].Score = 27;
			B.Side[1].Score = 24;
			B.Side[0].bBall = false;
			B.Period = TEXT("FINAL");
			B.Clock.Reset();
			B.Down = B.Side[0].Abbr + TEXT(" WIN");
			B.Spot.Reset();
			X->SetScoreBug(B);
		}
	});
	Shot(TEXT("final"), 0.5, true);
	Do([GfxOf, BaseBug]
	{
		if (FBroadcastGfx* X = GfxOf())
		{
			FGfxScoreBug B = BaseBug();
			B.Period = TEXT("OT");
			B.Side[0].TimeoutsMax = B.Side[1].TimeoutsMax = 2;
			B.Side[0].Timeouts = 2;
			B.Side[1].Timeouts = 1;
			B.Down = TEXT("1ST & GOAL");
			B.Spot = B.Side[1].Abbr + TEXT(" 8");
			X->SetScoreBug(B);
		}
	});
	Shot(TEXT("bug_overtime_goal"), 0.5, true);
	// ---- the lower thirds and the tags
	Do([GfxOf, BaseBug, LongL3] { if (FBroadcastGfx* X = GfxOf()) { X->SetScoreBug(BaseBug()); X->ClearGraphics(); X->ShowLowerThird(LongL3()); } });
	Shot(TEXT("lowerthird"), 0.6, true);
	Do([GfxOf, TwoTags] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowStatTags(TwoTags(false)); } });
	Shot(TEXT("stattag"), 0.6, true);
	Do([GfxOf, TwoTags] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowStatTags(TwoTags(true)); } });
	Shot(TEXT("stattags_two"), 0.8, true);
	Do([GfxOf, DriveOf] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowDriveSummary(DriveOf(0, TEXT("TOUCHDOWN"), 8, 75, 252, TEXT("OWN 25"))); } });
	Shot(TEXT("drive_td"), 0.6, true);
	Do([GfxOf, DriveOf] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowDriveSummary(DriveOf(1, TEXT("TURNOVER ON DOWNS"), 13, 61, 468, TEXT("OWN 12"))); } });
	Shot(TEXT("drive_downs"), 0.6, true);
	Do([GfxOf, DownOf] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowThirdDown(DownOf(0, X->ScoreBug().Side[0].Abbr, TEXT("3RD & 8"), TEXT("4 OF 9  ·  44%"), false)); } });
	Shot(TEXT("third_down"), 0.6, true);
	Do([GfxOf, DownOf] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowThirdDown(DownOf(1, X->ScoreBug().Side[1].Abbr, TEXT("4TH & GOAL"), TEXT("1 OF 2  ·  50%"), true)); } });
	Shot(TEXT("fourth_down"), 0.6, true);
	// ---- the matchup card (the menu's teams; the two longest names)
	Do([GfxOf, W, CardOf] { const TSharedPtr<SGridironOverlay> O = W.Pin(); if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowMatchupCard(CardOf(O ? O->SelAway : 1, O ? O->SelHome : 0)); } });
	Shot(TEXT("matchup"), 0.9, true);
	Do([GfxOf, LongCard] { if (FBroadcastGfx* X = GfxOf()) X->ShowMatchupCard(LongCard()); });
	Shot(TEXT("matchup_longest"), 0.9, true);
	Do([GfxOf] { if (FBroadcastGfx* X = GfxOf()) X->HideMatchupCard(); });

	// ---- the flag's panel, the booth's team number, the halftime / final box and the player of the game (the longest
	// names and the biggest numbers the game can give them)
	auto BigBox = [W](bool bFinal) -> FGfxBoxStats
	{
		FGfxBoxStats B;
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || O->Director->League.size() < 2) return B;
		const tag::League& L = O->Director->League;
		int32 Ix[2] = {0, 1};
		size_t Len[2] = {0, 0};
		const tag::Player* LongMan = nullptr;
		for (int32 i = 0; i < static_cast<int32>(L.size()); ++i)
		{
			const size_t N = L[i]->name.size() + L[i]->city.size();
			if (N > Len[0]) { Len[1] = Len[0]; Ix[1] = Ix[0]; Len[0] = N; Ix[0] = i; }
			else if (N > Len[1]) { Len[1] = N; Ix[1] = i; }
			for (const auto& P : L[i]->roster)
				if (P && (!LongMan || P->last.size() > LongMan->last.size())) LongMan = P.get();
		}
		const FString Man = LongMan ? FString::Printf(TEXT("#%d  %s. %s"), LongMan->number, *Up(LongMan->first.substr(0, 1)), *Up(LongMan->last)) : FString(TEXT("#55  T. WASHINGTON"));
		static const TCHAR* const Lines[4] = {TEXT("38/52  ·  487 YDS  ·  6 TD  ·  3 INT"), TEXT("31 CAR  ·  301 YDS  ·  9.7 AVG  ·  4 TD"),
			TEXT("14 REC  ·  288 YDS  ·  4 TD"), TEXT("17 TKL  ·  4 SK  ·  2 INT  ·  2 FF  ·  3 PD")};
		static const TCHAR* const Roles[4] = {TEXT("PASSING"), TEXT("RUSHING"), TEXT("RECEIVING"), TEXT("DEFENSE")};
		for (int32 s = 0; s < 2; ++s)
		{
			const tag::Team& T = *L[Ix[s]];
			const FGfxBugSide Look = FBroadcastGfx::BugSideFor(T);
			B.Team[s].City = Up(T.city);
			B.Team[s].Name = Up(T.name);
			B.Team[s].Abbr = Look.Abbr;
			B.Team[s].Color = Look.Color;
			B.Team[s].Color2 = Look.Color2;
			B.Team[s].Logo = Look.Logo;
			FGfxTeamTotals& Tt = B.Tot[s];
			const int32 Qs[5] = {s == 0 ? 21 : 24, s == 0 ? 28 : 24, 0, s == 0 ? 0 : 7, s == 0 ? 7 : 0};
			for (int32 p = 0; p < 5; ++p) Tt.ByQ[p] = bFinal || p < 2 ? Qs[p] : -1;
			Tt.Score = bFinal ? Qs[0] + Qs[1] + Qs[2] + Qs[3] + Qs[4] : Qs[0] + Qs[1];
			Tt.Yards = s == 0 ? 612 : 589;
			Tt.PassYds = s == 0 ? 487 : 402;
			Tt.RushYds = s == 0 ? 301 : 187;
			Tt.FirstDowns = s == 0 ? 38 : 33;
			Tt.ThirdConv = s == 0 ? 12 : 9;
			Tt.ThirdAtt = s == 0 ? 19 : 17;
			Tt.Turnovers = s == 0 ? 6 : 4;
			Tt.TopSecs = s == 0 ? 41 * 60 + 22 : 18 * 60 + 38;
			Tt.Sacks = s == 0 ? 9 : 7;
			Tt.Penalties = s == 0 ? 15 : 12;
			Tt.PenYds = s == 0 ? 145 : 110;
			for (int32 i = 0; i < 4; ++i)
			{
				B.Leaders[s][i].bOn = true;
				B.Leaders[s][i].Role = Roles[i];
				B.Leaders[s][i].Name = Man;
				B.Leaders[s][i].Line = Lines[i];
			}
		}
		B.NumQ = bFinal ? 5 : 2;
		B.Kicker = bFinal ? FString(TEXT("FINAL")) + Dot() + TEXT("OVERTIME") : FString(TEXT("HALFTIME"));
		B.Sub = B.Team[0].City + (bFinal ? TEXT(" WINS ") : TEXT(" LEADS ")) + FString::Printf(TEXT("%d-%d"), B.Tot[0].Score, B.Tot[1].Score);
		return B;
	};
	Do([GfxOf] { if (FBroadcastGfx* X = GfxOf())
	{
		X->ClearGraphics();
		FGfxPenalty P;
		P.Side = 0;
		P.Foul = TEXT("HOLDING");
		P.Team = X->ScoreBug().Side[0].Abbr;
		P.Who = TEXT("OFFENSE");
		P.Number = TEXT("#74");
		P.Yards = TEXT("10 YARDS");
		P.Result = TEXT("REPEAT 1ST DOWN");
		P.Secs = 60.f;
		X->ShowPenalty(P);
	} });
	Shot(TEXT("flag_panel"), 0.6, true);
	Do([GfxOf] { if (FBroadcastGfx* X = GfxOf())
	{
		X->ClearGraphics();
		FGfxPenalty P;
		P.Side = 1;
		P.Foul = TEXT("OFFENSIVE PASS INTERFERENCE");
		P.Team = TEXT("LOU");
		P.Who = TEXT("DEFENSE");
		P.Number = TEXT("#88");
		P.Yards = TEXT("HALF THE DISTANCE");
		P.Result = TEXT("AUTOMATIC 1ST DOWN");
		P.Secs = 60.f;
		X->ShowPenalty(P);
	} });
	Shot(TEXT("flag_panel_longest"), 0.6, true);
	Do([GfxOf] { if (FBroadcastGfx* X = GfxOf())
	{
		X->ClearGraphics();
		FGfxTeamStat S2;
		S2.Label = TEXT("PENALTIES");
		S2.Val[0] = TEXT("15-145");
		S2.Val[1] = TEXT("12-110");
		S2.Num[0] = 145;
		S2.Num[1] = 110;
		S2.Secs = 60.f;
		X->ShowTeamStat(S2);
	} });
	Shot(TEXT("team_number"), 0.6, true);
	Do([GfxOf, BigBox] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); FHalftimeStats S2; static_cast<FGfxBoxStats&>(S2) = BigBox(false); X->ShowHalftimeStats(S2, 60.f); } });
	Shot(TEXT("halftime_box"), 1.0, true);
	Do([GfxOf, BigBox] { if (FBroadcastGfx* X = GfxOf())
	{
		X->HideHalftimeStats();
		const FGfxBoxStats B = BigBox(true);
		FPlayerOfGame P;
		P.bValid = true;
		P.Side = 0;
		P.Team = B.Team[0];
		P.Name = TEXT("BARTHOLOMEW WASHINGTONPRICE");
		P.Number = TEXT("#88");
		P.Pos = TEXT("DEFENSIVE TACKLE");
		P.Grade = TEXT("X-FACTOR");
		const TCHAR* const Vals[4] = {TEXT("38/52"), TEXT("487"), TEXT("6"), TEXT("148.3")};
		const TCHAR* const Labels[4] = {TEXT("COMP / ATT"), TEXT("PASS YARDS"), TEXT("TOUCHDOWNS"), TEXT("RATING")};
		for (int32 i = 0; i < 4; ++i) { P.Stats[i].Value = Vals[i]; P.Stats[i].Label = Labels[i]; }
		P.NumStats = 4;
		P.Line = TEXT("38/52  ·  487 YDS  ·  6 TD  ·  3 INT     11 CAR  ·  96 YDS  ·  8.7 AVG  ·  2 TD     FG 4/5  XP 7/7");
		P.Why = FString(TEXT("61.4 GAME POINTS")) + Dot() + TEXT("WINNING SIDE");
		P.Points = 61.4;
		X->ShowPlayerOfGame(P, 60.f);
	} });
	Shot(TEXT("player_of_game"), 1.2, true);
	Do([GfxOf, BigBox] { if (FBroadcastGfx* X = GfxOf()) { X->HidePlayerOfGame(); FFinalStats S2; static_cast<FGfxBoxStats&>(S2) = BigBox(true); X->ShowFinalStats(S2, 60.f); } });
	Shot(TEXT("final_box_overtime"), 1.2, true);
	Do([GfxOf] { if (FBroadcastGfx* X = GfxOf()) X->HideFinalStats(); });

	// ---- over a real frame: a watched game, held (engine paused) once its first scrimmage play is set
	Do([W]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || !O->GfxPtr) return;
		O->GfxPtr->SetBackdrop(false);
		O->GfxPtr->SetScripted(false); // (the game's own bug until the hold)
		O->Director->StartMatch(O->SelHome, O->SelAway, false, true, 1, true, 300);
	});
	UntilT([W] { const TSharedPtr<SGridironOverlay> O = W.Pin(); return !O || !O->Director || (O->Director->State == EMatchState::Presnap && O->Director->Game); }, 180.0);
	Frames(45);
	Do([W, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director) return;
		AMatchDirector& D = *O->Director;
		Check(D.State == EMatchState::Presnap && D.Game, FString::Printf(TEXT("a watched game reached its first set play (state %d)"), static_cast<int32>(D.State)));
		if (UWorld* World = D.GetWorld()) UGameplayStatics::SetGamePaused(World, true);
		D.bEnginePaused = true; // (the director's Tick holds while it is set: the world stands still for the pictures)
		if (O->GfxPtr)
		{
			O->GfxPtr->SetScripted(true);
			O->GfxPtr->ClearGraphics();
		}
	});
	Shot(TEXT("live_bug"), 0.8, true);
	Do([Alert] { Alert(EGfxAlert::Flag, TEXT("FLAG"), FString(TEXT("FALSE START")) + Dot() + TEXT("#72 J. SMITH") + Dot() + TEXT("OFFENSE"), -1, 60.f); });
	Shot(TEXT("live_flag"), 0.5, true);
	Do([GfxOf, LongL3] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowLowerThird(LongL3()); } });
	Shot(TEXT("live_lowerthird"), 0.6, true);
	Do([GfxOf, TwoTags] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowStatTags(TwoTags(true)); } });
	Shot(TEXT("live_stattags"), 0.8, true);
	Do([GfxOf, DriveOf] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowDriveSummary(DriveOf(1, TEXT("FIELD GOAL"), 11, 52, 331, TEXT("OWN 30"))); } });
	Shot(TEXT("live_drive"), 0.6, true);
	Do([GfxOf, DownOf] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowThirdDown(DownOf(1, X->ScoreBug().Side[1].Abbr, TEXT("3RD & 2"), TEXT("FIRST OF THE GAME"), false)); } });
	Shot(TEXT("live_third_down"), 0.6, true);
	Do([GfxOf, W] { const TSharedPtr<SGridironOverlay> O = W.Pin(); if (FBroadcastGfx* X = GfxOf()) if (O && O->Director && O->Director->Game) { X->ClearGraphics();
		X->ShowMatchupCard(FBroadcastGfx::MatchupFor(*O->Director->Game->away, *O->Director->Game->home, &O->Director->League, Up(O->Director->Game->weatherText()))); } });
	Shot(TEXT("live_matchup"), 0.9, true);
	// ---- the booth's lines (OnBoothLine, as the booth's poll hands them over): a team number, once; a team's third downs
	Do([GfxOf, Check] { if (FBroadcastGfx* X = GfxOf())
	{
		X->HideMatchupCard();
		X->ClearGraphics();
		FGfxBoothCue Cue;
		Cue.Id = 9001;
		Cue.Kind = 6;
		Cue.Topic = TEXT("yds");
		Cue.Text = TEXT("(booth) the yardage so far");
		Cue.Secs = 30.f;
		Check(X->OnBoothLine(Cue), TEXT("a booth comparison line ('yds') brings the team number up with it"));
		Cue.Id = 9002;
		Check(!X->OnBoothLine(Cue), TEXT("the same comparison again a moment later brings nothing (shown lately)"));
	} });
	Shot(TEXT("live_booth_yards"), 0.6, true);
	Do([GfxOf, W, Check] { const TSharedPtr<SGridironOverlay> O = W.Pin(); if (FBroadcastGfx* X = GfxOf()) if (O && O->Director && O->Director->Game)
	{
		X->ClearGraphics();
		FGfxBoothCue Cue;
		Cue.Id = 9003;
		Cue.Kind = 6;
		Cue.Topic = TEXT("3rd:") + Up(O->Director->Game->away->abbr);
		Cue.Secs = 30.f;
		Check(X->OnBoothLine(Cue), FString::Printf(TEXT("a booth third-down line ('%s') brings the third-down graphic up"), *Cue.Topic));
		FGfxBoothCue Ref;
		Ref.Id = 9004;
		Ref.Kind = 10;
		Ref.bRef = true;
		Check(!X->OnBoothLine(Ref), TEXT("a referee's line with no flag waiting brings nothing"));
	} });
	Shot(TEXT("live_booth_third"), 0.6, true);
	// ---- the cards' builders on a whole game's book (a headless game of the two teams, played to its end here)
	struct FBuilt { FHalftimeStats Half; FFinalStats Final; FPlayerOfGame Potg; FGfxPenalty Pen; bool bPen = false; };
	const TSharedRef<FBuilt> Built = MakeShared<FBuilt>();
	Do([W, Check, Built]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director || O->Director->League.size() < 2) { Check(false, TEXT("no league for the headless game")); return; }
		tag::League& Lg = O->Director->League;
		tag::GameOptions Opt;
		Opt.seed = 7u;
		tag::Game Hg(*Lg[FMath::Clamp(O->SelHome, 0, static_cast<int32>(Lg.size()) - 1)], *Lg[FMath::Clamp(O->SelAway, 0, static_cast<int32>(Lg.size()) - 1)], Opt);
		Hg.simToEnd();
		Built->Half = FBroadcastGfx::HalftimeStatsFor(Hg);
		Built->Final = FBroadcastGfx::FinalStatsFor(Hg);
		Built->Potg = FBroadcastGfx::PlayerOfGameFor(Hg);
		for (const tag::PlayLog& L : Hg.log)
			if (!Built->bPen && FBroadcastGfx::PenaltyFromLog(Hg, L, FGfxSnapState(), Built->Pen)) Built->bPen = true;
		// the box against the book: each side's periods add up to its score, which is the book's
		for (int32 s = 0; s < 2; ++s)
		{
			const FGfxTeamTotals& Tt = Built->Final.Tot[s];
			int32 Sum = 0;
			for (int32 p = 0; p < 5; ++p) Sum += FMath::Max(0, Tt.ByQ[p]);
			const int32 Book = s == 1 ? Hg.score.home : Hg.score.away;
			Check(Sum == Tt.Score && Tt.Score == Book, FString::Printf(TEXT("the final box's %s: periods add to %d, score %d, the book's %d"), *Built->Final.Team[s].Abbr, Sum, Tt.Score, Book));
		}
		Check(Built->Potg.bValid && Built->Potg.NumStats > 0, FString::Printf(TEXT("a player of the game: %s %s, %.1f game points (%s)"), *Built->Potg.Number, *Built->Potg.Name,
			Built->Potg.Points, *Built->Potg.Line));
		UE_LOG(LogTemp, Display, TEXT("TAG gfx headless game %d-%d, %d log lines, a penalty line %s"), Hg.score.away, Hg.score.home, static_cast<int32>(Hg.log.size()),
			Built->bPen ? *Built->Pen.Foul : TEXT("none"));
	});
	Do([GfxOf, Built] { if (FBroadcastGfx* X = GfxOf()) { X->ClearGraphics(); X->ShowFinalStats(Built->Final, 60.f); } });
	Shot(TEXT("live_final_box_built"), 1.2, true);
	Do([GfxOf, Built] { if (FBroadcastGfx* X = GfxOf()) { X->HideFinalStats(); X->ShowHalftimeStats(Built->Half, 60.f); } });
	Shot(TEXT("live_halftime_box_built"), 1.2, true);
	Do([GfxOf, Built] { if (FBroadcastGfx* X = GfxOf()) { X->HideHalftimeStats(); X->ShowPlayerOfGame(Built->Potg, 60.f); } });
	Shot(TEXT("live_player_of_game_built"), 1.2, true);
	Do([GfxOf, Built] { if (FBroadcastGfx* X = GfxOf()) { X->HidePlayerOfGame(); if (Built->bPen) { Built->Pen.Secs = 60.f; X->ShowPenalty(Built->Pen); } } });
	Shot(TEXT("live_flag_panel_built"), 1.0, true);
	Do([S]
	{
		UE_LOG(LogTemp, Display, TEXT("TAG gfx shots done: %d pictures, %d pass, %d fail"), S->N, S->Pass, S->Fail);
		TagResult::Finish(TEXT("TagGfxShots"), S->Pass, S->Fail); // (the result line and the exit status, Game/TagResult.h)
	});

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
	{
		if (!S->Steps.IsValidIndex(S->I)) return false;
		if (S->Steps[S->I]()) ++S->I;
		return true;
	}));
}

#undef TAG_GFX_NAMES
