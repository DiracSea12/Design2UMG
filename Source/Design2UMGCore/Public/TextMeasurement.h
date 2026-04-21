#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

struct FDesignNode;

namespace TextMeasurement
{
	DESIGN2UMGCORE_API void ResetCache();
	DESIGN2UMGCORE_API bool IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API bool ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth = 0.0f);
	DESIGN2UMGCORE_API float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API float ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	DESIGN2UMGCORE_API bool ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API FSlateFontInfo BuildSlateFontInfo(const TSharedPtr<FDesignNode>& Node);
	DESIGN2UMGCORE_API FVector2D MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	DESIGN2UMGCORE_API FString PrepareRuntimeTextContent(const FString& Text);
}
