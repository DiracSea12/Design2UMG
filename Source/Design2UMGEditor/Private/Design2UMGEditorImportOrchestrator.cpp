// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportOrchestrator.h"

#include "Design2UMGEditorImportFrameSelection.h"
#include "Design2UMGEditorImportRequestSetup.h"
#include "Design2UMGEditorImportStateSubsystem.h"
#include "Design2UMGEditorImportSupport.h"
#include "Design2UMGEditorUnattendedExit.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGLog.h"
#include "DesignImportPipeline.h"
#include "DesignPathRasterizer.h"
#include "ExternalServices/PenMCPBridge.h"
#include "FigmaParser.h"
#include "FontResolver.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "PenParser.h"
#include "ProceduralShapeGenerator.h"

#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	// Design2UMG.ImportSource dispatch: "Pen" parses with FPenParser, "Figma"
	// with FFigmaParser; any other source id fails (no silent fallback).
	bool ConvertRegisteredSource(
		const FName SourceId,
		const FString& JsonContents,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot,
		FUMGWidgetData& OutData)
	{
		FDesignDocument Document;
		if (SourceId == TEXT("Pen"))
		{
			if (!FPenParser::ParseDocument(JsonContents, Document))
			{
				return false;
			}
		}
		else if (SourceId == TEXT("Figma"))
		{
			if (!FFigmaParser::ParseDocument(JsonContents, Document))
			{
				return false;
			}
		}
		else
		{
			return false;
		}

		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = false;
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		const FImportAnalysisResult Analysis = DesignImportPipeline::AnalyzeImport(Document, AnalysisInput);
		if (!Analysis.ErrorMessage.IsEmpty()) return false;
		OutData = Analysis.Data;
		return true;
	}

	void ImportPenFileWithFrameSelection(
		const Design2UMGEditorImportOrchestrator::FPenImportRequest& Request,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
	{
		TArray<FDesignFrameInfo> Frames;
		if (!DesignImportPipeline::LoadPenFrames(Request.FilePath, Frames))
		{
			return;
		}

		if (Frames.Num() == 0)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Design2UMG.Import: No frames found in %s"), *Request.FilePath);
			return;
		}

		Design2UMGEditorImportFrameSelection::ExecuteFrameImport(
			Frames,
			LOCTEXT("FrameSelectorTitle", "Select Frame to Import"),
			LOCTEXT("FrameSelectorHint", "This file contains multiple frames. Double-click to import:"),
			[CapturedRequest = Request, CapturedSettingsSnapshot = SettingsSnapshot](const FString& FrameId)
			{
				Design2UMGEditorImportRequestSetup::ExecutePenFileImportWithSettings(
					CapturedRequest.FilePath,
					FrameId,
					Design2UMGEditorImportRequestSetup::MakePenImportRequest(
						CapturedRequest.OutputPath,
						CapturedRequest.bUsePreview,
						CapturedRequest.bRenderPng,
						CapturedSettingsSnapshot));
			});
	}
}

namespace Design2UMGEditorImportOrchestrator
{
	void RunPenConvert(const FPenConvertRequest& Request)
	{
		FString NodesContents;
		FString VarsContents;
		bool bOk = true;
		if (!Design2UMGEditorImportSupport::LoadTextFileOrLog(Request.NodesPath, NodesContents) || !Design2UMGEditorImportSupport::LoadTextFileOrLog(Request.VarsPath, VarsContents))
		{
			bOk = false;
		}

		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
		FUMGWidgetData Data;
		if (bOk && !FPenMCPBridge::ConvertFromMCP(NodesContents, VarsContents, SettingsSnapshot, Data, Request.TargetNodeId))
		{
			bOk = false;
		}

		if (bOk)
		{
			for (int32 Index = 0; Index < Data.RootWidget.Children.Num(); ++Index)
			{
				const FUMGWidgetNode& Child = Data.RootWidget.Children[Index];
				UE_LOG(LogDesign2UMG, Log, TEXT("  [%d] %s (%s)"), Index, *Child.Name, *Child.Type);
			}
		}

		const FDesignImportExecutionResult ExecutionResult = bOk
			? Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(
				Data,
				Design2UMGEditorImportRequestSetup::MakePenImportRequest(
					Request.OutputPath, Design2UMGEditorUnattendedExit::ShouldUsePreviewInCurrentMode(Request.bUsePreview), true, SettingsSnapshot))
			: FDesignImportExecutionResult();

		Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(
			bOk && ExecutionResult.bSucceeded,
			TEXT("Unattended mode: pen convert finished; exiting"));
	}

	void RunPenImport(const FPenImportRequest& Request)
	{
		bool bOk = true;
		FDesignImportExecutionResult ExecutionResult;

		if (!FPaths::FileExists(Request.FilePath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("File not found: %s"), *Request.FilePath);
			bOk = false;
		}
		else if (FApp::IsUnattended() && Request.TargetNodeId.IsEmpty())
		{
			// Unattended runs must name the frame up front; the interactive
			// Frame Selector can never appear in an unattended session.
			UE_LOG(LogDesign2UMG, Error, TEXT("Unattended import requires --node=<id> for %s"), *Request.FilePath);
			bOk = false;
		}
		else
		{
			const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
				Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
			if (Request.TargetNodeId.IsEmpty())
			{
				ImportPenFileWithFrameSelection(Request, SettingsSnapshot);
			}
			else
			{
				ExecutionResult = Design2UMGEditorImportRequestSetup::ExecutePenFileImportWithSettings(
					Request.FilePath,
					Request.TargetNodeId,
					Design2UMGEditorImportRequestSetup::MakePenImportRequest(
						Request.OutputPath,
						Design2UMGEditorUnattendedExit::ShouldUsePreviewInCurrentMode(Request.bUsePreview),
						Request.bRenderPng,
						SettingsSnapshot));
				bOk = ExecutionResult.bSucceeded;
			}
		}

		Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: pen import finished; exiting"));
	}

	void RunRegisteredSourceImport(const FRegisteredSourceImportRequest& Request)
	{
		bool bOk = true;

		FString JsonContents;
		if (!Design2UMGEditorImportSupport::LoadTextFileOrLog(Request.JsonPath, JsonContents))
		{
			bOk = false;
		}

		FUMGWidgetData Data;
		if (bOk)
		{
			const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
				Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
			if (!ConvertRegisteredSource(
				Request.SourceId,
				JsonContents,
				SettingsSnapshot,
				Data))
			{
				bOk = false;
			}
			else
			{
				FDesignImportRequest ImportRequest = Design2UMGEditorImportRequestSetup::MakeImportRequest(
					Request.OutputPath,
					LOCTEXT("ImportSourcePreviewTitle", "Design2UMG Import Preview"),
					Design2UMGEditorUnattendedExit::ShouldUsePreviewInCurrentMode(Request.bUsePreview),
					EDesignBlueprintGenerationMode::BlueprintAndPng,
					SettingsSnapshot);
				const FDesignImportExecutionResult ExecutionResult =
					Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(Data, ImportRequest);
				bOk = ExecutionResult.bSucceeded;
			}
		}

		Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: import source finished; exiting"));
	}

	void RunFigmaImport(const FString& FileUrl, const FString& Token)
	{
		FString FileKey;
		if (!FFigmaParser::ExtractFileKey(FileUrl, FileKey))
		{
			Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("FigmaInvalidUrl", "Design2UMG: Invalid Figma URL or file key"), SNotificationItem::CS_Fail);
			Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma url validation failed; exiting with status 1"));
			return;
		}

		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
		UDesign2UMGImportStateSubsystem* State = UDesign2UMGImportStateSubsystem::Get();
		check(State);
		State->GetFigmaImportService().RunImport(FileKey, Token, SettingsSnapshot);
	}

	void RunLocalFigmaImport(const FLocalFigmaImportRequest& Request)
	{
		bool bOk = true;

		if (!FPaths::FileExists(Request.JsonFilePath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("File not found: %s"), *Request.JsonFilePath);
			bOk = false;
		}

		FDesignDocument Document;
		FString ErrorMessage;
		if (bOk && !Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(Request.JsonFilePath, Document, ErrorMessage))
		{
			Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("LocalFigmaImportFailed", "Design2UMG: Failed to import local Figma export"), SNotificationItem::CS_Fail);
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *ErrorMessage);
			bOk = false;
		}

		if (bOk)
		{
			const FString FinalOutputPath = Request.OutputPath.IsEmpty()
				? FString::Printf(TEXT("/Game/Design2UMG/Figma_%s"), *FDateTime::Now().ToString(TEXT("%m%d_%H%M%S")))
				: Request.OutputPath;

			const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
				Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
			FDesignImportRequest ImportRequest = Design2UMGEditorImportRequestSetup::MakeImportRequest(
				FinalOutputPath,
				LOCTEXT("LocalFigmaPreviewTitle", "Design2UMG Local Figma Preview"),
				Design2UMGEditorUnattendedExit::ShouldUsePreviewInCurrentMode(Request.bUsePreview),
				EDesignBlueprintGenerationMode::BlueprintAndPng,
				SettingsSnapshot,
				false);
			bOk = Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(Document, ImportRequest).bSucceeded;
		}

		Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: local figma import finished; exiting"));
	}

	// Design2UMG.Reimport console-command entry: the command-line path for reimport outside the reimport
	// menu UI (OpenReimportDialog). A thin parameter-mapping wrapper only: load the new-source design.json ->
	// DesignImportPipeline::Reimport; per-node diagnostics and the summary (reimport_*) are logged by the
	// pipeline itself to LogDesign2UMG; this only maps the pipeline outcome to the command exit status
	// (success / all conflicts resolved via sibling copies / not applied).
	void RunDesignReimport(const FString& TargetAssetPath, const FString& JsonFilePath, const bool bGenerateResolutionCopy)
	{
		bool bOk = false;

		FDesignDocument Document;
		FString ErrorMessage;
		if (!Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(JsonFilePath, Document, ErrorMessage))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *ErrorMessage);
		}
		else
		{
			FDesignReimportRequest Request;
			Request.TargetAssetPath = TargetAssetPath;
			Request.bGenerateResolutionCopy = bGenerateResolutionCopy;
			const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);

			bOk = Result.Commit.IsSuccess()
				|| (Result.ResolutionCopyCommit.IsSet() && Result.ResolutionCopyCommit->IsSuccess());
		}

		Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: design reimport finished; exiting"));
	}

	void ConnectToPencilAndImport()
	{
		UDesign2UMGImportStateSubsystem* State = UDesign2UMGImportStateSubsystem::Get();
		check(State);
		State->GetPencilImportService().ConnectAndImport();
	}

	void ShutdownTransientState()
	{
		// Null after editor subsystems deinitialized during engine shutdown: Deinitialize()
		// already ran the same shutdown there, so there is nothing left to do for the services.
		if (UDesign2UMGImportStateSubsystem* State = UDesign2UMGImportStateSubsystem::Get())
		{
			State->Shutdown();
		}
		FFontResolver::ClearCache();
		FDesignPathRasterizer::ClearCache();
		FProceduralShapeGenerator::ReleaseTransientTextures();
	}
}

#undef LOCTEXT_NAMESPACE
