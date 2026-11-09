#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"

/**
 * Generic timed screenshot for any map (used by Tools/Art/compare_shots.py for look comparisons):
 *   -TagShotAt=<seconds> [-TagShotName=<file.png>] [-TagShotExitAfter=<seconds>]
 * The timer starts once shaders and assets have finished compiling. Names no content paths.
 */
class FTAGGridironModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		float At = -1.f;
		if (!FParse::Value(FCommandLine::Get(), TEXT("TagShotAt="), At) || At < 0.f) return;
		ShotAt = At;
		ExitAfter = At + 2.f;
		FParse::Value(FCommandLine::Get(), TEXT("TagShotExitAfter="), ExitAfter);
		if (!FParse::Value(FCommandLine::Get(), TEXT("TagShotName="), ShotName)) ShotName = TEXT("tag_shot.png");
		Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTAGGridironModule::Tick));
	}

	virtual void ShutdownModule() override
	{
		if (Handle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	}

private:
	bool Tick(float Dt)
	{
		if (!GEngine || !GEngine->GameViewport) return true;
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return true;
#endif
		if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0) return true;
		T += Dt;
		if (!bShot && T >= ShotAt)
		{
			FScreenshotRequest::RequestScreenshot(ShotName, false, false);
			UE_LOG(LogTemp, Display, TEXT("TAG timed screenshot %s"), *ShotName);
			bShot = true;
		}
		if (bShot && T >= ExitAfter)
		{
			FPlatformMisc::RequestExit(false);
			return false;
		}
		return true;
	}

	FTSTicker::FDelegateHandle Handle;
	FString ShotName;
	float ShotAt = 0.f, ExitAfter = 0.f, T = 0.f;
	bool bShot = false;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FTAGGridironModule, TAGGridiron, "TAGGridiron");
