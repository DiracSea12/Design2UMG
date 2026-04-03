#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

class UWidgetBlueprint;
struct FDesignDocument;
struct FUMGWidgetData;

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
	FString* OutGeneratedAssetPath = nullptr;
	FIntPoint* OutRenderedSize = nullptr;
};

struct FDesignImportRequest
{
	FString OutputPath;
	FText PreviewWindowTitle;
	bool bUsePreview = true;
	bool bResolveImages = true;
	FBlueprintGenerationOptions Generation;
	const FDesignDocument* DiagnosticDocument = nullptr;
	FString DiagnosticImageReport;
};

struct FDesignImportExecutionResult
{
	bool bSucceeded = false;
	bool bPreviewOpened = false;
	UWidgetBlueprint* GeneratedBlueprint = nullptr;
	FString GeneratedAssetPath;
	FString RenderPngPath;
};

namespace DesignImportPipeline
{
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
