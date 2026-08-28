// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportOrchestrator.h"

#include "Design2UMGEditorDesignSourceRegistry.h"
#include "Design2UMGEditorImportRequestSetup.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGLog.h"
#include "DesignImportDialogs.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "DesignPathRasterizer.h"
#include "FigmaHttpClient.h"
#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FontResolver.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ExternalServices/PenMCPBridge.h"
#include "PencilConnector.h"
#include "ProceduralShapeGenerator.h"
#include "Serialization/JsonSerializer.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"


#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	/**
	 * Unattended exit policy lives at the command/orchestration boundary and covers
	 * every Run* entry point in this file (PenConvert/PenImport/ImportSource/
	 * FigmaImport/LocalFigmaImport/DesignReimport, including their async Figma callbacks):
	 * business import functions report success/failure, and this single helper
	 * turns the final outcome into an engine exit status (0 = ok, 1 = failed).
	 * Attended sessions are untouched - it returns false and the editor keeps running.
	 * Automation test sessions are untouched as well: FApp::IsUnattended() counts
	 * GIsAutomationTesting as unattended, but there a RequestExit from a command would
	 * kill the editor before the framework reports results (TEST COMPLETE); their
	 * lifecycle is owned by -TestExit. Returns true when an exit was requested
	 * (genuinely unattended mode only).
	 */
	bool ExitUnattendedWithStatus(const bool bSuccess, const TCHAR* Message)
	{
		if (GIsAutomationTesting || !FApp::IsUnattended())
		{
			return false;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("%s"), Message);
		// Force=false only reaches PostQuitMessage, which the editor main loop does not
		// forward as the process exit code (a failed import still exits 0). Force=true
		// makes the engine flush the log and TerminateProcess with the real status.
		// Failure forces the exit; success keeps the graceful shutdown path.
		FPlatformMisc::RequestExitWithStatus(!bSuccess, bSuccess ? 0 : 1);
		return true;
	}

	// Unattended sessions must never open an interactive preview: the preview
	// path reports success as soon as the window opens, so exiting right after
	// would end the process before any asset is actually generated.
	bool ShouldUsePreviewInCurrentMode(const bool bRequestedPreview)
	{
		return bRequestedPreview && !FApp::IsUnattended();
	}

	bool LoadTextFileOrLog(const FString& FilePath, FString& OutContents)
	{
		if (FFileHelper::LoadFileToString(OutContents, *FilePath))
		{
			return true;
		}

		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read file: %s"), *FilePath);
		return false;
	}

	void ShowNotification(const FText& Text, const SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(State);
		}
	}

	void ExecuteFrameImport(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		TFunction<void(const FString&)> ImportFrame)
	{
		if (Frames.Num() == 1)
		{
			ImportFrame(FString());
			return;
		}

		DesignImportDialogs::ShowFrameSelector(
			Frames,
			WindowTitle,
			HintText,
			[ImportFrame = MoveTemp(ImportFrame)](const FString& FrameId) mutable
			{
				ImportFrame(FrameId);
			});
	}

	TArray<FDesignFrameInfo> ExtractFramesFromMCPResult(const FString& NodesJson)
	{
		TArray<FDesignFrameInfo> Result;
		TArray<TSharedPtr<FJsonValue>> Array;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodesJson);
		if (!FJsonSerializer::Deserialize(Reader, Array))
		{
			return Result;
		}

		for (const TSharedPtr<FJsonValue>& Value : Array)
		{
			const TSharedPtr<FJsonObject>& Object = Value->AsObject();
			if (!Object.IsValid())
			{
				continue;
			}

			FDesignFrameInfo Info;
			Object->TryGetStringField(TEXT("id"), Info.Id);
			Object->TryGetStringField(TEXT("name"), Info.Name);
			if (Info.Name.IsEmpty())
			{
				Info.Name = Info.Id;
			}

			double Width = 0.0;
			double Height = 0.0;
			Object->TryGetNumberField(TEXT("width"), Width);
			Object->TryGetNumberField(TEXT("height"), Height);
			Info.Width = static_cast<float>(Width);
			Info.Height = static_cast<float>(Height);
			Result.Add(MoveTemp(Info));
		}

		return Result;
	}

	void ProcessMCPData(const FString& NodesJson, const FString& VarsJson, const FString& TargetNodeId, const FString& OutputPath)
	{
		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
		FUMGWidgetData Data;
		if (!FPenMCPBridge::ConvertFromMCP(NodesJson, VarsJson, SettingsSnapshot, Data, TargetNodeId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ConnectToPencil: ConvertFromMCP failed"));
			return;
		}

		FDesignImportRequest ImportRequest =
			Design2UMGEditorImportRequestSetup::MakePenImportRequest(OutputPath, true, true, SettingsSnapshot);
		Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(Data, ImportRequest);
	}

	void HandleMCPDataReady(const FString& NodesJson, const FString& VarsJson)
	{
		const TArray<FDesignFrameInfo> Frames = ExtractFramesFromMCPResult(NodesJson);
		if (Frames.Num() == 0)
		{
			ShowNotification(LOCTEXT("NoFrames", "Design2UMG: No frames found in Pencil document"), SNotificationItem::CS_Fail);
			return;
		}

		ExecuteFrameImport(
			Frames,
			LOCTEXT("PencilFrameSelectorTitle", "Select Frame from Pencil"),
			LOCTEXT("PencilFrameSelectorHint", "Multiple frames found. Double-click to import:"),
			[CapturedNodes = NodesJson, CapturedVars = VarsJson](const FString& FrameId)
			{
				ProcessMCPData(CapturedNodes, CapturedVars, FrameId, Design2UMGEditorImportOrchestrator::DefaultPenOutputPath);
			});
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

		ExecuteFrameImport(
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

	class FFigmaImportService;

	class FFigmaImportSession : public TSharedFromThis<FFigmaImportSession>
	{
	public:
		FFigmaImportSession(
			FFigmaImportService& InOwner,
			FString InFileKey,
			const FString& Token,
			const uint32 InGeneration,
			const FDesign2UMGImportSettingsSnapshot& InSettingsSnapshot)
			: Owner(InOwner)
			, FileKey(MoveTemp(InFileKey))
			, Generation(InGeneration)
			, Client(MakeUnique<FFigmaHttpClient>())
			, SettingsSnapshot(InSettingsSnapshot)
		{
			Client->SetToken(Token);
		}

		~FFigmaImportSession()
		{
			CancelRequests();
		}

		void Start();
		void CancelRequests();

	private:
		void HandleFileResponse(const bool bSuccess, const FString& Body);
		void HandleImagesResolved(const TSharedRef<FDesignDocument>& Document, const bool bImagesResolved);
		void HandleVectorsExported(
			const TSharedRef<FDesignDocument>& Document,
			const bool bVectorsExported,
			const TMap<FString, FString>& IdToUrl);
		bool IsCancelled() const;

		FFigmaImportService& Owner;
		FString FileKey;
		uint32 Generation = 0;
		TUniquePtr<FFigmaHttpClient> Client;
		FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
	};

	class FFigmaImportService
	{
	public:
		void RunImport(
			const FString& FileKey,
			const FString& Token,
			const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
		{
			Invalidate();
			const uint32 ImportGeneration = static_cast<uint32>(Generation.GetValue());
			const TSharedRef<FFigmaImportSession> Session = MakeShared<FFigmaImportSession>(
				*this,
				FileKey,
				Token,
				ImportGeneration,
				SettingsSnapshot);
			ActiveSession = Session;
			Session->Start();
		}

		void Invalidate()
		{
			Generation.Increment();
			if (ActiveSession.IsValid())
			{
				ActiveSession.Pin()->CancelRequests();
				ActiveSession.Reset();
			}
		}

		bool IsGenerationCurrent(const uint32 Token) const
		{
			return static_cast<uint32>(Generation.GetValue()) == Token;
		}

	private:
		FThreadSafeCounter Generation{ 1 };
		TWeakPtr<FFigmaImportSession> ActiveSession;
	};

	class FPencilImportService
	{
	public:
		void ConnectAndImport()
		{
			if (FPlatformAtomics::InterlockedCompareExchange(&bFetchInProgress, 1, 0) != 0)
			{
				return;
			}

			const uint32 FetchGeneration = static_cast<uint32>(Generation.GetValue());
			FPencilConnector& ActiveConnector = GetOrCreateConnector();
			if (!ActiveConnector.IsRunning())
			{
				ShowNotification(LOCTEXT("LaunchingMCP", "Design2UMG: Launching MCP Server..."), SNotificationItem::CS_Pending);
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
							ShowNotification(LOCTEXT("LaunchFailed", "Design2UMG: Failed to launch MCP Server"), SNotificationItem::CS_Fail);
							ReleaseFetchSlot();
							return;
						}

						BeginFetch(FetchGeneration);
					});
				return;
			}

			BeginFetch(FetchGeneration);
		}

		void Shutdown()
		{
			Generation.Increment();
			ReleaseFetchSlot();

			if (Connector)
			{
				Connector->GetClient().CancelRequests();
				Connector->Shutdown();
				Connector.Reset();
			}
		}

	private:
		void BeginFetch(const uint32 FetchGeneration)
		{
			if (!IsGenerationCurrent(FetchGeneration) || !Connector)
			{
				ReleaseFetchSlot();
				return;
			}

			ShowNotification(LOCTEXT("Connecting", "Design2UMG: Fetching design from Pencil..."), SNotificationItem::CS_Pending);

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
						ShowNotification(LOCTEXT("BatchGetFailed", "Design2UMG: batch_get failed"), SNotificationItem::CS_Fail);
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

							HandleMCPDataReady(CapturedNodes, FinalVars);
							ReleaseFetchSlot();
						});
				});
		}

		FPencilConnector& GetOrCreateConnector()
		{
			if (!Connector)
			{
				Connector = MakeUnique<FPencilConnector>();
			}

			return *Connector;
		}

		bool IsGenerationCurrent(const uint32 Token) const
		{
			return static_cast<uint32>(Generation.GetValue()) == Token;
		}

		void ReleaseFetchSlot()
		{
			FPlatformAtomics::InterlockedExchange(&bFetchInProgress, 0);
		}

		TUniquePtr<FPencilConnector> Connector;
		volatile int32 bFetchInProgress{ 0 };
		FThreadSafeCounter Generation{ 1 };
	};

	class FImportOrchestratorTransientState
	{
	public:
		static FImportOrchestratorTransientState& Get()
		{
			static FImportOrchestratorTransientState State;
			return State;
		}

		FFigmaImportService& GetFigmaImportService()
		{
			return FigmaService;
		}

		FPencilImportService& GetPencilImportService()
		{
			return PencilService;
		}

		void Shutdown()
		{
			FigmaService.Invalidate();
			PencilService.Shutdown();
		}

	private:
		FFigmaImportService FigmaService;
		FPencilImportService PencilService;
	};

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
		ShowNotification(LOCTEXT("FigmaFetching", "Design2UMG: Fetching design from Figma..."), SNotificationItem::CS_Pending);

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
			ShowNotification(LOCTEXT("FigmaGetFileFailed", "Design2UMG: Figma API request failed"), SNotificationItem::CS_Fail);
			ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma api request failed; exiting with status 1"));
			return;
		}

		FDesignDocument ParsedDocument;
		if (!Design2UMGEditorDesignSourceRegistry::TryParseSourceDocument(TEXT("Figma"), Body, ParsedDocument))
		{
			ShowNotification(LOCTEXT("FigmaParseFailed", "Design2UMG: Failed to parse Figma file"), SNotificationItem::CS_Fail);
			ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma parse failed; exiting with status 1"));
			return;
		}

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

		FImportAnalysisInput AnalysisInput;
		AnalysisInput.bResolveImages = false;
		AnalysisInput.SettingsSnapshot = &SettingsSnapshot;
		const FImportAnalysisResult AnalysisResult = DesignImportPipeline::AnalyzeImport(*Document, AnalysisInput);
		if (AnalysisResult.Data.RootWidget.Children.Num() == 0)
		{
			ShowNotification(LOCTEXT("FigmaNoFrames", "Design2UMG: No frames found in Figma file"), SNotificationItem::CS_Fail);
			ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma no frames; exiting with status 1"));
			return;
		}

		ShowNotification(
			FText::Format(
				LOCTEXT("FigmaSuccess", "Design2UMG: Imported {0} frame(s) from Figma"),
				FText::AsNumber(AnalysisResult.Data.RootWidget.Children.Num())),
			SNotificationItem::CS_Success);

		FDesignImportRequest ImportRequest = Design2UMGEditorImportRequestSetup::MakeImportRequest(
			Design2UMGEditorImportOrchestrator::DefaultFigmaOutputPath,
			LOCTEXT("FigmaPreviewTitle", "Design2UMG Figma Preview"),
			!FApp::IsUnattended(),
			EDesignBlueprintGenerationMode::BlueprintAndPng,
			SettingsSnapshot);
		const FDesignImportExecutionResult ExecutionResult =
			Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(*Document, ImportRequest);

		ExitUnattendedWithStatus(
			ExecutionResult.bSucceeded,
			TEXT("Unattended mode: figma import finished; exiting"));
	}
}

namespace Design2UMGEditorImportOrchestrator
{
	void RunPenConvert(const FPenConvertRequest& Request)
	{
		FString NodesContents;
		FString VarsContents;
		bool bOk = true;
		if (!LoadTextFileOrLog(Request.NodesPath, NodesContents) || !LoadTextFileOrLog(Request.VarsPath, VarsContents))
		{
			bOk = false;
		}

		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
		FUMGWidgetData Data;
		if (bOk && !FPenMCPBridge::ConvertFromMCP(NodesContents, VarsContents, SettingsSnapshot, Data, Request.TargetNodeId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] PenConvert failed"));
			bOk = false;
		}

		if (bOk)
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("[OK] PenConvert succeeded, root children: %d"), Data.RootWidget.Children.Num());
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
					Request.OutputPath, ShouldUsePreviewInCurrentMode(Request.bUsePreview), true, SettingsSnapshot))
			: FDesignImportExecutionResult();

		ExitUnattendedWithStatus(
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
						ShouldUsePreviewInCurrentMode(Request.bUsePreview),
						Request.bRenderPng,
						SettingsSnapshot));
				bOk = ExecutionResult.bSucceeded;
			}
		}

		ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: pen import finished; exiting"));
	}

	void RunRegisteredSourceImport(const FRegisteredSourceImportRequest& Request)
	{
		bool bOk = true;

		FString JsonContents;
		if (!LoadTextFileOrLog(Request.JsonPath, JsonContents))
		{
			bOk = false;
		}

		FUMGWidgetData Data;
		if (bOk)
		{
			const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
				Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
			if (!Design2UMGEditorDesignSourceRegistry::TryConvertSource(
				Request.SourceId,
				JsonContents,
				SettingsSnapshot,
				Data))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] ImportSource convert failed for: %s"), *Request.SourceId.ToString());
				bOk = false;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("[OK] ImportSource succeeded (%s), root children: %d"), *Request.SourceId.ToString(), Data.RootWidget.Children.Num());

				FDesignImportRequest ImportRequest = Design2UMGEditorImportRequestSetup::MakeImportRequest(
					Request.OutputPath,
					LOCTEXT("ImportSourcePreviewTitle", "Design2UMG Import Preview"),
					ShouldUsePreviewInCurrentMode(Request.bUsePreview),
					EDesignBlueprintGenerationMode::BlueprintAndPng,
					SettingsSnapshot);
				const FDesignImportExecutionResult ExecutionResult =
					Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(Data, ImportRequest);
				bOk = ExecutionResult.bSucceeded;
			}
		}

		ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: import source finished; exiting"));
	}

	void RunFigmaImport(const FString& FileUrl, const FString& Token)
	{
		FString FileKey;
		if (!FFigmaParser::ExtractFileKey(FileUrl, FileKey))
		{
			ShowNotification(LOCTEXT("FigmaInvalidUrl", "Design2UMG: Invalid Figma URL or file key"), SNotificationItem::CS_Fail);
			ExitUnattendedWithStatus(false, TEXT("Unattended mode: figma url validation failed; exiting with status 1"));
			return;
		}

		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
		FImportOrchestratorTransientState::Get().GetFigmaImportService().RunImport(FileKey, Token, SettingsSnapshot);
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
			ShowNotification(LOCTEXT("LocalFigmaImportFailed", "Design2UMG: Failed to import local Figma export"), SNotificationItem::CS_Fail);
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
				ShouldUsePreviewInCurrentMode(Request.bUsePreview),
				EDesignBlueprintGenerationMode::BlueprintAndPng,
				SettingsSnapshot,
				false);
			bOk = Design2UMGEditorImportRequestSetup::ExecuteImportWithSettings(Document, ImportRequest).bSucceeded;
		}

		ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: local figma import finished; exiting"));
	}

	// Design2UMG.Reimport 控制台命令入口：reimport 的菜单 UI（OpenReimportDialog）之外
	// 的命令行路径。只做参数映射薄包装：加载新源 design.json -> DesignImportPipeline::
	// Reimport；逐节点诊断与总结（reimport_*）由 pipeline 内部打到 LogDesign2UMG，
	// 这里补一条命令级结论（成功 / 全量冲突已生成并列副本 / 未生效），与菜单入口的
	// Notification 三个分支一一对应。
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

			if (Result.bSucceeded)
			{
				bOk = true;
				UE_LOG(LogDesign2UMG, Log, TEXT("[OK] Reimport applied: %s"), *TargetAssetPath);
			}
			else if (Result.bFullConflict && !Result.ResolutionCopyPath.IsEmpty())
			{
				bOk = true;
				UE_LOG(LogDesign2UMG, Log, TEXT("[OK] Resolution copy generated: %s"), *Result.ResolutionCopyPath);
			}
			else
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] Reimport not applied; asset keeps its previous state: %s"), *TargetAssetPath);
			}
		}

		ExitUnattendedWithStatus(
			bOk,
			TEXT("Unattended mode: design reimport finished; exiting"));
	}

	void ConnectToPencilAndImport()
	{
		FImportOrchestratorTransientState::Get().GetPencilImportService().ConnectAndImport();
	}

	void ShutdownTransientState()
	{
		FImportOrchestratorTransientState::Get().Shutdown();
		FFontResolver::ClearCache();
		FDesignPathRasterizer::ClearCache();
		FProceduralShapeGenerator::ReleaseTransientTextures();
	}
}

#undef LOCTEXT_NAMESPACE
