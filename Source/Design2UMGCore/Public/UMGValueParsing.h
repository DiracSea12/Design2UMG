// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Layout/Margin.h"
#include "UMGSchemaFoundation.h"

namespace FUMGValueParser
{
	DESIGN2UMGCORE_API FLinearColor ParseColor(const FString& ColorStr);
	DESIGN2UMGCORE_API EUMGSlotHorizontalAlignment ParseSlotHorizontalAlignment(const FString& Str);
	DESIGN2UMGCORE_API EUMGSlotVerticalAlignment ParseSlotVerticalAlignment(const FString& Str);
	DESIGN2UMGCORE_API EUMGSlotSizeRule ParseSlotSizeRule(const FString& Str);
	DESIGN2UMGCORE_API const TCHAR* FormatSlotHorizontalAlignment(EUMGSlotHorizontalAlignment Value);
	DESIGN2UMGCORE_API const TCHAR* FormatSlotVerticalAlignment(EUMGSlotVerticalAlignment Value);
	DESIGN2UMGCORE_API const TCHAR* FormatSlotSizeRule(EUMGSlotSizeRule Value);
	DESIGN2UMGCORE_API EHorizontalAlignment ToSlateHorizontalAlignment(EUMGSlotHorizontalAlignment Value);
	DESIGN2UMGCORE_API EVerticalAlignment ToSlateVerticalAlignment(EUMGSlotVerticalAlignment Value);
	DESIGN2UMGCORE_API bool IsFillSizeRule(EUMGSlotSizeRule Value);
	DESIGN2UMGCORE_API ESlateVisibility ParseVisibility(const FString& Str);
	// Accept a complete, finite decimal number; leave OutValue unchanged on failure.
	DESIGN2UMGCORE_API bool TryParseFloat(const FString& Value, float& OutValue);
	// Two comma-separated finite components; leave OutValue unchanged on failure.
	DESIGN2UMGCORE_API bool TryParseVector2D(const FString& Str, FVector2D& OutValue);
	DESIGN2UMGCORE_API FVector2D ParseVector2D(const FString& Str, FVector2D Default = FVector2D::ZeroVector);
	DESIGN2UMGCORE_API bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B);
}
