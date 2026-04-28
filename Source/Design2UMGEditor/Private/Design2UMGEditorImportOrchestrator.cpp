// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportOrchestrator.h"

#include "Design2UMGEditorDesignSourceRegistry.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGLog.h"
#include "DesignImportDialogs.h"
#include "DesignImportPipeline.h"
#include "DesignPathRasterizer.h"
#include "FigmaHttpClient.h"
#include "FigmaImageExporter.h"
#include "FigmaParser.h"
#include "FontResolver.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PenMCPBridge.h"
#include "PencilConnector.h"
#include "ProceduralShapeGenerator.h"
#include "Serialization/JsonSerializer.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"


#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	FDesignImportRequest MakeImportRequest(
		const FString& OutputPath,
		const FText& PreviewWindowTitle,
		const bool bUsePreview,
		const EDesignBlueprintGenerationMode GenerationMode,
		const bool bResolveImages = true)
	{
		FDesignImportRequest Request;
		Request.OutputPath = OutputPath;
		Request.PreviewWindowTitle = PreviewWindowTitle;
		Request.bUsePreview = bUsePreview;
		Request.bResolveImages = bResolveImages;
		Request.Generation.Mode = GenerationMode;
		return Request;
	}

	FDesignImportRequest MakePenImportRequest(const FString& OutputPath, const bool bUsePreview, const bool bRenderPng)
	{
		return MakeImportRequest(
			OutputPath,
			LOCTEXT("PenConvertPreviewWindowTitle", "Design2UMG Pen Preview"),
			bUsePreview,
			bRenderPng ? EDesignBlueprintGenerationMode::BlueprintAndPng : EDesignBlueprintGenerationMode::BlueprintOnly);
	}

	void RequestExitIfUnattended(const TCHAR* Message)
	{
		if (!FApp::IsUnattended())
		{
			return;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("%s"), Message);
		FPlatformMisc::RequestExit(false);
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
		FUMGWidgetData Data;
		if (!FPenMCPBridge::ConvertFromMCP(NodesJson, VarsJson, Data, TargetNodeId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ConnectToPencil: ConvertFromMCP failed"));
			return;
		}

		DesignImportPipeline::Execute(Data, MakePenImportRequest(OutputPath, true, true));
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

	void ImportPenFileWithFrameSelection(const Design2UMGEditorImportOrchestrator::FPenImportRequest& Request)
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
			[CapturedRequest = Request](const FString& FrameId)
			{
				DesignImportPipeline::ExecutePenFile(
					CapturedRequest.FilePath,
					FrameId,
					MakePenImportRequest(CapturedRequest.OutputPath, CapturedRequest.bUsePreview, CapturedRequest.bRenderPng));
			});
	}

	class FFigmaImportService;

	class FFigmaImportSession : public TSharedFromThis<FFigmaImportSession>
	{
	public:
		FFigmaImportSession(FFigmaImportService& InOwner, FString InFileKey, const FString& Token, const uint32 InGeneration)
			: Owner(InOwner)
			, FileKey(MoveTemp(InFileKey))
			, Generation(InGeneration)
			, Client(MakeUnique<FFigmaHttpClient>())
		{
			Client->SetToken(Token);
		}

		void Start();

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
	};

	class FFigmaImportService
	{
	public:
		void RunImport(const FString& FileKey, const FString& Token)
		{
			const uint32 ImportGeneration = static_cast<uint32>(Generation.GetValue());
			const TSharedRef<FFigmaImportSession> Session = MakeShared<FFigmaImportSession>(*this, FileKey, Token, ImportGeneration);
			Session->Start();
		}

		void Invalidate()
		{
			Generation.Increment();
		}

		bool IsGenerationCurrent(const uint32 Token) const
		{
			return static_cast<uint32>(Generation.GetValue()) == Token;
		}

	private:
		FThreadSafeCounter Generation{ 1 };
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

			Connector->GetClient().CallToolAsync(
				TEXT("batch_get"),
				BatchArgs,
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

					TSharedPtr<FJsonObject> VarsArgs = MakeShared<FJsonObject>();
					VarsArgs->SetStringField(TEXT("filePath"), TEXT(""));

					Connector->GetClient().CallToolAsync(
						TEXT("get_variables"),
						VarsArgs,
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
			RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma api request failed"));
			return;
		}

		FDesignDocument ParsedDocument;
		if (!FFigmaParser::ParseDocument(Body, ParsedDocument))
		{
			ShowNotification(LOCTEXT("FigmaParseFailed", "Design2UMG: Failed to parse Figma file"), SNotificationItem::CS_Fail);
			RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma parse failed"));
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

		const FImportAnalysisResult AnalysisResult = DesignImportPipeline::AnalyzeImport(*Document, FImportAnalysisInput{ false });
		if (AnalysisResult.Data.RootWidget.Children.Num() == 0)
		{
			ShowNotification(LOCTEXT("FigmaNoFrames", "Design2UMG: No frames found in Figma file"), SNotificationItem::CS_Fail);
			RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma no frames"));
			return;
		}

		ShowNotification(
			FText::Format(
				LOCTEXT("FigmaSuccess", "Design2UMG: Imported {0} frame(s) from Figma"),
				FText::AsNumber(AnalysisResult.Data.RootWidget.Children.Num())),
			SNotificationItem::CS_Success);

		DesignImportPipeline::Execute(
			*Document,
			MakeImportRequest(
				Design2UMGEditorImportOrchestrator::DefaultFigmaOutputPath,
				LOCTEXT("FigmaPreviewTitle", "Design2UMG Figma Preview"),
				!FApp::IsUnattended(),
				EDesignBlueprintGenerationMode::BlueprintAndPng));

		RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma import"));
	}
}

namespace Design2UMGEditorImportOrchestrator
{
	void RunPenConvert(const FPenConvertRequest& Request)
	{
		FString NodesContents;
		FString VarsContents;
		if (!LoadTextFileOrLog(Request.NodesPath, NodesContents) || !LoadTextFileOrLog(Request.VarsPath, VarsContents))
		{
			return;
		}

		FUMGWidgetData Data;
		if (!FPenMCPBridge::ConvertFromMCP(NodesContents, VarsContents, Data, Request.TargetNodeId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] PenConvert failed"));
			return;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("[OK] PenConvert succeeded, root children: %d"), Data.RootWidget.Children.Num());
		for (int32 Index = 0; Index < Data.RootWidget.Children.Num(); ++Index)
		{
			const FUMGWidgetNode& Child = Data.RootWidget.Children[Index];
			UE_LOG(LogDesign2UMG, Log, TEXT("  [%d] %s (%s)"), Index, *Child.Name, *Child.Type);
		}

		DesignImportPipeline::Execute(Data, MakePenImportRequest(Request.OutputPath, Request.bUsePreview, true));
		RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after pen convert"));
	}

	void RunPenImport(const FPenImportRequest& Request)
	{
		if (!FPaths::FileExists(Request.FilePath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("File not found: %s"), *Request.FilePath);
			return;
		}

		if (Request.TargetNodeId.IsEmpty())
		{
			ImportPenFileWithFrameSelection(Request);
		}
		else
		{
			DesignImportPipeline::ExecutePenFile(
				Request.FilePath,
				Request.TargetNodeId,
				MakePenImportRequest(Request.OutputPath, Request.bUsePreview, Request.bRenderPng));
		}

		RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after import"));
	}

	void RunRegisteredSourceImport(const FRegisteredSourceImportRequest& Request)
	{
		FString JsonContents;
		if (!LoadTextFileOrLog(Request.JsonPath, JsonContents))
		{
			return;
		}

		FUMGWidgetData Data;
		if (!Design2UMGEditorDesignSourceRegistry::TryConvertSource(Request.SourceId, JsonContents, Data))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] ImportSource convert failed for: %s"), *Request.SourceId.ToString());
			return;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("[OK] ImportSource succeeded (%s), root children: %d"), *Request.SourceId.ToString(), Data.RootWidget.Children.Num());

		DesignImportPipeline::Execute(
			Data,
			MakeImportRequest(
				Request.OutputPath,
				LOCTEXT("ImportSourcePreviewTitle", "Design2UMG Import Preview"),
				Request.bUsePreview,
				EDesignBlueprintGenerationMode::BlueprintAndPng));

		RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after import source"));
	}

	void RunFigmaImport(const FString& FileUrl, const FString& Token)
	{
		FString FileKey;
		if (!FFigmaParser::ExtractFileKey(FileUrl, FileKey))
		{
			ShowNotification(LOCTEXT("FigmaInvalidUrl", "Design2UMG: Invalid Figma URL or file key"), SNotificationItem::CS_Fail);
			RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma url validation failed"));
			return;
		}

		FImportOrchestratorTransientState::Get().GetFigmaImportService().RunImport(FileKey, Token);
	}

	void RunLocalFigmaImport(const FLocalFigmaImportRequest& Request)
	{
		if (!FPaths::FileExists(Request.JsonFilePath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("File not found: %s"), *Request.JsonFilePath);
			return;
		}

		FDesignDocument Document;
		FString ErrorMessage;
		if (!Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(Request.JsonFilePath, Document, ErrorMessage))
		{
			ShowNotification(LOCTEXT("LocalFigmaImportFailed", "Design2UMG: Failed to import local Figma export"), SNotificationItem::CS_Fail);
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *ErrorMessage);
			return;
		}

		const FString FinalOutputPath = Request.OutputPath.IsEmpty()
			? FString::Printf(TEXT("/Game/Design2UMG/Figma_%s"), *FDateTime::Now().ToString(TEXT("%m%d_%H%M%S")))
			: Request.OutputPath;

		DesignImportPipeline::Execute(
			Document,
			MakeImportRequest(
				FinalOutputPath,
				LOCTEXT("LocalFigmaPreviewTitle", "Design2UMG Local Figma Preview"),
				Request.bUsePreview && !FApp::IsUnattended(),
				EDesignBlueprintGenerationMode::BlueprintAndPng,
				false));

		RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after local figma import"));
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
