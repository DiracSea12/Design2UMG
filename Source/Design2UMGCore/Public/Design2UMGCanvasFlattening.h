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
	 * Post-conversion tree simplification for absolute-layout documents:
	 *
	 * - A bare CanvasPanel directly inside another CanvasPanel is a pure coordinate
	 *   container (visuals/effects/buttons rebase the canvas under a Border/RetainerBox/
	 *   Button wrapper, so a plain CanvasPanel child carries no rendering semantics).
	 *   When both slots are point-anchored, the child's descendants translate linearly
	 *   into the parent and the intermediate canvas node is dropped.
	 * - A childless bare CanvasPanel renders nothing and is removed outright.
	 *
	 * Grandchildren keep their relative paint order: they are spliced at the merged
	 * canvas's index, so every node's sequence position - and with it z-order - is
	 * unchanged.
	 */
	DESIGN2UMGCORE_API void FlattenBareNestedCanvasPanels(FUMGWidgetNode& Root, FFlattenStats& OutStats);
}
