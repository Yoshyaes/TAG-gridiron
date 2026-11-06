#include "Render/Athlete.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sim/Types.h"

FAthleteAssets& FAthleteAssets::Get()
{
	static FAthleteAssets A;
	return A;
}

template <class T>
static T* LoadAsset(const TCHAR* Path)
{
	T* Obj = LoadObject<T>(nullptr, Path);
	if (!Obj) UE_LOG(LogTemp, Warning, TEXT("TAG: missing asset %s"), Path);
	return Obj;
}

void FAthleteAssets::Load()
{
	if (bLoaded) return;
	bLoaded = true;
	const FString A = TEXT("/Game/Characters/Mannequins/");
	Mesh = LoadAsset<USkeletalMesh>(*(A + TEXT("Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	Locomotion = LoadAsset<UBlendSpace>(*(A + TEXT("Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run")));
	FallFront = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Death/MM_Death_Front_01.MM_Death_Front_01")));
	FallFront2 = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Death/MM_Death_Front_02.MM_Death_Front_02")));
	FallBack = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Death/MM_Death_Back_01.MM_Death_Back_01")));
	HitReact = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Rifle/HitReact/MM_HitReact_Front_Hvy_01.MM_HitReact_Front_Hvy_01")));
	Shove = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Unarmed/Attack/MM_Attack_02.MM_Attack_02")));
	Throw = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Unarmed/Attack/MM_Attack_03.MM_Attack_03")));
	Jump = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Unarmed/Jump/MM_Jump.MM_Jump")));
	Dash = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Unarmed/Jump/MM_Dash.MM_Dash")));
	Sphere = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Cylinder = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ShapeMat = LoadAsset<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UMaterialInstanceDynamic* TagColorMat(UObject* Outer, const FLinearColor& C, float Roughness)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	A.Load();
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(A.ShapeMat, Outer);
	M->SetVectorParameterValue(TEXT("Color"), C);
	M->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	return M;
}

AAthlete::AAthlete()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetRelativeRotation(FRotator(0, -90, 0)); // mannequin faces +Y
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->bCastDynamicShadow = true;
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Helmet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helmet"));
	Facemask = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Facemask"));
	Pads = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pads"));
	for (UStaticMeshComponent* C : {Helmet.Get(), Facemask.Get(), Pads.Get()})
	{
		C->SetupAttachment(Body);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	BackNumber = CreateDefaultSubobject<UTextRenderComponent>(TEXT("BackNumber"));
	FrontNumber = CreateDefaultSubobject<UTextRenderComponent>(TEXT("FrontNumber"));
	for (UTextRenderComponent* T : {BackNumber.Get(), FrontNumber.Get()})
	{
		T->SetupAttachment(Body);
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
	}
	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(Root);
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetCastShadow(false);
	Ring->SetVisibility(false);
	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetRelativeLocation(FVector(0, 0, 255));
	Label->SetWorldSize(70.f);
	Label->SetTextRenderColor(FColor(255, 212, 0));
	Label->SetVisibility(false);
}

void AAthlete::Setup(const tag::Player* InPlayer, const FUniform& U)
{
	Player = InPlayer;
	FAthleteAssets& A = FAthleteAssets::Get();
	A.Load();
	// body: scale to the player's height and build (mannequin ~ 180 cm, ~ 80 kg)
	const float HeightCm = static_cast<float>(InPlayer->heightIn * 2.54);
	const float S = HeightCm / 180.f;
	const float Bulk = FMath::Clamp(static_cast<float>(InPlayer->weightLb) / 215.f, 0.9f, 1.35f);
	if (A.Mesh) Body->SetSkeletalMesh(A.Mesh);
	Body->SetRelativeScale3D(FVector(S * FMath::Sqrt(Bulk), S * FMath::Sqrt(Bulk), S));
	// uniform: tint the mannequin "paint" (jersey/pants areas) via both material slots
	for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
	{
		if (UMaterialInstanceDynamic* M = Body->CreateDynamicMaterialInstance(i))
		{
			M->SetVectorParameterValue(TEXT("Paint Tint"), i == 0 ? U.Jersey : U.Pants);
		}
	}
	// helmet + facemask on the head bone
	Helmet->SetStaticMesh(A.Sphere);
	Helmet->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	Helmet->SetRelativeLocation(FVector(9, 2, 0));
	Helmet->SetRelativeScale3D(FVector(0.27f, 0.25f, 0.24f));
	Helmet->SetMaterial(0, TagColorMat(this, U.Helmet, 0.18f));
	Facemask->SetStaticMesh(A.Cube);
	Facemask->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	Facemask->SetRelativeLocation(FVector(4, 13, 0));
	Facemask->SetRelativeScale3D(FVector(0.12f, 0.03f, 0.2f));
	Facemask->SetMaterial(0, TagColorMat(this, FLinearColor(0.85f, 0.85f, 0.85f), 0.3f));
	// shoulder pads over the upper spine
	// rounded shoulder pads: a flattened ellipsoid over the upper chest/shoulders
	Pads->SetStaticMesh(A.Sphere);
	Pads->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("spine_05"));
	Pads->SetRelativeLocation(FVector(8, 0, 0));
	Pads->SetRelativeScale3D(FVector(0.16f, 0.30f, 0.54f) * FMath::Sqrt(Bulk));
	Pads->SetMaterial(0, TagColorMat(this, U.Jersey, 0.7f));
	// numbers on back and chest
	const FText Num = FText::AsNumber(InPlayer->number);
	for (UTextRenderComponent* T : {BackNumber.Get(), FrontNumber.Get()})
	{
		T->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("spine_04"));
		T->SetText(Num);
		T->SetTextRenderColor(U.Number.ToFColor(true));
		T->SetWorldSize(T == BackNumber ? 26.f : 16.f);
	}
	BackNumber->SetRelativeLocation(FVector(6, -19, 0));
	BackNumber->SetRelativeRotation(FRotator(0, 90, 90));
	FrontNumber->SetRelativeLocation(FVector(6, 17, 0));
	FrontNumber->SetRelativeRotation(FRotator(0, -90, 90));
	// selection ring
	Ring->SetStaticMesh(A.Cylinder);
	Ring->SetRelativeScale3D(FVector(1.3f, 1.3f, 0.01f));
	Ring->SetRelativeLocation(FVector(0, 0, 1));
	Ring->SetMaterial(0, TagColorMat(this, FLinearColor(1.f, 0.83f, 0.f), 0.5f));
	if (A.Locomotion)
	{
		const FBlendParameter& P = A.Locomotion->GetBlendParameter(0);
		BlendAxisMax = FMath::Max(1.f, P.Max);
	}
	CurrentAnim.Reset();
	CurrentClip = nullptr;
}

void AAthlete::PlayClip(UAnimationAsset* Clip, bool bLoop, float Rate)
{
	if (!Clip) return;
	if (CurrentClip != Clip)
	{
		Body->PlayAnimation(Clip, bLoop);
		CurrentClip = Clip;
	}
	if (UAnimSingleNodeInstance* I = Body->GetSingleNodeInstance()) I->SetPlayRate(Rate);
}

void AAthlete::Drive(const FVector& Location, float Yaw, float SpeedYds, float VMaxYds, const FString& Anim, float AnimT, float DeltaTime)
{
	SetActorLocation(Location);
	// smooth heading so turns read naturally
	if (!bYawInit)
	{
		SmoothedYaw = Yaw;
		bYawInit = true;
	}
	const float D = FMath::FindDeltaAngleDegrees(SmoothedYaw, Yaw);
	SmoothedYaw += D * FMath::Clamp(DeltaTime * 12.f, 0.f, 1.f);
	SetActorRotation(FRotator(0, SmoothedYaw, 0));

	FAthleteAssets& A = FAthleteAssets::Get();
	const float SpeedCm = SpeedYds * 91.44f;
	const bool bChanged = Anim != CurrentAnim;
	CurrentAnim = Anim;
	if (Anim == TEXT("down"))
	{
		if (bChanged) PlayClip(Player && (Player->id % 2) ? A.FallFront : A.FallFront2, false, 1.3f);
	}
	else if (Anim == TEXT("tackle") || Anim == TEXT("dive"))
	{
		if (bChanged) PlayClip(A.FallFront2, false, 1.6f);
	}
	else if (Anim == TEXT("block"))
	{
		if (bChanged) PlayClip(A.Shove, true, 0.55f);
	}
	else if (Anim == TEXT("throw"))
	{
		if (bChanged) PlayClip(A.Throw, false, 1.4f);
	}
	else if (Anim == TEXT("catch") || Anim == TEXT("celebrate"))
	{
		if (bChanged) PlayClip(A.Jump, Anim == TEXT("celebrate"), 1.0f);
	}
	else if (Anim == TEXT("juke"))
	{
		if (bChanged) PlayClip(A.Dash, false, 1.5f);
	}
	else
	{
		// locomotion blend space: idle -> walk -> run; above the clip's top speed, play faster
		if (A.Locomotion)
		{
			if (CurrentClip != A.Locomotion)
			{
				Body->PlayAnimation(A.Locomotion, true);
				CurrentClip = A.Locomotion;
			}
			if (UAnimSingleNodeInstance* I = Body->GetSingleNodeInstance())
			{
				const float AxisVal = FMath::Min(SpeedCm, BlendAxisMax);
				I->SetBlendSpacePosition(FVector(AxisVal, 0, 0));
				I->SetPlayRate(SpeedCm > BlendAxisMax ? FMath::Clamp(SpeedCm / BlendAxisMax, 1.f, 1.9f) : 1.f);
			}
		}
	}
	// lean into acceleration / sprint
	const float Lean = FMath::Clamp(SpeedYds / FMath::Max(1.f, VMaxYds), 0.f, 1.f) * 8.f;
	Body->SetRelativeRotation(FRotator(0, -90, 0) + FRotator(0, 0, -Lean));
	(void)AnimT;
}

void AAthlete::SetSelected(bool bSel)
{
	Ring->SetVisibility(bSel);
}

void AAthlete::SetLabel(const FString& Text)
{
	Label->SetVisibility(!Text.IsEmpty());
	Label->SetText(FText::FromString(Text));
}
