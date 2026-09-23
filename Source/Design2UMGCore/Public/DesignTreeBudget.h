// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

// Node-count and depth limits for one parsed Figma, Pencil or UMG JSON document.
// Count every parsed node, including serialized UMG wrappers, across the whole tree.
// Conversion uses a separate recursion guard in DesignTreeLimits.h.

namespace DesignTreeLimits
{
	// Parsed design tree depth: bounds raw parse/convert/size-estimation recursion over one document tree.
	// Distinct from RefExpander's ref-override recursion cap, which bounds override application within a
	// single expansion and therefore uses a tighter independent limit.
	constexpr int32 MaxNodeDepth = 512;
	constexpr int32 MaxParsedNodeCount = 100000;
}

/**
 * Whole-document node and depth budget. On exhaustion, reject the complete parse
 * rather than returning a truncated tree.
 */
class FDesignParserBudget
{
public:
	bool TryEnterNode(const int32 Depth)
	{
		if (bExceeded || Depth > DesignTreeLimits::MaxNodeDepth || NodeCount >= DesignTreeLimits::MaxParsedNodeCount)
		{
			bExceeded = true;
			return false;
		}

		++NodeCount;
		return true;
	}

	bool WasExceeded() const { return bExceeded; }

private:
	int32 NodeCount = 0;
	bool bExceeded = false;
};
