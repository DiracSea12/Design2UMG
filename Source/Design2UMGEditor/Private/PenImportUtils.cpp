// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenImportUtils.h"
#include "PenImporter.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "PenRemoteImageImporter.h"
#include "SDesign2UMGPreviewWindow.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "UMGWidgetUtils.h"
#include "PenParser.h"
#include "Design2UMGSettings.h"
#include "IncrementalUpdater.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "WidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "PenImportUtils"

namespace
{
	const TCHAR* FallbackOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");

	FString ResolveOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(FallbackOutputPath) : OutputPath;
	}
}

// ── 生成 ──

UWidgetBlueprint* PenImportUtils::GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath)
{
	const double StartTime = FPlatformTime::Seconds();
	const FString FinalPath = ResolveOutputPath(OutputPath);
	FUMGWidgetData EffectiveData = Data;

	// Incremental update: check for existing BP
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (Settings && Settings->bEnableIncrementalUpdate)
	{
		FString AssetPath = FinalPath / Data.WidgetName + TEXT(".") + Data.WidgetName;
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FAssetData ExistingAsset = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
		if (ExistingAsset.IsValid())
		{
			UWidgetBlueprint* ExistingBP = Cast<UWidgetBlueprint>(ExistingAsset.GetAsset());
			if (ExistingBP)
			{
				FDiffResult Diff = FIncrementalUpdater::ComputeDiff(Data.RootWidget, ExistingBP);
				if (Diff.HasChanges())
				{
					if (!FIncrementalUpdater::ShowDiffPreviewDialog(Diff))
					{
						UE_LOG(LogDesign2UMG, Log, TEXT("PenImportUtils: Incremental update cancelled"));
						return nullptr;
					}
					EffectiveData.RootWidget = FIncrementalUpdater::FilterByAccepted(Data.RootWidget, Diff);
				}
			}
		}
	}

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(EffectiveData, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImportUtils: Widget Blueprint generation failed"));
		FDesign2UMGNotification::Error(LOCTEXT("GenFailed", "Design2UMG: Widget Blueprint generation failed"));
		return nullptr;
	}
	UE_LOG(LogDesign2UMG, Log, TEXT("PenImportUtils: Generated %s"), *WBP->GetPathName());

	FImportResult Result = FImportResult::FromData(EffectiveData, FPlatformTime::Seconds() - StartTime);
	Result.ShowSummaryNotification();
	return WBP;
}

bool PenImportUtils::GenerateBlueprintAndPng(const FUMGWidgetData& Data, const FString& OutputPath)
{
	const FString FinalPath = ResolveOutputPath(OutputPath);

	// Build（不编译），此时 transient 纹理仍在内存中
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImportUtils: Widget Blueprint build failed"));
		return false;
	}

	// 在编译前渲染 PNG，transient 纹理 mip 数据此时仍有效
	float MaxRight = FMath::Max(1.0f, Data.RootWidget.Bounds.W);
	float MaxBottom = FMath::Max(1.0f, Data.RootWidget.Bounds.H);
	for (const auto& Child : Data.RootWidget.Children)
	{
		MaxRight = FMath::Max(MaxRight, Child.Bounds.X + Child.Bounds.W);
		MaxBottom = FMath::Max(MaxBottom, Child.Bounds.Y + Child.Bounds.H);
	}
	const FIntPoint FallbackSize(FMath::CeilToInt(MaxRight), FMath::CeilToInt(MaxBottom));

	TArray<uint8> PngData;
	FIntPoint Size;
	if (!FUMGWidgetUtils::RenderWidgetTreeToPng(WBP, FallbackSize, PngData, Size))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImportUtils: Widget PNG render failed"));
		// 渲染失败仍然保存 Blueprint
		FUMGGenerator::SaveWidgetBlueprint(WBP);
		return false;
	}

	// 保存 Blueprint 到磁盘
	FUMGGenerator::SaveWidgetBlueprint(WBP);

	// 保存 PNG
	const FString PngName = FPaths::GetBaseFilename(OutputPath);
	const FString OutPng = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (PngName + TEXT(".png"));
	FFileHelper::SaveArrayToFile(PngData, *OutPng);
	UE_LOG(LogDesign2UMG, Log, TEXT("PenImportUtils: Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
	return true;
}

void PenImportUtils::ResolveImages(FUMGWidgetData& Data)
{
	FString Report;
	const bool bOk = FPenRemoteImageImporter::ResolveRemoteImages(Data, &Report);
	if (!Report.IsEmpty())
		UE_LOG(LogDesign2UMG, Log, TEXT("%s"), *Report);
	if (!bOk)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("PenImportUtils: Unresolved remote images; generation will continue"));
		FDesign2UMGNotification::Warning(LOCTEXT("ImageWarning", "Design2UMG: Some images failed to download"));
	}
}

void PenImportUtils::GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath)
{
	FScopedSlowTask SlowTask(3.0f, NSLOCTEXT("PenImportUtils", "Generating", "Generating Widget Blueprint..."));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("PenImportUtils", "GenParse", "Parsing edited design..."));
	FUMGWidgetData Data;
	FString ParseError;
	if (!FUMGJsonParser::ParseJson(EditedJson, Data, &ParseError))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenImportUtils: Edited JSON parse failed: %s"), *ParseError);
		FDesign2UMGNotification::Error(LOCTEXT("EditedJsonParseFailed", "Design2UMG: Failed to parse edited design data"));
		return;
	}
	if (SlowTask.ShouldCancel()) return;

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("PenImportUtils", "GenImages", "Downloading remote images..."));
	ResolveImages(Data);
	if (SlowTask.ShouldCancel()) return;

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("PenImportUtils", "GenBP", "Generating Widget Blueprint..."));
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
		SNew(SDesign2UMGPreviewWindow)
		.OriginalJson(JsonPayload)
		.OutputPath(FinalPath)
		.Warnings(Data.Warnings)
		.OnConfirmed(FOnPreviewConfirmed::CreateLambda(
			[](const FString& EditedJson, const FString& ConfirmedPath)
			{
				PenImportUtils::GenerateFromEditedJson(EditedJson, ConfirmedPath);
			}))
		.OnCancelled(FOnPreviewCancelled::CreateLambda(
			[]()
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("PenImportUtils: Preview cancelled"));
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
