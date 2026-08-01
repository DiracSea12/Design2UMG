// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace FigmaParserInternal
{
	struct FNodeBounds
	{
		float X = 0.0f;
		float Y = 0.0f;
		float W = 0.0f;
		float H = 0.0f;
	};

	bool HasVisibleStroke(const TSharedPtr<FJsonObject>& NodeObj);
	bool HasVisibleEffect(const TSharedPtr<FJsonObject>& NodeObj);
	bool TryReadBounds(const TSharedPtr<FJsonObject>& BoundsObj, FNodeBounds& OutBounds);
	bool HasParentClippedRenderBounds(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj);
	void CaptureBoundsDeltaMetadata(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions);
	bool TryGetVisualBounds(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FNodeBounds& OutBounds);
}
