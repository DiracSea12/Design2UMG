// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesign2UMGImportSettingsSnapshot;

namespace Design2UMGAutomationRender
{
	bool IsAutomationRenderActive();
	bool PrepareGeneratedAssetForAutomationRender(
		const FString& GeneratedAssetPath,
		bool bForceReloadPackage,
		FString& OutError);
	void SanitizeImportSettingsSnapshot(FDesign2UMGImportSettingsSnapshot& InOutSettingsSnapshot);
	void HandleConsoleCommand(const TArray<FString>& Args);
}
