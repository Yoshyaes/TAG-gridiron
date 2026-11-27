// MetaHuman face for an athlete: copies the visible body's pose (head and neck follow the body) and drives the face
// rig's expression controls (CTRL_expressions_* curves) from what the player is doing. The face mesh's post-process
// anim blueprint (Epic's RigLogic) turns those curves into the facial joints, so the face blinks, breathes, grimaces
// through contact, focuses before the snap and shouts in celebration. Replaces leader-pose following, which never
// evaluated the face rig (faces were frozen in the neutral pose).
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "TagFaceAnimInstance.generated.h"

/** Expression drive, 0..1, written on the game thread each frame (AAthlete::UpdateFace). */
struct FTagFaceInput
{
	float Blink = 0.f;      // eyelids
	float Effort = 0.f;     // grimace: brows down, squint, nose wrinkle, mouth stretched (sprinting, contact)
	float Breath = 0.f;     // jaw drop with each breath (heavier after running)
	float Focus = 0.f;      // pre-snap: brows set, lips pressed
	float Shout = 0.f;      // celebration: open mouth, smile
	float Smile = 0.f;
};

USTRUCT()
struct FTagFaceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FTagFaceProxy() = default;
	FTagFaceProxy(UAnimInstance* InInstance, FAnimNode_CopyPoseFromMesh* InNode) : FAnimInstanceProxy(InInstance), Node(InNode) {}

	virtual FAnimNode_Base* GetCustomRootNode() override { return Node; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override { OutNodes.Add(Node); }
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	FAnimNode_CopyPoseFromMesh* Node = nullptr;
	FTagFaceInput In;
};

UCLASS(Transient, NotBlueprintable)
class TAGGRIDIRON_API UTagFaceAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Follow Body (the visible MetaHuman body) for the head and neck. */
	void Configure(USkeletalMeshComponent* Body);

	FTagFaceInput Face;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(Transient)
	FAnimNode_CopyPoseFromMesh CopyNode;
};
