// Stadium spectators through the MetaHuman Crowd Sample's Mass pipeline: each fan is a Mass entity dressed from the
// sample's MetaHuman Instances by MetaHumanMassCrowdVisualizationTrait (high-res actors near the camera, cheaper
// representations far away). DA_TAG_FanConfig is the sample config without its walking traits (Tools/Art/crowd_fan_config.py).
#pragma once

#include "CoreMinimal.h"
#include "MassSpawner.h"
#include "MassEntitySpawnDataGeneratorBase.h"
#include "MassEntityHandle.h"
#include "TagFanSpawner.generated.h"

/** Hands the spawner a fixed list of seat transforms (one entity per seat). */
UCLASS()
class TAGGRIDIRON_API UTagSeatSpawnGenerator : public UMassEntitySpawnDataGeneratorBase
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FTransform> Seats;

	virtual void Generate(UObject& QueryOwner, TConstArrayView<FMassSpawnedEntityType> EntityTypes, int32 Count,
		FFinishedGeneratingSpawnDataSignature& FinishedGeneratingSpawnPointsDelegate) const override;
};

/** Mass spawner for seated fans; Setup() then SpawnFans(). */
UCLASS()
class TAGGRIDIRON_API ATagFanSpawner : public AMassSpawner
{
	GENERATED_BODY()
public:
	/** Returns false when the fan config asset is missing. */
	bool Setup(const TArray<FTransform>& Seats);
	void SpawnFans() { DoSpawning(); }
	int32 SeatCount() const { return GetSpawnCount(); }
	/** Fans jump and bounce on their seats with Hype (0..1); settles back to the seat at 0. The instanced fans
	 *  play the sample's baked idle, so the bounce is driven through each entity's transform fragment. */
	void Animate(float Time, float Hype);
private:
	TArray<FMassEntityHandle> FanHandles;
	TArray<FVector> FanBase;
	bool bBouncing = false;
};
