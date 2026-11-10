#include "Render/TagRetargetAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TagRetargetAnimInstance)

FAnimInstanceProxy* UTagRetargetAnimInstance::CreateAnimInstanceProxy()
{
	return new FTagRetargetProxy(this, &RetargetNode);
}

void UTagRetargetAnimInstance::Configure(UIKRetargeter* Asset, USkeletalMeshComponent* Source)
{
	RetargetNode.IKRetargeterAsset = Asset;
	RetargetNode.RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	RetargetNode.SourceMeshComponent = Source;
	RetargetNode.bSuppressWarnings = false;
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

bool FTagRetargetProxy::Evaluate(FPoseContext& Output)
{
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
		return true;
	}
	Node->Evaluate_AnyThread(Output); // mocap / keyed clip retargeted onto this body
	ApplyFootballLayer(Output);
	ApplyStride(Output);
	ApplyLean(Output);
	ApplyCrossfade(Output);
	ApplyFeet(Output); // on the shown pose, after the blend (a blended foot would slide through every transition)
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
		In.LeanAlpha > 0.01f || In.StanceAlpha > 0.01f || In.LookAlpha > 0.01f;
	if (!bAny) return;
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FBones B = Resolve(BC);
	if (!B.bValid) return;

	FCSPose<FCompactPose> P;
	P.InitPose(Output.Pose);

	// --- torso: drive lean (blocks/tackles) and 3-point stance flatten the back (rotate about +X = lateral)
	const float LeanDeg = 16.f * In.LeanAlpha + 34.f * In.StanceAlpha;
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
			ArmIK(P, B.UL, B.LL, B.HL, C + Side * Spread, PoleL, In.ReachAlpha);
			ArmIK(P, B.UR, B.LR, B.HR, C - Side * Spread, PoleR, In.ReachAlpha * (1.f - In.CarryAlpha));
		}
		// --- 3-point stance: down hand on the turf in front of the right foot
		if (In.StanceAlpha > 0.01f)
		{
			const FVector Down = FVector(-18.f, 52.f, 3.f);
			ArmIK(P, B.UR, B.LR, B.HR, Down, FVector(-0.5f, -0.2f, 0.8f).GetSafeNormal(), In.StanceAlpha);
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
		Want.Z = FMath::Clamp(static_cast<float>(Want.Z), -Horiz * 0.58f, Horiz * 1.0f);
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
	if (S < 1.005f) return;
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
	Pel.SetLocation(PelPos - FVector(0, 0, (S - 1.f) * 22.f));
	const FBoneTransform BT[] = {FBoneTransform(B.Pelvis, Pel)};
	P.LocalBlendCSBoneTransforms(BT, 1.f);
	const FVector NL = FL + Dir * FVector::DotProduct(FL - PelPos, Dir) * (S - 1.f);
	const FVector NR = FR + Dir * FVector::DotProduct(FR - PelPos, Dir) * (S - 1.f);
	LegIK(P, B.ThL, B.CaL, B.FtL, NL, 1.f);
	LegIK(P, B.ThR, B.CaR, B.FtR, NR, 1.f);
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
	const float MaxDrift = 36.f;         // a pin that has to pull the foot further than this lets go
	for (int32 i = 0; i < 2; ++i)
	{
		// the pin is on the ball of the foot: the heel rises over a planted toe through the push-off, which a pinned
		// ankle would turn into the toe skating back
		FFootLock& L = Feet[i];
		const FVector FootCS = Pos(P, Ba[i]);
		const FVector FootW = CT.TransformPosition(FootCS);
		const bool bLow = FootCS.Z < Low;
		if (bLow && !L.bWasLow) ++TouchdownsFrame;
		if (bLow && L.bHadRaw) SlideRawFrame += FVector::Dist2D(FootW, L.PrevRawW);
		L.PrevRawW = FootW;
		L.bHadRaw = bLow;
		if (!In.bFootLock)
		{
			if (L.bLocked) { L.bLocked = false; L.ReleaseOff = (L.World - FootW) * L.Alpha; L.ReleaseAlpha = 1.f; }
		}
		else if (!L.bLocked && bLow && !L.bWasLow)
		{
			L.bLocked = true; // the foot came down: pin it here
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
			else L.Alpha = FMath::Min(1.f, L.Alpha + Dt * 14.f);
		}
		FVector TargetW = FootW;
		float A = 0.f;
		if (L.bLocked) { TargetW = L.World; A = L.Alpha; }
		else if (L.ReleaseAlpha > 0.f)
		{
			L.ReleaseAlpha = FMath::Max(0.f, L.ReleaseAlpha - Dt / 0.12f);
			TargetW = FootW + L.ReleaseOff * L.ReleaseAlpha;
			A = 1.f;
		}
		if (A > 0.001f)
		{
			FVector TargetCS = CT.InverseTransformPosition(TargetW);
			TargetCS.Z = FootCS.Z; // the clip owns the foot's height
			const FVector AnkleCS = Pos(P, Ft[i]);
			LegIK(P, Th[i], Ca[i], Ft[i], AnkleCS + (FMath::Lerp(FootCS, TargetCS, A) - FootCS), 1.f); // move the ankle by the toe's correction
		}
		const FVector ShownW = CT.TransformPosition(Pos(P, Ba[i]));
		if (bLow && L.bHadShown) SlideShownFrame += FVector::Dist2D(ShownW, L.PrevShownW);
		L.PrevShownW = ShownW;
		L.bHadShown = bLow;
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(P), Output.Pose);
}
