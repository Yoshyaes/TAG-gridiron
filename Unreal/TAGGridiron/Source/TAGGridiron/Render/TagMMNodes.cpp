#include "Render/TagMMNodes.h"

#include "Animation/AnimInertializationSyncScope.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_Inertialization.h"
#include "Animation/AnimRootMotionProvider.h"
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchInteractionLibrary.h"
#include "PoseSearch/PoseSearchInteractionValidator.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchSchema.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TagMMNodes)

// ---------------------------------------------------------------------------------------------------------------------
// FTagMotionMatchNode: FAnimNode_MotionMatching (UE 5.8) with the folded settings at their defaults: no blend space
// parameters (BlendParameters zero, delta threshold 0, update mode InitialOnly), no sync group, no state-updated function.

void FTagMotionMatchNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	GetEvaluateGraphExposedInputs().Execute(Context);
	FAnimNode_BlendStack_Standalone::Initialize_AnyThread(Context);
	MotionMatchingState.Reset();
}

void FTagMotionMatchNode::SetDatabasesToSearch(TConstArrayView<UPoseSearchDatabase*> InDatabases, EPoseSearchInterruptMode InterruptMode)
{
	DatabasesToSearch.Reset();
	for (UPoseSearchDatabase* InDatabase : InDatabases) DatabasesToSearch.AddUnique(InDatabase);
	NextUpdateInterruptMode = InterruptMode;
}

// (the engine node's calls: experimental / engine-internal PoseSearch API, used here exactly as the node uses it)
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
PRAGMA_DISABLE_INTERNAL_WARNINGS
void FTagMotionMatchNode::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	using namespace UE::PoseSearch;
	check(Context.AnimInstanceProxy);
	GetEvaluateGraphExposedInputs().Execute(Context);

	// synchronizing with GetAccumulatedTime or resetting MotionMatchingState, and conditionally resetting the blend stack
	if (bResetOnBecomingRelevant && UpdateCounter.HasEverBeenUpdated() && !UpdateCounter.WasSynchronizedCounter(Context.AnimInstanceProxy->GetUpdateCounter()))
	{
		MotionMatchingState.Reset();
		FAnimNode_BlendStack_Standalone::Reset();
	}
	else if (MotionMatchingState.SearchResult.SelectedDatabase == nullptr || MotionMatchingState.SearchResult.SelectedDatabase->Schema == nullptr)
	{
	}
#if WITH_EDITOR
	else if (EAsyncBuildIndexResult::Success != FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(MotionMatchingState.SearchResult.SelectedDatabase.Get(), ERequestAsyncBuildFlag::ContinueRequest))
	{
		// the database is indexing: its previous index properties cached in MotionMatchingState are not safe to use
		MotionMatchingState.Reset();
	}
#endif
	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());

	const IPoseHistory* PoseHistory = nullptr;
	if (const FPoseHistoryProvider* PoseHistoryProvider = Context.GetMessage<FPoseHistoryProvider>())
	{
		PoseHistory = PoseHistoryProvider->GetPoseHistoryPtr();
	}
	if (!PoseHistory)
	{
		UE_LOG(LogTemp, Error, TEXT("TAG: FTagMotionMatchNode::UpdateAssetPlayer, missing IPoseHistory"));
	}
	else
	{
		const UObject* AnimContext = Context.AnimInstanceProxy->GetAnimInstanceObject();
		check(AnimContext);
		// (the interaction search: no availabilities here, so it only keeps the result's interaction state as the engine node does)
		UPoseSearchInteractionLibrary::MotionMatchInteraction(MotionMatchingState.SearchResult, Availabilities,
			AnimContext, FName(), PoseHistory, true /*bValidateResultAgainstAvailabilities*/, false /*bKeepInteractionAlive*/, BlendTime, Context.GetDeltaTime());

		if (!MotionMatchingState.SearchResult.bIsInteraction)
		{
			// the state's asset time follows the player node's (it may have ticked more or less than expected: variable dt, the
			// play rate adjustment)
			MotionMatchingState.SearchResult.SelectedAnim = GetAnimAsset();
			MotionMatchingState.SearchResult.SelectedTime = GetAccumulatedTime();
			MotionMatchingState.SearchResult.bIsMirrored = GetMirror();
			MotionMatchingState.SearchResult.BlendParameters = GetBlendParameters();

			const FFloatInterval PoseSearchPlayRate = PlayRate;
			const float DeltaTime = Context.GetDeltaTime();

			FSearchContext SearchContext(0.f, PoseJumpThresholdTime, EventToSearch.GetPlayRateOverriddenEvent(PoseSearchPlayRate));
			FChooserEvaluationContext ChooserEvaluationContext(const_cast<UObject*>(AnimContext));
			SearchContext.AddRole(GetCommonDefaultRole(DatabasesToSearch), &ChooserEvaluationContext, PoseHistory);

			// search unless the continuing pose can advance and the throttle has not run out
			bool bSearch = true;
			FSearchResult SearchResult;
			if (!bShouldSearch || MotionMatchingState.ElapsedPoseSearchTime < SearchThrottleTime)
			{
				SearchResult.InitFrom(MotionMatchingState.SearchResult);
				SearchContext.UpdateContinuingPoseSearchResult(SearchResult, SearchResult);
				const bool bCanAdvance = SearchContext.GetContinuingPoseSearchResult().PoseIdx != INDEX_NONE;
				bSearch = !bCanAdvance;
			}

			if (bSearch)
			{
				MotionMatchingState.ElapsedPoseSearchTime = 0.f;
				TArrayView<const UObject*> AssetsToSearch((const UObject**)FMemory_Alloca(DatabasesToSearch.Num() * sizeof(UObject*)), (int32)DatabasesToSearch.Num());
				for (int32 i = 0; i < DatabasesToSearch.Num(); ++i) AssetsToSearch[i] = DatabasesToSearch[i].Get();
				FSearchResults_Single SearchResults;
				FPoseSearchContinuingProperties ContinuingProperties;
				ContinuingProperties.InitFrom(MotionMatchingState.SearchResult, NextUpdateInterruptMode);
				UPoseSearchLibrary::MotionMatch(SearchContext, AssetsToSearch, ContinuingProperties, SearchResults);
				SearchResult = SearchResults.GetBestResult();
			}
			else
			{
				MotionMatchingState.ElapsedPoseSearchTime += DeltaTime;
				SearchResult.bIsContinuingPoseSearch = true;
			}

			const float WantedPlayRate = CalculateWantedPlayRate(SearchResult, SearchContext, PoseSearchPlayRate, PoseHistory->GetTrajectorySpeedMultiplier(), EventToSearch);
			MotionMatchingState.SearchResult.InitFrom(SearchResult, WantedPlayRate);

			if (const FPoseIndicesHistory* PoseIndicesHistory = PoseHistory->GetPoseIndicesHistory())
			{
				// (safe: this is the thread owning the pose history, and the place the engine node updates it)
				const_cast<FPoseIndicesHistory*>(PoseIndicesHistory)->Update(SearchResult, DeltaTime, PoseReselectHistory);
			}
		}

		const float DesiredPlayRate = MotionMatchingState.SearchResult.WantedPlayRate * PlayRateMultiplier;

		// a new pose: blend into it through the blend stack
		const bool bJumpToPose = MotionMatchingState.SearchResult.SelectedAnim && !MotionMatchingState.SearchResult.bIsContinuingPoseSearch;
		if (bJumpToPose)
		{
			const UPoseSearchDatabase* CurrentResultDatabase = MotionMatchingState.SearchResult.SelectedDatabase.Get();
			if (CurrentResultDatabase && CurrentResultDatabase->Schema)
			{
				if (UAnimationAsset* AnimationAsset = MotionMatchingState.SearchResult.GetAnimationAssetForRole())
				{
					// (the engine node's blend space re-use test needs a delta threshold above 0: the default is 0)
					FAnimNode_BlendStack_Standalone::BlendTo(Context, AnimationAsset, MotionMatchingState.SearchResult.SelectedTime, MotionMatchingState.SearchResult.bLoop,
						MotionMatchingState.SearchResult.bIsMirrored, CurrentResultDatabase->Schema->GetMirrorDataTable(MotionMatchingState.SearchResult.Role), BlendTime,
						BlendProfile, BlendOption, bUseInertialBlend, NAME_None, MotionMatchingState.SearchResult.BlendParameters, DesiredPlayRate, 0,
						GetGroupName(), GetGroupRole(), GetGroupMethod(), GetOverridePositionWhenJoiningSyncGroupAsLeader());
				}
				else
				{
					checkNoEntry();
				}
			}
		}

		const bool bDidBlendToRequestAnInertialBlend = bJumpToPose && bUseInertialBlend;
		UE::Anim::TOptionalScopedGraphMessage<UE::Anim::FAnimInertializationSyncScope> InertializationSync(bDidBlendToRequestAnInertialBlend, Context);

		FAnimNode_BlendStack_Standalone::UpdatePlayRate(DesiredPlayRate);
		FAnimNode_BlendStack_Standalone::UpdateBlendspaceParameters(EBlendStack_BlendspaceUpdateMode::InitialOnly, FVector::ZeroVector);
	}

	FAnimNode_BlendStack_Standalone::UpdateAssetPlayer(Context);
	NextUpdateInterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
}
PRAGMA_ENABLE_INTERNAL_WARNINGS
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS

void FTagMotionMatchNode::Evaluate_AnyThread(FPoseContext& Output)
{
	using namespace UE::PoseSearch;
	check(Output.AnimInstanceProxy);
	CheckInteractionThreadSafety(Output.AnimInstanceProxy->GetAnimInstanceObject());
	FAnimNode_BlendStack_Standalone::Evaluate_AnyThread(Output);
	// (the engine node goes on to the interaction warp only with bEnableWarp, off)
}

// ---------------------------------------------------------------------------------------------------------------------
// FTagOffsetRootNode: FAnimNode_OffsetRootBone (UE 5.8), graph driven, no collision testing, no velocity clamps.

namespace TagOffsetRoot
{
	// (the engine node's: https://theorangeduck.com/page/spring-roll-call#implicitspringdamper)
	static float DamperImplicit(float Halflife, float DeltaTime, float Epsilon = 1e-8f)
	{
		Halflife = FMath::Max(Halflife, DeltaTime);
		return FMath::Clamp((1.0f - FMath::InvExpApprox((0.69314718056f * DeltaTime) / (Halflife + Epsilon))), 0.0f, 1.0f);
	}

	static bool ShouldExtractRootMotion(const EOffsetRootBoneMode OffsetMode)
	{
		switch (OffsetMode)
		{
		case EOffsetRootBoneMode::Accumulate:
		case EOffsetRootBoneMode::Interpolate:
		case EOffsetRootBoneMode::LockOffsetAndConsumeAnimation:
		case EOffsetRootBoneMode::LockOffsetIncreaseAndConsumeAnimation:
			return true;
		default:
			return false;
		}
	}

	static bool ShouldCounterComponentDelta(const EOffsetRootBoneMode OffsetMode)
	{
		switch (OffsetMode)
		{
		case EOffsetRootBoneMode::Accumulate:
		case EOffsetRootBoneMode::Interpolate:
			return false;
		default:
			return true;
		}
	}
}

void FTagOffsetRootNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	AnimInstanceProxy = Context.AnimInstanceProxy;
	Source.Initialize(Context);
	Reset(Context);
}

void FTagOffsetRootNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	FAnimNode_Base::CacheBones_AnyThread(Context);
	Source.CacheBones(Context);
}

void FTagOffsetRootNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	FAnimNode_Base::Update_AnyThread(Context);
	CachedDeltaTime = Context.GetDeltaTime();
	GetEvaluateGraphExposedInputs().Execute(Context);
	// just became relevant and not yet initialized: reset
	if (!bIsFirstUpdate && UpdateCounter.HasEverBeenUpdated() && !UpdateCounter.WasSynchronizedCounter(Context.AnimInstanceProxy->GetUpdateCounter()))
	{
		Reset(Context);
	}
	UpdateCounter.SynchronizeWith(Context.AnimInstanceProxy->GetUpdateCounter());
	UE::Anim::TScopedGraphMessage<UE::AnimationWarping::FRootOffsetProvider> ScopedMessage(Context, FTransform(SimulatedRotation, SimulatedTranslation));
	Source.Update(Context);
}

void FTagOffsetRootNode::Evaluate_AnyThread(FPoseContext& Output)
{
	FAnimNode_Base::Evaluate_AnyThread(Output);
	Source.Evaluate(Output);

	const UE::Anim::IAnimRootMotionProvider* RootMotionProvider = UE::Anim::IAnimRootMotionProvider::Get();
	bool bGraphDriven = RootMotionProvider != nullptr;
	ensureMsgf(bGraphDriven, TEXT("Graph driven Offset Root Bone expected a valid root motion delta provider interface."));

	const FCompactPoseBoneIndex TargetBoneIndex(0);
	const FTransform InputBoneTransform = Output.Pose[TargetBoneIndex];

	FTransform LastComponentTransform = ComponentTransform;
	ComponentTransform = AnimInstanceProxy->GetComponentTransform();

	// a teleport (past the component's threshold): the simulated root follows it, and the component's move is not countered
	const float TeleportDistanceThreshold = Output.AnimInstanceProxy->GetSkelMeshComponent()->GetTeleportDistanceThreshold();
	if (FVector::Distance(LastComponentTransform.GetLocation(), ComponentTransform.GetLocation()) > TeleportDistanceThreshold)
	{
		if (bResetOnTeleport)
		{
			SimulatedTranslation = ComponentTransform.GetTranslation();
			SimulatedRotation = ComponentTransform.GetRotation();
		}
		else
		{
			const FTransform OffsetTransform = FTransform(SimulatedRotation, SimulatedTranslation).GetRelativeTransformReverse(LastComponentTransform);
			const FTransform TeleportedTransform = OffsetTransform * ComponentTransform;
			SimulatedTranslation = TeleportedTransform.GetLocation();
			SimulatedRotation = TeleportedTransform.GetRotation();
		}
		LastComponentTransform = ComponentTransform;
	}

	const EOffsetRootBoneMode CurrentTranslationMode = TranslationMode;
	const EOffsetRootBoneMode CurrentRotationMode = RotationMode;
	const bool bShouldConsumeTranslationOffset = TagOffsetRoot::ShouldExtractRootMotion(CurrentTranslationMode);
	const bool bShouldConsumeRotationOffset = TagOffsetRoot::ShouldExtractRootMotion(CurrentRotationMode);

	FTransform RootMotionTransformDelta = FTransform::Identity;
	if (bGraphDriven)
	{
		bGraphDriven = RootMotionProvider->ExtractRootMotion(Output.CustomAttributes, RootMotionTransformDelta);
	}
	RootMotionTransformDelta.NormalizeRotation();

	const float MaxTranslationOffset = MaxTranslationError;

	FTransform ConsumedRootMotionDelta;
	if (bShouldConsumeTranslationOffset) ConsumedRootMotionDelta.SetTranslation(RootMotionTransformDelta.GetTranslation());
	if (bShouldConsumeRotationOffset) ConsumedRootMotionDelta.SetRotation(RootMotionTransformDelta.GetRotation());

	if (TagOffsetRoot::ShouldCounterComponentDelta(CurrentRotationMode))
	{
		const FQuat ComponentRotationDelta = LastComponentTransform.GetRotation().Inverse() * ComponentTransform.GetRotation();
		SimulatedRotation = ComponentRotationDelta * SimulatedRotation;
	}
	if (TagOffsetRoot::ShouldCounterComponentDelta(CurrentTranslationMode))
	{
		const FVector ComponentTranslationDelta = ComponentTransform.GetLocation() - LastComponentTransform.GetLocation();
		SimulatedTranslation += ComponentTranslationDelta;
	}

	if (CurrentTranslationMode == EOffsetRootBoneMode::LockOffsetIncreaseAndConsumeAnimation)
	{
		FVector DeltaDir = (SimulatedTranslation - ComponentTransform.GetLocation()).GetSafeNormal();
		DeltaDir = SimulatedRotation.UnrotateVector(DeltaDir).GetSafeNormal();
		ConsumedRootMotionDelta.SetTranslation(DeltaDir * ConsumedRootMotionDelta.GetTranslation().Dot(DeltaDir));
	}
	if (CurrentRotationMode == EOffsetRootBoneMode::LockOffsetIncreaseAndConsumeAnimation)
	{
		const FQuat DeltaRot = ComponentTransform.GetRotation() * SimulatedRotation.Inverse();
		FVector DeltaAxis;
		float DeltaAngle;
		DeltaRot.ToAxisAndAngle(DeltaAxis, DeltaAngle);
		float RootMotionAngle = ConsumedRootMotionDelta.GetRotation().GetTwistAngle(DeltaAxis);
		RootMotionAngle = DeltaAngle >= 0.0f ? FMath::Clamp(RootMotionAngle, 0.0f, DeltaAngle) : FMath::Clamp(RootMotionAngle, DeltaAngle, 0.0f);
		ConsumedRootMotionDelta.SetRotation(FQuat(DeltaAxis, RootMotionAngle));
	}

	FTransform SimulatedTransform(SimulatedRotation, SimulatedTranslation);
	SimulatedTransform = ConsumedRootMotionDelta * SimulatedTransform;
	SimulatedTranslation = SimulatedTransform.GetLocation();
	SimulatedRotation = SimulatedTransform.GetRotation();

	if (bOnGround)
	{
		SimulatedTranslation = FVector::PointPlaneProject(SimulatedTranslation, ComponentTransform.GetLocation(), GroundNormal);
	}

	if (TranslationMode == EOffsetRootBoneMode::Release || TranslationMode == EOffsetRootBoneMode::Interpolate)
	{
		const FVector TranslationOffset = ComponentTransform.GetLocation() - SimulatedTranslation;
		const float DampenAlpha = TagOffsetRoot::DamperImplicit(TranslationHalflife, CachedDeltaTime);
		const FVector TranslationOffsetDelta = FMath::Lerp(FVector::ZeroVector, TranslationOffset, DampenAlpha);
		SimulatedTranslation = SimulatedTranslation + TranslationOffsetDelta;
	}

	if (RotationMode == EOffsetRootBoneMode::Release || RotationMode == EOffsetRootBoneMode::Interpolate)
	{
		FQuat RotationOffset = ComponentTransform.GetRotation() * SimulatedRotation.Inverse();
		RotationOffset.Normalize();
		RotationOffset = RotationOffset.W < 0.0 ? -RotationOffset : RotationOffset;
		const float DampenAlpha = TagOffsetRoot::DamperImplicit(RotationHalfLife, CachedDeltaTime);
		const FQuat RotationOffsetDelta = FQuat::Slerp(FQuat::Identity, RotationOffset, DampenAlpha);
		SimulatedRotation = RotationOffsetDelta * SimulatedRotation;
	}

	if (MaxTranslationOffset >= 0.0f)
	{
		FVector TranslationOffset = ComponentTransform.GetLocation() - SimulatedTranslation;
		const float TranslationOffsetSize = TranslationOffset.Size();
		if (TranslationOffsetSize > MaxTranslationOffset)
		{
			TranslationOffset = TranslationOffset.GetClampedToMaxSize(MaxTranslationOffset);
			SimulatedTranslation = ComponentTransform.GetLocation() - TranslationOffset;
		}
	}

	const float MaxAngleRadians = FMath::DegreesToRadians(MaxRotationError);
	if (MaxRotationError >= 0.0f)
	{
		FQuat RotationOffset = ComponentTransform.GetRotation().Inverse() * SimulatedRotation;
		RotationOffset.Normalize();
		RotationOffset = RotationOffset.W < 0.0 ? -RotationOffset : RotationOffset;
		FVector OffsetAxis;
		float OffsetAngle;
		RotationOffset.ToAxisAndAngle(OffsetAxis, OffsetAngle);
		if (FMath::Abs(OffsetAngle) > MaxAngleRadians)
		{
			RotationOffset = FQuat(OffsetAxis, MaxAngleRadians);
			SimulatedRotation = RotationOffset * ComponentTransform.GetRotation();
			SimulatedRotation.Normalize();
		}
	}

	SimulatedTransform.SetLocation(SimulatedTranslation);
	SimulatedTransform.SetRotation(SimulatedRotation);

	// the input pose's root, with the simulated transform accumulated in and the component's countered
	FTransform TargetBoneTransform = InputBoneTransform;
	TargetBoneTransform.Accumulate(SimulatedTransform * ComponentTransform.Inverse());
	TargetBoneTransform.SetScale3D(InputBoneTransform.GetScale3D());
	TargetBoneTransform.NormalizeRotation();
	Output.Pose[TargetBoneIndex] = TargetBoneTransform;

	if (bGraphDriven)
	{
		// (as the engine node: it computes the remaining delta but writes the extracted one back - kept, so the attribute
		// above this node is what it was)
		const bool bRootMotionOverridden = RootMotionProvider->OverrideRootMotion(RootMotionTransformDelta, Output.CustomAttributes);
		ensure(bRootMotionOverridden);
	}

	bIsFirstUpdate = false;
}

void FTagOffsetRootNode::Reset(const FAnimationBaseContext& Context)
{
	ComponentTransform = Context.AnimInstanceProxy->GetComponentTransform();
	SimulatedTranslation = ComponentTransform.GetLocation();
	SimulatedRotation = ComponentTransform.GetRotation();
	bIsFirstUpdate = true;
}
