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
#include "Engine/Texture.h"
#include "Misc/CommandLine.h"
#include "Retargeter/IKRetargeter.h"
#include "Render/TagRetargetAnimInstance.h"
#include "Render/TagEditorLib.h"
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
		Idle = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop")), false);
		Jump = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Jump/M_Neutral_Jump_F_Land_Roll_Lfoot.M_Neutral_Jump_F_Land_Roll_Lfoot")), false);
		UniformMat = LoadAsset<UMaterialInterface>(*(G + TEXT("Materials/MI_UEFN_Mannequin_CMC.MI_UEFN_Mannequin_CMC")), false);
		// safety net for blend spaces authored headlessly (see UTagEditorLib::ResampleBlendSpace)
		UTagEditorLib::ResampleBlendSpace(Locomotion);
		UTagEditorLib::ResampleBlendSpace(Crouch);
		LocoMaxSpeed = FMath::Max(100.f, Locomotion->GetBlendParameter(1).Max);
		if (Crouch) CrouchMaxSpeed = FMath::Max(50.f, Crouch->GetBlendParameter(1).Max);
		UE_LOG(LogTemp, Display, TEXT("TAG: mocap athletes enabled (loco max %.0f cm/s, crouch %.0f)"), LocoMaxSpeed, CrouchMaxSpeed);

		// MetaHuman bodies (built by Tools/Art/mh_create.py), driven by runtime IK retargeting.
		if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoMH")))
		{
			static const TCHAR* Names[3] = {TEXT("TAG_Lineman"), TEXT("TAG_Skill"), TEXT("TAG_Big")};
			static const float Heights[3] = {196.f, 185.f, 191.f};
			static const float Depths[3] = {1.25f, 1.f, 1.12f};
			const FString Rig = TEXT("/Game/MetaHumans/Common/Common/Rigs/");
			MHUniformMat = LoadAsset<UMaterialInterface>(TEXT("/Game/TAG/Materials/M_TagMHUniform.M_TagMHUniform"), false);
			bool bAll = MHUniformMat != nullptr;
			for (int32 i = 0; i < 3 && bAll; ++i)
			{
				const FString B = FString::Printf(TEXT("/Game/TAG/MetaHumans/Unpacked/%s/"), Names[i]);
				FMHArchetype& M = MH[i];
				M.Body = LoadAsset<USkeletalMesh>(*FString::Printf(TEXT("%sBody/SKM_%s_BodyMesh.SKM_%s_BodyMesh"), *B, Names[i], Names[i]), false);
				M.Face = LoadAsset<USkeletalMesh>(*FString::Printf(TEXT("%sFace/SKM_%s_FaceMesh.SKM_%s_FaceMesh"), *B, Names[i], Names[i]), false);
				M.SkinBC = LoadAsset<UTexture>(*(B + TEXT("Body/Baked/T_Body_BC.T_Body_BC")), false);
				M.SkinN = LoadAsset<UTexture>(*(B + TEXT("Body/Baked/T_Body_N.T_Body_N")), false);
				// retargeter auto-aligned to these exact bodies (Tools/Art/mh_retarget.py); Epic's GASP one as fallback
				M.Retargeter = LoadAsset<UIKRetargeter>(TEXT("/Game/TAG/Anim/RTG_TAG_UEFN_to_MH.RTG_TAG_UEFN_to_MH"), false);
				if (!M.Retargeter)
					M.Retargeter = LoadAsset<UIKRetargeter>(*(Rig + (i == 1 ? TEXT("RTG_UEFN_to_Metahuman_nrw.RTG_UEFN_to_Metahuman_nrw") : TEXT("RTG_UEFN_to_Metahuman_ovw.RTG_UEFN_to_Metahuman_ovw"))), false);
				M.HeightCm = Heights[i];
				M.Depth = Depths[i];
				bAll = M.Body && M.Face && M.Retargeter;
				for (UObject* O : {(UObject*)M.Body.Get(), (UObject*)M.Face.Get(), (UObject*)M.Retargeter.Get(), (UObject*)M.SkinBC.Get(), (UObject*)M.SkinN.Get()})
					if (O) O->AddToRoot();
			}
			if (MHUniformMat) MHUniformMat->AddToRoot();
			bMetaHumans = bAll;
			UE_LOG(LogTemp, Display, TEXT("TAG: MetaHuman athletes %s"), bMetaHumans ? TEXT("enabled") : TEXT("unavailable (run Tools/Art/mh_create.py)"));
		}
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

int32 FAthleteAssets::ArchetypeFor(int32 PosIndex)
{
	switch (static_cast<tag::Pos>(PosIndex))
	{
	case tag::Pos::OT: case tag::Pos::OG: case tag::Pos::C: case tag::Pos::DE: case tag::Pos::DT:
		return 0;
	case tag::Pos::QB: case tag::Pos::TE: case tag::Pos::LB: case tag::Pos::FB:
		return 2;
	default:
		return 1;
	}
}

/** Reference-pose (bind) position of a bone in mesh space. */
static FVector RefBonePos(const FReferenceSkeleton& R, const TCHAR* Bone)
{
	int32 I = R.FindBoneIndex(FName(Bone));
	FTransform T = FTransform::Identity;
	const TArray<FTransform>& L = R.GetRefBonePose();
	while (I != INDEX_NONE)
	{
		T = T * L[I];
		I = R.GetParentIndex(I);
	}
	return T.GetLocation();
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
	const int32 Arch = FAthleteAssets::ArchetypeFor(static_cast<int32>(InPlayer->pos));
	// stable per-player style ("signature"): arm slot and timing/height variation
	{
		uint32 H = static_cast<uint32>(InPlayer->id) * 2654435761u;
		ArmSlot = static_cast<float>((H >> 8) % 1000) / 500.f - 1.f;
		StyleJitter = static_cast<float>((H >> 18) % 1000) / 500.f - 1.f;
	}
	float Depth = 1.f;
	if (A.bMetaHumans)
	{
		// the mannequin only plays the mocap now; the MetaHuman is what you see
		Body->SetRelativeScale3D(FVector::OneVector);
		Body->SetForcedLOD(1);
		if (FParse::Param(FCommandLine::Get(), TEXT("TagShowSource")))
		{
			// debug: show the mocap mannequin beside its MetaHuman
			BodyRelLoc = FVector(0, 90, 0);
			Body->SetRelativeLocation(BodyRelLoc);
		}
		else
		{
			Body->SetVisibility(false);
			Body->SetCastShadow(false);
		}
		SetupMetaHuman(Arch, U);
		Depth = A.MH[Arch].Depth;
	}
	else
	{
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
	}
	USkeletalMeshComponent* Vis = Visible();
	// helmet shell + facemask on the head bone
	Helmet->SetStaticMesh(A.Sphere);
	Helmet->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	Helmet->SetRelativeLocation(FVector(10, 1, 0));
	Helmet->SetRelativeScale3D(FVector(0.28f, 0.26f, 0.25f));
	Helmet->SetMaterial(0, TagColorMat(this, U.Helmet, 0.15f));
	// facemask: a cage of thin bars in front of the face (head bone: +X up, +Y forward, Z lateral)
	UMaterialInstanceDynamic* MaskMat = TagColorMat(this, FLinearColor(0.72f, 0.72f, 0.74f), 0.25f);
	for (UStaticMeshComponent* G : Gear)
		if (G) G->DestroyComponent();
	Gear.Reset();
	auto AddGear = [&](UStaticMesh* Mesh, const TCHAR* Bone, const FVector& Loc, const FRotator& Rot, const FVector& Scale, UMaterialInterface* Mat)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Vis, FName(Bone));
		C->RegisterComponent();
		C->SetRelativeLocationAndRotation(Loc, Rot);
		C->SetRelativeScale3D(Scale);
		C->SetMaterial(0, Mat);
		Gear.Add(C);
		return C;
	};
	// center vertical bar (reuses the Facemask component)
	Facemask->SetStaticMesh(A.Cylinder);
	Facemask->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	Facemask->SetRelativeLocationAndRotation(FVector(3.5f, 15.5f, 0), FRotator(90, 0, 0));
	Facemask->SetRelativeScale3D(FVector(0.011f, 0.011f, 0.10f));
	Facemask->SetMaterial(0, MaskMat);
	for (float X : {-1.f, 3.5f, 8.f}) // chin, mouth, nose bars
		AddGear(A.Cylinder, TEXT("head"), FVector(X, 15.f, 0), FRotator::ZeroRotator, FVector(0.011f, 0.011f, 0.17f), MaskMat);
	for (float Z : {-8.5f, 8.5f}) // side struts back to the shell
		AddGear(A.Cylinder, TEXT("head"), FVector(3.5f, 13.f, Z), FRotator(90, 0, 0), FVector(0.011f, 0.011f, 0.11f), MaskMat);
	// shoulder pads under the jersey: chest/back plate plus a cap over each shoulder
	const float PadK = A.bMetaHumans ? Depth : FMath::Sqrt(Bulk);
	Pads->SetStaticMesh(A.Sphere);
	Pads->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("spine_05"));
	Pads->SetRelativeLocation(FVector(6, 0, 0));
	Pads->SetRelativeScale3D(FVector(0.13f, 0.29f * PadK, 0.44f * FMath::Sqrt(PadK)));
	UMaterialInstanceDynamic* PadMat = TagColorMat(this, U.Jersey, 0.7f);
	Pads->SetMaterial(0, PadMat);
	if (A.bMetaHumans)
	{
		for (const TCHAR* Bone : {TEXT("upperarm_l"), TEXT("upperarm_r")})
			AddGear(A.Sphere, Bone, FVector(2, 0, 0), FRotator::ZeroRotator, FVector(0.19f, 0.2f, 0.2f) * FMath::Sqrt(PadK), PadMat);
	}
	// numbers
	const FText Num = FText::AsNumber(InPlayer->number);
	for (UTextRenderComponent* T : {BackNumber.Get(), FrontNumber.Get()})
	{
		T->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("spine_04"));
		T->SetText(Num);
		T->SetTextRenderColor(U.Number.ToFColor(true));
		T->SetWorldSize(T == BackNumber ? 26.f : 16.f);
	}
	BackNumber->SetRelativeLocation(FVector(6, -19 * Depth, 0));
	BackNumber->SetRelativeRotation(FRotator(0, 90, 90));
	FrontNumber->SetRelativeLocation(FVector(6, 17 * Depth, 0));
	// (0,-90,90) faces into the chest; spin it about its own up axis so it reads from the front
	FrontNumber->SetRelativeRotation((FRotator(0, -90, 90).Quaternion() * FQuat(FVector::ZAxisVector, PI)).Rotator());
	if (A.bMetaHumans)
	{
		// cleats: rounded black shoes fitted to each foot from the bind pose (heel -> ball of the foot)
		const FReferenceSkeleton& R = Vis->GetSkeletalMeshAsset()->GetRefSkeleton();
		UMaterialInstanceDynamic* ShoeMat = TagColorMat(this, FLinearColor(0.012f, 0.012f, 0.014f), 0.35f);
		for (const TCHAR* Side : {TEXT("l"), TEXT("r")})
		{
			const FName FootN(*FString::Printf(TEXT("foot_%s"), Side));
			const FName BallN(*FString::Printf(TEXT("ball_%s"), Side));
			const int32 FI = R.FindBoneIndex(FootN), BI = R.FindBoneIndex(BallN);
			if (FI == INDEX_NONE || BI == INDEX_NONE) continue;
			auto CS = [&R](int32 I)
			{
				FTransform T = FTransform::Identity;
				while (I != INDEX_NONE) { T = T * R.GetRefBonePose()[I]; I = R.GetParentIndex(I); }
				return T;
			};
			const FTransform FootCS = CS(FI);
			const FVector Ankle = FootCS.GetLocation(), Ball = CS(BI).GetLocation();
			FVector Fwd = Ball - Ankle;
			Fwd.Z = 0;
			Fwd = Fwd.GetSafeNormal();
			const FVector Center = FVector((Ankle.X + Ball.X) * 0.5f, (Ankle.Y + Ball.Y) * 0.5f, 5.f) + Fwd * 1.5f;
			// sphere: X across, Y along the foot, Z up (sizes in metres of a 1 m sphere)
			const FTransform Want(FRotationMatrix::MakeFromYZ(Fwd, FVector::UpVector).ToQuat(), Center);
			const FTransform Rel = Want.GetRelativeTransform(FootCS);
			AddGear(A.Sphere, *FootN.ToString(), Rel.GetLocation(), Rel.Rotator(), FVector(0.115f, 0.30f, 0.12f), ShoeMat);
		}
	}
	Ring->SetStaticMesh(A.Cylinder);
	Ring->SetRelativeScale3D(FVector(1.3f, 1.3f, 0.01f));
	Ring->SetRelativeLocation(FVector(0, 0, 1));
	Ring->SetMaterial(0, TagColorMat(this, FLinearColor(1.f, 0.83f, 0.f), 0.5f));
	CurrentAnim.Reset();
	CurrentClip = nullptr;
}

void AAthlete::SetupMetaHuman(int32 Arch, const FUniform& U)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const FMHArchetype& M = A.MH[Arch];
	if (!MHBody)
	{
		MHBody = NewObject<USkeletalMeshComponent>(this, TEXT("MHBody"));
		MHBody->SetupAttachment(Root);
		MHBody->RegisterComponent();
		MHFace = NewObject<USkeletalMeshComponent>(this, TEXT("MHFace"));
		MHFace->SetupAttachment(MHBody);
		MHFace->RegisterComponent();
		AddInstanceComponent(MHBody);
		AddInstanceComponent(MHFace);
	}
	MHBody->SetSkeletalMesh(M.Body);
	MHBody->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0, -90, 0)); // MetaHumans face +Y
	// fine-tune to the player's listed height around the archetype (proportions come from the body type)
	const float HeightCm = Player ? static_cast<float>(Player->heightIn * 2.54) : M.HeightCm;
	VisRelScale = FVector(FMath::Clamp(HeightCm / M.HeightCm, 0.94f, 1.06f));
	MHBody->SetRelativeScale3D(VisRelScale);
	MHBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MHBody->bCastDynamicShadow = true;
	MHBody->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	MHBody->SetAnimInstanceClass(UTagRetargetAnimInstance::StaticClass());
	if (UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()))
	{
		RI->Configure(M.Retargeter, Body);
	}
	MHBody->AddTickPrerequisiteComponent(Body); // mocap pose first, then retarget

	MHFace->SetSkeletalMesh(M.Face);
	MHFace->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MHFace->SetLeaderPoseComponent(MHBody);

	// uniform painted by body region; boundaries measured from this body's skeleton
	if (A.MHUniformMat)
	{
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(A.MHUniformMat, this);
		const FReferenceSkeleton& R = M.Body->GetRefSkeleton();
		const FVector Pelvis = RefBonePos(R, TEXT("pelvis"));
		const FVector Neck = RefBonePos(R, TEXT("neck_01"));
		const FVector Knee = RefBonePos(R, TEXT("calf_l"));
		const FVector Ankle = RefBonePos(R, TEXT("foot_l"));
		const FVector Shoulder = RefBonePos(R, TEXT("upperarm_l"));
		const FVector Elbow = RefBonePos(R, TEXT("lowerarm_l"));
		const FVector Hand = RefBonePos(R, TEXT("hand_l"));
		const FVector Thigh = RefBonePos(R, TEXT("thigh_l"));
		Mat->SetScalarParameterValue(TEXT("WaistZ"), Pelvis.Z + 7.f);
		Mat->SetScalarParameterValue(TEXT("NeckZ"), Neck.Z + 1.f);
		Mat->SetScalarParameterValue(TEXT("KneeZ"), Knee.Z - 7.f);
		Mat->SetScalarParameterValue(TEXT("ShoeZ"), Ankle.Z + 4.f);
		const float Sh = FMath::Abs(Shoulder.X), El = FMath::Abs(Elbow.X);
		Mat->SetScalarParameterValue(TEXT("SleeveX"), Sh + 0.42f * (El - Sh));
		Mat->SetScalarParameterValue(TEXT("ThighX"), FMath::Abs(Thigh.X));
		Mat->SetScalarParameterValue(TEXT("LegX"), FMath::Abs(Thigh.X) + 17.f);
		Mat->SetScalarParameterValue(TEXT("LegY"), Thigh.Y);
		Mat->SetScalarParameterValue(TEXT("HandX"), FMath::Abs(Hand.X) - 3.f);
		if (M.SkinBC) Mat->SetTextureParameterValue(TEXT("SkinBC"), M.SkinBC);
		if (M.SkinN) Mat->SetTextureParameterValue(TEXT("SkinN"), M.SkinN);
		Mat->SetVectorParameterValue(TEXT("Jersey"), U.Jersey);
		Mat->SetVectorParameterValue(TEXT("Pants"), U.Pants);
		Mat->SetVectorParameterValue(TEXT("Trim"), U.Trim);
		Mat->SetVectorParameterValue(TEXT("Socks"), U.Helmet);
		Mat->SetVectorParameterValue(TEXT("Shoe"), FLinearColor(0.015f, 0.015f, 0.015f));
		Mat->SetVectorParameterValue(TEXT("Glove"), FLinearColor(0.03f, 0.03f, 0.035f));
		for (int32 i = 0; i < MHBody->GetNumMaterials(); ++i) MHBody->SetMaterial(i, Mat);
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogTemp, Display, TEXT("TAG: MH regions pelvis=%s neck=%s knee=%s ankle=%s shoulder=%s elbow=%s hand=%s thigh=%s"),
				*Pelvis.ToString(), *Neck.ToString(), *Knee.ToString(), *Ankle.ToString(), *Shoulder.ToString(), *Elbow.ToString(), *Hand.ToString(), *Thigh.ToString());
		}
	}
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
	USkeletalMeshComponent* V = Visible();
	if (bRagdoll || !V->GetPhysicsAsset()) return;
	bRagdoll = true;
	V->SetCollisionProfileName(TEXT("Ragdoll"));
	V->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	V->SetAllBodiesSimulatePhysics(true);
	V->SetSimulatePhysics(true);
	V->WakeAllRigidBodies();
	// carry the player's momentum into the fall
	V->SetAllPhysicsLinearVelocity(VelWorld * 0.85f + FVector(0, 0, -50));
}

void AAthlete::ResetPose()
{
	if (!bRagdoll) return;
	bRagdoll = false;
	USkeletalMeshComponent* V = Visible();
	V->SetSimulatePhysics(false);
	V->SetAllBodiesSimulatePhysics(false);
	V->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	V->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	V->SetRelativeLocationAndRotation(BodyRelLoc, BodyRelRot);
	if (MHBody) MHBody->SetRelativeScale3D(VisRelScale);
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
	// standing still: play the idle clip itself (the blend space's zero-speed row leaves the rig in its bind pose)
	if (!bCrouched && A.Idle && SpeedCm < 20.f && SmoothedSpeed < 40.f)
	{
		PlayClip(A.Idle, true, 1.f);
		return;
	}
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

FVector AAthlete::BoneWorld(FName Bone) const
{
	const USkeletalMeshComponent* V = Visible();
	return V && V->GetBoneIndex(Bone) != INDEX_NONE ? V->GetSocketLocation(Bone) : GetActorLocation() + FVector(0, 0, 120);
}

void AAthlete::SetFootball(const FAthleteFootball& F, float Dt)
{
	if (!MHBody) return;
	UTagRetargetAnimInstance* I = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance());
	if (!I) return;
	auto Ease = [Dt](float& V, float Target, float Rate) { V = FMath::FInterpTo(V, Target, Dt, Rate); };
	const bool bThrow = F.ThrowT >= 0.f;
	Ease(CarryA, F.bHasBall && !F.bSecure && !bThrow ? 1.f : 0.f, 9.f);
	Ease(SecureA, F.bSecure && !bThrow ? 1.f : 0.f, 9.f);
	Ease(ReachA, F.bReach ? 1.f : 0.f, F.bReach ? 7.f : 4.f);
	Ease(LeanA, F.Lean, 5.f);
	Ease(StanceA, F.bStance ? 1.f : 0.f, 6.f);
	Ease(LookA, F.bLook ? 0.85f : 0.f, 4.f);
	const FTransform& C = MHBody->GetComponentTransform();
	if (F.bReach) ReachSmoothed = ReachSmoothed.IsZero() ? F.ReachWorld : FMath::VInterpTo(ReachSmoothed, F.ReachWorld, Dt, 18.f);
	if (F.bLook) LookSmoothed = LookSmoothed.IsZero() ? F.LookWorld : FMath::VInterpTo(LookSmoothed, F.LookWorld, Dt, 8.f);
	FTagProcInput& P = I->Proc;
	P.ThrowT = F.ThrowT;
	P.CarryAlpha = CarryA;
	P.SecureAlpha = SecureA;
	P.ReachAlpha = ReachA;
	P.ReachTarget = C.InverseTransformPosition(ReachSmoothed);
	P.ReachSpread = F.ReachSpread;
	P.LeanAlpha = LeanA;
	P.StanceAlpha = StanceA;
	P.LookAlpha = LookA;
	P.LookTarget = C.InverseTransformPosition(LookSmoothed);
	P.ArmSlot = ArmSlot;
	P.StyleJitter = StyleJitter;
}

bool AAthlete::GetBallHold(FVector& OutLoc, FRotator& OutRot) const
{
	if (!MHBody) return false;
	const FVector HR = MHBody->GetSocketLocation(TEXT("hand_r"));
	const FVector HL = MHBody->GetSocketLocation(TEXT("hand_l"));
	const FVector LR = MHBody->GetSocketLocation(TEXT("lowerarm_r"));
	const FVector Fwd = GetActorForwardVector();
	if (SecureA > 0.5f)
	{
		OutLoc = (HR + HL) * 0.5f + Fwd * 4.f;
		OutRot = GetActorRotation() + FRotator(25, 0, 0);
	}
	else
	{
		// palm side of the right hand, long axis along the forearm (tucked: nose up and forward)
		const FVector Along = (HR - LR).GetSafeNormal();
		OutLoc = HR + Along * 6.f + Fwd * 3.f - GetActorRightVector() * 2.f;
		OutRot = Along.Rotation();
	}
	return true;
}

FString AAthlete::DebugAnimState() const
{
	const UAnimSingleNodeInstance* I = Body->GetSingleNodeInstance();
	const int32 HI = Body->GetBoneIndex(TEXT("hand_l"));
	const FVector Hand = HI != INDEX_NONE && Body->GetComponentSpaceTransforms().IsValidIndex(HI) ? Body->GetComponentSpaceTransforms()[HI].GetLocation() : FVector::ZeroVector;
	FString Fb;
	if (MHBody)
		if (const UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()))
			Fb = FString::Printf(TEXT(" | throw=%.2f carry=%.2f secure=%.2f reach=%.2f@%s lean=%.2f look=%.2f handR=%s"),
				RI->Proc.ThrowT, RI->Proc.CarryAlpha, RI->Proc.SecureAlpha, RI->Proc.ReachAlpha, *RI->Proc.ReachTarget.ToCompactString(),
				RI->Proc.LeanAlpha, RI->Proc.LookAlpha, *MHBody->GetComponentTransform().InverseTransformPosition(MHBody->GetSocketLocation(TEXT("hand_r"))).ToCompactString());
	return Fb + FString::Printf(TEXT(" anim=%s mode=%d inst=%s asset=%s playing=%d pos=%.2f rate=%.2f hand_l=%s ticking=%d vis=%d"),
		*CurrentAnim, static_cast<int32>(Body->GetAnimationMode()), Body->GetAnimInstance() ? *Body->GetAnimInstance()->GetClass()->GetName() : TEXT("none"),
		I && I->GetAnimationAsset() ? *I->GetAnimationAsset()->GetName() : TEXT("none"), I ? I->IsPlaying() : -1, I ? I->GetCurrentTime() : -1.f,
		I ? I->GetPlayRate() : -1.f, *Hand.ToString(), Body->IsComponentTickEnabled(), Body->IsVisible());
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
