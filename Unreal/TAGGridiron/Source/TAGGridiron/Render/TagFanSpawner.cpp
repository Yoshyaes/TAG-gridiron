#include "Render/TagFanSpawner.h"

#include "MassEntityConfigAsset.h"
#include "MassSpawnLocationProcessor.h"
#include "MassSpawnerTypes.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"

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
	return true;
}

void ATagFanSpawner::Animate(float Time, float Hype)
{
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
