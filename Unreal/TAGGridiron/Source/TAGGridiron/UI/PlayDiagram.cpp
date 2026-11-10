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

		void Lines(const TArray<FVector2f>& P, const FLinearColor& C, float Th, int32 L = 0) const
		{
			if (P.Num() < 2) return;
			FSlateDrawElement::MakeLines(Out, Layer + L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C, true, Th);
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
	};
}

void SPlayDiagram::Construct(const FArguments& InArgs)
{
	Pic = InArgs._Picture;
	bOffense = InArgs._Offense;
	FirstDown = InArgs._FirstDown;
}

int32 SPlayDiagram::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 8 || Size.Y < 8) return LayerId;
	FPainter Pt{OutDrawElements, AllottedGeometry, LayerId};
	if (FSlateApplication::IsInitialized())
		if (const FSlateBrush* B = FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			Pt.Fill = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*B);

	// yards -> card pixels: the field's width across the card, the ball a third of the way up from the bottom
	const float S = FMath::Min(Size.X / static_cast<float>(tag::FIELD_W), Size.Y / static_cast<float>(SpanYd));
	const FVector2f Ctr(Size.X * 0.5f, Size.Y * 0.5f);
	const double MidX = Pic.Los + (AheadYd - BackYd) * 0.5;
	auto Map = [&](const tag::V2& V) -> FVector2f
	{
		// behind the offense looking up the field its right is -y; behind the defense, +y
		const float Px = Ctr.X + static_cast<float>((V.y - Pic.BallY) * S) * (bOffense ? -1.f : 1.f);
		const float Py = Ctr.Y + static_cast<float>((V.x - MidX) * S) * (bOffense ? -1.f : 1.f);
		return FVector2f(Px, Py);
	};
	const float Left = Ctr.X - static_cast<float>(tag::FIELD_W * 0.5 * S), Right = Ctr.X + static_cast<float>(tag::FIELD_W * 0.5 * S);
	const float Top = Ctr.Y - static_cast<float>(SpanYd * 0.5 * S), Bottom = Ctr.Y + static_cast<float>(SpanYd * 0.5 * S);

	// the turf, its yard lines, the ball and the first-down line
	Pt.Poly({FVector2f(Left, Top), FVector2f(Right, Top), FVector2f(Right, Bottom), FVector2f(Left, Bottom)}, FLinearColor(0.05f, 0.14f, 0.07f, 1.f));
	const FLinearColor YardLine(1.f, 1.f, 1.f, 0.10f);
	for (int32 Y = 0; Y <= 100; Y += 5)
	{
		const double X = static_cast<double>(Y);
		if (X < Pic.Los - BackYd || X > Pic.Los + AheadYd) continue;
		const float Py = Map(tag::V2{X, Pic.BallY}).Y;
		Pt.Lines({FVector2f(Left, Py), FVector2f(Right, Py)}, YardLine, 1.f);
	}
	{
		const float Py = Map(tag::V2{Pic.Los, Pic.BallY}).Y;
		Pt.Lines({FVector2f(Left, Py), FVector2f(Right, Py)}, FLinearColor(0.35f, 0.65f, 1.f, 0.9f), 1.5f, 1);
		if (FirstDown >= 0 && FirstDown > Pic.Los - BackYd && FirstDown < Pic.Los + AheadYd)
		{
			const float Fy = Map(tag::V2{FirstDown, Pic.BallY}).Y;
			Pt.Lines({FVector2f(Left, Fy), FVector2f(Right, Fy)}, FLinearColor(1.f, 0.85f, 0.1f, 0.8f), 1.5f, 1);
		}
	}

	// the art: zones under the lines, glows under their strokes
	auto Thick = [&](float WidthCm) { return FMath::Clamp(WidthCm / 11.f, 1.6f, 4.f); };
	for (const FArtStroke& St : Pic.Strokes)
	{
		if (St.Kind != FArtStroke::Zone || St.Pts.IsEmpty()) continue;
		const FVector2f C = Map(St.Pts[0]);
		const TArray<FVector2f> E = Pt.Ellipse(C, static_cast<float>(St.RY * S), static_cast<float>(St.RX * S));
		FLinearColor FillC = St.Color;
		FillC.A = 0.22f;
		Pt.Poly(E, FillC, 2);
		FLinearColor Rim = St.Color;
		Rim.A = 0.8f;
		Pt.Lines(E, Rim, 1.2f, 3);
	}
	for (const FArtStroke& St : Pic.Strokes)
	{
		if (St.Kind != FArtStroke::Line || St.Pts.Num() < 2) continue;
		TArray<FVector2f> P;
		P.Reserve(St.Pts.Num());
		for (const tag::V2& V : St.Pts) P.Add(Map(V));
		const float Th = Thick(St.Width);
		if (St.bGlow)
		{
			FLinearColor Gl = St.Color;
			Gl.A *= 0.35f;
			Pt.Lines(P, Gl, Th + 3.f, 4);
		}
		Pt.Lines(P, St.Color, Th, 5);
		// the end: an arrowhead, a block bar, a dot
		const FVector2f End = P.Last();
		FVector2f Dir = End - P[P.Num() - 2];
		if (Dir.IsNearlyZero()) Dir = FVector2f(0, bOffense ? -1.f : 1.f);
		Dir.Normalize();
		const FVector2f Side(-Dir.Y, Dir.X);
		switch (St.End)
		{
		case EArtEnd::Arrow:
		{
			const float L = Th * 1.6f + 3.f;
			Pt.Poly({End + Dir * L * 0.6f, End - Dir * L * 0.6f + Side * L * 0.55f, End - Dir * L * 0.6f - Side * L * 0.55f}, St.Color, 5);
			break;
		}
		case EArtEnd::Block:
		{
			const float L = Th * 1.2f + 3.f;
			Pt.Lines({End + Side * L, End - Side * L}, St.Color, Th, 5);
			break;
		}
		case EArtEnd::Dot:
			Pt.Poly(Pt.Ellipse(End, Th + 1.2f, Th + 1.2f, 12), St.Color, 5);
			break;
		default:
			break;
		}
	}

	// the men: offense as circles (the centre a square), defense as triangles pointing at the ball
	const float R = FMath::Max(3.f, 0.74f * S);
	for (const FArtMark& M : Pic.Marks)
	{
		const FVector2f C = Map(M.Pos);
		const FLinearColor Edge(0.02f, 0.03f, 0.04f, 0.9f);
		if (M.bOffense)
		{
			if (M.bCenter)
			{
				const TArray<FVector2f> Q{C + FVector2f(-R, -R), C + FVector2f(R, -R), C + FVector2f(R, R), C + FVector2f(-R, R)};
				Pt.Poly(Q, M.Color, 6);
				Pt.Lines({Q[0], Q[1], Q[2], Q[3], Q[0]}, Edge, 1.f, 7);
			}
			else
			{
				const float Rr = M.bQB ? R * 1.15f : R;
				Pt.Poly(Pt.Ellipse(C, Rr, Rr, 16), M.Color, 6);
				Pt.Lines(Pt.Ellipse(C, Rr, Rr, 16), Edge, 1.f, 7);
			}
		}
		else
		{
			// the point faces the offense: up the card on a defensive card, down it on an offensive one
			const float Up = bOffense ? 1.f : -1.f;
			const TArray<FVector2f> T{C + FVector2f(0, Up * R * 1.2f), C + FVector2f(R * 1.15f, -Up * R * 0.9f), C + FVector2f(-R * 1.15f, -Up * R * 0.9f)};
			Pt.Poly(T, M.Color, 6);
			Pt.Lines({T[0], T[1], T[2], T[0]}, Edge, 1.f, 7);
		}
		if (M.bMe) Pt.Lines(Pt.Ellipse(C, R * 2.f, R * 2.f, 16), FLinearColor(1.f, 0.83f, 0.f), 1.5f, 8);
	}
	return LayerId + 9;
}
