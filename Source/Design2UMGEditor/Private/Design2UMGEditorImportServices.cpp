// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportServices.h"

#include "DesignImportDefaults.h"
#include "Design2UMGEditorImportFrameSelection.h"
#include "Design2UMGEditorImportRequestSetup.h"
#include "Design2UMGEditorImportSupport.h"
#include "DesignImportPlanning.h"
#include "DesignImportDialogs.h"
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
	EFigmaImportCompletionKind ClassifyFigmaImportCompletion(
		const FUMGWidgetData& Data,
		const FDesignImageImportResult& ImageResult)
	{
		if (ImageResult.HasFailures())
		{
			return EFigmaImportCompletionKind::ImageFailure;
		}
		return Data.Warnings.IsEmpty()
			? EFigmaImportCompletionKind::Success
			: EFigmaImportCompletionKind::Warnings;
	}

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
				[this, FetchGeneration](const bool bLaunchOk)
				{
					if (!IsGenerationCurrent(FetchGeneration))
					{
						ReleaseFetchSlot();
						return;
					}

					if (!bLaunchOk)
					{
						UE_LOG(LogDesign2UMG, Error, TEXT("ConnectToPencil: %s"), *GetOrCreateConnector().GetClient().GetLastError());
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
			TEXT("get_app_state"),
			MakeShared<FJsonObject>(),
			[this, FetchGeneration](const bool bStateOk, const FString& StateResponse)
			{
				if (!IsGenerationCurrent(FetchGeneration))
				{
					ReleaseFetchSlot();
					return;
				}

				// A native path anchors relative resources; it is not the node source.
				FString SourceFilePath;
				if (bStateOk)
				{
					Design2UMGEditorMcpImportPayload::ParseActiveDocumentPath(StateResponse, SourceFilePath);
				}
				Connector->GetClient().CallToolAsync(
					TEXT("execute"),
					Design2UMGEditorImportRequestSetup::MakePencilReadDocumentArgs(),
					[this, FetchGeneration, SourceFilePath](const bool bReadOk, const FString& Response)
					{
						if (!IsGenerationCurrent(FetchGeneration))
						{
							ReleaseFetchSlot();
							return;
						}

						FString NodesJson, VarsJson;
						if (!bReadOk || !Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(Response, NodesJson, VarsJson))
						{
							UE_LOG(LogDesign2UMG, Error, TEXT("ConnectToPencil: Failed to read the active document via execute: %s"), *Response);
							Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("PencilReadFailed", "Design2UMG: Failed to read the active Pencil document"), SNotificationItem::CS_Fail);
							ReleaseFetchSlot();
							return;
						}

						Design2UMGEditorMcpImportPayload::HandleMCPDataReady(NodesJson, VarsJson, SourceFilePath);
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
			// Keep unresolved image references visible as a degraded import result.
			bImageResolutionFailed = true;
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

		OnResolvedDocument(Document, bImageResolutionFailed);
	}

	namespace
	{
		void ImportResolvedFigmaDocument(const TSharedRef<FDesignDocument>& Document,
			bool bImageResolutionFailed, const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
		{
			FDesignImportRequest ImportRequest = Design2UMGEditorImportRequestSetup::MakeImportRequest(
				DesignImportDefaults::FigmaOutputPath,
				LOCTEXT("FigmaPreviewTitle", "Design2UMG Figma Preview"),
				!FApp::IsUnattended(),
				EDesignBlueprintGenerationMode::BlueprintAndPng,
				SettingsSnapshot);

			// Single parse->convert->image-resolution pass: its result feeds the frame-count
			// gate/notification and is handed to Execute as the precomputed analysis.
			FImportAnalysisInput AnalysisInput;
			AnalysisInput.bResolveImages = ImportRequest.bResolveImages;
			AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
			FImportAnalysisResult AnalysisResult = ImportRequest.bUsePreview
				? DesignImportDialogs::AnalyzeForPreview(*Document, AnalysisInput)
				: DesignImportPlanning::AnalyzeImport(*Document, AnalysisInput);
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

			// Unresolved image references contribute to the final commit warning state.
			if (bImageResolutionFailed)
			{
				DesignImportPlanning::RecordImageResolutionFailure(AnalysisResult.ImageResult);
			}

			// Keep the notification reason aligned with the actual warning source. A conversion
			// warning remains visible, but it is not relabelled as an image failure.
			const EFigmaImportCompletionKind CompletionKind = ClassifyFigmaImportCompletion(
				AnalysisResult.Data,
				AnalysisResult.ImageResult);
			FText NotificationText;
			switch (CompletionKind)
			{
			case EFigmaImportCompletionKind::ImageFailure:
				NotificationText = FText::Format(
					LOCTEXT("FigmaImportImageFailure", "Design2UMG: Imported {0} frame(s) from Figma, but some images failed to resolve"),
					FText::AsNumber(AnalysisResult.Data.RootWidget.Children.Num()));
				break;
			case EFigmaImportCompletionKind::Warnings:
				NotificationText = FText::Format(
					LOCTEXT("FigmaImportWarnings", "Design2UMG: Imported {0} frame(s) from Figma with warnings; see import diagnostics"),
					FText::AsNumber(AnalysisResult.Data.RootWidget.Children.Num()));
				break;
			default:
				NotificationText = FText::Format(
					LOCTEXT("FigmaSuccess", "Design2UMG: Imported {0} frame(s) from Figma"),
					FText::AsNumber(AnalysisResult.Data.RootWidget.Children.Num()));
				break;
			}
			Design2UMGEditorImportSupport::ShowNotification(
				NotificationText,
				CompletionKind == EFigmaImportCompletionKind::Success
					? SNotificationItem::CS_Success
					: SNotificationItem::CS_None);

			const FDesignImportExecutionResult ExecutionResult =
				DesignImportPipeline::Execute(*Document, ImportRequest, &AnalysisResult);

			Design2UMGEditorUnattendedExit::ExitUnattendedWithStatus(
				ExecutionResult.bSucceeded,
				TEXT("Unattended mode: figma import finished; exiting"));
		}
	}

	void FFigmaImportService::RunImport(const FString& FileKey, const FString& Token,
		const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
	{
		Invalidate();
		const uint32 ImportGeneration = static_cast<uint32>(Generation.GetValue());
		const TSharedRef<FFigmaImportSession> Session = MakeShared<FFigmaImportSession>(
			*this, FileKey, Token, ImportGeneration, SettingsSnapshot,
			[SettingsSnapshot](const TSharedRef<FDesignDocument>& Document, bool bImageResolutionFailed)
			{
				ImportResolvedFigmaDocument(Document, bImageResolutionFailed, SettingsSnapshot);
			});
		ActiveSession = Session;
		Session->Start();
	}
}

#undef LOCTEXT_NAMESPACE
