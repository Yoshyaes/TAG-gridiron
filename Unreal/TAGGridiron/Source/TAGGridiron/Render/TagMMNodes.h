// The motion matching and root offset nodes of the athletes' C++ graph (UTagMotionMatchInstance), self-hosted.
//
// The engine's FAnimNode_MotionMatching and FAnimNode_OffsetRootBone keep some settings as "folded" properties: in an
// editor build a node with no Anim Blueprint behind it reads them off itself (FAnimNode_Base::GetData, WITH_EDITORONLY_DATA
// path), but in a packaged game those properties do not exist and every read goes through the node data an Anim
// Blueprint's generated class hands its nodes (check(NodeData), AnimNodeBase.h:851). A node built in C++ has none, and
// only UAnimBlueprintGeneratedClass may give it one (FAnimNode_Base::SetNodeData is private to it). The first matched
// update of the cooked game asserted there (MotionMatching reads its blend space / sync group / update function settings
// on every update, OffsetRootBone all of its settings).
//
// These two are the engine's nodes (UE 5.8, PoseSearch AnimNode_MotionMatching.cpp, AnimationWarping
// AnimNode_OffsetRootBone.cpp) with the folded settings as plain members at the values the editor build used - the
// defaults, and the offset settings UTagMotionMatchInstance set - so the editor game moves as before and the packaged game
// needs no node data. Left out: the multi-character interaction warp (bEnableWarp, off), collision testing (disabled),
// the debug draw / visual logger / rewind-debugger trace. -TagOldMMHost: the engine's own nodes again (editor builds only).
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchEvent.h"
#include "PoseSearch/PoseSearchInteractionAvailability.h"
#include "TagMMNodes.generated.h"

class UPoseSearchDatabase;
class UBlendProfile;

USTRUCT()
struct FTagMotionMatchNode : public FAnimNode_BlendStack_Standalone
{
	GENERATED_BODY()

	// FAnimNode_MotionMatching's settings (all of them plain properties there too)
	float BlendTime = 0.2f;
	UPROPERTY(Transient)
	TObjectPtr<UBlendProfile> BlendProfile = nullptr;
	EAlphaBlendOption BlendOption = EAlphaBlendOption::Linear;
	FFloatInterval PoseJumpThresholdTime = FFloatInterval(0.f, 0.f);
	float PoseReselectHistory = 0.3f;
	float SearchThrottleTime = 0.f;
	FFloatInterval PlayRate = FFloatInterval(1.f, 1.f);
	float PlayRateMultiplier = 1.f;
	bool bUseInertialBlend = false;
	bool bResetOnBecomingRelevant = true;
	bool bShouldSearch = true;

	void SetDatabasesToSearch(TConstArrayView<UPoseSearchDatabase*> InDatabases, EPoseSearchInterruptMode InterruptMode);
	const FMotionMatchingState& GetMotionMatchingState() const { return MotionMatchingState; }

	// FAnimNode_Base / asset player interface (the folded ones answer the engine node's defaults)
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual bool GetIgnoreForRelevancyTest() const override { return false; }
	virtual bool SetIgnoreForRelevancyTest(bool) override { return false; }
	virtual FName GetGroupName() const override { return NAME_None; }
	virtual EAnimGroupRole::Type GetGroupRole() const override { return EAnimGroupRole::ExclusiveAlwaysLeader; }
	virtual EAnimSyncMethod GetGroupMethod() const override { return EAnimSyncMethod::DoNotSync; }
	virtual bool GetOverridePositionWhenJoiningSyncGroupAsLeader() const override { return true; }
	virtual bool IsLooping() const override { return !AnimPlayers.IsEmpty() && AnimPlayers[0].IsLooping(); }

private:
	UPROPERTY(Transient)
	FMotionMatchingState MotionMatchingState;
	FGraphTraversalCounter UpdateCounter;
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UPoseSearchDatabase>> DatabasesToSearch;
	EPoseSearchInterruptMode NextUpdateInterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	FPoseSearchEvent EventToSearch;
	TArray<FPoseSearchInteractionAvailability> Availabilities;
};

USTRUCT()
struct FTagOffsetRootNode : public FAnimNode_Base
{
	GENERATED_BODY()

	FPoseLink Source;

	// FAnimNode_OffsetRootBone's folded settings (graph driven, no collision testing, on the ground)
	EOffsetRootBoneMode TranslationMode = EOffsetRootBoneMode::Interpolate;
	EOffsetRootBoneMode RotationMode = EOffsetRootBoneMode::Interpolate;
	float TranslationHalflife = 0.1f;
	float RotationHalfLife = 0.2f;
	float MaxTranslationError = -1.f;
	float MaxRotationError = -1.f;
	bool bResetOnTeleport = false;
	bool bOnGround = true;
	FVector GroundNormal = FVector(0, 0, 1);

	const FQuat& GetOffsetRootRotation() const { return SimulatedRotation; }

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;

private:
	void Reset(const FAnimationBaseContext& Context);

	FAnimInstanceProxy* AnimInstanceProxy = nullptr;
	float CachedDeltaTime = 0.f;
	FGraphTraversalCounter UpdateCounter;
	bool bIsFirstUpdate = true;
	FTransform ComponentTransform = FTransform::Identity;
	FVector SimulatedTranslation = FVector::ZeroVector;
	FQuat SimulatedRotation = FQuat::Identity;
};
