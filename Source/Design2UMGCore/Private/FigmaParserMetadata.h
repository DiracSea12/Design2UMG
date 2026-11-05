// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
struct FDesignNode;

namespace FigmaParserMetadata
{
	void CaptureSharedMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions);
	void CaptureLayoutMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions);
	FBox2D CalculateExportBounds(const FDesignNode& Root, const FVector2D& SourceOrigin);
	void ApplyFigmaSemanticHints(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& Node);
}
