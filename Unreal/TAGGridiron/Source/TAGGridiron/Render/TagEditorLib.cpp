#include "Render/TagEditorLib.h"

#include "Animation/BlendSpace.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TagEditorLib)

bool UTagEditorLib::ResampleBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
	if (!BlendSpace) return false;
	BlendSpace->ValidateSampleData();
	BlendSpace->ResampleData();
	BlendSpace->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
