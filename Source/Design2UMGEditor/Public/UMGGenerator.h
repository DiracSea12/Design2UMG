// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidgetBlueprint;

/**
 * Generates widget blueprints from normalized widget data.
 */
class FUMGGenerator
{
public:
	/**
	 * Generate, compile, and save a widget blueprint.
	 * @param WidgetData Parsed widget data
	 * @param PackagePath Target package path such as "/Game/UI"
	 * @return Generated widget blueprint, or nullptr on failure
	 */
	static UWidgetBlueprint* GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);

	/** Build and compile a widget blueprint without saving it to disk. */
	static UWidgetBlueprint* BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);

	/** Save a previously compiled widget blueprint to disk. */
	static void SaveWidgetBlueprint(UWidgetBlueprint* WBP);
};
