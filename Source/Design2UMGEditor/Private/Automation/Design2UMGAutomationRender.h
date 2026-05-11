// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace Design2UMGAutomationRender
{
	bool IsAutomationRenderActive();
	FString GetAutomationFontImportPath();
	void HandleConsoleCommand(const TArray<FString>& Args);
}
