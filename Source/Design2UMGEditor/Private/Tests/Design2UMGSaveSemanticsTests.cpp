// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportUtils.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

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
	FDesign2UMGFailedSaveDiscardsUnsavedAssetTest,
	"Design2UMG.Generation.FailedSaveDiscardsUnsavedAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGFailedSaveDiscardsUnsavedAssetTest::RunTest(const FString& Parameters)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/FailedSaveCleanupV2");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_FailedSaveCleanupV2");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds.W = 96.0f;
	Data.RootWidget.Bounds.H = 64.0f;

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint is built before the forced save failure"), Blueprint))
	{
		return false;
	}

	const FSoftObjectPath OriginalObjectPath(Blueprint);
	UPackage* TargetPackage = Blueprint->GetOutermost();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	UUserWidget* GeneratedCDO = Blueprint->GeneratedClass
		? Cast<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!TestNotNull(TEXT("Generated Blueprint CDO exists"), GeneratedCDO))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	GeneratedCDO->DesignTimeSize = FVector2D::ZeroVector;

	AddExpectedError(TEXT("missing valid DesignTimeSize"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Invalid Blueprint save fails"), FUMGGenerator::SaveWidgetBlueprint(Blueprint));
	TestFalse(TEXT("Failed save leaves no registered asset"), AssetRegistry.GetAssetByObjectPath(OriginalObjectPath).IsValid());
	TestFalse(TEXT("Failed save clears the target package dirty flag"), TargetPackage->IsDirty());
	TestEqual(TEXT("Failed save moves the Blueprint out of the target package"), Blueprint->GetOutermost(), GetTransientPackage());

	Data.WidgetName = TEXT("WBP_ExistingFailedSave");
	UWidgetBlueprint* ExistingBlueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Existing Blueprint is built before its baseline save"), ExistingBlueprint))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Existing Blueprint baseline save succeeds"), FUMGGenerator::SaveWidgetBlueprint(ExistingBlueprint));
	UPackage* ExistingPackage = ExistingBlueprint->GetOutermost();
	const FSoftObjectPath ExistingObjectPath(ExistingBlueprint);
	TestTrue(TEXT("Existing Blueprint is registered before save"), AssetRegistry.GetAssetByObjectPath(ExistingObjectPath).IsValid());
	UUserWidget* ExistingCDO = ExistingBlueprint->GeneratedClass
		? Cast<UUserWidget>(ExistingBlueprint->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (!TestNotNull(TEXT("Existing Blueprint CDO exists"), ExistingCDO))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	ExistingCDO->DesignTimeSize = FVector2D::ZeroVector;
	TestFalse(TEXT("Invalid existing Blueprint save fails"), FUMGGenerator::SaveWidgetBlueprint(ExistingBlueprint));
	TestEqual(TEXT("Failed save keeps an existing Blueprint in its package"), ExistingBlueprint->GetOutermost(), ExistingPackage);
	TestTrue(TEXT("Failed save keeps an existing Blueprint registered"), AssetRegistry.GetAssetByObjectPath(ExistingObjectPath).IsValid());

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGInvalidGenerationClearsArtifactOutputsTest,
	"Design2UMG.Generation.InvalidGenerationClearsArtifactOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGInvalidGenerationClearsArtifactOutputsTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("source data did not provide a valid root design size"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Root design size is invalid"), EAutomationExpectedErrorFlags::Contains, 2);
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

namespace
{
	FUMGWidgetData MakeSaveSemanticsWidgetData(const TCHAR* WidgetName)
	{
		FUMGWidgetData Data;
		Data.WidgetName = WidgetName;
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Id = TEXT("root");
		Data.RootWidget.Name = TEXT("Root");
		Data.RootWidget.Bounds.W = 96.0f;
		Data.RootWidget.Bounds.H = 64.0f;
		return Data;
	}
}

// A reload (ResetLoaders + ReloadPackage + CollectGarbage) of a blueprint whose
// package has unsaved changes would discard those changes. The pre-render reload
// must skip dirty packages and keep rendering the in-memory version.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGReloadSkipsDirtyBlueprintPackageTest,
	"Design2UMG.Generation.ReloadSkipsDirtyBlueprintPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGReloadSkipsDirtyBlueprintPackageTest::RunTest(const FString& Parameters)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/ReloadDirtyGuard");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGWidgetData Data = MakeSaveSemanticsWidgetData(TEXT("WBP_ReloadDirtyGuard"));
	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint is built before the dirty reload guard test"), Blueprint))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	if (!TestTrue(TEXT("Baseline save succeeds"), FUMGGenerator::SaveWidgetBlueprint(Blueprint)))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const FString AssetObjectPath = Blueprint->GetPathName();
	UWidgetBlueprint* Loaded = LoadObject<UWidgetBlueprint>(nullptr, *AssetObjectPath);
	if (!TestNotNull(TEXT("Saved blueprint loads"), Loaded))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UPackage* Package = Loaded->GetOutermost();

	UUserWidget* LoadedCDO = Loaded->GeneratedClass
		? Cast<UUserWidget>(Loaded->GeneratedClass->GetDefaultObject())
		: nullptr;
	const FVector2D UnsavedSize(321.0f, 123.0f);
	if (TestNotNull(TEXT("Loaded blueprint CDO exists"), LoadedCDO))
	{
		LoadedCDO->Modify();
		LoadedCDO->DesignTimeSize = UnsavedSize;
	}
	Package->MarkPackageDirty();
	if (!TestTrue(TEXT("Package is dirty before the reload"), Package->IsDirty()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	AddExpectedMessagePlain(
		TEXT("has unsaved changes; skipping pre-render reload"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		1);
	UWidgetBlueprint* Kept = DesignImportUtils::ReloadWidgetBlueprintForRender(Loaded);
	TestTrue(TEXT("Dirty package skips the reload and keeps the in-memory instance"), Kept == Loaded);
	TestTrue(TEXT("Dirty package stays dirty after the skipped reload"), Package->IsDirty());
	if (LoadedCDO)
	{
		TestEqual(TEXT("Unsaved in-memory edit survives the skipped reload"), LoadedCDO->DesignTimeSize, UnsavedSize);
	}

	// Clean-package control: after saving the edit, the reload path must run again.
	if (!TestTrue(TEXT("Save after the skipped reload succeeds"), FUMGGenerator::SaveWidgetBlueprint(Loaded)))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	if (!TestFalse(TEXT("Saved package is clean"), Package->IsDirty()))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	UWidgetBlueprint* Reloaded = DesignImportUtils::ReloadWidgetBlueprintForRender(Loaded);
	if (!TestNotNull(TEXT("Clean package reloads the blueprint"), Reloaded))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestTrue(TEXT("Clean package reload produces a fresh instance"), Reloaded != Loaded);
	if (UUserWidget* ReloadedCDO = Reloaded->GeneratedClass
		? Cast<UUserWidget>(Reloaded->GeneratedClass->GetDefaultObject())
		: nullptr)
	{
		TestEqual(TEXT("Reloaded blueprint carries the saved edit from disk"), ReloadedCDO->DesignTimeSize, UnsavedSize);
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

// Generation saves the asset to disk; a leftover editor transaction would let
// Ctrl+Z roll the in-memory state back while the saved .uasset keeps the import,
// leaving memory and disk inconsistent. Generation must not touch the undo stack.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGGenerationLeavesNoUndoTransactionTest,
	"Design2UMG.Generation.LeavesNoUndoTransaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGGenerationLeavesNoUndoTransactionTest::RunTest(const FString& Parameters)
{
	UTransactor* Transactor = GEditor ? GEditor->Trans.Get() : nullptr;
	if (!TestNotNull(TEXT("Editor transactor is available"), Transactor))
	{
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/Automation/GenerationNoUndo");
	DeleteGeneratedAssetsAtPath(OutputPath);

	FUMGWidgetData Data = MakeSaveSemanticsWidgetData(TEXT("WBP_GenerationNoUndo"));
	const int32 UndoCountBefore = Transactor->GetUndoCount();
	UWidgetBlueprint* Generated = DesignImportUtils::GenerateBlueprint(Data, OutputPath);
	if (!TestNotNull(TEXT("Blueprint generation succeeds"), Generated))
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}
	TestEqual(
		TEXT("Generation leaves no undo transaction behind"),
		Transactor->GetUndoCount(),
		UndoCountBefore);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif
