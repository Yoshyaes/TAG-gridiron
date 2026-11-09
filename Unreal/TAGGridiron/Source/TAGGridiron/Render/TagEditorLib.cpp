#include "Render/TagEditorLib.h"

#include "Animation/BlendSpace.h"
#include "PoseSearch/PoseSearchDatabase.h"

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

TArray<FString> UTagEditorLib::DatabaseAnimNames(UPoseSearchDatabase* Database)
{
	TArray<FString> Out;
	if (!Database) return Out;
	for (int32 i = 0; i < Database->GetNumAnimationAssets(); ++i)
		Out.Add(GetNameSafe(Database->GetAnimationAsset(i)));
	return Out;
}

int32 UTagEditorLib::DatabaseRemoveAnimsContaining(UPoseSearchDatabase* Database, const FString& Substring)
{
#if WITH_EDITOR
	if (!Database || Substring.IsEmpty()) return 0;
	int32 Removed = 0;
	for (int32 i = Database->GetNumAnimationAssets() - 1; i >= 0; --i)
		if (GetNameSafe(Database->GetAnimationAsset(i)).Contains(Substring))
		{
			Database->RemoveAnimationAssetAt(i);
			++Removed;
		}
	if (Removed) Database->MarkPackageDirty();
	return Removed;
#else
	return 0;
#endif
}

bool UTagEditorLib::DatabaseAddAnim(UPoseSearchDatabase* Database, UAnimationAsset* Anim)
{
#if WITH_EDITOR
	if (!Database || !Anim) return false;
	for (int32 i = 0; i < Database->GetNumAnimationAssets(); ++i)
		if (Database->GetAnimationAsset(i) == Anim) return false;
	FPoseSearchDatabaseAnimationAsset Entry;
	Entry.AnimAsset = Anim;
	Database->AddAnimationAsset(Entry);
	Database->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
