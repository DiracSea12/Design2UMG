// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportGeneration.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "Blueprint/UserWidget.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGRenderDimensionLimitTest,
	"Design2UMG.Generation.RenderDimensionLimit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGRenderDimensionLimitTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("exceeds max texture dimension"), EAutomationExpectedErrorFlags::Contains, 1);
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

	// Oversize the design-time size beyond the active RHI's 2D texture limit:
	// the render entry must fail structurally with an explicit reason instead of
	// submitting an oversized render target to the RHI (which asserts and kills the editor).
	UUserWidget* GeneratedCDO = Cast<UUserWidget>(WidgetBP->GeneratedClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Generated class default object exists"), GeneratedCDO))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	GeneratedCDO->DesignTimeSize = FVector2D(
		1024.0f,
		static_cast<float>(GetMax2DTextureDimension()) + 1024.0f);

	const FString PngPath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("D2URenderLimit"), TEXT(".png"));
	FIntPoint RenderedSize = FIntPoint::ZeroValue;
	FString Error;
	const bool bRendered = DesignImportGeneration::RenderCompiledBlueprintToPngFile(WidgetBP, PngPath, &RenderedSize, &Error);
	TestFalse(TEXT("Oversized render target is rejected before RHI submission"), bRendered);
	TestTrue(
		TEXT("Rejection carries the structured dimension-limit error message"),
		Error.Contains(TEXT("render target exceeds max texture dimension: 1024x")));
	TestFalse(TEXT("No PNG is written for the rejected render"), IFileManager::Get().FileExists(*PngPath));
	IFileManager::Get().Delete(*PngPath);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
