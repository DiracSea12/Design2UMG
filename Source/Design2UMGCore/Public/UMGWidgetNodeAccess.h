// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Templates/Function.h"
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
	DESIGN2UMGCORE_API FString SerializeWidgetColor(const FLinearColor& Value);
	DESIGN2UMGCORE_API FString SerializeWidgetVector4(const FVector4& Value);
	DESIGN2UMGCORE_API FString SerializeImageTransform(const FUMGVisualBrushData::FImageTransformData& Value);
	DESIGN2UMGCORE_API bool TryParseImageTransform(const FString& Value, FUMGVisualBrushData::FImageTransformData& OutTransform);
	DESIGN2UMGCORE_API FString SerializeDrawAs(const ESlateBrushDrawType::Type DrawAs);
	DESIGN2UMGCORE_API bool TryParseDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs);
	DESIGN2UMGCORE_API FString GetGradientMaterialRef(const FUMGVisualBrushData::FGradientData& Gradient);
	DESIGN2UMGCORE_API FString SerializeGradientMaterialParams(const FUMGVisualBrushData::FGradientData& Gradient);
	DESIGN2UMGCORE_API bool NormalizeBrushTexturePathOwner(FUMGVisualBrushData& Brush, FString* OutDiagnostic = nullptr);

	DESIGN2UMGCORE_API void ForEachStaticTypedWidgetPropertyKey(TFunctionRef<void(const TCHAR*)> Visitor);
	template <typename VisitorType>
	void ForEachStaticTypedWidgetPropertyKey(VisitorType&& Visitor)
	{
		ForEachStaticTypedWidgetPropertyKey(TFunctionRef<void(const TCHAR*)>(Forward<VisitorType>(Visitor)));
	}

	DESIGN2UMGCORE_API bool IsTypedWidgetPropertyKey(const FString& Key);
	DESIGN2UMGCORE_API bool TryGetTextStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetTextTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetBrushTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetEffectTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetPathTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetShadowTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetRenderTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetStateTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetScrollBoxTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	DESIGN2UMGCORE_API bool TryGetTypedWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue);
	DESIGN2UMGCORE_API bool HasWidgetPropertyPresence(const FUMGWidgetNode& Node, const TCHAR* Key);
	DESIGN2UMGCORE_API bool TryGetWidgetBool(const FUMGWidgetNode& Node, const TCHAR* Key, bool& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetInt(const FUMGWidgetNode& Node, const TCHAR* Key, int32& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetFloat(const FUMGWidgetNode& Node, const TCHAR* Key, float& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetVector2D(const FUMGWidgetNode& Node, const TCHAR* Key, FVector2D& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetVector4(const FUMGWidgetNode& Node, const TCHAR* Key, FVector4& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetMargin(const FUMGWidgetNode& Node, const TCHAR* Key, FMargin& OutValue);
	DESIGN2UMGCORE_API bool TryGetWidgetColor(const FUMGWidgetNode& Node, const TCHAR* Key, FLinearColor& OutValue);
}
