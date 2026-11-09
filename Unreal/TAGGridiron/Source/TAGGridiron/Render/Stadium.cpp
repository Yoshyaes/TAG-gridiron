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
		const FLinearColor SnowTint = bSnowy ? FLinearColor(0.85f, 0.9f, 0.9f) : FLinearColor(0.62f, 0.66f, 0.56f);
		// one surface for the whole 120-yard field: grass + mowing stripes + painted markings
		PlaneAt(FVector(50 * YD, 0, 0.5), 120, FW, TurfMID(1.f, SnowTint, 0.f, FLinearColor::Black, FLinearColor(60, 27, 0, 0), 1.f));
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
	AddMesh(A.Cylinder, FVector(50 * YD, 0, 0.6), FVector(9, 9, 0.004), Hex(Home.colors.primary) * 0.7f, 0.6f, FRotator::ZeroRotator, false);
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
	// ---------------- stands: lower bowl + upper deck on all four sides
	const FLinearColor Concrete(0.12f, 0.12f, 0.13f);
	const FLinearColor WallC = Hex(Home.colors.primary) * 0.6f;
	auto Deck = [&](const FVector& P, const FVector& Scale, const FRotator& R)
	{
		UStaticMeshComponent* D = AddMesh(Cube, P, Scale, Concrete, 0.9f, R);
		if (A.CrowdMat)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(A.CrowdMat, this);
			M->SetVectorParameterValue(TEXT("TeamColor"), Hex(Home.colors.primary));
			// crowd texture is 16 rows of fans ~ 2048px wide (~70 seats): tile to real seat spacing
			M->SetVectorParameterValue(TEXT("Tiling"), FLinearColor(Scale.X * 100 / 1450.f, Scale.Y * 100 / 4500.f, 0, 0));
			M->SetScalarParameterValue(TEXT("Glow"), bNight ? 0.25f : 0.08f);
			D->SetMaterial(0, M);
		}
		return D;
	};
	auto Stand = [&](const FVector& C, float Yaw, double Len)
	{
		const FRotator R(0, Yaw, 0);
		const FVector Fwd = R.Vector();
		AddMesh(Cube, C + Fwd * 0, FVector(1.2, Len / 100, 2.2), WallC, 0.5f, R);
		// seating bowls rise AWAY from the field (positive pitch about the stand's forward axis)
		Deck(C + Fwd * 1600 + FVector(0, 0, 1050), FVector(36, Len / 100, 0.5), FRotator(33, Yaw, 0));
		Deck(C + Fwd * 4200 + FVector(0, 0, 3300), FVector(28, Len / 100, 0.5), FRotator(38, Yaw, 0));
		// club-level fascia + suites between the decks (no sky gaps), and a back wall
		AddMesh(Cube, C + Fwd * 3050 + FVector(0, 0, 2100), FVector(1.0, Len / 100, 6.0), WallC * 0.7f, 0.4f, R);
		AddMesh(Cube, C + Fwd * 3060 + FVector(0, 0, 2050), FVector(1.05, Len / 100 * 0.98, 1.6), FLinearColor(0.02f, 0.03f, 0.05f), 0.1f, R, false);
		AddMesh(Cube, C + Fwd * 5300 + FVector(0, 0, 3000), FVector(1.0, Len / 100, 60), FLinearColor(0.08f, 0.08f, 0.09f), 0.9f, R);
		AddMesh(Cube, C + Fwd * 5400 + FVector(0, 0, 5600), FVector(24, Len / 100, 0.6), FLinearColor(0.2f, 0.21f, 0.24f), 0.7f, FRotator(8, Yaw, 0));
	};
	const double SideY = (FW / 2 + 9) * YD;
	Stand(FVector(50 * YD, SideY, 0), 90, 140 * YD);
	Stand(FVector(50 * YD, -SideY, 0), -90, 140 * YD);
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
	Stand(FVector(-24 * YD, 0, 0), 180, 76 * YD);
	Stand(FVector(124 * YD, 0, 0), 0, 76 * YD);
	if (!A.CrowdMat) BuildCrowd(Home);
	// sideline benches & personnel area tint
	for (double Y : {-(FW / 2 + 4.5), FW / 2 + 4.5})
		AddMesh(Cube, FVector(50 * YD, Y * YD, 30), FVector(30 * YD / 100, 0.6, 0.5), FLinearColor(0.05f, 0.05f, 0.06f), 0.6f);

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

	// stadium floodlights (always on at night; supplementary by day)
	const double LX[2] = {-12 * YD, 112 * YD};
	const double LY[2] = {-(FW / 2 + 30) * YD, (FW / 2 + 30) * YD};
	for (double X : LX)
		for (double Y : LY)
		{
			AddMesh(A.Cylinder, FVector(X, Y, 3500), FVector(1.2, 1.2, 70), FLinearColor(0.2f, 0.2f, 0.22f), 0.7f);
			AddMesh(Cube, FVector(X, Y, 7100), FVector(12, 2, 6), FLinearColor(1, 1, 0.95f) * (bNight ? 30.f : 1.f), 0.2f, FRotator(0, FMath::RadiansToDegrees(FMath::Atan2(-Y, 50 * YD - X)), 0), false);
		}
	// night: LED light banks along both stand roofs and over the end zones, the way a modern stadium is lit. Many
	// sources from both sides give every player the broadcast look: a fan of crisp shadows around the feet, specular
	// glints on the helmets from several directions, and no dark side to anyone. Six banks cast shadows (cost).
	if (bNight)
	{
		auto Bank = [&](const FVector& At, const FVector& Aim, bool bShadow, float Candela)
		{
			AddMesh(Cube, At, FVector(9, 1.2f, 3.5f), FLinearColor(1.f, 1.f, 0.97f) * 40.f, 0.2f, (Aim - At).Rotation(), false);
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
		for (int32 Side : {-1, 1})
			for (int32 i = 0; i < 5; ++i)
			{
				const double X = (10 + 20 * i) * YD;
				const FVector At(X, Side * (FW / 2 + 24) * YD, 4600);
				const FVector Aim(X + (50 * YD - X) * 0.25, -Side * 8 * YD, 0);
				Bank(At, Aim, i % 2 == 1 || i == 2, 60000.f);
			}
		Bank(FVector(-16 * YD, 0, 4200), FVector(22 * YD, 0, 0), false, 50000.f);
		Bank(FVector(116 * YD, 0, 4200), FVector(78 * YD, 0, 0), false, 50000.f);
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
	PP->Settings.MotionBlurAmount = 0.2f;
	PP->Settings.bOverride_MotionBlurMax = true;
	PP->Settings.MotionBlurMax = 2.f;
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
