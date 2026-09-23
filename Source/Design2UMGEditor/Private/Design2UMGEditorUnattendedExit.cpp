// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorUnattendedExit.h"

#include "Design2UMGLog.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CoreMisc.h"

namespace Design2UMGEditorUnattendedExit
{
	/**
	 * Requests process exit at the command boundary: zero on success, one on failure.
	 * Interactive sessions keep running. Automation tests leave exit to their test runner.
	 * Returns whether an exit was requested.
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
