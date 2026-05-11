// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidgetBlueprint;

class FUMGGenerator
{
public:
	static UWidgetBlueprint* GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);
	static UWidgetBlueprint* BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath);
	static void SaveWidgetBlueprint(UWidgetBlueprint* WBP);
};

