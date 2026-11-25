// The test modes' verdicts (TagResult.h): the "TAG RESULT" line, the exit status, the -TagTimeout watchdog.
#include "Game/TagResult.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include <atomic>
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
	/** -1: no mode has reported; else the worst status so far (0 all passed, 1 a check failed). */
	std::atomic<int32> GStatus{-1};
	bool bExitHooked = false;

	/** The process ends here, with Code (the log written out first). */
	void EndProcess(int32 Code, bool bFromWorker)
	{
		if (GLog)
		{
			if (bFromWorker) GLog->Panic(); // (the game thread may be stuck: this thread writes the log out itself)
			else GLog->Flush();
		}
#if PLATFORM_WINDOWS
		::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(Code));
#else
		FPlatformMisc::RequestExitWithStatus(true, static_cast<uint8>(Code));
#endif
	}

	/** -TagTimeout's thread: wakes four times a second and ends the process once the deadline has passed. */
	class FTagWatchdog final : public FRunnable
	{
	public:
		explicit FTagWatchdog(double InDeadline, double InSecs) : Deadline(InDeadline), Secs(InSecs) {}
		std::atomic<double> Deadline;
		std::atomic<bool> bQuit{false};
		const double Secs;
		virtual uint32 Run() override
		{
			while (!bQuit.load())
			{
				if (FPlatformTime::Seconds() >= Deadline.load())
				{
					UE_LOG(LogTemp, Display, TEXT("TAG timeout after %.0f s"), Secs);
					UE_LOG(LogTemp, Display, TEXT("TAG RESULT %s pass=0 fail=1 timeout"), TagResult::ModeName());
					EndProcess(2, true);
					return 0;
				}
				FPlatformProcess::Sleep(0.25f);
			}
			return 0;
		}
		virtual void Stop() override { bQuit = true; }
	};
	FTagWatchdog* GWatch = nullptr;
	FRunnableThread* GWatchThread = nullptr;

	void OnEngineExit()
	{
		// (the engine's shutdown is all but done: the watchdog stands down, and a failing run takes its code with it)
		if (GWatch && GWatchThread)
		{
			GWatchThread->Kill(true);
			delete GWatchThread;
			delete GWatch;
			GWatchThread = nullptr;
			GWatch = nullptr;
		}
		const int32 Status = GStatus.load();
		if (Status > 0)
		{
			UE_LOG(LogTemp, Display, TEXT("TAG exit status %d"), Status);
			EndProcess(Status, false);
		}
	}

	void HookExit()
	{
		if (bExitHooked) return;
		bExitHooked = true;
		FCoreDelegates::OnExit.AddStatic(&OnEngineExit);
	}
}

const TCHAR* TagResult::ModeName()
{
	static const TCHAR* const Modes[] = {TEXT("TagPauseTest"), TEXT("TagMultiTest"), TEXT("TagPadPlayTest"), TEXT("TagPadTest"), TEXT("TagControlsTest"),
		TEXT("TagInputTest"), TEXT("TagChartTest"), TEXT("TagFeelTest"), TEXT("TagHitStopTest"),
		TEXT("TagMixTest"), TEXT("TagGfxShots"), TEXT("TagRecentTest"), TEXT("TagReplayTest"), TEXT("TagShotTest"), TEXT("TagPaceTest"), TEXT("TagOpenTest"), TEXT("TagCallCoachTest"), TEXT("TagHalftimeTest"), TEXT("TagPostgameTest")};
	for (const TCHAR* M : Modes)
		if (FParse::Param(FCommandLine::Get(), M)) return M;
	if (FCString::Strifind(FCommandLine::Get(), TEXT("TagSoak="))) return TEXT("TagSoak");
	return TEXT("TagRun");
}

void TagResult::Finish(const TCHAR* Mode, int32 Pass, int32 Fail, bool bExit)
{
	// (a mode that checked nothing has not passed)
	const int32 Status = Fail > 0 || Pass <= 0 ? 1 : 0;
	UE_LOG(LogTemp, Display, TEXT("TAG RESULT %s pass=%d fail=%d"), Mode, Pass, Fail);
	GStatus = FMath::Max(GStatus.load(), Status);
	if (GWatch) GWatch->Deadline = FMath::Max(GWatch->Deadline.load(), FPlatformTime::Seconds() + 60.0);
	HookExit();
	if (bExit) FPlatformMisc::RequestExit(false);
}

void TagResult::StartWatchdog()
{
	float Secs = 0.f;
	if (GWatch || !FParse::Value(FCommandLine::Get(), TEXT("TagTimeout="), Secs) || Secs <= 0.f) return;
	GWatch = new FTagWatchdog(FPlatformTime::Seconds() + Secs, Secs);
	GWatchThread = FRunnableThread::Create(GWatch, TEXT("TagWatchdog"), 0, TPri_BelowNormal);
	if (!GWatchThread)
	{
		delete GWatch;
		GWatch = nullptr;
		return;
	}
	HookExit();
	UE_LOG(LogTemp, Display, TEXT("TAG watchdog: %s ends in %.0f s unless it finishes first"), ModeName(), Secs);
}
