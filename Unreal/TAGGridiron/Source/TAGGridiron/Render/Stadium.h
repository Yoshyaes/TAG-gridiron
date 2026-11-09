// Procedurally built stadium: turf, markings, end zones, posts, stands, crowd, sky & lighting.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Stadium.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

namespace tag { struct Team; struct Weather; }

UCLASS()
class TAGGRIDIRON_API AStadium : public AActor
{
	GENERATED_BODY()

public:
	AStadium();
	void Build(const tag::Team& Home, const tag::Team& Away, const tag::Weather& W, bool bNight);
	virtual void Tick(float DeltaSeconds) override;

	/** Line of scrimmage / first-down overlays (world X in cm, or <0 to hide). */
	void SetLines(double LosX, double FirstDownX);

	/** Crowd excitement 0..1 (makes the crowd bounce). */
	float Hype = 0.f;

	/** The down on the chain crew's down box (0 hides the number: kicks, tries). */
	void SetDown(int32 Down);

	/** The chain crew's posts (0 rear rod, 1 front rod, 2 down box): the crew stand attached to them. */
	USceneComponent* CrewPost(int32 i) const { return i == 0 ? ChainRear.Get() : i == 1 ? ChainFront.Get() : DownBox.Get(); }
	/** Where the down box stands (automation framing). */
	FVector DownBoxLocation() const;

	/** The end-zone video boards' score lines. */
	void SetScoreboard(const FString& Top, const FString& Bottom);

private:
	/** The seating bowl: one swept surface around a rounded-rectangle path (tiers, aisles, suites, ribbon boards,
	 *  roof canopy), so the stands wrap the corners instead of meeting as separate slabs. */
	void BuildBowl(const tag::Team& Home, const tag::Team& Away, bool bNight);
	/** Chain crew equipment: rods 10 yd apart and the down box on the far sideline (the crew are AMatchDirector's). */
	void BuildOfficials();
	bool LoadFanMeshes();
	UPROPERTY()
	TObjectPtr<USceneComponent> ChainRear;
	UPROPERTY()
	TObjectPtr<USceneComponent> ChainFront;
	UPROPERTY()
	TObjectPtr<USceneComponent> DownBox;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ChainLink;
	UPROPERTY()
	TArray<TObjectPtr<class UTextRenderComponent>> DownText;
	UPROPERTY()
	TArray<TObjectPtr<class UTextRenderComponent>> ScoreText;
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FVector& Loc, const FVector& Scale, const FLinearColor& Color, float Rough = 0.8f, const FRotator& Rot = FRotator::ZeroRotator, bool bShadow = true);
	UInstancedStaticMeshComponent* AddISM(UStaticMesh* Mesh, const FLinearColor& Color, float Rough = 0.8f, bool bShadow = true);
	void AddText(const FString& S, const FVector& Loc, const FRotator& Rot, float Size, const FColor& Color);
	void BuildCrowd(const tag::Team& Home);
	void BuildRain(const tag::Weather& W);
	/** 3D spectators (MetaHuman Crowd Sample merged meshes) in the lowest rows of the sideline stands, idling and
	 *  cheering with Hype. Superseded by the Mass fan spawner (TagFanSpawner); kept as a fallback. */
	void BuildHeroCrowd(const FVector& StandC, float Yaw);
	void UpdateHeroCrowd(float Dt);

	UPROPERTY()
	TArray<TObjectPtr<class USkeletalMeshComponent>> Fans;
	TArray<float> FanDelay;          // per-fan reaction delay (s) so the stands ripple instead of switching at once
	TArray<uint8> FanCheering;
	float HypeHeld = 0.f;            // time Hype has been above / below the switch thresholds
	UPROPERTY()
	TObjectPtr<class UAnimationAsset> FanIdle;
	UPROPERTY()
	TObjectPtr<class UAnimationAsset> FanCheer;
	UPROPERTY()
	TArray<TObjectPtr<class USkeletalMesh>> FanMeshes;
	/** Mass crowd (TagFanSpawner): cheer on Hype by swapping the close-up crowd actors' body to the keyed cheer. */
	void UpdateMassFans(float Dt);
	UPROPERTY()
	TObjectPtr<class UAnimationAsset> MassCheer;
	TMap<TWeakObjectPtr<class USkeletalMeshComponent>, TWeakObjectPtr<UClass>> MassFanAnimClass; // fans currently cheering
	bool bMassCheer = false;
	UPROPERTY()
	TObjectPtr<class ATagFanSpawner> FanSpawnerActor;
	float MassScanT = 0.f;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CrowdSections;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> LosLine;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> FdLine;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Rain;

	TArray<FTransform> CrowdBase;
	TArray<int32> CrowdSectionOf;
	TArray<int32> CrowdLocal;
	int32 CrowdCursor = 0;
	TArray<FVector> RainPos;
	FVector Wind = FVector::ZeroVector;
	bool bSnow = false;
	float Time = 0.f;
};
