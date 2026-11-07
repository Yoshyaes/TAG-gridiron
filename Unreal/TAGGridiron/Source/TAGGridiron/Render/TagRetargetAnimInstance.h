// Runtime IK retargeting without an Anim Blueprint: copies the pose of a (hidden) source mesh
// that plays the mocap onto this mesh through an IK Retargeter asset (e.g. UEFN mannequin -> MetaHuman).
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "TagRetargetAnimInstance.generated.h"

class UIKRetargeter;

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

	FAnimNode_RetargetPoseFromMesh* Node = nullptr;
};

UCLASS(Transient, NotBlueprintable)
class TAGGRIDIRON_API UTagRetargetAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Retarget from Source (must tick before this mesh) using Asset. */
	void Configure(UIKRetargeter* Asset, USkeletalMeshComponent* Source);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(Transient)
	FAnimNode_RetargetPoseFromMesh RetargetNode;
};
