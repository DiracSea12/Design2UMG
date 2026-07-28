// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportUtils.h"
#include "UMGGenerator.h"

#include "WidgetBlueprint.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGSaveWidgetBlueprintFailureTest,
	"Design2UMG.Generation.SaveWidgetBlueprintFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGSaveWidgetBlueprintFailureTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("Widget Blueprint is null"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("missing valid DesignTimeSize"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Null Blueprint cannot be reported as saved"), FUMGGenerator::SaveWidgetBlueprint(nullptr));

	UWidgetBlueprint* BlueprintWithoutDesignSize = NewObject<UWidgetBlueprint>(GetTransientPackage());
	TestNotNull(TEXT("Transient Blueprint created"), BlueprintWithoutDesignSize);
	if (BlueprintWithoutDesignSize)
	{
		TestFalse(
			TEXT("Blueprint without a source design size cannot be reported as saved"),
			FUMGGenerator::SaveWidgetBlueprint(BlueprintWithoutDesignSize));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGInvalidGenerationClearsArtifactOutputsTest,
	"Design2UMG.Generation.InvalidGenerationClearsArtifactOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGInvalidGenerationClearsArtifactOutputsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("source data did not provide a valid root design size"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Widget Blueprint build failed"), EAutomationExpectedErrorFlags::Contains, 1);

	FUMGWidgetData InvalidData;
	InvalidData.WidgetName = TEXT("WBP_InvalidSaveSemantics");

	FString GeneratedAssetPath = TEXT("stale-asset-path");
	FString RenderPngPath = TEXT("stale-render-path");
	TestFalse(
		TEXT("Invalid source data fails Blueprint and PNG generation"),
		DesignImportUtils::GenerateBlueprintAndPng(
			InvalidData,
			TEXT("/Game/Design2UMG/Automation/SaveSemantics"),
			&GeneratedAssetPath,
			&RenderPngPath));
	TestTrue(TEXT("Failed generation clears generated asset path"), GeneratedAssetPath.IsEmpty());
	TestTrue(TEXT("Failed generation clears render path"), RenderPngPath.IsEmpty());

	InvalidData.WidgetName = TEXT("WBP_InvalidCompiledSaveSemantics");
	GeneratedAssetPath = TEXT("stale-asset-path");
	FIntPoint RenderedSize(321, 654);
	TestFalse(
		TEXT("Invalid source data fails compiled PNG generation"),
		DesignImportUtils::GenerateBlueprintAndCompiledPng(
			InvalidData,
			TEXT("/Game/Design2UMG/Automation/SaveSemantics"),
			TEXT("G:/invalid/not-written.png"),
			&GeneratedAssetPath,
			&RenderedSize));
	TestTrue(TEXT("Failed compiled generation clears generated asset path"), GeneratedAssetPath.IsEmpty());
	TestEqual(TEXT("Failed compiled generation clears rendered size"), RenderedSize, FIntPoint::ZeroValue);
	return true;
}

#endif
