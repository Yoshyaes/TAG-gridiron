// A football player on the field: motion-captured character + uniform pieces, driven from the simulation.
#pragma once

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "UObject/StrongObjectPtr.h"
#include "GameFramework/Actor.h"
#include "Athlete.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAnimationAsset;
class FJsonObject;
class UMaterialInstanceDynamic;

namespace tag { struct Player; class PlaySim; class Ent; }

/** What the simulation says this player is doing this frame (world space), for the football anim layer. */
struct FAthleteFootball
{
	bool bHasBall = false;
	bool bSecure = false;      // two hands on the ball (QB in the pocket)
	bool bCoverBall = false;   // a ball carrier covering it up (R1): the pack's cover-ball carries
	float ThrowT = -1.f;       // 0..1 through a pass
	bool bReach = false;       // hands to ReachWorld (catch / wrap / block punch)
	FVector ReachWorld = FVector::ZeroVector;
	float ReachSpread = 8.f;
	bool bCatchReach = false;  // the last beat before a pass / kick meets his hands: both hands to ReachWorld (where the ball meets him, carried with his body) over the catch's own capture
	float Lean = 0.f;          // drive lean 0..1
	bool bStance = false;
	bool bPreSnap = false;     // before the snap: hand keyed stance loops
	bool bUnderCenter = false; // QB lined up under center (not shotgun / pistol)
	uint8 GetOffKind = 0;      // a lineman's job at the snap: 1 pass set, 2 run block, 3 defensive line (0 = not a lineman)
	bool bLook = false;
	FVector LookWorld = FVector::ZeroVector;
	bool bLookHigh = false;    // he is watching the ball in the air (the catcher, the men on the ball, the passer): the eyes may go up 45 deg, else 20
	bool bLive = false;       // the play is live (between the snap and the whistle)
	float CarrierDist = -1.f;  // a defender's distance to the ball carrier (cm), < 0 when there is none
};

/** AAthlete::ProbeBreaks: one frame of a free man's motion (time, the sim's heading and speed, the pelvis's yaw, the
 *  orientation warp and clip angle, planted-foot travel as shown and the feet planted) ... */
struct FAthleteBreakSample { float T = 0.f, Head = 0.f, Speed = 0.f, PelYaw = 0.f, Ang = 0.f, Orient = 0.f, Slide = 0.f; int32 Plant = 0; bool bAng = false; };
/** ... and a break being followed: its onset, when the sim's heading had turned 45 degrees, the first pick change, the
 *  pelvis's 45 degrees and the playing clip's travel back within 15 degrees of his (realign) after it, and the window's
 *  sums (warps where measured, slide). Group 0 the man a human drives, 1 a route runner, 2 anyone else; Kind the forecast
 *  he had (AAthlete::SimFutKind, 0 the old prediction). */
struct FAthleteBreak
{
	bool bOn = false;
	float T0 = 0.f, TSim = 0.f, H0 = 0.f, Sign = 1.f, Pel0 = 0.f, TPick = -99.f, TMesh = -99.f, TRealign = -99.f, TEnd = 0.f;
	double Ang = 0.0, Orient = 0.0, Slide = 0.0, PlantT = 0.0;
	int32 N = 0, AngN = 0;
	uint8 Group = 0, Kind = 0;
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
	/** Bench / staff, once a frame: the mocap body and its retarget stop animating while he is off screen (they run
	 *  every frame otherwise, seen or not) and pick the idle up again the frame he is back in shot. */
	void SleepWhenOffscreen(float Dt);
	/** A field player who is off the field (hidden by the director): his bodies stop ticking (the mocap graph, the pose
	 *  search and the retarget ran every frame otherwise, seen or not) and hold the pose they had. Waking restores each
	 *  body's tick exactly as it was; Drive, ShowReplayPose and StandOnSideline wake him by themselves. The same value
	 *  again costs one compare. A man who is down, falling, getting up or in a replay stays awake (ResetPose first). */
	void SetDormant(bool bDormant);
	bool IsDormant() const { return bDormantNow; }

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
	int32 LastPlantFeet = 0; // feet the capture held planted in the last evaluation (telemetry)
	// motion matching warp state for telemetry: what the playing clip's feet cover (cm/s, after the play rate), the stride
	// scale, the angle from the clip's travel to the man's (degrees, before the clamp) and the orientation warp applied
	float TelEff = 0.f, TelStride = 1.f, TelAng = 0.f, TelOrient = 0.f;
	float TelRootOff = 0.f; // how far the matched body's root is turned off the component's facing (deg, the offset node)
	float MMMisfitT = 0.f, MMForceCool = 0.f; // how long the matched clip has not fitted the move; time until the next forced pick
	float LastDev[7] = {0, 0, 0, 0, 0, 0, 0}; // shown pose vs the capture (UTagRetargetAnimInstance::Dev), telemetry
	/** Take the body's offset from the sim spot (a fall that ended away from it) as its position from now on. */
	/** Where the sim is steering this man (cm/s, world), for this frame's Drive: motion matching predicts his path
	 *  toward it. Without it the prediction only extends the acceleration measured so far, which at the snap is nothing:
	 *  a receiver coming off the line was read as a man starting to walk, and walked for 0.4 s of a sprint release. */
	void SetIntent(const FVector& WantVelW) { IntentVelW = WantVelW; IntentFrame = GFrameCounter; }
	FVector IntentVelW = FVector::ZeroVector;
	uint64 IntentFrame = 0;
	/** The sim's own forecast of his next 1.6 s, for motion matching's query (AMatchDirector::SyncPlay, this frame, while the
	 *  sim steers him and he is free: not engaged, not stunned). Called for every man; it forecasts the ones whose path
	 *  the sim already knows, read only from the sim (PlaySim::routeWant / steerVel, the law the sim moves him by):
	 *    - a route runner (EntState::Route, no pass in the air to him): his route, step by step, as PlaySim::predictPos;
	 *    - the man a human drives, with the stick held: his stick's want held, under the sim's acceleration and turn limits;
	 *    - a pursuer: his want held the same way.
	 *  An impulse under way (a juke, a catch) is carried through. The others, and anyone the call does not reach this frame,
	 *  keep the old prediction (the measured acceleration extended half a second). Alpha: how far the shown frame is between
	 *  the sim's last two steps. -TagNoSimFuture: the old prediction for all; -TagSimFutureNoPursue: not for pursuers;
	 *  -TagSimFutureAll: every free man's want held.
	 *  It also marks the man a human drives (his own motion matching settings, UTagMotionMatchInstance::bUserMan). */
	void SetSimFuture(const tag::PlaySim& Sim, const tag::Ent& E, int32 Dir, float Alpha);
	/** Contact reactions (Game/TagContact.h): contacts over the stumble threshold on men free to react, and reactions
	 *  played, since the last reset (all athletes). */
	static void StumbleStats(int32& OutOver, int32& OutPlayed, bool bReset);
	/** The director takes over from where the body stands (the post-whistle jog): the offset is gone and the follower
	 *  starts over from here (it read the hand-over as the sim stepping 60 cm and pulled the body back as far: a pop). */
	void AbsorbVisOffset()
	{
		// (a travel handed over by a capture that just ended goes with him: he stays where his body is)
		if (!PendingCarry.IsNearlyZero(1.f)) SetActorLocation(GetActorLocation() + FVector(PendingCarry.X, PendingCarry.Y, 0.f));
		PendingCarry = FVector::ZeroVector;
		VisOffset = FVector::ZeroVector; bFollowActive = false; LastSimLoc = GetActorLocation();
		AvoidOff = FVector::ZeroVector; // (his step-aside is in the place handed over too: AvoidOthers)
	}
	/** Personal space off the live play (AvoidOthers): before the snap and after the whistle, a man walking or jogging
	 *  steps aside from anyone he would pass inside half a metre of in the next 0.6 s (each takes half), on top of his sim
	 *  spot and eased; what he still has when something else takes him (the snap, a fall, a scene) eases out as any
	 *  offset does. The walk-ins and the jogs back ran men through each other (new2 defects: 22 high in 40 plays, none
	 *  live). -TagNoAvoid. */
	FVector AvoidOff = FVector::ZeroVector;
	FVector AvoidVelW = FVector::ZeroVector; // his velocity this frame, for the others' look ahead
	uint64 AvoidFrame = 0;                   // the frame he was last driven
	bool bAvoidUpright = false;              // on his feet then (an obstacle to the others)
	bool bAvoidListed = false;
	void AvoidOthers(const FVector& Location, const FVector& VelWorld, float DeltaTime);
	FVector PendingCarry = FVector::ZeroVector; // travel a capture leaves for the actor to take next frame
	// (GetBallHold) which hand carries the ball: the arm that stays tucked, smoothed and latched (per frame it flipped
	// hands every few frames as the free arm swung past the chest, bc5)
	mutable float BallSideEma = 0.f;
	mutable bool bBallLeft = false;
	mutable uint64 BallSideFrame = 0;
	// (GetBallHold) the visible body where its last pose was shown, before anything moved him this frame: a re-seat
	// (StartGetUp stands the actor where the body lies, turned to it) moves the mesh before its new pose is evaluated,
	// and the ball, read off the old pose in the new place, jumped 3.4 m for a frame (new2 defects: ball_teleport, held)
	FTransform ShownMeshXf = FTransform::Identity;
	uint64 ShownMeshFrame = 0;
	uint64 ReseatFrame = 0;   // the frame a re-seat moved him (StartGetUp)
	float ShownMoveCm = 0.f;  // how far his drive this frame expects him to go (sim speed x frame)
	void NoteShownMesh(float ExpectCm);
	bool GetBallHoldPose(FVector& OutLoc, FRotator& OutRot) const;
	FVector SepNow = FVector::ZeroVector; // the push that keeps him out of another body, eased (AMatchDirector::ResolveEngagedOverlap)
	float VisOffsetCm() const { return static_cast<float>(VisOffset.Size2D()); } // how far the body is from its sim spot (telemetry)
	float GetExertion() const { return Exertion; } // 0..1, the face's breathing (UpdateFace); the stadium's sweat on the kit reads it (A6)
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
	/** A one-shot for a man set before the snap (the quarterback calling an audible, a hard count, a linebacker's hand
	 *  signals): it plays over his stance and he settles back into it; the snap ends it. (An ordinary one-shot is
	 *  dropped while the men are set: nothing left over from the last play goes off in the stance.) */
	void PlayPreSnapShot(UAnimationAsset* Clip);
	bool PreShotPlaying() const { return bPreShot && OneShot != nullptr && OneShotLeft > 0.f; }
	void PlayOneShotFrom(UAnimationAsset* Clip, float Start); // from Start seconds into the clip (a library capture's moment less its lead)
	/** Seconds into the current keyed get-up (-1 when not getting up). */
	float GetUpElapsed() const;
	/** The sim says this player just caught the ball: pick the catch for the ball height, his speed, where the ball
	 *  came from and whether he is at the sideline (SidelineDirW: unit vector toward the nearest sideline, or zero). */
	void PlayCatch(float BallZCm, float SpeedCm, const FVector& BallVelW, const FVector& SidelineDirW,
		const FVector& BallPosW = FVector::ZeroVector, bool bContested = false, bool bAggressive = false, float CatchIn = -1.f);	// (CatchIn, on the catches / swats / kick catches / dives: seconds until the ball meets his hands. The library
	// capture then starts where its own hands-on-the-ball moment comes that long from now; < 0: from its lead, as before.)
	/** The sim just released a pass: play the throw that fits it (FAthleteAssets::EThrow). */
	void PlayThrow(int32 Kind);
	/** The throw from its windup, timed so the capture's release comes ReleaseIn seconds from now (when the sim lets the
	 *  ball go); a one-shot, so the follow-through plays out. */
	void PlayThrowTimed(int32 Kind, float ReleaseIn);
	/** The pass changed during its windup (the user's tap settled as a lob: Game/MatchFeel.cpp): the windup goes on as the
	 *  throw of Kind, timed to the same release, unless the clip playing is already one of Kind's (nothing to change). The
	 *  change of clip is dead-blended like any other (UTagRetargetAnimInstance). True: switched. */
	bool RetimeThrow(int32 Kind, float ReleaseIn);
	/** The last clip switch's transition jerk on his body (xfj: 1 = a switch the joints do not show; measured with
	 *  -TagDev / -TagHlFull only) and how many he has had measured. */
	float LastClipXfj = -1.f;
	int32 ClipXfjN = 0;
	/** A defender plays the ball in the air: a swat for how he is moving (Gait 0 running at it, 1 strafing, 2 backpedal /
	 *  set), or (bInt) a one-man interception. */
	bool PlayBallDefense(int32 Gait, bool bInt, float CatchIn = -1.f);
	/** A returner fields a kick (the pack's kick catches, on the run or set). */
	bool PlayKickCatch(bool bRunning, float CatchIn = -1.f);
	/** A back taking a toss: the pack's pitch catch that fits where it comes from, timed to its arrival. */
	bool PlayPitchCatch(const FVector& BallVelW, float CatchIn);
	/** A shovel pass (the pack's, on the move or set), timed to its release. */
	void PlayShovelTimed(bool bMoving, float ReleaseIn);
	/** A pump fake (the pack's). */
	void PlayPumpFake();
	/** A timed throw is playing (the ball sits in his throwing hand's grip, not between both hands). */
	bool IsThrowing() const;
	bool bThrowTimed = false;
	UPROPERTY() TObjectPtr<UAnimationAsset> ThrowTimedClip;
	int32 ThrowTimedKind = -1; // (the FAthleteAssets::EThrow it was picked for; -1 a shovel)
	/** Paired tackle: the first tackler's hit decides the carrier's reaction (head on / from behind / from the side)
	 *  and the pile's momentum (both players' masses and velocities). */
	void ApplyHit(const FVector& TacklerVelW, float TacklerKg, const FVector& TacklerLoc);
	/** Broken tackle: the defender whiffs (bCarrierLeft: the carrier slipped by on his left). */
	void PlayMissedTackle(bool bCarrierLeft);
	/** Broken tackle: the carrier bounces off contact (bHitFromRight: the defender came from his right). */
	void PlayBrokenTackle(bool bHitFromRight);
	/** Layout dive at an incompletion just out of reach (gets back up at the end). */
	void PlayDiveAttempt(float CatchIn = -1.f);
	/** A scrambling quarterback's slide (the sim ends the play where he goes down). */
	void PlaySlide();
	/** Turnovers. A loose ball: a dive at it, timed so his hands reach the turf in TouchIn seconds at AtW (the capture
	 *  whose remaining travel fits best, started that far before its touch). The scoop on the run. A strip: the second
	 *  man's punch at a carrier already going down (his part of a strip capture, from just before its punch). */
	bool PlayLooseDive(float TouchIn, const FVector& AtW);
	bool PlayScoop();
	bool PlayStripPunch(const AAthlete* Carrier);
	/** A pass the sim has rolled as intercepted (PlaySim::preRollCatch), CatchIn seconds before it gets there: the
	 *  defender and the receiver go into one of the pack's two-man interceptions together, picked by where the receiver
	 *  is relative to the defender, the defender's speed and the ball's height, from as far before its catch as there
	 *  is time. False if none loaded. */
	static bool PlayIntPair(AAthlete* D, AAthlete* R, float CatchIn, float BallZCm, int32 Which);
	/** A pass rolled as dropped: the receiver's drop capture, its hands at the ball in CatchIn seconds. */
	bool PlayDrop(float CatchIn);
	/** The celebration this man plays when his anim is "celebrate" (a turnover's own: null = his usual pick). */
	TObjectPtr<UAnimationAsset> CelebPick;
	/** Which hand the ball is drawn in for the next seconds (0 right, 1 left; a strip capture's carrying arm, where the
	 *  ball comes out of): GetBallHold's own pick otherwise. */
	void ForceBallHand(bool bRight, float Secs) { BallHandForce = bRight ? 0 : 1; BallHandForceUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + Secs : 0.0; }
	int32 BallHandForce = -1;
	/** A collision with another body: the velocity change it gave this one (world, cm/s). The upper body and the head
	 *  answer it on springs (FTagRetargetProxy::ApplyImpact). */
	void AddImpact(const FVector& DvW) { ImpactAccW += FVector(DvW.X, DvW.Y, 0.f); }
	double BallHandForceUntil = 0.0;
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
	/** A pass rusher beats his blocker: both men play their own part of the same two-man capture from its start (the pack's
	 *  rush wins; Which picks the capture, bBig one that leaves the blocker on his back). False when there is none. */
	static bool PlayRepWin(AAthlete* Rusher, AAthlete* Blocker, int32 Which, bool bBig);
	/** A rusher's move stoned: the pack's two-man capture of the rep he loses (the anchored bull rush for a power move). */
	static bool PlayRepHold(AAthlete* Rusher, AAthlete* Blocker, int32 Which, bool bPower);
	/** A double team (two blockers on one rusher): the three men on their parts of one of the pack's three-man captures,
	 *  each blocker the part on his own side. Returns the seconds it runs (0: none played). */
	static float PlayRepDouble(AAthlete* Rusher, AAthlete* B1, AAthlete* B2, int32 Which);
	/** A run block's double team (the sim's combo, Tune::comboBlock): the down lineman and his two run blockers on a
	 *  three-man capture of one (pack_dbl.json "run"), a drive when the blockers are winning the rep (bDrive), a stand-off
	 *  or the start of a climb otherwise. Each part ends when the sim takes the double apart (RunDblHeld). The seconds it
	 *  runs (0: none). Started from the down lineman's own drive (TryRunDouble), not the director. */
	static float PlayRepRunDouble(AAthlete* Down, AAthlete* B1, AAthlete* B2, int32 Which, bool bDrive);
	/** The down lineman: two run blockers on him now (the men whose block partner he is) start a run double. */
	bool TryRunDouble();
	/** The run blockers on this man this frame (up to two), and his co-blocker's for a blocker. */
	int32 RunBlockersOn(AAthlete* Out[2]) const;
	AAthlete* CoBlocker() const;
	/** His part of a run double still fits what the sim has (a blocker still on the man, the man still with two on him). */
	bool RunDblHeld() const;
	TWeakObjectPtr<AAthlete> RunDblOn; // the down lineman of the run double he is part of
	double RunDblNext = 0.0;           // (the down lineman) no new run double before this (world seconds)
	/** A handoff: the quarterback and the back on their parts of one of the pack's handoffs, timed so the exchange
	 *  comes ExchangeIn seconds from now. False when none fits. */
	static bool PlayRepHandoff(AAthlete* QB, AAthlete* Back, bool bGun, float ExchangeIn, int32 Which);
	/** The same, chosen for the play (the run exchange of PlaySim::mesh): the capture with the quarterback on the side of
	 *  the back's path the play has him (bQbLeft: on the back's left as the back runs), from the same alignment where
	 *  there is one, nearest the play's spacing (ArmCm, hip to hip) - and started only once the exchange is no further
	 *  off than the capture has before its own (false until then: ask again next frame with the same Which; bNone comes
	 *  back true when no capture fits at all). The two then close onto the capture's own spacing as the ball changes
	 *  hands (Drive: MeshMate). */
	static bool PlayRepHandoffFit(AAthlete* QB, AAthlete* Back, bool bGun, bool bQbLeft, int32 RunKind, float ArmCm, float ExchangeIn, int32 Which, bool& bNone,
		int32 FakeSide = 0, float BackMs = -1.f, float QbMs = -1.f);
	/** In a two-man handoff (PlayRepHandoffFit) and the ball still to change hands: the other man of it. */
	AAthlete* MeshPartner() const { return MeshMate.Get(); }
	/** ... and the sim's latest word on when it does (seconds from now): his part of the capture is played a little
	 *  faster or slower (0.85-1.18) so that its exchange is the sim's, and the pair's closing is timed to it. */
	void MeshRetime(float ExchangeIn);
	/** The exchange is off (a zone read the quarterback keeps, an RPO he pulls to throw): his part of the two-man handoff
	 *  is let go over a fifth of a second and he is his own man again. */
	void BreakMesh();
	/** Seconds of his handoff capture part still to play to its contact frame (the back's hands at the ball); <= 0 once
	 *  there, 99 when he is not in one. The ball is handed over on this, not on the sim's clock. */
	float MeshContactIn() const;
	/** The football's toss / handoff in the air between two men is the director's; this is where a man's hands are for
	 *  it: the mid point of his two hands (the back's pocket as he takes a handoff, a tosser's hands). */
	FVector HandsMid() const;
	/** Play action: the quarterback and the back into one of the pack's fakes, the mesh MeshIn seconds from now. */
	static bool PlayRepPaFake(AAthlete* QB, AAthlete* Back, bool bGun, bool bBoot, float MeshIn, int32 Which);
	/** Pancake: the blocker drives through and finishes over the man he put down. */
	void PlayPancakeFinish();
	/** Keyed fall to the turf, see bFalling. bEaseIn: glide from where the actor is onto From in the first 0.15 s. */
	void BeginPairedFall(UAnimationAsset* Clip, const FVector& From, const FVector& To, float YawW, float DriveTime,
		float LieTime, bool bEaseIn, float Start = 0.f, bool bRootDrive = false);
	bool IsFalling() const { return bFalling; }
	/** His part in a staged capture (a field goal's snap, hold and kick): the clip plays from Start with its root
	 *  carrying the body from the world anchor and heading, as captured. It runs while the director drives him with
	 *  the anim "scene" (any other drive ends it); at its end he holds the last pose. A loop stays on its anchor. */
	void BeginScene(class UAnimSequence* Clip, const FVector& AnchorW, float YawW, float Start = 0.f, bool bLoop = false);
	bool InScene() const { return bScene; }
	float SceneClipTime() const { return SceneStart + SceneT * SceneRate; }
	/** The scene clip's play rate (after BeginScene, which resets it to 1): a field goal's operation timed to the sim's. */
	void SetSceneRate(float Rate) { SceneRate = FMath::Clamp(Rate, 0.5f, 2.f); }
	/** On the ground, falling, or getting up: the body is not his to move. */
	bool IsDown() const { return bFalling || bGettingUp || bRagdoll || FallHoldLeft > 0.f || TackledLeft > 0.f; }
	bool IsGettingUp() const { return bGettingUp; }
	/** Torn out of a tackle that had begun: off the fall's capture and back on his feet where the sim has him. */
	void BreakFree();
	/** His hips over the turf as shown (cm), and where they are. */
	float PelvisZ() const;
	FVector PelvisAt() const;
	/** Where the user marker goes: on the turf under his visible body (the pelvis, its gait sway smoothed out), not
	 *  his actor - a capture's root motion carries the body off the actor until the capture hands the travel back. */
	FVector MarkerFeet() const;
	mutable FVector MarkerOffSm = FVector::ZeroVector;
	mutable uint64 MarkerFrame = 0;
	/** The bodies his arms keep out of this frame (world, w = radius): the men he is in a pile with. */
	void SetKeepOut(const FVector4f* S, int32 N) { KeepOutN = FMath::Min(N, 12); for (int32 i = 0; i < KeepOutN; ++i) KeepOut[i] = S[i]; }
	/** His own head, chest and hips as spheres (world, w = radius), as shown last frame; and his hands. */
	int32 BodySpheres(FVector4f* Out) const;
	bool Hands(FVector& L, FVector& R) const;
	bool Feet(FVector& L, FVector& R) const;
	/** Any two of the visible body's bones (the elbows: lowerarm_l / _r, the knees: calf_l / _r). */
	bool BonePair(const TCHAR* Ln, const TCHAR* Rn, FVector& L, FVector& R) const;
	FVector4f KeepOut[12];
	int32 KeepOutN = 0;
	/** Where the pile ends: the carrier's anchor at the end of the drive. */
	FVector FallAnchorEnd() const { return AnchorB; }
	FVector GetLastVel() const { return LastVelW; }
	FVector PileBodyEnd = FVector::ZeroVector; // the carrier's body at the end of a paired fall
	/** Move where a running paired fall's drive ends (the sim's pile stopped somewhere else). */
	void ShiftFallTarget(const FVector& Delta);
	/** The running fall's clip time (its start plus the time fallen, held a frame short of the end). */
	float FallClipTime() const;
	/** Where a root-carried fall (BeginPairedFall with bRoot) takes his actor from its start, facing Yaw: the same root
	 *  motion TickFall applies, so a fall can be planned to end where it will. Zero for a clip without root motion. */
	FVector FallRootTravel(const UAnimationAsset* Clip, float Start, float Yaw) const;
	/** Where a scene clip's root motion takes him between two clip times, turned to a scene yaw (BeginScene's frame). */
	FVector SceneTravel(const UAnimSequence* Seq, float From, float To, float Yaw) const;
	/** The velocity a fall's glide starts at (cm/s; set after BeginPairedFall: a pile shares one, the momentum of the men
	 *  in it, so neither drives into the other while the closing speed is spent in the hit). */
	void SetFallCarry(const FVector& V) { FallV0 = FVector(V.X, V.Y, 0.f).GetClampedToMaxSize(1000.f); }
	/** Tackled with nobody to pair him with (the tackler already on his way down): he goes down on his own, now. */
	void FallAlone(const FVector& SimLoc) { if (!bFalling && !bGettingUp) { FallHoldLeft = 0.f; TackledLeft = 0.f; BeginSoloFall(SimLoc, LastVelW); } }
	/** Hit as he caught it: stay on the catch (held where the hit landed) for Secs before any fall takes him down. */
	void HoldForCatch(float Secs) { if (!bFalling) { CatchHoldLeft = Secs; CatchHoldAt = GetActorLocation(); } }
	/** Body mass (kg) from the player's weight (100 without one). */
	float MassKg() const;
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
	/** Where set linemen look before the snap: the head's forward axis below (-) or above the horizon, degrees, over
	 *  every pre-snap frame of a lineman standing still once he has been set half a second (the look has eased in;
	 *  mean, the lowest, sample count). */
	static void GazeStats(double& OutMeanDeg, double& OutMinDeg, int32& OutSamples, bool bReset);

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
	// SetSimFuture: where the sim takes him, world cm, every 1/30 s from the sim's latest step (index 0) to 1.6 s; worked
	// out again only when the sim has stepped. Fresh for this frame's motion matching only (SimFutFrame).
	FVector SimFut[49];
	uint64 SimFutFrame = 0;
	double SimFutT = -1.0;
	uint64 SimFutSerial = 0; // (the play's PlaySim::serial)
	float SimFutLag = 0.f;   // the shown frame is this far (s) behind the sim's latest step
	uint8 SimFutKind = 0;    // 1 his route, 2 the stick, 3 his want held (a pursuer)
	uint64 UserFrame = 0;    // the frame a human was last seen driving him (SetSimFuture)
	uint64 RouteFrame = 0;   // ... and he was last running a route
	bool IsUserMan() const { return UserFrame == GFrameCounter; }
	/** The motion matching sets of his body (Tools/Art/mm_style_sets_hero.py): 0 big (the line), 1 medium (linebackers,
	 *  tight ends, fullbacks, power backs), 2 agile (receivers, the secondary, light backs); -1 none. */
	int32 BodyClass() const;
	float LiveT = 0.f;       // seconds into the live play (the tired gaits wait 2 s)
	// the break probe (-TagHlFull / -TagMoveTest / -TagPadPlayTest / -TagBreakLog): how soon the body answers a sharp change
	// of the sim's heading - the first matched pick change, the pelvis's turn, the warps and the planted-foot travel
	// around it (AAthlete::ProbeBreaks, "TAG break lag" in the log)
	void ProbeBreaks(const FVector& VelWorld, float Dt);
	TArray<FAthleteBreakSample> BreakRing; // the last 0.9 s
	float BreakClock = 0.f;
	float BreakCool = 0.f;
	TArray<float> PickTimes;               // matched pick changes over the last second (BreakClock)
	TWeakObjectPtr<const UAnimationAsset> BreakPickAsset;
	FAthleteBreak Brk;
	// contact reactions (Game/TagContact.h): the contacts read so far, and how long until he may stumble again
	void ReadContacts(const FString& Anim, const FVector& VelWorld, float Dt);
	uint64 ContactCursor = 0;
	bool bContactCursorSet = false;
	float StumbleCool = 0.f;
	FVector ProbeVelW = FVector::ZeroVector; // last frame's sim velocity (the break probe reads the frame it describes)
	// the transition jerk measured on his body (UTagRetargetAnimInstance::JerkOut), pooled over everyone (log "TAG xfj")
	static void PoolJerk(TArray<FVector2f>& In);
	int32 SidelineVariant = 0;
	bool bAnimAwake = true;  // SleepWhenOffscreen: the bodies' ticks are on
	float SleepClock = 0.f;  // seconds since the first SleepWhenOffscreen (the first second always animates: a pose to freeze in)
	bool bDormantNow = false; // SetDormant: the bodies' ticks are off, as they were kept in DormantTick
	bool DormantTick[4] = {true, true, true, true}; // Body, MHBody, MHFace, HeroHelmet: ticking when he went dormant
	bool bLabelKnown = false; // SetLabel: LabelShown is what the label component holds
	FString LabelShown;
	// SetFootball: whether the clip he is on is a ball-carrier capture, kept per asset (its name was tested every frame)
	mutable TWeakObjectPtr<const UAnimationAsset> CarryClipFor;
	mutable bool bCarryClipIs = false;
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
	int32 GetOffPending = 0; // frames a lineman's first step still waits for the sim to say which way he goes
	bool bUnderCenter = false;
	uint8 GetOffKind = 0;
	UPROPERTY()
	TObjectPtr<UAnimationAsset> OneShot;   // keyed one-shot in progress (get-off, snap catch, handoff)
	float OneShotLeft = 0.f;
	bool bOneShotCatch = false;  // the running one-shot is a catch (it hands over to the run once he is away with it)
	bool bPreShot = false;       // the running one-shot was asked for before the snap (PlayPreSnapShot)
	float OneShotAge = 0.f;
	float OneShotStart = 0.f;     // seconds into the one-shot's clip it starts at (a library capture's moment less its lead)
public:
	/** -TagArmSweepOff: the 27 Nov animation sweep's changes off (A/B: the pack loader's floor, the arm layers over captures
	 *  that hold the ball, the throw / catch key alignment) - the game as it was before it. */
	static bool ArmSweepOff();
	/** The running one-shot's key moment in its own clip (s): a throw's release, a catch's hands on the ball; < 0 unknown. */
	float OneShotKeyT = -1.f;
	/** Seconds from now until the running one-shot reaches its key moment as played (+: still to come, -: past); NAN when
	 *  no one-shot with a known key moment is playing. Telemetry for the key-moment timing check (TAG keytime). */
	float OneShotKeyIn() const;
	/** Where the running one-shot is in its clip (s), or -1. */
	float OneShotClipT() const;
	/** Time-warp the running timed one-shot so its key moment (OneShotKeyT) comes EventIn seconds from now: its rate is
	 *  steered within 0.7-1.4 (the sim moved the release, the ball arrives sooner than the flight's plan). */
	void AlignKey(float EventIn);
	float OneShotPos = 0.f; // where the one-shot is in its clip (s): its start plus each frame's time at that frame's rate
	/** The arms against the capture this evaluation (UTagRetargetAnimInstance::Arm), telemetry. */
	float LastArm[17] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
private:
	// get-up after a ragdoll: snapshot the lying pose, play the matching keyed get-up where the body lies
	void StartGetUp(const FVector& SimLoc);
	// keyed fall to the ground (paired tackle, pancake, solo knockdown): the actor is anchored and the clip carries the
	// body; the anchor slides from From to To over DriveTime (the pile driving onto the sim's spot). Ends lying still
	// for LieTime, then the keyed get-up out of the landing.
	bool bFalling = false;
	UPROPERTY()
	TObjectPtr<UAnimationAsset> FallClip;
	// a part in a staged capture (BeginScene)
	bool bScene = false, bSceneLoop = false;
	UPROPERTY()
	TObjectPtr<class UAnimSequence> SceneClip;
	float SceneT = 0.f, SceneStart = 0.f, SceneYaw = 0.f;
	float SceneRate = 1.f;  // (SetSceneRate: the clip's seconds per real second)
	FVector SceneAnchor = FVector::ZeroVector;
	FVector SceneEaseOff = FVector::ZeroVector; // where he stood off the capture's start, eased out over 0.3 s
	float SceneEaseYaw = 0.f;                   // and how far he faced off its start heading (deg)
	void TickScene(float DeltaTime);
	float FallStart = 0.f;        // seconds into FallClip the fall starts (a library capture's contact)
	bool bFallRoot = false;       // the clip's root motion carries the body through the fall (library captures)
	float FallT = 0.f, FallLen = 0.f, FallLie = 0.f, FallDrive = 0.4f, FallFromYaw = 0.f, FallYaw = 0.f;
	FVector FallFrom = FVector::ZeroVector, AnchorA = FVector::ZeroVector, AnchorB = FVector::ZeroVector;
	FVector FallV0 = FVector::ZeroVector; // his velocity going into the fall (the glide onto it starts at it)
	bool bFallEase = false;
	float FallHoldLeft = 0.f; // down, waiting for the director to pair the fall; a solo fall after this
	float CatchHold = 0.f;    // (unused: the hold is CatchHoldLeft)
	float CatchHoldLeft = 0.f;  // HoldForCatch: on his catch, where he made it, whatever the sim's tackle moved him to
	FVector CatchHoldAt = FVector::ZeroVector;
	void TickFall(const FVector& SimLoc, float DeltaTime);
	void BeginSoloFall(const FVector& SimLoc, const FVector& MoveDirW);
	float DownTime = 0.f;
	float GetUpLeft = 0.f;
	bool bGettingUp = false;
	bool bStoodUp = false;
	FVector VisOffset = FVector::ZeroVector;
	/** The body follows the sim on its own feet (motion matched locomotion): each frame it moves by what the playing
	 *  clip's feet cover (the root travel as played, stride and orientation warp included), steered at the sim's spot.
	 *  The planted foot then stays planted by construction; what the clip cannot do becomes a short lag behind the sim
	 *  (VisOffset, at most FollowMax) that the next steps make up, instead of a foot skating under him. */
	FVector FollowVel = FVector::ZeroVector; // the body's own velocity this frame (cm/s, world)
	bool bFollowActive = false;              // set by DriveMotionMatching for the next frame
	FVector LastSimLoc = FVector::ZeroVector;
	float FallEaseT = 0.2f;
	float OrientWarpSm = 0.f; // the smoothed orientation warp (deg) handed to the retarget layer
	float ClipStrideSm = 1.f; // the smoothed stride scale of a followed single clip
	int32 TackleCount = 0;
	float StillTime = 0.f;   // seconds standing still (post-play idles)
	FVector StillRef = FVector::ZeroVector; // where he stood at the start of last frame's drive
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
	TWeakObjectPtr<UAnimationAsset> LineHeld; // the line clip he is on, and for how long (a clip is given half a second)
	float LineHeldT = 0.f, LineHeldRate = 1.f;
	float LineStillT = 0.f;   // how long a locked-up man on the walk has had nothing to cover
	float LagTime = 0.f;      // how long a followed one-shot has sat at the follow limit
	double DropTravelUntil = 0.0; // the blend out of the layout dive leaves the dive's travel behind (the actor took it)
	bool bPocketLoop = false; // the quarterback is on the standing pocket loop
	int32 QBWalk = -1;        // the in-pocket walk / jog he is on (index; +100 for the jogs), -1 none
	float QBWalkT = 0.f;      // how long he has been on it
	int32 LineWalk = -1;      // the pass-block walk he is on while the pile moves (index into BlockWalks, -1 none)
	bool bRepClip = false;    // playing his part of a two-man capture: its hands are where the capture put them (no reach)
	bool bOneShotRep = false; // the running one-shot is such a part
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
	int32 OfficialPatch = -1;                   // an official's back patch (the pack's strip: DJ LJ R U BJ SJ FJ), -1 none
	bool bCapTint = false;                      // the referee: the pack's black official's cap repainted white
	FLinearColor CapTint = FLinearColor::White;
	void HideHair(bool bHide);
	void PaintHero(const FUniform& U);
	FUniform CurUniform{};
	int32 HeroTone = 1;      // 1..8, the pack's skin tones (arms and legs match the head)
	bool bHeroHelmetOn = true;
	FVector VisRelScale = FVector::OneVector;
	// smoothed football-layer weights
	float CarryA = 0.f, SecureA = 0.f, ReachA = 0.f, LeanA = 0.f, StanceA = 0.f, StanceHandA = 0.f, LookA = 0.f;
	FVector ReachSmoothed = FVector::ZeroVector, LookSmoothed = FVector::ZeroVector;
	// the catch reach (FAthleteFootball::bCatchReach): its target in the body's own space, kept while the reach eases
	// out after the catch (a world point would be left metres behind a man running on, and pull his arms back at it)
	FVector CatchReachLocal = FVector::ZeroVector;
	bool bCatchReachHeld = false;
	float GazeSetT = 0.f; // seconds this lineman has stood set before the snap (GazeStats)
	float LookUpMax = 20.f; // how far up his face may turn (eased: 45 deg watching a ball in the air, 20 otherwise)
	float HeadUpT = 0.f, HeadUpMax = 0.f; // -TagHeadLog: how long his face has been turned up at the stands
	float ArmSlot = 0.f, StyleJitter = 0.f;
	/** Signature (Research/NextLevel 01 #8, #10; -TagNoSignature: the old picks by roster id). A number of his own from who
	 *  he is - his name and position, not where the roster put him - and from his body and ratings his gait (-TagSigGait,
	 *  opt in: it cost ball-in-hand accuracy for little that reads on screen): the cadence
	 *  his matched loops play at (the stride warp makes up the speed: under 1 a long strider, over 1 quick short steps)
	 *  and his posture at speed (forward lean: + low and compact, - upright). In each capture library a man prefers two of
	 *  the full pool (every third pick still the game's rotation); a Superstar or X-Factor always his first, and his
	 *  locomotion searches his body class's captures first, the rest at a cost (FAthleteAssets::SigClassSets). The pools
	 *  are never cut. */
	uint32 SigId = 0;
	float SigCadence = 1.f;
	float SigLeanDeg = 0.f;
	// (SigBase slots. Stances, breakdowns and post-play stands stay by roster id: the line and the huddle are groups, and
	// consecutive ids keep neighbours on different captures)
	static constexpr int32 SigMove = 1;
	static constexpr int32 SigThrow = 2;
	static constexpr int32 SigDrop = 3;
	static constexpr int32 SigKick = 4;
	static constexpr int32 SigCeleb = 5;
	void SetupSignature();
	bool IsStarMan() const;
	/** A fixed pick of his for a library (what Player->id was): his own number for the slot, or the id with -TagNoSignature. */
	int32 SigBase(int32 Slot) const;
	/** A library pick sequence: Seq (the game's rotation) turned into his preference (see SigId). */
	int32 SigSeq(int32 Slot, int32 Seq) const;
	// body physics inputs (TagRetargetAnimInstance): the lean from the measured acceleration, planted feet pinned
	void MeasureMotion(const FVector& SimPos, float Dt);
	FVector LeanSmoothed = FVector::ZeroVector;
	FVector ImpactAccW = FVector::ZeroVector; // collisions' velocity changes this frame (world, cm/s), for the body layer
	// the quarterback's drop and pocket, the defender's breakdown (SetFootball feeds these a frame ahead of Drive)
	bool bHasBallF = false, bLiveF = false, bDropped = false, bCoverBallF = false;
	bool bOneShotPA = false; // the one-shot playing is his part of a play-action fake (PlayRepPaFake) or of a handoff (PlayRepHandoffFit)
	/** The run exchange (PlayRepHandoffFit): the other man of the two-man handoff he is in, which of the two he is, where
	 *  the capture has the back around the quarterback as the ball changes hands (cm in the quarterback's frame: X
	 *  forward, Y right), the back's heading off the quarterback's there (degrees, UE yaw) and the world time of the
	 *  exchange. Drive closes the pair onto that over the approach and lets go after it. */
	TWeakObjectPtr<AAthlete> MeshMate;
	bool bMeshQB = false;
	FVector2D MeshOff = FVector2D::ZeroVector;
	float MeshYawRel = 0.f;
	float MeshClipT = 0.f; // (the exchange, seconds into his part of the capture)
	bool bMeshBack = false;  // the back of a handoff capture, until he is on his run (motion matching skips the starts)
	TWeakObjectPtr<AAthlete> MeshSepMate; // the other man of the handoff, kept 60 cm off until 0.8 s after the exchange
	bool bMeshFake = false;  // the quarterback of one, while his part plays (his turn into the fake is a step-turn, 300 deg/s)
	double MeshAtW = 0.0;
	float CarrierDistF = -1.f, DropLeft = 0.f;

	UPROPERTY()
	TObjectPtr<UAnimationAsset> CurrentClip;
	bool bRestartClip = false; // a one-shot asked for again while it plays: restart it, crossfaded from the pose shown


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
	/** The mocap on its own skeleton: the pack was captured on the model pack's MaddenFB_Hero_Skeleton (same bones, same
	 *  bind pose to the micron), so with the pack bodies every clip, blend space and motion matching database comes from
	 *  /Game/TAG/AnimH (Tools/Anim/build_*.sh -r hero, Tools/Art/*.py with TAG_ANIM_RIG=hero), the hidden source mesh is
	 *  the hero rig, and the bodies copy its pose bone for bone: the capture as captured. Without it (MetaHumans,
	 *  -TagUEFNAnim for an A/B) the tree is /Game/TAG/Anim on the UEFN mannequin, retargeted twice on the way to a pack
	 *  body (pack -> UEFN in Blender, UEFN -> pack at runtime: 114 mm mean joint error, Tools/Anim/mocap_fidelity.py). */
	bool bNativeAnim = false;
	FString AnimRoot = TEXT("/Game/TAG/Anim");
	TArray<FHeroItem> HeroItems;
	TObjectPtr<class UIKRetargeter> HeroRetargeter;
	TObjectPtr<class UIKRetargeter> CrowdRetargeter;
	/** The runtime retargeter for a skeleton family (hero, crowd, male, female, child, sideplayer). With bNativeAnim,
	 *  nullptr for the rigs that share the hero rig's bind pose (hero, male, side player, crowd): those copy the pose. */
	class UIKRetargeter* RetargeterFor(const FString& Rig);
	/** A clip baked for the people that stand around (Idle, Cheer, Clap, Kneel) on a skeleton family. */
	UAnimationAsset* HeroClip(const FString& Rig, const TCHAR* Clip);
	TMap<FString, TObjectPtr<class UIKRetargeter>> HeroRetargeters;
	TMap<FString, TObjectPtr<UAnimationAsset>> HeroClips;
	TArray<TObjectPtr<class UTexture>> HeroSkinArm, HeroSkinLegs, HeroEyes;
	TArray<TObjectPtr<class UTexture>> HeroSkinArmRSM; // each skin tone's own roughness / specular / scatter map
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
	/** The pack's two-man pass-rush captures, each man his own part: D the defensive lineman's (__P01: in the wins he is
	 *  the one who gets past and runs on), O the blocker's (__P02). Before, both men of every locked-up pair played the
	 *  defender's half of the stand-off loop, a beaten blocker the winner's rush and the rusher the loser's half of it.
	 *  Loops: the stand-off; Wins: the rusher past his man; BigWins: the blocker put on his back; Holds: a rush stoned. */
	struct FLineRep { TStrongObjectPtr<UAnimSequence> D, O; };
	TArray<FLineRep> RepLoops, RepWins, RepBigWins, RepHolds, RepPowerHolds;
	/** The pack's three-man pass-block double teams (pack_dbl.json, Tools/Art/pack_dbl.py): the defender's part, the
	 *  blocker on his left (A) and on his right (B), and the stretch [T0, T1] where both are on him and the pile holds. */
	struct FDoubleRep { TStrongObjectPtr<UAnimSequence> D, A, B; float T0 = 0.f, T1 = 0.f; int32 Out = 0; };
	TArray<FDoubleRep> RepDoubles;
	// run blocks' double teams (pack_dbl.json Kind "run"; Out 0 drive, 1 hold, 2 climb; the defender's wins are left out)
	TArray<FDoubleRep> RepRunDoubles;
	/** The pack's two-man handoffs (pack_handoffs.json, Tools/Art/pack_handoffs.py): the quarterback's part, the back's,
	 *  the exchange T seconds in, and whether it is from the shotgun / pistol. */
	struct FHandoffRep
	{
		TStrongObjectPtr<UAnimSequence> Q, B;
		float T = 0.f;
		bool bGun = false;
		float Bx = 0.f;
		// (the back around the quarterback as the ball changes hands, m: By back of him (negative: in front), BYaw his
		// heading off the quarterback's, degrees, + = to the left; and from those the quarterback's place beside the
		// back's path: Lat m to the back's left (negative: his right), Along m ahead of abreast)
		float By = 0.f, BYaw = 0.f, Lat = 0.f, Along = 0.f;
		int32 Kind = 0;      // the run it was filmed as: 0 inside, 1 stretch, 2 draw (the json's "Kind"; none: inside)
		bool bSnap = false;  // filmed from the snap (the quarterback's whole footwork), not only the mesh
		// the two men's speeds over the 0.2 s before the exchange (m/s: "Vq", "Vb") and the quarterback's carry-out after
		// it, from the capture's name: +1 a boot to his left, -1 to his right, 0 a drop or nothing named
		float Vq = -1.f, Vb = -1.f;
		int32 Fake = 0;
		// at the contact frame (T): how near the back's hands come to the quarterback's ball hand (m, "Dc"), and the
		// closest the two bodies come within 0.4 s of it (m, "MinSep"); -1: not measured (an older list)
		float Dc = -1.f, MinSep = -1.f;
	};
	TArray<FHandoffRep> RepHandoffs;
	/** The pack's two-man play-action fakes (pack_pafakes.json): the quarterback's part (the fake, then his drop or his boot)
	 *  and the back's (the mesh and the carry-out), T the mesh. */
	struct FPaFakeRep { TStrongObjectPtr<UAnimSequence> Q, B; float T = 0.f; bool bGun = false, bBoot = false; float Bx = 0.f, Len = 0.f; };
	TArray<FPaFakeRep> RepPaFakes;
	bool bDoublesLoaded = false;
	void LoadPackDoubles();
	/** The pack's pass-block walks (hands up, on the move: forward, back, sideways, the diagonals), for a locked-up man
	 *  while the pile travels, each with the way its root goes (component space, unit). The stand-off and the engaged
	 *  loops are captures of men holding their ground: under a pocket giving ground at 0.5-1 m/s their feet skated. */
	TArray<TStrongObjectPtr<UAnimSequence>> BlockWalks;
	TArray<FVector> BlockWalkDir;
	TArray<float> BlockWalkSpeed; // cm/s each walk covers
	/** The pack's quarterback-in-the-pocket set: walks and jogs to all eight sides with the ball held ready, as captured
	 *  (MG_QB_InPocket_Walk / Jog D1-D8). A quarterback sliding or stepping up plays the one that goes his way. */
	struct FWalkSet { TArray<TStrongObjectPtr<UAnimSequence>> Clips; TArray<FVector> Dir; TArray<float> Speed; };
	FWalkSet QBWalks, QBJogs;
	/** A quarterback on the run with the ball (MG_Loco_QB_Scramble_Jog, MG_Loco_QB_Run_ShortStride) and the speed each covers. */
	TStrongObjectPtr<UAnimSequence> QBScramble, QBRun;
	/** A ball carrier covering it up (MG_BC_Coverball Idle / Jog / Run / Sprint, D1) and the speed each covers. */
	TStrongObjectPtr<UAnimSequence> CoverIdle, CoverJog, CoverRun, CoverSprint;
	float CoverJogSpeed = 300.f, CoverRunSpeed = 590.f, CoverSprintSpeed = 780.f;
	float QBScrambleSpeed = 450.f, QBRunSpeed = 650.f;
	/** The pack's field goal operation, a three-man capture (FG_Normal_Kick_v1: P03 the snapper, P01 the holder, P02
	 *  the kicker) with the loops the three wait in, and where each starts in the capture's world (m: x left and y back
	 *  of the spot the ball is kicked from, heading in degrees turned left of downfield). */
	struct FKickScene
	{
		TStrongObjectPtr<UAnimSequence> Snap, Hold, Kick, SnapWait, HoldWait, KickWait;
		bool Ok() const { return Snap.IsValid() && Hold.IsValid() && Kick.IsValid(); }
	};
	FKickScene FgScene;
	/** A help-up after the whistle (Getup_NonBC_15_HelpUp_Knees_D7 P02 the man on his knees, P01 the teammate who pulls him up). */
	TStrongObjectPtr<UAnimSequence> HelpUpDown, HelpUpBy;
	/** A call at the line: the quarterback's audible (shotgun, under center, a hand signal), the back and the
	 *  linebackers who hear it and re-settle, the linebacker who answers with his own. */
	TStrongObjectPtr<UAnimSequence> QBAudibleGun, QBAudibleUnder, QBSignal, HBAudible, LBHearsAudible, LBDefAudible;
	/** The punt the same way (Punt_Normal_Kick_v1: P02 the snapper, P01 the punter 12 m behind him; no holder), and
	 *  the returner's fair catch (the wave, then the catch). */
	FKickScene PuntScene;
	TStrongObjectPtr<UAnimSequence> FairCatch;
	/** A blocked kick's rusher: Madden's field goal block, the edge man off a three-point stance and laid out across
	 *  the kick (FGBlock_Left_Edge_Block_v2__P03; he leaves his feet at FgBlockAt). */
	TStrongObjectPtr<UAnimSequence> FgBlockRush;
	static constexpr float FgBlockAt = 2.2f;
	TObjectPtr<UAnimationAsset> BlockPassSet;   // pass protection set before contact
	/** A blocker with nobody on him: the pack's pass-block ready stance (linemen) and a back's / tight end's blocking idle. */
	TObjectPtr<UAnimationAsset> BlockReadyOL, BlockReadyBack;
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
	/** The spectacular ones: one-handers, fingertips, leaps (on his feet after: a catch the sim runs on from). */
	TArray<FPackClip> PackCatchSpectacular;
	/** A back taking a pitch (Catch_Pitch_<45|90|135>_<height>): timed to the toss's arrival (arm sweep 2). */
	TArray<FPackClip> PackCatchPitch;
	/** The referee's touchdown signal (Ref_Touchdown_Signal), played by the referee and the nearer wing on a touchdown. */
	TObjectPtr<UAnimationAsset> RefTdSignal;
	/** A scrambling quarterback giving himself up: the feet-first slide (started a beat before he goes down). */
	TArray<FPackClip> PackQBSlide;
	/** A defender playing the ball in the air: the pack's swats on the run, strafing, backpedalling / set, and its
	 *  one-man interceptions. */
	TArray<FPackClip> PackSwatRun, PackSwatStrafe, PackSwatBack, PackIntCatch;
	/** A returner fielding a kick: the pack's kick catches, set and on the run. */
	TArray<FPackClip> PackKickCatchStand, PackKickCatchRun;
	TArray<FPackClip> PackShovelSet, PackShovelMove, PackPumpFakes;
	TArray<FPackClip> PackTackledFront, PackTackledBehind, PackTackledSide, PackTacklers, PackWhiffs, PackStumbles;
	/** A man knocked off his line who stays up (AAthlete::ReadContacts): the pack's traffic bumps (walking, jogging or
	 *  backpedalling into a man: Avoidance_Bump, TwoMan_Neg_Bump), the jams a receiver loses (DB_Jam_Loss, DB_WR_Jam) and
	 *  a stumble off a hit (NonBC_Stumble_Hit). The ball carrier's are PackStumbles, upright ones only. */
	TArray<FPackClip> PackBumps;
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
	struct FPackPair { TObjectPtr<UAnimationAsset> T, C; float Ox = 0, Oy = 0, Yaw = 0, EndX = 0, EndY = 0, TLie = 0.45f, CLie = 1.05f, Start = 0, Sep = 0; FString Name; };
	TArray<FPackPair> PackPairs[6];
	/** A carried ball in a capture (Tools/Anim/pack_role_check.py): the RightFootball / LeftFootball socket held still
	 *  at the chest (spine_05) over [T0, T1] s - min over the hands of the mean + 1.5 std of its distance (cm; ~30-40
	 *  for a carrier, 50-80 for a man pumping his arms or reaching to tackle). < 0: no ball sockets. Cached. */
	static float CarryScore(const UAnimSequence* Seq, float T0, float T1);
	/** The capture the game plays on the sim's carrier (CSeq) is the man with the ball: not the tackler's (TSeq) or the
	 *  second man's (ASeq), and the ball does not end with one of them (unless bBallLeaves: a strip). The pack's
	 *  participant order is not the roles (Madden's __P01 was the tackler in 84 of 381 pairs): a capture that would put
	 *  the tackler's motion on the carrier is left out. False with the reason in OutWhy. -TagNoRoleGuard: always true. */
	static bool CarrierHoldsBall(const UAnimSequence* CSeq, const UAnimSequence* TSeq, const UAnimSequence* ASeq, float Start, bool bBallLeaves, FString& OutWhy);
	/** A two-man interception's defender capture (DSeq) is the man who comes away with the ball: over 0.2-1.2 s after
	 *  the catch (CatchT) the receiver's (RSeq) is not clearly the one holding it still at his chest while the defender's
	 *  is not (else the sim's interceptor would play the receiver). False with the reason. -TagNoRoleGuard: true. */
	static bool DefenderHoldsBall(const UAnimSequence* DSeq, const UAnimSequence* RSeq, float CatchT, FString& OutWhy);
	void LoadPackPairs(int32 Seed);
	/** The pack's three-man gang tackles (Tools/Art/pack_gangs.py: pack_gangs.json): the carrier, the first tackler and
	 *  the man who joins him, from the first contact (Start). Tx/Ty/TYaw and Ax/Ay/AYaw: the two tacklers' roots in the
	 *  carrier's frame then (m; x left, y back; deg, + turns left); AHit: when the second reaches him; EndX/Y: where
	 *  the carrier's pelvis ends. */
	struct FPackGang { TObjectPtr<UAnimationAsset> C, T, A; float Start = 0, AHit = 0, Tx = 0, Ty = 0, TYaw = 0, Ax = 0, Ay = 0, AYaw = 0, EndX = 0, EndY = 0; FString Name; };
	TArray<FPackGang> PackGangs;
	void LoadPackGangs();
	/** The pack's turnover captures (Tools/Art/pack_turnovers.py: pack_turnovers.json), streamed in once at load:
	 *  strips - two-man strip tackles: the carrier (C) and the man who takes the ball (T), the contact (Start), the ball
	 *           coming out (Out: the punch at the carrying arm's ball socket), which arm carries it, the punch's way out
	 *           (Pop, m/s, in the carrier's frame: x left, y back, z up), the pair at the contact as FPackPair has it
	 *           (Ox / Oy / Yaw), a sack strip, how it ends ("loses", "tackled", "keeps", "breaks", "misses");
	 *  ints   - two-man interceptions: the defender (D) and the receiver (R), the catch (T), the receiver in the
	 *           defender's frame then (Rx / Ry, m; RYaw, deg), the ball's height (Bz, m) and both men's speeds into it;
	 *  dives  - a man going down onto a ball on the turf: when his hands reach it (Touch) and how far ahead (Reach, m). */
	struct FPackStrip { TObjectPtr<UAnimSequence> T, C; float Start = 0, Out = 0, PopX = 0, PopY = 0, PopZ = 0, Ox = 0, Oy = 0, Yaw = 0, CMinZ = 0; bool bRightHand = true, bSack = false; FString Name, Outcome; };
	struct FPackInt { TObjectPtr<UAnimSequence> D, R; float T = 0, Rx = 0, Ry = 0, RYaw = 0, Bz = 0, DSpeed = 0, RSpeed = 0; FString Name; };
	struct FPackDive { TObjectPtr<UAnimSequence> Anim; float Touch = 0, Reach = 0, V0 = 0; FString Name; };
	TArray<FPackStrip> PackStrips;
	TArray<FPackInt> PackInts;
	TArray<FPackDive> PackDives;
	/** A receiver who drops it (one man, the pack's Drop_ / CatchDrop_ captures) and the turnover celebrations: the man
	 *  who came up with a fumble holding it up, his teammates pointing the way, an interception's. */
	TArray<FPackClip> PackCatchDrops;
	TArray<TObjectPtr<UAnimationAsset>> PackCelebFumbleBC, PackCelebFumblePoint, PackCelebInt, PackCelebIntMates; // (BC: the man with the ball; the others' without)
	FStreamableManager TurnoverStreamer;
	TSharedPtr<FStreamableHandle> TurnoverHandle;
	bool bTurnoversRequested = false;
	void LoadPackTurnovers();
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
	int32 PackEssentialN = 0; // the queue's first share - the first rounds of every family - loaded unless memory is critical
	bool bPackDrawn = false;  // a draw has landed (a re-draw with no room beside it keeps it instead of emptying the families)
	static double PackFloorMB(); // free memory the pack loader leaves the machine (-TagMemFloorMB=, 1500 MB)
	TSet<FSoftObjectPath> PackPlaced;
	double PackBudgetMB = 0.0, PackT0 = 0.0;
	void LoadNextPackChunk();
	void FillPackLoaded();
	struct FPendingFamily { FFamilySink Sink; int32 Max = 0; bool bLoop = false; TArray<FSoftObjectPath> Paths; };
	struct FPendingTimed { TArray<FPackClip>* Sink = nullptr; TArray<FPackClip> Clips; };
	struct FPendingPair { int32 Kind = 0; FPackPair P; FSoftObjectPath TP, CP; bool bBad = false; };
	struct FPendingGang { FPackGang G; FSoftObjectPath CP, TP, AP; bool bBad = false; };
	TArray<FPendingGang> PendingGangs;
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
	// (six variants: the walk has six sets, S1-S6, mm_walk_sets_hero.py; a gait with three uses S1-S3 twice over)
	TArray<class UPoseSearchDatabase*> MMMoveV[3][6], MMStartV[3][6], MMPivotV[3][6];
	// standing: the stops and the stand idles split three ways (PSD_TAG_Stand_Idles_S1..S3, Tools/Art/mm_idle_sets_hero.py)
	TArray<class UPoseSearchDatabase*> MMStopV[3][6];
	/** Locomotion by body class (Tools/Art/mm_style_sets_hero.py; AAthlete::BodyClass): [gait][style set][class] the move /
	 *  start / pivot sets with that set's loops split in two - the class's own captures with the general ones
	 *  (PSD_TAG_<Gait>_Loops_S<n>_<Class>) and the other classes' at a cost (..._<Class>_Off, BaseCostBias +0.1: they win
	 *  only where nothing of his own fits; a class has 8-12 captures, too few to stand alone in every direction). Empty
	 *  where the databases are not built (the style set as before). The tired captures (MG_NonBC_Fatigued_MC_*) are in
	 *  none of them: MMFatigued[gait] is searched besides only by a tired man (Player::fatigue, -TagFatigueGait=0.35)
	 *  after the first 2 s of a live play. Off by default (it put more men on one loop at once: Athlete.cpp, the set
	 *  choice in DriveMotionMatching); -TagStyleSets turns it on, -TagNoStyleSets keeps it off whatever else is given. */
	TArray<class UPoseSearchDatabase*> MMMoveC[3][6][3], MMStartC[3][6][3], MMPivotC[3][6][3];
	class UPoseSearchDatabase* MMFatigued[3] = {nullptr, nullptr, nullptr};
	bool bMMStyle = false; // any class set is in
	/** The class a capture was filmed in, from its name (0 big, 1 medium, 2 agile, 3 tired, -1 general): the style-share
	 *  stats ("TAG style share") and the editor script share these rules. */
	static int32 CaptureClass(const FString& Name);
	/** -TagStyleSets given (and not -TagNoStyleSets): the class sets and the tired-gait set are loaded and searched. */
	static bool StyleSetsOn();
	/** Signature styles on (not -TagNoSignature): AAthlete::SigId. */
	static bool SignatureOn();
	/** The body-class sets for the Superstars and X-Factors alone (everyone else on the style sets); on with the
	 *  signature, off with -TagNoSigClassSets. */
	static bool SigClassSets();
	/** The class sets are loaded: -TagStyleSets, or -TagSigClassSets. */
	static bool ClassSetsLoad();
	/** The style set (0-5) of a gait whose loops hold the largest share of captures of body class Cls (ties: by Sig);
	 *  Fallback when nothing is loaded. Counted once per gait from the loaded databases. */
	int32 SignatureSet(int32 Gait, int32 Cls, uint32 Sig, int32 Fallback) const;
	mutable int32 MMSetClassN[3][6][5] = {};  // [gait][set][capture class + 1: general, big, medium, agile, tired]
	mutable bool bMMSetCensus[3] = {false, false, false};
	// the man with the ball: the pack's ball-carrier loops (MG_BC_*, Tools/Art/mm_build_bc_dbs.py), the ball tucked as captured
	TArray<class UPoseSearchDatabase*> MMBallMove[3], MMBallPivot[3], MMBallStart[3];
	// crouched line footwork (pass sets, pulls, drive blocks), same state split: [0] loops, [1] starts, [2] pivots, [3] stops + idles
	TArray<class UPoseSearchDatabase*> MMCrouch[4];   // that gait's stops + standing idles + turns in place
	TArray<class UPoseSearchDatabase*> MMAll;
	/** Asks for the databases behind the game (they drag in ~730 animation packages: 8.8 s of the launch when loaded in
	 *  place). bMotionMatch is set at once when the databases are on disk, so the bodies get the matching instance;
	 *  MotionMatchReady stays false, and the blend spaces drive, until FinishMotionMatching has filled the sets.
	 *  -TagMMSync loads them in place as before. */
	void LoadMotionMatching();
	/** The databases are in (bLoadSync: load them here instead): fills the sets, or turns motion matching off. */
	void FinishMotionMatching(bool bLoadSync);
	/** Blocks until the request has landed. The director calls it once the world is built, before the first frame:
	 *  the load has run beside the stadium build by then, and left to finish over live frames the engine's pose search
	 *  re-checks every pending database's dependents each frame (380 ms frames for 10-25 s on the menu). */
	void WaitMotionMatching();
	FStreamableManager MMStreamer;
	TSharedPtr<FStreamableHandle> MMHandle; // kept: holds the databases with the root flag each one gets
	bool bMMLoading = false;
	double MMLoadT0 = 0.0;
	/** True once every database is loaded and its search index is built (game thread; checked at most once a frame). */
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
