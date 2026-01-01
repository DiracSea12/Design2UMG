// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportMapping.h"

FVector2D FViewportMapping::NormalizedToPixel(const FVector2D& NormCoord, const FIntPoint& DesignSize)
{
	return FVector2D(NormCoord.X * DesignSize.X, NormCoord.Y * DesignSize.Y);
}

FVector2D FViewportMapping::PixelToNormalized(const FVector2D& PixelCoord, const FIntPoint& DesignSize)
{
	if (DesignSize.X <= 0 || DesignSize.Y <= 0)
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(PixelCoord.X / DesignSize.X, PixelCoord.Y / DesignSize.Y);
}

FBounds FViewportMapping::MapNormalizedBounds(const FBounds& SourceBounds, const FViewportSpec& Spec)
{
	// Stretch模式：直接返回，归一化坐标不需要转换
	if (Spec.Mode == EAspectRatioMode::Stretch)
	{
		return SourceBounds;
	}

	float SourceAspect = (float)Spec.SourceSize.X / FMath::Max(1, Spec.SourceSize.Y);
	float DesignAspect = (float)Spec.DesignSize.X / FMath::Max(1, Spec.DesignSize.Y);

	FBounds Result = SourceBounds;

	if (Spec.Mode == EAspectRatioMode::Fit)
	{
		// Fit模式：保持比例，可能出现黑边
		if (SourceAspect > DesignAspect)
		{
			// 源图更宽，上下有黑边
			float Scale = DesignAspect / SourceAspect;
			float OffsetY = (1.0f - Scale) / 2.0f;
			Result.Y = (SourceBounds.Y - OffsetY) / Scale;
			Result.H = SourceBounds.H / Scale;
		}
		else
		{
			// 源图更高，左右有黑边
			float Scale = SourceAspect / DesignAspect;
			float OffsetX = (1.0f - Scale) / 2.0f;
			Result.X = (SourceBounds.X - OffsetX) / Scale;
			Result.W = SourceBounds.W / Scale;
		}
	}
	// Fill模式暂不实现，留待Phase 4

	return Result;
}
