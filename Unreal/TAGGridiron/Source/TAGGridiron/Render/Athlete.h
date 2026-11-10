// A football player on the field: motion-captured character + uniform pieces, driven from the simulation.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Athlete.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAnimationAsset;
class UMaterialInstanceDynamic;

namespace tag { struct Player; }

/** What the simulation says this player is doing this frame (world space), for the football anim layer. */
struct FAthleteFootball
{
	bool bHasBall = false;
	bool bSecure = false;      // two hands on the ball (QB in the pocket)
	float ThrowT = -1.f;       // 0..1 through a pass
	bool bReach = false;       // hands to ReachWorld (catch / wrap / block punch)
	FVector ReachWorld = FVector::ZeroVector;
	float ReachSpread = 8.f;
	float Lean = 0.f;          // drive lean 0..1
	bool bStance = false;
	bool bPreSnap = false;     // before the snap: hand keyed stance loops
	bool bUnderCenter = false; // QB lined up under center (not shotgun / pistol)
	bool bLook = false;
	FVector LookWorld = FVector::ZeroVector;
	bool bLive = false;        // the play is live (between the snap and the whistle)
	float CarrierDist = -1.f;  // a defender's distance to the ball carrier (cm), < 0 when there is none
};

struct FUniform
{
	FLinearColor Jersey;
	FLinearColor Pants;
	FLinearColor Helmet;
	FLinearColor Trim;
	FLinearColor Number;
};

UCLASS()
class TAGGRIDIRON_API AAthlete : public AActor
{
	GENERATED_BODY()

public:
	AAthlete();

	void Setup(const tag::Player* InPlayer, const FUniform& U);

	/**
	 * Drive from simulation state.
	 * Location: world cm. Yaw: facing (deg). VelWorld: cm/s. Anim: sim animation state name.
	 * bLineman: uses crouched stance/footwork.
	 */
	void Drive(const FVector& Location, float Yaw, const FVector& VelWorld, float VMaxYds, const FString& Anim, float AnimT, float DeltaTime, bool bLineman);

	/** Bench player in the team area: stands at Loc facing Yaw, idling (Variant picks the idle, side and timing). */
	void StandOnSideline(const FVector& Loc, float Yaw, int32 Variant);
	/** Sideline staff (chain crew, coaches): no helmet, facemask, pads or numbers; just the clothes and shoes. */
	void SetCivilian();
	/** Bench reaction to a score: celebrate, or settle back into the idle. */
	void SidelineCheer(bool bCheer);

	/** Instant replay: the visible body's world transform and local pose (mesh bone order) as shown this frame. */
	bool CaptureReplayPose(FTransform& OutWorld, TArray<FTransform>& OutLocal) const;
	/** Show a recorded pose. EndReplay hands back to the live animation (blending out of the last replay pose); a
	 *  player who was down holds the replay's last pose until he gets up. */
	void ShowReplayPose(const FTransform& World, const TArray<FTransform>& Local);
	void EndReplay();

	/** Restore an animated (non-ragdoll) pose, e.g. before the next snap. */
	void ResetPose();

	void SetSelected(bool bSel);
	FString DebugAnimState() const;

	/** Feed the procedural football layer (throw, ball security, catch, block, tackle, stance, look). */
	void SetFootball(const FAthleteFootball& F, float DeltaTime);
	/** Where the held ball should be drawn (in the hands), world space. */
	bool GetBallHold(FVector& OutLoc, FRotator& OutRot) const;
	/** World position of a bone on the visible body (falls back to the actor). */
	FVector BoneWorld(FName Bone) const;
	void SetLabel(const FString& Text);
	/** Play a keyed clip once over the current state, then hand back (crossfaded). Contact states override it. */
	void PlayOneShot(UAnimationAsset* Clip);
	/** Seconds into the current keyed get-up (-1 when not getting up). */
	float GetUpElapsed() const;
	/** The sim says this player just caught the ball: pick the catch for the ball height, his speed, where the ball
	 *  came from and whether he is at the sideline (SidelineDirW: unit vector toward the nearest sideline, or zero). */
	void PlayCatch(float BallZCm, float SpeedCm, const FVector& BallVelW, const FVector& SidelineDirW,
		const FVector& BallPosW = FVector::ZeroVector, bool bContested = false);
	/** The sim just released a pass: play the throw that fits it (FAthleteAssets::EThrow). */
	void PlayThrow(int32 Kind);
	/** Paired tackle: the first tackler's hit decides the carrier's reaction (head on / from behind / from the side)
	 *  and the pile's momentum (both players' masses and velocities). */
	void ApplyHit(const FVector& TacklerVelW, float TacklerKg, const FVector& TacklerLoc);
	/** Broken tackle: the defender whiffs (bCarrierLeft: the carrier slipped by on his left). */
	void PlayMissedTackle(bool bCarrierLeft);
	/** Broken tackle: the carrier bounces off contact (bHitFromRight: the defender came from his right). */
	void PlayBrokenTackle(bool bHitFromRight);
	/** Layout dive at an incompletion just out of reach (gets back up at the end). */
	void PlayDiveAttempt();
	/** Paired tackle: this defender's tackle is on Carrier; ease into wrap distance and face him. */
	void BeginTackleOn(AAthlete* Carrier);
	/** Paired block: the man this lineman is locked up with this frame (null when not engaged). */
	void SetBlockPartner(AAthlete* Partner) { BlockPartner = Partner; }
	/** Engaged rep state from the sim: Push -1 (blocker driving the defender) .. +1 (defender driving the blocker). */
	void SetLinePush(float Push, bool bDefender, bool bPassPro) { LinePush = Push; bLineDefender = bDefender; bLinePassPro = bPassPro; }
	/** The rusher beat his blocker with Move ("swim", "rip", "club", "spin", "bull", "longarm", "diprip"): play it to
	 *  the side he beats him on. */
	void PlayRushMove(const FString& Move, const AAthlete* Blocker);
	/** The blocker was beaten by Rusher: open the hips, lunge and reach to the side he went, turn and chase. */
	void PlayBeaten(const AAthlete* Rusher);
	/** Pancake: the blocker drives through and finishes over the man he put down. */
	void PlayPancakeFinish();
	/** Keyed fall to the turf, see bFalling. bEaseIn: glide from where the actor is onto From in the first 0.15 s. */
	void BeginPairedFall(UAnimationAsset* Clip, const FVector& From, const FVector& To, float YawW, float DriveTime,
		float LieTime, bool bEaseIn);
	bool IsFalling() const { return bFalling; }
	/** On the ground, falling, or getting up: the body is not his to move. */
	bool IsDown() const { return bFalling || bGettingUp || bRagdoll || FallHoldLeft > 0.f || TackledLeft > 0.f; }
	/** Where the pile ends: the carrier's anchor at the end of the drive. */
	FVector FallAnchorEnd() const { return AnchorB; }
	FVector GetLastVel() const { return LastVelW; }
	FVector PileBodyEnd = FVector::ZeroVector; // the carrier's body at the end of a paired fall
	/** Pancaked: the next fall goes over backward along PushDirW (away from the blocker), about a metre, instead of
	 *  travelling with a tackle pile. */
	void SetPancaked(const FVector& PushDirW) { PancakeDir = PushDirW.GetSafeNormal2D(); bPancakePending = true; }
	/** Look-dev (-TagGapTest): this player's next get-up is the injury beat. */
	bool bForceInjury = false;
	/** Planted-foot slide over every athlete since the last reset (cm): in the animation as retargeted, and as shown
	 *  after the foot lock (UTagRetargetAnimInstance). */
	static void SlideStats(double& OutRaw, double& OutShown, bool bReset);
	/** Gait over every athlete running faster than 8 m/s since the last reset: seconds, metres, foot touchdowns
	 *  (cadence = steps / seconds, step length = metres / steps; sprint biomechanics put 10 m/s at ~4.7 steps/s, 2.1 m). */
	static void GaitStats(double& OutSeconds, double& OutMetres, int32& OutSteps, bool bReset);
	/** Sprinting body height, summed over the same frames as GaitStats: hips and head above the turf (m). */
	static void HeightStats(double& OutHipsM, double& OutHeadM, bool bReset);

	const tag::Player* Player = nullptr;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Helmet;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Facemask;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Pads;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> BackNumber;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> FrontNumber;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Ring;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> Label;

	/** MetaHuman body/face (visible) - pose retargeted from the hidden mocap mannequin `Body`. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> MHBody;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> MHFace;

	/** Facemask cage bars and shoulder-pad caps (created in Setup). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Gear;

	/** The mesh the player actually sees (MetaHuman if available, otherwise the mannequin). */
	USkeletalMeshComponent* Visible() const { return MHBody ? MHBody.Get() : Body.Get(); }

private:
	void PlayClip(UAnimationAsset* Clip, bool bLoop, float Rate = 1.f, float StartPos = 0.f);
	/** Start a clip or blend space on the hidden source body (leaves motion matching), crossfading the visible body. */
	void StartBodyAsset(UAnimationAsset* Asset, bool bLoop);
	// motion matched locomotion (Game Animation Sample PoseSearch databases), see UTagMotionMatchInstance
	class UTagMotionMatchInstance* MatchInstance() const;
	/** Search the motion databases for this frame's move; false when motion matching is unavailable. */
	bool DriveMotionMatching(const FVector& VelWorld, float SimYaw, float DeltaTime, bool bCrouch = false);
	void RecordTrajectory();
	bool bInMatch = false;
	int32 SidelineVariant = 0;
	bool bReplaying = false;
	bool bReplayHold = false;     // down at the end of a replay: keeps its last pose until the get-up
	FTransform ReplayRel;
	void ClearReplayHold();
	int32 MMGait = 1;                         // 0 walk, 1 run, 2 sprint
	TArray<FVector> TrajPos;                  // recorded source body transforms (world), newest last
	TArray<FQuat> TrajRot;
	TArray<double> TrajTime;
	FVector PrevVel = FVector::ZeroVector;     // measured (smoothed) body velocity
	FVector PrevPos = FVector::ZeroVector;
	FVector SmoothedAccel = FVector::ZeroVector;
	bool bHavePrevVel = false;
	float MovingTime = 0.f;                   // seconds since this player started moving (motion matching start state)
	bool bPreSnap = false;
	bool bWasPreSnap = false;
	bool bUnderCenter = false;
	UPROPERTY()
	TObjectPtr<UAnimationAsset> OneShot;   // keyed one-shot in progress (get-off, snap catch, handoff)
	float OneShotLeft = 0.f;
	// get-up after a ragdoll: snapshot the lying pose, play the matching keyed get-up where the body lies
	void StartGetUp(const FVector& SimLoc);
	// keyed fall to the ground (paired tackle, pancake, solo knockdown): the actor is anchored and the clip carries the
	// body; the anchor slides from From to To over DriveTime (the pile driving onto the sim's spot). Ends lying still
	// for LieTime, then the keyed get-up out of the landing.
	bool bFalling = false;
	TObjectPtr<UAnimationAsset> FallClip;
	float FallT = 0.f, FallLen = 0.f, FallLie = 0.f, FallDrive = 0.4f, FallFromYaw = 0.f, FallYaw = 0.f;
	FVector FallFrom = FVector::ZeroVector, AnchorA = FVector::ZeroVector, AnchorB = FVector::ZeroVector;
	bool bFallEase = false;
	float FallHoldLeft = 0.f; // down, waiting for the director to pair the fall; a solo fall after this
	void TickFall(const FVector& SimLoc, float DeltaTime);
	void BeginSoloFall(const FVector& SimLoc, const FVector& MoveDirW);
	float DownTime = 0.f;
	float GetUpLeft = 0.f;
	bool bGettingUp = false;
	bool bStoodUp = false;
	FVector VisOffset = FVector::ZeroVector;
	int32 TackleCount = 0;
	float StillTime = 0.f;   // seconds standing still (post-play idles)
	// face (UTagFaceAnimInstance): blinks, effort, breathing, focus, celebration
	void UpdateFace(float DeltaTime);
	float BlinkClock = 0.f, NextBlink = 3.f, BlinkPhase = 10.f;
	bool bDoubleBlink = false;
	float Exertion = 0.f, FaceEffort = 0.f, FaceFocus = 0.f, FaceShout = 0.f;
	// paired tackles: the carrier is held where the hit lands and carried with the pile before the ragdoll fall;
	// the tackler eases into wrap distance of him
	float TackledLeft = 0.f;
	FVector PileVel = FVector::ZeroVector;
	TWeakObjectPtr<AAthlete> TackleOn;
	TWeakObjectPtr<AAthlete> BlockPartner;
	TWeakObjectPtr<AAthlete> PunchedPartner; // the man this lineman struck at first contact (one punch per engagement)
	float LinePush = 0.f, SmoothedPush = 0.f;
	bool bLineDefender = false, bLinePassPro = true;
	int32 LineLoop = 0; // engaged loop in use: +1 defender winning, -1 blocker winning, 0 hand fight
	FVector PancakeDir = FVector::ZeroVector;
	bool bPancakePending = false;
	bool bFaceOnLeader = false; // bench / staff: the face always follows the head by leader pose
	/** Ragdoll: the face follows the physics-driven head by leader pose (its pose copy runs before physics and would
	 *  leave it hanging where the head was); back to the face rig when he is up. */
	void FaceFollowBody(bool bFollow);
	bool bInjuredChain = false;   // getting up: the injury beat plays first, then the slow get-up
	bool bInjuredMirror = false;  // ...on the same side
	float OneShotRate = 1.f;      // per-player timing (style), so two players never move in lockstep
	bool bHitApplied = false;     // the carrier's reaction already matched to the first tackler
	bool bMissing = false;        // whiffing a tackle (one-shot), the tackle clips stand aside
	FVector LastVelW = FVector::ZeroVector; // last moving velocity (the carrier's momentum into a tackle)
	float InjuredLeft = 0.f;
	FVector BlockAlign = FVector::ZeroVector;
	float AlignT = -1.f;   // rotates tackle variants so the same player never repeats one back to back
	bool bEngaged = false;   // hands on an opponent (football layer reach), picks the engaged block loop
	void StartRagdoll(const FVector& VelWorld);
	void SetupMetaHuman(int32 Arch, const FUniform& U);
	FVector VisRelScale = FVector::OneVector;
	// smoothed football-layer weights
	float CarryA = 0.f, SecureA = 0.f, ReachA = 0.f, LeanA = 0.f, StanceA = 0.f, StanceHandA = 0.f, LookA = 0.f;
	FVector ReachSmoothed = FVector::ZeroVector, LookSmoothed = FVector::ZeroVector;
	float ArmSlot = 0.f, StyleJitter = 0.f;
	// body physics inputs (TagRetargetAnimInstance): the lean from the measured acceleration, planted feet pinned
	void MeasureMotion(float Dt);
	FVector LeanSmoothed = FVector::ZeroVector;
	// the quarterback's drop and pocket, the defender's breakdown (SetFootball feeds these a frame ahead of Drive)
	bool bHasBallF = false, bLiveF = false, bDropped = false;
	float CarrierDistF = -1.f, DropLeft = 0.f;

	UPROPERTY()
	TObjectPtr<UAnimationAsset> CurrentClip;

	FString CurrentAnim;
	float SmoothedYaw = 0.f;
	float SmoothedDir = 0.f;
	float SmoothedSpeed = 0.f;
	bool bYawInit = false;
	bool bRagdoll = false;
	bool bMaterialNumbers = false;
	FVector BodyRelLoc = FVector::ZeroVector;
	FRotator BodyRelRot = FRotator(0, -90, 0);
};

/** One MetaHuman body type (lineman / skill / big). */
struct FMHArchetype
{
	TObjectPtr<class USkeletalMesh> Body;
	TObjectPtr<class USkeletalMesh> Face;
	TObjectPtr<class UIKRetargeter> Retargeter;
	TObjectPtr<class UTexture> SkinBC;
	TObjectPtr<class UTexture> SkinN;
	float HeightCm = 185.f;
	float Depth = 1.f; // chest depth relative to the skill body (number placement)
};

/** Shared asset cache for athletes (loaded once). */
struct FAthleteAssets
{
	static FAthleteAssets& Get();
	bool bLoaded = false;
	bool bMocap = false; // Game Animation Sample (UEFN mannequin) assets available
	bool bMetaHumans = false; // TAG MetaHuman bodies available (retargeted from the mocap)
	static constexpr int32 MHCount = 5;
	FMHArchetype MH[MHCount]; // 0 OL, 1 skill, 2 big, 3 defensive line, 4 lean (QB/K/P); 3 and 4 optional
	TObjectPtr<class UMaterialInterface> MHUniformMat;
	static int32 ArchetypeFor(int32 PosIndex);
	TObjectPtr<class USkeletalMesh> Mesh;
	TObjectPtr<class UBlendSpace> Locomotion;
	TObjectPtr<class UBlendSpace> Crouch;
	TObjectPtr<UAnimationAsset> FallFront;
	TObjectPtr<UAnimationAsset> FallFront2;
	TObjectPtr<UAnimationAsset> FallBack;
	TObjectPtr<UAnimationAsset> HitReact;
	TObjectPtr<UAnimationAsset> Shove;
	TObjectPtr<UAnimationAsset> Throw;
	TObjectPtr<UAnimationAsset> Jump;
	TObjectPtr<UAnimationAsset> Dash;
	TObjectPtr<UAnimationAsset> Celebrate;
	/** TAG hand keyed clips (Tools/Anim, Blender; imported by Tools/Art/import_tag_anims.py). Null = old behaviour. */
	TObjectPtr<UAnimationAsset> Stance3pt;      // OL / DL pre-snap loop
	TObjectPtr<UAnimationAsset> Stance2pt;      // WR, TE, RB, LB, DB pre-snap loop
	TObjectPtr<UAnimationAsset> StanceShotgun;  // QB pre-snap loop
	TObjectPtr<UAnimationAsset> ThrowKeyed;     // QB pass; played from its stride frame so release meets the sim's
	static constexpr float ThrowKeyedStart = 16.f / 30.f; // release at frame 23, 0.2 s after the plant (frame 18)
	bool bCelebrateKeyed = false;
	TObjectPtr<UAnimationAsset> GetOff;         // OL / DL explode out of the three point stance at the snap
	TObjectPtr<UAnimationAsset> TackleKeyed;    // form tackle: sink, explode, wrap, drive (then ragdoll on "down")
	TObjectPtr<UAnimationAsset> TackleHigh;     // chest-level wrap
	TObjectPtr<UAnimationAsset> TackleLow;      // shoot the thighs
	TObjectPtr<UAnimationAsset> TackleDive;     // full extension (sim "dive")
	TObjectPtr<UAnimationAsset> JukeL;          // ball carrier cut to his left / right
	TObjectPtr<UAnimationAsset> JukeR;
	TObjectPtr<UAnimationAsset> Kneel;          // QB victory formation
	TObjectPtr<UAnimationAsset> BlockEngaged;   // hands inside, choppy feet (engaged OL / DL)
	TObjectPtr<UAnimationAsset> BlockPassSet;   // pass protection set before contact
	TObjectPtr<UAnimationAsset> SnapCatch;      // shotgun QB takes the snap
	TObjectPtr<UAnimationAsset> Handoff;        // QB hands off right and carries out the fake
	TObjectPtr<UAnimationAsset> GetUpDown;      // get up from lying face down
	TObjectPtr<UAnimationAsset> GetUpUp;        // get up from lying on the back
	TObjectPtr<UAnimationAsset> ShedSwim;       // pass rush moves when a defender beats his blocker
	TObjectPtr<UAnimationAsset> ShedRip;
	// batch 2 (Tools/Anim/clips_more.py): coverage of the remaining football situations
	TObjectPtr<UAnimationAsset> CatchHigh, CatchChest, CatchLow, CatchOverShoulder, CatchSidelineL, CatchSidelineR;
	TObjectPtr<UAnimationAsset> SpinL, SpinR, StiffArmL, StiffArmR, Truck, Hurdle;   // ball carrier moves
	TObjectPtr<UAnimationAsset> RushBull, RushSpinL, RushSpinR, RushClub;             // pass rush moves
	// the fight in the trenches (Tools/Anim/clips_more3.py): engaged loops by who is winning the rep, the strike at
	// contact, the power and edge moves, the beaten blocker, the pancake finish
	TObjectPtr<UAnimationAsset> BullDriveLoop, AnchorLoop, DriveLoop, DrivenBackLoop, BlockPunch, LongArm, DipRip,
		Beaten, BlockFinish;
	TObjectPtr<UAnimationAsset> TackleShoulder, TackleArm, TackleAssist;
	TObjectPtr<UAnimationAsset> Stance4pt, StanceRB, StanceDB, StanceLB, StanceK;
	TObjectPtr<UAnimationAsset> KickKickoff, KickFieldGoal, KickPunt;
	TObjectPtr<UAnimationAsset> GetUpRoll, IdleHandsHips, IdleHandsKnees;
	// batch 3: the last uncovered situations
	TObjectPtr<UAnimationAsset> StanceUnderCenter, UnderCenterSnap, Pitch, CatchDive, GetUpSlow;
	TObjectPtr<UAnimationAsset> TackledHit; // the carrier's side of a tackle
	TObjectPtr<UAnimationAsset> InjuredDown; // hurt on the play, chained into the slow get-up
	// batch 6 (Tools/Anim/clips_more5.py): the quarterback's drops and pocket, the defender's breakdown
	TObjectPtr<UAnimationAsset> QBDropShotgun, QBDropUnder, QBPocket, Breakdown;
	static constexpr float DropShotgunSpeed = 275.f, DropUnderSpeed = 400.f; // cm/s the drops were keyed at
	// batch 4: the throwing family (index = EThrow), more catches, collision reactions
	enum EThrow : int32 { ThrowSet = 0, ThrowQuickK, ThrowDeepK, ThrowFadeK, ThrowSidearmK, ThrowTouchK, ThrowRunRK, ThrowRunLK, ThrowCount };
	UAnimationAsset* Throws[ThrowCount] = {};
	float ThrowStart[ThrowCount] = {};   // where each clip starts so its release lands just after the sim's
	TObjectPtr<UAnimationAsset> CatchInStride, CatchHighPoint, CatchOneHand, CatchBackShoulder, CatchComeback, CatchScoop;
	TObjectPtr<UAnimationAsset> TackledFromBehind, TackledFromSide, TackleMiss, BrokenStumble;
	// paired tackles to the ground (Tools/Anim/clips_more4.py): tackler (T) and carrier (C) authored together
	enum ETacklePair : uint8 { PairDrive, PairOver, PairBehind, PairSide, PairDive, PairCount };
	TObjectPtr<UAnimationAsset> PairT[PairCount], PairC[PairCount], PileOn;
	/** Mirrored (other side) version of a keyed clip, built by Tools/Anim (<name>_M); the clip itself if none. */
	TMap<UAnimationAsset*, UAnimationAsset*> MirrorOf;
	UAnimationAsset* Side(UAnimationAsset* Clip, bool bMirror) const
	{
		if (!Clip || !bMirror) return Clip;
		UAnimationAsset* const* M = MirrorOf.Find(Clip);
		return M ? *M : Clip;
	}
	static constexpr float CatchDiveTravel = 115.f; // cm the dive carries the body forward (Tools/Anim CATCH_DIVE_TRAVEL)
	TArray<UAnimationAsset*> Celebrations;                                            // keyed celebration loops
	// where foot contact falls in each kick clip (s), so the director can line it up with the ball leaving
	static constexpr float KickoffContact = 26.f / 30.f, FieldGoalContact = 20.f / 30.f, PuntContact = 19.f / 30.f;
	TObjectPtr<UAnimationAsset> Idle;
	// motion matching over the Game Animation Sample's Dense PoseSearch databases (rooted, never collected)
	bool bMotionMatch = false;
	// per gait (walk / run / sprint), split by state the way the sample's chooser does: the start and pivot databases are
	// only offered while starting / pivoting (offered all the time, their clips won over the loops on straight runs)
	TArray<class UPoseSearchDatabase*> MMMove[3];   // loops
	TArray<class UPoseSearchDatabase*> MMStart[3];  // starts + loops
	TArray<class UPoseSearchDatabase*> MMPivot[3];  // pivots + spins + loops
	TArray<class UPoseSearchDatabase*> MMStop[3];
	// crouched line footwork (pass sets, pulls, drive blocks), same state split: [0] loops, [1] starts, [2] pivots, [3] stops + idles
	TArray<class UPoseSearchDatabase*> MMCrouch[4];   // that gait's stops + standing idles + turns in place
	TArray<class UPoseSearchDatabase*> MMAll;
	void LoadMotionMatching();
	/** True once every database's search index is built (game thread; checked at most once a frame). */
	bool MotionMatchReady();
	bool bMMReady = false;
	uint64 MMCheckedFrame = 0;
	TObjectPtr<class UMaterialInterface> UniformMat;
	float LocoMaxSpeed = 700.f;
	float CrouchMaxSpeed = 225.f;
	TObjectPtr<class UStaticMesh> Sphere;
	/** TAG gear meshes (Tools/Art/gear_build.py). Null falls back to the primitive helmet and bar facemask. */
	TObjectPtr<class UStaticMesh> HelmetMesh;
	TObjectPtr<class UStaticMesh> FacemaskMesh[3]; // Open (skill), Std (RB/LB/TE/FB), Cage (linemen)
	TObjectPtr<class UStaticMesh> PadsMesh;
	TObjectPtr<class UStaticMesh> FootballMesh;   // 28 x 17 cm along X, slot 0 leather, slot 1 laces
	TObjectPtr<class UStaticMesh> Cube;
	TObjectPtr<class UStaticMesh> Cylinder;
	TObjectPtr<class UStaticMesh> Torus;
	TObjectPtr<class UStaticMesh> Plane;
	TObjectPtr<class UMaterialInterface> TurfMat;
	TObjectPtr<class UMaterialInterface> CrowdMat;
	TObjectPtr<class UMaterialInterface> LEDMat; // emissive panels: ribbon boards, video boards (Tools/Art/ue_build_stadium_mats.py)
	TObjectPtr<class UMaterialInterface> ShapeMat;
	void Load();
};

/** Make a solid-color dynamic material from the engine's basic shape material. */
UMaterialInstanceDynamic* TagColorMat(UObject* Outer, const FLinearColor& C, float Roughness = 0.6f);
