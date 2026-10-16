// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

struct FDesignNode;

namespace DesignSourceExportBounds
{
	/** Source silhouette and visible effects, independent of render-target filter gutters. */
	FBox2D Calculate(const TSharedPtr<FDesignNode>& Node,
		TFunctionRef<TSharedPtr<FDesignNode>(const TSharedPtr<FDesignNode>&)> ResolveRef);
}
