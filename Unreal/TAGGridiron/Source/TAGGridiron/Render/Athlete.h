// A football player on the field: motion-captured character + uniform pieces, driven from the simulation.
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

	/**
	 * Drive from simulation state.
	 * Location: world cm. Yaw: facing (deg). VelWorld: cm/s. Anim: sim animation state name.
	 * bLineman: uses crouched stance/footwork.
	 */
	void Drive(const FVector& Location, float Yaw, const FVector& VelWorld, float VMaxYds, const FString& Anim, float AnimT, float DeltaTime, bool bLineman);

	/** Restore an animated (non-ragdoll) pose, e.g. before the next snap. */
	void ResetPose();

	void SetSelected(bool bSel);
	FString DebugAnimState() const;
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

	/** MetaHuman body/face (visible) - pose retargeted from the hidden mocap mannequin `Body`. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> MHBody;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> MHFace;

	/** Facemask cage bars and shoulder-pad caps (created in Setup). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Gear;

	/** The mesh the player actually sees (MetaHuman if available, otherwise the mannequin). */
	USkeletalMeshComponent* Visible() const { return MHBody ? MHBody.Get() : Body.Get(); }

private:
	void PlayClip(UAnimationAsset* Clip, bool bLoop, float Rate = 1.f);
	void StartRagdoll(const FVector& VelWorld);
	void SetupMetaHuman(int32 Arch, const FUniform& U);
	FVector VisRelScale = FVector::OneVector;

	UPROPERTY()
	TObjectPtr<UAnimationAsset> CurrentClip;

	FString CurrentAnim;
	float SmoothedYaw = 0.f;
	float SmoothedDir = 0.f;
	float SmoothedSpeed = 0.f;
	bool bYawInit = false;
	bool bRagdoll = false;
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

/** Shared asset cache for athletes (loaded once). */
struct FAthleteAssets
{
	static FAthleteAssets& Get();
	bool bLoaded = false;
	bool bMocap = false; // Game Animation Sample (UEFN mannequin) assets available
	bool bMetaHumans = false; // TAG MetaHuman bodies available (retargeted from the mocap)
	FMHArchetype MH[3]; // 0 lineman, 1 skill, 2 big
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
	TObjectPtr<UAnimationAsset> Idle;
	TObjectPtr<class UMaterialInterface> UniformMat;
	float LocoMaxSpeed = 700.f;
	float CrouchMaxSpeed = 225.f;
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
