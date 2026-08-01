// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FigmaParserInternal.h"
#include "Dom/JsonObject.h"

namespace FigmaParserInternal
{
	struct FNodeBounds;

	bool HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj);
	void ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode);
	void ParseSize(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout);
	void ParsePosition(
		const TSharedPtr<FJsonObject>& NodeObj,
		const TSharedPtr<FJsonObject>& ParentObj,
		const FNodeBounds* ParentCoordinateBounds,
		FDesignNode& OutNode,
		bool bParentHasAutoLayout);
	void FinalizeNodePaddingRuntimeWrap(FDesignNode& Node, bool bInsideInstanceSubtree);
}
