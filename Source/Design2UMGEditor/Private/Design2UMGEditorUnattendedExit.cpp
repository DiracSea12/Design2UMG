// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorUnattendedExit.h"

#include "Design2UMGLog.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CoreMisc.h"

namespace Design2UMGEditorUnattendedExit
{
	/**
	 * Unattended exit policy lives at the command/orchestration boundary and covers
	 * every Run* entry point in the import orchestrator (PenConvert/PenImport/ImportSource/
	 * FigmaImport/LocalFigmaImport/DesignReimport, including their async Figma callbacks):
	 * business import functions report success/failure, and this single helper
	 * turns the final outcome into an engine exit status (0 = ok, 1 = failed).
	 * Attended sessions are untouched - it returns false and the editor keeps running.
	 * Automation test sessions are untouched as well: FApp::IsUnattended() counts
	 * GIsAutomationTesting as unattended, but there a RequestExit from a command would
	 * kill the editor before the framework reports results (TEST COMPLETE); their
	 * lifecycle is owned by -TestExit. Returns true when an exit was requested
	 * (genuinely unattended mode only).
	 */
	bool ExitUnattendedWithStatus(const bool bSuccess, const TCHAR* Message)
	{
		if (GIsAutomationTesting || !FApp::IsUnattended())
		{
			return false;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("%s"), Message);
		// Force=false only reaches PostQuitMessage, which the editor main loop does not
		// forward as the process exit code (a failed import still exits 0). Force=true
		// makes the engine flush the log and TerminateProcess with the real status.
		// Failure forces the exit; success keeps the graceful shutdown path.
		FPlatformMisc::RequestExitWithStatus(!bSuccess, bSuccess ? 0 : 1);
		return true;
	}

	// Unattended sessions must never open an interactive preview: the preview
	// path reports success as soon as the window opens, so exiting right after
	// would end the process before any asset is actually generated.
	bool ShouldUsePreviewInCurrentMode(const bool bRequestedPreview)
	{
		return bRequestedPreview && !FApp::IsUnattended();
	}
}
