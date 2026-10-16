// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPipeline.h"
#include "DesignImportDiagnosticReport.h"
#include "DesignImportGeneration.h"
#include "DesignImportPlanning.h"
#include "Design2UMGLog.h"
#include "DesignImportDialogs.h"
#include "DesignJsonUtils.h"
#include "DesignRemoteImageImporter.h"
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
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings);
		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportInPlaceAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument);
		FDesign2UMGImportSettingsSnapshot BuildFallbackImportSettingsSnapshot()
		{
			return FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		}

		// Single settings-resolution rule shared by every Execute path: the analysis
		// snapshot wins over the request snapshot; both fall back to a settings-CDO snapshot.
		const FDesign2UMGGenerationSettingsSnapshot& ResolveEffectiveGenerationSettings(
			const FDesignImportRequest& Request,
			const TOptional<FDesign2UMGImportSettingsSnapshot>& AnalysisSnapshot,
			FDesign2UMGImportSettingsSnapshot& OutFallback)
		{
			if (AnalysisSnapshot.IsSet())
			{
				return AnalysisSnapshot->GenerationSettings;
			}
			if (Request.SettingsSnapshot.IsSet())
			{
				return Request.SettingsSnapshot->GenerationSettings;
			}
			OutFallback = BuildFallbackImportSettingsSnapshot();
			return OutFallback.GenerationSettings;
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
			if (!Request.DiagnosticDocument.IsSet())
			{
				return;
			}

			DesignImportDiagnosticReport::FImportReportArtifacts Artifacts;
			if (Request.DiagnosticImageResultOverride.IsSet())
			{
				Artifacts.ImageResult = Request.DiagnosticImageResultOverride.GetValue();
			}
			else if (Request.RuntimeImageResult.IsSet())
			{
				Artifacts.ImageResult = Request.RuntimeImageResult.GetValue();
			}
			Artifacts.RequestedOutputPath = Request.OutputPath;
			Artifacts.RequestedRenderPngPath = Result.RenderPngPath.IsEmpty()
				? Request.Generation.CompiledPngFilePath
				: Result.RenderPngPath;
			Artifacts.GeneratedAssetPath = Result.GeneratedAssetPath;
			Artifacts.RenderPngPath = Result.RenderPngPath;
			Artifacts.Commit = Result.Commit;
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
			if (!Data.ConversionError.IsEmpty())
			{
				Result.ErrorMessage = Data.ConversionError;
				return Result;
			}
			FDesign2UMGGenerationOutput GenerationOutput;
			switch (Request.Generation.Mode)
			{
			case EDesignBlueprintGenerationMode::BlueprintOnly:
			{
				UWidgetBlueprint* GeneratedBlueprint =
					DesignImportGeneration::GenerateBlueprint(
						Data,
						Request.OutputPath,
						GenerationSettings,
						&GenerationOutput,
						nullptr,
						Request.Generation.bPersistIconFontMappingChanges);
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
				Result = DesignImportGeneration::GenerateBlueprintAndPng(Data, Request, GenerationSettings);
				break;
			case EDesignBlueprintGenerationMode::BlueprintAndCompiledPng:
				Result = DesignImportGeneration::GenerateBlueprintAndCompiledPng(Data, Request, GenerationSettings);
				break;
			}
			if (Request.Generation.Mode == EDesignBlueprintGenerationMode::BlueprintOnly)
			{
				Result.Commit = GenerationOutput.Commit;
				Result.ComponentMappingDiagnostics = MoveTemp(GenerationOutput.MappingDiagnostics);
			}

			Result.Warnings = Data.Warnings;
			const bool bHasImageFailures = Request.RuntimeImageResult.IsSet()
				&& Request.RuntimeImageResult->HasFailures();
			if (bHasImageFailures)
			{
				FDesignImageImportResult ImageResult = Request.RuntimeImageResult.GetValue();
				if (ImageResult.DisplaySummary.IsEmpty())
				{
					ImageResult.RefreshDisplaySummary();
				}
				Result.Warnings.AddUnique(ImageResult.DisplaySummary);
			}
			if (Result.Commit.IsSuccess() && (!Data.Warnings.IsEmpty() || bHasImageFailures))
				Result.Commit.Status = EDesign2UMGCommitStatus::SucceededWithDegradation;
			Result.bSucceeded = Result.bSucceeded && Result.Commit.IsSuccess();
			if (Result.ErrorMessage.IsEmpty() && Result.Commit.Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure)
				Result.ErrorMessage = TEXT("The asset was saved, but its generation manifest was not updated: ") + Result.Commit.AssetPath;
			if (Result.ErrorMessage.IsEmpty() && Result.Commit.Status == EDesign2UMGCommitStatus::RollbackIncomplete)
				Result.ErrorMessage = TEXT("Save failed and recovery is incomplete: ") + Result.Commit.AssetPath;
			if (Result.Commit.Asset == EDesign2UMGFileState::Saved)
				Result.GeneratedAssetPath = Result.Commit.AssetPath;
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
			const FDesign2UMGGenerationSettingsSnapshot& GenerationSettings)
		{
			return FOnPreviewConfirmed::CreateLambda(
				[CapturedGeneration = Request.Generation,
				 bCapturedResolveImages = Request.bResolveImages,
				 CapturedDiagnosticDocument = DiagnosticDocument,
				 CapturedSettings = GenerationSettings](const FPreviewGenerationRequest& PreviewGenerationRequest)
				{
					const FPreviewApprovedSnapshot& ApprovedSnapshot = PreviewGenerationRequest.ApprovedSnapshot;

					FArtifactGenerationRequest GenerationRequest = PreviewGenerationRequest.GenerationRequest;
					GenerationRequest.Generation = CapturedGeneration;
					GenerationRequest.bResolveImages = bCapturedResolveImages;
					if (CapturedDiagnosticDocument.IsSet())
					{
						GenerationRequest.DiagnosticDocument = CapturedDiagnosticDocument;
					}

					return GenerateArtifacts(ApprovedSnapshot, GenerationRequest, CapturedSettings);
				});
		}

		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportInPlaceAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument)
		{
			FDesignImportExecutionResult Result;
			if (!AnalysisResult.ErrorMessage.IsEmpty())
			{
				Result.ErrorMessage = AnalysisResult.ErrorMessage;
				return Result;
			}
			FDesign2UMGImportSettingsSnapshot FallbackSettingsSnapshot;
			const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings = &ResolveEffectiveGenerationSettings(
				Request, AnalysisResult.SettingsSnapshot, FallbackSettingsSnapshot);

			if (Request.bUsePreview
				&& DesignImportDialogs::OpenPreviewWindow(
					Data,
					Request.OutputPath,
					Request.PreviewWindowTitle,
					CreatePreviewConfirmedHandler(Request, DiagnosticDocument, *GenerationSettings),
					FOnPreviewCancelled::CreateLambda(
						[]()
						{
							UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportPipeline: Preview cancelled"));
						}),
					GenerationSettings
						? TOptional<FDesignPreviewMaterializationSettingsSnapshot>(
							BuildPreviewMaterializationSettingsSnapshot(*GenerationSettings))
						: TOptional<FDesignPreviewMaterializationSettingsSnapshot>(),
					AnalysisResult.ImageResult))
			{
				Result.bSucceeded = true;
				Result.bPreviewOpened = true;
				return Result;
			}

			FArtifactGenerationRequest GenerationRequest;
			GenerationRequest.OutputPath = Request.OutputPath;
			GenerationRequest.Generation = Request.Generation;
			GenerationRequest.RuntimeImageResult = AnalysisResult.ImageResult;
			GenerationRequest.DiagnosticImageResultOverride = Request.DiagnosticImageResult;
			if (DiagnosticDocument.IsSet())
			{
				GenerationRequest.DiagnosticDocument = DiagnosticDocument;
			}
			else if (Request.DiagnosticDocument.IsSet())
			{
				GenerationRequest.DiagnosticDocument = Request.DiagnosticDocument;
			}

			FArtifactGenerationRuntimeResult RuntimeResult;
			FArtifactGenerationResult GenerationResult =
				GenerateArtifactsInternal(Data, GenerationRequest, GenerationSettings, &RuntimeResult);
			Result.Commit = GenerationResult.Commit;
			Result.bSucceeded = GenerationResult.bSucceeded;
			Result.ErrorMessage = GenerationResult.ErrorMessage;
			Result.GeneratedBlueprint = RuntimeResult.GeneratedBlueprint;
			Result.GeneratedAssetPath = GenerationResult.GeneratedAssetPath;
			Result.RenderPngPath = GenerationResult.RenderPngPath;
			Result.RenderedSize = GenerationResult.RenderedSize;
			Result.RenderedWidget = MoveTemp(GenerationResult.RenderedWidget);
			Result.RenderedSlateWidget = MoveTemp(GenerationResult.RenderedSlateWidget);
			return Result;
		}
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
		if (!GenerationRequest.RuntimeImageResult.IsSet())
		{
			GenerationRequest.RuntimeImageResult = ApprovedSnapshot.ImageResult;
		}

		FUMGWidgetData GenerationData = ApprovedSnapshot.Data;
		if (GenerationRequest.bResolveImages)
		{
			const FDesignImageImportResult* PreviousImageResult = GenerationRequest.RuntimeImageResult.IsSet()
				? &GenerationRequest.RuntimeImageResult.GetValue()
				: nullptr;
			GenerationRequest.RuntimeImageResult =
				DesignImportPlanning::ResolveImages(GenerationData, PreviousImageResult, &GenerationSettings.ImageSettings);
		}
		return GenerateArtifactsInternal(GenerationData, GenerationRequest, &GenerationSettings);
	}

	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request)
	{
		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = Request.bResolveImages;
		AnalysisInput.SettingsSnapshot = Request.SettingsSnapshot.IsSet() ? &Request.SettingsSnapshot.GetValue() : nullptr;
		const FImportInPlaceAnalysisResult AnalysisResult = DesignImportPlanning::AnalyzeImport(Data, AnalysisInput);
		return ExecuteWithAnalysis(Data, AnalysisResult, Request, Request.DiagnosticDocument);
	}

	FDesignImportExecutionResult Execute(FDesignDocument& Document, const FDesignImportRequest& Request, const FImportAnalysisResult* PrecomputedAnalysis)
	{
		FImportAnalysisResult OwnedAnalysisResult;
		const FImportAnalysisResult* AnalysisResult = PrecomputedAnalysis;
		if (!AnalysisResult)
		{
			FImportAnalysisInput AnalysisInput;
			AnalysisInput.bResolveImages = Request.bResolveImages;
			AnalysisInput.SettingsSnapshot = Request.SettingsSnapshot.IsSet() ? &Request.SettingsSnapshot.GetValue() : nullptr;
			OwnedAnalysisResult = DesignImportPlanning::AnalyzeImport(Document, AnalysisInput);
			AnalysisResult = &OwnedAnalysisResult;
		}
		FUMGWidgetData Data = PrecomputedAnalysis
			? AnalysisResult->Data
			: MoveTemp(OwnedAnalysisResult.Data);
		FDesignImportRequest FinalRequest = Request;
		// The analysis snapshot rides on the request so the async preview-confirmed
		// handler keeps it; ExecuteWithAnalysis applies the same resolution rule.
		if (!FinalRequest.SettingsSnapshot.IsSet() && AnalysisResult->SettingsSnapshot.IsSet())
		{
			FinalRequest.SettingsSnapshot = AnalysisResult->SettingsSnapshot;
		}
		FinalRequest.DiagnosticImageResult = AnalysisResult->ImageResult;
		return ExecuteWithAnalysis(Data, *AnalysisResult, FinalRequest, TOptional<FDesignDocument>(Document));
	}

	void WriteImportDiagnosticReport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FDesignImageImportResult& ImageResult,
		const FString& RequestedOutputPath,
		const FString& RequestedRenderPngPath,
		const FString& GeneratedAssetPath,
		const FString& RenderPngPath)
	{
		DesignImportDiagnosticReport::FImportReportArtifacts Artifacts;
		Artifacts.ImageResult = ImageResult;
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
		// codepoints, and the bundled library documents provide component definitions and variable tables that
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
		void AnchorRelativeImageFillUrl(FDesignFill& Fill, const FString& PenDirectory)
		{
			FString& Url = Fill.Image.Url;
			if (!Fill.IsImage() || Url.IsEmpty() || FDesignRemoteImageImporter::IsHttpUrl(Url) || !FPaths::IsRelative(Url)) return;
			const FString Anchored = FPaths::ConvertRelativePathToFull(FPaths::Combine(PenDirectory, Url));
			if (FPaths::FileExists(Anchored)) Url = Anchored;
		}

		void AnchorRelativeImageFillUrlsInNode(FDesignNode& Node, const FString& PenDirectory);

		void AnchorRelativeImageFillUrlsInPatch(FDesignNodePatch& Patch, const FString& PenDirectory)
		{
			if (Patch.Fills.IsSet())
				for (FDesignFill& Fill : Patch.Fills.GetValue()) AnchorRelativeImageFillUrl(Fill, PenDirectory);
			if (Patch.StrokeFill.IsSet() && Patch.StrokeFill->IsSet())
				AnchorRelativeImageFillUrl(Patch.StrokeFill->GetValue(), PenDirectory);
			if (Patch.Children.IsSet())
				for (const TSharedPtr<FDesignNode>& Child : Patch.Children.GetValue())
					if (Child.IsValid()) AnchorRelativeImageFillUrlsInNode(*Child, PenDirectory);
		}

		void AnchorRelativeImageFillUrlsInNode(FDesignNode& Node, const FString& PenDirectory)
		{
			for (FDesignFill& Fill : Node.Visual.Fills) AnchorRelativeImageFillUrl(Fill, PenDirectory);
			if (Node.Visual.Stroke.Fill.IsSet()) AnchorRelativeImageFillUrl(Node.Visual.Stroke.Fill.GetValue(), PenDirectory);
			if (Node.RefOverrides.IsValid())
			{
				FDesignNodePatch Patch = *Node.RefOverrides;
				AnchorRelativeImageFillUrlsInPatch(Patch, PenDirectory);
				Node.RefOverrides = MakeShared<FDesignNodePatch>(MoveTemp(Patch));
			}
			for (auto& Pair : Node.Descendants)
			{
				if (Pair.Value.Replacement.IsValid()) AnchorRelativeImageFillUrlsInNode(*Pair.Value.Replacement, PenDirectory);
				AnchorRelativeImageFillUrlsInPatch(Pair.Value.Patch, PenDirectory);
			}
			for (const TSharedPtr<FDesignNode>& Child : Node.Children)
				if (Child.IsValid()) AnchorRelativeImageFillUrlsInNode(*Child, PenDirectory);
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
