// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportDialogs.h"

#include "Design2UMGLog.h"
#include "SDesign2UMGPreviewWindow.h"
#include "DesignImportTypes.h"
#include "DesignImportDefaults.h"
#include "UMGSchema.h"
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
	bool OpenPreviewWindow(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
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
