#include "Game/GridironGameMode.h"
#include "Game/MatchDirector.h"
#include "Engine/World.h"

AGridironPlayerController::AGridironPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
}

AGridironGameMode::AGridironGameMode()
{
	PlayerControllerClass = AGridironPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}

void AGridironGameMode::BeginPlay()
{
	Super::BeginPlay();
	GetWorld()->SpawnActor<AMatchDirector>(AMatchDirector::StaticClass(), FTransform::Identity);
}
