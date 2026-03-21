#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

class DESIGN2UMGCORE_API FDesign2UMGConverter
{
public:
	static FUMGWidgetData Convert(const FDesignDocument& Document);
};

