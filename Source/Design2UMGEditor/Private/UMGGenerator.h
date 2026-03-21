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

