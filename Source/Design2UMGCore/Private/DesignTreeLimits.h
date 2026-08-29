// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGLog.h"

namespace DesignTreeLimits
{
	// Parsed design tree depth: bounds raw parse/convert/size-estimation recursion over one document tree.
	// Distinct from RefExpander's ref-override recursion cap, which bounds override application within a
	// single expansion and therefore uses a tighter independent limit.
	constexpr int32 MaxNodeDepth = 512;
	constexpr int32 MaxParsedNodeCount = 100000;
}

/** Shared depth guard for parse/convert/size-estimation recursion over design trees (DesignTreeLimits::MaxNodeDepth). */
struct FDesignRecursionGuard
{
	explicit FDesignRecursionGuard(int32& InDepth, const TCHAR* InName = TEXT(""))
		: Depth(InDepth)
		, bValid(InDepth < DesignTreeLimits::MaxNodeDepth)
	{
		if (bValid)
		{
			++Depth;
		}
		else
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("%s: recursion depth exceeded (%d)"), InName, DesignTreeLimits::MaxNodeDepth);
			UE_LOG(LogDesign2UMG, Error, TEXT("Design tree nesting is too deep (over %d levels). A cyclic reference may exist."), DesignTreeLimits::MaxNodeDepth);
		}
	}

	~FDesignRecursionGuard()
	{
		if (bValid)
		{
			--Depth;
		}
	}

	int32& Depth;
	bool bValid = false;
};

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
