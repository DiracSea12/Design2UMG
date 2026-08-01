// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace DesignTreeLimits
{
	constexpr int32 MaxNodeDepth = 512;
	constexpr int32 MaxParsedNodeCount = 100000;
}

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
	int32 GetNodeCount() const { return NodeCount; }

private:
	int32 NodeCount = 0;
	bool bExceeded = false;
};
