// The hidden source body's anim instance: single node playback for keyed clips and blend spaces (the AAthlete API is
// unchanged: GetSingleNodeInstance, SetPosition, SetPlayRate, SetBlendSpacePosition), plus motion matching over Epic's
// Game Animation Sample PoseSearch databases for open field locomotion (starts, stops, plants, cuts, backpedals).
//
// Graph (no Anim Blueprint): PoseHistory collector -> switch -> { single node clip | OffsetRootBone -> MotionMatching }.
// The history collector sits above the switch so it keeps recording the pose through keyed clips, and motion matching
// picks up from wherever the last clip left the body (juke -> run, get-off -> run, stop -> stance).
// OffsetRootBone lets the matched clip's root motion drive the visible turn and stride (a plant-and-go turns the body
// over the clip's own steps instead of snapping with the sim's facing), easing back onto the sim's position and
// facing and never drifting more than a set distance / angle from them.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Animation/TrajectoryTypes.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "TagMotionMatchInstance.generated.h"

class UPoseSearchDatabase;

/** Picks the single node clip or motion matching (only the active branch updates and evaluates). */
struct FTagLocoSwitchNode : public FAnimNode_Base
{
	FPoseLink Clip;
	FPoseLink Match;
	bool bMatch = false;
	bool bWasMatch = false; // re-initialize the matching branch when it becomes active (no stale root offset)

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};

USTRUCT()
struct FTagMotionMatchProxy : public FAnimSingleNodeInstanceProxy
{
	GENERATED_BODY()

	FTagMotionMatchProxy() = default;
	FTagMotionMatchProxy(UAnimInstance* InInstance, FAnimNode_PoseSearchHistoryCollector* InHistory, FAnimNode_OffsetRootBone* InOffset,
		FAnimNode_MotionMatching* InMatch);

	virtual FAnimNode_Base* GetCustomRootNode() override { return History; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override { return false; } // evaluate the node graph from the root

	FAnimNode_PoseSearchHistoryCollector* History = nullptr;
	FAnimNode_OffsetRootBone* Offset = nullptr;
	FAnimNode_MotionMatching* Match = nullptr;
	FTagLocoSwitchNode Switch;
	int32 SeenDbSerial = -1;
};

UCLASS(Transient, NotBlueprintable)
class TAGGRIDIRON_API UTagMotionMatchInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	UTagMotionMatchInstance(const FObjectInitializer& ObjectInitializer);

	// game thread inputs, copied to the worker thread proxy in PreUpdate
	bool bMatch = false;               // true: motion matching drives the body; false: the single node clip
	FTransformTrajectory Trajectory;   // world space trajectory of this skeletal mesh component (history, now, prediction)
	/** < 1 when the player moves faster than the capture: the query is slowed to the clips' speed and the picked clip
	 *  plays 1 / multiplier faster (PoseSearch scales the query; the search itself never accounts for play rate). */
	float TrajectorySpeedMultiplier = 1.f;
	/** Play rate of the matched clip. Keep it near 1: the pose history records the body's real bone velocities, so a
	 *  clip played much faster than captured no longer matches its own continuation and the search churns. */
	float MatchPlayRate = 1.f;
	FFloatInterval* PlayRateSetting = nullptr; // the node's (private) PlayRate, found through reflection
	/** Databases to search (changes take effect next update; the continuing pose is kept when it is still valid). */
	void SetDatabases(TConstArrayView<UPoseSearchDatabase*> InDatabases);

	/** Debug: the animation motion matching is playing (copied after each worker thread update). */
	const UAnimationAsset* GetMatchedAsset() const { return MatchedAsset.Get(); }
	TWeakObjectPtr<const UAnimationAsset> MatchedAsset;

	TArray<UPoseSearchDatabase*> Databases;
	int32 DbSerial = 0;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	template <class T> void SetMatchSetting(const TCHAR* Name, const T& Value);

	UPROPERTY(Transient)
	FAnimNode_PoseSearchHistoryCollector HistoryNode;

	UPROPERTY(Transient)
	FAnimNode_OffsetRootBone OffsetNode;

	UPROPERTY(Transient)
	FAnimNode_MotionMatching MatchNode;
};
