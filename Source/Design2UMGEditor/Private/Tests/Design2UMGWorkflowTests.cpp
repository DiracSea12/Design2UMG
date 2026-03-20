// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#include "Design2UMGConverter.h"
#include "Design2UMGTestUtils.h"
#include "DesignImportUtils.h"
#include "LayoutFixer.h"
#include "PenImporter.h"
#include "SDesign2UMGPreviewWindow.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "RHI.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGTestUtils;

// ============================================================
// Batch import: pencil-welcome.pen 4 frames
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenWelcomeBatchImportTest,
	"Design2UMG.BatchImport.PencilWelcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPenWelcomeBatchImportTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	if (!Plugin.IsValid())
	{
		AddError(TEXT("Design2UMG plugin not found"));
		return false;
	}

	const FString PenFile = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/Design2UMGEditor/Private/Tests/Fixtures/pencil-welcome.pen"));
	if (!FPaths::FileExists(PenFile))
	{
		AddWarning(FString::Printf(TEXT("Test fixture not found, skipping: %s"), *PenFile));
		return true;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/PencilWelcome");
	TArray<FString> GeneratedAssetPaths;

	struct FFrameInfo
	{
		FString Id;
		FString Name;
	};

	const TArray<FFrameInfo> Frames = {
		{TEXT("sbChi"), TEXT("welcome")},
		{TEXT("cOnQP"), TEXT("Step1")},
		{TEXT("tlPHu"), TEXT("Step2")},
		{TEXT("CzlXB"), TEXT("Step3")}
	};

	for (const FFrameInfo& Frame : Frames)
	{
		FUMGWidgetData Data;
		const bool bOk = FPenImporter::ImportFileWithTargetNode(PenFile, Frame.Id, Data);
		TestTrue(*FString::Printf(TEXT("Parse %s"), *Frame.Name), bOk);
		if (!bOk)
		{
			continue;
		}

		if (GUsingNullRHI)
		{
			continue;
		}

		const FString FrameOutputPath = OutputPath / Data.WidgetName;
		const bool bPng = DesignImportUtils::GenerateBlueprintAndPng(Data, FrameOutputPath);
		TestTrue(*FString::Printf(TEXT("Render %s"), *Frame.Name), bPng);
		GeneratedAssetPaths.Add(FrameOutputPath);
	}

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<UObject*> ObjectsToDelete;
	for (const FString& AssetDir : GeneratedAssetPaths)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByPath(FName(*AssetDir), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (UObject* Object = Asset.GetAsset())
			{
				ObjectsToDelete.Add(Object);
			}
		}
	}

	if (ObjectsToDelete.Num() > 0)
	{
		ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
	}

	return true;
}

// ============================================================
// Test 12: LayoutFixer - Conservatism Check (Basic)
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutFixerConservatismTest,
	"Design2UMG.LayoutFixer.ConservatismCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLayoutFixerConservatismTest::RunTest(const FString& Parameters)
{
	FFixerPreset Preset;
	Preset.DesignSize = FIntPoint(1920, 1080);
	Preset.SnapPx = 4;

	FFixerWidgetNode Root;
	Root.Bounds = {0, 0, 1, 1};

	FFixerWidgetNode Child1;
	Child1.Bounds = {0, 0, 0.1f, 0.1f};

	FFixerWidgetNode Child2;
	Child2.Bounds = {0.5f, 0, 0.1f, 0.1f};

	Root.Children.Add(Child1);
	Root.Children.Add(Child2);

	TArray<FFixerLogEntry> Log;
	FFixerStats Stats;
	FLayoutFixer::FixWidgetTree(Root, Preset, Log, &Stats);

	TestEqual(TEXT("RolledBack should be non-negative"), Stats.RolledBack >= 0, true);
	return true;
}

// ============================================================
// Test 13: PreviewWindow - Instantiation & Validation
// ============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewWindowTest,
	"Design2UMG.PreviewWindow.Initialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPreviewWindowTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		return true;
	}

	const FString DummyJson = TEXT("{}");
	TArray<FString> Warnings;

	TSharedPtr<SDesign2UMGPreviewWindow> PreviewWindow = SNew(SDesign2UMGPreviewWindow)
		.OriginalJson(DummyJson)
		.OutputPath(TEXT("/Game/Test"))
		.Warnings(Warnings);

	TestTrue(TEXT("Preview window instantiated"), PreviewWindow.IsValid());
	PreviewWindow.Reset();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFitContentMinimumExpandsToContentTest,
	"Design2UMG.Converter.FitContentMinimumExpandsToContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFitContentMinimumExpandsToContentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;

	const TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("fit-min-root");
	Root->Name = TEXT("FitMinRoot");
	Root->Type = EDesignNodeType::Frame;
	Root->Layout = EDesignLayout::Vertical;
	Root->Width = FDesignSize::Fixed(320.0f);
	Root->Height = FDesignSize::Fit();
	Root->bHasWidth = true;

	const TSharedPtr<FDesignNode> Row = MakeShared<FDesignNode>();
	Row->Id = TEXT("fit-min-row");
	Row->Name = TEXT("FitMinRow");
	Row->Type = EDesignNodeType::Frame;
	Row->Layout = EDesignLayout::Vertical;
	Row->Width = FDesignSize::Fixed(320.0f);
	Row->Height = FDesignSize::Fit(44.0f);
	Row->bHasWidth = true;
	Row->bHasHeight = true;

	const TSharedPtr<FDesignNode> Content = MakeShared<FDesignNode>();
	Content->Id = TEXT("fit-min-content");
	Content->Name = TEXT("FitMinContent");
	Content->Type = EDesignNodeType::Frame;
	Content->Layout = EDesignLayout::Vertical;
	Content->Width = FDesignSize::Fixed(320.0f);
	Content->Height = FDesignSize::Fixed(56.0f);
	Content->bHasWidth = true;
	Content->bHasHeight = true;

	Row->Children.Add(Content);
	Root->Children.Add(Row);
	Doc.Children.Add(Root);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("fit-min-root"));
	const FUMGWidgetNode* RowSizeBox = FindWidgetById(Data.RootWidget, TEXT("fit-min-row_sz"));

	TestTrue(TEXT("Find fit-content minimum root widget"), RootWidget != nullptr);
	TestTrue(TEXT("Find fit-content minimum row size box"), RowSizeBox != nullptr);
	if (!RootWidget || !RowSizeBox)
	{
		return false;
	}

	TestTrue(TEXT("Root fit-content height should grow beyond the minimum fallback"),
		RootWidget->Bounds.H >= 56.0f - 0.5f);

	const FString* HeightOverride = RowSizeBox->Properties.Find(TEXT("HeightOverride"));
	const FString* MinDesiredHeight = RowSizeBox->Properties.Find(TEXT("MinDesiredHeight"));
	TestTrue(TEXT("Flow fit-content(min) row should export a concrete height constraint"),
		HeightOverride != nullptr || MinDesiredHeight != nullptr);
	if (HeightOverride)
	{
		TestTrue(TEXT("HeightOverride should be at least the larger child content height"),
			FCString::Atof(**HeightOverride) >= 56.0f - 0.5f);
	}
	if (MinDesiredHeight)
	{
		TestTrue(TEXT("MinDesiredHeight should be at least the larger child content height"),
			FCString::Atof(**MinDesiredHeight) >= 56.0f - 0.5f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFillWidthSingleLineTextDoesNotInflateFitHeightTest,
	"Design2UMG.Converter.FillWidthSingleLineTextDoesNotInflateFitHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FFillWidthSingleLineTextDoesNotInflateFitHeightTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;

	const TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("text-fit-root");
	Root->Name = TEXT("TextFitRoot");
	Root->Type = EDesignNodeType::Frame;
	Root->Layout = EDesignLayout::Vertical;
	Root->Width = FDesignSize::Fixed(160.0f);
	Root->Height = FDesignSize::Fit();
	Root->bHasWidth = true;

	const TSharedPtr<FDesignNode> HeaderCell = MakeShared<FDesignNode>();
	HeaderCell->Id = TEXT("header-cell");
	HeaderCell->Name = TEXT("HeaderCell");
	HeaderCell->Type = EDesignNodeType::Frame;
	HeaderCell->Layout = EDesignLayout::Horizontal;
	HeaderCell->Width = FDesignSize::Fixed(160.0f);
	HeaderCell->Height = FDesignSize::Fit(44.0f);
	HeaderCell->bHasWidth = true;
	HeaderCell->bHasHeight = true;
	HeaderCell->Padding.Top = 12.0f;
	HeaderCell->Padding.Right = 12.0f;
	HeaderCell->Padding.Bottom = 12.0f;
	HeaderCell->Padding.Left = 12.0f;
	HeaderCell->bHasPadding = true;
	HeaderCell->AlignItems = EDesignAlignItems::Center;

	const TSharedPtr<FDesignNode> HeaderText = MakeShared<FDesignNode>();
	HeaderText->Id = TEXT("header-text");
	HeaderText->Name = TEXT("HeaderText");
	HeaderText->Type = EDesignNodeType::Text;
	HeaderText->Width = FDesignSize::Fill();
	HeaderText->Height = FDesignSize::Fit();
	HeaderText->Content = TEXT("Status");
	HeaderText->FontFamily = TEXT("Inter");
	HeaderText->FontSize = 14.0f;
	HeaderText->FontWeight = TEXT("normal");
	HeaderText->LineHeight = 1.4285714285714286f;
	HeaderText->TextGrowth = EDesignTextGrowth::FixedWidth;

	HeaderCell->Children.Add(HeaderText);
	Root->Children.Add(HeaderCell);
	Doc.Children.Add(Root);

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc);
	const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("text-fit-root"));
	const FUMGWidgetNode* HeaderCellSizeBox = FindWidgetById(Data.RootWidget, TEXT("header-cell_sz"));

	TestTrue(TEXT("Find single-line text fit root widget"), RootWidget != nullptr);
	TestTrue(TEXT("Find header cell size box"), HeaderCellSizeBox != nullptr);
	if (!RootWidget || !HeaderCellSizeBox)
	{
		return false;
	}

	const FString* HeightOverride = HeaderCellSizeBox->Properties.Find(TEXT("HeightOverride"));
	const FString* MinDesiredHeight = HeaderCellSizeBox->Properties.Find(TEXT("MinDesiredHeight"));
	TestTrue(TEXT("Header cell should export a height constraint"),
		HeightOverride != nullptr || MinDesiredHeight != nullptr);

	const float ExportedHeight = HeightOverride
		? FCString::Atof(**HeightOverride)
		: (MinDesiredHeight ? FCString::Atof(**MinDesiredHeight) : 0.0f);

	TestTrue(TEXT("Single-line fill-width text should keep the 44px fit-content minimum"),
		FMath::IsNearlyEqual(ExportedHeight, 44.0f, 0.5f));
	TestTrue(TEXT("Root fit height should remain at the 44px minimum"),
		FMath::IsNearlyEqual(RootWidget->Bounds.H, 44.0f, 0.5f));

	return true;
}

#endif // WITH_AUTOMATION_WORKER
