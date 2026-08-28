// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGComponentMappingConfig.h"
#include "DesignPreviewContracts.h"
#include "DesignImportTypes.h"
#include "UMGSchema.h"

class UWidgetBlueprint;
class UDesign2UMGSettings;

struct FDesign2UMGImportSettingsSnapshot
{
	FDesign2UMGConversionSettings ConversionSettings;
	FDesign2UMGGenerationSettingsSnapshot GenerationSettings;

	static FDesign2UMGImportSettingsSnapshot FromSettings(const UDesign2UMGSettings* Settings);
};

// Import-core seams: analysis, preview, and artifact generation exchange
// explicit request/result payloads so callers do not depend on UI state.
struct FImportAnalysisInput
{
	bool bResolveImages = true;
	const FDesign2UMGImportSettingsSnapshot* SettingsSnapshot = nullptr;
};

struct FImportAnalysisResult
{
	FUMGWidgetData Data;
	FString ImageReport;
	TOptional<FDesign2UMGImportSettingsSnapshot> SettingsSnapshot;
};

struct FArtifactGenerationResult
{
	bool bSucceeded = false;
	FString GeneratedAssetPath;
	FString RenderPngPath;
	FIntPoint RenderedSize = FIntPoint::ZeroValue;

	/** Component Mapping 诊断（ini 校验 + 命中/回退/inputs/Prototype 边界），进导入诊断报告。 */
	TArray<FDesign2UMGMappingDiagnostic> ComponentMappingDiagnostics;
};

struct FDesignImportRequest
{
	FString OutputPath;
	FText PreviewWindowTitle;
	bool bUsePreview = true;
	bool bResolveImages = true;
	FBlueprintGenerationOptions Generation;
	TOptional<FDesign2UMGImportSettingsSnapshot> SettingsSnapshot;
	TOptional<FDesignDocument> DiagnosticDocument;
	FString DiagnosticImageReport;
};

struct FDesignImportExecutionResult
{
	bool bSucceeded = false;
	bool bPreviewOpened = false;
	UWidgetBlueprint* GeneratedBlueprint = nullptr;
	FString GeneratedAssetPath;
	FString RenderPngPath;
	FIntPoint RenderedSize = FIntPoint::ZeroValue;
};

/** Reimport 请求：目标 WBP 资产 + 新源数据（新源经 Reimport 的 Document 参数传入）。 */
struct FDesignReimportRequest
{
	/** 目标页面宿主 WBP 的包路径（/Game/.../WBP_X）。 */
	FString TargetAssetPath;
	TOptional<FDesign2UMGImportSettingsSnapshot> SettingsSnapshot;
	/** 全量冲突时是否生成并列 WBP 副本；默认保留原资产 + 冲突诊断，不隐式生成副本。 */
	bool bGenerateResolutionCopy = false;
};

/** Reimport 结果：逐节点诊断进 Diagnostics（kind 前缀 reimport_*）。 */
struct FDesignReimportResult
{
	bool bSucceeded = false;
	/** 目标与生成记录关系断裂（manifest 缺失/scope 不一致/结构对不上），未做原地更新。 */
	bool bFullConflict = false;
	/** bGenerateResolutionCopy 生效时新生成的并列副本资产路径。 */
	FString ResolutionCopyPath;
	UWidgetBlueprint* UpdatedBlueprint = nullptr;
	TArray<FDesign2UMGMappingDiagnostic> Diagnostics;
	/** Component Mapping 诊断（ini 校验 + 命中/回退），进报告 componentMapping 章节（与导入路径同通道）。 */
	TArray<FDesign2UMGMappingDiagnostic> ComponentMappingDiagnostics;
};

namespace DesignImportPipeline
{
	FImportAnalysisResult AnalyzeImport(FUMGWidgetData& Data, const FImportAnalysisInput& Input = FImportAnalysisInput());
	FImportAnalysisResult AnalyzeImport(FDesignDocument& Document, const FImportAnalysisInput& Input = FImportAnalysisInput());
	FPreviewRefreshResult AnalyzePreviewRefresh(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges = nullptr);
	FArtifactGenerationResult GenerateArtifacts(FUMGWidgetData& Data, const FArtifactGenerationRequest& Request);
	FArtifactGenerationResult GenerateArtifacts(
		FUMGWidgetData& Data,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings);
	FArtifactGenerationResult GenerateArtifacts(const FPreviewApprovedSnapshot& ApprovedSnapshot, const FArtifactGenerationRequest& Request);
	FArtifactGenerationResult GenerateArtifacts(
		const FPreviewApprovedSnapshot& ApprovedSnapshot,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings);
	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request);
	FDesignImportExecutionResult Execute(FDesignDocument& Document, const FDesignImportRequest& Request);

	/**
	 * Safe Reimport：load manifest → 重新解析源 → DocumentScope 匹配 → Convert →
	 * 更新计划 → 编辑器事务内应用 → 编译保存 → manifest/诊断。
	 * 只原地更新 D2U-owned 内容；保存失败回滚事务且不更新 manifest；
	 * 关系断裂时按全量冲突处理（默认保留原资产）。
	 */
	FDesignReimportResult Reimport(FDesignDocument& Document, const FDesignReimportRequest& Request);
	void WriteImportDiagnosticReport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& ImageReport = FString(),
		const FString& RequestedOutputPath = FString(),
		const FString& RequestedRenderPngPath = FString(),
		const FString& GeneratedAssetPath = FString(),
		const FString& RenderPngPath = FString());
	bool LoadPenDocument(const FString& FilePath, const FString& TargetNodeId, FDesignDocument& OutDocument, FString* OutError = nullptr);
	FDesignImportExecutionResult ExecutePenFile(const FString& FilePath, const FString& TargetNodeId, const FDesignImportRequest& Request);
	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames);
}
