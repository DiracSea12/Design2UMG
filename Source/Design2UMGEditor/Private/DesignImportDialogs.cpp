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
#include "SPencilFontTool.h"
#include "DesignImportPlanning.h"
#include "DesignImportPipeline.h"
#include "DesignReferenceExpansion.h"
#include "DesignSchemaSourceExtensions.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SNullWidget.h"
#include "HttpModule.h"
#include "HttpManager.h"

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
	FImportAnalysisResult AnalyzeForPreview(FDesignDocument& Document, const FImportAnalysisInput& Input)
	{
		if (FApp::IsUnattended() || !FSlateApplication::IsInitialized())
			return DesignImportPlanning::AnalyzeImport(Document, Input);

		auto Analyze = [&Document, &Input]()
		{
			// Metadata is owned by the copy constructor; nodes must be copied separately.
			FDesignDocument Working = Document;
			for (auto& Child : Working.Children) Child = RefExpander::DeepCopyNode(Child);
			if (Working.HasSourceDocumentData())
				for (auto& Imported : Working.MutableSourceDocument().ImportedNodes) Imported = RefExpander::DeepCopyNode(Imported);
			FImportAnalysisResult Analysis = DesignImportPlanning::AnalyzeImport(Working, Input);
			if (Analysis.ErrorMessage.IsEmpty()) Document = MoveTemp(Working);
			return Analysis;
		};
		FImportAnalysisResult Result = Analyze();
		if (Result.ErrorMessage.IsEmpty()) return Result;
		FPencilFontToolText Texts;
		FText Status = FText::FromString(Result.ErrorMessage + Texts.Get(TEXT("RemedyHint")));
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(FText::FromString(Texts.Get(TEXT("RemedyTitle"))))
			.ClientSize(FVector2D(760, 360)).SupportsMinimize(false).SupportsMaximize(false);
		TSharedPtr<SPencilFontTool> Tool;
		Window->SetContent(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(16)
			[ SAssignNew(Tool, SPencilFontTool) ]
			+ SVerticalBox::Slot().FillHeight(1).Padding(16, 0)
			[ SNew(STextBlock).AutoWrapText(true).Text_Lambda([&Status]() { return Status; }) ]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(16)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[ SNew(SButton).Text(FText::FromString(Texts.Get(TEXT("RemedyCancel"))))
					.OnClicked_Lambda([Window, Tool]() { Tool->CancelInstall(); Window->RequestDestroyWindow(); return FReply::Handled(); }) ]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
				[ SNew(SButton).Text(FText::FromString(Texts.Get(TEXT("RemedyRetry"))))
					.IsEnabled_Lambda([]() { return !PencilFontTool::IsBusy(); })
					.OnClicked_Lambda([&Analyze, &Result, &Status, Window]()
					{
						Result = Analyze();
						if (Result.ErrorMessage.IsEmpty())
						{
							Window->RequestDestroyWindow();
						}
						else Status = FText::FromString(Result.ErrorMessage + FPencilFontToolText().Get(TEXT("RemedyRetryHint")));
						return FReply::Handled();
					}) ]
			]);
		Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([Tool](const TSharedRef<SWindow>&) { Tool->CancelInstall(); }));
		// Slate's modal loop pauses the engine tick; keep HTTP completion/cancellation moving.
		const FDelegateHandle HttpTick = FSlateApplication::Get().GetOnModalLoopTickEvent().AddLambda(
			[](float Delta) { FHttpModule::Get().GetHttpManager().Tick(Delta); });
		FSlateApplication::Get().AddModalWindow(Window, nullptr);
		FSlateApplication::Get().GetOnModalLoopTickEvent().Remove(HttpTick);
		// Release callbacks capturing the modal stack before returning.
		Window->SetContent(SNullWidget::NullWidget);
		Window->SetOnWindowClosed(FOnWindowClosed());
		return Result;
	}

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

		TSharedRef<SDesign2UMGPreviewWindow> Preview = SNew(SDesign2UMGPreviewWindow)
			.OriginalJson(FUMGJsonSerializer::Serialize(Data))
			.OriginalData(MakeShared<FUMGWidgetData>(Data))
			.InitialImageResult(InitialImageResult)
			.OutputPath(ResolveDialogOutputPath(OutputPath))
			.SourceFilePath(SourceFilePath)
			.Warnings(Data.Warnings)
			.InitialMaterializationSettingsSnapshot(InitialMaterializationSettingsSnapshot)
			.OnConfirmed(MoveTemp(OnConfirmed))
			.OnCancelled(MoveTemp(OnCancelled));
		Window->SetContent(Preview);
		TWeakPtr<SDesign2UMGPreviewWindow> WeakPreview = Preview;
		Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([WeakPreview](const TSharedRef<SWindow>&)
		{
			if (auto ClosedPreview = WeakPreview.Pin()) ClosedPreview->CancelFontToolInstall();
		}));

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
