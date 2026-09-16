// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportServices.h"

#include "Design2UMGEditorImportOrchestrator.h"
#include "Design2UMGEditorImportFrameSelection.h"
#include "Design2UMGEditorImportRequestSetup.h"
#include "Design2UMGEditorImportSupport.h"
#include "Design2UMGEditorMcpImportPayload.h"
#include "Design2UMGEditorUnattendedExit.h"
#include "Design2UMGLog.h"
#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FigmaComponentDependencies.h"
#include "DesignJsonUtils.h"
#include "DesignSchemaSourceExtensions.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/App.h"
#include "Widgets/Notifications/SNotificationList.h"

// Same LOCTEXT namespace string as the pre-split orchestrator, so the moved keys keep their identity.
#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace Design2UMGEditorImportServices
{
	void FPencilImportService::ConnectAndImport()
	{
		if (FPlatformAtomics::InterlockedCompareExchange(&bFetchInProgress, 1, 0) != 0)
		{
			return;
		}

		const uint32 FetchGeneration = static_cast<uint32>(Generation.GetValue());
		FPencilConnector& ActiveConnector = GetOrCreateConnector();
		if (!ActiveConnector.IsRunning())
		{
			Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("LaunchingMCP", "Design2UMG: Launching MCP Server..."), SNotificationItem::CS_Pending);
			ActiveConnector.LaunchAsync(
				0,
				[this, FetchGeneration](const bool bLaunchOk)
				{
					if (!IsGenerationCurrent(FetchGeneration))
					{
						ReleaseFetchSlot();
						return;
					}

					if (!bLaunchOk)
					{
						Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("LaunchFailed", "Design2UMG: Failed to launch MCP Server"), SNotificationItem::CS_Fail);
						ReleaseFetchSlot();
						return;
					}

					BeginFetch(FetchGeneration);
				});
			return;
		}

		BeginFetch(FetchGeneration);
	}

	void FPencilImportService::BeginFetch(const uint32 FetchGeneration)
	{
		if (!IsGenerationCurrent(FetchGeneration) || !Connector)
		{
			ReleaseFetchSlot();
			return;
		}

		Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("Connecting", "Design2UMG: Fetching design from Pencil..."), SNotificationItem::CS_Pending);

		Connector->GetClient().CallToolAsync(
			TEXT("batch_get"),
			Design2UMGEditorImportRequestSetup::MakePencilBatchGetArgs(),
			[this, FetchGeneration](const bool bBatchOk, const FString& NodesJson)
			{
				if (!IsGenerationCurrent(FetchGeneration))
				{
					ReleaseFetchSlot();
					return;
				}

				if (!bBatchOk)
				{
					Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("BatchGetFailed", "Design2UMG: batch_get failed"), SNotificationItem::CS_Fail);
					ReleaseFetchSlot();
					return;
				}

				if (!Connector)
				{
					ReleaseFetchSlot();
					return;
				}

				Connector->GetClient().CallToolAsync(
					TEXT("get_variables"),
					Design2UMGEditorImportRequestSetup::MakePencilVariablesArgs(),
					[this, FetchGeneration, CapturedNodes = NodesJson](const bool bVarsOk, const FString& VarsJson)
					{
						if (!IsGenerationCurrent(FetchGeneration))
						{
							ReleaseFetchSlot();
							return;
						}

						const FString FinalVars = bVarsOk ? VarsJson : TEXT("{}");
						if (!bVarsOk)
						{
							UE_LOG(LogDesign2UMG, Warning, TEXT("ConnectToPencil: get_variables failed, using empty"));
						}

						Design2UMGEditorMcpImportPayload::HandleMCPDataReady(CapturedNodes, FinalVars);
						ReleaseFetchSlot();
					});
			});
	}

	bool FFigmaImportSession::IsCancelled() const
	{
		return !Owner.IsGenerationCurrent(Generation);
	}

	void FFigmaImportSession::CancelRequests()
	{
		if (Client)
		{
			Client->CancelRequests();
		}
	}

	void FFigmaImportSession::Start()
	{
		Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("FigmaFetching", "Design2UMG: Fetching design from Figma..."), SNotificationItem::CS_Pending);

		Client->GetFile(
			FileKey,
			[Self = AsShared()](const bool bSuccess, const FString& Body)
			{
				Self->HandleFileResponse(bSuccess, Body);
			});
	}

	void FFigmaImportSession::HandleFileResponse(const bool bSuccess, const FString& Body)
	{
		if (IsCancelled())
		{
			return;
		}

		if (!bSuccess)
		{
			Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("FigmaGetFileFailed", "Design2UMG: Figma API request failed"), SNotificationItem::CS_Fail);
			Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma api request failed; exiting with status 1"));
			return;
		}

		FString PreparedSource;
		FString DependencyError;
		if (!FigmaComponentDependencies::PrepareRestFile(Body, FileKey, PreparedSource, DependencyError)
			|| !DesignJsonUtils::TryParseObject(PreparedSource, ComponentSource))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Figma dependency preparation failed: %s"), *DependencyError);
			Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("FigmaParseFailed", "Design2UMG: Failed to parse Figma file"), SNotificationItem::CS_Fail);
			Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma parse failed; exiting with status 1"));
			return;
		}

		ResolvedLibraryFiles.Add(FileKey);
		SelectImportRootAndResolve(PreparedSource);
	}

	void FFigmaImportSession::SelectImportRootAndResolve(const FString& PreparedSource)
	{
		if (!SelectedRootId.IsEmpty())
		{
			BeginResolveSelectedRoot(SelectedRootId);
			return;
		}

		FDesignDocument SelectionDocument;
		if (!FFigmaParser::ParseDocument(PreparedSource, SelectionDocument))
		{
			FailComponentImport(TEXT("Failed to parse Figma file before component dependency resolution"));
			return;
		}
		const TArray<FDesignFrameInfo> Frames = ExtractTopFrames(SelectionDocument);
		if (Frames.IsEmpty())
		{
			FailComponentImport(TEXT("No importable Figma roots found"));
			return;
		}
		if (FApp::IsUnattended() && Frames.Num() > 1)
		{
			FailComponentImport(TEXT("Unattended Figma import requires a selected root"));
			return;
		}

		const FString SingleRootId = Frames.Num() == 1 ? Frames[0].Id : FString();
		Design2UMGEditorImportFrameSelection::ExecuteFrameImport(
			Frames,
			LOCTEXT("FigmaFrameSelectorTitle", "Select Figma Frame to Import"),
			LOCTEXT("FigmaFrameSelectorHint", "This file contains multiple frames. Double-click to import:"),
			[Self = AsShared(), SingleRootId](const FString& FrameId)
			{
				Self->BeginResolveSelectedRoot(FrameId.IsEmpty() ? SingleRootId : FrameId);
			});
	}

	void FFigmaImportSession::BeginResolveSelectedRoot(const FString& RootId)
	{
		if (IsCancelled()) return;
		FString Error;
		if (!FigmaComponentDependencies::FilterRestFileToSelectedRoot(ComponentSource, RootId, Error))
		{
			FailComponentImport(Error);
			return;
		}
		SelectedRootId = RootId;
		ResolveMissingComponentDefinitions();
	}

	void FFigmaImportSession::ParseResolvedComponentFile()
	{
		FString Source;
		FJsonSerializer::Serialize(ComponentSource.ToSharedRef(), TJsonWriterFactory<>::Create(&Source));
#if WITH_DEV_AUTOMATION_TESTS
		if (AutomationComponentsResolved)
		{
			AutomationComponentsResolved(Source, ComponentImageUrls);
			return;
		}
#endif
		FDesignDocument ParsedDocument;
		if (!FFigmaParser::ParseDocument(Source, ParsedDocument))
		{
			FailComponentImport(TEXT("Failed to parse resolved Figma component file"));
			return;
		}
		ParsedDocument.MutableSourceDocument().ResolvedComponentImageUrls = ComponentImageUrls;
		const TSharedRef<FDesignDocument> Document = MakeShared<FDesignDocument>(MoveTemp(ParsedDocument));
		FFigmaImageExporter::ResolveImageRefs(
			*Client,
			FileKey,
			Document,
			[Self = AsShared(), Document](const bool bImagesResolved)
			{
				Self->HandleImagesResolved(Document, bImagesResolved);
			});
	}

	void FFigmaImportSession::FailComponentImport(const FString& Reason)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Figma component import failed: %s"), *Reason);
		Design2UMGEditorImportSupport::ShowNotification(FText::FromString(TEXT("Design2UMG: ") + Reason), SNotificationItem::CS_Fail);
		Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(false, *Reason);
	}

	void FFigmaImportSession::HandleImagesResolved(const TSharedRef<FDesignDocument>& Document, const bool bImagesResolved)
	{
		if (IsCancelled())
		{
			return;
		}

		if (!bImagesResolved)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImport: Some image refs unresolved"));
		}

		FFigmaImageExporter::ExportVectorNodes(
			*Client,
			FileKey,
			Document,
			[Self = AsShared(), Document](const bool bVectorsExported, const TMap<FString, FString>& IdToUrl)
			{
				Self->HandleVectorsExported(Document, bVectorsExported, IdToUrl);
			});
	}

	void FFigmaImportSession::HandleVectorsExported(
		const TSharedRef<FDesignDocument>& Document,
		const bool bVectorsExported,
		const TMap<FString, FString>& IdToUrl)
	{
		if (IsCancelled())
		{
			return;
		}

		if (!bVectorsExported)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImport: Some vector nodes not exported"));
		}

		if (IdToUrl.Num() > 0)
		{
			FFigmaImageExporter::ApplyExportedImages(Document->Children, IdToUrl);
		}

		FDesignImportRequest ImportRequest = Design2UMGEditorImportRequestSetup::MakeImportRequest(
			Design2UMGEditorImportOrchestrator::DefaultFigmaOutputPath,
			LOCTEXT("FigmaPreviewTitle", "Design2UMG Figma Preview"),
			!FApp::IsUnattended(),
			EDesignBlueprintGenerationMode::BlueprintAndPng,
			SettingsSnapshot);

		// Single parse->convert->image-resolution pass: its result feeds the frame-count
		// gate/notification and is handed to Execute as the precomputed analysis.
		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = ImportRequest.bResolveImages;
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		const FImportAnalysisResult AnalysisResult = DesignImportPipeline::AnalyzeImport(*Document, AnalysisInput);
		if (!AnalysisResult.ErrorMessage.IsEmpty())
		{
			Design2UMGEditorImportSupport::ShowNotification(FText::FromString(AnalysisResult.ErrorMessage), SNotificationItem::CS_Fail);
			Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(false, TEXT("Font preparation failed"));
			return;
		}
		if (AnalysisResult.Data.RootWidget.Children.Num() == 0)
		{
			Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("FigmaNoFrames", "Design2UMG: No frames found in Figma file"), SNotificationItem::CS_Fail);
			Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma no frames; exiting with status 1"));
			return;
		}

		Design2UMGEditorImportSupport::ShowNotification(
			FText::Format(
				LOCTEXT("FigmaSuccess", "Design2UMG: Imported {0} frame(s) from Figma"),
				FText::AsNumber(AnalysisResult.Data.RootWidget.Children.Num())),
			SNotificationItem::CS_Success);

		const FDesignImportExecutionResult ExecutionResult =
			Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(*Document, ImportRequest, &AnalysisResult);

		Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(
			ExecutionResult.bSucceeded,
			TEXT("Unattended mode: figma import finished; exiting"));
	}
}

#undef LOCTEXT_NAMESPACE
