#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"

namespace TextMeasurementRuntimeProbe
{
	struct FTextMeasureRequest
	{
		FString Text;
		float FontSize = 0.0f;
		FString FontAssetPath;
		FString TypefaceName;
		int32 LetterSpacing = 0;
	};

	struct FTextBlockMeasureRequest : FTextMeasureRequest
	{
		EDesignTextAlign TextAlign = EDesignTextAlign::Left;
		bool bApplyExplicitLineHeight = false;
		float LineHeightPercentage = 1.0f;
		bool bAutoWrapText = false;
		float WrapTextAt = 0.0f;
	};

	bool IsAvailable();
	bool TryMeasureText(const FTextMeasureRequest& Request, FVector2D& OutSize);
	bool TryMeasureTextBlockDesiredSize(const FTextBlockMeasureRequest& Request, FVector2D& OutSize);
}
