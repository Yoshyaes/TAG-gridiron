#include "UI/PlayDiagram.h"

#include "Sim/Playbook.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace
{
	// the card shows the full width of the field and this much of its length (yards behind the ball .. ahead of it)
	constexpr double BackYd = 7.0;
	constexpr double AheadYd = 24.5;
	constexpr double SpanYd = BackYd + AheadYd;

	struct FPainter
	{
		FSlateWindowElementList& Out;
		const FGeometry& G;
		int32 Layer;
		FSlateResourceHandle Fill;

		FVector2f Box = FVector2f::ZeroVector; // the card: lines are cut at its edges (a route running off the top)
		void Lines(const TArray<FVector2f>& P, const FLinearColor& C, float Th, int32 L = 0) const
		{
			if (P.Num() < 2) return;
			if (Box.X <= 0.f)
			{
				FSlateDrawElement::MakeLines(Out, Layer + L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C, true, Th);
				return;
			}
			// cut against the card (Liang-Barsky per segment), drawing each stretch that stays on it
			TArray<FVector2f> Run;
			auto Flush = [&]()
			{
				if (Run.Num() >= 2) FSlateDrawElement::MakeLines(Out, Layer + L, G.ToPaintGeometry(), Run, ESlateDrawEffect::None, C, true, Th);
				Run.Reset();
			};
			for (int32 i = 0; i + 1 < P.Num(); ++i)
			{
				const FVector2f A = P[i], D = P[i + 1] - P[i];
				float T0 = 0.f, T1 = 1.f;
				bool bIn = true;
				auto Edge = [&](float Pp, float Q)
				{
					if (FMath::IsNearlyZero(Pp))
					{
						if (Q < 0.f) bIn = false;
						return;
					}
					const float R = Q / Pp;
					if (Pp < 0.f) T0 = FMath::Max(T0, R);
					else T1 = FMath::Min(T1, R);
				};
				Edge(-D.X, A.X);
				Edge(D.X, Box.X - A.X);
				Edge(-D.Y, A.Y);
				Edge(D.Y, Box.Y - A.Y);
				if (!bIn || T0 > T1)
				{
					Flush();
					continue;
				}
				const FVector2f S0 = A + D * T0, S1 = A + D * T1;
				if (Run.Num() == 0 || !Run.Last().Equals(S0, 0.01f))
				{
					Flush();
					Run.Add(S0);
				}
				Run.Add(S1);
				if (T1 < 1.f) Flush();
			}
			Flush();
		}
		/** A filled convex polygon (a fan from its first point). */
		void Poly(const TArray<FVector2f>& P, const FLinearColor& C, int32 L = 0) const
		{
			if (P.Num() < 3 || !Fill.IsValid()) return;
			const FSlateRenderTransform& RT = G.GetAccumulatedRenderTransform();
			const FColor Col = C.ToFColor(true);
			TArray<FSlateVertex> V;
			TArray<SlateIndex> I;
			V.Reserve(P.Num());
			for (const FVector2f& Q : P) V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, Q, FVector2f(0.5f, 0.5f), Col));
			for (int32 k = 1; k + 1 < P.Num(); ++k)
			{
				I.Add(0);
				I.Add(k);
				I.Add(k + 1);
			}
			FSlateDrawElement::MakeCustomVerts(Out, Layer + L, Fill, V, I, nullptr, 0, 0);
		}
		TArray<FVector2f> Ellipse(const FVector2f& C, float RX, float RY, int32 N = 28) const
		{
			TArray<FVector2f> P;
			P.Reserve(N + 1);
			for (int32 i = 0; i <= N; ++i)
			{
				const float A = 2.f * PI * i / N;
				P.Add(C + FVector2f(RX * FMath::Cos(A), RY * FMath::Sin(A)));
			}
			return P;
		}
		/** A rectangle HX x HY half extents about C with its corners rounded by Rad (convex: Poly fills it). */
		TArray<FVector2f> RoundRect(const FVector2f& C, float HX, float HY, float Rad) const
		{
			Rad = FMath::Clamp(Rad, 0.f, FMath::Min(HX, HY));
			TArray<FVector2f> P;
			const FVector2f Corner[4] = {FVector2f(HX - Rad, HY - Rad), FVector2f(-(HX - Rad), HY - Rad), FVector2f(-(HX - Rad), -(HY - Rad)), FVector2f(HX - Rad, -(HY - Rad))};
			for (int32 k = 0; k < 4; ++k)
				for (int32 i = 0; i <= 4; ++i)
				{
					const float A = 0.5f * PI * (k + i / 4.f);
					P.Add(C + Corner[k] + FVector2f(Rad * FMath::Cos(A), Rad * FMath::Sin(A)));
				}
			return P;
		}
	};

	/** How a picture sits on a card: its scale (pixels a yard, the same both ways), the yard the card's centre shows, and
	 *  the sizes of the men and the lines at that scale. */
	struct FArtFit
	{
		float S = 10.f;             // the scale the men and lines are sized by (the smaller way)
		float SX = 10.f, SY = 10.f; // pixels a yard across the field / along it (within MaxStretch of each other)
		double MidA = 0, MidL = 0;  // yards across / along the field at the card's centre
		float R = 6.f;              // a skill man's marker radius (px)
		float LineHalf = 4.f;       // an offensive lineman's half width (px)
		float Th = 3.f;             // the base line thickness (px)
	};

	constexpr float MaxStretch = 1.5f;
	float MarkRadius(float S) { return FMath::Clamp(0.84f * S, 3.5f, 14.f); }
	float LineThick(float S) { return FMath::Clamp(0.33f * S, 1.6f, 5.5f); }
	float ArrowLen(float W) { return W * 2.4f + 3.f; }

	/** Everything the card draws, in yards: the men it shows (an offensive card: the offense only), every line's points
	 *  (the routes in full to their cap, the run path, the blocks, the drop, the rushes) and every zone's reach. */
	void ArtExtent(const FArtPicture& Pic, bool bOffense, double& MinL, double& MaxL, double& MinA, double& MaxA)
	{
		MinL = MaxL = Pic.Los;
		MinA = MaxA = Pic.BallY;
		auto Grow = [&](double L, double A)
		{
			MinL = FMath::Min(MinL, L);
			MaxL = FMath::Max(MaxL, L);
			MinA = FMath::Min(MinA, A);
			MaxA = FMath::Max(MaxA, A);
		};
		for (const FArtMark& M : Pic.Marks)
			if (!bOffense || M.bOffense) Grow(M.Pos.x, M.Pos.y);
		for (const FArtStroke& St : Pic.Strokes)
		{
			if (St.Kind == FArtStroke::Zone)
			{
				if (St.Pts.IsEmpty()) continue;
				// (as the card draws it: a little inside the zone's reach)
				const tag::V2& C = St.Pts[0];
				Grow(C.x - St.RX * 0.75, C.y - St.RY * 0.8);
				Grow(C.x + St.RX * 0.75, C.y + St.RY * 0.8);
			}
			else
				for (const tag::V2& V : St.Pts) Grow(V.x, V.y);
		}
	}

	/** The fit: the art's whole extent inside the card with a margin for the markers and arrowheads, yards the same size
	 *  both ways. A compact play (the goal line set, a sneak) is zoomed in at most ~1.45x a typical spread play's scale
	 *  (44 x 31 yd on the card); a wide or deep one is shrunk as far as it needs - nothing is cut. */
	FArtFit FitArt(const FArtPicture& Pic, bool bOffense, const FVector2f& Size)
	{
		FArtFit F;
		double MinL, MaxL, MinA, MaxA;
		ArtExtent(Pic, bOffense, MinL, MaxL, MinA, MaxA);
		// at least this much of the field round the ball (a play of five men in a phone box still shows the line)
		constexpr double MinSpanA = 18.0, MinSpanL = 11.0;
		const double SpanA = FMath::Max(MaxA - MinA, MinSpanA);
		const double SpanL = FMath::Max(MaxL - MinL, MinSpanL);
		F.MidA = (MinA + MaxA) * 0.5;
		F.MidL = (MinL + MaxL) * 0.5;
		const float SRef = FMath::Min(Size.X / 44.f, Size.Y / 31.f);
		// (a route cut at the depth cap has a second head out past its end)
		const bool bRunsOn = Pic.Strokes.ContainsByPredicate([](const FArtStroke& St) { return St.bRunsOn; });
		float S = FMath::Min(Size.X / static_cast<float>(SpanA + 4.0), Size.Y / static_cast<float>(SpanL + 4.0));
		for (int32 It = 0; It < 3; ++It)
		{
			const float R = MarkRadius(S);
			const float W = LineThick(S) * 1.3f;
			const float Margin = FMath::Max(R * 1.25f + 2.f, ArrowLen(W) * (bRunsOn ? 1.3f : 0.75f) + 2.f) + FMath::Max(4.f, 0.02f * FMath::Min(Size.X, Size.Y));
			const float AvailX = FMath::Max(8.f, Size.X - 2.f * Margin), AvailY = FMath::Max(8.f, Size.Y - 2.f * Margin);
			S = FMath::Min(AvailX / static_cast<float>(SpanA), AvailY / static_cast<float>(SpanL));
			S = FMath::Min(S, SRef * 1.45f);
			// (the other way the play may stretch up to MaxStretch: a run, forty yards wide and fifteen deep, fills more
			// of its card than a strictly square yard allows - "roughly" the same size both ways)
			F.SX = FMath::Min(AvailX / static_cast<float>(SpanA), FMath::Min(S * MaxStretch, SRef * 1.45f));
			F.SY = FMath::Min(AvailY / static_cast<float>(SpanL), FMath::Min(S * MaxStretch, SRef * 1.45f));
		}
		F.S = FMath::Max(S, 0.5f);
		F.SX = FMath::Max(F.SX, F.S);
		F.SY = FMath::Max(F.SY, F.S);
		F.R = MarkRadius(F.S);
		F.Th = LineThick(F.S);
		// the five linemen side by side: each as wide as most of the gap to the next (they read as one line, not a smear)
		double Gap = 1.4;
		{
			TArray<double> Ys;
			for (const FArtMark& M : Pic.Marks)
				if (M.bLine) Ys.Add(M.Pos.y);
			Ys.Sort();
			for (int32 i = 0; i + 1 < Ys.Num(); ++i) Gap = FMath::Min(Gap, FMath::Max(0.6, Ys[i + 1] - Ys[i]));
		}
		F.LineHalf = FMath::Clamp(static_cast<float>(Gap * F.SX * 0.44), 2.5f, F.R * 0.95f);
		return F;
	}
}

void SPlayDiagram::Construct(const FArguments& InArgs)
{
	Pic = InArgs._Picture;
	bOffense = InArgs._Offense;
	FirstDown = InArgs._FirstDown;
	SetClipping(EWidgetClipping::ClipToBounds); // (a deep zone or route runs off the top of the card)
}

int32 SPlayDiagram::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 8 || Size.Y < 8) return LayerId;
	FPainter Pt{OutDrawElements, AllottedGeometry, LayerId};
	Pt.Box = Size;
	if (FSlateApplication::IsInitialized())
		if (const FSlateBrush* B = FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			Pt.Fill = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*B);

	// yards -> card pixels: the play fitted to its card (FitArt) - every man, every route in full with its breaks, the
	// run path and the backfield, the blocks - with the yards the same size both ways and the line of scrimmage where the
	// play puts it (a quick game sits low on a short board, a shot's deep routes reach the top). The cards used to share
	// one fixed window (42 x 28 yd from 6 behind the line): the deep routes ran off the top, the tailback's path off the
	// bottom, and the line was a cluster of dots in a thin band of an otherwise empty board.
	const FArtFit Fit = FitArt(Pic, bOffense, Size);
	const float S = Fit.S;
	const double MidA = Fit.MidA, MidL = Fit.MidL;
	const FVector2f Ctr(Size.X * 0.5f, Size.Y * 0.5f);
	auto Map = [&](const tag::V2& V) -> FVector2f
	{
		// from behind the offense looking up the field (its right is -y), for both sides' cards
		return FVector2f(Ctr.X - static_cast<float>((V.y - MidA) * Fit.SX), Ctr.Y - static_cast<float>((V.x - MidL) * Fit.SY));
	};
	const float Left = 0.f, Right = Size.X, Top = 0.f, Bottom = Size.Y;

	// the board: a dark field (as the play art is drawn on the broadcast's chalkboard), its yard lines and hash marks
	// faint, the sidelines where the card reaches them, the line of scrimmage and the line to gain bright
	Pt.Poly({FVector2f(Left, Top), FVector2f(Right, Top), FVector2f(Right, Bottom), FVector2f(Left, Bottom)}, FLinearColor(0.018f, 0.045f, 0.03f, 1.f));
	{
		// out of bounds: darker past the sideline, the sideline itself white
		const float SideL = Map(tag::V2{MidL, tag::FIELD_W}).X, SideR = Map(tag::V2{MidL, 0.0}).X;
		const FLinearColor Oob(0.008f, 0.014f, 0.012f, 1.f);
		if (SideL > Left) Pt.Poly({FVector2f(Left, Top), FVector2f(SideL, Top), FVector2f(SideL, Bottom), FVector2f(Left, Bottom)}, Oob, 0);
		if (SideR < Right) Pt.Poly({FVector2f(SideR, Top), FVector2f(Right, Top), FVector2f(Right, Bottom), FVector2f(SideR, Bottom)}, Oob, 0);
		const FLinearColor SideC(1.f, 1.f, 1.f, 0.22f);
		if (SideL > Left && SideL < Right) Pt.Lines({FVector2f(SideL, Top), FVector2f(SideL, Bottom)}, SideC, FMath::Max(1.5f, S * 0.15f), 1);
		if (SideR > Left && SideR < Right) Pt.Lines({FVector2f(SideR, Top), FVector2f(SideR, Bottom)}, SideC, FMath::Max(1.5f, S * 0.15f), 1);
	}
	const FLinearColor YardLine(1.f, 1.f, 1.f, 0.075f), FiveLine(1.f, 1.f, 1.f, 0.13f);
	const float HashHalf = FMath::Clamp(S * 0.35f, 1.5f, 5.f);
	for (int32 Yd = 0; Yd <= 100; ++Yd)
	{
		const float Py = Map(tag::V2{static_cast<double>(Yd), MidA}).Y;
		if (Py < Top || Py > Bottom) continue;
		if (Yd % 5 == 0) Pt.Lines({FVector2f(Left, Py), FVector2f(Right, Py)}, FiveLine, FMath::Max(1.f, S * 0.09f));
		for (const double HashY : {tag::FIELD_W / 2 - 3.08, tag::FIELD_W / 2 + 3.08})
		{
			const float Hx = Map(tag::V2{static_cast<double>(Yd), HashY}).X;
			if (Hx > Left && Hx < Right) Pt.Lines({FVector2f(Hx - HashHalf, Py), FVector2f(Hx + HashHalf, Py)}, YardLine, 1.f);
		}
	}
	{
		const float LosTh = FMath::Max(2.f, Fit.Th * 0.7f);
		const float Py = Map(tag::V2{Pic.Los, MidA}).Y;
		Pt.Lines({FVector2f(Left, Py), FVector2f(Right, Py)}, FLinearColor(0.45f, 0.75f, 1.f, 0.85f), LosTh, 1);
		if (FirstDown >= 0)
		{
			const float Fy = Map(tag::V2{FirstDown, MidA}).Y;
			if (Fy > Top && Fy < Bottom) Pt.Lines({FVector2f(Left, Fy), FVector2f(Right, Fy)}, FLinearColor(1.f, 0.85f, 0.1f, 0.85f), LosTh, 1);
		}
	}

	// the art: zones under the lines; every line on a dark edge so it reads on the board and over a zone
	const float Th = Fit.Th;
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.75f);
	for (const FArtStroke& St : Pic.Strokes)
	{
		if (St.Kind != FArtStroke::Zone || St.Pts.IsEmpty()) continue;
		const FVector2f C = Map(St.Pts[0]);
		// (drawn a little inside the zone's reach, so neighbouring zones sit side by side on the card instead of piling into
		// one another - the field's art keeps the full size)
		const TArray<FVector2f> E = Pt.Ellipse(C, static_cast<float>(St.RY * Fit.SX * 0.8), static_cast<float>(St.RX * Fit.SY * 0.75), 40);
		FLinearColor FillC = St.Color;
		FillC.A = 0.17f;
		Pt.Poly(E, FillC, 2);
		FLinearColor Rim = St.Color;
		Rim.A = 0.95f;
		Pt.Lines(E, Rim, FMath::Max(1.2f, Th * 0.45f), 3);
	}
	// the lines in two passes: the blocks and the drop first, then the routes, rushes and the run path over them (the
	// shadows share one layer and the colours the next, so a crossing route never loses its colour under another's edge)
	const float Edge = FMath::Max(1.4f, Th * 0.75f);
	for (int32 Pass = 0; Pass < 2; ++Pass)
		for (const FArtStroke& St : Pic.Strokes)
		{
			if (St.Kind != FArtStroke::Line || St.Pts.Num() < 2) continue;
			const bool bUnder = St.Role == FArtStroke::BlockMark || St.Role == FArtStroke::Drop || St.Role == FArtStroke::Motion || St.Role == FArtStroke::Other;
			if (bUnder != (Pass == 0)) continue;
			TArray<FVector2f> P;
			P.Reserve(St.Pts.Num());
			for (const tag::V2& V : St.Pts) P.Add(Map(V));
			const float W = Th * FMath::Clamp(St.Width / 16.f, 0.75f, 1.3f);
			if (St.bGlow)
			{
				FLinearColor Gl = St.Color;
				Gl.A *= 0.3f;
				Pt.Lines(P, Gl, W + Th * 1.7f, 3);
			}
			Pt.Lines(P, Shadow, W + Edge, 4);
			Pt.Lines(P, St.Color, W, 5);
			// the end: an arrowhead, a block bar, a dot
			const FVector2f End = P.Last();
			FVector2f Dir = End - P[P.Num() - 2];
			if (Dir.IsNearlyZero()) Dir = FVector2f(0, -1.f);
			Dir.Normalize();
			const FVector2f Side(-Dir.Y, Dir.X);
			switch (St.End)
			{
			case EArtEnd::Arrow:
			{
				const float L = ArrowLen(W);
				const TArray<FVector2f> Head{End + Dir * L * 0.55f, End - Dir * L * 0.55f + Side * L * 0.6f, End - Dir * L * 0.55f - Side * L * 0.6f};
				const float E1 = Edge * 0.6f;
				Pt.Poly({Head[0] + Dir * E1, Head[1] - Dir * E1 * 0.7f + Side * E1, Head[2] - Dir * E1 * 0.7f - Side * E1}, Shadow, 4);
				Pt.Poly(Head, St.Color, 5);
				if (St.bRunsOn)
				{
					// cut at the card's depth cap: a second, fainter head ahead of the first says it keeps going
					FLinearColor C2 = St.Color;
					C2.A *= 0.55f;
					const FVector2f O = Dir * L * 0.7f;
					Pt.Poly({Head[0] + O, Head[1] + O, End + O - Dir * L * 0.1f, Head[2] + O}, C2, 5);
				}
				break;
			}
			case EArtEnd::Block:
			{
				const float L = W * 1.6f + 3.f;
				Pt.Lines({End + Side * (L + Edge * 0.5f), End - Side * (L + Edge * 0.5f)}, Shadow, W + Edge, 4);
				Pt.Lines({End + Side * L, End - Side * L}, St.Color, W, 5);
				break;
			}
			case EArtEnd::Dot:
				Pt.Poly(Pt.Ellipse(End, W + Edge + 1.f, W + Edge + 1.f, 14), Shadow, 4);
				Pt.Poly(Pt.Ellipse(End, W + 1.f, W + 1.f, 14), St.Color, 5);
				break;
			default:
				break;
			}
		}

	// the men: the side being called bold - the offense's line as one row of linemen (rounded blocks a little narrower
	// than their splits, the centre over the ball), the quarterback and the skill men as circles (the receivers in their
	// buttons' colours); the defense as triangles pointing at the ball. The side the card does not call is drawn faintly
	// (an offensive card shows no defenders at all, as Madden's does)
	const float R = Fit.R;
	const float MarkEdge = FMath::Max(1.2f, R * 0.18f);
	for (const FArtMark& M : Pic.Marks)
	{
		const bool bMine = M.bOffense == bOffense;
		if (!bMine && bOffense) continue;
		const FVector2f C = Map(M.Pos);
		if (C.X < -R || C.X > Size.X + R || C.Y < -R || C.Y > Size.Y + R) continue;
		const bool bLineman = M.bLine;
		FLinearColor Col = M.Color;
		if (!bMine) Col.A = 0.42f;
		const FLinearColor EdgeC = bMine ? FLinearColor(0.f, 0.f, 0.f, 0.85f) : FLinearColor(0.f, 0.f, 0.f, 0.f);
		if (M.bOffense)
		{
			if (bLineman)
			{
				// a lineman: a block as wide as most of his split, a little shallower than wide; the centre's darker
				// notch is the ball
				const float HX = bMine ? Fit.LineHalf : Fit.LineHalf * 0.8f;
				const float HY = HX * 0.82f;
				Pt.Poly(Pt.RoundRect(C, HX + MarkEdge, HY + MarkEdge, HX * 0.35f + MarkEdge), EdgeC, 6);
				Pt.Poly(Pt.RoundRect(C, HX, HY, HX * 0.35f), Col, 7);
				if (M.bCenter && bMine)
					Pt.Poly(Pt.Ellipse(C + FVector2f(0.f, -HY * 0.15f), HX * 0.42f, HY * 0.3f, 12), FLinearColor(0.42f, 0.22f, 0.1f, 1.f), 8);
			}
			else
			{
				const float Rr = bMine ? (M.bQB ? R * 1.02f : R) : R * 0.72f;
				Pt.Poly(Pt.Ellipse(C, Rr + MarkEdge, Rr + MarkEdge, 22), EdgeC, 6);
				Pt.Poly(Pt.Ellipse(C, Rr, Rr, 22), Col, 7);
				// the quarterback: a white disc with a dark core, so he reads apart from the backs beside him
				if (M.bQB && bMine) Pt.Poly(Pt.Ellipse(C, Rr * 0.42f, Rr * 0.42f, 14), FLinearColor(0.05f, 0.07f, 0.1f, 0.9f), 8);
			}
		}
		else
		{
			// the point faces the offense, down the card
			const float Rr = bMine ? R : R * 0.6f;
			const TArray<FVector2f> T{C + FVector2f(0, Rr * 1.25f), C + FVector2f(Rr * 1.2f, -Rr * 0.95f), C + FVector2f(-Rr * 1.2f, -Rr * 0.95f)};
			const float E2 = MarkEdge * 1.4f;
			const TArray<FVector2f> TE{C + FVector2f(0, Rr * 1.25f + E2), C + FVector2f(Rr * 1.2f + E2, -Rr * 0.95f - E2 * 0.6f), C + FVector2f(-Rr * 1.2f - E2, -Rr * 0.95f - E2 * 0.6f)};
			Pt.Poly(TE, EdgeC, 6);
			Pt.Poly(T, Col, 7);
		}
		if (M.bMe) Pt.Lines(Pt.Ellipse(C, R * 2.f, R * 2.f, 20), FLinearColor(1.f, 0.83f, 0.f), FMath::Max(1.5f, Th * 0.6f), 8);
	}
	return LayerId + 10;
}
