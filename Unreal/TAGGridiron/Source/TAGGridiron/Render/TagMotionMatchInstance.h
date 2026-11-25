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
#include "Animation/AnimNode_DeadBlending.h"
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

/** Sits between motion matching and the root offset node and reads the root motion the blend stack hands up: how far
 *  the matched body's root travels this evaluation, all playing clips blended, at the rate they play (root space). */
struct FTagRootTapNode : public FAnimNode_Base
{
	FPoseLink Source;
	FTransform Delta = FTransform::Identity;
	bool bHave = false;
	// pelvis continuity (see Evaluate): the pelvis as the blend stack gave it last evaluation, in component space, and the
	// correction being eased off
	FVector PrevPelvis = FVector::ZeroVector;
	FVector PelvisFix = FVector::ZeroVector;
	bool bPrevPelvis = false;
	float LastDt = 0.f;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override { Source.Initialize(Context); bPrevPelvis = false; PelvisFix = FVector::ZeroVector; }
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override { Source.CacheBones(Context); }
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override { LastDt = Context.GetDeltaTime(); Source.Update(Context); }
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
};

USTRUCT()
struct FTagMotionMatchProxy : public FAnimSingleNodeInstanceProxy
{
	GENERATED_BODY()

	FTagMotionMatchProxy() = default;
	FTagMotionMatchProxy(UAnimInstance* InInstance, FAnimNode_PoseSearchHistoryCollector* InHistory, FAnimNode_OffsetRootBone* InOffset,
		FAnimNode_MotionMatching* InMatch, FAnimNode_DeadBlending* InDead);

	virtual FAnimNode_Base* GetCustomRootNode() override { return History; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override { return false; } // evaluate the node graph from the root

	FAnimNode_PoseSearchHistoryCollector* History = nullptr;
	FAnimNode_OffsetRootBone* Offset = nullptr;
	FAnimNode_MotionMatching* Match = nullptr;
	/** -TagMMDeadBlend: the engine's dead blending node between the root tap and motion matching (null without the flag) */
	FAnimNode_DeadBlending* Dead = nullptr;
	FTagLocoSwitchNode Switch;
	FTagRootTapNode Tap;
	int32 SeenDbSerial = -1;
	int32 SeenForceSerial = 0;
	float ForceLeft = 0.f; // seconds of ForceInterrupt left after a database change, then back to InterruptOnDatabaseChange
	// the minimum dwell (see UTagMotionMatchInstance::MinDwell): the clip it holds and the move it was picked for
	TWeakObjectPtr<const UAnimationAsset> DwellAsset;
	FVector DwellVel = FVector::ZeroVector;
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
	float RateSpread = 1.f; // > 1: the search may play a candidate between rate / spread and rate * spread
	FFloatInterval* PlayRateSetting = nullptr; // the node's (private) PlayRate, found through reflection
	/** Speed matching: the player's ground speed (cm/s). Each update the picked clip's root speed at its play position
	 *  is measured (root motion over 0.1 s) and the clip's rate scaled so its feet cover the ground the sim covers,
	 *  within [SpeedMatchMin, SpeedMatchMax]; set SpeedMatchMax <= 1 to turn it off. Planted feet slide by the
	 *  difference between the two speeds, so this is the first line against skating whichever clip the search picks. */
	float SimSpeed = 0.f;
	float SpeedMatchMin = 0.75f, SpeedMatchMax = 1.25f;
	/** The crouched gait's ceiling: a lineman's 1.9 m/s kick slide against the pack's 1.2 m/s back-diagonal walk reads
	 *  as quick feet at 1.5x, where a sprint sped up that far would read as a cartoon (cadence, -TagMoveTest gait). */
	float CrouchSpeedMatchMax = 1.5f;
	bool bCrouchGait = false;
	/** His signature cadence (AAthlete::SigCadence) on top of the speed match: the feet a little quicker or slower than
	 *  the capture at the same ground speed; the stride warp (AAthlete::DriveMotionMatching, the clip's root travel at the
	 *  rate played) lengthens or shortens the step by as much. 1 = the capture's own. */
	float Cadence = 1.f;
	float* RateMultiplierSetting = nullptr; // the node's PlayRateMultiplier, found through reflection
	/** The player's ground velocity (cm/s, world): the minimum dwell lets a search through early when it changes. */
	FVector SimVel = FVector::ZeroVector;
	/** A clip the search picks is held at least this long (searches wait) unless the move changes by more than
	 *  much (a cut of DwellTurnDeg, a change of speed of DwellBreak cm/s: a burst, a stop) or the databases change. At the
	 *  node's 66 ms throttle alone the pick changed every 6 frames (0.2 s, p13 telemetry) under a 0.3 s blend, so two to
	 *  four clips were always mixing on the body: limbs that follow no clip. (A plain 1.5 m/s velocity difference let
	 *  nearly every search through: a route runner's acceleration and curve cross it in a few frames.) */
	float MinDwell = 0.3f, DwellBreak = 250.f, DwellTurnDeg = 30.f, BaseThrottle = 0.066f;
	float* ThrottleSetting = nullptr; // the node's SearchThrottleTime, found through reflection
	/** The man a human drives (AAthlete, this frame): the most responsive animator on the field. He searches every update,
	 *  holds a pick 0.12 s (not 0.3), lets a 15 degree turn of the stick through the dwell (not 30) and blends 0.2 s (not
	 *  0.3), so the body turns with the thumb. One man of 22 at full search rate (all 22 cost ~25 fps; one ~1).
	 *  -TagUserMMSame: he animates like the other 21; -TagUserMMThrottle= / -TagUserMMDwell= / -TagUserMMTurn= /
	 *  -TagUserMMBlend= to try others. */
	bool bUserMan = false;
	float UserThrottle = 0.f, UserMinDwell = 0.12f, UserDwellTurnDeg = 15.f, UserBlendTime = 0.2f;
	float BaseBlendTime = 0.3f;
	float* BlendTimeSetting = nullptr; // the node's BlendTime, found through reflection
	/** The next database change must pick a fresh clip (a start running out, a stop, a crouch): else the playing clip
	 *  carries on into the new set until the search finds better (a run into the sprint loops, a cut's pivot set). */
	bool bForceOnDbChange = true;
	int32 LastDbFamily = -1; // the AAthlete's last set: 0 stop / idle, 1 start, 2 pivot, 3 moving loops
	bool bLastDbCrouch = false;
	bool bLastDbBall = false; // the last pick was for a man with the ball
	/** The AAthlete's moving / standing call with hysteresis (a man easing along at walking pace flipped between the stop
	 *  and start sets every few frames, each a forced fresh pick): StillFor = seconds below the stop line. */
	bool bMovingLatch = false;
	float StillFor = 0.f;
	/** -TagMMWhy: why picks change (logged in totals every 400 switches): the last update's dwell state, time held. */
	uint8 DiagFlags = 0; // 1 move changed, 2 database changed, 4 forced window, 8 held by the dwell
	float DiagHeld = 0.f, DiagPrevTime = 0.f;
	TWeakObjectPtr<const UAnimationAsset> DiagPrev;
	float SpeedRate = 1.f;                  // the smoothed multiplier applied
	/** Bumped by the AAthlete when the playing clip cannot cover what the man is doing (its feet cover far less or far
	 *  more ground than he does, or travel another way, beyond what the play rate, the stride and the orientation warp
	 *  make up): the next search is a fresh pick, without the continuing pose's bonus. */
	int32 ForceSerial = 0;
	float ForceHoldSecs = 0.f; // how long a forced search keeps the continuing pose out (0: the default 0.15 s)
	float BallForceCool = 0.f; // (AAthlete) a carrier still on an empty-handed clip: the next fresh pick's wait
	/** Debug, copied after each worker thread update: where the pick is and what the search thought of it. */
	float MatchedTime = 0.f, MatchedCost = 0.f, MatchedWantedRate = 1.f, MatchedClipSpeed = 0.f;
	/** Which way the playing clip's root travels around its play position (component space of the source mesh, unit, 2D). */
	FVector MatchedClipDir = FVector::ZeroVector;
	/** The matched body's root as shown (world rotation): the OffsetRootBone node lets the clip's own turning carry the
	 *  body up to 60 degrees off the component's facing, so the clip's travel in the world is this rotation applied to
	 *  MatchedClipDir, not the component's. Valid while motion matching runs through the offset node. */
	FQuat RootRotW = FQuat::Identity;
	bool bRootRotValid = false;
	/** The root motion of the last evaluation (FTagRootTapNode): translation in the root's space over RootDeltaDt seconds. */
	FVector RootDelta = FVector::ZeroVector;
	float RootDeltaDt = 0.f;
	bool bRootDeltaValid = false;
	bool bMatchedContinuing = false, bMatchedLoop = false;
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

	/** (Not usable yet: the engine node asserts outside an Anim Blueprint in builds with anim tracing, so -TagMMDeadBlend
	 *  is ignored there - TagMotionMatchInstance.cpp.)
	 *  -TagMMDeadBlend (A/B, off by default): the matching node's transitions as the engine's dead blending (the outgoing
	 *  clip extrapolated with its own velocities and faded into the new one) instead of its blend stack (up to four clips
	 *  evaluated and mixed per man). The node asks for it through the graph's inertialization message, which this node
	 *  answers from above it in the update; with it the blend stack holds one clip (MaxActiveBlends 0). */
	UPROPERTY(Transient)
	FAnimNode_DeadBlending DeadNode;
};
