// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

/**
 * 程序化形状生成器 - 运行时创建简单几何形状纹理
 * 无需手动创建 Material 资产
 *
 * 生命周期说明: 所有 Generate* 函数返回临时纹理 (UTexture2D::CreateTransient)
 * - 无引用时，纹理会被垃圾回收
 * - 调用方必须持有引用 (如 TStrongObjectPtr<UTexture2D>) 以保持纹理存活
 * - 无需显式清理 - Unreal GC 自动处理销毁
 * - 预览窗口中，存储到 TArray<TStrongObjectPtr<UTexture2D>> PreviewTextures
 */
class FProceduralShapeGenerator
{
public:
	/** 生成纯色圆形纹理 */
	static UTexture2D* GenerateCircle(int32 Size, FLinearColor Color, UObject* Outer = nullptr);

	/** 生成圆环纹理 */
	static UTexture2D* GenerateRing(int32 Size, FLinearColor Color, float Thickness = 0.1f, UObject* Outer = nullptr);

	/** 生成圆角矩形纹理 */
	static UTexture2D* GenerateRoundedRect(int32 Width, int32 Height, FLinearColor Color, float CornerRadius = 0.2f, UObject* Outer = nullptr);

	/** 生成水平渐变纹理 */
	static UTexture2D* GenerateGradientH(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer = nullptr);

	/** 生成垂直渐变纹理 */
	static UTexture2D* GenerateGradientV(int32 Width, int32 Height, FLinearColor ColorStart, FLinearColor ColorEnd, UObject* Outer = nullptr);

	/** 生成径向渐变纹理 */
	static UTexture2D* GenerateRadialGradient(int32 Size, FLinearColor ColorCenter, FLinearColor ColorEdge, UObject* Outer = nullptr);

	/** 解析十六进制颜色 */
	static FLinearColor ParseHexColor(const FString& HexColor, FLinearColor Default = FLinearColor::White);

private:
	/** 创建临时纹理 */
	static UTexture2D* CreateTransientTexture(int32 Width, int32 Height, UObject* Outer);

	/** 填充纹理数据 */
	static void FillTextureData(UTexture2D* Texture, const TArray<FColor>& PixelData);
};
