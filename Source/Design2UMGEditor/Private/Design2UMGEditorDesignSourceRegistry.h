// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetData;

namespace Design2UMGEditorDesignSourceRegistry
{
	void RegisterBuiltInSources();
	void UnregisterBuiltInSources();
	void GetRegisteredSourceIds(TArray<FName>& OutSourceIds);
	bool TryConvertSource(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData);
}
