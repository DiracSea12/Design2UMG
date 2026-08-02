// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FigmaParserInternal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace FigmaParserInternal
{
	FString ExtractSyntheticVectorRasterImageRef(const TArray<TSharedPtr<FJsonValue>>& Fills);
	TArray<FDesignFill> ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills);
	FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& NodeObj);
	FDesignCornerRadius ParseCornerRadius(const TSharedPtr<FJsonObject>& NodeObj);
	TArray<FDesignEffect> ParseEffects(const TArray<TSharedPtr<FJsonValue>>& Effects);
}
