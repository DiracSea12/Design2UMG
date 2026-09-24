// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Automation/Design2UMGAutomationRender.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Design2UMGSettings.h"
#include "DesignImportPipeline.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UMGSchema.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutomationCaseLocalIconFontPathClassifierTest,
	"Design2UMG.BulkImport.AutomationCaseLocalIconFontPathClassifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAutomationCaseLocalIconFontPathClassifierTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Case-local automation imported font path should be detected"),
		Design2UMGAutomationRender::IsCaseLocalImportedFontPath(
			TEXT("/Game/Design2UMG/Automation/pen-pencil-demo-dashboard/ImportedFonts/lucide.lucide")));
	TestTrue(TEXT("Nested case-local automation imported font path should be detected"),
		Design2UMGAutomationRender::IsCaseLocalImportedFontPath(
			TEXT("/Game/Design2UMG/Automation/figma-figma-export-local/primary/ImportedFonts/lucide.lucide")));
	TestFalse(TEXT("Pinned global automation imported font path should stay reusable"),
		Design2UMGAutomationRender::IsCaseLocalImportedFontPath(
			TEXT("/Game/Design2UMG/Automation/ImportedFonts/lucide.lucide")));
	TestFalse(TEXT("Normal project font path should not be treated as automation imported"),
		Design2UMGAutomationRender::IsCaseLocalImportedFontPath(
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIconFontMappingPersistenceAcrossGenerationModesTest,
	"Design2UMG.BulkImport.IconFontMappingPersistenceAcrossGenerationModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FIconFontMappingPersistenceAcrossGenerationModesTest::RunTest(const FString& Parameters)
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
		Settings->SaveConfig();
	};

	const FString OutputRoot = TEXT("/Game/Design2UMG/_TestOutput/IconFontMappingPersistence");
	Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputRoot);
	ON_SCOPE_EXIT
	{
		Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputRoot);
	};

	auto MakeInitialMap = []()
	{
		TMap<FString, TSoftObjectPtr<UFont>> Result;
		Result.Add(TEXT("permanent"), TSoftObjectPtr<UFont>(FSoftObjectPath(TEXT("/Game/Fonts/Permanent.Permanent"))));
		Result.Add(TEXT("remove-me"), TSoftObjectPtr<UFont>(FSoftObjectPath(TEXT("/Game/Fonts/RemoveMe.RemoveMe"))));
		Result.Add(TEXT("replace-me"), TSoftObjectPtr<UFont>(FSoftObjectPath(TEXT("/Game/Fonts/Old.Old"))));
		return Result;
	};

	const TArray<EDesignBlueprintGenerationMode> Modes = {
		EDesignBlueprintGenerationMode::BlueprintOnly,
		EDesignBlueprintGenerationMode::BlueprintAndPng,
		EDesignBlueprintGenerationMode::BlueprintAndCompiledPng
	};
	for (int32 ModeIndex = 0; ModeIndex < Modes.Num(); ++ModeIndex)
	{
		for (int32 PersistenceIndex = 0; PersistenceIndex < 2; ++PersistenceIndex)
		{
			const bool bPersistChanges = PersistenceIndex != 0;
			const TMap<FString, TSoftObjectPtr<UFont>> InitialMap = MakeInitialMap();
			Settings->IconFontAssetMap = InitialMap;

			FUMGWidgetData Data;
			Data.WidgetName = FString::Printf(
				TEXT("WBP_IconFontPersistence_%d_%s"),
				ModeIndex,
				bPersistChanges ? TEXT("Persistent") : TEXT("Transient"));
			Data.RootWidget.Type = TEXT("CanvasPanel");
			Data.RootWidget.Name = TEXT("Root");
			Data.RootWidget.Bounds = FDesignPixelBounds(0.0f, 0.0f, 64.0f, 64.0f);
			FUMGWidgetNode Surface;
			Surface.Type = TEXT("Border");
			Surface.Id = TEXT("surface");
			Surface.Name = TEXT("Surface");
			Surface.Bounds = FDesignPixelBounds(0.0f, 0.0f, 64.0f, 64.0f);
			Data.RootWidget.Children.Add(MoveTemp(Surface));
			Data.IconFontMappingChanges.Removals.Add(TEXT("remove-me"));
			Data.IconFontMappingChanges.Upserts.Add(TEXT("replace-me"), TEXT("/Game/Fonts/New.New"));
			Data.IconFontMappingChanges.Upserts.Add(TEXT("added"), TEXT("/Game/Fonts/Added.Added"));

			FArtifactGenerationRequest Request;
			Request.OutputPath = OutputRoot;
			Request.Generation.Mode = Modes[ModeIndex];
			Request.Generation.bPersistIconFontMappingChanges = bPersistChanges;
			Request.Generation.CompiledPngFilePath = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				FString::Printf(TEXT("Design2UMG/IconFontPersistence-%d-%d.png"), ModeIndex, PersistenceIndex));

			const FArtifactGenerationResult GenerationResult =
				DesignImportPipeline::GenerateArtifacts(Data, Request);
			TestTrue(
				FString::Printf(TEXT("Mode %d persistence %d generation succeeds"), ModeIndex, bPersistChanges),
				GenerationResult.bSucceeded);
			if (Modes[ModeIndex] == EDesignBlueprintGenerationMode::BlueprintAndCompiledPng
				&& GenerationResult.bSucceeded)
			{
				TestTrue(
					TEXT("Compiled pipeline returns the actual rendered widget instance"),
					GenerationResult.RenderedWidget.IsValid());
				if (GenerationResult.RenderedWidget.IsValid())
				{
					UWidget* RootWidget = GenerationResult.RenderedWidget->WidgetTree
						? GenerationResult.RenderedWidget->WidgetTree->RootWidget.Get()
						: nullptr;
					TestNotNull(TEXT("Rendered widget instance has a root widget"), RootWidget);
					bool bHasRenderedTreeGeometry = false;
					GenerationResult.RenderedWidget->WidgetTree->ForEachWidgetAndDescendants(
						[&bHasRenderedTreeGeometry](UWidget* Widget)
						{
							const FVector2D CachedSize = Widget->GetCachedGeometry().GetLocalSize();
							bHasRenderedTreeGeometry |= Widget->GetCachedWidget().IsValid()
								&& (CachedSize.X > 0.0f || CachedSize.Y > 0.0f);
						});
					TestTrue(
						TEXT("Rendered widget tree retains geometry from the completed Slate draw"),
						bHasRenderedTreeGeometry);
				}
			}
			IFileManager::Get().Delete(*Request.Generation.CompiledPngFilePath, false, true);

			if (!bPersistChanges)
			{
				TestTrue(
					FString::Printf(TEXT("Mode %d request-scoped changes leave settings untouched"), ModeIndex),
					Settings->IconFontAssetMap.OrderIndependentCompareEqual(InitialMap));
				continue;
			}

			TestEqual(
				FString::Printf(TEXT("Mode %d preserves unrelated permanent mapping"), ModeIndex),
				Settings->IconFontAssetMap.FindRef(TEXT("permanent")).ToString(),
				FString(TEXT("/Game/Fonts/Permanent.Permanent")));
			TestFalse(
				FString::Printf(TEXT("Mode %d applies mapping removal"), ModeIndex),
				Settings->IconFontAssetMap.Contains(TEXT("remove-me")));
			TestEqual(
				FString::Printf(TEXT("Mode %d applies mapping replacement"), ModeIndex),
				Settings->IconFontAssetMap.FindRef(TEXT("replace-me")).ToString(),
				FString(TEXT("/Game/Fonts/New.New")));
			TestEqual(
				FString::Printf(TEXT("Mode %d applies mapping addition"), ModeIndex),
				Settings->IconFontAssetMap.FindRef(TEXT("added")).ToString(),
				FString(TEXT("/Game/Fonts/Added.Added")));
		}
	}
	return true;
}
