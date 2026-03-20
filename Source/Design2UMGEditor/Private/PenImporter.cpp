// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenImporter.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "FontResolver.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "PenMCPBridge.h"
#include "UMGSchema.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "PenImporter"

namespace
{
	void ResolveDocumentFonts(FDesignDocument& Document)
	{
		FFontResolver::AutoImportFonts(Document);
		FFontResolver::FindMissingFonts(Document);
	}
}

int32 FDesignImportResult::CountWidgets(const FUMGWidgetNode& Node)
{
	int32 Count = 1;
	for (const auto& Child : Node.Children)
		Count += CountWidgets(Child);
	return Count;
}

FDesignImportResult FDesignImportResult::FromData(const FUMGWidgetData& Data, double Elapsed)
{
	FDesignImportResult R;
	R.bSuccess = true;
	R.WidgetCount = CountWidgets(Data.RootWidget);
	R.WarningCount = Data.Warnings.Num();
	R.Warnings = Data.Warnings;
	R.ElapsedSeconds = Elapsed;
	return R;
}

void FDesignImportResult::ShowSummaryDialog() const
{
	if (!FSlateApplication::IsInitialized())
		return;

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SummaryTitle", "Import Summary"))
		.ClientSize(FVector2D(480.0f, 320.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TWeakPtr<SWindow> WeakWindow = Window;

	TSharedRef<SScrollBox> WarningList = SNew(SScrollBox);
	for (const FString& W : Warnings)
	{
		WarningList->AddSlot()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("  \x2022 %s"), *W)))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.7f, 0.0f)))
		];
	}

	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 12.0f, 12.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(
				LOCTEXT("SummaryStats", "Generated {0} widgets in {1}s"),
				FText::AsNumber(WidgetCount),
				FText::FromString(FString::Printf(TEXT("%.1f"), ElapsedSeconds))))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("WarningHeader", "{0} warning(s):"), FText::AsNumber(WarningCount)))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.7f, 0.0f)))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(12.0f, 4.0f)
		[
			WarningList
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(12.0f, 8.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("SummaryOk", "OK"))
			.OnClicked_Lambda([WeakWindow]()
			{
				if (auto Win = WeakWindow.Pin())
					Win->RequestDestroyWindow();
				return FReply::Handled();
			})
		]
	);

	FSlateApplication::Get().AddWindow(Window);
}

void FDesignImportResult::ShowSummaryNotification() const
{
	if (!bSuccess)
		return;

	if (WarningCount > 0)
	{
		ShowSummaryDialog();
		return;
	}

	FText Msg = FText::Format(
		LOCTEXT("ImportSummaryOk", "Design2UMG: {0} widgets ({1}s)"),
		FText::AsNumber(WidgetCount),
		FText::FromString(FString::Printf(TEXT("%.1f"), ElapsedSeconds)));
	FDesign2UMGNotification::Success(Msg);
}

bool FPenImporter::ImportFromFile(const FString& FilePath, FUMGWidgetData& OutData)
{
	FScopedSlowTask SlowTask(2.0f, FText::FromString(TEXT("Importing .pen file...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Parsing design file...")));
	FDesignDocument Document;
	if (!FPenParser::ParseFromFile(FilePath, Document))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: Failed to parse file: %s"), *FilePath);
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	ResolveDocumentFonts(Document);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Converting design to widgets...")));
	OutData = FDesign2UMGConverter::Convert(Document);
	return true;
}

bool FPenImporter::ImportFromJson(const FString& JsonString, FUMGWidgetData& OutData)
{
	FScopedSlowTask SlowTask(2.0f, FText::FromString(TEXT("Importing design...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Parsing design...")));
	FDesignDocument Document;
	if (!FPenParser::ParseDocument(JsonString, Document))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: Failed to parse JSON"));
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	ResolveDocumentFonts(Document);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Converting design to widgets...")));
	OutData = FDesign2UMGConverter::Convert(Document);
	return true;
}

bool FPenImporter::ImportFileWithTargetNode(const FString& FilePath, const FString& TargetNodeId, FUMGWidgetData& OutData)
{
	FScopedSlowTask SlowTask(3.0f, FText::FromString(TEXT("Importing .pen frame...")));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Reading design file...")));
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: Failed to read file: %s"), *FilePath);
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Parsing design file...")));
	TSharedPtr<FJsonObject> DocObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, DocObj) || !DocObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: JSON parse failed: %s"), *FilePath);
		return false;
	}
	if (SlowTask.ShouldCancel()) return false;

	FString FilteredDocumentJson;
	if (!FPenMCPBridge::BuildFilteredDocumentJson(DocObj, FilteredDocumentJson, TargetNodeId))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: BuildFilteredDocumentJson failed for node %s"), *TargetNodeId);
		return false;
	}

	// Keep font behavior consistent with ImportFromFile/ImportFromJson path,
	// but only for the filtered document to avoid unrelated font work.
	FDesignDocument FilteredDocument;
	if (FPenParser::ParseDocument(FilteredDocumentJson, FilteredDocument))
	{
		ResolveDocumentFonts(FilteredDocument);
	}
	else
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImporter: ParseDocument failed after filtering node %s"), *TargetNodeId);
		return false;
	}

	SlowTask.EnterProgressFrame(1.0f, FText::FromString(TEXT("Converting design to widgets...")));
	OutData = FDesign2UMGConverter::Convert(FilteredDocument);
	return true;
}

#undef LOCTEXT_NAMESPACE


