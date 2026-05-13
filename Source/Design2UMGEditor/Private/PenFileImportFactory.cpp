// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenFileImportFactory.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "DesignImportDialogs.h"
#include "DesignImportPipeline.h"

#include "WidgetBlueprint.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "PenFileImportFactory"

namespace
{
	const TCHAR* DefaultOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");

	FDesignImportRequest MakeFactoryImportRequest(
		const FString& OutputPath,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
	{
		FDesignImportRequest Request;
		Request.OutputPath = OutputPath;
		Request.PreviewWindowTitle = LOCTEXT("PenImportPreviewTitle", "Design2UMG Import Preview");
		Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
		Request.SettingsSnapshot = SettingsSnapshot;
		return Request;
	}

	FDesignImportExecutionResult ExecuteFactoryImport(
		const FString& FilePath,
		const FString& TargetNodeId,
		const FString& OutputPath,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
	{
		return DesignImportPipeline::ExecutePenFile(
			FilePath,
			TargetNodeId,
			MakeFactoryImportRequest(OutputPath, SettingsSnapshot));
	}
}

// --- UFactory implementation ---

UPenFileImportFactory::UPenFileImportFactory()
{
	bEditorImport = true;
	bText = false;
	SupportedClass = UWidgetBlueprint::StaticClass();
	Formats.Add(TEXT("pen;Pencil Design File"));
}

bool UPenFileImportFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("pen"), ESearchCase::IgnoreCase);
}

UObject* UPenFileImportFactory::FactoryCreateFile(
	UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags,
	const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn,
	bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;

	TArray<FDesignFrameInfo> Frames;
	if (!DesignImportPipeline::LoadPenFrames(Filename, Frames))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenFileImport: Failed to parse %s"), *Filename);
		FDesign2UMGNotification::Error(LOCTEXT("ParseFailed", "Design2UMG: Failed to parse .pen file"));
		return nullptr;
	}

	if (Frames.Num() == 0)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenFileImport: No frames found in %s"), *Filename);
		FDesign2UMGNotification::Error(LOCTEXT("NoFrames", "Design2UMG: No frames found in file"));
		return nullptr;
	}

	const FString OutputPath = InParent ? InParent->GetPathName() : FString(DefaultOutputPath);
	const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
		FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());

	if (Frames.Num() == 1)
	{
		const FDesignImportExecutionResult Result =
			ExecuteFactoryImport(Filename, FString(), OutputPath, SettingsSnapshot);
		bOutOperationCanceled = Result.bPreviewOpened;
		return Result.GeneratedBlueprint;
	}

	// Multiple frames: show a picker and generate asynchronously.
	DesignImportDialogs::ShowFrameSelector(
		Frames,
		LOCTEXT("FrameSelectorTitle", "Select Frame to Import"),
		LOCTEXT("FrameSelectorHint", "This file contains multiple frames. Double-click to import:"),
		[Filename, OutputPath, SettingsSnapshot](const FString& FrameId)
		{
			const FDesignImportExecutionResult Result =
				ExecuteFactoryImport(Filename, FrameId, OutputPath, SettingsSnapshot);
			if (!Result.bSucceeded)
			{
				FDesign2UMGNotification::Error(LOCTEXT("TargetNodeFailed", "Design2UMG: Failed to import selected frame"));
			}
		});
	bOutOperationCanceled = true;
	return nullptr;
}

#undef LOCTEXT_NAMESPACE



