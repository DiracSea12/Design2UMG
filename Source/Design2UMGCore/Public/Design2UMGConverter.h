#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "UMGSchema.h"

/**
 * Converts shared design IR into normalized UMG generation IR.
 */
class DESIGN2UMGCORE_API FDesign2UMGConverter
{
public:
	/**
	 * Convert a parsed design document into typed widget data ready for Blueprint generation.
	 * @param Document Parsed design-side IR.
	 * @return Generated UMG widget tree and associated metadata.
	 */
	static FUMGWidgetData Convert(const FDesignDocument& Document);
};

