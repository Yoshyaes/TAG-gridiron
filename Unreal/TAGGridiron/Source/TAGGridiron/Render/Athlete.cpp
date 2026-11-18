#include "Render/Athlete.h"
#include "Algo/Sort.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Engine/Texture.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformMemory.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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
	LEDMat = LoadAsset<UMaterialInterface>(TEXT("/Game/TAG/Materials/M_TagLED.M_TagLED"), false);

	// the bodies first: the model pack characters decide which animation tree plays (bNativeAnim)
	const FString G = TEXT("/Game/Characters/UEFN_Mannequin/");
	{
		const bool bTryHero = !FParse::Param(FCommandLine::Get(), TEXT("TagNoHero"));
		bNativeAnim = bTryHero && !FParse::Param(FCommandLine::Get(), TEXT("TagUEFNAnim")) &&
			FPackageName::DoesPackageExist(TEXT("/Game/TAG/AnimH/BS_TAG_Locomotion")) && FPackageName::DoesPackageExist(TEXT("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig"));
		if (bTryHero) bHero = LoadHero();
		if (!bHero) bNativeAnim = false;
		AnimRoot = bNativeAnim ? TEXT("/Game/TAG/AnimH") : TEXT("/Game/TAG/Anim");
		UE_LOG(LogTemp, Display, TEXT("TAG: animation tree %s (%s)"), *AnimRoot,
			bNativeAnim ? TEXT("the mocap on its own skeleton, copied onto the pack bodies") : TEXT("retargeted to the UEFN mannequin"));
	}
	// the hidden source mesh that plays the animation: the hero rig itself, or the UEFN mannequin
	USkeletalMesh* MocapMesh = bNativeAnim ? LoadAsset<USkeletalMesh>(TEXT("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig.SKM_TAG_HeroRig"), false)
		: LoadAsset<USkeletalMesh>(*(G + TEXT("Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin")), false);
	UBlendSpace* MocapLoco = LoadAsset<UBlendSpace>(*FString::Printf(TEXT("%s/BS_TAG_Locomotion.BS_TAG_Locomotion"), *AnimRoot), false);
	if (MocapMesh && MocapLoco)
	{
		bMocap = true;
		Mesh = MocapMesh;
		Locomotion = MocapLoco;
		Crouch = LoadAsset<UBlendSpace>(*FString::Printf(TEXT("%s/BS_TAG_Crouch.BS_TAG_Crouch"), *AnimRoot), false);
		// TAG football clips: Tools/Anim (mocap pack builds) -> Tools/Art/import_tag_anims.py
		const FString K = AnimRoot + TEXT("/Authored/");
		auto Keyed = [&](const TCHAR* N) { return LoadAsset<UAnimationAsset>(*FString::Printf(TEXT("%s%s.%s"), *K, N, N), false); };
		Shove = Keyed(TEXT("A_TAG_Block_Punch"));
		Throw = Keyed(TEXT("A_TAG_QB_Throw"));
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
		if (bNativeAnim)
		{
			auto Rep = [&](TArray<FLineRep>& Sink, const TCHAR* N)
			{
				FLineRep R;
				R.D.Reset(LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/Blocking/A_TAG_Pack_%s__P01.A_TAG_Pack_%s__P01"), *AnimRoot, N, N), nullptr, LOAD_NoWarn | LOAD_Quiet));
				R.O.Reset(LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/Blocking/A_TAG_Pack_%s__P02.A_TAG_Pack_%s__P02"), *AnimRoot, N, N), nullptr, LOAD_NoWarn | LOAD_Quiet));
				if (R.D.IsValid() && R.O.IsValid()) Sink.Add(MoveTemp(R));
				else UE_LOG(LogTemp, Warning, TEXT("TAG: line rep %s not found"), N);
			};
			static const bool bNoReps = FParse::Param(FCommandLine::Get(), TEXT("TagNoLineReps"));
			if (!bNoReps)
			{
				Rep(RepLoops, TEXT("Dline_PassBlock_HeadsUp_Base_Loop_v01"));
				Rep(RepWins, TEXT("Dline_PassBlock_Stationary_to_Left_Win_v01"));
				Rep(RepWins, TEXT("Dline_PassBlock_Stationary_to_Left_Win_v03"));
				Rep(RepWins, TEXT("Dline_PassBlock_Stationary_to_Left_Win_v04"));
				Rep(RepWins, TEXT("Dline_PassBlock_Disengage_Left_from_Stationary_v01"));
				Rep(RepBigWins, TEXT("Dline_PassBlock_Stationary_to_Left_Win_Aggressive_v01"));
				Rep(RepBigWins, TEXT("Dline_PassBlock_Stationary_to_Left_Win_Aggressive_v02"));
				Rep(RepHolds, TEXT("Dline_PassBlock_Stationary_to_Right_Loss_v01"));
				Rep(RepHolds, TEXT("Dline_PassBlock_Stationary_to_Right_Loss_v02"));
				Rep(RepPowerHolds, TEXT("DL_Move_BullRush_Loss_v1"));
				// (the pack walks forward, back, right and back-right; the two left-hand walks are their mirrors, built with
				// the placed clips: mocap_map Block_Walk_L / Block_Walk_BackL)
				for (const TCHAR* N : {TEXT("Pack/Blocking/A_TAG_Pack_MG_PASSBLOCK_WALK_0__P01"), TEXT("Pack/Blocking/A_TAG_Pack_MG_PASSBLOCK_WALK_90__P01"),
					TEXT("Pack/Blocking/A_TAG_Pack_MG_PASSBLOCK_WALK_135__P01"), TEXT("Pack/Blocking/A_TAG_Pack_MG_PASSBLOCK_WALK_180__P01"),
					TEXT("Authored/A_TAG_Block_Walk_L"), TEXT("Authored/A_TAG_Block_Walk_BackL")})
				{
					const FString Leaf = FPaths::GetCleanFilename(N);
					UAnimSequence* Sq = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *AnimRoot, N, *Leaf), nullptr, LOAD_NoWarn | LOAD_Quiet);
					if (!Sq || !Sq->HasRootMotion()) continue;
					FVector T = Sq->ExtractRootMotionFromRange(0.0, Sq->GetPlayLength(), FAnimExtractContext(0.0, true)).GetTranslation();
					T.Z = 0.f;
					if (T.Size() < 20.f) continue; // stays put
					BlockWalks.Emplace(Sq);
					BlockWalkDir.Add(T.GetSafeNormal());
					BlockWalkSpeed.Add(static_cast<float>(T.Size() / Sq->GetPlayLength()));
					UE_LOG(LogTemp, Display, TEXT("TAG block walk %s: %.0f cm/s toward %s"), N, T.Size() / Sq->GetPlayLength(), *T.GetSafeNormal().ToCompactString());
				}
				UE_LOG(LogTemp, Display, TEXT("TAG line reps: %d loops, %d wins, %d big wins, %d holds, %d walks"), RepLoops.Num(), RepWins.Num(), RepBigWins.Num(), RepHolds.Num(), BlockWalks.Num());
				auto Walks = [&](FWalkSet& Set, const TCHAR* Gait)
				{
					for (const TCHAR* D : {TEXT("D1"), TEXT("D2"), TEXT("D3"), TEXT("D4"), TEXT("D5"), TEXT("D6"), TEXT("D7"), TEXT("D8")})
					{
						const FString N = FString::Printf(TEXT("A_TAG_Pack_MG_QB_InPocket_%s_%s__P01"), Gait, D);
						UAnimSequence* Sq = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/Quarterback/%s.%s"), *AnimRoot, *N, *N), nullptr, LOAD_NoWarn | LOAD_Quiet);
						if (!Sq || !Sq->HasRootMotion()) continue;
						FVector T = Sq->ExtractRootMotionFromRange(0.0, Sq->GetPlayLength(), FAnimExtractContext(0.0, true)).GetTranslation();
						T.Z = 0.f;
						if (T.Size() < 20.f) continue;
						Set.Clips.Emplace(Sq);
						Set.Dir.Add(T.GetSafeNormal());
						Set.Speed.Add(static_cast<float>(T.Size() / Sq->GetPlayLength()));
					}
				};
				Walks(QBWalks, TEXT("Walk"));
				Walks(QBJogs, TEXT("Jog"));
				auto QbRun = [&](const TCHAR* N, TStrongObjectPtr<UAnimSequence>& Out, float& Speed)
				{
					UAnimSequence* Sq = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/Quarterback/A_TAG_Pack_%s.A_TAG_Pack_%s"), *AnimRoot, N, N), nullptr, LOAD_NoWarn | LOAD_Quiet);
					if (!Sq) return;
					Out.Reset(Sq);
					FVector T = Sq->ExtractRootMotionFromRange(0.0, Sq->GetPlayLength(), FAnimExtractContext(0.0, true)).GetTranslation();
					T.Z = 0.f;
					if (T.Size() > 50.f) Speed = static_cast<float>(T.Size() / Sq->GetPlayLength());
				};
				QbRun(TEXT("MG_Loco_QB_Scramble_Jog__P01"), QBScramble, QBScrambleSpeed);
				QbRun(TEXT("MG_Loco_QB_Run_ShortStride__P01"), QBRun, QBRunSpeed);
				UE_LOG(LogTemp, Display, TEXT("TAG qb scramble: jog %d (%.0f cm/s), run %d (%.0f cm/s)"), QBScramble.IsValid(), QBScrambleSpeed, QBRun.IsValid(), QBRunSpeed);
				UE_LOG(LogTemp, Display, TEXT("TAG qb pocket set: %d walks, %d jogs"), QBWalks.Clips.Num(), QBJogs.Clips.Num());
				auto KickClip = [&](const TCHAR* N) -> UAnimSequence*
				{
					return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/Kicking/A_TAG_Pack_%s.A_TAG_Pack_%s"), *AnimRoot, N, N), nullptr, LOAD_NoWarn | LOAD_Quiet);
				};
				FgScene.Snap.Reset(KickClip(TEXT("FG_Normal_Kick_v1__P03")));
				FgScene.Hold.Reset(KickClip(TEXT("FG_Normal_Kick_v1__P01")));
				FgScene.Kick.Reset(KickClip(TEXT("FG_Normal_Kick_v1__P02")));
				FgScene.SnapWait.Reset(KickClip(TEXT("MADNOCT04_1059_1_5_Long_Snapper_Presnap_Cycle__P01")));
				FgScene.HoldWait.Reset(KickClip(TEXT("P_Holder_PreSnap_Loop__P01")));
				FgScene.KickWait.Reset(KickClip(TEXT("Kicker_FG_loop_Default__P01")));
				UE_LOG(LogTemp, Display, TEXT("TAG field goal capture: %s (waits %d %d %d)"), FgScene.Ok() ? TEXT("snap, hold and kick") : TEXT("missing"),
					FgScene.SnapWait.IsValid(), FgScene.HoldWait.IsValid(), FgScene.KickWait.IsValid());
				PuntScene.Snap.Reset(KickClip(TEXT("Punt_Normal_Kick_v1__P02")));
				PuntScene.Kick.Reset(KickClip(TEXT("Punt_Normal_Kick_v1__P01")));
				PuntScene.SnapWait = FgScene.SnapWait;
				PuntScene.KickWait.Reset(KickClip(TEXT("Punt_Normal_Idle_v1__P01")));
				FairCatch.Reset(KickClip(TEXT("cfairabx_FairKickCatch__P01")));
				FgBlockRush.Reset(LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/Blocking/A_TAG_Pack_FGBlock_Left_Edge_Block_v2__P03.A_TAG_Pack_FGBlock_Left_Edge_Block_v2__P03"), *AnimRoot), nullptr, LOAD_NoWarn | LOAD_Quiet));
				UE_LOG(LogTemp, Display, TEXT("TAG kick block capture: %d"), FgBlockRush.IsValid());
				auto UpClip = [&](const TCHAR* N) -> UAnimSequence*
				{
					return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/GetUps/A_TAG_Pack_%s.A_TAG_Pack_%s"), *AnimRoot, N, N), nullptr, LOAD_NoWarn | LOAD_Quiet);
				};
				HelpUpDown.Reset(UpClip(TEXT("Getup_NonBC_15_HelpUp_Knees_D7_Dejected_Var4__P02")));
				HelpUpBy.Reset(UpClip(TEXT("Getup_NonBC_15_HelpUp_Knees_D7_Dejected_Var4__P01")));
				UE_LOG(LogTemp, Display, TEXT("TAG help-up capture: %s"), HelpUpDown.IsValid() && HelpUpBy.IsValid() ? TEXT("loaded") : TEXT("missing"));
				auto PreClip = [&](const TCHAR* N) -> UAnimSequence*
				{
					return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/Pack/PrePlay/A_TAG_Pack_%s.A_TAG_Pack_%s"), *AnimRoot, N, N), nullptr, LOAD_NoWarn | LOAD_Quiet);
				};
				QBAudibleGun.Reset(PreClip(TEXT("MADNMAY04_0973_1_QB_Stance_Shotgun_Hands_Up_Quick_Audible_Point_to_Side__P01")));
				QBAudibleUnder.Reset(PreClip(TEXT("MADNMAY04_0971_1_QB_Stance_Under_Center_Quick_Audible_Point_to_Side__P01")));
				QBSignal.Reset(PreClip(TEXT("MADNMAY04_0986_1_QB_Stance_Shotgun_Hands_Up_Hand_Signal_Variation_1__P01")));
				HBAudible.Reset(PreClip(TEXT("MADNMAY04_1586_1_HB_Stance_2_Point_Audible_look_around_and_re_settle__P01")));
				LBHearsAudible.Reset(PreClip(TEXT("MADNAUG04_0341_1_Pre_Play_Stance_LB_Stance_2_point_Offensive_audible_look_up_and_re_settle__P01")));
				LBDefAudible.Reset(PreClip(TEXT("MADNAUG04_0329_1_4_LB_HandSignals_Def_Audible__P01")));
				UE_LOG(LogTemp, Display, TEXT("TAG audible captures: qb %d %d %d, back %d, linebackers %d %d"), QBAudibleGun.IsValid(), QBAudibleUnder.IsValid(), QBSignal.IsValid(),
					HBAudible.IsValid(), LBHearsAudible.IsValid(), LBDefAudible.IsValid());
				UE_LOG(LogTemp, Display, TEXT("TAG punt capture: %s, fair catch %d"), PuntScene.Snap.IsValid() && PuntScene.Kick.IsValid() ? TEXT("snap and punt") : TEXT("missing"), FairCatch.IsValid());
			}
		}
		BlockPassSet = Keyed(TEXT("A_TAG_Block_PassSet_Loop"));
		BlockReadyOL = LoadAsset<UAnimationAsset>(*FString::Printf(TEXT("%s/Pack/Blocking/A_TAG_Pack_MG_PASSBLOCK_STAND__P01.A_TAG_Pack_MG_PASSBLOCK_STAND__P01"), *AnimRoot), false);
		BlockReadyBack = LoadAsset<UAnimationAsset>(*FString::Printf(TEXT("%s/Pack/Blocking/A_TAG_Pack_HB_Blocking_Idle_Loop__P01.A_TAG_Pack_HB_Blocking_Idle_Loop__P01"), *AnimRoot), false);
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
		BullDriveLoop = Keyed(TEXT("A_TAG_Rush_BullDrive_Loop"));
		AnchorLoop = Keyed(TEXT("A_TAG_Block_Anchor_Loop"));
		DriveLoop = Keyed(TEXT("A_TAG_Block_Drive_Loop"));
		DrivenBackLoop = Keyed(TEXT("A_TAG_Block_DrivenBack_Loop"));
		BlockPunch = Keyed(TEXT("A_TAG_Block_Punch"));
		LongArm = Keyed(TEXT("A_TAG_Rush_LongArm"));
		DipRip = Keyed(TEXT("A_TAG_Rush_DipRip"));
		Beaten = Keyed(TEXT("A_TAG_Block_Beaten"));
		BlockFinish = Keyed(TEXT("A_TAG_Block_Finish"));
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
		InjuredDown = Keyed(TEXT("A_TAG_Injured_Down"));
		QBDropShotgun = Keyed(TEXT("A_TAG_QB_Drop_Shotgun"));
		QBDropUnder = Keyed(TEXT("A_TAG_QB_Drop_UnderCenter"));
		QBPocket = Keyed(TEXT("A_TAG_QB_Pocket_Loop"));
		Breakdown = Keyed(TEXT("A_TAG_Def_Breakdown_Loop"));
		// the breakdown's variants: the pack's bounces and the hopping breakdown idle, captures whose feet travel with the
		// hips (they skate as locomotion, Tools/Art/mm_clip_plant.py) and so play in place here, each defender his own
		Breakdowns.Reset();
		if (Breakdown) Breakdowns.Add(Breakdown);
		for (const TCHAR* N : {TEXT("A_TAG_Loco_Line_Bounce_Back"), TEXT("A_TAG_Loco_Line_Bounce_Back_M"), TEXT("A_TAG_Loco_Line_Bounce_Fwd"),
			TEXT("A_TAG_Loco_Line_Bounce_Side"), TEXT("A_TAG_Loco_Line_Bounce_Side_M"), TEXT("A_TAG_Loco_Line_Back_Walk"), TEXT("A_TAG_Loco_Idle_Breakdown")})
			if (UAnimationAsset* C = Keyed(N)) Breakdowns.Add(C);
		// the whole pack behind the one-shot and idle families (see PackFamily): the cut, placed clips above stay first in
		// each list (their timing is measured), the library's captures stream in behind the game (SelectPack), a fresh
		// draw every quarter (ReseedPack from the director)
		if (FPackageName::DoesPackageExist(AnimRoot + TEXT("/Pack/Celebrations/A_TAG_Pack_PostPlay_BC_15_Celeb_FistPump_v1")) || FPackageName::DoesPackageExist(AnimRoot + TEXT("/Pack/Locomotion/A_TAG_Pack_MG_Loco_Sprint_Normal__P01")))
		{
			PackSeed = FDateTime::Now().GetDayOfYear();
			FParse::Value(FCommandLine::Get(), TEXT("TagPackSeed="), PackSeed);
			FParse::Value(FCommandLine::Get(), TEXT("TagPackScale="), PackScale);
			PackScale = FMath::Clamp(PackScale, 1, 20);
			LoadPackIndex();
			ReseedPack(PackSeed);
		}
		{
			// the throwing family; each starts 7 frames before its release (the set throw starts at its stride frame)
			const TCHAR* TN[ThrowCount] = {TEXT("A_TAG_QB_Throw"), TEXT("A_TAG_QB_Throw_Quick"), TEXT("A_TAG_QB_Throw_Deep"),
				TEXT("A_TAG_QB_Throw_Fade"), TEXT("A_TAG_QB_Throw_Sidearm"), TEXT("A_TAG_QB_Throw_Touch"), TEXT("A_TAG_QB_Throw_RunR"),
				TEXT("A_TAG_QB_Throw_RunL")};
			const int32 Release[ThrowCount] = {17, 14, 17, 15, 16, 15, 15, 15}; // each clip's right-hand peak speed (Tools/Anim/mocap_constants.py)
			for (int32 i = 0; i < ThrowCount; ++i)
			{
				Throws[i] = Keyed(TN[i]);
				ThrowStart[i] = FMath::Max(0, Release[i] - 7) / 30.f;
			}
		}
		CatchInStride = Keyed(TEXT("A_TAG_Catch_InStride"));
		CatchHighPoint = Keyed(TEXT("A_TAG_Catch_HighPoint"));
		CatchOneHand = Keyed(TEXT("A_TAG_Catch_OneHand"));
		CatchBackShoulder = Keyed(TEXT("A_TAG_Catch_BackShoulder"));
		CatchComeback = Keyed(TEXT("A_TAG_Catch_Comeback"));
		CatchScoop = Keyed(TEXT("A_TAG_Catch_Scoop"));
		TackledFromBehind = Keyed(TEXT("A_TAG_Tackled_FromBehind"));
		TackledFromSide = Keyed(TEXT("A_TAG_Tackled_FromSide"));
		TackleMiss = Keyed(TEXT("A_TAG_Tackle_Miss"));
		{
			static const TCHAR* PairNames[PairCount] = {TEXT("Drive"), TEXT("Over"), TEXT("Behind"), TEXT("Side"), TEXT("Dive")};
			for (int32 i = 0; i < PairCount; ++i)
			{
				PairT[i] = Keyed(*FString::Printf(TEXT("A_TAG_PT_%s_T"), PairNames[i]));
				PairC[i] = Keyed(*FString::Printf(TEXT("A_TAG_PT_%s_C"), PairNames[i]));
			}
			PileOn = Keyed(TEXT("A_TAG_PT_PileOn"));
		}
		BrokenStumble = Keyed(TEXT("A_TAG_Broken_Stumble"));
		IdleHandsHips = Keyed(TEXT("A_TAG_Idle_HandsHips_Loop"));
		IdleHandsKnees = Keyed(TEXT("A_TAG_Idle_HandsKnees_Loop"));
		for (const TCHAR* N : {TEXT("A_TAG_Celebrate_Flex_Loop"), TEXT("A_TAG_Celebrate_PointUp_Loop"), TEXT("A_TAG_Celebrate_Leap_Loop"),
			TEXT("A_TAG_Celebrate_Shimmy_Loop"), TEXT("A_TAG_Celebrate_FirstDown_Loop")})
			if (UAnimationAsset* C = Keyed(N)) Celebrations.Add(C);
		if (UAnimationAsset* C = Keyed(TEXT("A_TAG_Celebrate_Loop"))) { Celebrate = C; bCelebrateKeyed = true; Celebrations.Insert(C, 0); }
		// the other side's version of every asymmetric keyed clip (A_TAG_<name>_M)
		for (const TCHAR* N : {TEXT("Tackle_Wrap"), TEXT("Tackle_High"), TEXT("Tackle_Low"), TEXT("Tackle_Dive"), TEXT("Tackle_Shoulder"),
			TEXT("Tackle_Arm"), TEXT("Tackle_Assist"), TEXT("Tackled_Hit"), TEXT("Catch_High"), TEXT("Catch_Chest"), TEXT("Catch_Low"),
			TEXT("Catch_OverShoulder"), TEXT("Catch_Dive"), TEXT("GetUp_FaceDown"), TEXT("GetUp_FaceUp"), TEXT("GetUp_Roll"), TEXT("GetUp_Slow"),
			TEXT("Injured_Down"), TEXT("Truck"), TEXT("Hurdle"), TEXT("QB_Pitch"), TEXT("QB_Handoff"), TEXT("Celebrate_Loop"),
			TEXT("Celebrate_Flex_Loop"), TEXT("Celebrate_PointUp_Loop"), TEXT("Celebrate_Leap_Loop"), TEXT("Celebrate_FirstDown_Loop"),
			TEXT("Celebrate_Shimmy_Loop"), TEXT("Catch_OneHand"), TEXT("Catch_BackShoulder"), TEXT("Catch_Comeback"),
			TEXT("Tackled_FromSide"), TEXT("Tackle_Miss"), TEXT("Broken_Stumble"), TEXT("Rush_LongArm"), TEXT("Rush_DipRip"),
			TEXT("Block_Beaten"), TEXT("Shed_Swim"), TEXT("Shed_Rip"), TEXT("Rush_Club"), TEXT("PT_Side_T"), TEXT("PT_Side_C")})
		{
			UAnimationAsset* Orig = Keyed(*FString::Printf(TEXT("A_TAG_%s"), N));
			UAnimationAsset* Mir = Keyed(*FString::Printf(TEXT("A_TAG_%s_M"), N));
			if (Orig && Mir) MirrorOf.Add(Orig, Mir);
		}
		// the plain standing idle and the fall's lunge: the pack's neutral post-play stand and its tackle whiff (a dive
		// to the ground), the sample's clips only when those are missing
		Idle = Keyed(TEXT("A_TAG_Idle_Stand_Loop"));
		Jump = Keyed(TEXT("A_TAG_Tackle_Miss"));
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

		// model pack characters (Tools/Art/hero_*.py): loaded above (LoadHero), they pick the animation tree
		UE_LOG(LogTemp, Display, TEXT("TAG: model pack characters %s (%d in the catalog)"), bHero ? TEXT("enabled") : TEXT("unavailable (Tools/Art/hero_import.py)"), HeroItems.Num());
		// MetaHuman bodies (built by Tools/Art/mh_create.py), driven by runtime IK retargeting.
		if (!bHero && !FParse::Param(FCommandLine::Get(), TEXT("TagNoMH")))
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
	// no UEFN mannequin (and so no pack clips): the simple mannequin stands in, unanimated; every animation the game
	// plays comes from the mocap pack retargets
	const FString A = TEXT("/Game/Characters/Mannequins/");
	Mesh = LoadAsset<USkeletalMesh>(*(A + TEXT("Meshes/SKM_Manny_Simple.SKM_Manny_Simple")));
	UE_LOG(LogTemp, Warning, TEXT("TAG: UEFN mannequin or TAG blend spaces missing; players will not animate"));
}

bool FAthleteAssets::LoopsCleanly(const UAnimSequence* Seq)
{
	// first and last pose close enough to loop: pelvis within 6 cm and 12 deg, the big limbs within 25 deg
	if (!Seq || !Seq->GetSkeleton()) return false;
	const FReferenceSkeleton& Ref = Seq->GetSkeleton()->GetReferenceSkeleton();
	const double L = Seq->GetPlayLength();
	for (const TCHAR* B : {TEXT("pelvis"), TEXT("spine_01"), TEXT("thigh_l"), TEXT("thigh_r"), TEXT("upperarm_l"), TEXT("upperarm_r")})
	{
		const int32 Idx = Ref.FindBoneIndex(B);
		if (Idx == INDEX_NONE) return false;
		FTransform T0, T1;
		Seq->GetBoneTransform(T0, FSkeletonPoseBoneIndex(Idx), 0.0, false);
		Seq->GetBoneTransform(T1, FSkeletonPoseBoneIndex(Idx), L, false);
		const float Deg = FMath::RadiansToDegrees(T0.GetRotation().AngularDistance(T1.GetRotation()));
		if (Deg > (Idx == Ref.FindBoneIndex(TEXT("pelvis")) ? 12.f : 25.f)) return false;
		if (FString(B) == TEXT("pelvis") && FVector::Dist(T0.GetTranslation(), T1.GetTranslation()) > 6.f) return false;
	}
	return true;
}

/**
 * The pack's assets under a category folder (recursive for the root), read from the registry once a run: a forced
 * rescan of the folders and a fresh registry query for every family at each quarter's re-draw held the game thread for
 * over 2 s. An uncooked run fills the registry lazily, hence the one synchronous scan.
 */
struct FPackAssets { TArray<FAssetData> Data; TArray<FString> Names; };
static const FPackAssets& PackAssets(const TCHAR* Category)
{
	static TMap<FString, FPackAssets> Cache;
	if (const FPackAssets* Hit = Cache.Find(Category)) return *Hit;
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	const FString Path = FString::Printf(TEXT("%s/Pack/%s"), *FAthleteAssets::Get().AnimRoot, Category);
	AR.ScanPathsSynchronous({Path}, true);
	FPackAssets& Out = Cache.Add(Category);
	AR.GetAssetsByPath(FName(*Path), Out.Data, FCString::Strcmp(Category, TEXT("")) == 0);
	Out.Names.Reserve(Out.Data.Num());
	for (const FAssetData& D : Out.Data) Out.Names.Add(D.AssetName.ToString());
	return Out;
}

void FAthleteAssets::PackFamily(const TCHAR* Category, std::initializer_list<const TCHAR*> Any, std::initializer_list<const TCHAR*> Any2,
	std::initializer_list<const TCHAR*> None, int32 Max, int32 Seed, bool bLoop, FFamilySink Sink)
{
	const FPackAssets& Assets = PackAssets(Category);
	TArray<FAssetData> Hits;
	for (int32 Ai = 0; Ai < Assets.Data.Num(); ++Ai)
	{
		const FAssetData& D = Assets.Data[Ai];
		const FString& N = Assets.Names[Ai];
		auto AnyOf = [&N](std::initializer_list<const TCHAR*> L) { if (L.size() == 0) return true; for (const TCHAR* T : L) if (N.Contains(T)) return true; return false; };
		bool bOk = AnyOf(Any) && AnyOf(Any2);
		for (const TCHAR* T : None) if (N.Contains(T)) { bOk = false; break; }
		if (bOk) Hits.Add(D);
	}
	Hits.Sort([](const FAssetData& L, const FAssetData& R) { return L.AssetName.LexicalLess(R.AssetName); });
	FRandomStream Rand(Seed);
	for (int32 i = Hits.Num() - 1; i > 0; --i) Hits.Swap(i, Rand.RandRange(0, i));
	FPendingFamily F;
	F.Sink = Sink;
	F.Max = Max * PackScale;
	F.bLoop = bLoop;
	const int32 Want = bLoop ? F.Max * 3 : F.Max + 2; // a looping family rejects many once loaded
	for (const FAssetData& D : Hits)
	{
		if (F.Paths.Num() >= Want) break;
		F.Paths.Add(D.GetSoftObjectPath());
	}
	{
		TArray<TObjectPtr<UAnimationAsset>>& S = Sink();
		if (!SinkBase.Contains(&S)) SinkBase.Add(&S, S.Num());
	}
	PendingFamilies.Add(MoveTemp(F));
}

/** A pack table (pack_index.json / pack_pairs.json) of the tree in play, the UEFN tree's when that tree has none yet
 *  (the tables are keyed by asset name and hold capture times, the same on both skeletons). */
static FString PackJson(const TCHAR* Name)
{
	const FString Mine = FPaths::ProjectContentDir() / FAthleteAssets::Get().AnimRoot.RightChop(6) / TEXT("Pack") / Name; // "/Game/" -> Content
	return FPaths::FileExists(Mine) ? Mine : FPaths::ProjectContentDir() / TEXT("TAG/Anim/Pack") / Name;
}

void FAthleteAssets::LoadPackIndex()
{
	FString Text;
	const FString File = PackJson(TEXT("pack_index.json"));
	if (!FFileHelper::LoadFileToString(Text, *File)) return;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return;
	for (const auto& KV : Root->Values)
		if (KV.Value.IsValid() && KV.Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = KV.Value->AsObject();
			PackIndex.Add(FString(KV.Key), Obj);
		}
}

void FAthleteAssets::PackTimed(const TCHAR* Category, std::initializer_list<const TCHAR*> Any, std::initializer_list<const TCHAR*> Any2,
	std::initializer_list<const TCHAR*> None, int32 Max, int32 Seed, const TCHAR* Field, int32 LeadFrames, float MinAfter, float MinEventSpeed, TArray<FPackClip>& Sink)
{
	const FPackAssets& Assets = PackAssets(Category);
	TArray<FAssetData> Hits;
	for (int32 Ai = 0; Ai < Assets.Data.Num(); ++Ai)
	{
		const FAssetData& D = Assets.Data[Ai];
		const FString& N = Assets.Names[Ai];
		auto AnyOf = [&N](std::initializer_list<const TCHAR*> L) { if (L.size() == 0) return true; for (const TCHAR* T : L) if (N.Contains(T)) return true; return false; };
		bool bOk = AnyOf(Any) && AnyOf(Any2);
		for (const TCHAR* T : None) if (N.Contains(T)) { bOk = false; break; }
		if (bOk) Hits.Add(D);
	}
	Hits.Sort([](const FAssetData& L, const FAssetData& R) { return L.AssetName.LexicalLess(R.AssetName); });
	FRandomStream Rand(Seed);
	for (int32 i = Hits.Num() - 1; i > 0; --i) Hits.Swap(i, Rand.RandRange(0, i));
	FPendingTimed T;
	T.Sink = &Sink;
	for (const FAssetData& D : Hits)
	{
		if (T.Clips.Num() >= Max * PackScale) break;
		const FString N = D.AssetName.ToString();
		const TSharedPtr<FJsonObject>* E = PackIndex.Find(N);
		if (!E || !E->IsValid()) continue;
		FPackClip C;
		C.Name = N;
		C.Len = static_cast<float>((*E)->GetNumberField(TEXT("len")));
		if (Field && *Field)
		{
			double Frame = 0.0;
			if (!(*E)->TryGetNumberField(Field, Frame)) continue;
			double Ms = 0.0;
			if (MinEventSpeed > 0.f && (!(*E)->TryGetNumberField(*(FString(Field) + TEXT("_ms")), Ms) || Ms < MinEventSpeed)) continue;
			C.Start = FMath::Max(0.f, (static_cast<float>(Frame) - LeadFrames) / 30.f);
		}
		if (C.Len - C.Start < MinAfter) continue;
		double Speed = 0.0;
		if ((*E)->TryGetNumberField(TEXT("speed"), Speed)) C.Speed = static_cast<float>(Speed);
		C.Path = D.GetSoftObjectPath();
		T.Clips.Add(C);
	}
	PendingTimed.Add(MoveTemp(T));
}

void FAthleteAssets::LoadPackDoubles()
{
	if (bDoublesLoaded) return;
	bDoublesLoaded = true;
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PackJson(TEXT("pack_dbl.json")))) return;
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Rows)) return;
	auto Load = [](const FString& N)
	{
		return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/TAG/AnimH/Pack/Blocking/%s.%s"), *N, *N), nullptr, LOAD_NoWarn | LOAD_Quiet);
	};
	for (const TSharedPtr<FJsonValue>& V : Rows)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O) continue;
		// (the holds only, a second or more of them: the sheds are the sim's call and play the two-man wins)
		const float T0 = static_cast<float>((*O)->GetNumberField(TEXT("T0"))), T1 = static_cast<float>((*O)->GetNumberField(TEXT("T1")));
		if ((*O)->GetStringField(TEXT("Kind")) != TEXT("hold") || T1 - T0 < 1.0f) continue;
		FDoubleRep R;
		R.D.Reset(Load((*O)->GetStringField(TEXT("D"))));
		R.A.Reset(Load((*O)->GetStringField(TEXT("A"))));
		R.B.Reset(Load((*O)->GetStringField(TEXT("B"))));
		R.T0 = T0;
		R.T1 = T1;
		if (R.D.IsValid() && R.A.IsValid() && R.B.IsValid()) RepDoubles.Add(MoveTemp(R));
	}
	UE_LOG(LogTemp, Display, TEXT("TAG double teams: %d captures"), RepDoubles.Num());
	// and the handoffs
	FString HText;
	TArray<TSharedPtr<FJsonValue>> HRows;
	if (FFileHelper::LoadFileToString(HText, *PackJson(TEXT("pack_handoffs.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(HText), HRows))
		for (const TSharedPtr<FJsonValue>& V : HRows)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V.IsValid() || !V->TryGetObject(O) || !O) continue;
			const FString Cat = (*O)->GetStringField(TEXT("Cat"));
			auto LoadH = [&Cat](const FString& N)
			{
				return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/TAG/AnimH/Pack/%s/%s.%s"), *Cat, *N, *N), nullptr, LOAD_NoWarn | LOAD_Quiet);
			};
			FHandoffRep R;
			R.Q.Reset(LoadH((*O)->GetStringField(TEXT("Q"))));
			R.B.Reset(LoadH((*O)->GetStringField(TEXT("B"))));
			R.T = static_cast<float>((*O)->GetNumberField(TEXT("T")));
			R.bGun = (*O)->GetBoolField(TEXT("Gun"));
			R.Bx = static_cast<float>((*O)->GetNumberField(TEXT("Bx")));
			if (R.Q.IsValid() && R.B.IsValid()) RepHandoffs.Add(MoveTemp(R));
		}
	UE_LOG(LogTemp, Display, TEXT("TAG handoffs: %d captures"), RepHandoffs.Num());
}

void FAthleteAssets::LoadPackPairs(int32 Seed)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PackJson(TEXT("pack_pairs.json")))) return;
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Rows)) return;
	static const TCHAR* Kinds[6] = {TEXT("Drive"), TEXT("Over"), TEXT("Behind"), TEXT("Side"), TEXT("SideL"), TEXT("Dive")};
	TArray<TSharedPtr<FJsonObject>> ByKind[6];
	for (const TSharedPtr<FJsonValue>& V : Rows)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O) continue;
		const FString K = (*O)->GetStringField(TEXT("Kind"));
		for (int32 i = 0; i < 6; ++i) if (K == Kinds[i]) ByKind[i].Add(*O);
	}
	FRandomStream Rand(Seed + 200);
	for (int32 k = 0; k < 6; ++k)
	{
		TArray<TSharedPtr<FJsonObject>>& L = ByKind[k];
		for (int32 i = L.Num() - 1; i > 0; --i) L.Swap(i, Rand.RandRange(0, i));
		int32 Taken = 0;
		for (const TSharedPtr<FJsonObject>& O : L)
		{
			if (Taken >= 10 * PackScale) break;
			const FString TN = O->GetStringField(TEXT("T")), CN = O->GetStringField(TEXT("C"));
			// fouls (facemask, horse collar) are not clean tackles
			if (TN.Contains(TEXT("Facemask"), ESearchCase::IgnoreCase) || TN.Contains(TEXT("HorseCollar"), ESearchCase::IgnoreCase)) continue;
			// a jam (LB chuck) parts the two at once: no tackle
			if (TN.Contains(TEXT("Chuck"), ESearchCase::IgnoreCase)) continue;
			// a stand-up capture (the carrier held up, forward progress stopped, never on the ground: his pelvis stays
			// over 45 cm): the sim's tackle puts him down, so a carrier who stayed on his feet read as a tackle that missed
			{
				double CMinZ = 0.0;
				if (O->TryGetNumberField(TEXT("CMinZ"), CMinZ) && CMinZ > 0.45) continue;
			}
			FPendingPair Q;
			Q.Kind = k;
			Q.TP = FSoftObjectPath(FString::Printf(TEXT("%s/Pack/Tackles/%s.%s"), *AnimRoot, *TN, *TN));
			Q.CP = FSoftObjectPath(FString::Printf(TEXT("%s/Pack/Tackles/%s.%s"), *AnimRoot, *CN, *CN));
			FPackPair& P = Q.P;
			P.Name = TN;
			P.Ox = O->GetNumberField(TEXT("Ox")); P.Oy = O->GetNumberField(TEXT("Oy")); P.Yaw = O->GetNumberField(TEXT("Yaw"));
			P.EndX = O->GetNumberField(TEXT("EndX")); P.EndY = O->GetNumberField(TEXT("EndY"));
			P.TLie = O->GetNumberField(TEXT("TLie")); P.CLie = O->GetNumberField(TEXT("CLie")); P.Start = O->GetNumberField(TEXT("Start"));
			double Sep = 0.0;
			if (O->TryGetNumberField(TEXT("Sep"), Sep)) P.Sep = static_cast<float>(Sep);
			PendingPairs.Add(MoveTemp(Q));
			++Taken;
		}
	}
}

void FAthleteAssets::LoadPackGangs()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PackJson(TEXT("pack_gangs.json")))) return;
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Rows)) return;
	for (const TSharedPtr<FJsonValue>& V : Rows)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O) continue;
		auto Path = [this](const FString& N) { return FSoftObjectPath(FString::Printf(TEXT("%s/Pack/Tackles/%s.%s"), *AnimRoot, *N, *N)); };
		// (a carrier who is held up and never goes down: not a tackle to the ground, see LoadPackPairs)
		{
			double CMinZ = 0.0;
			if ((*O)->TryGetNumberField(TEXT("CMinZ"), CMinZ) && CMinZ > 0.45) continue;
		}
		FPendingGang Q;
		Q.CP = Path((*O)->GetStringField(TEXT("C")));
		Q.TP = Path((*O)->GetStringField(TEXT("T")));
		Q.AP = Path((*O)->GetStringField(TEXT("A")));
		FPackGang& G = Q.G;
		G.Name = (*O)->GetStringField(TEXT("C"));
		auto F = [&](const TCHAR* K) { return static_cast<float>((*O)->GetNumberField(K)); };
		G.Start = F(TEXT("Start")); G.AHit = F(TEXT("AHit"));
		G.Tx = F(TEXT("Tx")); G.Ty = F(TEXT("Ty")); G.TYaw = F(TEXT("TYaw"));
		G.Ax = F(TEXT("Ax")); G.Ay = F(TEXT("Ay")); G.AYaw = F(TEXT("AYaw"));
		G.EndX = F(TEXT("EndX")); G.EndY = F(TEXT("EndY"));
		PendingGangs.Add(MoveTemp(Q));
	}
}

void FAthleteAssets::SelectPack(int32 Seed)
{
	const std::initializer_list<const TCHAR*> Many = {TEXT("2Man"), TEXT("2man"), TEXT("3Man"), TEXT("4Man"), TEXT("Nman"), TEXT("NMan"), TEXT("Group"), TEXT("Mascot"), TEXT("Interaction"), TEXT("Avoid"), TEXT("Bump"), TEXT("Man0"), TEXT("Man1"), TEXT("Man2"), TEXT("Man3")};
	PackFamily(TEXT("Celebrations"), {TEXT("Celeb")}, {}, {TEXT("2Man"), TEXT("2man"), TEXT("3Man"), TEXT("4Man"), TEXT("Nman"), TEXT("NMan"), TEXT("Group"), TEXT("Mascot"), TEXT("Interaction"), TEXT("Avoid"), TEXT("Bump"), TEXT("Man0"), TEXT("Man1"), TEXT("Man2"), TEXT("Man3"), TEXT("Loco"), TEXT("Walk"), TEXT("Jog"), TEXT("Run"), TEXT("on_Ground"), TEXT("Toss_Ball"), TEXT("Fan")}, 24, Seed + 1, false, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackCelebrations; });
	PackFamily(TEXT("PostPlay"), {TEXT("Idle"), TEXT("Fidget"), TEXT("Stand")}, {}, {TEXT("2Man"), TEXT("Man_"), TEXT("Interaction"), TEXT("Jog"), TEXT("Walk"), TEXT("Run"), TEXT("Avoid"), TEXT("Turn"), TEXT("Tackle"), TEXT("Getup"), TEXT("Injured"), TEXT("To_"), TEXT("Trans"),
		// (a man standing in the huddle loops this for ten seconds: only the quiet stands. The family also held the rally wave
		// - an arm thrown up to call teammates over - big-play and safety reactions, a sideline catch, coaches, a home-run
		// swing and a clap; looped by half a huddle they were the arms going every which way a user saw between plays)
		TEXT("WaveOver"), TEXT("Rally"), TEXT("Big_Play"), TEXT("Reactions"), TEXT("Saftey"), TEXT("Safety"), TEXT("Small_Moment"), TEXT("Upset"), TEXT("HandClap"), TEXT("HomeRun"),
		TEXT("Low_Crouch"), TEXT("Coach"), TEXT("Catch"), TEXT("Frustrated")}, 12, Seed + 2, true, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackPostPlayIdles; });
	PackFamily(TEXT("GetUps"), {TEXT("Splat"), TEXT("Knees")}, {TEXT("InPlace"), TEXT("LowIntensity"), TEXT("Low_Intensity")}, {TEXT("BackSplat"), TEXT("Backsplat"), TEXT("Back_Splat"), TEXT("TwoMan"), TEXT("2Man"), TEXT("Man_"), TEXT("PTM_"), TEXT("Idle"), TEXT("BC_")}, 8, Seed + 3, false, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackGetUpsFaceDown; });
	PackFamily(TEXT("GetUps"), {TEXT("BackSplat"), TEXT("Backsplat"), TEXT("Back_Splat"), TEXT("Sitting"), TEXT("Sit_")}, {TEXT("InPlace"), TEXT("LowIntensity"), TEXT("Low_Intensity")}, {TEXT("TwoMan"), TEXT("2Man"), TEXT("Man_"), TEXT("PTM_"), TEXT("Idle"), TEXT("BC_")}, 8, Seed + 4, false, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackGetUpsFaceUp; });
	PackFamily(TEXT("Injuries"), {TEXT("FLD_PLYR_Injury")}, {}, {TEXT("2man"), TEXT("Reaction"), TEXT("Bench"), TEXT("Trainer"), TEXT("Cart")}, 6, Seed + 5, false, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackInjuries; });
	PackFamily(TEXT(""), {TEXT("Breakdown")}, {}, {TEXT("Whiff"), TEXT("Tackle"), TEXT("2man"), TEXT("Trans")}, 8, Seed + 6, true, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return Breakdowns; });
	struct FStanceRule { int32 Pos; std::initializer_list<const TCHAR*> Any; };
	const std::initializer_list<const TCHAR*> StanceNone = {TEXT("Point"), TEXT("Audible"), TEXT("Signal"), TEXT("Trans"), TEXT("Dead_Ball"), TEXT("Walk"), TEXT("Jog"), TEXT("BreakHuddle"), TEXT("Huddle"), TEXT("Adjust_Gear"), TEXT("Check_Sideline"), TEXT("Pump_Up"), TEXT("Shift"), TEXT("Motion"), TEXT("Snap"), TEXT("Cadence"), TEXT("Ready_Stance"), TEXT("Get_Set")};
	const FStanceRule Rules[] = {
		{static_cast<int32>(tag::Pos::WR), {TEXT("Stance_WR"), TEXT("WR_Stance"), TEXT("Fidgets_WR"), TEXT("WR_Idle")}},
		{static_cast<int32>(tag::Pos::TE), {TEXT("Stance_TE"), TEXT("TE_Stance"), TEXT("Fidgets_TE")}},
		{static_cast<int32>(tag::Pos::RB), {TEXT("STANCE_P_HB"), TEXT("Stance_HB"), TEXT("HB_Stance"), TEXT("Fidgets_HB"), TEXT("Fidgets_RB")}},
		{static_cast<int32>(tag::Pos::LB), {TEXT("Idle_PrePlay_LB"), TEXT("LB_Idle"), TEXT("Fidgets_DEF_LB"), TEXT("Fidgets_LB")}},
		{static_cast<int32>(tag::Pos::CB), {TEXT("Pre_Play_DB"), TEXT("DB_Idle"), TEXT("Fidgets_DB"), TEXT("Fidgets_DEF_LB_DB")}},
		{static_cast<int32>(tag::Pos::S), {TEXT("Pre_Play_DB"), TEXT("DB_Idle"), TEXT("Fidgets_DB"), TEXT("Fidgets_DEF_LB_DB")}},
		{static_cast<int32>(tag::Pos::QB), {TEXT("Idle_QBUC"), TEXT("QB_Idle"), TEXT("Idle_QB"), TEXT("QBIdle")}},
		{static_cast<int32>(tag::Pos::K), {TEXT("Kicker_FG"), TEXT("Kicker_Idle")}},
		{static_cast<int32>(tag::Pos::P), {TEXT("Punter"), TEXT("Kicker_Idle")}},
	};
	for (const FStanceRule& R : Rules)
		PackFamily(TEXT("PrePlay"), R.Any, {}, StanceNone, 6, Seed + 10 + R.Pos, true, [this, Pos = R.Pos]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackStances.FindOrAdd(Pos); });
	// the line's three and four point stances: the pack's OL / DL fidget cycles that return to the set
	PackFamily(TEXT("PrePlay"), {TEXT("OL_3pt"), TEXT("OL_3_pt")}, {}, StanceNone, 6, Seed + 40, true, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackStances.FindOrAdd(1000); });
	PackFamily(TEXT("PrePlay"), {TEXT("DL_4pt"), TEXT("DL_3pt"), TEXT("Def_4pt")}, {}, StanceNone, 6, Seed + 41, true, [this]() -> TArray<TObjectPtr<UAnimationAsset>>& { return PackStances.FindOrAdd(1001); });
	// the timed families (PackTimed): started at the capture's own moment less a lead
	LoadPackPairs(Seed);
	LoadPackDoubles();
	LoadPackGangs();
	if (PackIndex.Num())
	{
		const std::initializer_list<const TCHAR*> ThrowNone = {TEXT("Fake"), TEXT("Pump"), TEXT("Drop_"), TEXT("Handoff"), TEXT("Pitch"), TEXT("Scramble"), TEXT("Sack"), TEXT("Fumble"), TEXT("Kneel"), TEXT("Spike"), TEXT("Celeb"), TEXT("Idle"), TEXT("Walk"), TEXT("Jog"), TEXT("2man"), TEXT("Snap"), TEXT("Option"), TEXT("Toss"), TEXT("Lateral"), TEXT("UBRig")};
		PackTimed(TEXT("Quarterback"), {TEXT("Pass_Set"), TEXT("Throw"), TEXT("Pass_")}, {}, ThrowNone, 24, Seed + 50, TEXT("release"), 7, 0.6f, 8.f, PackThrowsSet);
		PackTimed(TEXT("Quarterback"), {TEXT("Quick"), TEXT("Short"), TEXT("Bullet_D2"), TEXT("Slant")}, {TEXT("Pass"), TEXT("Throw")}, ThrowNone, 12, Seed + 51, TEXT("release"), 7, 0.6f, 8.f, PackThrowsQuick);
		PackTimed(TEXT("Quarterback"), {TEXT("Deep"), TEXT("D8"), TEXT("Bomb"), TEXT("Long")}, {TEXT("Pass"), TEXT("Throw")}, ThrowNone, 12, Seed + 52, TEXT("release"), 7, 0.6f, 8.f, PackThrowsDeep);
		PackTimed(TEXT("Quarterback"), {TEXT("Rollout"), TEXT("Roll_"), TEXT("Run_")}, {TEXT("Right"), TEXT("_Rt"), TEXT("HeadRight"), TEXT("_R_")}, ThrowNone, 8, Seed + 53, TEXT("release"), 7, 0.6f, 8.f, PackThrowsRollR);
		PackTimed(TEXT("Quarterback"), {TEXT("Rollout"), TEXT("Roll_"), TEXT("Run_")}, {TEXT("Left"), TEXT("_Lt"), TEXT("HeadLeft"), TEXT("_L_")}, ThrowNone, 8, Seed + 54, TEXT("release"), 7, 0.6f, 8.f, PackThrowsRollL);
		const std::initializer_list<const TCHAR*> KickNone = {TEXT("Fake"), TEXT("Block"), TEXT("Holder"), TEXT("Hold"), TEXT("Snap"), TEXT("Return"), TEXT("Catch"), TEXT("Onside"), TEXT("Celeb"), TEXT("Miss"), TEXT("Hurr"), TEXT("Lineup"), TEXT("LINEUP"), TEXT("Idle"), TEXT("Walk"), TEXT("Jog"), TEXT("Long_Snap")};
		PackTimed(TEXT("Kicking"), {TEXT("kick_off"), TEXT("Kickoff"), TEXT("KICKOFF"), TEXT("KO_")}, {}, KickNone, 8, Seed + 60, TEXT("contact"), 0, 0.8f, 10.f, PackKickoffs);
		PackTimed(TEXT("Kicking"), {TEXT("FG"), TEXT("Field_Goal"), TEXT("FieldGoal"), TEXT("PAT")}, {}, KickNone, 8, Seed + 61, TEXT("contact"), 0, 0.8f, 10.f, PackFieldGoals);
		PackTimed(TEXT("Kicking"), {TEXT("Punt")}, {}, KickNone, 8, Seed + 62, TEXT("contact"), 0, 0.8f, 10.f, PackPunts);
		const std::initializer_list<const TCHAR*> DropNone = {TEXT("Pump"), TEXT("Fake"), TEXT("PA_"), TEXT("Boot"), TEXT("Roll"), TEXT("Bubble"), TEXT("Spike"), TEXT("Pocket"), TEXT("Throw"), TEXT("Pass")};
		PackTimed(TEXT(""), {TEXT("QB_Drop_SG")}, {}, DropNone, 8, Seed + 63, nullptr, 0, 1.0f, 0.f, PackDropsSG);
		PackTimed(TEXT(""), {TEXT("QB_Drop_UC")}, {}, DropNone, 8, Seed + 64, nullptr, 0, 1.0f, 0.f, PackDropsUC);
		const std::initializer_list<const TCHAR*> CatchNone = {TEXT("2man"), TEXT("Drop"), TEXT("Def_"), TEXT("INT"), TEXT("Int_"), TEXT("Swat"), TEXT("Knock"), TEXT("Tip"), TEXT("Bobble"), TEXT("Miss"), TEXT("Fumble"), TEXT("Kick"), TEXT("Punt"), TEXT("Pump"), TEXT("Handoff"), TEXT("Pitch"), TEXT("Almost"), TEXT("Out_of_Bounds"), TEXT("Celeb"), TEXT("Jump_Ball"), TEXT("Juggl")};
		// a diving / layout capture carries the body forward on its own; it plays only when the catch is a dive (the sim ends the
		// play there): on any other catch the body dived ahead while the runner turned upfield, then snapped back
		const std::initializer_list<const TCHAR*> CatchNoDive = {TEXT("2man"), TEXT("Drop"), TEXT("Def_"), TEXT("INT"), TEXT("Int_"), TEXT("Swat"), TEXT("Knock"), TEXT("Tip"), TEXT("Bobble"), TEXT("Miss"), TEXT("Fumble"), TEXT("Kick"), TEXT("Punt"), TEXT("Pump"), TEXT("Handoff"), TEXT("Pitch"), TEXT("Almost"), TEXT("Out_of_Bounds"), TEXT("Celeb"), TEXT("Jump_Ball"), TEXT("Juggl"), TEXT("Dive"), TEXT("Layout"), TEXT("Slide"), TEXT("Fall"),
			// (captures that end with the catcher on the turf: played on a catch the sim runs on from, he went to the ground,
			// got up and ran on - "SecureBall_InFront_GoToGround", p73 c035)
			// ("Knees" in a name is the ball's height, a low catch on the run: kept)
			TEXT("Ground")};
		PackTimed(TEXT("Catching"), {TEXT("Catch_Run"), TEXT("InStride"), TEXT("In_Stride")}, {}, CatchNoDive, 16, Seed + 70, TEXT("release"), 4, 0.7f, 0.f, PackCatchStride);
		PackTimed(TEXT("Catching"), {TEXT("Jump"), TEXT("AboveHead"), TEXT("HighPoint"), TEXT("High_Point")}, {TEXT("Catch")}, CatchNoDive, 12, Seed + 71, TEXT("release"), 4, 0.7f, 0.f, PackCatchHigh);
		PackTimed(TEXT("Catching"), {TEXT("Low"), TEXT("Shoestring"), TEXT("Scoop"), TEXT("Shoe")}, {TEXT("Catch")}, CatchNoDive, 8, Seed + 72, TEXT("release"), 4, 0.7f, 0.f, PackCatchLow);
		PackTimed(TEXT("Catching"), {TEXT("OverShoulder"), TEXT("Over_Shoulder"), TEXT("OvrShldr"), TEXT("BackShoulder"), TEXT("Back_Shoulder")}, {}, CatchNoDive, 12, Seed + 73, TEXT("release"), 4, 0.7f, 0.f, PackCatchOverShoulder);
		PackTimed(TEXT("Catching"), {TEXT("Sideline")}, {TEXT("Catch")}, CatchNoDive, 8, Seed + 74, TEXT("release"), 4, 0.7f, 0.f, PackCatchSideline);
		PackTimed(TEXT("Catching"), {TEXT("Catch_Stand"), TEXT("Stand_"), TEXT("Strafe"), TEXT("Chest")}, {TEXT("Catch")}, CatchNoDive, 12, Seed + 75, TEXT("release"), 4, 0.7f, 0.f, PackCatchStand);
		PackTimed(TEXT("Catching"), {TEXT("Bkpd"), TEXT("Comeback"), TEXT("Curl")}, {TEXT("Catch")}, CatchNoDive, 8, Seed + 76, TEXT("release"), 4, 0.7f, 0.f, PackCatchComeback);
		PackTimed(TEXT("Catching"), {TEXT("Dive"), TEXT("Layout")}, {TEXT("Catch")}, CatchNone, 8, Seed + 77, TEXT("release"), 6, 0.9f, 0.f, PackCatchDive);
		// (its own exclusions: CatchNoDive's "Tip" took every FingerTip; and these captures end a beat after the ball is
		// in, 0.15-0.3 s, where the everyday catches run on 0.7 s: shorter ones only blend back into the run sooner)
		{
			const std::initializer_list<const TCHAR*> SwatNone = {TEXT("2man"), TEXT("MPCI"), TEXT("Dive"), TEXT("Diving"), TEXT("Fall"), TEXT("Ground"), TEXT("Def_")};
			PackTimed(TEXT("Catching"), {TEXT("Swat_Run")}, {}, SwatNone, 10, Seed + 120, TEXT("release"), 4, 0.3f, 0.f, PackSwatRun);
			PackTimed(TEXT("Quarterback"), {TEXT("Pass_Shovel_Set")}, {TEXT("RightHand")}, {}, 4, Seed + 132, TEXT("release"), 7, 0.3f, 0.f, PackShovelSet);
			PackTimed(TEXT("Quarterback"), {TEXT("Pass_Shovel_Rollout"), TEXT("Pass_Shovel_Scramble")}, {TEXT("RightHand")}, {}, 8, Seed + 133, TEXT("release"), 7, 0.3f, 0.f, PackShovelMove);
			PackTimed(TEXT(""), {TEXT("PumpFake")}, {}, {TEXT("Celeb")}, 8, Seed + 134, nullptr, 0, 0.3f, 0.f, PackPumpFakes);
			const std::initializer_list<const TCHAR*> KcNone = {TEXT("Idle"), TEXT("Fake"), TEXT("Fair"), TEXT("Celeb"), TEXT("Muff"), TEXT("Drop")};
			PackTimed(TEXT("Kicking"), {TEXT("KickCatch"), TEXT("KickReturn_Catch"), TEXT("from_punt")}, {TEXT("Stand"), TEXT("Catch_Var"), TEXT("D3_catch")}, KcNone, 8, Seed + 130,
				TEXT("release"), 6, 0.4f, 0.f, PackKickCatchStand);
			PackTimed(TEXT("Kicking"), {TEXT("KickCatch_Run"), TEXT("KickCatch_Chest_Run"), TEXT("KickCatch_Helmet_Run"), TEXT("Returner_run")}, {}, KcNone, 8, Seed + 131,
				TEXT("release"), 6, 0.4f, 0.f, PackKickCatchRun);
			PackTimed(TEXT("Catching"), {TEXT("Swat_Strafe")}, {}, SwatNone, 10, Seed + 121, TEXT("release"), 4, 0.3f, 0.f, PackSwatStrafe);
			PackTimed(TEXT("Catching"), {TEXT("Swat_Backpedal"), TEXT("Swat_Stand")}, {}, SwatNone, 8, Seed + 122, TEXT("release"), 4, 0.3f, 0.f, PackSwatBack);
			PackTimed(TEXT("Catching"), {TEXT("_INT"), TEXT("Interception"), TEXT("INT_")}, {},
				{TEXT("2man"), TEXT("MPCI"), TEXT("Dive"), TEXT("Diving"), TEXT("Fall"), TEXT("Ground"), TEXT("Def_"), TEXT("Swat"), TEXT("Drop"), TEXT("2014_")}, 10, Seed + 123,
				TEXT("release"), 4, 0.3f, 0.f, PackIntCatch);
		}
		PackTimed(TEXT("Catching"), {TEXT("OneHand"), TEXT("One_Hand"), TEXT("one_hand"), TEXT("FingerTip"), TEXT("Fingertip"), TEXT("Leap")}, {TEXT("Catch")},
			{TEXT("2man"), TEXT("Drop"), TEXT("Def_"), TEXT("INT"), TEXT("Int_"), TEXT("Swat"), TEXT("Knock"), TEXT("Bobble"), TEXT("Miss"), TEXT("Fumble"), TEXT("Juggl"),
			 TEXT("Dive"), TEXT("Layout"), TEXT("Slide"), TEXT("Fall"), TEXT("Ground"), TEXT("Celeb"), TEXT("Kick"), TEXT("Punt")}, 16, Seed + 78,
			TEXT("release"), 4, 0.15f, 0.f, PackCatchSpectacular);
		const std::initializer_list<const TCHAR*> TackleNone = {TEXT("Dive"), TEXT("Strip"), TEXT("Whiff"), TEXT("Miss"), TEXT("Sack"), TEXT("Pile"), TEXT("Gang"), TEXT("Sideline"), TEXT("Spin"), TEXT("Brkn"), TEXT("Broken"), TEXT("Hurdle"), TEXT("Truck"), TEXT("StiffArm"), TEXT("Celeb"), TEXT("Fumble"),
			// a facemask tackle is a 15-yard foul, not a clean tackle: kept out until the penalty that calls for it exists
			TEXT("FaceMask"), TEXT("Facemask"), TEXT("Horsecollar"), TEXT("HorseCollar")};
		PackTimed(TEXT("Tackles"), {TEXT("_D1"), TEXT("_D2"), TEXT("_D8")}, {TEXT("__P01")}, TackleNone, 12, Seed + 80, TEXT("hit"), 2, 0.8f, 0.f, PackTackledFront);
		PackTimed(TEXT("Tackles"), {TEXT("_D5"), TEXT("_D4"), TEXT("_D6")}, {TEXT("__P01")}, TackleNone, 12, Seed + 81, TEXT("hit"), 2, 0.8f, 0.f, PackTackledBehind);
		PackTimed(TEXT("Tackles"), {TEXT("_D3"), TEXT("_D7")}, {TEXT("__P01")}, TackleNone, 12, Seed + 82, TEXT("hit"), 2, 0.8f, 0.f, PackTackledSide);
		PackTimed(TEXT("Tackles"), {TEXT("Tackle")}, {TEXT("__P02")}, TackleNone, 16, Seed + 83, TEXT("release"), 6, 0.8f, 3.f, PackTacklers);
		PackTimed(TEXT(""), {TEXT("Whiff"), TEXT("Miss_Tackle"), TEXT("MissTackle"), TEXT("Missed")}, {}, {TEXT("Strip"), TEXT("2man"), TEXT("Catch"), TEXT("Kick"), TEXT("Celeb")}, 8, Seed + 84, TEXT("hit"), 10, 0.8f, 0.f, PackWhiffs);
		PackTimed(TEXT("BrokenTackles"), {TEXT("Stumble"), TEXT("Brkn"), TEXT("Broken")}, {TEXT("__P01")}, {TEXT("Strip"), TEXT("Fumble"), TEXT("Spin"), TEXT("Hurdle")}, 12, Seed + 85, TEXT("onset"), 3, 0.8f, 0.f, PackStumbles);
		const std::initializer_list<const TCHAR*> JukeNone = {TEXT("Spin"), TEXT("Hurdle"), TEXT("Truck"), TEXT("StiffArm"), TEXT("Stiff_Arm"), TEXT("Hesi"), TEXT("2man"), TEXT("Strip"), TEXT("Fumble"), TEXT("Celeb"), TEXT("Catch"), TEXT("Tackle")};
		PackTimed(TEXT("BallCarrier"), {TEXT("Juke"), TEXT("Cut"), TEXT("SkillMoves")}, {TEXT("Left"), TEXT("_L_"), TEXT("Lt_"), TEXT("_L0"), TEXT("D7"), TEXT("D8")}, JukeNone, 12, Seed + 90, TEXT("onset"), 5, 0.6f, 0.f, PackJukesL);
		PackTimed(TEXT("BallCarrier"), {TEXT("Juke"), TEXT("Cut"), TEXT("SkillMoves")}, {TEXT("Right"), TEXT("_R_"), TEXT("Rt_"), TEXT("_R0"), TEXT("D3"), TEXT("D2")}, JukeNone, 12, Seed + 91, TEXT("onset"), 5, 0.6f, 0.f, PackJukesR);
		PackTimed(TEXT("BallCarrier"), {TEXT("Spin")}, {TEXT("Left"), TEXT("_L_"), TEXT("Lt_")}, {TEXT("2man"), TEXT("Strip")}, 8, Seed + 92, TEXT("onset"), 5, 0.6f, 0.f, PackSpinsL);
		PackTimed(TEXT("BallCarrier"), {TEXT("Spin")}, {TEXT("Right"), TEXT("_R_"), TEXT("Rt_")}, {TEXT("2man"), TEXT("Strip")}, 8, Seed + 93, TEXT("onset"), 5, 0.6f, 0.f, PackSpinsR);
		PackTimed(TEXT(""), {TEXT("Truck"), TEXT("LowerShoulder"), TEXT("Lower_Shoulder"), TEXT("Bruiser_Shoulder")}, {}, {TEXT("2man"), TEXT("Strip"), TEXT("Catch")}, 8, Seed + 94, TEXT("onset"), 5, 0.6f, 0.f, PackTrucks);
		PackTimed(TEXT(""), {TEXT("Hurdle")}, {}, {TEXT("2man"), TEXT("Strip")}, 8, Seed + 95, TEXT("onset"), 5, 0.6f, 0.f, PackHurdles);
		PackTimed(TEXT(""), {TEXT("StiffArm"), TEXT("Stiff_Arm"), TEXT("Stiffarm")}, {}, {TEXT("2man"), TEXT("Strip")}, 8, Seed + 96, TEXT("release"), 6, 0.6f, 0.f, PackStiffArmsL);
		PackTimed(TEXT(""), {TEXT("StiffArm"), TEXT("Stiff_Arm"), TEXT("Stiffarm")}, {}, {TEXT("2man"), TEXT("Strip")}, 8, Seed + 97, TEXT("release"), 6, 0.6f, 0.f, PackStiffArmsR);
		// (the Dline "Stationary_to_<side>" captures spin the man through a turn the in-place import cannot follow: a
		// one-frame jump of the body and a metre of foot slide a step in the full-game captures)
		const std::initializer_list<const TCHAR*> BlockNone = {TEXT("2man"), TEXT("Celeb"), TEXT("Idle"), TEXT("Stance"), TEXT("Double"), TEXT("3Man"), TEXT("Jammed"), TEXT("FakeFG"), TEXT("Option"), TEXT("Stationary_to")};
		PackTimed(TEXT("Blocking"), {TEXT("Win")}, {TEXT("Left"), TEXT("_L_")}, BlockNone, 10, Seed + 100, nullptr, 0, 0.8f, 0.f, PackRushWinsL);
		PackTimed(TEXT("Blocking"), {TEXT("Win")}, {TEXT("Right"), TEXT("_R_")}, BlockNone, 10, Seed + 101, nullptr, 0, 0.8f, 0.f, PackRushWinsR);
		PackTimed(TEXT("Blocking"), {TEXT("Loss")}, {TEXT("Left"), TEXT("_L_")}, BlockNone, 10, Seed + 102, nullptr, 0, 0.8f, 0.f, PackBeatenL);
		PackTimed(TEXT("Blocking"), {TEXT("Loss")}, {TEXT("Right"), TEXT("_R_")}, BlockNone, 10, Seed + 103, nullptr, 0, 0.8f, 0.f, PackBeatenR);
		PackTimed(TEXT("Blocking"), {TEXT("Pancake"), TEXT("Impact"), TEXT("Stand_Loss_Big")}, {}, BlockNone, 8, Seed + 104, TEXT("hit"), 4, 0.8f, 0.f, PackPancakes);
		PackTimed(TEXT(""), {TEXT("InitialMove"), TEXT("InitMove"), TEXT("Get_Off"), TEXT("GetOff"), TEXT("RunBlock_D1")}, {TEXT("OL"), TEXT("DL"), TEXT("Dline"), TEXT("Block")}, {TEXT("KickSlide"), TEXT("lateral"), TEXT("2man"), TEXT("Idle"), TEXT("Celeb"), TEXT("Stance")}, 10, Seed + 105, nullptr, 0, 0.8f, 0.f, PackGetOffs);
		PackTimed(TEXT(""), {TEXT("PassBlock_OL_InitialMove")}, {}, {TEXT("lateral"), TEXT("2man"), TEXT("DbleTm")}, 10, Seed + 106, nullptr, 0, 0.8f, 0.f, PackGetOffsPass);
		PackTimed(TEXT(""), {TEXT("InitMove_RunBlock"), TEXT("OL_InitMove_Zone"), TEXT("RunBlock_D1")}, {}, {TEXT("lateral"), TEXT("2man"), TEXT("Pull")}, 10, Seed + 107, nullptr, 0, 0.8f, 0.f, PackGetOffsRun);
		PackTimed(TEXT(""), {TEXT("InitMove_DL"), TEXT("DL_InitialMove")}, {}, {TEXT("lateral"), TEXT("2man"), TEXT("OptionRead")}, 10, Seed + 108, nullptr, 0, 0.8f, 0.f, PackGetOffsDL);
		PackTimed(TEXT("Blocking"), {TEXT("Block_Impact"), TEXT("GetEng")}, {TEXT("__P01")}, {TEXT("Loss_Big"), TEXT("Pancake"), TEXT("3man")}, 12, Seed + 109, TEXT("contact"), 4, 0.8f, 0.f, PackEngageOff);
		PackTimed(TEXT("Blocking"), {TEXT("Block_Impact"), TEXT("GetEng")}, {TEXT("__P02")}, {TEXT("Loss_Big"), TEXT("Pancake"), TEXT("3man")}, 12, Seed + 110, TEXT("contact"), 4, 0.8f, 0.f, PackEngageDef);
	}
}

void FAthleteAssets::ReseedPack(int32 Seed)
{
	if (bPackLoading) return; // the last draw is still on its way; the next quarter tries again
	PendingFamilies.Reset();
	PendingTimed.Reset();
	PendingPairs.Reset();
	PendingGangs.Reset();
	const double SelectT0 = FPlatformTime::Seconds();
	SelectPack(Seed);
	UE_LOG(LogTemp, Display, TEXT("TAG: pack draw selected in %.0f ms"), (FPlatformTime::Seconds() - SelectT0) * 1000.0);
	// the last draw's celebrations leave the shared list first: PackCelebrations is one of the family sinks truncated
	// below, and a clip left in Celebrations is unrooted when this draw lands, collected at the next GC and played by
	// the next bench cheer (the access violation in SidelineCheer after a quarter's re-draw)
	for (const TObjectPtr<UAnimationAsset>& O : PackCelebrations) Celebrations.Remove(O.Get());
	PackCelebrations.Reset();
	// the families refill from the placed clips up; the previous draw stays resident until this one has landed
	for (FPendingFamily& F : PendingFamilies)
	{
		TArray<TObjectPtr<UAnimationAsset>>& S = F.Sink();
		const int32* Base = SinkBase.Find(&S);
		S.SetNum(Base ? *Base : 0);
	}
	for (FPendingTimed& T : PendingTimed) T.Sink->Reset();
	for (TArray<FPackPair>& L : PackPairs) L.Reset();
	PackGangs.Reset();
	PackPlaced.Reset();
	// the queue: families interleaved, so a budget cut still leaves every family part of its draw
	PackQueue.Reset();
	PackQueuePos = 0;
	PackChunk = 0;
	TSet<FSoftObjectPath> Seen;
	auto Push = [&](const FSoftObjectPath& P) { if (!Seen.Contains(P)) { Seen.Add(P); PackQueue.Add(P); } };
	bool bMore = true;
	for (int32 i = 0; bMore; ++i)
	{
		bMore = false;
		for (const FPendingFamily& F : PendingFamilies) if (F.Paths.IsValidIndex(i)) { Push(F.Paths[i]); bMore = true; }
		for (const FPendingTimed& T : PendingTimed) if (T.Clips.IsValidIndex(i)) { Push(T.Clips[i].Path); bMore = true; }
		if (i < 10 * PackScale)
			for (int32 k = 0; k < 6; ++k)
			{
				int32 n = 0;
				for (const FPendingPair& Q : PendingPairs)
					if (Q.Kind == k && n++ == i) { Push(Q.TP); Push(Q.CP); bMore = true; break; }
			}
		if (PendingGangs.IsValidIndex(i)) { Push(PendingGangs[i].CP); Push(PendingGangs[i].TP); Push(PendingGangs[i].AP); bMore = true; }
	}
	if (PackBudgetMB <= 0.0)
	{
		PackBudgetMB = 2500.0;
		FParse::Value(FCommandLine::Get(), TEXT("TagPackMB="), PackBudgetMB);
	}
	PackLoadedMB = 0.0;
	UE_LOG(LogTemp, Display, TEXT("TAG: pack batch %d: %d captures queued (seed %d, scale %d), loading behind the game, up to %.0f MB of clips (%.0f MB in use now)"), PackBatch, PackQueue.Num(), Seed, PackScale, PackBudgetMB, FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0));
	if (PackQueue.Num() == 0) return;
	bPackLoading = true;
	PackT0 = FPlatformTime::Seconds();
	LoadNextPackChunk();
}

/**
 * While a pack draw streams in behind the game the loader gets 1 ms of the game thread a frame and gives back what it
 * does not use (the engine default is 5 ms, always used in full). The clips land a little later and the frame rate holds
 * (Tools/Perf/ab.ps1 over a streaming window: 75 -> 92 fps). The engine's own values come back once the draw is in.
 */
static void PackLoadBudget(bool bStreaming)
{
	static bool bOn = false;
	static float SavedLimit = 5.f;
	static int32 SavedFull = 1;
	IConsoleVariable* Limit = IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingTimeLimit"));
	IConsoleVariable* Full = IConsoleManager::Get().FindConsoleVariable(TEXT("s.AsyncLoadingUseFullTimeLimit"));
	if (bStreaming == bOn || !Limit || !Full) return;
	if (bStreaming)
	{
		SavedLimit = Limit->GetFloat();
		SavedFull = Full->GetInt();
		Limit->Set(1.f, ECVF_SetByCode);
		Full->Set(0, ECVF_SetByCode);
	}
	else
	{
		Limit->Set(SavedLimit, ECVF_SetByCode);
		Full->Set(SavedFull, ECVF_SetByCode);
	}
	bOn = bStreaming;
}

void FAthleteAssets::LoadNextPackChunk()
{
	PackLoadBudget(true);
	// the budget counts the clips themselves (their resource size), not the process, which grows with the world
	// loading alongside; the process still gets a ceiling at 80 % of physical memory so a wrong estimate cannot swap
	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
	const double UsedMB = Mem.UsedPhysical / (1024.0 * 1024.0), CeilingMB = Mem.TotalPhysical / (1024.0 * 1024.0) * 0.80;
	const int32 Rooted = BatchRoots[PackBatch % 2].Num();
	if (PackChunk > 0 && PackChunk % 4 == 0)
		UE_LOG(LogTemp, Display, TEXT("TAG: pack batch %d: %d of %d captures, %d rooted, %.0f MB of clips (%.1f MB a clip), %.0f MB used, %.0f s"), PackBatch, PackQueuePos, PackQueue.Num(), Rooted,
			PackLoadedMB, Rooted ? PackLoadedMB / Rooted : 0.0, UsedMB, FPlatformTime::Seconds() - PackT0);
	const bool bDone = PackQueuePos >= PackQueue.Num();
	if (bDone || PackLoadedMB > PackBudgetMB || UsedMB > CeilingMB)
	{
		UE_LOG(LogTemp, Display, TEXT("TAG: pack batch %d %s: %d of %d captures in %.0f s, %d rooted, %.0f MB of clips (%.0f MB used)"), PackBatch,
			bDone ? TEXT("complete") : UsedMB > CeilingMB ? TEXT("stopped at the memory ceiling") : TEXT("stopped at the clip budget"), PackQueuePos, PackQueue.Num(), FPlatformTime::Seconds() - PackT0, Rooted, PackLoadedMB, UsedMB);
		OnPackBatchLoaded();
		return;
	}
	TArray<FSoftObjectPath> Chunk;
	for (int32 i = 0; i < 48 && PackQueuePos < PackQueue.Num(); ++i) Chunk.Add(PackQueue[PackQueuePos++]);
	PackHandle = PackStreamer.RequestAsyncLoad(Chunk, FStreamableDelegate::CreateLambda([this]
	{
		FillPackLoaded();
		LoadNextPackChunk();
	}));
	if (!PackHandle.IsValid()) { FillPackLoaded(); LoadNextPackChunk(); }
}

void FAthleteAssets::FillPackLoaded()
{
	// whatever has landed goes into its family now (the game draws on it from this frame); the rest waits
	auto Keep = [this](UAnimSequence* S)
	{
		S->AddToRoot();
		BatchRoots[PackBatch % 2].Add(S);
		// the sequence's own data: an uncooked capture is held raw (its data model) and compressed
		double Bytes = S->GetApproxCompressedSize();
#if WITH_EDITOR
		Bytes += S->GetApproxRawSize();
#endif
		PackLoadedMB += Bytes / (1024.0 * 1024.0);
	};
	for (FPendingFamily& F : PendingFamilies)
	{
		TArray<TObjectPtr<UAnimationAsset>>& S = F.Sink();
		const int32* Base = SinkBase.Find(&S);
		const int32 B = Base ? *Base : 0;
		for (const FSoftObjectPath& P : F.Paths)
		{
			if (S.Num() - B >= F.Max) break;
			if (PackPlaced.Contains(P)) continue;
			UAnimSequence* Seq = Cast<UAnimSequence>(P.ResolveObject());
			if (!Seq || !Seq->HasAnyFlags(RF_LoadCompleted)) continue;
			PackPlaced.Add(P);
			if (Seq->GetPlayLength() < 0.5f) continue;
			if (F.bLoop && !LoopsCleanly(Seq)) continue;
			Keep(Seq);
			S.Add(Seq);
			if (&S == &PackCelebrations) Celebrations.Add(Seq);
		}
	}
	PackTimedCount = 0;
	for (FPendingTimed& T : PendingTimed)
	{
		for (FPackClip& C : T.Clips)
		{
			if (C.Anim) continue;
			UAnimSequence* Seq = Cast<UAnimSequence>(C.Path.ResolveObject());
			if (!Seq || !Seq->HasAnyFlags(RF_LoadCompleted)) continue;
			Keep(Seq);
			C.Anim = Seq;
			T.Sink->Add(C);
		}
		PackTimedCount += T.Sink->Num();
	}
	// a capture whose root jumps (a tracking glitch: the root of eight of the pack's 381 tackle pairs steps 1-2 m in a
	// frame, hands at 30-60 m/s in the capture's own data) or runs on for tens of metres carries the falling body
	// with it: a tackled back on the turf gliding 10 yd upfield at 7 m/s (p16, clip 5). The fall follows the root
	// (AAthlete::TickFall), so such a capture cannot play a fall.
	auto RootSane = [](const UAnimSequence* Seq, float Start)
	{
		const double Len = Seq->GetPlayLength();
		const FAnimExtractContext Ctx(0.0, true);
		double Total = 0.0;
		for (double S = FMath::Max(0.0, static_cast<double>(Start)); S + 1e-3 < Len; S += 1.0 / 30.0)
		{
			const double Step = Seq->ExtractRootMotionFromRange(S, FMath::Min(Len, S + 1.0 / 30.0), Ctx).GetTranslation().Size2D();
			if (Step > 50.0) return false; // 15 m/s
			Total += Step;
		}
		return Total < 1200.0;
	};
	for (FPendingPair& Q : PendingPairs)
	{
		if (Q.P.T || Q.bBad) continue;
		UAnimSequence* T = Cast<UAnimSequence>(Q.TP.ResolveObject());
		UAnimSequence* C = Cast<UAnimSequence>(Q.CP.ResolveObject());
		if (!T || !C || !T->HasAnyFlags(RF_LoadCompleted) || !C->HasAnyFlags(RF_LoadCompleted)) continue;
		if (!RootSane(T, Q.P.Start) || !RootSane(C, Q.P.Start))
		{
			Q.bBad = true;
			UE_LOG(LogTemp, Display, TEXT("TAG: tackle pair %s left out: its root jumps or runs on"), *C->GetName());
			continue;
		}
		Keep(T);
		Keep(C);
		Q.P.T = T;
		Q.P.C = C;
		PackPairs[Q.Kind].Add(Q.P);
	}
	for (FPendingGang& Q : PendingGangs)
	{
		if (Q.G.C || Q.bBad) continue;
		UAnimSequence* C = Cast<UAnimSequence>(Q.CP.ResolveObject());
		UAnimSequence* T = Cast<UAnimSequence>(Q.TP.ResolveObject());
		UAnimSequence* G = Cast<UAnimSequence>(Q.AP.ResolveObject());
		if (!C || !T || !G || !C->HasAnyFlags(RF_LoadCompleted) || !T->HasAnyFlags(RF_LoadCompleted) || !G->HasAnyFlags(RF_LoadCompleted)) continue;
		if (!RootSane(C, Q.G.Start) || !RootSane(T, Q.G.Start) || !RootSane(G, Q.G.Start))
		{
			Q.bBad = true;
			UE_LOG(LogTemp, Display, TEXT("TAG: gang tackle %s left out: its root jumps or runs on"), *C->GetName());
			continue;
		}
		Keep(C);
		Keep(T);
		Keep(G);
		Q.G.C = C;
		Q.G.T = T;
		Q.G.A = G;
		PackGangs.Add(Q.G);
	}
	++PackChunk;
}

void FAthleteAssets::OnPackBatchLoaded()
{
	// this draw is in; the one before the previous goes (a clip still playing is held by its mesh), and anything the
	// two live draws share stays rooted
	{
		TArray<TObjectPtr<UAnimSequence>>& Old = BatchRoots[(PackBatch + 1) % 2];
		for (const TObjectPtr<UAnimSequence>& S : Old) if (S) S->RemoveFromRoot();
		Old.Reset();
		for (const TObjectPtr<UAnimSequence>& S : BatchRoots[PackBatch % 2]) if (S) S->AddToRoot();
	}
	++PackBatch;
	PendingFamilies.Reset();
	PendingTimed.Reset();
	PendingPairs.Reset();
	PendingGangs.Reset();
	PackHandle.Reset();
	PackQueue.Reset();
	bPackLoading = false;
	PackLoadBudget(false);
	PackLibraryCount = PackCelebrations.Num() + PackPostPlayIdles.Num() + PackGetUpsFaceDown.Num() + PackGetUpsFaceUp.Num() + PackInjuries.Num();
	for (const auto& KV : PackStances) PackLibraryCount += KV.Value.Num();
	UE_LOG(LogTemp, Display, TEXT("TAG: pack library tackle pairs: drive %d over %d behind %d side %d / %d dive %d, gang tackles %d"),
		PackPairs[0].Num(), PackPairs[1].Num(), PackPairs[2].Num(), PackPairs[3].Num(), PackPairs[4].Num(), PackPairs[5].Num(), PackGangs.Num());
	UE_LOG(LogTemp, Display, TEXT("TAG: pack library timed families: %d captures (throws %d/%d/%d/%d/%d, kicks %d/%d/%d, drops %d/%d, catches %d/%d/%d/%d/%d/%d/%d/%d, tackled %d/%d/%d, tacklers %d, whiffs %d, stumbles %d, jukes %d/%d, spins %d/%d, trucks %d, hurdles %d, stiff arms %d/%d, rush wins %d/%d, beaten %d/%d, pancakes %d, get-offs %d; index %d)"),
		PackTimedCount, PackThrowsSet.Num(), PackThrowsQuick.Num(), PackThrowsDeep.Num(), PackThrowsRollR.Num(), PackThrowsRollL.Num(), PackKickoffs.Num(), PackFieldGoals.Num(), PackPunts.Num(),
		PackDropsSG.Num(), PackDropsUC.Num(), PackCatchStride.Num(), PackCatchHigh.Num(), PackCatchLow.Num(), PackCatchOverShoulder.Num(), PackCatchSideline.Num(), PackCatchStand.Num(), PackCatchComeback.Num(), PackCatchDive.Num() * 100 + PackCatchSpectacular.Num(),
		PackTackledFront.Num(), PackTackledBehind.Num(), PackTackledSide.Num(), PackTacklers.Num(), PackWhiffs.Num(), PackStumbles.Num(), PackJukesL.Num(), PackJukesR.Num(), PackSpinsL.Num(), PackSpinsR.Num(),
		PackTrucks.Num(), PackHurdles.Num(), PackStiffArmsL.Num(), PackStiffArmsR.Num(), PackRushWinsL.Num(), PackRushWinsR.Num(), PackBeatenL.Num(), PackBeatenR.Num(), PackPancakes.Num(), PackGetOffs.Num(), PackIndex.Num());
	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
	UE_LOG(LogTemp, Display, TEXT("TAG: pack library in play: %d celebrations, %d post-play idles, %d / %d get-ups, %d injuries, %d breakdowns, %d stances (batch %d, %d clips rooted, %.0f MB used)"),
		PackCelebrations.Num(), PackPostPlayIdles.Num(), PackGetUpsFaceDown.Num(), PackGetUpsFaceUp.Num(), PackInjuries.Num(), Breakdowns.Num(), PackLibraryCount - PackCelebrations.Num() - PackPostPlayIdles.Num() - PackGetUpsFaceDown.Num() - PackGetUpsFaceUp.Num() - PackInjuries.Num(),
		PackBatch - 1, BatchRoots[(PackBatch + 1) % 2].Num(), Mem.UsedPhysical / (1024.0 * 1024.0));
}

void FAthleteAssets::LoadMotionMatching()
{
	// Epic's Game Animation Sample motion matching data (free, UEFN mannequin), Dense variant: every database is
	// searched against the same normalization set, so costs compare across databases
	// every set prefers its TAG copy (Tools/Art/mm_build_dbs.py: the sample's clips plus the pack's starts, stops,
	// pivots and idles with root motion, mocap_map.LOCO_DB), else the sample's
	// the TAG databases only (the pack's clips, Tools/Art/mm_build_dbs.py); without them motion matching is off and the
	// blend spaces (also the pack's) drive the body
	const FString MM = AnimRoot + TEXT("/MM/");
	auto Db = [&MM](const FString& N) { return LoadAsset<UPoseSearchDatabase>(*FString::Printf(TEXT("%sPSD_TAG_%s.PSD_TAG_%s"), *MM, *N, *N), false); };
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
		// the TAG copies carry the football locomotion from the mocap pack (Tools/Art/mm_build_dbs.py)
		UPoseSearchDatabase* Loops = Db(FString::Printf(TEXT("%s_Loops"), Gait[g]));
		if (UPoseSearchDatabase* X = Loops) { MMMove[g].Add(X); MMStart[g].Add(X); MMPivot[g].Add(X); }
		if (UPoseSearchDatabase* X = Db(FString::Printf(TEXT("Stand_%s_Starts"), Gait[g]))) MMStart[g].Add(X);
		for (const TCHAR* K : {TEXT("Pivots"), TEXT("SpinTransition")})
			if (UPoseSearchDatabase* X = Db(FString::Printf(TEXT("Stand_%s_%s"), Gait[g], K))) MMPivot[g].Add(X);
		if (UPoseSearchDatabase* X = Db(FString::Printf(TEXT("Stand_%s_Stops"), Gait[g]))) MMStop[g].Add(X);
		MMStop[g].Add(Idles);
		// (no turn-in-place: standing players re-face through the actor's rotation; with the root offset, turn clips
		// fed back on themselves and the search flipped between left and right turns)
		(void)Turn;
		// the ball carrier's own loops (Madden's MG_BC_* captures): a man with the ball ran on the empty-handed loops with a
		// procedural tuck over them, and the ball read as fused to his hand (Fred, 16 Nov)
		MMBallMove[g].Reset();
		MMBallPivot[g].Reset();
		MMBallStart[g].Reset();
		if (UPoseSearchDatabase* BL = Db(FString::Printf(TEXT("BC_%s_Loops"), Gait[g])))
		{
			MMBallMove[g] = {BL};
			MMBallPivot[g] = MMPivot[g];
			for (UPoseSearchDatabase*& X : MMBallPivot[g]) if (X == Loops) X = BL;
			MMBallStart[g] = MMStart[g];
			for (UPoseSearchDatabase*& X : MMBallStart[g]) if (X == Loops) X = BL;
			All.AddUnique(BL);
		}
		bOk = MMMove[g].Num() >= 1 && MMStart[g].Num() >= 2 && MMPivot[g].Num() >= 2 && MMStop[g].Num() >= 2;
		for (UPoseSearchDatabase* X : MMStart[g]) All.AddUnique(X);
		for (UPoseSearchDatabase* X : MMPivot[g]) All.AddUnique(X);
		for (UPoseSearchDatabase* X : MMStop[g]) All.AddUnique(X);
		for (int32 v = 0; v < 6 && bOk; ++v)
		{
			// (three sets that share no capture where a direction has three or more: S1, S2, S3; the whole list only
			// where a set is missing)
			// (the walk has six: a man walking back to the huddle searched one of three sets and a third of the walkers
			// fell on the same best capture - 4+ men in step on one loop, wk1; S4-S6 missing: S1-S3 again)
			UPoseSearchDatabase* LV = Db(FString::Printf(TEXT("%s_Loops_S%d"), Gait[g], v + 1));
			if (!LV && v >= 3) LV = Db(FString::Printf(TEXT("%s_Loops_S%d"), Gait[g], v - 2));
			if (!LV) LV = Loops;
			auto Swap = [Loops, LV](const TArray<UPoseSearchDatabase*>& In) { TArray<UPoseSearchDatabase*> O = In; for (UPoseSearchDatabase*& X : O) if (X == Loops) X = LV; return O; };
			MMMoveV[g][v] = Swap(MMMove[g]);
			MMStartV[g][v] = Swap(MMStart[g]);
			MMPivotV[g][v] = Swap(MMPivot[g]);
			All.AddUnique(LV);
		}
	}
	// crouched footwork: the sample's crouch walk (~1.5-2 m/s) plus, with the TAG database, the pack's kick slides,
	// shuffles and crouched runs (to 4.4 m/s)
	bMMPack = bOk && FPackageName::DoesPackageExist(MM + TEXT("PSD_TAG_Crouch_Loops"));
	if (bOk)
	{
		for (TArray<UPoseSearchDatabase*>& C : MMCrouch) C.Reset();
		UPoseSearchDatabase* CLoops = Db(TEXT("Crouch_Loops"));
		UPoseSearchDatabase* CIdles = Db(TEXT("Crouch_Idles"));
		if (CLoops && CIdles)
		{
			MMCrouch[0] = {CLoops};
			MMCrouch[1] = {CLoops};
			MMCrouch[2] = {CLoops};
			// crouch starts: with the TAG databases the pack's own (PSD_TAG_Crouch_Starts: a lineman's first steps out of
			// his crouch, forward, sideways and back); the sample's generic walk starts only without them (they kept
			// playing for their full 2 s against the lineman's kick slide, -TagMMTrace)
			if (UPoseSearchDatabase* X = Db(TEXT("Crouch_Starts")))
				MMCrouch[1].Add(X);
			if (UPoseSearchDatabase* X = Db(TEXT("Crouch_Walk_Pivots"))) MMCrouch[2].Add(X);
			if (UPoseSearchDatabase* X = Db(TEXT("Crouch_Walk_Stops"))) MMCrouch[3].Add(X);
			MMCrouch[3].Add(CIdles);
			for (const TArray<UPoseSearchDatabase*>& C : MMCrouch) for (UPoseSearchDatabase* X : C) All.AddUnique(X);
		}
	}
	MMAll = All;
	bMotionMatch = bOk;
#if WITH_EDITOR
	// every entry must be on the source mesh's skeleton: a clip of the other tree plays through skeleton remapping
	// (bent, sunk bodies at a third of the frame rate: the first hero databases still held the UEFN clips, 12 Nov)
	if (bOk && Mesh)
	{
		int32 Foreign = 0;
		FString First;
		for (UPoseSearchDatabase* X : All)
			for (int32 i = 0; i < X->GetNumAnimationAssets(); ++i)
				if (const UAnimationAsset* An = Cast<UAnimationAsset>(X->GetAnimationAsset(i)))
					if (An->GetSkeleton() != Mesh->GetSkeleton())
					{
						if (!Foreign++) First = FString::Printf(TEXT("%s in %s"), *An->GetPathName(), *X->GetName());
					}
		if (Foreign) UE_LOG(LogTemp, Error, TEXT("TAG: %d motion matching entries are on another skeleton than %s (first: %s)"), Foreign, *GetNameSafe(Mesh->GetSkeleton()), *First);
	}
#endif
	// the continuing pose's cost bias (a runtime value, not part of the search index): the pack has several clips of
	// each gait from different men, and at the sample's -0.01 the search hopped between them every few tenths of a
	// second (-TagMoveTest: 17 clip changes in 27 samples of a sprint route), each 0.3 s blend dragging the planted
	// foot; at -0.15 (search costs run 0.05-0.4) the sprint held its clip and the route's slide fell by a third.
	// -TagMMCont=<bias> to try others.
	{
		static float Cont = FParse::Value(FCommandLine::Get(), TEXT("TagMMCont="), Cont) ? Cont : -0.3f; // (-0.15 until 12 Nov: A/B -0.3 held clips longer, hands steadier)
		if (bOk) for (UPoseSearchDatabase* X : All) X->ContinuingPoseCostBias = Cont;
	}
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

bool FAthleteAssets::LoadHero()
{
	FString Text;
	const FString File = FPaths::ProjectContentDir() / TEXT("TAG/Hero/hero_catalog.json");
	if (!FFileHelper::LoadFileToString(Text, *File)) return false;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return false;
	HeroItems.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Root->TryGetArrayField(TEXT("items"), Items))
		for (const TSharedPtr<FJsonValue>& V : *Items)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			FHeroItem I;
			I.Kind = O->GetStringField(TEXT("kind"));
			I.Name = O->GetStringField(TEXT("name"));
			I.Path = O->GetStringField(TEXT("path"));
			O->TryGetStringField(TEXT("build"), I.Build);
			O->TryGetStringField(TEXT("style"), I.Style);
			O->TryGetStringField(TEXT("top"), I.Top);
			O->TryGetStringField(TEXT("shell"), I.Shell);
			O->TryGetNumberField(TEXT("tone"), I.Tone);
			O->TryGetNumberField(TEXT("cells"), I.Cells);
			O->TryGetBoolField(TEXT("hat"), I.bHat);
			O->TryGetStringField(TEXT("rig"), I.Rig);
			O->TryGetStringField(TEXT("weather"), I.Weather);
			O->TryGetNumberField(TEXT("height"), I.Height);
			O->TryGetNumberField(TEXT("far"), I.Far);
			O->TryGetBoolField(TEXT("nohead"), I.bNoHead);
			if (I.Rig.IsEmpty()) I.Rig = TEXT("hero");
			I.Tone = FMath::Clamp(I.Tone, 1, 8);
			HeroItems.Add(MoveTemp(I));
		}
	auto Textures = [&Root](const TCHAR* Field, TArray<TObjectPtr<UTexture>>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
		if (Root->TryGetArrayField(Field, L))
			for (const TSharedPtr<FJsonValue>& V : *L)
				Out.Add(LoadAsset<UTexture>(*V->AsString(), false));
	};
	Textures(TEXT("skin_arm"), HeroSkinArm);
	Textures(TEXT("skin_arm_rsm"), HeroSkinArmRSM);
	Textures(TEXT("skin_legs"), HeroSkinLegs);
	Textures(TEXT("eyes"), HeroEyes);
	const TSharedPtr<FJsonObject>* Masks = nullptr;
	if (Root->TryGetObjectField(TEXT("masks"), Masks))
		for (TPair<const TCHAR*, TArray<TObjectPtr<UTexture>>*> M : {TPair<const TCHAR*, TArray<TObjectPtr<UTexture>>*>(TEXT("Jersey"), &HeroJerseyMasks),
			TPair<const TCHAR*, TArray<TObjectPtr<UTexture>>*>(TEXT("Pants"), &HeroPantsMasks), TPair<const TCHAR*, TArray<TObjectPtr<UTexture>>*>(TEXT("Helmet"), &HeroHelmetMasks)})
		{
			const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
			if ((*Masks)->TryGetArrayField(M.Key, L))
				for (const TSharedPtr<FJsonValue>& V : *L)
					if (UTexture* T = LoadAsset<UTexture>(*V->AsString(), false)) M.Value->Add(T);
		}
	// the UEFN tree reaches the pack bodies through these; the native tree (bNativeAnim) copies the hero rig's pose
	if (!bNativeAnim)
	{
		HeroRetargeter = LoadAsset<UIKRetargeter>(TEXT("/Game/TAG/Hero/Rig/RTG_TAG_UEFN_to_Hero.RTG_TAG_UEFN_to_Hero"), false);
		CrowdRetargeter = LoadAsset<UIKRetargeter>(TEXT("/Game/TAG/Hero/Rig/RTG_TAG_UEFN_to_Crowd.RTG_TAG_UEFN_to_Crowd"), false);
	}
	const bool bOk = (bNativeAnim || HeroRetargeter) && HeroOf(TEXT("Kit")).Num() && HeroOf(TEXT("Head")).Num() && HeroOf(TEXT("Helmet")).Num();
	if (const UPhysicsAsset* PA = LoadObject<UPhysicsAsset>(nullptr, TEXT("/Game/TAG/Hero/Rig/SKM_TAG_HeroRig_PhysicsAsset.SKM_TAG_HeroRig_PhysicsAsset"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		FString Bones;
		for (const USkeletalBodySetup* B : PA->SkeletalBodySetups)
			if (B) Bones += B->BoneName.ToString() + TEXT(" ");
		UE_LOG(LogTemp, Display, TEXT("TAG: model pack ragdoll %d bodies, %d constraints: %s"), PA->SkeletalBodySetups.Num(), PA->ConstraintSetup.Num(), *Bones);
	}
	UE_LOG(LogTemp, Display, TEXT("TAG: model pack catalog %d items: kits %d, heads %d, helmets %d, coaches %d+%d, officials %d, crew %d, fans %d"),
		HeroItems.Num(), HeroOf(TEXT("Kit")).Num(), HeroOf(TEXT("Head")).Num(), HeroOf(TEXT("Helmet")).Num(), HeroOf(TEXT("Coach")).Num(),
		HeroOf(TEXT("CoachHead")).Num(), HeroOf(TEXT("Ref")).Num(), HeroOf(TEXT("Crew")).Num(), HeroOf(TEXT("Fan")).Num());
	return bOk;
}

UIKRetargeter* FAthleteAssets::RetargeterFor(const FString& Rig)
{
	if (bNativeAnim)
	{
		// the rigs on the hero rig's bind pose (hero, male, side player; the crowd rig is a 30-bone subset of it) copy
		// its pose bone for bone (nullptr); the female and child rigs are built differently (up to 14 cm) and retarget
		// from it (Tools/Art/hero_retarget.py, TAG_HERO_SOURCE=hero)
		static const TMap<FString, FString> Own = {{TEXT("female"), TEXT("Female")}, {TEXT("child"), TEXT("Child")}};
		const FString* T = Own.Find(Rig);
		if (!T) return nullptr;
		if (const TObjectPtr<UIKRetargeter>* R = HeroRetargeters.Find(Rig)) return *R;
		UIKRetargeter* R = LoadAsset<UIKRetargeter>(*FString::Printf(TEXT("/Game/TAG/Hero/Rig/RTG_TAG_Hero_to_%s.RTG_TAG_Hero_to_%s"), **T, **T), false);
		HeroRetargeters.Add(Rig, R);
		return R;
	}
	if (Rig == TEXT("hero") || Rig.IsEmpty()) return HeroRetargeter;
	if (Rig == TEXT("crowd")) return CrowdRetargeter;
	if (const TObjectPtr<UIKRetargeter>* R = HeroRetargeters.Find(Rig)) return *R;
	static const TMap<FString, FString> Tags = {{TEXT("male"), TEXT("Male")}, {TEXT("female"), TEXT("Female")}, {TEXT("child"), TEXT("Child")},
		{TEXT("sideplayer"), TEXT("SidePlayer")}};
	const FString* T = Tags.Find(Rig);
	UIKRetargeter* R = T ? LoadAsset<UIKRetargeter>(*FString::Printf(TEXT("/Game/TAG/Hero/Rig/RTG_TAG_UEFN_to_%s.RTG_TAG_UEFN_to_%s"), **T, **T), false) : nullptr;
	HeroRetargeters.Add(Rig, R);
	return R ? R : HeroRetargeter.Get();
}

UAnimationAsset* FAthleteAssets::HeroClip(const FString& Rig, const TCHAR* Clip)
{
	static const TMap<FString, FString> Prefix = {{TEXT("hero"), TEXT("Hero")}, {TEXT("crowd"), TEXT("Fan")}, {TEXT("male"), TEXT("Male")},
		{TEXT("female"), TEXT("Female")}, {TEXT("child"), TEXT("Child")}, {TEXT("sideplayer"), TEXT("SidePlayer")}};
	const FString* P = Prefix.Find(Rig.IsEmpty() ? FString(TEXT("hero")) : Rig);
	if (!P) return nullptr;
	const FString Key = *P + TEXT("_") + Clip;
	if (const TObjectPtr<UAnimationAsset>* A = HeroClips.Find(Key)) return *A;
	UAnimationAsset* A = LoadAsset<UAnimationAsset>(*FString::Printf(TEXT("/Game/TAG/Hero/Crowd/Anims/A_%s.A_%s"), *Key, *Key), false);
	HeroClips.Add(Key, A);
	return A;
}

USkeletalMesh* FAthleteAssets::HeroMesh(int32 Index)
{
	if (!HeroItems.IsValidIndex(Index)) return nullptr;
	FHeroItem& I = HeroItems[Index];
	if (!I.Mesh) I.Mesh = LoadAsset<USkeletalMesh>(*I.Path);
	return I.Mesh;
}

TArray<int32> FAthleteAssets::HeroOf(const TCHAR* Kind, TFunctionRef<bool(const FHeroItem&)> Pred) const
{
	TArray<int32> Out;
	for (int32 i = 0; i < HeroItems.Num(); ++i)
		if (HeroItems[i].Kind == Kind && Pred(HeroItems[i])) Out.Add(i);
	return Out;
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

void AAthlete::TickAfter(AActor* Driver)
{
	if (!Driver) return;
	Body->PrimaryComponentTick.AddPrerequisite(Driver, Driver->PrimaryActorTick);
	if (MHBody) MHBody->PrimaryComponentTick.AddPrerequisite(Driver, Driver->PrimaryActorTick);
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
	if (A.bHero || A.bMetaHumans)
	{
		// the mannequin only plays the mocap now; the model pack character (or MetaHuman) is what you see
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
			Body->bComponentUseFixedSkelBounds = true; // never drawn: no bounds pass over its pose each frame
		}
		if (A.bHero)
		{
			SetupHero(U);
		}
		else
		{
			SetupMetaHuman(Arch, U);
			Depth = A.MH[Arch].Depth;
		}
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
	if (A.bHero)
	{
		// the pack's helmet, pads (in the jersey), cleats and gloves are part of the character: no stand-in gear
		for (UStaticMeshComponent* C : {Helmet.Get(), Facemask.Get(), Pads.Get()})
			if (C) C->SetVisibility(false);
		for (UStaticMeshComponent* G : Gear)
			if (G) G->DestroyComponent();
		Gear.Reset();
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

void AAthlete::SetupHero(const FUniform& U)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	using tag::Pos;
	const Pos P = Player ? Player->pos : Pos::WR;
	const bool bLine = P == Pos::OT || P == Pos::OG || P == Pos::C || P == Pos::DE || P == Pos::DT;
	const double Lb = Player ? Player->weightLb : 215.0;
	// body type from the build (the pack's thin / default / muscular / heavy), gear from the position
	const FString Build = (bLine || Lb >= 275.0) ? TEXT("h") : Lb >= 235.0 ? TEXT("m") : Lb >= 200.0 ? TEXT("d") : TEXT("t");
	const FString Style = (bLine || P == Pos::TE || P == Pos::FB) ? TEXT("line") : P == Pos::QB ? TEXT("qb")
		: (P == Pos::K || P == Pos::P) ? TEXT("kick") : TEXT("skill");
	const FString Mask = bLine ? TEXT("cage") : (P == Pos::RB || P == Pos::FB || P == Pos::TE || P == Pos::LB) ? TEXT("std")
		: (P == Pos::K || P == Pos::P) ? TEXT("kick") : TEXT("open");
	const uint32 H = (Player ? static_cast<uint32>(Player->id) : 7u) * 2654435761u;
	TArray<int32> Kits = A.HeroOf(TEXT("Kit"), [&](const FHeroItem& I) { return I.Build == Build && I.Style == Style; });
	if (!Kits.Num()) Kits = A.HeroOf(TEXT("Kit"), [&](const FHeroItem& I) { return I.Build == Build; });
	if (!Kits.Num()) Kits = A.HeroOf(TEXT("Kit"));
	TArray<int32> Helmets = A.HeroOf(TEXT("Helmet"), [&](const FHeroItem& I) { return I.Style == Mask; });
	if (!Helmets.Num()) Helmets = A.HeroOf(TEXT("Helmet"));
	const TArray<int32> Heads = A.HeroOf(TEXT("Head"));
	const int32 Head = Heads.Num() ? Heads[(H >> 5) % Heads.Num()] : INDEX_NONE;
	HeroTone = Head != INDEX_NONE ? A.HeroItems[Head].Tone : 4;
	DressHero(Kits.Num() ? A.HeroMesh(Kits[(H >> 9) % Kits.Num()]) : nullptr, A.HeroMesh(Head),
		Helmets.Num() ? A.HeroMesh(Helmets[(H >> 13) % Helmets.Num()]) : nullptr, TEXT("hero"));
	SetHelmetOn(true);
	PaintHero(U);
}

void AAthlete::DressHero(USkeletalMesh* BodyMesh, USkeletalMesh* HeadMesh, USkeletalMesh* HelmetMesh, const FString& Rig)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (!BodyMesh) return;
	if (!MHBody)
	{
		MHBody = NewObject<USkeletalMeshComponent>(this, TEXT("MHBody"));
		MHBody->SetupAttachment(Root);
		MHBody->RegisterComponent();
		AddInstanceComponent(MHBody);
	}
	if (!MHFace)
	{
		MHFace = NewObject<USkeletalMeshComponent>(this, TEXT("MHFace"));
		MHFace->SetupAttachment(MHBody);
		MHFace->RegisterComponent();
		AddInstanceComponent(MHFace);
	}
	if (!HeroHelmet)
	{
		HeroHelmet = NewObject<USkeletalMeshComponent>(this, TEXT("HeroHelmet"));
		HeroHelmet->SetupAttachment(MHBody);
		HeroHelmet->RegisterComponent();
		AddInstanceComponent(HeroHelmet);
	}
	// a new body (a staff role, another rig) gets a fresh retarget instance for its skeleton
	MHBody->SetAnimInstanceClass(nullptr);
	MHBody->SetSkeletalMesh(BodyMesh);
	MHBody->EmptyOverrideMaterials();
	MHBody->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0, -90, 0)); // the pack characters face +Y
	const float HeightCm = Player && Player->heightIn > 0 ? static_cast<float>(Player->heightIn * 2.54) : A.HeroHeightCm;
	VisRelScale = FVector(FMath::Clamp(HeightCm / A.HeroHeightCm, 0.93f, 1.08f));
	MHBody->SetRelativeScale3D(VisRelScale);
	MHBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MHBody->bCastDynamicShadow = true;
	MHBody->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	static const bool bRefPose = FParse::Param(FCommandLine::Get(), TEXT("TagHeroRefPose")); // debug: the bind pose, no retarget
	if (!bRefPose) MHBody->SetAnimInstanceClass(UTagRetargetAnimInstance::StaticClass());
	if (UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()))
		RI->Configure(A.RetargeterFor(Rig), Body);
	MHBody->AddTickPrerequisiteComponent(Body); // mocap pose first, then retarget
	// head and helmet: same skeleton, they wear the body's pose
	USkeletalMeshComponent* Comps[2] = {MHFace.Get(), HeroHelmet.Get()};
	USkeletalMesh* Meshes[2] = {HeadMesh, HelmetMesh};
	for (int32 k = 0; k < 2; ++k)
	{
		USkeletalMeshComponent* C = Comps[k];
		C->SetAnimInstanceClass(nullptr);
		C->SetSkeletalMesh(Meshes[k]);
		C->EmptyOverrideMaterials();
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->bCastDynamicShadow = true;
		C->SetVisibility(Meshes[k] != nullptr);
		C->SetLeaderPoseComponent(Meshes[k] ? MHBody.Get() : nullptr, true);
		C->bUseBoundsFromLeaderPoseComponent = true; // the body's bounds hold the head and the helmet: no bounds pass of their own
	}
	bFaceOnLeader = true;
	bMaterialNumbers = false;
}

void AAthlete::SetHelmetOn(bool bOn)
{
	bHeroHelmetOn = bOn;
	if (HeroHelmet) HeroHelmet->SetVisibility(bOn && HeroHelmet->GetSkeletalMeshAsset() != nullptr);
	HideHair(bOn);
}

void AAthlete::WearCap(int32 Variant)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<int32> Caps = A.HeroOf(TEXT("Cap"));
	if (!Caps.Num() || !HeroHelmet || !MHBody) return;
	HeroHelmet->SetSkeletalMesh(A.HeroMesh(Caps[Variant % Caps.Num()]));
	HeroHelmet->EmptyOverrideMaterials();
	HeroHelmet->SetLeaderPoseComponent(MHBody.Get(), true);
	HeroHelmet->SetVisibility(true);
	bHeroHelmetOn = false;
	HideHair(true); // the cap sits over the hair
}

void AAthlete::HideHair(bool bHide)
{
	// the hair (not the cap of short hair painted on the scalp) would show through a helmet or a cap
	const bool bOn = bHide;
	USkeletalMesh* M = MHFace ? MHFace->GetSkeletalMeshAsset() : nullptr;
	FSkeletalMeshRenderData* RD = M ? M->GetResourceForRendering() : nullptr;
	if (!RD) return;
	const TArray<FSkeletalMaterial>& Mats = M->GetMaterials();
	for (int32 L = 0; L < RD->LODRenderData.Num(); ++L)
		for (int32 S = 0; S < RD->LODRenderData[L].RenderSections.Num(); ++S)
		{
			const int32 Mi = RD->LODRenderData[L].RenderSections[S].MaterialIndex;
			if (Mats.IsValidIndex(Mi) && Mats[Mi].MaterialSlotName == FName(TEXT("Hair")))
				MHFace->ShowMaterialSection(Mi, S, !bOn, L);
		}
}

/** True when a slot's material paints a team colour over a placeholder (the hero master's colour Mode 2). */
static bool TeamPainted(USkeletalMeshComponent* C, const FName& Slot)
{
	const int32 I = C ? C->GetMaterialIndex(Slot) : INDEX_NONE;
	UMaterialInterface* M = I != INDEX_NONE ? C->GetMaterial(I) : nullptr;
	float Mode = 0.f;
	return M && M->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Mode")), Mode) && Mode > 1.5f && Mode < 2.5f;
}

/** A scalar of the material a mesh slot carries (the import's flags: Packed, TeamGlove); 0 when absent. */
static float SlotScalar(USkeletalMeshComponent* C, const FName& Slot, const TCHAR* Param)
{
	UMaterialInterface* M = C ? C->GetMaterialByName(Slot) : nullptr;
	float V = 0.f;
	return M && M->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(Param)), V) ? V : 0.f;
}

void AAthlete::PaintHero(const FUniform& U)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	CurUniform = U;
	auto Each = [](USkeletalMeshComponent* C, TFunctionRef<void(const FName&, TFunctionRef<UMaterialInstanceDynamic*()>)> Fn)
	{
		if (!C || !C->GetSkeletalMeshAsset()) return;
		const TArray<FSkeletalMaterial>& Mats = C->GetSkeletalMeshAsset()->GetMaterials();
		for (int32 i = 0; i < Mats.Num(); ++i)
			Fn(Mats[i].MaterialSlotName, [C, i]() { return C->CreateDynamicMaterialInstance(i); });
	};
	const int32 Tone = FMath::Clamp(HeroTone, 1, 8) - 1;
	UTexture* Arm = A.HeroSkinArm.IsValidIndex(Tone) ? A.HeroSkinArm[Tone].Get() : nullptr;
	UTexture* ArmRSM = A.HeroSkinArmRSM.IsValidIndex(Tone) ? A.HeroSkinArmRSM[Tone].Get() : nullptr;
	UTexture* Legs = A.HeroSkinLegs.IsValidIndex(Tone) ? A.HeroSkinLegs[Tone].Get() : nullptr;
	const bool bKit = MHBody && MHBody->GetSkeletalMeshAsset() && MHBody->GetSkeletalMeshAsset()->GetName().StartsWith(TEXT("Kit_"));
	// numbers are placed from this body's bind pose (neck height, the torso's front / back divide)
	float NeckZ = 157.f, TorsoY = 0.f;
	if (MHBody && MHBody->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& R = MHBody->GetSkeletalMeshAsset()->GetRefSkeleton();
		if (R.FindBoneIndex(TEXT("neck_01")) != INDEX_NONE && R.FindBoneIndex(TEXT("pelvis")) != INDEX_NONE)
		{
			const FVector Neck = RefBonePos(R, TEXT("neck_01")), Pelvis = RefBonePos(R, TEXT("pelvis"));
			NeckZ = Neck.Z;
			TorsoY = 0.5f * (Neck.Y + Pelvis.Y);
		}
	}
	// the team's stripe layouts (the pack's uniform sets), fixed per team: picked from its colours
	const uint32 TeamHash = GetTypeHash(U.Jersey.ToFColor(true).ToPackedARGB()) ^ (GetTypeHash(U.Helmet.ToFColor(true).ToPackedARGB()) * 31u);
	auto MaskFor = [TeamHash](const TArray<TObjectPtr<UTexture>>& L, uint32 Salt) -> UTexture*
	{
		return L.Num() ? L[((TeamHash >> 3) + Salt) % L.Num()].Get() : nullptr;
	};
	UTexture* JerseyMask = MaskFor(A.HeroJerseyMasks, 0), *PantsMask = nullptr, *HelmetMask = MaskFor(A.HeroHelmetMasks, 5);
	if (JerseyMask && A.HeroPantsMasks.Num() == A.HeroJerseyMasks.Num()) PantsMask = A.HeroPantsMasks[A.HeroJerseyMasks.IndexOfByKey(JerseyMask)];
	const tag::Pos P = Player ? Player->pos : tag::Pos::WR;
	const bool bDarkGloves = P == tag::Pos::OT || P == tag::Pos::OG || P == tag::Pos::C || P == tag::Pos::DE || P == tag::Pos::DT || P == tag::Pos::TE;
	Each(MHBody, [&](const FName& Slot, TFunctionRef<UMaterialInstanceDynamic*()> Mid)
	{
		if (Slot == TEXT("Skin") && Arm && (bKit || MHFace->GetSkeletalMeshAsset()))
		{
			// the tone's colour with its own roughness / specular / scatter (the pack ships one RSM per tone)
			UMaterialInstanceDynamic* M = Mid();
			M->SetTextureParameterValue(TEXT("BaseColor"), Arm);
			if (ArmRSM) M->SetTextureParameterValue(TEXT("RSM"), ArmRSM);
		}
		else if (Slot == TEXT("SkinLegs") && Legs) Mid()->SetTextureParameterValue(TEXT("BaseColor"), Legs);
		else if (Slot == TEXT("Jersey"))
		{
			UMaterialInstanceDynamic* M = Mid();
			M->SetVectorParameterValue(TEXT("Tint"), U.Jersey);
			M->SetVectorParameterValue(TEXT("Trim"), U.Trim);
			M->SetVectorParameterValue(TEXT("Trim2"), U.Trim);
			if (JerseyMask) M->SetTextureParameterValue(TEXT("Mask"), JerseyMask);
			M->SetVectorParameterValue(TEXT("NumColor"), U.Number);
			M->SetScalarParameterValue(TEXT("Number"), Player ? static_cast<float>(Player->number) : 0.f);
			M->SetScalarParameterValue(TEXT("NumOn"), Player && Player->number > 0 ? 1.f : 0.f);
			M->SetScalarParameterValue(TEXT("NeckZ"), NeckZ);
			M->SetScalarParameterValue(TEXT("TorsoY"), TorsoY);
			// front numbers 9 in tall (NFL 8-10 in), their top 13 cm under the neck: at 19 cm they did not read on the field
			M->SetScalarParameterValue(TEXT("NumHF"), 23.f);
			M->SetScalarParameterValue(TEXT("NumTopF"), 13.f);
			// TV numbers on the shoulder tops (the kits only: the pads give them a top to sit on)
			if (bKit)
			{
				static float TvH = 9.f, TvX = 15.5f, TvY = 0.f, TvZ = 11.f;
				static const bool bTvOnce = [&]()
				{
					FParse::Value(FCommandLine::Get(), TEXT("TagTvNumH="), TvH);
					FParse::Value(FCommandLine::Get(), TEXT("TagTvNumX="), TvX);
					FParse::Value(FCommandLine::Get(), TEXT("TagTvNumY="), TvY);
					FParse::Value(FCommandLine::Get(), TEXT("TagTvNumZ="), TvZ);
					return true;
				}();
				M->SetScalarParameterValue(TEXT("TvOn"), Player && Player->number > 0 ? 1.f : 0.f);
				M->SetScalarParameterValue(TEXT("TvH"), TvH);
				M->SetScalarParameterValue(TEXT("TvX"), TvX);
				M->SetScalarParameterValue(TEXT("TvY"), TvY);
				M->SetScalarParameterValue(TEXT("TvZ"), TvZ);
			}
			bMaterialNumbers = true;
		}
		else if (Slot == TEXT("Pants") && bKit)
		{
			// the side stripe in whichever team colour stands out on the pants (gold trim vanished on gold pants)
			auto Dist = [](const FLinearColor& X, const FLinearColor& Y) { return FMath::Abs(X.R - Y.R) + FMath::Abs(X.G - Y.G) + FMath::Abs(X.B - Y.B); };
			const bool bJerseyEdge = Dist(U.Jersey, U.Pants) > Dist(U.Trim, U.Pants);
			UMaterialInstanceDynamic* M = Mid();
			M->SetVectorParameterValue(TEXT("Tint"), U.Pants);
			M->SetVectorParameterValue(TEXT("Trim"), bJerseyEdge ? U.Jersey : U.Trim);
			M->SetVectorParameterValue(TEXT("Trim2"), bJerseyEdge ? U.Trim : U.Jersey);
			if (PantsMask) M->SetTextureParameterValue(TEXT("Mask"), PantsMask);
		}
		else if (Slot == TEXT("Socks")) Mid()->SetVectorParameterValue(TEXT("Tint"), U.Jersey);
		else if (Slot == TEXT("Hands") && bKit && SlotScalar(MHBody, Slot, TEXT("TeamGlove")) > 0.5f)
		{
			// gloves: the team colour for the skill players, black for the line; taped hands and wraps keep their white
			Mid()->SetVectorParameterValue(TEXT("Tint"), bDarkGloves ? FLinearColor(0.03f, 0.03f, 0.035f) : U.Jersey * 0.85f);
		}
		else if (Slot == TEXT("Shoes") && SlotScalar(MHBody, Slot, TEXT("Packed")) > 0.5f)
		{
			// the pack's cleat maps are grey with the sole / heel / brand mark masked: black or white cleats, the
			// marked parts white or in the team colour, per player
			const uint32 Hc = (Player ? static_cast<uint32>(Player->id) : 3u) * 2246822519u;
			const bool bWhite = (Hc >> 7) % 3 == 0;
			UMaterialInstanceDynamic* M = Mid();
			M->SetVectorParameterValue(TEXT("Tint"), bWhite ? FLinearColor(0.85f, 0.85f, 0.85f) : FLinearColor(0.02f, 0.02f, 0.022f));
			M->SetVectorParameterValue(TEXT("Trim"), (Hc >> 11) % 2 ? U.Jersey : (bWhite ? FLinearColor(0.02f, 0.02f, 0.022f) : FLinearColor(0.85f, 0.85f, 0.85f)));
			// the pack's brand marks (white in the decal) read dark on a white cleat
			M->SetVectorParameterValue(TEXT("DecalTint"), bWhite ? FLinearColor(0.03f, 0.03f, 0.035f) : FLinearColor(1.f, 1.f, 1.f));
		}
		else if (!bKit && OfficialPatch >= 0 && Slot == TEXT("Top"))
		{
			static float PatchTop = 7.f, PatchH = 22.f, PatchW = 22.f;
			static const bool bPatchOnce = [&]()
			{
				FParse::Value(FCommandLine::Get(), TEXT("TagPatchTop="), PatchTop);
				FParse::Value(FCommandLine::Get(), TEXT("TagPatchH="), PatchH);
				FParse::Value(FCommandLine::Get(), TEXT("TagPatchW="), PatchW);
				return true;
			}();
			UMaterialInstanceDynamic* M = Mid();
			M->SetScalarParameterValue(TEXT("PatchOn"), 1.f);
			M->SetScalarParameterValue(TEXT("PatchIdx"), static_cast<float>(OfficialPatch));
			M->SetScalarParameterValue(TEXT("NeckZ"), NeckZ);
			M->SetScalarParameterValue(TEXT("TorsoY"), TorsoY);
			M->SetScalarParameterValue(TEXT("PatchTop"), PatchTop);
			M->SetScalarParameterValue(TEXT("PatchH"), PatchH);
			M->SetScalarParameterValue(TEXT("PatchW"), PatchW);
		}
		else if (!bKit && TeamPainted(MHBody, Slot))
		{
			// coach / staff clothing the pack leaves in placeholder colours: tops and caps in the team colour, the
			// trousers in the bottom colour
			UMaterialInstanceDynamic* M = Mid();
			M->SetVectorParameterValue(TEXT("Tint"), Slot.ToString().StartsWith(TEXT("Bottom")) ? U.Pants : U.Jersey);
			M->SetVectorParameterValue(TEXT("Trim"), U.Trim);
		}
	});
	Each(HeroHelmet, [&](const FName& Slot, TFunctionRef<UMaterialInstanceDynamic*()> Mid)
	{
		if (Slot == TEXT("Shell"))
		{
			UMaterialInstanceDynamic* M = Mid();
			// stripe colours that stand out on the shell (a trim close to the helmet colour vanished)
			auto D = [](const FLinearColor& X, const FLinearColor& Y) { return FMath::Abs(X.R - Y.R) + FMath::Abs(X.G - Y.G) + FMath::Abs(X.B - Y.B); };
			FLinearColor Cand[4] = {U.Trim, U.Number, U.Jersey, U.Pants};
			Algo::Sort(Cand, [&](const FLinearColor& X, const FLinearColor& Y) { return D(X, U.Helmet) > D(Y, U.Helmet); });
			static TSet<uint32> GLoggedTeams;
			if (!GLoggedTeams.Contains(TeamHash))
			{
				GLoggedTeams.Add(TeamHash);
				UE_LOG(LogTemp, Display, TEXT("TAG uniform layout: helmet %s (shell %s, stripe %s), jersey %s, pants %s"), *GetNameSafe(HelmetMask),
					*U.Helmet.ToFColor(true).ToHex(), *Cand[0].ToFColor(true).ToHex(), *GetNameSafe(JerseyMask), *GetNameSafe(PantsMask));
			}
			M->SetVectorParameterValue(TEXT("Tint"), U.Helmet);
			M->SetVectorParameterValue(TEXT("Trim"), Cand[0]);
			M->SetVectorParameterValue(TEXT("Trim2"), D(Cand[1], Cand[0]) > 0.3f ? Cand[1] : FLinearColor(0.92f, 0.92f, 0.92f));
			if (HelmetMask) M->SetTextureParameterValue(TEXT("Mask"), HelmetMask);
			if (U.TeamId >= 0 && U.TeamId < 32)
			{
				// the team mark on both sides: its second colour, outlined in whichever of the trims stands off the shell
				M->SetScalarParameterValue(TEXT("LogoOn"), 1.f);
				M->SetScalarParameterValue(TEXT("LogoIdx"), static_cast<float>(U.TeamId));
				const FLinearColor Fill = D(U.Logo, U.Helmet) > 0.35f ? U.Logo : Cand[0];
				M->SetVectorParameterValue(TEXT("LogoFill"), Fill);
				M->SetVectorParameterValue(TEXT("LogoLine"), D(Cand[1], Fill) > 0.35f ? Cand[1] : (Fill.GetLuminance() > 0.5f ? FLinearColor(0.03f, 0.03f, 0.035f) : FLinearColor(0.92f, 0.92f, 0.92f)));
			}
		}
	});
	Each(MHFace, [&](const FName& Slot, TFunctionRef<UMaterialInstanceDynamic*()> Mid)
	{
		if (bCapTint && Slot == TEXT("Hat"))
		{
			// (the master's recolour mode: the cap's own shading under the new colour; its map is near black, so the
			// shading is read against that)
			UMaterialInstanceDynamic* M = Mid();
			M->SetScalarParameterValue(TEXT("Mode"), 2.f);
			M->SetScalarParameterValue(TEXT("AlphaTint"), 0.f);
			M->SetScalarParameterValue(TEXT("LumRef"), 0.016f); // (the map's median, linear)
			M->SetVectorParameterValue(TEXT("Tint"), CapTint);
			M->SetVectorParameterValue(TEXT("Trim"), CapTint);
			return;
		}
		if (TeamPainted(MHFace, Slot))
		{
			UMaterialInstanceDynamic* M = Mid();
			M->SetVectorParameterValue(TEXT("Tint"), U.Jersey);
			M->SetVectorParameterValue(TEXT("Trim"), U.Trim);
		}
	});
}

void AAthlete::PlayOneShot(UAnimationAsset* Clip)
{
	PlayOneShotFrom(Clip, 0.f);
}

void AAthlete::PlayOneShotFrom(UAnimationAsset* Clip, float Start)
{
	if (!Clip || bRagdoll) return;
	OneShot = Clip;
	bOneShotCatch = false;
	OneShotAge = 0.f;
	OneShotStart = Start;
	OneShotRate = 1.f + StyleJitter * 0.07f; // each player's own timing, within 7 %
	// (two frames short of the end: several captures' last frame snaps back - the falls already stop a frame short -
	// and a one-shot played to its very end showed it, a 65 cm jump of a rusher's feet on a win's last frame, p36)
	OneShotLeft = FMath::Max(0.05f, Clip->GetPlayLength() - Start - 0.07f) / OneShotRate;
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
	// velocity and acceleration as measured by MeasureMotion (the sim's velocity field goes stale when play stops:
	// positions freeze, the velocity keeps its last value, which read to the search as "standing, about to sprint")
	static const bool bNoAccel = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoAccel"));
	if (bNoAccel) SmoothedAccel = FVector::ZeroVector;
	// what he is asked to do: the sim's velocity, plus making up the body's lag behind its spot (3/s of all but the last
	// 8 cm, at most 2 m/s): the search then picks, and the stride stretches, for the body to arrive where the sim is
	static const bool bFollowOn = !FParse::Param(FCommandLine::Get(), TEXT("TagNoFollow"));
	// the matched body's root velocity in the world, as the blend stack played it last evaluation (zero: not known)
	FVector RootVelW = FVector::ZeroVector;
	if (M->bRootDeltaValid && bInMatch && MHBody)
	{
		RootVelW = (M->bRootRotValid ? M->RootRotW : MHBody->GetComponentQuat()).RotateVector(M->RootDelta) / M->RootDeltaDt;
		RootVelW.Z = 0.f;
	}
	if (MHBody)
		if (UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()))
		{
			// (the capture's root velocity, unwarped: the verdict is taken on the capture's pose before the layers)
			RI->Proc.bRootVelValid = M->bRootDeltaValid && bInMatch;
			RI->Proc.RootVelCS = MHBody->GetComponentTransform().InverseTransformVectorNoScale(RootVelW);
		}
	FVector V = PrevVel;
	if (bFollowOn)
	{
		const float L = VisOffset.Size2D();
		if (L > 8.f) V -= VisOffset.GetSafeNormal2D() * FMath::Min((L - 8.f) * 2.5f, 200.f);
	}
	(void)VelWorld;

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
	static const bool bNoIntent = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoIntent"));
	// (only for a start from rest - a release off the line, a back taking the handoff: there the measured acceleration is
	// still nothing. Used all through the play it asked for more than the sim then did, the search picked faster clips
	// than the men ran, and the planted feet of matched runs travelled 19 cm/s against 7 without it - p19 / p20n.)
	const bool bIntent = !bNoIntent && IntentFrame == GFrameCounter && Speed < 150.f && IntentVelW.SizeSquared2D() > 300.f * 300.f;
	for (int32 k = 1; k <= 32; ++k)
	{
		const float t = k * H;
		const FVector Acc = (t <= 0.5f && !bStopped) ? SmoothedAccel : FVector::ZeroVector;
		FVector Vn = Vel + Acc * H;
		// with the sim's intent: toward the velocity it is steering for, as fast as a man changes his (6 m/s2 speeding
		// up or turning, 9 braking)
		if (bIntent)
		{
			const FVector To = IntentVelW - Vel;
			const bool bBraking = IntentVelW.SizeSquared2D() < Vel.SizeSquared2D();
			Vn = t <= 0.6f ? Vel + To.GetClampedToMaxSize((bBraking ? 900.f : 450.f) * H) : Vel;
		}
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
	if (MMGait == 1 && GaitSpeed > 680.f) MMGait = 2; // (a wider band: a man running 5.6-6.4 m/s flipped run <-> sprint)
	if (MMGait == 2 && GaitSpeed < 520.f) MMGait = 1;
	// the stop / idle databases only once he is really slowing to a stand: from 4.5 m/s a predicted stop searched them and
	// an idle loop played under a man still moving 1-4 m/s (80-100 cm of slide per plant in the p3 capture)
	// (latched: on above the old line, off only after 0.15 s well below it)
	const bool bMovingRaw = FutSpeed > 50.f || Speed > 250.f;
	M->StillFor = (FutSpeed < 30.f && Speed < 150.f) ? M->StillFor + Dt : 0.f;
	if (bMovingRaw) M->bMovingLatch = true;
	else if (M->StillFor > 0.15f) M->bMovingLatch = false;
	const bool bMoving = M->bMovingLatch;
	MovingTime = bMoving ? MovingTime + Dt : 0.f;
	// pivot: the predicted path turns sharply in the next half second (cut, plant, break on the ball)
	bool bPivot = false;
	{
		const FTransformTrajectorySample S5 = Tr.GetSampleAtTime(0.5f), S4 = Tr.GetSampleAtTime(0.4f);
		const FVector VFut = (S5.Position - S4.Position) / 0.1f;
		if (Speed > 150.f && VFut.Size2D() > 100.f)
			bPivot = FVector::DotProduct(V.GetSafeNormal2D(), VFut.GetSafeNormal2D()) < 0.7f; // > ~45 deg
	}
	// a fresh pick only when the kind of set changes in a way the playing clip cannot carry over: a start running out, a
	// stop beginning or ending, the crouch coming or going. A new gait band or the pivot set lets the loop play on until
	// the search finds better (UTagMotionMatchInstance::bForceOnDbChange)
	{
		const int32 Fam = !bMoving ? 0 : MovingTime < 0.6f ? 1 : bPivot ? 2 : 3;
		const int32 Last = M->LastDbFamily;
		// (a standing start hands over to the loops softly; a crouched start must end, see bForceOnDbChange)
		// (and the moment he gets the ball or gives it up: a back taking the handoff at a run kept his empty-handed sprint,
		// a soft change letting the playing clip run on)
		const bool bBallNow = bHasBallF && A.MMBallMove[MMGait].Num() > 0;
		M->bForceOnDbChange = Last < 0 || bCrouch != M->bLastDbCrouch || bBallNow != M->bLastDbBall || (Fam != Last && (Fam == 0 || Last == 0 || (Last == 1 && bCrouch)));
		M->LastDbFamily = Fam;
		M->bLastDbCrouch = bCrouch;
		// (a man who has just got the ball: half a second without the continuing pose, so the search lands on a ball-carrier
		// loop - at the default 0.15 s a back taking a handoff at full speed kept his empty-handed sprint for 1.2 s, bc4 c003)
		if (bBallNow && !M->bLastDbBall) { M->ForceHoldSecs = 0.5f; ++M->ForceSerial; }
		if (bBallNow != M->bLastDbBall)
			UE_LOG(LogTemp, Display, TEXT("TAG carrier loops %s: #%d gait %d moving %d t %.2f pivot %d crouch %d fam %d force %d"), bBallNow ? TEXT("on") : TEXT("off"),
				Player ? Player->number : -1, MMGait, bMoving ? 1 : 0, MovingTime, bPivot ? 1 : 0, bCrouch ? 1 : 0, Fam, M->bForceOnDbChange ? 1 : 0);
		M->bLastDbBall = bBallNow;
		// a carrier past his start still on an empty-handed clip: a fresh pick (twice a second at most). The change of set
		// alone let the continuing pose win: a kick returner ran his whole return on a 3 s sprint start, a back taking the
		// handoff kept his empty-handed sprint 1.2 s (bc5 c001 / c003)
		M->BallForceCool -= Dt;
		if (bBallNow && Fam >= 2 && !bCrouch && M->bMatch && M->BallForceCool <= 0.f)
			if (const UAnimationAsset* Ma = M->GetMatchedAsset())
				if (!Ma->GetName().Contains(TEXT("_BC_")))
				{
					M->ForceHoldSecs = 0.3f;
					++M->ForceSerial;
					M->BallForceCool = 0.5f;
					UE_LOG(LogTemp, Display, TEXT("TAG carrier refresh: #%d off %s t %.2f"), Player ? Player->number : -1, *Ma->GetName(), MovingTime);
				}
		M->SimVel = V;
	}
	if (bCrouch)
		M->SetDatabases(!bMoving ? A.MMCrouch[3] : MovingTime < 0.6f ? A.MMCrouch[1] : bPivot ? A.MMCrouch[2] : A.MMCrouch[0]);
	else
	{
		// his style's sets (a third of the men on each: the same gait does not mean the same capture)
		const int32 Sv = Player ? FMath::Abs(Player->id) % 6 : 0;
		// with the ball: the ball carrier's loops, the ball tucked as Madden captured it
		const bool bBall = bHasBallF && A.MMBallMove[MMGait].Num() > 0;
		M->SetDatabases(!bMoving ? A.MMStop[MMGait] : MovingTime < 0.6f ? (bBall ? A.MMBallStart[MMGait] : A.MMStartV[MMGait][Sv])
			: bPivot ? (bBall ? A.MMBallPivot[MMGait] : A.MMPivotV[MMGait][Sv]) : (bBall ? A.MMBallMove[MMGait] : A.MMMoveV[MMGait][Sv]));
	}
	// what the databases cover: with the mocap pack's locomotion (mm_build_dbs.py) 10-11 m/s forward (sprint loops),
	// 4.9 m/s sideways and 6 m/s backwards; crouched 4.4 m/s forward, 3.2 sideways, 3.9 backwards. Without the TAG
	// databases (Epic's sample alone) 7 / 3.5 / 3 m/s and 2 / 1.7 / 1.5. Above those, search a proportionally slower
	// query and play the clip faster (<= 1.5x).
	{
		const FVector Fwd = FRotator(0.f, SimYaw, 0.f).Vector();
		const float C = Speed > 1.f ? FVector::DotProduct(V / Speed, Fwd) : 1.f;
		const bool bPack = A.bMMPack;
		const float AnimMax = bCrouch ? (C >= 0.f ? FMath::Lerp(bPack ? 320.f : 170.f, bPack ? 440.f : 200.f, C) : FMath::Lerp(bPack ? 320.f : 170.f, bPack ? 390.f : 150.f, -C))
			: C >= 0.f ? FMath::Lerp(bPack ? 490.f : 350.f, bPack ? 1000.f : 700.f, C) : FMath::Lerp(bPack ? 490.f : 350.f, bPack ? 450.f : 300.f, -C);
		M->TrajectorySpeedMultiplier = FMath::Clamp(AnimMax / FMath::Max(GaitSpeed, 1.f), 1.f / 1.5f, 1.f);
		// quicker strides up to 1.1x (7 m/s capture -> 7.7 m/s); the rest of a sprinter's speed comes from a longer
		// stride (ApplyStride), not quicker feet: at 1.3x the measured cadence was ~5.5 steps/s against the ~4.6 of
		// a real sprinter at 9.4 m/s (-TagMoveTest gait log), at 1.1x ~5.3 with the step 1.6 -> 1.8 m
		static float RateCap = FParse::Value(FCommandLine::Get(), TEXT("TagMMRateCap="), RateCap) ? RateCap : 1.1f;
		M->MatchPlayRate = FMath::Min(1.f / M->TrajectorySpeedMultiplier, RateCap);
		// with the pack's loops at their true speeds the search picks the clip and a rate within 15 % of it
		static float Spread = FParse::Value(FCommandLine::Get(), TEXT("TagMMSpread="), Spread) ? Spread : 1.f;
		M->RateSpread = bPack ? Spread : 1.f;
		M->SimSpeed = Speed; // speed matching of the picked clip's root speed (UTagMotionMatchInstance)
		M->bCrouchGait = bCrouch;
		// faster still (a 10 m/s ball carrier against the 9.1 m/s the sped-up sprint covers): stretch the stride
		if (MHBody && bMoving)
			if (UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()))
			{
				// what the feet of the playing clip actually cover (its root speed at the play position x the rate it
				// plays at) against the ground the sim covers: the speed match's clamp leaves a remainder that skated
				// the planted foot (sideways, backpedal and crouched gaits most). Each step is stretched or shortened
				// along the direction of travel by that remainder.
				// (the blend stack's own root motion where it reports one: every playing clip, blended, at the rate played.
				// The estimate from the picked clip alone misses the clip being blended out for the first 0.3 s of a pick.)
				const float Eff = RootVelW.IsNearlyZero() ? M->MatchedClipSpeed * M->MatchedWantedRate * M->SpeedRate : static_cast<float>(RootVelW.Size());
				FVector ClipW = FVector::ZeroVector;
				float S = 1.f;
				if (M->bMatch && Eff > 60.f && Speed > 60.f) S = FMath::Clamp(Speed / Eff, 0.7f, bCrouch ? 1.3f : 1.35f);
				else if (!bCrouch)
				{
					const float Covered = AnimMax * RateCap;
					S = GaitSpeed > Covered ? FMath::Min(GaitSpeed / Covered, 1.25f) : 1.f;
				}
				RI->Proc.StrideScale = S;
				TelEff = M->bMatch ? Eff : 0.f;
				TelStride = S;
				// where the playing clip's root travels in the world: its own direction turned by the root's rotation as
				// shown. (Not the component's: the offset node lets a clip's own turn carry the body up to 60 degrees off the
				// sim's facing and eases it back over a quarter second, so through every turn the two differ. Read in the
				// component's frame the clip seemed to travel 25-60 degrees off the man's path - the p14 / p15 captures, a
				// fifth of all matched frames over 40 - and the warp below then turned the legs away from a path they were on.)
				ClipW = RootVelW.IsNearlyZero() ? (M->bRootRotValid ? M->RootRotW : MHBody->GetComponentQuat()).RotateVector(M->MatchedClipDir) : RootVelW;
				ClipW.Z = 0.f;
				ClipW = ClipW.GetSafeNormal();
				if (M->bRootRotValid) TelRootOff = FMath::FindDeltaAngleDegrees(MHBody->GetComponentQuat().Rotator().Yaw, M->RootRotW.Rotator().Yaw);
				// orientation warping: the angle from where the clip's root travels to where he travels, at most 40 degrees
				// (past that the search should have found a different clip), eased so a new match does not snap the hips
				float WarpWant = 0.f;
				float Ang = 0.f;
				if (M->bMatch && !ClipW.IsNearlyZero() && Eff > 80.f && Speed > 80.f)
				{
					const FVector Want2 = V.GetSafeNormal2D();
					Ang = FMath::RadiansToDegrees(FMath::Atan2(ClipW.X * Want2.Y - ClipW.Y * Want2.X, ClipW.X * Want2.X + ClipW.Y * Want2.Y));
					WarpWant = FMath::Clamp(Ang, -40.f, 40.f);
					TelAng = Ang;
				}
				// a clip that cannot cover the move: the play rate (0.75-1.25, crouched 1.5) and the stride (0.7-1.35) make up
				// a clip whose feet cover between 0.6 and 1.6 times the ground he covers, the orientation warp 40 degrees.
				// Outside that the planted foot skates by the difference (the p14 capture: a 0.65 m/s walk loop held under a
				// man jogging 3 m/s by the dwell and the continuing pose's bonus; a sideways sprint under a man who had turned
				// upfield; 36 % of matched frames skated over 1 m/s). After 0.15 s of that the next search is a fresh pick.
				{
					static const bool bNoMisfit = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoMisfit"));
					bool bMisfit = false;
					if (M->bMatch && Speed > 120.f)
					{
						const float ClipSp = M->MatchedClipSpeed * FMath::Max(M->MatchedWantedRate, 0.1f);
						const float Ratio = Speed / FMath::Max(ClipSp, 1.f);
						bMisfit = Ratio > (bCrouch ? 1.85f : 1.6f) || Ratio < 0.6f || FMath::Abs(Ang) > 50.f;
					}
					MMMisfitT = bMisfit ? MMMisfitT + Dt : 0.f;
					MMForceCool -= Dt;
					if (!bNoMisfit && MMMisfitT > 0.15f && MMForceCool <= 0.f)
					{
						++M->ForceSerial;
						MMForceCool = 0.6f; // (if the databases hold nothing better, not a forced pick every search)
						MMMisfitT = 0.f;
						static int32 GForced = 0;
						if (++GForced % 500 == 0) UE_LOG(LogTemp, Display, TEXT("TAG MM misfit: %d forced picks"), GForced);
					}
				}
				// (eased at 5/s: at 10/s a new match swung the hips and both legs 60 degrees in a few frames)
				OrientWarpSm = FMath::FInterpTo(OrientWarpSm, WarpWant, Dt, 5.f);
				RI->Proc.OrientWarpDeg = OrientWarpSm;
				TelOrient = OrientWarpSm;
				// the stride is stretched along the way the feet actually travel: the clip's direction as warped (the
				// man's own path only where there is no clip direction to go by)
				const FVector FeetW = ClipW.IsNearlyZero() ? V.GetSafeNormal2D() : ClipW.RotateAngleAxis(OrientWarpSm, FVector::UpVector);
				RI->Proc.StrideDirCS = MHBody->GetComponentTransform().InverseTransformVectorNoScale(FeetW);
			}
	}

	// the body's own velocity for the next frame: where the playing clip's root travels (turned by the orientation warp
	// the legs get), at the speed its feet cover as played times the stride scale they get
	// (motion matched running: only with -TagFollowMM. Its speed match and warps already keep the planted foot on its
	// spot - 7 cm/s of travel on the feet the capture holds planted, p17o - and following there only added lag.)
	static const bool bFollowMM = FParse::Param(FCommandLine::Get(), TEXT("TagFollowMM"));
	if (bFollowOn && bFollowMM && M->bMatch && bInMatch && MHBody && (!RootVelW.IsNearlyZero() || !M->MatchedClipDir.IsNearlyZero() || M->bRootDeltaValid))
		if (const UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()))
		{
			FVector VelW = RootVelW;
			if (!M->bRootDeltaValid)
			{
				VelW = (M->bRootRotValid ? M->RootRotW : MHBody->GetComponentQuat()).RotateVector(M->MatchedClipDir);
				VelW.Z = 0.f;
				VelW = VelW.GetSafeNormal() * (M->MatchedClipSpeed * M->MatchedWantedRate * M->SpeedRate);
			}
			FollowVel = VelW.RotateAngleAxis(RI->Proc.OrientWarpDeg, FVector::UpVector) * RI->Proc.StrideScale;
			bFollowActive = true;
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
			FString DbNames;
			for (UPoseSearchDatabase* X : M->Databases) DbNames += (DbNames.IsEmpty() ? TEXT("") : TEXT("+")) + X->GetName().Replace(TEXT("PSD_"), TEXT(""));
			UE_LOG(LogTemp, Display, TEXT("TAG mm trace t=%.2f v=%s |v|=%.0f a=%s fut=%.0f hist=%d p-0.3=%s p+0.5=%s p+1.0=%s face+1.0=%.0f db=%s mt=%.2f pivot=%d crouch=%d set=%s mm=%s at=%.2f cost=%.3f cont=%d want=%.2f clip=%.0f mul=%.2f"),
				Now, *V.ToCompactString(), Speed, *SmoothedAccel.ToCompactString(), FutSpeed, TrajTime.Num(), *Loc(H3.Position), *Loc(F5.Position), *Loc(F10.Position),
				FMath::RadiansToDegrees((Cur.GetRotation().Inverse() * F10.Facing).GetTwistAngle(FVector::UpVector)),
				bMoving ? TEXT("move") : TEXT("stop"), MovingTime, bPivot ? 1 : 0, bCrouch ? 1 : 0, *DbNames, *GetNameSafe(M->GetMatchedAsset()),
				M->MatchedTime, M->MatchedCost, M->bMatchedContinuing ? 1 : 0, M->MatchedWantedRate, M->MatchedClipSpeed, M->SpeedRate);
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
	FaceFollowBody(true);
	V->SetCollisionProfileName(TEXT("Ragdoll"));
	V->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	V->SetAllBodiesSimulatePhysics(true);
	V->SetSimulatePhysics(true);
	V->WakeAllRigidBodies();
	// carry the player's momentum into the fall
	V->SetAllPhysicsLinearVelocity(VelWorld * 0.85f + FVector(0, 0, -50));
}

void AAthlete::PlayCatch(float BallZCm, float SpeedCm, const FVector& BallVelW, const FVector& SidelineDirW,
	const FVector& BallPosW, bool bContested, bool bAggressive)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	const FVector Fwd = GetActorForwardVector(), Right = GetActorRightVector();
	const FVector Rel = BallPosW.IsNearlyZero() ? FVector::ZeroVector : BallPosW - GetActorLocation();
	const float Lateral = FVector::DotProduct(FVector(Rel.X, Rel.Y, 0.f), Right);   // + = ball to his right
	const float Along = FVector::DotProduct(BallVelW.GetSafeNormal2D(), Fwd);         // + = ball travelling his way
	const bool bHanded = Player && (Player->id % 2) == 1;
	static int32 GCatchSeq = 0;
	const int32 Seq = GCatchSeq++;
	UAnimationAsset* C = nullptr;
	bool bMirror = bHanded;
	// a hard catch made: a defender on him, the ball over his reach or wide of his body - the pack's one-handers,
	// fingertip grabs and leaps (the aggressive catch, TRIANGLE held with the ball in the air, goes up for them more)
	{
		const int32 Hard = (bContested ? 1 : 0) + (BallZCm > 205.f ? 1 : 0) + (FMath::Abs(Lateral) > 50.f ? 1 : 0) + (FMath::Abs(Lateral) > 75.f ? 1 : 0);
		static const bool bAllSpec = FParse::Param(FCommandLine::Get(), TEXT("TagSpectacularAll")); // look-dev: every catch
		const float Rate = bAllSpec ? 1.f : (Hard >= 2 ? 0.75f : Hard == 1 ? 0.3f : 0.f) + (bAggressive ? (Hard ? 0.35f : 0.15f) : 0.f);
		UE_LOG(LogTemp, Verbose, TEXT("TAG catch #%d hard %d (contested %d, ball %.0f cm, lateral %.0f cm)"), Player ? Player->number : -1, Hard, bContested ? 1 : 0, BallZCm, Lateral);
		if (SidelineDirW.IsNearlyZero() && A.PackCatchSpectacular.Num() && FMath::Frac(Seq * 0.6180339f + 0.13f) < Rate)
			if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(A.PackCatchSpectacular, Seq * 5 + 1))
			{
				UE_LOG(LogTemp, Display, TEXT("TAG spectacular catch #%d: %s (hard %d%s)"), Player ? Player->number : -1, *GetNameSafe(P->Anim), Hard, bAggressive ? TEXT(", aggressive") : TEXT(""));
				PlayOneShotFrom(P->Anim, P->Start);
				bOneShotCatch = true;
				return;
			}
	}
	if (!SidelineDirW.IsNearlyZero() && A.CatchSidelineL && A.CatchSidelineR)
	{
		C = FVector::DotProduct(SidelineDirW, Right) > 0.f ? A.CatchSidelineR.Get() : A.CatchSidelineL.Get(); // reach to the boundary, drag the toes
		bMirror = false;
	}
	else if (bContested && BallZCm > 170.f && A.CatchHighPoint)
		C = A.CatchHighPoint; // jump ball: high point it over the defender
	else if (SpeedCm > 350.f)
	{
		if (Along > 0.5f && A.CatchOverShoulder && A.CatchBackShoulder)
		{
			// thrown over the top from behind: over the shoulder in stride, or back to the outside shoulder
			const bool bBack = FMath::Abs(Lateral) > 45.f || Along < 0.8f;
			C = bBack ? A.CatchBackShoulder.Get() : A.CatchOverShoulder.Get();
			if (bBack) bMirror = Lateral > 0.f; // the keyed one turns back over the left shoulder
		}
		else if (BallZCm < 70.f && A.CatchScoop) C = A.CatchScoop;
		else if (FMath::Abs(Lateral) > 55.f && A.CatchOneHand)
		{
			C = A.CatchOneHand; // thrown wide: stab it with the near hand
			bMirror = Lateral > 0.f;
		}
		else C = A.CatchInStride; // the everyday one: hands out in front, in stride
	}
	else if (Along < -0.5f && A.CatchComeback && Seq % 3 == 0)
		C = A.CatchComeback; // working back to the quarterback
	else
		C = BallZCm > 160.f ? A.CatchHigh.Get() : BallZCm < 80.f ? A.CatchLow.Get() : A.CatchChest.Get();
	// every other catch a library capture of the same kind, from 4 frames before the hands reach
	{
		const TArray<FAthleteAssets::FPackClip>* Lib =
			(C == A.CatchSidelineL || C == A.CatchSidelineR) ? &A.PackCatchSideline :
			(C == A.CatchHighPoint || C == A.CatchHigh) ? &A.PackCatchHigh :
			(C == A.CatchOverShoulder || C == A.CatchBackShoulder) ? &A.PackCatchOverShoulder :
			(C == A.CatchScoop || C == A.CatchLow) ? &A.PackCatchLow :
			(C == A.CatchComeback) ? &A.PackCatchComeback :
			(C == A.CatchChest) ? &A.PackCatchStand : &A.PackCatchStride;
		if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(*Lib, Seq)) { PlayOneShotFrom(P->Anim, P->Start); bOneShotCatch = true; return; }
	}
	if (C) PlayOneShot(A.Side(C, bMirror));
	bOneShotCatch = C != nullptr;
}

void AAthlete::PlayThrow(int32 Kind)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (Kind < 0 || Kind >= FAthleteAssets::ThrowCount || !A.Throws[Kind]) return;
	CurrentClip = nullptr; // restart cleanly in the chosen throw
	// every other throw a library capture of the kind, from 7 frames before its release
	static int32 GThrowSeq = 0;
	const int32 Seq = GThrowSeq++;
	const TArray<FAthleteAssets::FPackClip>* Lib = Kind == 1 ? &A.PackThrowsQuick : Kind == 2 ? &A.PackThrowsDeep : Kind == 6 ? &A.PackThrowsRollR : Kind == 7 ? &A.PackThrowsRollL : &A.PackThrowsSet;
	if (!Lib->Num()) Lib = &A.PackThrowsSet;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(*Lib, Seq))
	{
		PlayClip(P->Anim, false, 1.f, P->Start);
		return;
	}
	PlayClip(A.Throws[Kind], false, 1.f, A.ThrowStart[Kind]);
}

void AAthlete::PlayThrowTimed(int32 Kind, float ReleaseIn)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (Kind < 0 || Kind >= FAthleteAssets::ThrowCount || !A.Throws[Kind]) return;
	static int32 GThrowSeq = 0;
	const int32 Seq = GThrowSeq++;
	const TArray<FAthleteAssets::FPackClip>* Lib = Kind == 1 ? &A.PackThrowsQuick : Kind == 2 ? &A.PackThrowsDeep : Kind == 6 ? &A.PackThrowsRollR : Kind == 7 ? &A.PackThrowsRollL : &A.PackThrowsSet;
	if (!Lib->Num()) Lib = &A.PackThrowsSet;
	// (the families start 7 frames before the capture's release: its release is there + 7/30 s)
	UAnimationAsset* Clip = A.Throws[Kind];
	float ReleaseAt = A.ThrowStart[Kind] + 7.f / 30.f;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(*Lib, Seq)) { Clip = P->Anim; ReleaseAt = P->Start + 7.f / 30.f; }
	PlayOneShotFrom(Clip, FMath::Max(0.f, ReleaseAt - FMath::Max(0.f, ReleaseIn)));
	bThrowTimed = true;
	ThrowTimedClip = Clip;
	UE_LOG(LogTemp, Verbose, TEXT("TAG throw timed: %s, release in %.2f s (clip from %.2f)"), *GetNameSafe(Clip), ReleaseIn, FMath::Max(0.f, ReleaseAt - ReleaseIn));
}

bool AAthlete::PlayBallDefense(int32 Gait, bool bInt)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<FAthleteAssets::FPackClip>& L = bInt ? A.PackIntCatch : Gait == 0 ? A.PackSwatRun : Gait == 1 ? A.PackSwatStrafe : A.PackSwatBack;
	if (!L.Num()) return false;
	static int32 GSeq = 0;
	const FAthleteAssets::FPackClip& P = L[(GSeq++) % L.Num()];
	PlayOneShotFrom(P.Anim, P.Start);
	bOneShotCatch = bInt;
	UE_LOG(LogTemp, Display, TEXT("TAG ball defense #%d: %s"), Player ? Player->number : -1, *GetNameSafe(P.Anim));
	return true;
}

bool AAthlete::PlayKickCatch(bool bRunning)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<FAthleteAssets::FPackClip>& L = bRunning && A.PackKickCatchRun.Num() ? A.PackKickCatchRun : A.PackKickCatchStand.Num() ? A.PackKickCatchStand : A.PackKickCatchRun;
	if (!L.Num()) return false;
	static int32 GSeq = 0;
	const FAthleteAssets::FPackClip& P = L[(GSeq++) % L.Num()];
	PlayOneShotFrom(P.Anim, P.Start);
	bOneShotCatch = true;
	UE_LOG(LogTemp, Display, TEXT("TAG kick catch #%d: %s"), Player ? Player->number : -1, *GetNameSafe(P.Anim));
	return true;
}

void AAthlete::PlayShovelTimed(bool bMoving, float ReleaseIn)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<FAthleteAssets::FPackClip>& L = bMoving && A.PackShovelMove.Num() ? A.PackShovelMove : A.PackShovelSet.Num() ? A.PackShovelSet : A.PackShovelMove;
	if (!L.Num()) { PlayThrowTimed(FAthleteAssets::ThrowQuickK, ReleaseIn); return; }
	static int32 GSeq = 0;
	const FAthleteAssets::FPackClip& P = L[(GSeq++) % L.Num()];
	const float ReleaseAt = P.Start + 7.f / 30.f;
	PlayOneShotFrom(P.Anim, FMath::Max(0.f, ReleaseAt - FMath::Max(0.f, ReleaseIn)));
	bThrowTimed = true;
	ThrowTimedClip = P.Anim;
	UE_LOG(LogTemp, Display, TEXT("TAG shovel pass: %s"), *GetNameSafe(P.Anim));
}

void AAthlete::PlayPumpFake()
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.PackPumpFakes.Num()) return;
	static int32 GSeq = 0;
	const FAthleteAssets::FPackClip& P = A.PackPumpFakes[(GSeq++) % A.PackPumpFakes.Num()];
	PlayOneShotFrom(P.Anim, 0.f);
	UE_LOG(LogTemp, Display, TEXT("TAG pump fake: %s"), *GetNameSafe(P.Anim));
}

bool AAthlete::IsThrowing() const { return bThrowTimed && ThrowTimedClip && OneShot == ThrowTimedClip && OneShotLeft > 0.f; }

void AAthlete::ApplyHit(const FVector& TacklerVelW, float TacklerKg, const FVector& TacklerLoc)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (bHitApplied || TackledLeft <= 0.f) return;
	bHitApplied = true;
	// where the hit comes from: the tackler's motion, or his position if he was nearly still
	FVector HitDir = FVector(TacklerVelW.X, TacklerVelW.Y, 0.f);
	if (HitDir.Size() < 150.f) HitDir = GetActorLocation() - TacklerLoc;
	HitDir.Z = 0.f;
	HitDir = HitDir.GetSafeNormal();
	const float Front = FVector::DotProduct(HitDir, GetActorForwardVector());  // + = pushed the way he faces (from behind)
	const float Side = FVector::DotProduct(HitDir, GetActorRightVector());     // + = pushed to his right
	UAnimationAsset* R = A.TackledHit;                                         // head on
	if (Front > 0.5f && A.TackledFromBehind) R = A.TackledFromBehind;
	else if (FMath::Abs(Front) <= 0.5f && A.TackledFromSide) R = A.Side(A.TackledFromSide, Side > 0.f); // knocked to the side the hit drives him
	// every other hit the carrier's reaction is a library capture hit from that side, from 2 frames before he goes down
	static int32 GHitSeq = 0;
	float Start = 0.f;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(Front > 0.5f ? A.PackTackledBehind : FMath::Abs(Front) <= 0.5f ? A.PackTackledSide : A.PackTackledFront, GHitSeq++))
		{ R = P->Anim; Start = P->Start; }
	if (R && R != CurrentClip)
	{
		PlayClip(R, false, 1.f, Start);
		TackledLeft = R->GetPlayLength() - Start;
	}
	// the pile moves with both players' momentum (kg * cm/s), the fall carries it
	const float CarrierKg = Player ? static_cast<float>(Player->weightLb) * 0.4536f : 100.f;
	const FVector P = LastVelW * CarrierKg + FVector(TacklerVelW.X, TacklerVelW.Y, 0.f) * TacklerKg;
	const FVector V = P / FMath::Max(1.f, CarrierKg + TacklerKg);
	PileVel = V.GetClampedToMaxSize(650.f) + HitDir * 120.f - FVector(0, 0, 60);
	UE_LOG(LogTemp, Verbose, TEXT("TAG hit on #%d: %s (front %.2f side %.2f), pile %.0f cm/s"), Player ? Player->number : -1,
		R ? *R->GetName() : TEXT("-"), Front, Side, PileVel.Size2D());
}

void AAthlete::PlayMissedTackle(bool bCarrierLeft)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.TackleMiss || bRagdoll) return;
	bMissing = true;
	static int32 GWhiffSeq = 0;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(A.PackWhiffs, GWhiffSeq++)) { PlayOneShotFrom(P->Anim, P->Start); return; }
	PlayOneShot(A.Side(A.TackleMiss, !bCarrierLeft));
	UE_LOG(LogTemp, Verbose, TEXT("TAG missed tackle #%d"), Player ? Player->number : -1);
}

void AAthlete::PlayBrokenTackle(bool bHitFromRight)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.BrokenStumble || bRagdoll) return;
	static int32 GStumbleSeq = 0;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(A.PackStumbles, GStumbleSeq++)) { PlayOneShotFrom(P->Anim, P->Start); return; }
	PlayOneShot(A.Side(A.BrokenStumble, !bHitFromRight)); // the keyed one is hit on his right, stumbles left
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
	const FAthleteAssets& A = FAthleteAssets::Get();
	static int32 GDiveSeq = 0;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(A.PackCatchDive, GDiveSeq++)) { PlayOneShotFrom(P->Anim, P->Start); return; }
	if (A.CatchDive) PlayOneShot(A.Side(A.CatchDive, Player && (Player->id % 2) == 1));
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
	static int32 GUpSide = 0;
	const bool bUpMirror = (GUpSide++ % 2) == 1; // come up over either shoulder
	static int32 GGetUpSeq = 0; // every other face-up landing rolls to the side and up instead
	if (!bFaceDown && A.GetUpRoll && (GGetUpSeq++ % 2)) Clip = A.GetUpRoll.Get();
	// one in three from the pack library's get-ups of that landing (in place, low intensity)
	{
		static int32 GLibSeq = 0;
		const TArray<TObjectPtr<UAnimationAsset>>& Lib = bFaceDown ? A.PackGetUpsFaceDown : A.PackGetUpsFaceUp;
		if (Lib.Num() && (GLibSeq++ % 3) == 2) Clip = Lib[(GLibSeq / 3) % Lib.Num()].Get();
	}
	static int32 GSlowSeq = 0;  // now and then a player is shaken up and slow to get up (hand on the ribs)
	bInjuredChain = false;
	if (bFaceDown && A.GetUpSlow && ((GSlowSeq++ % 5) == 4 || bForceInjury))
	{
		Clip = A.GetUpSlow.Get();
		// every other one of those (and on request) he is hurt: stays down clutching the leg before getting up
		static int32 GHurtSeq = 0;
		if (A.InjuredDown && ((GHurtSeq++ % 2) == 1 || bForceInjury))
		{
			Clip = A.InjuredDown.Get();
			if (A.PackInjuries.Num() && (GHurtSeq % 3) != 1) Clip = A.PackInjuries[GHurtSeq % A.PackInjuries.Num()].Get(); // the pack's field injuries
			bInjuredChain = true;
			bInjuredMirror = bUpMirror;
			bForceInjury = false;
			InjuredLeft = Clip->GetPlayLength();
			UE_LOG(LogTemp, Verbose, TEXT("TAG injury: #%d stays down"), Player ? Player->number : -1);
		}
	}
	if (!Clip) return;

	const int32 NB = V->GetNumBones();
	TArray<FTransform> W;
	W.SetNum(NB);
	for (int32 i = 0; i < NB; ++i) W[i] = V->GetBoneTransform(i); // ragdoll pose, world space
	ClearReplayHold(); // (or the pose a replay left him in)

	bRagdoll = false;
	FaceFollowBody(false);
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
	Clip = A.Side(Clip, bUpMirror);
	PlayClip(Clip, false);
	GetUpLeft = Clip->GetPlayLength() + (bInjuredChain && A.GetUpSlow ? A.GetUpSlow->GetPlayLength() : 0.f);
	bGettingUp = true;
	bStoodUp = true;
	CurrentAnim.Reset();
	UE_LOG(LogTemp, Verbose, TEXT("TAG one-shot %s #%d"), *Clip->GetName(), Player ? Player->number : -1);
}

void AAthlete::StandOnSideline(const FVector& Loc, float Yaw, int32 Variant)
{
	SidelineVariant = Variant;
	SetActorLocationAndRotation(Loc, FRotator(0, Yaw, 0));
	// bench players are small in frame and helmeted: the face follows the head (leader pose) instead of running the
	// facial rig, and the visible body updates at a reduced rate when it is small on screen
	if (MHFace && MHBody)
	{
		MHFace->SetAnimInstanceClass(nullptr);
		MHFace->SetLeaderPoseComponent(MHBody);
		bFaceOnLeader = true;
	}
	if (MHBody) MHBody->bEnableUpdateRateOptimizations = true;
	// model pack players: about four in ten on the bench have the helmet off
	if (FAthleteAssets::Get().bHero && HeroHelmet && HeroHelmet->GetSkeletalMeshAsset() && Player && Player->id < 900000)
	{
		SetHelmetOn(FMath::Frac(Variant * 0.618f + 0.11f) >= 0.4f);
		if (!bHeroHelmetOn && Variant % 3 == 0) WearCap(Variant / 3); // some wear a cap on the bench
	}
	SidelineCheer(false);
}

void AAthlete::SleepWhenOffscreen(float Dt)
{
	if (!Body || !MHBody) return;
	SleepClock += Dt;
	// on screen this frame or the last (the shadow pass alone does not count: a bench shadow holding still reads fine)
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	auto Seen = [Now](const UPrimitiveComponent* C) { return C && C->IsVisible() && Now - C->GetLastRenderTimeOnScreen() < 0.12; };
	const bool bAwake = SleepClock < 1.f || Seen(MHBody) || Seen(MHFace) || Seen(HeroHelmet);
	if (bAwake == bAnimAwake) return;
	bAnimAwake = bAwake;
	Body->SetComponentTickEnabled(bAwake);
	MHBody->SetComponentTickEnabled(bAwake);
}

void AAthlete::PlayRushMove(const FString& Move, const AAthlete* Blocker)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	// the side he beats his man on: with the blocker on his right he goes past on the left, the side the swim, long
	// arm, club and dip-rip are keyed to (the rip is keyed to the right)
	const bool bBlockerRight = !Blocker || FVector::DotProduct(Blocker->GetActorLocation() - GetActorLocation(), GetActorRightVector()) > 0.f;
	UAnimationAsset* C = nullptr;
	if (Move == TEXT("swim")) C = A.Side(A.ShedSwim, !bBlockerRight);
	else if (Move == TEXT("rip")) C = A.Side(A.ShedRip, bBlockerRight);
	else if (Move == TEXT("club")) C = A.Side(A.RushClub, !bBlockerRight);
	else if (Move == TEXT("longarm")) C = A.Side(A.LongArm, !bBlockerRight);
	else if (Move == TEXT("diprip")) C = A.Side(A.DipRip, !bBlockerRight);
	else if (Move == TEXT("spin")) C = bBlockerRight ? A.RushSpinR.Get() : A.RushSpinL.Get();
	else if (Move == TEXT("bull")) C = A.RushBull;
	if (!C) C = A.Side(A.ShedSwim, !bBlockerRight);
	// every other rush a library win past the blocker on the open side
	static int32 GRushSeq = 0;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(bBlockerRight ? A.PackRushWinsL : A.PackRushWinsR, GRushSeq++)) C = P->Anim;
	UE_LOG(LogTemp, Display, TEXT("TAG rush move #%d: %s (%s)"), Player ? Player->number : -1, *Move, C ? *C->GetName() : TEXT("-"));
	if (C) PlayOneShot(C);
}

bool AAthlete::PlayRepWin(AAthlete* Rusher, AAthlete* Blocker, int32 Which, bool bBig)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<FAthleteAssets::FLineRep>& Pool = bBig && A.RepBigWins.Num() ? A.RepBigWins : A.RepWins;
	if (!Rusher || !Blocker || !Pool.Num()) return false;
	const FAthleteAssets::FLineRep& R = Pool[FMath::Abs(Which) % Pool.Num()];
	// both from the capture's first frame: the two start it squared up at arm's length, as the stand-off leaves them
	Rusher->PlayOneShotFrom(R.D.Get(), 0.f);
	Rusher->OneShotRate = 1.f;
	Rusher->OneShotLeft = R.D->GetPlayLength() - 0.07f;
	Rusher->bOneShotRep = true;
	Blocker->PlayOneShotFrom(R.O.Get(), 0.f);
	Blocker->OneShotRate = 1.f;
	Blocker->OneShotLeft = R.O->GetPlayLength() - 0.07f;
	Blocker->bOneShotRep = true;
	UE_LOG(LogTemp, Display, TEXT("TAG rush win #%d past #%d: %s"), Rusher->Player ? Rusher->Player->number : -1, Blocker->Player ? Blocker->Player->number : -1, *R.D->GetName());
	return true;
}

bool AAthlete::PlayRepHold(AAthlete* Rusher, AAthlete* Blocker, int32 Which, bool bPower)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	const TArray<FAthleteAssets::FLineRep>& Pool = bPower && A.RepPowerHolds.Num() ? A.RepPowerHolds : A.RepHolds;
	if (!Rusher || !Blocker || !Pool.Num()) return false;
	const FAthleteAssets::FLineRep& R = Pool[FMath::Abs(Which) % Pool.Num()];
	// from the capture's first frame, both men, as a win is played; it runs out back into the stand-off
	Rusher->PlayOneShotFrom(R.D.Get(), 0.f);
	Rusher->OneShotRate = 1.f;
	Rusher->OneShotLeft = R.D->GetPlayLength() - 0.07f;
	Rusher->bOneShotRep = true;
	Blocker->PlayOneShotFrom(R.O.Get(), 0.f);
	Blocker->OneShotRate = 1.f;
	Blocker->OneShotLeft = R.O->GetPlayLength() - 0.07f;
	Blocker->bOneShotRep = true;
	UE_LOG(LogTemp, Display, TEXT("TAG rush stoned #%d by #%d: %s"), Rusher->Player ? Rusher->Player->number : -1, Blocker->Player ? Blocker->Player->number : -1, *R.D->GetName());
	return true;
}

float AAthlete::PlayRepDouble(AAthlete* Rusher, AAthlete* B1, AAthlete* B2, int32 Which)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!Rusher || !B1 || !B2 || !A.RepDoubles.Num()) return 0.f;
	const FAthleteAssets::FDoubleRep& R = A.RepDoubles[FMath::Abs(Which) % A.RepDoubles.Num()];
	// the blocker more to the rusher's left takes the capture's left man
	const FVector Rt = Rusher->GetActorRightVector();
	const bool bB1Left = FVector::DotProduct(B1->GetActorLocation() - Rusher->GetActorLocation(), Rt) < FVector::DotProduct(B2->GetActorLocation() - Rusher->GetActorLocation(), Rt);
	AAthlete* L = bB1Left ? B1 : B2;
	AAthlete* Rr = bB1Left ? B2 : B1;
	const float Len = R.T1 - R.T0;
	auto Go = [&](AAthlete* Who, UAnimSequence* Part)
	{
		Who->PlayOneShotFrom(Part, R.T0);
		Who->OneShotRate = 1.f;
		Who->OneShotLeft = Len - 0.05f;
		Who->bOneShotRep = true;
	};
	Go(Rusher, R.D.Get());
	Go(L, R.A.Get());
	Go(Rr, R.B.Get());
	UE_LOG(LogTemp, Display, TEXT("TAG double team on #%d by #%d and #%d: %s (%.1f s)"), Rusher->Player ? Rusher->Player->number : -1, L->Player ? L->Player->number : -1,
		Rr->Player ? Rr->Player->number : -1, *R.D->GetName(), Len);
	return Len;
}

bool AAthlete::PlayRepHandoff(AAthlete* QB, AAthlete* Back, bool bGun, float ExchangeIn, int32 Which)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!QB || !Back || !A.RepHandoffs.Num()) return false;
	// (the captures cannot be mirrored: one with the back on the same side of the quarterback as ours, from the same
	// alignment if there is one)
	const bool bBackLeft = FVector::DotProduct(Back->GetActorLocation() - QB->GetActorLocation(), QB->GetActorRightVector()) < 0.f;
	TArray<const FAthleteAssets::FHandoffRep*> Fit;
	for (const FAthleteAssets::FHandoffRep& R : A.RepHandoffs) if (R.bGun == bGun && (R.Bx > 0.f) == bBackLeft) Fit.Add(&R);
	if (!Fit.Num()) for (const FAthleteAssets::FHandoffRep& R : A.RepHandoffs) if ((R.Bx > 0.f) == bBackLeft) Fit.Add(&R);
	if (!Fit.Num()) for (const FAthleteAssets::FHandoffRep& R : A.RepHandoffs) if (R.bGun == bGun) Fit.Add(&R);
	if (!Fit.Num()) for (const FAthleteAssets::FHandoffRep& R : A.RepHandoffs) Fit.Add(&R);
	const FAthleteAssets::FHandoffRep& R = *Fit[FMath::Abs(Which) % Fit.Num()];
	const float From = FMath::Max(0.f, R.T - FMath::Max(0.f, ExchangeIn));
	QB->PlayOneShotFrom(R.Q.Get(), From);
	QB->OneShotLeft = FMath::Min(QB->OneShotLeft, R.T - From + 0.9f);  // the exchange and the carry-out, then his own motion
	Back->PlayOneShotFrom(R.B.Get(), From);
	Back->OneShotLeft = FMath::Min(Back->OneShotLeft, R.T - From + 0.6f); // the mesh and the first steps with it, then the run
	UE_LOG(LogTemp, Display, TEXT("TAG handoff mesh: %s (exchange in %.2f s)"), *R.Q->GetName(), ExchangeIn);
	return true;
}

void AAthlete::PlayBeaten(const AAthlete* Rusher)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	if (!A.Beaten || !Rusher) return;
	// keyed with the rusher getting by on his left
	const bool bRusherLeft = FVector::DotProduct(Rusher->GetActorLocation() - GetActorLocation(), GetActorRightVector()) < 0.f;
	static int32 GBeatenSeq = 0;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(bRusherLeft ? A.PackBeatenL : A.PackBeatenR, GBeatenSeq++)) { PlayOneShot(P->Anim); return; }
	PlayOneShot(A.Side(A.Beaten, !bRusherLeft));
}

void AAthlete::PlayPancakeFinish()
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	static int32 GPancakeSeq = 0;
	if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(A.PackPancakes, GPancakeSeq++)) { PlayOneShotFrom(P->Anim, P->Start); return; }
	if (A.BlockFinish) PlayOneShot(A.BlockFinish);
}

void AAthlete::FaceFollowBody(bool bFollow)
{
	if (!MHFace || !MHBody || bFaceOnLeader) return;
	MHFace->SetLeaderPoseComponent(bFollow ? MHBody.Get() : nullptr);
}

void AAthlete::BeginPairedFall(UAnimationAsset* Clip, const FVector& From, const FVector& To, float YawW, float DriveTime,
	float LieTime, bool bEaseIn, float Start, bool bRootDrive)
{
	if (!Clip || bRagdoll) return;
	ClearReplayHold();
	OneShot = nullptr;
	OneShotLeft = 0.f;
	TackledLeft = 0.f;
	FallHoldLeft = 0.f;
	TackleOn = nullptr;
	bMissing = false;
	bGettingUp = false;
	bFalling = true;
	FallClip = Clip;
	FallStart = Start;
	bFallRoot = bRootDrive && Cast<UAnimSequence>(Clip) != nullptr;
	FallT = 0.f;
	FallLen = Clip->GetPlayLength() - Start;
	FallLie = LieTime;
	FallDrive = FMath::Max(0.05f, DriveTime);
	FallFrom = GetActorLocation();
	// his run carries into the hit: the glide onto the capture starts at his speed (the smoothstep from a standing
	// start stopped a man at full speed dead in a frame, a 6 m/s jump at the contact) and the speed bleeds off like a
	// hit's (about 0.12 s), handing over to the capture's own motion
	FallV0 = bEaseIn ? FVector(LastVelW.X, LastVelW.Y, 0.f).GetClampedToMaxSize(1000.f) : FVector::ZeroVector;
	FallFromYaw = GetActorRotation().Yaw;
	AnchorA = FVector(From.X, From.Y, FallFrom.Z);
	AnchorB = FVector(To.X, To.Y, FallFrom.Z);
	FallYaw = YawW;
	bFallEase = bEaseIn;
	FallEaseT = 0.2f;
	VisOffset = FVector::ZeroVector;
	// restart the clip even if it is already playing, and crossfade out of the pose shown now (the run, the tackle,
	// the hit). Clearing CurrentClip alone skipped the blend (StartBodyAsset only blends from a playing clip): the body
	// snapped into the fall's first frame, a 60-140 cm jump of the head in one frame.
	CurrentClip = nullptr;
	PlayClip(Clip, false, 1.f, Start);
	if (MHBody)
		if (UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance())) ++RI->Proc.ClipSerial;
	if (!bEaseIn) SetActorLocationAndRotation(AnchorA, FRotator(0, YawW, 0));
	SmoothedYaw = YawW;
	UE_LOG(LogTemp, Verbose, TEXT("TAG fall #%d: %s, drive %.0f cm, lie %.2f s"), Player ? Player->number : -1, *Clip->GetName(),
		FVector::Dist2D(AnchorA, AnchorB), LieTime);
}

void AAthlete::ShiftFallTarget(const FVector& Delta)
{
	// the pile ends somewhere else than first thought (help stopped it): where the drive is still going, it goes there
	if (!bFalling || FallT >= FallDrive) return;
	// from where the drive has him now: moving only the end point jumped him by the shift times the progress (a 35 cm
	// pop of the pelvis); the start moves back by as much so this frame's spot stays put and the rest of the drive carries it
	// (and no more than the rest of the drive carries at a pile's walking pace: a shift arriving with 0.07 s of drive
	// left went by in two frames, the whole pile jumping 30 cm; what is left over only moves the umpire's spot)
	const float D = FMath::Clamp(FallT / FallDrive, 0.f, 1.f);
	const float P = 1.f - FMath::Square(1.f - D);
	if (1.f - P < 1e-3f) return;
	const FVector Dl = FVector(Delta.X, Delta.Y, 0.f).GetClampedToMaxSize(150.f * (FallDrive - FallT));
	AnchorB += Dl;
	AnchorA -= Dl * (P / (1.f - P));
	PileBodyEnd += Dl;
}

float AAthlete::MassKg() const
{
	return Player ? static_cast<float>(Player->weightLb) * 0.4536f : 100.f;
}

void AAthlete::BeginScene(UAnimSequence* Clip, const FVector& AnchorW, float YawW, float Start, bool bLoop)
{
	if (!Clip || bRagdoll) return;
	ClearReplayHold();
	OneShot = nullptr;
	OneShotLeft = 0.f;
	TackledLeft = 0.f;
	FallHoldLeft = 0.f;
	bFalling = false;
	bGettingUp = false;
	bMissing = false;
	bScene = true;
	bSceneLoop = bLoop;
	SceneClip = Clip;
	SceneStart = Start;
	SceneT = 0.f;
	SceneYaw = YawW;
	SceneAnchor = FVector(AnchorW.X, AnchorW.Y, GetActorLocation().Z);
	// a man a step from the capture's start is eased onto it, not put there in a frame (the helper of a help-up began
	// 43 cm short of his mark and jumped, p64 c026); a placement from far off (a kick's set-up) is still a cut
	SceneEaseOff = GetActorLocation() - SceneAnchor;
	SceneEaseOff.Z = 0.f;
	// (and his facing: turned to the capture's in a frame, the crossfaded pose swung his feet 60+ cm, p71 c004)
	SceneEaseYaw = FMath::FindDeltaAngleDegrees(YawW, GetActorRotation().Yaw);
	if (bLoop || SceneEaseOff.SizeSquared2D() > 80.f * 80.f) { SceneEaseOff = FVector::ZeroVector; SceneEaseYaw = 0.f; }
	VisOffset = FVector::ZeroVector;
	CurrentClip = nullptr; // (restart and crossfade out of the pose shown now)
	PlayClip(Clip, bLoop, 1.f, Start);
	if (MHBody)
		if (UTagRetargetAnimInstance* RI = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance())) ++RI->Proc.ClipSerial;
	SetActorLocationAndRotation(SceneAnchor + SceneEaseOff, FRotator(0, YawW + SceneEaseYaw, 0)); // (from where he stands: the ease starts there)
	SmoothedYaw = YawW + SceneEaseYaw;
}

void AAthlete::TickScene(float DeltaTime)
{
	UAnimSequence* Seq = SceneClip.Get();
	if (!Seq) { bScene = false; return; }
	SceneT += DeltaTime;
	if (bSceneLoop)
	{
		// a wait in place: the loop runs on its own clock, the body on its anchor
		SetActorLocationAndRotation(SceneAnchor, FRotator(0, SceneYaw, 0));
		PlayClip(Seq, true, 1.f, SceneStart);
		return;
	}
	// the capture's root carries the body (the same transform a library fall uses: TickFall), the pose set at the time
	// the root was taken at, held a frame short of the end
	const float Tc = FMath::Clamp(SceneStart + SceneT, 0.f, FMath::Max(SceneStart, Seq->GetPlayLength() - 1.f / 30.f));
	const FTransform RM = Seq->ExtractRootMotionFromRange(SceneStart, Tc, FAnimExtractContext(0.0, true));
	const USkeletalMeshComponent* V = Visible();
	FTransform Crel = V ? V->GetComponentTransform().GetRelativeTransform(GetActorTransform()) : FTransform::Identity;
	Crel.SetScale3D(FVector::OneVector);
	const FTransform Now = Crel.Inverse() * RM * Crel * FTransform(FRotator(0, SceneYaw, 0), SceneAnchor);
	const float Yaw = Now.Rotator().Yaw;
	const float EaseA = 1.f - FMath::SmoothStep(0.f, 0.3f, SceneT);
	const FVector Ease = SceneEaseOff * EaseA;
	SetActorLocationAndRotation(FVector(Now.GetLocation().X + Ease.X, Now.GetLocation().Y + Ease.Y, SceneAnchor.Z), FRotator(0, Yaw + SceneEaseYaw * EaseA, 0));
	SmoothedYaw = Yaw + SceneEaseYaw * EaseA;
	PlayClip(Seq, false, 0.f, SceneStart);
	if (UAnimSingleNodeInstance* SI = Body->GetSingleNodeInstance()) SI->SetPosition(Tc, true);
}

float AAthlete::FallClipTime() const
{
	const float Len = FallClip ? FallClip->GetPlayLength() : 0.f;
	return FMath::Clamp(FallStart + FallT, 0.f, FMath::Max(FallStart, Len - 1.f / 30.f));
}

FVector AAthlete::SceneTravel(const UAnimSequence* Seq, float From, float To, float Yaw) const
{
	if (!Seq) return FVector::ZeroVector;
	const FTransform RM = Seq->ExtractRootMotionFromRange(From, To, FAnimExtractContext(0.0, true));
	const USkeletalMeshComponent* V = Visible();
	FTransform Crel = V ? V->GetComponentTransform().GetRelativeTransform(GetActorTransform()) : FTransform::Identity;
	Crel.SetScale3D(FVector::OneVector);
	const FVector T = (Crel.Inverse() * RM * Crel).GetLocation();
	return FRotator(0, Yaw, 0).RotateVector(FVector(T.X, T.Y, 0.f));
}

FVector AAthlete::FallRootTravel(const UAnimationAsset* Clip, float Start, float Yaw) const
{
	const UAnimSequence* Seq = Cast<UAnimSequence>(Clip);
	if (!Seq) return FVector::ZeroVector;
	// to where he comes to rest on the turf, not to the clip's end: some captures lie still and then get up and walk on
	// (the momentum tackle lies from 1.6 s and walks 2.5 m from 3.5 s), and the ball is spotted where he went down
	static TMap<TPair<const UAnimSequence*, int32>, double> RestAt;
	const double Len = FMath::Max<double>(Start, Seq->GetPlayLength() - 1.0 / 30.0);
	const TPair<const UAnimSequence*, int32> Key(Seq, FMath::RoundToInt(Start * 100.f));
	double End = Len;
	if (const double* Found = RestAt.Find(Key)) End = *Found;
	else
	{
		constexpr double Step = 1.0 / 15.0, Hold = 0.5;
		auto RootAt = [&](double T) { return Seq->ExtractRootMotionFromRange(Start, T, FAnimExtractContext(0.0, true)).GetLocation(); };
		for (double T = Start + 0.4; T + Hold <= Len; T += Step)
			if (FVector::Dist2D(RootAt(T), RootAt(T + Hold)) < 10.0) { End = T; break; }
		RestAt.Add(Key, End);
	}
	const FTransform RM = Seq->ExtractRootMotionFromRange(Start, End, FAnimExtractContext(0.0, true));
	const USkeletalMeshComponent* V = Visible();
	FTransform Crel = V ? V->GetComponentTransform().GetRelativeTransform(GetActorTransform()) : FTransform::Identity;
	Crel.SetScale3D(FVector::OneVector);
	const FVector T = (Crel.Inverse() * RM * Crel).GetLocation();
	return FRotator(0, Yaw, 0).RotateVector(FVector(T.X, T.Y, 0.f));
}

void AAthlete::TickFall(const FVector& SimLoc, float DeltaTime)
{
	FallT += DeltaTime;
	// the pile drives onto the spot early in the fall (most of the momentum is spent by the time they land)
	const float D = FMath::Clamp(FallT / FallDrive, 0.f, 1.f);
	FVector At = FMath::Lerp(AnchorA, AnchorB, 1.f - FMath::Square(1.f - D));
	float Yaw = FallYaw;
	// a library capture: its root carries the body through the fall, its travel and its turn (the placed cuts travel
	// within the pose). The captures are root locked, so the body is drawn as Pc(t) Q(t)^-1 Rest Crel Actor(t) (Pc the
	// capture's pose, Q its root track, Rest the root's rest, Crel the mesh in the actor); for the capture's world to stay
	// put, Actor(t) = Crel^-1 Rest^-1 Q(t) Q(s)^-1 Rest Crel Actor(s), and ExtractRootMotionFromRange(s, t) is exactly
	// Rest^-1 Q(t) Q(s)^-1 Rest. The old code took only the translation (turned by the root's rest) and kept the heading
	// fixed: each man travelled along a direction that depended on his own root heading (the hero root rests turned
	// 180, and the pack's tackles turn a median 155 deg), so the two men of a pair drifted apart at ~1 m/s against the
	// capture's own pelvis-to-pelvis distances (Tools/Art/pair_truth.py, -TagPairLog).
	if (bFallRoot)
		if (const UAnimSequence* Seq = Cast<UAnimSequence>(FallClip.Get()))
		{
			const double End = FallClipTime();
			const FTransform RM = Seq->ExtractRootMotionFromRange(FallStart, End, FAnimExtractContext(0.0, true));
			const USkeletalMeshComponent* V = Visible();
			FTransform Crel = V ? V->GetComponentTransform().GetRelativeTransform(GetActorTransform()) : FTransform::Identity;
			// the capture's own distances, not scaled with this man's body (0.93-1.08): two men of a pair scaled apart
			// travelled their shared root paths by different amounts and the pair drifted into each other (13 cm)
			Crel.SetScale3D(FVector::OneVector);
			const FTransform Now = Crel.Inverse() * RM * Crel * FTransform(FRotator(0, FallYaw, 0), At);
			At = FVector(Now.GetLocation().X, Now.GetLocation().Y, At.Z);
			Yaw = Now.Rotator().Yaw;
		}
	if (bFallEase && FallT < FallEaseT)
	{
		const float E = FMath::SmoothStep(0.f, 1.f, FallT / FallEaseT);
		constexpr float Tau = 0.12f;
		At = FMath::Lerp(FallFrom, At, E) + FallV0 * (Tau * (1.f - FMath::Exp(-FallT / Tau)) * (1.f - E));
		Yaw = FallFromYaw + FMath::FindDeltaAngleDegrees(FallFromYaw, Yaw) * E;
	}
	SetActorLocationAndRotation(At, FRotator(0, Yaw, 0));
	SmoothedYaw = Yaw;
	// the clip's time is set here, the time the root motion above was taken at: left to play on its own the pose ran a
	// frame ahead of the root, and where a capture's root steps (the pose making up for it) that frame showed as a
	// 20-30 cm jump of the head mid-fall. Held one frame short of the end (several captures' last frame snaps 1-4 m).
	PlayClip(FallClip, false, 0.f, FallStart);
	if (UAnimSingleNodeInstance* SI = Body->GetSingleNodeInstance()) SI->SetPosition(FallClipTime(), true);
	if (FallT >= FallLen + FallLie)
	{
		bFalling = false;
		StartGetUp(SimLoc);
	}
}

void AAthlete::BeginSoloFall(const FVector& SimLoc, const FVector& MoveDirW)
{
	// nobody paired him (a knockdown, a pancake without its blocker): fall the way he was going. Backward onto his back
	// when knocked against his facing, forward onto his face otherwise.
	const FAthleteAssets& A = FAthleteAssets::Get();
	const FVector F = GetActorForwardVector();
	const bool bBack = FVector::DotProduct(MoveDirW.GetSafeNormal2D(), F) < -0.3f;
	UAnimationAsset* Clip = bBack ? A.PairC[FAthleteAssets::PairDrive].Get() : A.PairC[FAthleteAssets::PairBehind].Get();
	if (!Clip) return;
	const FVector Here = GetActorLocation();
	const FVector Travel = F * (bBack ? -100.f : 100.f);
	const FVector Want = FVector(SimLoc.X, SimLoc.Y, Here.Z) - Travel;
	BeginPairedFall(Clip, Here, Here + (Want - Here).GetClampedToMaxSize2D(150.f), GetActorRotation().Yaw, 0.35f, 0.7f, false);
}

void AAthlete::SetCivilian(ECivilian Who, int32 Index)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (A.bHero && Who != ECivilian::Generic)
	{
		const int32 W = A.HeroWeather; // 0 warm, 1 cold, 2 rain
		const uint32 H = static_cast<uint32>(Index + 1) * 2654435761u;
		auto Pick = [&](const TCHAR* Kind, TFunctionRef<bool(const FHeroItem&)> Pred) -> int32
		{
			TArray<int32> L = A.HeroOf(Kind, Pred);
			if (!L.Num()) L = A.HeroOf(Kind);
			return L.Num() ? L[(H >> 7) % L.Num()] : INDEX_NONE;
		};
		const TCHAR* Weather = W == 2 ? TEXT("rain") : W == 1 ? TEXT("cold") : TEXT("warm");
		USkeletalMesh* BodyM = nullptr;
		int32 HeadI = INDEX_NONE;
		int32 BodyI = INDEX_NONE;
		FString Rig = TEXT("hero");
		if (Who == ECivilian::Coach)
		{
			// every coach character of the pack is in the rotation: a named coach in his own warm / cold clothes, or a
			// coach body (coach-referee polo / windbreaker / rain jacket, create-a-coach outfits) with one of the heads
			const FString Wx = W == 0 ? TEXT("warm") : TEXT("cold");
			const FString Top = W == 2 ? TEXT("rainjacket") : W == 1 ? TEXT("windbreaker") : TEXT("poloshirt");
			if ((H >> 3) % 2 == 0)
			{
				BodyI = Pick(TEXT("CoachFull"), [&](const FHeroItem& I) { return I.Weather == Wx; });
			}
			if (BodyI == INDEX_NONE)
			{
				BodyI = Pick(TEXT("Coach"), [&](const FHeroItem& I) { return I.Top == Top || (I.Top.IsEmpty() && I.Weather == Wx); });
				HeadI = Pick(TEXT("CoachHead"), [](const FHeroItem& I) { return !I.bNoHead; });
			}
			BodyM = A.HeroMesh(BodyI);
		}
		else if (Who == ECivilian::Official)
		{
			BodyM = A.HeroMesh(Pick(TEXT("Ref"), [&](const FHeroItem& I) { return I.Name.EndsWith(W == 0 ? TEXT("warm") : TEXT("cold")); }));
			HeadI = Pick(TEXT("RefHead"), [](const FHeroItem&) { return true; });
		}
		else
		{
			// crowd rig people: the chain crew carry their own rods and down box
			const TCHAR* Part = Who == ECivilian::Crew ? (Index >= 2 ? TEXT("chaingangdownmarker") : TEXT("chaingangscrimage"))
				: Who == ECivilian::Assistant ? (Index % 2 ? TEXT("asstcoachnegative") : TEXT("asstcoachpositive"))
				: (Index % 2 ? TEXT("teamtrainernegative") : TEXT("teamtrainerpositive"));
			int32 I = Pick(TEXT("Crew"), [&](const FHeroItem& It) { return It.Name.Contains(Part) && It.Name.Contains(Weather); });
			if (I == INDEX_NONE || !A.HeroItems[I].Name.Contains(Part)) I = Pick(TEXT("Crew"), [&](const FHeroItem& It) { return It.Name.Contains(Part); });
			BodyM = A.HeroMesh(I);
			Rig = TEXT("crowd");
		}
		if (BodyM)
		{
			// arms in the head's skin tone (coaches' and officials' arms use the players' tone textures)
			HeroTone = HeadI != INDEX_NONE ? A.HeroItems[HeadI].Tone : BodyI != INDEX_NONE ? A.HeroItems[BodyI].Tone : 4;
			DressHero(BodyM, A.HeroMesh(HeadI), nullptr, Rig);
			// the referee's white cap: the pack's official's cap (black, on the head mesh) repainted for the crew chief
			bCapTint = Who == ECivilian::Official && Index == 0;
			// his patch: the referee, the umpire, the down and line judges, the side, field and back judges (the crew in
			// the director's order) on the pack's strip
			static const int32 PatchOf[7] = {2, 3, 0, 1, 5, 6, 4};
			OfficialPatch = Who == ECivilian::Official && Index >= 0 && Index < 7 ? PatchOf[Index] : -1;
			CapTint = FLinearColor(0.8f, 0.8f, 0.8f);
			PaintHero(CurUniform);
		}
	}
	for (UStaticMeshComponent* C : {Helmet.Get(), Facemask.Get(), Pads.Get()})
		if (C) C->SetVisibility(false);
	for (UStaticMeshComponent* C : Gear)
	{
		if (!C) continue;
		const FString S = C->GetAttachSocketName().ToString();
		if (!S.Contains(TEXT("foot")) && !S.Contains(TEXT("ball"))) C->SetVisibility(false); // keep the shoes
	}
	for (UTextRenderComponent* T : {BackNumber.Get(), FrontNumber.Get()})
		if (T) T->SetVisibility(false);
}

void AAthlete::SidelineCheer(bool bCheer)
{
	const FAthleteAssets& A = FAthleteAssets::Get();
	const int32 V = SidelineVariant;
	const bool bMirror = (V / 3) % 2 == 1;
	UAnimationAsset* Clip = nullptr;
	if (bCheer && A.Celebrations.Num())
		Clip = A.Side(A.Celebrations[V % A.Celebrations.Num()], bMirror);
	else
		Clip = (V % 3 == 1 && A.IdleHandsHips) ? A.Side(A.IdleHandsHips, bMirror) : A.Idle.Get(); // standing idles, a few hands on hips
	if (!Clip) return;
	// each bench player on his own beat: random phase, a little tempo spread (the cheer starts staggered too)
	const float Phase = FMath::Frac(V * 0.618f) * Clip->GetPlayLength();
	PlayClip(Clip, true, 0.9f + 0.2f * FMath::Frac(V * 0.377f), bCheer ? FMath::Frac(V * 0.29f) * 0.6f : Phase);
}

bool AAthlete::CaptureReplayPose(FTransform& OutWorld, TArray<FTransform>& OutLocal) const
{
	if (!MHBody) return false;
	OutWorld = MHBody->GetComponentTransform();
	OutLocal = MHBody->GetBoneSpaceTransforms();
	return OutLocal.Num() > 0;
}

void AAthlete::ShowReplayPose(const FTransform& World, const TArray<FTransform>& Local)
{
	if (!MHBody) return;
	UTagRetargetAnimInstance* R = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance());
	if (!R) return;
	if (!bReplaying)
	{
		bReplaying = true;
		bReplayHold = false;
		ReplayRel = MHBody->GetRelativeTransform();
		if (MHBody->IsSimulatingPhysics())
		{
			MHBody->SetSimulatePhysics(false); // a downed player's ragdoll is replaced by the recording
			MHBody->SetAllBodiesSimulatePhysics(false);
		}
	}
	MHBody->SetWorldTransform(World);
	R->ReplayMesh = Local;
	R->bReplay = true;
}

void AAthlete::EndReplay()
{
	if (!bReplaying || !MHBody) return;
	bReplaying = false;
	UTagRetargetAnimInstance* R = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance());
	if (bRagdoll)
	{
		bReplayHold = true; // stays down in the replay's last pose; the get-up blends out of it
		return;
	}
	MHBody->SetRelativeTransform(ReplayRel);
	if (R)
	{
		R->bReplay = false;
		R->SnapshotMesh = R->ReplayMesh;
		R->SnapshotBlend = 0.3f;
		++R->SnapshotSerial;
	}
}

void AAthlete::ClearReplayHold()
{
	if (!bReplayHold || !MHBody) return;
	bReplayHold = false;
	if (UTagRetargetAnimInstance* R = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance())) R->bReplay = false;
}

void AAthlete::ResetPose()
{
	ClearReplayHold();
	bReplaying = false;
	VisOffset = FVector::ZeroVector;
	bGettingUp = false;
	bStoodUp = false;
	TackledLeft = 0.f;
	bHitApplied = false;
	bMissing = false;
	TackleOn = nullptr;
	BlockPartner = nullptr;
	PunchedPartner = nullptr;
	PunchedThisPlay.Reset();
	SincePunch = 99.f;
	LineLoop = 0;
	SmoothedPush = 0.f;
	BlockAlign = FVector::ZeroVector;
	AlignT = -1.f;
	DownTime = 0.f;
	// new play: the players are teleported to the formation, so the motion history starts over
	TrajPos.Reset();
	TrajRot.Reset();
	TrajTime.Reset();
	bHavePrevVel = false;
	SmoothedAccel = FVector::ZeroVector;
	LeanSmoothed = FVector::ZeroVector;
	bDropped = false;
	DropLeft = 0.f;
	// a fall still running (a long library capture with its lie) ends here too: otherwise TickFall keeps the actor at
	// the last play's pile, root motion and all, and the next play starts from wherever he lies
	if (bFalling || FallHoldLeft > 0.f)
	{
		bFalling = false;
		bFallRoot = false;
		FallClip = nullptr;
		FallHoldLeft = 0.f;
		CurrentClip = nullptr;
		CurrentAnim.Reset();
	}
	if (!bRagdoll) return;
	bRagdoll = false;
	FaceFollowBody(false);
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

static double GSlideRaw = 0.0, GSlideShown = 0.0;
static double GFastSec = 0.0, GFastM = 0.0;
static int32 GFastSteps = 0;
static double GFastHips = 0.0, GFastHead = 0.0;

static double GGazeSum = 0.0, GGazeMin = 90.0;
static int32 GGazeN = 0;

void AAthlete::GazeStats(double& OutMeanDeg, double& OutMinDeg, int32& OutSamples, bool bReset)
{
	OutSamples = GGazeN;
	OutMeanDeg = GGazeN ? GGazeSum / GGazeN : 0.0;
	OutMinDeg = GGazeN ? GGazeMin : 0.0;
	if (bReset) { GGazeSum = 0.0; GGazeMin = 90.0; GGazeN = 0; }
}

void AAthlete::HeightStats(double& OutHipsM, double& OutHeadM, bool bReset)
{
	OutHipsM = GFastHips;
	OutHeadM = GFastHead;
	if (bReset) GFastHips = GFastHead = 0.0;
}

void AAthlete::GaitStats(double& OutSeconds, double& OutMetres, int32& OutSteps, bool bReset)
{
	OutSeconds = GFastSec;
	OutMetres = GFastM;
	OutSteps = GFastSteps;
	if (bReset) { GFastSec = GFastM = 0.0; GFastSteps = 0; }
}

void AAthlete::SlideStats(double& OutRaw, double& OutShown, bool bReset)
{
	OutRaw = GSlideRaw;
	OutShown = GSlideShown;
	if (bReset) GSlideRaw = GSlideShown = 0.0;
}

void AAthlete::MeasureMotion(const FVector& SimPos, float Dt)
{
	// velocity and acceleration of the man's spot in the sim (smoothed): the lean, and motion matching's query. (Not of
	// the body: the body follows the sim on its own feet, and its lag must not feed back into what it is asked to do.)
	const FVector Pos = SimPos;
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
		PrevVel = LastVelW;
		SmoothedAccel = FVector::ZeroVector;
	}
	PrevPos = Pos;
	bHavePrevVel = true;
}

void AAthlete::Drive(const FVector& Location, float Yaw, const FVector& VelWorld, float VMaxYds, const FString& Anim, float AnimT, float DeltaTime, bool bLineman)
{
	FAthleteAssets& A = FAthleteAssets::Get();
	if (!PendingCarry.IsNearlyZero(1.f))
	{
		// a capture's travel handed to the actor last frame (the layout dive): his body is there, the offset to the sim
		// spot eases out as after any capture
		SetActorLocation(GetActorLocation() + FVector(PendingCarry.X, PendingCarry.Y, 0.f));
		VisOffset += FVector(PendingCarry.X, PendingCarry.Y, 0.f);
		PendingCarry = FVector::ZeroVector;
	}
	const FVector PrevActorLoc = GetActorLocation();
	const FVector SimDelta = Location - LastSimLoc;
	const bool bFollowed = bFollowActive && SimDelta.SizeSquared2D() < 150.f * 150.f; // (a teleport is not followed)
	bFollowActive = false;
	LastSimLoc = Location;
	if (!bFollowed) ClipStrideSm = 1.f; // (a newly followed clip starts from its own stride)
	UTagRetargetAnimInstance* RI = MHBody ? Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance()) : nullptr;
	if (RI)
	{
		LastSlideRaw = RI->SlideRaw;
		LastSlideShown = RI->SlideShown;
		for (int32 k = 0; k < 7; ++k) LastDev[k] = RI->Dev[k];
		LastPlantFeet = RI->PlantFeet;
		// the clip's root travel for the layer's planted-foot verdict: zero (a clip played in place) unless motion matching
		// reports its own below; unknown while the body is driven some other way (falls, get-ups)
		RI->Proc.RootVelCS = FVector::ZeroVector;
		RI->Proc.bRootVelValid = false;
		TelEff = 0.f; TelStride = 1.f; TelAng = 0.f; TelOrient = 0.f; TelRootOff = 0.f;
		GSlideRaw += RI->SlideRaw; // foot slide over all athletes (SlideStats)
		GSlideShown += RI->SlideShown;
		RI->SlideRaw = RI->SlideShown = 0.f;
		if (FVector(VelWorld.X, VelWorld.Y, 0.f).Size() > 800.f) // sprinting: cadence and step length (GaitStats)
		{
			GFastSec += DeltaTime;
			GFastM += FVector(VelWorld.X, VelWorld.Y, 0.f).Size() * DeltaTime / 100.0;
			GFastSteps += RI->Touchdowns;
			const float Ground = GetActorLocation().Z;
			GFastHips += (MHBody->GetSocketLocation(TEXT("pelvis")).Z - Ground) / 100.0 * DeltaTime;
			GFastHead += (MHBody->GetSocketLocation(TEXT("head")).Z - Ground) / 100.0 * DeltaTime;
		}
		RI->Touchdowns = 0;
		// body physics are off while the body is down: falls, get-ups and ragdolls own the pose (set again below)
		RI->Proc.bFootLock = false;
		RI->Proc.LeanCS = FVector::ZeroVector;
		RI->Proc.StrideScale = 1.f; // motion matching sets it when the man outruns the capture
		OrientWarpSm = FMath::FInterpTo(OrientWarpSm, 0.f, DeltaTime, 4.f); // and the orientation warp while he moves
		RI->Proc.OrientWarpDeg = OrientWarpSm;
	}
	if (bPreSnap)
	{
		bDropped = false; DropLeft = 0.f; // a new play: the quarterback has his drop again
		// and nothing left over from the last one: a one-shot started as the whistle blew (a receiver laying out for an
		// incompletion) never got to play, waited through the huddle and the stance and went off at the next snap - a
		// wide receiver diving out of his stance on a run play, then a 3.3 m jump back (p14-p19, clip 3)
		OneShot = nullptr;
		OneShotLeft = 0.f;
		bMissing = false;
	}
	if (Anim != TEXT("down") && FVector(VelWorld.X, VelWorld.Y, 0.f).SizeSquared() > 100.f) LastVelW = FVector(VelWorld.X, VelWorld.Y, 0.f);
	if (bRagdoll)
	{
		// physics owns the body; after a beat the player gets up with a keyed get-up out of the ragdoll pose
		DownTime += DeltaTime;
		if (DownTime > 0.6f && A.GetUpDown && A.GetUpUp) StartGetUp(Location); // ragdoll settled: get up before the next play resets him
		if (bRagdoll) return;
	}
	if (bFalling)
	{
		TickFall(Location, DeltaTime);
		return;
	}
	if (bScene)
	{
		if (Anim == TEXT("scene"))
		{
			TickScene(DeltaTime);
			return;
		}
		// someone else drives him again: from where the capture left his body (the offset eases out below)
		bScene = false;
		VisOffset = (GetActorLocation() - Location).GetClampedToMaxSize2D(300.f);
		VisOffset.Z = 0.f;
		CurrentAnim.Reset();
	}
	if (FallHoldLeft > 0.f)
	{
		// down, held where the hit landed while the director pairs the tackle (same frame, normally); a keyed solo
		// fall if nobody claims him
		FallHoldLeft -= DeltaTime;
		SetActorLocation(Location + VisOffset);
		if (FallHoldLeft <= 0.f) BeginSoloFall(Location, PileVel);
		return;
	}
	if (TackledLeft > 0.f)
	{
		TackledLeft -= DeltaTime;
		SetActorLocation(Location + VisOffset);
		if (TackledLeft <= 0.f) BeginSoloFall(Location, PileVel);
		return;
	}
	if (bGettingUp)
	{
		GetUpLeft -= DeltaTime;
		if (bInjuredChain)
		{
			InjuredLeft -= DeltaTime;
			if (InjuredLeft <= 0.f && A.GetUpSlow)
			{
				bInjuredChain = false;
				PlayClip(A.Side(A.GetUpSlow, bInjuredMirror), false); // the injury beat ends in the slow get-up's first pose
			}
		}
		SetActorLocation(Location + VisOffset);
		if (GetUpLeft > 0.f) return;
		bGettingUp = false;
		CurrentAnim.Reset();
	}
	else if (bFollowed)
	{
		// on his own feet: the body moved by what the clip's feet covered, the sim by what it did; the difference is the
		// lag (or lead) he carries. Past FollowMax the sim pulls him along (and only then a foot slides).
		static float FollowMax = FParse::Value(FCommandLine::Get(), TEXT("TagFollowMax="), FollowMax) ? FollowMax : 60.f;
		const float Was = static_cast<float>(VisOffset.Size2D());
		VisOffset += FollowVel * DeltaTime - FVector(SimDelta.X, SimDelta.Y, 0.f);
		VisOffset.Z = 0.f;
		// (an offset already past the limit when the following starts - a dive's travel, a get-up - comes in at 1.5 m/s,
		// not in one frame)
		const float Max = FMath::Max(FollowMax, Was - 150.f * DeltaTime);
		if (VisOffset.SizeSquared2D() > Max * Max) VisOffset = VisOffset.GetClampedToMaxSize2D(Max);
	}
	else
	{
		// drift back onto the sim position (after a get-up, a dive's travel): all but the last few centimetres (a man
		// standing 8 cm off his spot is nobody's concern; easing it out slid his planted feet)
		// (a man standing still keeps up to 25 cm: nobody lines up on a grid, and the walk-in ends where his feet do)
		const float L = VisOffset.Size2D();
		const float Keep = DeltaTime > 1e-4f && SimDelta.Size2D() / DeltaTime < 40.f ? 25.f : 8.f;
		if (L > Keep) VisOffset *= FMath::Max(Keep / L, FMath::Exp(-2.5f * DeltaTime));
	}
	// two locked-up men keep their bodies apart whatever each one's lag behind the sim: with both up to 60 cm off their
	// spots toward each other, pairs the sim holds 90 cm apart stood inside one another (pelvises under 30 cm in 1.8 %
	// of live frames, p23, against 0.3 % before the follower). Each gives back half of what is missing to 95 cm.
	if (const AAthlete* Bp = BlockPartner.Get())
	{
		FVector Apart = Location + VisOffset - Bp->GetActorLocation();
		Apart.Z = 0.f;
		const float Gap = static_cast<float>(Apart.Size());
		if (Gap > 1.f && Gap < 95.f) VisOffset += Apart / Gap * FMath::Min((95.f - Gap) * 0.5f, 400.f * DeltaTime);
	}
	const bool bFall = Anim == TEXT("down") || Anim == TEXT("tackle") || Anim == TEXT("dive");
	bRepClip = false; // (set again below while his part of a two-man capture plays)
	SetActorLocation(Location + VisOffset);
	RecordTrajectory();
	MeasureMotion(Location, DeltaTime);
	if (RI)
	{
		// body physics: the lean from the ankles into the measured acceleration (tan a = a / g: a 5 m/s2 start is
		// ~27 deg; capped and scaled down since the capture's starts, plants and stops carry some of their own),
		// and planted feet pinned to the turf. -TagNoLean / -TagNoFootLock for A/B.
		static const bool bNoLean = FParse::Param(FCommandLine::Get(), TEXT("TagNoLean"));
		static const bool bNoFootLock = FParse::Param(FCommandLine::Get(), TEXT("TagNoFootLock"));
		FVector Want = FVector::ZeroVector;
		FVector Ah(SmoothedAccel.X, SmoothedAccel.Y, 0.f);
		const float Mag = Ah.Size();
		if (!bNoLean && !bPreSnap && Mag > 40.f)
		{
			// the capture's starts, plants and breaks already lean ~20 deg of their own, so this adds the part the clip
			// can't know: a little over a third of the physical angle, at most 12 deg (a 5 m/s2 start adds ~10)
			float Deg = FMath::RadiansToDegrees(FMath::Atan(FMath::Min(Mag, 700.f) / 981.f)) * 0.38f;
			Deg = FMath::Min(Deg, 12.f);
			if (OneShot && OneShotLeft > 0.f) Deg *= 0.5f; // a keyed catch / move has its own body
			Want = MHBody->GetComponentTransform().InverseTransformVectorNoScale(Ah / Mag) * Deg;
		}
		LeanSmoothed = FMath::VInterpTo(LeanSmoothed, Want, DeltaTime, 7.f);
		RI->Proc.LeanCS = LeanSmoothed;
		RI->Proc.bFootLock = !bNoFootLock;
		// a plain clip: in place (root velocity zero) unless it is a travelling capture played with its root locked (a
		// drop, a locomotion loop): then its own root travel at the play position; a blend space: unknown
		RI->Proc.bRootVelValid = true;
		if (!bInMatch)
			if (const UAnimSingleNodeInstance* SN = Body->GetSingleNodeInstance())
			{
				const UAnimationAsset* Cur = const_cast<UAnimSingleNodeInstance*>(SN)->GetCurrentAsset();
				if (const UAnimSequence* Seq = Cast<UAnimSequence>(Cur))
				{
					if (Seq->HasRootMotion())
					{
						const double Len = Seq->GetPlayLength(), T = const_cast<UAnimSingleNodeInstance*>(SN)->GetCurrentTime();
						const double S = FMath::Clamp(T - 0.05, 0.0, Len), E = FMath::Clamp(T + 0.05, 0.0, Len);
						if (E > S + 1e-3)
							RI->Proc.RootVelCS = Seq->ExtractRootMotionFromRange(S, E, FAnimExtractContext(0.0, true)).GetTranslation() / (E - S) *
								const_cast<UAnimSingleNodeInstance*>(SN)->GetPlayRate();
						// and the body travels with it (the follower): a lineman's set, a first step off the ball, a drop, a
						// juke are captures of men covering ground; played in place under the sim's own path their planted feet
						// skated 1-2.7 m/s (p17: one-shots 109 cm/s on the feet the capture holds planted, against 7 for
						// motion matched running). The body now takes the capture's steps; where that leaves it off the sim's
						// spot (at most FollowMax) the locomotion that follows makes up.
						static const bool bFollowClips = !FParse::Param(FCommandLine::Get(), TEXT("TagNoFollow"));
						if (bFollowClips && !bPreSnap)
						{
							FVector ClipVelW = MHBody->GetComponentTransform().TransformVectorNoScale(RI->Proc.RootVelCS);
							ClipVelW.Z = 0.f;
							ClipVelW = ClipVelW.GetClampedToMaxSize(1100.f);
							// steered at the sim like a matched run: the capture's steps stretched or shortened (0.55-1.4) and turned
							// (up to 40 degrees) toward what the sim does, plus making up the lag. (Unsteered, a guard's angled set
							// travelled back-right at the capture's 45 degrees while the sim took him straight back: 60 cm off in
							// a third of a second, then dragged - p18, a twentieth of all frames at the limit.)
							FVector Ask = PrevVel;
							const float Lag = VisOffset.Size2D();
							if (Lag > 8.f) Ask -= VisOffset.GetSafeNormal2D() * FMath::Min((Lag - 8.f) * 2.5f, 200.f);
							// a locked-up man also steps to hand-fighting distance of his man (the steps keep the spacing; the
							// alignment offset below would slide him there under his planted feet)
							if (const AAthlete* Bp = BlockPartner.Get())
							{
								FVector ToHim = Bp->GetActorLocation() - GetActorLocation();
								ToHim.Z = 0.f;
								const float Dh = static_cast<float>(ToHim.Size());
								if (Dh > 1.f && Dh < 260.f && FMath::Abs(Dh - 110.f) > 10.f) Ask += ToHim / Dh * FMath::Clamp((Dh - 110.f) * 2.5f, -120.f, 120.f);
							}
							const float Eff = static_cast<float>(ClipVelW.Size()), WantSp = static_cast<float>(Ask.Size2D());
							float Sc = 1.f, WarpWant = 0.f;
							if (Eff > 60.f)
							{
								Sc = FMath::Clamp(WantSp / Eff, 0.55f, 1.4f);
								if (WantSp > 60.f)
								{
									const FVector C2 = ClipVelW / Eff, W2 = Ask.GetSafeNormal2D();
									WarpWant = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(C2.X * W2.Y - C2.Y * W2.X, C2.X * W2.X + C2.Y * W2.Y)), -40.f, 40.f);
								}
							}
							OrientWarpSm = FMath::FInterpTo(OrientWarpSm, WarpWant, DeltaTime, 6.f);
							RI->Proc.OrientWarpDeg = OrientWarpSm;
							// (eased: a capture's root speed steps with its feet, and the raw ratio flipped between the limits
							// from one frame to the next - the feet of a centre's first step jumped 65 cm, p19 clip 34)
							ClipStrideSm = FMath::FInterpTo(ClipStrideSm, Sc, DeltaTime, 6.f);
							Sc = ClipStrideSm;
							RI->Proc.StrideScale = Sc;
							const FVector FeetW = ClipVelW.RotateAngleAxis(OrientWarpSm, FVector::UpVector);
							if (Eff > 1.f) RI->Proc.StrideDirCS = MHBody->GetComponentTransform().InverseTransformVectorNoScale(FeetW / Eff);
							TelEff = Eff; TelStride = Sc; TelOrient = OrientWarpSm;
							FollowVel = FeetW * Sc;
							bFollowActive = true;
						}
					}
				}
				else if (Cur) RI->Proc.bRootVelValid = false;
			}
	}
	if (!bYawInit)
	{
		SmoothedYaw = Yaw;
		bYawInit = true;
	}
	// (and no faster than a man can turn: 600 deg/s. A blocker meeting his man on the run was swung 60 degrees in one
	// frame, his feet 63 cm across the turf - the one body pop left in a filmed game)
	SmoothedYaw += FMath::Clamp(FMath::FindDeltaAngleDegrees(SmoothedYaw, Yaw) * FMath::Clamp(DeltaTime * 10.f, 0.f, 1.f), -600.f * DeltaTime, 600.f * DeltaTime);
	SetActorRotation(FRotator(0, SmoothedYaw, 0));

	const float SpeedCm = FVector(VelWorld.X, VelWorld.Y, 0).Size();
	const bool bChanged = Anim != CurrentAnim;
	// still: told to stand, or not actually moving (a man held off his post-play spot by a body in the way was given a
	// small velocity every frame he never moved on, and stood on the one matched idle instead of his own stand - p69 c005)
	const float MovedCm = DeltaTime > 1e-4f ? static_cast<float>(FVector::Dist2D(PrevActorLoc, StillRef)) / DeltaTime : 0.f;
	StillRef = PrevActorLoc;
	// (only a nudge counts: a man told to run who has not moved yet is starting, and held in a stand that does not move
	// the body he never did - the planted feet slid 1.1 cm/s against 0.7, p70)
	StillTime = (SpeedCm < 20.f || (SpeedCm < 70.f && MovedCm < 20.f)) ? StillTime + DeltaTime : 0.f;
	CurrentAnim = Anim;

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
			if (bPancakePending) // knocked over backward by the blocker's drive, not carried by a pile
			{
				PileVel = PancakeDir * 170.f - FVector(0, 0, 40);
				bPancakePending = false;
			}
			bHitApplied = true; // the paired fall plays the hit
			FallHoldLeft = 0.12f;
			UE_LOG(LogTemp, Verbose, TEXT("TAG tackled #%d held %.0f cm behind the sim spot"), Player ? Player->number : -1, Jump);
			return;
		}
		BeginSoloFall(Location, FVector(VelWorld.X, VelWorld.Y, 0.f));
		return;
	}
	// hand keyed contact and evasion clips (in place; the sim moves the actor)
	if (Anim == TEXT("tackle") && A.TackleKeyed && !bMissing)
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
			const int32 Seq = GTackleSeq++;
			UAnimationAsset* T = A.Side(Pool[Seq % 6], (Seq / 6 + (Player ? Player->id : 0)) % 2 == 1); // wrap from either side
			float Start = 0.f;
			// a library tackler by default (the placed six are one entry among them), from 6 frames before his arms close
			if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(A.PackTacklers, Seq)) { T = P->Anim; Start = P->Start; }
			PlayClip(T ? T : A.TackleKeyed.Get(), false, OneShotRate, Start);
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
				const FVector Off = FVector(Want.X, Want.Y, 0.f).GetClampedToMaxSize(250.f); // the pairing window is 3 yd
				VisOffset = FMath::Lerp(VisOffset, Off, 1.f - FMath::Exp(-14.f * DeltaTime));
				SetActorLocation(Location + VisOffset);
				SmoothedYaw += FMath::FindDeltaAngleDegrees(SmoothedYaw, To.Rotation().Yaw) * FMath::Clamp(DeltaTime * 14.f, 0.f, 1.f);
				SetActorRotation(FRotator(0, SmoothedYaw, 0));
			}
		}
		// pileup: once the wrap clip has played out on a carrier who went down, the tackler goes down with him (a
		// ragdoll driven onto the carrier; ragdolls collide, so gang tacklers land on and around him) instead of
		// freezing upright in the clip's last pose until the next play
		return;
	}
	if (Anim == TEXT("dive") && A.TackleDive)
	{
		if (bChanged) PlayClip(A.TackleDive, false);
		return;
	}
	// a ball carrier's move: "juke" (no move named: picked from the runner, as before the highlight stick) or the sim's
	// own move, "mv_<juke|spin|truck|stiffarm|hurdle>_<l|r>" (the side he cuts to; a stiff arm's arm)
	const bool bNamedMove = Anim.StartsWith(TEXT("mv_"));
	if ((Anim == TEXT("juke") || bNamedMove) && A.JukeL && A.JukeR)
	{
		// cut direction from the carrier's lateral velocity relative to his facing (a named move says it)
		float Side = FVector::DotProduct(FVector(VelWorld.X, VelWorld.Y, 0), GetActorRightVector());
		if (bNamedMove) Side = Anim.EndsWith(TEXT("_r")) ? 1.f : -1.f;
		if (bChanged)
		{
			// the move fits the runner: elusive backs juke and spin, power backs lower the shoulder or stiff arm,
			// the most agile occasionally hurdle; a game-wide counter keeps consecutive moves from repeating
			static int32 GMoveSeq = 0;
			const int32 Seq = GMoveSeq++;
			UAnimationAsset* Move = Side > 0.f ? A.JukeR.Get() : A.JukeL.Get();
			if (bNamedMove)
			{
				// the highlight stick: exactly the move the sim made
				if (Anim.Contains(TEXT("_spin"))) Move = Side > 0.f ? A.SpinR.Get() : A.SpinL.Get();
				else if (Anim.Contains(TEXT("_truck"))) Move = A.Side(A.Truck, Side > 0.f);
				else if (Anim.Contains(TEXT("_stiffarm"))) Move = Side > 0.f ? A.StiffArmR.Get() : A.StiffArmL.Get();
				else if (Anim.Contains(TEXT("_hurdle"))) Move = A.Side(A.Hurdle, Side > 0.f);
				if (!Move) Move = Side > 0.f ? A.JukeR.Get() : A.JukeL.Get();
			}
			if (Player)
			{
				const tag::Ratings& R = Player->r;
				if (bNamedMove) {}
				else if (A.Hurdle && R.agility > 85 && Seq % 7 == 3) Move = A.Side(A.Hurdle, Side > 0.f);
				else if (R.breakTackle > R.elusiveness + 5)
					Move = (Seq % 2) ? A.Side(A.Truck, Side > 0.f) : (Side > 0.f ? A.StiffArmR.Get() : A.StiffArmL.Get());
				else if (Seq % 3 == 1)
					Move = Side > 0.f ? A.SpinR.Get() : A.SpinL.Get();
				if (!Move) Move = Side > 0.f ? A.JukeR.Get() : A.JukeL.Get();
				// every other move a library capture of the same move to the same side, from 5 frames before its first step
				{
					const bool bR = Side > 0.f;
					const TArray<FAthleteAssets::FPackClip>* Lib =
						(Move == A.Hurdle || Move == A.Side(A.Hurdle, true)) ? &A.PackHurdles :
						(Move == A.Truck || Move == A.Side(A.Truck, true)) ? &A.PackTrucks :
						(Move == A.StiffArmR || Move == A.StiffArmL) ? (bR ? &A.PackStiffArmsR : &A.PackStiffArmsL) :
						(Move == A.SpinR || Move == A.SpinL) ? (bR ? &A.PackSpinsR : &A.PackSpinsL) : (bR ? &A.PackJukesR : &A.PackJukesL);
					if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(*Lib, Seq)) { PlayClip(P->Anim, false, 1.f, P->Start); return; }
				}
			}
			PlayClip(Move, false);
		}
		return;
	}
	// kicks (the director starts them so foot contact meets the ball leaving; AnimT = start position in the clip)
	if (Anim == TEXT("kickoff") || Anim == TEXT("punt") || Anim == TEXT("fgkick"))
	{
		UAnimationAsset* K = Anim == TEXT("kickoff") ? A.KickKickoff.Get() : Anim == TEXT("punt") ? A.KickPunt.Get() : A.KickFieldGoal.Get();
		// the kicker's own library capture (every other kicker), its contact moved onto the placed clip's contact
		const TArray<FAthleteAssets::FPackClip>& Lib = Anim == TEXT("kickoff") ? A.PackKickoffs : Anim == TEXT("punt") ? A.PackPunts : A.PackFieldGoals;
		const float Contact = Anim == TEXT("kickoff") ? FAthleteAssets::KickoffContact : Anim == TEXT("punt") ? FAthleteAssets::PuntContact : FAthleteAssets::FieldGoalContact;
		float Shift = 0.f;
		if (Player)
			if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(Lib, Player->id)) { K = P->Anim; Shift = P->Start - Contact; }
		if (K)
		{
			if (bChanged) PlayClip(K, false, 1.f, FMath::Max(0.f, AnimT + Shift));
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
	if (Anim == TEXT("throw") && IsThrowing()) return; // (the throw timed to the sim's release is playing: no restart over it)
	if (Anim == TEXT("throw"))
	{
		// the set throw from 7 frames before its release: plant, hip-shoulder separation, release ~0.23 s after the sim's
		if (A.ThrowKeyed) { if (bChanged) PlayClip(A.ThrowKeyed, false, 1.f, FAthleteAssets::ThrowKeyedStart); }
		else if (bChanged) PlayClip(A.Throw, false, 1.6f);
		return;
	}
	const bool bLine = Player && (Player->pos == tag::Pos::OT || Player->pos == tag::Pos::OG || Player->pos == tag::Pos::C ||
		Player->pos == tag::Pos::DE || Player->pos == tag::Pos::DT);
	// the snap: linemen explode out of the stance (get-off), the shotgun QB catches the snap; both are one-shots
	// a lineman's first step: the capture that goes his way. The pools hold sets and steps to every side (a vertical set,
	// angled sets left and right, steps down the line); taken in turn, a guard the sim sends straight back played a set
	// angled 45 degrees to his right. The pick waits up to three frames (a lineman's reaction to the snap is longer) for
	// the sim to steer him, then takes, of eight candidates in turn, the one whose first 0.7 s travels nearest that way.
	if (bWasPreSnap && !bPreSnap && bLine && A.GetOff) GetOffPending = 3;
	if (bPreSnap) GetOffPending = 0;
	if (GetOffPending > 0 && !bPreSnap)
	{
		const bool bSteered = IntentFrame == GFrameCounter && IntentVelW.SizeSquared2D() > 50.f * 50.f;
		if (bSteered || --GetOffPending == 0)
		{
			GetOffPending = 0;
			static int32 GGetOffSeq = 0;
			// the pool for this man's job at the snap; the shared pool only when the job's pool is empty
			const TArray<FAthleteAssets::FPackClip>& Pool = GetOffKind == 1 && A.PackGetOffsPass.Num() ? A.PackGetOffsPass
				: GetOffKind == 2 && A.PackGetOffsRun.Num() ? A.PackGetOffsRun : GetOffKind == 3 && A.PackGetOffsDL.Num() ? A.PackGetOffsDL : A.PackGetOffs;
			const int32 Seq = GGetOffSeq++;
			const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(Pool, Seq);
			if (bSteered && MHBody && Pool.Num() > 1)
			{
				const FVector WantCS = MHBody->GetComponentTransform().InverseTransformVectorNoScale(IntentVelW.GetSafeNormal2D());
				float BestDot = -2.f;
				for (int32 k = 0; k < 8; ++k)
				{
					const FAthleteAssets::FPackClip* C = FAthleteAssets::Pick(Pool, Seq + k);
					const UAnimSequence* Sq = C ? Cast<UAnimSequence>(C->Anim) : nullptr;
					if (!Sq || !Sq->HasRootMotion()) continue;
					FVector T = Sq->ExtractRootMotionFromRange(C->Start, FMath::Min(C->Start + 0.7f, Sq->GetPlayLength()), FAnimExtractContext(0.0, true)).GetTranslation();
					T.Z = 0.f;
					const float Dot = T.Size() > 12.f ? static_cast<float>(FVector::DotProduct(T.GetSafeNormal(), WantCS)) : 0.f; // (a set that stays put fits any way a little)
					if (Dot > BestDot) { BestDot = Dot; P = C; }
				}
			}
			if (P) PlayOneShotFrom(P->Anim, P->Start);
			else PlayOneShot(A.GetOff);
		}
	}
	if (bWasPreSnap && !bPreSnap)
	{
		if (bLine && A.GetOff) {}
		else if (Player && Player->pos == tag::Pos::QB && bUnderCenter && A.UnderCenterSnap) PlayOneShot(A.UnderCenterSnap);
		else if (Player && Player->pos == tag::Pos::QB && A.SnapCatch) PlayOneShot(A.SnapCatch);
	}
	bWasPreSnap = bPreSnap;
	if (OneShotLeft > 0.f && OneShot && !bPreSnap)
	{
		OneShotLeft -= DeltaTime;
		OneShotAge += DeltaTime;
		// a catch is over once he is away with the ball: a returner or receiver sprinting off does not keep the crouched
		// catch pose for the clip's full length (the run crossfades in from where the catch is)
		const bool bDiveClip = OneShot == A.CatchDive || OneShot == A.Side(A.CatchDive, true);
		if (bOneShotCatch && !bDiveClip && OneShotAge > 0.3f && SpeedCm > 450.f) OneShotLeft = 0.f; // a layout dive plays out
		// (and once he has scored: a three-second catch capture played on through the celebration it should have given way to)
		if (bOneShotCatch && !bDiveClip && OneShotAge > 0.45f && Anim == TEXT("celebrate")) OneShotLeft = 0.f;
		// and a move made on the spot is over once the sim has him running: a beaten blocker turning to chase played his
		// lunge for a second while covering 4 m/s (a capture that travels is followed instead and keeps its feet)
		if (!bOneShotCatch && !bOneShotRep && !bDiveClip && OneShotAge > 0.35f && SpeedCm > 320.f)
			if (const UAnimSequence* OSq = Cast<UAnimSequence>(OneShot); OSq && !OSq->HasRootMotion()) OneShotLeft = 0.f;
		// and a capture that travels gives way once it cannot keep the body with the sim: at the follow limit for a
		// quarter second the man is being dragged (a rush win curling left while the sim sends him straight at the
		// quarterback, a set angled away from where the pocket goes); what follows (the run, the pass-block walk) is
		// steered at where he should be
		LagTime = VisOffset.SizeSquared2D() > 52.f * 52.f ? LagTime + DeltaTime : 0.f;
		// (never a man the capture has put on the turf: cut there he stood up out of his back in one crossfade - a
		// blocker run over by a bull rush snapping into a shuffle, p37)
		const bool bOnTurf = MHBody && MHBody->GetSocketLocation(TEXT("pelvis")).Z - GetActorLocation().Z < 58.f;
		if (!bOneShotCatch && !bDiveClip && !bOnTurf && OneShotAge > 0.4f && LagTime > 0.25f) OneShotLeft = 0.f;
		PlayClip(OneShot, false, OneShotRate, OneShotStart);
		bRepClip = bOneShotRep;
		if (!BlockAlign.IsNearlyZero(0.5f)) SetActorLocation(Location + VisOffset + BlockAlign);
		if (OneShotLeft <= 0.f)
		{
			bOneShotRep = false;
			// a capture that leaves him on the turf (a pancake, the blocker of a bull rush): he gets up from there
			if (bOnTurf && !bDiveClip && A.GetUpDown && A.GetUpUp)
			{
				OneShot = nullptr;
				bMissing = false;
				StartGetUp(Location);
				return;
			}
			// the layout dive ends ~1 m ahead of where it started: hand that travel to the actor so the next pose
			// starts where the body is (VisOffset then eases back onto the sim position)
			// (by where the body actually is: a dive cut short had carried it 1.9 m, and the full 3.3 m put on top threw
			// the man 3 m in a frame - the one pop left in the p33 and p36 games)
			if (OneShot == A.CatchDive || OneShot == A.Side(A.CatchDive, true))
			{
				FVector Carried = MHBody ? MHBody->GetSocketLocation(TEXT("pelvis")) - GetActorLocation() : GetActorForwardVector() * FAthleteAssets::CatchDiveTravel;
				Carried.Z = 0.f;
				// the actor itself goes there, from the next frame (this frame still shows the dive's last pose): an offset
				// alone was thrown away after the whistle, where a man 1.5 m off his sim spot is left where he stands, and the
				// body snapped back 3 m once the sim spotted the catch at the landing
				PendingCarry = Carried.GetClampedToMaxSize(FAthleteAssets::CatchDiveTravel + 60.f);
				// (and the crossfade into the next clip must not carry that travel a second time: it blended out of the
				// dive's pose, 3.3 m ahead of the actor, on top of the actor having moved there - the one pop of p58 / p59)
				if (GetWorld()) DropTravelUntil = GetWorld()->GetTimeSeconds() + 0.4;
			}
			bMissing = false;
			OneShot = nullptr;
			CurrentAnim.Reset(); // one-shot done: let whatever state follows (re)start next frame (crossfaded)
		}
		return;
	}
	// pre-snap: hand keyed stance loops (three point for the line, shotgun for the QB, two point for the rest)
	if ((bPreSnap || GetOffPending > 0) && SpeedCm < 40.f && Player) // (a lineman holds his stance the beat before his first step)
	{
		const tag::Pos P = Player->pos;
		UAnimationAsset* S = bLine ? A.Stance3pt.Get() : P == tag::Pos::QB ? (bUnderCenter && A.StanceUnderCenter ? A.StanceUnderCenter.Get() : A.StanceShotgun.Get()) : A.Stance2pt.Get();
		if (P == tag::Pos::DT && A.Stance4pt && Player->id % 2) S = A.Stance4pt; // some tackles fire out of a four point
		else if ((P == tag::Pos::RB || P == tag::Pos::FB) && A.StanceRB) S = A.StanceRB;
		else if ((P == tag::Pos::CB || P == tag::Pos::S) && A.StanceDB) S = A.StanceDB;
		else if (P == tag::Pos::LB && A.StanceLB) S = A.StanceLB;
		else if ((P == tag::Pos::K || P == tag::Pos::P) && A.StanceK) S = A.StanceK;
		// the pack library's stances for the position (index 0 keeps the measured cut), the player's own every snap
		if (S && Player)
		{
			const int32 Key = bLine ? ((P == tag::Pos::DE || P == tag::Pos::DT) ? 1001 : 1000) : static_cast<int32>(P);
			if (const TArray<TObjectPtr<UAnimationAsset>>* Lib = A.PackStances.Find(Key))
				if (Lib->Num()) { const int32 i = Player->id % (Lib->Num() + 1); if (i > 0) S = (*Lib)[i - 1].Get(); }
		}
		if (S)
		{
			// per player phase offset so a whole line does not breathe in unison
			PlayClip(S, true, 1.f, FMath::Frac(Player->id * 0.37f) * 2.5f);
			return;
		}
	}
	// the quarterback's drop: the keyed three step (shotgun) or five step (under center) drop while the sim backs him
	// out of the snap with the ball, at the sim's speed; then the pocket (on the toes, ball at the chest, eyes
	// scanning) instead of a standing idle. The throw cuts in from the pocket's pose.
	if (Player && Player->pos == tag::Pos::QB && bHasBallF && bLiveF && !bPreSnap && A.QBDropShotgun && A.QBDropUnder && A.QBPocket)
	{
		const FVector Fwd = FRotator(0, SmoothedYaw, 0).Vector();
		const float Back = -FVector::DotProduct(FVector(VelWorld.X, VelWorld.Y, 0.f), Fwd);
		const bool bDropping = Anim == TEXT("run") && SpeedCm > 120.f && Back > 0.6f * SpeedCm;
		static const bool bTrace = FParse::Param(FCommandLine::Get(), TEXT("TagQBTrace"));
		if (bTrace) UE_LOG(LogTemp, Display, TEXT("TAG qb #%d anim=%s v=%.0f back=%.0f under=%d dropped=%d left=%.2f oneshot=%.2f"), Player->number, *Anim, SpeedCm, Back, bUnderCenter, bDropped, DropLeft, OneShotLeft);
		if (bDropping && (DropLeft > 0.f || !bDropped))
		{
			UAnimationAsset* D = bUnderCenter ? A.QBDropUnder.Get() : A.QBDropShotgun.Get();
			float DropSpeed = bUnderCenter ? FAthleteAssets::DropUnderSpeed : FAthleteAssets::DropShotgunSpeed;
			// the quarterback's own library drop (the placed one is one entry among them)
			if (const FAthleteAssets::FPackClip* P = FAthleteAssets::PickOr(bUnderCenter ? A.PackDropsUC : A.PackDropsSG, Player->id))
				{
					D = P->Anim; // at the placed drop's pace: a whole capture's mean speed holds its setup and read far too slow
				}
			const float Rate = FMath::Clamp(SpeedCm / DropSpeed, 0.7f, 1.5f);
			if (!bDropped)
			{
				DropLeft = D->GetPlayLength() / Rate;
				bDropped = true;
				UE_LOG(LogTemp, Display, TEXT("TAG QB drop %s #%d at %.0f cm/s (rate %.2f)"), *D->GetName(), Player->number, SpeedCm, Rate);
			}
			DropLeft -= DeltaTime;
			PlayClip(D, false, Rate);
			return;
		}
		// (only while he stands: the loop is a quarterback on one spot, and under a man sliding or stepping up at
		// 0.4-0.9 m/s its planted feet travelled 78 cm/s - a tenth of all the skating in the p24 game. Moving, he is on
		// the matched locomotion with both hands on the ball.)
		bPocketLoop = !bDropping && (bPocketLoop ? SpeedCm < 55.f : SpeedCm < 30.f) && (Anim == TEXT("idle") || Anim == TEXT("run"));
		// moving in the pocket (a slide, a step up, drifting from the rush): the pack's quarterback walks and jogs to
		// that side, ball held ready as captured, their steps steered at the sim by the follower. (The matched
		// locomotion there was a field player's run with the hands pulled to the ball by IK.)
		if (!bPocketLoop && !bDropping && SpeedCm < 300.f && (Anim == TEXT("idle") || Anim == TEXT("run")) && A.QBWalks.Clips.Num() && MHBody)
		{
			const bool bJog = A.QBJogs.Clips.Num() && (QBWalk >= 100 ? SpeedCm > 170.f : SpeedCm > 230.f);
			const FAthleteAssets::FWalkSet& Set = bJog ? A.QBJogs : A.QBWalks;
			const FVector VelCS = MHBody->GetComponentTransform().InverseTransformVectorNoScale(FVector(VelWorld.X, VelWorld.Y, 0.f).GetSafeNormal());
			const int32 Cur = QBWalk >= 100 ? QBWalk - 100 : QBWalk;
			const bool bSameSet = QBWalk >= 0 && (QBWalk >= 100) == bJog && Set.Clips.IsValidIndex(Cur);
			QBWalkT += DeltaTime;
			int32 Best = bSameSet ? Cur : -1;
			float BestDot = bSameSet ? static_cast<float>(FVector::DotProduct(Set.Dir[Cur], VelCS)) + (QBWalkT < 0.5f ? 2.f : 0.25f) : -2.f; // (half a second on a pick, then a real change of way)
			for (int32 i = 0; i < Set.Clips.Num(); ++i)
			{
				const float Dot = static_cast<float>(FVector::DotProduct(Set.Dir[i], VelCS));
				if (Dot > BestDot) { BestDot = Dot; Best = i; }
			}
			if (Best >= 0)
			{
				if (Best != Cur || !bSameSet) QBWalkT = 0.f;
				QBWalk = Best + (bJog ? 100 : 0);
				bRepClip = true; // the capture holds the ball: no hands pulled to it
				PlayClip(Set.Clips[Best].Get(), true, FMath::Clamp(SpeedCm / FMath::Max(Set.Speed[Best], 1.f), 0.5f, 1.2f));
				return;
			}
		}
		QBWalk = -1;
		// out of the pocket at a run: the pack's quarterback scramble (ball in one hand, eyes up) and, flat out, his run.
		// (He kept the pocket's sideways jog up to 4.8 m/s - a man shuffling while the user held the stick to run.)
		if (!bDropping && SpeedCm >= 300.f && Anim == TEXT("run") && A.QBScramble.IsValid())
		{
			const bool bFlat = SpeedCm > 600.f && A.QBRun.IsValid();
			UAnimSequence* Rn = bFlat ? A.QBRun.Get() : A.QBScramble.Get();
			bRepClip = true; // the capture carries the ball itself
			PlayClip(Rn, true, FMath::Clamp(SpeedCm / FMath::Max(bFlat ? A.QBRunSpeed : A.QBScrambleSpeed, 1.f), 0.75f, 1.45f));
			return;
		}
		if (bPocketLoop)
		{
			PlayClip(A.QBPocket, true, 1.f + StyleJitter * 0.08f, FMath::Frac(Player->id * 0.37f) * 1.2f);
			return;
		}
	}
	// a defender closing on the carrier in space breaks down: hips sunk, chest up, arms out, choppy feet
	if (A.Breakdown && bLiveF && !bPreSnap && Player && !bLineman && CarrierDistF > 0.f && CarrierDistF < 320.f && SpeedCm < 230.f &&
		!bHasBallF && (Anim == TEXT("run") || Anim == TEXT("idle")))
	{
		UAnimationAsset* B = A.Breakdowns.Num() ? A.Breakdowns[Player->id % A.Breakdowns.Num()].Get() : A.Breakdown.Get();
		if (CurrentClip != B) UE_LOG(LogTemp, Display, TEXT("TAG breakdown #%d at %.0f cm from the carrier"), Player->number, CarrierDistF);
		PlayClip(B, true, 1.f + StyleJitter * 0.1f, FMath::Frac(Player->id * 0.43f) * 0.8f);
		return;
	}
	if (Anim == TEXT("celebrate"))
	{
		// each player has his own celebration (keyed loops), phase offset so nobody moves in unison
		UAnimationAsset* C = A.Celebrations.Num() && Player ? A.Side(A.Celebrations[Player->id % A.Celebrations.Num()], (Player->id / 7) % 2 == 1) : A.Celebrate.Get();
		// (a pack capture is a celebration played once, from its start, and held: looped from a point inside it, a man who
		// dived to the turf to pose was back on his feet in one frame when it wrapped)
		const bool bCapture = C && C->GetName().StartsWith(TEXT("A_TAG_Pack_"));
		if (bChanged) PlayClip(C, !bCapture, A.bCelebrateKeyed || A.Celebrations.Num() ? 1.f : 1.2f, bCapture ? 0.f : Player ? FMath::Frac(Player->id * 0.29f) : 0.f);
		return;
	}
	// locomotion: direction of travel relative to facing, speed-matched so feet don't slide
	const float VelYaw = SpeedCm > 25.f ? FMath::RadiansToDegrees(FMath::Atan2(VelWorld.Y, VelWorld.X)) : SmoothedYaw;
	const float Dir = SpeedCm > 25.f ? FMath::FindDeltaAngleDegrees(SmoothedYaw, VelYaw) : 0.f;
	SmoothedDir += FMath::FindDeltaAngleDegrees(SmoothedDir, Dir) * FMath::Clamp(DeltaTime * 8.f, 0.f, 1.f);
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, SpeedCm, DeltaTime, 10.f);
	// near-stationary blockers: keyed engaged loop (hands on the chest plate) or the pass set before contact;
	// moving blockers keep the crouch blend space
	// paired block: locked-up linemen close to hand fighting distance (each takes half the correction, so the pair
	// meets in the middle) and square their pads to each other; eases back off when they disengage
	{
		FVector Want = FVector::ZeroVector;
		if (AAthlete* O = BlockPartner.Get(); O && Anim == TEXT("block"))
		{
			FVector To = O->GetActorLocation() - (Location + VisOffset); // (from where his body is, not his sim spot)
			To.Z = 0.f;
			const float D = To.Size();
			if (D > 1.f && D < 260.f)
			{
				Want = (To / D * (D - 110.f) * 0.5f).GetClampedToMaxSize(80.f); // hip to hip: both lean in, arms on the chest plate
				// (within 20 cm of that the pair is left as it stands: corrected to the centimetre every frame, two men on
				// their own captures were slid under their planted feet all rep - 27-35 cm/s on locked-up pairs, p27)
				if (FMath::Abs(static_cast<float>(FVector::Dist2D(O->GetActorLocation(), GetActorLocation())) - 110.f) < 20.f) Want = BlockAlign;
				// (and while he is on a capture that travels: his steps are steered to the spacing, and the offset working
				// against them halved his ground speed under full-length strides - a cornerback on the pass-block walk
				// covering 51 cm/s on steps of 100, p28)
				if (bFollowActive) Want = BlockAlign;
				// squaring up at a human turn rate (a fifth of the angle a frame turned a man 36 degrees in one frame)
				const float DYaw = FMath::FindDeltaAngleDegrees(SmoothedYaw, To.Rotation().Yaw) * FMath::Clamp(DeltaTime * 6.f, 0.f, 1.f);
				SmoothedYaw += FMath::Clamp(DYaw, -240.f * DeltaTime, 240.f * DeltaTime);
				SetActorRotation(FRotator(0, SmoothedYaw, 0));
			}
		}
		// closing to hand-fighting range at a shuffle's pace (1.2 m/s): the old ease covered 80 cm at up to 6 m/s, a glide
		BlockAlign = FMath::VInterpConstantTo(BlockAlign, Want, DeltaTime, 60.f);
		if (!BlockAlign.IsNearlyZero(0.5f)) SetActorLocation(Location + VisOffset + BlockAlign);
		// the strike: both men's hands shoot to the chest plate the moment they meet (once per engagement)
		SincePunch += DeltaTime;
		if (AAthlete* O = BlockPartner.Get(); O && Anim == TEXT("block") && O != PunchedPartner.Get() && A.BlockPunch &&
			SincePunch > 1.f && !PunchedThisPlay.Contains(O) && PunchedThisPlay.Num() < 4)
		{
			PunchedPartner = O;
			PunchedThisPlay.Add(O);
			SincePunch = 0.f;
			LineLoop = 0;
			SmoothedPush = 0.f;
			// three strikes in four from a capture of real first contact (his side's man in it), the keyed punch otherwise
			// (every other one left the keyed punch a quarter of all one-shots)
			const bool bDefender = Player && (Player->pos == tag::Pos::DE || Player->pos == tag::Pos::DT || Player->pos == tag::Pos::LB ||
				Player->pos == tag::Pos::CB || Player->pos == tag::Pos::S);
			const TArray<FAthleteAssets::FPackClip>& EPool = bDefender ? A.PackEngageDef : A.PackEngageOff;
			const int32 PunchSeq = (Player ? Player->id : 0) + PunchedThisPlay.Num();
			const FAthleteAssets::FPackClip* EP = (PunchSeq % 4) ? FAthleteAssets::PickOr(EPool, PunchSeq) : nullptr;
			if (EP)
			{
				PlayOneShotFrom(EP->Anim, EP->Start);
				OneShotLeft = FMath::Min(OneShotLeft, 0.9f); // the strike, then back to the engaged loop
			}
			else PlayOneShot(A.BlockPunch);
			return;
		}
	}
	if (Anim == TEXT("block") && SpeedCm < 220.f && A.BlockEngaged && A.BlockPassSet)
	{
		// locked up only with somebody to be locked up with: a blocker with nobody on him (a lineman uncovered, a back
		// or tight end kept in) stands in the pack's pass-block ready stance, hands up and feet live, not in an
		// engaged clinch with the air
		const bool bBackOrEnd = Player && (Player->pos == tag::Pos::RB || Player->pos == tag::Pos::FB || Player->pos == tag::Pos::TE);
		UAnimationAsset* Ready = bBackOrEnd && A.BlockReadyBack ? A.BlockReadyBack.Get() : A.BlockReadyOL ? A.BlockReadyOL.Get() : A.BlockPassSet.Get();
		UAnimationAsset* Loop = BlockPartner.IsValid() ? A.BlockEngaged.Get() : Ready;
		float Rate = 1.f;
		if (BlockPartner.IsValid())
		{
			// who is winning the rep picks the loop (with hysteresis, so a balanced rep doesn't flicker): the rusher
			// walking his man into the pocket, the blocker anchoring / driving, or the hand fight in between
			SmoothedPush = FMath::FInterpTo(SmoothedPush, LinePush, DeltaTime, 3.f);
			if (LineLoop == 0) LineLoop = SmoothedPush > 0.32f ? 1 : SmoothedPush < -0.32f ? -1 : 0;
			else if ((LineLoop == 1 && SmoothedPush < 0.22f) || (LineLoop == -1 && SmoothedPush > -0.22f)) LineLoop = 0;
			UAnimationAsset* Want = nullptr;
			float Authored = 100.f; // the loop's own ground speed (cm/s): the play rate follows the pile
			if (LineLoop == 1)
			{
				if (bLineDefender) { Want = A.BullDriveLoop; Authored = 120.f; }
				else if (bLinePassPro) { Want = A.AnchorLoop; Authored = 90.f; }
				else { Want = A.DrivenBackLoop; Authored = 140.f; }
			}
			else if (LineLoop == -1)
			{
				if (bLineDefender) { Want = A.DrivenBackLoop; Authored = 140.f; }
				else if (!bLinePassPro) { Want = A.DriveLoop; Authored = 160.f; }
			}
			if (Want)
			{
				Loop = Want;
				Rate = FMath::Clamp(SpeedCm / Authored, 0.6f, 1.4f);
			}
		}
		float StartAt = Player ? FMath::Frac(Player->id * 0.41f) : 0.f;
		// a pass rush locked up and even: the pack's two-man stand-off, each man his own part, both on the pair's clock
		// (the defender's id sets the pair's phase, so the pairs along a line are not in step)
		const AAthlete* Him = BlockPartner.Get();
		if (Him && LineLoop == 0 && bLinePassPro && A.RepLoops.Num() && Player && Him->Player)
		{
			const int32 DefId = bLineDefender ? Player->id : Him->Player->id;
			const FAthleteAssets::FLineRep& R = A.RepLoops[FMath::Abs(DefId) % A.RepLoops.Num()];
			UAnimSequence* Part = bLineDefender ? R.D.Get() : R.O.Get();
			const float Len = FMath::Max(0.1f, static_cast<float>(FMath::Min(R.D->GetPlayLength(), R.O->GetPlayLength())));
			Loop = Part;
			Rate = 1.f;
			StartAt = FMath::Fmod(static_cast<float>(GetWorld()->GetTimeSeconds()) + FMath::Frac(DefId * 0.37f) * Len, Len);
			bRepClip = true;
		}
		// the pile on the move (a pocket giving ground, a bull rush walking his man back): the pass-block walk that goes
		// that way, its steps steered at the sim by the follower; back to the stand-off once it settles
		// (what he has to cover: the pile's motion and whatever his body is behind his spot)
		FVector AskW(VelWorld.X, VelWorld.Y, 0.f);
		if (const float Lag = static_cast<float>(VisOffset.Size2D()); Lag > 8.f) AskW -= VisOffset.GetSafeNormal2D() * FMath::Min((Lag - 8.f) * 2.5f, 200.f);
		if (Him)
		{
			FVector ToHim = Him->GetActorLocation() - GetActorLocation();
			ToHim.Z = 0.f;
			const float Dh = static_cast<float>(ToHim.Size());
			if (Dh > 1.f && Dh < 260.f && FMath::Abs(Dh - 110.f) > 10.f) AskW += ToHim / Dh * FMath::Clamp((Dh - 110.f) * 2.5f, -120.f, 120.f);
		}
		const float AskSp = static_cast<float>(AskW.Size());
		// (on the walk he stays on it until there has been nothing to cover for 0.4 s: slow steps at 0.4 of the capture's
		// pace stand in for a shuffle, and the pick no longer alternates with the stand-off every half second)
		LineStillT = AskSp < 22.f ? LineStillT + DeltaTime : 0.f;
		// (with nobody on him too: an uncovered man sliding with the pocket walks it in his set, he does not skate in the stance)
		const bool bWalk = A.BlockWalks.Num() && MHBody && (LineWalk >= 0 ? LineStillT < 0.4f : AskSp > 60.f);
		if (bWalk)
		{
			const FVector VelCS = MHBody->GetComponentTransform().InverseTransformVectorNoScale(AskW.GetSafeNormal());
			int32 Best = LineWalk;
			float BestDot = A.BlockWalkDir.IsValidIndex(LineWalk) ? static_cast<float>(FVector::DotProduct(A.BlockWalkDir[LineWalk], VelCS)) + 0.25f : -2.f; // (hold the walk he is on)
			for (int32 i = 0; i < A.BlockWalks.Num(); ++i)
			{
				const float Dot = static_cast<float>(FVector::DotProduct(A.BlockWalkDir[i], VelCS));
				if (Dot > BestDot) { BestDot = Dot; Best = i; }
			}
			LineWalk = Best;
			Loop = A.BlockWalks[Best].Get();
			// (slower steps for a slower pile: the walks cover 1.3-1.7 m/s, a pocket gives ground at a third of that)
			Rate = FMath::Clamp(AskSp / FMath::Max(A.BlockWalkSpeed[Best], 1.f), 0.4f, 1.15f);
			StartAt = Player ? FMath::Frac(Player->id * 0.41f) : 0.f;
			bRepClip = true; // (the walks are captured with the hands up in the block: no reach over them either)
		}
		else LineWalk = -1;
		// a line clip gets half a second before another replaces it: the pick flipped with every wobble of the pile (a
		// pass-block walk lasted a median 7 frames, 957 of them in 40 plays), and nearly all of the line's skating was
		// in the 3-12 frames after a change - the crossfade and the released foot pins (p29: 2.1 cm a planted foot a frame
		// there, 0.09 once a clip had run 12 frames)
		LineHeldT += DeltaTime;
		if (UAnimationAsset* Held = LineHeld.Get(); Held && Held != Loop && Held == CurrentClip && LineHeldT < 0.5f)
		{
			Loop = Held;
			Rate = LineHeldRate;
		}
		else if (LineHeld.Get() != Loop)
		{
			LineHeld = Loop;
			LineHeldT = 0.f;
		}
		LineHeldRate = Rate;
		PlayClip(Loop, true, Rate, StartAt);
		return;
	}
	const bool bCrouched = A.bMocap && A.Crouch && (Anim == TEXT("block") || (bLineman && SpeedCm < 260.f && !bPreSnap)); // (walking to his spot he walks upright)
	// after the whistle, standing around: hands on the hips (winded players bend over, hands on the knees)
	// (half a second: the whistle stops a dozen men at once, and for the first second they all stood on the one matched
	// idle - 4 or more men on it in 10-12 % of post-play frames, p65 / p66)
	// (and a man the sim still has on "run" but who stands still: most of those clones were that, p67)
	if (!bPreSnap && !bLiveF && StillTime > 0.5f && (Anim == TEXT("idle") || Anim == TEXT("run")) && A.IdleHandsHips && A.IdleHandsKnees && Player) // (after the whistle only: a zone defender waiting on the play is not resting; and a second, not 2.5: until then every man stood on the one matched idle)
	{
		// (bent over with his hands on his knees while he gets his wind: eight seconds of it, not the whole huddle)
		UAnimationAsset* I = Player->r.stamina < 70 && StillTime < 8.f ? A.IdleHandsKnees.Get() : A.IdleHandsHips.Get();
		if (Player->r.stamina >= 70 && A.PackPostPlayIdles.Num()) // the pack's post-play stands, the player's own
			if (const int32 i = Player->id % (A.PackPostPlayIdles.Num() + 1)) I = A.PackPostPlayIdles[i - 1].Get();
		PlayClip(I, true, 1.f, FMath::Frac(Player->id * 0.53f) * 2.f);
		return;
	}
	// standing through a live play (a deep safety, a corner on the far side, a back who stayed in): his own ready stance
	// from the pack's set for his position, not the one matched idle every standing man shared (4 or more men on it in
	// 6 % of all frames of the p33 game)
	if (bLiveF && !bPreSnap && !bHasBallF && !bLine && Anim == TEXT("idle") && StillTime > 0.6f && Player)
		if (const TArray<TObjectPtr<UAnimationAsset>>* Lib = A.PackStances.Find(static_cast<int32>(Player->pos)); Lib && Lib->Num())
		{
			// (a held stance, not one of the set's pre-snap fidgets: nobody resets his feet or taps his helmet mid play)
			for (int32 k = 0; k < Lib->Num(); ++k)
			{
				UAnimationAsset* St = (*Lib)[(FMath::Abs(Player->id) + k) % Lib->Num()].Get();
				if (!St || St->GetName().Contains(TEXT("Fidget")) || St->GetName().Contains(TEXT("Adjust")) || St->GetName().Contains(TEXT("Tap"))) continue;
				PlayClip(St, true, 1.f, FMath::Frac(Player->id * 0.37f) * 2.5f);
				return;
			}
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
	bHasBallF = InF.bHasBall;
	bLiveF = InF.bLive;
	CarrierDistF = InF.CarrierDist;
	if (InF.bPreSnap) bUnderCenter = InF.bUnderCenter; // keep the alignment through the snap
	if (InF.bPreSnap || InF.GetOffKind) GetOffKind = InF.GetOffKind;
	bEngaged = InF.bReach;
	if (!MHBody) return;
	// pre-snap gaze of a set lineman (GazeStats): the head's bind forward (+Y in component space) as shown now
	GazeSetT = InF.bPreSnap && SmoothedSpeed < 40.f ? GazeSetT + Dt : 0.f;
	if (InF.bPreSnap && Player && GazeSetT > 0.5f && MHBody->GetSkeletalMeshAsset())
	{
		const tag::Pos Pp = Player->pos;
		const int32 Hi = MHBody->GetBoneIndex(TEXT("head"));
		if ((Pp == tag::Pos::OT || Pp == tag::Pos::OG || Pp == tag::Pos::C || Pp == tag::Pos::DE || Pp == tag::Pos::DT) && Hi != INDEX_NONE &&
			MHBody->GetComponentSpaceTransforms().IsValidIndex(Hi))
		{
			const FReferenceSkeleton& R = MHBody->GetSkeletalMeshAsset()->GetRefSkeleton();
			FTransform Ref = FTransform::Identity;
			for (int32 I = Hi; I != INDEX_NONE; I = R.GetParentIndex(I)) Ref = Ref * R.GetRefBonePose()[I];
			const FVector G = MHBody->GetComponentSpaceTransforms()[Hi].GetRotation().RotateVector(Ref.GetRotation().UnrotateVector(FVector(0, 1, 0)));
			const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(G.Z, G.Size2D()));
			if (Pitch < -30.0 && FParse::Param(FCommandLine::Get(), TEXT("TagGazeDebug")))
				UE_LOG(LogTemp, Display, TEXT("TAG gaze low %s pitch %.1f set %.2f s look %.2f oneshot %d left %.2f speed %.0f"), *GetName(), Pitch, GazeSetT, LookA,
					OneShot ? 1 : 0, OneShotLeft, SmoothedSpeed);
			GGazeSum += Pitch;
			GGazeMin = FMath::Min(GGazeMin, Pitch);
			++GGazeN;
		}
	}
	// -TagHeadLog: a head turned up at the stands (the face's bind forward 25 deg or more over the horizon for 0.3 s),
	// with what he is playing and what the look layer is aiming at
	static const bool bHeadLog = FParse::Param(FCommandLine::Get(), TEXT("TagHeadLog"));
	if (bHeadLog && Player && MHBody->GetSkeletalMeshAsset() && !IsHidden())
	{
		const int32 Hi = MHBody->GetBoneIndex(TEXT("head"));
		if (Hi != INDEX_NONE && MHBody->GetComponentSpaceTransforms().IsValidIndex(Hi))
		{
			const FReferenceSkeleton& R = MHBody->GetSkeletalMeshAsset()->GetRefSkeleton();
			FTransform Ref = FTransform::Identity;
			for (int32 K = Hi; K != INDEX_NONE; K = R.GetParentIndex(K)) Ref = Ref * R.GetRefBonePose()[K];
			const FVector G = MHBody->GetComponentTransform().TransformVectorNoScale(
				MHBody->GetComponentSpaceTransforms()[Hi].GetRotation().RotateVector(Ref.GetRotation().UnrotateVector(FVector(0, 1, 0))));
			const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(G.Z, G.Size2D()));
			HeadUpT = Pitch > 25.f ? HeadUpT + Dt : 0.f;
			HeadUpMax = HeadUpT > 0.f ? FMath::Max(HeadUpMax, Pitch) : 0.f;
			if (HeadUpT > 0.3f && HeadUpT - Dt <= 0.3f)
			{
				const FVector Hd = MHBody->GetSocketLocation(TEXT("head"));
				const FVector To = LookSmoothed - Hd;
				UE_LOG(LogTemp, Display, TEXT("TAG head up: #%d %s pitch %.0f anim %s look %.2f(%d) aim %.0f deg %.0f cm presnap %d live %d ball %d | want z %.0f d %.0f, me z %.0f, head z %.0f"),
					Player->number, ANSI_TO_TCHAR(tag::posName(Player->pos)), Pitch, *TelemetryAnim(), LookA, InF.bLook ? 1 : 0,
					FMath::RadiansToDegrees(FMath::Atan2(To.Z, FMath::Max(To.Size2D(), 1.0))), To.Size(), InF.bPreSnap ? 1 : 0, InF.bLive ? 1 : 0, InF.bHasBall ? 1 : 0,
					InF.LookWorld.Z, FVector::Dist2D(InF.LookWorld, Hd), GetActorLocation().Z, Hd.Z);
			}
		}
	}
	UTagRetargetAnimInstance* I = Cast<UTagRetargetAnimInstance>(MHBody->GetAnimInstance());
	if (!I) return;
	auto Ease = [Dt](float& V, float Target, float Rate) { V = FMath::FInterpTo(V, Target, Dt, Rate); };
	const FAthleteAssets& AA = FAthleteAssets::Get();
	FAthleteFootball F = InF;
	// hand keyed clips already carry the throw and the stance: keep the procedural versions off them
	if (AA.ThrowKeyed) F.ThrowT = -1.f;
	// the keyed stance owns the body; the down hand is planted on the turf over it (the rig's arm is too short to
	// reach from a flat back), both hands in a four point
	// On the mocap's own skeleton (bNativeAnim) the captured stance plants its hand on the turf by itself, and the
	// captured blocks, tackles and carries lean as the men who played them leaned: no planted-hand IK, no added drive
	// lean or carrier pad level over them (those made up for the UEFN rig's shorter arm and straighter spine).
	const bool bHandDown = !AA.bNativeAnim && F.bStance && F.bPreSnap && AA.Stance3pt && !(OneShot && OneShotLeft > 0.f);
	if (AA.bNativeAnim) F.Lean = 0.f;
	if (F.bPreSnap && AA.Stance3pt) F.bStance = false;
	const bool bThrow = F.ThrowT >= 0.f;
	// a hand keyed one-shot (catch, handoff, pitch, rush move, ...), a keyed fall or get-up authors its own arms: keep
	// the procedural tuck / secure / reach off it (they would pull the hands off the catch), ease them back after
	const bool bKeyedArms = (OneShot && OneShotLeft > 0.f) || bFalling || bGettingUp;
	// (and a ball-carrier capture already carries the ball: no procedural tuck over Madden's own)
	bool bCarryClip = false;
	if (const UTagMotionMatchInstance* MI = MatchInstance())
		if (const UAnimationAsset* Ma = MI->bMatch ? MI->GetMatchedAsset() : CurrentClip.Get())
		{
			const FString Nm = Ma->GetName();
			bCarryClip = Nm.Contains(TEXT("_BC_")) && !Nm.Contains(TEXT("NonBC"));
		}
	Ease(CarryA, F.bHasBall && !F.bSecure && !bThrow && !bKeyedArms && !bCarryClip ? 1.f : 0.f, bKeyedArms || bCarryClip ? 14.f : 6.f);
	Ease(SecureA, F.bSecure && !bThrow && !bKeyedArms && !bRepClip ? 1.f : 0.f, 9.f);
	// (his part of a two-man capture has its hands where the two men put them: no reach over it)
	Ease(ReachA, F.bReach && !bKeyedArms && !bRepClip ? 1.f : 0.f, F.bReach && !bKeyedArms && !bRepClip ? 7.f : 10.f);
	Ease(LeanA, F.Lean, 5.f);
	Ease(StanceA, F.bStance ? 1.f : 0.f, 6.f);
	Ease(StanceHandA, bHandDown ? 1.f : 0.f, bHandDown ? 6.f : 12.f);
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
	static const bool bNoKeepOut = FParse::Param(FCommandLine::Get(), TEXT("TagNoKeepOut"));
	P.KeepOutN = bNoKeepOut ? 0 : KeepOutN;
	P.bDropFromTravel = GetWorld() && GetWorld()->GetTimeSeconds() < DropTravelUntil;
	for (int32 k = 0; k < P.KeepOutN; ++k) P.KeepOut[k] = KeepOut[k];
	P.ReachSpread = F.ReachSpread;
	P.LeanAlpha = LeanA;
	P.StanceAlpha = StanceA;
	P.StanceHandAlpha = StanceHandA;
	P.bStanceBothHands = Player && Player->pos == tag::Pos::DT && AA.Stance4pt && (Player->id % 2);
	P.CarryLeanDeg = AA.bNativeAnim ? 0.f : 7.f;
	P.LookAlpha = LookA;
	P.LookTarget = C.InverseTransformPosition(LookSmoothed);
	Ease(LookUpMax, F.bLookHigh ? 45.f : 20.f, 3.f);
	P.LookUpMaxDeg = LookUpMax;
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
		// Madden's own ball socket: the pack rig's RightFootball / LeftFootball joints, a fixed 14 cm off the wrist,
		// where the captures put the ball in the hand. Tucked, the carrying hand's joint lies along the inside of the
		// forearm with its Y axis along the arm (probe: Y . forearm 0.92-0.98 on the MG_BC loops). The carrying hand is
		// the one whose socket is nearer the chest. (The ball sat on our own palm point before, 4-5 cm short of the
		// socket and off the palm: from outside it read as fused to the hand, Fred 16 Nov.)
		static const bool bNoSocket = FParse::Param(FCommandLine::Get(), TEXT("TagNoBallSocket"));
		const int32 RS = MHBody->GetBoneIndex(TEXT("RightFootball")), LS = MHBody->GetBoneIndex(TEXT("LeftFootball"));
		if (!bNoSocket && RS != INDEX_NONE && LS != INDEX_NONE && MHBody->GetBoneIndex(TEXT("spine_05")) != INDEX_NONE)
		{
			const FVector Chest = MHBody->GetSocketLocation(TEXT("spine_05"));
			const FTransform TR = MHBody->GetBoneTransform(RS), TL = MHBody->GetBoneTransform(LS);
			// the carrying arm stays tucked while the free one swings: the hands' distances from the chest smoothed over
			// ~0.4 s, the hand latched until the other is clearly the tucked one (a fresh carry starts from this frame)
			const float Side = FVector::Dist(TR.GetLocation(), Chest) - FVector::Dist(TL.GetLocation(), Chest); // > 0: the left nearer
			const uint64 Fr = GFrameCounter;
			if (BallSideFrame + 1 < Fr) { BallSideEma = Side; bBallLeft = Side > 0.f; }
			else if (BallSideFrame != Fr)
			{
				const float Dt = GetWorld() ? FMath::Clamp(GetWorld()->GetDeltaSeconds(), 0.f, 0.1f) : 1.f / 30.f;
				BallSideEma += (Side - BallSideEma) * FMath::Min(1.f, Dt / 0.4f);
				if (bBallLeft ? BallSideEma < -10.f : BallSideEma > 10.f) bBallLeft = !bBallLeft;
			}
			BallSideFrame = Fr;
			// (in the throw the ball is in the throwing hand's grip, fingers on the laces: cocking the arm back put that
			// hand further from the chest, and the latch handed the ball to the other one)
			const bool bLeft = IsThrowing() ? false : bBallLeft;
			const FTransform& T = bLeft ? TL : TR;
			const FVector Hand = MHBody->GetSocketLocation(bLeft ? TEXT("hand_l") : TEXT("hand_r"));
			const FVector Elbow = MHBody->GetSocketLocation(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
			FVector Long = T.GetUnitAxis(EAxis::Y);
			if (FVector::DotProduct(Long, Hand - Elbow) < 0.f) Long = -Long; // the nose toward the hand
			OutLoc = T.GetLocation();
			OutRot = FRotationMatrix::MakeFromXZ(Long, T.GetUnitAxis(EAxis::Z)).Rotator();
			return true;
		}
		// in the right hand: the ball sits on the palm, held between the fingers and the thumb (its centre half a ball
		// off the palm, toward the thumb's side), long axis along the forearm (tucked: nose up and forward)
		const FVector Along = (HR - LR).GetSafeNormal();
		if (MHBody->GetBoneIndex(TEXT("middle_01_r")) != INDEX_NONE && MHBody->GetBoneIndex(TEXT("thumb_02_r")) != INDEX_NONE)
		{
			const FVector Mid = MHBody->GetSocketLocation(TEXT("middle_01_r"));
			const FVector Th = MHBody->GetSocketLocation(TEXT("thumb_02_r"));
			const FVector Palm = (HR + Mid) * 0.5f;                       // palm centre, between the wrist and the knuckles
			FVector Up = FVector::CrossProduct(Mid - HR, Th - HR);          // the palm's normal...
			if (FVector::DotProduct(Up, Th - Palm) < 0.f) Up = -Up;         // ...toward the side the thumb curls to
			Up.Normalize();
			OutLoc = Palm + Up * 7.5f + Along * 2.f;
		}
		else OutLoc = HR + Along * 6.f + Fwd * 3.f - GetActorRightVector() * 2.f;
		OutRot = Along.Rotation();
	}
	return true;
}

FString AAthlete::TelemetryAnim() const
{
	if (bRagdoll) return TEXT("ragdoll");
	if (bReplaying) return TEXT("replay");
	if (bFalling) return FString::Printf(TEXT("fall:%s@%.2f"), *GetNameSafe(FallClip), FallT);
	if (bGettingUp) return FString::Printf(TEXT("getup:%s"), *GetNameSafe(CurrentClip));
	if (OneShot && OneShotLeft > 0.f) return FString::Printf(TEXT("one:%s"), *GetNameSafe(OneShot));
	if (const UTagMotionMatchInstance* M = MatchInstance())
	{
		// the motion matching node also plays plain clips (stances, celebrations, loops): report the clip then
		if (!M->bMatch && CurrentClip) return FString::Printf(TEXT("clip:%s"), *GetNameSafe(CurrentClip));
		return FString::Printf(TEXT("mm:%s"), *GetNameSafe(M->GetMatchedAsset()));
	}
	if (UAnimSingleNodeInstance* I = Body->GetSingleNodeInstance())
		return FString::Printf(TEXT("clip:%s"), *GetNameSafe(I->GetCurrentAsset()));
	return FString::Printf(TEXT("clip:%s"), *GetNameSafe(CurrentClip));
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
	if (MHBody && MHBody->GetSkeletalMeshAsset() && FAthleteAssets::Get().bHero)
	{
		// model pack body: a spread of bone heights (component space) and the mesh, to check the skinning frame
		Fb += FString::Printf(TEXT(" | hero %s"), *MHBody->GetSkeletalMeshAsset()->GetName());
		{
			const TArray<FTransform>& CS = MHBody->GetComponentSpaceTransforms();
			int32 Hi = 0, Sc = 0, Lo = 0;
			for (int32 i = 1; i < CS.Num(); ++i)
			{
				if (CS[i].GetLocation().Z > CS[Hi].GetLocation().Z) Hi = i;
				if (CS[i].GetLocation().Z < CS[Lo].GetLocation().Z) Lo = i;
				if (CS[i].GetScale3D().GetAbsMax() > CS[Sc].GetScale3D().GetAbsMax() || CS[i].GetScale3D().GetAbsMin() < CS[Sc].GetScale3D().GetAbsMin()) Sc = i;
			}
			if (CS.Num())
				Fb += FString::Printf(TEXT(" top=%s %.0f low=%s %.0f scale=%s %s"), *MHBody->GetBoneName(Hi).ToString(), CS[Hi].GetLocation().Z,
					*MHBody->GetBoneName(Lo).ToString(), CS[Lo].GetLocation().Z, *MHBody->GetBoneName(Sc).ToString(), *CS[Sc].GetScale3D().ToCompactString());
		}
		for (const TCHAR* Bn : {TEXT("root"), TEXT("pelvis"), TEXT("spine_05"), TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("upperarm_l"), TEXT("RM_LeftUpLegTwist1"), TEXT("RM_BackPads"), TEXT("helmetPlacement")})
		{
			const int32 Bi = MHBody->GetBoneIndex(Bn);
			if (Bi != INDEX_NONE && MHBody->GetComponentSpaceTransforms().IsValidIndex(Bi))
			{
				const FTransform& T = MHBody->GetComponentSpaceTransforms()[Bi];
				Fb += FString::Printf(TEXT(" %s=%.0f/%.2f"), Bn, T.GetLocation().Z, T.GetScale3D().GetMax());
			}
		}
		// the hidden source mesh's own heights (what the shown body copies or retargets from)
		Fb += TEXT(" | src");
		for (const TCHAR* Bn : {TEXT("root"), TEXT("pelvis"), TEXT("spine_05"), TEXT("head")})
		{
			const int32 Bi = Body->GetBoneIndex(Bn);
			if (Bi != INDEX_NONE && Body->GetComponentSpaceTransforms().IsValidIndex(Bi))
			{
				const FTransform& T = Body->GetComponentSpaceTransforms()[Bi];
				const FRotator R = T.Rotator();
				Fb += FString::Printf(TEXT(" %s=%.0f(%.0f,%.0f,%.0f)"), Bn, T.GetLocation().Z, R.Pitch, R.Yaw, R.Roll);
			}
		}
	}
	if (const UTagMotionMatchInstance* M = MatchInstance())
		if (bInMatch) Fb += FString::Printf(TEXT(" | mm=%s gait=%d tsm=%.2f at=%.2f cost=%.3f clip=%.0f mul=%.2f"), *GetNameSafe(M->GetMatchedAsset()), MMGait, M->TrajectorySpeedMultiplier,
			M->MatchedTime, M->MatchedCost, M->MatchedClipSpeed, M->SpeedRate);
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

int32 AAthlete::BodySpheres(FVector4f* Out) const
{
	USkeletalMeshComponent* M = Visible();
	if (!M) return 0;
	int32 N = 0;
	auto Add = [&](const TCHAR* Bone, const FVector& Off, float R)
	{
		if (M->GetBoneIndex(Bone) == INDEX_NONE) return;
		const FVector W = M->GetBoneTransform(M->GetBoneIndex(Bone)).TransformPosition(Off);
		Out[N++] = FVector4f(static_cast<float>(W.X), static_cast<float>(W.Y), static_cast<float>(W.Z), R);
	};
	Add(TEXT("head"), FVector::ZeroVector, 15.f);   // the helmet
	Add(TEXT("spine_04"), FVector::ZeroVector, 19.f); // the pads
	Add(TEXT("pelvis"), FVector::ZeroVector, 16.f);
	return N;
}

bool AAthlete::Hands(FVector& L, FVector& R) const
{
	USkeletalMeshComponent* M = Visible();
	if (!M || M->GetBoneIndex(TEXT("hand_l")) == INDEX_NONE || M->GetBoneIndex(TEXT("hand_r")) == INDEX_NONE) return false;
	L = M->GetBoneLocation(TEXT("hand_l"));
	R = M->GetBoneLocation(TEXT("hand_r"));
	return true;
}

bool AAthlete::BonePair(const TCHAR* Ln, const TCHAR* Rn, FVector& L, FVector& R) const
{
	USkeletalMeshComponent* M = Visible();
	if (!M || M->GetBoneIndex(Ln) == INDEX_NONE || M->GetBoneIndex(Rn) == INDEX_NONE) return false;
	L = M->GetBoneLocation(Ln);
	R = M->GetBoneLocation(Rn);
	return true;
}

bool AAthlete::Feet(FVector& L, FVector& R) const
{
	USkeletalMeshComponent* M = Visible();
	if (!M || M->GetBoneIndex(TEXT("foot_l")) == INDEX_NONE || M->GetBoneIndex(TEXT("foot_r")) == INDEX_NONE) return false;
	L = M->GetBoneLocation(TEXT("foot_l"));
	R = M->GetBoneLocation(TEXT("foot_r"));
	return true;
}

float AAthlete::PelvisZ() const
{
	USkeletalMeshComponent* M = Visible();
	return M && M->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE ? static_cast<float>(M->GetBoneLocation(TEXT("pelvis")).Z) : 95.f;
}

FVector AAthlete::PelvisAt() const
{
	USkeletalMeshComponent* M = Visible();
	return M && M->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE ? M->GetBoneLocation(TEXT("pelvis")) : GetActorLocation();
}

void AAthlete::BreakFree()
{
	if (!bFalling) return;
	bFalling = false;
	bGettingUp = false;
	FallHoldLeft = 0.f;
	TackledLeft = 0.f;
	// he goes on from where the capture had him: the gap to the sim's spot is eased off as after any capture
	VisOffset = (GetActorLocation() - LastSimLoc).GetClampedToMaxSize2D(300.f);
	VisOffset.Z = 0.f;
	CurrentClip = nullptr;
	CurrentAnim.Reset();
}
