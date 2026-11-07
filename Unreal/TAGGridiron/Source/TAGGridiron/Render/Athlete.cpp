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
#include "PhysicsEngine/PhysicsAsset.h"
#include "Sim/Types.h"

FAthleteAssets& FAthleteAssets::Get()
{
	static FAthleteAssets A;
	return A;
}

template <class T>
static T* LoadAsset(const TCHAR* Path, bool bWarn = true)
{
	T* Obj = LoadObject<T>(nullptr, Path, nullptr, bWarn ? LOAD_None : (LOAD_NoWarn | LOAD_Quiet));
	if (!Obj && bWarn) UE_LOG(LogTemp, Warning, TEXT("TAG: missing asset %s"), Path);
	return Obj;
}

void FAthleteAssets::Load()
{
	if (bLoaded) return;
	bLoaded = true;
	Sphere = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Cylinder = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Plane = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane.Plane"));
	ShapeMat = LoadAsset<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	TurfMat = LoadAsset<UMaterialInterface>(TEXT("/Game/TAG/Materials/M_TagTurf.M_TagTurf"), false);
	CrowdMat = LoadAsset<UMaterialInterface>(TEXT("/Game/TAG/Materials/M_TagCrowd.M_TagCrowd"), false);

	// Preferred: Epic's Game Animation Sample motion capture on the UEFN mannequin.
	const FString G = TEXT("/Game/Characters/UEFN_Mannequin/");
	USkeletalMesh* MocapMesh = LoadAsset<USkeletalMesh>(*(G + TEXT("Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin")), false);
	UBlendSpace* MocapLoco = LoadAsset<UBlendSpace>(TEXT("/Game/TAG/Anim/BS_TAG_Locomotion.BS_TAG_Locomotion"), false);
	if (MocapMesh && MocapLoco)
	{
		bMocap = true;
		Mesh = MocapMesh;
		Locomotion = MocapLoco;
		Crouch = LoadAsset<UBlendSpace>(TEXT("/Game/TAG/Anim/BS_TAG_Crouch.BS_TAG_Crouch"), false);
		Shove = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Interactions/Shoves/M_relaxed_ragdoll_shove_stand_F_A.M_relaxed_ragdoll_shove_stand_F_A")), false);
		Throw = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Idle/M_Neutral_Stand_Turn_045_R.M_Neutral_Stand_Turn_045_R")), false);
		Celebrate = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Idle/M_Neutral_Stand_Idle_Break_v02.M_Neutral_Stand_Idle_Break_v02")), false);
		Jump = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Jump/M_Neutral_Jump_F_Land_Roll_Lfoot.M_Neutral_Jump_F_Land_Roll_Lfoot")), false);
		UniformMat = LoadAsset<UMaterialInterface>(*(G + TEXT("Materials/MI_UEFN_Mannequin_CMC.MI_UEFN_Mannequin_CMC")), false);
		LocoMaxSpeed = FMath::Max(100.f, Locomotion->GetBlendParameter(1).Max);
		if (Crouch) CrouchMaxSpeed = FMath::Max(50.f, Crouch->GetBlendParameter(1).Max);
		UE_LOG(LogTemp, Display, TEXT("TAG: mocap athletes enabled (loco max %.0f cm/s, crouch %.0f)"), LocoMaxSpeed, CrouchMaxSpeed);
		return;
	}
	// Fallback: template mannequin
	const FString A = TEXT("/Game/Characters/Mannequins/");
	Mesh = LoadAsset<USkeletalMesh>(*(A + TEXT("Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	Locomotion = LoadAsset<UBlendSpace>(*(A + TEXT("Anims/Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run")));
	FallFront = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Death/MM_Death_Front_01.MM_Death_Front_01")));
	FallFront2 = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Death/MM_Death_Front_02.MM_Death_Front_02")));
	Shove = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Unarmed/Attack/MM_Attack_02.MM_Attack_02")));
	Throw = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Unarmed/Attack/MM_Attack_03.MM_Attack_03")));
	Jump = LoadAsset<UAnimationAsset>(*(A + TEXT("Anims/Unarmed/Jump/MM_Jump.MM_Jump")));
	Celebrate = Jump;
	if (Locomotion) LocoMaxSpeed = FMath::Max(1.f, Locomotion->GetBlendParameter(0).Max);
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
	Body->SetRelativeRotation(FRotator(0, -90, 0)); // mannequins face +Y
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
	// body: scale to height and build (mannequins ~ 180 cm)
	const float HeightCm = static_cast<float>(InPlayer->heightIn * 2.54);
	const float S = HeightCm / 180.f;
	const float Bulk = FMath::Clamp(static_cast<float>(InPlayer->weightLb) / 215.f, 0.9f, 1.35f);
	if (A.Mesh) Body->SetSkeletalMesh(A.Mesh);
	Body->SetRelativeScale3D(FVector(S * FMath::Sqrt(Bulk), S * FMath::Sqrt(Bulk), S));
	BodyRelLoc = Body->GetRelativeLocation();
	BodyRelRot = Body->GetRelativeRotation();
	// uniform: two-tone jersey (primary) / pants (secondary)
	for (int32 i = 0; i < Body->GetNumMaterials(); ++i)
	{
		UMaterialInstanceDynamic* M = A.UniformMat ? UMaterialInstanceDynamic::Create(A.UniformMat, this) : Body->CreateDynamicMaterialInstance(i);
		if (!M) continue;
		if (A.UniformMat) Body->SetMaterial(i, M);
		M->SetVectorParameterValue(TEXT("Primary Color"), U.Jersey);
		M->SetVectorParameterValue(TEXT("Secondary Color"), U.Pants);
		M->SetVectorParameterValue(TEXT("Paint Tint"), i == 0 ? U.Jersey : U.Pants);
	}
	// helmet shell + facemask on the head bone
	Helmet->SetStaticMesh(A.Sphere);
	Helmet->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	Helmet->SetRelativeLocation(FVector(10, 1, 0));
	Helmet->SetRelativeScale3D(FVector(0.28f, 0.26f, 0.25f));
	Helmet->SetMaterial(0, TagColorMat(this, U.Helmet, 0.15f));
	Facemask->SetStaticMesh(A.Cube);
	Facemask->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	Facemask->SetRelativeLocation(FVector(5, 13, 0));
	Facemask->SetRelativeScale3D(FVector(0.12f, 0.03f, 0.2f));
	Facemask->SetMaterial(0, TagColorMat(this, FLinearColor(0.85f, 0.85f, 0.85f), 0.3f));
	// rounded shoulder pads
	Pads->SetStaticMesh(A.Sphere);
	Pads->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("spine_05"));
	Pads->SetRelativeLocation(FVector(8, 0, 0));
	Pads->SetRelativeScale3D(FVector(0.16f, 0.30f, 0.54f) * FMath::Sqrt(Bulk));
	Pads->SetMaterial(0, TagColorMat(this, U.Jersey, 0.7f));
	// numbers
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
	Ring->SetStaticMesh(A.Cylinder);
	Ring->SetRelativeScale3D(FVector(1.3f, 1.3f, 0.01f));
	Ring->SetRelativeLocation(FVector(0, 0, 1));
	Ring->SetMaterial(0, TagColorMat(this, FLinearColor(1.f, 0.83f, 0.f), 0.5f));
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

void AAthlete::StartRagdoll(const FVector& VelWorld)
{
	if (bRagdoll || !Body->GetPhysicsAsset()) return;
	bRagdoll = true;
	Body->SetCollisionProfileName(TEXT("Ragdoll"));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetSimulatePhysics(true);
	Body->WakeAllRigidBodies();
	// carry the player's momentum into the fall
	Body->SetAllPhysicsLinearVelocity(VelWorld * 0.85f + FVector(0, 0, -50));
}

void AAthlete::ResetPose()
{
	if (!bRagdoll) return;
	bRagdoll = false;
	Body->SetSimulatePhysics(false);
	Body->SetAllBodiesSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Body->SetRelativeLocationAndRotation(BodyRelLoc, BodyRelRot);
	CurrentClip = nullptr;
	CurrentAnim.Reset();
}

void AAthlete::Drive(const FVector& Location, float Yaw, const FVector& VelWorld, float VMaxYds, const FString& Anim, float AnimT, float DeltaTime, bool bLineman)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (bRagdoll) return; // physics owns the body until the next snap
	const bool bFall = Anim == TEXT("down") || Anim == TEXT("tackle") || Anim == TEXT("dive");
	SetActorLocation(Location);
	if (!bYawInit)
	{
		SmoothedYaw = Yaw;
		bYawInit = true;
	}
	SmoothedYaw += FMath::FindDeltaAngleDegrees(SmoothedYaw, Yaw) * FMath::Clamp(DeltaTime * 10.f, 0.f, 1.f);
	SetActorRotation(FRotator(0, SmoothedYaw, 0));

	const float SpeedCm = FVector(VelWorld.X, VelWorld.Y, 0).Size();
	const bool bChanged = Anim != CurrentAnim;
	CurrentAnim = Anim;

	if (A.bMocap && Anim == TEXT("down"))
	{
		StartRagdoll(VelWorld);
		return;
	}
	if (bFall)
	{
		if (A.bMocap) PlayClip(A.Jump, false, 1.4f); // diving lunge
		else if (bChanged) PlayClip(Player && (Player->id % 2) ? A.FallFront : A.FallFront2, false, 1.3f);
		return;
	}
	if (Anim == TEXT("throw"))
	{
		if (bChanged) PlayClip(A.Throw, false, 1.6f);
		return;
	}
	if (Anim == TEXT("celebrate"))
	{
		if (bChanged) PlayClip(A.Celebrate, true, 1.2f);
		return;
	}
	// locomotion: direction of travel relative to facing, speed-matched so feet don't slide
	const float VelYaw = SpeedCm > 25.f ? FMath::RadiansToDegrees(FMath::Atan2(VelWorld.Y, VelWorld.X)) : SmoothedYaw;
	const float Dir = SpeedCm > 25.f ? FMath::FindDeltaAngleDegrees(SmoothedYaw, VelYaw) : 0.f;
	SmoothedDir += FMath::FindDeltaAngleDegrees(SmoothedDir, Dir) * FMath::Clamp(DeltaTime * 8.f, 0.f, 1.f);
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, SpeedCm, DeltaTime, 10.f);
	const bool bCrouched = A.bMocap && A.Crouch && (Anim == TEXT("block") || (bLineman && SpeedCm < 260.f));
	UBlendSpace* BS = bCrouched ? A.Crouch.Get() : A.Locomotion.Get();
	const float MaxSp = bCrouched ? A.CrouchMaxSpeed : A.LocoMaxSpeed;
	if (!BS) return;
	if (CurrentClip != BS)
	{
		Body->PlayAnimation(BS, true);
		CurrentClip = BS;
	}
	if (UAnimSingleNodeInstance* I = Body->GetSingleNodeInstance())
	{
		const float AxisSpeed = FMath::Min(SmoothedSpeed, MaxSp);
		if (A.bMocap)
			I->SetBlendSpacePosition(FVector(SmoothedDir, AxisSpeed, 0));
		else
			I->SetBlendSpacePosition(FVector(AxisSpeed, 0, 0));
		// faster than the fastest clip (NFL top speed ~ 1000 cm/s): quicker strides
		I->SetPlayRate(SmoothedSpeed > MaxSp ? FMath::Clamp(SmoothedSpeed / MaxSp, 1.f, 1.6f) : 1.f);
	}
	(void)AnimT;
	(void)VMaxYds;
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
