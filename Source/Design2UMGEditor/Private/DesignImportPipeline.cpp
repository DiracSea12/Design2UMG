#include "DesignImportPipeline.h"

#include "Design2UMGConverter.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignImportDialogs.h"
#include "DesignImportUtils.h"
#include "DesignRemoteImageImporter.h"
#include "FontResolver.h"
#include "PenMCPBridge.h"
#include "PenParser.h"
#include "UMGJsonParser.h"
#include "UMGSchema.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonSerializer.h"

namespace DesignImportPipeline
{
	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request)
	{
		FDesignImportExecutionResult Result;

		if (Request.bResolveImages)
		{
			ResolveImages(Data);
		}

		if (Request.bUsePreview
			&& DesignImportDialogs::OpenPreviewWindow(
				Data,
				Request.OutputPath,
				Request.PreviewWindowTitle,
				FOnPreviewConfirmed::CreateStatic(&DesignImportPipeline::GenerateFromEditedJson),
				FOnPreviewCancelled::CreateLambda(
					[]()
					{
						UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportPipeline: Preview cancelled"));
					})))
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

	FDesignImportExecutionResult Execute(FDesignDocument& Document, const FDesignImportRequest& Request)
	{
		FUMGWidgetData Data;
		ConvertDocument(Document, Data);
		return Execute(Data, Request);
	}

	void ConvertDocument(FDesignDocument& Document, FUMGWidgetData& OutData)
	{
		FFontResolver::AutoImportFonts(Document);
		FFontResolver::FindMissingFonts(Document);
		OutData = FDesign2UMGConverter::Convert(Document);
	}

	bool ParseWidgetJson(const FString& JsonString, FUMGWidgetData& OutData, FString* OutError)
	{
		FString ParseError;
		if (FUMGJsonParser::ParseJson(JsonString, OutData, &ParseError))
		{
			return true;
		}

		if (OutError)
		{
			*OutError = MoveTemp(ParseError);
		}
		return false;
	}

	void ResolveImages(FUMGWidgetData& Data)
	{
		FString Report;
		const bool bOk = FDesignRemoteImageImporter::ResolveRemoteImages(Data, &Report);
		if (!Report.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("%s"), *Report);
		}
		if (!bOk)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportPipeline: Unresolved remote images; generation will continue"));
			FDesign2UMGNotification::Warning(NSLOCTEXT("DesignImportPipeline", "ImageWarning", "Design2UMG: Some images failed to download"));
		}
	}

	void GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath)
	{
		FScopedSlowTask SlowTask(3.0f, NSLOCTEXT("DesignImportPipeline", "Generating", "Generating Widget Blueprint..."));
		SlowTask.MakeDialog(true);

		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("DesignImportPipeline", "GenParse", "Parsing edited design..."));
		FUMGWidgetData Data;
		FString ParseError;
		if (!ParseWidgetJson(EditedJson, Data, &ParseError))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportPipeline: Edited JSON parse failed: %s"), *ParseError);
			FDesign2UMGNotification::Error(NSLOCTEXT("DesignImportPipeline", "EditedJsonParseFailed", "Design2UMG: Failed to parse edited design data"));
			return;
		}
		if (SlowTask.ShouldCancel())
		{
			return;
		}

		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("DesignImportPipeline", "GenImages", "Downloading remote images..."));
		ResolveImages(Data);
		if (SlowTask.ShouldCancel())
		{
			return;
		}

		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("DesignImportPipeline", "GenBP", "Generating Widget Blueprint..."));
		DesignImportUtils::GenerateBlueprint(Data, OutputPath);
	}

	bool ImportPenJson(const FString& JsonString, FUMGWidgetData& OutData)
	{
		FDesignDocument Document;
		if (!FPenParser::ParseDocument(JsonString, Document))
		{
			return false;
		}

		ConvertDocument(Document, OutData);
		return true;
	}

	bool LoadPenDocument(
		const FString& FilePath,
		const FString& TargetNodeId,
		FDesignDocument& OutDocument,
		FString* OutError)
	{
		auto Fail = [OutError](const FString& Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
			return false;
		};

		if (TargetNodeId.IsEmpty())
		{
			if (FPenParser::ParseFromFile(FilePath, OutDocument))
			{
				return true;
			}

			return Fail(FString::Printf(TEXT("Failed to parse pen source: %s"), *FilePath));
		}

		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			return Fail(FString::Printf(TEXT("Failed to read pen source: %s"), *FilePath));
		}

		TSharedPtr<FJsonObject> RootObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return Fail(FString::Printf(TEXT("Failed to parse pen source JSON: %s"), *FilePath));
		}

		TSharedPtr<FJsonObject> FilteredDocumentObject;
		if (!FPenMCPBridge::BuildFilteredDocumentObject(RootObject, FilteredDocumentObject, TargetNodeId))
		{
			return Fail(FString::Printf(TEXT("Failed to filter pen source for target node '%s'"), *TargetNodeId));
		}

		if (!FPenParser::ParseDocumentObject(FilteredDocumentObject, OutDocument))
		{
			return Fail(FString::Printf(TEXT("Failed to parse filtered pen source: %s"), *FilePath));
		}

		return true;
	}

	FDesignImportExecutionResult ExecutePenFile(
		const FString& FilePath,
		const FString& TargetNodeId,
		const FDesignImportRequest& Request)
	{
		FDesignDocument Document;
		if (!LoadPenDocument(FilePath, TargetNodeId, Document, nullptr))
		{
			return FDesignImportExecutionResult();
		}

		return Execute(Document, Request);
	}

	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames)
	{
		FDesignDocument Document;
		if (!LoadPenDocument(FilePath, FString(), Document, nullptr))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportPipeline: Failed to parse %s"), *FilePath);
			return false;
		}

		OutFrames = ExtractTopFrames(Document);
		return true;
	}
}
