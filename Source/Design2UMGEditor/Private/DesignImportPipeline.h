#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"
#include "UMGSchema.h"

class UWidgetBlueprint;

enum class EDesignBlueprintGenerationMode : uint8
{
	BlueprintOnly,
	BlueprintAndPng,
	BlueprintAndCompiledPng
};

struct FBlueprintGenerationOptions
{
	EDesignBlueprintGenerationMode Mode = EDesignBlueprintGenerationMode::BlueprintAndPng;
	FString CompiledPngFilePath;
	FIntPoint CompiledRenderSize = FIntPoint::ZeroValue;
};

// Transitional import-core seams: analysis and generation are routed through
// explicit payloads before preview/orchestration is split in later tasks.
struct FImportAnalysisInput
{
	bool bResolveImages = true;
};

struct FImportAnalysisResult
{
	FUMGWidgetData Data;
	FString ImageReport;
};

struct FPreviewRefreshInput
{
	uint64 RequestId = 0;
	FString EditedJson;
	bool bResolveImages = true;
};

struct FPreviewApprovedSnapshot
{
	uint64 RequestId = 0;
	FString EditedJson;
	FUMGWidgetData Data;
	FString ImageReport;
};

struct FPreviewRefreshResult
{
	bool bSucceeded = false;
	uint64 RequestId = 0;
	FString EditedJson;
	FUMGWidgetData Data;
	FString ImageReport;
	FString ParseError;
};

struct FArtifactGenerationRequest
{
	uint64 SourceRequestId = 0;
	FString OutputPath;
	FBlueprintGenerationOptions Generation;
	bool bResolveImages = false;
	TOptional<FDesignDocument> DiagnosticDocument;
	FString RuntimeImageReport;
	FString DiagnosticImageReportOverride;
};

struct FArtifactGenerationResult
{
	bool bSucceeded = false;
	FString GeneratedAssetPath;
	FString RenderPngPath;
	FIntPoint RenderedSize = FIntPoint::ZeroValue;
};

struct FDesignImportRequest
{
	FString OutputPath;
	FText PreviewWindowTitle;
	bool bUsePreview = true;
	bool bResolveImages = true;
	FBlueprintGenerationOptions Generation;
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

namespace DesignImportPipeline
{
	FImportAnalysisResult AnalyzeImport(FUMGWidgetData& Data, const FImportAnalysisInput& Input = FImportAnalysisInput());
	FImportAnalysisResult AnalyzeImport(FDesignDocument& Document, const FImportAnalysisInput& Input = FImportAnalysisInput());
	FPreviewRefreshResult AnalyzePreviewRefresh(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges = nullptr);
	FArtifactGenerationResult GenerateArtifacts(FUMGWidgetData& Data, const FArtifactGenerationRequest& Request);
	FArtifactGenerationResult GenerateArtifacts(const FPreviewApprovedSnapshot& ApprovedSnapshot, const FArtifactGenerationRequest& Request);
	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request);
	FDesignImportExecutionResult Execute(FDesignDocument& Document, const FDesignImportRequest& Request);
	void ConvertDocument(FDesignDocument& Document, FUMGWidgetData& OutData);
	void WriteImportDiagnosticReport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& ImageReport = FString(),
		const FString& RequestedOutputPath = FString(),
		const FString& RequestedRenderPngPath = FString(),
		const FString& GeneratedAssetPath = FString(),
		const FString& RenderPngPath = FString());
	bool ParseWidgetJson(const FString& JsonString, FUMGWidgetData& OutData, FString* OutError = nullptr);
	void ResolveImages(FUMGWidgetData& Data, FString* OutReport = nullptr);
	void GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath);
	bool ImportPenJson(const FString& JsonString, FUMGWidgetData& OutData);
	bool LoadPenDocument(const FString& FilePath, const FString& TargetNodeId, FDesignDocument& OutDocument, FString* OutError = nullptr);
	FDesignImportExecutionResult ExecutePenFile(const FString& FilePath, const FString& TargetNodeId, const FDesignImportRequest& Request);
	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames);
}
