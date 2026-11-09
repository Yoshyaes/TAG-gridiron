#include "Render/TagFaceAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"

namespace
{
	// MetaHuman face rig expression controls (Face_Archetype_Skeleton), evaluated by RigLogic in the post-process ABP
	struct FCurveNames
	{
		FName BlinkL{TEXT("CTRL_expressions_eyeBlinkL")}, BlinkR{TEXT("CTRL_expressions_eyeBlinkR")};
		FName JawOpen{TEXT("CTRL_expressions_jawOpen")};
		FName BrowDownL{TEXT("CTRL_expressions_browDownL")}, BrowDownR{TEXT("CTRL_expressions_browDownR")};
		FName BrowInL{TEXT("CTRL_expressions_browRaiseInL")}, BrowInR{TEXT("CTRL_expressions_browRaiseInR")};
		FName SquintL{TEXT("CTRL_expressions_eyeSquintInnerL")}, SquintR{TEXT("CTRL_expressions_eyeSquintInnerR")};
		FName NoseL{TEXT("CTRL_expressions_noseWrinkleL")}, NoseR{TEXT("CTRL_expressions_noseWrinkleR")};
		FName StretchL{TEXT("CTRL_expressions_mouthStretchL")}, StretchR{TEXT("CTRL_expressions_mouthStretchR")};
		FName PressL{TEXT("CTRL_expressions_mouthLipsPressL")}, PressR{TEXT("CTRL_expressions_mouthLipsPressR")};
		FName SmileL{TEXT("CTRL_expressions_mouthCornerPullL")}, SmileR{TEXT("CTRL_expressions_mouthCornerPullR")};
	};
	const FCurveNames& Names()
	{
		static FCurveNames N;
		return N;
	}
}

void FTagFaceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	In = CastChecked<UTagFaceAnimInstance>(InAnimInstance)->Face;
}

bool FTagFaceProxy::Evaluate(FPoseContext& Output)
{
	if (Node) Node->Evaluate_AnyThread(Output);
	else Output.ResetToRefPose();
	const FCurveNames& N = Names();
	auto Set = [&Output](const FName& L, const FName& R, float V)
	{
		V = FMath::Clamp(V, 0.f, 1.f);
		Output.Curve.Set(L, V);
		Output.Curve.Set(R, V);
	};
	const float E = In.Effort, F = In.Focus, S = In.Shout;
	Set(N.BlinkL, N.BlinkR, In.Blink);
	Output.Curve.Set(N.JawOpen, FMath::Clamp(In.Breath * 0.18f + E * 0.10f + S * 0.55f, 0.f, 1.f));
	Set(N.BrowDownL, N.BrowDownR, E * 0.65f + F * 0.30f);
	Set(N.BrowInL, N.BrowInR, S * 0.35f);
	Set(N.SquintL, N.SquintR, E * 0.55f + F * 0.20f);
	Set(N.NoseL, N.NoseR, E * 0.35f);
	Set(N.StretchL, N.StretchR, E * 0.45f + S * 0.20f);
	Set(N.PressL, N.PressR, F * 0.40f);
	Set(N.SmileL, N.SmileR, In.Smile * 0.8f + S * 0.4f);
	return true;
}

void UTagFaceAnimInstance::Configure(USkeletalMeshComponent* Body)
{
	CopyNode.SourceMeshComponent = Body;
	CopyNode.bCopyCurves = false;
	CopyNode.bUseAttachedParent = false;
	InitializeAnimation();
}

FAnimInstanceProxy* UTagFaceAnimInstance::CreateAnimInstanceProxy()
{
	return new FTagFaceProxy(this, &CopyNode);
}
