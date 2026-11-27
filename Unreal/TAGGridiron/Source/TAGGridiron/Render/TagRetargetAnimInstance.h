// Runtime pose transfer without an Anim Blueprint: copies the pose of a (hidden) source mesh that plays the mocap onto
// this mesh, bone for bone when both are on the mocap's own skeleton (the model pack bodies under the hero rig: the
// capture as captured), else through an IK Retargeter asset (e.g. UEFN mannequin -> MetaHuman); then layers procedural football motion on top (throw, ball security, catch reach, block hands,
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
	float CarryLeanDeg = 7.f;  // a ball carrier's pad level added over the capture (0 on the mocap's own skeleton)
	float StanceAlpha = 0.f;   // lineman 3-point stance (torso and hand, the procedural fallback)
	float StanceHandAlpha = 0.f; // the down hand planted on the turf over the keyed stance (the rig's arm is short)
	bool bStanceBothHands = false; // a four point stance
	float LookAlpha = 0.f;     // head tracks LookTarget (ball / carrier / receiver)
	FVector LookTarget = FVector::ZeroVector;
	float LookUpMaxDeg = 20.f; // how far over the horizon his face may turn (a clip's head tipped up at the stands is brought down to it)
	// per-player style ("signature") - stable per player, see AAthlete::Setup
	float ArmSlot = 0.f;       // -1 sidearm .. +1 over the top
	float StyleJitter = 0.f;   // -1..1 small timing / height variation
	// transition crossfade: bump ClipSerial whenever the source clip changes; the displayed pose eases from the last
	// shown pose into the new one over BlendTime (single node playback cannot blend on its own). The pose blended out of
	// keeps moving the way it was moving (dead blending: each joint's turn rate at the switch, decaying with a half-life
	// of DeadBlendHalfLife s), so an arm in mid swing carries its swing into the new clip instead of easing out of a pose
	// that stopped dead. -TagFrozenFade: the old frozen pose; -TagDeadBlendHL=<s> to try other half-lives.
	int32 ClipSerial = 0;
	float BlendTime = 0.2f;
	// body physics (AAthlete::Drive): the whole body leans from the ankles into its measured acceleration (starts,
	// cuts, braking, the centripetal lean of a curved run), as a horizontal component-space direction scaled by the
	// lean angle in degrees; and planted feet are pinned to the turf (keyed clips play in place while the sim moves
	// the actor, and the capture's top speed is below a sprinter's: both slide the feet otherwise)
	FVector LeanCS = FVector::ZeroVector;
	bool bFootLock = false;
	// stride warping: above the speed the capture covers (7 m/s sprint x the 1.3 play-rate cap) the stride is
	// stretched along the direction of travel instead of letting the feet skate (NFL ball carriers reach 10 m/s)
	float StrideScale = 1.f;
	FVector StrideDirCS = FVector(0, 1, 0);
	// orientation warping: the hips and legs turn (degrees, about the vertical) so the clip's steps go where the man
	// actually travels (a back-right jog under a man drifting straight back skated its planted foot sideways); the
	// spine turns back by the same angle, so the chest and the eyes keep their facing
	float OrientWarpDeg = 0.f;
	// the playing clip's root velocity in this mesh's component space (cm/s; zero for a clip played in place), and
	// whether it is known this frame. With it the layer can tell a foot the capture holds on the turf (its motion in
	// the clip plus the root's travel is ~0) from one that is merely low: a walking swing foot clears the ground by
	// 4-6 cm, and by height alone it was "planted" - pinned in mid swing and counted as a sliding plant.
	FVector RootVelCS = FVector::ZeroVector;
	bool bRootVelValid = false;
	// bodies in a pile: the heads, chests and hips of the men he is down with (world space, w = radius, cm). His hands
	// and forearms are kept out of them (the captures were of two men; a third man's arm went through a helmet)
	FVector4f KeepOut[12];
	int32 KeepOutN = 0;
	// the pose being blended out of sits a travelling capture's distance from the actor, and the actor has since been
	// moved there: the blend takes the new clip's root and hips position at once (the limbs still blend)
	bool bDropFromTravel = false;
	// contact: the velocity change the body took from a collision this frame (cm/s, this mesh's component space), handed
	// over once per ImpactSerial. The hips go with the push; the upper body and the head are left behind and come back on
	// damped springs (ApplyImpact) - the jolt of two bodies meeting that no capture of one man has.
	FVector ImpactCS = FVector::ZeroVector;
	int32 ImpactSerial = 0;
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
	virtual void PostUpdate(UAnimInstance* InAnimInstance) const override;
	virtual bool Evaluate(FPoseContext& Output) override;

	FAnimNode_RetargetPoseFromMesh* Node = nullptr;
	/** Copy mode (no retargeter): the source's component-space pose, in this mesh's bone order (game thread copy). */
	bool bCopy = false;
	TArray<FTransform> CopyCS;
	TArray<uint8> CopyHave;
	FTagProcInput In;
	/** Planted-foot travel this evaluation (cm): in the animation as retargeted, and as shown after the foot lock. */
	float SlideRawFrame = 0.f, SlideShownFrame = 0.f;
	int32 TouchdownsFrame = 0;
	int32 PlantFeetFrame = 0; // feet the capture holds planted this evaluation (0-2)
	/** How far the shown pose is from the capture as played (cm, mean over the eleven joints a pose reads by, component
	 *  space): [0] in all, then what each layer moved: football layer, orientation, stride, lean, crossfade, foot lock.
	 *  Measured only in telemetry runs (-TagHlFull / -TagDev). */
	float DevFrame[7] = {0, 0, 0, 0, 0, 0, 0};
	/** The arms against the capture as played (telemetry runs only; MeasureArms), left then right:
	 *  [0,1] hand off the capture's in the chest's frame after every layer (cm), [2,3] what the football layer alone moved it
	 *  (cm), [4,5] elbow bend the football layer put on the capture's (deg), [6,7] the shown arm's fastest segment turn in the chest's frame
	 *  (deg/s), [8,9] the capture's own, [10] the layers' jitter: the largest second difference of any of the eleven
	 *  joints' offset from the capture (cm per frame per frame). */
	float ArmFrame[17] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // [11,12] the hand's twist about the forearm L/R, [13,14] the upper arm's own twist L/R (deg, as shown: what ApplyTwist shares out)

private:
	void MeasureArms(const FPoseContext& Output, int32 Stage);
	FTransform ArmT[3][7];                // the chest and both arms (upper, fore, hand) as copied, after the football layer, as shown
	FVector ArmDirPrev[2][2][2];          // [shown/capture][side][upper/fore]: the segments' last directions in the chest's frame
	bool bArmPrev = false;
	FVector JitOff[2][11];                // the eleven joints' offsets from the capture, the last two evaluations
	int32 JitHave = 0;
	FVector DevCap[11];
	FQuat HeadPrev[2] = {FQuat::Identity, FQuat::Identity}; // the head in the chest's frame last evaluation: shown, captured
	FQuat HeadCap = FQuat::Identity;
	bool bHeadPrev = false;                   // the eleven joints as copied (this evaluation)
	void ApplyFootballLayer(FPoseContext& Output) const;
	void ApplyStride(FPoseContext& Output) const;
	void ApplyOrient(FPoseContext& Output) const;
	void ApplyLean(FPoseContext& Output) const;
	void ApplyFeet(FPoseContext& Output);
	void ApplyKeepOut(FPoseContext& Output);
	void ApplyImpact(FPoseContext& Output);
	FVector ImpTheta = FVector::ZeroVector, ImpOmega = FVector::ZeroVector;   // the torso's tilt off the pose (rad, a rotation vector in component space) and its rate
	FVector HeadTheta = FVector::ZeroVector, HeadOmega = FVector::ZeroVector; // the head's own (it whips further and faster)
	int32 SeenImpact = 0;
	FVector KeepOff[2] = {FVector::ZeroVector, FVector::ZeroVector}; // each hand's push, eased
	FVector KeepOffLeg[2] = {FVector::ZeroVector, FVector::ZeroVector}; // each foot's push, eased
	void ApplyCrossfade(FPoseContext& Output);
	void EvaluateCopy(FPoseContext& Output);
	TArray<int32> CopyMap;              // this mesh's bone -> the source mesh's bone (by name), INDEX_NONE where it has none
	const void* CopySrcMesh = nullptr;
	const void* CopyDstMesh = nullptr;
	/** Model pack bodies: the forearm / calf twist helpers take their share of the hand / foot turn and the upper arm /
	 *  thigh helpers hold back the limb's own turn near the shoulder / hip (the pack rig's skinning expects it). */
	void ApplyTwist(FPoseContext& Output);
	struct FTwistBone { int32 Bone = INDEX_NONE, Parent = INDEX_NONE, Ref = INDEX_NONE; float Along = 0.f; bool bCounter = false; };
	TArray<FTwistBone> Twists;      // mesh pose indices, resolved once per mesh
	const void* TwistMesh = nullptr;
	struct FFootLock
	{
		bool bLocked = false, bWasLow = false, bHadRaw = false, bHadShown = false;
		FVector World = FVector::ZeroVector;      // where the foot planted (world)
		float Alpha = 0.f;                        // lock eased in
		FVector ReleaseOff = FVector::ZeroVector; // offset blending out after a release
		float ReleaseAlpha = 0.f;
		FVector PrevRawW = FVector::ZeroVector, PrevShownW = FVector::ZeroVector;
	};
	FFootLock Feet[2];
	/** The capture's own verdict on each foot (MeasurePlants, on the pose as copied, before any layer). */
	void MeasurePlants(FPoseContext& Output);
	bool bTruthPlant[2] = {false, false};
	bool bTruthValid = false;
	FVector PrevRawFootCS[2] = {FVector::ZeroVector, FVector::ZeroVector};
	bool bHavePrevRaw = false;
	int32 PlantSerial = 0; // the clip the verdict was last taken on
	FVector PrevCompLoc = FVector::ZeroVector;
	bool bHaveComp = false;
	float Dt = 0.f;
	float RefFootZ = -1.f;  // the foot bone's height in the reference pose (component space), computed on first use

	TArray<FTransform> LastPose;   // last displayed local pose (compact order)
	TArray<FTransform> PrevPose;   // the one shown an evaluation before it (with LastPose: each joint's turn rate)
	float LastPoseDt = 0.f;        // seconds between the two
	bool bPrevPoseValid = false;
	TArray<FTransform> FromPose;   // pose a transition blends out of (as it was at the switch)
	TArray<FVector> FromAngVel;    // each bone's local turn rate at the switch (rad/s, rotation vector; dead blending)
	bool bFromVel = false;
	int32 SeenSerial = 0;
	/** xfj (-TagHlFull / -TagDev): how much a transition jerks the joints - the mean acceleration of the eleven DevJoints
	 *  (second difference, cm/s2) over the 0.25 s after a switch over the same man's mean over the 0.25 s before. Kind 0:
	 *  a ClipSerial bump (into or out of a single clip), 1: motion matching changing its pick (the source's matched clip).
	 *  1 = the switch cannot be seen in the joints' motion; a frozen fade shows as a dip then a spike. */
	void MeasureJerk(FPoseContext& Output);
	FVector JerkJ[2][11];
	float JerkDt[2] = {0.f, 0.f};
	int32 JerkHave = 0;
	float JerkRingA[48] = {}, JerkRingT[48] = {}; // the last evaluations' joint acceleration and when (JerkClock)
	int32 JerkRingHead = 0, JerkRingCount = 0;
	float JerkClock = 0.f;
	int32 JerkSerial = 0, JerkPickSerial = 0; // the switches seen
	bool bJerkPost = false;
	uint8 JerkKind = 0;
	float JerkPostT0 = 0.f, JerkPre = 0.f;
	double JerkPostSum = 0.0;
	int32 JerkPostN = 0;
	mutable TArray<FVector2f> JerkOutFrame; // finished since the last PostUpdate: (xfj, kind)
	TWeakObjectPtr<const UAnimationAsset> JerkPickAsset; // the source's matched clip last seen (game thread, PreUpdate)
	int32 PickSerialIn = 0;
	float BlendElapsed = 1e3f;
	float CurBlendTime = 0.f;      // > 0 overrides In.BlendTime for the current blend (pose snapshots)
	TArray<FTransform> CsFrom, CsTo; // component-space scratch for a snapshot blend
	TArray<FTransform> PendingMesh; // snapshot in mesh bone order, converted to compact order in Evaluate
	bool bPendingSnap = false;
	TArray<FTransform> ReplayMesh;  // instant replay: shown as is (mesh bone order)
	bool bReplay = false;
	int32 SeenSnap = 0;
};

UCLASS(Transient, NotBlueprintable)
class TAGGRIDIRON_API UTagRetargetAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Retarget from Source (must tick before this mesh) using Asset; with no Asset the pose is copied bone for bone by
	 *  name (in component space, so a rig with fewer bones in between still lands every bone where the source has it). */
	void Configure(UIKRetargeter* Asset, USkeletalMeshComponent* Source);
	TWeakObjectPtr<USkeletalMeshComponent> CopySource;
	bool bCopy = false;

	/** Football layer inputs; copied to the worker-thread proxy each update. */
	FTagProcInput Proc;

	/** Blend the displayed body out of an explicit pose (local transforms, mesh bone order), e.g. the ragdoll pose
	 *  a get-up starts from. Bump SnapshotSerial after filling SnapshotMesh. */
	TArray<FTransform> SnapshotMesh;
	int32 SnapshotSerial = 0;
	float SnapshotBlend = 0.35f;

	/** Instant replay: while bReplay, the body shows ReplayMesh (local transforms, mesh bone order) instead of the
	 *  retargeted animation and football layer. */
	TArray<FTransform> ReplayMesh;
	bool bReplay = false;

	/** Foot slide accumulated since the game thread last cleared it (cm): the animation's and the shown body's. */
	float SlideRaw = 0.f, SlideShown = 0.f;
	int32 PlantFeet = 0; // feet the capture held planted in the last evaluation
	float Dev[7] = {0, 0, 0, 0, 0, 0, 0}; // the last evaluation's distance from the capture (FTagRetargetProxy::DevFrame)
	float Arm[17] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // the arms against the capture (FTagRetargetProxy::ArmFrame)
	int32 Touchdowns = 0; // feet that came down on the turf since the game thread last cleared it (gait cadence)
	/** Transition jerk measured since the game thread last cleared it (FTagRetargetProxy::MeasureJerk): x = xfj, y = kind
	 *  (0 a clip switch, 1 a matched pick change). AAthlete::XfjStats pools them. */
	TArray<FVector2f> JerkOut;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(Transient)
	FAnimNode_RetargetPoseFromMesh RetargetNode;
};
