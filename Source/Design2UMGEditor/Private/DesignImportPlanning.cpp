// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPlanning.h"

#include "Design2UMGConverter.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "DesignFontImportService.h"
#include "DesignImageBakeUtils.h"
#include "DesignRemoteImageImporter.h"
#include "UMGJsonParser.h"

namespace
{
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
		DesignFontImportService::PrepareDocumentFonts(Document);
		DesignImageBakeUtils::BakeDocumentLocalImageBlendOverlays(
			Document,
			FString(),
			TEXT("DesignImportPipeline"));
		OutPreparedData = FDesign2UMGConverter::Convert(Document, ConversionSettings);
	}
}

namespace DesignImportPlanning
{
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
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportPipeline: Unresolved remote images; generation will continue"));
			FDesign2UMGNotification::Warning(NSLOCTEXT("DesignImportPipeline", "ImageWarning", "Design2UMG: Some images failed to download"));
		}
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

		const FDesign2UMGConversionSettings& ConversionSettings =
			Result.SettingsSnapshot.IsSet()
				? Result.SettingsSnapshot->ConversionSettings
				: BuildFallbackConversionSettings();
		PrepareDocumentForGeneration(Document, Result.Data, ConversionSettings);
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
		if (!ParseWidgetJson(Input.EditedJson, Result.Data, &Result.ParseError))
		{
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
