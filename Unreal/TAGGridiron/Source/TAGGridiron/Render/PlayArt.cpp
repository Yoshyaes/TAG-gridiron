#include "Render/PlayArt.h"

#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// heights over the turf (cm): the line markings sit at ~2.3 cm
	constexpr float ZGlow = 3.0f;
	constexpr float ZOutline = 3.3f;
	constexpr float ZColor = 3.6f;
	constexpr float OutlineGrow = 9.f;  // cm of dark edge each side of a stroke
	const FLinearColor OutlineCol(0.01f, 0.012f, 0.02f, 0.82f);

	FVector2D Perp(const FVector2D& D) { return FVector2D(-D.Y, D.X); }
	/** The mesh stores vertex colours sRGB-encoded and the material reads them as they are: pre-darken so the
	 *  colour on screen is the one asked for (not a washed-out lighter one). */
	FLinearColor Enc(const FLinearColor& C)
	{
		return FLinearColor(FMath::Pow(C.R, 2.2f), FMath::Pow(C.G, 2.2f), FMath::Pow(C.B, 2.2f), C.A);
	}
}

ATagPlayArt::ATagPlayArt()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	for (int32 i = 0; i < 3; ++i)
	{
		UProceduralMeshComponent* M = CreateDefaultSubobject<UProceduralMeshComponent>(*FString::Printf(TEXT("Layer%d"), i));
		M->SetupAttachment(RootComponent);
		M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		M->SetCastShadow(false);
		M->bReceivesDecals = false;
		M->SetTranslucentSortPriority(10 + i); // glows and zones, then outlines, then the colour
		Meshes.Add(M);
	}
}

void ATagPlayArt::Tick(float Dt)
{
	Super::Tick(Dt);
	const float Was = Fade;
	Fade = FMath::FInterpConstantTo(Fade, bShown ? 1.f : 0.f, Dt, bShown ? 6.f : 4.f);
	if (Fade != Was || !Mat)
	{
		if (!Mat)
		{
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagPlayArt.M_TagPlayArt"));
			if (!Base) return;
			Mat = UMaterialInstanceDynamic::Create(Base, this);
			for (UProceduralMeshComponent* M : Meshes)
				for (int32 s = 0; s < M->GetNumSections(); ++s) M->SetMaterial(s, Mat);
		}
		Mat->SetScalarParameterValue(TEXT("Opacity"), FMath::SmoothStep(0.f, 1.f, Fade));
		for (UProceduralMeshComponent* M : Meshes) M->SetVisibility(Fade > 0.001f);
	}
}

void ATagPlayArt::Begin()
{
	for (FLayer& L : Layers) L.Reset();
}

void ATagPlayArt::FLayer::Tri(const FVector& A, const FVector& B, const FVector& D, const FLinearColor& Col)
{
	const int32 I = V.Num();
	V.Append({A, B, D});
	const FLinearColor E = Enc(Col);
	C.Append({E, E, E});
	T.Append({I, I + 1, I + 2});
}

void ATagPlayArt::FLayer::Quad(const FVector& A, const FVector& B, const FVector& D, const FVector& E, const FLinearColor& Col)
{
	// A-B on one side, D-E across (A..D, B..E)
	const int32 I = V.Num();
	V.Append({A, B, D, E});
	const FLinearColor K = Enc(Col);
	C.Append({K, K, K, K});
	T.Append({I, I + 2, I + 1, I + 1, I + 2, I + 3});
}

void ATagPlayArt::Ribbon(FLayer& L, const TArray<FVector2D>& P, float W, const FLinearColor& Col, float Z)
{
	const int32 N = P.Num();
	if (N < 2) return;
	// mitered joins: one continuous strip (no overlaps, so translucent colour stays even through the turns)
	TArray<FVector> Lft, Rgt;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D In = i > 0 ? (P[i] - P[i - 1]).GetSafeNormal() : (P[1] - P[0]).GetSafeNormal();
		const FVector2D Out = i < N - 1 ? (P[i + 1] - P[i]).GetSafeNormal() : In;
		FVector2D M = (Perp(In) + Perp(Out)).GetSafeNormal();
		if (M.IsNearlyZero()) M = Perp(In);
		const float Cos = FMath::Max(0.45f, static_cast<float>(FVector2D::DotProduct(M, Perp(In))));
		const FVector2D Off = M * (W * 0.5f / Cos);
		Lft.Add(FVector(P[i] + Off, Z));
		Rgt.Add(FVector(P[i] - Off, Z));
	}
	for (int32 i = 0; i < N - 1; ++i) L.Quad(Lft[i], Lft[i + 1], Rgt[i], Rgt[i + 1], Col);
}

void ATagPlayArt::Disc(FLayer& L, const FVector2D& C, float RX, float RY, const FLinearColor& Col, float Z)
{
	const int32 Seg = 40;
	for (int32 i = 0; i < Seg; ++i)
	{
		const float A0 = 2.f * PI * i / Seg, A1 = 2.f * PI * (i + 1) / Seg;
		L.Tri(FVector(C, Z), FVector(C + FVector2D(FMath::Cos(A0) * RX, FMath::Sin(A0) * RY), Z),
			FVector(C + FVector2D(FMath::Cos(A1) * RX, FMath::Sin(A1) * RY), Z), Col);
	}
}

void ATagPlayArt::Ring(FLayer& L, const FVector2D& C, float RX, float RY, float W, const FLinearColor& Col, float Z)
{
	const int32 Seg = 48;
	for (int32 i = 0; i < Seg; ++i)
	{
		const float A0 = 2.f * PI * i / Seg, A1 = 2.f * PI * (i + 1) / Seg;
		const FVector2D U0(FMath::Cos(A0), FMath::Sin(A0)), U1(FMath::Cos(A1), FMath::Sin(A1));
		auto At = [&](const FVector2D& U, float Grow) { return FVector(C + FVector2D(U.X * (RX + Grow), U.Y * (RY + Grow)), Z); };
		L.Quad(At(U0, W * 0.5f), At(U1, W * 0.5f), At(U0, -W * 0.5f), At(U1, -W * 0.5f), Col);
	}
}

void ATagPlayArt::EndCap(FLayer& L, const FVector2D& At, const FVector2D& Dir, EArtEnd End, float W, float Grow, const FLinearColor& Col, float Z)
{
	const FVector2D S = Perp(Dir);
	switch (End)
	{
	case EArtEnd::Arrow:
	{
		// the head starts where the stroke ends
		const float Len = W * 3.4f + Grow * 2.2f, Half = W * 1.75f + Grow * 1.4f;
		const FVector2D B = At - Dir * Grow * 1.2f;
		L.Tri(FVector(B + S * Half, Z), FVector(B - S * Half, Z), FVector(B + Dir * Len, Z), Col);
		break;
	}
	case EArtEnd::Block:
	{
		// a T: the bar across the end of the stroke
		const float Half = W * 2.6f + Grow, Th = W * 0.9f + Grow;
		const FVector2D B = At - Dir * Grow;
		L.Quad(FVector(B + S * Half, Z), FVector(B - S * Half, Z), FVector(B + S * Half + Dir * Th, Z), FVector(B - S * Half + Dir * Th, Z), Col);
		break;
	}
	case EArtEnd::Dot:
		Disc(L, At, W * 1.3f + Grow, W * 1.3f + Grow, Col, Z);
		break;
	default:
		break;
	}
}

void ATagPlayArt::Line(const TArray<FVector>& Pts, const FLinearColor& Color, float Width, EArtEnd End, bool bGlow)
{
	TArray<FVector2D> P;
	for (const FVector& V : Pts)
		if (P.Num() == 0 || FVector2D::Distance(P.Last(), FVector2D(V)) > 4.f) P.Add(FVector2D(V));
	if (P.Num() < 2) return;
	// shorten the stroke under the arrow head / bar so their tips land on the end point
	const FVector2D Dir = (P.Last() - P[P.Num() - 2]).GetSafeNormal();
	const FVector2D Tip = P.Last();
	if (End == EArtEnd::Arrow)
	{
		const float Cut = Width * 3.0f;
		if (FVector2D::Distance(P.Last(), P[P.Num() - 2]) > Cut + 5.f) P.Last() -= Dir * Cut;
	}
	const FVector2D CapAt = End == EArtEnd::Arrow ? P.Last() : Tip;
	if (bGlow)
	{
		FLinearColor G = Color;
		G.A = 0.22f;
		Ribbon(Layers[0], P, Width * 3.2f, G, ZGlow);
		EndCap(Layers[0], CapAt, Dir, End, Width, Width * 1.1f, G, ZGlow);
	}
	Ribbon(Layers[1], P, Width + OutlineGrow * 2.f, OutlineCol, ZOutline);
	EndCap(Layers[1], CapAt, Dir, End, Width, OutlineGrow, OutlineCol, ZOutline);
	Ribbon(Layers[2], P, Width, Color, ZColor);
	EndCap(Layers[2], CapAt, Dir, End, Width, 0.f, Color, ZColor);
}

void ATagPlayArt::Zone(const FVector& Center, float RadiusX, float RadiusY, const FLinearColor& Color)
{
	FLinearColor Fill = Color;
	Fill.A = 0.24f;
	Disc(Layers[0], FVector2D(Center), RadiusX, RadiusY, Fill, ZGlow);
	Ring(Layers[1], FVector2D(Center), RadiusX, RadiusY, 26.f, OutlineCol, ZOutline);
	FLinearColor Rim = Color;
	Rim.A = 0.95f;
	Ring(Layers[2], FVector2D(Center), RadiusX, RadiusY, 12.f, Rim, ZColor);
}

void ATagPlayArt::Commit()
{
	for (int32 i = 0; i < 3; ++i)
	{
		UProceduralMeshComponent* M = Meshes[i];
		const FLayer& L = Layers[i];
		if (L.V.Num() == 0)
		{
			M->ClearAllMeshSections();
			continue;
		}
		TArray<FVector> Nrm;
		Nrm.Init(FVector::UpVector, L.V.Num());
		TArray<FVector2D> UV;
		UV.Init(FVector2D::ZeroVector, L.V.Num());
		M->CreateMeshSection_LinearColor(0, L.V, L.T, Nrm, UV, L.C, TArray<FProcMeshTangent>(), false);
		if (Mat) M->SetMaterial(0, Mat);
	}
}
