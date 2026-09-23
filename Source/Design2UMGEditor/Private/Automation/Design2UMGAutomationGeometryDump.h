// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetTree;

/**
 * Dump compiled-after-draw widget geometry in deterministic tree order.
 * Keys are sorted and floats use %.9g. The Python geometry comparator derives expectations.
 * Requires design2umgAutomation.GeometryDump and a non-empty per-render output path.
 */
namespace Design2UMGAutomationGeometryDump
{
#if WITH_DEV_AUTOMATION_TESTS
	/** Per-case dump output path; empty means no dump. Only meaningful during automation renders. */
	extern FString GDesign2UMGAutomationGeometryDumpPath;
#endif

	/** Writes the geometry dump when the cvar is on and the automation dump path is non-empty. */
	void MaybeWriteGeometryDump(const UWidgetTree* WidgetTree);
}
