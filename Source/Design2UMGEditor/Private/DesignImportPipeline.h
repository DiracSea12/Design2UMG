#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

class UWidgetBlueprint;
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
};

struct FDesignImportExecutionResult
{
	bool bSucceeded = false;
	bool bPreviewOpened = false;
	UWidgetBlueprint* GeneratedBlueprint = nullptr;
};

namespace DesignImportPipeline
{
	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request);
	FDesignImportExecutionResult ExecutePenFile(const FString& FilePath, const FString& TargetNodeId, const FDesignImportRequest& Request);
	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames);
}
