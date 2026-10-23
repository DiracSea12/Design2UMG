// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportDialogs.h"

#include "Design2UMGLog.h"
#include "SDesign2UMGPreviewWindow.h"
#include "DesignImportTypes.h"
#include "DesignImportDefaults.h"
#include "UMGSchema.h"
#include "UMGSchemaSourceExtensions.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "UMGJsonSerializer.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"

namespace
{
	FString ResolveDialogOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(DesignImportDefaults::PenOutputPath) : OutputPath;
	}
}

namespace DesignImportDialogs
{
	FString FormatImportResult(const FUMGWidgetData& Data, const FString& ErrorMessage, const bool bSucceeded, const FString& SourceFilePath)
	{
		const FString& FilePath = SourceFilePath.IsEmpty() ? Data.SourceDocument().SourceFilePath : SourceFilePath;
		FString Message = bSucceeded ? TEXT("Design2UMG：导入已完成，但发现以下问题。") : TEXT("Design2UMG：导入未完成。");
		if (!FilePath.IsEmpty()) Message += TEXT("\n文件：") + FilePath;
		const FUMGSourceNodeData& Source = Data.RootWidget.Source();
		const FString& NodeName = Source.SourceNodeName.IsEmpty() ? Data.RootWidget.Name : Source.SourceNodeName;
		const FString& NodeType = Source.SourceNodeType.IsEmpty() ? Data.RootWidget.Type : Source.SourceNodeType;
		Message += FString::Printf(TEXT("\n根节点：%s (%s), %s, %.3g × %.3g\n原因：%s"),
			*NodeName, *Source.SourceNodeId, *NodeType, Data.RootWidget.Bounds.W, Data.RootWidget.Bounds.H,
			ErrorMessage.IsEmpty() ? (bSucceeded ? TEXT("请检查以下导入诊断。") : TEXT("生成未完成，具体原因请查看输出日志。")) : *ErrorMessage);
		for (const FString& Warning : Data.Warnings)
		{
			if (!Message.Contains(Warning)) Message += TEXT("\n已有诊断：") + Warning;
		}
		return Message;
	}

	FString FormatImageIssues(const FDesignImageImportResult& Result)
	{
		FString Message = FString::Printf(TEXT("%d 张图片资源处理失败："), Result.FailedCount);
		for (const FDesignImageDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Outcome != EDesignImageDiagnosticOutcome::Failed) continue;
			Message += FString::Printf(TEXT("\n节点：%s (%s)\n资源：%s\n原因：%s"),
				*Diagnostic.NodeName, *Diagnostic.NodeId, *Diagnostic.Resource,
				Diagnostic.Reason.IsEmpty() ? *Diagnostic.Message : *Diagnostic.Reason);
		}
		return Message;
	}

	void ShowImportProblem(const FString& Message)
	{
		if (!FApp::IsUnattended()) FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
	}

	bool OpenPreviewWindow(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FString& SourceFilePath,
		const FText& WindowTitle,
		FOnPreviewConfirmed OnConfirmed,
		FOnPreviewCancelled OnCancelled,
		const TOptional<FDesignPreviewMaterializationSettingsSnapshot>& InitialMaterializationSettingsSnapshot,
		const FDesignImageImportResult& InitialImageResult)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(WindowTitle)
			.ClientSize(FVector2D(1600.0f, 980.0f))
			.SupportsMinimize(false)
			.SupportsMaximize(true);

		Window->SetContent(
			SNew(SDesign2UMGPreviewWindow)
			.OriginalJson(FUMGJsonSerializer::Serialize(Data))
			.OriginalData(MakeShared<FUMGWidgetData>(Data))
			.InitialImageResult(InitialImageResult)
			.OutputPath(ResolveDialogOutputPath(OutputPath))
			.SourceFilePath(SourceFilePath)
			.Warnings(Data.Warnings)
			.InitialMaterializationSettingsSnapshot(InitialMaterializationSettingsSnapshot)
			.OnConfirmed(MoveTemp(OnConfirmed))
			.OnCancelled(MoveTemp(OnCancelled)));

		FSlateApplication::Get().AddWindow(Window);
		return true;
	}

	void ShowFrameSelector(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		FFrameSelectedCallback OnFrameSelected)
	{
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		TSharedPtr<TArray<TSharedPtr<FDesignFrameInfo>>> Items = MakeShared<TArray<TSharedPtr<FDesignFrameInfo>>>();
		for (const FDesignFrameInfo& Frame : Frames)
		{
			Items->Add(MakeShared<FDesignFrameInfo>(Frame));
		}

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(WindowTitle)
			.ClientSize(FVector2D(420.0f, 360.0f))
			.SupportsMinimize(false)
			.SupportsMaximize(false);

		TWeakPtr<SWindow> WeakWindow = Window;
		TSharedPtr<FFrameSelectedCallback> Callback = MakeShared<FFrameSelectedCallback>(MoveTemp(OnFrameSelected));

		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f)
			[
				SNew(STextBlock)
				.Text(HintText)
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8.0f)
			[
				SNew(SListView<TSharedPtr<FDesignFrameInfo>>)
				.ListItemsSource(Items.Get())
				.OnGenerateRow_Lambda([Items](TSharedPtr<FDesignFrameInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
				{
					return SNew(STableRow<TSharedPtr<FDesignFrameInfo>>, OwnerTable)
						.Padding(FMargin(4.0f, 2.0f))
						[
							SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(
								TEXT("%s  (%.0f x %.0f)"),
								*Item->Name, Item->Width, Item->Height)))
						];
				})
				.OnMouseButtonDoubleClick_Lambda([Callback, WeakWindow](TSharedPtr<FDesignFrameInfo> Item)
				{
					if (Item.IsValid() && *Callback)
					{
						(*Callback)(Item->Id);
						if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
						{
							Win->RequestDestroyWindow();
						}
					}
				})
			]);

		FSlateApplication::Get().AddWindow(Window);
	}
}
