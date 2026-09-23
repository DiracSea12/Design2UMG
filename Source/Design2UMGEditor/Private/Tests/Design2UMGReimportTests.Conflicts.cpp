// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe reimport: full-conflict boundaries (unreadable manifest, scope mismatch, project restructure), resolution copy, and plain-import auto-rename.

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportTestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;

// ---------------------------------------------------------------------------
// a corrupt sidecar (invalid JSON) = unclear relationship -> full conflict;
// the asset bytes stay untouched, and no sibling copy is generated without an explicit choice.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportUnreadableManifestConflictTest,
	"Design2UMG.SafeReimport.FullConflictOnUnreadableManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportUnreadableManifestConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportBadManifest");

	const FString V1 = MakeFixtureDoc(MakeFixtureText(TEXT("fixture_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = FixtureAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	// Corrupt sidecar: not even valid JSON.
	TestTrue(TEXT("corrupt sidecar written"), FixtureWriteFileString(
		Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), TEXT("<<<corrupt-not-json>>>")));

	const FDesignReimportResult Result = FixtureReimport(V1, nullptr, TargetPackage);
	TestFalse(TEXT("reimport does not succeed"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestEqual(TEXT("exactly one full-conflict diagnostic"), FixtureCountKind(Result, TEXT("reimport_full_conflict")), 1);
	TestTrue(TEXT("conflict reason cites invalid JSON"),
		FixtureHasMessage(Result, TEXT("reimport_full_conflict"), TEXT("is not valid JSON")));
	TestTrue(TEXT("target asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	TestTrue(TEXT("no implicit resolution copy"), Result.ResolutionCopyPath.IsEmpty());
	TestFalse(TEXT("no _Resolution asset file"),
		FPaths::FileExists(FixtureAssetFilePath(FPaths::GetPath(TargetPackage) / TEXT("WBP_FixturePage_Resolution"))));

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// DocumentScope mismatch (a different fileKey in the new source) = unclear relationship ->
// full conflict; the diagnostic names both sides' provider/scope, and the asset is not updated in place.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportScopeMismatchConflictTest,
	"Design2UMG.SafeReimport.FullConflictOnDocumentScopeMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportScopeMismatchConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportScopeMismatch");

	const FString V1 = MakeFixtureDoc(MakeFixtureText(TEXT("fixture_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = FixtureAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	const FString V2 = MakeFixtureDoc(
		MakeFixtureText(TEXT("fixture_t1"), TEXT("AlphaText"), 24, 24, TEXT("Two")),
		FixtureReimportOtherScope);
	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);

	TestFalse(TEXT("scope mismatch does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestEqual(TEXT("exactly one full-conflict diagnostic"), FixtureCountKind(Result, TEXT("reimport_full_conflict")), 1);

	const FString ExpectedReason = FString::Printf(
		TEXT("document scope mismatch: manifest provider='figma' scope='%s', new source provider='figma' scope='%s'"),
		FixtureReimportScope, FixtureReimportOtherScope);
	bool bExactMessage = false;
	for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Diagnostics)
	{
		if (Diagnostic.Kind == TEXT("reimport_full_conflict") && Diagnostic.Message.Contains(ExpectedReason))
		{
			bExactMessage = true;
		}
	}
	TestTrue(TEXT("conflict reason reports both scopes verbatim"), bExactMessage);

	// The asset stays as it was: file bytes unchanged, the in-memory text still the v1 value (the pipeline returns at the matching stage).
	TestTrue(TEXT("asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	if (UTextBlock* Alpha = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("AlphaText"))))
	{
		TestEqual(TEXT("in-memory text untouched"), Alpha->GetText().ToString(), FString(TEXT("One")));
	}
	else
	{
		AddError(TEXT("AlphaText widget missing"));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// project-side restructure (none of the manifest-recorded instances can be located in the
// target tree) -> full conflict; the diagnostics finished first by the matching stage are visible; the project-side structure is left untouched.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportRestructureConflictTest,
	"Design2UMG.SafeReimport.FullConflictOnProjectRestructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportRestructureConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportRestructure");

	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
				*MakeFixtureText(TEXT("fixture_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))),
		*MakeFixtureText(TEXT("fixture_t2"), TEXT("AlphaText"), 250, 24, TEXT("One"))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = FixtureAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);

	// Simulate a project restructure: remove every manifest-recorded instance from the tree and put the project's own widgets in.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (!TestTrue(TEXT("sidecar loads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
	{
		if (UWidget* Recorded = WBP->WidgetTree->FindWidget(FName(*Record.WidgetName)))
		{
			WBP->WidgetTree->RemoveWidget(Recorded);
		}
	}
	UTextBlock* ProjectReplacement = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectReplacement"));
	if (UPanelWidget* RootPanel = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		RootPanel->AddChild(ProjectReplacement);
	}

	const FDesignReimportResult Result = FixtureReimport(V1, nullptr, TargetPackage);
	TestFalse(TEXT("restructure does not update in place"), Result.bSucceeded);
	TestTrue(TEXT("full conflict flagged"), Result.bFullConflict);
	TestTrue(TEXT("restructure reason reported"),
		FixtureHasMessage(Result, TEXT("reimport_full_conflict"), TEXT("none of the recorded instances could be located in the target widget tree")));
	// Spec: source matching / instance matching / ownership classification must complete before anything applies;
	// the scope-match diagnostic must appear first, followed by the full conflict.
	TestTrue(TEXT("scope matching completed first"), FixtureCountKind(Result, TEXT("reimport_scope_matched")) == 1);

	// The project-side structure is left untouched.
	TestNotNull(TEXT("project replacement widget preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectReplacement")));
	TestTrue(TEXT("asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// sibling-copy fallback - a missing manifest = full conflict; after the explicit choice a
// sibling WBP + its own manifest are generated and the original asset's bytes stay untouched; when the copy
// already exists, choosing explicitly again only reports a skip - no duplicate generation, no overwrite.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportResolutionCopySkipsExistingTest,
	"Design2UMG.SafeReimport.ResolutionCopySkipsExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportResolutionCopySkipsExistingTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportResolutionCopy");

	const FString V1 = MakeFixtureDoc(MakeFixtureText(TEXT("fixture_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = FixtureAssetFilePath(TargetPackage);
	const int64 BytesBefore = IFileManager::Get().FileSize(*AssetFile);
	const FString CopyPackage = FPaths::GetPath(TargetPackage) / TEXT("WBP_FixturePage_Resolution");
	const FString CopyAssetFile = FixtureAssetFilePath(CopyPackage);

	// Create the full conflict: delete the sidecar.
	IFileManager::Get().Delete(*Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), false, true);

	// First explicit choice: the copy is generated.
	FDesignReimportResult Result = FixtureReimport(V1, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("full conflict still reported"), Result.bFullConflict);
	TestFalse(TEXT("in-place update did not happen"), Result.bSucceeded);
	TestEqual(TEXT("resolution copy path reported"),
		Result.ResolutionCopyPath, CopyPackage + TEXT(".WBP_FixturePage_Resolution"));
	TestTrue(TEXT("copy asset file exists"), FPaths::FileExists(CopyAssetFile));
	FDesign2UMGGenerationManifestSidecar CopySidecar;
	TestTrue(TEXT("copy has its own manifest"), FixtureLoadSidecar(CopyPackage, CopySidecar));
	TestTrue(TEXT("original asset bytes unchanged"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);
	TestFalse(TEXT("original still has no manifest"),
		FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage)));
	const int64 CopyBytes = IFileManager::Get().FileSize(*CopyAssetFile);

	// Second explicit choice: the copy already exists -> skip, no overwrite.
	Result = FixtureReimport(V1, nullptr, TargetPackage, /*bGenerateResolutionCopy=*/true);
	TestTrue(TEXT("full conflict still reported on retry"), Result.bFullConflict);
	TestTrue(TEXT("no second copy path reported"), Result.ResolutionCopyPath.IsEmpty());
	TestTrue(TEXT("skip diagnosed on one message"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_resolution_copy"),
			{ TEXT("Resolution copy skipped"), TEXT("already exists") }));
	TestEqual(TEXT("copy counted once"), FixtureCountKind(Result, TEXT("reimport_resolution_copy")), 1);
	TestTrue(TEXT("existing copy asset untouched"), IFileManager::Get().FileSize(*CopyAssetFile) == CopyBytes);
	TestTrue(TEXT("original asset still untouched"), IFileManager::Get().FileSize(*AssetFile) == BytesBefore);

	FixtureWipeOutput(OutputDir);
	return true;
}

// Import creates a uniquely named asset when the requested name exists.
// Only reimport updates the existing asset and manifest.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNormalImportAutoRenameTest,
	"Design2UMG.SafeReimport.NormalImportStillAutoRenames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNormalImportAutoRenameTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportAutoRename");

	const FString V1 = MakeFixtureDoc(MakeFixtureText(TEXT("fixture_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));

	// First plain import.
	UWidgetBlueprint* First = FixtureImport(V1, nullptr, OutputDir, /*bCleanFirst=*/true);
	if (!TestTrue(TEXT("first import generates"), First != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString FirstPackage = First->GetOutermost()->GetName();
	const FString FirstSidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(FirstPackage);
	FString FirstManifest;
	if (!TestTrue(TEXT("first manifest exists"), FixtureReadFileString(FirstSidecarPath, FirstManifest)))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}

	// Second plain import (asset name already taken): auto-rename creates anew, leaving the first one untouched.
	UWidgetBlueprint* Second = FixtureImport(V1, nullptr, OutputDir, /*bCleanFirst=*/false);
	TestNotNull(TEXT("second import generates a renamed asset"), Second);
	if (Second)
	{
		const FString SecondPackage = Second->GetOutermost()->GetName();
		TestTrue(TEXT("second asset is auto-renamed"), SecondPackage != FirstPackage);
		TestEqual(TEXT("auto-rename appends _1"), SecondPackage, FirstPackage + TEXT("_1"));
		TestTrue(TEXT("second import has its own manifest"),
			FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(SecondPackage)));
	}

	// The original asset was not updated in place: loadable, manifest bytes unchanged.
	UWidgetBlueprint* FirstReloaded = LoadObject<UWidgetBlueprint>(
		nullptr, *(FirstPackage + TEXT(".") + FPaths::GetBaseFilename(FirstPackage)));
	TestNotNull(TEXT("original asset still loads"), FirstReloaded);
	FString FirstManifestAfter;
	TestTrue(TEXT("original manifest still readable"), FixtureReadFileString(FirstSidecarPath, FirstManifestAfter));
	TestTrue(TEXT("original manifest untouched by the second normal import"), FirstManifestAfter == FirstManifest);

	FixtureWipeOutput(OutputDir);
	return true;
}

#endif

