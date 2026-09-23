// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe reimport: generation-manifest schema, sidecar load rejection, save failure rollback, writability pre-check, and sidecar write-failure cases.

#include "Misc/AutomationTest.h"
#include "Tests/Design2UMGFileFailureFixture.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGComponentInputTestWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportTestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;

// The manifest stores input declarations, not design values; stored value fields are ignored.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportManifestInputSchemaTest,
	"Design2UMG.SafeReimport.ManifestInputSchemaDeclarationsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportManifestInputSchemaTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportManifestSchema");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	FDesign2UMGGenerationManifestPayload Payload;
	Payload.Provider = TEXT("figma");
	Payload.DocumentScope = TEXT("unit-scope-1");
	Payload.SourceRootNodeId = TEXT("fixture-unit-root");
	Payload.SourceRootName = TEXT("FixturePage");
	{
		FDesign2UMGMappedInstanceRecord Mapped;
		Mapped.DesignNodeId = TEXT("fixture-m-1");
		Mapped.WidgetName = TEXT("W1");
		Mapped.MappedKey = TEXT("figma|unit-scope-1|k");
		Mapped.MappedClass = TEXT("/Script/Design2UMGEditor.Design2UMGComponentInputTestWidget");
		Mapped.bMatchedAtSetLevel = true;
		FDesign2UMGMappedInputRecord Input;
		Input.Source = TEXT("label");
		Input.Target = TEXT("TextValue");
		Input.ValueKind = TEXT("ComponentProperty");
		Mapped.Inputs.Add(MoveTemp(Input));
		Payload.MappedInstances.Add(MoveTemp(Mapped));
	}
	{
		FDesign2UMGGeneratedInstanceRecord Generated;
		Generated.DesignNodeId = TEXT("fixture-g-1");
		Generated.WidgetName = TEXT("G1");
		Payload.TopLevelGenerated.Add(MoveTemp(Generated));
	}
	Payload.D2UOwnedNames = { TEXT("W1"), TEXT("G1") };

	if (!TestTrue(TEXT("WriteSidecarForAsset succeeds"), Design2UMGGenerationManifest::WriteSidecarForAsset(Package, Payload) == EDesign2UMGFileState::Saved))
	{
		IFileManager::Get().Delete(*SidecarPath, false, true);
		return false;
	}

	// Write side: the inputs object has exactly the three fields source/target/valueKind; value/bApplied are not persisted.
	FString RawJson;
	TestTrue(TEXT("sidecar file readable"), FixtureReadFileString(SidecarPath, RawJson));
	TSharedPtr<FJsonObject> Root;
	if (TestTrue(TEXT("sidecar JSON parses"), FixtureParseJsonObject(RawJson, Root)))
	{
		TestEqual(TEXT("schemaVersion is 1"), static_cast<int32>(Root->GetNumberField(TEXT("schemaVersion"))), 1);
		TestEqual(TEXT("provider written"), Root->GetStringField(TEXT("provider")), FString(TEXT("figma")));
		TestEqual(TEXT("documentScope written"), Root->GetStringField(TEXT("documentScope")), FString(TEXT("unit-scope-1")));
		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		if (TestTrue(TEXT("sourceRoot present"), Root->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot != nullptr))
		{
			TestEqual(TEXT("sourceRoot.nodeId written"), (*SourceRoot)->GetStringField(TEXT("nodeId")), FString(TEXT("fixture-unit-root")));
		}

		const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
		if (TestTrue(TEXT("instances present"), Root->TryGetArrayField(TEXT("instances"), Instances) && Instances != nullptr)
			&& TestEqual(TEXT("two instances written"), Instances->Num(), 2))
		{
			const TSharedPtr<FJsonObject> MappedObject = (*Instances)[0]->AsObject();
			const TSharedPtr<FJsonObject> GeneratedObject = (*Instances)[1]->AsObject();
			if (TestTrue(TEXT("mapped instance object"), MappedObject.IsValid())
				&& TestTrue(TEXT("generated instance object"), GeneratedObject.IsValid()))
			{
				TestEqual(TEXT("mapped kind"), MappedObject->GetStringField(TEXT("kind")), FString(TEXT("mapped")));
				TestEqual(TEXT("set-level flag written"), MappedObject->GetBoolField(TEXT("matchedAtSetLevel")), true);
				TestEqual(TEXT("mappedKey written"), MappedObject->GetStringField(TEXT("mappedKey")), FString(TEXT("figma|unit-scope-1|k")));

				const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
				if (TestTrue(TEXT("inputs array present"), MappedObject->TryGetArrayField(TEXT("inputs"), Inputs) && Inputs != nullptr)
					&& TestEqual(TEXT("one input written"), Inputs->Num(), 1))
				{
					const TSharedPtr<FJsonObject> InputObject = (*Inputs)[0]->AsObject();
					if (TestTrue(TEXT("input object"), InputObject.IsValid()))
					{
						TestEqual(TEXT("input source"), InputObject->GetStringField(TEXT("source")), FString(TEXT("label")));
						TestEqual(TEXT("input target"), InputObject->GetStringField(TEXT("target")), FString(TEXT("TextValue")));
						TestEqual(TEXT("input valueKind"), InputObject->GetStringField(TEXT("valueKind")), FString(TEXT("ComponentProperty")));
						TestFalse(TEXT("inputs[].value NOT serialized"), InputObject->HasField(TEXT("value")));
						TestFalse(TEXT("inputs[].bApplied NOT serialized"), InputObject->HasField(TEXT("bApplied")));
					}
				}
				TestEqual(TEXT("generated kind"), GeneratedObject->GetStringField(TEXT("kind")), FString(TEXT("generated")));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Owned = nullptr;
		if (TestTrue(TEXT("d2uOwned present"), Root->TryGetArrayField(TEXT("d2uOwned"), Owned) && Owned != nullptr)
			&& TestEqual(TEXT("d2uOwned count"), Owned->Num(), 2))
		{
			TestEqual(TEXT("d2uOwned[0]"), (*Owned)[0]->AsString(), FString(TEXT("W1")));
			TestEqual(TEXT("d2uOwned[1]"), (*Owned)[1]->AsString(), FString(TEXT("G1")));
		}
	}

	// Read side: the three declaration fields serialization as-is (the record structure no longer has value/bApplied fields to restore).
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("TryLoadSidecarForAsset succeeds"), FixtureLoadSidecar(Package, Sidecar)))
	{
		TestEqual(TEXT("loaded schemaVersion"), Sidecar.SchemaVersion, 1);
		TestEqual(TEXT("loaded provider"), Sidecar.Provider, FString(TEXT("figma")));
		TestEqual(TEXT("loaded documentScope"), Sidecar.DocumentScope, FString(TEXT("unit-scope-1")));
		TestEqual(TEXT("loaded sourceRoot id"), Sidecar.SourceRootNodeId, FString(TEXT("fixture-unit-root")));
		TestEqual(TEXT("loaded sourceRoot name"), Sidecar.SourceRootName, FString(TEXT("FixturePage")));
		TestEqual(TEXT("instance count"), Sidecar.Instances.Num(), 2);
		if (Sidecar.Instances.Num() == 2)
		{
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& M = Sidecar.Instances[0];
			TestEqual(TEXT("loaded mapped kind"), M.Kind, FString(TEXT("mapped")));
			TestTrue(TEXT("loaded set-level flag"), M.bMatchedAtSetLevel);
			TestEqual(TEXT("loaded mappedClass"), M.MappedClass, FString(FixtureMappedClassPath));
			if (TestEqual(TEXT("loaded input count"), M.Inputs.Num(), 1) && M.Inputs.Num() == 1)
			{
				TestEqual(TEXT("loaded input source"), M.Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("loaded input target"), M.Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("loaded input valueKind"), M.Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
			TestEqual(TEXT("loaded generated kind"), Sidecar.Instances[1].Kind, FString(TEXT("generated")));
			TestEqual(TEXT("loaded generated id"), Sidecar.Instances[1].DesignNodeId, FString(TEXT("fixture-g-1")));
		}
		TestTrue(TEXT("d2uOwned loaded"), Sidecar.D2UOwned.Contains(TEXT("W1")) && Sidecar.D2UOwned.Contains(TEXT("G1")));
	}

	// Insert an extra value field beside valueKind and verify declaration-only loading.
	FString ExtraValueJson = RawJson;
	const TCHAR* DeclarationNeedle = TEXT("\"ComponentProperty\"");
	ExtraValueJson.ReplaceInline(
		DeclarationNeedle,
		TEXT("\"ComponentProperty\",\"value\":\"extra-field-77f\""),
		ESearchCase::CaseSensitive);
	TestTrue(TEXT("extra-field value injected"), ExtraValueJson != RawJson);
	TestTrue(TEXT("extra-field sidecar written"), FixtureWriteFileString(SidecarPath, ExtraValueJson));

	FDesign2UMGGenerationManifestSidecar LoadedSidecar;
	if (TestTrue(TEXT("extra-field sidecar still loads"), FixtureLoadSidecar(Package, LoadedSidecar))
		&& TestEqual(TEXT("extra-field input count"), LoadedSidecar.Instances.Num() > 0 ? LoadedSidecar.Instances[0].Inputs.Num() : -1, 1)
		&& LoadedSidecar.Instances.Num() > 0 && LoadedSidecar.Instances[0].Inputs.Num() == 1)
	{
		TestEqual(TEXT("extra-field inputs[].value ignored; declarations intact"),
			LoadedSidecar.Instances[0].Inputs[0].Source, FString(TEXT("label")));
		TestEqual(TEXT("extra-field inputs[] declaration target intact"),
			LoadedSidecar.Instances[0].Inputs[0].Target, FString(TEXT("TextValue")));
		TestEqual(TEXT("extra-field inputs[] declaration valueKind intact"),
			LoadedSidecar.Instances[0].Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// error classification of sidecar read failures - missing file / invalid JSON / unrecognized schema
// version / missing provider|documentScope / empty instances each return false + a precise reason.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportManifestLoadRejectsBadSidecarsTest,
	"Design2UMG.SafeReimport.ManifestLoadRejectsBadSidecars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportManifestLoadRejectsBadSidecarsTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportManifestErrors");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	// 1) Missing file.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	FString Error;
	TestFalse(TEXT("missing sidecar rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-file reason"), Error.Contains(TEXT("generation manifest sidecar not found")));

	// 2) Invalid JSON.
	TestTrue(TEXT("garbage written"), FixtureWriteFileString(SidecarPath, TEXT("{ this is not json")));
	TestFalse(TEXT("invalid JSON rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("invalid-JSON reason"), Error.Contains(TEXT("generation manifest sidecar is not valid JSON")));

	// 3) Unrecognized schema version.
	TestTrue(TEXT("v2 schema written"), FixtureWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":2,\"provider\":\"figma\",\"documentScope\":\"s\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("unsupported schema rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("schema-version reason"), Error.Contains(TEXT("schema version 2 is not supported (expected 1)")));

	// 4) provider / documentScope missing.
	TestTrue(TEXT("empty provider written"), FixtureWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":1,\"provider\":\"\",\"documentScope\":\"\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("missing provider/documentScope rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-provider reason"), Error.Contains(TEXT("missing provider/documentScope")));

	// 5) Empty instances.
	TestTrue(TEXT("empty instances written"), FixtureWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":1,\"provider\":\"figma\",\"documentScope\":\"s\",\"sourceRoot\":{},\"instances\":[],\"d2uOwned\":[]}")));
	TestFalse(TEXT("empty instances rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("empty-instances reason"), Error.Contains(TEXT("records no instances")));
	for (const FString& Instances : {
		FString(TEXT("[{}]")),
		FString(TEXT("[null]")),
		FString(TEXT("[{\"kind\":\"generated\",\"designNodeId\":\"a\",\"widgetName\":\"A\"},{\"kind\":\"generated\",\"designNodeId\":\"a\",\"widgetName\":\"B\"}]")) })
	{
		const FString Json = TEXT("{\"schemaVersion\":1,\"provider\":\"figma\",\"documentScope\":\"s\",\"instances\":")
			+ Instances + TEXT(",\"d2uOwned\":[\"A\",\"B\"]}");
		TestTrue(TEXT("Write semantically invalid manifest"), FixtureWriteFileString(SidecarPath, Json));
		TestFalse(TEXT("Invalid instance identity is rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// save-failure semantics - transaction rollback (in-memory text and structure return to
// pre-reimport), manifest bytes unchanged, the diagnostic names the failure; retrying the same reimport afterwards succeeds (the asset was not poisoned).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportSaveFailureRollbackTest,
	"Design2UMG.SafeReimport.SaveFailureRollsBackAndKeepsManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportSaveFailureRollbackTest::RunTest(const FString&)
{
	// The save-failure injection path logs one error by design; register that exact boundary.
	AddExpectedError(TEXT("SaveWidgetBlueprint: failed to save"), EAutomationExpectedErrorFlags::Contains, 1);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportSaveFailure");

	const FString V1 = MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
			*MakeFixtureText(TEXT("fixture_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString ManifestBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), FixtureReadFileString(SidecarPath, ManifestBefore)))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}

	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
				*MakeFixtureText(TEXT("fixture_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v2")))),
		*MakeFixtureText(TEXT("fixture_fresh"), TEXT("FreshText"), 250, 24, TEXT("Fresh"))));

	Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure(Design2UMGEditorTests::IsPackageSaveTargetFile);
	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);

	TestFalse(TEXT("reimport reports failure"), Result.bSucceeded);
	TestTrue(TEXT("Failed save reports an incomplete file outcome"), Result.Commit.Status == EDesign2UMGCommitStatus::RollbackIncomplete);
	TestTrue(TEXT("Failed write leaves the reported file state indeterminate"), Result.Commit.Asset == EDesign2UMGFileState::Indeterminate);
	TestTrue(TEXT("Manifest unchanged"), Result.Commit.Manifest == EDesign2UMGFileState::Unchanged);
	TestTrue(TEXT("Memory explicitly restored"), Result.Commit.Memory == EDesign2UMGMemoryState::Restored);
	TestFalse(TEXT("failure is not a full conflict"), Result.bFullConflict);
	TestNull(TEXT("no updated blueprint on failure"), Result.UpdatedBlueprint);
	TestEqual(TEXT("save failure diagnosed once"), FixtureCountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("failure diagnostic states rollback and untouched manifest on one message"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_save_failed"),
			{ TEXT("transaction rolled back"), TEXT("asset keeps its pre-reimport state"), TEXT("manifest not updated") }));

	// Transaction rollback: the in-memory state stays pre-reimport (v1 design values, the v2 text never written).
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("in-memory text rolled back"), Head->GetText().ToString(), FString(TEXT("Head v1")));
	}
	else
	{
		AddError(TEXT("Head widget missing after rollback"));
	}
	TestNull(TEXT("reimport-added widget rolled back"), WBP->WidgetTree->FindWidget(TEXT("FreshText")));

	// Manifest not updated: byte-for-byte identical.
	FString ManifestAfter;
	TestTrue(TEXT("manifest still exists"), FixtureReadFileString(SidecarPath, ManifestAfter));
	TestTrue(TEXT("manifest untouched on save failure"), ManifestAfter == ManifestBefore);

	// The asset was not poisoned: retrying the same v2 immediately applies successfully.
	const FDesignReimportResult Retry = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry after rollback succeeds"), Retry.bSucceeded);
	TestNotNull(TEXT("Retry reuses the rolled-back widget name without an orphan collision"), WBP->WidgetTree->FindWidget(TEXT("FreshText")));
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// scenario B (sidecar read-only only)
// and scenario A (uasset + sidecar read-only) must both be intercepted before entering the save flow -
// the uasset does not land a new state, the sidecar is unchanged, completed is not reported, bSucceeded=false, and the diagnostic names the concrete file.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportSaveWritabilityPrecheckTest,
	"Design2UMG.SafeReimport.SaveWritabilityPrecheckBlocksHalfUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportSaveWritabilityPrecheckTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportWritability");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
			MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = FixtureImport(BuildDoc(TEXT("fail-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = FixtureAssetFilePath(TargetPackage);
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	const FMD5Hash AssetHashBefore = FMD5Hash::HashFile(*AssetFile);
	FString SidecarBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), FixtureReadFileString(SidecarFile, SidecarBefore)))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString V2 = BuildDoc(TEXT("fail-v2"));

	// Restore-writability safety net (an early return on a failed assertion must not leave a read-only file behind for cleanup either).
	const auto MakeWritable = [&]()
	{
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, false);
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, false);
	};

	// Scenario B: sidecar read-only only. The pre-check must stop the save: never the half-updated
	// combination "uasset updated + sidecar still old".
	TestTrue(TEXT("sidecar set read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, true));
	{
		const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario B reports failure"), Result.bSucceeded);
		TestFalse(TEXT("scenario B is not a full conflict"), Result.bFullConflict);
		TestEqual(TEXT("scenario B save failure diagnosed once"), FixtureCountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario B diagnostic names the blocked sidecar file"),
			FixtureHasMessageWithAll(Result, TEXT("reimport_save_failed"), { *SidecarFile, TEXT("read-only") }));
		TestEqual(TEXT("scenario B does not report completion"), FixtureCountKind(Result, TEXT("reimport_completed")), 0);
		// Combined assertion: the disk is completely untouched (uasset hash unchanged + sidecar bytes unchanged).
		TestTrue(TEXT("scenario B leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario B sidecar still readable"), FixtureReadFileString(SidecarFile, SidecarNow));
		TestTrue(TEXT("scenario B sidecar untouched"), SidecarNow == SidecarBefore);
		if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
		{
			TestEqual(TEXT("scenario B in-memory text rolled back"), Head->GetText().ToString(), FString(TEXT("fail-v1")));
		}
	}
	FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, false);

	// Scenario A: uasset + sidecar both read-only (the full Perforce-unchecked-out shape).
	TestTrue(TEXT("uasset set read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*AssetFile, true));
	TestTrue(TEXT("sidecar set read-only again"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*SidecarFile, true));
	{
		const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario A reports failure"), Result.bSucceeded);
		TestEqual(TEXT("scenario A save failure diagnosed once"), FixtureCountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario A diagnostic names both blocked files"),
			FixtureHasMessage(Result, TEXT("reimport_save_failed"), *AssetFile)
			&& FixtureHasMessage(Result, TEXT("reimport_save_failed"), *SidecarFile));
		TestEqual(TEXT("scenario A does not report completion"), FixtureCountKind(Result, TEXT("reimport_completed")), 0);
		TestTrue(TEXT("scenario A leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario A sidecar still readable"), FixtureReadFileString(SidecarFile, SidecarNow));
		TestTrue(TEXT("scenario A sidecar untouched"), SidecarNow == SidecarBefore);
	}
	MakeWritable();

	// After writability is restored, retrying the same v2 succeeds (the asset was not poisoned by the two failed rounds).
	const FDesignReimportResult Retry = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry succeeds after restoring writability"), Retry.bSucceeded);
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("fail-v2")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// the uasset saved but the manifest write failed
// (an IO failure the pre-check cannot catch) - completed is not reported; a reimport_save_failed states
// that asset and record may be inconsistent, bSucceeded=false; memory keeps the new state (the disk already holds the new asset).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportManifestWriteFailurePropagatesTest,
	"Design2UMG.SafeReimport.ManifestWriteFailurePropagates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportManifestWriteFailurePropagatesTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("GenerationManifest: failed to write temp manifest"), EAutomationExpectedErrorFlags::Contains, 1);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportManifestWriteFail");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
			MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = FixtureImport(BuildDoc(TEXT("half-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString SidecarV1;
	if (!TestTrue(TEXT("v1 manifest exists"), FixtureReadFileString(SidecarFile, SidecarV1)))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}

	// Inject the sidecar write failure (a genuinely read-only sidecar would be caught by the pre-check, so this branch is unreachable otherwise).
	Design2UMGEditorTests::FScopedFileFailures FileFailures;
	FileFailures.RejectWrite = [](const FString& Path) { return Path.EndsWith(TEXT(".d2u.json.tmp")); };
	const FDesignReimportResult Result = FixtureReimport(BuildDoc(TEXT("half-v2")), nullptr, TargetPackage);

	TestFalse(TEXT("manifest write failure is not reported as success"), Result.bSucceeded);
	TestTrue(TEXT("Metadata failure remains a partial commit"), Result.Commit.Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure);
	TestTrue(TEXT("Asset saved"), Result.Commit.Asset == EDesign2UMGFileState::Saved);
	TestTrue(TEXT("Manifest not updated"), Result.Commit.Manifest == EDesign2UMGFileState::Unchanged);
	TestTrue(TEXT("Saved memory was not rolled back"), Result.Commit.Memory == EDesign2UMGMemoryState::Modified);
	TestEqual(TEXT("Saved blueprint remains accessible"), Result.UpdatedBlueprint, WBP);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("save failure diagnosed once"), FixtureCountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("diagnostic states asset/manifest inconsistency"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_save_failed"),
			{ TEXT("was saved"), TEXT("manifest could not be written"), TEXT("inconsistent") }));
	TestTrue(TEXT("underlying manifest_write_failed surfaces in the report"),
		FixtureCountKind(Result, TEXT("manifest_write_failed")) >= 1);
	TestEqual(TEXT("completion not reported"), FixtureCountKind(Result, TEXT("reimport_completed")), 0);

	// The uasset saved the new state: memory and disk agree (not rolled back to the old value).
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("in-memory text stays at the saved v2 value"), Head->GetText().ToString(), FString(TEXT("half-v2")));
	}
	FString SidecarAfter;
	TestTrue(TEXT("sidecar still readable"), FixtureReadFileString(SidecarFile, SidecarAfter));
	TestTrue(TEXT("sidecar keeps the stale v1 content (inconsistency is reported, not hidden)"),
		SidecarAfter == SidecarV1);

	FixtureWipeOutput(OutputDir);
	return true;
}

#endif
