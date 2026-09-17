// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe Reimport QA: generation-manifest schema, sidecar load rejection, save failure rollback, writability pre-check, and sidecar write-failure cases.

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGMappingQAWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportQATestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportQA;

// ---------------------------------------------------------------------------
// Case 1 (unit): manifest schema consolidation - inputs persist declarations only (source/target/valueKind);
// design values never enter the manifest; the historical inputs[].value field in old sidecars is always ignored.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestInputSchemaTest,
	"Design2UMG.SafeReimportQA.ManifestInputSchemaDeclarationsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestInputSchemaTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestSchema");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	FDesign2UMGGenerationManifestPayload Payload;
	Payload.Provider = TEXT("figma");
	Payload.DocumentScope = TEXT("unit-scope-1");
	Payload.SourceRootNodeId = TEXT("qa-unit-root");
	Payload.SourceRootName = TEXT("QAPage");
	{
		FDesign2UMGMappedInstanceRecord Mapped;
		Mapped.DesignNodeId = TEXT("qa-m-1");
		Mapped.WidgetName = TEXT("W1");
		Mapped.MappedKey = TEXT("figma|unit-scope-1|k");
		Mapped.MappedClass = TEXT("/Script/Design2UMGEditor.Design2UMGMappingQAWidget");
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
		Generated.DesignNodeId = TEXT("qa-g-1");
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
	TestTrue(TEXT("sidecar file readable"), QAReadFileString(SidecarPath, RawJson));
	TSharedPtr<FJsonObject> Root;
	if (TestTrue(TEXT("sidecar JSON parses"), QAParseJsonObject(RawJson, Root)))
	{
		TestEqual(TEXT("schemaVersion is 1"), static_cast<int32>(Root->GetNumberField(TEXT("schemaVersion"))), 1);
		TestEqual(TEXT("provider written"), Root->GetStringField(TEXT("provider")), FString(TEXT("figma")));
		TestEqual(TEXT("documentScope written"), Root->GetStringField(TEXT("documentScope")), FString(TEXT("unit-scope-1")));
		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		if (TestTrue(TEXT("sourceRoot present"), Root->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot != nullptr))
		{
			TestEqual(TEXT("sourceRoot.nodeId written"), (*SourceRoot)->GetStringField(TEXT("nodeId")), FString(TEXT("qa-unit-root")));
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

	// Read side: the three declaration fields round-trip as-is (the record structure no longer has value/bApplied fields to restore).
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("TryLoadSidecarForAsset succeeds"), QALoadSidecar(Package, Sidecar)))
	{
		TestEqual(TEXT("loaded schemaVersion"), Sidecar.SchemaVersion, 1);
		TestEqual(TEXT("loaded provider"), Sidecar.Provider, FString(TEXT("figma")));
		TestEqual(TEXT("loaded documentScope"), Sidecar.DocumentScope, FString(TEXT("unit-scope-1")));
		TestEqual(TEXT("loaded sourceRoot id"), Sidecar.SourceRootNodeId, FString(TEXT("qa-unit-root")));
		TestEqual(TEXT("loaded sourceRoot name"), Sidecar.SourceRootName, FString(TEXT("QAPage")));
		TestEqual(TEXT("instance count"), Sidecar.Instances.Num(), 2);
		if (Sidecar.Instances.Num() == 2)
		{
			const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& M = Sidecar.Instances[0];
			TestEqual(TEXT("loaded mapped kind"), M.Kind, FString(TEXT("mapped")));
			TestTrue(TEXT("loaded set-level flag"), M.bMatchedAtSetLevel);
			TestEqual(TEXT("loaded mappedClass"), M.MappedClass, FString(QAMappedClassPath));
			if (TestEqual(TEXT("loaded input count"), M.Inputs.Num(), 1) && M.Inputs.Num() == 1)
			{
				TestEqual(TEXT("loaded input source"), M.Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("loaded input target"), M.Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("loaded input valueKind"), M.Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
			TestEqual(TEXT("loaded generated kind"), Sidecar.Instances[1].Kind, FString(TEXT("generated")));
			TestEqual(TEXT("loaded generated id"), Sidecar.Instances[1].DesignNodeId, FString(TEXT("qa-g-1")));
		}
		TestTrue(TEXT("d2uOwned loaded"), Sidecar.D2UOwned.Contains(TEXT("W1")) && Sidecar.D2UOwned.Contains(TEXT("G1")));
	}

	// A historical sidecar (with a value field) ignores value per the schema consolidation rule and loads without errors.
	// The injection needle is serialization-whitespace-independent (the value itself): append a historical value sibling field after the valueKind value.
	FString LegacyJson = RawJson;
	const TCHAR* DeclarationNeedle = TEXT("\"ComponentProperty\"");
	LegacyJson.ReplaceInline(
		DeclarationNeedle,
		TEXT("\"ComponentProperty\",\"value\":\"legacy-77f\""),
		ESearchCase::CaseSensitive);
	TestTrue(TEXT("legacy value injected"), LegacyJson != RawJson);
	TestTrue(TEXT("legacy sidecar written"), QAWriteFileString(SidecarPath, LegacyJson));

	FDesign2UMGGenerationManifestSidecar LegacySidecar;
	if (TestTrue(TEXT("legacy sidecar still loads"), QALoadSidecar(Package, LegacySidecar))
		&& TestEqual(TEXT("legacy input count"), LegacySidecar.Instances.Num() > 0 ? LegacySidecar.Instances[0].Inputs.Num() : -1, 1)
		&& LegacySidecar.Instances.Num() > 0 && LegacySidecar.Instances[0].Inputs.Num() == 1)
	{
		TestEqual(TEXT("legacy inputs[].value ignored; declarations intact"),
			LegacySidecar.Instances[0].Inputs[0].Source, FString(TEXT("label")));
		TestEqual(TEXT("legacy inputs[] declaration target intact"),
			LegacySidecar.Instances[0].Inputs[0].Target, FString(TEXT("TextValue")));
		TestEqual(TEXT("legacy inputs[] declaration valueKind intact"),
			LegacySidecar.Instances[0].Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// Case 2 (unit): error classification of sidecar read failures - missing file / invalid JSON / unrecognized schema
// version / missing provider|documentScope / empty instances each return false + a precise reason.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestLoadRejectsBadSidecarsTest,
	"Design2UMG.SafeReimportQA.ManifestLoadRejectsBadSidecars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestLoadRejectsBadSidecarsTest::RunTest(const FString&)
{
	const FString Package = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestErrors");
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);

	// 1) Missing file.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	FString Error;
	TestFalse(TEXT("missing sidecar rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-file reason"), Error.Contains(TEXT("generation manifest sidecar not found")));

	// 2) Invalid JSON.
	TestTrue(TEXT("garbage written"), QAWriteFileString(SidecarPath, TEXT("{ this is not json")));
	TestFalse(TEXT("invalid JSON rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("invalid-JSON reason"), Error.Contains(TEXT("generation manifest sidecar is not valid JSON")));

	// 3) Unrecognized schema version.
	TestTrue(TEXT("v2 schema written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":2,\"provider\":\"figma\",\"documentScope\":\"s\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("unsupported schema rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("schema-version reason"), Error.Contains(TEXT("schema version 2 is not supported (expected 1)")));

	// 4) provider / documentScope missing.
	TestTrue(TEXT("empty provider written"), QAWriteFileString(
		SidecarPath,
		TEXT("{\"schemaVersion\":1,\"provider\":\"\",\"documentScope\":\"\",\"sourceRoot\":{},\"instances\":[{}],\"d2uOwned\":[]}")));
	TestFalse(TEXT("missing provider/documentScope rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	TestTrue(TEXT("missing-provider reason"), Error.Contains(TEXT("missing provider/documentScope")));

	// 5) Empty instances.
	TestTrue(TEXT("empty instances written"), QAWriteFileString(
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
		TestTrue(TEXT("Write semantically invalid manifest"), QAWriteFileString(SidecarPath, Json));
		TestFalse(TEXT("Invalid instance identity is rejected"), Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error));
	}

	IFileManager::Get().Delete(*SidecarPath, false, true);
	return true;
}

// ---------------------------------------------------------------------------
// Case 17 (integration): save-failure semantics - transaction rollback (in-memory text and structure return to
// pre-reimport), manifest bytes unchanged, the diagnostic names the failure; retrying the same reimport afterwards succeeds (the asset was not poisoned).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASaveFailureRollbackTest,
	"Design2UMG.SafeReimportQA.SaveFailureRollsBackAndKeepsManifest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASaveFailureRollbackTest::RunTest(const FString&)
{
	// The save-failure injection path logs an error by design; registered as expected.
	AddExpectedError(TEXT("failed to save"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSaveFailure");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
			*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v1")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString ManifestBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarPath, ManifestBefore)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
				*MakeQAText(TEXT("qa_tail"), TEXT("Tail"), 32, 60, TEXT("Tail v2")))),
		*MakeQAText(TEXT("qa_fresh"), TEXT("FreshText"), 250, 24, TEXT("Fresh"))));

	FUMGGenerator::ArmAutomationSaveFailureOnce();
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);

	TestFalse(TEXT("reimport reports failure"), Result.bSucceeded);
	TestTrue(TEXT("Failed save did not commit"), Result.Commit.Status == EDesign2UMGCommitStatus::NotCommitted);
	TestTrue(TEXT("Asset file unchanged"), Result.Commit.Asset == EDesign2UMGFileState::Unchanged);
	TestTrue(TEXT("Manifest unchanged"), Result.Commit.Manifest == EDesign2UMGFileState::Unchanged);
	TestTrue(TEXT("Memory explicitly restored"), Result.Commit.Memory == EDesign2UMGMemoryState::Restored);
	TestFalse(TEXT("failure is not a full conflict"), Result.bFullConflict);
	TestNull(TEXT("no updated blueprint on failure"), Result.UpdatedBlueprint);
	TestEqual(TEXT("save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("failure diagnostic states rollback and untouched manifest on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_save_failed"),
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
	TestTrue(TEXT("manifest still exists"), QAReadFileString(SidecarPath, ManifestAfter));
	TestTrue(TEXT("manifest untouched on save failure"), ManifestAfter == ManifestBefore);

	// The asset was not poisoned: retrying the same v2 immediately applies successfully.
	const FDesignReimportResult Retry = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry after rollback succeeds"), Retry.bSucceeded);
	TestNotNull(TEXT("Retry reuses the rolled-back widget name without an orphan collision"), WBP->WidgetTree->FindWidget(TEXT("FreshText")));
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 28 (integration, S1 pre-save writability check): scenario B (sidecar read-only only)
// and scenario A (uasset + sidecar read-only) must both be intercepted before entering the save flow -
// the uasset does not land a new state, the sidecar is unchanged, completed is not reported, bSucceeded=false, and the diagnostic names the concrete file.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASaveWritabilityPrecheckTest,
	"Design2UMG.SafeReimportQA.SaveWritabilityPrecheckBlocksHalfUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASaveWritabilityPrecheckTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportWritability");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("fail-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString AssetFile = QAAssetFilePath(TargetPackage);
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	const FMD5Hash AssetHashBefore = FMD5Hash::HashFile(*AssetFile);
	FString SidecarBefore;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarFile, SidecarBefore)))
	{
		QAWipeOutput(OutputDir);
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
		const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario B reports failure"), Result.bSucceeded);
		TestFalse(TEXT("scenario B is not a full conflict"), Result.bFullConflict);
		TestEqual(TEXT("scenario B save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario B diagnostic names the blocked sidecar file"),
			QAHasMessageWithAll(Result, TEXT("reimport_save_failed"), { *SidecarFile, TEXT("read-only") }));
		TestEqual(TEXT("scenario B does not report completion"), QACountKind(Result, TEXT("reimport_completed")), 0);
		// Combined assertion: the disk is completely untouched (uasset hash unchanged + sidecar bytes unchanged).
		TestTrue(TEXT("scenario B leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario B sidecar still readable"), QAReadFileString(SidecarFile, SidecarNow));
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
		const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
		TestFalse(TEXT("scenario A reports failure"), Result.bSucceeded);
		TestEqual(TEXT("scenario A save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
		TestTrue(TEXT("scenario A diagnostic names both blocked files"),
			QAHasMessage(Result, TEXT("reimport_save_failed"), *AssetFile)
			&& QAHasMessage(Result, TEXT("reimport_save_failed"), *SidecarFile));
		TestEqual(TEXT("scenario A does not report completion"), QACountKind(Result, TEXT("reimport_completed")), 0);
		TestTrue(TEXT("scenario A leaves the uasset untouched on disk"),
			FMD5Hash::HashFile(*AssetFile) == AssetHashBefore);
		FString SidecarNow;
		TestTrue(TEXT("scenario A sidecar still readable"), QAReadFileString(SidecarFile, SidecarNow));
		TestTrue(TEXT("scenario A sidecar untouched"), SidecarNow == SidecarBefore);
	}
	MakeWritable();

	// After writability is restored, retrying the same v2 succeeds (the asset was not poisoned by the two failed rounds).
	const FDesignReimportResult Retry = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("retry succeeds after restoring writability"), Retry.bSucceeded);
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("retry applies the design update"), Head->GetText().ToString(), FString(TEXT("fail-v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 29 (integration, S2 sidecar write-failure propagation): the uasset saved but the manifest write failed
// (an IO failure the pre-check cannot catch) - completed is not reported; a reimport_save_failed states
// that asset and record may be inconsistent, bSucceeded=false; memory keeps the new state (the disk already holds the new asset).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAManifestWriteFailurePropagatesTest,
	"Design2UMG.SafeReimportQA.ManifestWriteFailurePropagates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAManifestWriteFailurePropagatesTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("automation-injected sidecar write failure"), EAutomationExpectedErrorFlags::Contains, 0);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportManifestWriteFail");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
			MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("half-v1")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	const FString SidecarFile = Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage);
	FString SidecarV1;
	if (!TestTrue(TEXT("v1 manifest exists"), QAReadFileString(SidecarFile, SidecarV1)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// Inject the sidecar write failure (a genuinely read-only sidecar would be caught by the pre-check, so this branch is unreachable otherwise).
	Design2UMGGenerationManifest::ArmAutomationSidecarWriteFailureOnce();
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("half-v2")), nullptr, TargetPackage);

	TestFalse(TEXT("manifest write failure is not reported as success"), Result.bSucceeded);
	TestTrue(TEXT("Metadata failure remains a partial commit"), Result.Commit.Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure);
	TestTrue(TEXT("Asset saved"), Result.Commit.Asset == EDesign2UMGFileState::Saved);
	TestTrue(TEXT("Manifest not updated"), Result.Commit.Manifest == EDesign2UMGFileState::Unchanged);
	TestTrue(TEXT("Saved memory was not rolled back"), Result.Commit.Memory == EDesign2UMGMemoryState::Modified);
	TestEqual(TEXT("Saved blueprint remains accessible"), Result.UpdatedBlueprint, WBP);
	TestFalse(TEXT("not a full conflict"), Result.bFullConflict);
	TestEqual(TEXT("save failure diagnosed once"), QACountKind(Result, TEXT("reimport_save_failed")), 1);
	TestTrue(TEXT("diagnostic states asset/manifest inconsistency"),
		QAHasMessageWithAll(Result, TEXT("reimport_save_failed"),
			{ TEXT("was saved"), TEXT("manifest could not be written"), TEXT("inconsistent") }));
	TestTrue(TEXT("underlying manifest_write_failed surfaces in the report"),
		QACountKind(Result, TEXT("manifest_write_failed")) >= 1);
	TestEqual(TEXT("completion not reported"), QACountKind(Result, TEXT("reimport_completed")), 0);

	// The uasset saved the new state: memory and disk agree (not rolled back to the old value).
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("in-memory text stays at the saved v2 value"), Head->GetText().ToString(), FString(TEXT("half-v2")));
	}
	FString SidecarAfter;
	TestTrue(TEXT("sidecar still readable"), QAReadFileString(SidecarFile, SidecarAfter));
	TestTrue(TEXT("sidecar keeps the stale v1 content (inconsistency is reported, not hidden)"),
		SidecarAfter == SidecarV1);

	QAWipeOutput(OutputDir);
	return true;
}

#endif
