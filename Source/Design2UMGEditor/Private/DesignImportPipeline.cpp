// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPipeline.h"
#include "DesignImportDiagnosticReport.h"
#include "DesignImportPlanning.h"
#include "Automation/Design2UMGAutomationIrDump.h"

#include "Design2UMGLog.h"
#include "DesignImportDialogs.h"
#include "DesignImportUtils.h"
#include "DesignJsonUtils.h"
#include "DesignSchemaSourceExtensions.h"
#include "ExternalServices/PenMCPBridge.h"
#include "ExternalServices/PencilExtensionResources.h"
#include "PenParser.h"
#include "UMGSchema.h"
#include "Design2UMGSettings.h"

#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FDesign2UMGImportSettingsSnapshot FDesign2UMGImportSettingsSnapshot::FromSettings(const UDesign2UMGSettings* Settings)
{
	FDesign2UMGImportSettingsSnapshot Snapshot;
	Snapshot.ConversionSettings = FDesign2UMGConversionSettings::FromSettings(Settings);
	if (Settings)
	{
		Snapshot.GenerationSettings = FDesign2UMGGenerationSettingsSnapshot::FromSettings(*Settings);
	}
	return Snapshot;
}

namespace DesignImportPipeline
{
	namespace
	{
		struct FArtifactGenerationRuntimeResult
		{
			UWidgetBlueprint* GeneratedBlueprint = nullptr;
		};

		FOnPreviewConfirmed CreatePreviewConfirmedHandler(
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument,
			const FUMGIconFontMappingChanges& IconFontMappingChanges);
		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument);
		FDesign2UMGImportSettingsSnapshot BuildFallbackImportSettingsSnapshot()
		{
			return FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		}

		FDesignPreviewMaterializationSettingsSnapshot BuildPreviewMaterializationSettingsSnapshot(
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings)
		{
			return FDesignPreviewMaterializationSettingsSnapshot::FromGenerationSettings(GenerationSettings);
		}

		void MaybeWriteImportDiagnosticReport(
			const FArtifactGenerationRequest& Request,
			const FUMGWidgetData& Data,
			const FArtifactGenerationResult& Result)
		{
			if (!Request.DiagnosticDocument.IsSet() || (!Result.bSucceeded && Result.GeneratedAssetPath.IsEmpty()))
			{
				return;
			}

			DesignImportDiagnosticReport::FImportReportArtifacts Artifacts;
			Artifacts.ImageReport = Request.DiagnosticImageReportOverride.IsEmpty()
				? Request.RuntimeImageReport
				: Request.DiagnosticImageReportOverride;
			Artifacts.RequestedOutputPath = Request.OutputPath;
			Artifacts.RequestedRenderPngPath = Result.RenderPngPath.IsEmpty()
				? Request.Generation.CompiledPngFilePath
				: Result.RenderPngPath;
			Artifacts.GeneratedAssetPath = Result.GeneratedAssetPath;
			Artifacts.RenderPngPath = Result.RenderPngPath;
			DesignImportDiagnosticReport::Write(
				Request.DiagnosticDocument.GetValue(),
				Data,
				Artifacts,
				&Result.ComponentMappingDiagnostics);
		}

		FArtifactGenerationResult GenerateArtifactsInternal(
			FUMGWidgetData& Data,
			const FArtifactGenerationRequest& Request,
			const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings,
			FArtifactGenerationRuntimeResult* OutRuntimeResult = nullptr)
		{
			FArtifactGenerationResult Result;
			FDesign2UMGGenerationOutput GenerationOutput;
			Design2UMGAutomationIrDump::MaybeWriteIrDump(Data);
			switch (Request.Generation.Mode)
			{
			case EDesignBlueprintGenerationMode::BlueprintOnly:
			{
				UWidgetBlueprint* GeneratedBlueprint =
					DesignImportUtils::GenerateBlueprint(Data, Request.OutputPath, GenerationSettings, &GenerationOutput);
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
					&Result.RenderPngPath,
					GenerationSettings,
					&GenerationOutput);
				break;

			case EDesignBlueprintGenerationMode::BlueprintAndCompiledPng:
				Result.bSucceeded = DesignImportUtils::GenerateBlueprintAndCompiledPng(
					Data,
					Request.OutputPath,
					Request.Generation.CompiledPngFilePath,
					&Result.GeneratedAssetPath,
					&Result.RenderedSize,
					GenerationSettings,
					&GenerationOutput,
					&Result.ErrorMessage);
				if (Result.bSucceeded)
				{
					Result.RenderPngPath = Request.Generation.CompiledPngFilePath;
				}
				break;
			}

			Result.ComponentMappingDiagnostics = MoveTemp(GenerationOutput.MappingDiagnostics);
			if (!Result.bSucceeded
				&& Result.ComponentMappingDiagnostics.Num() == 0
				&& GenerationSettings)
			{
				// When the build fails before assembly output is produced, the ini validation diagnostics must still reach the report.
				Result.ComponentMappingDiagnostics.Append(GenerationSettings->ComponentMapping.ValidationDiagnostics);
			}

			MaybeWriteImportDiagnosticReport(Request, Data, Result);
			return Result;
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
				 CapturedDiagnosticDocument = DiagnosticDocument,
				 CapturedSettingsSnapshot = Request.SettingsSnapshot](const FPreviewGenerationRequest& PreviewGenerationRequest)
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

					if (CapturedSettingsSnapshot.IsSet())
					{
						GenerateArtifacts(ApprovedSnapshot, GenerationRequest, CapturedSettingsSnapshot->GenerationSettings);
					}
					else
					{
						GenerateArtifacts(ApprovedSnapshot, GenerationRequest);
					}
				});
		}

		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument)
		{
			FDesignImportExecutionResult Result;
			const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = nullptr;
			FDesign2UMGImportSettingsSnapshot FallbackSettingsSnapshot;
			if (AnalysisResult.SettingsSnapshot.IsSet())
			{
				GenerationSettings = &AnalysisResult.SettingsSnapshot->GenerationSettings;
			}
			else if (Request.SettingsSnapshot.IsSet())
			{
				GenerationSettings = &Request.SettingsSnapshot->GenerationSettings;
			}
			else
			{
				FallbackSettingsSnapshot = BuildFallbackImportSettingsSnapshot();
				GenerationSettings = &FallbackSettingsSnapshot.GenerationSettings;
			}

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
						}),
					GenerationSettings
						? TOptional<FDesignPreviewMaterializationSettingsSnapshot>(
							BuildPreviewMaterializationSettingsSnapshot(*GenerationSettings))
						: TOptional<FDesignPreviewMaterializationSettingsSnapshot>()))
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
			const FArtifactGenerationResult GenerationResult =
				GenerateArtifactsInternal(Data, GenerationRequest, GenerationSettings, &RuntimeResult);
			Result.bSucceeded = GenerationResult.bSucceeded;
			Result.ErrorMessage = GenerationResult.ErrorMessage;
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
		const FDesign2UMGImportSettingsSnapshot FallbackSettingsSnapshot = BuildFallbackImportSettingsSnapshot();
		return GenerateArtifactsInternal(Data, Request, &FallbackSettingsSnapshot.GenerationSettings);
	}

	FArtifactGenerationResult GenerateArtifacts(
		FUMGWidgetData& Data,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings)
	{
		return GenerateArtifactsInternal(Data, Request, &GenerationSettings);
	}

	FArtifactGenerationResult GenerateArtifacts(const FPreviewApprovedSnapshot& ApprovedSnapshot, const FArtifactGenerationRequest& Request)
	{
		const FDesign2UMGImportSettingsSnapshot FallbackSettingsSnapshot = BuildFallbackImportSettingsSnapshot();
		return GenerateArtifacts(ApprovedSnapshot, Request, FallbackSettingsSnapshot.GenerationSettings);
	}

	FArtifactGenerationResult GenerateArtifacts(
		const FPreviewApprovedSnapshot& ApprovedSnapshot,
		const FArtifactGenerationRequest& Request,
		const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings)
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
		return GenerateArtifactsInternal(GenerationData, GenerationRequest, &GenerationSettings);
	}

	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request)
	{
		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = Request.bResolveImages;
		AnalysisInput.SettingsSnapshot = Request.SettingsSnapshot.IsSet() ? &Request.SettingsSnapshot.GetValue() : nullptr;
		const FImportAnalysisResult AnalysisResult = AnalyzeImport(Data, AnalysisInput);
		return ExecuteWithAnalysis(Data, AnalysisResult, Request, Request.DiagnosticDocument);
	}

	FDesignImportExecutionResult Execute(FDesignDocument& Document, const FDesignImportRequest& Request, const FImportAnalysisResult* PrecomputedAnalysis)
	{
		FImportAnalysisResult AnalysisResult;
		if (PrecomputedAnalysis)
		{
			AnalysisResult = *PrecomputedAnalysis;
		}
		else
		{
			FImportAnalysisInput AnalysisInput;
			AnalysisInput.bResolveImages = Request.bResolveImages;
			AnalysisInput.SettingsSnapshot = Request.SettingsSnapshot.IsSet() ? &Request.SettingsSnapshot.GetValue() : nullptr;
			AnalysisResult = AnalyzeImport(Document, AnalysisInput);
		}
		FUMGWidgetData Data = AnalysisResult.Data;
		FDesignImportRequest FinalRequest = Request;
		if (!FinalRequest.SettingsSnapshot.IsSet() && AnalysisResult.SettingsSnapshot.IsSet())
		{
			FinalRequest.SettingsSnapshot = AnalysisResult.SettingsSnapshot;
		}
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
		DesignImportDiagnosticReport::FImportReportArtifacts Artifacts;
		Artifacts.ImageReport = ImageReport;
		Artifacts.RequestedOutputPath = RequestedOutputPath;
		Artifacts.RequestedRenderPngPath = RequestedRenderPngPath;
		Artifacts.GeneratedAssetPath = GeneratedAssetPath;
		Artifacts.RenderPngPath = RenderPngPath;
		DesignImportDiagnosticReport::Write(Document, Data, Artifacts);
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

		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			return Fail(FString::Printf(TEXT("Failed to read pen source: %s"), *FilePath));
		}

		// Extension resources feed the parse that follows: the icon index provides icon
		// codepoints, and the bundled library documents provide the variables tables that
		// document imports ("pencil:shadcn.lib.pen") expand against. Register both before
		// any parse/conversion consumes them.
		PencilExtensionResources::EnsureIconGlyphsRegistered();
		PencilExtensionResources::EnsureBundledLibrariesRegistered();

		if (TargetNodeId.IsEmpty())
		{
			if (!FPenParser::ParseDocument(JsonString, OutDocument))
			{
				return Fail(FString::Printf(TEXT("Failed to parse pen source: %s"), *FilePath));
			}
		}
		else
		{
			TSharedPtr<FJsonObject> RootObject;
			if (!DesignJsonUtils::TryParseObject(JsonString, RootObject))
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
			FPenMCPBridge::PromoteSelectedNodeToRenderRoot(OutDocument, TargetNodeId);
		}

		const FString ResolvedSourceFilePath = FPaths::ConvertRelativePathToFull(FilePath);
		OutDocument.MutableSourceDocument().SourceFilePath = ResolvedSourceFilePath;
		OutDocument.Metadata.Add(TEXT("design.sourceFilePath"), ResolvedSourceFilePath);
		AnchorRelativeImageFillUrls(OutDocument, ResolvedSourceFilePath);
		PencilExtensionResources::ReportPenResourceGaps(OutDocument);
		return true;
	}

	namespace
	{
		bool IsHttpUrl(const FString& Value)
		{
			return Value.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
				|| Value.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
		}

		void AnchorRelativeImageFillUrlsInNode(FDesignNode& Node, const FString& PenDirectory)
		{
			for (FDesignFill& Fill : Node.Visual.Fills)
			{
				if (!Fill.IsImage())
				{
					continue;
				}
				FString& Url = Fill.Image.Url;
				if (Url.IsEmpty() || IsHttpUrl(Url) || !FPaths::IsRelative(Url))
				{
					continue;
				}
				const FString Anchored = FPaths::ConvertRelativePathToFull(FPaths::Combine(PenDirectory, Url));
				if (FPaths::FileExists(Anchored))
				{
					Url = Anchored;
				}
			}
			// Ref-instance descendant override fragments carry fills that later merge into the
			// expanded tree, so they need anchoring too; reusable component definitions already
			// live in Children and are covered by the recursion below.
			for (TPair<FString, TSharedPtr<FDesignNode>>& Pair : Node.Descendants)
			{
				if (Pair.Value.IsValid())
				{
					AnchorRelativeImageFillUrlsInNode(*Pair.Value, PenDirectory);
				}
			}
			for (const TSharedPtr<FDesignNode>& Child : Node.Children)
			{
				if (Child.IsValid())
				{
					AnchorRelativeImageFillUrlsInNode(*Child, PenDirectory);
				}
			}
		}
	}

	void AnchorRelativeImageFillUrls(FDesignDocument& Document, const FString& PenFilePath)
	{
		const FString PenDirectory = FPaths::GetPath(FPaths::ConvertRelativePathToFull(PenFilePath));
		for (const TSharedPtr<FDesignNode>& Child : Document.Children)
		{
			if (Child.IsValid())
			{
				AnchorRelativeImageFillUrlsInNode(*Child, PenDirectory);
			}
		}
	}

	FDesignImportExecutionResult ExecutePenFile(
		const FString& FilePath,
		const FString& TargetNodeId,
		const FDesignImportRequest& Request)
	{
		FDesignDocument Document;
		FString LoadError;
		if (!LoadPenDocument(FilePath, TargetNodeId, Document, &LoadError))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportPipeline: ExecutePenFile failed for %s: %s"), *FilePath, *LoadError);
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
