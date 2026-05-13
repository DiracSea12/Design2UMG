// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorDesignSourceRegistry.h"

#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "FigmaParser.h"
#include "PenParser.h"

namespace
{
	using FDesignSourceParseFunc = TFunction<bool(const FString& RawJson, FDesignDocument& OutDocument)>;

	TMap<FName, FDesignSourceParseFunc>& GetRegistry()
	{
		static TMap<FName, FDesignSourceParseFunc> Registry;
		return Registry;
	}

	void RegisterSource(FName SourceId, FDesignSourceParseFunc&& Parse)
	{
		if (GetRegistry().Contains(SourceId))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignSourceRegistry: Overwriting existing source '%s'"), *SourceId.ToString());
		}

		GetRegistry().Add(SourceId, MoveTemp(Parse));
	}

	void UnregisterSource(FName SourceId)
	{
		GetRegistry().Remove(SourceId);
	}
}

namespace Design2UMGEditorDesignSourceRegistry
{
	void RegisterBuiltInSources()
	{
		RegisterSource(TEXT("Pen"), [](const FString& RawJson, FDesignDocument& OutDocument)
		{
			return FPenParser::ParseDocument(RawJson, OutDocument);
		});

		RegisterSource(TEXT("Figma"), [](const FString& RawJson, FDesignDocument& OutDocument)
		{
			return FFigmaParser::ParseDocument(RawJson, OutDocument);
		});
	}

	void UnregisterBuiltInSources()
	{
		UnregisterSource(TEXT("Pen"));
		UnregisterSource(TEXT("Figma"));
	}

	void GetRegisteredSourceIds(TArray<FName>& OutSourceIds)
	{
		GetRegistry().GetKeys(OutSourceIds);
	}

	bool TryParseSourceDocument(FName SourceId, const FString& RawJson, FDesignDocument& OutDocument)
	{
		const FDesignSourceParseFunc* Parse = GetRegistry().Find(SourceId);
		return Parse && (*Parse)(RawJson, OutDocument);
	}

	bool TryConvertSource(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData)
	{
		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		return TryConvertSource(SourceId, RawJson, SettingsSnapshot, OutData);
	}

	bool TryConvertSource(
		FName SourceId,
		const FString& RawJson,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		FUMGWidgetData& OutData)
	{
		FDesignDocument Document;
		if (!TryParseSourceDocument(SourceId, RawJson, Document))
		{
			return false;
		}

		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = false;
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		OutData = DesignImportPipeline::AnalyzeImport(Document, AnalysisInput).Data;
		return true;
	}
}
