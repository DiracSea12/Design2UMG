#pragma once

#include "DesignSchema.h"
#include "UMGSchemaSourceExtensions.h"

namespace Design2UMGPrototypeAnimationSource
{
	void Resolve(const FDesignDocument& Document, FUMGPrototypeNavigationData& Navigation, TArray<FString>& Warnings);
}
