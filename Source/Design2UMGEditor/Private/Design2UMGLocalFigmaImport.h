#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetData;

namespace Design2UMGLocalFigmaImport
{
	bool BuildLocalFigmaWidgetData(const FString& JsonFilePath, FUMGWidgetData& OutData, FString& OutError);
}
