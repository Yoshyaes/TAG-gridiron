// Input latency, measured from inside the game (06-controls-feel-ui.md item 7), and the frame-pacing flags to compare.
//
// -TagLatencyLog: every gameplay press the director reads in a live play is followed to the sim step that takes it and
// the frame that draws that step, and a summary goes to the log every 5 s (and at the end):
//   TAG latency press <key>: arrive->read <ms>, read->step <ms> (<n> frames), step->frame end <ms>, est. press->photon <ms>
//   TAG latency: <fps> | game / render / RHI / GPU ms | engine input->vblank ms | Reflex total / game / render ms |
//                presses: p50 / p95 of each part | the frame-sync settings in force
// "arrive" is when the device's event reached the game (FTagInputHub, the Slate pre-processor: the start of the frame
// that reads it); a press itself comes on average half a frame before that, which the estimate adds. After the frame
// the photon path is the renderer's: the engine's own input-sample-to-vblank time (GInputLatencyTime, when the
// swap chain reports its flips), else Reflex's PC latency markers (with the Reflex plugin on an NVIDIA GPU: simulation
// start to GPU end), else the render thread + GPU times (+ a frame with r.OneFrameThreadLag). The display's own lag is
// not in any of them.
//
// The A/B flags (each applied at startup and logged as "TAG latency setting"):
//   -TagReflex=0|1|2      Reflex low latency off / on / on + boost (needs the Reflex plugin and an NVIDIA GPU)
//   -TagGTSync=0|1|2      r.GTSyncType (0 the default: the game thread syncs with the render thread; 2 with the swap
//                         chain's present - needs vsync)
//   -TagThreadLag=0|1     r.OneFrameThreadLag
//   -TagSyncSlack=<ms>    rhi.SyncSlackMS
//   -TagMaxFPS=<n>        t.MaxFPS (0 uncapped)
//   -TagVSync=0|1         r.VSync
// None of them changes anything unless given: the defaults are the engine's.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class FTagLatency
{
public:
	static FTagLatency& Get();
	/** -TagLatencyLog (or -TagFeelTest, which checks it). */
	static bool On();
	/** Once (the director's BeginPlay): the A/B flags, and - when logging - Reflex's latency markers turned on. */
	void Start();
	/** A gameplay press the director read this frame. Arrived: when the device's event reached the game (platform
	 *  seconds; < 0 not known - an injected press). */
	void NotePress(const FKey& Key, double Arrived);
	/** A sim step was taken: every press read and not yet stepped is taken by it. */
	void NoteStep();
	/** Once a frame, after every actor has ticked (the director's OnPostActorTick): presses settle, the frame's timings
	 *  are sampled, and every 5 s the summary. */
	void EndFrame();
	void LogSummary(const TCHAR* Why);

	struct FPress
	{
		FKey Key;
		double Arrived = -1.0, Read = 0.0, Stepped = -1.0, FrameEnd = -1.0;
		uint64 ReadFrame = 0, StepFrame = 0;
		float EstPhotonMs = -1.f;
	};
	/** The settled presses (the last 512; the self-check reads them). */
	TArray<FPress> Done;
	int32 NumSummaries = 0;
	/** The last summary's numbers. */
	float Fps = 0.f, GameMs = 0.f, RenderMs = 0.f, RhiMs = 0.f, GpuMs = 0.f, EngineInputMs = 0.f;
	float ReflexTotalMs = 0.f, ReflexGameMs = 0.f, ReflexRenderMs = 0.f, ReflexSimMs = 0.f;
	bool bReflexAvailable = false, bReflexLowLatency = false;

private:
	TArray<FPress> Waiting;
	double LastSummaryT = 0.0;
	bool bStarted = false;
	// the frame timings since the last summary (sums)
	double SumDt = 0.0, SumGame = 0.0, SumRender = 0.0, SumRhi = 0.0, SumGpu = 0.0;
	int32 Frames = 0;
	float PipelineMs() const;
};
