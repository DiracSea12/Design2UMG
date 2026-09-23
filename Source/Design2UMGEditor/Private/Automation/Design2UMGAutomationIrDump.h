// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetData;

/**
 * Dump converted widget IR with sorted keys and %.9g floats for deterministic comparison.
 * Requires design2umgAutomation.IrDump and a non-empty per-render output path.
 */
namespace Design2UMGAutomationIrDump
{
#if WITH_DEV_AUTOMATION_TESTS
	/** Per-case dump output path; empty means no dump. Only meaningful during automation renders. */
	extern FString GDesign2UMGAutomationIrDumpPath;
#endif

	/** Writes the IR dump when the cvar is on and the automation dump path is non-empty. */
	void MaybeWriteIrDump(const FUMGWidgetData& Data);
}
