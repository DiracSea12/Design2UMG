// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

// Genuinely-shared design-tree limits/budget live in the public header so the Editor
// module's UMG JSON parser can use the same budget and depth semantics as the Core
// parsers (Figma / Pen / SizeEstimator). This private header is retained so the
// existing Core/Private includers keep compiling unchanged, and it additionally holds
// the Core-only recursion guard -- the Editor has no use for converter /
// size-estimation recursion, so that type stays out of the public surface.
#include "DesignTreeBudget.h"
// Only the Core-private recursion guard below logs; the public budget header does not,
// so the log dependency stays on this side of the boundary.
#include "Design2UMGLog.h"

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
