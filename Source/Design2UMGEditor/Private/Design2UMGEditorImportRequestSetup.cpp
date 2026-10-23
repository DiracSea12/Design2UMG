// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportRequestSetup.h"

#include "Design2UMGSettings.h"
#include "Dom/JsonObject.h"

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

	TSharedPtr<FJsonObject> MakePencilReadDocumentArgs()
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("filePath"), TEXT(""));
		// One execute reads every top-level subtree and the current variables/themes.
		Args->SetStringField(TEXT("input"), TEXT("Print(\"Design2UMGDocument:\"+JSON.stringify({nodes:Get((n,c)=>{c.skipChildren();return Get(n.id,{depth:99,includePathGeometry:true})},{depth:0}),variables:GetVariables()}))"));
		return Args;
	}
}

#undef LOCTEXT_NAMESPACE
