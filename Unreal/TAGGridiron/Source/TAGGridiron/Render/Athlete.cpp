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
#include "Misc/PackageName.h"
#include "Retargeter/IKRetargeter.h"
#include "Render/TagRetargetAnimInstance.h"
#include "Render/TagMotionMatchInstance.h"
#include "Render/TagFaceAnimInstance.h"
#include "PoseSearch/PoseSearchDatabase.h"
#if WITH_EDITOR
#include "PoseSearch/PoseSearchDerivedData.h"
#endif
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
	// FAthleteAssets is a plain static struct, so its TObjectPtrs do not keep assets alive. Without rooting, a clip
	// that nothing is playing (Throw, Jump, FallFront) is garbage collected at the first GC pass (~60 s) and the next
	// PlayClip uses a dangling pointer (the recurring null access in PlayClip -> PlayAnimation).
	if (Obj) Obj->AddToRoot();
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
	if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoGear"))) // -TagNoGear: primitive fallback, for A/B checks
	{
		HelmetMesh = LoadAsset<UStaticMesh>(TEXT("/Game/TAG/Gear/SM_TAG_Helmet.SM_TAG_Helmet"), false);
		FacemaskMesh[0] = LoadAsset<UStaticMesh>(TEXT("/Game/TAG/Gear/SM_TAG_Facemask_Open.SM_TAG_Facemask_Open"), false);
		FacemaskMesh[1] = LoadAsset<UStaticMesh>(TEXT("/Game/TAG/Gear/SM_TAG_Facemask_Std.SM_TAG_Facemask_Std"), false);
		FacemaskMesh[2] = LoadAsset<UStaticMesh>(TEXT("/Game/TAG/Gear/SM_TAG_Facemask_Cage.SM_TAG_Facemask_Cage"), false);
		if (!FacemaskMesh[0]) HelmetMesh = nullptr; // need at least one mask style to use the TAG helmet
		PadsMesh = LoadAsset<UStaticMesh>(TEXT("/Game/TAG/Gear/SM_TAG_ShoulderPads.SM_TAG_ShoulderPads"), false);
		FootballMesh = LoadAsset<UStaticMesh>(TEXT("/Game/TAG/Gear/SM_TAG_Football.SM_TAG_Football"), false);
	}
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
		// TAG hand keyed football clips (no mocap): Tools/Anim -> Tools/Art/import_tag_anims.py
		const FString K = TEXT("/Game/TAG/Anim/Authored/");
		auto Keyed = [&](const TCHAR* N) { return LoadAsset<UAnimationAsset>(*FString::Printf(TEXT("%s%s.%s"), *K, N, N), false); };
		Stance3pt = Keyed(TEXT("A_TAG_Stance3pt_Loop"));
		Stance2pt = Keyed(TEXT("A_TAG_Stance2pt_Loop"));
		StanceShotgun = Keyed(TEXT("A_TAG_QB_Shotgun_Loop"));
		ThrowKeyed = Keyed(TEXT("A_TAG_QB_Throw"));
		GetOff = Keyed(TEXT("A_TAG_Lineman_GetOff"));
		TackleKeyed = Keyed(TEXT("A_TAG_Tackle_Wrap"));
		TackleHigh = Keyed(TEXT("A_TAG_Tackle_High"));
		TackleLow = Keyed(TEXT("A_TAG_Tackle_Low"));
		TackleDive = Keyed(TEXT("A_TAG_Tackle_Dive"));
		JukeL = Keyed(TEXT("A_TAG_Juke_L"));
		JukeR = Keyed(TEXT("A_TAG_Juke_R"));
		Kneel = Keyed(TEXT("A_TAG_QB_Kneel"));
		BlockEngaged = Keyed(TEXT("A_TAG_Block_Engaged_Loop"));
		BlockPassSet = Keyed(TEXT("A_TAG_Block_PassSet_Loop"));
		SnapCatch = Keyed(TEXT("A_TAG_QB_SnapCatch"));
		Handoff = Keyed(TEXT("A_TAG_QB_Handoff"));
		GetUpDown = Keyed(TEXT("A_TAG_GetUp_FaceDown"));
		GetUpUp = Keyed(TEXT("A_TAG_GetUp_FaceUp"));
		ShedSwim = Keyed(TEXT("A_TAG_Shed_Swim"));
		ShedRip = Keyed(TEXT("A_TAG_Shed_Rip"));
		CatchHigh = Keyed(TEXT("A_TAG_Catch_High"));
		CatchChest = Keyed(TEXT("A_TAG_Catch_Chest"));
		CatchLow = Keyed(TEXT("A_TAG_Catch_Low"));
		CatchOverShoulder = Keyed(TEXT("A_TAG_Catch_OverShoulder"));
		CatchSidelineL = Keyed(TEXT("A_TAG_Catch_Sideline_L"));
		CatchSidelineR = Keyed(TEXT("A_TAG_Catch_Sideline_R"));
		SpinL = Keyed(TEXT("A_TAG_Spin_L"));
		SpinR = Keyed(TEXT("A_TAG_Spin_R"));
		StiffArmL = Keyed(TEXT("A_TAG_StiffArm_L"));
		StiffArmR = Keyed(TEXT("A_TAG_StiffArm_R"));
		Truck = Keyed(TEXT("A_TAG_Truck"));
		Hurdle = Keyed(TEXT("A_TAG_Hurdle"));
		RushBull = Keyed(TEXT("A_TAG_Rush_Bull"));
		RushSpinL = Keyed(TEXT("A_TAG_Rush_Spin_L"));
		RushSpinR = Keyed(TEXT("A_TAG_Rush_Spin_R"));
		RushClub = Keyed(TEXT("A_TAG_Rush_Club"));
		TackleShoulder = Keyed(TEXT("A_TAG_Tackle_Shoulder"));
		TackleArm = Keyed(TEXT("A_TAG_Tackle_Arm"));
		TackleAssist = Keyed(TEXT("A_TAG_Tackle_Assist"));
		Stance4pt = Keyed(TEXT("A_TAG_Stance4pt_Loop"));
		StanceRB = Keyed(TEXT("A_TAG_StanceRB_Loop"));
		StanceDB = Keyed(TEXT("A_TAG_StanceDB_Loop"));
		StanceLB = Keyed(TEXT("A_TAG_StanceLB_Loop"));
		StanceK = Keyed(TEXT("A_TAG_StanceK_Loop"));
		KickKickoff = Keyed(TEXT("A_TAG_Kick_Kickoff"));
		KickFieldGoal = Keyed(TEXT("A_TAG_Kick_FieldGoal"));
		KickPunt = Keyed(TEXT("A_TAG_Kick_Punt"));
		GetUpRoll = Keyed(TEXT("A_TAG_GetUp_Roll"));
		StanceUnderCenter = Keyed(TEXT("A_TAG_StanceUnderCenter_Loop"));
		UnderCenterSnap = Keyed(TEXT("A_TAG_QB_UnderCenterSnap"));
		Pitch = Keyed(TEXT("A_TAG_QB_Pitch"));
		CatchDive = Keyed(TEXT("A_TAG_Catch_Dive"));
		GetUpSlow = Keyed(TEXT("A_TAG_GetUp_Slow"));
		TackledHit = Keyed(TEXT("A_TAG_Tackled_Hit"));
		IdleHandsHips = Keyed(TEXT("A_TAG_Idle_HandsHips_Loop"));
		IdleHandsKnees = Keyed(TEXT("A_TAG_Idle_HandsKnees_Loop"));
		for (const TCHAR* N : {TEXT("A_TAG_Celebrate_Flex_Loop"), TEXT("A_TAG_Celebrate_PointUp_Loop"), TEXT("A_TAG_Celebrate_Leap_Loop"),
			TEXT("A_TAG_Celebrate_Shimmy_Loop"), TEXT("A_TAG_Celebrate_FirstDown_Loop")})
			if (UAnimationAsset* C = Keyed(N)) Celebrations.Add(C);
		Celebrate = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Idle/M_Neutral_Stand_Idle_Break_v02.M_Neutral_Stand_Idle_Break_v02")), false);
		if (UAnimationAsset* C = Keyed(TEXT("A_TAG_Celebrate_Loop"))) { Celebrate = C; bCelebrateKeyed = true; Celebrations.Insert(C, 0); } // after the GASP default
		Idle = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop")), false);
		Jump = LoadAsset<UAnimationAsset>(*(G + TEXT("Animations/Jump/M_Neutral_Jump_F_Land_Roll_Lfoot.M_Neutral_Jump_F_Land_Roll_Lfoot")), false);
		UniformMat = LoadAsset<UMaterialInterface>(*(G + TEXT("Materials/MI_UEFN_Mannequin_CMC.MI_UEFN_Mannequin_CMC")), false);
		// safety net for blend spaces authored headlessly (see UTagEditorLib::ResampleBlendSpace)
		UTagEditorLib::ResampleBlendSpace(Locomotion);
		UTagEditorLib::ResampleBlendSpace(Crouch);
		LocoMaxSpeed = FMath::Max(100.f, Locomotion->GetBlendParameter(1).Max);
		if (Crouch) CrouchMaxSpeed = FMath::Max(50.f, Crouch->GetBlendParameter(1).Max);
		UE_LOG(LogTemp, Display, TEXT("TAG: mocap athletes enabled (loco max %.0f cm/s, crouch %.0f)"), LocoMaxSpeed, CrouchMaxSpeed);
		// motion matched locomotion (on by default since it matched or beat the blend spaces on every -TagMoveTest route;
		// -TagNoMM for the blend spaces, e.g. to A/B)
		if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoMM"))) LoadMotionMatching();

		// MetaHuman bodies (built by Tools/Art/mh_create.py), driven by runtime IK retargeting.
		if (!FParse::Param(FCommandLine::Get(), TEXT("TagNoMH")))
		{
			// 0 OL, 1 skill (RB/WR/DB), 2 big (LB/TE/FB), 3 defensive line, 4 lean (QB/K/P). 0-2 are required;
			// 3 and 4 are optional and ArchetypeFor falls back until mh_create.py has built them.
			static const TCHAR* Names[MHCount] = {TEXT("TAG_Lineman"), TEXT("TAG_Skill"), TEXT("TAG_Big"), TEXT("TAG_DLine"), TEXT("TAG_Lean")};
			static const float Heights[MHCount] = {196.f, 185.f, 191.f, 193.f, 189.f};
			static const float Depths[MHCount] = {1.25f, 1.f, 1.12f, 1.2f, 0.98f};
			const FString Rig = TEXT("/Game/MetaHumans/Common/Common/Rigs/");
			MHUniformMat = LoadAsset<UMaterialInterface>(TEXT("/Game/TAG/Materials/M_TagMHUniform.M_TagMHUniform"), false);
			bool bAll = MHUniformMat != nullptr;
			for (int32 i = 0; i < MHCount && bAll; ++i)
			{
				if (i >= 3 && !FPackageName::DoesPackageExist(FString::Printf(TEXT("/Game/TAG/MetaHumans/Unpacked/%s/Body/SKM_%s_BodyMesh"), Names[i], Names[i])))
					continue; // optional body type not built yet
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
				if (i < 3) bAll = M.Body && M.Face && M.Retargeter;
				else if (!(M.Body && M.Face && M.Retargeter)) M = FMHArchetype(); // incomplete optional body: unused
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

void FAthleteAssets::LoadMotionMatching()
{
	// Epic's Game Animation Sample motion matching data (free, UEFN mannequin), Dense variant: every database is
	// searched against the same normalization set, so costs compare across databases
	const FString D = TEXT("/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/Dense/PSD_Dense_");
	auto Db = [&D](const FString& N) { return LoadAsset<UPoseSearchDatabase>(*FString::Printf(TEXT("%s%s.PSD_Dense_%s"), *D, *N, *N), false); };
	UPoseSearchDatabase* Idles = Db(TEXT("Stand_Idles"));
	UPoseSearchDatabase* Turn = Db(TEXT("Stand_TurnInPlace"));
	static const TCHAR* Gait[3] = {TEXT("Walk"), TEXT("Run"), TEXT("Sprint")};
	bool bOk = Idles != nullptr;
	TArray<UPoseSearchDatabase*> All;
	for (int32 g = 0; g < 3 && bOk; ++g)
	{
		MMMove[g].Reset();
		MMStart[g].Reset();
		MMPivot[g].Reset();
		MMStop[g].Reset();
		// loops: TAG's football copies when built (Tools/Art/mm_build_dbs.py: sprint without the acceleration
		// transitions + curve loops, run + arcs), else the sample's
		UPoseSearchDatabase* Loops = g > 0 ? LoadAsset<UPoseSearchDatabase>(*FString::Printf(TEXT("/Game/TAG/Anim/MM/PSD_TAG_%s_Loops.PSD_TAG_%s_Loops"), Gait[g], Gait[g]), false) : nullptr;
		if (!Loops) Loops = Db(FString::Printf(TEXT("Stand_%s_Loops"), Gait[g]));
		if (UPoseSearchDatabase* X = Loops) { MMMove[g].Add(X); MMStart[g].Add(X); MMPivot[g].Add(X); }
		if (UPoseSearchDatabase* X = Db(FString::Printf(TEXT("Stand_%s_Starts"), Gait[g]))) MMStart[g].Add(X);
		for (const TCHAR* K : {TEXT("Pivots"), TEXT("SpinTransition")})
			if (UPoseSearchDatabase* X = Db(FString::Printf(TEXT("Stand_%s_%s"), Gait[g], K))) MMPivot[g].Add(X);
		if (UPoseSearchDatabase* X = Db(FString::Printf(TEXT("Stand_%s_Stops"), Gait[g]))) MMStop[g].Add(X);
		MMStop[g].Add(Idles);
		// (no turn-in-place: standing players re-face through the actor's rotation; with the root offset, turn clips
		// fed back on themselves and the search flipped between left and right turns)
		(void)Turn;
		bOk = MMMove[g].Num() >= 1 && MMStart[g].Num() >= 2 && MMPivot[g].Num() >= 2 && MMStop[g].Num() >= 2;
		for (UPoseSearchDatabase* X : MMStart[g]) All.AddUnique(X);
		for (UPoseSearchDatabase* X : MMPivot[g]) All.AddUnique(X);
		for (UPoseSearchDatabase* X : MMStop[g]) All.AddUnique(X);
	}
	// crouched footwork: the sample's crouch walk (~1.5-2 m/s); linemen shuffle a little faster, covered by the speed multiplier
	if (bOk)
	{
		for (TArray<UPoseSearchDatabase*>& C : MMCrouch) C.Reset();
		UPoseSearchDatabase* CLoops = Db(TEXT("Crouch_Walk_Loops"));
		UPoseSearchDatabase* CIdles = Db(TEXT("Crouch_Idles"));
		if (CLoops && CIdles)
		{
			MMCrouch[0] = {CLoops};
			MMCrouch[1] = {CLoops};
			MMCrouch[2] = {CLoops};
			if (UPoseSearchDatabase* X = Db(TEXT("Crouch_Walk_Starts"))) MMCrouch[1].Add(X);
			if (UPoseSearchDatabase* X = Db(TEXT("Crouch_Walk_Pivots"))) MMCrouch[2].Add(X);
			if (UPoseSearchDatabase* X = Db(TEXT("Crouch_Walk_Stops"))) MMCrouch[3].Add(X);
			MMCrouch[3].Add(CIdles);
			for (const TArray<UPoseSearchDatabase*>& C : MMCrouch) for (UPoseSearchDatabase* X : C) All.AddUnique(X);
		}
	}
	MMAll = All;
	bMotionMatch = bOk;
	UE_LOG(LogTemp, Display, TEXT("TAG: motion matching %s (%d databases)"), bMotionMatch ? TEXT("enabled") : TEXT("unavailable"), All.Num());
}

bool FAthleteAssets::MotionMatchReady()
{
	if (!bMotionMatch) return false;
	if (bMMReady) return true;
#if WITH_EDITOR
	// uncooked runs build the search indexes through the DDC in the background (deferred until their animations have
	// loaded; minutes the first time, cached after that). Locomotion stays on the blend space until all are built.
	if (MMCheckedFrame == GFrameCounter) return false;
	MMCheckedFrame = GFrameCounter;
	using namespace UE::PoseSearch;
	int32 Done = 0;
	for (UPoseSearchDatabase* X : MMAll)
	{
		const EAsyncBuildIndexResult R = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(X, ERequestAsyncBuildFlag::ContinueRequest);
		if (R == EAsyncBuildIndexResult::Failed)
		{
			UE_LOG(LogTemp, Warning, TEXT("TAG: motion matching index failed for %s; staying on blend spaces"), *X->GetName());
			bMotionMatch = false;
			return false;
		}
		Done += R == EAsyncBuildIndexResult::Success;
	}
	if (Done < MMAll.Num()) return false;
#endif
	bMMReady = true;
	UE_LOG(LogTemp, Display, TEXT("TAG: motion matching ready (%d databases indexed, %.1f s after start)"), MMAll.Num(), FPlatformTime::Seconds() - GStartTime);
	return true;
}

int32 FAthleteAssets::ArchetypeFor(int32 PosIndex)
{
	const FAthleteAssets& A = Get();
	switch (static_cast<tag::Pos>(PosIndex))
	{
	case tag::Pos::OT: case tag::Pos::OG: case tag::Pos::C:
		return 0;
	case tag::Pos::DE: case tag::Pos::DT:
		return A.MH[3].Body ? 3 : 0;
	case tag::Pos::QB:
		return A.MH[4].Body ? 4 : 2;
	case tag::Pos::K: case tag::Pos::P:
		return A.MH[4].Body ? 4 : 1;
	case tag::Pos::TE: case tag::Pos::LB: case tag::Pos::FB:
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
	if (A.bMotionMatch)
	{
		// single node playback plus motion matched locomotion (the instance keeps the single node API)
		Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Body->SetAnimInstanceClass(UTagMotionMatchInstance::StaticClass());
	}
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
	UMaterialInstanceDynamic* MaskMat = TagColorMat(this, FLinearColor(0.72f, 0.72f, 0.74f), 0.25f);
	// helmet shell + facemask on the head bone (head bone: +X up, +Y forward, Z lateral)
	if (A.HelmetMesh)
	{
		// TAG gear meshes (Tools/Art/gear_build.py), authored in the head bone frame around the shell centre
		const FVector GearLoc(11.f, 1.f, 0.f);
		Helmet->SetStaticMesh(A.HelmetMesh);
		Helmet->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
		Helmet->SetRelativeLocationAndRotation(GearLoc, FRotator::ZeroRotator);
		Helmet->SetRelativeScale3D(FVector::OneVector);
		Helmet->SetMaterial(0, TagColorMat(this, U.Helmet, 0.12f));
		Helmet->SetMaterial(1, TagColorMat(this, U.Trim, 0.18f));
		const tag::Pos P = InPlayer->pos;
		const int32 Style = (P == tag::Pos::OT || P == tag::Pos::OG || P == tag::Pos::C || P == tag::Pos::DE || P == tag::Pos::DT) ? 2
			: (P == tag::Pos::RB || P == tag::Pos::FB || P == tag::Pos::TE || P == tag::Pos::LB) ? 1 : 0;
		UStaticMesh* MaskMesh = A.FacemaskMesh[Style] ? A.FacemaskMesh[Style].Get() : A.FacemaskMesh[0].Get();
		Facemask->SetStaticMesh(MaskMesh);
		Facemask->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
		Facemask->SetRelativeLocationAndRotation(GearLoc, FRotator::ZeroRotator);
		Facemask->SetRelativeScale3D(FVector::OneVector);
		Facemask->SetMaterial(0, MaskMat);
	}
	else
	{
	Helmet->SetStaticMesh(A.Sphere);
	Helmet->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	Helmet->SetRelativeLocation(FVector(10, 1, 0));
	Helmet->SetRelativeScale3D(FVector(0.28f, 0.26f, 0.25f));
	Helmet->SetMaterial(0, TagColorMat(this, U.Helmet, 0.15f));
	}
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
	if (!A.HelmetMesh)
	{
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
	}
	// shoulder pads under the jersey: chest/back plate plus a cap over each shoulder
	const float PadK = A.bMetaHumans ? Depth : FMath::Sqrt(Bulk);
	UMaterialInstanceDynamic* PadMat = TagColorMat(this, U.Jersey, 0.7f);
	Pads->AttachToComponent(Vis, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("spine_05"));
	if (A.PadsMesh)
	{
		// TAG pad mesh (Tools/Art/gear_build.py): yoke plus shoulder caps, authored in the spine_05 frame
		Pads->SetStaticMesh(A.PadsMesh);
		Pads->SetRelativeLocationAndRotation(FVector(2, 0, 0), FRotator::ZeroRotator);
		Pads->SetRelativeScale3D(FVector(1.f, PadK, FMath::Sqrt(PadK)));
	}
	else
	{
		Pads->SetStaticMesh(A.Sphere);
		Pads->SetRelativeLocation(FVector(6, 0, 0));
		Pads->SetRelativeScale3D(FVector(0.13f, 0.29f * PadK, 0.44f * FMath::Sqrt(PadK)));
	}
	Pads->SetMaterial(0, PadMat);
	if (A.bMetaHumans && !A.PadsMesh)
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
		T->SetVisibility(!bMaterialNumbers); // the MetaHuman uniform material draws the numbers itself
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
	// the face runs its own rig: copies the body's head and neck, drives the expression controls; Epic's RigLogic
	// post-process turns them into the facial joints (leader-pose following left every face frozen in neutral)
	MHFace->SetAnimInstanceClass(UTagFaceAnimInstance::StaticClass());
	if (UTagFaceAnimInstance* FI = Cast<UTagFaceAnimInstance>(MHFace->GetAnimInstance())) FI->Configure(MHBody);
	MHFace->AddTickPrerequisiteComponent(MHBody);
	MHFace->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	{
		const uint32 H = Player ? static_cast<uint32>(Player->id) * 2246822519u : 1u;
		NextBlink = 1.5f + static_cast<float>(H % 1000u) / 300.f; // players don't blink in unison
	}

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
		// jersey numbers drawn by the material from the TAG digit atlas (front and back)
		Mat->SetScalarParameterValue(TEXT("Number"), Player ? static_cast<float>(Player->number) : 0.f);
		Mat->SetVectorParameterValue(TEXT("NumColor"), U.Number);
		Mat->SetScalarParameterValue(TEXT("TorsoY"), 0.5f * (Pelvis.Y + Neck.Y));
		bMaterialNumbers = true;
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

void AAthlete::PlayOneShot(UAnimationAsset* Clip)
{
	if (!Clip || bRagdoll) return;
	OneShot = Clip;
	OneShotLeft = Clip->GetPlayLength();
	UE_LOG(LogTemp, Verbose, TEXT("TAG one-shot %s #%d"), *Clip->GetName(), Player ? Player->number : -1);
	CurrentClip = CurrentClip == Clip ? nullptr : CurrentClip; // restart if the same clip is already playing
}

void AAthlete::PlayClip(UAnimationAsset* Clip, bool bLoop, float Rate, float StartPos)
{
	if (!Clip) return;
	if (bInMatch || CurrentClip != Clip)
	{
		StartBodyAsset(Clip, bLoop);
		if (StartPos > 0.f)
			if (UAnimSingleNodeInstance* I = Body->GetSingleNodeInstance()) I->SetPosition(StartPos, false);
	}
	if (UAnimSingleNodeInstance* I = Body->GetSingleNodeInstance()) I->SetPlayRate(Rate);
}

void AAthlete::StartBodyAsset(UAnimationAsset* Asset, bool bLoop)
{
	if (UTagMotionMatchInstance* M = MatchInstance())
	{
		// PlayAnimation would swap in a plain single node instance; drive ours directly
		M->bMatch = false;
		M->SetAnimationAsset(Asset, bLoop);
		M->SetPlaying(true);
	}
	else
	{
		Body->PlayAnimation(Asset, bLoop);
	}
	// ease the visible body out of the previous clip or motion matched move (stance -> snap, throw -> run, ...)
	if ((CurrentClip || bInMatch) && MHBody)
		if (UTagRetargetAnimInstance* R = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance())) ++R->Proc.ClipSerial;
	bInMatch = false;
	CurrentClip = Asset;
}

UTagMotionMatchInstance* AAthlete::MatchInstance() const
{
	return FAthleteAssets::Get().bMotionMatch ? Cast<UTagMotionMatchInstance>(Body->GetAnimInstance()) : nullptr;
}

void AAthlete::RecordTrajectory()
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FTransform T = Body->GetComponentTransform();
	// a teleport (new play, get-up offset) is not motion: start the history over
	if (TrajPos.Num() && FVector::Dist2D(TrajPos.Last(), T.GetLocation()) > 150.f) { TrajPos.Reset(); TrajRot.Reset(); TrajTime.Reset(); }
	if (TrajTime.Num() && Now - TrajTime.Last() < 0.05) return;
	TrajPos.Add(T.GetLocation());
	TrajRot.Add(T.GetRotation());
	TrajTime.Add(Now);
	while (TrajTime.Num() > 2 && Now - TrajTime[0] > 1.0) { TrajPos.RemoveAt(0); TrajRot.RemoveAt(0); TrajTime.RemoveAt(0); }
}

bool AAthlete::DriveMotionMatching(const FVector& VelWorld, float SimYaw, float Dt, bool bCrouch)
{
	UTagMotionMatchInstance* M = MatchInstance();
	FAthleteAssets& A = FAthleteAssets::Get();
	if (!M || !A.MotionMatchReady()) return false;
	if (bCrouch && A.MMCrouch[0].IsEmpty()) return false; // no crouch data: the crouch blend space
	// velocity and acceleration measured from how the body actually moves (smoothed). The sim's velocity field goes
	// stale when play stops (dead ball: positions freeze, the velocity keeps its last value), which reads to the search
	// as "standing, about to sprint" and picked a start clip for every frame of it.
	const FVector Pos = Body->GetComponentLocation();
	if (bHavePrevVel && Dt > 1e-4f)
	{
		FVector Raw = (Pos - PrevPos) / Dt;
		Raw.Z = 0.f;
		if (Raw.Size() > 1500.f) Raw = PrevVel; // teleport
		const FVector NewVel = FMath::Lerp(PrevVel, Raw, FMath::Clamp(Dt * 15.f, 0.f, 1.f));
		const FVector Acc = ((NewVel - PrevVel) / Dt).GetClampedToMaxSize(1500.f);
		SmoothedAccel = FMath::Lerp(SmoothedAccel, Acc, FMath::Clamp(Dt * 6.f, 0.f, 1.f));
		PrevVel = NewVel;
	}
	else
	{
		PrevVel = FVector(VelWorld.X, VelWorld.Y, 0.f);
		SmoothedAccel = FVector::ZeroVector;
	}
	static const bool bNoAccel = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoAccel"));
	if (bNoAccel) SmoothedAccel = FVector::ZeroVector;
	PrevPos = Pos;
	bHavePrevVel = true;
	const FVector V = PrevVel;

	// trajectory of the source body component: recorded history, now, and 1.6 s of prediction
	// (accelerate for half a second, never brake through zero, top speed 10.5 m/s; facing keeps its offset from the
	// direction of travel, so a backpedal stays a backpedal and a runner turns with his path)
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FTransform Cur = Body->GetComponentTransform();
	const FQuat Rel = Body->GetRelativeRotation().Quaternion();
	FTransformTrajectory& Tr = M->Trajectory;
	Tr.Samples.Reset();
	for (int32 i = 0; i < TrajTime.Num(); ++i)
		if (Now - TrajTime[i] > 1e-3)
		{
			FTransformTrajectorySample S;
			S.Position = TrajPos[i];
			S.Facing = TrajRot[i];
			S.TimeInSeconds = static_cast<float>(TrajTime[i] - Now);
			Tr.Samples.Add(S);
		}
	{
		FTransformTrajectorySample S;
		S.Position = Cur.GetLocation();
		S.Facing = Cur.GetRotation();
		S.TimeInSeconds = 0.f;
		Tr.Samples.Add(S);
	}
	const float Speed = V.Size();
	const float VelYaw0 = Speed > 60.f ? FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)) : SimYaw;
	const float FaceOff = FMath::FindDeltaAngleDegrees(VelYaw0, SimYaw); // facing relative to travel
	FVector P = Cur.GetLocation(), Vel = V;
	float Yaw = SmoothedYaw;
	bool bStopped = false;
	float FutSpeed = Speed;
	const float H = 0.05f;
	for (int32 k = 1; k <= 32; ++k)
	{
		const float t = k * H;
		const FVector Acc = (t <= 0.5f && !bStopped) ? SmoothedAccel : FVector::ZeroVector;
		FVector Vn = Vel + Acc * H;
		if (Speed > 30.f && FVector::DotProduct(Vn, Vel) <= 0.f) { Vn = FVector::ZeroVector; bStopped = true; }
		Vn = Vn.GetClampedToMaxSize(1050.f);
		P += (Vel + Vn) * 0.5f * H;
		Vel = Vn;
		const float Sp = Vel.Size();
		const float TargetYaw = Sp > 100.f && Speed > 100.f ? FMath::RadiansToDegrees(FMath::Atan2(Vel.Y, Vel.X)) + FaceOff : SimYaw;
		Yaw += FMath::FindDeltaAngleDegrees(Yaw, TargetYaw) * FMath::Clamp(H * 10.f, 0.f, 1.f);
		if (k == 12) FutSpeed = Sp; // 0.6 s out
		if (k % 2 == 0)
		{
			FTransformTrajectorySample S;
			S.Position = P;
			S.Facing = FQuat(FRotator(0.f, Yaw, 0.f)) * Rel;
			S.TimeInSeconds = t;
			Tr.Samples.Add(S);
		}
	}

	// databases, the way the sample's chooser splits them: moving (loops, starts, plants, spins) or stopping / standing
	// (stops, idles, turns in place), by gait with hysteresis (sample speeds ~ walk 2, run 4.5, sprint 7 m/s)
	const float GaitSpeed = FMath::Max(Speed, FutSpeed);
	if (MMGait == 0 && GaitSpeed > 320.f) MMGait = 1;
	if (MMGait == 1 && GaitSpeed < 260.f) MMGait = 0;
	if (MMGait == 1 && GaitSpeed > 640.f) MMGait = 2;
	if (MMGait == 2 && GaitSpeed < 560.f) MMGait = 1;
	const bool bMoving = FutSpeed > 50.f || Speed > 450.f;
	MovingTime = bMoving ? MovingTime + Dt : 0.f;
	// pivot: the predicted path turns sharply in the next half second (cut, plant, break on the ball)
	bool bPivot = false;
	{
		const FTransformTrajectorySample S5 = Tr.GetSampleAtTime(0.5f), S4 = Tr.GetSampleAtTime(0.4f);
		const FVector VFut = (S5.Position - S4.Position) / 0.1f;
		if (Speed > 150.f && VFut.Size2D() > 100.f)
			bPivot = FVector::DotProduct(V.GetSafeNormal2D(), VFut.GetSafeNormal2D()) < 0.7f; // > ~45 deg
	}
	if (bCrouch)
		M->SetDatabases(!bMoving ? A.MMCrouch[3] : MovingTime < 0.6f ? A.MMCrouch[1] : bPivot ? A.MMCrouch[2] : A.MMCrouch[0]);
	else
		M->SetDatabases(!bMoving ? A.MMStop[MMGait] : MovingTime < 0.6f ? A.MMStart[MMGait] : bPivot ? A.MMPivot[MMGait] : A.MMMove[MMGait]);
	// the capture tops out at 7 m/s forward (sprint loop), 3.5 m/s sideways and 3 m/s backwards (run loops); NFL
	// players run up to ~10 m/s. Above those, search a proportionally slower query and play the clip faster (<= 1.5x).
	{
		const FVector Fwd = FRotator(0.f, SimYaw, 0.f).Vector();
		const float C = Speed > 1.f ? FVector::DotProduct(V / Speed, Fwd) : 1.f;
		const float AnimMax = bCrouch ? (C >= 0.f ? FMath::Lerp(170.f, 200.f, C) : FMath::Lerp(170.f, 150.f, -C))
			: C >= 0.f ? FMath::Lerp(350.f, 700.f, C) : FMath::Lerp(350.f, 300.f, -C);
		M->TrajectorySpeedMultiplier = FMath::Clamp(AnimMax / FMath::Max(GaitSpeed, 1.f), 1.f / 1.5f, 1.f);
		// quicker strides up to 1.3x (7 m/s capture -> 9.1 m/s, NFL receiver speed, no foot slide); stable on -TagMoveTest
		// once start / pivot databases were offered by state
		static float RateCap = FParse::Value(FCommandLine::Get(), TEXT("TagMMRateCap="), RateCap) ? RateCap : 1.3f;
		M->MatchPlayRate = FMath::Min(1.f / M->TrajectorySpeedMultiplier, RateCap);
	}

	if (!bInMatch)
	{
		// ease the visible body out of the clip into the matched move
		if (CurrentClip && MHBody)
			if (UTagRetargetAnimInstance* R = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance())) ++R->Proc.ClipSerial;
		bInMatch = true;
		CurrentClip = nullptr;
	}
	M->bMatch = true;

	// trace one player's query (-TagMMTrace=<jersey number>): speed, accel, predicted path in body space, history, pick
	{
		static int32 TraceNum = FParse::Value(FCommandLine::Get(), TEXT("TagMMTrace="), TraceNum) ? TraceNum : -1;
		static double LastTrace = 0.0;
		if (Player && Player->number == TraceNum && Now - LastTrace > 0.1)
		{
			LastTrace = Now;
			auto Loc = [&Cur](const FVector& W) { return Cur.InverseTransformPosition(W).ToCompactString(); };
			const FTransformTrajectorySample F5 = Tr.GetSampleAtTime(0.5f), F10 = Tr.GetSampleAtTime(1.0f), H3 = Tr.GetSampleAtTime(-0.3f);
			UE_LOG(LogTemp, Display, TEXT("TAG mm trace t=%.2f v=%s |v|=%.0f a=%s fut=%.0f hist=%d p-0.3=%s p+0.5=%s p+1.0=%s face+1.0=%.0f db=%s mm=%s"),
				Now, *V.ToCompactString(), Speed, *SmoothedAccel.ToCompactString(), FutSpeed, TrajTime.Num(), *Loc(H3.Position), *Loc(F5.Position), *Loc(F10.Position),
				FMath::RadiansToDegrees((Cur.GetRotation().Inverse() * F10.Facing).GetTwistAngle(FVector::UpVector)),
				bMoving ? TEXT("move") : TEXT("stop"), *GetNameSafe(M->GetMatchedAsset()));
		}
	}

	// motion matching usage stats over all players (verbose log every 20 s): which kinds of clips the search picks
	{
		static TMap<FString, int32> Kinds;
		static TMap<FString, int32> Assets;
		static double SpeedSum = 0.0, OffSum = 0.0;
		static int32 N = 0;
		static double LastDump = 0.0;
		const FString Name = GetNameSafe(M->GetMatchedAsset());
		const TCHAR* Kind = Name.Contains(TEXT("Loop")) ? TEXT("loop") : Name.Contains(TEXT("Start")) ? TEXT("start") :
			Name.Contains(TEXT("Stop")) ? TEXT("stop") : Name.Contains(TEXT("Pivot")) ? TEXT("pivot") : Name.Contains(TEXT("Turn")) ? TEXT("turn") :
			Name.Contains(TEXT("Idle")) ? TEXT("idle") : Name.Contains(TEXT("Spin")) ? TEXT("spin") : Name == TEXT("None") ? TEXT("none") : TEXT("other");
		++Kinds.FindOrAdd(FString::Printf(TEXT("%s/%s/g%d"), bMoving ? TEXT("move") : TEXT("stop"), Kind, MMGait));
		++Assets.FindOrAdd(Name);
		SpeedSum += Speed;
		OffSum += Speed > 100.f ? FMath::Abs(FaceOff) : 0.f;
		++N;
		if (Now - LastDump > 20.0)
		{
			LastDump = Now;
			Kinds.ValueSort([](int32 L, int32 R) { return L > R; });
			Assets.ValueSort([](int32 L, int32 R) { return L > R; });
			FString K, As;
			for (const auto& KV : Kinds) K += FString::Printf(TEXT("%s=%d "), *KV.Key, KV.Value);
			int32 i = 0;
			for (const auto& KV : Assets) if (i++ < 12) As += FString::Printf(TEXT("%s=%d "), *KV.Key, KV.Value);
			UE_LOG(LogTemp, Display, TEXT("TAG mm stats n=%d speed=%.0f faceoff=%.0f | %s| top: %s"), N, SpeedSum / FMath::Max(N, 1), OffSum / FMath::Max(N, 1), *K, *As);
			Kinds.Reset(); Assets.Reset(); SpeedSum = OffSum = 0.0; N = 0;
		}
	}
	return true;
}

void AAthlete::StartRagdoll(const FVector& VelWorld)
{
	USkeletalMeshComponent* V = Visible();
	if (bRagdoll || !V->GetPhysicsAsset()) return;
	bRagdoll = true;
	DownTime = 0.f;
	V->SetCollisionProfileName(TEXT("Ragdoll"));
	V->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	V->SetAllBodiesSimulatePhysics(true);
	V->SetSimulatePhysics(true);
	V->WakeAllRigidBodies();
	// carry the player's momentum into the fall
	V->SetAllPhysicsLinearVelocity(VelWorld * 0.85f + FVector(0, 0, -50));
}

void AAthlete::PlayCatch(float BallZCm, float SpeedCm, const FVector& BallVelW, const FVector& SidelineDirW)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	UAnimationAsset* C = nullptr;
	if (!SidelineDirW.IsNearlyZero() && A.CatchSidelineL && A.CatchSidelineR)
		C = FVector::DotProduct(SidelineDirW, GetActorRightVector()) > 0.f ? A.CatchSidelineR.Get() : A.CatchSidelineL.Get(); // reach toward the boundary, drag the toes
	else if (SpeedCm > 350.f)
	{
		// at speed only the deep ball over the shoulder gets a full body catch (its legs keep the stride); other
		// running catches stay on the procedural hands layer over the run
		if (A.CatchOverShoulder && FVector::DotProduct(BallVelW.GetSafeNormal2D(), GetActorForwardVector()) > 0.5f)
			C = A.CatchOverShoulder;
	}
	else
		C = BallZCm > 160.f ? A.CatchHigh.Get() : BallZCm < 80.f ? A.CatchLow.Get() : A.CatchChest.Get();
	if (C) PlayOneShot(C);
}

void AAthlete::BeginTackleOn(AAthlete* Carrier)
{
	if (!Carrier || Carrier == this) return;
	TackleOn = Carrier;
	AlignT = 0.f;
	UE_LOG(LogTemp, Verbose, TEXT("TAG paired tackle #%d on #%d at %.0f cm"), Player ? Player->number : -1, Carrier->Player ? Carrier->Player->number : -1,
		FVector::Dist2D(GetActorLocation(), Carrier->GetActorLocation()));
}

void AAthlete::PlayDiveAttempt()
{
	if (const FAthleteAssets& A = FAthleteAssets::Get(); A.CatchDive) PlayOneShot(A.CatchDive);
}

float AAthlete::GetUpElapsed() const
{
	return bGettingUp && CurrentClip ? CurrentClip->GetPlayLength() - GetUpLeft : -1.f;
}

void AAthlete::StartGetUp(const FVector& SimLoc)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	USkeletalMeshComponent* V = Visible();
	if (!V || !V->GetSkeletalMeshAsset()) return;
	// which way did the body land? F = U x L (UE: forward = up x left); face down when the chest points at the turf
	const FVector Pel = V->GetBoneLocation(TEXT("pelvis"));
	const FVector U = (V->GetBoneLocation(TEXT("neck_01")) - Pel).GetSafeNormal();
	const FVector L = (V->GetBoneLocation(TEXT("upperarm_l")) - V->GetBoneLocation(TEXT("upperarm_r"))).GetSafeNormal();
	const bool bFaceDown = FVector::CrossProduct(U, L).Z < 0.f;
	FVector H(U.X, U.Y, 0.f);
	if (!H.Normalize()) H = GetActorForwardVector();
	// the keyed get-ups lie along the actor axis: face down with the head forward, face up with the head behind
	const float NewYaw = (bFaceDown ? H : -H).Rotation().Yaw;
	UAnimationAsset* Clip = bFaceDown ? A.GetUpDown.Get() : A.GetUpUp.Get();
	static int32 GGetUpSeq = 0; // every other face-up landing rolls to the side and up instead
	if (!bFaceDown && A.GetUpRoll && (GGetUpSeq++ % 2)) Clip = A.GetUpRoll.Get();
	static int32 GSlowSeq = 0;  // now and then a player is shaken up and slow to get up (hand on the ribs)
	if (bFaceDown && A.GetUpSlow && (GSlowSeq++ % 5) == 4) Clip = A.GetUpSlow.Get();
	if (!Clip) return;

	const int32 NB = V->GetNumBones();
	TArray<FTransform> W;
	W.SetNum(NB);
	for (int32 i = 0; i < NB; ++i) W[i] = V->GetBoneTransform(i); // ragdoll pose, world space

	bRagdoll = false;
	V->SetSimulatePhysics(false);
	V->SetAllBodiesSimulatePhysics(false);
	V->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	V->AttachToComponent(Root, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	V->SetRelativeLocationAndRotation(BodyRelLoc, BodyRelRot);
	if (MHBody) MHBody->SetRelativeScale3D(VisRelScale);

	// stand the actor where the body actually lies; the gap to the sim position eases out after the get-up
	const FVector NewLoc(Pel.X, Pel.Y, SimLoc.Z);
	VisOffset = FVector(NewLoc.X - SimLoc.X, NewLoc.Y - SimLoc.Y, 0.f);
	SetActorLocation(NewLoc);
	SmoothedYaw = NewYaw;
	SetActorRotation(FRotator(0, NewYaw, 0));

	// snapshot: world -> new component space -> parent relative (mesh order); the get-up crossfades out of it
	const FTransform C = V->GetComponentTransform();
	const FReferenceSkeleton& R = V->GetSkeletalMeshAsset()->GetRefSkeleton();
	TArray<FTransform> CS, Loc;
	CS.SetNum(NB);
	Loc.SetNum(NB);
	for (int32 i = 0; i < NB; ++i) CS[i] = W[i].GetRelativeTransform(C);
	for (int32 i = 0; i < NB; ++i)
	{
		const int32 P = R.GetParentIndex(i);
		Loc[i] = P == INDEX_NONE ? CS[i] : CS[i].GetRelativeTransform(CS[P]);
	}
	if (UTagRetargetAnimInstance* RI = MHBody ? Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()) : nullptr)
	{
		RI->SnapshotMesh = MoveTemp(Loc);
		++RI->SnapshotSerial;
	}
	CurrentClip = nullptr; // restart cleanly; the snapshot (not the last pose) is the blend source
	PlayClip(Clip, false);
	GetUpLeft = Clip->GetPlayLength();
	bGettingUp = true;
	bStoodUp = true;
	CurrentAnim.Reset();
	UE_LOG(LogTemp, Verbose, TEXT("TAG one-shot %s #%d"), *Clip->GetName(), Player ? Player->number : -1);
}

void AAthlete::ResetPose()
{
	VisOffset = FVector::ZeroVector;
	bGettingUp = false;
	bStoodUp = false;
	TackledLeft = 0.f;
	TackleOn = nullptr;
	AlignT = -1.f;
	DownTime = 0.f;
	// new play: the players are teleported to the formation, so the motion history starts over
	TrajPos.Reset();
	TrajRot.Reset();
	TrajTime.Reset();
	bHavePrevVel = false;
	SmoothedAccel = FVector::ZeroVector;
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
	const FVector PrevActorLoc = GetActorLocation();
	if (bRagdoll)
	{
		// physics owns the body; after a beat the player gets up with a keyed get-up out of the ragdoll pose
		DownTime += DeltaTime;
		if (DownTime > 0.6f && A.GetUpDown && A.GetUpUp) StartGetUp(Location); // ragdoll settled: get up before the next play resets him
		if (bRagdoll) return;
	}
	if (TackledLeft > 0.f)
	{
		// held by the tackler and driven forward with the pile onto the sim's spot, then the fall is physics
		TackledLeft -= DeltaTime;
		VisOffset *= FMath::Exp(-9.f * DeltaTime);
		SetActorLocation(Location + VisOffset);
		if (TackledLeft <= 0.f)
		{
			VisOffset = FVector::ZeroVector;
			StartRagdoll(PileVel);
		}
		return;
	}
	if (bGettingUp)
	{
		GetUpLeft -= DeltaTime;
		SetActorLocation(Location + VisOffset);
		if (GetUpLeft > 0.f) return;
		bGettingUp = false;
		CurrentAnim.Reset();
	}
	else
	{
		VisOffset *= FMath::Exp(-2.5f * DeltaTime); // drift back onto the sim position after a get-up
	}
	const bool bFall = Anim == TEXT("down") || Anim == TEXT("tackle") || Anim == TEXT("dive");
	SetActorLocation(Location + VisOffset);
	RecordTrajectory();
	if (!bYawInit)
	{
		SmoothedYaw = Yaw;
		bYawInit = true;
	}
	SmoothedYaw += FMath::FindDeltaAngleDegrees(SmoothedYaw, Yaw) * FMath::Clamp(DeltaTime * 10.f, 0.f, 1.f);
	SetActorRotation(FRotator(0, SmoothedYaw, 0));

	const float SpeedCm = FVector(VelWorld.X, VelWorld.Y, 0).Size();
	const bool bChanged = Anim != CurrentAnim;
	// a defender who comes off a block straight into a run beat his blocker: show the rush move (swim / rip)
	const bool bShed = bChanged && CurrentAnim == TEXT("block") && (Anim == TEXT("run") || Anim == TEXT("idle")) &&
		Player && (Player->pos == tag::Pos::DE || Player->pos == tag::Pos::DT || Player->pos == tag::Pos::LB) &&
		A.ShedSwim && A.ShedRip;
	StillTime = SpeedCm < 20.f ? StillTime + DeltaTime : 0.f;
	CurrentAnim = Anim;
	if (bShed)
	{
		static int32 GShedSeq = 0; // rotate game wide so consecutive sheds differ
		UAnimationAsset* Pool[] = {A.ShedSwim.Get(), A.ShedRip.Get(), A.RushBull.Get(), A.RushClub.Get(), A.RushSpinL.Get(), A.RushSpinR.Get()};
		UAnimationAsset* M = nullptr;
		for (int32 Tries = 0; Tries < 6 && !M; ++Tries) M = Pool[GShedSeq++ % 6];
		PlayOneShot(M ? M : A.ShedSwim.Get());
	}

	if (A.bMocap && Anim == TEXT("down"))
	{
		if (bStoodUp) // already back on his feet this play: stand until the sim moves him on
		{
			if (A.Idle) PlayClip(A.Idle, true);
			return;
		}
		// tackled ball carrier: the sim resolves the tackle in one tick and moves him forward by the yards after
		// contact. Keep the body where the hit landed, play the hit, carry it with the pile, then let it fall.
		const float Jump = FVector::Dist2D(PrevActorLoc, Location);
		if (bChanged && A.TackledHit && Jump < 800.f && PrevActorLoc != FVector::ZeroVector)
		{
			VisOffset = FVector(PrevActorLoc.X - Location.X, PrevActorLoc.Y - Location.Y, 0.f);
			SetActorLocation(Location + VisOffset);
			PileVel = (Location - PrevActorLoc).GetSafeNormal2D() * 320.f + FVector(VelWorld.X, VelWorld.Y, 0.f) * 0.4f - FVector(0, 0, 50);
			TackledLeft = A.TackledHit->GetPlayLength();
			UE_LOG(LogTemp, Verbose, TEXT("TAG tackled #%d held %.0f cm behind the sim spot"), Player ? Player->number : -1, Jump);
			PlayClip(A.TackledHit, false);
			return;
		}
		StartRagdoll(VelWorld);
		return;
	}
	// hand keyed contact and evasion clips (in place; the sim moves the actor)
	if (Anim == TEXT("tackle") && A.TackleKeyed)
	{
		if (bChanged)
		{
			// variety: rotate wrap / high / low, starting from a per-player offset so a gang tackle mixes heights
			UAnimationAsset* Pool[6] = {A.TackleKeyed.Get(), A.TackleHigh.Get(), A.TackleLow.Get(), A.TackleShoulder.Get(),
				A.TackleArm.Get(), A.TackleAssist.Get()};
			// game-wide rotation: every tackle takes the next variant, so simultaneous gang tacklers and consecutive
			// plays never show the same one (a per-player hash over three variants collides too often)
			static int32 GTackleSeq = 0;
			++TackleCount;
			UAnimationAsset* T = Pool[GTackleSeq++ % 6];
			PlayClip(T ? T : A.TackleKeyed.Get(), false);
		}
		// paired: ease into wrap distance of the carrier (his body, not the sim spot) and square up to him
		if (AAthlete* C = TackleOn.Get(); C && AlignT >= 0.f && AlignT < 0.6f)
		{
			AlignT += DeltaTime;
			const FVector Me = Location + VisOffset, Him = C->GetActorLocation();
			FVector To = Him - Me;
			To.Z = 0.f;
			const float D = To.Size();
			if (D > 1.f)
			{
				const FVector Want = (Him - To / D * 62.f) - Location; // wrap distance, chest to chest
				const FVector Off = FVector(Want.X, Want.Y, 0.f).GetClampedToMaxSize(160.f);
				VisOffset = FMath::Lerp(VisOffset, Off, 1.f - FMath::Exp(-14.f * DeltaTime));
				SetActorLocation(Location + VisOffset);
				SmoothedYaw += FMath::FindDeltaAngleDegrees(SmoothedYaw, To.Rotation().Yaw) * FMath::Clamp(DeltaTime * 14.f, 0.f, 1.f);
				SetActorRotation(FRotator(0, SmoothedYaw, 0));
			}
		}
		return;
	}
	if (Anim == TEXT("dive") && A.TackleDive)
	{
		if (bChanged) PlayClip(A.TackleDive, false);
		return;
	}
	if (Anim == TEXT("juke") && A.JukeL && A.JukeR)
	{
		// cut direction from the carrier's lateral velocity relative to his facing
		const float Side = FVector::DotProduct(FVector(VelWorld.X, VelWorld.Y, 0), GetActorRightVector());
		if (bChanged)
		{
			// the move fits the runner: elusive backs juke and spin, power backs lower the shoulder or stiff arm,
			// the most agile occasionally hurdle; a game-wide counter keeps consecutive moves from repeating
			static int32 GMoveSeq = 0;
			const int32 Seq = GMoveSeq++;
			UAnimationAsset* Move = Side > 0.f ? A.JukeR.Get() : A.JukeL.Get();
			if (Player)
			{
				const tag::Ratings& R = Player->r;
				if (A.Hurdle && R.agility > 85 && Seq % 7 == 3) Move = A.Hurdle;
				else if (R.breakTackle > R.elusiveness + 5)
					Move = (Seq % 2) ? A.Truck.Get() : (Side > 0.f ? A.StiffArmR.Get() : A.StiffArmL.Get());
				else if (Seq % 3 == 1)
					Move = Side > 0.f ? A.SpinR.Get() : A.SpinL.Get();
				if (!Move) Move = Side > 0.f ? A.JukeR.Get() : A.JukeL.Get();
			}
			PlayClip(Move, false);
		}
		return;
	}
	// kicks (the director starts them so foot contact meets the ball leaving; AnimT = start position in the clip)
	if (Anim == TEXT("kickoff") || Anim == TEXT("punt") || Anim == TEXT("fgkick"))
	{
		UAnimationAsset* K = Anim == TEXT("kickoff") ? A.KickKickoff.Get() : Anim == TEXT("punt") ? A.KickPunt.Get() : A.KickFieldGoal.Get();
		if (K)
		{
			if (bChanged) PlayClip(K, false, 1.f, FMath::Max(0.f, AnimT));
			return;
		}
	}
	if (Anim == TEXT("kneel") && A.Kneel)
	{
		if (bChanged) PlayClip(A.Kneel, false);
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
		// hand keyed pass from the stride frame: plant, hip-shoulder separation, release ~0.3 s later with the sim
		if (A.ThrowKeyed) { if (bChanged) PlayClip(A.ThrowKeyed, false, 1.f, FAthleteAssets::ThrowKeyedStart); }
		else if (bChanged) PlayClip(A.Throw, false, 1.6f);
		return;
	}
	const bool bLine = Player && (Player->pos == tag::Pos::OT || Player->pos == tag::Pos::OG || Player->pos == tag::Pos::C ||
		Player->pos == tag::Pos::DE || Player->pos == tag::Pos::DT);
	// the snap: linemen explode out of the stance (get-off), the shotgun QB catches the snap; both are one-shots
	if (bWasPreSnap && !bPreSnap)
	{
		if (bLine && A.GetOff) PlayOneShot(A.GetOff);
		else if (Player && Player->pos == tag::Pos::QB && bUnderCenter && A.UnderCenterSnap) PlayOneShot(A.UnderCenterSnap);
		else if (Player && Player->pos == tag::Pos::QB && A.SnapCatch) PlayOneShot(A.SnapCatch);
	}
	bWasPreSnap = bPreSnap;
	if (OneShotLeft > 0.f && OneShot && !bPreSnap)
	{
		OneShotLeft -= DeltaTime;
		PlayClip(OneShot, false);
		if (OneShotLeft <= 0.f)
		{
			// the layout dive ends ~1 m ahead of where it started: hand that travel to the actor so the next pose
			// starts where the body is (VisOffset then eases back onto the sim position)
			if (OneShot == A.CatchDive) VisOffset += GetActorForwardVector() * FAthleteAssets::CatchDiveTravel;
			OneShot = nullptr;
			CurrentAnim.Reset(); // one-shot done: let whatever state follows (re)start next frame (crossfaded)
		}
		return;
	}
	// pre-snap: hand keyed stance loops (three point for the line, shotgun for the QB, two point for the rest)
	if (bPreSnap && SpeedCm < 40.f && Player)
	{
		const tag::Pos P = Player->pos;
		UAnimationAsset* S = bLine ? A.Stance3pt.Get() : P == tag::Pos::QB ? (bUnderCenter && A.StanceUnderCenter ? A.StanceUnderCenter.Get() : A.StanceShotgun.Get()) : A.Stance2pt.Get();
		if (P == tag::Pos::DT && A.Stance4pt && Player->id % 2) S = A.Stance4pt; // some tackles fire out of a four point
		else if ((P == tag::Pos::RB || P == tag::Pos::FB) && A.StanceRB) S = A.StanceRB;
		else if ((P == tag::Pos::CB || P == tag::Pos::S) && A.StanceDB) S = A.StanceDB;
		else if (P == tag::Pos::LB && A.StanceLB) S = A.StanceLB;
		else if ((P == tag::Pos::K || P == tag::Pos::P) && A.StanceK) S = A.StanceK;
		if (S)
		{
			// per player phase offset so a whole line does not breathe in unison
			PlayClip(S, true, 1.f, FMath::Frac(Player->id * 0.37f) * 2.5f);
			return;
		}
	}
	if (Anim == TEXT("celebrate"))
	{
		// each player has his own celebration (keyed loops), phase offset so nobody moves in unison
		UAnimationAsset* C = A.Celebrations.Num() && Player ? A.Celebrations[Player->id % A.Celebrations.Num()] : A.Celebrate.Get();
		if (bChanged) PlayClip(C, true, A.bCelebrateKeyed || A.Celebrations.Num() ? 1.f : 1.2f, Player ? FMath::Frac(Player->id * 0.29f) : 0.f);
		return;
	}
	// locomotion: direction of travel relative to facing, speed-matched so feet don't slide
	const float VelYaw = SpeedCm > 25.f ? FMath::RadiansToDegrees(FMath::Atan2(VelWorld.Y, VelWorld.X)) : SmoothedYaw;
	const float Dir = SpeedCm > 25.f ? FMath::FindDeltaAngleDegrees(SmoothedYaw, VelYaw) : 0.f;
	SmoothedDir += FMath::FindDeltaAngleDegrees(SmoothedDir, Dir) * FMath::Clamp(DeltaTime * 8.f, 0.f, 1.f);
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, SpeedCm, DeltaTime, 10.f);
	// near-stationary blockers: keyed engaged loop (hands on the chest plate) or the pass set before contact;
	// moving blockers keep the crouch blend space
	if (Anim == TEXT("block") && SpeedCm < 150.f && A.BlockEngaged && A.BlockPassSet)
	{
		PlayClip(bEngaged ? A.BlockEngaged.Get() : A.BlockPassSet.Get(), true, 1.f, Player ? FMath::Frac(Player->id * 0.41f) : 0.f);
		return;
	}
	const bool bCrouched = A.bMocap && A.Crouch && (Anim == TEXT("block") || (bLineman && SpeedCm < 260.f));
	// after the whistle, standing around: hands on the hips (winded players bend over, hands on the knees)
	if (!bPreSnap && StillTime > 2.5f && Anim == TEXT("idle") && A.IdleHandsHips && A.IdleHandsKnees && Player)
	{
		PlayClip(Player->r.stamina < 70 ? A.IdleHandsKnees.Get() : A.IdleHandsHips.Get(), true, 1.f, FMath::Frac(Player->id * 0.53f) * 2.f);
		return;
	}
	// open field locomotion and standing: motion matching (crouched line footwork keeps its blend space)
	if (DriveMotionMatching(VelWorld, Yaw, DeltaTime, bCrouched)) return;
	// standing still: play the idle clip itself (the blend space's zero-speed row leaves the rig in its bind pose)
	if (!bCrouched && A.Idle && SpeedCm < 20.f && SmoothedSpeed < 40.f)
	{
		PlayClip(A.Idle, true, 1.f);
		return;
	}
	UBlendSpace* BS = bCrouched ? A.Crouch.Get() : A.Locomotion.Get();
	const float MaxSp = bCrouched ? A.CrouchMaxSpeed : A.LocoMaxSpeed;
	if (!BS) return;
	if (bInMatch || CurrentClip != BS) StartBodyAsset(BS, true); // crossfaded (keyed clip -> run)
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

void AAthlete::UpdateFace(float Dt)
{
	if (!MHFace) return;
	UTagFaceAnimInstance* FI = Cast<UTagFaceAnimInstance>(MHFace->GetAnimInstance());
	if (!FI) return;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// blinks: every 2-6 s, ~0.16 s each, sometimes a double blink
	BlinkClock += Dt;
	if (BlinkClock > NextBlink)
	{
		BlinkClock = 0.f;
		NextBlink = FMath::FRandRange(2.f, 6.f);
		BlinkPhase = 0.f;
		bDoubleBlink = FMath::FRand() < 0.15f;
	}
	BlinkPhase += Dt;
	auto Tri = [](float T) { return T >= 0.f && T <= 0.16f ? 1.f - FMath::Abs(T - 0.08f) / 0.08f : 0.f; };
	float Blink = Tri(BlinkPhase) + (bDoubleBlink ? Tri(BlinkPhase - 0.26f) : 0.f);
	// effort: sprinting builds it, contact maxes it (tackles, blocks, sheds, getting up)
	const float Sp = SmoothedSpeed; // cm/s
	Exertion = FMath::Clamp(Exertion + (Sp > 500.f ? Dt * 0.12f : -Dt * 0.04f), 0.f, 1.f);
	const bool bContact = CurrentAnim == TEXT("tackle") || CurrentAnim == TEXT("block") || CurrentAnim == TEXT("dive") ||
		(OneShot != nullptr && OneShotLeft > 0.f);
	const float EffortGoal = FMath::Clamp(FMath::Clamp((Sp - 450.f) / 500.f, 0.f, 1.f) * 0.6f + (bContact ? 1.f : 0.f) +
		(bGettingUp ? 0.7f : 0.f) + (bRagdoll ? 0.8f : 0.f), 0.f, 1.f);
	FaceEffort = FMath::FInterpTo(FaceEffort, EffortGoal, Dt, 8.f);
	FaceFocus = FMath::FInterpTo(FaceFocus, bPreSnap ? 1.f : 0.f, Dt, 3.f);
	const bool bCeleb = CurrentAnim == TEXT("celebrate");
	FaceShout = FMath::FInterpTo(FaceShout, bCeleb ? 1.f : 0.f, Dt, 5.f);
	FTagFaceInput& F = FI->Face;
	F.Blink = FMath::Clamp(Blink * (1.f - FaceEffort * 0.3f) + FaceEffort * 0.15f, 0.f, 1.f); // grimacing eyes narrow
	F.Effort = FaceEffort;
	// breathing: faster and deeper after running (mouth breathing)
	F.Breath = (0.5f + 0.5f * FMath::Sin(Now * 2.f * PI * (0.25f + Exertion * 0.45f))) * (0.15f + Exertion);
	F.Focus = FaceFocus;
	F.Shout = FaceShout * (0.65f + 0.35f * FMath::Sin(Now * 5.f + (Player ? Player->id : 0)));
	F.Smile = FaceShout;
}

void AAthlete::SetFootball(const FAthleteFootball& InF, float Dt)
{
	UpdateFace(Dt);
	bPreSnap = InF.bPreSnap;
	if (InF.bPreSnap) bUnderCenter = InF.bUnderCenter; // keep the alignment through the snap
	bEngaged = InF.bReach;
	if (!MHBody) return;
	UTagRetargetAnimInstance* I = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance());
	if (!I) return;
	auto Ease = [Dt](float& V, float Target, float Rate) { V = FMath::FInterpTo(V, Target, Dt, Rate); };
	const FAthleteAssets& AA = FAthleteAssets::Get();
	FAthleteFootball F = InF;
	// hand keyed clips already carry the throw and the stance: keep the procedural versions off them
	if (AA.ThrowKeyed) F.ThrowT = -1.f;
	if (F.bPreSnap && AA.Stance3pt) F.bStance = false;
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
	if (const UTagMotionMatchInstance* M = MatchInstance())
		if (bInMatch) Fb += FString::Printf(TEXT(" | mm=%s gait=%d tsm=%.2f"), *GetNameSafe(M->GetMatchedAsset()), MMGait, M->TrajectorySpeedMultiplier);
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
