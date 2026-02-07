// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutoEvaluator.generated.h"

/**
 * 优化开关配置，用于消融实验和A/B测试
 * 每个开关控制一个优化模块的启用/禁用
 */
USTRUCT(BlueprintType)
struct FOptimizationFlags
{
	GENERATED_BODY()

	// ========== LayoutCalculator 相关 ==========
	/** 是否启用 LayoutCalculator（Bounds→Slot计算） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutCalculator")
	bool bEnableLayoutCalculator = true;

	/** 是否启用 Hysteresis（锚点滞后算法，防止微小抖动导致锚点跳变） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutCalculator")
	bool bEnableHysteresis = true;

	/** 是否启用边缘吸附（靠近边缘时自动对齐到边缘锚点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutCalculator")
	bool bEnableEdgeSnap = true;

	/** 边缘吸附阈值（归一化坐标，默认5%） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutCalculator", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float EdgeSnapThreshold = 0.05f;

	/** 是否启用拉伸锚点（宽/高超过阈值时使用拉伸锚点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutCalculator")
	bool bEnableStretchAnchors = true;

	/** 拉伸锚点阈值（宽/高占比，默认60%） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutCalculator", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StretchThreshold = 0.60f;

	/** 是否启用9宫格锚点（根据位置选择9个标准锚点之一） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutCalculator")
	bool bEnableNineGrid = true;

	// ========== LayoutFixer 相关 ==========
	/** 是否启用 LayoutFixer（规则修复） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutFixer")
	bool bEnableFixer = true;

	/** 是否启用越界修正 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutFixer")
	bool bEnableOverflowFix = true;

	/** 是否启用对齐吸附（对齐到父容器边缘/中心） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutFixer")
	bool bEnableAlignSnap = true;

	/** 是否启用兄弟节点吸附（对齐到相邻元素） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutFixer")
	bool bEnableSiblingSnap = true;

	/** 是否启用等距检测（同行/同列元素等距分布） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutFixer")
	bool bEnableEqualSpacing = true;

	/** 是否启用置信度阈值（低置信度元素跳过修复） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LayoutFixer")
	bool bEnableConfidenceThreshold = true;

	// ========== AI/Prompt 相关 ==========
	/** 是否启用严格JSON Schema */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	bool bEnableSchemaStrict = true;

	/** 是否使用v2版本的Prompt */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	bool bUsePromptV2 = true;

	/** 是否启用ViewportMapping（不同分辨率/纵横比的坐标映射） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	bool bEnableViewportMapping = true;
};

struct FAutoEvalOptions
{
	FString ConfigName = TEXT("v2_full");
	int32 Iterations = 1;
	bool bUseCache = true;
	FString OutputPath;
	FIntPoint ViewportSourceSize = FIntPoint::ZeroValue;
	/** 是否保存渲染的PNG图片到本地（用于检查） */
	bool bSaveRenderedImages = false;
	/** PNG图片保存路径（为空则使用默认路径：Saved/ImageToUMG/RenderedImages） */
	FString RenderedImagesPath;
};

struct FAblationOptions
{
	FString BaselineConfig = TEXT("v2_full");
	bool bUseCache = true;
	int32 Iterations = 1;
	FString OutputPath;
	FIntPoint ViewportSourceSize = FIntPoint::ZeroValue;
	TArray<FString> Presets;
	/** 是否保存渲染的PNG图片到本地（用于检查） */
	bool bSaveRenderedImages = false;
	/** PNG图片保存路径（为空则使用默认路径：Saved/ImageToUMG/RenderedImages） */
	FString RenderedImagesPath;
};

struct FOptimizationReport;

class FAutoEvaluator
{
public:
	static bool RunAutoEvaluation(const FString& ContentPath, const FAutoEvalOptions& Options, FString* OutReportPath = nullptr, FOptimizationReport* OutReport = nullptr);
	static bool RunABTest(const FString& ContentPath, const FString& ConfigA, const FString& ConfigB, const FString& OutputPath, bool bUseCache = true, int32 Iterations = 1, const FIntPoint& ViewportSourceSize = FIntPoint::ZeroValue, FString* OutReportPath = nullptr);
	static bool RunAblationExperiment(const FString& ContentPath, const FAblationOptions& Options, FString* OutReportPath = nullptr);

	static bool BuildFlagsFromPreset(const FString& PresetName, FOptimizationFlags& OutFlags);
	static TArray<FString> GetAblationPresets();
};
