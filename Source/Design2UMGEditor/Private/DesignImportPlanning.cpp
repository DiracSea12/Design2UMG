// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPlanning.h"

#include "Design2UMGConverter.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "DesignImageBakeUtils.h"
#include "DesignRemoteImageImporter.h"
#include "FontResolver.h"
#include "DesignLocalFontImport.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "DesignPreviewContext.h"

namespace
{
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

	void PrepareDocumentForGeneration(
		FDesignDocument& Document,
		FUMGWidgetData& OutPreparedData,
		const FDesign2UMGConversionSettings& ConversionSettings)
	{
		FFontResolver::FindMissingFonts(Document);
		DesignImageBakeUtils::BakeDocumentLocalImageBlendOverlays(
			Document,
			FString(),
			TEXT("DesignImportPipeline"));
		OutPreparedData = FDesign2UMGConverter::Convert(Document, ConversionSettings);
	}
}

namespace
{
	using FImageSourcesByNode = TMap<FString, TSet<FString>>;

	void CollectImageSources(const FUMGWidgetNode& Node, FImageSourcesByNode& OutSources)
	{
		const FString Source = Node.Brush.Image.TexturePath.Get(FString()).TrimStartAndEnd();
		if (!Source.IsEmpty())
		{
			OutSources.FindOrAdd(Node.Id.IsEmpty() ? Node.Name : Node.Id).Add(Source);
		}
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			CollectImageSources(Child, OutSources);
		}
	}

	FDesignImageImportResult ReconcileImageResults(
		const FImageSourcesByNode& ActiveSources,
		FDesignImageImportResult Current,
		const FDesignImageImportResult& Previous)
	{
		TMap<FString, TMap<FString, bool>> Attempts;
		for (const FDesignImageDiagnostic& Diagnostic : Current.Diagnostics)
		{
			const FString& NodeIdentity = Diagnostic.NodeId.IsEmpty() ? Diagnostic.NodeName : Diagnostic.NodeId;
			bool& bAttempted = Attempts.FindOrAdd(NodeIdentity).FindOrAdd(Diagnostic.Resource);
			bAttempted |= Diagnostic.Outcome != EDesignImageDiagnosticOutcome::Skipped;
		}

		for (const FDesignImageDiagnostic& Diagnostic : Previous.Diagnostics)
		{
			// Source-reference resolution happens before image import and is not retried here.
			if (Diagnostic.Category == EDesignImageDiagnosticCategory::UnresolvedReference)
			{
				Current.AddDiagnostic(Diagnostic);
				continue;
			}
			const FString& NodeIdentity = Diagnostic.NodeId.IsEmpty() ? Diagnostic.NodeName : Diagnostic.NodeId;
			if (NodeIdentity.IsEmpty() && Diagnostic.Resource.IsEmpty())
			{
				if (!Current.bWasExecuted)
				{
					Current.AddDiagnostic(Diagnostic);
				}
				continue;
			}
			const TSet<FString>* Sources = ActiveSources.Find(NodeIdentity);
			if (!Sources || (!Sources->Contains(Diagnostic.Resource)
				&& (Diagnostic.ResolvedAssetPath.IsEmpty()
					|| !Sources->Contains(Diagnostic.ResolvedAssetPath))))
			{
				continue;
			}
			const TMap<FString, bool>* NodeAttempts = Attempts.Find(NodeIdentity);
			const bool* bAttempted = NodeAttempts ? NodeAttempts->Find(Diagnostic.Resource) : nullptr;
			if (!bAttempted || (!*bAttempted && Diagnostic.Outcome != EDesignImageDiagnosticOutcome::Skipped))
			{
				Current.AddDiagnostic(Diagnostic);
			}
		}
		Current.RefreshDisplaySummary();
		return Current;
	}
}

namespace DesignImportPlanning
{
	FDesignImageImportResult ResolveImages(
		FUMGWidgetData& Data,
		const FDesignImageImportResult* PreviousResult,
		const FDesignImageImportSettingsSnapshot* Settings)
	{
		FImageSourcesByNode ActiveSources;
		CollectImageSources(Data.RootWidget, ActiveSources);
		const FDesignImageImportSettingsSnapshot EffectiveSettings = Settings
			? *Settings : FDesignImageImportSettingsSnapshot::FromSettings(*UDesign2UMGSettings::Get());
		FDesignImageImportResult Result = FDesignRemoteImageImporter::ResolveRemoteImages(Data, EffectiveSettings);
		if (PreviousResult)
		{
			Result = ReconcileImageResults(ActiveSources, MoveTemp(Result), *PreviousResult);
		}
		else
		{
			Result.RefreshDisplaySummary();
		}
		if (!Result.DisplaySummary.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("%s"), *Result.DisplaySummary);
			if (Result.HasFailures())
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportPipeline: image resolution completed with failures: %s"), *Result.DisplaySummary);
				FDesign2UMGNotification::Warning(NSLOCTEXT("DesignImportPipeline", "ImageWarning", "Design2UMG: one or more images failed to resolve. See Output Log for details."));
			}
		}
		return Result;
	}

	void RecordImageResolutionFailure(FDesignImageImportResult& InOutResult)
	{
		if (InOutResult.Diagnostics.ContainsByPredicate([](const FDesignImageDiagnostic& Diagnostic)
		{
			return Diagnostic.Outcome == EDesignImageDiagnosticOutcome::Failed
				&& Diagnostic.Category == EDesignImageDiagnosticCategory::UnresolvedReference;
		}))
		{
			return;
		}
		FDesignImageDiagnostic Diagnostic;
		Diagnostic.Outcome = EDesignImageDiagnosticOutcome::Failed;
		Diagnostic.Category = EDesignImageDiagnosticCategory::UnresolvedReference;
		Diagnostic.Reason = TEXT("One or more source image references have no resolved resource");
		Diagnostic.Message = TEXT("Figma image refs: some image refs could not be resolved; images may be missing");
		InOutResult.AddDiagnostic(MoveTemp(Diagnostic));
		InOutResult.RefreshDisplaySummary();
	}
	FImportInPlaceAnalysisResult AnalyzeImport(FUMGWidgetData& Data, const FImportAnalysisInput& Input)
	{
		FImportInPlaceAnalysisResult Result;
		if (!Data.ConversionError.IsEmpty())
		{
			Result.ErrorMessage = Data.ConversionError;
			return Result;
		}
		Result.SettingsSnapshot = Input.SettingsSnapshot ? *Input.SettingsSnapshot
			: FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		if (Input.bResolveImages)
		{
			Result.ImageResult = ResolveImages(Data, nullptr,
				&Result.SettingsSnapshot->GenerationSettings.ImageSettings);
		}
		return Result;
	}

	FImportAnalysisResult AnalyzeImport(FDesignDocument& Document, const FImportAnalysisInput& Input)
	{
		FImportAnalysisResult Result;
		Result.SettingsSnapshot = Input.SettingsSnapshot ? *Input.SettingsSnapshot
			: FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());

		// A document whose only top-level node is a reusable library entry (e.g. a selected
		// Figma component definition exported as the design root) is being imported as the
		// asset body; the converter would otherwise skip that root and emit an empty assembly.
		if (Document.Children.Num() == 1 && Document.Children[0].IsValid() && Document.Children[0]->bReusable)
		{
			Document.Children[0]->bReusable = false;
		}

		const FDesign2UMGConversionSettings& ConversionSettings = Result.SettingsSnapshot->ConversionSettings;
		TArray<FString> FontWarnings;
		if (!DesignLocalFontImport::Prepare(Document, FontWarnings, Result.ErrorMessage))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Result.ErrorMessage);
			return Result;
		}
		PrepareDocumentForGeneration(Document, Result.Data, ConversionSettings);
		if (!Result.Data.ConversionError.IsEmpty())
		{
			Result.ErrorMessage = Result.Data.ConversionError;
			return Result;
		}
		Result.Data.Warnings.Append(FontWarnings);
		Result.SettingsSnapshot->GenerationSettings.ComponentConversionSettings = ConversionSettings;
		if (Input.bResolveImages)
		{
			Result.ImageResult = ResolveImages(Result.Data, nullptr,
				&Result.SettingsSnapshot->GenerationSettings.ImageSettings);
		}
		return Result;
	}

	FPreviewRefreshResult AnalyzePreviewRefreshInput(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges)
	{
		FPreviewRefreshResult Result;
		Result.RequestId = Input.RequestId;
		Result.EditedJson = Input.EditedJson;
		if (Input.OriginalData && Input.EditedJson == FUMGJsonSerializer::Serialize(*Input.OriginalData))
		{
			Result.Data = *Input.OriginalData;
		}
		else if (!ParseWidgetJson(Input.EditedJson, Result.Data, &Result.ParseError))
		{
			return Result;
		}
		else if (Input.OriginalData && !RestoreDesignPreviewContext(*Input.OriginalData, Result.Data, Result.ParseError))
		{
			Result.Data = FUMGWidgetData();
			return Result;
		}

		if (MappingChanges)
		{
			Result.Data.IconFontMappingChanges = *MappingChanges;
		}

		FImageSourcesByNode ActiveSources;
		CollectImageSources(Result.Data.RootWidget, ActiveSources);
		if (Input.bResolveImages)
		{
			const FDesignImageImportResult RetainedFacts = ReconcileImageResults(
				ActiveSources, FDesignImageImportResult(), Input.InitialImageResult);
			Result.ImageResult = ResolveImages(Result.Data, &RetainedFacts,
				Input.ImageSettings.IsSet() ? &Input.ImageSettings.GetValue() : nullptr);
		}
		else
		{
			Result.ImageResult = ReconcileImageResults(
				ActiveSources, FDesignImageImportResult(), Input.InitialImageResult);
			Result.ImageResult.ImportPath = Input.InitialImageResult.ImportPath;
			Result.ImageResult.bWasExecuted = Input.InitialImageResult.bWasExecuted;
			Result.ImageResult.RefreshDisplaySummary();
		}

		Result.bSucceeded = true;
		return Result;
	}
}
