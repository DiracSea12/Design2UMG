// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Automation/Design2UMGAutomationRender.h"

#include "Design2UMGSettings.h"
#include "DesignImportUtils.h"
#include "DesignImportPipeline.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UMGSchema.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutomationCaseLocalIconFontPathClassifierTest,
	"Design2UMG.BulkImport.AutomationCaseLocalIconFontPathClassifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAutomationCaseLocalIconFontPathClassifierTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Case-local automation imported font path should be detected"),
		DesignImportUtils::IsCaseLocalAutomationImportedFontPath(
			TEXT("/Game/Design2UMG/Automation/pen-pencil-demo-dashboard/ImportedFonts/lucide.lucide")));
	TestTrue(TEXT("Nested case-local automation imported font path should be detected"),
		DesignImportUtils::IsCaseLocalAutomationImportedFontPath(
			TEXT("/Game/Design2UMG/Automation/figma-figma-export-local/primary/ImportedFonts/lucide.lucide")));
	TestFalse(TEXT("Pinned global automation imported font path should stay reusable"),
		DesignImportUtils::IsCaseLocalAutomationImportedFontPath(
			TEXT("/Game/Design2UMG/Automation/ImportedFonts/lucide.lucide")));
	TestFalse(TEXT("Normal project font path should not be treated as automation imported"),
		DesignImportUtils::IsCaseLocalAutomationImportedFontPath(
			TEXT("/Game/Design2UMG/Fonts/lucide.lucide")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutomationSnapshotStripsCaseLocalIconFontMappingsTest,
	"Design2UMG.BulkImport.AutomationSnapshotStripsCaseLocalIconFontMappings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAutomationSnapshotStripsCaseLocalIconFontMappingsTest::RunTest(const FString& Parameters)
{
	FDesign2UMGImportSettingsSnapshot Snapshot;
	Snapshot.ConversionSettings.IconFontAssetPaths.Add(
		TEXT("lucide"),
		TEXT("/Game/Design2UMG/Automation/pen-pencil-demo-dashboard/ImportedFonts/lucide.lucide"));
	Snapshot.ConversionSettings.IconFontAssetPaths.Add(
		TEXT("material-symbols"),
		TEXT("/Game/Design2UMG/Fonts/MaterialSymbols.MaterialSymbols"));
	Snapshot.ConversionSettings.DefaultIconFontAssetPath =
		TEXT("/Game/Design2UMG/Automation/figma-figma-export-local/primary/ImportedFonts/defaulticon.defaulticon");

	Design2UMGAutomationRender::SanitizeImportSettingsSnapshot(Snapshot);

	TestFalse(TEXT("Case-local automation icon font mapping should be removed from automation snapshots"),
		Snapshot.ConversionSettings.IconFontAssetPaths.Contains(TEXT("lucide")));
	TestEqual(TEXT("Non-automation icon font mapping should stay intact"),
		Snapshot.ConversionSettings.IconFontAssetPaths.FindRef(TEXT("material-symbols")),
		TEXT("/Game/Design2UMG/Fonts/MaterialSymbols.MaterialSymbols"));
	TestTrue(TEXT("Case-local automation default icon font path should be cleared"),
		Snapshot.ConversionSettings.DefaultIconFontAssetPath.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FApplyPendingIconFontMappingChangesSkipsTransientAutomationPathsTest,
	"Design2UMG.BulkImport.ApplyPendingIconFontMappingChangesSkipsTransientAutomationPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FApplyPendingIconFontMappingChangesSkipsTransientAutomationPathsTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!Settings)
	{
		AddError(TEXT("Design2UMG settings were unavailable"));
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMap = Settings->IconFontAssetMap;
	ON_SCOPE_EXIT
	{
		Settings->IconFontAssetMap = SavedMap;
	};

	Settings->IconFontAssetMap.Empty();
	FUMGWidgetData Data;
	Data.IconFontMappingChanges.Upserts.Add(
		TEXT("lucide"),
		TEXT("/Game/Design2UMG/Automation/pen-pencil-demo-dashboard/ImportedFonts/lucide.lucide"));

	DesignImportUtils::ApplyPendingIconFontMappingChanges(Data);
	TestFalse(TEXT("Transient automation icon font paths should not be stored in global settings"),
		Settings->IconFontAssetMap.Contains(TEXT("lucide")));
	return true;
}
