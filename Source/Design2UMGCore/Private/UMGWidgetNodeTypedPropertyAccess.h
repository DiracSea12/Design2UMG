// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGWidgetNode;

namespace Design2UMGWidgetNodeTypedPropertyAccess
{
	void ForEachStaticTypedWidgetPropertyKey(TFunctionRef<void(const TCHAR*)> Visitor);
	bool TryGetTextStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetWidgetStyleTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetTextTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetBrushTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetEffectTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetPathTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetShadowTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetRenderTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetWidgetStateTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetScrollBoxTypedWidgetString(const FUMGWidgetNode& Node, const FString& KeyString, FString& OutValue);
	bool TryGetTypedWidgetString(const FUMGWidgetNode& Node, const TCHAR* Key, FString& OutValue);
	bool TryParseWidgetVector4String(const FString& Value, FVector4& OutValue);
}
