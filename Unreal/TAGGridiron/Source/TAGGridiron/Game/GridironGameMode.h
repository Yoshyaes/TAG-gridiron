#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GridironGameMode.generated.h"

UCLASS()
class TAGGRIDIRON_API AGridironPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AGridironPlayerController();
};

UCLASS()
class TAGGRIDIRON_API AGridironGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AGridironGameMode();
	virtual void BeginPlay() override;
};
