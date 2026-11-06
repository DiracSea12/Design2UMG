// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchema.h"
#include "UMGSchemaSourceExtensions.h"

namespace Design2UMGPrototypeAnimationSource
{
	void Resolve(const FDesignDocument& Document, FUMGPrototypeNavigationData& Navigation, TArray<FString>& Warnings);
}
