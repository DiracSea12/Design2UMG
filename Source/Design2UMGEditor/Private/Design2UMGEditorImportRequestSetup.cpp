// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportRequestSetup.h"

#include "Design2UMGSettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace Design2UMGEditorImportRequestSetup
{
	FDesign2UMGImportSettingsSnapshot CaptureSettingsSnapshot()
	{
		return FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
	}

	FDesignImportRequest MakeImportRequest(
		const FString& OutputPath,
		const FText& PreviewWindowTitle,
		const bool bUsePreview,
		const EDesignBlueprintGenerationMode GenerationMode,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		const bool bResolveImages,
		const TOptional<FDesignDocument>& DiagnosticDocument)
	{
		FDesignImportRequest Request;
		Request.OutputPath = OutputPath;
		Request.PreviewWindowTitle = PreviewWindowTitle;
		Request.bUsePreview = bUsePreview;
		Request.bResolveImages = bResolveImages;
		Request.Generation.Mode = GenerationMode;
		Request.SettingsSnapshot = SettingsSnapshot;
		Request.DiagnosticDocument = DiagnosticDocument;
		return Request;
	}

	FDesignImportRequest MakePenImportRequest(
		const FString& OutputPath,
		const bool bUsePreview,
		const bool bRenderPng,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
	{
		return MakeImportRequest(
			OutputPath,
			LOCTEXT("PenConvertPreviewWindowTitle", "Design2UMG Pen Preview"),
			bUsePreview,
			bRenderPng ? EDesignBlueprintGenerationMode::BlueprintAndPng : EDesignBlueprintGenerationMode::BlueprintOnly,
			SettingsSnapshot);
	}

	template<typename TInvoke>
	FDesignImportExecutionResult ExecuteWithCapturedSettings(
		const FDesignImportRequest& Request,
		TInvoke&& Invoke)
	{
		if (Request.SettingsSnapshot.IsSet())
		{
			const Design2UMGGenerationSettingsContext::FScopedSettingsSnapshot ScopedGenerationSettings(
				Request.SettingsSnapshot->GenerationSettings);
			return Invoke();
		}

		return Invoke();
	}

	FDesignImportExecutionResult ExecuteImportWithSettings(
		FUMGWidgetData& Data,
		const FDesignImportRequest& Request)
	{
		return ExecuteWithCapturedSettings(
			Request,
			[&Data, &Request]()
		{
			return DesignImportPipeline::Execute(Data, Request);
		});
	}

	FDesignImportExecutionResult ExecuteImportWithSettings(
		FDesignDocument& Document,
		const FDesignImportRequest& Request)
	{
		return ExecuteWithCapturedSettings(
			Request,
			[&Document, &Request]()
		{
			return DesignImportPipeline::Execute(Document, Request);
		});
	}

	FDesignImportExecutionResult ExecutePenFileImportWithSettings(
		const FString& FilePath,
		const FString& TargetNodeId,
		const FDesignImportRequest& Request)
	{
		return ExecuteWithCapturedSettings(
			Request,
			[&FilePath, &TargetNodeId, &Request]()
		{
			return DesignImportPipeline::ExecutePenFile(FilePath, TargetNodeId, Request);
		});
	}

	TSharedPtr<FJsonObject> MakePencilBatchGetArgs()
	{
		TSharedPtr<FJsonObject> BatchArgs = MakeShared<FJsonObject>();
		BatchArgs->SetStringField(TEXT("filePath"), TEXT(""));

		TArray<TSharedPtr<FJsonValue>> Patterns;
		TSharedPtr<FJsonObject> Pattern = MakeShared<FJsonObject>();
		Pattern->SetStringField(TEXT("type"), TEXT("frame"));
		Patterns.Add(MakeShared<FJsonValueObject>(Pattern));
		BatchArgs->SetArrayField(TEXT("patterns"), Patterns);
		BatchArgs->SetBoolField(TEXT("includePathGeometry"), true);
		BatchArgs->SetNumberField(TEXT("searchDepth"), 1);
		BatchArgs->SetNumberField(TEXT("readDepth"), 99);
		return BatchArgs;
	}

	TSharedPtr<FJsonObject> MakePencilVariablesArgs()
	{
		TSharedPtr<FJsonObject> VarsArgs = MakeShared<FJsonObject>();
		VarsArgs->SetStringField(TEXT("filePath"), TEXT(""));
		return VarsArgs;
	}
}

#undef LOCTEXT_NAMESPACE
