// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportGeneration.h"
#include "UMGGenerator.h"
#include "UMGWidgetRenderer.h"
#include "Design2UMGConverter.h"
#include "PenParser.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "UMGJsonParser.h"
#include "UMGJsonSerializer.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGRenderDimensionLimitTest,
	"Design2UMG.Generation.RenderDimensionLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGRenderDimensionLimitTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("exceeds pixel budget"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("RenderCompiledBlueprintToPngFile failed"), EAutomationExpectedErrorFlags::Contains, 1);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_RenderDimensionLimit");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds.W = 96.0f;
	Data.RootWidget.Bounds.H = 64.0f;

	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/RenderDimensionLimit");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* WidgetBP = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint is built for the dimension limit contract"), WidgetBP))
	{
		return false;
	}

	FUMGWidgetExportPlan Plan;
	FString PlanError;
	TestTrue(TEXT("Hardware-compatible 81M export is accepted"), FUMGWidgetRenderer::BuildExportPlan(FIntPoint(9000, 9000), 16384, Plan, &PlanError));
	TestEqual(TEXT("Hardware-compatible export uses one original-resolution target"), Plan.TileCount, FIntPoint(1, 1));
	TestEqual(TEXT("Single-target extent is not scaled"), Plan.TileSize, FIntPoint(9000, 9000));
	TestTrue(TEXT("228M export retains its full resolution"), FUMGWidgetRenderer::BuildExportPlan(FIntPoint(24000, 9500), 16384, Plan, &PlanError));
	TestEqual(TEXT("Export extent is not scaled"), Plan.Size, FIntPoint(24000, 9500));
	TestTrue(TEXT("Large exports require more than one bounded target"), Plan.TileCount.X * Plan.TileCount.Y > 1);
	TestTrue(TEXT("Hardware-limited tiles retain bounded pixel allocations"), static_cast<int64>(Plan.TileSize.X) * Plan.TileSize.Y <= 64ll * 1024 * 1024);
	TestTrue(TEXT("Wide one-pixel export respects the hardware edge"), FUMGWidgetRenderer::BuildExportPlan(FIntPoint(19012, 1), 16384, Plan));
	TestEqual(TEXT("Edge tile is not scaled"), Plan.TileCount, FIntPoint(3, 1));

	// Output budget is independent of the unchanged input-image resource budget.
	UUserWidget* GeneratedCDO = Cast<UUserWidget>(WidgetBP->GeneratedClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Generated class default object exists"), GeneratedCDO))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	GeneratedCDO->DesignTimeSize = FVector2D(
		32768.0f,
		32768.0f);

	const FString PngPath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("D2URenderLimit"), TEXT(".png"));
	FIntPoint RenderedSize = FIntPoint::ZeroValue;
	FString Error;
	const bool bRendered = DesignImportGeneration::RenderCompiledBlueprintToPngFile(WidgetBP, PngPath, &RenderedSize, &Error);
	TestFalse(TEXT("Export over the pixel budget is rejected before allocation"), bRendered);
	TestTrue(
		TEXT("Rejection carries the structured pixel-budget error message"),
		Error.Contains(TEXT("render export exceeds pixel budget:")));
	TestFalse(TEXT("No PNG is written for the rejected render"), IFileManager::Get().FileExists(*PngPath));
	IFileManager::Get().Delete(*PngPath);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGRenderSinglePixelDimensionTest,
	"Design2UMG.Generation.RenderSinglePixelDimension",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGRenderSinglePixelDimensionTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_SinglePixelDivider");
	Data.RootWidget.Type = TEXT("Border");
	Data.RootWidget.Id = TEXT("divider");
	Data.RootWidget.Bounds.W = 320.0f;
	Data.RootWidget.Bounds.H = 1.0f;
	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/SinglePixelDivider");
	DeleteGeneratedAssetsAtPath(OutputPath);
	UWidgetBlueprint* WidgetBP = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("One-pixel design generates a blueprint"), WidgetBP)) return false;
	const FString PngPath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("D2USinglePixel"), TEXT(".png"));
	FIntPoint Size = FIntPoint::ZeroValue;
	FString Error;
	TestTrue(TEXT("Compiled renderer accepts a one-pixel dimension"),
		DesignImportGeneration::RenderCompiledBlueprintToPngFile(WidgetBP, PngPath, &Size, &Error));
	TestEqual(TEXT("Compiled render retains the authored size"), Size, FIntPoint(320, 1));
	TArray<uint8> PngData;
	TestTrue(TEXT("Widget-tree renderer accepts a one-pixel dimension"),
		FUMGWidgetRenderer::RenderWidgetTreeToPng(WidgetBP, PngData, Size));
	TestEqual(TEXT("Tree render retains the authored size"), Size, FIntPoint(320, 1));
	const FIntPoint WideSize(static_cast<int32>(GetMax2DTextureDimension()) + 1, 1);
	FUMGWidgetRenderer::SetBlueprintExportBounds(WidgetBP, FBox2D(FVector2D::ZeroVector, FVector2D(WideSize)));
	TestTrue(TEXT("Native tile replay exports an extent wider than one GPU target"),
		FUMGWidgetRenderer::RenderWidgetToPng(WidgetBP, PngData, Size, &Error));
	TestEqual(TEXT("Tiled export keeps every original pixel without scaling"), Size, WideSize);
	TestEqual(TEXT("Tiling does not change the blueprint layout"),
		FUMGWidgetRenderer::GetBlueprintDesignTimeSize(WidgetBP).GetValue(), FVector2D(320, 1));

	IFileManager::Get().Delete(*PngPath);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesign2UMGSourceExportBoundsTest,
	"Design2UMG.Generation.SourceExportBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGSourceExportBoundsTest::RunTest(const FString&)
{
	FDesignDocument Doc;
	if (!TestTrue(TEXT("Source with outer shadow parses"), FPenParser::ParseDocument(TEXT(R"({"version":"2.8",
		"children":[{"id":"root","type":"rectangle","width":45,"height":45,"fill":"#ffffff",
			"effect":{"type":"shadow","shadowType":"outer","color":"#00000050","offset":{"x":0,"y":4},"blur":12}}]})"), Doc))) return false;
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	if (!TestTrue(TEXT("Conversion records source paint bounds"), Data.ExportBounds.IsSet())) return false;
	TestEqual(TEXT("Layout width remains authored"), Data.RootWidget.Bounds.W, 45.0f);
	TestEqual(TEXT("Export contains the shadow without changing layout"), Data.ExportBounds->GetSize(), FVector2D(81,81));
	TestEqual(TEXT("Shadow offset determines export origin"), Data.ExportBounds->Min, FVector2D(-18,-14));
	FUMGWidgetData RoundTrip;
	if (!TestTrue(TEXT("Export bounds survive the JSON bridge"), FUMGJsonParser::ParseJson(FUMGJsonSerializer::Serialize(Data), RoundTrip))) return false;
	if (!TestTrue(TEXT("JSON retains export geometry"), RoundTrip.ExportBounds.IsSet())) return false;
	TestEqual(TEXT("JSON preserves the source export origin"), RoundTrip.ExportBounds->Min, Data.ExportBounds->Min);
	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/SourceExportBounds");
	DeleteGeneratedAssetsAtPath(OutputPath);
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Source export generates native widgets"), Blueprint)) return false;
	TestEqual(TEXT("Blueprint retains layout size"), FUMGWidgetRenderer::GetBlueprintDesignTimeSize(Blueprint).GetValue(), FVector2D(45,45));
	TestEqual(TEXT("Blueprint stores independent export size"), FUMGWidgetRenderer::GetBlueprintExportBounds(Blueprint).GetSize(), FVector2D(81,81));
	{
		FScopedTransaction Transaction(FText::FromString(TEXT("Source export geometry")));
		FUMGWidgetRenderer::SetBlueprintExportBounds(Blueprint, FBox2D(FVector2D::ZeroVector, FVector2D(90,90)));
	}
	TestTrue(TEXT("Export bounds change can be undone"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores export geometry"), FUMGWidgetRenderer::GetBlueprintExportBounds(Blueprint).GetSize(), FVector2D(81,81));
	TestTrue(TEXT("Export bounds change can be redone"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores changed export geometry"), FUMGWidgetRenderer::GetBlueprintExportBounds(Blueprint).GetSize(), FVector2D(90,90));
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
