#include "Render/Stadium.h"

#include "Render/Athlete.h"
#include "Render/Coords.h"
#include "Render/TagFanSpawner.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sim/Types.h"
#include "Sim/PlaySim.h"

using namespace TagCoords;

static constexpr double FW = tag::FIELD_W;
// the bowl's front edge: sideline stands 9 yd off the sideline, end stands 14 yd behind the end lines
static const double BowlSideY = (FW / 2 + 9) * YD;
static const double BowlHalfX = 74 * YD;      // midfield to the end stands
static const double BowlCornerR = 14 * YD;    // corner radius where the sideline and end stands meet
static const double BowlRoofEdgeD = 3900;     // roof canopy's front edge, out from the bowl's front edge (cm)
static const double BowlRoofEdgeZ = 5600;

namespace
{
	// rounded-rectangle path along the bowl's front edge. Each sample carries its outward normal angle Phi; a point at
	// offset d outward is P + N(Phi) * d, and its distance around the bowl at that offset is L + d * Phi, so the seat
	// spacing holds through the corners.
	struct FBowlSample { FVector2D P; double Phi = 0; double L = 0; };
	struct FBowlBuf { TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV; };

	TArray<FBowlSample> BowlPath(double X0, double X1, double HalfY, double Rc, int32 NCorner)
	{
		const FVector2D C[4] = {{X1 - Rc, HalfY - Rc}, {X0 + Rc, HalfY - Rc}, {X0 + Rc, -HalfY + Rc}, {X1 - Rc, -HalfY + Rc}};
		TArray<FBowlSample> S;
		for (int32 q = 0; q < 4; ++q)
			for (int32 k = 0; k <= NCorner; ++k)
			{
				const double Phi = (q + double(k) / NCorner) * UE_HALF_PI;
				S.Add({C[q] + Rc * FVector2D(FMath::Cos(Phi), FMath::Sin(Phi)), Phi, 0.0});
			}
		S.Add({S[0].P, 2 * UE_PI, 0.0}); // closes the loop along the end stand
		for (int32 i = 1; i < S.Num(); ++i) S[i].L = S[i - 1].L + FVector2D::Distance(S[i].P, S[i - 1].P);
		return S;
	}

	FBowlSample PathAt(const TArray<FBowlSample>& S, double L)
	{
		for (int32 i = 1; i < S.Num(); ++i)
			if (S[i].L >= L)
			{
				const double A = (L - S[i - 1].L) / FMath::Max(S[i].L - S[i - 1].L, 1e-6);
				return {FMath::Lerp(S[i - 1].P, S[i].P, A), FMath::Lerp(S[i - 1].Phi, S[i].Phi, A), L};
			}
		return S.Last();
	}

	/** A short stretch of path centred on L, HalfW either side (aisles, tunnels, boards). */
	TArray<FBowlSample> PathPatch(const TArray<FBowlSample>& S, double L, double HalfW)
	{
		const FBowlSample C = PathAt(S, L);
		const FVector2D T(-FMath::Sin(C.Phi), FMath::Cos(C.Phi));
		return {{C.P - T * HalfW, C.Phi, L - HalfW}, {C.P + T * HalfW, C.Phi, L + HalfW}};
	}

	// Sweeps a cross-section (d outward from the path, z up; one flat face per profile segment) along the path. Hint
	// (d, z) points to the front side; both sides are emitted so nothing vanishes from behind. UVs: U is distance
	// around the bowl / USc, V is distance along the profile / VSc.
	void Sweep(FBowlBuf& B, const TArray<FBowlSample>& Path, const TArray<FVector2D>& Prof, const FVector2D& Hint, double USc, double VSc)
	{
		double V0 = 0;
		for (int32 k = 0; k + 1 < Prof.Num(); ++k)
		{
			const FVector2D Pa = Prof[k], Pb = Prof[k + 1];
			const double Seg = FVector2D::Distance(Pa, Pb);
			for (int32 Back = 0; Back < 2; ++Back)
			{
				const int32 Base = B.V.Num();
				for (const FBowlSample& S : Path)
				{
					const FVector N3(FMath::Cos(S.Phi), FMath::Sin(S.Phi), 0), T3(-N3.Y, N3.X, 0);
					FVector Nrm = FVector::CrossProduct(T3, N3 * (Pb.X - Pa.X) + FVector::UpVector * (Pb.Y - Pa.Y)).GetSafeNormal();
					if (FVector::DotProduct(Nrm, N3 * Hint.X + FVector::UpVector * Hint.Y) < 0) Nrm = -Nrm;
					if (Back) Nrm = -Nrm;
					for (int32 j = 0; j < 2; ++j)
					{
						const FVector2D E = j ? Pb : Pa;
						B.V.Add(FVector(S.P.X + N3.X * E.X, S.P.Y + N3.Y * E.X, E.Y));
						B.N.Add(Nrm);
						B.UV.Add(FVector2D((S.L + E.X * S.Phi) / USc, (V0 + (j ? Seg : 0.0)) / VSc));
					}
				}
				for (int32 i = 0; i + 1 < Path.Num(); ++i)
				{
					const int32 A0 = Base + 2 * i, B0 = A0 + 1, A1 = A0 + 2, B1 = A0 + 3;
					// UE front faces: (v1 - v0) x (v2 - v0) points away from the face normal (left-handed, clockwise)
					const bool bFlip = FVector::DotProduct(FVector::CrossProduct(B.V[A1] - B.V[A0], B.V[B1] - B.V[A0]), B.N[A0]) > 0;
					B.T.Append(bFlip ? TArray<int32>{A0, B1, A1, A0, B0, B1} : TArray<int32>{A0, A1, B1, A0, B1, B0});
				}
			}
			V0 += Seg;
		}
	}

	/** Point on a pitched deck's top surface: deck box centred at (Dc, Zc), Depth along the slope, 50 cm thick;
	 *  T 0..1 from the front row to the back row, Lift off the surface. */
	FVector2D DeckPt(double Dc, double Zc, double Depth, double PitchDeg, double T, double Lift = 0)
	{
		const double P = FMath::DegreesToRadians(PitchDeg);
		const FVector2D Along(FMath::Cos(P), FMath::Sin(P)), Up(-FMath::Sin(P), FMath::Cos(P));
		return FVector2D(Dc, Zc) + Along * (Depth * (T - 0.5)) + Up * (25 + Lift);
	}
}

AStadium::AStadium()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

UStaticMeshComponent* AStadium::AddMesh(UStaticMesh* Mesh, const FVector& Loc, const FVector& Scale, const FLinearColor& Color, float Rough, const FRotator& Rot, bool bShadow)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetRelativeLocation(Loc);
	C->SetRelativeRotation(Rot);
	C->SetRelativeScale3D(Scale);
	C->SetMaterial(0, TagColorMat(this, Color, Rough));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadow);
	C->RegisterComponent();
	return C;
}

UInstancedStaticMeshComponent* AStadium::AddISM(UStaticMesh* Mesh, const FLinearColor& Color, float Rough, bool bShadow)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TagColorMat(this, Color, Rough));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadow);
	C->RegisterComponent();
	return C;
}

void AStadium::AddText(const FString& S, const FVector& Loc, const FRotator& Rot, float Size, const FColor& Color)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetupAttachment(RootComponent);
	T->SetText(FText::FromString(S));
	T->SetWorldSize(Size);
	T->SetTextRenderColor(Color);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetRelativeLocation(Loc);
	T->SetRelativeRotation(Rot);
	T->RegisterComponent();
}

void AStadium::BuildBowl(const tag::Team& Home, const tag::Team& Away, bool bNight)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<FBowlSample> Path = BowlPath(50 * YD - BowlHalfX, 50 * YD + BowlHalfX, BowlSideY, BowlCornerR, 16);
	const double Around = Path.Last().L;

	// the two seating decks: the same boxes the stands were (lower 36 m deep at 33 deg, upper 28 m at 38 deg), so the
	// Mass fans in the front sideline rows still sit on the seats
	auto Lower = [](double T, double Lift = 0) { return DeckPt(1600, 1050, 3600, 33, T, Lift); };
	auto Upper = [](double T, double Lift = 0) { return DeckPt(4200, 3300, 2800, 38, T, Lift); };
	const FVector2D Wall(-1, 0), Up(0, 1), Down(0, -1);

	FBowlBuf Crowd, Concrete, Steps, Tunnel, Padding, Glass, Ribbon, Roof, Edge;
	// seats: crowd texture is 16 rows of fans (~1450 cm of slope) by ~55 seats (~3000 cm) per tile
	Sweep(Crowd, Path, {Lower(0), Lower(1)}, Up, 3000, 1450);
	Sweep(Crowd, Path, {Upper(0), Upper(1)}, Up, 3000, 1450);
	for (FVector2D& UV : Crowd.UV) UV = FVector2D(UV.Y, UV.X); // T_Crowd is stored turned: rows along U, seats along V
	// field-level wall (padded, team colour) and its cap
	Sweep(Padding, Path, {{-60, 0}, {-60, 220}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{-60, 220}, {80, 220}}, Up, 100, 100);
	// cross aisle two thirds up the lower bowl, with tunnel mouths (vomitories) below it between every other pair of aisles
	Sweep(Steps, Path, {Lower(0.60, 2), Lower(0.645, 2)}, Up, 100, 100);
	// stair aisles every ~17 m around both decks (offset on the upper deck)
	const double Step = 1700;
	for (double L = Step * 0.5; L < Around; L += Step)
	{
		Sweep(Steps, PathPatch(Path, L, 60), {Lower(0.0, 3), Lower(1.0, 3)}, Up, 100, 100);
		Sweep(Steps, PathPatch(Path, L + Step * 0.5, 60), {Upper(0.0, 3), Upper(1.0, 3)}, Up, 100, 100);
		if (FMath::RoundToInt(L / Step) % 2 == 0)
			Sweep(Tunnel, PathPatch(Path, L + Step * 0.5, 190), {Lower(0.48, 3), Lower(0.60, 3)}, Up, 100, 100);
	}
	// club level between the decks: concrete lip, suite glass, LED ribbon board, lip, and the upper deck's front cap
	const double F = 2990;
	Sweep(Concrete, Path, {{F, 1975}, {F, 2010}}, Wall, 100, 100);
	Sweep(Glass, Path, {{F, 2010}, {F, 2230}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{F, 2230}, {F, 2260}}, Wall, 100, 100);
	Sweep(Ribbon, Path, {{F, 2260}, {F, 2440}}, Wall, 100, 180);
	Sweep(Concrete, Path, {{F, 2440}, {F, 2480}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{F, 2480}, {Upper(0).X + 10, 2480}}, Up, 100, 100);
	// back of the upper deck: press / suite level glass under the roof
	const double Bk = Upper(1).X + 10;
	Sweep(Concrete, Path, {{Bk, Upper(1).Y - 15}, {Bk, 4250}}, Wall, 100, 100);
	Sweep(Glass, Path, {{Bk, 4250}, {Bk, 4650}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{Bk, 4650}, {Bk, 5900}}, Wall, 100, 100);
	// roof canopy: team-colour front edge and the steel underside, rising gently outward
	Sweep(Edge, Path, {{BowlRoofEdgeD, BowlRoofEdgeZ - 180}, {BowlRoofEdgeD, BowlRoofEdgeZ}}, Wall, 100, 100);
	Sweep(Roof, Path, {{BowlRoofEdgeD, BowlRoofEdgeZ - 180}, {BowlRoofEdgeD + 3100, BowlRoofEdgeZ + 270}}, Down, 400, 400);

	UProceduralMeshComponent* M = NewObject<UProceduralMeshComponent>(this);
	M->SetupAttachment(RootComponent);
	M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	M->SetCastShadow(true);
	M->RegisterComponent();
	int32 Sec = 0;
	auto Add = [&](const FBowlBuf& B, UMaterialInterface* Mat)
	{
		if (B.V.Num() == 0) return;
		M->CreateMeshSection(Sec, B.V, B.T, B.N, B.UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
		M->SetMaterial(Sec++, Mat);
	};
	auto LED = [&](const FLinearColor& C, const FLinearColor& C2, float Intensity, float Count, float Speed, float Diffuse, float Rough) -> UMaterialInterface*
	{
		if (!A.LEDMat) return TagColorMat(this, C * 0.5f, Rough);
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(A.LEDMat, this);
		D->SetVectorParameterValue(TEXT("Color"), C);
		D->SetVectorParameterValue(TEXT("ColorB"), C2);
		D->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		D->SetScalarParameterValue(TEXT("Count"), Count);
		D->SetScalarParameterValue(TEXT("Speed"), Speed);
		D->SetScalarParameterValue(TEXT("Diffuse"), Diffuse);
		D->SetScalarParameterValue(TEXT("Roughness"), Rough);
		return D;
	};
	UMaterialInterface* CrowdM = TagColorMat(this, FLinearColor(0.12f, 0.12f, 0.13f), 0.9f);
	if (A.CrowdMat)
	{
		UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(A.CrowdMat, this);
		D->SetVectorParameterValue(TEXT("TeamColor"), Hex(Home.colors.primary));
		D->SetVectorParameterValue(TEXT("Tiling"), FLinearColor(1, 1, 0, 0)); // the sweep's UVs are already in tiles
		D->SetScalarParameterValue(TEXT("Glow"), bNight ? 0.25f : 0.08f);
		CrowdM = D;
	}
	const FLinearColor HomeC = Hex(Home.colors.primary), HomeC2 = Hex(Home.colors.secondary), AwayC = Hex(Away.colors.primary);
	Add(Crowd, CrowdM);
	Add(Concrete, TagColorMat(this, FLinearColor(0.16f, 0.16f, 0.17f), 0.9f));
	Add(Steps, TagColorMat(this, FLinearColor(0.32f, 0.32f, 0.33f), 0.85f));
	Add(Tunnel, TagColorMat(this, FLinearColor(0.008f, 0.008f, 0.01f), 0.9f));
	Add(Padding, TagColorMat(this, HomeC * 0.6f, 0.5f));
	// suites: dark reflective glass, warm interiors that read at night
	Add(Glass, LED(FLinearColor(1.f, 0.82f, 0.6f), FLinearColor(1.f, 0.82f, 0.6f), bNight ? 0.35f : 0.03f, 0, 0, 0.02f, 0.08f));
	// ribbon board: 12.5 m panels alternating the home colours, scrolling slowly around the bowl
	Add(Ribbon, LED(HomeC, HomeC2, bNight ? 3.f : 5.f, 0.08f, 0.12f, 0.1f, 0.3f));
	Add(Roof, TagColorMat(this, FLinearColor(0.34f, 0.35f, 0.37f), 0.6f));
	Add(Edge, TagColorMat(this, HomeC * 0.5f, 0.4f));

	// video boards on the end-zone roofs, facing the field, with the score
	for (int32 End : {-1, 1})
	{
		const double D = BowlRoofEdgeD + 900, X = 50 * YD + End * (BowlHalfX + D);
		const double Z = BowlRoofEdgeZ + 270.0 * 900 / 3100 + 1050; // board centre: sits on the canopy
		const FRotator R(0, End < 0 ? 0.f : 180.f, 0);
		const FVector In = -End * FVector::XAxisVector;
		AddMesh(A.Cube, FVector(X, 0, Z), FVector(2.4, 50, 19), FLinearColor(0.03f, 0.03f, 0.035f), 0.5f, R);
		for (double LegY : {-1600.0, 1600.0}) // legs down to the canopy
			AddMesh(A.Cube, FVector(X, LegY, Z - 1100), FVector(1.2, 1.2, 4), FLinearColor(0.2f, 0.2f, 0.22f), 0.6f, R);
		UStaticMeshComponent* Screen = AddMesh(A.Cube, FVector(X, 0, Z) + In * 125, FVector(0.05, 47, 16.4), FLinearColor::Black, 0.5f, R, false);
		Screen->SetMaterial(0, LED(FLinearColor(0.02f, 0.04f, 0.12f), FLinearColor(0.02f, 0.04f, 0.12f), bNight ? 1.5f : 1.1f, 0, 0, 0.02f, 0.2f));
		UStaticMeshComponent* Band = AddMesh(A.Cube, FVector(X, 0, Z + 640) + In * 128, FVector(0.05, 47, 2.2), FLinearColor::Black, 0.5f, R, false);
		Band->SetMaterial(0, LED(HomeC, AwayC, bNight ? 3.f : 6.f, 0.5f / 47, 0, 0.05f, 0.3f));
		for (int32 Line = 0; Line < 2; ++Line)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
			T->SetupAttachment(RootComponent);
			T->SetWorldSize(Line ? 480.f : 620.f);
			T->SetTextRenderColor(Line ? FColor(255, 214, 90) : FColor::White);
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetRelativeLocation(FVector(X, 0, Z + (Line ? -480 : 120)) + In * 140);
			T->SetRelativeRotation(R);
			T->SetCastShadow(false);
			T->RegisterComponent();
			ScoreText.Add(T);
		}
	}
	SetScoreboard(FString::Printf(TEXT("%s 0   %s 0"), UTF8_TO_TCHAR(Away.abbr.c_str()), UTF8_TO_TCHAR(Home.abbr.c_str())), TEXT("1ST  15:00"));
}

void AStadium::SetScoreboard(const FString& Top, const FString& Bottom)
{
	for (int32 i = 0; i < ScoreText.Num(); ++i)
		if (ScoreText[i]) ScoreText[i]->SetText(FText::FromString(i % 2 ? Bottom : Top));
}

void AStadium::Build(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	A.Load();
	UStaticMesh* Cube = A.Cube;
	const bool bSnowy = W.precip == tag::Precip::Snow;
	const bool bWet = W.precip == tag::Precip::Rain;
	// invisible ground for physics (ragdoll tackles land on the turf)
	{
		UStaticMeshComponent* Ground = NewObject<UStaticMeshComponent>(this);
		Ground->SetupAttachment(RootComponent);
		Ground->SetStaticMesh(Cube);
		Ground->SetRelativeLocation(FVector(50 * YD, 0, -50));
		Ground->SetRelativeScale3D(FVector(200 * YD / 100, 120 * YD / 100, 1));
		Ground->SetCollisionProfileName(TEXT("BlockAll"));
		Ground->SetHiddenInGame(true);
		Ground->SetCastShadow(false);
		Ground->RegisterComponent();
	}
	const bool bPro = A.TurfMat && A.Plane;
	auto TurfMID = [&](float LinesAmt, const FLinearColor& Tint, float PaintAmt, const FLinearColor& Paint, const FLinearColor& Tiling, float Stripes)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(A.TurfMat, this);
		M->SetScalarParameterValue(TEXT("LinesAmount"), LinesAmt);
		M->SetVectorParameterValue(TEXT("Tint"), Tint);
		M->SetScalarParameterValue(TEXT("PaintAmount"), PaintAmt);
		M->SetVectorParameterValue(TEXT("PaintColor"), Paint);
		M->SetVectorParameterValue(TEXT("GrassTiling"), Tiling);
		M->SetScalarParameterValue(TEXT("StripeAmount"), Stripes);
		M->SetScalarParameterValue(TEXT("Roughness"), bWet ? 0.45f : 0.88f);
		return M;
	};
	auto PlaneAt = [&](const FVector& C, double LenYd, double WidYd, UMaterialInterface* M)
	{
		UStaticMeshComponent* P = NewObject<UStaticMeshComponent>(this);
		P->SetupAttachment(RootComponent);
		P->SetStaticMesh(A.Plane);
		P->SetRelativeLocation(C);
		P->SetRelativeScale3D(FVector(LenYd * YD / 100, WidYd * YD / 100, 1));
		P->SetMaterial(0, M);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->RegisterComponent();
		return P;
	};
	if (bPro)
	{
		// broadcast turf: a deep, natural green (the grass texture alone renders neon under the sun), and 5-yard mowing
		// stripes strong enough to read from the high camera (StripeAmount above 1 pushes the material's stripe contrast)
		static FLinearColor TurfTint(1.05f, 0.30f, 1.3f);
		static float StripeAmt = 2.4f;
		FParse::Value(FCommandLine::Get(), TEXT("TagTurfG="), TurfTint.G);
		FParse::Value(FCommandLine::Get(), TEXT("TagStripes="), StripeAmt);
		const FLinearColor SnowTint = bSnowy ? FLinearColor(0.85f, 0.9f, 0.9f) : TurfTint;
		// one surface for the whole 120-yard field: grass + mowing stripes + painted markings
		PlaneAt(FVector(50 * YD, 0, 0.5), 120, FW, TurfMID(1.f, SnowTint, 0.f, FLinearColor::Black, FLinearColor(60, 27, 0, 0), StripeAmt));
		// team-painted end zones, inset so the boundary paint stays visible
		auto EZ = [&](double X0, const tag::Team& T, float Yaw)
		{
			PlaneAt(FVector((X0 + 5) * YD, 0, 1.0), 9.6, FW - 0.8, TurfMID(0.f, SnowTint, 0.92f, Hex(T.colors.primary), FLinearColor(5, 27, 0, 0), 0.f));
			AddText(FString(T.name.c_str()).ToUpper(), FVector((X0 + 5) * YD, 0, 3), FRotator(90, Yaw, 0), 520.f, FColor(240, 240, 240));
		};
		EZ(-10, Away, 90);
		EZ(100, Home, -90);
		// apron around the field (darker, unpainted turf)
		PlaneAt(FVector(50 * YD, 0, -0.5), 170, 100, TurfMID(0.f, FLinearColor(0.7f, 0.75f, 0.7f) * SnowTint, 0.f, FLinearColor::Black, FLinearColor(85, 50, 0, 0), 0.f));
		// the 6 ft white border painted around the field (outside the sidelines and end lines), and the team areas'
		// yellow coaching-box line 6 ft further out between the 32s
		UMaterialInstanceDynamic* Border = TurfMID(0.f, SnowTint, 0.97f, FLinearColor(0.75f, 0.75f, 0.73f), FLinearColor(40, 1, 0, 0), 0.f);
		for (double S : {-1.0, 1.0})
		{
			PlaneAt(FVector(50 * YD, S * (FW / 2 + 1) * YD, 0.4), 124, 2, Border);
			PlaneAt(FVector((50 + S * 61) * YD, 0, 0.4), 2, FW, Border);
			PlaneAt(FVector(50 * YD, S * (FW / 2 + 2.05) * YD, 0.4), 36, 0.1, TurfMID(0.f, SnowTint, 0.95f, FLinearColor(0.45f, 0.38f, 0.02f), FLinearColor(10, 1, 0, 0), 0.f));
		}
	}
	// ---------------- turf: 5-yard mowing stripes (cube is 100 cm)
	const FLinearColor G1 = bSnowy ? FLinearColor(0.11f, 0.2f, 0.1f) : FLinearColor(0.035f, 0.13f, 0.03f);
	const FLinearColor G2 = bSnowy ? FLinearColor(0.1f, 0.18f, 0.09f) : FLinearColor(0.028f, 0.105f, 0.025f);
	const float TurfRough = bWet ? 0.35f : 0.9f;
	for (int i = 0; i < (bPro ? 0 : 24); ++i)
	{
		const double X0 = (-10 + i * 5) * YD;
		AddMesh(Cube, FVector(X0 + 2.5 * YD, 0, -50), FVector(5 * YD / 100, FW * YD / 100, 1), i % 2 ? G1 : G2, TurfRough, FRotator::ZeroRotator, false);
	}
	// surround
	if (!bPro) AddMesh(Cube, FVector(50 * YD, 0, -52), FVector(170 * YD / 100, 100 * YD / 100, 1), bSnowy ? FLinearColor(0.6f, 0.62f, 0.65f) : FLinearColor(0.02f, 0.08f, 0.02f), 0.95f, FRotator::ZeroRotator, false);
	// end zones
	auto EndZone = [&](double X0, const tag::Team& T, float Yaw)
	{
		AddMesh(Cube, FVector((X0 + 5) * YD, 0, -49.6), FVector(10 * YD / 100, FW * YD / 100, 1), Hex(T.colors.primary) * 0.55f, 0.85f, FRotator::ZeroRotator, false);
		AddText(FString(T.name.c_str()).ToUpper(), FVector((X0 + 5) * YD, 0, 2), FRotator(90, Yaw, 0), 520.f, FColor::White);
	};
	if (!bPro)
	{
		EndZone(-10, Away, 90);
		EndZone(100, Home, -90);
	}
	// ---------------- markings (white paint, slightly emissive-looking via low roughness off)
	const FLinearColor Paint(0.85f, 0.85f, 0.82f);
	UInstancedStaticMeshComponent* Lines = AddISM(Cube, Paint, 0.7f, false);
	auto Line = [&](double X, double Y, double Len, double Wid, bool bAlongY)
	{
		const FVector S = bAlongY ? FVector(Wid * YD / 100, Len * YD / 100, 0.005) : FVector(Len * YD / 100, Wid * YD / 100, 0.005);
		Lines->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X * YD, Y * YD, 1.0), S));
	};
	for (int yd = 0; yd <= (bPro ? -1 : 100); ++yd)
	{
		if (yd % 5 == 0)
			Line(yd, 0, FW - 0.6, (yd == 0 || yd == 100) ? 0.22 : 0.12, true);
		else
		{
			for (double y : {-FW / 2 + 0.6, FW / 2 - 0.6, -3.083, 3.083})
				Line(yd, y, 0.67, 0.1, true);
		}
	}
	if (!bPro)
	{
		Line(50, -FW / 2, 120, 0.33, false);
		Line(50, FW / 2, 120, 0.33, false);
		Line(-10, 0, FW, 0.33, true);
		Line(110, 0, FW, 0.33, true);
		Line(2, 0, 1, 0.12, true);
		Line(98, 0, 1, 0.12, true);
	}
	// yard numbers (6 ft tall, tops 9 yd from the sideline) - baked into the turf texture when available
	for (int yd = 10; yd <= (bPro ? 0 : 90); yd += 10)
	{
		const int N = yd <= 50 ? yd : 100 - yd;
		const FString S = FString::Printf(TEXT("%d"), N);
		AddText(S, FVector(yd * YD, (FW / 2 - 11) * YD, 2), FRotator(90, 180, 0), 180.f, FColor(225, 225, 220));
		AddText(S, FVector(yd * YD, -(FW / 2 - 11) * YD, 2), FRotator(90, 0, 0), 180.f, FColor(225, 225, 220));
	}
	// midfield emblem
	AddMesh(A.Cylinder, FVector(50 * YD, 0, 0.6), FVector(9, 9, 0.004), Hex(Home.colors.primary) * 0.45f, 0.85f, FRotator::ZeroRotator, false);
	AddText(FString(Home.abbr.c_str()), FVector(50 * YD, 0, 3), FRotator(90, 0, 0), 300.f, FColor::White);
	// ---------------- line overlays
	LosLine = AddMesh(Cube, FVector(0, 0, 2), FVector(0.2, (FW - 0.6) * YD / 100, 0.005), FLinearColor(0.1f, 0.35f, 1.f), 0.3f, FRotator::ZeroRotator, false);
	FdLine = AddMesh(Cube, FVector(0, 0, 2), FVector(0.2, (FW - 0.6) * YD / 100, 0.005), FLinearColor(1.f, 0.82f, 0.f), 0.3f, FRotator::ZeroRotator, false);
	SetLines(-1, -1);
	// ---------------- goal posts (18'6" wide, 10' crossbar, 35' uprights)
	const FLinearColor Gold(0.95f, 0.75f, 0.05f);
	for (double GX : {-10.0, 110.0})
	{
		const double Off = GX < 0 ? -2 : 2; // posts sit on the end line, offset base
		AddMesh(A.Cylinder, FVector((GX + Off * 0.0) * YD, 0, 150), FVector(0.25, 0.25, 3.0), Gold, 0.35f);
		AddMesh(A.Cylinder, FVector(GX * YD, 0, 305), FVector(0.16, 0.16, 5.64), Gold, 0.35f, FRotator(0, 0, 90));
		for (double Z : {-1.0, 1.0})
			AddMesh(A.Cylinder, FVector(GX * YD, Z * 282, 305 + 533), FVector(0.12, 0.12, 10.67), Gold, 0.35f);
	}
	// pylons
	for (double X : {0.0, 100.0, -10.0, 110.0})
		for (double Y : {-FW / 2, FW / 2})
			AddMesh(Cube, FVector(X * YD, Y * YD, 23), FVector(0.1, 0.1, 0.46), FLinearColor(1.f, 0.3f, 0.f), 0.5f);
	// ---------------- stands: one bowl (lower bowl, suites, upper deck, roof) wrapping all four sides
	const double SideY = BowlSideY;
	BuildBowl(Home, Away, bNight);
	// 3D fans in the front sideline rows (Mass, dressed by the MetaHuman Crowd Sample). ~485 fans cost about 45 percent
	// of frame rate (175 -> 96 fps in a full game at 1600x900); -TagNoHeroCrowd turns them off on slower machines.
	if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoHeroCrowd")))
	{
		// seats on the lowest sideline rows; fans are Mass entities dressed by the crowd sample's appearance pipeline
		TArray<FTransform> Seats;
		for (const float Yaw : {90.f, -90.f})
		{
			const FVector C(50 * YD, Yaw > 0 ? SideY : -SideY, 0);
			const FVector Fwd = FRotator(0, Yaw, 0).Vector();
			const FVector Side = FRotator(0, Yaw + 90, 0).Vector();
			const float P = FMath::DegreesToRadians(33.f);
			const FVector SlopeF = Fwd * FMath::Cos(P) + FVector::UpVector * FMath::Sin(P);
			const FVector SlopeU = -Fwd * FMath::Sin(P) + FVector::UpVector * FMath::Cos(P);
			const FVector DeckC = C + Fwd * 1600 + FVector(0, 0, 1050);
			const float FaceYaw = (-Fwd).Rotation().Yaw;
			FRandomStream Rand(static_cast<int32>(Yaw) + 11);
			for (int32 Row = 0; Row < 3; ++Row)
				for (double T = -34.0 * YD; T <= 34.0 * YD; T += 72.0)
				{
					if (Rand.FRand() < 0.1f) continue;
					const FVector Feet = DeckC + SlopeF * (120.0 + Row * 92.0 - 1800.0) + SlopeU * 25.0 + Side * (T + Rand.FRandRange(-10, 10));
					Seats.Add(FTransform(FRotator(0, FaceYaw + Rand.FRandRange(-14, 14), 0), GetActorTransform().TransformPosition(Feet)));
				}
		}
		FActorSpawnParameters SP;
		SP.Owner = this;
		if (ATagFanSpawner* FanSpawner = GetWorld()->SpawnActor<ATagFanSpawner>(ATagFanSpawner::StaticClass(), FTransform::Identity, SP))
		{
			const bool bOk = FanSpawner->Setup(Seats);
			if (bOk) FanSpawner->SpawnFans();
			FanSpawnerActor = FanSpawner;
			UE_LOG(LogTemp, Display, TEXT("TAG: Mass fan spawner %d seats, config %s, spawn count %d"), Seats.Num(),
				bOk ? TEXT("ok") : TEXT("MISSING"), FanSpawner->SeatCount());
		}
	}
	if (!A.CrowdMat) BuildCrowd(Home);
	// sideline benches & personnel area tint
	// team benches (home on the far sideline, visitors near): seat, backrest and a team-colour skirt
	for (double S : {-1.0, 1.0})
	{
		const FLinearColor TeamC = Hex((S > 0 ? Home : Away).colors.primary);
		const double Y = S * (FW / 2 + 4.6) * YD;
		AddMesh(Cube, FVector(50 * YD, Y, 45), FVector(30 * YD / 100, 0.55, 0.08), FLinearColor(0.06f, 0.06f, 0.07f), 0.5f);
		AddMesh(Cube, FVector(50 * YD, Y + S * 25, 70), FVector(30 * YD / 100, 0.06, 0.5), TeamC * 0.5f, 0.5f);
		AddMesh(Cube, FVector(50 * YD, Y - S * 22, 22), FVector(30 * YD / 100, 0.04, 0.44), TeamC * 0.35f, 0.6f);
	}

	// ---------------- sky, sun / floodlights, atmosphere
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetupAttachment(RootComponent);
	Sun->SetRelativeRotation(bNight ? FRotator(-62, 30, 0) : FRotator(-48, 135, 0));
	Sun->SetIntensity(bNight ? 0.05f : (W.precip == tag::Precip::None ? 9.f : 3.f));
	Sun->SetLightColor(bNight ? FLinearColor(0.6f, 0.7f, 1.f) : FLinearColor(1.f, 0.95f, 0.88f));
	Sun->bAtmosphereSunLight = true;
	Sun->SetDynamicShadowCascades(4);
	Sun->RegisterComponent();

	USkyAtmosphereComponent* Atmo = NewObject<USkyAtmosphereComponent>(this);
	Atmo->SetupAttachment(RootComponent);
	Atmo->RegisterComponent();

	USkyLightComponent* Sky = NewObject<USkyLightComponent>(this);
	Sky->SetupAttachment(RootComponent);
	Sky->bRealTimeCapture = true;
	Sky->SetIntensity(bNight ? 0.04f : 1.0f); // night: the sky above the bowl goes near black
	Sky->RegisterComponent();

	if (!bNight)
	{
		UVolumetricCloudComponent* Clouds = NewObject<UVolumetricCloudComponent>(this);
		Clouds->SetupAttachment(RootComponent);
		Clouds->RegisterComponent();
	}

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(RootComponent);
	Fog->SetFogDensity(W.precip == tag::Precip::None ? 0.004f : 0.03f);
	Fog->SetVolumetricFog(true);
	Fog->RegisterComponent();

	// LED light banks hung from the roof's front edge along both sidelines and over the end zones, the way a modern
	// stadium is lit (housings by day; lit at night). Many sources from both sides give every player the broadcast
	// look: a fan of crisp shadows around the feet, specular glints on the helmets from several directions, and no dark
	// side to anyone. Six banks cast shadows (cost). Each bank's intensity is set by its throw to the field, so moving
	// the banks out to the roof edge kept the field's illuminance where it was tuned.
	{
		UMaterialInstanceDynamic* LampMat = nullptr;
		if (A.LEDMat)
		{
			LampMat = UMaterialInstanceDynamic::Create(A.LEDMat, this);
			LampMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.97f, 0.92f));
			LampMat->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(1.f, 0.97f, 0.92f));
			LampMat->SetScalarParameterValue(TEXT("Intensity"), bNight ? 60.f : 0.4f);
			LampMat->SetScalarParameterValue(TEXT("Diffuse"), 0.6f);
		}
		auto Bank = [&](const FVector& At, const FVector& Aim, const FVector& WasAt, bool bShadow, float Candela)
		{
			UStaticMeshComponent* H = AddMesh(Cube, At, FVector(9, 1.2f, 3.5f), FLinearColor(0.75f, 0.75f, 0.72f), 0.3f, (Aim - At).Rotation(), false);
			if (LampMat) H->SetMaterial(0, LampMat);
			if (!bNight) return;
			Candela *= FMath::Square(FVector::Dist(At, Aim) / FVector::Dist(WasAt, Aim));
			USpotLightComponent* L = NewObject<USpotLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetRelativeLocation(At);
			L->SetRelativeRotation((Aim - At).Rotation());
			L->SetIntensityUnits(ELightUnits::Candelas);
			L->SetIntensity(Candela);
			L->SetAttenuationRadius(32000.f);
			L->SetOuterConeAngle(46.f);
			L->SetInnerConeAngle(24.f);
			L->bUseTemperature = true;
			L->Temperature = 5700.f;                 // LED sports lighting
			L->SourceRadius = 80.f;                  // a bank of lamps, not a point: slightly soft shadow edges
			L->SetCastShadows(bShadow);
			L->SetVolumetricScatteringIntensity(0.12f); // faint beams in the night air (more washes the bowl out in rain)
			L->RegisterComponent();
		};
		const double EdgeD = BowlRoofEdgeD + 150, EdgeZ = BowlRoofEdgeZ - 260; // just under the canopy's front edge
		for (int32 Side : {-1, 1})
			for (int32 i = 0; i < 5; ++i)
			{
				const double X = (10 + 20 * i) * YD;
				const FVector At(X, Side * (BowlSideY + EdgeD), EdgeZ);
				const FVector Aim(X + (50 * YD - X) * 0.25, -Side * 8 * YD, 0);
				Bank(At, Aim, FVector(X, Side * (FW / 2 + 24) * YD, 4600), i % 2 == 1 || i == 2, 60000.f);
			}
		for (int32 End : {-1, 1})
		{
			const FVector At(50 * YD + End * (BowlHalfX + EdgeD), 0, EdgeZ);
			const FVector Aim((50 + End * 28) * YD, 0, 0);
			Bank(At, Aim, FVector((50 + End * 66) * YD, 0, 4200), false, 50000.f);
		}
	}

	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this);
	PP->SetupAttachment(RootComponent);
	PP->bUnbound = true;
	// keep exposure in a narrow, broadcast-like range so the turf doesn't wash out
	// a broadcast camera holds its exposure: at night pin it (the lit bowl stays bright, the sky stays black, no
	// pumping when the shot swings across the stands); by day a narrow range follows sun and cloud
	static float NightEV = FParse::Value(FCommandLine::Get(), TEXT("TagNightEV="), NightEV) ? NightEV : 4.5f;
	PP->Settings.bOverride_AutoExposureMinBrightness = true;
	PP->Settings.AutoExposureMinBrightness = bNight ? NightEV : 0.6f;
	PP->Settings.bOverride_AutoExposureMaxBrightness = true;
	PP->Settings.AutoExposureMaxBrightness = bNight ? NightEV : 2.0f;
	PP->Settings.bOverride_AutoExposureBias = true;
	PP->Settings.AutoExposureBias = bNight ? 0.f : 0.4f;
	PP->Settings.bOverride_MotionBlurAmount = true;
	PP->Settings.MotionBlurAmount = 0.15f;  // broadcast shutter: a hint of blur on fast pans, never a smear
	PP->Settings.bOverride_MotionBlurMax = true;
	PP->Settings.MotionBlurMax = 0.6f;
	PP->Settings.bOverride_ColorSaturation = true;
	PP->Settings.ColorSaturation = FVector4(1.08f, 1.08f, 1.08f, 1.f);
	PP->Settings.bOverride_ColorContrast = true;
	PP->Settings.ColorContrast = FVector4(1.1f, 1.1f, 1.1f, 1.f);
	PP->Settings.bOverride_BloomIntensity = true;
	PP->Settings.BloomIntensity = bNight ? 0.9f : 0.5f;
	PP->Settings.bOverride_VignetteIntensity = true;
	PP->Settings.VignetteIntensity = 0.35f;
	PP->Settings.bOverride_FilmGrainIntensity = true;
	PP->Settings.FilmGrainIntensity = 0.04f;
	PP->RegisterComponent();

	// weather
	Wind = FVector(W.windX, -W.windY, 0) * YD;
	bSnow = bSnowy;
	if (W.precip != tag::Precip::None) BuildRain(W);
}

void AStadium::BuildHeroCrowd(const FVector& C, float Yaw)
{
	if (FanMeshes.Num() == 0)
	{
		// merged crowd meshes from Epic's MetaHuman Crowd Sample (one skeletal mesh per fan, no grooms) and our
		// spectator clips retargeted onto their skeleton (Tools/Art/crowd_retarget.py)
		FanIdle = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_Crowd_Idle.A_Crowd_Idle"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		FanCheer = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_Crowd_Cheer.A_Crowd_Cheer"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!FanIdle || !FanCheer) return;
		// merged crowd meshes (hard paths: the asset registry is not populated for this plugin in -game)
		static const TCHAR* Names[] = {
			TEXT("AverageFemale_Head01/MHC_AverageFemale_Head01_CombinedSkelMesh"), TEXT("AverageFemale_Head02/MHC_AverageFemale_Head02_CombinedSkelMesh"),
			TEXT("AverageMale_Head01/MHC_AverageMale_Head01_CombinedSkelMesh"), TEXT("AverageMale_Head02/MHC_AverageMale_Head02_CombinedSkelMesh"),
			TEXT("HeavysetFemale_Head01/MHC_HeavysetFemale_Head01_FullBody"), TEXT("HeavysetFemale_Head02/MHC_HeavysetFemale_Head02_FullBody"),
			TEXT("HeavysetMale_Head01/MHC_HeavysetMale_Head01_FullBody"), TEXT("ShortFemale_Head01/MHC_ShortFemale_Head01_FullBody"),
			TEXT("ShortMale_Head01/MHC_ShortMale_Head01_FullBody"), TEXT("TallFemale_Head01/MHC_TallFemale_head01_FullBody"),
			TEXT("TallMale_Head01/MHC_TallMale_Head01_FullBody")};
		int32 Tried = 0;
		FString OtherSkel;
		for (const TCHAR* N : Names)
		{
			const FString Leaf = FPaths::GetCleanFilename(N);
			const FString Path = FString::Printf(TEXT("/MetaHumanCrowdContent/Characters/Assembled/%s.%s"), N, *Leaf);
			USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			++Tried;
			if (!M) continue;
			if (M->GetSkeleton() == FanIdle->GetSkeleton()) FanMeshes.Add(M); // anims must share the skeleton
			else if (M->GetSkeleton()) OtherSkel = M->GetSkeleton()->GetPathName();
		}
		UE_LOG(LogTemp, Display, TEXT("TAG: hero crowd meshes %d of %d (anim skeleton %s, other %s)"), FanMeshes.Num(), Tried,
			FanIdle->GetSkeleton() ? *FanIdle->GetSkeleton()->GetPathName() : TEXT("-"), *OtherSkel);
		if (FanMeshes.Num() == 0) return;
	}
	// lower deck surface (matches Stand(): box 3600 x Len x 50 cm, centre Fwd*1600 + Z 1050, pitched 33 deg)
	const FRotator R(0, Yaw, 0);
	const FVector Fwd = R.Vector();
	const FVector Side = FRotator(0, Yaw + 90, 0).Vector();
	const float P = FMath::DegreesToRadians(33.f);
	const FVector SlopeF = Fwd * FMath::Cos(P) + FVector::UpVector * FMath::Sin(P);
	const FVector SlopeU = -Fwd * FMath::Sin(P) + FVector::UpVector * FMath::Cos(P);
	const FVector DeckC = C + Fwd * 1600 + FVector(0, 0, 1050);
	const float FaceYaw = (-Fwd).Rotation().Yaw;
	FRandomStream Rand(static_cast<int32>(Yaw) + 11);
	constexpr int32 Rows = 3;
	constexpr double SeatW = 72.0;
	const double Half = 34.0 * YD; // between the 16s
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		const double S = 120.0 + Row * 92.0;
		for (double T = -Half; T <= Half; T += SeatW)
		{
			if (Rand.FRand() < 0.1f) continue; // a few empty seats
			const FVector Feet = DeckC + SlopeF * (S - 1800.0) + SlopeU * 25.0 + Side * (T + Rand.FRandRange(-10, 10));
			USkeletalMeshComponent* F = NewObject<USkeletalMeshComponent>(this);
			F->SetSkeletalMesh(FanMeshes[Rand.RandRange(0, FanMeshes.Num() - 1)]);
			F->SetupAttachment(RootComponent);
			F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			F->SetCastShadow(false);
			F->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			F->bEnableUpdateRateOptimizations = true;
			F->RegisterComponent();
			// MetaHumans face +Y in component space: yaw -90 turns them to the field; a little head turn variety
			F->SetWorldLocationAndRotation(Feet, FRotator(0, FaceYaw - 90.f + Rand.FRandRange(-14, 14), 0));
			F->PlayAnimation(FanIdle, true);
			if (UAnimSingleNodeInstance* I = F->GetSingleNodeInstance())
			{
				I->SetPosition(Rand.FRandRange(0.f, FanIdle->GetPlayLength()), false);
				I->SetPlayRate(Rand.FRandRange(0.85f, 1.15f));
			}
			Fans.Add(F);
			FanDelay.Add(Rand.FRandRange(0.f, 0.9f));
			FanCheering.Add(0);
		}
	}
}

void AStadium::UpdateMassFans(float Dt)
{
	// big plays: the dressed close-up crowd actors (Mass high-res representation) cheer with our keyed loop;
	// far LODs keep the sample's baked idle. Back to the sample's crowd anim blueprint when the hype fades.
	if (!MassCheer)
	{
		MassCheer = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_CrowdActor_Cheer.A_CrowdActor_Cheer"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!MassCheer) return;
	}
	const bool bWant = bMassCheer ? Hype > 0.15f : Hype > 0.5f;
	MassScanT -= Dt;
	if (bWant == bMassCheer && (!bWant || MassScanT > 0.f)) return;
	MassScanT = 0.5f; // pick up crowd actors the Mass LOD spawns while the crowd is cheering
	bMassCheer = bWant;
	if (!bWant)
	{
		for (auto& KV : MassFanAnimClass)
		{
			if (USkeletalMeshComponent* C = KV.Key.Get())
			{
				C->SetAnimationMode(EAnimationMode::AnimationBlueprint);
				if (UClass* Cls = KV.Value.Get()) C->SetAnimInstanceClass(Cls);
			}
		}
		MassFanAnimClass.Reset();
		return;
	}
	int32 N = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->GetClass()->GetName().Contains(TEXT("MassHighRes"))) continue;
		TInlineComponentArray<USkeletalMeshComponent*> Comps(*It);
		for (USkeletalMeshComponent* C : Comps)
		{
			if (!C->GetSkeletalMeshAsset() || C->GetSkeletalMeshAsset()->GetSkeleton() != MassCheer->GetSkeleton()) continue;
			if (MassFanAnimClass.Contains(C)) continue;
			MassFanAnimClass.Add(C, C->GetAnimClass());
			C->PlayAnimation(MassCheer, true);
			if (UAnimSingleNodeInstance* I = C->GetSingleNodeInstance())
			{
				const uint32 H = GetTypeHash(C->GetUniqueID());
				I->SetPosition((H % 997) / 997.f * MassCheer->GetPlayLength(), false);
				I->SetPlayRate(0.88f + (H % 31) / 31.f * 0.25f);
			}
			++N;
		}
	}
	if (N) UE_LOG(LogTemp, Verbose, TEXT("TAG crowd cheer +%d (total %d)"), N, MassFanAnimClass.Num());
	static bool bListed = false;
	if (!bListed)
	{
		bListed = true;
		TMap<FString, int32> Classes;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It) Classes.FindOrAdd(It->GetClass()->GetName())++;
		for (auto& KV : Classes) if (KV.Value > 3 || KV.Key.Contains(TEXT("Mass")) || KV.Key.Contains(TEXT("Crowd")))
			UE_LOG(LogTemp, Display, TEXT("TAG crowd scan class %s x%d"), *KV.Key, KV.Value);
	}
}

void AStadium::UpdateHeroCrowd(float Dt)
{
	UpdateMassFans(Dt);
	if (FanSpawnerActor) FanSpawnerActor->Animate(Time, Hype);
	if (Fans.Num() == 0) return;
	// cheer while the crowd is hyped (big plays, scores), settle back to idle as Hype decays
	const bool bWantCheer = Hype > 0.5f;
	HypeHeld = (bWantCheer == (Hype > 0.15f)) ? HypeHeld + Dt : 0.f;
	for (int32 i = 0; i < Fans.Num(); ++i)
	{
		const bool bCheer = FanCheering[i] != 0;
		const bool bTarget = bCheer ? Hype > 0.15f : bWantCheer;
		if (bTarget == bCheer || HypeHeld < FanDelay[i]) continue;
		FanCheering[i] = bTarget ? 1 : 0;
		USkeletalMeshComponent* F = Fans[i];
		UAnimationAsset* Clip = bTarget ? FanCheer.Get() : FanIdle.Get();
		F->PlayAnimation(Clip, true);
		if (UAnimSingleNodeInstance* I = F->GetSingleNodeInstance())
		{
			I->SetPosition(FMath::Frac(i * 0.137f) * Clip->GetPlayLength(), false);
			I->SetPlayRate(0.9f + FMath::Frac(i * 0.29f) * 0.25f);
		}
	}
}

void AStadium::BuildCrowd(const tag::Team& Home)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const FLinearColor Cols[6] = {Hex(Home.colors.primary), Hex(Home.colors.secondary), FLinearColor(0.75f, 0.75f, 0.75f), FLinearColor(0.08f, 0.08f, 0.1f), FLinearColor(0.45f, 0.3f, 0.2f), Hex(Home.colors.primary) * 0.6f};
	for (const FLinearColor& C : Cols) CrowdSections.Add(AddISM(A.Cube, C, 0.9f, false));
	FRandomStream R(7);
	auto Bowl = [&](const FVector& Edge, float Yaw, double Len, double Depth, double Rise, double Base, double Back)
	{
		const FRotator Rot(0, Yaw, 0);
		const FVector Fwd = Rot.Vector();
		const FVector Side = FRotator(0, Yaw + 90, 0).Vector();
		const int Rows = static_cast<int>(Depth / 90.0);
		const int Seats = static_cast<int>(Len / 65.0);
		for (int r = 0; r < Rows; ++r)
			for (int s = 0; s < Seats; ++s)
			{
				if (R.FRand() < 0.12f) continue; // empty seats
				const FVector P = Edge + Fwd * (Back + r * 90.0 * FMath::Cos(FMath::DegreesToRadians(33.f))) + Side * (-Len / 2 + s * 65.0 + R.FRandRange(-8, 8)) + FVector(0, 0, Base + r * Rise);
				const FTransform T(Rot, P + FVector(0, 0, 55), FVector(0.42f, 0.45f, R.FRandRange(0.95f, 1.3f)));
				const int32 Sec = R.RandRange(0, 5);
				const int32 Local = CrowdSections[Sec]->AddInstance(T);
				CrowdBase.Add(T);
				CrowdSectionOf.Add(Sec);
				CrowdLocal.Add(Local);
			}
	};
	const double SideY = (FW / 2 + 9) * YD;
	Bowl(FVector(50 * YD, SideY, 0), 90, 130 * YD, 2600, 58, 260, 300);
	Bowl(FVector(50 * YD, -SideY, 0), -90, 130 * YD, 2600, 58, 260, 300);
	Bowl(FVector(50 * YD, SideY, 0), 90, 130 * YD, 2200, 70, 2700, 3200);
	Bowl(FVector(50 * YD, -SideY, 0), -90, 130 * YD, 2200, 70, 2700, 3200);
	Bowl(FVector(-24 * YD, 0, 0), 180, 70 * YD, 2600, 58, 260, 300);
	Bowl(FVector(124 * YD, 0, 0), 0, 70 * YD, 2600, 58, 260, 300);
}

void AStadium::BuildRain(const tag::Weather& W)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	Rain = AddISM(A.Cube, bSnow ? FLinearColor(1, 1, 1) : FLinearColor(0.6f, 0.65f, 0.75f), 0.2f, false);
	FRandomStream R(11);
	const int N = bSnow ? 9000 : 12000;
	for (int i = 0; i < N; ++i)
	{
		const FVector P(R.FRandRange(-30, 130) * YD, R.FRandRange(-60, 60) * YD, R.FRandRange(0, 60) * YD);
		RainPos.Add(P);
		const FVector S = bSnow ? FVector(0.03f) : FVector(0.01f, 0.01f, 0.6f);
		Rain->AddInstance(FTransform(FRotator::ZeroRotator, P, S));
	}
	(void)W;
}

void AStadium::SetLines(double LosX, double FirstDownX)
{
	if (LosLine)
	{
		LosLine->SetVisibility(LosX >= 0);
		LosLine->SetRelativeLocation(FVector(LosX, 0, 2));
	}
	if (FdLine)
	{
		FdLine->SetVisibility(FirstDownX >= 0);
		FdLine->SetRelativeLocation(FVector(FirstDownX, 0, 2));
	}
}

void AStadium::Tick(float Dt)
{
	Super::Tick(Dt);
	Time += Dt;
	Hype = FMath::Max(0.f, Hype - Dt * 0.25f);
	UpdateHeroCrowd(Dt);
	// crowd: idle sway, jumping on big plays (update a rotating slice each frame to stay cheap)
	if (CrowdBase.Num() > 0)
	{
		const int32 Slice = FMath::Max(1, CrowdBase.Num() / 4);
		for (int32 k = 0; k < Slice; ++k)
		{
			const int32 i = (CrowdCursor + k) % CrowdBase.Num();
			FTransform T = CrowdBase[i];
			const float Bounce = FMath::Max(0.f, FMath::Sin(Time * (5.f + (i % 7)) + i)) * (3.f + Hype * 45.f);
			T.AddToTranslation(FVector(0, 0, Bounce));
			CrowdSections[CrowdSectionOf[i]]->UpdateInstanceTransform(CrowdLocal[i], T, false, false, true);
		}
		CrowdCursor = (CrowdCursor + Slice) % CrowdBase.Num();
		for (UInstancedStaticMeshComponent* C : CrowdSections) C->MarkRenderStateDirty();
	}
	// precipitation
	if (Rain)
	{
		const float Fall = (bSnow ? 2.0f : 30.f) * YD * Dt;
		for (int32 i = 0; i < RainPos.Num(); ++i)
		{
			FVector& P = RainPos[i];
			P += Wind * Dt * (bSnow ? 1.f : 0.6f) - FVector(0, 0, Fall);
			if (bSnow) P.X += FMath::Sin(Time * 1.3f + i) * 3.f * Dt * 60.f;
			if (P.Z < 0) P.Z += 60 * YD;
			const FVector S = bSnow ? FVector(0.03f) : FVector(0.01f, 0.01f, 0.6f);
			Rain->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, P, S), false, false, true);
		}
		Rain->MarkRenderStateDirty();
	}
}
