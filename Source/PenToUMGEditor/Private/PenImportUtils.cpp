// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenImportUtils.h"
#include "PenToUMGLog.h"
#include "PenRemoteImageImporter.h"
#include "SPenToUMGPreviewWindow.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGWidgetUtils.h"
#include "PenParser.h"

#include "WidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "PenImportUtils"

namespace
{
	const TCHAR* FallbackOutputPath = TEXT("/Game/PenToUMG/PenConvert_Output");

	FString ResolveOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(FallbackOutputPath) : OutputPath;
	}
}

// ── 生成 ──

UWidgetBlueprint* PenImportUtils::GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath)
{
	const FString FinalPath = ResolveOutputPath(OutputPath);
	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImportUtils: Widget Blueprint generation failed"));
		return nullptr;
	}
	UE_LOG(LogPenToUMG, Log, TEXT("PenImportUtils: Generated %s"), *WBP->GetPathName());
	return WBP;
}

bool PenImportUtils::GenerateBlueprintAndPng(const FUMGWidgetData& Data, const FString& OutputPath)
{
	UWidgetBlueprint* WBP = GenerateBlueprint(Data, OutputPath);
	if (!WBP)
		return false;

	// 优先用 RootWidget 自身的 Bounds，否则从子节点边界计算
	float MaxRight = FMath::Max(400.0f, Data.RootWidget.Bounds.W);
	float MaxBottom = FMath::Max(300.0f, Data.RootWidget.Bounds.H);
	for (const auto& Child : Data.RootWidget.Children)
	{
		MaxRight = FMath::Max(MaxRight, Child.Bounds.X + Child.Bounds.W);
		MaxBottom = FMath::Max(MaxBottom, Child.Bounds.Y + Child.Bounds.H);
	}
	const FIntPoint FallbackSize(FMath::CeilToInt(MaxRight), FMath::CeilToInt(MaxBottom));

	TArray<uint8> PngData;
	FIntPoint Size;
	if (!FUMGWidgetUtils::RenderWidgetToPng(WBP, FallbackSize, PngData, Size))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImportUtils: Widget PNG render failed"));
		return false;
	}

	const FString OutPng = FPaths::ProjectSavedDir() / TEXT("PenToUMG/rendered.png");
	FFileHelper::SaveArrayToFile(PngData, *OutPng);
	UE_LOG(LogPenToUMG, Log, TEXT("PenImportUtils: Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
	return true;
}

void PenImportUtils::ResolveImages(FUMGWidgetData& Data)
{
	FString Report;
	const bool bOk = FPenRemoteImageImporter::ResolveRemoteImages(Data, &Report);
	if (!Report.IsEmpty())
		UE_LOG(LogPenToUMG, Log, TEXT("%s"), *Report);
	if (!bOk)
		UE_LOG(LogPenToUMG, Warning, TEXT("PenImportUtils: Unresolved remote images; generation will continue"));
}

void PenImportUtils::GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath)
{
	FUMGWidgetData Data;
	FString ParseError;
	if (!FUMGJsonParser::ParseJson(EditedJson, Data, &ParseError))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenImportUtils: Edited JSON parse failed: %s"), *ParseError);
		return;
	}

	ResolveImages(Data);
	GenerateBlueprint(Data, OutputPath);
}

// ── 预览窗口 ──

bool PenImportUtils::OpenPreviewWindow(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	const FText& WindowTitle)
{
	if (!FSlateApplication::IsInitialized())
		return false;

	const FString JsonPayload = FUMGJsonSerializer::Serialize(Data);
	const FString FinalPath = ResolveOutputPath(OutputPath);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(1600.0f, 980.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(true);

	Window->SetContent(
		SNew(SPenToUMGPreviewWindow)
		.OriginalJson(JsonPayload)
		.OutputPath(FinalPath)
		.OnConfirmed(FOnPreviewConfirmed::CreateLambda(
			[](const FString& EditedJson, const FString& ConfirmedPath)
			{
				PenImportUtils::GenerateFromEditedJson(EditedJson, ConfirmedPath);
			}))
		.OnCancelled(FOnPreviewCancelled::CreateLambda(
			[]()
			{
				UE_LOG(LogPenToUMG, Log, TEXT("PenImportUtils: Preview cancelled"));
			}))
	);

	FSlateApplication::Get().AddWindow(Window);
	return true;
}

// ── Frame 选择器 ──

void PenImportUtils::ShowFrameSelector(
	const TArray<FPenFrameInfo>& Frames,
	const FText& WindowTitle,
	const FText& HintText,
	FFrameSelectedCallback OnFrameSelected)
{
	if (!FSlateApplication::IsInitialized())
		return;

	TSharedPtr<TArray<TSharedPtr<FPenFrameInfo>>> Items = MakeShared<TArray<TSharedPtr<FPenFrameInfo>>>();
	for (const FPenFrameInfo& F : Frames)
		Items->Add(MakeShared<FPenFrameInfo>(F));

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
			SNew(SListView<TSharedPtr<FPenFrameInfo>>)
			.ListItemsSource(Items.Get())
			.OnGenerateRow_Lambda([Items](TSharedPtr<FPenFrameInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
			{
				return SNew(STableRow<TSharedPtr<FPenFrameInfo>>, OwnerTable)
					.Padding(FMargin(4.0f, 2.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(
							TEXT("%s  (%.0f x %.0f)"),
							*Item->Name, Item->Width, Item->Height)))
					];
			})
			.OnMouseButtonDoubleClick_Lambda([Callback, WeakWindow](TSharedPtr<FPenFrameInfo> Item)
			{
				if (Item.IsValid() && *Callback)
				{
					(*Callback)(Item->Id);
					if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
						Win->RequestDestroyWindow();
				}
			})
		]
	);

	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
