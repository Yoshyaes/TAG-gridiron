#include "Render/TagMotionMatchInstance.h"

#include "PoseSearch/PoseSearchDatabase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/AnimTraceDefines.h"
#include "Misc/CommandLine.h"

/** -TagMMDeadBlend asked for and the engine node usable here: never in a build with anim tracing (see the constructor). */
static bool TagMMDeadBlendUsable()
{
	static const bool bAsked = FParse::Param(FCommandLine::Get(), TEXT("TagMMDeadBlend"));
#if ANIM_TRACE_ENABLED
	static bool bWarned = false;
	if (bAsked && !bWarned)
	{
		bWarned = true;
		UE_LOG(LogTemp, Warning, TEXT("TAG: -TagMMDeadBlend ignored: the engine's dead blending node asserts outside an Anim Blueprint in a build with anim tracing"));
	}
	return false;
#else
	return bAsked;
#endif
}

void FTagRootTapNode::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	// pelvis continuity: on a new pick the blend stack's first evaluations put the pelvis (and the whole body under it,
	// feet too) up to a step of travel behind where it was, then back within 2-3 frames - a runner frozen for a frame
	// and catching up (0.3 % of running frames, to10; -TagStallTrace: srcPel -10 -> -25 -> -16 -> -7 cm on the frames a
	// Bruiser sprint was picked at 0.000, the root steady). A matched locomotion pose moves its pelvis under a centimetre a
	// frame in component space, so a jump of more than 10 cm in one evaluation is that: it is cancelled through the root
	// (the body stays where it was shown) and the correction eased off over ~0.1 s. (-TagMMNoPelvisFix: as before.)
	static const bool bNoPelvisFix = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoPelvisFix"));
	if (!bNoPelvisFix && Output.Pose.GetNumBones() > 1)
	{
		const FBoneContainer& BC = Output.Pose.GetBoneContainer();
		const int32 Mesh = BC.GetPoseBoneIndexForBoneName(TEXT("pelvis"));
		const FCompactPoseBoneIndex Pel = Mesh != INDEX_NONE ? BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh)) : FCompactPoseBoneIndex(INDEX_NONE);
		if (Pel.IsValid())
		{
			FTransform T = Output.Pose[Pel];
			for (FCompactPoseBoneIndex P = BC.GetParentBoneIndex(Pel); P.IsValid(); P = BC.GetParentBoneIndex(P)) T = T * Output.Pose[P];
			const FVector Now = T.GetLocation();
			// the last correction eases off (0.05 s half-life: the blend stack's own 2-3 frame recovery plays through it),
			// then this evaluation's jump, if any, is taken out whole
			PelvisFix *= FMath::Exp(-0.693f * FMath::Max(LastDt, 0.f) / 0.05f);
			if (bPrevPelvis)
			{
				FVector D = Now - PrevPelvis;
				D.Z = 0.f;
				if (D.SizeSquared() > 10.f * 10.f) PelvisFix -= D;
			}
			PrevPelvis = Now;
			bPrevPelvis = true;
			PelvisFix = PelvisFix.GetClampedToMaxSize(40.f);
			if (!PelvisFix.IsNearlyZero(0.05f))
			{
				const FCompactPoseBoneIndex Root(0);
				Output.Pose[Root].AddToTranslation(PelvisFix);
			}
		}
	}
	bHave = false;
	if (const UE::Anim::IAnimRootMotionProvider* P = UE::Anim::IAnimRootMotionProvider::Get())
	{
		FTransform D = FTransform::Identity;
		if (P->ExtractRootMotion(Output.CustomAttributes, D))
		{
			Delta = D;
			bHave = true;
		}
	}
}

void FTagLocoSwitchNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Clip.Initialize(Context);
	Match.Initialize(Context);
}

void FTagLocoSwitchNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Clip.CacheBones(Context);
	Match.CacheBones(Context);
}

void FTagLocoSwitchNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	if (bMatch && !bWasMatch)
	{
		FAnimationInitializeContext Init(Context.AnimInstanceProxy, Context.SharedContext);
		Match.Initialize(Init);
		FAnimationCacheBonesContext Bones(Context.AnimInstanceProxy);
		Match.CacheBones(Bones);
	}
	bWasMatch = bMatch;
	(bMatch ? Match : Clip).Update(Context);
}

void FTagLocoSwitchNode::Evaluate_AnyThread(FPoseContext& Output)
{
	(bMatch ? Match : Clip).Evaluate(Output);
}

FTagMotionMatchProxy::FTagMotionMatchProxy(UAnimInstance* InInstance, FAnimNode_PoseSearchHistoryCollector* InHistory,
	FAnimNode_OffsetRootBone* InOffset, FAnimNode_MotionMatching* InMatch, FTagOffsetRootNode* InTOffset, FTagMotionMatchNode* InTMatch,
	FAnimNode_DeadBlending* InDead)
	: FAnimSingleNodeInstanceProxy(InInstance), History(InHistory), Offset(InOffset), Match(InMatch), TOffset(InTOffset), TMatch(InTMatch), Dead(InDead)
{
	Switch.Clip.SetLinkNode(&SingleNode);
	static const bool bNoOffset = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoOffset")); // A/B: no root offset
	Switch.Match.SetLinkNode(bNoOffset ? static_cast<FAnimNode_Base*>(&Tap) : OffsetBase());
	if (TOffset) TOffset->Source.SetLinkNode(&Tap);
	else Offset->Source.SetLinkNode(&Tap);
	// (-TagMMDeadBlend: the dead blending node under the tap, so the tap reads the pose and the root motion as blended -
	// what the body shows - and its pelvis patch sees the transition the node has already smoothed)
	if (Dead)
	{
		Tap.Source.SetLinkNode(Dead);
		Dead->Source.SetLinkNode(MatchBase());
	}
	else Tap.Source.SetLinkNode(MatchBase());
	History->Source.SetLinkNode(&Switch);
}

void FTagMotionMatchProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(History);
	OutNodes.Add(OffsetBase());
	if (Dead) OutNodes.Add(Dead);
	OutNodes.Add(MatchBase());
}

void FTagMotionMatchProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	UTagMotionMatchInstance* I = CastChecked<UTagMotionMatchInstance>(InAnimInstance);
	// motion matched clips carry root motion; the simulation moves the player, so lock their root in place
	I->RootMotionMode = I->bMatch ? ERootMotionMode::IgnoreRootMotion : ERootMotionMode::RootMotionFromMontagesOnly;
	FAnimSingleNodeInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	Switch.bMatch = I->bMatch;
	History->TransformTrajectory = I->Trajectory;
	History->TrajectorySpeedMultiplier = I->TrajectorySpeedMultiplier;
	// the node plays at PlayRate / TrajectorySpeedMultiplier. A range lets the search try each candidate at the
	// rate that fits the query's speed (the pack's loops run at their captured speeds: a 1.9 m/s kick slide against
	// a 1.5 m/s crouch strafe is then a matter of which clip, not of the strafe sped up), capped at RateSpread
	if (I->PlayRateSetting)
	{
		const float Fixed = I->MatchPlayRate * I->TrajectorySpeedMultiplier;
		*I->PlayRateSetting = FFloatInterval(Fixed / I->RateSpread, Fixed * I->RateSpread);
	}
	// speed matching (see SimSpeed): the clip's root speed around its play position against the sim's
	if (I->RateMultiplierSetting)
	{
		float Mul = 1.f;
		const UAnimSequence* Seq = Cast<UAnimSequence>(I->MatchedAsset.Get());
		if (I->bMatch && Seq && I->SpeedMatchMax > 1.f)
		{
			const double Len = Seq->GetPlayLength(), W = 0.1;
			double S = I->MatchedTime - W * 0.5, E = I->MatchedTime + W * 0.5;
			FVector Move = FVector::ZeroVector;
			const FAnimExtractContext Ctx(0.0, true);
			if (!I->bMatchedLoop) { S = FMath::Clamp(S, 0.0, Len); E = FMath::Clamp(E, 0.0, Len); }
			if (S < 0.0) { Move += Seq->ExtractRootMotionFromRange(Len + S, Len, Ctx).GetTranslation(); S = 0.0; }
			if (E > Len) { Move += Seq->ExtractRootMotionFromRange(0.0, E - Len, Ctx).GetTranslation(); E = Len; }
			if (E > S) Move += Seq->ExtractRootMotionFromRange(S, E, Ctx).GetTranslation();
			const float ClipSpeed = Move.Size2D() / W;
			I->MatchedClipSpeed = ClipSpeed;
			I->MatchedClipDir = Move.GetSafeNormal2D();
			// the node plays at WantedPlayRate (= MatchPlayRate for a fixed interval) x multiplier
			if (ClipSpeed > 60.f && I->SimSpeed > 60.f)
				Mul = FMath::Clamp(I->SimSpeed / ClipSpeed, I->SpeedMatchMin, I->bCrouchGait ? I->CrouchSpeedMatchMax : I->SpeedMatchMax) / FMath::Max(I->MatchedWantedRate, 0.1f) *
					FMath::Clamp(I->Cadence, 0.85f, 1.15f);
		}
		else
		{
			I->MatchedClipSpeed = 0.f;
			I->MatchedClipDir = FVector::ZeroVector;
		}
		I->SpeedRate += (Mul - I->SpeedRate) * FMath::Min(1.f, DeltaSeconds / 0.1f);
		*I->RateMultiplierSetting = I->SpeedRate;
	}
	bool bDbChanged = false;
	bool bForcedNow = false; // this update carries a ForceInterrupt: it must search (the node drops the mode after one update)
	const bool bForceNow = SeenForceSerial != I->ForceSerial; // the clip no longer fits the move (AAthlete::DriveMotionMatching)
	SeenForceSerial = I->ForceSerial;
	if (SeenDbSerial != I->DbSerial || bForceNow)
	{
		// a clip from a database no longer searched (a start once the player has stopped, or once he has been moving
		// for 0.6 s and the loops take over) must not keep playing: InterruptOnDatabaseChange left a crouch start
		// running for its full 2 s against the lineman's kick slide (-TagMMTrace), so such a change forces one fresh
		// search; the next update puts the mode back so the continuing pose counts again. A change of gait band or into
		// the pivot set does not: the playing loop carries on until the search finds better (forcing those had the
		// pick flipping sprint <-> run every few frames while the speed hovered at a band's edge)
		bDbChanged = SeenDbSerial != I->DbSerial;
		SeenDbSerial = I->DbSerial;
		if (I->bForceOnDbChange || bForceNow)
		{
			SetMatchDatabases(I->Databases, EPoseSearchInterruptMode::ForceInterrupt);
			bForcedNow = true;
			ForceLeft = FMath::Max(0.15f, I->ForceHoldSecs); // longer than the 66 ms search throttle, so the forced search actually runs
			I->ForceHoldSecs = 0.f;
		}
		else
			SetMatchDatabases(I->Databases, EPoseSearchInterruptMode::DoNotInterrupt);
	}
	else if (ForceLeft > 0.f)
	{
		ForceLeft -= GetDeltaSeconds();
		if (ForceLeft <= 0.f) SetMatchDatabases(I->Databases, EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	}
	// the man a human drives: his own search rate, dwell and blend (UTagMotionMatchInstance::bUserMan)
	static const bool bUserSame = FParse::Param(FCommandLine::Get(), TEXT("TagUserMMSame"));
	const bool bUser = I->bUserMan && !bUserSame;
	if (I->BlendTimeSetting) *I->BlendTimeSetting = bUser ? I->UserBlendTime : I->BaseBlendTime;
	// the minimum dwell: a new pick holds while the move it was picked for holds (searches wait MinDwell); a change of
	// the move lets the next search through at once and becomes the move to hold to
	if (I->ThrottleSetting)
	{
		const float Throttle = bUser ? I->UserThrottle : I->BaseThrottle;
		const float Dwell = bUser ? I->UserMinDwell : I->MinDwell;
		const float TurnDeg = bUser ? I->UserDwellTurnDeg : I->DwellTurnDeg;
		const UAnimationAsset* Cur = I->MatchedAsset.Get();
		if (Cur != DwellAsset.Get())
		{
			DwellAsset = Cur;
			DwellVel = I->SimVel;
		}
		const float S0 = DwellVel.Size2D(), S1 = I->SimVel.Size2D();
		const bool bTurned = S0 > 150.f && S1 > 150.f &&
			FVector::DotProduct(DwellVel.GetSafeNormal2D(), I->SimVel.GetSafeNormal2D()) < FMath::Cos(FMath::DegreesToRadians(TurnDeg));
		const bool bMoveChanged = bTurned || FMath::Abs(S1 - S0) > I->DwellBreak;
		if (bMoveChanged) DwellVel = I->SimVel;
		// (a soft database change - a gait band, the pivot set - holds too: freeing those let a third of all switches
		// through, -TagMMWhy; a real cut still frees the search by the turn)
		const bool bFree = bMoveChanged || (bDbChanged && I->bForceOnDbChange) || ForceLeft > 0.f || !I->bMatch;
		// (a forced pick searches now: the node resets the interrupt mode after every update, and inside the 66 ms throttle
		// that update did not search - the force was lost and the continuing pose kept a carrier on his empty-handed sprint, bc6)
		*I->ThrottleSetting = bForcedNow ? 0.f : bFree ? Throttle : FMath::Max(Throttle, Dwell);
		I->DiagFlags = (bMoveChanged ? 1 : 0) | (bDbChanged ? 2 : 0) | (ForceLeft > 0.f ? 4 : 0) | (bFree ? 0 : 8);
	}
}

void FTagMotionMatchProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimSingleNodeInstanceProxy::PostUpdate(InAnimInstance);
	// game thread, worker finished: safe to read the blend stack and the search state
	UTagMotionMatchInstance* I = CastChecked<UTagMotionMatchInstance>(InAnimInstance);
	I->MatchedAsset = Switch.bMatch ? (TMatch ? TMatch->GetAnimAsset() : Match->GetAnimAsset()) : nullptr;
	const FPoseSearchBlueprintResult& R = (TMatch ? TMatch->GetMotionMatchingState() : Match->GetMotionMatchingState()).SearchResult;
	static const bool bWhy = FParse::Param(FCommandLine::Get(), TEXT("TagMMWhy"));
	if (bWhy && Switch.bMatch)
	{
		// why the pick changed: the move, the databases, a forced window, the end of a clip that cannot run on, or a
		// search the dwell should have held off (the continuing pose could not advance)
		static int32 N[6] = {0, 0, 0, 0, 0, 0}, NS[6] = {0, 0, 0, 0, 0, 0}, Short = 0, Total = 0;
		const UAnimationAsset* Cur = I->MatchedAsset.Get();
		const UAnimationAsset* Prev = I->DiagPrev.Get();
		if (Cur && Prev && Cur != Prev)
		{
			const UAnimSequence* PS = Cast<UAnimSequence>(Prev);
			const bool bEnd = PS && !PS->bLoop && I->DiagPrevTime > PS->GetPlayLength() - 0.5f;
			const int32 K = (I->DiagFlags & 1) ? 0 : (I->DiagFlags & 2) ? 1 : (I->DiagFlags & 4) ? 2 : bEnd ? 3 : (I->DiagFlags & 8) ? 4 : 5;
			++N[K];
			++Total;
			const bool bShort = I->DiagHeld < I->MinDwell;
			Short += bShort ? 1 : 0;
			NS[K] += bShort ? 1 : 0;
			if (Total % 400 == 0)
				UE_LOG(LogTemp, Display, TEXT("TAG MM why: %d switches (%d held < dwell): move %d/%d, db %d/%d, forced %d/%d, clip end %d/%d, held-yet-searched %d/%d, other %d (all/short)"),
					Total, Short, N[0], NS[0], N[1], NS[1], N[2], NS[2], N[3], NS[3], N[4], NS[4], N[5]);
			I->DiagHeld = 0.f;
		}
		else I->DiagHeld += GetDeltaSeconds();
		I->DiagPrev = Cur;
		I->DiagPrevTime = R.SelectedTime;
	}
	static const bool bNoOffset = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoOffset"));
	I->bRootRotValid = Switch.bMatch && Switch.bWasMatch && !bNoOffset && OffsetBase() != nullptr;
	if (I->bRootRotValid) I->RootRotW = TOffset ? TOffset->GetOffsetRootRotation() : Offset->GetOffsetRootRotation();
	I->bRootDeltaValid = Switch.bMatch && Switch.bWasMatch && Tap.bHave && GetDeltaSeconds() > 1e-4f;
	if (I->bRootDeltaValid)
	{
		I->RootDelta = Tap.Delta.GetTranslation();
		I->RootDeltaDt = GetDeltaSeconds();
	}
	I->MatchedTime = R.SelectedTime;
	I->MatchedCost = R.SearchCost;
	I->MatchedWantedRate = R.WantedPlayRate;
	I->bMatchedContinuing = R.bIsContinuingPoseSearch;
	I->bMatchedLoop = R.bLoop;
}

void FTagMotionMatchProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	// the root graph (history -> switch -> clip | motion matching), not just the single node
	FAnimInstanceProxy::UpdateAnimationNode(InContext);
}

UTagMotionMatchInstance::UTagMotionMatchInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// pose history: 10 poses at 25 Hz (pose velocities and short history features), all bones
	HistoryNode.PoseCount = 10;
	HistoryNode.SamplingInterval = 0.04f;
	// motion matching (the node's settings are private outside an Anim Blueprint; set them through reflection).
	// Defaults kept: blend stack transitions (4 active blends, no inertialization node in this graph), 0.3 s
	// reselect history, reset on becoming relevant.
	// (the TAG node's settings are its own members; the engine node's are reached through reflection, -TagOldMMHost)
	const bool bOld = UseOldMMHost();
	SetMatchSetting(TEXT("BlendTime"), 0.3f);
	TagMatchNode.BlendTime = 0.3f;
	BaseBlendTime = 0.3f;
	if (!bOld) BlendTimeSetting = &TagMatchNode.BlendTime;
	else if (const FProperty* P = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("BlendTime")))
		if (P->GetElementSize() == sizeof(float)) BlendTimeSetting = P->ContainerPtrToValuePtr<float>(&MatchNode);
	// the man a human drives (bUserMan): his own search rate, dwell, turn and blend
	{
		static float UThr = FParse::Value(FCommandLine::Get(), TEXT("TagUserMMThrottle="), UThr) ? UThr : 0.f;
		static float UDwell = FParse::Value(FCommandLine::Get(), TEXT("TagUserMMDwell="), UDwell) ? UDwell : 0.12f;
		static float UTurn = FParse::Value(FCommandLine::Get(), TEXT("TagUserMMTurn="), UTurn) ? UTurn : 15.f;
		static float UBlend = FParse::Value(FCommandLine::Get(), TEXT("TagUserMMBlend="), UBlend) ? UBlend : 0.2f;
		UserThrottle = UThr;
		UserMinDwell = UDwell;
		UserDwellTurnDeg = UTurn;
		UserBlendTime = UBlend;
	}
	// -TagMMDeadBlend: transitions by the engine's dead blending instead of the blend stack (DeadNode): the node asks for an
	// inertial blend of BlendTime and keeps one clip playing. NOT AVAILABLE in this graph: FAnimNode_DeadBlending's
	// Evaluate traces under its node index (GetNodeIndex), which only an Anim Blueprint gives a node - in a C++ graph it
	// asserts (NodeData, AnimNodeBase.h) on the first matched update in any build with anim tracing (Development: the
	// 24 Nov bench crashed both runs). The flag is ignored until the node can be hosted; the wiring is kept for that.
	static const bool bMMDead = TagMMDeadBlendUsable();
	if (bMMDead)
	{
		SetMatchSetting(TEXT("bUseInertialBlend"), true);
		MatchNode.SetMaxActiveBlends(0);
		TagMatchNode.bUseInertialBlend = true;
		TagMatchNode.SetMaxActiveBlends(0);
	}
	// 22 players: search at most every 66 ms (the current clip keeps playing in between; a cut still reads within a
	// few frames). Searching every frame for every player cost ~25 fps once the linemen joined.
	static float Throttle = FParse::Value(FCommandLine::Get(), TEXT("TagMMThrottle="), Throttle) ? Throttle : 0.066f;
	SetMatchSetting(TEXT("SearchThrottleTime"), Throttle);
	TagMatchNode.SearchThrottleTime = Throttle;
	BaseThrottle = Throttle;
	// -TagMMDwell=<s> (0 = off): the minimum dwell of a pick
	{
		static float Dwell = FParse::Value(FCommandLine::Get(), TEXT("TagMMDwell="), Dwell) ? Dwell : 0.3f;
		MinDwell = Dwell;
		static float Brk = FParse::Value(FCommandLine::Get(), TEXT("TagMMDwellBreak="), Brk) ? Brk : 250.f;
		DwellBreak = Brk;
	}
	if (!bOld) ThrottleSetting = &TagMatchNode.SearchThrottleTime;
	else if (const FProperty* P = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("SearchThrottleTime")))
		if (P->GetElementSize() == sizeof(float)) ThrottleSetting = P->ContainerPtrToValuePtr<float>(&MatchNode);
	// play rate: fixed, set every update from MatchPlayRate (see PreUpdate)
	SetMatchSetting(TEXT("PlayRate"), FFloatInterval(1.f, 1.f));
	TagMatchNode.PlayRate = FFloatInterval(1.f, 1.f);
	if (!bOld)
	{
		PlayRateSetting = &TagMatchNode.PlayRate;
		RateMultiplierSetting = &TagMatchNode.PlayRateMultiplier;
	}
	else
	{
		if (const FProperty* P = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("PlayRate")))
			if (P->GetElementSize() == sizeof(FFloatInterval)) PlayRateSetting = P->ContainerPtrToValuePtr<FFloatInterval>(&MatchNode);
		if (const FProperty* P = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("PlayRateMultiplier")))
			if (P->GetElementSize() == sizeof(float)) RateMultiplierSetting = P->ContainerPtrToValuePtr<float>(&MatchNode);
	}
	// -TagMMSpeedMatch=min,max (default 0.75,1.25; max 1 turns speed matching off)
	{
		FString Range;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagMMSpeedMatch="), Range))
		{
			FString A, B;
			if (Range.Split(TEXT(","), &A, &B)) { SpeedMatchMin = FCString::Atof(*A); SpeedMatchMax = FCString::Atof(*B); }
		}
		FParse::Value(FCommandLine::Get(), TEXT("TagMMSpeedMatchCrouch="), CrouchSpeedMatchMax);
	}
	// root offset: the clip turns and strides the body, the sim stays the truth (at most 25 cm / 60 deg apart)
	// (translation: only without the follower, -TagNoFollow. With it the actor itself moves by the clip's root travel,
	// AAthlete::FollowVel, and an offset on top would count that travel twice.)
	static const bool bFollowMM = FParse::Param(FCommandLine::Get(), TEXT("TagFollowMM")) && !FParse::Param(FCommandLine::Get(), TEXT("TagNoFollow"));
	TagOffsetNode.TranslationMode = bFollowMM ? EOffsetRootBoneMode::Release : EOffsetRootBoneMode::Interpolate;
	TagOffsetNode.RotationMode = EOffsetRootBoneMode::Interpolate;
	TagOffsetNode.TranslationHalflife = 0.15f;
	TagOffsetNode.RotationHalfLife = 0.25f;
	TagOffsetNode.MaxTranslationError = 25.f;
	TagOffsetNode.MaxRotationError = 60.f;
	TagOffsetNode.bResetOnTeleport = true;
#if WITH_EDITORONLY_DATA
	// (the engine node's folded settings: they exist only in editor builds; a packaged game reads them from an anim
	// blueprint's node data, which a native node does not have - the reason for TagOffsetNode, TagMMNodes.h)
	OffsetNode.TranslationMode = bFollowMM ? EOffsetRootBoneMode::Release : EOffsetRootBoneMode::Interpolate;
	OffsetNode.RotationMode = EOffsetRootBoneMode::Interpolate;
	OffsetNode.TranslationHalflife = 0.15f;
	OffsetNode.RotationHalfLife = 0.25f;
	OffsetNode.MaxTranslationError = 25.f;
	OffsetNode.MaxRotationError = 60.f;
	OffsetNode.bResetOnTeleport = true;
#endif
}

template <class T>
void UTagMotionMatchInstance::SetMatchSetting(const TCHAR* Name, const T& Value)
{
	const FProperty* P = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(Name);
	if (P && P->GetElementSize() == sizeof(T)) *P->ContainerPtrToValuePtr<T>(&MatchNode) = Value;
	else UE_LOG(LogTemp, Warning, TEXT("TAG: motion matching setting %s not found"), Name);
}

void UTagMotionMatchInstance::SetDatabases(TConstArrayView<UPoseSearchDatabase*> InDatabases)
{
	bool bSame = Databases.Num() == InDatabases.Num();
	for (int32 i = 0; bSame && i < InDatabases.Num(); ++i) bSame = Databases[i] == InDatabases[i];
	if (bSame) return;
	Databases = TArray<UPoseSearchDatabase*>(InDatabases);
	++DbSerial;
}

bool UTagMotionMatchInstance::UseOldMMHost()
{
	static const bool bOld = []
	{
		const bool bAsked = FParse::Param(FCommandLine::Get(), TEXT("TagOldMMHost"));
#if WITH_EDITORONLY_DATA
		if (bAsked) UE_LOG(LogTemp, Display, TEXT("TAG: motion matching on the engine's nodes (-TagOldMMHost)"));
		return bAsked;
#else
		if (bAsked) UE_LOG(LogTemp, Warning, TEXT("TAG: -TagOldMMHost ignored: the engine's motion matching nodes need an Anim Blueprint's node data in a packaged game"));
		return false;
#endif
	}();
	return bOld;
}

FAnimInstanceProxy* UTagMotionMatchInstance::CreateAnimInstanceProxy()
{
	static const bool bMMDead = TagMMDeadBlendUsable();
	const bool bOld = UseOldMMHost();
	return new FTagMotionMatchProxy(this, &HistoryNode, bOld ? &OffsetNode : nullptr, bOld ? &MatchNode : nullptr,
		bOld ? nullptr : &TagOffsetNode, bOld ? nullptr : &TagMatchNode, bMMDead ? &DeadNode : nullptr);
}
