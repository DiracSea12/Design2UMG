// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * 布局预设 - 可配置的阈值参数
 */
struct FLayoutPreset
{
	/** 9宫格左阈值（默认0.33） */
	float AnchorThresholdL = 0.33f;

	/** 9宫格右阈值（默认0.67） */
	float AnchorThresholdR = 0.67f;

	/** Hysteresis缓冲区（默认0.05） */
	float HysteresisBuffer = 0.05f;

	/** 边缘吸附阈值（默认0.05） */
	float EdgeSnapThreshold = 0.05f;

	/** 拉伸锚点阈值（默认0.60） */
	float StretchThreshold = 0.60f;

	/** Enable nine-grid anchor selection */
	bool bEnableNineGrid = true;
	bool bEnableHysteresis = true;
	bool bEnableEdgeSnap = true;
	bool bEnableStretchAnchors = true;

	/** 获取标准HUD预设 */
	static FLayoutPreset GetStandardHUDPreset()
	{
		return FLayoutPreset();
	}
};

/**
 * 布局上下文 - 计算时的环境信息
 */
struct FLayoutContext
{
	/** 设计分辨率（UMG目标） */
	FIntPoint DesignSize = FIntPoint(1920, 1080);

	/** 上一轮的锚点X（用于Hysteresis） */
	TOptional<float> OldAnchorX;

	/** 上一轮的锚点Y（用于Hysteresis） */
	TOptional<float> OldAnchorY;

	/** 布局预设 */
	FLayoutPreset Preset;
};

// FBounds已在UMGSchema.h中定义

/**
 * 布局计算器 - 将Bounds转换为UMG的Anchors/Offsets
 */
class FLayoutCalculator
{
public:
	/**
	 * 计算单个Widget的Slot数据
	 * @param Bounds 归一化边界框
	 * @param Context 布局上下文
	 * @return 计算得到的Slot数据
	 */
	static FUMGSlotData CalculateSlot(const FBounds& Bounds, const FLayoutContext& Context);

private:
	/** 单轴计算结果 */
	struct FAxisResult
	{
		float AnchorMin = 0.f;
		float AnchorMax = 0.f;
		float Alignment = 0.f;
		float OffsetStart = 0.f;  // Left or Top
		float OffsetEnd = 0.f;    // Right or Bottom
	};

	/** 单轴 Slot 计算（水平/垂直共用） */
	static FAxisResult CalculateAxis(float Pos, float Size, int32 CanvasSize, TOptional<float> OldAnchor, const FLayoutPreset& Preset);

	/**
	 * 9宫格锚点选择（带Hysteresis防跳变）
	 */
	static float SelectAnchorWithHysteresis(
		float Center,
		TOptional<float> OldAnchor,
		const FLayoutPreset& Preset
	);

	/**
	 * 边缘吸附检测
	 * @param Position 元素位置（0-1）
	 * @param Size 元素尺寸（0-1）
	 * @param EdgeThreshold 边缘阈值
	 * @param bSnapToStart 输出：是否吸附到起始边缘
	 * @param bSnapToEnd 输出：是否吸附到结束边缘
	 * @return 是否需要边缘吸附
	 */
	static bool ShouldSnapToEdge(
		float Position,
		float Size,
		float EdgeThreshold,
		bool& bSnapToStart,
		bool& bSnapToEnd
	);

	/**
	 * 拉伸锚点检测
	 * @param Size 元素尺寸（0-1）
	 * @param StretchThreshold 拉伸阈值
	 * @return 是否应使用拉伸锚点
	 */
	static bool ShouldUseStretchAnchors(float Size, float StretchThreshold);
};
