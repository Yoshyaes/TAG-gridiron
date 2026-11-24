// Settings > Controls: Madden's controller screen. A tab for each part of the game (the ball carrier, passing,
// receiving, defense, the two pre-snaps, kicking); the controller in the player's hands drawn in the middle - a
// DualSense while a PlayStation pad is plugged in, an Xbox pad otherwise (or whichever the "Buttons" option forces) -
// with a line from every button to what it does there. Any action moves to any button: pick it, press the new button,
// and whatever was on that button takes the old one (TagBind keeps each layout a permutation, so nothing is ever lost).
// The keyboard has its own view (every key, same rules). Every change is saved at once and the game reads its buttons
// through the layout (Game/TagBindings.h); its prompts (the hints, the pre-snap panel, the receivers' icons, the
// highlight cue) name the buttons the player chose.
//
// -TagControlsTest: the screen driven by its own buttons (every tab, both pads, the keyboard view, a capture, a swap,
// the resets), the swap read back through the game's reader and hints, then a game: a pre-snap call made on the button
// it was moved to. PASS / FAIL lines, pictures (tag_controls_*.png), "TAG controls test done", then the game exits.
#include "UI/GridironOverlay.h"
#include "UI/TagTheme.h"

#include "Audio/TagAudio.h"
#include "Containers/Ticker.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/MatchDirector.h"
#include "Game/TagBindings.h"
#include "Game/TagInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

using TagBind::ECtx;

/** The screen's state: the tab, the view, the cursor, the button being waited for, the last thing said. */
struct FCtlState
{
	int32 Ctx = 0;
	bool bKbd = false;
	bool bViewPicked = false;  // the player chose the view (else it follows whether a pad is plugged in)
	int32 Sel = 0;
	bool bCapture = false;
	double CaptureT = 0.0;
	FString Msg;
	FLinearColor MsgC = TagTheme::Gold;
	double MsgT = -10.0;
	std::function<void()> Back;
	// (painted each frame; the mouse reads them)
	mutable TArray<FBox2D> TabRects, RowRects;
};

// (a named namespace: in a unity build this file shares a translation unit with the other UI files)
namespace CtlUI
{
	constexpr float BoardW = 1760.f, BoardH = 760.f;
	constexpr double CaptureSecs = 8.0;
	constexpr int32 KbdPerCol = 11;

	FVector2D Measure(const FString& S, const FSlateFontInfo& F)
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, F);
	}
	void Text(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& TL, const FString& S, const FSlateFontInfo& F, const FLinearColor& C)
	{
		const FVector2D M = Measure(S, F);
		FSlateDrawElement::MakeText(Out, Layer, G.ToPaintGeometry(FVector2f(M), FSlateLayoutTransform(FVector2f(TL))), S, F, ESlateDrawEffect::None, C);
	}
	/** Text with its centre (bCenter), its right edge (bRight) or its left edge at X; Y its middle. */
	void TextAt(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, float X, float Y, const FString& S, const FSlateFontInfo& F, const FLinearColor& C, int32 Align)
	{
		const FVector2D M = Measure(S, F);
		const float L = Align == 0 ? X : Align == 1 ? X - M.X * 0.5f : X - M.X;
		Text(Out, Layer, G, FVector2D(L, Y - M.Y * 0.5), S, F, C);
	}

	// ---- filled shapes (custom vertices on the white texture: a controller is not made of boxes)
	void Fill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& P, const TArray<SlateIndex>& Tri,
		TFunctionRef<FLinearColor(const FVector2f&)> Col)
	{
		if (P.Num() < 3 || Tri.Num() < 3 || !FSlateApplication::IsInitialized()) return;
		const FSlateResourceHandle H = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush("GenericWhiteBox"));
		const FSlateRenderTransform& RT = G.GetAccumulatedRenderTransform();
		TArray<FSlateVertex> V;
		V.Reserve(P.Num());
		for (const FVector2f& X : P) V.Add(FSlateVertex::Make(RT, X, FVector2f(0.5f, 0.5f), Col(X).ToFColor(true), FColor(), ESlateVertexRounding::Disabled));
		FSlateDrawElement::MakeCustomVerts(Out, Layer, H, V, Tri, nullptr, 0, 0);
	}
	void Lines(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& P, const FLinearColor& C, float Th)
	{
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C, true, Th);
	}
	TArray<FVector2f> Circle(const FVector2f& C, float R, int32 N = 40)
	{
		TArray<FVector2f> P;
		for (int32 i = 0; i <= N; ++i) P.Add(FVector2f(C.X + FMath::Cos(2.f * PI * i / N) * R, C.Y + FMath::Sin(2.f * PI * i / N) * R));
		return P;
	}
	/** A disc, shaded top (In) to bottom (Lo), edged (Edge.A > 0). */
	void Disc(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& C, float R, const FLinearColor& In, const FLinearColor& Lo,
		const FLinearColor& Edge = FLinearColor::Transparent, float EdgeTh = 1.5f)
	{
		TArray<FVector2f> P = {C};
		TArray<SlateIndex> T;
		const int32 N = 44;
		for (int32 i = 0; i < N; ++i) P.Add(FVector2f(C.X + FMath::Cos(2.f * PI * i / N) * R, C.Y + FMath::Sin(2.f * PI * i / N) * R));
		for (int32 i = 0; i < N; ++i) T.Append({0, static_cast<SlateIndex>(1 + i), static_cast<SlateIndex>(1 + (i + 1) % N)});
		Fill(Out, Layer, G, P, T, [&](const FVector2f& X) { return FMath::Lerp(In, Lo, FMath::Clamp((X.Y - (C.Y - R)) / (2.f * R), 0.f, 1.f)); });
		if (Edge.A > 0.f) Lines(Out, Layer + 1, G, Circle(C, R, N), Edge, EdgeTh);
	}

	/** Ear clipping: a simple polygon (either winding) into triangles. */
	TArray<SlateIndex> Triangulate(const TArray<FVector2f>& P)
	{
		TArray<SlateIndex> Out;
		const int32 N = P.Num();
		if (N < 3) return Out;
		double Area = 0;
		for (int32 i = 0; i < N; ++i) Area += static_cast<double>(P[i].X) * P[(i + 1) % N].Y - static_cast<double>(P[(i + 1) % N].X) * P[i].Y;
		const bool bPos = Area > 0;
		auto Cross = [](const FVector2f& A, const FVector2f& B, const FVector2f& C) { return static_cast<double>(B.X - A.X) * (C.Y - A.Y) - static_cast<double>(B.Y - A.Y) * (C.X - A.X); };
		auto Inside = [&](const FVector2f& X, const FVector2f& A, const FVector2f& B, const FVector2f& C)
		{
			const double D1 = Cross(A, B, X), D2 = Cross(B, C, X), D3 = Cross(C, A, X);
			return bPos ? (D1 > 0 && D2 > 0 && D3 > 0) : (D1 < 0 && D2 < 0 && D3 < 0);
		};
		TArray<int32> I;
		for (int32 i = 0; i < N; ++i) I.Add(i);
		int32 Guard = 0;
		while (I.Num() > 3 && Guard++ < N * N)
		{
			bool bCut = false;
			const int32 M = I.Num();
			for (int32 k = 0; k < M; ++k)
			{
				const int32 A = I[(k + M - 1) % M], B = I[k], C = I[(k + 1) % M];
				const double Cr = Cross(P[A], P[B], P[C]);
				if (FMath::Abs(Cr) < 1e-12) { I.RemoveAt(k); bCut = true; break; }  // (a straight run: no triangle)
				if ((Cr > 0) != bPos) continue;  // a reflex corner
				bool bIn = false;
				for (int32 j : I)
					if (j != A && j != B && j != C && Inside(P[j], P[A], P[B], P[C])) { bIn = true; break; }
				if (bIn) continue;
				Out.Append({static_cast<SlateIndex>(A), static_cast<SlateIndex>(B), static_cast<SlateIndex>(C)});
				I.RemoveAt(k);
				bCut = true;
				break;
			}
			if (!bCut) break;
		}
		if (I.Num() == 3) Out.Append({static_cast<SlateIndex>(I[0]), static_cast<SlateIndex>(I[1]), static_cast<SlateIndex>(I[2])});
		return Out;
	}

	/** A closed Catmull-Rom curve through the points. */
	TArray<FVector2f> Smooth(const TArray<FVector2f>& C, int32 Sub)
	{
		TArray<FVector2f> Out;
		const int32 N = C.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2f P0 = C[(i + N - 1) % N], P1 = C[i], P2 = C[(i + 1) % N], P3 = C[(i + 2) % N];
			for (int32 s = 0; s < Sub; ++s)
			{
				const float T = static_cast<float>(s) / Sub, T2 = T * T, T3 = T2 * T;
				Out.Add(0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3));
			}
		}
		return Out;
	}
	/** The right half (top centre round to bottom centre, x >= 0) made whole: the left half is its mirror. */
	TArray<FVector2f> Mirrored(const TArray<FVector2f>& Half)
	{
		TArray<FVector2f> R = Half;
		for (int32 i = Half.Num() - 2; i >= 1; --i) R.Add(FVector2f(-Half[i].X, Half[i].Y));
		return R;
	}

	/** A shape in the art's own units (x across -0.5..0.5, y down, both by the art's width), triangulated once. */
	struct FShape
	{
		TArray<FVector2f> P;
		TArray<SlateIndex> T;
	};
	FShape Make(const TArray<FVector2f>& Half, int32 Sub = 7)
	{
		FShape S;
		S.P = Smooth(Mirrored(Half), Sub);
		S.T = Triangulate(S.P);
		return S;
	}
	const FShape& XboxBody()
	{
		static const FShape S = Make({{0.f, 0.105f}, {0.10f, 0.095f}, {0.20f, 0.075f}, {0.30f, 0.055f}, {0.38f, 0.06f}, {0.44f, 0.10f}, {0.475f, 0.17f},
			{0.49f, 0.26f}, {0.495f, 0.36f}, {0.485f, 0.47f}, {0.46f, 0.57f}, {0.42f, 0.65f}, {0.37f, 0.70f}, {0.31f, 0.705f}, {0.265f, 0.67f},
			{0.235f, 0.60f}, {0.20f, 0.52f}, {0.15f, 0.475f}, {0.08f, 0.46f}, {0.f, 0.455f}});
		return S;
	}
	const FShape& SonyBody()
	{
		static const FShape S = Make({{0.f, 0.06f}, {0.12f, 0.06f}, {0.20f, 0.07f}, {0.29f, 0.06f}, {0.37f, 0.065f}, {0.43f, 0.10f}, {0.47f, 0.17f},
			{0.49f, 0.27f}, {0.495f, 0.38f}, {0.49f, 0.50f}, {0.47f, 0.60f}, {0.44f, 0.67f}, {0.39f, 0.71f}, {0.34f, 0.70f}, {0.30f, 0.64f},
			{0.26f, 0.55f}, {0.22f, 0.48f}, {0.16f, 0.44f}, {0.08f, 0.425f}, {0.f, 0.42f}});
		return S;
	}
	/** The DualSense's black middle (the sticks, the PS button). */
	const FShape& SonyPlate()
	{
		static const FShape S = Make({{0.f, 0.27f}, {0.10f, 0.268f}, {0.165f, 0.275f}, {0.205f, 0.31f}, {0.222f, 0.37f}, {0.21f, 0.43f}, {0.17f, 0.455f},
			{0.10f, 0.44f}, {0.f, 0.43f}}, 6);
		return S;
	}

	/** A button's place on the drawn pad and its row in the callouts: a button (Key), or a control that stays put
	 *  (Fixed: "LS", "RS", "MENU", "VIEW"). */
	struct FSlot
	{
		FKey Key;
		FString Fixed;
		FVector2f At;
	};
	const TArray<FSlot>& Slots(bool bSony)
	{
		using K = EKeys;
		static TArray<FSlot> X, P;
		if (X.Num() == 0)
		{
			// left column top to bottom, then the right: in the order they sit on each pad (lines cross as little as can be)
			X = {{K::Gamepad_LeftTrigger, TEXT(""), {-0.33f, -0.005f}}, {K::Gamepad_LeftShoulder, TEXT(""), {-0.31f, 0.04f}},
				{FKey(), TEXT("VIEW"), {-0.075f, 0.215f}}, {FKey(), TEXT("LS"), {-0.245f, 0.215f}}, {K::Gamepad_LeftThumbstick, TEXT(""), {-0.245f, 0.215f}},
				{K::Gamepad_DPad_Up, TEXT(""), {-0.12f, 0.31f}}, {K::Gamepad_DPad_Left, TEXT(""), {-0.155f, 0.345f}},
				{K::Gamepad_DPad_Right, TEXT(""), {-0.085f, 0.345f}}, {K::Gamepad_DPad_Down, TEXT(""), {-0.12f, 0.38f}},
				{K::Gamepad_RightTrigger, TEXT(""), {0.33f, -0.005f}}, {K::Gamepad_RightShoulder, TEXT(""), {0.31f, 0.04f}},
				{FKey(), TEXT("MENU"), {0.075f, 0.215f}}, {K::Gamepad_FaceButton_Top, TEXT(""), {0.245f, 0.163f}},
				{K::Gamepad_FaceButton_Right, TEXT(""), {0.297f, 0.215f}}, {K::Gamepad_FaceButton_Left, TEXT(""), {0.193f, 0.215f}},
				{K::Gamepad_FaceButton_Bottom, TEXT(""), {0.245f, 0.267f}}, {FKey(), TEXT("RS"), {0.12f, 0.345f}}, {K::Gamepad_RightThumbstick, TEXT(""), {0.12f, 0.345f}}};
			P = {{K::Gamepad_LeftTrigger, TEXT(""), {-0.32f, -0.01f}}, {K::Gamepad_LeftShoulder, TEXT(""), {-0.31f, 0.042f}},
				{FKey(), TEXT("VIEW"), {-0.2f, 0.12f}}, {K::Gamepad_DPad_Up, TEXT(""), {-0.30f, 0.18f}}, {K::Gamepad_DPad_Left, TEXT(""), {-0.345f, 0.225f}},
				{K::Gamepad_DPad_Right, TEXT(""), {-0.255f, 0.225f}}, {K::Gamepad_DPad_Down, TEXT(""), {-0.30f, 0.27f}},
				{FKey(), TEXT("LS"), {-0.125f, 0.36f}}, {K::Gamepad_LeftThumbstick, TEXT(""), {-0.125f, 0.36f}},
				{K::Gamepad_RightTrigger, TEXT(""), {0.32f, -0.01f}}, {K::Gamepad_RightShoulder, TEXT(""), {0.31f, 0.042f}},
				{FKey(), TEXT("MENU"), {0.2f, 0.12f}}, {K::Gamepad_FaceButton_Top, TEXT(""), {0.30f, 0.175f}},
				{K::Gamepad_FaceButton_Right, TEXT(""), {0.35f, 0.225f}}, {K::Gamepad_FaceButton_Left, TEXT(""), {0.25f, 0.225f}},
				{K::Gamepad_FaceButton_Bottom, TEXT(""), {0.30f, 0.275f}}, {FKey(), TEXT("RS"), {0.125f, 0.36f}}, {K::Gamepad_RightThumbstick, TEXT(""), {0.125f, 0.36f}}};
		}
		return bSony ? P : X;
	}
	constexpr int32 NumSlots = 18, PerCol = 9;

	/** The action on physical button K in context C (its logical button's), or null. */
	const TagBind::FAction* ActionOnPad(ECtx C, const FKey& K)
	{
		const FKey L = TagBind::Logical(C, K);
		for (const TagBind::FAction& A : TagBind::Actions(C))
			if (A.Pad.IsValid() && A.Pad == L) return &A;
		return nullptr;
	}
	FString FixedName(ECtx C, const FString& Where, bool bPad)
	{
		for (const TagBind::FFixed& F : TagBind::Fixed(C, bPad))
			if (F.Where == Where) return F.Name;
		return FString();
	}
	FString FixedKeycap(const FString& Where, bool bSony)
	{
		if (Where == TEXT("LS")) return TEXT("L-STICK");
		if (Where == TEXT("RS")) return TEXT("R-STICK");
		if (Where == TEXT("MENU")) return bSony ? TEXT("OPTIONS") : TEXT("MENU");
		if (Where == TEXT("VIEW")) return bSony ? TEXT("CREATE") : TEXT("VIEW");
		return Where;
	}

	/** The keyboard view's rows: the actions with a key, then the keys that stay put. */
	struct FKRow
	{
		const TagBind::FAction* A = nullptr;
		FString Where, Name;
	};
	TArray<FKRow> KbdRows(ECtx C)
	{
		TArray<FKRow> R;
		for (const TagBind::FAction& A : TagBind::Actions(C))
			if (A.Kbd.IsValid()) R.Add({&A, FString(), FString()});
		for (const TagBind::FFixed& F : TagBind::Fixed(C, false)) R.Add({nullptr, F.Where, F.Name});
		return R;
	}

	/** The screen's root: the one focusable widget (every key comes to the screen); it ticks the wait for a button. */
	class SRoot : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRoot) {}
			SLATE_DEFAULT_SLOT(FArguments, Content)
			SLATE_ARGUMENT(std::function<bool(const FKey&, bool)>, OnKey)
			SLATE_ARGUMENT(std::function<void()>, OnTick)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			OnKey = A._OnKey;
			OnTickFn = A._OnTick;
			ChildSlot[A._Content.Widget];
		}
		virtual bool SupportsKeyboardFocus() const override { return true; }
		virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override { return OnKey && OnKey(E.GetKey(), E.IsRepeat()) ? FReply::Handled() : FReply::Unhandled(); }
		virtual FNavigationReply OnNavigation(const FGeometry&, const FNavigationEvent& E) override
		{
			// (a key the screen left alone that Slate turned into a move: the screen's own move)
			if (OnKey)
				switch (E.GetNavigationType())
				{
				case EUINavigation::Left: OnKey(EKeys::Gamepad_DPad_Left, false); break;
				case EUINavigation::Right: OnKey(EKeys::Gamepad_DPad_Right, false); break;
				case EUINavigation::Up: OnKey(EKeys::Gamepad_DPad_Up, false); break;
				case EUINavigation::Down: OnKey(EKeys::Gamepad_DPad_Down, false); break;
				default: break;
				}
			return FNavigationReply::Stop();
		}
		virtual void Tick(const FGeometry& G, const double Now, const float Dt) override
		{
			SCompoundWidget::Tick(G, Now, Dt);
			if (OnTickFn) OnTickFn();
		}
		void Key(const FKey& K) { if (OnKey) OnKey(K, false); }
	private:
		std::function<bool(const FKey&, bool)> OnKey;
		std::function<void()> OnTickFn;
	};

	/** One button-bar hint whose glyph, key and words follow the device in use and the screen's state. */
	class SHint : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SHint) : _Owner(nullptr), _Pad(EPadGlyph::None) {}
			SLATE_ARGUMENT(const SGridironOverlay*, Owner)
			SLATE_ARGUMENT(EPadGlyph, Pad)
			SLATE_ARGUMENT(std::function<FString()>, PadKey)  // (a pad button with no glyph: its name on a keycap)
			SLATE_ARGUMENT(FString, Kbd)
			SLATE_ARGUMENT(std::function<FString()>, What)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Owner = A._Owner;
			Pad = A._Pad;
			PadKey = A._PadKey;
			Kbd = A._Kbd;
			What = A._What;
		}
		bool IsPad() const { return Owner && Owner->PadGlyphs(); }
		EPadGlyph Gl() const { return IsPad() ? Pad : EPadGlyph::None; }
		FString Key() const { return IsPad() ? (PadKey ? PadKey() : FString()) : Kbd; }
		FString Words() const { return What ? What().ToUpper() : FString(); }
		static FSlateFontInfo F() { return TagTheme::Font(13, "BoldCondensed", 30); }
		virtual FVector2D ComputeDesiredSize(float) const override
		{
			if (!Owner || !FSlateApplication::IsInitialized()) return FVector2D(120, 28);
			const FVector2D Gs = Owner->GlyphExtent(Gl(), Key(), 24.f, IsPad());
			return FVector2D(Gs.X + 6 + Measure(Words(), F()).X + 22, FMath::Max(28.0, Gs.Y));
		}
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override
		{
			if (!Owner) return Layer;
			const FVector2D Sz = G.GetLocalSize();
			const FVector2D Gs = Owner->GlyphExtent(Gl(), Key(), 24.f, IsPad());
			Owner->PaintGlyph(Out, Layer, G, FVector2D(Gs.X * 0.5, Sz.Y * 0.5), 24.f, Gl(), Key(), IsPad(), FLinearColor::White);
			TextAt(Out, Layer + 5, G, static_cast<float>(Gs.X + 6), static_cast<float>(Sz.Y * 0.5), Words(), F(), FLinearColor(0.88f, 0.9f, 0.94f), 0);
			return Layer + 6;
		}
	private:
		const SGridironOverlay* Owner = nullptr;
		EPadGlyph Pad = EPadGlyph::None;
		std::function<FString()> PadKey;
		FString Kbd;
		std::function<FString()> What;
	};

	/** The board: the tabs, the pad drawn with its callouts (or the keyboard's list), what was said, the wait for a
	 *  button. All painted from the state each frame (nothing to rebuild when the cursor moves). */
	class SBoard : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SBoard) : _Owner(nullptr) {}
			SLATE_ARGUMENT(const SGridironOverlay*, Owner)
			SLATE_ARGUMENT(TSharedPtr<FCtlState>, State)
			/** A click: on a tab (0, its index) or a row (1, its index). */
			SLATE_ARGUMENT(std::function<void(int32, int32)>, OnClick)
		SLATE_END_ARGS()
		void Construct(const FArguments& A)
		{
			Owner = A._Owner;
			State = A._State;
			OnClick = A._OnClick;
		}
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(BoardW, BoardH); }
		virtual FReply OnMouseButtonDown(const FGeometry& G, const FPointerEvent& E) override
		{
			if (!State || !OnClick) return FReply::Unhandled();
			const FVector2D L = G.AbsoluteToLocal(E.GetScreenSpacePosition());
			for (int32 i = 0; i < State->TabRects.Num(); ++i)
				if (State->TabRects[i].IsInside(L)) { OnClick(0, i); return FReply::Handled(); }
			for (int32 i = 0; i < State->RowRects.Num(); ++i)
				if (State->RowRects[i].IsInside(L)) { OnClick(1, i); return FReply::Handled(); }
			return FReply::Unhandled();
		}
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const override;
	private:
		const SGridironOverlay* Owner = nullptr;
		TSharedPtr<FCtlState> State;
		std::function<void(int32, int32)> OnClick;
		int32 PaintTabs(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
		int32 PaintPad(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
		int32 PaintArt(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, bool bSony, float CX, float Top, float AW) const;
		int32 PaintKeyboard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
		int32 PaintCapture(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const;
	};

	/** A small padlock (a control that stays where it is). */
	void Lock(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& C, const FLinearColor& Col)
	{
		TagTheme::Box(Out, Layer, G, FVector2D(C.X - 6, C.Y - 2), FVector2D(12, 9), Col, 2.f);
		TArray<FVector2f> Arc;
		for (int32 i = 0; i <= 12; ++i)
		{
			const float A = PI + PI * i / 12.f;
			Arc.Add(FVector2f(C.X + FMath::Cos(A) * 4.f, C.Y - 2 + FMath::Sin(A) * 5.f));
		}
		Lines(Out, Layer, G, Arc, Col, 1.6f);
	}

	int32 SBoard::OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
	{
		if (!State || !Owner) return Layer;
		const FCtlState& S = *State;
		S.TabRects.Reset();
		S.RowRects.Reset();
		Layer = PaintTabs(Out, Layer, G);
		const ECtx C = static_cast<ECtx>(S.Ctx);
		TextAt(Out, Layer, G, BoardW * 0.5f, 86.f, TagBind::Info(C).Desc, TagTheme::Font(14), TagTheme::Dim, 1);
		Layer = S.bKbd ? PaintKeyboard(Out, Layer + 1, G) : PaintPad(Out, Layer + 1, G);
		// what the last change did (it fades after a few seconds)
		const float Age = static_cast<float>(TagTheme::Now() - S.MsgT);
		if (!S.Msg.IsEmpty() && Age < 5.f)
		{
			const float A = FMath::Clamp(5.f - Age, 0.f, 1.f) * TagTheme::EaseOut(Age / 0.2f);
			const FSlateFontInfo F = TagTheme::Font(16, "BoldCondensed", 40);
			const FVector2D M = Measure(S.Msg, F);
			const FVector2D TL(BoardW * 0.5f - M.X * 0.5f - 22.f, BoardH - 50.f);
			TagTheme::Box(Out, Layer, G, TL, FVector2D(M.X + 44, 38), FLinearColor(0.02f, 0.025f, 0.04f, 0.9f * A), 19.f);
			TagTheme::Box(Out, Layer + 1, G, TL + FVector2D(14, 15), FVector2D(8, 8), S.MsgC * FLinearColor(1, 1, 1, A), 4.f);
			TextAt(Out, Layer + 2, G, BoardW * 0.5f + 6.f, BoardH - 31.f, S.Msg, F, FLinearColor(0.95f, 0.96f, 0.98f, A), 1);
			Layer += 3;
		}
		if (S.bCapture) Layer = PaintCapture(Out, Layer + 1, G);
		return Layer;
	}

	int32 SBoard::PaintTabs(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
	{
		const FCtlState& S = *State;
		const TArray<TagBind::FCtxInfo>& Cx = TagBind::Contexts();
		const float TabW = 196.f, Gap = 8.f, H = 62.f;
		const float Total = Cx.Num() * TabW + (Cx.Num() - 1) * Gap;
		const float X0 = BoardW * 0.5f - Total * 0.5f;
		const bool bPad = Owner->PadGlyphs();
		Owner->PaintGlyph(Out, Layer, G, FVector2D(X0 - 40, H * 0.5), 30.f, EPadGlyph::L1, TEXT("Q"), bPad, FLinearColor::White);
		Owner->PaintGlyph(Out, Layer, G, FVector2D(X0 + Total + 40, H * 0.5), 30.f, EPadGlyph::R1, TEXT("E"), bPad, FLinearColor::White);
		Layer += 6;
		for (int32 i = 0; i < Cx.Num(); ++i)
		{
			const FVector2D TL(X0 + i * (TabW + Gap), 0);
			const bool bOn = i == S.Ctx;
			if (bOn) TagTheme::PaintSelection(Out, Layer, G, TL, FVector2D(TabW, H), true, true, 8.f);
			else TagTheme::Box(Out, Layer, G, TL, FVector2D(TabW, H), FLinearColor(0.035f, 0.045f, 0.07f, 0.86f), 8.f);
			const float CX = static_cast<float>(TL.X) + TabW * 0.5f;
			TextAt(Out, Layer + 8, G, CX, 19.f, Cx[i].Group, TagTheme::Font(10, "BoldCondensed", 260), bOn ? TagTheme::Gold : TagTheme::Faint, 1);
			TextAt(Out, Layer + 8, G, CX, 41.f, Cx[i].Name.ToUpper(), TagTheme::Font(17, "BoldCondensed", 40), bOn ? FLinearColor::White : TagTheme::Dim, 1);
			// a dot: this tab's layout (for the view shown) is the player's own
			if (!TagBind::IsDefault(Cx[i].Ctx, !S.bKbd)) TagTheme::Box(Out, Layer + 8, G, FVector2D(TL.X + TabW - 16, 9), FVector2D(7, 7), TagTheme::Gold, 3.5f);
			S.TabRects.Add(FBox2D(TL, TL + FVector2D(TabW, H)));
		}
		return Layer + 10;
	}

	int32 SBoard::PaintPad(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
	{
		const FCtlState& S = *State;
		const ECtx C = static_cast<ECtx>(S.Ctx);
		const bool bSony = Owner->SonyPad();
		const float AW = 600.f, CX = BoardW * 0.5f, Top = 196.f;
		const float ColW = 450.f, Pitch = 62.f, RowH = 54.f, RowTop = 128.f;
		const TArray<FSlot>& Sl = Slots(bSony);
		auto ArtAt = [&](const FVector2f& U) { return FVector2f(CX + U.X * AW, Top + U.Y * AW); };
		// the lines to the buttons the cursor is not on run under the pad (only the chosen one crosses it)
		for (int32 i = 0; i < NumSlots && i < Sl.Num(); ++i)
		{
			if (i == S.Sel) continue;
			const bool bL = i < PerCol;
			const float RX = bL ? 0.f : BoardW - ColW, CY = RowTop + (bL ? i : i - PerCol) * Pitch + RowH * 0.5f;
			const bool bEmpty = Sl[i].Key.IsValid() ? ActionOnPad(C, Sl[i].Key) == nullptr : FixedName(C, Sl[i].Fixed, true).IsEmpty();
			const FLinearColor LC(1, 1, 1, bEmpty ? 0.07f : 0.2f);
			Lines(Out, Layer, G, {FVector2f(bL ? RX + ColW : RX, CY), FVector2f(bL ? RX + ColW + 26.f : RX - 26.f, CY), ArtAt(Sl[i].At)}, LC, 1.f);
		}
		Layer = PaintArt(Out, Layer + 2, G, bSony, CX, Top, AW);
		const float Pulse = 0.6f + 0.4f * FMath::Sin(static_cast<float>(TagTheme::Now()) * 6.f);
		const FSlateFontInfo LabelF = TagTheme::Font(16, "BoldCondensed", 20);
		for (int32 i = 0; i < NumSlots && i < Sl.Num(); ++i)
		{
			const FSlot& X = Sl[i];
			const bool bLeft = i < PerCol;
			const int32 Row = bLeft ? i : i - PerCol;
			const FVector2D TL(bLeft ? 0.f : BoardW - ColW, RowTop + Row * Pitch);
			const bool bSel = i == S.Sel;
			const bool bFixed = !X.Key.IsValid();
			const TagBind::FAction* A = bFixed ? nullptr : ActionOnPad(C, X.Key);
			const FString Fx = bFixed ? FixedName(C, X.Fixed, true) : FString();
			const bool bEmpty = bFixed ? Fx.IsEmpty() : A == nullptr;
			const bool bMoved = !bFixed && TagBind::Logical(C, X.Key) != X.Key && A;
			// the row
			if (bSel) TagTheme::PaintSelection(Out, Layer, G, TL, FVector2D(ColW, RowH), true, true, 7.f);
			else TagTheme::Box(Out, Layer, G, TL, FVector2D(ColW, RowH), FLinearColor(0.03f, 0.038f, 0.06f, bEmpty ? 0.55f : 0.82f), 7.f);
			S.RowRects.Add(FBox2D(TL, TL + FVector2D(ColW, RowH)));
			// the button at the end nearest the pad, the words beside it
			const float RX = static_cast<float>(TL.X);
			const float GlyphX = bLeft ? RX + ColW - 34.f : RX + 34.f;
			const float CY = static_cast<float>(TL.Y + RowH * 0.5);
			if (bFixed) Owner->PaintGlyph(Out, Layer + 8, G, FVector2D(GlyphX + (bLeft ? -14.f : 14.f), CY), 26.f, EPadGlyph::None, FixedKeycap(X.Fixed, bSony), false, TagTheme::Dim);
			else Owner->PaintGlyph(Out, Layer + 8, G, FVector2D(GlyphX, CY), 32.f, TagBind::GlyphOf(X.Key), FString(), true, FLinearColor::White);
			const float TextX = bLeft ? RX + ColW - (bFixed ? 92.f : 66.f) : RX + (bFixed ? 92.f : 66.f);
			const FString Words = bFixed ? (Fx.IsEmpty() ? FString(TEXT("-")) : Fx) : A ? A->Name : FString(TEXT("-"));
			const FLinearColor WC = bEmpty ? TagTheme::Faint : bFixed ? TagTheme::Dim : bSel ? FLinearColor::White : FLinearColor(0.9f, 0.92f, 0.95f);
			TextAt(Out, Layer + 14, G, TextX, CY, Words, LabelF, WC, bLeft ? 2 : 0);
			if (bFixed && !bEmpty)
			{
				const float W = static_cast<float>(Measure(Words, LabelF).X);
				Lock(Out, Layer + 14, G, FVector2f(bLeft ? TextX - W - 14.f : TextX + W + 14.f, CY), TagTheme::Faint);
			}
			if (bMoved) TagTheme::Box(Out, Layer + 14, G, FVector2D(bLeft ? RX + 12.f : RX + ColW - 19.f, CY - 3.5f), FVector2D(7, 7), TagTheme::Gold, 3.5f);
			// the line to the button on the pad
			const FVector2f Edge(bLeft ? RX + ColW : RX, CY);
			const FVector2f Elbow(bLeft ? RX + ColW + 26.f : RX - 26.f, CY);
			const FVector2f To = ArtAt(X.At);
			const FLinearColor LC = TagTheme::Gold * FLinearColor(1, 1, 1, 0.95f);
			if (bSel)
			{
				Lines(Out, Layer + 1, G, {Edge, Elbow, To}, LC, 2.f);
				Disc(Out, Layer + 30, G, To, 4.5f, LC, LC);
			}
			if (bSel)
			{
				// the button itself rings on the pad
				const float R = (X.Key == EKeys::Gamepad_LeftTrigger || X.Key == EKeys::Gamepad_RightTrigger || X.Key == EKeys::Gamepad_LeftShoulder ||
					X.Key == EKeys::Gamepad_RightShoulder) ? 0.055f * AW : bFixed || X.Key == EKeys::Gamepad_LeftThumbstick || X.Key == EKeys::Gamepad_RightThumbstick ? 0.07f * AW : 0.038f * AW;
				Lines(Out, Layer + 31, G, Circle(To, R + 3.f * Pulse), TagTheme::Gold * FLinearColor(1, 1, 1, 0.5f + 0.5f * Pulse), 2.5f);
			}
		}
		return Layer + 34;
	}

	int32 SBoard::PaintArt(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, bool bSony, float CX, float Top, float AW) const
	{
		auto At = [&](float U, float V) { return FVector2f(CX + U * AW, Top + V * AW); };
		auto Place = [&](const FShape& Sh) { TArray<FVector2f> P = Sh.P; for (FVector2f& X : P) X = At(X.X, X.Y); return P; };
		auto RBox = [&](float U, float V, float W, float H, const FLinearColor& Cl, float R, int32 L)
		{
			TagTheme::Box(Out, Layer + L, G, FVector2D(CX + U * AW, Top + V * AW), FVector2D(W * AW, H * AW), Cl, R * AW);
		};
		// the buttons the player has down right now, on any pad (the drawing answers the pad in his hands)
		TSet<FKey> Held;
		for (const FTagDevice& D : FTagInputHub::Get().Devices())
			if (D.bPad && D.TwinOf < 0) Held.Append(D.Down);
		const FLinearColor Glow = TagTheme::Gold * FLinearColor(1, 1, 1, 0.9f);
		auto HeldRing = [&](const FKey& K, const FVector2f& C, float R)
		{
			if (Held.Contains(K)) Lines(Out, Layer + 26, G, Circle(C, R), Glow, 3.f);
		};
		// a stick: its well, the cap shaded, the dished rim
		auto Stick = [&](const FVector2f& C, float R, bool bLight)
		{
			Disc(Out, Layer + 8, G, C, R * 1.24f, bLight ? FLinearColor(0.07f, 0.075f, 0.09f) : FLinearColor(0.018f, 0.02f, 0.026f), bLight ? FLinearColor(0.04f, 0.045f, 0.055f) : FLinearColor(0.012f, 0.014f, 0.018f));
			Disc(Out, Layer + 10, G, C, R, FLinearColor(0.2f, 0.205f, 0.225f), FLinearColor(0.085f, 0.09f, 0.1f), FLinearColor(0.42f, 0.44f, 0.48f, 0.7f), 1.3f);
			Lines(Out, Layer + 12, G, Circle(C, R * 0.7f), FLinearColor(0.02f, 0.022f, 0.03f, 0.7f), 2.f);
			Lines(Out, Layer + 12, G, Circle(C + FVector2f(0, 1.5f), R * 0.7f), FLinearColor(0.5f, 0.52f, 0.56f, 0.28f), 1.f);
		};
		const float BtnR = 0.027f * AW;
		auto Face = [&](float U, float V, EPadGlyph Gl)
		{
			const FVector2f C = At(U, V);
			Disc(Out, Layer + 8, G, C, BtnR * 1.18f, FLinearColor(0.f, 0.f, 0.f, 0.35f), FLinearColor(0.f, 0.f, 0.f, 0.5f));
			Owner->PaintGlyph(Out, Layer + 10, G, FVector2D(C), BtnR * 2.f, Gl, FString(), true, FLinearColor::White);
			HeldRing(TagBind::PadKeyOf(Gl), C, BtnR * 1.35f);
		};
		if (!bSony)
		{
			// ---- an Xbox pad: carbon black. The triggers behind, the bumpers, the body.
			for (int32 s = -1; s <= 1; s += 2)
			{
				const FKey T = s < 0 ? EKeys::Gamepad_LeftTrigger : EKeys::Gamepad_RightTrigger, B = s < 0 ? EKeys::Gamepad_LeftShoulder : EKeys::Gamepad_RightShoulder;
				RBox(s * 0.33f - 0.068f, -0.048f, 0.136f, 0.086f, FLinearColor(0.24f, 0.25f, 0.28f), 0.03f, 0);
				RBox(s * 0.33f - 0.065f, -0.045f, 0.13f, 0.08f, Held.Contains(T) ? TagTheme::GoldDeep : FLinearColor(0.10f, 0.105f, 0.12f), 0.028f, 1);
				RBox(s * 0.31f - 0.112f, 0.019f, 0.224f, 0.046f, FLinearColor(0.28f, 0.29f, 0.33f), 0.02f, 2);
				RBox(s * 0.31f - 0.11f, 0.022f, 0.22f, 0.04f, Held.Contains(B) ? TagTheme::GoldDeep : FLinearColor(0.13f, 0.135f, 0.155f), 0.018f, 3);
			}
			const FShape& Body = XboxBody();
			Fill(Out, Layer + 4, G, Place(Body), Body.T, [&](const FVector2f& X)
			{
				const float V = (X.Y - Top) / AW, U = (X.X - CX) / AW;
				const float Hi = FMath::Max(0.f, 1.f - FMath::Sqrt(U * U * 3.f + (V - 0.2f) * (V - 0.2f) * 6.f));
				return FMath::Lerp(FLinearColor(0.085f, 0.09f, 0.102f), FLinearColor(0.03f, 0.032f, 0.04f), FMath::Clamp(V / 0.7f, 0.f, 1.f)) + FLinearColor(0.035f, 0.037f, 0.042f, 0.f) * Hi;
			});
			TArray<FVector2f> Rim = Place(Body);
			{ const FVector2f First = Rim[0]; Rim.Add(First); }
			Lines(Out, Layer + 5, G, Rim, FLinearColor(0.34f, 0.36f, 0.4f, 0.9f), 2.f);
			// the guide button (lit), view, menu, share
			Disc(Out, Layer + 8, G, At(0.f, 0.135f), 0.036f * AW, FLinearColor(0.2f, 0.21f, 0.23f), FLinearColor(0.12f, 0.125f, 0.14f), FLinearColor(0.9f, 0.92f, 0.95f, 0.85f), 2.f);
			Disc(Out, Layer + 8, G, At(-0.075f, 0.215f), 0.018f * AW, FLinearColor(0.16f, 0.165f, 0.18f), FLinearColor(0.09f, 0.095f, 0.11f), FLinearColor(0.4f, 0.42f, 0.46f, 0.7f));
			Disc(Out, Layer + 8, G, At(0.075f, 0.215f), 0.018f * AW, FLinearColor(0.16f, 0.165f, 0.18f), FLinearColor(0.09f, 0.095f, 0.11f), FLinearColor(0.4f, 0.42f, 0.46f, 0.7f));
			{
				// view: two windows; menu: three lines
				const FVector2f V = At(-0.075f, 0.215f), M = At(0.075f, 0.215f);
				const FLinearColor Ic(0.8f, 0.82f, 0.86f, 0.9f);
				Lines(Out, Layer + 10, G, {V + FVector2f(-4, -2), V + FVector2f(1, -2), V + FVector2f(1, 3), V + FVector2f(-4, 3), V + FVector2f(-4, -2)}, Ic, 1.2f);
				Lines(Out, Layer + 10, G, {V + FVector2f(-1, -5), V + FVector2f(4, -5), V + FVector2f(4, 0)}, Ic, 1.2f);
				for (int32 k = -1; k <= 1; ++k) Lines(Out, Layer + 10, G, {M + FVector2f(-4.5f, k * 3.f), M + FVector2f(4.5f, k * 3.f)}, Ic, 1.3f);
				HeldRing(EKeys::Gamepad_Special_Left, V, 0.024f * AW);
				HeldRing(EKeys::Gamepad_Special_Right, M, 0.024f * AW);
			}
			RBox(-0.014f, 0.262f, 0.028f, 0.014f, FLinearColor(0.15f, 0.155f, 0.17f), 0.007f, 8);
			// the sticks, the D-pad
			Stick(At(-0.245f, 0.215f), 0.058f * AW, false);
			Stick(At(0.12f, 0.345f), 0.056f * AW, false);
			HeldRing(EKeys::Gamepad_LeftThumbstick, At(-0.245f, 0.215f), 0.075f * AW);
			HeldRing(EKeys::Gamepad_RightThumbstick, At(0.12f, 0.345f), 0.072f * AW);
			Disc(Out, Layer + 8, G, At(-0.12f, 0.345f), 0.064f * AW, FLinearColor(0.02f, 0.022f, 0.028f), FLinearColor(0.012f, 0.014f, 0.018f));
			RBox(-0.12f - 0.05f, 0.345f - 0.017f, 0.10f, 0.034f, FLinearColor(0.19f, 0.195f, 0.215f), 0.008f, 10);
			RBox(-0.12f - 0.017f, 0.345f - 0.05f, 0.034f, 0.10f, FLinearColor(0.19f, 0.195f, 0.215f), 0.008f, 10);
			{
				const FLinearColor Ar(0.05f, 0.055f, 0.065f);
				const FVector2f D = At(-0.12f, 0.345f);
				const float a = 0.034f * AW, w = 0.009f * AW;
				Lines(Out, Layer + 12, G, {D + FVector2f(-w, -a + w), D + FVector2f(0, -a), D + FVector2f(w, -a + w)}, Ar, 1.6f);
				Lines(Out, Layer + 12, G, {D + FVector2f(-w, a - w), D + FVector2f(0, a), D + FVector2f(w, a - w)}, Ar, 1.6f);
				Lines(Out, Layer + 12, G, {D + FVector2f(-a + w, -w), D + FVector2f(-a, 0), D + FVector2f(-a + w, w)}, Ar, 1.6f);
				Lines(Out, Layer + 12, G, {D + FVector2f(a - w, -w), D + FVector2f(a, 0), D + FVector2f(a - w, w)}, Ar, 1.6f);
				HeldRing(EKeys::Gamepad_DPad_Up, D + FVector2f(0, -a * 0.75f), 0.022f * AW);
				HeldRing(EKeys::Gamepad_DPad_Down, D + FVector2f(0, a * 0.75f), 0.022f * AW);
				HeldRing(EKeys::Gamepad_DPad_Left, D + FVector2f(-a * 0.75f, 0), 0.022f * AW);
				HeldRing(EKeys::Gamepad_DPad_Right, D + FVector2f(a * 0.75f, 0), 0.022f * AW);
			}
			Face(0.245f, 0.163f, EPadGlyph::Triangle);
			Face(0.297f, 0.215f, EPadGlyph::Circle);
			Face(0.193f, 0.215f, EPadGlyph::Square);
			Face(0.245f, 0.267f, EPadGlyph::Cross);
			// the pad's name across the foot
			TextAt(Out, Layer + 12, G, CX, Top + 0.6f * AW, TEXT("XBOX WIRELESS CONTROLLER"), TagTheme::Font(11, "BoldCondensed", 300), FLinearColor(0.42f, 0.44f, 0.5f), 1);
			return Layer + 32;
		}
		// ---- a DualSense: white wings, the black middle, the touch pad with its light bar
		for (int32 s = -1; s <= 1; s += 2)
		{
			const FKey T = s < 0 ? EKeys::Gamepad_LeftTrigger : EKeys::Gamepad_RightTrigger, B = s < 0 ? EKeys::Gamepad_LeftShoulder : EKeys::Gamepad_RightShoulder;
			RBox(s * 0.32f - 0.073f, -0.053f, 0.146f, 0.096f, FLinearColor(0.3f, 0.31f, 0.34f), 0.036f, 0);
			RBox(s * 0.32f - 0.07f, -0.05f, 0.14f, 0.09f, Held.Contains(T) ? TagTheme::GoldDeep : FLinearColor(0.09f, 0.095f, 0.11f), 0.034f, 1);
			RBox(s * 0.31f - 0.112f, 0.021f, 0.224f, 0.048f, FLinearColor(0.32f, 0.33f, 0.36f), 0.022f, 2);
			RBox(s * 0.31f - 0.11f, 0.024f, 0.22f, 0.042f, Held.Contains(B) ? TagTheme::GoldDeep : FLinearColor(0.08f, 0.085f, 0.1f), 0.02f, 3);
		}
		const FShape& Body = SonyBody();
		Fill(Out, Layer + 4, G, Place(Body), Body.T, [&](const FVector2f& X)
		{
			const float V = (X.Y - Top) / AW, U = (X.X - CX) / AW;
			const float Hi = FMath::Max(0.f, 1.f - FMath::Abs(FMath::Abs(U) - 0.3f) * 5.f) * FMath::Max(0.f, 1.f - FMath::Abs(V - 0.25f) * 4.f);
			return FMath::Lerp(FLinearColor(0.9f, 0.91f, 0.935f), FLinearColor(0.62f, 0.64f, 0.69f), FMath::Clamp(V / 0.72f, 0.f, 1.f)) + FLinearColor(0.06f, 0.06f, 0.06f, 0.f) * Hi;
		});
		TArray<FVector2f> Rim = Place(Body);
		{ const FVector2f First = Rim[0]; Rim.Add(First); }
		Lines(Out, Layer + 5, G, Rim, FLinearColor(0.97f, 0.98f, 1.f, 0.85f), 2.f);
		const FShape& Plate = SonyPlate();
		Fill(Out, Layer + 6, G, Place(Plate), Plate.T, [&](const FVector2f& X)
		{
			const float V = (X.Y - Top) / AW;
			return FMath::Lerp(FLinearColor(0.075f, 0.08f, 0.095f), FLinearColor(0.035f, 0.038f, 0.047f), FMath::Clamp((V - 0.27f) / 0.18f, 0.f, 1.f));
		});
		// the touch pad (its click is OPTIONS' neighbour: drawn, not a button here), the light bar either side of it
		RBox(-0.159f, 0.058f, 0.318f, 0.205f, FLinearColor(0.58f, 0.6f, 0.65f), 0.024f, 7);
		RBox(-0.155f, 0.062f, 0.31f, 0.197f, FLinearColor(0.86f, 0.87f, 0.9f), 0.022f, 8);
		RBox(-0.17f, 0.085f, 0.007f, 0.15f, FLinearColor(0.25f, 0.55f, 1.f), 0.0035f, 9);
		RBox(0.163f, 0.085f, 0.007f, 0.15f, FLinearColor(0.25f, 0.55f, 1.f), 0.0035f, 9);
		// create, options
		RBox(-0.2f - 0.01f, 0.12f - 0.018f, 0.02f, 0.036f, FLinearColor(0.12f, 0.125f, 0.14f), 0.01f, 9);
		RBox(0.2f - 0.01f, 0.12f - 0.018f, 0.02f, 0.036f, FLinearColor(0.12f, 0.125f, 0.14f), 0.01f, 9);
		HeldRing(EKeys::Gamepad_Special_Left, At(-0.2f, 0.12f), 0.03f * AW);
		HeldRing(EKeys::Gamepad_Special_Right, At(0.2f, 0.12f), 0.03f * AW);
		// the D-pad: four arrows of their own
		{
			const FLinearColor Dp(0.2f, 0.21f, 0.235f), DpEdge(0.08f, 0.085f, 0.1f);
			const float Ux = -0.30f, Vy = 0.225f, o = 0.045f;
			RBox(Ux - 0.02f, Vy - o - 0.019f, 0.04f, 0.038f, DpEdge, 0.01f, 9);
			RBox(Ux - 0.018f, Vy - o - 0.017f, 0.036f, 0.034f, Dp, 0.009f, 10);
			RBox(Ux - 0.02f, Vy + o - 0.019f, 0.04f, 0.038f, DpEdge, 0.01f, 9);
			RBox(Ux - 0.018f, Vy + o - 0.017f, 0.036f, 0.034f, Dp, 0.009f, 10);
			RBox(Ux - o - 0.019f, Vy - 0.02f, 0.038f, 0.04f, DpEdge, 0.01f, 9);
			RBox(Ux - o - 0.017f, Vy - 0.018f, 0.034f, 0.036f, Dp, 0.009f, 10);
			RBox(Ux + o - 0.019f, Vy - 0.02f, 0.038f, 0.04f, DpEdge, 0.01f, 9);
			RBox(Ux + o - 0.017f, Vy - 0.018f, 0.034f, 0.036f, Dp, 0.009f, 10);
			HeldRing(EKeys::Gamepad_DPad_Up, At(Ux, Vy - o), 0.026f * AW);
			HeldRing(EKeys::Gamepad_DPad_Down, At(Ux, Vy + o), 0.026f * AW);
			HeldRing(EKeys::Gamepad_DPad_Left, At(Ux - o, Vy), 0.026f * AW);
			HeldRing(EKeys::Gamepad_DPad_Right, At(Ux + o, Vy), 0.026f * AW);
		}
		Face(0.30f, 0.175f, EPadGlyph::Triangle);
		Face(0.35f, 0.225f, EPadGlyph::Circle);
		Face(0.25f, 0.225f, EPadGlyph::Square);
		Face(0.30f, 0.275f, EPadGlyph::Cross);
		Stick(At(-0.125f, 0.36f), 0.056f * AW, true);
		Stick(At(0.125f, 0.36f), 0.056f * AW, true);
		HeldRing(EKeys::Gamepad_LeftThumbstick, At(-0.125f, 0.36f), 0.072f * AW);
		HeldRing(EKeys::Gamepad_RightThumbstick, At(0.125f, 0.36f), 0.072f * AW);
		// the PS button, the mute button
		Disc(Out, Layer + 10, G, At(0.f, 0.36f), 0.02f * AW, FLinearColor(0.16f, 0.17f, 0.19f), FLinearColor(0.1f, 0.105f, 0.12f), FLinearColor(0.5f, 0.52f, 0.56f, 0.7f), 1.2f);
		RBox(-0.02f, 0.405f, 0.04f, 0.009f, FLinearColor(0.18f, 0.185f, 0.2f), 0.0045f, 10);
		TextAt(Out, Layer + 12, G, CX, Top + 0.6f * AW, TEXT("DUALSENSE WIRELESS CONTROLLER"), TagTheme::Font(11, "BoldCondensed", 300), FLinearColor(0.5f, 0.52f, 0.58f), 1);
		return Layer + 32;
	}

	int32 SBoard::PaintKeyboard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
	{
		const FCtlState& S = *State;
		const ECtx C = static_cast<ECtx>(S.Ctx);
		const TArray<FKRow> Rows = KbdRows(C);
		// two columns when the list is long, else one down the middle; the panel as tall as the rows
		const float Gap = 40.f, ColW = (BoardW - 300.f - Gap) * 0.5f, Pitch = 50.f, RowH = 44.f, Y0 = 124.f;
		const bool bTwo = Rows.Num() > KbdPerCol;
		const float X0 = bTwo ? 150.f : (BoardW - ColW) * 0.5f;
		const float PanelW = bTwo ? BoardW - 300.f : ColW;
		const int32 Tall = FMath::Min(Rows.Num(), KbdPerCol);
		TagTheme::PaintPanel(Out, Layer, G, FVector2D(X0 - 24.f, Y0 - 18.f), FVector2D(PanelW + 48.f, Tall * Pitch + 30.f), FLinearColor(0.02f, 0.026f, 0.044f, 0.8f), 10.f);
		Layer += 4;
		const FSlateFontInfo F = TagTheme::Font(16, "BoldCondensed", 20);
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			const FKRow& R = Rows[i];
			const int32 Col = i / KbdPerCol, Row = i % KbdPerCol;
			const FVector2D TL(X0 + Col * (ColW + Gap), Y0 + Row * Pitch);
			const bool bSel = i == S.Sel;
			if (bSel) TagTheme::PaintSelection(Out, Layer, G, TL, FVector2D(ColW, RowH), true, true, 7.f);
			else TagTheme::Box(Out, Layer, G, TL, FVector2D(ColW, RowH), FLinearColor(0.04f, 0.05f, 0.075f, R.A ? 0.8f : 0.45f), 7.f);
			S.RowRects.Add(FBox2D(TL, TL + FVector2D(ColW, RowH)));
			const float CY = static_cast<float>(TL.Y + RowH * 0.5);
			FString Key = R.A ? TagBind::KeyLabel(TagBind::Phys(C, R.A->Kbd)) : R.Where;
			const FVector2D Ext = Owner->GlyphExtent(EPadGlyph::None, Key, 28.f, false);
			Owner->PaintGlyph(Out, Layer + 8, G, FVector2D(TL.X + 18.f + Ext.X * 0.5f, CY), 28.f, EPadGlyph::None, Key, false, R.A ? FLinearColor::White : TagTheme::Dim);
			const float TX = static_cast<float>(TL.X + 18.f + FMath::Max<double>(Ext.X, 70.0) + 14.f);
			const FString Words = R.A ? R.A->Name : R.Name;
			TextAt(Out, Layer + 14, G, TX, CY, Words, F, R.A ? (bSel ? FLinearColor::White : FLinearColor(0.9f, 0.92f, 0.95f)) : TagTheme::Dim, 0);
			if (!R.A) Lock(Out, Layer + 14, G, FVector2f(TX + static_cast<float>(Measure(Words, F).X) + 14.f, CY), TagTheme::Faint);
			else if (TagBind::Phys(C, R.A->Kbd) != R.A->Kbd) TagTheme::Box(Out, Layer + 14, G, FVector2D(TL.X + ColW - 20.f, CY - 3.5f), FVector2D(7, 7), TagTheme::Gold, 3.5f);
			if (R.A && !R.A->Note.IsEmpty() && bSel)
				TextAt(Out, Layer + 14, G, static_cast<float>(TL.X + ColW - 34.f), CY, R.A->Note, TagTheme::Font(11), TagTheme::Dim, 2);
		}
		return Layer + 20;
	}

	int32 SBoard::PaintCapture(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G) const
	{
		const FCtlState& S = *State;
		const ECtx C = static_cast<ECtx>(S.Ctx);
		FString What;
		if (S.bKbd)
		{
			const TArray<FKRow> Rows = KbdRows(C);
			if (Rows.IsValidIndex(S.Sel) && Rows[S.Sel].A) What = Rows[S.Sel].A->Name;
		}
		else if (const TagBind::FAction* A = Slots(Owner->SonyPad()).IsValidIndex(S.Sel) ? ActionOnPad(C, Slots(Owner->SonyPad())[S.Sel].Key) : nullptr) What = A->Name;
		TagTheme::Box(Out, Layer, G, FVector2D(0, 100), FVector2D(BoardW, BoardH - 100), FLinearColor(0.f, 0.f, 0.01f, 0.62f), 12.f);
		const FVector2D Sz(780, 236), TL(BoardW * 0.5f - Sz.X * 0.5f, 250);
		Layer = TagTheme::PaintPanel(Out, Layer + 1, G, TL, Sz, FLinearColor(0.03f, 0.038f, 0.062f, 0.98f), 12.f);
		TagTheme::Box(Out, Layer, G, TL, FVector2D(Sz.X, 4), TagTheme::Gold, 2.f);
		const float CX = BoardW * 0.5f, TY = static_cast<float>(TL.Y);
		TextAt(Out, Layer + 1, G, CX, TY + 42, S.bKbd ? TEXT("PRESS THE NEW KEY") : TEXT("PRESS THE NEW BUTTON"), TagTheme::Font(13, "BoldCondensed", 320), TagTheme::Gold, 1);
		TextAt(Out, Layer + 1, G, CX, TY + 92, What.ToUpper(), TagTheme::Font(34, "Black", 10), FLinearColor::White, 1);
		const bool bPadNow = Owner->PadGlyphs();
		const FString Cancel = S.bKbd ? FString(TEXT("ESC cancels")) : bPadNow ? FString::Printf(TEXT("%s cancels"), Owner->SonyPad() ? TEXT("OPTIONS") : TEXT("MENU")) : FString(TEXT("ESC cancels"));
		TextAt(Out, Layer + 1, G, CX, TY + 140, FString::Printf(TEXT("%s  %s   -   whatever is there now takes its old one   -   %s"),
			*TagBind::Info(C).Group, *TagBind::Info(C).Name.ToUpper(), *Cancel), TagTheme::Font(13, "BoldCondensed", 40), TagTheme::Dim, 1);
		// the time left to press it
		const float Left = FMath::Clamp(1.f - static_cast<float>((TagTheme::Now() - S.CaptureT) / CaptureSecs), 0.f, 1.f);
		TagTheme::Box(Out, Layer + 1, G, FVector2D(TL.X + 60, TL.Y + 186), FVector2D(Sz.X - 120, 6), FLinearColor(1, 1, 1, 0.08f), 3.f);
		TagTheme::Box(Out, Layer + 2, G, FVector2D(TL.X + 60, TL.Y + 186), FVector2D((Sz.X - 120) * Left, 6), TagTheme::Gold, 3.f);
		return Layer + 4;
	}
}

// ------------------------------------------------------------------ the screen
bool SGridironOverlay::CtlHasPad() const
{
	return Director && (Director->bPlayStationPad || Director->bXboxPad || Director->PadUI());
}

void SGridironOverlay::CtlSay(const FString& Msg, const FLinearColor& C)
{
	if (!Ctl) return;
	Ctl->Msg = Msg;
	Ctl->MsgC = C;
	Ctl->MsgT = TagTheme::Now();
}

void SGridironOverlay::ShowControls(std::function<void()> Back)
{
	ATagAudio::Screen(TEXT("Controls"));
	if (!Ctl) Ctl = MakeShared<FCtlState>();
	FCtlState& S = *Ctl;
	S.Back = Back;
	S.bCapture = false;
	S.bViewPicked = false;
	S.bKbd = !CtlHasPad();
	S.Sel = S.bKbd ? 0 : CtlUI::PerCol + 6; // (the pad's CROSS / A: its busiest button)
	S.Msg.Reset();
	TagBind::Load();
	const TSharedPtr<FCtlState> St = Ctl;
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());

	// the button bar (each hint follows the device in use and the view)
	auto Hint = [this](EPadGlyph Pad, const FString& Kbd, std::function<FString()> What, std::function<FString()> PadKey = nullptr) -> TSharedRef<SWidget>
	{
		return SNew(CtlUI::SHint).Owner(this).Pad(Pad).Kbd(Kbd).What(What).PadKey(PadKey);
	};
	TSharedRef<SHorizontalBox> Hints = SNew(SHorizontalBox);
	auto Add = [&Hints](TSharedRef<SWidget> H) { Hints->AddSlot().AutoWidth().VAlign(VAlign_Center)[H]; };
	Add(Hint(EPadGlyph::DUpDown, TEXT("↑↓"), [] { return FString(TEXT("Move")); }));
	Add(Hint(EPadGlyph::Cross, TEXT("Enter"), [St] { return FString(St->bKbd ? TEXT("Change key") : TEXT("Change button")); }));
	Add(Hint(EPadGlyph::Triangle, TEXT("T"), [] { return FString(TEXT("Reset tab")); }));
	Add(Hint(EPadGlyph::Square, TEXT("K"), [St] { return FString(St->bKbd ? TEXT("Controller view") : TEXT("Keyboard view")); }));
	Add(Hint(EPadGlyph::None, TEXT("G"), [] {
		const int32 Gs = TagBind::GlyphStyle();
		return FString::Printf(TEXT("Buttons: %s"), Gs == 1 ? TEXT("Xbox") : Gs == 2 ? TEXT("PlayStation") : TEXT("Auto")); },
		[W] { const TSharedPtr<SGridironOverlay> O = W.Pin(); return FString(O && O->SonyPad() ? TEXT("CREATE") : TEXT("VIEW")); }));
	Add(Hint(EPadGlyph::R3, TEXT("Del"), [] { return FString(TEXT("Reset all")); }));
	Add(Hint(EPadGlyph::Circle, TEXT("Esc"), [] { return FString(TEXT("Back")); }));

	// the pad in the player's hands, in the corner
	auto Device = [W]() -> FString
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director) return FString();
		const AMatchDirector& D = *O->Director;
		if (D.bPlayStationPad && D.bXboxPad) return TEXT("PS5 + XBOX CONTROLLERS CONNECTED");
		if (D.bPlayStationPad) return TEXT("PS5 CONTROLLER CONNECTED");
		if (D.bXboxPad) return TEXT("XBOX CONTROLLER CONNECTED");
		return D.PadUI() ? TEXT("CONTROLLER CONNECTED") : TEXT("NO CONTROLLER  -  KEYBOARD");
	};
	auto DeviceOn = [W]() { const TSharedPtr<SGridironOverlay> O = W.Pin(); return O && O->CtlHasPad(); };
	TSharedRef<SWidget> Chip = SNew(SBorder).BorderImage(TagTheme::Round(16.f)).BorderBackgroundColor(FLinearColor(0.03f, 0.04f, 0.065f, 0.92f)).Padding(FMargin(16, 8, 18, 8))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
		[
			SNew(SBox).WidthOverride(10).HeightOverride(10)
			[ SNew(SBorder).BorderImage(TagTheme::Round(5.f)).BorderBackgroundColor_Lambda([DeviceOn] { return FSlateColor(DeviceOn() ? FLinearColor(0.25f, 0.95f, 0.45f) : TagTheme::Faint); }) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[ SNew(STextBlock).Font(TagTheme::Font(13, "BoldCondensed", 160)).ColorAndOpacity(TagTheme::Text).Text_Lambda([Device] { return FText::FromString(Device()); }) ]
	];

	TSharedRef<CtlUI::SRoot> Root = SNew(CtlUI::SRoot)
		.OnKey([W](const FKey& K, bool bRepeat) { const TSharedPtr<SGridironOverlay> O = W.Pin(); return O && O->CtlKey(K, bRepeat); })
		.OnTick([W] { if (const TSharedPtr<SGridironOverlay> O = W.Pin()) O->CtlTick(); })
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SBorder).BorderImage(TagTheme::White()).BorderBackgroundColor(FLinearColor(0.006f, 0.008f, 0.016f, 0.72f)) ]
		+ SOverlay::Slot()[ SNew(TagTheme::SBackdrop).Strength(1.f) ]
		+ SOverlay::Slot().VAlign(VAlign_Top).Padding(FMargin(80, 40, 80, 0)) // (the chip under the toasts' corner)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ TagTheme::Title(TEXT("Settings"), TEXT("Controls"), 36) ]
			+ SHorizontalBox::Slot().FillWidth(1)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0, 0, 0, 4)[ Chip ]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0, 70, 0, 40))
		[
			SNew(CtlUI::SBoard).Owner(this).State(Ctl).OnClick([W](int32 Kind, int32 I)
			{
				const TSharedPtr<SGridironOverlay> O = W.Pin();
				if (!O || !O->Ctl || O->Ctl->bCapture) return;
				FCtlState& S2 = *O->Ctl;
				if (Kind == 0) { S2.Ctx = I; ATagAudio::Ui(TEXT("UiTab")); return; }
				if (S2.Sel == I) O->CtlKey(EKeys::Enter, false);
				else { S2.Sel = I; ATagAudio::Ui(TEXT("UiMove")); }
			})
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0, 0, 0, 24))[ TagTheme::HintBar(Hints) ]
	];
	CallRoot = Root;  // (the screen draws its own cursor: no focus frame)
	ShowModal(Root);
}

void SGridironOverlay::CtlTick()
{
	if (!Ctl) return;
	FCtlState& S = *Ctl;
	if (S.bCapture && TagTheme::Now() - S.CaptureT > CtlUI::CaptureSecs)
	{
		S.bCapture = false;
		CtlSay(TEXT("No button pressed: nothing changed"), TagTheme::Dim);
	}
	// the view follows the pad being plugged in or taken out, until the player picks one
	if (!S.bViewPicked && !S.bCapture)
	{
		const bool bKbd = !CtlHasPad();
		if (bKbd != S.bKbd) { S.bKbd = bKbd; S.Sel = bKbd ? 0 : CtlUI::PerCol + 6; }
	}
}

bool SGridironOverlay::CtlKey(const FKey& K, bool bRepeat)
{
	if (!Ctl) return false;
	FCtlState& S = *Ctl;
	const ECtx C = static_cast<ECtx>(S.Ctx);
	const bool bSony = bSonyPad;
	auto Name = [&](const FKey& X) { return S.bKbd ? TagBind::KeyLabel(X) : TagBind::PadName(X, bSony); };
	// ---- waiting for the new button
	if (S.bCapture)
	{
		if (bRepeat) return true;
		const bool bCancel = K == EKeys::Escape || K == EKeys::Gamepad_Special_Right || (S.bKbd && K == EKeys::Gamepad_FaceButton_Right);
		if (bCancel)
		{
			S.bCapture = false;
			ATagAudio::Ui(TEXT("UiBack"));
			CtlSay(TEXT("Nothing changed"), TagTheme::Dim);
			return true;
		}
		FKey L;
		FString What;
		if (S.bKbd)
		{
			if (K.IsGamepadKey()) return true;
			const TArray<CtlUI::FKRow> Rows = CtlUI::KbdRows(C);
			if (!Rows.IsValidIndex(S.Sel) || !Rows[S.Sel].A) { S.bCapture = false; return true; }
			if (!TagBind::CanBind(C, K))
			{
				ATagAudio::Ui(TEXT("UiBack"));
				CtlSay(FString::Printf(TEXT("%s is kept for moving, the camera or pause: another key"), *TagBind::KeyLabel(K)), FLinearColor(0.94f, 0.44f, 0.24f));
				return true;
			}
			L = Rows[S.Sel].A->Kbd;
			What = Rows[S.Sel].A->Name;
		}
		else
		{
			if (!K.IsGamepadKey() || !TagBind::PadButtons().Contains(K)) return true;  // (a stick pushed, VIEW: not a button for this)
			const TArray<CtlUI::FSlot>& Sl = CtlUI::Slots(bSony);
			const TagBind::FAction* A = Sl.IsValidIndex(S.Sel) ? CtlUI::ActionOnPad(C, Sl[S.Sel].Key) : nullptr;
			if (!A) { S.bCapture = false; return true; }
			L = A->Pad;
			What = A->Name;
		}
		S.bCapture = false;
		const FKey Was = TagBind::Phys(C, L);
		if (Was == K)
		{
			ATagAudio::Ui(TEXT("UiMove"));
			CtlSay(FString::Printf(TEXT("%s is already on %s"), *What, *Name(K)), TagTheme::Dim);
			return true;
		}
		const FKey Moved = TagBind::Assign(C, L, K);
		const TagBind::FAction* Other = nullptr;
		for (const TagBind::FAction& A : TagBind::Actions(C))
			if ((S.bKbd ? A.Kbd : A.Pad) == Moved && Moved.IsValid()) Other = &A;
		ATagAudio::Ui(TEXT("UiSelect"));
		CtlSay(Other ? FString::Printf(TEXT("%s on %s   -   %s moved to %s"), *What, *Name(K), *Other->Name, *Name(Was))
			: FString::Printf(TEXT("%s on %s"), *What, *Name(K)), TagTheme::Gold);
		// the cursor goes with the action to its new button
		if (!S.bKbd)
		{
			const TArray<CtlUI::FSlot>& Sl = CtlUI::Slots(bSony);
			for (int32 i = 0; i < Sl.Num(); ++i)
				if (Sl[i].Key == K) S.Sel = i;
		}
		return true;
	}
	// ---- moving about
	const bool bLeft = K == EKeys::Gamepad_DPad_Left || K == EKeys::Gamepad_LeftStick_Left || K == EKeys::Left || K == EKeys::A;
	const bool bRight = K == EKeys::Gamepad_DPad_Right || K == EKeys::Gamepad_LeftStick_Right || K == EKeys::Right || K == EKeys::D;
	const bool bUp = K == EKeys::Gamepad_DPad_Up || K == EKeys::Gamepad_LeftStick_Up || K == EKeys::Up || K == EKeys::W;
	const bool bDown = K == EKeys::Gamepad_DPad_Down || K == EKeys::Gamepad_LeftStick_Down || K == EKeys::Down || K == EKeys::S;
	const int32 NRows = S.bKbd ? CtlUI::KbdRows(C).Num() : CtlUI::NumSlots;
	if (bUp || bDown || bLeft || bRight)
	{
		const int32 Was = S.Sel;
		if (S.bKbd)
		{
			const int32 P = CtlUI::KbdPerCol;
			if (bUp) S.Sel = FMath::Max(0, S.Sel - 1);
			if (bDown) S.Sel = FMath::Min(NRows - 1, S.Sel + 1);
			if (bLeft && S.Sel >= P) S.Sel -= P;
			if (bRight && S.Sel + P < NRows) S.Sel += P;
			if (bRight && S.Sel + P >= NRows && S.Sel < P && NRows > P) S.Sel = NRows - 1;
		}
		else
		{
			const int32 P = CtlUI::PerCol;
			const int32 Col = S.Sel / P, Row = S.Sel % P;
			if (bUp) S.Sel = Col * P + FMath::Max(0, Row - 1);
			if (bDown) S.Sel = Col * P + FMath::Min(P - 1, Row + 1);
			if (bLeft || bRight) S.Sel = (bRight ? 1 : 0) * P + Row;
		}
		if (S.Sel != Was) ATagAudio::Ui(TEXT("UiMove"));
		return true;
	}
	if (bRepeat) return true;
	const bool bPrev = K == EKeys::Gamepad_LeftShoulder || K == EKeys::Q || K == EKeys::PageUp;
	const bool bNext = K == EKeys::Gamepad_RightShoulder || K == EKeys::E || K == EKeys::PageDown;
	if (bPrev || bNext)
	{
		S.Ctx = (S.Ctx + (bNext ? 1 : -1) + TagBind::NumCtx) % TagBind::NumCtx;
		if (S.bKbd) S.Sel = FMath::Clamp(S.Sel, 0, FMath::Max(0, CtlUI::KbdRows(static_cast<ECtx>(S.Ctx)).Num() - 1));
		ATagAudio::Ui(TEXT("UiTab"));
		return true;
	}
	if (K == EKeys::Gamepad_FaceButton_Bottom || K == EKeys::Enter || K == EKeys::SpaceBar)
	{
		// the button / key the cursor is on: wait for its new one (a control that stays put says so)
		if (S.bKbd)
		{
			const TArray<CtlUI::FKRow> Rows = CtlUI::KbdRows(C);
			if (!Rows.IsValidIndex(S.Sel)) return true;
			if (!Rows[S.Sel].A) { ATagAudio::Ui(TEXT("UiBack")); CtlSay(FString::Printf(TEXT("%s stays on %s"), *Rows[S.Sel].Name, *Rows[S.Sel].Where), TagTheme::Dim); return true; }
		}
		else
		{
			const TArray<CtlUI::FSlot>& Sl = CtlUI::Slots(bSony);
			if (!Sl.IsValidIndex(S.Sel)) return true;
			const CtlUI::FSlot& X = Sl[S.Sel];
			if (!X.Key.IsValid())
			{
				ATagAudio::Ui(TEXT("UiBack"));
				const FString Fx = CtlUI::FixedName(C, X.Fixed, true);
				CtlSay(Fx.IsEmpty() ? FString::Printf(TEXT("Nothing on %s here"), *CtlUI::FixedKeycap(X.Fixed, bSony)) : FString::Printf(TEXT("%s stays on %s"), *Fx, *CtlUI::FixedKeycap(X.Fixed, bSony)), TagTheme::Dim);
				return true;
			}
			if (!CtlUI::ActionOnPad(C, X.Key))
			{
				ATagAudio::Ui(TEXT("UiBack"));
				CtlSay(FString::Printf(TEXT("Nothing on %s here: pick an action and press this button to move it here"), *TagBind::PadName(X.Key, bSony)), TagTheme::Dim);
				return true;
			}
		}
		S.bCapture = true;
		S.CaptureT = TagTheme::Now();
		ATagAudio::Ui(TEXT("UiSelect"));
		return true;
	}
	if (K == EKeys::Gamepad_FaceButton_Top || K == EKeys::T)
	{
		TagBind::Reset(C, !S.bKbd);
		ATagAudio::Ui(TEXT("UiTab"));
		CtlSay(FString::Printf(TEXT("%s %s: the %s layout back to the defaults"), *TagBind::Info(C).Group, *TagBind::Info(C).Name, S.bKbd ? TEXT("keyboard") : TEXT("controller")), TagTheme::Gold);
		return true;
	}
	if (K == EKeys::Gamepad_FaceButton_Left || K == EKeys::K)
	{
		S.bKbd = !S.bKbd;
		S.bViewPicked = true;
		S.Sel = S.bKbd ? 0 : CtlUI::PerCol + 6;
		ATagAudio::Ui(TEXT("UiTab"));
		return true;
	}
	if (K == EKeys::Gamepad_Special_Left || K == EKeys::G)
	{
		// the buttons drawn: the pad plugged in, or always Xbox / PlayStation (the game's prompts follow)
		TagBind::SetGlyphStyle((TagBind::GlyphStyle() + 1) % 3);
		SetPadKind(TagBind::Sony(Director && Director->bPlayStationPad));
		const int32 Gs = TagBind::GlyphStyle();
		ATagAudio::Ui(TEXT("UiTab"));
		CtlSay(Gs == 1 ? FString(TEXT("Buttons: always Xbox")) : Gs == 2 ? FString(TEXT("Buttons: always PlayStation")) : FString(TEXT("Buttons: the controller plugged in")), TagTheme::Gold);
		return true;
	}
	if (K == EKeys::Gamepad_RightThumbstick || K == EKeys::Delete)
	{
		TagBind::ResetAll();
		ATagAudio::Ui(TEXT("UiTab"));
		CtlSay(TEXT("Every tab, controller and keyboard, back to the defaults"), TagTheme::Gold);
		return true;
	}
	if (K == EKeys::Gamepad_FaceButton_Right || K == EKeys::Escape || K == EKeys::BackSpace || K == EKeys::Gamepad_Special_Right)
	{
		ATagAudio::Ui(TEXT("UiBack"));
		const std::function<void()> B = S.Back;
		S.Back = nullptr;
		CallRoot.Reset();
		if (B) B();
		else ShowMainMenu();
		return true;
	}
	return false;
}

// ------------------------------------------------------------------ -TagControlsTest
void SGridironOverlay::StartControlsTest()
{
	constexpr int32 Pad = 8401, Kbd = 0;
	struct FState
	{
		TArray<TFunction<bool()>> Steps;
		int32 I = 0, Wait = 0, Pass = 0, Fail = 0, N = 0;
		double StepT = 0;
	};
	const TSharedRef<FState> S = MakeShared<FState>();
	const TWeakPtr<SGridironOverlay> W = StaticCastSharedRef<SGridironOverlay>(AsShared());
	auto Send = [](int32 Dev, const FKey& K, bool bDown)
	{
		if (!FSlateApplication::IsInitialized()) return;
		const FKeyEvent E(K, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(Dev), false, 0, 0, TOptional<int32>(0));
		if (bDown) FSlateApplication::Get().ProcessKeyDownEvent(E);
		else FSlateApplication::Get().ProcessKeyUpEvent(E);
	};
	auto Check = [S](bool bOk, const FString& What)
	{
		(bOk ? S->Pass : S->Fail)++;
		UE_LOG(LogTemp, Display, TEXT("TAG controls test %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	auto Do = [S](TFunction<void()> F) { S->Steps.Add([F] { F(); return true; }); };
	auto Frames = [S](int32 N) { S->Steps.Add([S, N] { if (++S->Wait < N) return false; S->Wait = 0; return true; }); };
	auto Tap = [Do, Frames, Send](int32 Dev, const FKey& K) { Do([Send, Dev, K] { Send(Dev, K, true); }); Frames(2); Do([Send, Dev, K] { Send(Dev, K, false); }); Frames(10); };
	auto Shot = [S, Do](const FString& Name)
	{
		Do([S, Name]
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("tag_controls_%02d_%s.png"), S->N++, *Name), true, false);
			UE_LOG(LogTemp, Display, TEXT("TAG controls shot %s"), *Name);
		});
		S->Steps.Add([S] { if (++S->Wait < 12) return false; S->Wait = 0; return true; });
	};
	auto SelSlot = [W](const FKey& K) { if (auto O = W.Pin()) if (O->Ctl) { const TArray<CtlUI::FSlot>& Sl = CtlUI::Slots(O->bSonyPad); for (int32 i = 0; i < Sl.Num(); ++i) if (Sl[i].Key == K) O->Ctl->Sel = i; } };
	// a pad button pressed into the player controller (the game's own reading, as the pad test presses them)
	auto PadKey = [W, Do, Frames](const FKey& Key)
	{
		auto Press = [W, Key](EInputEvent Ev)
		{
			const TSharedPtr<SGridironOverlay> O = W.Pin();
			APlayerController* PC = O && O->Director && O->Director->GetWorld() ? O->Director->GetWorld()->GetFirstPlayerController() : nullptr;
			FViewport* VP = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
			if (PC) PC->InputKey(FInputKeyEventArgs(VP, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Key, Ev, 1.f, false, FPlatformTime::Cycles64()));
		};
		Do([Press] { Press(IE_Pressed); });
		Frames(3);
		Do([Press] { Press(IE_Released); });
		Frames(10);
	};
	using K = EKeys;
	using C = ECtx;

	Frames(60);
	Do([] { TagBind::ResetAll(); TagBind::SetGlyphStyle(1); });
	Do([W] { if (auto O = W.Pin()) { O->SetPadKind(false); O->ShowControls([W] { if (auto O2 = W.Pin()) O2->ShowMainMenu(); }); } });
	Frames(30);
	// no pad in an offscreen run: the keyboard view comes up; SQUARE picks the controller's
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && O->Ctl->bKbd == !O->CtlHasPad(), TEXT("the view opens on the device there is (no pad: the keyboard)")); });
	Do([W] { if (auto O = W.Pin()) if (O->Ctl) { O->Ctl->bKbd = true; O->Ctl->bViewPicked = true; } });  // (from the keyboard view, whatever is plugged in)
	Tap(Pad, K::Gamepad_FaceButton_Left);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && !O->Ctl->bKbd, TEXT("SQUARE: the controller view")); });
	Shot(TEXT("xbox_carrier"));
	for (int32 t = 1; t < TagBind::NumCtx; ++t)
	{
		Tap(Pad, K::Gamepad_RightShoulder);
		Do([W, Check, t] { if (auto O = W.Pin()) Check(O->Ctl && O->Ctl->Ctx == t, FString::Printf(TEXT("R1 steps to tab %d (%s)"), t, *TagBind::Contexts()[t].Name)); });
		if (t == 1 || t == 3 || t == 4 || t == 6) Shot(FString::Printf(TEXT("xbox_%s"), *TagBind::Contexts()[t].Name.Replace(TEXT(" "), TEXT("_")).ToLower()));
	}
	// the PlayStation pad drawn (VIEW / CREATE: the buttons style)
	Tap(Pad, K::Gamepad_Special_Left);
	Do([W, Check] { if (auto O = W.Pin()) Check(TagBind::GlyphStyle() == 2 && O->SonyPad(), TEXT("CREATE: always PlayStation buttons")); });
	Shot(TEXT("ps5_kicking"));
	for (int32 t = 0; t < 2; ++t) Tap(Pad, K::Gamepad_LeftShoulder);
	Shot(TEXT("ps5_preoff"));
	for (int32 t = 0; t < 4; ++t) Tap(Pad, K::Gamepad_LeftShoulder);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && O->Ctl->Ctx == 0, TEXT("L1 back to the ball carrier")); });
	Shot(TEXT("ps5_carrier"));

	// ---- a swap: the stiff arm (CROSS) onto TRIANGLE; the hurdle takes CROSS
	Do([SelSlot] { SelSlot(EKeys::Gamepad_FaceButton_Bottom); });
	Frames(4);
	Do([Send] { Send(Pad, EKeys::Gamepad_FaceButton_Bottom, true); });
	Frames(2);
	Do([Send] { Send(Pad, EKeys::Gamepad_FaceButton_Bottom, false); });
	Frames(10);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && O->Ctl->bCapture, TEXT("CROSS on the stiff arm: waiting for its new button")); });
	Shot(TEXT("capture"));
	Tap(Pad, K::Gamepad_LeftStick_Up);  // (a stick push is not a button: still waiting)
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && O->Ctl->bCapture, TEXT("a push of the stick is not taken for the button")); });
	Tap(Pad, K::Gamepad_FaceButton_Top);
	Do([W, Check]
	{
		const bool bSwap = TagBind::Phys(C::Carrier, K::Gamepad_FaceButton_Bottom) == K::Gamepad_FaceButton_Top && TagBind::Phys(C::Carrier, K::Gamepad_FaceButton_Top) == K::Gamepad_FaceButton_Bottom;
		Check(bSwap, TEXT("TRIANGLE: the stiff arm on TRIANGLE, the hurdle moved to CROSS"));
		Check(TagBind::IsDefault(C::Passing, true) && TagBind::IsDefault(C::Defense, true), TEXT("the other tabs are as they were"));
		if (auto O = W.Pin()) Check(O->Ctl && !O->Ctl->bCapture && CtlUI::Slots(true)[O->Ctl->Sel].Key == K::Gamepad_FaceButton_Top, TEXT("the cursor went with the stiff arm"));
	});
	Shot(TEXT("swapped"));
	// the game's reader and its hints follow the swap (a pad's TRIANGLE read as the stiff arm's CROSS)
	Do([Check]
	{
		FTagInputHub& H = FTagInputHub::Get();
		const int32 D = H.AddFake(true, TEXT("ControlsTest"));
		H.Inject(D, K::Gamepad_FaceButton_Top, true);
		const FTagReader R(D, true);
		{
			const TagBind::FScope Sc(C::Carrier);
			Check(R.IsInputKeyDown(K::Gamepad_FaceButton_Bottom) && !R.IsInputKeyDown(K::Gamepad_FaceButton_Top), TEXT("ball carrier: TRIANGLE pressed reads as the stiff arm's button"));
			const FString Hn = TagBind::TranslateHint(TEXT("R2 sprint    CROSS stiff arm    CIRCLE spin    TRIANGLE hurdle"), true);
			Check(Hn == TEXT("R2 sprint    TRIANGLE stiff arm    CIRCLE spin    CROSS hurdle"), FString::Printf(TEXT("the ball carrier's hint names the new buttons (\"%s\")"), *Hn));
			Check(TagBind::PhysGlyph(EPadGlyph::Triangle) == EPadGlyph::Cross, TEXT("the hurdle's cue glyph is CROSS"));
		}
		{
			const TagBind::FScope Sc(C::Defense);
			Check(R.IsInputKeyDown(K::Gamepad_FaceButton_Top), TEXT("defense: TRIANGLE is still TRIANGLE"));
		}
		Check(R.IsInputKeyDown(K::Gamepad_FaceButton_Top), TEXT("the menus (no layout): TRIANGLE is TRIANGLE"));
		H.Inject(D, K::Gamepad_FaceButton_Top, false);
	});

	// ---- the keyboard view: the stiff arm's G onto Z; a kept key refused; ESC cancels
	Tap(Kbd, K::K);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && O->Ctl->bKbd, TEXT("K: the keyboard view")); });
	Do([W] { if (auto O = W.Pin()) if (O->Ctl) { const TArray<CtlUI::FKRow> R = CtlUI::KbdRows(C::Carrier); for (int32 i = 0; i < R.Num(); ++i) if (R[i].A && R[i].A->Kbd == K::G) O->Ctl->Sel = i; } });
	Tap(Kbd, K::Enter);
	Tap(Kbd, K::W);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && O->Ctl->bCapture && TagBind::Phys(C::Carrier, K::G) == K::G, TEXT("W (moving) is refused; still waiting")); });
	Tap(Kbd, K::Z);
	Do([Check] { Check(TagBind::Phys(C::Carrier, K::G) == K::Z && TagBind::Phys(C::Carrier, K::Z) == K::G, TEXT("Z: the stiff arm on Z")); });
	Shot(TEXT("keyboard_carrier"));
	Do([Check]
	{
		const TagBind::FScope Sc(C::Carrier);
		const FString Hn = TagBind::TranslateHint(TEXT("F truck   G stiff arm   Q / E juke"), false);
		Check(Hn == TEXT("F truck   Z stiff arm   Q / E juke"), FString::Printf(TEXT("the keyboard hint names Z (\"%s\")"), *Hn));
	});
	Tap(Kbd, K::Enter);
	Tap(Kbd, K::Escape);
	Do([W, Check] { if (auto O = W.Pin()) Check(O->Ctl && !O->Ctl->bCapture && TagBind::Phys(C::Carrier, K::G) == K::Z, TEXT("ESC: nothing changed")); });
	// ---- the resets
	Tap(Kbd, K::T);
	Do([Check] { Check(TagBind::IsDefault(C::Carrier, false) && !TagBind::IsDefault(C::Carrier, true), TEXT("T: the keyboard's tab reset, the pad's kept")); });
	Tap(Kbd, K::Delete);
	Do([Check] { Check(TagBind::IsDefault(C::Carrier, true), TEXT("DEL: everything back to the defaults")); });
	// ---- a game: the audible (SQUARE) moved to TRIANGLE before the snap, called on TRIANGLE
	Do([W]
	{
		TagBind::Assign(C::PreOff, K::Gamepad_FaceButton_Left, K::Gamepad_FaceButton_Top);
		TagBind::Assign(C::PreDef, K::Gamepad_FaceButton_Left, K::Gamepad_FaceButton_Top);
		TagBind::SetGlyphStyle(0);
		if (auto O = W.Pin()) O->SetPadKind(TagBind::Sony(O->Director && O->Director->bPlayStationPad));
	});
	Tap(Pad, K::Gamepad_FaceButton_Right);  // (back to the main menu)
	Do([W, Check] { if (auto O = W.Pin()) Check(!O->Ctl || !O->CallRoot.IsValid(), TEXT("CIRCLE: back out of the screen")); });
	Do([W] { if (auto O = W.Pin()) if (O->Director) O->Director->StartMatch(O->SelHome, O->SelAway, true, true, 1, false, 300); });
	Do([S] { S->StepT = FPlatformTime::Seconds(); });
	S->Steps.Add([W, S, Check, Send]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (O && O->IsPlayCall()) { S->Wait = 0; return true; }
		if (++S->Wait % 45 == 0) { Send(Pad + 1, EKeys::Gamepad_FaceButton_Bottom, true); Send(Pad + 1, EKeys::Gamepad_FaceButton_Bottom, false); }
		if (FPlatformTime::Seconds() - S->StepT > 150) { Check(false, TEXT("no play call within 150 s")); S->I = S->Steps.Num() - 2; return false; }
		return false;
	});
	Frames(30);
	Tap(Pad + 1, K::Gamepad_FaceButton_Bottom);
	Do([S] { S->StepT = FPlatformTime::Seconds(); });
	S->Steps.Add([W, S] { const TSharedPtr<SGridironOverlay> O = W.Pin(); return (O && !O->HasModal() && O->Director && O->Director->State == EMatchState::Presnap && O->PreRows.Num() > 0 && FPlatformTime::Seconds() - S->StepT > 2.5) || FPlatformTime::Seconds() - S->StepT > 20.0; });
	Do([W, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O) return;
		bool bTri = false;
		for (const FPreRow& R : O->PreRows) if (R.Label.StartsWith(TEXT("Audible")) && R.Glyph == EPadGlyph::Triangle) bTri = true;
		Check(bTri, TEXT("the pre-snap panel shows the audible on TRIANGLE"));
	});
	Shot(TEXT("presnap_base"));
	PadKey(K::Gamepad_FaceButton_Top);
	// (the menu it opens, as soon as it is up: a CPU offense snaps on its own clock)
	Do([S] { S->StepT = FPlatformTime::Seconds(); });
	S->Steps.Add([W, S] { const TSharedPtr<SGridironOverlay> O = W.Pin(); return (O && O->Director && O->Director->PreMode == AMatchDirector::EPre::Audible) || FPlatformTime::Seconds() - S->StepT > 2.0; });
	Do([W, Check]
	{
		const TSharedPtr<SGridironOverlay> O = W.Pin();
		if (!O || !O->Director) return;
		Check(O->Director->PreMode == AMatchDirector::EPre::Audible, TEXT("before the snap: TRIANGLE opens the audibles it was moved to"));
		UE_LOG(LogTemp, Display, TEXT("TAG controls test: pre-snap panel %s, %d rows"), *O->PreTitle, O->PreRows.Num());
		Check(O->PreRows.Num() > 0, TEXT("the audible panel is up"));
	});
	Shot(TEXT("presnap_audible"));
	Do([S]
	{
		TagBind::ResetAll();
		UE_LOG(LogTemp, Display, TEXT("TAG controls test done: %d pass, %d fail"), S->Pass, S->Fail);
		FPlatformMisc::RequestExit(false);
	});

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool
	{
		if (!S->Steps.IsValidIndex(S->I)) return false;
		if (S->Steps[S->I]()) ++S->I;
		return true;
	}));
}
