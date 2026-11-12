#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Components/SkinnedMeshComponent.h"
#include "UObject/UObjectIterator.h"

/**
 * Generic timed screenshot for any map (used by Tools/Art/compare_shots.py for look comparisons):
 *   -TagShotAt=<seconds> [-TagShotName=<file.png>] [-TagShotExitAfter=<seconds>]
 * Frame-time benchmark (Tools/Perf/bench.ps1):
 *   -TagBench=<seconds> [-TagBenchWarm=<seconds, default 25>]
 * logs "TAG bench" lines every 2 s and a "TAG bench summary" (avg fps, 1% low, game/draw/rhi/gpu ms), then exits.
 * Both timers start once shaders and assets have finished compiling. Names no content paths.
 */
class FTAGGridironModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		float At = -1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagShotAt="), At) && At >= 0.f)
		{
			ShotAt = At;
			ExitAfter = At + 2.f;
			FParse::Value(FCommandLine::Get(), TEXT("TagShotExitAfter="), ExitAfter);
			if (!FParse::Value(FCommandLine::Get(), TEXT("TagShotName="), ShotName)) ShotName = TEXT("tag_shot.png");
			bShotOn = true;
		}
		float B = -1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("TagBench="), B) && B > 0.f)
		{
			BenchSecs = B;
			FParse::Value(FCommandLine::Get(), TEXT("TagBenchWarm="), BenchWarm);
			bBenchOn = true;
		}
		if (bShotOn || bBenchOn)
			Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTAGGridironModule::Tick));
	}

	virtual void ShutdownModule() override
	{
		if (Handle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	}

private:
	struct FSample { float Frame, Game, Draw, Rhi, Gpu; };

	bool Tick(float Dt)
	{
		if (!GEngine || !GEngine->GameViewport) return true;
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0) return true;
#endif
		if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0) return true;
		T += Dt;
		if (bShotOn)
		{
			if (!bShot && T >= ShotAt)
			{
				FScreenshotRequest::RequestScreenshot(ShotName, false, false);
				UE_LOG(LogTemp, Display, TEXT("TAG timed screenshot %s"), *ShotName);
				bShot = true;
			}
			if (bShot && T >= ExitAfter && !bBenchOn)
			{
				FPlatformMisc::RequestExit(false);
				return false;
			}
		}
		if (bBenchOn) return TickBench();
		return true;
	}

	bool TickBench()
	{
		const double Now = FPlatformTime::Seconds();
		if (T < BenchWarm) { LastFrame = Now; return true; }
		if (!bBenchRegion) { TRACE_BEGIN_REGION(TEXT("TagBench")); bBenchRegion = true; Census(); }
		FSample S;
		S.Frame = static_cast<float>((Now - LastFrame) * 1000.0);
		LastFrame = Now;
		S.Game = static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime));
		S.Draw = static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		S.Rhi = static_cast<float>(FPlatformTime::ToMilliseconds(GRHIThreadTime));
		S.Gpu = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
		Samples.Add(S);
		Window.Add(S);
		if (Now - WindowStart >= 2.0)
		{
			if (WindowStart > 0.0) Log(TEXT("TAG bench"), Window);
			Window.Reset();
			WindowStart = Now;
		}
		if (T >= BenchWarm + BenchSecs)
		{
			TRACE_END_REGION(TEXT("TagBench"));
			Log(TEXT("TAG bench summary"), Samples);
			FPlatformMisc::RequestExit(false);
			return false;
		}
		return true;
	}

	/** One line per (owner class, component name): how many skinned / static / other primitives the frame carries. */
	static void Census()
	{
		struct FC { int32 N = 0, Vis = 0, Rendered = 0, Ticking = 0, Leader = 0, Uro = 0, Shadow = 0; FString Opt; };
		TMap<FString, FC> M;
		int32 Prims = 0, PrimsVis = 0;
		for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
		{
			UPrimitiveComponent* P = *It;
			UWorld* W = P->GetWorld();
			if (!W || !W->IsGameWorld() || !P->IsRegistered()) continue;
			++Prims;
			if (P->IsVisible()) ++PrimsVis;
			const AActor* O = P->GetOwner();
			FString Name = P->GetName();
			int32 Us = INDEX_NONE;
			if (Name.FindLastChar(TEXT('_'), Us) && Us > 0 && FCString::IsNumeric(*Name.Mid(Us + 1))) Name.LeftInline(Us);
			const FString Key = FString::Printf(TEXT("%s/%s/%s"), O ? *O->GetClass()->GetName() : TEXT("-"), *P->GetClass()->GetName(), *Name);
			FC& C = M.FindOrAdd(Key);
			++C.N;
			if (P->IsVisible()) ++C.Vis;
			if (P->WasRecentlyRendered(0.25f)) ++C.Rendered;
			if (P->IsComponentTickEnabled()) ++C.Ticking;
			if (P->CastShadow && P->IsVisible()) ++C.Shadow;
			if (const USkinnedMeshComponent* S = Cast<USkinnedMeshComponent>(P))
			{
				if (S->LeaderPoseComponent.IsValid()) ++C.Leader;
				if (S->bEnableUpdateRateOptimizations) ++C.Uro;
				C.Opt = FString::FromInt(static_cast<int32>(S->VisibilityBasedAnimTickOption));
			}
		}
		M.ValueSort([](const FC& A, const FC& B) { return A.N > B.N; });
		UE_LOG(LogTemp, Display, TEXT("TAG census primitives=%d visible=%d"), Prims, PrimsVis);
		for (const TPair<FString, FC>& KV : M)
			UE_LOG(LogTemp, Display, TEXT("TAG census %-60s n=%d vis=%d rendered=%d tick=%d shadow=%d leader=%d uro=%d opt=%s"), *KV.Key,
				KV.Value.N, KV.Value.Vis, KV.Value.Rendered, KV.Value.Ticking, KV.Value.Shadow, KV.Value.Leader, KV.Value.Uro, *KV.Value.Opt);
	}

	static void Log(const TCHAR* Tag, const TArray<FSample>& In)
	{
		if (In.Num() == 0) return;
		double F = 0, G = 0, D = 0, R = 0, P = 0;
		TArray<float> Frames;
		for (const FSample& S : In) { F += S.Frame; G += S.Game; D += S.Draw; R += S.Rhi; P += S.Gpu; Frames.Add(S.Frame); }
		const double N = In.Num();
		Frames.Sort();
		const float P99 = Frames[FMath::Clamp(static_cast<int32>(Frames.Num() * 0.99), 0, Frames.Num() - 1)];
		const float Med = Frames[Frames.Num() / 2];
		const FIntPoint Sz = GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport ? GEngine->GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		UE_LOG(LogTemp, Display, TEXT("%s frames=%d fps=%.1f median=%.2fms low1=%.1ffps game=%.2f draw=%.2f rhi=%.2f gpu=%.2f res=%dx%d"),
			Tag, In.Num(), 1000.0 / (F / N), Med, 1000.f / FMath::Max(P99, 0.01f), G / N, D / N, R / N, P / N, Sz.X, Sz.Y);
	}

	FTSTicker::FDelegateHandle Handle;
	FString ShotName;
	float ShotAt = 0.f, ExitAfter = 0.f, T = 0.f;
	bool bShot = false, bShotOn = false, bBenchOn = false, bBenchRegion = false;
	float BenchSecs = 0.f, BenchWarm = 25.f;
	double LastFrame = 0.0, WindowStart = 0.0;
	TArray<FSample> Samples, Window;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FTAGGridironModule, TAGGridiron, "TAGGridiron");
