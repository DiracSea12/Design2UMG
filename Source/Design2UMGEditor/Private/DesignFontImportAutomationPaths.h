// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace DesignFontImportAutomationPaths
{
	FString NormalizeFontImportPath(const FString& InPath);
	bool CanReusePinnedFontImportPath(const FString& AutomationFontImportPath);
	bool IsAutomationImportedFontPath(const FString& ExistingFontPath);
}
