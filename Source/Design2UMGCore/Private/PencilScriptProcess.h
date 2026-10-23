// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"

namespace PencilScriptProcess
{
	// Runs one bare-engine evaluation in a separate LPAC process; returns only its JSON result.
	bool Evaluate(const FString& Program, FString& OutJSON, FString& Error);
}
