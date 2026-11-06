// A football player on the field: skinned character + uniform pieces, posed from the simulation.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Athlete.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UAnimationAsset;
class UMaterialInstanceDynamic;

namespace tag { struct Player; }

struct FUniform
{
	FLinearColor Jersey;
	FLinearColor Pants;
	FLinearColor Helmet;
	FLinearColor Trim;
	FLinearColor Number;
};

UCLASS()
class TAGGRIDIRON_API AAthlete : public AActor
{
	GENERATED_BODY()

public:
	AAthlete();

	void Setup(const tag::Player* InPlayer, const FUniform& U);

	/** Drive from simulation state. Location in world cm, yaw degrees, speed in yd/s. */
	void Drive(const FVector& Location, float Yaw, float SpeedYds, float VMaxYds, const FString& Anim, float AnimT, float DeltaTime);

	void SetSelected(bool bSel);
	void SetLabel(const FString& Text);

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

private:
	void PlayClip(UAnimationAsset* Clip, bool bLoop, float Rate = 1.f);

	UPROPERTY()
	TObjectPtr<UAnimationAsset> CurrentClip;

	FString CurrentAnim;
	float BlendAxisMax = 500.f;
	float SmoothedYaw = 0.f;
	bool bYawInit = false;
};

/** Shared asset cache for athletes (loaded once). */
struct FAthleteAssets
{
	static FAthleteAssets& Get();
	bool bLoaded = false;
	TObjectPtr<class USkeletalMesh> Mesh;
	TObjectPtr<class UBlendSpace> Locomotion;
	TObjectPtr<UAnimationAsset> FallFront;
	TObjectPtr<UAnimationAsset> FallFront2;
	TObjectPtr<UAnimationAsset> FallBack;
	TObjectPtr<UAnimationAsset> HitReact;
	TObjectPtr<UAnimationAsset> Shove;
	TObjectPtr<UAnimationAsset> Throw;
	TObjectPtr<UAnimationAsset> Jump;
	TObjectPtr<UAnimationAsset> Dash;
	TObjectPtr<class UStaticMesh> Sphere;
	TObjectPtr<class UStaticMesh> Cube;
	TObjectPtr<class UStaticMesh> Cylinder;
	TObjectPtr<class UStaticMesh> Torus;
	TObjectPtr<class UStaticMesh> Plane;
	TObjectPtr<class UMaterialInterface> TurfMat;
	TObjectPtr<class UMaterialInterface> CrowdMat;
	TObjectPtr<class UMaterialInterface> ShapeMat;
	void Load();
};

/** Make a solid-color dynamic material from the engine's basic shape material. */
UMaterialInstanceDynamic* TagColorMat(UObject* Outer, const FLinearColor& C, float Roughness = 0.6f);
