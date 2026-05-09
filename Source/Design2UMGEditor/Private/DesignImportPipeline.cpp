#include "DesignImportPipeline.h"
#include "DesignImportDiagnosticReport.h"
#include "DesignImportPlanning.h"

#include "Design2UMGLog.h"
#include "DesignImportDialogs.h"
#include "DesignImportUtils.h"
#include "PenMCPBridge.h"
#include "PenParser.h"
#include "FigmaParser.h"
#include "UMGSchema.h"

#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace DesignImportPipeline
{
	namespace
	{
		struct FArtifactGenerationRuntimeResult
		{
			UWidgetBlueprint* GeneratedBlueprint = nullptr;
		};

		bool ImportPenJsonInternal(const FString& JsonString, FUMGWidgetData& OutData);
		FOnPreviewConfirmed CreatePreviewConfirmedHandler(
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument,
			const FUMGIconFontMappingChanges& IconFontMappingChanges);
		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument);

		void MaybeWriteImportDiagnosticReport(
			const FArtifactGenerationRequest& Request,
			const FUMGWidgetData& Data,
			const FArtifactGenerationResult& Result)
		{
			if (!Request.DiagnosticDocument.IsSet() || (!Result.bSucceeded && Result.GeneratedAssetPath.IsEmpty()))
			{
				return;
			}

			DesignImportDiagnosticReport::Write(
				Request.DiagnosticDocument.GetValue(),
				Data,
				Request.DiagnosticImageReportOverride.IsEmpty() ? Request.RuntimeImageReport : Request.DiagnosticImageReportOverride,
				Request.OutputPath,
				Result.RenderPngPath.IsEmpty() ? Request.Generation.CompiledPngFilePath : Result.RenderPngPath,
				Result.GeneratedAssetPath,
				Result.RenderPngPath);
		}

		FArtifactGenerationResult GenerateArtifactsInternal(
			FUMGWidgetData& Data,
			const FArtifactGenerationRequest& Request,
			FArtifactGenerationRuntimeResult* OutRuntimeResult = nullptr)
		{
			FArtifactGenerationResult Result;
			switch (Request.Generation.Mode)
			{
			case EDesignBlueprintGenerationMode::BlueprintOnly:
			{
				UWidgetBlueprint* GeneratedBlueprint = DesignImportUtils::GenerateBlueprint(Data, Request.OutputPath);
				Result.bSucceeded = (GeneratedBlueprint != nullptr);
				if (GeneratedBlueprint)
				{
					Result.GeneratedAssetPath = GeneratedBlueprint->GetPathName();
				}
				if (OutRuntimeResult)
				{
					OutRuntimeResult->GeneratedBlueprint = GeneratedBlueprint;
				}
				break;
			}

			case EDesignBlueprintGenerationMode::BlueprintAndPng:
				Result.bSucceeded = DesignImportUtils::GenerateBlueprintAndPng(
					Data,
					Request.OutputPath,
					&Result.GeneratedAssetPath,
					&Result.RenderPngPath);
				break;

			case EDesignBlueprintGenerationMode::BlueprintAndCompiledPng:
				Result.bSucceeded = DesignImportUtils::GenerateBlueprintAndCompiledPng(
					Data,
					Request.OutputPath,
					Request.Generation.CompiledPngFilePath,
					&Result.GeneratedAssetPath,
					&Result.RenderedSize);
				Result.RenderPngPath = Request.Generation.CompiledPngFilePath;
				break;
			}

			MaybeWriteImportDiagnosticReport(Request, Data, Result);
			return Result;
		}



		bool ImportPenJsonInternal(const FString& JsonString, FUMGWidgetData& OutData)
		{
			FDesignDocument Document;
			if (!FPenParser::ParseDocument(JsonString, Document))
			{
				return false;
			}

			OutData = AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
			return true;
		}

		FOnPreviewConfirmed CreatePreviewConfirmedHandler(
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument,
			const FUMGIconFontMappingChanges& IconFontMappingChanges)
		{
			return FOnPreviewConfirmed::CreateLambda(
				[CapturedIconFontMappingChanges = IconFontMappingChanges,
				 CapturedGeneration = Request.Generation,
				 bCapturedResolveImages = Request.bResolveImages,
				 CapturedDiagnosticDocument = DiagnosticDocument](const FPreviewGenerationRequest& PreviewGenerationRequest)
				{
					FPreviewApprovedSnapshot ApprovedSnapshot = PreviewGenerationRequest.ApprovedSnapshot;
					ApprovedSnapshot.Data.IconFontMappingChanges = CapturedIconFontMappingChanges;

					FArtifactGenerationRequest GenerationRequest = PreviewGenerationRequest.GenerationRequest;
					GenerationRequest.Generation = CapturedGeneration;
					GenerationRequest.bResolveImages = bCapturedResolveImages;
					if (CapturedDiagnosticDocument.IsSet())
					{
						GenerationRequest.DiagnosticDocument = CapturedDiagnosticDocument;
					}

					GenerateArtifacts(ApprovedSnapshot, GenerationRequest);
				});
		}

		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument)
		{
			FDesignImportExecutionResult Result;
			if (Request.bUsePreview
				&& DesignImportDialogs::OpenPreviewWindow(
					Data,
					Request.OutputPath,
					Request.PreviewWindowTitle,
					CreatePreviewConfirmedHandler(Request, DiagnosticDocument, AnalysisResult.Data.IconFontMappingChanges),
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

			FArtifactGenerationRequest GenerationRequest;
			GenerationRequest.OutputPath = Request.OutputPath;
			GenerationRequest.Generation = Request.Generation;
			GenerationRequest.RuntimeImageReport = AnalysisResult.ImageReport;
			GenerationRequest.DiagnosticImageReportOverride = Request.DiagnosticImageReport;
			if (DiagnosticDocument.IsSet())
			{
				GenerationRequest.DiagnosticDocument = DiagnosticDocument;
			}
			else if (Request.DiagnosticDocument.IsSet())
			{
				GenerationRequest.DiagnosticDocument = Request.DiagnosticDocument;
			}

			FArtifactGenerationRuntimeResult RuntimeResult;
			const FArtifactGenerationResult GenerationResult = GenerateArtifactsInternal(Data, GenerationRequest, &RuntimeResult);
			Result.bSucceeded = GenerationResult.bSucceeded;
			Result.GeneratedBlueprint = RuntimeResult.GeneratedBlueprint;
			Result.GeneratedAssetPath = GenerationResult.GeneratedAssetPath;
			Result.RenderPngPath = GenerationResult.RenderPngPath;
			Result.RenderedSize = GenerationResult.RenderedSize;
			return Result;
		}
	}

	FImportAnalysisResult AnalyzeImport(FUMGWidgetData& Data, const FImportAnalysisInput& Input)
	{
		return DesignImportPlanning::AnalyzeWidgetData(Data, Input);
	}

	FImportAnalysisResult AnalyzeImport(FDesignDocument& Document, const FImportAnalysisInput& Input)
	{
		return DesignImportPlanning::AnalyzeDocument(Document, Input);
	}

	FPreviewRefreshResult AnalyzePreviewRefresh(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges)
	{
		return DesignImportPlanning::AnalyzePreviewRefreshInput(Input, MappingChanges);
	}

	FArtifactGenerationResult GenerateArtifacts(FUMGWidgetData& Data, const FArtifactGenerationRequest& Request)
	{
		return GenerateArtifactsInternal(Data, Request);
	}

	FArtifactGenerationResult GenerateArtifacts(const FPreviewApprovedSnapshot& ApprovedSnapshot, const FArtifactGenerationRequest& Request)
	{
		FArtifactGenerationRequest GenerationRequest = Request;
		if (GenerationRequest.SourceRequestId == 0)
		{
			GenerationRequest.SourceRequestId = ApprovedSnapshot.RequestId;
		}
		if (GenerationRequest.RuntimeImageReport.IsEmpty())
		{
			GenerationRequest.RuntimeImageReport = ApprovedSnapshot.ImageReport;
		}

		FUMGWidgetData GenerationData = ApprovedSnapshot.Data;
		if (GenerationRequest.bResolveImages)
		{
			FString ResolvedImageReport;
			DesignImportPlanning::ResolveImages(GenerationData, &ResolvedImageReport);
			GenerationRequest.RuntimeImageReport = ResolvedImageReport;
		}
		return GenerateArtifactsInternal(GenerationData, GenerationRequest);
	}

	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request)
	{
		const FImportAnalysisResult AnalysisResult = AnalyzeImport(Data, FImportAnalysisInput{Request.bResolveImages});
		return ExecuteWithAnalysis(Data, AnalysisResult, Request, Request.DiagnosticDocument);
	}

	FDesignImportExecutionResult Execute(FDesignDocument& Document, const FDesignImportRequest& Request)
	{
		const FImportAnalysisResult AnalysisResult = AnalyzeImport(Document, FImportAnalysisInput{Request.bResolveImages});
		FUMGWidgetData Data = AnalysisResult.Data;
		FDesignImportRequest FinalRequest = Request;
		FinalRequest.DiagnosticImageReport = AnalysisResult.ImageReport;
		return ExecuteWithAnalysis(Data, AnalysisResult, FinalRequest, TOptional<FDesignDocument>(Document));
	}

	void WriteImportDiagnosticReport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& ImageReport,
		const FString& RequestedOutputPath,
		const FString& RequestedRenderPngPath,
		const FString& GeneratedAssetPath,
		const FString& RenderPngPath)
	{
		DesignImportDiagnosticReport::Write(
			Document,
			Data,
			ImageReport,
			RequestedOutputPath,
			RequestedRenderPngPath,
			GeneratedAssetPath,
			RenderPngPath);
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
				OutDocument.Metadata.Add(TEXT("design.sourceFilePath"), FPaths::ConvertRelativePathToFull(FilePath));
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

		OutDocument.Metadata.Add(TEXT("design.sourceFilePath"), FPaths::ConvertRelativePathToFull(FilePath));
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
