#include "DesignImportPipeline.h"

#include "Design2UMGLog.h"
#include "DesignImportUtils.h"
#include "PenImporter.h"
#include "PenParser.h"
#include "UMGSchema.h"

namespace DesignImportPipeline
{
	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request)
	{
		FDesignImportExecutionResult Result;

		if (Request.bResolveImages)
		{
			DesignImportUtils::ResolveImages(Data);
		}

		if (Request.bUsePreview
			&& DesignImportUtils::OpenPreviewWindow(Data, Request.OutputPath, Request.PreviewWindowTitle))
		{
			Result.bSucceeded = true;
			Result.bPreviewOpened = true;
			return Result;
		}

		switch (Request.Generation.Mode)
		{
		case EDesignBlueprintGenerationMode::BlueprintOnly:
			Result.GeneratedBlueprint = DesignImportUtils::GenerateBlueprint(Data, Request.OutputPath);
			Result.bSucceeded = (Result.GeneratedBlueprint != nullptr);
			break;

		case EDesignBlueprintGenerationMode::BlueprintAndPng:
			Result.bSucceeded = DesignImportUtils::GenerateBlueprintAndPng(Data, Request.OutputPath);
			break;

		case EDesignBlueprintGenerationMode::BlueprintAndCompiledPng:
			Result.bSucceeded = DesignImportUtils::GenerateBlueprintAndCompiledPng(
				Data,
				Request.OutputPath,
				Request.Generation.CompiledPngFilePath,
				Request.Generation.CompiledRenderSize,
				Request.Generation.OutGeneratedAssetPath,
				Request.Generation.OutRenderedSize);
			break;
		}

		return Result;
	}

	bool ImportPenFile(const FString& FilePath, const FString& TargetNodeId, FUMGWidgetData& OutData)
	{
		return TargetNodeId.IsEmpty()
			? FPenImporter::ImportFromFile(FilePath, OutData)
			: FPenImporter::ImportFileWithTargetNode(FilePath, TargetNodeId, OutData);
	}

	FDesignImportExecutionResult ExecutePenFile(
		const FString& FilePath,
		const FString& TargetNodeId,
		const FDesignImportRequest& Request)
	{
		FUMGWidgetData Data;
		if (!ImportPenFile(FilePath, TargetNodeId, Data))
		{
			return FDesignImportExecutionResult();
		}

		return Execute(Data, Request);
	}

	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames)
	{
		FDesignDocument Document;
		if (!FPenParser::ParseFromFile(FilePath, Document))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportPipeline: Failed to parse %s"), *FilePath);
			return false;
		}

		OutFrames = ExtractTopFrames(Document);
		return true;
	}
}
