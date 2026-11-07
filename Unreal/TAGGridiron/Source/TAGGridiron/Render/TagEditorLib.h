// Small helpers exposed to editor Python (unreal.TagEditorLib) for the headless asset pipeline.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TagEditorLib.generated.h"

class UBlendSpace;

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
};
