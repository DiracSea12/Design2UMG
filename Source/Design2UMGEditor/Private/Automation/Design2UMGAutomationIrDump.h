// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetData;

/**
 * Validation layer 1 (IR golden): fidelity serialization of the converted FUMGWidgetData
 * typed tree. Writes a byte-stable ir_dump.json (object keys sorted, floats via %.9g)
 * so consecutive runs can be diffed field-by-field against a frozen golden.
 *
 * Off by default: gated by the design2umgAutomation.IrDump console variable plus a
 * non-empty automation dump path (set by the automation driver per render spec via
 * TGuardValue, mirroring GDesign2UMGAutomationFontImportPath).
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
