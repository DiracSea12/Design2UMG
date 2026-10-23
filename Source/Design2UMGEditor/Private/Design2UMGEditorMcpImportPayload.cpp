// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorMcpImportPayload.h"

#include "Design2UMGEditorImportFrameSelection.h"
#include "DesignImportDefaults.h"
#include "Design2UMGEditorImportRequestSetup.h"
#include "Design2UMGEditorImportSupport.h"
#include "Design2UMGLog.h"
#include "DesignImportTypes.h"
#include "DesignImportPipeline.h"
#include "DesignSchema.h"
#include "DesignJsonUtils.h"
#include "ExternalServices/PenMCPBridge.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "FDesign2UMGEditorModule"

namespace
{
	TArray<FDesignFrameInfo> ExtractFramesFromMCPResult(const FString& NodesJson)
	{
		TArray<FDesignFrameInfo> Result;
		TArray<TSharedPtr<FJsonValue>> Array;
		if (!DesignJsonUtils::TryParseArray(NodesJson, Array))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ExtractFramesFromMCPResult: JSON deserialization failed (length=%d)"), NodesJson.Len());
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

	void ProcessMCPData(const FString& NodesJson, const FString& VarsJson, const FString& TargetNodeId, const FString& OutputPath, const FString& SourceFilePath)
	{
		const FDesign2UMGImportSettingsSnapshot SettingsSnapshot =
			Design2UMGEditorImportRequestSetup::CaptureSettingsSnapshot();
		FDesignDocument Document;
		if (!FPenMCPBridge::ParseFromMCP(NodesJson, VarsJson, Document, TargetNodeId, SourceFilePath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ConnectToPencil: Failed to parse the live document"));
			return;
		}

		FDesignImportRequest ImportRequest =
			Design2UMGEditorImportRequestSetup::MakePenImportRequest(OutputPath, true, true, SettingsSnapshot);
		DesignImportPipeline::Execute(Document, ImportRequest);
	}
}

namespace Design2UMGEditorMcpImportPayload
{
	bool ParseActiveDocumentPath(const FString& Response, FString& OutFilePath)
	{
		OutFilePath.Reset();
		const FString Prefix = TEXT("- Currently active canvas editor: `");
		TArray<FString> Lines;
		Response.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (!Line.StartsWith(Prefix, ESearchCase::CaseSensitive)) continue;
			const int32 End = Line.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Prefix.Len());
			if (End == INDEX_NONE) return false;

			FString FilePath = Line.Mid(Prefix.Len(), End - Prefix.Len());
			FPaths::NormalizeFilename(FilePath);
			if (FilePath.Len() >= 4 && FilePath[0] == TEXT('/') && FChar::IsAlpha(FilePath[1])
				&& FilePath[2] == TEXT(':') && FilePath[3] == TEXT('/'))
			{
				FilePath.RightChopInline(1);
			}
			if (FPaths::IsRelative(FilePath)
				|| !FPaths::GetExtension(FilePath).Equals(TEXT("pen"), ESearchCase::IgnoreCase)) return false;

			OutFilePath = MoveTemp(FilePath);
			return true;
		}
		return false;
	}

	bool ParseReadDocumentResponse(const FString& Response, FString& OutNodesJson, FString& OutVarsJson)
	{
		OutNodesJson.Reset();
		OutVarsJson.Reset();
		TArray<FString> Lines;
		Response.ParseIntoArrayLines(Lines);
		const FString Marker = TEXT("Design2UMGDocument:");
		for (const FString& Line : Lines)
		{
			if (!Line.StartsWith(Marker, ESearchCase::CaseSensitive)) continue;
			TSharedPtr<FJsonObject> Payload;
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			const TSharedPtr<FJsonObject>* Variables = nullptr;
			const TSharedPtr<FJsonObject>* Definitions = nullptr;
			if (!DesignJsonUtils::TryParseObject(Line.Mid(Marker.Len()), Payload)
				|| !Payload->TryGetArrayField(TEXT("nodes"), Nodes)
				|| !Payload->TryGetObjectField(TEXT("variables"), Variables)
				|| !(*Variables)->TryGetObjectField(TEXT("variables"), Definitions)) return false;
			return FJsonSerializer::Serialize(*Nodes, TJsonWriterFactory<>::Create(&OutNodesJson))
				&& FJsonSerializer::Serialize(Variables->ToSharedRef(), TJsonWriterFactory<>::Create(&OutVarsJson));
		}
		return false;
	}

	void HandleMCPDataReady(const FString& NodesJson, const FString& VarsJson, const FString& SourceFilePath)
	{
		const TArray<FDesignFrameInfo> Frames = ExtractFramesFromMCPResult(NodesJson);
		if (Frames.Num() == 0)
		{
			Design2UMGEditorImportSupport::ShowNotification(LOCTEXT("NoFrames", "Design2UMG: No frames found in Pencil document"), SNotificationItem::CS_Fail);
			return;
		}

		const FString SingleRootId = Frames.Num() == 1 ? Frames[0].Id : FString();
		Design2UMGEditorImportFrameSelection::ExecuteFrameImport(
			Frames,
			LOCTEXT("PencilFrameSelectorTitle", "Select Frame from Pencil"),
			LOCTEXT("PencilFrameSelectorHint", "Multiple frames found. Double-click to import:"),
			[CapturedNodes = NodesJson, CapturedVars = VarsJson, SourceFilePath, SingleRootId](const FString& FrameId)
			{
				ProcessMCPData(CapturedNodes, CapturedVars, FrameId.IsEmpty() ? SingleRootId : FrameId,
					DesignImportDefaults::PenOutputPath, SourceFilePath);
			});
	}
}

#undef LOCTEXT_NAMESPACE
