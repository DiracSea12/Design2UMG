// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "DesignImportUtils.h"
#include "PenImporter.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignRemoteImageImporter.h"
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

#define LOCTEXT_NAMESPACE "DesignImportUtils"

namespace
{
	const TCHAR* FallbackOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");

	FString ResolveOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(FallbackOutputPath) : OutputPath;
	}
}

// Blueprint 生成

UWidgetBlueprint* DesignImportUtils::GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath)
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
						UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Incremental update cancelled"));
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
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint generation failed"));
		FDesign2UMGNotification::Error(LOCTEXT("GenFailed", "Design2UMG: Widget Blueprint generation failed"));
		return nullptr;
	}
	UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Generated %s"), *WBP->GetPathName());

	FImportResult Result = FImportResult::FromData(EffectiveData, FPlatformTime::Seconds() - StartTime);
	Result.ShowSummaryNotification();
	return WBP;
}

bool DesignImportUtils::GenerateBlueprintAndPng(const FUMGWidgetData& Data, const FString& OutputPath)
{
	const FString FinalPath = ResolveOutputPath(OutputPath);

	// 先构建（不立即落盘），再用瞬态实例渲染 PNG，最后统一保存 Blueprint。
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint build failed"));
		return false;
	}

	// 渲染尺寸优先使用根节点 Bounds；合成根（如多 frame 的 CanvasPanel）Bounds 为 0，
	// 退化到 1x1 会导致 PNG 无意义，此时使用 1920x1080 默认尺寸。
	int32 RenderW = FMath::CeilToInt(Data.RootWidget.Bounds.W);
	int32 RenderH = FMath::CeilToInt(Data.RootWidget.Bounds.H);
	if (RenderW < 2 || RenderH < 2)
	{
		RenderW = 1920;
		RenderH = 1080;
	}
	const FIntPoint FallbackSize(RenderW, RenderH);

	TArray<uint8> PngData;
	FIntPoint Size;
	if (!FUMGWidgetUtils::RenderWidgetTreeToPng(WBP, FallbackSize, PngData, Size))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget PNG render failed"));
		// 渲染失败时也保存 Blueprint，方便定位问题。
		FUMGGenerator::SaveWidgetBlueprint(WBP);
		return false;
	}

	// 成功渲染后保存 Blueprint
	FUMGGenerator::SaveWidgetBlueprint(WBP);

	// 保存 PNG 到 Saved/Design2UMG（使用最终生成的资产名，避免同输出目录下多次导入互相覆盖）
	const FString PngName = WBP ? WBP->GetName() : FPaths::GetBaseFilename(FinalPath);
	const FString OutPng = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (PngName + TEXT(".png"));
	if (!FFileHelper::SaveArrayToFile(PngData, *OutPng))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Failed to save PNG: %s"), *OutPng);
		return false;
	}
	UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
	return true;
}

void DesignImportUtils::ResolveImages(FUMGWidgetData& Data)
{
	FString Report;
	const bool bOk = FDesignRemoteImageImporter::ResolveRemoteImages(Data, &Report);
	if (!Report.IsEmpty())
		UE_LOG(LogDesign2UMG, Log, TEXT("%s"), *Report);
	if (!bOk)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportUtils: Unresolved remote images; generation will continue"));
		FDesign2UMGNotification::Warning(LOCTEXT("ImageWarning", "Design2UMG: Some images failed to download"));
	}
}

void DesignImportUtils::GenerateFromEditedJson(const FString& EditedJson, const FString& OutputPath)
{
	FScopedSlowTask SlowTask(3.0f, NSLOCTEXT("DesignImportUtils", "Generating", "Generating Widget Blueprint..."));
	SlowTask.MakeDialog(true);

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("DesignImportUtils", "GenParse", "Parsing edited design..."));
	FUMGWidgetData Data;
	FString ParseError;
	if (!FUMGJsonParser::ParseJson(EditedJson, Data, &ParseError))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Edited JSON parse failed: %s"), *ParseError);
		FDesign2UMGNotification::Error(LOCTEXT("EditedJsonParseFailed", "Design2UMG: Failed to parse edited design data"));
		return;
	}
	if (SlowTask.ShouldCancel()) return;

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("DesignImportUtils", "GenImages", "Downloading remote images..."));
	ResolveImages(Data);
	if (SlowTask.ShouldCancel()) return;

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("DesignImportUtils", "GenBP", "Generating Widget Blueprint..."));
	GenerateBlueprint(Data, OutputPath);
}

// 预览窗口

bool DesignImportUtils::OpenPreviewWindow(
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
				DesignImportUtils::GenerateFromEditedJson(EditedJson, ConfirmedPath);
			}))
		.OnCancelled(FOnPreviewCancelled::CreateLambda(
			[]()
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Preview cancelled"));
			}))
	);

	FSlateApplication::Get().AddWindow(Window);
	return true;
}

// Frame 选择窗口

void DesignImportUtils::ShowFrameSelector(
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

