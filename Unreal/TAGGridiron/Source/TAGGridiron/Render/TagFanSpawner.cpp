#include "Render/TagFanSpawner.h"

#include "MassEntityConfigAsset.h"
#include "MassSpawnLocationProcessor.h"
#include "MassSpawnerTypes.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"
#include "MassRepresentationFragments.h"
#include "MassRepresentationTypes.h"
#include "MassCommonTypes.h"
#include "Misc/PackageName.h"

void UTagSeatSpawnGenerator::Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count,
	FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const
{
	TArray<FMassEntitySpawnDataGeneratorResult> Results;
	BuildResultsFromEntityTypes(FMath::Min(Count, Seats.Num()), EntityTypes, Results);
	int32 Next = 0;
	for (FMassEntitySpawnDataGeneratorResult& R : Results)
	{
		R.SpawnDataProcessor = UMassSpawnLocationProcessor::StaticClass();
		R.SpawnData.InitializeAs<FMassTransformsSpawnData>();
		FMassTransformsSpawnData& T = R.SpawnData.GetMutable<FMassTransformsSpawnData>();
		T.bRandomize = false; // one entity per seat, in order
		T.Transforms.Reserve(R.NumEntities);
		for (int32 i = 0; i < R.NumEntities && Next < Seats.Num(); ++i) T.Transforms.Add(Seats[Next++]);
	}
	FinishedGeneratingSpawnPointsDelegate.Execute(Results);
}

bool ATagFanSpawner::Setup(const TArray<FTransform>& Seats)
{
	UMassEntityConfigAsset* Config = LoadObject<UMassEntityConfigAsset>(nullptr, TEXT("/Game/TAG/Crowd/DA_TAG_FanConfig.DA_TAG_FanConfig"),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Config || Seats.Num() == 0) return false;
	Count = Seats.Num();
	bAutoSpawnOnBeginPlay = false;
	EntityTypes.Reset();
	FMassSpawnedEntityType& Type = EntityTypes.AddDefaulted_GetRef();
	Type.EntityConfig = Config;
	Type.Proportion = 1.f;
	UTagSeatSpawnGenerator* Gen = NewObject<UTagSeatSpawnGenerator>(this);
	Gen->Seats = Seats;
	SpawnDataGenerators.Reset();
	FMassSpawnDataGenerator& G = SpawnDataGenerators.AddDefaulted_GetRef();
	G.GeneratorInstance = Gen;
	G.Proportion = 1.f;
	// the TAG crowd rebuild bakes our cheer as sequence 2 (Tools/Art/crowd_rebuild.py rewires the config to it)
	bCheerBaked = FPackageName::DoesPackageExist(TEXT("/Game/TAG/Crowd/CrowdCollection_TAG")) &&
		FPackageName::DoesPackageExist(TEXT("/Game/TAG/Crowd/Instances/MHI_g0_bbody_0_fface_0_v0")); // set last by the script
	UE_LOG(LogTemp, Display, TEXT("TAG fans: %d seats, distant cheer %s"), Seats.Num(), bCheerBaked ? TEXT("baked") : TEXT("not baked (bounce only)"));
	return true;
}

void ATagFanSpawner::Animate(float Time, float Hype)
{
	UTagFanCheerProcessor::CheerAmount = bCheerBaked ? FMath::Clamp((Hype - 0.15f) / 0.6f, 0.f, 1.f) : 0.f;
	if (Hype < 0.05f && !bBouncing) return;
	UMassEntitySubsystem* ES = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!ES) return;
	FMassEntityManager& EM = ES->GetMutableEntityManager();
	if (FanHandles.Num() == 0)
	{
		for (const FSpawnedEntities& S : AllSpawnedEntities)
			for (const FMassEntityHandle H : S.Entities)
				if (const FTransformFragment* T = EM.GetFragmentDataPtr<FTransformFragment>(H))
				{
					FanHandles.Add(H);
					FanBase.Add(T->GetTransform().GetLocation());
				}
	}
	bBouncing = Hype >= 0.05f;
	for (int32 i = 0; i < FanHandles.Num(); ++i)
	{
		FTransformFragment* T = EM.IsEntityValid(FanHandles[i]) ? EM.GetFragmentDataPtr<FTransformFragment>(FanHandles[i]) : nullptr;
		if (!T) continue;
		// each fan on its own rhythm; jumps grow with the hype (up to ~26 cm on a touchdown)
		const float Hop = bBouncing ? FMath::Max(0.f, FMath::Sin(Time * (6.5f + (i % 5) * 0.6f) + i * 1.7f)) * (6.f + 20.f * Hype) : 0.f;
		T->GetMutableTransform().SetLocation(FanBase[i] + FVector(0, 0, Hop));
	}
}

float UTagFanCheerProcessor::CheerAmount = 0.f;

UTagFanCheerProcessor::UTagFanCheerProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Standalone);
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
	ExecutionOrder.ExecuteAfter.Add(TEXT("MetaHumanMassSimpleAnimationProcessor"));
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Representation);
	bRequiresGameThreadExecution = false;
}

void UTagFanCheerProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FMassRepresentationAnimationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
}

void UTagFanCheerProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	static bool bWasCheering = false;
	const float Amount = CheerAmount;
	if (Amount <= 0.f && !bWasCheering) return;
	bWasCheering = Amount > 0.f;
	EntityQuery.ForEachEntityChunk(Context, [Amount](FMassExecutionContext& Ctx)
	{
		const TConstArrayView<FMassRepresentationFragment> Reps = Ctx.GetFragmentView<FMassRepresentationFragment>();
		const TArrayView<FMassRepresentationAnimationFragment> Anims = Ctx.GetMutableFragmentView<FMassRepresentationAnimationFragment>();
		for (FMassExecutionContext::FEntityIterator It = Ctx.CreateEntityIterator(); It; ++It)
		{
			if (UE::Mass::Representation::IsValidActorRepresentation(Reps[It].CurrentRepresentation)) continue; // close-up actors: anim BP
			FAnimSequenceTrackAutoPlayData& A = Anims[It].AnimData;
			// stable per-fan threshold: the most excitable fans are up first, the whole stand on a touchdown
			const uint32 H = GetTypeHash(Ctx.GetEntity(It));
			const float Threshold = static_cast<float>(H % 1000u) / 1000.f;
			const int32 Want = Amount > Threshold ? 2 : 0;
			if (A.SequenceIndex != Want && (A.SequenceIndex == 2 || Want == 2))
			{
				A.SequenceIndex = Want;
				A.BlendTime = 0.35f;
			}
		}
	});
}
