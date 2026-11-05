// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FigmaParserInternal.h"
#include "Dom/JsonObject.h"

namespace FigmaParserInternal
{
	struct FNodeBounds;
	struct FRelativeTransform
	{
		double A = 0.0, B = 0.0, C = 0.0, D = 0.0, X = 0.0, Y = 0.0;
	};
	struct FLocalGeometry
	{
		FRelativeTransform Transform;
		FVector2D Size = FVector2D::ZeroVector;
	};

	bool HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj);
	bool TryGetRelativeTransform(const TSharedPtr<FJsonObject>& NodeObj, FRelativeTransform& OutTransform);
	bool TryGetLocalGeometry(const TSharedPtr<FJsonObject>& NodeObj, FLocalGeometry& OutGeometry);
	void ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode);
	void ParseSize(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout, const FLocalGeometry* LocalGeometry);
	void ParsePosition(
		const TSharedPtr<FJsonObject>& NodeObj,
		const TSharedPtr<FJsonObject>& ParentObj,
		const FNodeBounds* ParentCoordinateBounds,
		FDesignNode& OutNode,
		bool bParentHasAutoLayout,
		bool bParentTransformedCoordinates,
		const FLocalGeometry* LocalGeometry);
	void FinalizeNodePaddingRuntimeWrap(FDesignNode& Node);
}
