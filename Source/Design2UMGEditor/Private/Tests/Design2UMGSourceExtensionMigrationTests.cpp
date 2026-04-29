// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "Design2UMGPrototypeNavigationExtensions.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FDesign2UMGFigmaWidgetMetadataRecord MakeFigmaRecord(const bool bLegacyValue, const bool bLegacyAuthored)
	{
		FDesign2UMGFigmaWidgetMetadataRecord Record;
		Record.WidgetName = TEXT("CardTitle");
		Record.WidgetId = TEXT("widget-card-title");
		Record.SourceNode.SourceNodeId = TEXT("node-1");
		Record.SourceNode.SourceNodeType = TEXT("INSTANCE");
		Record.SourceNode.bIsExposedInstance = bLegacyValue;
		Record.SourceNode.bHasIsExposedInstance = bLegacyAuthored;
		return Record;
	}

	FUMGPrototypeNavigationBinding MakeNavigationBinding()
	{
		FUMGPrototypeNavigationBinding Binding;
		Binding.TriggerWidgetId = TEXT("trigger-1");
		Binding.TriggerWidgetName = TEXT("PrimaryButton");
		Binding.Action = EUMGPrototypeNavigationAction::Navigate;
		Binding.TargetPageId = TEXT("page-2");
		Binding.TargetPageIndex = 1;

		FUMGPrototypeNavigationStep& Step = Binding.Steps.AddDefaulted_GetRef();
		Step.Kind = EUMGPrototypeNavigationStepKind::Navigate;
		Step.TargetPageId = TEXT("page-3");
		Step.TargetPageIndex = 2;
		return Binding;
	}

	FUMGPrototypeNavigationData MakeNavigationData(FUMGPrototypeNavigationBinding Binding)
	{
		FUMGPrototypeNavigationData Data;
		Data.SwitcherWidgetId = TEXT("switcher-1");
		Data.SwitcherWidgetName = TEXT("PageSwitcher");
		Data.PageIds = { TEXT("page-1"), TEXT("page-2"), TEXT("page-3") };
		Data.ClickBindings.Add(MoveTemp(Binding));
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaMetadataPostLoadMigratesLegacyExposureFlagTest,
	"Design2UMG.Unit.SourceExtensions.FigmaMetadata.PostLoadMigratesLegacyExposureFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaMetadataPostLoadMigratesLegacyExposureFlagTest::RunTest(const FString& Parameters)
{
	UDesign2UMGFigmaMetadataClassExtension* Extension =
		NewObject<UDesign2UMGFigmaMetadataClassExtension>(GetTransientPackageAsObject());
	TestTrue(TEXT("Figma metadata extension created"), Extension != nullptr);
	if (!Extension)
	{
		return false;
	}

	TArray<FDesign2UMGFigmaWidgetMetadataRecord> Records;
	Records.Add(MakeFigmaRecord(true, true));
	Extension->SetMetadata(FUMGSourceDocumentData(), Records);
	Extension->PostLoad();

	TestTrue(TEXT("One valid record kept"), Extension->GetWidgetRecords().Num() == 1);
	if (Extension->GetWidgetRecords().Num() != 1)
	{
		return false;
	}

	const FUMGSourceNodeData& SourceNode = Extension->GetWidgetRecords()[0].SourceNode;
	TestTrue(TEXT("Legacy exposure flag migrates into optional wrapper"), SourceNode.HasIsExposedInstance.bIsSet);
	TestTrue(TEXT("Migrated optional wrapper preserves legacy value"), SourceNode.HasIsExposedInstance.bValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaMetadataPostLoadKeepsExistingExposureOverrideTest,
	"Design2UMG.Unit.SourceExtensions.FigmaMetadata.PostLoadKeepsExistingExposureOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaMetadataPostLoadKeepsExistingExposureOverrideTest::RunTest(const FString& Parameters)
{
	UDesign2UMGFigmaMetadataClassExtension* Extension =
		NewObject<UDesign2UMGFigmaMetadataClassExtension>(GetTransientPackageAsObject());
	TestTrue(TEXT("Figma metadata extension created"), Extension != nullptr);
	if (!Extension)
	{
		return false;
	}

	FDesign2UMGFigmaWidgetMetadataRecord Record = MakeFigmaRecord(true, true);
	Record.SourceNode.HasIsExposedInstance.bIsSet = true;
	Record.SourceNode.HasIsExposedInstance.bValue = false;

	TArray<FDesign2UMGFigmaWidgetMetadataRecord> Records;
	Records.Add(Record);
	Extension->SetMetadata(FUMGSourceDocumentData(), Records);
	Extension->PostLoad();

	const FUMGSourceNodeData& SourceNode = Extension->GetWidgetRecords()[0].SourceNode;
	TestTrue(TEXT("Existing optional wrapper stays marked as authored"), SourceNode.HasIsExposedInstance.bIsSet);
	TestFalse(TEXT("Existing optional wrapper is not overwritten by legacy bool"), SourceNode.HasIsExposedInstance.bValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaMetadataPostLoadKeepsLegacyUnsetExposureStateTest,
	"Design2UMG.Unit.SourceExtensions.FigmaMetadata.PostLoadKeepsLegacyUnsetExposureState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaMetadataPostLoadKeepsLegacyUnsetExposureStateTest::RunTest(const FString& Parameters)
{
	UDesign2UMGFigmaMetadataClassExtension* Extension =
		NewObject<UDesign2UMGFigmaMetadataClassExtension>(GetTransientPackageAsObject());
	TestTrue(TEXT("Figma metadata extension created"), Extension != nullptr);
	if (!Extension)
	{
		return false;
	}

	TArray<FDesign2UMGFigmaWidgetMetadataRecord> Records;
	Records.Add(MakeFigmaRecord(true, false));
	Extension->SetMetadata(FUMGSourceDocumentData(), Records);
	Extension->PostLoad();

	const FUMGSourceNodeData& SourceNode = Extension->GetWidgetRecords()[0].SourceNode;
	TestFalse(TEXT("Legacy unset exposure state stays unset after migration"), SourceNode.HasIsExposedInstance.bIsSet);
	TestFalse(TEXT("Legacy unset exposure state does not smuggle authored true"), SourceNode.HasIsExposedInstance.bValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeNavigationPostLoadMigratesLegacyMatchLayersTest,
	"Design2UMG.Unit.SourceExtensions.PrototypeNavigation.PostLoadMigratesLegacyMatchLayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrototypeNavigationPostLoadMigratesLegacyMatchLayersTest::RunTest(const FString& Parameters)
{
	UDesign2UMGPrototypeNavigationClassExtension* Extension =
		NewObject<UDesign2UMGPrototypeNavigationClassExtension>(GetTransientPackageAsObject());
	TestTrue(TEXT("Prototype navigation extension created"), Extension != nullptr);
	if (!Extension)
	{
		return false;
	}

	FUMGPrototypeNavigationBinding Binding = MakeNavigationBinding();
	Binding.Transition.bMatchLayers = true;
	Binding.Transition.bHasMatchLayers = true;
	Binding.Steps[0].Transition.bMatchLayers = false;
	Binding.Steps[0].Transition.bHasMatchLayers = true;
	Extension->SetNavigationData(MakeNavigationData(MoveTemp(Binding)));
	Extension->PostLoad();

	const FUMGPrototypeNavigationData& NavigationData = Extension->GetNavigationDataForTest();
	TestTrue(TEXT("One binding stored"), NavigationData.ClickBindings.Num() == 1);
	if (NavigationData.ClickBindings.Num() != 1)
	{
		return false;
	}

	const FUMGPrototypeNavigationBinding& StoredBinding = NavigationData.ClickBindings[0];
	TestTrue(TEXT("Binding transition migrates into optional wrapper"), StoredBinding.Transition.HasMatchLayers.bIsSet);
	TestTrue(TEXT("Binding transition keeps legacy value"), StoredBinding.Transition.HasMatchLayers.bValue);
	TestTrue(TEXT("Step transition migrates into optional wrapper"), StoredBinding.Steps[0].Transition.HasMatchLayers.bIsSet);
	TestFalse(TEXT("Step transition keeps legacy value"), StoredBinding.Steps[0].Transition.HasMatchLayers.bValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeNavigationPostLoadKeepsExistingMatchLayersOverrideTest,
	"Design2UMG.Unit.SourceExtensions.PrototypeNavigation.PostLoadKeepsExistingMatchLayersOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrototypeNavigationPostLoadKeepsExistingMatchLayersOverrideTest::RunTest(const FString& Parameters)
{
	UDesign2UMGPrototypeNavigationClassExtension* Extension =
		NewObject<UDesign2UMGPrototypeNavigationClassExtension>(GetTransientPackageAsObject());
	TestTrue(TEXT("Prototype navigation extension created"), Extension != nullptr);
	if (!Extension)
	{
		return false;
	}

	FUMGPrototypeNavigationBinding Binding = MakeNavigationBinding();
	Binding.Transition.bMatchLayers = true;
	Binding.Transition.bHasMatchLayers = true;
	Binding.Transition.HasMatchLayers.bIsSet = true;
	Binding.Transition.HasMatchLayers.bValue = false;
	Binding.Steps[0].Transition.bMatchLayers = false;
	Binding.Steps[0].Transition.bHasMatchLayers = true;
	Binding.Steps[0].Transition.HasMatchLayers.bIsSet = true;
	Binding.Steps[0].Transition.HasMatchLayers.bValue = true;
	Extension->SetNavigationData(MakeNavigationData(MoveTemp(Binding)));
	Extension->PostLoad();

	const FUMGPrototypeNavigationBinding& StoredBinding = Extension->GetNavigationDataForTest().ClickBindings[0];
	TestTrue(TEXT("Binding optional wrapper stays authored"), StoredBinding.Transition.HasMatchLayers.bIsSet);
	TestFalse(TEXT("Binding optional wrapper is not overwritten"), StoredBinding.Transition.HasMatchLayers.bValue);
	TestTrue(TEXT("Step optional wrapper stays authored"), StoredBinding.Steps[0].Transition.HasMatchLayers.bIsSet);
	TestTrue(TEXT("Step optional wrapper is not overwritten"), StoredBinding.Steps[0].Transition.HasMatchLayers.bValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPrototypeNavigationPostLoadKeepsLegacyUnsetMatchLayersStateTest,
	"Design2UMG.Unit.SourceExtensions.PrototypeNavigation.PostLoadKeepsLegacyUnsetMatchLayersState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPrototypeNavigationPostLoadKeepsLegacyUnsetMatchLayersStateTest::RunTest(const FString& Parameters)
{
	UDesign2UMGPrototypeNavigationClassExtension* Extension =
		NewObject<UDesign2UMGPrototypeNavigationClassExtension>(GetTransientPackageAsObject());
	TestTrue(TEXT("Prototype navigation extension created"), Extension != nullptr);
	if (!Extension)
	{
		return false;
	}

	FUMGPrototypeNavigationBinding Binding = MakeNavigationBinding();
	Binding.Transition.bMatchLayers = true;
	Binding.Transition.bHasMatchLayers = false;
	Binding.Steps[0].Transition.bMatchLayers = true;
	Binding.Steps[0].Transition.bHasMatchLayers = false;
	Extension->SetNavigationData(MakeNavigationData(MoveTemp(Binding)));
	Extension->PostLoad();

	const FUMGPrototypeNavigationBinding& StoredBinding = Extension->GetNavigationDataForTest().ClickBindings[0];
	TestFalse(TEXT("Binding legacy unset state stays unset after migration"), StoredBinding.Transition.HasMatchLayers.bIsSet);
	TestFalse(TEXT("Binding legacy unset state does not smuggle authored true"), StoredBinding.Transition.HasMatchLayers.bValue);
	TestFalse(TEXT("Step legacy unset state stays unset after migration"), StoredBinding.Steps[0].Transition.HasMatchLayers.bIsSet);
	TestFalse(TEXT("Step legacy unset state does not smuggle authored true"), StoredBinding.Steps[0].Transition.HasMatchLayers.bValue);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
