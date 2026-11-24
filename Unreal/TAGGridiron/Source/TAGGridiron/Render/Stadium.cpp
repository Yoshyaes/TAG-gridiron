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
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#if WITH_EDITOR
#include "SkinnedAssetCompiler.h"
#endif
#include "EngineUtils.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
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
	// seats: crowd texture is 16 rows of fans (~1450 cm of slope) by ~55 seats (~3000 cm) per tile. The decks are real
	// seating steps, one row of fans per riser + tread, so the stands get depth and a shadow line along every row; the
	// front rows (where the 3D fans sit) stay on the plain slope. A landing two thirds up the lower bowl is the cross
	// aisle, and the wall behind it carries the tunnel mouths (vomitories).
	const double RowLen = 1450.0 / 16.0;
	const FVector2D StepFace(-1, 1); // risers face the field, treads face up
	auto StepRun = [&](FVector2D C, int32 Rows, double PitchDeg, const FVector2D& Shift = FVector2D::ZeroVector)
	{
		const double R = FMath::DegreesToRadians(PitchDeg), Tread = RowLen * FMath::Cos(R), Rise = RowLen * FMath::Sin(R);
		TArray<FVector2D> P{C + Shift};
		for (int32 i = 0; i < Rows; ++i)
		{
			C.Y += Rise;
			P.Add(C + Shift);
			C.X += Tread;
			P.Add(C + Shift);
		}
		return P;
	};
	const double StepV = 16.0 * RowLen * (FMath::Cos(FMath::DegreesToRadians(33.0)) + FMath::Sin(FMath::DegreesToRadians(33.0)));
	const double StepVU = 16.0 * RowLen * (FMath::Cos(FMath::DegreesToRadians(38.0)) + FMath::Sin(FMath::DegreesToRadians(38.0)));
	const double T0 = 0.1, TLand = 0.58, LandDepth = 300;
	const int32 Rows1 = FMath::RoundToInt((TLand - T0) * 3600 / RowLen);
	const TArray<FVector2D> Lower1 = StepRun(Lower(T0), Rows1, 33);
	const FVector2D LandA = Lower1.Last(), LandB = LandA + FVector2D(LandDepth, 0);
	const FVector2D WallTop(LandB.X, LandA.Y + LandDepth * FMath::Tan(FMath::DegreesToRadians(33.0)));
	const int32 Rows2 = FMath::FloorToInt((FVector2D::Distance(WallTop, Lower(1)) - 1.0) / RowLen);
	const TArray<FVector2D> Lower2 = StepRun(WallTop, Rows2, 33);
	const int32 RowsU = FMath::FloorToInt(2800.0 / RowLen);
	const TArray<FVector2D> UpperS = StepRun(Upper(0), RowsU, 38);
	FBowlBuf Flat;
	Sweep(Flat, Path, {Lower(0), Lower(T0)}, Up, 3000, 1450);
	Sweep(Crowd, Path, Lower1, StepFace, 3000, StepV);
	Sweep(Crowd, Path, Lower2, StepFace, 3000, StepV);
	Sweep(Crowd, Path, UpperS, StepFace, 3000, StepVU);
	Crowd.T.Reserve(Crowd.T.Num() + Flat.T.Num());
	for (int32 t : Flat.T) Crowd.T.Add(t + Crowd.V.Num());
	Crowd.V.Append(Flat.V);
	Crowd.N.Append(Flat.N);
	Crowd.UV.Append(Flat.UV);
	for (FVector2D& UV : Crowd.UV) UV = FVector2D(UV.Y, UV.X); // T_Crowd is stored turned: rows along U, seats along V
	// the landing (cross aisle) and the wall behind it
	Sweep(Steps, Path, {LandA + FVector2D(0, 1), LandB + FVector2D(0, 1)}, Up, 100, 100);
	Sweep(Concrete, Path, {LandB, WallTop}, Wall, 100, 100);
	// field-level wall (padded, team colour) and its cap
	Sweep(Padding, Path, {{-60, 0}, {-60, 220}}, Wall, 100, 100);
	Sweep(Concrete, Path, {{-60, 220}, {80, 220}}, Up, 100, 100);
	// stair aisles every ~17 m around both decks (offset on the upper deck), stepped with the seats; tunnel mouths in the
	// landing wall between every other pair of aisles
	const double Step = 1700;
	const FVector2D AisleLift(-3, 3);
	const TArray<FVector2D> AisleL1 = StepRun(Lower(T0), Rows1, 33, AisleLift), AisleL2 = StepRun(WallTop, Rows2, 33, AisleLift);
	const TArray<FVector2D> AisleU = StepRun(Upper(0), RowsU, 38, AisleLift);
	for (double L = Step * 0.5; L < Around; L += Step)
	{
		const TArray<FBowlSample> At = PathPatch(Path, L, 60);
		Sweep(Steps, At, {Lower(0.0, 3), Lower(T0, 3)}, Up, 100, 100);
		Sweep(Steps, At, AisleL1, StepFace, 100, 100);
		Sweep(Steps, At, AisleL2, StepFace, 100, 100);
		Sweep(Steps, PathPatch(Path, L + Step * 0.5, 60), AisleU, StepFace, 100, 100);
		if (FMath::RoundToInt(L / Step) % 2 == 0)
			Sweep(Tunnel, PathPatch(Path, L + Step * 0.5, 160), {LandB + FVector2D(-2, 1), FVector2D(LandB.X - 2, FMath::Min(WallTop.Y, LandA.Y + 230))}, Wall, 100, 100);
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
		D->SetScalarParameterValue(TEXT("Glow"), bNight ? 0.07f : 0.08f); // at night the floodlit field outshines the stands
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
	// The realistic turf (Tools/Art/ue_build_turf.py): M_TagTurf2 is the whole playing surface in one material mapped
	// from the world position (grass, view-dependent mowing stripes, wear, lines, end zones and their wordmarks, the
	// midfield logo, the white border, the apron, rain and snow), so the paint lies in the grass and the close-up
	// blades (M_TagGrassBlade) take the colour of the ground under them. -TagOldTurf falls back to M_TagTurf.
	UMaterialInterface* Turf2Mat = (A.Plane && !FParse::Param(FCommandLine::Get(), TEXT("TagOldTurf")))
		? LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagTurf2.M_TagTurf2"), nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
	const bool bTurf2 = Turf2Mat != nullptr;
	const bool bPro = bTurf2 || (A.TurfMat && A.Plane);
	auto TurfMID =[&](float LinesAmt, const FLinearColor& Tint, float PaintAmt, const FLinearColor& Paint, const FLinearColor& Tiling, float Stripes)
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
	if (bTurf2)
	{
		// -TagTurfLum= scales the grass albedo (1 = linear 0.0135 / 0.0306 / 0.0063, before the material's Tint: the
		// deep green of NFL broadcast turf in the sun, sRGB ~85 / 118 / 52; twice that read lime under Lumen), -TagStripes= the mowing stripes,
		// -TagWear= the season wear between the hashes (0 = a fresh field), -TagSnowCover= how much snow lies in a snow game
		static float Turf2Lum = 1.f, Turf2Stripes = 1.f, Turf2Wear = 0.6f, Turf2Snow = 0.5f;
		FParse::Value(FCommandLine::Get(), TEXT("TagTurfLum="), Turf2Lum);
		FParse::Value(FCommandLine::Get(), TEXT("TagStripes="), Turf2Stripes);
		FParse::Value(FCommandLine::Get(), TEXT("TagWear="), Turf2Wear);
		FParse::Value(FCommandLine::Get(), TEXT("TagSnowCover="), Turf2Snow);
		const FLinearColor PaintWhite(0.72f, 0.72f, 0.70f); // brighter clips flat white under the sun
		auto PaintLum = [](const FLinearColor& Col) { return 0.2126f * Col.R + 0.7152f * Col.G + 0.0722f * Col.B; };
		// an end zone is the team's first colour laid on the grass; the wordmark (and the midfield mark's letter) its
		// second colour when that stands off the first, white otherwise
		auto FieldColor = [&](const tag::Team& Tm)
		{
			const FLinearColor First = Hex(Tm.colors.primary) * 0.6f;
			return First * FMath::Min(1.f, 0.45f / FMath::Max(PaintLum(First), 0.01f));
		};
		auto WordColor = [&](const tag::Team& Tm)
		{
			const FLinearColor First = Hex(Tm.colors.primary), Second = Hex(Tm.colors.secondary);
			return FMath::Abs(PaintLum(Second) - PaintLum(First)) > 0.18f ? Second * 0.8f : PaintWhite;
		};
		auto SetTurf2 = [&](UMaterialInstanceDynamic* M)
		{
			M->SetScalarParameterValue(TEXT("HomeIdx"), static_cast<float>(FMath::Clamp(Home.id, 0, 31)));
			M->SetScalarParameterValue(TEXT("AwayIdx"), static_cast<float>(FMath::Clamp(Away.id, 0, 31)));
			M->SetVectorParameterValue(TEXT("HomeA"), FieldColor(Home));
			M->SetVectorParameterValue(TEXT("HomeB"), WordColor(Home));
			M->SetVectorParameterValue(TEXT("AwayA"), FieldColor(Away));
			M->SetVectorParameterValue(TEXT("AwayB"), WordColor(Away));
			M->SetVectorParameterValue(TEXT("LogoLine"), FieldColor(Home));
			M->SetVectorParameterValue(TEXT("LogoFill"), WordColor(Home));
			M->SetVectorParameterValue(TEXT("LineColor"), PaintWhite);
			M->SetVectorParameterValue(TEXT("GrassColor"), FLinearColor(0.0135f, 0.0306f, 0.0063f) * Turf2Lum);
			M->SetScalarParameterValue(TEXT("StripeAmount"), Turf2Stripes);
			M->SetScalarParameterValue(TEXT("WearAmount"), Turf2Wear);
			M->SetScalarParameterValue(TEXT("Wet"), bWet ? 1.f : 0.f);
			M->SetScalarParameterValue(TEXT("Snow"), bSnowy ? Turf2Snow : 0.f);
			M->SetScalarParameterValue(TEXT("Specular"), bWet ? 0.45f : 0.3f);
		};
		// one plane for field, border and apron, at the height the old field plane had
		UMaterialInstanceDynamic* GroundM = UMaterialInstanceDynamic::Create(Turf2Mat, this);
		SetTurf2(GroundM);
		PlaneAt(FVector(50 * YD, 0, 0.5), 170, 100, GroundM);
		// blades for the close shots
		UMaterialInterface* BladeMat = FParse::Param(FCommandLine::Get(), TEXT("TagNoGrass3D")) ? nullptr
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TAG/Materials/M_TagGrassBlade.M_TagGrassBlade"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (BladeMat)
		{
			GrassMID = UMaterialInstanceDynamic::Create(BladeMat, this);
			SetTurf2(GrassMID);
			// the tips stir with the wind: 0.15 cm in still air, up to 0.7 cm in a gale
			const FVector2D WindDir = FVector2D(W.windX, -W.windY).GetSafeNormal();
			const float Sway = FMath::Clamp(0.15f + 0.05f * static_cast<float>(FMath::Sqrt(W.windX * W.windX + W.windY * W.windY)), 0.15f, 0.7f);
			GrassMID->SetVectorParameterValue(TEXT("WindVec"), WindDir.IsNearlyZero() ? FLinearColor(Sway, 0.f, 0.f, 0.f)
				: FLinearColor(static_cast<float>(WindDir.X) * Sway, static_cast<float>(WindDir.Y) * Sway, 0.f, 0.f));
			BuildGrass3D();
		}
		UE_LOG(LogTemp, Display, TEXT("TAG turf: M_TagTurf2, stripes %.2f wear %.2f wet %d snow %.2f, 3D grass %s"), Turf2Stripes, Turf2Wear,
			bWet ? 1 : 0, bSnowy ? Turf2Snow : 0.f, Grass3D ? TEXT("on") : TEXT("off"));
	}
	else if (bPro)
	{
		// broadcast turf: a deep, natural green (the grass texture alone renders neon under the sun), and 5-yard mowing
		// stripes strong enough to read from the high camera (StripeAmount above 1 pushes the material's stripe contrast)
		static FLinearColor TurfTint(1.05f, 0.30f, 1.3f);
		static float StripeAmt = 2.4f;
		FParse::Value(FCommandLine::Get(), TEXT("TagTurfG="), TurfTint.G);
		FParse::Value(FCommandLine::Get(), TEXT("TagStripes="), StripeAmt);
		const FLinearColor SnowTint = bSnowy ? FLinearColor(0.85f, 0.9f, 0.9f) : TurfTint;
		// one surface for the whole 120-yard field: grass + mowing stripes + painted markings
		UMaterialInstanceDynamic* FieldM = TurfMID(1.f, SnowTint, 0.f, FLinearColor::Black, FLinearColor(60, 27, 0, 0), StripeAmt);
		// season wear between the hashes (T_TurfWear); -TagWear=0 for a fresh field
		static float WearAmt = 0.7f;
		FParse::Value(FCommandLine::Get(), TEXT("TagWear="), WearAmt);
		FieldM->SetScalarParameterValue(TEXT("WearAmount"), bSnowy ? WearAmt * 0.4f : WearAmt);
		PlaneAt(FVector(50 * YD, 0, 0.5), 120, FW, FieldM);
		// team-painted end zones, inset so the boundary paint stays visible
		auto EZ = [&](double X0, const tag::Team& T, float Yaw)
		{
			PlaneAt(FVector((X0 + 5) * YD, 0, 1.0), 9.6, FW - 0.8, TurfMID(0.f, SnowTint, 0.92f, Hex(T.colors.primary), FLinearColor(5, 27, 0, 0), 0.f));
			AddText(FString(T.name.c_str()).ToUpper(), FVector((X0 + 5) * YD, 0, 3), FRotator(90, Yaw, 0), 520.f, FColor(240, 240, 240));
		};
		EZ(-10, Away, 0); // lettering runs across the width, tops toward the end line
		EZ(100, Home, 180);
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
		EndZone(-10, Away, 0);
		EndZone(100, Home, 180);
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
	// midfield emblem (painted in the grass by M_TagTurf2)
	if (!bTurf2)
	{
		AddMesh(A.Cylinder, FVector(50 * YD, 0, 0.6), FVector(9, 9, 0.004), Hex(Home.colors.primary) * 0.45f, 0.85f, FRotator::ZeroRotator, false);
		AddText(FString(Home.abbr.c_str()), FVector(50 * YD, 0, 3), FRotator(90, 0, 0), 300.f, FColor::White);
	}
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
		// the front rows behind both end zones too, eight deep (-TagEndRows=N, 0 for none): the painted crowd there sits
		// right behind every touchdown close-up and read as rows of orange discs. The straight run of the end stands,
		// corners left out. Measured (bench, 1080p, seed 7): 3 sideline rows 485 fans 111 fps; + 3 end rows 780 fans 114;
		// 8 rows all round 2059 fans 106 (1% low 62); 12 rows 3073 fans 92
		static int32 EndRows = FParse::Value(FCommandLine::Get(), TEXT("TagEndRows="), EndRows) ? FMath::Clamp(EndRows, 0, 16) : 8;
		TArray<float> Yaws{90.f, -90.f};
		if (EndRows > 0) { Yaws.Add(0.f); Yaws.Add(180.f); }
		for (const float Yaw : Yaws)
		{
			const bool bEnd = Yaw == 0.f || Yaw == 180.f;
			// yd either side of the stand's middle: the straight run of each stand (the sidelines stopped at the 16s, and the
			// stands level with the end zones were the painted crowd)
			const double Span = (bEnd ? BowlSideY - BowlCornerR : BowlHalfX - BowlCornerR) / YD - 1.0;
			const FVector C = bEnd ? FVector(50 * YD + (Yaw == 0.f ? BowlHalfX : -BowlHalfX), 0, 0) : FVector(50 * YD, Yaw > 0 ? SideY : -SideY, 0);
			const FVector Fwd = FRotator(0, Yaw, 0).Vector();
			const FVector Side = FRotator(0, Yaw + 90, 0).Vector();
			const float P = FMath::DegreesToRadians(33.f);
			const FVector SlopeF = Fwd * FMath::Cos(P) + FVector::UpVector * FMath::Sin(P);
			const FVector SlopeU = -Fwd * FMath::Sin(P) + FVector::UpVector * FMath::Cos(P);
			const FVector DeckC = C + Fwd * 1600 + FVector(0, 0, 1050);
			const float FaceYaw = (-Fwd).Rotation().Yaw;
			FRandomStream Rand(static_cast<int32>(Yaw) + 11);
			// (-TagFanRows=N along the sidelines: past the three on the plain slope the rows stand on the seating steps, one
			// a tread - a close-up sees ten rows or more, and above the third the painted crowd shows)
			static int32 SideRows = FParse::Value(FCommandLine::Get(), TEXT("TagFanRows="), SideRows) ? FMath::Clamp(SideRows, 1, 16) : 3;
			const int32 FanRows = bEnd ? EndRows : SideRows;
			const double StepTread = (1450.0 / 16.0) * FMath::Cos(P), StepRise = (1450.0 / 16.0) * FMath::Sin(P);
			const FVector StepBase = C + Fwd * (1600.0 + FMath::Cos(P) * 3600.0 * (0.1 - 0.5) - FMath::Sin(P) * 25.0) + FVector(0, 0, 1050.0 + FMath::Sin(P) * 3600.0 * (0.1 - 0.5) + FMath::Cos(P) * 25.0);
			for (int32 Row = 0; Row < FanRows; ++Row)
				for (double T = -Span * YD; T <= Span * YD; T += 72.0)
				{
					if (Rand.FRand() < 0.1f) continue;
					const FVector OnRow = Row < 3 ? DeckC + SlopeF * (120.0 + Row * 92.0 - 1800.0) + SlopeU * 25.0
						: StepBase + Fwd * ((Row - 3 + 0.5) * StepTread) + FVector(0, 0, (Row - 3 + 1) * StepRise);
					const FVector Feet = OnRow + Side * (T + Rand.FRandRange(-10, 10));
					Seats.Add(FTransform(FRotator(0, FaceYaw + Rand.FRandRange(-14, 14), 0), GetActorTransform().TransformPosition(Feet)));
				}
		}
		// the four corners, round the bowl's arcs (-TagCornerRows=N, default 3): the same rows, out from the curved front
		// edge, facing in to the arc's centre
		{
			static int32 CornerRows = FParse::Value(FCommandLine::Get(), TEXT("TagCornerRows="), CornerRows) ? FMath::Clamp(CornerRows, 0, 16) : 3;
			const double P = FMath::DegreesToRadians(33.0);
			const double StepTread = (1450.0 / 16.0) * FMath::Cos(P), StepRise = (1450.0 / 16.0) * FMath::Sin(P);
			auto RowXZ = [&](int32 Row) -> FVector2D
			{
				// (out from the front edge, up): as the straight stands place them
				if (Row < 3)
				{
					const double S = 120.0 + Row * 92.0 - 1800.0;
					return FVector2D(1600.0 + FMath::Cos(P) * S - FMath::Sin(P) * 25.0, 1050.0 + FMath::Sin(P) * S + FMath::Cos(P) * 25.0);
				}
				const FVector2D Base(1600.0 + FMath::Cos(P) * 3600.0 * (0.1 - 0.5) - FMath::Sin(P) * 25.0, 1050.0 + FMath::Sin(P) * 3600.0 * (0.1 - 0.5) + FMath::Cos(P) * 25.0);
				return Base + FVector2D((Row - 3 + 0.5) * StepTread, (Row - 3 + 1) * StepRise);
			};
			const double X0 = 50 * YD - BowlHalfX, X1 = 50 * YD + BowlHalfX, Rc = BowlCornerR;
			const FVector2D Cc[4] = {{X1 - Rc, SideY - Rc}, {X0 + Rc, SideY - Rc}, {X0 + Rc, -SideY + Rc}, {X1 - Rc, -SideY + Rc}};
			FRandomStream Rand(29);
			for (int32 q = 0; q < 4; ++q)
				for (int32 Row = 0; Row < CornerRows; ++Row)
				{
					const FVector2D Xz = RowXZ(Row);
					const double R = Rc + Xz.X, Arc = R * UE_HALF_PI;
					const int32 N = FMath::Max(1, FMath::FloorToInt(Arc / 72.0));
					for (int32 k = 0; k < N; ++k)
					{
						if (Rand.FRand() < 0.1f) continue;
						const double Phi = (q + (k + 0.5 + Rand.FRandRange(-0.12f, 0.12f)) / N) * UE_HALF_PI;
						const FVector Feet(Cc[q].X + R * FMath::Cos(Phi), Cc[q].Y + R * FMath::Sin(Phi), Xz.Y);
						const float Face = static_cast<float>(FMath::RadiansToDegrees(Phi)) + 180.f;
						Seats.Add(FTransform(FRotator(0, Face + Rand.FRandRange(-14, 14), 0), GetActorTransform().TransformPosition(Feet)));
					}
				}
		}
		// model pack spectators when the pack characters are in (one skeletal mesh per fan on the 29 bone crowd rig,
		// dressed in the home or visiting colours); the Mass MetaHuman crowd otherwise
		if (BuildPackFans(Seats, Home, Away))
		{
			UE_LOG(LogTemp, Display, TEXT("TAG: model pack fans %d in %d seats"), Fans.Num(), Seats.Num());
		}
		else if (ATagFanSpawner* FanSpawner = [this]() { FActorSpawnParameters SP; SP.Owner = this;
			return GetWorld()->SpawnActor<ATagFanSpawner>(ATagFanSpawner::StaticClass(), FTransform::Identity, SP); }())
		{
			const bool bOk = FanSpawner->Setup(Seats);
			if (bOk) FanSpawner->SpawnFans();
			FanSpawnerActor = FanSpawner;
			UE_LOG(LogTemp, Display, TEXT("TAG: Mass fan spawner %d seats, config %s, spawn count %d"), Seats.Num(),
				bOk ? TEXT("ok") : TEXT("MISSING"), FanSpawner->SeatCount());
		}
	}
	if (!A.CrowdMat) BuildCrowd(Home);
	BuildOfficials();
	BuildPackSideline(Home, Away);
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
	Sun->SetCastShadows(!bNight); // at night it is 0.05 lux under the floodlights: a shadow pass nobody can see
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
	// volumetric fog for the floodlight beams at night and the thick air of rain and snow; a dry day's thin haze
	// (density 0.004) is left to the height fog alone
	Fog->SetVolumetricFog(bNight || W.precip != tag::Precip::None);
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

bool AStadium::LoadFanMeshes()
{
	if (FanMeshes.Num() > 0) return true;
	// merged crowd meshes from Epic's MetaHuman Crowd Sample (one skeletal mesh per fan, no grooms) and our
	// spectator clips retargeted onto their skeleton (Tools/Art/crowd_retarget.py)
	FanIdle = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_Crowd_Idle.A_Crowd_Idle"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	FanCheer = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/TAG/Crowd/Anims/A_Crowd_Cheer.A_Crowd_Cheer"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!FanIdle || !FanCheer) return false;
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
	return FanMeshes.Num() > 0;
}

/**
 * Loads the catalog meshes a build is about to use as one batch. One at a time (HeroMesh, then SetSkeletalMesh) every
 * newly seen mesh stalls the game thread alone on its skinned asset build while the other build workers idle; here
 * every package is requested first, so the builds all start, and they are waited for together. HeroMesh then finds
 * each mesh in memory (and roots and caches it as before).
 */
static void PreloadHeroMeshes(FAthleteAssets& A, const TArray<int32>& Items, const TCHAR* What)
{
	const double T0 = FPlatformTime::Seconds();
	TArray<int32> Requests;
	for (int32 Item : Items)
	{
		if (!A.HeroItems.IsValidIndex(Item) || A.HeroItems[Item].Mesh) continue;
		const FString Package = FPackageName::ObjectPathToPackageName(A.HeroItems[Item].Path);
		if (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package)) Requests.Add(LoadPackageAsync(Package));
	}
	if (Requests.Num()) FlushAsyncLoading(Requests);
	TArray<USkinnedAsset*> Meshes;
	for (int32 Item : Items)
		if (USkeletalMesh* M = A.HeroMesh(Item)) Meshes.AddUnique(M);
#if WITH_EDITOR
	if (Meshes.Num()) FSkinnedAssetCompilingManager::Get().FinishCompilation(Meshes);
#endif
	UE_LOG(LogTemp, Display, TEXT("TAG: model pack %s preload %d meshes (%d packages requested) in %.2f s"), What, Meshes.Num(), Requests.Num(),
		FPlatformTime::Seconds() - T0);
}

USkeletalMeshComponent* AStadium::AddStaticPerson(int32 Item, int32 HeadItem, const FVector& Rel, float FaceYaw, const FLinearColor& Team,
	const FLinearColor& Bottom, bool bKneel, int32 Seed)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (PackPreload) // BuildPackSideline's collect pass: note the meshes, build nothing
	{
		if (A.HeroItems.IsValidIndex(Item)) PackPreload->AddUnique(Item);
		if (A.HeroItems.IsValidIndex(HeadItem)) PackPreload->AddUnique(HeadItem);
		return nullptr;
	}
	USkeletalMesh* M = A.HeroMesh(Item);
	if (!M) return nullptr;
	const FString Rig = A.HeroItems[Item].Rig;
	UAnimationAsset* Clip = A.HeroClip(Rig, bKneel ? TEXT("Kneel") : TEXT("Idle"));
	USkeletalMeshComponent* P = NewObject<USkeletalMeshComponent>(this);
	P->SetSkeletalMesh(M);
	P->SetupAttachment(RootComponent);
	P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->bCastDynamicShadow = true;
	P->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	P->bEnableUpdateRateOptimizations = true;
	P->RegisterComponent();
	P->SetRelativeLocationAndRotation(Rel, FRotator(0, FaceYaw - 90.f, 0)); // the pack characters face +Y
	// team colour on placeholder clothing (recolour slots) and on the untextured sideline players
	auto Paint = [&](USkeletalMeshComponent* C)
	{
		for (int32 Mi = 0; Mi < C->GetNumMaterials(); ++Mi)
		{
			float Mode = 0.f;
			UMaterialInterface* Cur = C->GetMaterial(Mi);
			const FString Slot = Mi < C->GetMaterialSlotNames().Num() ? C->GetMaterialSlotNames()[Mi].ToString() : FString();
			const bool bRecolour = Cur && Cur->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Mode")), Mode) && Mode > 1.5f && Mode < 2.5f;
			const bool bFlatTeam = A.HeroItems[Item].Kind == TEXT("SidePlayer"); // region-painted uniform (Mode 4)
			if (!bRecolour && !bFlatTeam) continue;
			if (UMaterialInstanceDynamic* Mid = C->CreateDynamicMaterialInstance(Mi))
			{
				Mid->SetVectorParameterValue(TEXT("Tint"), Slot.StartsWith(TEXT("Bottom")) ? Bottom : Team);
				Mid->SetVectorParameterValue(TEXT("Trim"), bFlatTeam ? Bottom : Team); // neon placeholder pixels take the team colour
				Mid->SetVectorParameterValue(TEXT("Trim2"), Team);                     // a sideline player's helmet
			}
		}
	};
	Paint(P);
	if (USkeletalMesh* HM = A.HeroMesh(HeadItem))
	{
		USkeletalMeshComponent* Hc = NewObject<USkeletalMeshComponent>(this);
		Hc->SetSkeletalMesh(HM);
		Hc->SetupAttachment(P);
		Hc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Hc->bUseBoundsFromLeaderPoseComponent = true;
		Hc->RegisterComponent();
		Hc->SetLeaderPoseComponent(P);
		Paint(Hc);
	}
	if (Clip)
	{
		P->PlayAnimation(Clip, !bKneel); // the kneel holds its last frame
		if (UAnimSingleNodeInstance* I = P->GetSingleNodeInstance())
		{
			if (!bKneel) I->SetPosition(FMath::Frac(Seed * 0.618f) * Clip->GetPlayLength(), false);
			I->SetPlayRate(0.9f + FMath::Frac(Seed * 0.37f) * 0.2f);
		}
	}
	SidelinePeople.Add(P);
	return P;
}

void AStadium::BuildPackSideline(const tag::Team& Home, const tag::Team& Away)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.bHero || FParse::Param(FCommandLine::Get(), TEXT("TagNoPackSideline"))) return;
	// two passes over the same code: the first only collects the meshes it would use (AddStaticPerson records them
	// while PackPreload is set), they load as one batch, then the second pass builds
	if (!PackPreload)
	{
		TArray<int32> Want;
		PackPreload = &Want;
		BuildPackSideline(Home, Away);
		PackPreload = nullptr;
		PreloadHeroMeshes(A, Want, TEXT("sideline"));
	}
	const TCHAR* Wx = A.HeroWeather == 2 ? TEXT("rain") : A.HeroWeather == 1 ? TEXT("cold") : TEXT("warm");
	const FString Top = A.HeroWeather == 2 ? TEXT("rainjacket") : A.HeroWeather == 1 ? TEXT("windbreaker") : TEXT("poloshirt");
	auto Of = [&](const TCHAR* Token) -> TArray<int32>
	{
		TArray<int32> L = A.HeroOf(TEXT("Crew"), [&](const FHeroItem& I) { return I.Name.Contains(Token) && I.Name.Contains(Wx); });
		if (!L.Num()) L = A.HeroOf(TEXT("Crew"), [&](const FHeroItem& I) { return I.Name.Contains(Token); });
		return L;
	};
	const TArray<int32> Heads = A.HeroOf(TEXT("CoachHead"), [](const FHeroItem& I) { return !I.bNoHead; });
	const FLinearColor Grey(0.2f, 0.2f, 0.22f), Khaki(0.55f, 0.47f, 0.33f), Black(0.03f, 0.03f, 0.035f);
	const double Edge = FW / 2;
	int32 Seed = Home.id * 13 + Away.id;
	auto Put = [&](const TArray<int32>& L, int32 k, double Xyd, double Yyd, float Face, bool bKneel = false)
	{
		if (L.Num()) AddStaticPerson(L[k % L.Num()], INDEX_NONE, FVector(Xyd * YD, Yyd * YD, 0), Face, Grey, Black, bKneel, ++Seed);
	};
	// photographers behind both end lines (standing, on monopods, kneeling), facing up the field
	const TArray<int32> Slr = Of(TEXT("handheldslr")), Mono = Of(TEXT("monopodslr_")), Kneel = Of(TEXT("monopodslrkneeling"));
	for (int32 End = 0; End < 2; ++End)
	{
		const double X = End ? 112.5 : -12.5;
		const float Face = End ? 180.f : 0.f;
		for (int32 k = 0; k < 4; ++k) Put(Slr, k + End * 4, X + (End ? 0.6 : -0.6) * (k % 2), -14.0 + k * 9.0, Face);
		Put(Mono, End, X, 10.0 * (End ? -1 : 1), Face);
		Put(Kneel, End, X + (End ? -0.8 : 0.8), 3.0 * (End ? 1 : -1), Face, true);
	}
	// broadcast crews on the near sideline outside the team area: steadicam, handheld video, parabolic audio dish
	const TArray<int32> Steadi = Of(TEXT("steadicam")), Video = Of(TEXT("broadcastvideo")), Dish = Of(TEXT("audiodish"));
	const double NearY = -(Edge + 3.0);
	Put(Steadi, 0, 22.0, NearY, 90.f);
	Put(Video, 0, 26.0, NearY - 0.4, 90.f);
	Put(Dish, 0, 74.0, NearY, 90.f);
	Put(Steadi, 1, 78.0, NearY - 0.3, 90.f);
	Put(Video, 1, 12.0, Edge + 3.2, -90.f);
	Put(Dish, 1, 88.0, Edge + 3.2, -90.f);
	// TV camera operators (media vests) by the far-side 20s
	const TArray<int32> Tv = A.HeroOf(TEXT("Staff"), [](const FHeroItem& I) { return I.Name.Contains(TEXT("tvcameraman")); });
	for (int32 k = 0; k < Tv.Num(); ++k)
		AddStaticPerson(Tv[k], Heads.Num() ? Heads[(Seed + k * 5) % Heads.Num()] : INDEX_NONE, FVector((20.0 + k * 60.0) * YD, (Edge + 3.6) * YD, 0), -90.f, Grey, Black, false, ++Seed);
	// coordinators with headsets beside each head coach (the pack's coordinator outfits in the team colour)
	const TArray<int32> Coord = A.HeroOf(TEXT("Staff"), [&](const FHeroItem& I) { return I.Name.Contains(TEXT("coordinator")) && I.Top == Top; });
	for (int32 T = 0; T < 2 && Coord.Num(); ++T)
	{
		const tag::Team& Tm = T == 0 ? Home : Away;
		const double Side = T == 0 ? 1.0 : -1.0;
		const double CoachX = T == 0 ? 46.0 : 54.0;
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 HeadI = Heads.Num() ? Heads[(Tm.id * 7 + k * 11 + 3) % Heads.Num()] : INDEX_NONE;
			AddStaticPerson(Coord[(Tm.id + k) % Coord.Num()], HeadI, FVector((CoachX + Side * (k ? 2.2 : -2.0) + 0.6) * YD, Side * (Edge + 2.0 + k * 0.6) * YD, 0),
				Side > 0 ? -90.f : 90.f, Hex(Tm.colors.primary) * 0.8f, Khaki, false, ++Seed);
		}
		// the pack's sideline players (helmets on, team colours) at the back of the team area
		const TArray<int32> Sp = A.HeroOf(TEXT("SidePlayer"));
		for (int32 k = 0; k < Sp.Num(); ++k)
			AddStaticPerson(Sp[k], INDEX_NONE, FVector((36.0 + k * 13.0 + T * 4.0) * YD, Side * (Edge + 5.2) * YD, 0), Side > 0 ? -90.f : 90.f,
				Hex(Tm.colors.primary), Hex(Tm.colors.secondary), false, ++Seed);
	}
	if (PackPreload) return; // the collect pass
	UE_LOG(LogTemp, Display, TEXT("TAG: model pack sideline people %d (media, coordinators, sideline players)"), SidelinePeople.Num());
}

/**
 * A seated fan: out of the ray tracing scene (hundreds of skinned meshes rebuilding their ray tracing geometry every
 * frame cost more than the rest of the render thread; Lumen still lights them from the screen) and on the mesh's fixed
 * bounds (a fan sways in place, so no bounds pass over the pose each frame).
 */
static void CrowdCheap(USkeletalMeshComponent* F)
{
	F->bVisibleInRayTracing = false;
	F->bComponentUseFixedSkelBounds = true;
}

bool AStadium::BuildPackFans(const TArray<FTransform>& Seats, const tag::Team& Home, const tag::Team& Away)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.bHero || FParse::Param(FCommandLine::Get(), TEXT("TagMassFans"))) return false;
	const TArray<int32> Kinds = A.HeroOf(TEXT("Fan"));
	if (!Kinds.Num()) return false;
	FanIdle = A.HeroClip(TEXT("crowd"), TEXT("Idle"));
	FanCheer = A.HeroClip(TEXT("crowd"), TEXT("Cheer"));
	FanClap = A.HeroClip(TEXT("crowd"), TEXT("Clap"));
	if (!FanIdle || !FanCheer) return false;
	// the season's fans: warm weather in shirts, cold in jackets, rain in ponchos where the pack has them
	const TCHAR* Wx = A.HeroWeather == 2 ? TEXT("_rain_") : A.HeroWeather == 1 ? TEXT("_cold_") : TEXT("_warm_");
	// fans whose skin weights the pack has wrong (they tear apart once animated) stay out of the seats
	TArray<int32> Pool = A.HeroOf(TEXT("Fan"), [Wx](const FHeroItem& I) { return I.Name.Contains(Wx) && I.Far < 0.15f; });
	if (Pool.Num() < 6) Pool = A.HeroOf(TEXT("Fan"), [](const FHeroItem& I) { return I.Far < 0.15f; });
	const FLinearColor HomeC[2] = {Hex(Home.colors.primary), Hex(Home.colors.secondary)};
	const FLinearColor AwayC[2] = {Hex(Away.colors.primary), Hex(Away.colors.secondary)};
	FRandomStream Rand(Home.id * 977 + 13);
	// the pack's story characters take every 24th seat, a different run of them each game (all 166 over a few games)
	const TArray<int32> Story = A.HeroOf(TEXT("Story"), [](const FHeroItem& I) { return (I.Height <= 0.f || I.Height > 140.f) && I.Far < 0.15f; }); // whole people, intact skins only
	const TArray<int32> Props = A.HeroOf(TEXT("FanProp"));
	int32 StoryNext = Story.Num() ? (Home.id * 61 + Away.id * 7) % Story.Num() : 0;
	// every mesh the seats below can ask for, loaded as one batch before the first fan is built: the weather's fan
	// pool (1,900 draws over a few dozen fans use them all), this game's run of story characters (every 24th seat,
	// in order, so the run is known up front) and the hand props. The seat loop itself is untouched and draws from
	// Rand exactly as before; it now finds each mesh already loaded.
	{
		TArray<int32> Want;
		auto Wanted = [&](int32 WantItem)
		{
			const FString WantRig = A.HeroItems[WantItem].Rig;
			if (A.HeroClip(WantRig, TEXT("Idle")) && A.HeroClip(WantRig, TEXT("Cheer"))) Want.AddUnique(WantItem); // seats skip the others
		};
		for (int32 PoolItem : Pool) Wanted(PoolItem);
		int32 StoryAt = StoryNext;
		for (int32 SeatI = 0; SeatI < Seats.Num() && Story.Num(); ++SeatI)
			if (SeatI % 24 == 3) Wanted(Story[StoryAt++ % Story.Num()]);
		for (int32 PropItem : Props) Want.AddUnique(PropItem);
		PreloadHeroMeshes(A, Want, TEXT("fans"));
	}
	for (int32 SeatI = 0; SeatI < Seats.Num(); ++SeatI)
	{
		const FTransform& Seat = Seats[SeatI];
		const bool bStory = Story.Num() && SeatI % 24 == 3;
		const int32 Item = bStory ? Story[StoryNext++ % Story.Num()] : Pool[Rand.RandRange(0, Pool.Num() - 1)];
		const FString Rig = A.HeroItems[Item].Rig;
		UAnimationAsset* Idle = A.HeroClip(Rig, TEXT("Idle"));
		UAnimationAsset* Cheer = A.HeroClip(Rig, TEXT("Cheer"));
		if (!Idle || !Cheer) continue;
		USkeletalMesh* M = A.HeroMesh(Item);
		if (!M) continue;
		USkeletalMeshComponent* F = NewObject<USkeletalMeshComponent>(this);
		F->SetSkeletalMesh(M);
		F->SetupAttachment(RootComponent);
		F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F->SetCastShadow(false);
		F->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		// A fan animates at about 30 Hz on a tick of his own (-TagFanHz=N, 0 = every frame). The engine's update rate
		// optimisation keeps one tracker per owning actor, and every fan belongs to the stadium: one near fan held all
		// 1,951 at full rate (measured: the crowd was 8 ms of a 15 ms game thread from the play-call camera, plus the
		// skin cache dispatches of 2,000 bodies a frame). Intervals differ by a few percent so the fans spread over
		// the frames instead of all posing on the same one.
		static float FanHz = FParse::Value(FCommandLine::Get(), TEXT("TagFanHz="), FanHz) ? FMath::Clamp(FanHz, 0.f, 240.f) : 30.f;
		const float FanTick = FanHz > 0.f ? (0.85f + 0.3f * static_cast<float>((SeatI * 37) % 101) / 100.f) / FanHz : 0.f;
		F->bEnableUpdateRateOptimizations = FanHz <= 0.f;
		F->PrimaryComponentTick.TickInterval = FanTick;
		CrowdCheap(F);
		F->RegisterComponent();
		// the pack characters face +Y: yaw -90 turns them to the field
		F->SetWorldLocationAndRotation(Seat.GetLocation(), FRotator(0, Seat.Rotator().Yaw - 90.f, 0));
		// one of the N x N people in the fan's texture, in the home colours (three in four) or the visitors'
		const int32 Cells = FMath::Max(1, A.HeroItems[Item].Cells);
		const bool bAway = Rand.FRand() < 0.25f;
		const FLinearColor C = (bAway ? AwayC : HomeC)[Rand.FRand() < 0.7f ? 0 : 1];
		const FLinearColor Tint = Rand.FRand() < 0.12f ? FLinearColor(0.85f, 0.85f, 0.85f) : C;
		for (int32 Mi = 0; Mi < F->GetNumMaterials(); ++Mi)
		{
			float Mode = 0.f;
			UMaterialInterface* Cur = F->GetMaterial(Mi);
			if (!Cur || !Cur->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Mode")), Mode) || Mode < 0.5f || Mode > 2.5f) continue;
			// crowd atlases: one of the N x N people; team colour on the shirt (fans) or the placeholder clothes (story)
			UMaterialInstanceDynamic* Mid = F->CreateDynamicMaterialInstance(Mi);
			Mid->SetScalarParameterValue(TEXT("CellU"), static_cast<float>(Rand.RandRange(0, Cells - 1)));
			Mid->SetScalarParameterValue(TEXT("CellV"), static_cast<float>(Rand.RandRange(0, Cells - 1)));
			Mid->SetVectorParameterValue(TEXT("Tint"), Tint);
		}
		// a sign, towel or foam finger in a few hands (crowd rig props follow the fan's pose)
		if (!bStory && Props.Num() && Rig == TEXT("crowd") && Rand.FRand() < 0.15f)
			if (USkeletalMesh* PM = A.HeroMesh(Props[Rand.RandRange(0, Props.Num() - 1)]))
			{
				USkeletalMeshComponent* Pc = NewObject<USkeletalMeshComponent>(this);
				Pc->SetSkeletalMesh(PM);
				Pc->SetupAttachment(F);
				Pc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Pc->SetCastShadow(false);
				CrowdCheap(Pc);
				Pc->PrimaryComponentTick.TickInterval = FanTick; // (it follows the fan's pose: no need to tick more often than he does)
				Pc->bUseBoundsFromLeaderPoseComponent = true;
				Pc->RegisterComponent();
				Pc->SetLeaderPoseComponent(F);
				if (UMaterialInstanceDynamic* Mid = Pc->CreateDynamicMaterialInstance(0)) Mid->SetVectorParameterValue(TEXT("Tint"), C);
			}
		F->PlayAnimation(Idle, true);
		if (UAnimSingleNodeInstance* I = F->GetSingleNodeInstance())
		{
			I->SetPosition(Rand.FRandRange(0.f, Idle->GetPlayLength()), false);
			I->SetPlayRate(Rand.FRandRange(0.85f, 1.15f));
		}
		Fans.Add(F);
		FanIdles.Add(Idle);
		FanCheers.Add((A.HeroClip(Rig, TEXT("Clap")) && Fans.Num() % 3 == 2) ? A.HeroClip(Rig, TEXT("Clap")) : Cheer);
		FanDelay.Add(Rand.FRandRange(0.f, 0.9f));
		FanCheering.Add(0);
	}
	return Fans.Num() > 0;
}

void AStadium::BuildHeroCrowd(const FVector& C, float Yaw)
{
	if (!LoadFanMeshes()) return;
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
			CrowdCheap(F);
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
		// each fan's own clips (its skeleton family); some clap instead of throwing their arms up
		UAnimationAsset* Clip = FanIdles.IsValidIndex(i) ? (bTarget ? FanCheers[i].Get() : FanIdles[i].Get())
			: (bTarget ? ((FanClap && i % 3 == 1) ? FanClap.Get() : FanCheer.Get()) : FanIdle.Get());
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
	UTagPrecipComponent* Col = NewObject<UTagPrecipComponent>(this);
	Col->SetupAttachment(RootComponent);
	Col->SetStaticMesh(A.Cube);
	Col->SetMaterial(0, TagColorMat(this, bSnow ? FLinearColor(1, 1, 1) : FLinearColor(0.6f, 0.65f, 0.75f), 0.2f));
	Col->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Col->SetCastShadow(false);
	Rain = Col;
	// The streaks (flakes) stand still in the component and the component falls: one period carries it a column height
	// down and the wind's drift along, then it wraps. The column is laid twice, the copy one period back (up and
	// upwind), so at the wrap the copy sits exactly where the first one started. Moving every instance on the CPU and
	// rebuilding the render state each frame cost the game thread ~3 ms in snow. The tiny cubes stay out of the ray
	// tracing scene and the distance field lighting (a moving field of 20k instances would invalidate both every frame).
	Rain->bVisibleInRayTracing = false;
	Rain->bAffectDistanceFieldLighting = false;
	const double H = 60 * YD;
	const double Fall = (bSnow ? 2.0 : 30.0) * YD; // cm/s
	FVector Drift = Wind * (bSnow ? 1.0 : 0.6) * (H / Fall);
	Drift.Z = 0;
	const double MaxDrift = 15 * YD; // a gale would carry the column off the field in one period
	if (Drift.Size() > MaxDrift) Drift *= MaxDrift / Drift.Size();
	RainStep = FVector(Drift.X, Drift.Y, -H);
	RainPeriod = static_cast<float>(H / Fall);
	RainPhase = 0.f;
	const FRotator Slant = bSnow ? FRotator::ZeroRotator : FRotationMatrix::MakeFromZ(RainStep.GetSafeNormal()).Rotator();
	const FVector S = bSnow ? FVector(0.03f) : FVector(0.01f, 0.01f, 0.6f);
	FRandomStream R(11);
	const int N = bSnow ? 9000 : 12000;
	TArray<FTransform> Ts;
	Ts.Reserve(N * 2);
	for (int i = 0; i < N; ++i)
	{
		const FVector P(R.FRandRange(-30, 130) * YD, R.FRandRange(-60, 60) * YD, R.FRandRange(0, 60) * YD);
		Ts.Add(FTransform(Slant, P, S));
		Ts.Add(FTransform(Slant, P - RainStep, S));
	}
	// the column's box (instance centres padded by the streak length), then the instances
	for (const FTransform& T : Ts) Col->LocalBox += T.GetLocation();
	Col->LocalBox = Col->LocalBox.ExpandBy(100.0);
	Col->RegisterComponent();
	Rain->AddInstances(Ts, false);
	(void)W;
}

void AStadium::BuildGrass3D()
{
	// One square of blades, built once at the origin and never moved: the blade material wraps every blade's foot into
	// the square around the shot's subject (GrassCenter), so the patch follows the camera while each blade stays where
	// it grows, and shrinks the blades to nothing at the patch's rim and on worn turf. A blade is five points (foot,
	// a bend at 55 % of its height, tip), both windings, its normal near straight up so it is lit like the ground it
	// stands on. TexCoord 1 = the point's offset from the foot (x, y), TexCoord 2 = (offset z, random); vertex colour
	// R = the blade's random number, G = height along the blade. No shadows, not in the ray tracing / Lumen scenes.
	// Cost: nothing while hidden (the game camera); ~90k blades, 270k triangles a view when shown.
	int32 Count = 90000;
	FParse::Value(FCommandLine::Get(), TEXT("TagGrassN="), Count);
	Count = FMath::Clamp(Count, 1000, 400000);
	FParse::Value(FCommandLine::Get(), TEXT("TagGrassPx="), GrassPx);
	FParse::Value(FCommandLine::Get(), TEXT("TagGrassDist="), GrassMaxDist);
	bGrassAlways = FParse::Param(FCommandLine::Get(), TEXT("TagGrassAlways"));
	GrassPx = FMath::Max(GrassPx, 0.5f);
	TArray<FVector> Verts, Norms;
	TArray<int32> Tris;
	TArray<FVector2D> Uv0, Uv1, Uv2;
	TArray<FLinearColor> Cols;
	Verts.Reserve(Count * 5);
	Norms.Reserve(Count * 5);
	Uv0.Reserve(Count * 5);
	Uv1.Reserve(Count * 5);
	Uv2.Reserve(Count * 5);
	Cols.Reserve(Count * 5);
	Tris.Reserve(Count * 18);
	FRandomStream Rand(4107);
	static const int32 Idx[9] = {0, 1, 2, 1, 3, 2, 2, 3, 4};
	static const float Along[5] = {0.f, 0.f, 0.55f, 0.55f, 1.f};
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Foot(Rand.FRand() * GrassPatch, Rand.FRand() * GrassPatch, 0.0);
		const float Az = Rand.FRandRange(0.f, 2.f * UE_PI);
		const FVector Side(-FMath::Sin(Az), FMath::Cos(Az), 0.0);
		const float Tall = Rand.FRandRange(2.4f, 4.0f), Wide = Rand.FRandRange(0.6f, 1.0f); // cm: mown to about 3 cm
		const float LeanAz = Rand.FRandRange(0.f, 2.f * UE_PI);
		const FVector Lean = FVector(FMath::Cos(LeanAz), FMath::Sin(LeanAz), 0.0) * (Tall * Rand.FRandRange(0.1f, 0.5f));
		const float Rnd = Rand.FRand(), Rnd2 = Rand.FRand();
		const FVector Nrm = FVector(Lean.X * 0.08, Lean.Y * 0.08, 1.0).GetSafeNormal();
		const FVector Off[5] = {
			-Side * (Wide * 0.5), Side * (Wide * 0.5),
			Lean * 0.35 - Side * (Wide * 0.36) + FVector(0, 0, Tall * 0.55), Lean * 0.35 + Side * (Wide * 0.36) + FVector(0, 0, Tall * 0.55),
			Lean + FVector(0, 0, Tall * 0.97)};
		const int32 Base = Verts.Num();
		for (int32 k = 0; k < 5; ++k)
		{
			Verts.Add(Foot + Off[k]);
			Norms.Add(Nrm);
			Uv0.Add(FVector2D(k == 4 ? 0.5 : double(k % 2), Along[k]));
			Uv1.Add(FVector2D(Off[k].X, Off[k].Y));
			Uv2.Add(FVector2D(Off[k].Z, Rnd2));
			Cols.Add(FLinearColor(Rnd, Along[k], Rnd2, 1.f));
		}
		for (int32 k = 0; k < 9; k += 3)
		{
			Tris.Append({Base + Idx[k], Base + Idx[k + 1], Base + Idx[k + 2]});
			Tris.Append({Base + Idx[k], Base + Idx[k + 2], Base + Idx[k + 1]});
		}
	}
	UProceduralMeshComponent* G = NewObject<UProceduralMeshComponent>(this);
	G->SetupAttachment(RootComponent);
	G->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	G->SetCastShadow(false);
	G->bVisibleInRayTracing = false;
	G->bAffectDistanceFieldLighting = false;
	G->bAffectDynamicIndirectLighting = false;
	G->bReceivesDecals = false;
	G->SetBoundsScale(40.f); // the vertex shader carries the blades anywhere on the field: never cull by the built square
	G->RegisterComponent();
	G->CreateMeshSection_LinearColor(0, Verts, Tris, Norms, Uv0, Uv1, Uv2, TArray<FVector2D>(), Cols, TArray<FProcMeshTangent>(), false);
	if (GrassMID)
	{
		GrassMID->SetScalarParameterValue(TEXT("PatchSize"), GrassPatch);
		GrassMID->SetScalarParameterValue(TEXT("Grow"), 0.f);
		G->SetMaterial(0, GrassMID);
	}
	G->SetVisibility(false);
	Grass3D = G;
	GrassGrow = 0.f;
}

void AStadium::UpdateGrass3D(float Dt)
{
	if (!Grass3D || !GrassMID) return;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CamMgr = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!CamMgr) return;
	const FVector Loc = CamMgr->GetCameraLocation();
	const FVector Fwd = CamMgr->GetCameraRotation().Vector();
	const float HalfFov = FMath::DegreesToRadians(FMath::Clamp(CamMgr->GetFOVAngle(), 1.f, 170.f)) * 0.5f;
	// the director's cine camera says what the shot is of (focus distance) and whether it is a close shot (f/1.2)
	double Focus = 0.0;
	float Aperture = 5.6f;
	if (const ACineCameraActor* CineCam = Cast<ACineCameraActor>(PC->GetViewTarget()))
		if (const UCineCameraComponent* Lens = CineCam->GetCineCameraComponent())
		{
			Focus = Lens->FocusSettings.ManualFocusDistance;
			Aperture = Lens->CurrentAperture;
		}
	// the subject: the focus distance down the view, no further than where the view meets the turf
	const double ToTurf = Fwd.Z < -0.02 ? (Loc.Z - 0.5) / -Fwd.Z : 1.0e6;
	const double Dist = FMath::Max(Focus > 50.0 ? FMath::Min(Focus, ToTurf) : ToTurf, 50.0);
	// a 3.2 cm blade's height on a 1080p frame
	const double BladePx = 3.2 * 960.0 / (Dist * FMath::Tan(HalfFov));
	float Want = Aperture <= 2.f ? 1.f : FMath::Clamp(static_cast<float>((BladePx - GrassPx) / (GrassPx * 0.5)), 0.f, 1.f);
	Want *= FMath::Clamp(static_cast<float>((GrassMaxDist - Dist) / 400.0), 0.f, 1.f);
	if (bGrassAlways) Want = 1.f;
	const bool bCut = FVector::DistSquared(Loc, GrassCamPrev) > FMath::Square(500.0);
	GrassCamPrev = Loc;
	GrassGrow = bCut ? Want : FMath::FInterpTo(GrassGrow, Want, Dt, 6.f);
	const bool bShow = GrassGrow > 0.02f;
	if (Grass3D->IsVisible() != bShow)
	{
		Grass3D->SetVisibility(bShow);
		UE_LOG(LogTemp, Log, TEXT("TAG grass3D %s: subject %.0f cm, f/%.1f, blade %.1f px"), bShow ? TEXT("on") : TEXT("off"), Dist, Aperture, BladePx);
	}
	if (!bShow) return;
	const FVector Subject = Loc + Fwd * FMath::Min(Dist, static_cast<double>(GrassMaxDist));
	GrassMID->SetVectorParameterValue(TEXT("GrassCenter"), FLinearColor(static_cast<float>(Subject.X), static_cast<float>(Subject.Y), 0.f, 0.f));
	GrassMID->SetScalarParameterValue(TEXT("Grow"), GrassGrow);
}

void AStadium::BuildOfficials()
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const double CrewY = (FW / 2 + 1.3) * YD; // just outside the far sideline, facing the broadcast cameras
	auto Rig = [&](const TCHAR* Name)
	{
		USceneComponent* R = NewObject<USceneComponent>(this, Name);
		R->SetupAttachment(RootComponent);
		R->RegisterComponent();
		R->SetRelativeLocation(FVector(50 * YD, CrewY, 0));
		return R;
	};
	auto Part = [&](USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Rel, const FVector& Scale, const FLinearColor& C, float Rough)
	{
		UStaticMeshComponent* M = AddMesh(Mesh, FVector::ZeroVector, Scale, C, Rough, FRotator::ZeroRotator, true);
		M->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
		M->SetRelativeLocation(Rel);
		return M;
	};
	const FLinearColor Black(0.02f, 0.02f, 0.02f), Orange(1.f, 0.32f, 0.02f);
	// each rod: an 8 ft pole with an orange marker on top, held upright at the crew member's side (field side)
	for (int32 k = 0; k < 2; ++k)
	{
		USceneComponent* R = Rig(k ? TEXT("ChainFront") : TEXT("ChainRear"));
		(k ? ChainFront : ChainRear) = R;
		// (the model pack's rod men come with a rod skinned to one finger and modelled lying across the body: it swung
		// with the hand and rendered as a slanted 2.4 m bar. hero_build strips it (strip_prop) and these stand instead.)
		Part(R, A.Cylinder, FVector(0, -30, 122), FVector(0.04, 0.04, 2.44), Black, 0.5f);
		Part(R, A.Cylinder, FVector(0, -30, 236), FVector(0.16, 0.16, 0.3), Orange, 0.6f);
	}
	{
		USceneComponent* R = Rig(TEXT("DownBox"));
		DownBox = R;
		Part(R, A.Cylinder, FVector(0, -30, 115), FVector(0.045, 0.045, 2.3), Black, 0.5f);
		Part(R, A.Cube, FVector(0, -30, 258), FVector(0.42, 0.42, 0.42), Orange, 0.5f);
		for (int32 Face = 0; Face < 2; ++Face)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
			T->SetupAttachment(R);
			T->SetWorldSize(36.f);
			T->SetTextRenderColor(FColor(15, 15, 15));
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetCastShadow(false);
			T->RegisterComponent();
			T->SetRelativeLocationAndRotation(FVector(0, -30 + (Face ? -22.f : 22.f), 258), FRotator(0, Face ? -90.f : 90.f, 0));
			DownText.Add(T);
		}
	}
	// the chain lies on the ground between the rods
	ChainLink = AddMesh(A.Cube, FVector(50 * YD, CrewY - 30, 1.5), FVector(10 * YD / 100, 0.012, 0.012), FLinearColor(0.35f, 0.35f, 0.37f), 0.3f, FRotator::ZeroRotator, false);
	SetDown(1);
}

FVector AStadium::DownBoxLocation() const
{
	return DownBox ? DownBox->GetComponentLocation() : FVector::ZeroVector;
}

void AStadium::SetDown(int32 Down)
{
	for (UTextRenderComponent* T : DownText)
		if (T) T->SetText(Down >= 1 && Down <= 4 ? FText::AsNumber(Down) : FText::GetEmpty());
}

// show / hide a chain post: the stadium's own parts, and the crew member standing at it as a whole actor (propagating
// visibility into his components would turn his hidden gear and markers back on)
static void ShowPost(AActor* Self, USceneComponent* Post, bool bShow)
{
	if (!Post) return;
	Post->SetVisibility(bShow, false);
	for (USceneComponent* C : Post->GetAttachChildren())
	{
		if (!C) continue;
		if (C->GetOwner() == Self) C->SetVisibility(bShow, true);
		else if (AActor* O = C->GetOwner()) O->SetActorHiddenInGame(!bShow);
	}
}

void AStadium::SetLines(double LosX, double FirstDownX)
{
	// the chain crew: down box at the line of scrimmage, the rods at the start of the series and the line to gain
	if (DownBox)
	{
		ShowPost(this, DownBox, LosX >= 0);
		if (LosX >= 0) DownBox->SetRelativeLocation(FVector(LosX, DownBox->GetRelativeLocation().Y, 0));
	}
	if (ChainRear && ChainFront && ChainLink)
	{
		const bool bChains = FirstDownX >= 0 && LosX >= 0;
		ShowPost(this, ChainRear, bChains);
		ShowPost(this, ChainFront, bChains);
		ChainLink->SetVisibility(bChains);
		if (bChains)
		{
			const double Dir = FirstDownX >= LosX ? 1.0 : -1.0;
			const double RearX = FirstDownX - Dir * 10 * YD;
			const double Y = ChainRear->GetRelativeLocation().Y;
			ChainRear->SetRelativeLocation(FVector(RearX, Y, 0));
			ChainFront->SetRelativeLocation(FVector(FirstDownX, Y, 0));
			ChainLink->SetRelativeLocation(FVector((RearX + FirstDownX) * 0.5, Y - 30, 1.5));
		}
	}
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
	UpdateGrass3D(Dt);
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
		RainPhase = FMath::Fmod(RainPhase + Dt / FMath::Max(RainPeriod, 0.01f), 1.f);
		Rain->SetRelativeLocation(RainStep * RainPhase);
	}
}
