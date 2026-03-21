#pragma once

#include "DesignSchema.h"
#include "Fonts/SlateFontInfo.h"

namespace TextMeasurement
{
	void ResetCache();
	bool IsWrappingTextMode(const TSharedPtr<FDesignNode>& Node);
	bool ShouldApplyExplicitLineHeight(const TSharedPtr<FDesignNode>& Node, float ProbeFontSize, float ParentContentWidth = 0.0f);
	float ResolveTextFontSizeForUMG(const TSharedPtr<FDesignNode>& Node);
	float ResolveTextWrapAt(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
	bool ShouldAutoWrapText(const TSharedPtr<FDesignNode>& Node);
	FSlateFontInfo BuildSlateFontInfo(const TSharedPtr<FDesignNode>& Node);
	FVector2D MeasureTextNodeSize(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth);
}
