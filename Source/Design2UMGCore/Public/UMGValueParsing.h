// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Layout/Margin.h"
#include "UMGSchemaFoundation.h"

namespace FUMGValueParser
{
	DESIGN2UMGCORE_API FLinearColor ParseColor(const FString& ColorStr);
	DESIGN2UMGCORE_API EHorizontalAlignment ParseHAlign(const FString& Str);
	DESIGN2UMGCORE_API EVerticalAlignment ParseVAlign(const FString& Str);
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
	DESIGN2UMGCORE_API FVector2D ParseVector2D(const FString& Str, FVector2D Default = FVector2D::ZeroVector);
	DESIGN2UMGCORE_API bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B);
}
