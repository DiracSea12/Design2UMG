#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

class FDesign2UMGConverter
{
public:
	static FUMGWidgetData Convert(const FDesignDocument& Document);
};

