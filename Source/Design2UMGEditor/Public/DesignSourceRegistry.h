// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * Conversion function for a registered design source.
 * Input is the raw source JSON, output is normalized widget data.
 */
using FDesignSourceConvertFunc = TFunction<bool(const FString& RawJson, FUMGWidgetData& OutData)>;

/**
 * Registry for internal design source adapters.
 * Adapters register during startup and unregister during shutdown.
 */
class FDesignSourceRegistry
{
public:
	static void Register(FName SourceId, FDesignSourceConvertFunc Func);
	static void Unregister(FName SourceId);
	static bool Convert(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData);
	static bool IsRegistered(FName SourceId);
	static TArray<FName> GetRegisteredSources();

private:
	static TMap<FName, FDesignSourceConvertFunc>& GetRegistry();
};
