// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PenSchema.h"

class UTexture2D;

/**
 * .pen 几何图形的运行时纹理生成器
 */
class FPenTextureGenerator
{
public:
	/** 生成椭圆纹理 */
	static UTexture2D* GenerateEllipseTexture(int32 Width, int32 Height, float InnerRadius, float StartAngle, float SweepAngle, const FLinearColor& Color);

	/** 生成线条纹理 */
	static UTexture2D* GenerateLineTexture(int32 Width, int32 Height, float Thickness, const FLinearColor& Color);

	/** 生成多边形纹理 */
	static UTexture2D* GeneratePolygonTexture(int32 Width, int32 Height, int32 PolygonCount, const FLinearColor& Color);

	/** 生成渐变纹理 */
	static UTexture2D* GenerateGradientTexture(int32 Width, int32 Height, const FPenGradient& Gradient);

private:
	/** 创建空白纹理 */
	static UTexture2D* CreateTexture(int32 Width, int32 Height);

	/** 填充像素数据 */
	static void FillPixels(UTexture2D* Texture, TFunction<FColor(int32, int32)> PixelFunc);
};
