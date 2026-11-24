#include "Render/TagRetargetAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "TwoBoneIK.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TagRetargetAnimInstance)

FAnimInstanceProxy* UTagRetargetAnimInstance::CreateAnimInstanceProxy()
{
	return new FTagRetargetProxy(this, &RetargetNode);
}

void UTagRetargetAnimInstance::Configure(UIKRetargeter* Asset, USkeletalMeshComponent* Source)
{
	CopySource = Source;
	bCopy = Asset == nullptr && Source != nullptr;
	RetargetNode.IKRetargeterAsset = Asset;
	RetargetNode.RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	RetargetNode.SourceMeshComponent = Source;
	RetargetNode.bSuppressWarnings = bCopy;
	if (FIKRetargetProcessor* P = RetargetNode.GetRetargetProcessor())
	{
		P->SetNeedsInitialized();
	}
}

void FTagRetargetProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UTagRetargetAnimInstance* I = Cast<UTagRetargetAnimInstance>(InAnimInstance))
	{
		In = I->Proc; // game thread -> worker copy
		bReplay = I->bReplay && I->ReplayMesh.Num() > 0;
		if (bReplay) ReplayMesh = I->ReplayMesh;
		// copy mode: the source mesh has finished its pose (it is this mesh's tick prerequisite); take its component
		// space transforms here on the game thread, like the retarget node does, in this mesh's bone order
		bCopy = I->bCopy;
		const USkeletalMeshComponent* Src = I->CopySource.Get();
		const USkeletalMeshComponent* Dst = I->GetSkelMeshComponent();
		const USkeletalMesh* SM = Src ? Src->GetSkeletalMeshAsset() : nullptr;
		const USkeletalMesh* DM = Dst ? Dst->GetSkeletalMeshAsset() : nullptr;
		if (bCopy && SM && DM)
		{
			if (SM != CopySrcMesh || DM != CopyDstMesh)
			{
				CopySrcMesh = SM;
				CopyDstMesh = DM;
				const FReferenceSkeleton& SR = SM->GetRefSkeleton();
				const FReferenceSkeleton& DR = DM->GetRefSkeleton();
				CopyMap.SetNum(DR.GetNum());
				for (int32 i = 0; i < DR.GetNum(); ++i) CopyMap[i] = SR.FindBoneIndex(DR.GetBoneName(i));
			}
			const bool bCached = (Src->IsUsingExternalInterpolation() || (Src->ShouldUseUpdateRateOptimizations() && Src->AnimUpdateRateParams != nullptr))
				&& Src->GetCachedComponentSpaceTransforms().Num() == Src->GetComponentSpaceTransforms().Num();
			const TArray<FTransform>& CS = bCached ? Src->GetCachedComponentSpaceTransforms() : Src->GetComponentSpaceTransforms();
			CopyCS.SetNum(CopyMap.Num(), EAllowShrinking::No);
			CopyHave.SetNumZeroed(CopyMap.Num(), EAllowShrinking::No);
			for (int32 i = 0; i < CopyMap.Num(); ++i)
			{
				const int32 S = CopyMap[i];
				CopyHave[i] = CS.IsValidIndex(S) ? 1 : 0;
				if (CopyHave[i])
				{
					CopyCS[i] = CS[S];
					CopyCS[i].SetScale3D(FVector::OneVector); // component-space translations already carry any scale
				}
			}
		}
		else bCopy = false;
	}
	BlendElapsed += DeltaSeconds;
	Dt = DeltaSeconds;
	if (In.ClipSerial != SeenSerial)
	{
		SeenSerial = In.ClipSerial;
		FromPose = LastPose; // previous Evaluate finished before this PreUpdate
		BlendElapsed = 0.f;
		CurBlendTime = 0.f;
	}
	// an explicit pose snapshot (ragdoll -> get-up) wins over the last displayed pose
	if (const UTagRetargetAnimInstance* I = Cast<UTagRetargetAnimInstance>(InAnimInstance))
	{
		if (I->SnapshotSerial != SeenSnap)
		{
			SeenSnap = I->SnapshotSerial;
			PendingMesh = I->SnapshotMesh;
			bPendingSnap = PendingMesh.Num() > 0;
			BlendElapsed = 0.f;
			CurBlendTime = I->SnapshotBlend;
		}
	}
}

namespace
{
	// the joints a viewer reads a pose by, in component space (for the distance of the shown pose from the capture)
	constexpr int32 kDevN = 11;
	void DevJoints(const FPoseContext& Ctx, FVector Out[kDevN])
	{
		static const FName Names[kDevN] = {TEXT("pelvis"), TEXT("spine_05"), TEXT("head"), TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("hand_l"),
			TEXT("hand_r"), TEXT("calf_l"), TEXT("calf_r"), TEXT("foot_l"), TEXT("foot_r")};
		const FBoneContainer& BC = Ctx.Pose.GetBoneContainer();
		for (int32 k = 0; k < kDevN; ++k)
		{
			Out[k] = FVector::ZeroVector;
			const int32 Mesh = BC.GetPoseBoneIndexForBoneName(Names[k]);
			if (Mesh == INDEX_NONE) continue;
			FCompactPoseBoneIndex I = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
			if (!I.IsValid()) continue;
			FTransform T = Ctx.Pose[I];
			for (FCompactPoseBoneIndex P = BC.GetParentBoneIndex(I); P.IsValid(); P = BC.GetParentBoneIndex(P)) T = T * Ctx.Pose[P];
			Out[k] = T.GetLocation();
		}
	}
	float DevMean(const FVector A[kDevN], const FVector B[kDevN])
	{
		double S = 0.0;
		for (int32 k = 0; k < kDevN; ++k) S += FVector::Dist(A[k], B[k]);
		return static_cast<float>(S / kDevN);
	}
}

bool FTagRetargetProxy::Evaluate(FPoseContext& Output)
{
	static const bool bMeasure = FParse::Param(FCommandLine::Get(), TEXT("TagHlFull")) || FParse::Param(FCommandLine::Get(), TEXT("TagDev"));
	if (bReplay)
	{
		// instant replay: the recorded pose as it was shown, nothing layered on top
		Output.ResetToRefPose();
		const FBoneContainer& BC = Output.Pose.GetBoneContainer();
		const int32 N = Output.Pose.GetNumBones();
		LastPose.SetNum(N, EAllowShrinking::No);
		for (FCompactPoseBoneIndex I(0); I < N; ++I)
		{
			const int32 M = BC.MakeMeshPoseIndex(I).GetInt();
			if (ReplayMesh.IsValidIndex(M)) Output.Pose[I] = ReplayMesh[M];
			LastPose[I.GetInt()] = Output.Pose[I];
		}
		BlendElapsed = 1e3f;
		for (float& D : DevFrame) D = 0.f;
		return true;
	}
	if (bCopy) EvaluateCopy(Output); // the mocap on its own skeleton: the source's pose, bone for bone
	else Node->Evaluate_AnyThread(Output); // mocap retargeted onto this body
	MeasurePlants(Output);
	FVector J0[kDevN], Ja[kDevN], Jb[kDevN];
	if (bMeasure)
	{
		DevJoints(Output, J0);
		for (int32 k = 0; k < kDevN; ++k) Ja[k] = J0[k];
	}
	auto Step = [&](int32 Layer)
	{
		if (!bMeasure) return;
		DevJoints(Output, Jb);
		DevFrame[Layer] = DevMean(Ja, Jb);
		for (int32 k = 0; k < kDevN; ++k) Ja[k] = Jb[k];
	};
	ApplyFootballLayer(Output);
	Step(1);
	ApplyOrient(Output);
	Step(2);
	ApplyStride(Output);
	Step(3);
	ApplyLean(Output);
	Step(4);
	ApplyCrossfade(Output);
	ApplyImpact(Output); // (after the blend: a jolt is not blended away by a change of clip)
	Step(5);
	ApplyFeet(Output); // on the shown pose, after the blend (a blended foot would slide through every transition)
	Step(6);
	ApplyKeepOut(Output);
	if (bMeasure) DevFrame[0] = DevMean(J0, Ja);
	ApplyTwist(Output);
	const int32 N = Output.Pose.GetNumBones();
	LastPose.SetNum(N, EAllowShrinking::No);
	for (FCompactPoseBoneIndex I(0); I < N; ++I) LastPose[I.GetInt()] = Output.Pose[I];
	return true;
}

void FTagRetargetProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimInstanceProxy::PostUpdate(InAnimInstance);
	// game thread, worker finished: hand this evaluation's foot slide to the instance (AAthlete reads and clears it)
	if (UTagRetargetAnimInstance* I = Cast<UTagRetargetAnimInstance>(InAnimInstance))
	{
		I->SlideRaw += SlideRawFrame;
		I->SlideShown += SlideShownFrame;
		I->Touchdowns += TouchdownsFrame;
		for (int32 k = 0; k < 7; ++k) I->Dev[k] = DevFrame[k];
		I->PlantFeet = PlantFeetFrame;
	}
}

void FTagRetargetProxy::ApplyTwist(FPoseContext& Output)
{
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoTwist"));
	if (bOff) return;
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const void* Mesh = BC.GetSkeletalMeshAsset();
	if (Mesh != TwistMesh)
	{
		TwistMesh = Mesh;
		Twists.Reset();
		const FReferenceSkeleton& R = BC.GetReferenceSkeleton();
		const TArray<FTransform>& RefPose = R.GetRefBonePose();
		struct FSet { const TCHAR* Bone; const TCHAR* Parent; const TCHAR* Ref; bool bCounter; int32 Count; };
		static const FSet Sets[] = {
			{TEXT("ForeArmTwist"), TEXT("lowerarm_"), TEXT("hand_"), false, 3},
			{TEXT("ArmTwist"), TEXT("upperarm_"), TEXT("lowerarm_"), true, 3},
			{TEXT("LegTwist"), TEXT("calf_"), TEXT("foot_"), false, 2},
			{TEXT("UpLegTwist"), TEXT("thigh_"), TEXT("calf_"), true, 3}};
		for (const FSet& Set : Sets)
			for (const TCHAR* Side : {TEXT("l"), TEXT("r")})
			{
				const TCHAR* Long = Side[0] == 'l' ? TEXT("Left") : TEXT("Right");
				const int32 P = R.FindBoneIndex(FName(*(FString(Set.Parent) + Side)));
				const int32 Q = R.FindBoneIndex(FName(*(FString(Set.Ref) + Side)));
				if (P == INDEX_NONE || Q == INDEX_NONE) continue;
				const FVector Axis = RefPose[Q].GetTranslation(); // the limb, in the parent's frame
				for (int32 k = 1; k <= Set.Count; ++k)
				{
					const int32 B = R.FindBoneIndex(FName(*(FString(TEXT("RM_")) + Long + Set.Bone + FString::FromInt(k))));
					if (B == INDEX_NONE || R.GetParentIndex(B) != P || Axis.SizeSquared() < 1e-6f) continue;
					FTwistBone T;
					T.Bone = B;
					T.Parent = P;
					T.Ref = Q;
					T.Along = FMath::Clamp(static_cast<float>(FVector::DotProduct(RefPose[B].GetTranslation(), Axis) / Axis.SizeSquared()), 0.f, 1.f);
					T.bCounter = Set.bCounter;
					Twists.Add(T);
				}
			}
	}
	if (!Twists.Num()) return;
	for (const FTwistBone& T : Twists)
	{
		const FCompactPoseBoneIndex B = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(T.Bone));
		const FCompactPoseBoneIndex P = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(T.Parent));
		const FCompactPoseBoneIndex Q = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(T.Ref));
		if (B.GetInt() == INDEX_NONE || P.GetInt() == INDEX_NONE || Q.GetInt() == INDEX_NONE) continue;
		FQuat Swing, Twist;
		if (!T.bCounter)
		{
			// the hand's (foot's) turn about the forearm (calf), in the forearm's frame, shared out along it
			const FVector Axis = Output.Pose.GetRefPose(Q).GetTranslation().GetSafeNormal();
			const FQuat Delta = Output.Pose[Q].GetRotation() * Output.Pose.GetRefPose(Q).GetRotation().Inverse();
			Delta.ToSwingTwist(Axis, Swing, Twist);
			Output.Pose[B].SetRotation((FQuat::Slerp(FQuat::Identity, Twist, T.Along) * Output.Pose.GetRefPose(B).GetRotation()).GetNormalized());
		}
		else
		{
			// the upper arm's (thigh's) own turn about itself, held back near the shoulder (hip)
			const FVector Axis = Output.Pose.GetRefPose(Q).GetTranslation().GetSafeNormal();
			const FQuat Delta = Output.Pose.GetRefPose(P).GetRotation().Inverse() * Output.Pose[P].GetRotation();
			Delta.ToSwingTwist(Axis, Swing, Twist);
			Output.Pose[B].SetRotation((FQuat::Slerp(FQuat::Identity, Twist.Inverse(), 1.f - T.Along) * Output.Pose.GetRefPose(B).GetRotation()).GetNormalized());
		}
	}
}

void FTagRetargetProxy::EvaluateCopy(FPoseContext& Output)
{
	// every bone the source has takes the source's component-space transform; the others (face, pads, helpers the
	// source rig lacks) keep their reference pose under their parent. Local = CS relative to the parent's CS, so a rig
	// whose hierarchy skips bones (the 30-bone crowd rig) still puts each bone where the source has it.
	Output.ResetToRefPose();
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const int32 N = Output.Pose.GetNumBones();
	TArray<FTransform, TInlineAllocator<300>> CS;
	CS.SetNum(N);
	for (FCompactPoseBoneIndex I(0); I < N; ++I)
	{
		const int32 M = BC.MakeMeshPoseIndex(I).GetInt();
		const FCompactPoseBoneIndex P = BC.GetParentBoneIndex(I);
		if (CopyHave.IsValidIndex(M) && CopyHave[M])
		{
			CS[I.GetInt()] = CopyCS[M];
			Output.Pose[I] = P.GetInt() != INDEX_NONE ? CS[I.GetInt()].GetRelativeTransform(CS[P.GetInt()]) : CS[I.GetInt()];
			Output.Pose[I].NormalizeRotation();
		}
		else
		{
			CS[I.GetInt()] = P.GetInt() != INDEX_NONE ? Output.Pose[I] * CS[P.GetInt()] : Output.Pose[I];
		}
	}
}

void FTagRetargetProxy::ApplyCrossfade(FPoseContext& Output)
{
	const int32 N = Output.Pose.GetNumBones();
	if (bPendingSnap)
	{
		// mesh order -> this frame's compact order
		const FBoneContainer& BC = Output.Pose.GetBoneContainer();
		FromPose.SetNum(N);
		for (FCompactPoseBoneIndex I(0); I < N; ++I)
		{
			const int32 M = BC.MakeMeshPoseIndex(I).GetInt();
			FromPose[I.GetInt()] = PendingMesh.IsValidIndex(M) ? PendingMesh[M] : Output.Pose[I];
		}
		bPendingSnap = false;
	}
	const float BlendTime = CurBlendTime > 0.f ? CurBlendTime : In.BlendTime;
	if (FromPose.Num() == N && BlendElapsed < BlendTime && BlendTime > 0.f)
	{
		float A = FMath::Clamp(BlendElapsed / BlendTime, 0.f, 1.f);
		A = A * A * (3.f - 2.f * A); // ease in / out
		if (CurBlendTime > 0.f)
		{
			// out of a pose snapshot (the lying body into its get-up): blended in component space, each part on a straight
			// line from where it lies to where the get-up starts it. Both poses lie on the turf, so the blend stays on it;
			// the parent-relative blend swung the legs through arcs into the ground (feet 30-60 cm under the turf in the
			// first 0.2 s of a get-up, -TagTackleCam).
			const FBoneContainer& BC = Output.Pose.GetBoneContainer();
			CsFrom.SetNum(N, EAllowShrinking::No);
			CsTo.SetNum(N, EAllowShrinking::No);
			for (FCompactPoseBoneIndex I(0); I < N; ++I)
			{
				const FCompactPoseBoneIndex P = BC.GetParentBoneIndex(I);
				const FTransform F = FromPose[I.GetInt()], T = Output.Pose[I];
				CsFrom[I.GetInt()] = P.IsValid() ? F * CsFrom[P.GetInt()] : F;
				CsTo[I.GetInt()] = P.IsValid() ? T * CsTo[P.GetInt()] : T;
			}
			for (int32 i = 0; i < N; ++i)
			{
				FTransform& B = CsFrom[i];
				B.SetRotation(FQuat::Slerp(B.GetRotation(), CsTo[i].GetRotation(), A).GetNormalized());
				B.SetTranslation(FMath::Lerp(B.GetTranslation(), CsTo[i].GetTranslation(), A));
				B.SetScale3D(FMath::Lerp(B.GetScale3D(), CsTo[i].GetScale3D(), A));
			}
			for (FCompactPoseBoneIndex I(0); I < N; ++I)
			{
				const FCompactPoseBoneIndex P = BC.GetParentBoneIndex(I);
				Output.Pose[I] = P.IsValid() ? CsFrom[I.GetInt()].GetRelativeTransform(CsFrom[P.GetInt()]) : CsFrom[I.GetInt()];
			}
		}
		else
		{
		if (In.bDropFromTravel)
		{
			// the root, and the hips under it, wherever the capture carried its travel
			const FBoneContainer& BC = Output.Pose.GetBoneContainer();
			const int32 PelMesh = BC.GetPoseBoneIndexForBoneName(TEXT("pelvis"));
			const FCompactPoseBoneIndex Pel = PelMesh != INDEX_NONE ? BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(PelMesh)) : FCompactPoseBoneIndex(INDEX_NONE);
			for (const FCompactPoseBoneIndex I : {FCompactPoseBoneIndex(0), Pel})
			{
				if (!I.IsValid() || I.GetInt() >= N) continue;
				FVector Ft = FromPose[I.GetInt()].GetTranslation();
				const FVector Tt = Output.Pose[I].GetTranslation();
				if (FVector::DistSquared2D(Ft, Tt) > 60.f * 60.f) FromPose[I.GetInt()].SetTranslation(FVector(Tt.X, Tt.Y, Ft.Z)); // (his height still blends)
			}
		}
		for (FCompactPoseBoneIndex I(0); I < N; ++I)
		{
			FTransform& T = Output.Pose[I];
			const FTransform& F = FromPose[I.GetInt()];
			T.SetRotation(FQuat::Slerp(F.GetRotation(), T.GetRotation(), A).GetNormalized());
			T.SetTranslation(FMath::Lerp(F.GetTranslation(), T.GetTranslation(), A));
			T.SetScale3D(FMath::Lerp(F.GetScale3D(), T.GetScale3D(), A));
		}
		}
	}
}

// ------------------------------------------------------------------ football layer helpers
namespace
{
	// MetaHuman component space: +X = player's left, +Y = forward, +Z = up (cm).
	constexpr float kTau = 6.2831853f;

	float Smooth(float A, float B, float T) { const float X = FMath::Clamp((T - A) / FMath::Max(B - A, 1e-4f), 0.f, 1.f); return X * X * (3.f - 2.f * X); }

	struct FBones
	{
		FCompactPoseBoneIndex Pelvis{INDEX_NONE}, Spine1{INDEX_NONE}, Spine3{INDEX_NONE}, Spine5{INDEX_NONE}, Neck{INDEX_NONE}, Head{INDEX_NONE};
		FCompactPoseBoneIndex UL{INDEX_NONE}, LL{INDEX_NONE}, HL{INDEX_NONE}, UR{INDEX_NONE}, LR{INDEX_NONE}, HR{INDEX_NONE};
		FCompactPoseBoneIndex ThL{INDEX_NONE}, CaL{INDEX_NONE}, FtL{INDEX_NONE}, ThR{INDEX_NONE}, CaR{INDEX_NONE}, FtR{INDEX_NONE};
		FCompactPoseBoneIndex BaL{INDEX_NONE}, BaR{INDEX_NONE}; // the balls of the feet (the ankle when a rig has none)
		bool bValid = false;
		bool bLegs = false;
	};

	FCompactPoseBoneIndex Find(const FBoneContainer& BC, const TCHAR* Name)
	{
		const int32 Mesh = BC.GetPoseBoneIndexForBoneName(FName(Name));
		return Mesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
	}

	FBones Resolve(const FBoneContainer& BC)
	{
		FBones B;
		B.Pelvis = Find(BC, TEXT("pelvis"));
		B.Spine1 = Find(BC, TEXT("spine_01"));
		B.Spine3 = Find(BC, TEXT("spine_03"));
		B.Spine5 = Find(BC, TEXT("spine_05"));
		B.Neck = Find(BC, TEXT("neck_01"));
		B.Head = Find(BC, TEXT("head"));
		B.UL = Find(BC, TEXT("upperarm_l"));
		B.LL = Find(BC, TEXT("lowerarm_l"));
		B.HL = Find(BC, TEXT("hand_l"));
		B.UR = Find(BC, TEXT("upperarm_r"));
		B.LR = Find(BC, TEXT("lowerarm_r"));
		B.HR = Find(BC, TEXT("hand_r"));
		B.bValid = B.Pelvis.IsValid() && B.Spine1.IsValid() && B.Spine3.IsValid() && B.Spine5.IsValid() && B.Head.IsValid() &&
			B.UL.IsValid() && B.LL.IsValid() && B.HL.IsValid() && B.UR.IsValid() && B.LR.IsValid() && B.HR.IsValid();
		B.ThL = Find(BC, TEXT("thigh_l"));
		B.CaL = Find(BC, TEXT("calf_l"));
		B.FtL = Find(BC, TEXT("foot_l"));
		B.ThR = Find(BC, TEXT("thigh_r"));
		B.CaR = Find(BC, TEXT("calf_r"));
		B.FtR = Find(BC, TEXT("foot_r"));
		B.BaL = Find(BC, TEXT("ball_l"));
		B.BaR = Find(BC, TEXT("ball_r"));
		if (!B.BaL.IsValid()) B.BaL = B.FtL;
		if (!B.BaR.IsValid()) B.BaR = B.FtR;
		B.bLegs = B.Pelvis.IsValid() && B.ThL.IsValid() && B.CaL.IsValid() && B.FtL.IsValid() && B.ThR.IsValid() && B.CaR.IsValid() && B.FtR.IsValid();
		return B;
	}

	/** Two-bone leg IK to a component-space target. The knee keeps bending the way it already does, the foot keeps its
	 *  rotation, and the target is pulled in to the leg's reach (no locked knee). */
	void LegIK(FCSPose<FCompactPose>& P, FCompactPoseBoneIndex Th, FCompactPoseBoneIndex Ca, FCompactPoseBoneIndex Ft,
		FVector Target, float Alpha)
	{
		if (Alpha <= 0.001f) return;
		const FTransform TT = P.GetComponentSpaceTransform(Th);
		const FTransform TC = P.GetComponentSpaceTransform(Ca);
		const FTransform TF = P.GetComponentSpaceTransform(Ft);
		const FVector Hip = TT.GetLocation(), Knee = TC.GetLocation(), Foot = TF.GetLocation();
		const double L1 = FVector::Dist(Hip, Knee), L2 = FVector::Dist(Knee, Foot);
		if (L1 < 1.0 || L2 < 1.0) return;
		const FVector ToT = Target - Hip;
		const double Reach = (L1 + L2) * 0.985;
		if (ToT.Size() > Reach) Target = Hip + ToT.GetSafeNormal() * Reach;
		// the knee's own bend direction (off the hip-foot line), so the leg never flips
		const FVector Axis = (Foot - Hip).GetSafeNormal();
		FVector Bend = (Knee - Hip) - Axis * FVector::DotProduct(Knee - Hip, Axis);
		if (!Bend.Normalize()) Bend = FVector(0, 1, 0);
		FVector J, E;
		AnimationCore::SolveTwoBoneIK(Hip, Knee, Foot, Knee + Bend * 40.f, Target, J, E, false, 1.0, 1.0);
		const FQuat DU = FQuat::FindBetweenNormals((Knee - Hip).GetSafeNormal(), (J - Hip).GetSafeNormal());
		const FVector LowerNow = DU.RotateVector(Foot - Knee).GetSafeNormal();
		const FQuat DL = FQuat::FindBetweenNormals(LowerNow, (E - J).GetSafeNormal());
		FTransform NT = TT, NC = TC, NF = TF;
		NT.SetRotation((DU * TT.GetRotation()).GetNormalized());
		NC.SetRotation((DL * DU * TC.GetRotation()).GetNormalized());
		NC.SetLocation(J);
		NF.SetLocation(E); // rotation kept: the sole stays where the clip put it
		const FBoneTransform BT[] = {FBoneTransform(Th, NT), FBoneTransform(Ca, NC), FBoneTransform(Ft, NF)};
		P.LocalBlendCSBoneTransforms(BT, FMath::Clamp(Alpha, 0.f, 1.f));
	}

	/** Reference-pose component-space position of a bone. */
	FVector BindPos(const FPoseContext& Ctx, FCompactPoseBoneIndex B)
	{
		const FBoneContainer& BC = Ctx.Pose.GetBoneContainer();
		const USkeletalMesh* Mesh = Ctx.AnimInstanceProxy->GetSkelMeshComponent()->GetSkeletalMeshAsset();
		if (!Mesh) return FVector::ZeroVector;
		const FReferenceSkeleton& R = Mesh->GetRefSkeleton();
		int32 I = BC.MakeMeshPoseIndex(B).GetInt();
		FTransform T = FTransform::Identity;
		while (I != INDEX_NONE) { T = T * R.GetRefBonePose()[I]; I = R.GetParentIndex(I); }
		return T.GetLocation();
	}

	/** Rotate one bone about its own pivot by a component-space rotation; children follow. */
	void RotateCS(FCSPose<FCompactPose>& P, FCompactPoseBoneIndex B, const FQuat& Q, float Alpha)
	{
		if (!B.IsValid() || Alpha <= 0.f) return;
		FTransform T = P.GetComponentSpaceTransform(B);
		T.SetRotation((FQuat::Slerp(FQuat::Identity, Q, FMath::Min(Alpha, 1.f)) * T.GetRotation()).GetNormalized());
		const FBoneTransform BT[] = {FBoneTransform(B, T)};
		P.LocalBlendCSBoneTransforms(BT, 1.f);
	}

	/** Two-bone arm IK to a component-space target; Pole biases where the elbow points. */
	void ArmIK(FCSPose<FCompactPose>& P, FCompactPoseBoneIndex U, FCompactPoseBoneIndex L, FCompactPoseBoneIndex H,
		const FVector& Target, const FVector& Pole, float Alpha)
	{
		if (Alpha <= 0.001f) return;
		const FTransform TU = P.GetComponentSpaceTransform(U);
		const FTransform TL = P.GetComponentSpaceTransform(L);
		const FTransform TH = P.GetComponentSpaceTransform(H);
		FVector J, E;
		AnimationCore::SolveTwoBoneIK(TU.GetLocation(), TL.GetLocation(), TH.GetLocation(), TL.GetLocation() + Pole * 40.f, Target, J, E, false, 1.0, 1.0);
		const FQuat DU = FQuat::FindBetweenNormals((TL.GetLocation() - TU.GetLocation()).GetSafeNormal(), (J - TU.GetLocation()).GetSafeNormal());
		const FVector LowerNow = DU.RotateVector(TH.GetLocation() - TL.GetLocation()).GetSafeNormal();
		const FQuat DL = FQuat::FindBetweenNormals(LowerNow, (E - J).GetSafeNormal());
		FTransform NU = TU, NL = TL, NH = TH;
		NU.SetRotation((DU * TU.GetRotation()).GetNormalized());
		NL.SetRotation((DL * DU * TL.GetRotation()).GetNormalized());
		NL.SetLocation(J);
		NH.SetRotation((DL * DU * TH.GetRotation()).GetNormalized());
		NH.SetLocation(E);
		const FBoneTransform BT[] = {FBoneTransform(U, NU), FBoneTransform(L, NL), FBoneTransform(H, NH)};
		P.LocalBlendCSBoneTransforms(BT, FMath::Clamp(Alpha, 0.f, 1.f));
	}

	FVector Pos(FCSPose<FCompactPose>& P, FCompactPoseBoneIndex B) { return P.GetComponentSpaceTransform(B).GetLocation(); }

	/** Bind-pose component-space rotation of a bone (to know which local axis is "forward"). */
	FQuat BindRot(const FPoseContext& Ctx, FCompactPoseBoneIndex B)
	{
		const FBoneContainer& BC = Ctx.Pose.GetBoneContainer();
		const USkeletalMesh* Mesh = Ctx.AnimInstanceProxy->GetSkelMeshComponent()->GetSkeletalMeshAsset();
		if (!Mesh) return FQuat::Identity;
		const FReferenceSkeleton& R = Mesh->GetRefSkeleton(); // mesh bone order (MakeMeshPoseIndex)
		int32 I = BC.MakeMeshPoseIndex(B).GetInt();
		FTransform T = FTransform::Identity;
		while (I != INDEX_NONE) { T = T * R.GetRefBonePose()[I]; I = R.GetParentIndex(I); }
		return T.GetRotation();
	}
}

void FTagRetargetProxy::ApplyFootballLayer(FPoseContext& Output) const
{
	const bool bAny = In.ThrowT >= 0.f || In.CarryAlpha > 0.01f || In.SecureAlpha > 0.01f || In.ReachAlpha > 0.01f ||
		In.LeanAlpha > 0.01f || In.StanceAlpha > 0.01f || In.StanceHandAlpha > 0.01f || In.LookAlpha > 0.01f;
	if (!bAny) return;
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bValid) return;

	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);

	// --- torso: drive lean (blocks/tackles) and 3-point stance flatten the back (rotate about +X = lateral); a ball
	// carrier runs with his pad level down a little (the capture's upright sprint against a back in traffic)
	const float LeanDeg = 16.f * In.LeanAlpha + 34.f * In.StanceAlpha + In.CarryLeanDeg * In.CarryAlpha;
	if (LeanDeg > 0.1f)
	{
		RotateCS(P, B.Spine1, FQuat(FVector(1, 0, 0), -FMath::DegreesToRadians(LeanDeg * 0.55f)), 1.f);
		RotateCS(P, B.Spine3, FQuat(FVector(1, 0, 0), -FMath::DegreesToRadians(LeanDeg * 0.45f)), 1.f);
	}

	// --- throw: load -> release -> follow-through, torso coil and arm path from the style vector
	if (In.ThrowT >= 0.f)
	{
		const float T = In.ThrowT;
		const float Cock = 0.42f + 0.04f * In.StyleJitter, Rel = Cock + 0.16f;
		// torso: turn away from the target while loading, fire through at release
		const float Twist = 26.f * Smooth(0.f, Cock, T) - 50.f * Smooth(Cock, Rel + 0.08f, T);
		RotateCS(P, B.Spine1, FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(Twist * 0.4f)), 1.f);
		RotateCS(P, B.Spine3, FQuat(FVector(0, 0, 1), FMath::DegreesToRadians(Twist * 0.6f)), 1.f);
		const FVector SR = Pos(P, B.UR), SL = Pos(P, B.UL);
		const float Slot = FMath::Clamp(In.ArmSlot, -1.f, 1.f); // over the top raises the release, sidearm widens it
		const FVector Hold = SR + FVector(10, 22, -26);
		const FVector Cocked = SR + FVector(-10.f - 6.f * (1.f - Slot), -20, 20.f + 6.f * Slot);
		const FVector Release = SR + FVector(-4.f - 10.f * (1.f - Slot), 40, 16.f + 10.f * Slot);
		const FVector Follow = SR + FVector(30, 26, -42);
		FVector HandR;
		if (T < Cock) HandR = FMath::Lerp(Hold, Cocked, Smooth(0.f, Cock, T));
		else if (T < Rel) HandR = FMath::Lerp(Cocked, Release, Smooth(Cock, Rel, T));
		else HandR = FMath::Lerp(Release, Follow, Smooth(Rel, 1.f, T));
		ArmIK(P, B.UR, B.LR, B.HR, HandR, FVector(-0.6f, -0.6f, -0.5f).GetSafeNormal(), 1.f);
		// off arm points at the target while loading, then pulls to the hip to open the chest
		const FVector Guide = SL + FVector(-4, 40, -2);
		const FVector Tuck = SL + FVector(10, 6, -34);
		ArmIK(P, B.UL, B.LL, B.HL, FMath::Lerp(Guide, Tuck, Smooth(Cock - 0.05f, Rel + 0.1f, T)), FVector(0.7f, -0.3f, -0.6f).GetSafeNormal(), 0.9f);
	}
	else
	{
		const FVector Chest = Pos(P, B.Spine5);
		// --- QB ball security: both hands on the ball, chest high
		if (In.SecureAlpha > 0.01f)
		{
			const FVector Ball = Chest + FVector(-4, 24, -6);
			ArmIK(P, B.UR, B.LR, B.HR, Ball + FVector(-7, 0, -2), FVector(-0.8f, -0.3f, -0.5f).GetSafeNormal(), In.SecureAlpha);
			ArmIK(P, B.UL, B.LL, B.HL, Ball + FVector(7, 2, 3), FVector(0.8f, -0.3f, -0.5f).GetSafeNormal(), In.SecureAlpha);
		}
		// --- ball carrier: tuck high and tight in the right arm (elbow pinned to the ribs)
		if (In.CarryAlpha > 0.01f)
		{
			const FVector Tuck = Chest + FVector(-16, 14, -14);
			ArmIK(P, B.UR, B.LR, B.HR, Tuck, FVector(-0.4f, -0.7f, -0.6f).GetSafeNormal(), In.CarryAlpha);
		}
		// --- reach: catch hands to the ball / wrap the ball carrier / hands inside on a block
		if (In.ReachAlpha > 0.01f)
		{
			const FVector C = In.ReachTarget;
			const FVector ToT = (C - Chest).GetSafeNormal2D();
			const FVector Side = FVector::CrossProduct(FVector::UpVector, ToT).GetSafeNormal(); // player's left of the target line
			const float Spread = In.ReachSpread;
			const bool bLow = C.Z < Pos(P, B.Pelvis).Z + 10.f;
			const FVector PoleL = (Side * 0.7f + FVector(0, 0, bLow ? 0.3f : -0.7f)).GetSafeNormal();
			const FVector PoleR = (-Side * 0.7f + FVector(0, 0, bLow ? 0.3f : -0.7f)).GetSafeNormal();
			// only what the arms can reach: within 60 cm of the chest the hands go all the way, by 100 cm not at all. (Pulled
			// toward a ball still three metres off, both arms locked out straight at it from the shoulders - arms that
			// looked bolted on - and stayed there through the catch.)
			const float Near = 1.f - FMath::Clamp((static_cast<float>(FVector::Dist(C, Chest)) - 60.f) / 40.f, 0.f, 1.f);
			ArmIK(P, B.UL, B.LL, B.HL, C + Side * Spread, PoleL, In.ReachAlpha * Near);
			ArmIK(P, B.UR, B.LR, B.HR, C - Side * Spread, PoleR, In.ReachAlpha * Near * (1.f - In.CarryAlpha));
		}
		// --- 3-point stance: down hand on the turf in front of the right foot
		if (In.StanceAlpha > 0.01f)
		{
			const FVector Down = FVector(-18.f, 52.f, 3.f);
			ArmIK(P, B.UR, B.LR, B.HR, Down, FVector(-0.5f, -0.2f, 0.8f).GetSafeNormal(), In.StanceAlpha);
		}
		// --- the keyed stances carry a flat back with the eyes up; the down hand (both in a four point) is planted on the
		// turf here, a hand's length ahead of the foot, as far as the arm reaches
		if (In.StanceHandAlpha > 0.01f)
		{
			ArmIK(P, B.UR, B.LR, B.HR, FVector(-20.f, 46.f, 2.f), FVector(-0.5f, -0.2f, 0.8f).GetSafeNormal(), In.StanceHandAlpha);
			if (In.bStanceBothHands) ArmIK(P, B.UL, B.LL, B.HL, FVector(20.f, 46.f, 2.f), FVector(0.5f, -0.2f, 0.8f).GetSafeNormal(), In.StanceHandAlpha);
		}
	}

	// --- head tracks the ball / carrier (neck limits ~60 degrees)
	if (In.LookAlpha > 0.01f)
	{
		const FTransform TH = P.GetComponentSpaceTransform(B.Head);
		const FVector LocalFwd = BindRot(Output, B.Head).Inverse().RotateVector(FVector(0, 1, 0));
		const FVector Fwd = TH.GetRotation().RotateVector(LocalFwd).GetSafeNormal();
		FVector Want = In.LookTarget - TH.GetLocation();
		// neck range: don't bury the chin (max ~30 deg down) or look straight behind
		const float Horiz = FMath::Max(Want.Size2D(), 1.f);
		const float UpTan = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(In.LookUpMaxDeg, 5.f, 45.f)));
		Want.Z = FMath::Clamp(static_cast<float>(Want.Z), -Horiz * 0.58f, Horiz * UpTan);
		Want = Want.GetSafeNormal();
		const float Ang = FMath::Acos(FMath::Clamp(FVector::DotProduct(Fwd, Want), -1.f, 1.f));
		const float MaxAng = FMath::DegreesToRadians(55.f);
		FQuat D = FQuat::FindBetweenNormals(Fwd, Want);
		if (Ang > MaxAng) D = FQuat::Slerp(FQuat::Identity, D, MaxAng / Ang);
		RotateCS(P, B.Neck, FQuat::Slerp(FQuat::Identity, D, 0.4f), In.LookAlpha);
		const FTransform TH2 = P.GetComponentSpaceTransform(B.Head);
		const FVector Fwd2 = TH2.GetRotation().RotateVector(LocalFwd).GetSafeNormal();
		FQuat D2 = FQuat::FindBetweenNormals(Fwd2, Want);
		const float Ang2 = FMath::Acos(FMath::Clamp(FVector::DotProduct(Fwd2, Want), -1.f, 1.f));
		if (Ang2 > MaxAng * 0.6f) D2 = FQuat::Slerp(FQuat::Identity, D2, MaxAng * 0.6f / Ang2);
		RotateCS(P, B.Head, D2, In.LookAlpha);
		// a man on his feet does not stare at the upper deck: several of the pack's post-play idles and jogs tip the head
		// back 50-60 deg (scoreboard glances), more than the look above takes off at its weight. Whatever is left over
		// the limit comes off here, split over the neck and head. (Lying on his back, his pelvis-to-head line is flat and
		// the face up is right: only an upright body.)
		const FVector Up = (P.GetComponentSpaceTransform(B.Head).GetLocation() - P.GetComponentSpaceTransform(B.Pelvis).GetLocation()).GetSafeNormal();
		if (Up.Z > 0.75f)
		{
			const FTransform TH3 = P.GetComponentSpaceTransform(B.Head);
			const FVector Fwd3 = TH3.GetRotation().RotateVector(LocalFwd).GetSafeNormal();
			const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Fwd3.Z, FMath::Max(static_cast<float>(Fwd3.Size2D()), 1e-3f)));
			const float Over = Pitch - FMath::Clamp(In.LookUpMaxDeg, 5.f, 45.f) - 4.f;
			if (Over > 0.f)
			{
				const FVector Axis = FVector::CrossProduct(FVector::UpVector, Fwd3).GetSafeNormal(); // (a positive turn about it tips the face down)
				if (!Axis.IsNearlyZero())
				{
					const float W = FMath::Clamp(In.LookAlpha / 0.85f, 0.f, 1.f);
					RotateCS(P, B.Neck, FQuat(Axis, FMath::DegreesToRadians(Over * 0.45f)), W);
					RotateCS(P, B.Head, FQuat(Axis, FMath::DegreesToRadians(Over * 0.55f)), W);
				}
			}
		}
	}

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
	(void)kTau;
}

void FTagRetargetProxy::ApplyStride(FPoseContext& Output) const
{
	// a longer stride for a faster man: each foot's offset from the pelvis along the direction of travel is scaled,
	// the hips sit a touch lower, the legs follow through IK (the clip's cadence stays; the ground covered per step
	// grows with the speed, as a sprinter's does)
	const float S = In.StrideScale;
	if (FMath::Abs(S - 1.f) < 0.005f) return; // shorter steps too (a clip covering more ground than the man does)
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bLegs) return;
	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);
	const FVector Dir = FVector(In.StrideDirCS.X, In.StrideDirCS.Y, 0.f).GetSafeNormal();
	if (Dir.IsNearlyZero()) return;
	FTransform Pel = P.GetComponentSpaceTransform(B.Pelvis);
	const FVector PelPos = Pel.GetLocation();
	const FVector FL = Pos(P, B.FtL), FR = Pos(P, B.FtR);
	Pel.SetLocation(PelPos - FVector(0, 0, (FMath::Max(S, 1.f) - 1.f) * 10.f)); // a touch lower when longer (the sprint bands put the hips at 0.90-0.99 m)
	const FBoneTransform BT[] = {FBoneTransform(B.Pelvis, Pel)};
	P.LocalBlendCSBoneTransforms(BT, 1.f);
	const FVector NL = FL + Dir * FVector::DotProduct(FL - PelPos, Dir) * (S - 1.f);
	const FVector NR = FR + Dir * FVector::DotProduct(FR - PelPos, Dir) * (S - 1.f);
	LegIK(P, B.ThL, B.CaL, B.FtL, NL, 1.f);
	LegIK(P, B.ThR, B.CaR, B.FtR, NR, 1.f);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
}

void FTagRetargetProxy::ApplyOrient(FPoseContext& Output) const
{
	const float Deg = In.OrientWarpDeg;
	if (FMath::Abs(Deg) < 0.5f) return;
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bLegs || !B.Spine1.IsValid()) return;
	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);
	const FQuat Q(FVector::UpVector, FMath::DegreesToRadians(Deg));
	RotateCS(P, B.Pelvis, Q, 1.f);         // the hips and both legs turn about the pelvis
	RotateCS(P, B.Spine1, Q.Inverse(), 1.f); // the trunk turns back: the upper body keeps its facing
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
}

void FTagRetargetProxy::ApplyLean(FPoseContext& Output) const
{
	// the body tilts from the ankles into its acceleration: the drive of a start, the lean into a cut or a curved run,
	// sitting back into a stop. The pelvis and everything above it rotate about the point between the soles; the feet
	// stay where the clip put them (leg IK), so the legs take the lean the way ankles and knees do.
	const float Deg = In.LeanCS.Size2D();
	if (Deg < 0.2f) return;
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bLegs) return;
	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);
	const FVector FL = Pos(P, B.FtL), FR = Pos(P, B.FtR);
	FVector Pivot = (FL + FR) * 0.5f;
	Pivot.Z = FMath::Min(FL.Z, FR.Z) - 8.f; // the sole
	const FVector Dir = FVector(In.LeanCS.X, In.LeanCS.Y, 0.f) / Deg;
	const FQuat R(FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal(), FMath::DegreesToRadians(Deg));
	FTransform T = P.GetComponentSpaceTransform(B.Pelvis);
	T.SetLocation(Pivot + R.RotateVector(T.GetLocation() - Pivot));
	T.SetRotation((R * T.GetRotation()).GetNormalized());
	const FBoneTransform BT[] = {FBoneTransform(B.Pelvis, T)};
	P.LocalBlendCSBoneTransforms(BT, 1.f);
	LegIK(P, B.ThL, B.CaL, B.FtL, FL, 1.f);
	LegIK(P, B.ThR, B.CaR, B.FtR, FR, 1.f);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
}

void FTagRetargetProxy::ApplyImpact(FPoseContext& Output)
{
	// two bodies meeting: the push moves the hips (the sim moves the man), the upper body and the head lag behind and
	// spring back - a damped torsion spring about the pelvis for the trunk (2.2 Hz, damping ratio 0.35) and a stiffer,
	// lighter one for the head (3 Hz, 0.3), each kicked by the impact's velocity change. A 4-5 m/s collision between
	// two big men tips the trunk ~5 degrees and snaps the head ~9; a big hit more. -TagNoImpact for A/B.
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoImpact"));
	if (bOff || Dt <= 1e-5f) return;
	if (In.ImpactSerial != SeenImpact)
	{
		SeenImpact = In.ImpactSerial;
		FVector J = In.ImpactCS;
		J.Z = 0.f;
		const float Mag = FMath::Min(static_cast<float>(J.Size()), 900.f);
		if (Mag > 25.f)
		{
			// a turn about (up x push) by a negative angle carries the top of the body back against the push
			const FVector Axis = FVector::CrossProduct(FVector::UpVector, J.GetSafeNormal());
			ImpOmega -= Axis * (Mag * 0.0043f);
			HeadOmega -= Axis * (Mag * 0.009f);
		}
	}
	// the springs (semi-implicit Euler in small steps)
	{
		const int32 N = FMath::Clamp(FMath::CeilToInt(Dt / (1.f / 240.f)), 1, 8);
		const float H = FMath::Min(Dt, 0.1f) / N;
		const float W1 = 2.f * PI * 2.2f, Z1 = 0.35f, W2 = 2.f * PI * 3.f, Z2 = 0.3f;
		for (int32 i = 0; i < N; ++i)
		{
			ImpOmega += (-W1 * W1 * ImpTheta - 2.f * Z1 * W1 * ImpOmega) * H;
			ImpTheta += ImpOmega * H;
			HeadOmega += (-W2 * W2 * HeadTheta - 2.f * Z2 * W2 * HeadOmega) * H;
			HeadTheta += HeadOmega * H;
		}
		ImpTheta = ImpTheta.GetClampedToMaxSize(0.38f);
		HeadTheta = HeadTheta.GetClampedToMaxSize(0.5f);
		if (ImpTheta.SizeSquared() < 1e-8f && ImpOmega.SizeSquared() < 1e-6f) { ImpTheta = ImpOmega = FVector::ZeroVector; }
		if (HeadTheta.SizeSquared() < 1e-8f && HeadOmega.SizeSquared() < 1e-6f) { HeadTheta = HeadOmega = FVector::ZeroVector; }
	}
	if (ImpTheta.IsZero() && HeadTheta.IsZero()) return;
	const FBones B = Resolve(Output.Pose.GetBoneContainer());
	if (!B.bValid) return;
	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);
	auto Turn = [&P](FCompactPoseBoneIndex Bone, const FVector& Rv)
	{
		const float A = static_cast<float>(Rv.Size());
		if (!Bone.IsValid() || A < 1e-5f) return;
		FTransform T = P.GetComponentSpaceTransform(Bone);
		T.SetRotation((FQuat(Rv / A, A) * T.GetRotation()).GetNormalized());
		const FBoneTransform BT[] = {FBoneTransform(Bone, T)};
		P.LocalBlendCSBoneTransforms(BT, 1.f);
	};
	// the trunk bends through the spine (most low and in the middle), the head through the neck and the skull
	Turn(B.Spine1, ImpTheta * 0.35f);
	Turn(B.Spine3, ImpTheta * 0.35f);
	Turn(B.Spine5, ImpTheta * 0.3f);
	Turn(B.Neck, HeadTheta * 0.55f);
	Turn(B.Head, HeadTheta * 0.45f);
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
}

void FTagRetargetProxy::MeasurePlants(FPoseContext& Output)
{
	// on the capture's pose, before any layer: a foot is planted when it is on the turf and the capture holds it still
	// over the ground - its motion within the clip plus the clip's root travel comes to under 45 cm/s (it lets go above
	// 90, or when it lifts). The feet of a clip played in place are judged against the body (root velocity zero).
	PlantFeetFrame = 0;
	bTruthValid = false;
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("TagNoTruthPlant"));
	if (bOff || !In.bRootVelValid || Dt <= 1e-4f) { bHavePrevRaw = false; bTruthPlant[0] = bTruthPlant[1] = false; return; }
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bLegs) { bHavePrevRaw = false; return; }
	if (RefFootZ < 0.f) RefFootZ = BindPos(Output, B.BaL).Z;
	const FCompactPoseBoneIndex Ba[2] = {B.BaL, B.BaR};
	FVector Now[2];
	for (int32 i = 0; i < 2; ++i)
	{
		FTransform T = Output.Pose[Ba[i]];
		for (FCompactPoseBoneIndex P = BC.GetParentBoneIndex(Ba[i]); P.IsValid(); P = BC.GetParentBoneIndex(P)) T = T * Output.Pose[P];
		Now[i] = T.GetLocation();
	}
	// a new clip: its first pose against the last clip's is no velocity. The verdict carries over for that frame (read
	// as a foot flying off, every clip change let go of both pins and eased the feet out over 0.2 s: four fifths of the
	// live skating sat in the 3-12 frames after a change of clip, p30)
	if (In.ClipSerial != PlantSerial)
	{
		PlantSerial = In.ClipSerial;
		if (bHavePrevRaw)
		{
			for (int32 i = 0; i < 2; ++i)
			{
				bTruthPlant[i] = bTruthPlant[i] && Now[i].Z < RefFootZ + 9.f;
				PlantFeetFrame += bTruthPlant[i] ? 1 : 0;
			}
			bTruthValid = true;
		}
		PrevRawFootCS[0] = Now[0];
		PrevRawFootCS[1] = Now[1];
		bHavePrevRaw = true;
		return;
	}
	if (bHavePrevRaw)
	{
		for (int32 i = 0; i < 2; ++i)
		{
			FVector Vel = (Now[i] - PrevRawFootCS[i]) / Dt + In.RootVelCS;
			Vel.Z = 0.f;
			const float Sp = static_cast<float>(Vel.Size());
			const bool bOnTurf = Now[i].Z < RefFootZ + 7.f;
			bTruthPlant[i] = bOnTurf && Sp < (bTruthPlant[i] ? 90.f : 45.f);
			PlantFeetFrame += bTruthPlant[i] ? 1 : 0;
		}
		bTruthValid = true;
	}
	PrevRawFootCS[0] = Now[0];
	PrevRawFootCS[1] = Now[1];
	bHavePrevRaw = true;
}

void FTagRetargetProxy::ApplyFeet(FPoseContext& Output)
{
	// planted feet stay where they landed: a foot that comes down low is pinned to that spot of turf (leg IK) until
	// the clip lifts it or it has drifted too far (then the pin eases off). Keyed clips play in place while the sim
	// moves the actor, and clips sped up past their capture speed slide too; both read as skating without this.
	SlideRawFrame = SlideShownFrame = 0.f;
	TouchdownsFrame = 0;
	bool bAnyPin = In.bFootLock;
	for (const FFootLock& L : Feet) bAnyPin = bAnyPin || L.bLocked || L.ReleaseAlpha > 0.f;
	if (!bAnyPin) return; // nothing pinned and nothing to pin (a bench player, a body on the ground)
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bLegs) return;
	const FTransform& CT = GetComponentTransform();
	if (bHaveComp && FVector::DistSquared2D(CT.GetLocation(), PrevCompLoc) > 150.f * 150.f)
		for (FFootLock& L : Feet) L = FFootLock(); // teleported (new play): the plants are gone
	PrevCompLoc = CT.GetLocation();
	bHaveComp = true;
	if (RefFootZ < 0.f) RefFootZ = BindPos(Output, B.BaL).Z;
	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);
	const FCompactPoseBoneIndex Th[2] = {B.ThL, B.ThR}, Ca[2] = {B.CaL, B.CaR}, Ft[2] = {B.FtL, B.FtR}, Ba[2] = {B.BaL, B.BaR};
	const float Low = RefFootZ + 6.f;    // the ball of the foot is on the turf
	// a pin that has to pull the foot further than this lets go. (At 60 cm a let-go swept the foot 60 cm in 0.12 s: a leg
	// whipping out of its clip, the "flailing" of the 12 Nov play test.)
	const float MaxDrift = 30.f;
	for (int32 i = 0; i < 2; ++i)
	{
		// the pin is on the ball of the foot: the heel rises over a planted toe through the push-off, which a pinned
		// ankle would turn into the toe skating back
		FFootLock& L = Feet[i];
		const FVector FootCS = Pos(P, Ba[i]);
		const FVector FootW = CT.TransformPosition(FootCS);
		// planted: by the capture's own verdict where it is known (MeasurePlants), by height alone otherwise
		// (-TagHeightLock: the lock by height as before, the slide still measured on the capture's verdict - the A/B)
		static const bool bHeightLock = FParse::Param(FCommandLine::Get(), TEXT("TagHeightLock"));
		const bool bTruth = bTruthValid ? (bTruthPlant[i] && FootCS.Z < Low + 4.f) : FootCS.Z < Low;
		const bool bLow = bHeightLock ? FootCS.Z < Low : bTruth;
		if (bLow && !L.bWasLow) ++TouchdownsFrame;
		if (bTruth && L.bHadRaw) SlideRawFrame += FVector::Dist2D(FootW, L.PrevRawW);
		L.PrevRawW = FootW;
		L.bHadRaw = bTruth;
		if (!In.bFootLock)
		{
			if (L.bLocked) { L.bLocked = false; L.ReleaseOff = (L.World - FootW) * L.Alpha; L.ReleaseAlpha = 1.f; }
		}
		else if (!L.bLocked && bLow && (!L.bWasLow || L.ReleaseAlpha <= 0.f))
		{
			// the foot came down: pin it here. A foot still low after a pin let go (a shuffle, a slide of the clip) pins
			// again once the release has eased off: without that, half the low-foot frames of a game carried no pin at all
			L.bLocked = true;
			L.World = FootW;
			L.Alpha = 0.f;
			L.ReleaseAlpha = 0.f;
		}
		L.bWasLow = bLow;
		if (L.bLocked)
		{
			if (!bLow || FVector::Dist2D(L.World, FootW) > MaxDrift)
			{
				L.bLocked = false;
				L.ReleaseOff = (L.World - FootW) * L.Alpha;
				L.ReleaseAlpha = 1.f;
			}
			else L.Alpha = FMath::Min(1.f, L.Alpha + Dt * 30.f);
		}
		FVector TargetW = FootW;
		float A = 0.f;
		float Lift = 0.f;
		if (L.bLocked) { TargetW = L.World; A = L.Alpha; }
		else if (L.ReleaseAlpha > 0.f)
		{
			L.ReleaseAlpha = FMath::Max(0.f, L.ReleaseAlpha - Dt / 0.2f);
			TargetW = FootW + L.ReleaseOff * L.ReleaseAlpha;
			A = 1.f;
			// a pin let go with the foot well off where the clip now has it (a new clip planted it elsewhere, the body
			// outran the pin): the foot is picked up and set down there, a short step, not dragged over the turf. (Dragged,
			// that was four fifths of the live skating: the 3-12 frames after a change of clip, p30 / p31.)
			if (L.ReleaseOff.SizeSquared2D() > 8.f * 8.f)
				Lift = FMath::Min(8.f, static_cast<float>(L.ReleaseOff.Size2D()) * 0.35f) * FMath::Sin(PI * (1.f - L.ReleaseAlpha));
		}
		// the ground: a ball of the foot pushed under the turf (a crouched clip on a shorter body, the stride's lowered hips)
		// is lifted back onto it
		const float ZFix = FMath::Max(0.f, 0.5f - FootCS.Z);
		if (A > 0.001f || ZFix > 0.f || Lift > 0.f)
		{
			FVector TargetCS = A > 0.001f ? CT.InverseTransformPosition(TargetW) : FootCS;
			FVector NewCS = FMath::Lerp(FootCS, TargetCS, A > 0.001f ? A : 0.f);
			NewCS.Z = FootCS.Z + ZFix + Lift; // the clip owns the foot's height, above the ground (and a step's arc over it)
			const FVector AnkleCS = Pos(P, Ft[i]);
			LegIK(P, Th[i], Ca[i], Ft[i], AnkleCS + (NewCS - FootCS), 1.f); // move the ankle by the toe's correction
		}
		const FVector ShownW = CT.TransformPosition(Pos(P, Ba[i]));
		// (travel over the turf: a foot in the air on such a step is not sliding)
		const bool bOnTurf = bTruth && Lift < 2.f;
		if (bOnTurf && L.bHadShown) SlideShownFrame += FVector::Dist2D(ShownW, L.PrevShownW);
		L.PrevShownW = ShownW;
		L.bHadShown = bOnTurf;
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
}

void FTagRetargetProxy::ApplyKeepOut(FPoseContext& Output)
{
	// a hand or a forearm inside another man's head, chest or hips is pushed out to the surface and the arm solved to
	// it (eased, so an arm does not snap as bodies settle)
	if (In.KeepOutN <= 0 && KeepOff[0].IsNearlyZero(0.5) && KeepOff[1].IsNearlyZero(0.5) && KeepOffLeg[0].IsNearlyZero(0.5) && KeepOffLeg[1].IsNearlyZero(0.5)) return;
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bValid) return;
	const FTransform& CT = GetComponentTransform();
	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);
	const FCompactPoseBoneIndex U[2] = {B.UL, B.UR}, L[2] = {B.LL, B.LR}, H[2] = {B.HL, B.HR};
	bool bAny = false;
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector Sh = Pos(P, U[i]), El = Pos(P, L[i]), Hd = Pos(P, H[i]);
		FVector Push = FVector::ZeroVector;
		for (int32 k = 0; k < In.KeepOutN; ++k)
		{
			const FVector C = CT.InverseTransformPosition(FVector(In.KeepOut[k].X, In.KeepOut[k].Y, In.KeepOut[k].Z));
			const float Rr = In.KeepOut[k].W + 5.f; // the hand's own thickness
			for (const FVector& Pt : {Hd + Push, FMath::Lerp(El, Hd, 0.5f) + Push * 0.5f})
			{
				const FVector D = Pt - C;
				const float Dist = static_cast<float>(D.Size());
				if (Dist < Rr) Push += (Dist > 1.f ? D / Dist : FVector::UpVector) * (Rr - Dist);
			}
		}
		Push = Push.GetClampedToMaxSize(32.f);
		KeepOff[i] = FMath::Lerp(KeepOff[i], Push, FMath::Min(1.f, Dt * 14.f));
		if (KeepOff[i].SizeSquared() < 0.25f) continue;
		const FVector Pole = (El - 0.5f * (Sh + Hd)).GetSafeNormal();
		ArmIK(P, U[i], L[i], H[i], Hd + KeepOff[i], Pole.IsNearlyZero() ? FVector(0, -1, 0) : Pole, 1.f);
		bAny = true;
	}
	// the legs the same way: a foot or a shin inside another man in the pile (a boot through a helmet, a shin through
	// a back) is pushed out and the leg solved to it. Only for a man on the ground - a standing man's feet are the foot
	// lock's, and the bodies he is near are standing too
	if (B.bLegs)
	{
		const FCompactPoseBoneIndex Th[2] = {B.ThL, B.ThR}, Ca[2] = {B.CaL, B.CaR}, Ft[2] = {B.FtL, B.FtR};
		for (int32 i = 0; i < 2; ++i)
		{
			const FVector Kn = Pos(P, Ca[i]), Fo = Pos(P, Ft[i]);
			FVector Push = FVector::ZeroVector;
			for (int32 k = 0; k < In.KeepOutN; ++k)
			{
				const FVector C = CT.InverseTransformPosition(FVector(In.KeepOut[k].X, In.KeepOut[k].Y, In.KeepOut[k].Z));
				const float Rr = In.KeepOut[k].W + 6.f; // the boot's / shin's own thickness
				for (const FVector& Pt : {Fo + Push, FMath::Lerp(Kn, Fo, 0.5f) + Push * 0.5f})
				{
					const FVector D = Pt - C;
					const float Dist = static_cast<float>(D.Size());
					if (Dist < Rr) Push += (Dist > 1.f ? D / Dist : FVector::UpVector) * (Rr - Dist);
				}
			}
			Push = Push.GetClampedToMaxSize(30.f);
			KeepOffLeg[i] = FMath::Lerp(KeepOffLeg[i], Push, FMath::Min(1.f, Dt * 12.f));
			if (KeepOffLeg[i].SizeSquared() < 0.25f) continue;
			LegIK(P, Th[i], Ca[i], Ft[i], Fo + KeepOffLeg[i], 1.f);
			bAny = true;
		}
	}
	if (bAny) FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
}
