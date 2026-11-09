// Runtime IK retargeting without an Anim Blueprint: copies the pose of a (hidden) source mesh
// that plays the mocap onto this mesh through an IK Retargeter asset (e.g. UEFN mannequin -> MetaHuman),
// then layers procedural football motion on top (throw, ball security, catch reach, block hands,
// tackle wrap, 3-point stance) using component-space IK driven by the simulation.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "TagRetargetAnimInstance.generated.h"

class UIKRetargeter;

/** Per-frame football layer inputs (component space of the target mesh, cm). Written on the game thread. */
struct FTagProcInput
{
	float ThrowT = -1.f;       // 0..1 progress through a pass (<0: not throwing)
	float CarryAlpha = 0.f;    // ball tucked in the right arm
	float SecureAlpha = 0.f;   // two hands on the ball at the chest (QB in the pocket)
	float ReachAlpha = 0.f;    // both hands reach for ReachTarget (catch / tackle wrap / block punch)
	FVector ReachTarget = FVector::ZeroVector;
	float ReachSpread = 8.f;   // lateral half-gap between the hands
	float LeanAlpha = 0.f;     // forward drive lean (blocking / tackling), 0..1
	float StanceAlpha = 0.f;   // lineman 3-point stance
	float LookAlpha = 0.f;     // head tracks LookTarget (ball / carrier / receiver)
	FVector LookTarget = FVector::ZeroVector;
	// per-player style ("signature") - stable per player, see AAthlete::Setup
	float ArmSlot = 0.f;       // -1 sidearm .. +1 over the top
	float StyleJitter = 0.f;   // -1..1 small timing / height variation
	// transition crossfade: bump ClipSerial whenever the source clip changes; the displayed pose eases from the last
	// shown pose into the new one over BlendTime (single node playback cannot blend on its own)
	int32 ClipSerial = 0;
	float BlendTime = 0.2f;
};

USTRUCT()
struct FTagRetargetProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FTagRetargetProxy() = default;
	FTagRetargetProxy(UAnimInstance* InInstance, FAnimNode_RetargetPoseFromMesh* InNode)
		: FAnimInstanceProxy(InInstance), Node(InNode)
	{
	}

	virtual FAnimNode_Base* GetCustomRootNode() override { return Node; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override { OutNodes.Add(Node); }
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	FAnimNode_RetargetPoseFromMesh* Node = nullptr;
	FTagProcInput In;

private:
	void ApplyFootballLayer(FPoseContext& Output) const;
	void ApplyCrossfade(FPoseContext& Output);

	TArray<FTransform> LastPose;   // last displayed local pose (compact order)
	TArray<FTransform> FromPose;   // frozen pose a transition blends out of
	int32 SeenSerial = 0;
	float BlendElapsed = 1e3f;
	float CurBlendTime = 0.f;      // > 0 overrides In.BlendTime for the current blend (pose snapshots)
	TArray<FTransform> PendingMesh; // snapshot in mesh bone order, converted to compact order in Evaluate
	bool bPendingSnap = false;
	int32 SeenSnap = 0;
};

UCLASS(Transient, NotBlueprintable)
class TAGGRIDIRON_API UTagRetargetAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Retarget from Source (must tick before this mesh) using Asset. */
	void Configure(UIKRetargeter* Asset, USkeletalMeshComponent* Source);

	/** Football layer inputs; copied to the worker-thread proxy each update. */
	FTagProcInput Proc;

	/** Blend the displayed body out of an explicit pose (local transforms, mesh bone order), e.g. the ragdoll pose
	 *  a get-up starts from. Bump SnapshotSerial after filling SnapshotMesh. */
	TArray<FTransform> SnapshotMesh;
	int32 SnapshotSerial = 0;
	float SnapshotBlend = 0.35f;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(Transient)
	FAnimNode_RetargetPoseFromMesh RetargetNode;
};
