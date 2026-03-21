#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "Layout/Margin.h"

namespace FUMGValueParser
{
	FLinearColor ParseColor(const FString& ColorStr);
	EHorizontalAlignment ParseHAlign(const FString& Str);
	EVerticalAlignment ParseVAlign(const FString& Str);
	ESlateVisibility ParseVisibility(const FString& Str);
	FVector2D ParseVector2D(const FString& Str, FVector2D Default = FVector2D::ZeroVector);
	FMargin ParseMargin(const FString& Str);
	bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B);
}
