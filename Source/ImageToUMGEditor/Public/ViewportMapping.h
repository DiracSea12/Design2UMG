// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LayoutCalculator.h"

/**
 * 纵横比处理模式
 */
enum class EAspectRatioMode : uint8
{
	Fit,     // Letterbox模式，保持比例，出现黑边
	Fill,    // 裁切模式，填充屏幕，裁切超出部分
	Stretch  // 拉伸模式，可能变形（默认）
};

/**
 * 视口规格
 */
struct FViewportSpec
{
	/** 源图尺寸（AI输入图） */
	FIntPoint SourceSize = FIntPoint(1920, 1080);

	/** 设计尺寸（UMG目标） */
	FIntPoint DesignSize = FIntPoint(1920, 1080);

	/** 纵横比处理模式 */
	EAspectRatioMode Mode = EAspectRatioMode::Stretch;

	/** 安全区边距（可选） */
	FMargin SafeZone = FMargin(0.0f);
};

/**
 * 视口映射器 - 处理不同分辨率/纵横比的坐标转换
 */
class FViewportMapping
{
public:
	/**
	 * 将AI输出的归一化坐标映射为逻辑画布归一化坐标
	 * @param SourceBounds 源Bounds（0-1，相对于AI输入图）
	 * @param Spec 视口规格
	 * @return 映射后的Bounds（0-1，相对于设计尺寸）
	 */
	static FBounds MapNormalizedBounds(const FBounds& SourceBounds, const FViewportSpec& Spec);

	/**
	 * 将归一化坐标转换为像素坐标
	 */
	static FVector2D NormalizedToPixel(const FVector2D& NormCoord, const FIntPoint& DesignSize);

	/**
	 * 将像素坐标转换为归一化坐标
	 */
	static FVector2D PixelToNormalized(const FVector2D& PixelCoord, const FIntPoint& DesignSize);
};
