#include "Render/TagEditorLib.h"

#include "Animation/BlendSpace.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "MetaHumanCollection.h"
#include "MetaHumanInstance.h"
#include "MetaHumanCharacterEditorPipeline.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "Containers/Ticker.h"
#include "Async/TaskGraphInterfaces.h"
#include "AssetCompilingManager.h"
#include "HAL/PlatformProcess.h"

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

bool UTagEditorLib::CrowdSetAnimationConfig(UMetaHumanCollection* Collection, UObject* AnimationConfig)
{
#if WITH_EDITOR
	if (!Collection || !AnimationConfig) return false;
	UMetaHumanCollectionEditorPipeline* EP = const_cast<UMetaHumanCollectionEditorPipeline*>(Collection->GetEditorPipeline());
	if (!EP) return false;
	FObjectPropertyBase* P = FindFProperty<FObjectPropertyBase>(EP->GetClass(), TEXT("AnimationConfig"));
	if (!P || !AnimationConfig->IsA(P->PropertyClass)) return false;
	EP->Modify();
	P->SetObjectPropertyValue_InContainer(EP, AnimationConfig);
	Collection->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UTagEditorLib::CrowdBuildCollection(UMetaHumanCollection* Collection, float TimeoutSeconds)
{
#if WITH_EDITOR
	if (!Collection) return false;
	bool bDone = false, bOk = false;
	Collection->Build(FInstancedStruct(), UMetaHumanCollection::FOnBuildComplete::CreateLambda([&bDone, &bOk](EMetaHumanBuildStatus Status)
	{
		bOk = Status == EMetaHumanBuildStatus::Succeeded;
		bDone = true;
	}));
	const double T0 = FPlatformTime::Seconds();
	double Last = T0, LastGC = T0;
	while (!bDone && FPlatformTime::Seconds() - T0 < TimeoutSeconds)
	{
		// the editor collects garbage between frames; a commandlet pump has to do it itself or every per-head bake's
		// temporary meshes and sequences pile up (the first headless builds ran out of commit memory)
		if (FPlatformTime::Seconds() - LastGC > 15.0)
		{
			LastGC = FPlatformTime::Seconds();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FTSTicker::GetCoreTicker().Tick(0.01f);
		FAssetCompilingManager::Get().ProcessAsyncTasks(true);
		FPlatformProcess::Sleep(0.01f);
		if (FPlatformTime::Seconds() - Last > 30.0)
		{
			Last = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Display, TEXT("TAG crowd build: %.0f s"), Last - T0);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("TAG crowd build %s after %.0f s"), !bDone ? TEXT("timed out") : bOk ? TEXT("succeeded") : TEXT("failed"), FPlatformTime::Seconds() - T0);
	if (bOk) Collection->MarkPackageDirty();
	return bOk;
#else
	return false;
#endif
}

bool UTagEditorLib::CrowdSetInstanceCollection(UMetaHumanInstance* Instance, UMetaHumanCollection* Collection)
{
	if (!Instance || !Collection) return false;
	Instance->SetMetaHumanCollection(Collection);
	Instance->MarkPackageDirty();
	return true;
}
