// A football player on the field: motion-captured character + uniform pieces, driven from the simulation.
#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/Actor.h"
#include "Athlete.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAnimationAsset;
class FJsonObject;
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
	uint8 GetOffKind = 0;      // a lineman's job at the snap: 1 pass set, 2 run block, 3 defensive line (0 = not a lineman)
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
	int32 TeamId = -1;  // the team's mark on the helmet (gen_team_logos.py atlas cell); -1 none
	FLinearColor Logo = FLinearColor::White; // the mark's fill
};

UCLASS()
class TAGGRIDIRON_API AAthlete : public AActor
{
	GENERATED_BODY()

public:
	AAthlete();

	void Setup(const tag::Player* InPlayer, const FUniform& U);
	/** The body's animation ticks after Driver's tick (the director moves the actor from its Tick; without the
	 *  dependency the two ran in either order within the tick group, and on the frames the pose was evaluated
	 *  against the previous position the planted feet, pinned in world space, were pulled a frame's travel off). */
	void TickAfter(AActor* Driver);

	/**
	 * Drive from simulation state.
	 * Location: world cm. Yaw: facing (deg). VelWorld: cm/s. Anim: sim animation state name.
	 * bLineman: uses crouched stance/footwork.
	 */
	void Drive(const FVector& Location, float Yaw, const FVector& VelWorld, float VMaxYds, const FString& Anim, float AnimT, float DeltaTime, bool bLineman);

	/** Bench player in the team area: stands at Loc facing Yaw, idling (Variant picks the idle, side and timing). */
	void StandOnSideline(const FVector& Loc, float Yaw, int32 Variant);
	/** Who a non-player is (model pack characters): chain crew, head coach, official, assistant coach, trainer. */
	enum class ECivilian : uint8 { Generic, Crew, Coach, Official, Assistant, Trainer };
	/** Sideline staff (chain crew, coaches, officials): no helmet, facemask, pads or numbers; just the clothes and
	 *  shoes. With the model pack characters the role picks the person (crew vest, coach top in the team colour,
	 *  official's stripes) and Index varies him. */
	void SetCivilian(ECivilian Who = ECivilian::Generic, int32 Index = 0);
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
	/** -TagHlFull telemetry: what drives the body this frame, "mm:<matched clip>", "clip:<asset>", "one:<asset>",
	 *  "fall:<asset>@t", "getup:<asset>", "ragdoll" or "replay" (the sim's own anim name is logged beside it). */
	FString TelemetryAnim() const;
	/** Planted-foot travel (cm) over the last animation update: as the clip had it, and as shown after the foot lock. */
	float LastSlideRaw = 0.f, LastSlideShown = 0.f;
	/** Take the body's offset from the sim spot (a fall that ended away from it) as its position from now on. */
	void AbsorbVisOffset() { VisOffset = FVector::ZeroVector; }
	/** How long a fall eases from where he was onto its anchor (a pile-on from a few yards away travels there). */
	void SetFallEaseTime(float T) { FallEaseT = FMath::Max(0.05f, T); }

	/** Feed the procedural football layer (throw, ball security, catch, block, tackle, stance, look). */
	void SetFootball(const FAthleteFootball& F, float DeltaTime);
	/** Where the held ball should be drawn (in the hands), world space. */
	bool GetBallHold(FVector& OutLoc, FRotator& OutRot) const;
	/** World position of a bone on the visible body (falls back to the actor). */
	FVector BoneWorld(FName Bone) const;
	void SetLabel(const FString& Text);
	/** Play a keyed clip once over the current state, then hand back (crossfaded). Contact states override it. */
	void PlayOneShot(UAnimationAsset* Clip);
	void PlayOneShotFrom(UAnimationAsset* Clip, float Start); // from Start seconds into the clip (a library capture's moment less its lead)
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
	AAthlete* GetBlockPartner() const { return BlockPartner.Get(); }
	const FString& GetAnimName() const { return CurrentAnim; }
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
		float LieTime, bool bEaseIn, float Start = 0.f, bool bRootDrive = false);
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

	/** Model pack helmet (shell, facemask, chin strap), skinned to the head: follows the body by leader pose. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> HeroHelmet;

	/** Helmet on or off (model pack players): the hair shows when it is off. */
	void SetHelmetOn(bool bOn);
	/** A sideline cap (the pack's ball caps and visor hat) in place of the helmet. */
	void WearCap(int32 Variant);

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
	uint8 GetOffKind = 0;
	UPROPERTY()
	TObjectPtr<UAnimationAsset> OneShot;   // keyed one-shot in progress (get-off, snap catch, handoff)
	float OneShotLeft = 0.f;
	bool bOneShotCatch = false;  // the running one-shot is a catch (it hands over to the run once he is away with it)
	float OneShotAge = 0.f;
	float OneShotStart = 0.f;     // seconds into the one-shot's clip it starts at (a library capture's moment less its lead)
	// get-up after a ragdoll: snapshot the lying pose, play the matching keyed get-up where the body lies
	void StartGetUp(const FVector& SimLoc);
	// keyed fall to the ground (paired tackle, pancake, solo knockdown): the actor is anchored and the clip carries the
	// body; the anchor slides from From to To over DriveTime (the pile driving onto the sim's spot). Ends lying still
	// for LieTime, then the keyed get-up out of the landing.
	bool bFalling = false;
	UPROPERTY()
	TObjectPtr<UAnimationAsset> FallClip;
	float FallStart = 0.f;        // seconds into FallClip the fall starts (a library capture's contact)
	bool bFallRoot = false;       // the clip's root motion carries the body through the fall (library captures)
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
	float FallEaseT = 0.2f;
	float OrientWarpSm = 0.f; // the smoothed orientation warp (deg) handed to the retarget layer
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
	TArray<TWeakObjectPtr<AAthlete>> PunchedThisPlay; // everyone he has struck this play: a double team handing off or a
	                                                  // partner flickering back does not re-punch
	float SincePunch = 99.f;
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
	// model pack characters (FAthleteAssets::bHero): body (kit / coach / official / crew), head and helmet meshes on
	// the pack's hero rig, retargeted from the hidden mannequin like the MetaHumans were
	void SetupHero(const FUniform& U);
	void DressHero(class USkeletalMesh* BodyMesh, class USkeletalMesh* HeadMesh, class USkeletalMesh* HelmetMesh, const FString& Rig);
	void HideHair(bool bHide);
	void PaintHero(const FUniform& U);
	FUniform CurUniform{};
	int32 HeroTone = 1;      // 1..8, the pack's skin tones (arms and legs match the head)
	bool bHeroHelmetOn = true;
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

/** One character mesh from the model pack catalog (Tools/Art/hero_import.py -> Content/TAG/Hero/hero_catalog.json). */
struct FHeroItem
{
	FString Kind, Name, Path, Build, Style, Top, Shell, Rig, Weather;
	int32 Tone = 1;
	int32 Cells = 1;
	float Height = 0.f;   // bind-pose height (cm); partial recipes (head only) are short
	float Far = 0.f;      // share of vertices skinned to a bone > 60 cm away (a torn mesh in the pack scores high)
	bool bHat = false;
	bool bNoHead = false; // a coach "head" recipe with the cap and headset but no face
	TObjectPtr<class USkeletalMesh> Mesh; // loaded (and rooted) on first use
};

/** Shared asset cache for athletes (loaded once). */
struct FAthleteAssets
{
	static FAthleteAssets& Get();
	/** Model pack characters (players, coaches, officials, crew, fans) replace the MetaHumans when the catalog is there
	 *  (-TagNoHero for the MetaHumans). */
	bool bHero = false;
	TArray<FHeroItem> HeroItems;
	TObjectPtr<class UIKRetargeter> HeroRetargeter;
	TObjectPtr<class UIKRetargeter> CrowdRetargeter;
	/** The runtime retargeter for a skeleton family (hero, crowd, male, female, child, sideplayer). */
	class UIKRetargeter* RetargeterFor(const FString& Rig);
	/** A clip baked for the people that stand around (Idle, Cheer, Clap, Kneel) on a skeleton family. */
	UAnimationAsset* HeroClip(const FString& Rig, const TCHAR* Clip);
	TMap<FString, TObjectPtr<class UIKRetargeter>> HeroRetargeters;
	TMap<FString, TObjectPtr<UAnimationAsset>> HeroClips;
	TArray<TObjectPtr<class UTexture>> HeroSkinArm, HeroSkinLegs, HeroEyes;
	TArray<TObjectPtr<class UTexture>> HeroJerseyMasks, HeroPantsMasks, HeroHelmetMasks; // stripe layouts, one per team
	float HeroHeightCm = 187.f;   // the pack rig's standing height (bind pose, head included)
	int32 HeroWeather = 0;        // 0 warm, 1 cold, 2 rain (coach tops, officials); set by the director per game
	bool LoadHero();
	class USkeletalMesh* HeroMesh(int32 Index);
	/** Indices of the catalog items of a kind (optionally filtered), in catalog order. */
	TArray<int32> HeroOf(const TCHAR* Kind, TFunctionRef<bool(const FHeroItem&)> Pred = [](const FHeroItem&) { return true; }) const;
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
	/** TAG football clips (Tools/Anim: retargeted from the mocap pack by mocap_map.py / build_mocap.py, a few still hand
	 *  keyed in clips*.py; imported by Tools/Art/import_tag_anims.py). Null = old behaviour. */
	TObjectPtr<UAnimationAsset> Stance3pt;      // OL / DL pre-snap loop
	TObjectPtr<UAnimationAsset> Stance2pt;      // WR, TE, RB, LB, DB pre-snap loop
	TObjectPtr<UAnimationAsset> StanceShotgun;  // QB pre-snap loop
	TObjectPtr<UAnimationAsset> ThrowKeyed;     // QB pass; played from its stride frame so release meets the sim's
	static constexpr float ThrowKeyedStart = 10.f / 30.f; // 7 frames before the set throw's release (frame 17, Tools/Anim/mocap_constants.py)
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
	TArray<TObjectPtr<UAnimationAsset>> Breakdowns; // Breakdown and the pack's in-place bounces, one per player
	/** The mocap pack library (/Game/TAG/Anim/Pack/<Category>: every capture of the pack on the rig,
	 *  Tools/Art/import_pack_library.py). The families below draw on all of it: per family a bounded selection,
	 *  seeded by the day (-TagPackSeed=<n> to pin one), loaded at start; a clip that has to loop (stances, idles,
	 *  breakdowns) is taken only when its last pose returns to its first (LoopsCleanly). Chosen per player. */
	TArray<TObjectPtr<UAnimationAsset>> PackCelebrations, PackPostPlayIdles, PackGetUpsFaceDown, PackGetUpsFaceUp, PackInjuries;
	TMap<int32, TArray<TObjectPtr<UAnimationAsset>>> PackStances; // by tag::Pos
	int32 PackLibraryCount = 0;
	/** Registers a family for the next batch (SelectPack): up to Max * PackScale captures matching the names, drawn by
	 *  the seed, delivered into Sink() when the batch has loaded (the sink's own entries up to its first registration
	 *  are kept: the keyed breakdowns stay first). */
	using FFamilySink = TFunction<TArray<TObjectPtr<UAnimationAsset>>&()>;
	void PackFamily(const TCHAR* Category, std::initializer_list<const TCHAR*> Any, std::initializer_list<const TCHAR*> Any2,
		std::initializer_list<const TCHAR*> None, int32 Max, int32 Seed, bool bLoop, FFamilySink Sink);
	static bool LoopsCleanly(const UAnimSequence* Seq);
	/** The timed families: a whole capture started at its moment (pack_index.json, Tools/Art/pack_index.py: the
	 *  right hand's peak = a throw's release or a catch's reach, the right foot's peak = a kick's contact, the pelvis
	 *  going down = the hit, the root's first sideways step = a juke's onset) less a lead, the way the placed clips
	 *  were cut by hand. Every family keeps its placed clip and takes a library capture every other time. */
	struct FPackClip { TObjectPtr<UAnimationAsset> Anim; float Start = 0.f; float Len = 0.f; float Speed = 0.f; FString Name; FSoftObjectPath Path; };
	TArray<FPackClip> PackThrowsSet, PackThrowsQuick, PackThrowsDeep, PackThrowsRollR, PackThrowsRollL;
	TArray<FPackClip> PackKickoffs, PackFieldGoals, PackPunts, PackDropsSG, PackDropsUC;
	TArray<FPackClip> PackCatchStride, PackCatchHigh, PackCatchLow, PackCatchOverShoulder, PackCatchSideline, PackCatchStand, PackCatchComeback, PackCatchDive;
	TArray<FPackClip> PackTackledFront, PackTackledBehind, PackTackledSide, PackTacklers, PackWhiffs, PackStumbles;
	TArray<FPackClip> PackJukesL, PackJukesR, PackSpinsL, PackSpinsR, PackTrucks, PackHurdles, PackStiffArmsL, PackStiffArmsR;
	TArray<FPackClip> PackRushWinsL, PackRushWinsR, PackBeatenL, PackBeatenR, PackPancakes, PackGetOffs;
	// get-offs by job (the shared PackGetOffs pool gave centers a DL get-off and DL a guard's pull)
	TArray<FPackClip> PackGetOffsPass, PackGetOffsRun, PackGetOffsDL;
	// first contact of a block from the captures (the blocker __P01, the defender __P02), started just before contact
	TArray<FPackClip> PackEngageOff, PackEngageDef;
	TMap<FString, TSharedPtr<FJsonObject>> PackIndex;
	int32 PackTimedCount = 0;
	void LoadPackIndex();
	void PackTimed(const TCHAR* Category, std::initializer_list<const TCHAR*> Any, std::initializer_list<const TCHAR*> Any2,
		std::initializer_list<const TCHAR*> None, int32 Max, int32 Seed, const TCHAR* Field, int32 LeadFrames, float MinAfter, float MinEventSpeed, TArray<FPackClip>& Sink);
	static const FPackClip* Pick(const TArray<FPackClip>& L, int32 Seq) { return L.Num() ? &L[FMath::Abs(Seq) % L.Num()] : nullptr; }
	/** The library by default: the placed clip is one entry among the library's N (null = the placed one). */
	static const FPackClip* PickOr(const TArray<FPackClip>& L, int32 Seq) { const int32 i = L.Num() ? FMath::Abs(Seq) % (L.Num() + 1) : 0; return i ? &L[i - 1] : nullptr; }
	static bool NameHas(const FString& N, std::initializer_list<const TCHAR*> L) { for (const TCHAR* T : L) if (N.Contains(T)) return true; return false; }
	static constexpr float DropShotgunSpeed = 253.f, DropUnderSpeed = 368.f; // cm/s the drops were captured at (mocap_constants.py)
	// batch 4: the throwing family (index = EThrow), more catches, collision reactions
	enum EThrow : int32 { ThrowSet = 0, ThrowQuickK, ThrowDeepK, ThrowFadeK, ThrowSidearmK, ThrowTouchK, ThrowRunRK, ThrowRunLK, ThrowCount };
	UAnimationAsset* Throws[ThrowCount] = {};
	float ThrowStart[ThrowCount] = {};   // where each clip starts so its release lands just after the sim's
	TObjectPtr<UAnimationAsset> CatchInStride, CatchHighPoint, CatchOneHand, CatchBackShoulder, CatchComeback, CatchScoop;
	TObjectPtr<UAnimationAsset> TackledFromBehind, TackledFromSide, TackleMiss, BrokenStumble;
	// paired tackles to the ground (Tools/Anim/clips_more4.py): tackler (T) and carrier (C) authored together
	enum ETacklePair : uint8 { PairDrive, PairOver, PairBehind, PairSide, PairDive, PairCount };
	TObjectPtr<UAnimationAsset> PairT[PairCount], PairC[PairCount], PileOn;
	/** The pack library's two-man tackles (Tools/Art/pack_pairs.py: pack_pairs.json): both men's captures, their
	 *  relation at the contact and where the carrier ends, the fall root-driven from the contact. By kind: Drive, Over,
	 *  Behind, Side (from his right), SideL (from his left), Dive. The placed pair is one entry among them. */
	struct FPackPair { TObjectPtr<UAnimationAsset> T, C; float Ox = 0, Oy = 0, Yaw = 0, EndX = 0, EndY = 0, TLie = 0.45f, CLie = 1.05f, Start = 0; FString Name; };
	TArray<FPackPair> PackPairs[6];
	void LoadPackPairs(int32 Seed);
	/** The library streams in behind the game. SelectPack picks every family's captures for a seed from the asset
	 *  registry (no loading), the batch loads asynchronously (PackStreamer) and OnPackBatchLoaded fills the families,
	 *  each replacing its previous draw. The director reseeds every quarter (ReseedPack), so a game runs through
	 *  four draws; the batch before last is released then. -TagPackScale=<n> multiplies every family's bound (3). */
	int32 PackSeed = 0, PackScale = 8, PackBatch = 0;
	bool bPackLoading = false;
	FStreamableManager PackStreamer;
	TSharedPtr<FStreamableHandle> PackHandle;
	/** The batch loads in chunks, families interleaved (every family gets some of its draw early) and stops when
	 *  the batch has grown the process by the memory budget (-TagPackMB=<MB>, default 2500): an uncooked capture costs
	 *  several MB resident, so the budget, not the bounds, sets how much of the pack a draw holds. */
	TArray<FSoftObjectPath> PackQueue;
	int32 PackQueuePos = 0, PackChunk = 0;
	TSet<FSoftObjectPath> PackPlaced;
	double PackBudgetMB = 0.0, PackT0 = 0.0;
	void LoadNextPackChunk();
	void FillPackLoaded();
	struct FPendingFamily { FFamilySink Sink; int32 Max = 0; bool bLoop = false; TArray<FSoftObjectPath> Paths; };
	struct FPendingTimed { TArray<FPackClip>* Sink = nullptr; TArray<FPackClip> Clips; };
	struct FPendingPair { int32 Kind = 0; FPackPair P; FSoftObjectPath TP, CP; };
	TArray<FPendingFamily> PendingFamilies;
	TArray<FPendingTimed> PendingTimed;
	TArray<FPendingPair> PendingPairs;
	TMap<void*, int32> SinkBase;             // each sink's own entries before the library's (kept across batches)
	TArray<TObjectPtr<UAnimSequence>> BatchRoots[2]; // the draw in play and the one before it (released once the next has landed)
	double PackLoadedMB = 0.0;                        // the batch's own clips by their resource size: the budget bounds this, not the process
	void SelectPack(int32 Seed);
	void ReseedPack(int32 Seed);
	void OnPackBatchLoaded();
	/** Mirrored (other side) version of a keyed clip, built by Tools/Anim (<name>_M); the clip itself if none. */
	TMap<UAnimationAsset*, UAnimationAsset*> MirrorOf;
	UAnimationAsset* Side(UAnimationAsset* Clip, bool bMirror) const
	{
		if (!Clip || !bMirror) return Clip;
		UAnimationAsset* const* M = MirrorOf.Find(Clip);
		return M ? *M : Clip;
	}
	static constexpr float CatchDiveTravel = 330.f; // cm the dive carries the body forward (the sprinting layout slides; Tools/Anim/mocap_constants.py)
	TArray<UAnimationAsset*> Celebrations;                                            // keyed celebration loops
	// where foot contact falls in each kick clip (s), so the director can line it up with the ball leaving
	static constexpr float KickoffContact = 40.f / 30.f, FieldGoalContact = 24.f / 30.f, PuntContact = 27.f / 30.f; // the kicking foot's peak speed (mocap_constants.py)
	TObjectPtr<UAnimationAsset> Idle;
	// motion matching over the Game Animation Sample's Dense PoseSearch databases (rooted, never collected)
	bool bMotionMatch = false;
	bool bMMPack = false; // the TAG databases with the mocap pack's locomotion are present (speeds in DriveMotionMatching)
	// per gait (walk / run / sprint), split by state the way the sample's chooser does: the start and pivot databases are
	// only offered while starting / pivoting (offered all the time, their clips won over the loops on straight runs)
	TArray<class UPoseSearchDatabase*> MMMove[3];   // loops
	TArray<class UPoseSearchDatabase*> MMStart[3];  // starts + loops
	TArray<class UPoseSearchDatabase*> MMPivot[3];  // pivots + spins + loops
	TArray<class UPoseSearchDatabase*> MMStop[3];
	// per-man style variants of the move / start / pivot sets: [gait][player id % 3], each with its own loops database
	// (PSD_TAG_<gait>_Loops_S1/_S2 hold a different two thirds of each direction's clips; variant 0 is the full set)
	TArray<class UPoseSearchDatabase*> MMMoveV[3][3], MMStartV[3][3], MMPivotV[3][3];
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
