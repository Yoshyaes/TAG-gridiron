#include "Render/Stadium.h"

#include "Render/Athlete.h"
#include "Render/Coords.h"
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
	// ---------------- turf: 5-yard mowing stripes (cube is 100 cm)
	const FLinearColor G1 = bSnowy ? FLinearColor(0.11f, 0.2f, 0.1f) : FLinearColor(0.035f, 0.13f, 0.03f);
	const FLinearColor G2 = bSnowy ? FLinearColor(0.1f, 0.18f, 0.09f) : FLinearColor(0.028f, 0.105f, 0.025f);
	const float TurfRough = bWet ? 0.35f : 0.9f;
	for (int i = 0; i < 24; ++i)
	{
		const double X0 = (-10 + i * 5) * YD;
		AddMesh(Cube, FVector(X0 + 2.5 * YD, 0, -50), FVector(5 * YD / 100, FW * YD / 100, 1), i % 2 ? G1 : G2, TurfRough, FRotator::ZeroRotator, false);
	}
	// surround
	AddMesh(Cube, FVector(50 * YD, 0, -52), FVector(170 * YD / 100, 100 * YD / 100, 1), bSnowy ? FLinearColor(0.6f, 0.62f, 0.65f) : FLinearColor(0.02f, 0.08f, 0.02f), 0.95f, FRotator::ZeroRotator, false);
	// end zones
	auto EndZone = [&](double X0, const tag::Team& T, float Yaw)
	{
		AddMesh(Cube, FVector((X0 + 5) * YD, 0, -49.6), FVector(10 * YD / 100, FW * YD / 100, 1), Hex(T.colors.primary) * 0.55f, 0.85f, FRotator::ZeroRotator, false);
		AddText(FString(T.name.c_str()).ToUpper(), FVector((X0 + 5) * YD, 0, 2), FRotator(90, Yaw, 0), 520.f, FColor::White);
	};
	EndZone(-10, Away, 90);
	EndZone(100, Home, -90);
	// ---------------- markings (white paint, slightly emissive-looking via low roughness off)
	const FLinearColor Paint(0.85f, 0.85f, 0.82f);
	UInstancedStaticMeshComponent* Lines = AddISM(Cube, Paint, 0.7f, false);
	auto Line = [&](double X, double Y, double Len, double Wid, bool bAlongY)
	{
		const FVector S = bAlongY ? FVector(Wid * YD / 100, Len * YD / 100, 0.005) : FVector(Len * YD / 100, Wid * YD / 100, 0.005);
		Lines->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X * YD, Y * YD, 1.0), S));
	};
	for (int yd = 0; yd <= 100; ++yd)
	{
		if (yd % 5 == 0)
			Line(yd, 0, FW - 0.6, (yd == 0 || yd == 100) ? 0.22 : 0.12, true);
		else
		{
			for (double y : {-FW / 2 + 0.6, FW / 2 - 0.6, -3.083, 3.083})
				Line(yd, y, 0.67, 0.1, true);
		}
	}
	// sidelines and end lines
	Line(50, -FW / 2, 120, 0.33, false);
	Line(50, FW / 2, 120, 0.33, false);
	Line(-10, 0, FW, 0.33, true);
	Line(110, 0, FW, 0.33, true);
	// two-point marks
	Line(2, 0, 1, 0.12, true);
	Line(98, 0, 1, 0.12, true);
	// yard numbers (6 ft tall, tops 9 yd from the sideline)
	for (int yd = 10; yd <= 90; yd += 10)
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
	auto Stand = [&](const FVector& C, float Yaw, double Len)
	{
		const FRotator R(0, Yaw, 0);
		const FVector Fwd = R.Vector();
		AddMesh(Cube, C + Fwd * 0, FVector(1.2, Len / 100, 2.2), WallC, 0.5f, R);
		AddMesh(Cube, C + Fwd * 1600 + FVector(0, 0, 1050), FVector(36, Len / 100, 0.5), Concrete, 0.9f, FRotator(-33, Yaw, 0));
		AddMesh(Cube, C + Fwd * 4200 + FVector(0, 0, 3300), FVector(28, Len / 100, 0.5), Concrete, 0.9f, FRotator(-38, Yaw, 0));
		AddMesh(Cube, C + Fwd * 5400 + FVector(0, 0, 5600), FVector(24, Len / 100, 0.6), FLinearColor(0.2f, 0.21f, 0.24f), 0.7f, FRotator(-8, Yaw, 0));
	};
	const double SideY = (FW / 2 + 9) * YD;
	Stand(FVector(50 * YD, SideY, 0), 90, 140 * YD);
	Stand(FVector(50 * YD, -SideY, 0), -90, 140 * YD);
	Stand(FVector(-24 * YD, 0, 0), 180, 76 * YD);
	Stand(FVector(124 * YD, 0, 0), 0, 76 * YD);
	BuildCrowd(Home);
	// sideline benches & personnel area tint
	for (double Y : {-(FW / 2 + 4.5), FW / 2 + 4.5})
		AddMesh(Cube, FVector(50 * YD, Y * YD, 30), FVector(30 * YD / 100, 0.6, 0.5), FLinearColor(0.05f, 0.05f, 0.06f), 0.6f);

	// ---------------- sky, sun / floodlights, atmosphere
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetupAttachment(RootComponent);
	Sun->SetRelativeRotation(bNight ? FRotator(-62, 30, 0) : FRotator(-48, 135, 0));
	Sun->SetIntensity(bNight ? 0.4f : (W.precip == tag::Precip::None ? 9.f : 3.f));
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
	Sky->SetIntensity(bNight ? 0.6f : 1.0f);
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
			if (bNight)
			{
				USpotLightComponent* L = NewObject<USpotLightComponent>(this);
				L->SetupAttachment(RootComponent);
				L->SetRelativeLocation(FVector(X, Y, 7000));
				L->SetRelativeRotation((FVector(50 * YD, 0, 0) - FVector(X, Y, 7000)).Rotation());
				L->SetIntensityUnits(ELightUnits::Candelas);
				L->SetIntensity(60000.f);
				L->SetAttenuationRadius(40000.f);
				L->SetOuterConeAngle(38.f);
				L->SetInnerConeAngle(20.f);
				L->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
				L->RegisterComponent();
			}
		}

	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this);
	PP->SetupAttachment(RootComponent);
	PP->bUnbound = true;
	PP->Settings.bOverride_AutoExposureMinBrightness = true;
	PP->Settings.AutoExposureMinBrightness = bNight ? 0.6f : 1.f;
	PP->Settings.bOverride_AutoExposureMaxBrightness = true;
	PP->Settings.AutoExposureMaxBrightness = bNight ? 2.f : 4.f;
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
