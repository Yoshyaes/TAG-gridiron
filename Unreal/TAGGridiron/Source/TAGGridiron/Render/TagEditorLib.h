// Small helpers exposed to editor Python (unreal.TagEditorLib) for the headless asset pipeline.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TagEditorLib.generated.h"

class UBlendSpace;
class UPoseSearchDatabase;
class UAnimationAsset;
class UMetaHumanCollection;
class UMetaHumanInstance;

UCLASS()
class TAGGRIDIRON_API UTagEditorLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Validate samples and rebuild a blend space's runtime triangulation/grid data.
	 * The blend space editor does this when you edit samples in its UI; assets authored from
	 * Python never get it, and then evaluate to the bind pose at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "TAG")
	static bool ResampleBlendSpace(UBlendSpace* BlendSpace);

	/** Names of the animations a PoseSearch database indexes (Tools/Art/mm_build_dbs.py). */
	UFUNCTION(BlueprintCallable, Category = "TAG")
	static TArray<FString> DatabaseAnimNames(UPoseSearchDatabase* Database);

	/** Remove every database entry whose animation name contains Substring; returns how many were removed. */
	UFUNCTION(BlueprintCallable, Category = "TAG")
	static int32 DatabaseRemoveAnimsContaining(UPoseSearchDatabase* Database, const FString& Substring);

	/** Append an animation (sequence or blend space) to a database with default sampling; false if already present. */
	UFUNCTION(BlueprintCallable, Category = "TAG")
	static bool DatabaseAddAnim(UPoseSearchDatabase* Database, UAnimationAsset* Anim);

	/** MetaHuman Crowd: point a collection's crowd editor pipeline at an animation config (the clips baked for
	 *  distant instanced crowd members). Tools/Art/crowd_rebuild.py */
	UFUNCTION(BlueprintCallable, Category = "TAG")
	static bool CrowdSetAnimationConfig(UMetaHumanCollection* Collection, UObject* AnimationConfig);

	/** Build a MetaHuman collection and wait for it (headless commandlets pump the engine until it completes or
	 *  TimeoutSeconds passes). Returns true on success. */
	UFUNCTION(BlueprintCallable, Category = "TAG")
	static bool CrowdBuildCollection(UMetaHumanCollection* Collection, float TimeoutSeconds = 7200.f);

	/** Point a MetaHuman instance at another collection (e.g. a TAG rebuild of the crowd sample's). */
	UFUNCTION(BlueprintCallable, Category = "TAG")
	static bool CrowdSetInstanceCollection(UMetaHumanInstance* Instance, UMetaHumanCollection* Collection);
};
