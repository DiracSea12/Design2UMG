// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetTree;

/**
 * Validation layer 2 (geometry golden): runtime-facts-only dump of the compiled
 * widget tree geometry. Writes a byte-stable geometry_dump.json (object keys
 * sorted, floats via %.9g, nodes in deterministic pre-order tree walk) taken at
 * the "compiled-after-draw" site so consecutive runs can be diffed against an
 * IR-derived expectation and a frozen golden.
 *
 * No expectation derivation happens here: policies and expected values are
 * computed by Tools/design2umg_geometry_compare.py from the same run's
 * ir_dump.json. Off by default: gated by the design2umgAutomation.GeometryDump
 * console variable plus a non-empty automation dump path (set by the automation
 * driver per render spec via TGuardValue, mirroring
 * GDesign2UMGAutomationIrDumpPath).
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
