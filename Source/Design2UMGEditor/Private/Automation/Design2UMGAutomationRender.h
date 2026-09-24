// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesign2UMGImportSettingsSnapshot;

namespace Design2UMGAutomationRender
{
	bool IsCaseLocalImportedFontPath(const FString& FontAssetPath);
	void SanitizeImportSettingsSnapshot(FDesign2UMGImportSettingsSnapshot& InOutSettingsSnapshot);
	void HandleConsoleCommand(const TArray<FString>& Args);
}
