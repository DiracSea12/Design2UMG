// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

// 评估阈值常量（与文档 Phase1_OptimizationEvaluation.md 保持一致）
namespace EvalThresholds
{
	// MAE 归一化上限（用于 CompositeScore 计算）
	// MAEScore = max(0, 1 - MAE / MAE_Normalization_Max)
	// MAE = 0 → Score = 1.0, MAE >= 25 → Score = 0.0
	constexpr float MAE_Normalization_Max = 25.0f;

	// 样例级 PassRate 阈值（MAE ≤ 此值视为通过）
	constexpr float Sample_Pass_MAE = 4.0f;

	// 验收标准
	constexpr float Acceptance_MAE = 4.0f;      // PosMAE ≤ 4px
	constexpr float Acceptance_P95 = 8.0f;      // PosP95 ≤ 8px
	constexpr float Acceptance_PassRate = 0.95f; // PassRate ≥ 95%

	// 失败样例的默认 MAE（用于统计）
	// 设置为 >= MAE_Normalization_Max，确保失败样例的 MAEScore = 0，CompositeScore = 0
	constexpr float Failed_Sample_MAE = 25.0f;

	// 根节点边界检测阈值（归一化坐标）
	constexpr float RootBoundsEpsilon = 1.0f / 4096.0f;
}

struct FEvalNode
{
	FString Id;
	FString Type;
	FBounds Bounds;
};

struct FEvalMetrics
{
	float PosMAE = 0.0f;
	float PosP95 = 0.0f;

	int32 TruePositives = 0;
	int32 FalseNegatives = 0;
	int32 FalsePositives = 0;
	int32 TypeMismatches = 0;

	float Recall = 0.0f;
	float Precision = 0.0f;
	float CompositeScore = 0.0f;
};

struct FSampleMetrics
{
	FString SampleName;
	float PosMAE = 0.0f;
	float BoundsMaxP95 = 0.0f;
	int32 ElementCount = 0;
	int32 MissingCount = 0;
	int32 ExtraCount = 0;
	float Recall = 0.0f;
	float Precision = 0.0f;
	float CompositeScore = 0.0f;
};

struct FFailedSample
{
	FString SampleName;
	FString Reason;
};

struct FStageMetrics
{
	FString StageName;
	float PosMAE = 0.0f;
	float PosP95 = 0.0f;
	float PassRate = 0.0f;
	int32 OverflowCount = 0;
	float AlignmentError = 0.0f;
	float AlignmentErrorSum = 0.0f;      // 对齐误差累加和（用于全局平均计算）
	int32 AlignmentCount = 0;            // 对齐误差计数
	float SpacingVariance = 0.0f;
	float SpacingVarianceSum = 0.0f;     // 间距方差累加和（用于全局平均计算）
	int32 SpacingGroupCount = 0;         // 间距组计数
	int32 ElementCount = 0;
	int32 AnchorJumpCount = 0;
	int32 AnchorJumpTotal = 0;
	float AnchorJumpRate = 0.0f;
	bool bAnchorJumpRateValid = false;   // 当 LayoutCalculator 禁用时为 false

	// ========== LayoutCalculator 专用指标 (问题5) ==========
	float OffsetsChangeP95 = 0.0f;       // [预留] 微调时 Offsets 变化的 P95（像素），需要实现微调对比逻辑
	int32 EdgeSnapHitCount = 0;          // 边缘吸附触发次数（非准确率，仅统计触发频率）
	int32 EdgeSnapTotal = 0;             // 边缘吸附检测总次数
	float EdgeSnapHitRate = 0.0f;        // 边缘吸附触发率
	int32 StretchHitCount = 0;           // 拉伸适配触发次数（非准确率，仅统计触发频率）
	int32 StretchTotal = 0;              // 拉伸适配检测总次数
	float StretchHitRate = 0.0f;         // 拉伸适配触发率
	bool bLayoutCalcMetricsValid = false; // 当 LayoutCalculator 禁用时为 false

	// ========== Schema/Prompt 专用指标 (问题6) ==========
	int32 FirstParseSuccessCount = 0;    // 首次解析成功次数
	int32 TotalParseAttempts = 0;        // 总解析尝试次数（包括失败样例）
	float FirstParseSuccessRate = 0.0f;  // 首次解析成功率
	float AverageRetryCount = 0.0f;      // [预留] 平均重试次数，需要 Python 脚本支持
	int32 TotalTokenUsage = 0;           // [预留] Token 总消耗，需要 Python 脚本支持
	int32 PromptTokens = 0;              // [预留] Prompt Token 消耗
	int32 CompletionTokens = 0;          // [预留] Completion Token 消耗
	int32 LowConfidenceCount = 0;        // 低置信度元素计数（Confidence < 0.8）
	int32 TotalConfidenceCount = 0;      // 置信度检测总数
	float LowConfidenceRate = 0.0f;      // 低置信度命中率
	bool bSchemaMetricsValid = false;    // 当有解析尝试数据时为 true

	float AverageRecall = 0.0f;          // 样例级Recall平均值
	float AveragePrecision = 0.0f;       // 样例级Precision平均值
	float Recall = 0.0f;                 // 全局Recall（基于TP/FN/FP）
	float Precision = 0.0f;              // 全局Precision（基于TP/FP）
	float CompositeScore = 0.0f;
	int32 TruePositives = 0;
	int32 FalseNegatives = 0;
	int32 FalsePositives = 0;
	int32 TypeMismatches = 0;

	TArray<FSampleMetrics> SampleErrors;
	TArray<float> ElementPosErrors;
	TArray<float> ElementBoundsMaxErrors;
};

struct FOptimizationReport
{
	TArray<FStageMetrics> Stages;
	TArray<FFailedSample> FailedSamples;
	FString TestSetName;
	FString ConfigName;
	FDateTime Timestamp;

	const FStageMetrics* GetFinalStage() const;
	float GetFinalPosMAE() const;
	float GetFinalPosP95() const;

	float GetFixerContribution() const;
	float GetLayoutCalcContribution() const;
	float GetTotalContribution() const;
	float GetFixerContributionPercent() const;
	float GetLayoutCalcContributionPercent() const;
};

struct FRegressionThresholds
{
	float MAE_Mean_Regression = 0.5f;
	float MAE_Median_Regression = 0.3f;
	float P95_Regression = 1.0f;

	int32 MaxRegressedSamples = 3;
	float SampleRegressionThreshold = 2.0f;

	float MinRecall = 0.88f;
	float MinPrecision = 0.88f;
	float MinCompositeScore = 0.8f;

	static FRegressionThresholds ForTestSetSize(int32 SampleCount);
};

struct FRegressionCheckResult
{
	bool bPassed = true;
	FString FailReason;

	float MAE_Mean_Diff = 0.0f;
	float MAE_Median_Diff = 0.0f;
	float P95_Diff = 0.0f;
	float Recall_Diff = 0.0f;
	float Precision_Diff = 0.0f;
	float Composite_Diff = 0.0f;

	int32 RegressedSampleCount = 0;
	TArray<FString> RegressedSampleNames;
	TArray<float> RegressedSampleDeltas;
};

struct FMatchingConfig
{
	float IoUThreshold = 0.5f;
	bool bRequireTypeMatch = true;
	float TypeMismatchPenalty = 0.3f;

	float TinyAreaThreshold = 0.01f;
	float TinyIoUThreshold = 0.3f;

	// 中心距离门控（归一化坐标）
	// 在计算 IoU 前先过滤中心距离过远的元素对，提高匹配效率
	// 默认 0.05 = 5%，在 1920x1080 下约 96px
	// 与文档 Phase1_OptimizationEvaluation.md 第11.3.1节保持一致
	float CenterDistanceGate = 0.05f;
};

struct FMatchResult
{
	TMap<int32, int32> Matches;
	TSet<int32> TruePositives;
	TSet<int32> FalseNegatives;
	TSet<int32> FalsePositives;
	TSet<int32> TypeMismatches;
};

class FOptimizationMetrics
{
public:
	/**
	 * 检测节点是否是根容器节点（覆盖整个画布）
	 * 根容器节点通常是 CanvasPanel，Bounds 接近 (0,0,1,1)
	 * 在评估时应该跳过根容器，只评估其子节点
	 */
	static bool IsRootContainerNode(const FBounds& Bounds, bool bHasChildren);

	static void CollectNodes(const FUMGWidgetNode& Root, TArray<FEvalNode>& OutNodes, bool bIncludeRoot = false);

	static float ComputePosError(const FBounds& Actual, const FBounds& Expected, const FIntPoint& DesignSize);
	static float ComputeBoundsMaxError(const FBounds& Actual, const FBounds& Expected, const FIntPoint& DesignSize);
	static float ComputeIoU(const FBounds& A, const FBounds& B);
	static bool PassCenterDistanceGate(const FBounds& A, const FBounds& B, float Gate);
	static float GetAdaptiveIoUThreshold(const FBounds& Truth, const FMatchingConfig& Config);
	static float ComputeMatchScore(const FEvalNode& Truth, const FEvalNode& Gen, const FMatchingConfig& Config);

	static FMatchResult HungarianMatch(const TArray<FEvalNode>& TruthNodes, const TArray<FEvalNode>& GenNodes, const FMatchingConfig& Config);

	static FEvalMetrics CompareNodes(
		const TArray<FEvalNode>& TruthNodes,
		const TArray<FEvalNode>& GenNodes,
		const FMatchingConfig& Config,
		const FIntPoint& DesignSize,
		TArray<float>& OutPosErrors,
		TArray<float>& OutBoundsMaxErrors);

	static float ComputeCompositeScore(const FEvalMetrics& Metrics);
	static float ComputeQuantile(const TArray<float>& Values, float Quantile);
	static float GetMedian(const TArray<float>& Values);

	static bool SaveReportToJson(const FOptimizationReport& Report, const FString& OutputPath, FString* OutError = nullptr);

	static FRegressionCheckResult CheckForRegressionEnhanced(
		const FOptimizationReport& Current,
		const FOptimizationReport& Baseline,
		const FRegressionThresholds& Thresholds);
};
