// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorDesignSourceRegistry.h"

#include "Design2UMGLog.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "PenParser.h"

namespace
{
	using FDesignSourceConvertFunc = TFunction<bool(const FString& RawJson, FUMGWidgetData& OutData)>;

	TMap<FName, FDesignSourceConvertFunc>& GetRegistry()
	{
		static TMap<FName, FDesignSourceConvertFunc> Registry;
		return Registry;
	}

	void RegisterSource(FName SourceId, FDesignSourceConvertFunc&& Convert)
	{
		if (GetRegistry().Contains(SourceId))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignSourceRegistry: Overwriting existing source '%s'"), *SourceId.ToString());
		}

		GetRegistry().Add(SourceId, MoveTemp(Convert));
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
		RegisterSource(TEXT("Pen"), [](const FString& RawJson, FUMGWidgetData& OutData)
		{
			FDesignDocument Document;
			if (!FPenParser::ParseDocument(RawJson, Document))
			{
				return false;
			}

			OutData = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{ false }).Data;
			return true;
		});

		RegisterSource(TEXT("Figma"), [](const FString& RawJson, FUMGWidgetData& OutData)
		{
			FDesignDocument Document;
			if (!FFigmaParser::ParseDocument(RawJson, Document))
			{
				return false;
			}

			OutData = DesignImportPipeline::AnalyzeImport(Document, FImportAnalysisInput{ false }).Data;
			return true;
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

	bool TryConvertSource(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData)
	{
		const FDesignSourceConvertFunc* Convert = GetRegistry().Find(SourceId);
		return Convert && (*Convert)(RawJson, OutData);
	}
}
