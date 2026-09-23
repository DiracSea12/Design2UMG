// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe reimport: mapped component boundaries (class change, switcher host duplicates, mapping diagnostics, own-attribute divergence).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "Tests/Design2UMGComponentInputTestWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportTestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;

// ---------------------------------------------------------------------------
// Mapped Class change conflict - the mapping points at another WBP class -> the existing
// instance is kept, not replaced, inputs not overwritten; the diagnostic reports old and new classes; the manifest record keeps the old class.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportMappedClassChangeTest,
	"Design2UMG.SafeReimport.MappedClassChangeIsConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportMappedClassChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportMappedClass");

	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")),
		*MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = FixtureImport(V1, FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGComponentInputTestWidget* MappedBefore = FixtureFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGComponentInputTestWidget> MappedWeak = MappedBefore;
	MappedBefore->TextValue = FText::FromString(TEXT("Hacked"));

	// v2: the same Key's mapping points at another WBP class.
	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Pay Now")),
		*MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	const FDesignReimportResult Result = FixtureReimport(V2, FixtureOtherMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);
	TestTrue(TEXT("mapped class conflict raises explicit degradation"), Result.bHasDegradation);
	TestEqual(TEXT("mapped class conflict is not plain success"), Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);

	UDesign2UMGComponentInputTestWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("mapped instance kept (not destroyed, not replaced)"), MappedAfter))
	{
		TestTrue(TEXT("instance object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("instance class not replaced"), MappedAfter->GetClass()->GetPathName(), FString(FixtureMappedClassPath));
		TestEqual(TEXT("inputs not overwritten on mapped-class conflict"), MappedAfter->TextValue.ToString(), FString(TEXT("Hacked")));
	}

	TestEqual(TEXT("mapped class conflict diagnosed once"), FixtureCountKind(Result, TEXT("reimport_mapped_class_conflict")), 1);
	TestTrue(TEXT("conflict reports node, old class, new class and no-replacement on one message"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_mapped_class_conflict"),
			{ TEXT("HeroButton"), FixtureMappedClassPath, FixtureOtherMappedClassPath, TEXT("automatic mapped-class replacement is not supported") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_btn_1"));
		if (TestNotNull(TEXT("conflicted instance stays recorded"), Record))
		{
			TestEqual(TEXT("manifest keeps the old mapped class"), Record->MappedClass, FString(FixtureMappedClassPath));
		}
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// under a multi-page prototype document (synthesized switcher host root),
// the mapped instance gains an interaction in v2 (the same-named IR-side node becomes the trigger wrapper
// form named by node id). The reimport update plan must handle it via the Matched path: the instance is not
// created twice, no node-id-named copy appears, the sidecar holds no double record for the same designNodeId, and no false "content slot occupied" move conflict.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportSwitcherMappedNoDuplicateTest,
	"Design2UMG.SafeReimport.SwitcherHostMappedNoDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportSwitcherMappedNoDuplicateTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportSwitcherMapped");

	// The mapped instance: no interaction in v1; in v2 it gains an ON_CLICK NODE navigation (becoming the trigger wrapper form).
	const auto MakeMappedButton = [](const TCHAR* Label, const bool bWithInteraction)
	{
		const FString Interactions = bWithInteraction
			? TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_pagea\",\"navigation\":\"NAVIGATE\"}]}]")
			: FString();
		return FString::Printf(
			TEXT("{\"id\":\"fixture_btn_1\",\"name\":\"PrimaryButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}]%s}"),
			FixtureReimportComponentKey, Label, *Interactions);
	};
	// The text inside PageA carries a cross-page navigation (making the document a multi-page prototype; v1/v2 both synthesize the switcher host root).
	const auto MakePageAText = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"c41_pat\",\"name\":\"PageAText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":90,\"height\":20},\"characters\":\"to-main\",%s,"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_page\",\"navigation\":\"NAVIGATE\"}]}]}"),
			*FixtureTextStyle());
	};
	const auto MakeSwitcherDoc = [&MakeMappedButton, &MakePageAText](const bool bButtonInteractive)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"c41_page\",\"name\":\"MainPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]},"
				 "{\"id\":\"c41_pagea\",\"name\":\"PageA\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":400,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1}}],"
				 "\"children\":[%s]}]}]}}"),
			FixtureReimportScope,
			*MakeMappedButton(bButtonInteractive ? TEXT("proto-v2") : TEXT("proto-v1"), bButtonInteractive),
			*MakePageAText());
	};

	UWidgetBlueprint* WBP = FixtureImport(MakeSwitcherDoc(false), FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGComponentInputTestWidget* MappedBefore = FixtureFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("v1 mapped instance generated"), MappedBefore)
		|| !TestEqual(TEXT("v1 mapped instance keeps its design name"), MappedBefore->GetFName(), FName(TEXT("PrimaryButton")))
		|| !TestTrue(TEXT("v1 mapped instance sits under the page inner layer"),
			MappedBefore->GetParent() && MappedBefore->GetParent()->GetFName() == FName(TEXT("MainPage_inner"))))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UDesign2UMGComponentInputTestWidget> MappedWeak = MappedBefore;

	const FDesignReimportResult Result = FixtureReimport(MakeSwitcherDoc(true), FixtureMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// The only instance + identity preserved + name unchanged (no node-id-named copy appears).
	int32 MappedInstanceCount = 0;
	UDesign2UMGComponentInputTestWidget* MappedAfter = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedInstanceCount, &MappedAfter](UWidget* Widget)
	{
		if (UDesign2UMGComponentInputTestWidget* Candidate = Cast<UDesign2UMGComponentInputTestWidget>(Widget))
		{
			++MappedInstanceCount;
			MappedAfter = Candidate;
		}
	});
	TestEqual(TEXT("exactly one mapped instance in the tree"), MappedInstanceCount, 1);
	TestNull(TEXT("no duplicate named after the design node id"),
		WBP->WidgetTree->FindWidget(TEXT("fixture_btn_1")));
	if (TestNotNull(TEXT("mapped instance still alive"), MappedAfter))
	{
		TestTrue(TEXT("mapped instance object identity preserved"), MappedAfter == MappedWeak.Get());
		TestEqual(TEXT("mapped instance name unchanged"), MappedAfter->GetFName(), FName(TEXT("PrimaryButton")));
		// The Matched path's inputs are overwritten directly.
		TestEqual(TEXT("declared input overwritten with the v2 design value"),
			MappedAfter->TextValue.ToString(), FString(TEXT("proto-v2")));
	}

	// Diagnostics: the refresh is named; no reimport_added (no duplicate creation); no false move conflict.
	TestTrue(TEXT("mapped refresh diagnosed"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_updated"), { TEXT("PrimaryButton"), TEXT("fixture_btn_1") }));
	TestEqual(TEXT("no added duplicate"), FixtureCountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict from the synthetic inner layer"),
		FixtureCountKind(Result, TEXT("reimport_move_conflict")), 0);

	// Sidecar: only one mapped record per designNodeId.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		int32 RecordsForButton = 0;
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* ButtonRecord = nullptr;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
		{
			if (Record.DesignNodeId == TEXT("fixture_btn_1"))
			{
				++RecordsForButton;
				ButtonRecord = &Record;
			}
		}
		TestEqual(TEXT("single manifest record for the design node"), RecordsForButton, 1);
		if (TestNotNull(TEXT("mapped record present"), ButtonRecord))
		{
			TestEqual(TEXT("record kind mapped"), ButtonRecord->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("record widget name preserved"), ButtonRecord->WidgetName, FString(TEXT("PrimaryButton")));
		}
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// the reimport report's componentMapping section - mapping config errors (ini
// validation diagnostics + the unmappable-class-load fallback) must be as visible on the reimport path as on
// the import path: both the result's ComponentMappingDiagnostics and the persisted report JSON's componentMapping.entries must contain the entries.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportMappingDiagnosticsSurfacedTest,
	"Design2UMG.SafeReimport.MappingDiagnosticsSurfaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportMappingDiagnosticsSurfacedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportMappingDiagnostics");

	const FString V1 = MakeFixtureDoc(MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = FixtureImport(V1, FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// The report file is located by the before/after directory diff of the reimport (the file name carries a timestamp + GUID and cannot be predicted).
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TSet<FString> ReportsBefore;
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(ReportDir / TEXT("*.import_report.json")), true, false);
		for (const FString& FileName : Found)
		{
			ReportsBefore.Add(FileName);
		}
	}

	// v2 reimport: the mapping table points at an unloadable class (the class-load-failure shape of an ini config
	// error), while a snapshot-carried ini validation diagnostic rides along (the same payload Load() produces on an illegal entry).
	const TCHAR* BrokenClassPath = TEXT("/Script/Design2UMGEditor.NoSuchMappedWidgetClass");
	FDesignDocument Document;
	if (!TestTrue(TEXT("v2 source parses"), FFigmaParser::ParseDocument(V1, Document)))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	FDesignReimportRequest Request;
	Request.TargetAssetPath = TargetPackage;
	Request.SettingsSnapshot = MakeFixtureSnapshot(BrokenClassPath);
	{
		FDesign2UMGMappingDiagnostic ValidationDiagnostic;
		ValidationDiagnostic.Kind = TEXT("mapping_config_invalid");
		ValidationDiagnostic.Message = TEXT("Mapping 'figma|fixture-reimport-doc-9f31|fixture-reimport-comp-key' dropped (fixture diagnostic)");
		Request.SettingsSnapshot->GenerationSettings.ComponentMapping.ValidationDiagnostics.Add(
			MoveTemp(ValidationDiagnostic));
	}
	UDesign2UMGComponentInputTestWidget* OriginalInstance = FixtureFindMappedInstance(WBP);
	FString OriginalManifest;
	FixtureReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), OriginalManifest);
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
	TestFalse(TEXT("unavailable mapped class aborts reimport before mutation"), Result.bSucceeded);
	TestTrue(TEXT("original mapped instance retained"), FixtureFindMappedInstance(WBP) == OriginalInstance);
	TestEqual(TEXT("no false kind-change success"), FixtureCountKind(Result, TEXT("reimport_kind_changed")), 0);
	TestEqual(TEXT("no false completion"), FixtureCountKind(Result, TEXT("reimport_completed")), 0);
	FString CurrentManifest;
	FixtureReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), CurrentManifest);
	TestEqual(TEXT("manifest unchanged after class-load failure"), CurrentManifest, OriginalManifest);

	// Result exit: the ini validation diagnostic + the BuildState-collected match/fallback diagnostics are all in ComponentMappingDiagnostics.
	auto HasMappingDiagnostic = [&Result](const TCHAR* Kind, const TCHAR* Substring = nullptr)
	{
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.ComponentMappingDiagnostics)
		{
			if (Diagnostic.Kind == Kind && (!Substring || Diagnostic.Message.Contains(Substring)))
			{
				return true;
			}
		}
		return false;
	};
	TestTrue(TEXT("seeded ini validation diagnostic transported"),
		HasMappingDiagnostic(TEXT("mapping_config_invalid"), TEXT("fixture diagnostic")));
	TestTrue(TEXT("mapping scope diagnostic present"),
		HasMappingDiagnostic(TEXT("mapping_scope"), FixtureReimportScope));
	TestTrue(TEXT("mapping hit diagnostic present"),
		HasMappingDiagnostic(TEXT("mapping_hit"), TEXT("HeroButton")));
	TestTrue(TEXT("unavailable mapped class diagnostic carries the configured class path"),
		HasMappingDiagnostic(TEXT("mapping_class_unavailable"), BrokenClassPath));
	// Classification aligned with the import path: mapping diagnostics do not leak into the reimport section (reimport_* prefix).
	TestEqual(TEXT("mapping diagnostics stay out of the reimport section"),
		static_cast<int32>(Result.Diagnostics.ContainsByPredicate([](const FDesign2UMGMappingDiagnostic& Diagnostic)
		{
			return Diagnostic.Kind.StartsWith(TEXT("mapping_"));
		})), 0);

	// Persisted exit: the newly written report JSON's componentMapping.entries contains the class-load-failure entry.
	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFiles(ReportsAfter, *(ReportDir / TEXT("*.import_report.json")), true, false);
	FString NewReport;
	for (const FString& FileName : ReportsAfter)
	{
		if (!ReportsBefore.Contains(FileName))
		{
			NewReport = ReportDir / FileName;
		}
	}
	if (TestTrue(TEXT("a new import report was written by reimport"), !NewReport.IsEmpty()))
	{
		FString ReportJson;
		if (TestTrue(TEXT("report file readable"), FixtureReadFileString(NewReport, ReportJson)))
		{
			TSharedPtr<FJsonObject> ReportRoot;
			if (TestTrue(TEXT("report JSON parses"), FixtureParseJsonObject(ReportJson, ReportRoot)))
			{
				const TSharedPtr<FJsonObject>* ComponentMapping = nullptr;
				bool bFoundClassUnavailable = false;
				if (TestTrue(TEXT("componentMapping section present"),
						ReportRoot->TryGetObjectField(TEXT("componentMapping"), ComponentMapping) && ComponentMapping != nullptr))
				{
					const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
					if (TestTrue(TEXT("componentMapping.entries present"),
							(*ComponentMapping)->TryGetArrayField(TEXT("entries"), Entries) && Entries != nullptr))
					{
						for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
						{
							const TSharedPtr<FJsonObject> Entry = EntryValue->AsObject();
							if (Entry.IsValid()
								&& Entry->GetStringField(TEXT("kind")) == TEXT("mapping_class_unavailable")
								&& Entry->GetStringField(TEXT("message")).Contains(BrokenClassPath))
							{
								bFoundClassUnavailable = true;
							}
						}
					}
				}
				TestTrue(TEXT("report componentMapping entries carry the unavailable-class diagnostic"),
					bFoundClassUnavailable);
			}
		}
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// the mapped instance's own visual
// properties (node-level opacity) are written with the generic properties at import; reimport does not refresh
// them per the "mapped interior belongs to the project" boundary, but the divergence between the declared design value and the instance's current state must be named (project value vs design value).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportMappedOwnAttrDivergenceTest,
	"Design2UMG.SafeReimport.MappedOwnAttrDivergenceReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportMappedOwnAttrDivergenceTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportMappedOwnAttr");

	// The mapped instance node carries a node-level opacity (Figma "opacity" -> Render.RenderOpacity, declared only when < 1).
	const auto BuildDoc = [](const TCHAR* NodeOpacity)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"FixtureDoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"fixture_root\",\"name\":\"FixturePage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":["
				 "{\"id\":\"fixture_moa\",\"name\":\"HeroButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"}},"
				 "\"opacity\":%s,"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
				 "\"children\":[{\"id\":\"fixture_moa_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":32,\"y\":36,\"width\":80,\"height\":18},\"characters\":\"Checkout\",%s}]}"
				 "]}]}]}}"),
			FixtureReimportScope, FixtureReimportComponentKey, NodeOpacity, *FixtureTextStyle());
	};

	UWidgetBlueprint* WBP = FixtureImport(BuildDoc(TEXT("0.5")), FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGComponentInputTestWidget* MappedInstance = FixtureFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedInstance))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	TestEqual(TEXT("v1 import writes design opacity onto the instance"),
		MappedInstance->GetRenderOpacity(), 0.5f);
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: the design changes the node opacity to 0.25. Reimport does not refresh the self-property (the project side
	// keeps 0.5), but the divergence must be named in the report.
	const FDesignReimportResult Result = FixtureReimport(BuildDoc(TEXT("0.25")), FixtureMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("own opacity kept as project-side"),
		FixtureFindMappedInstance(WBP)->GetRenderOpacity(), 0.5f);
	TestTrue(TEXT("divergence diagnostic names kept + design values"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_mapped_attr_kept"),
			{ TEXT("renderOpacity project=0.500 design=0.250") }));

	FixtureWipeOutput(OutputDir);
	return true;
}

#endif

