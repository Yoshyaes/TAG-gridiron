#include "Render/TagMotionMatchInstance.h"

#include "PoseSearch/PoseSearchDatabase.h"
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
	// with a fixed PlayRate the node plays at PlayRate / TrajectorySpeedMultiplier
	if (I->PlayRateSetting)
	{
		const float Fixed = I->MatchPlayRate * I->TrajectorySpeedMultiplier;
		*I->PlayRateSetting = FFloatInterval(Fixed, Fixed);
	}
	if (SeenDbSerial != I->DbSerial)
	{
		// a clip from a database no longer searched (a start once the player has stopped) must not keep playing
		SeenDbSerial = I->DbSerial;
		Match->SetDatabasesToSearch(I->Databases, EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	}
}

void FTagMotionMatchProxy::PostUpdate(UAnimInstance* InAnimInstance) const
{
	FAnimSingleNodeInstanceProxy::PostUpdate(InAnimInstance);
	// game thread, worker finished: safe to read the blend stack
	CastChecked<UTagMotionMatchInstance>(InAnimInstance)->MatchedAsset = Switch.bMatch ? Match->GetAnimAsset() : nullptr;
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
