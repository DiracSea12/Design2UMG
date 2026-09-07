// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGSchemaFoundation.h"
#include "UMGSchemaTypedPayloads.h"

struct FUMGWidgetNode;

namespace Design2UMGWidgetNodeAccess
{
	// Cross-module typed-node read contract. Core owns typed payloads and
	// serializers; Editor final consumers use this facade to read the current
	// typed data shape without reaching into Core private implementation.
	DESIGN2UMGCORE_API FString SerializeWidgetFloat(const float Value);
	DESIGN2UMGCORE_API FString SerializeWidgetBool(const bool Value);
	DESIGN2UMGCORE_API FString SerializeWidgetVector2D(const FVector2D& Value);
	DESIGN2UMGCORE_API FString SerializeWidgetCompactVector2D(const FVector2D& Value);
	DESIGN2UMGCORE_API FString SerializeWidgetMargin(const FMargin& Value);
	// Precision variant for call sites whose historical wire format used a
	// precision other than the canonical serializer above.
	DESIGN2UMGCORE_API FString SerializeWidgetMargin(const FMargin& Value, int32 Precision);
	DESIGN2UMGCORE_API FString SerializeWidgetColor(const FLinearColor& Value);
	DESIGN2UMGCORE_API FString SerializeWidgetVector4(const FVector4& Value);
	// Precision variant for call sites whose historical wire format used a
	// precision other than the canonical serializer above.
	DESIGN2UMGCORE_API FString SerializeWidgetVector4(const FVector4& Value, int32 Precision);
	DESIGN2UMGCORE_API FString SerializeImageTransform(const FUMGVisualBrushData::FImageTransformData& Value);
	DESIGN2UMGCORE_API bool TryParseImageTransform(const FString& Value, FUMGVisualBrushData::FImageTransformData& OutTransform);
	DESIGN2UMGCORE_API FString SerializeDrawAs(const ESlateBrushDrawType::Type DrawAs);
	DESIGN2UMGCORE_API bool TryParseDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs);
	DESIGN2UMGCORE_API FString GetGradientMaterialRef(const FUMGVisualBrushData::FGradientData& Gradient);
	DESIGN2UMGCORE_API FString SerializeGradientMaterialParams(const FUMGVisualBrushData::FGradientData& Gradient);
	DESIGN2UMGCORE_API bool NormalizeBrushTexturePathOwner(FUMGVisualBrushData& Brush, FString* OutDiagnostic = nullptr);

	// Verbose-log presentation helper: renders the set typed payload fields of a
	// widget node as " Key=Value" fragments grouped by payload, in the shared
	// serializer formats. Pure log display; not a property lookup contract.
	DESIGN2UMGCORE_API FString BuildWidgetNodeVerbosePropertyDump(const FUMGWidgetNode& Node);
}
