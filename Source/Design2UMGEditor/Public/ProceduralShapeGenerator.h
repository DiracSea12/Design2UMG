// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

class FUMGWidgetUtils;

/**
 * 程序化形状生成器 - 运行时创建简单几何形状纹理
 * 无需手动创建 Material 资产
 *
 * 生命周期说明: 所有 Generate* 函数返回临时纹理 (UTexture2D::CreateTransient)
 * - 纹理通过 AddToRoot() 防止 GC 回收（CreateBlueprint 深拷贝时 Transient Mips 会丢失）
 * - 必须在适当时机调用 ReleaseTransientTextures() 释放 Root 引用
 * - 模块卸载时自动调用 ReleaseTransientTextures()
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

	/** 色标结构（用于多色标渐变） */
	struct FGradientStop
	{
		float Position; // 0.0 ~ 1.0
		FLinearColor Color;
	};

	/** 生成多色标水平渐变纹理 */
	static UTexture2D* GenerateGradientHMultiStop(int32 Width, int32 Height, const TArray<FGradientStop>& Stops, UObject* Outer = nullptr);

	/** 生成多色标垂直渐变纹理 */
	static UTexture2D* GenerateGradientVMultiStop(int32 Width, int32 Height, const TArray<FGradientStop>& Stops, UObject* Outer = nullptr);

	/**
	 * 根据 MaterialRef 和 Properties 生成程序化纹理（统一入口）
	 * @param MaterialRef 形状名称（circle/ring/roundedrect/gradienth/gradientv/radialgradient）
	 * @param Properties Widget 属性表（读取 MaterialParams/WidthOverride/HeightOverride）
	 * @param Outer 纹理的 Outer 对象
	 * @return 生成的纹理，未知形状返回 nullptr
	 */
	static UTexture2D* GenerateFromMaterialRef(const FString& MaterialRef, const TMap<FString, FString>& Properties, UObject* Outer = nullptr);

	/** 释放所有通过 Generate* 创建的临时纹理的 Root 引用，允许 GC 回收 */
	static void ReleaseTransientTextures();

private:
	/** 创建临时纹理 */
	static UTexture2D* CreateTransientTexture(int32 Width, int32 Height, UObject* Outer);

	/** 填充纹理数据 */
	static void FillTextureData(UTexture2D* Texture, const TArray<FColor>& PixelData);

	/** 追踪所有 AddToRoot 的临时纹理 */
	static TArray<TWeakObjectPtr<UTexture2D>> RootedTextures;
};
