#include "Render/TagRetargetAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TagRetargetAnimInstance)

FAnimInstanceProxy* UTagRetargetAnimInstance::CreateAnimInstanceProxy()
{
	return new FTagRetargetProxy(this, &RetargetNode);
}

void UTagRetargetAnimInstance::Configure(UIKRetargeter* Asset, USkeletalMeshComponent* Source)
{
	RetargetNode.IKRetargeterAsset = Asset;
	RetargetNode.RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
	RetargetNode.SourceMeshComponent = Source;
	RetargetNode.bSuppressWarnings = false;
	if (FIKRetargetProcessor* P = RetargetNode.GetRetargetProcessor())
	{
		P->SetNeedsInitialized();
	}
}
