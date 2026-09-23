// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetNode;

namespace Design2UMGCanvasFlattening
{
	struct FFlattenStats
	{
		int32 MergedNestedCanvasCount = 0;
		int32 RemovedEmptyCanvasCount = 0;
		int32 RemovedRedundantSizeCarrierCount = 0;
	};

	/**
	 * Flatten bare, point-anchored CanvasPanel children into their parent by translating slots.
	 * Remove empty bare canvases. Splice descendants at the removed node's position to retain
	 * paint order; visual/effect wrappers remain intact.
	 */
	DESIGN2UMGCORE_API void FlattenBareNestedCanvasPanels(FUMGWidgetNode& Root, FFlattenStats& OutStats);
}
