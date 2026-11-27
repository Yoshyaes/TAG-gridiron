// Shared look for the roster screens (UI/DepthChart.cpp, UI/CreatePlayer.cpp): the colours, text, cards, the
// focusable root that takes every key, the pad / key glyph, team logos, and the saved roster edits.
#pragma once

#include "UI/GridironOverlay.h"
#include "UI/TagTheme.h"

#include "Brushes/SlateDynamicImageBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "Sim/Roster.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"
#include <functional>
#include <string>

namespace RosterUI
{
	inline const FLinearColor Ink(0.012f, 0.016f, 0.026f, 1.f);
	inline const FLinearColor Card(0.035f, 0.045f, 0.068f, 0.95f);
	inline const FLinearColor CardHi(0.06f, 0.075f, 0.11f, 0.97f);
	inline const FLinearColor Gold(1.f, 0.83f, 0.f);
	inline const FLinearColor Dim(0.58f, 0.63f, 0.71f);
	inline const FLinearColor Faint(0.36f, 0.4f, 0.47f);
	inline const FLinearColor White90(0.93f, 0.95f, 0.98f);
	inline const FLinearColor Warn(0.94f, 0.44f, 0.24f);
	inline const FLinearColor Good(0.18f, 0.83f, 0.49f);
	inline const FLinearColor Cap(0.36f, 0.86f, 1.f); // a created player's chip

	inline FSlateFontInfo Font(int32 Size, const char* Weight = "Regular") { return FCoreStyle::GetDefaultFontStyle(Weight, Size); }
	inline const FSlateBrush* WhiteBrush() { return FCoreStyle::Get().GetBrush("WhiteBrush"); }
	inline FText Tx(const FString& S) { return FText::FromString(S); }
	inline FString Str(const std::string& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	inline std::string Std(const FString& s) { return std::string(TCHAR_TO_UTF8(*s)); }
	inline FLinearColor HexCol(const std::string& H)
	{
		FString S = Str(H);
		S.RemoveFromStart(TEXT("#"));
		return FLinearColor(FColor::FromHex(S));
	}
	/** A team colour readable on the dark cards (a near-black primary falls back to the secondary). */
	inline FLinearColor TeamInk(const tag::Team& T)
	{
		const FLinearColor P = HexCol(T.colors.primary);
		return P.GetLuminance() < 0.08f ? HexCol(T.colors.secondary) : P;
	}
	inline FString Pos(tag::Pos P) { return ANSI_TO_TCHAR(tag::posName(P)); }
	inline FString Short(const tag::Player& P) { return FString::Printf(TEXT("%s. %s"), *Str(P.first.substr(0, 1)), *Str(P.last)); }
	inline FString Full(const tag::Player& P) { return Str(P.first) + TEXT(" ") + Str(P.last); }
	inline FString Height(double In)
	{
		const int32 I = FMath::RoundToInt(In);
		return FString::Printf(TEXT("%d'%d\""), I / 12, I % 12);
	}
	/** An OVR's colour: the elite gold, the good green, the starters white, the depth grey. */
	inline FLinearColor OvrColor(int32 O)
	{
		return O >= 88 ? Gold : O >= 80 ? Good : O >= 70 ? White90 : O >= 60 ? Dim : Faint;
	}
	inline TSharedRef<SWidget> Label(const FString& S, int32 Size = 10, const FLinearColor& C = Dim, const char* W = "Bold")
	{
		return SNew(STextBlock).Font(Font(Size, W)).ColorAndOpacity(C).Text(Tx(S));
	}
	inline TSharedRef<SWidget> Clip(const FString& S, int32 Size, const FLinearColor& C, const char* W = "Bold")
	{
		return SNew(STextBlock).Font(Font(Size, W)).ColorAndOpacity(C).Text(Tx(S)).Clipping(EWidgetClipping::ClipToBounds);
	}
	inline TSharedRef<SWidget> Fill(const FLinearColor& C, float H = 1.f)
	{
		return SNew(SBox).HeightOverride(H)[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(C) ];
	}
	inline TSharedRef<SWidget> Chip(const FString& S, const FLinearColor& Bg, const FLinearColor& Fg, int32 Size = 8)
	{
		return SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(Bg).Padding(FMargin(6, 1))[ Label(S, Size, Fg) ];
	}
	inline TSharedRef<SWidget> Heading(const FString& S)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Label(S.ToUpper(), 9, Faint) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 6)[ Fill(FLinearColor(1, 1, 1, 0.08f)) ];
	}
	inline TSharedRef<SWidget> Click(TSharedRef<SWidget> Content, std::function<void()> OnClick)
	{
		return SNew(SButton).IsFocusable(false).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
			.OnClicked_Lambda([OnClick] { OnClick(); return FReply::Handled(); })[ Content ];
	}
	/** A rating bar: the value's share of 99, coloured by how good it is. */
	inline TSharedRef<SWidget> Bar(double V, float W, float H = 6.f, const FLinearColor* Tint = nullptr)
	{
		const float F = FMath::Clamp(static_cast<float>(V) / 99.f, 0.f, 1.f);
		const FLinearColor C = Tint ? *Tint : OvrColor(FMath::RoundToInt(V));
		return SNew(SBox).WidthOverride(W).HeightOverride(H)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(FMath::Max(0.001f, F))[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(C) ]
				+ SHorizontalBox::Slot().FillWidth(FMath::Max(0.001f, 1.f - F))[ SNew(SBorder).BorderImage(WhiteBrush()).BorderBackgroundColor(FLinearColor(1, 1, 1, 0.07f)) ]
			];
	}
	/** The selection frame the screens draw themselves (gold round the row under the cursor). */
	inline TSharedRef<SWidget> Frame(bool bSel, bool bActive, TSharedRef<SWidget> In, const FLinearColor& Bg = Card)
	{
		// (the theme's selection: glow, lifted card, gold edge and bar; UI/TagTheme.h)
		return SNew(TagTheme::SSelect).Selected(bSel).Active(bActive).Fill(Bg).Radius(4.f)[ In ];
	}

	/** The team's mark (Content/Slate/TeamLogos/<ABBR>.png), or null when it is missing. */
	inline const FSlateBrush* Logo(const tag::Team& T)
	{
		static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> Cache;
		const FString Abbr = Str(T.abbr);
		if (const TSharedPtr<FSlateDynamicImageBrush>* Hit = Cache.Find(Abbr)) return Hit->Get();
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Slate/TeamLogos") / (Abbr + TEXT(".png")));
		TSharedPtr<FSlateDynamicImageBrush>& B = Cache.Add(Abbr);
		if (IFileManager::Get().FileExists(*Path)) B = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(256, 256));
		return B.Get();
	}

	/** The screen's root: the one focusable widget (every key comes to the screen). */
	class SKeyRoot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SKeyRoot) {}
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
		/** Scripted checks: a key straight to the screen. */
		bool Press(const FKey& K) { return OnKey && OnKey(K); }
	private:
		std::function<bool(const FKey&)> OnKey;
	};

	/** A pad button / key, drawn by the overlay's painter. */
	class SPadGlyph : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SPadGlyph) : _Owner(nullptr), _Glyph(EPadGlyph::None), _Size(22.f) {}
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
			return Owner ? Owner->GlyphExtent(Glyph, Key, Size, Owner->PadGlyphs() && Glyph != EPadGlyph::None) : FVector2D(Size + 4, Size + 4);
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

	/** The keys the screens share. */
	struct FNav
	{
		bool bLeft, bRight, bUp, bDown, bPrev, bNext, bPrevT, bNextT, bOk, bBack, bSquare, bTriangle, bL3, bR3, bStart;
		explicit FNav(const FKey& K)
		{
			bLeft = K == EKeys::Gamepad_DPad_Left || K == EKeys::Gamepad_LeftStick_Left || K == EKeys::Left || K == EKeys::A;
			bRight = K == EKeys::Gamepad_DPad_Right || K == EKeys::Gamepad_LeftStick_Right || K == EKeys::Right || K == EKeys::D;
			bUp = K == EKeys::Gamepad_DPad_Up || K == EKeys::Gamepad_LeftStick_Up || K == EKeys::Up || K == EKeys::W;
			bDown = K == EKeys::Gamepad_DPad_Down || K == EKeys::Gamepad_LeftStick_Down || K == EKeys::Down || K == EKeys::S;
			bPrev = K == EKeys::Gamepad_LeftShoulder || K == EKeys::Q;
			bNext = K == EKeys::Gamepad_RightShoulder || K == EKeys::E;
			bPrevT = K == EKeys::Gamepad_LeftTrigger || K == EKeys::Z;
			bNextT = K == EKeys::Gamepad_RightTrigger || K == EKeys::C;
			bOk = K == EKeys::Gamepad_FaceButton_Bottom || K == EKeys::Enter || K == EKeys::SpaceBar;
			bBack = K == EKeys::Gamepad_FaceButton_Right || K == EKeys::Escape || K == EKeys::BackSpace;
			bSquare = K == EKeys::Gamepad_FaceButton_Left || K == EKeys::R;
			bTriangle = K == EKeys::Gamepad_FaceButton_Top || K == EKeys::T;
			bL3 = K == EKeys::Gamepad_LeftThumbstick || K == EKeys::X;
			bR3 = K == EKeys::Gamepad_RightThumbstick || K == EKeys::V;
			bStart = K == EKeys::Gamepad_Special_Right || K == EKeys::F;
		}
		bool Moves() const { return bLeft || bRight || bUp || bDown || bPrev || bNext; }
	};

	// ---- the saved roster edits (GameUserSettings.ini; UI/CreatePlayer.cpp)
	/** Scripted screen checks (-TagRosterShots) never touch the user's saves. */
	bool NoSave();
	/** The team's depth chart saved ([TAG.DepthChart] ABBR = "QB=12,7,3;FS=24,31"; jersey numbers), or forgotten. */
	void SaveChart(const tag::Team& T);
	/** Every created player saved ([TAG.CreatedPlayers]). */
	void SaveCreated(AMatchDirector* D);
	/** The men a team has cut for created players ([TAG.Cut] ABBR = "45,61"; the league's own men, by number). */
	void SaveCuts(AMatchDirector* D);
	/** After a roster change: the league graded again (the stars by their standing) and the saved loadouts laid back on. */
	void Regrade(AMatchDirector* D);
	/** A man off his team without freeing him (a game or an athlete may still hold his address): kept for the session. */
	std::unique_ptr<tag::Player> Retire(tag::Team& T, tag::Player* P);
	/** The men cut this session (kept alive), and a cut man back onto his team. */
	TArray<TPair<int32, tag::Player*>> CutMen(int32 TeamId);
	bool Uncut(tag::Team& T, tag::Player* P);
}
