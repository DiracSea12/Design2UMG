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

#include <atomic>

#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	TUniquePtr<FPencilConnector> GConnector;
	TUniquePtr<FFigmaHttpClient> GFigmaClient;
	std::atomic<bool> GFetchInProgress{ false };

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

		if (!GFigmaClient)
		{
			GFigmaClient = MakeUnique<FFigmaHttpClient>();
		}
		GFigmaClient->SetToken(Token);

		ShowNotification(LOCTEXT("FigmaFetching", "Design2UMG: Fetching design from Figma..."), SNotificationItem::CS_Pending);

		GFigmaClient->GetFile(
			FileKey,
			[FileKey](const bool bSuccess, const FString& Body)
			{
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

				TSharedRef<FDesignDocument> Document = MakeShared<FDesignDocument>(MoveTemp(ParsedDocument));
				if (!GFigmaClient)
				{
					return;
				}

				FFigmaImageExporter::ResolveImageRefs(
					*GFigmaClient,
					FileKey,
					Document,
					[Document, FileKey](const bool bImagesResolved)
					{
						if (!bImagesResolved)
						{
							UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaImport: Some image refs unresolved"));
						}

						if (!GFigmaClient)
						{
							return;
						}

						FFigmaImageExporter::ExportVectorNodes(
							*GFigmaClient,
							FileKey,
							Document,
							[Document](const bool bVectorsExported, const TMap<FString, FString>& IdToUrl)
							{
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
										DefaultFigmaOutputPath,
										LOCTEXT("FigmaPreviewTitle", "Design2UMG Figma Preview"),
										!FApp::IsUnattended(),
										EDesignBlueprintGenerationMode::BlueprintAndPng));

								RequestExitIfUnattended(TEXT("Unattended mode: requesting engine exit after figma import"));
							});
					});
			});
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
		bool bExpected = false;
		if (!GFetchInProgress.compare_exchange_strong(bExpected, true))
		{
			return;
		}

		if (!GConnector)
		{
			GConnector = MakeUnique<FPencilConnector>();
		}

		const auto BeginFetch = []()
		{
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

			GConnector->GetClient().CallToolAsync(
				TEXT("batch_get"),
				BatchArgs,
				[](const bool bBatchOk, const FString& NodesJson)
				{
					if (!bBatchOk)
					{
						ShowNotification(LOCTEXT("BatchGetFailed", "Design2UMG: batch_get failed"), SNotificationItem::CS_Fail);
						GFetchInProgress = false;
						return;
					}

					if (!GConnector)
					{
						GFetchInProgress = false;
						return;
					}

					TSharedPtr<FJsonObject> VarsArgs = MakeShared<FJsonObject>();
					VarsArgs->SetStringField(TEXT("filePath"), TEXT(""));

					GConnector->GetClient().CallToolAsync(
						TEXT("get_variables"),
						VarsArgs,
						[CapturedNodes = NodesJson](const bool bVarsOk, const FString& VarsJson)
						{
							const FString FinalVars = bVarsOk ? VarsJson : TEXT("{}");
							if (!bVarsOk)
							{
								UE_LOG(LogDesign2UMG, Warning, TEXT("ConnectToPencil: get_variables failed, using empty"));
							}

							HandleMCPDataReady(CapturedNodes, FinalVars);
							GFetchInProgress = false;
						});
				});
		};

		if (!GConnector->IsRunning())
		{
			ShowNotification(LOCTEXT("LaunchingMCP", "Design2UMG: Launching MCP Server..."), SNotificationItem::CS_Pending);
			GConnector->LaunchAsync(
				0,
				[BeginFetch](const bool bLaunchOk)
				{
					if (!bLaunchOk)
					{
						ShowNotification(LOCTEXT("LaunchFailed", "Design2UMG: Failed to launch MCP Server"), SNotificationItem::CS_Fail);
						GFetchInProgress = false;
						return;
					}

					BeginFetch();
				});
			return;
		}

		BeginFetch();
	}

	void ShutdownTransientState()
	{
		if (GConnector)
		{
			GConnector->Shutdown();
			GConnector.Reset();
		}

		GFigmaClient.Reset();
		FFontResolver::ClearCache();
		FDesignPathRasterizer::ClearCache();
		FProceduralShapeGenerator::ReleaseTransientTextures();
	}
}

#undef LOCTEXT_NAMESPACE
