// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 差异项 - AI 分析产出的单条差异
 */
struct FVisualDiffItem
{
	FString Region;        // 语义位置描述，如 "top-left button"
	FString Type;          // offset / missing / color / size
	FString Description;   // 具体差异描述
	float Severity = 0.f;  // 0-1 严重程度
};

/**
 * 对比报告
 */
struct FVisualCompareReport
{
	float SSIM = 0.f;                       // 结构相似性 0-1
	float DiffPixelRatio = 0.f;             // 差异像素占比 0-1
	TArray<FVisualDiffItem> Diffs;          // AI 差异项列表
	FString AIAnalysis;                     // AI 分析原文
	bool bPassThreshold = false;            // 是否通过阈值
};

/**
 * 截图对比器
 *
 * 第一层：SSIM + 像素统计（同步，毫秒级）
 * 第二层：AI 多模态分析（异步，秒级，内部先调第一层）
 */
class FVisualComparator
{
public:
	/** 脚本级对比：SSIM + 差异像素占比 */
	static FVisualCompareReport CompareImages(
		const TArray<uint8>& DesignPng,
		const TArray<uint8>& RenderedPng,
		float PassThreshold = 0.85f
	);

	/** AI 增强对比（内部先调 CompareImages，再追加 AI 分析）
	 *  注意：OnComplete 在 HTTP 回调线程触发，如果捕获了 UObject 指针，
	 *  调用者应使用 TWeakObjectPtr 或确保对象生命周期覆盖请求周期。
	 */
	static void CompareWithAI(
		const TArray<uint8>& DesignPng,
		const TArray<uint8>& RenderedPng,
		float PassThreshold,
		TFunction<void(const FVisualCompareReport&)> OnComplete
	);

private:
	/** 解码 PNG 到 BGRA 像素数组 */
	static bool DecodePng(const TArray<uint8>& PngData, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);

	/** 计算 SSIM（8x8 窗口简化版） */
	static float ComputeSSIM(const TArray<uint8>& GrayA, const TArray<uint8>& GrayB, int32 Width, int32 Height);

	/** BGRA → 灰度 */
	static void ToGrayscale(const TArray<FColor>& Pixels, TArray<uint8>& OutGray);

	/** 计算差异像素占比 */
	static float ComputeDiffRatio(const TArray<uint8>& GrayA, const TArray<uint8>& GrayB, uint8 Threshold = 25);

	/** 拼接两张图为左右对比图 PNG */
	static bool StitchSideBySide(const TArray<uint8>& PngA, const TArray<uint8>& PngB, TArray<uint8>& OutPng, int32& OutWidth, int32& OutHeight);

	/** 解析 AI 返回的差异 JSON */
	static void ParseAIDiffs(const FString& AIResponse, TArray<FVisualDiffItem>& OutDiffs);
};
