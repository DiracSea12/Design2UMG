// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Layout/Margin.h"

namespace FUMGValueParser
{
	DESIGN2UMGCORE_API FLinearColor ParseColor(const FString& ColorStr);
	DESIGN2UMGCORE_API EHorizontalAlignment ParseHAlign(const FString& Str);
	DESIGN2UMGCORE_API EVerticalAlignment ParseVAlign(const FString& Str);
	DESIGN2UMGCORE_API ESlateVisibility ParseVisibility(const FString& Str);
	DESIGN2UMGCORE_API FVector2D ParseVector2D(const FString& Str, FVector2D Default = FVector2D::ZeroVector);
	DESIGN2UMGCORE_API FMargin ParseMargin(const FString& Str);
	DESIGN2UMGCORE_API bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B);
}
