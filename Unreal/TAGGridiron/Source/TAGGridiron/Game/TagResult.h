// The verdict of a self-checking test mode (-TagPauseTest, -TagMultiTest, -TagInputTest ...), for the gate script
// (Tools/Gate): one machine-readable line in the log and the process's exit status.
//
//   TAG RESULT <mode> pass=<n> fail=<n>              (the mode's checks: <mode> is its flag's name, e.g. TagPauseTest)
//   TAG RESULT <mode> pass=<n> fail=<n> timeout      (-TagTimeout=<s> ran out first; the process exits 2)
//
// The process ends with 0 when every check passed, 1 when any failed (or none ran: nothing checked is not a pass) and 2
// on a timeout. On Windows the engine drops the code of a normal exit (FPlatformMisc::RequestExitWithStatus(false, n) only
// posts it as WM_QUIT; GuardedMain returns its init status), so the game shuts down as it always has and, at the very
// end of the engine's own shutdown (FCoreDelegates::OnExit: the world, Slate and audio already gone), a failing run
// ends the process there with its code. A passing run exits normally (0).
#pragma once

#include "CoreMinimal.h"

namespace TagResult
{
	/** Log the mode's result line, keep its status for the process's exit, and (bExit) ask the engine to quit. Several
	 *  modes in one run each log their own line; the worst status is the one the process exits with. */
	void Finish(const TCHAR* Mode, int32 Pass, int32 Fail, bool bExit = true);
	/** -TagTimeout=<seconds>: a watchdog thread that, when the time runs out before the run has finished (a hang, a
	 *  softlock, a check that never comes), logs "TAG timeout" and the result line and ends the process with 2. Called
	 *  once from the director's BeginPlay; nothing without the flag. A run that has finished gets 60 s more to shut down. */
	void StartWatchdog();
	/** The self-checking mode on the command line (its flag's name), or "TagRun" for none. */
	const TCHAR* ModeName();
}
