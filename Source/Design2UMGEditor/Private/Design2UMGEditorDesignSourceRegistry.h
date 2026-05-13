// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesign2UMGImportSettingsSnapshot;
struct FUMGWidgetData;

namespace Design2UMGEditorDesignSourceRegistry
{
	void RegisterBuiltInSources();
	void UnregisterBuiltInSources();
	void GetRegisteredSourceIds(TArray<FName>& OutSourceIds);
	bool TryParseSourceDocument(FName SourceId, const FString& RawJson, FDesignDocument& OutDocument);
	bool TryConvertSource(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData);
	bool TryConvertSource(
		FName SourceId,
		const FString& RawJson,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		FUMGWidgetData& OutData);
}
