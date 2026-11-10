#include "Render/TagMotionMatchInstance.h"

#include "PoseSearch/PoseSearchDatabase.h"
#include "Animation/AnimSequence.h"
#include "Misc/CommandLine.h"

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
	FAnimNode_OffsetRootBone* InOffset, FAnimNode_MotionMatching* InMatch)
	: FAnimSingleNodeInstanceProxy(InInstance), History(InHistory), Offset(InOffset), Match(InMatch)
{
	Switch.Clip.SetLinkNode(&SingleNode);
	static const bool bNoOffset = FParse::Param(FCommandLine::Get(), TEXT("TagMMNoOffset")); // A/B: no root offset
	Switch.Match.SetLinkNode(bNoOffset ? static_cast<FAnimNode_Base*>(Match) : static_cast<FAnimNode_Base*>(Offset));
	Offset->Source.SetLinkNode(Match);
	History->Source.SetLinkNode(&Switch);
}

void FTagMotionMatchProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Add(History);
	OutNodes.Add(Offset);
	OutNodes.Add(Match);
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
			// the node plays at WantedPlayRate (= MatchPlayRate for a fixed interval) x multiplier
			if (ClipSpeed > 60.f && I->SimSpeed > 60.f)
				Mul = FMath::Clamp(I->SimSpeed / ClipSpeed, I->SpeedMatchMin, I->bCrouchGait ? I->CrouchSpeedMatchMax : I->SpeedMatchMax) / FMath::Max(I->MatchedWantedRate, 0.1f);
		}
		else I->MatchedClipSpeed = 0.f;
		I->SpeedRate += (Mul - I->SpeedRate) * FMath::Min(1.f, DeltaSeconds / 0.1f);
		*I->RateMultiplierSetting = I->SpeedRate;
	}
	if (SeenDbSerial != I->DbSerial)
	{
		// a clip from a database no longer searched (a start once the player has stopped, or once he has been moving
		// for 0.6 s and the loops take over) must not keep playing: InterruptOnDatabaseChange left a crouch start
		// running for its full 2 s against the lineman's kick slide (-TagMMTrace), so the change forces one fresh
		// search; the next update puts the mode back so the continuing pose counts again
		SeenDbSerial = I->DbSerial;
		Match->SetDatabasesToSearch(I->Databases, EPoseSearchInterruptMode::ForceInterrupt);
		ForceLeft = 0.15f; // longer than the 66 ms search throttle, so the forced search actually runs
	}
	else if (ForceLeft > 0.f)
	{
		ForceLeft -= GetDeltaSeconds();
		if (ForceLeft <= 0.f) Match->SetDatabasesToSearch(I->Databases, EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	}
}

void FTagMotionMatchProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimSingleNodeInstanceProxy::PostUpdate(InAnimInstance);
	// game thread, worker finished: safe to read the blend stack and the search state
	UTagMotionMatchInstance* I = CastChecked<UTagMotionMatchInstance>(InAnimInstance);
	I->MatchedAsset = Switch.bMatch ? Match->GetAnimAsset() : nullptr;
	const FPoseSearchBlueprintResult& R = Match->GetMotionMatchingState().SearchResult;
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
	SetMatchSetting(TEXT("BlendTime"), 0.3f);
	// 22 players: search at most every 66 ms (the current clip keeps playing in between; a cut still reads within a
	// few frames). Searching every frame for every player cost ~25 fps once the linemen joined.
	static float Throttle = FParse::Value(FCommandLine::Get(), TEXT("TagMMThrottle="), Throttle) ? Throttle : 0.066f;
	SetMatchSetting(TEXT("SearchThrottleTime"), Throttle);
	// play rate: fixed, set every update from MatchPlayRate (see PreUpdate)
	SetMatchSetting(TEXT("PlayRate"), FFloatInterval(1.f, 1.f));
	if (const FProperty* P = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("PlayRate")))
		if (P->GetElementSize() == sizeof(FFloatInterval)) PlayRateSetting = P->ContainerPtrToValuePtr<FFloatInterval>(&MatchNode);
	if (const FProperty* P = FAnimNode_MotionMatching::StaticStruct()->FindPropertyByName(TEXT("PlayRateMultiplier")))
		if (P->GetElementSize() == sizeof(float)) RateMultiplierSetting = P->ContainerPtrToValuePtr<float>(&MatchNode);
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
	OffsetNode.TranslationMode = EOffsetRootBoneMode::Interpolate;
	OffsetNode.RotationMode = EOffsetRootBoneMode::Interpolate;
	OffsetNode.TranslationHalflife = 0.15f;
	OffsetNode.RotationHalfLife = 0.25f;
	OffsetNode.MaxTranslationError = 25.f;
	OffsetNode.MaxRotationError = 60.f;
	OffsetNode.bResetOnTeleport = true;
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

FAnimInstanceProxy* UTagMotionMatchInstance::CreateAnimInstanceProxy()
{
	return new FTagMotionMatchProxy(this, &HistoryNode, &OffsetNode, &MatchNode);
}
