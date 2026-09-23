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
	// Mark retryable resource warnings so a successful resolution can remove them
	// without depending on each diagnostic message.
	const TCHAR* GResourceResolutionWarningMarker = TEXT("Remote image import:");

	void RemoveResourceResolutionWarnings(FUMGWidgetData& Data)
	{
		Data.Warnings.RemoveAll([](const FString& Warning)
		{
			return Warning.StartsWith(GResourceResolutionWarningMarker);
		});
	}

	FDesign2UMGConversionSettings BuildFallbackConversionSettings()
	{
		return FDesign2UMGConversionSettings::FromSettings(UDesign2UMGSettings::Get());
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

namespace DesignImportPlanning
{
	FString MakeResourceResolutionWarning(const FString& Message)
	{
		// The resolve report already opens with the marker (DesignImportDiagnosticReport
		// parses it); other producers reach here without it and get it attached.
		return Message.StartsWith(GResourceResolutionWarningMarker)
			? Message
			: FString::Printf(TEXT("%s %s"), GResourceResolutionWarningMarker, *Message);
	}

	void ResolveImages(FUMGWidgetData& Data, FString* OutReport)
	{
		FString Report;
		const bool bOk = FDesignRemoteImageImporter::ResolveRemoteImages(Data, &Report);
		if (OutReport)
		{
			*OutReport = Report;
		}
		if (!Report.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("%s"), *Report);
		}
		if (!bOk)
		{
			// Record partial image resolution in Data.Warnings so the commit reports degradation.
			// The shared marker lets a successful retry remove this transient warning.
			const FString Warning = MakeResourceResolutionWarning(
				Report.IsEmpty() ? TEXT("Some remote images failed to resolve or download") : Report);
			Data.Warnings.AddUnique(Warning);
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportPipeline: Unresolved remote images; generation will continue"));
			FDesign2UMGNotification::Warning(NSLOCTEXT("DesignImportPipeline", "ImageWarning", "Design2UMG: Some images failed to download"));
			return;
		}

		// Remove retryable resource warnings after successful resolution.
		RemoveResourceResolutionWarnings(Data);
	}

	void RecordImageResolutionFailure(FUMGWidgetData& Data)
	{
		// Record unresolved Figma references separately from retryable URL downloads.
		// Downloading known URLs does not resolve references with no exported URL.
		Data.Warnings.AddUnique(TEXT("Figma image refs: some image refs could not be resolved; images may be missing"));
	}

	FImportAnalysisResult AnalyzeWidgetData(FUMGWidgetData& Data, const FImportAnalysisInput& Input)
	{
		FImportAnalysisResult Result;
		if (Input.SettingsSnapshot)
		{
			Result.SettingsSnapshot = *Input.SettingsSnapshot;
		}
		if (Input.bResolveImages)
		{
			ResolveImages(Data, &Result.ImageReport);
		}
		Result.Data = Data;
		return Result;
	}

	FImportAnalysisResult AnalyzeDocument(FDesignDocument& Document, const FImportAnalysisInput& Input)
	{
		FImportAnalysisResult Result;
		if (Input.SettingsSnapshot)
		{
			Result.SettingsSnapshot = *Input.SettingsSnapshot;
		}

		// A document whose only top-level node is a reusable library entry (e.g. a selected
		// Figma component definition exported as the design root) is being imported as the
		// asset body; the converter would otherwise skip that root and emit an empty assembly.
		if (Document.Children.Num() == 1 && Document.Children[0].IsValid() && Document.Children[0]->bReusable)
		{
			Document.Children[0]->bReusable = false;
		}

		const FDesign2UMGConversionSettings& ConversionSettings =
			Result.SettingsSnapshot.IsSet()
				? Result.SettingsSnapshot->ConversionSettings
				: BuildFallbackConversionSettings();
		TArray<FString> FontWarnings;
		if (!DesignLocalFontImport::Prepare(Document, FontWarnings, Result.ErrorMessage))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Result.ErrorMessage);
			return Result;
		}
		PrepareDocumentForGeneration(Document, Result.Data, ConversionSettings);
		Result.Data.Warnings.Append(FontWarnings);
		if (Result.SettingsSnapshot.IsSet())
		{
			Result.SettingsSnapshot->GenerationSettings.ComponentConversionSettings = ConversionSettings;
		}
		if (Input.bResolveImages)
		{
			ResolveImages(Result.Data, &Result.ImageReport);
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

		if (Input.bResolveImages)
		{
			ResolveImages(Result.Data, &Result.ImageReport);
		}

		Result.bSucceeded = true;
		return Result;
	}
}
