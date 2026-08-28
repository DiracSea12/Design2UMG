// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesign2UMGMappingDiagnostic;
struct FUMGWidgetData;

namespace DesignImportDiagnosticReport
{
	/** 导入/reimport 诊断报告的产物与路径字段（Write 的结构化载荷，替代 5 个位置字符串）。 */
	struct FImportReportArtifacts
	{
		/** 图像解析报告文本（多行，图像失败条目来源）；可空。 */
		FString ImageReport;
		/** 请求侧输出位置：导入=请求输出目录，reimport=目标资产包路径。 */
		FString RequestedOutputPath;
		FString RequestedRenderPngPath;
		FString GeneratedAssetPath;
		FString RenderPngPath;
	};

	void Write(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FImportReportArtifacts& Artifacts,
		const TArray<FDesign2UMGMappingDiagnostic>* ComponentMappingDiagnostics = nullptr,
		const TArray<FDesign2UMGMappingDiagnostic>* ReimportDiagnostics = nullptr);

	/**
	 * Safe Reimport 结束时的报告出口：与导入共用同一份报告格式与文件落点
	 * （ImportReports/*.import_report.json）。目标资产落在 requestedOutputPath，
	 * 逐节点诊断进顶层 reimport 章节（条目 kind/message，与 componentMapping 同构）。
	 * ComponentMappingDiagnostics（可选）随 componentMapping 章节落盘，与导入路径同一分类出口。
	 */
	void WriteReimport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& TargetAssetPath,
		const TArray<FDesign2UMGMappingDiagnostic>& ReimportDiagnostics,
		const TArray<FDesign2UMGMappingDiagnostic>* ComponentMappingDiagnostics = nullptr);
}
