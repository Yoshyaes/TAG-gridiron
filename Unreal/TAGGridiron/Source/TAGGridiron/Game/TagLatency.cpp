// The input latency log and the frame-pacing A/B flags (Game/TagLatency.h).
#include "Game/TagLatency.h"

#include "DynamicRHI.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Performance/LatencyMarkerModule.h"
#include "Performance/MaxTickRateHandlerModule.h"
#include "RHICommandList.h"
#include "RenderTimer.h"

namespace
{
	int32 LatCVarInt(const TCHAR* Name, int32 Default = -1)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetInt() : Default;
	}

	float LatCVarFloat(const TCHAR* Name, float Default = -1.f)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetFloat() : Default;
	}

	/** -<Flag>=<value> sets the console variable (logged either way). */
	void LatApplyFlag(const TCHAR* FlagName, const TCHAR* CVar)
	{
		FString V;
		if (!FParse::Value(FCommandLine::Get(), FlagName, V)) return;
		IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(CVar);
		if (C) C->Set(*V, ECVF_SetByCommandline);
		UE_LOG(LogTemp, Display, TEXT("TAG latency setting: %s %s (%s)"), CVar, *V, C ? TEXT("set") : TEXT("no such console variable"));
	}

	TArray<ILatencyMarkerModule*> LatMarkers()
	{
		return IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	}

	TArray<IMaxTickRateHandlerModule*> LatTickRateHandlers()
	{
		return IModularFeatures::Get().GetModularFeatureImplementations<IMaxTickRateHandlerModule>(IMaxTickRateHandlerModule::GetModularFeatureName());
	}

	/** The P-th percentile (0..100) of V (sorted here). */
	float LatPct(TArray<float> V, float P)
	{
		if (V.Num() == 0) return -1.f;
		V.Sort();
		return V[FMath::Clamp(FMath::RoundToInt(P / 100.f * (V.Num() - 1)), 0, V.Num() - 1)];
	}
}

FTagLatency& FTagLatency::Get()
{
	static FTagLatency L;
	return L;
}

bool FTagLatency::On()
{
	static const bool b = FParse::Param(FCommandLine::Get(), TEXT("TagLatencyLog")) || FParse::Param(FCommandLine::Get(), TEXT("TagFeelTest"));
	return b;
}

void FTagLatency::Start()
{
	if (bStarted) return;
	bStarted = true;
	// the frame-pacing flags (nothing unless given: the engine's defaults stand)
	LatApplyFlag(TEXT("TagGTSync="), TEXT("r.GTSyncType"));
	LatApplyFlag(TEXT("TagThreadLag="), TEXT("r.OneFrameThreadLag"));
	LatApplyFlag(TEXT("TagSyncSlack="), TEXT("rhi.SyncSlackMS"));
	LatApplyFlag(TEXT("TagMaxFPS="), TEXT("t.MaxFPS"));
	LatApplyFlag(TEXT("TagVSync="), TEXT("r.VSync"));
	// Reflex (the engine's Reflex plugin registers these features; without it, or on another GPU, there are none)
	for (IMaxTickRateHandlerModule* H : LatTickRateHandlers()) bReflexAvailable = bReflexAvailable || H->GetAvailable();
	int32 Mode = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("TagReflex="), Mode))
	{
		for (IMaxTickRateHandlerModule* H : LatTickRateHandlers())
		{
			H->SetEnabled(Mode > 0);
			H->SetFlags(Mode >= 2 ? 3u : Mode == 1 ? 1u : 0u);
		}
		bReflexLowLatency = Mode > 0 && bReflexAvailable;
		UE_LOG(LogTemp, Display, TEXT("TAG latency setting: Reflex low latency %s (%s)"), Mode >= 2 ? TEXT("on + boost") : Mode == 1 ? TEXT("on") : TEXT("off"),
			bReflexAvailable ? TEXT("available") : LatTickRateHandlers().Num() ? TEXT("not available on this GPU / driver") : TEXT("the Reflex plugin is not loaded"));
	}
	if (On())
		for (ILatencyMarkerModule* M : LatMarkers())
			if (M->GetAvailable()) M->SetEnabled(true);
	if (On())
		UE_LOG(LogTemp, Display, TEXT("TAG latency log on: Reflex %s; r.GTSyncType %d, r.OneFrameThreadLag %d, rhi.SyncSlackMS %.1f, t.MaxFPS %.0f, r.VSync %d"),
			bReflexAvailable ? TEXT("available") : TEXT("not available"), LatCVarInt(TEXT("r.GTSyncType")), LatCVarInt(TEXT("r.OneFrameThreadLag")), LatCVarFloat(TEXT("rhi.SyncSlackMS")),
			LatCVarFloat(TEXT("t.MaxFPS")), LatCVarInt(TEXT("r.VSync")));
	LastSummaryT = FPlatformTime::Seconds();
}

void FTagLatency::NotePress(const FKey& Key, double Arrived)
{
	if (!On()) return;
	FPress P;
	P.Key = Key;
	P.Arrived = Arrived;
	P.Read = FPlatformTime::Seconds();
	P.ReadFrame = GFrameCounter;
	Waiting.Add(P);
	if (Waiting.Num() > 64) Waiting.RemoveAt(0); // (never stepped: the play ended under it)
}

void FTagLatency::NoteStep()
{
	if (!On() || Waiting.Num() == 0) return;
	const double Now = FPlatformTime::Seconds();
	for (FPress& P : Waiting)
		if (P.Stepped < 0.0)
		{
			P.Stepped = Now;
			P.StepFrame = GFrameCounter;
		}
}

float FTagLatency::PipelineMs() const
{
	// after the game thread's frame: the engine's input -> vblank less the game thread's part, else Reflex's total less
	// its simulation, else the render thread and the GPU (+ a frame queued with r.OneFrameThreadLag)
	if (EngineInputMs > 0.f) return FMath::Max(0.f, EngineInputMs - GameMs);
	if (ReflexTotalMs > 0.f) return FMath::Max(0.f, ReflexTotalMs - ReflexSimMs);
	const float Frame = Fps > 0.f ? 1000.f / Fps : 16.7f;
	return RenderMs + GpuMs + (LatCVarInt(TEXT("r.OneFrameThreadLag"), 1) > 0 ? Frame : 0.f);
}

void FTagLatency::EndFrame()
{
	if (!On()) return;
	const double Now = FPlatformTime::Seconds();
	// this frame's timings (the engine's last measured values)
	SumDt += FApp::GetDeltaTime();
	SumGame += FPlatformTime::ToMilliseconds(GGameThreadTime);
	SumRender += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	SumRhi += FPlatformTime::ToMilliseconds(GRHIThreadTime);
	SumGpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	++Frames;
	if (Frames == 1 && Fps <= 0.f)
	{
		// (the first frame: something to estimate with before the first summary)
		Fps = SumDt > 0.0 ? static_cast<float>(1.0 / SumDt) : 0.f;
		GameMs = static_cast<float>(SumGame);
		RenderMs = static_cast<float>(SumRender);
		GpuMs = static_cast<float>(SumGpu);
	}
	// the presses whose step was drawn this frame settle
	const float FrameMs = Fps > 0.f ? 1000.f / Fps : 16.7f;
	for (int32 i = Waiting.Num() - 1; i >= 0; --i)
	{
		FPress& P = Waiting[i];
		if (P.Stepped < 0.0) continue;
		P.FrameEnd = Now;
		const double ArriveToRead = P.Arrived > 0.0 ? FMath::Max(0.0, P.Read - P.Arrived) : 0.0;
		P.EstPhotonMs = static_cast<float>(0.5 * FrameMs + ArriveToRead * 1000.0 + (P.Stepped - P.Read) * 1000.0 + (P.FrameEnd - P.Stepped) * 1000.0) + PipelineMs();
		UE_LOG(LogTemp, Display, TEXT("TAG latency press %s: arrive->read %s, read->step %.1f ms (%llu frames), step->frame end %.1f ms, est. press->photon %.1f ms"),
			*P.Key.ToString(), P.Arrived > 0.0 ? *FString::Printf(TEXT("%.1f ms"), ArriveToRead * 1000.0) : TEXT("n/a (injected)"), (P.Stepped - P.Read) * 1000.0,
			P.StepFrame - P.ReadFrame, (P.FrameEnd - P.Stepped) * 1000.0, P.EstPhotonMs);
		Done.Add(P);
		Waiting.RemoveAt(i);
	}
	if (Done.Num() > 512) Done.RemoveAt(0, Done.Num() - 512);
	if (Now - LastSummaryT >= 5.0) LogSummary(TEXT("every 5 s"));
}

void FTagLatency::LogSummary(const TCHAR* Why)
{
	if (!On()) return;
	LastSummaryT = FPlatformTime::Seconds();
	if (Frames > 0)
	{
		Fps = SumDt > 0.0 ? static_cast<float>(Frames / SumDt) : 0.f;
		GameMs = static_cast<float>(SumGame / Frames);
		RenderMs = static_cast<float>(SumRender / Frames);
		RhiMs = static_cast<float>(SumRhi / Frames);
		GpuMs = static_cast<float>(SumGpu / Frames);
	}
	SumDt = SumGame = SumRender = SumRhi = SumGpu = 0.0;
	Frames = 0;
	EngineInputMs = static_cast<float>(FPlatformTime::ToMilliseconds64(GInputLatencyTime));
	ReflexTotalMs = ReflexGameMs = ReflexRenderMs = ReflexSimMs = 0.f;
	for (ILatencyMarkerModule* M : LatMarkers())
		if (M->GetAvailable() && M->GetEnabled())
		{
			ReflexTotalMs = M->GetTotalLatencyInMs();
			ReflexGameMs = M->GetGameLatencyInMs();
			ReflexRenderMs = M->GetRenderLatencyInMs();
			ReflexSimMs = M->GetSimulationLatencyInMs();
		}
	TArray<float> ArriveRead, ReadStep, Photon;
	uint64 MaxWait = 0;
	for (const FPress& P : Done)
	{
		if (P.Arrived > 0.0) ArriveRead.Add(static_cast<float>((P.Read - P.Arrived) * 1000.0));
		ReadStep.Add(static_cast<float>((P.Stepped - P.Read) * 1000.0));
		Photon.Add(P.EstPhotonMs);
		MaxWait = FMath::Max(MaxWait, P.StepFrame - P.ReadFrame);
	}
	++NumSummaries;
	UE_LOG(LogTemp, Display, TEXT("TAG latency (%s): %.0f fps | game %.1f render %.1f rhi %.1f gpu %.1f ms | engine input->vblank %s | Reflex %s | %d presses: arrive->read p50 %.1f p95 %.1f, read->step p50 %.1f p95 %.1f ms (most frames waited %llu), est. press->photon p50 %.1f p95 %.1f ms | r.GTSyncType %d r.OneFrameThreadLag %d rhi.SyncSlackMS %.1f t.MaxFPS %.0f r.VSync %d, Reflex low latency %s"),
		Why, Fps, GameMs, RenderMs, RhiMs, GpuMs, EngineInputMs > 0.f ? *FString::Printf(TEXT("%.1f ms"), EngineInputMs) : TEXT("n/a"),
		ReflexTotalMs > 0.f ? *FString::Printf(TEXT("total %.1f game %.1f render %.1f sim %.1f ms"), ReflexTotalMs, ReflexGameMs, ReflexRenderMs, ReflexSimMs) : bReflexAvailable ? TEXT("no reading yet") : TEXT("n/a"),
		Done.Num(), LatPct(ArriveRead, 50.f), LatPct(ArriveRead, 95.f), LatPct(ReadStep, 50.f), LatPct(ReadStep, 95.f), MaxWait, LatPct(Photon, 50.f), LatPct(Photon, 95.f),
		LatCVarInt(TEXT("r.GTSyncType")), LatCVarInt(TEXT("r.OneFrameThreadLag")), LatCVarFloat(TEXT("rhi.SyncSlackMS")), LatCVarFloat(TEXT("t.MaxFPS")), LatCVarInt(TEXT("r.VSync")),
		bReflexLowLatency ? TEXT("on") : TEXT("off"));
}
