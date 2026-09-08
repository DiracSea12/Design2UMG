// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe Reimport QA: mapped component boundaries (class change, inert interactions, switcher host duplicates, mapping diagnostics, own-attribute divergence).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "FigmaParser.h"
#include "Tests/Design2UMGMappingQAWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportQATestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportQA;

// ---------------------------------------------------------------------------
// Case 16 (integration): Mapped Class change conflict - the mapping points at another WBP class -> the existing
// instance is kept, not replaced, inputs not overwritten; the diagnostic reports old and new classes; the manifest record keeps the old class.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedClassChangeTest,
	"Design2UMG.SafeReimportQA.MappedClassChangeIsConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedClassChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedClass");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;
	MappedBefore->TextValue = FText::FromString(TEXT("Hacked"));

	// v2: the same Key's mapping points at another WBP class.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Pay Now")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep"))));
	const FDesignReimportResult Result = QAReimport(V2, QAOtherMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("mapped instance kept (not destroyed, not replaced)"), MappedAfter))
	{
		TestTrue(TEXT("instance object identity preserved"), MappedAfter == MappedBefore);
		TestEqual(TEXT("instance class not replaced"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
		TestEqual(TEXT("inputs not overwritten on mapped-class conflict"), MappedAfter->TextValue.ToString(), FString(TEXT("Hacked")));
	}

	TestEqual(TEXT("mapped class conflict diagnosed once"), QACountKind(Result, TEXT("reimport_mapped_class_conflict")), 1);
	TestTrue(TEXT("conflict reports node, old class, new class and no-replacement on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_mapped_class_conflict"),
			{ TEXT("HeroButton"), QAMappedClassPath, QAOtherMappedClassPath, TEXT("not replaced in v1") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("conflicted instance stays recorded"), Record))
		{
			TestEqual(TEXT("manifest keeps the old mapped class"), Record->MappedClass, FString(QAMappedClassPath));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 30 (integration, S4 dedicated mapped-interaction diagnostic): a design interaction attached to a mapped
// component also emits a dedicated "inert" entry at reimport (mapping_prototype_inert, naming the trigger
// and type), not just the generic mapped-refresh entry.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedInteractionInertTest,
	"Design2UMG.SafeReimportQA.MappedInteractionInertDiagnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedInteractionInertTest::RunTest(const FString&)
{
	// Pin the opt-in navigation mode: this QA case verifies the prototype extension itself (default is Disabled).
	const FScopedPrototypeNavigationGenerationMode ScopedNavMode(EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension);

	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedInert");

	// A mapped button + an ON_CLICK design interaction (attached to the mapped instance body).
	const auto BuildDoc = [](const TCHAR* Label)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":["
				 "{\"id\":\"qa_mib\",\"name\":\"HeroButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://qa.example/inert\"}]}],"
				 "\"children\":[{\"id\":\"qa_mib_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":32,\"y\":36,\"width\":80,\"height\":18},\"characters\":\"%s\",%s}]}"
				 "]}]}]}}"),
			QAReimportScope, QAReimportComponentKey, Label, Label, *QATextStyle());
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr)
		|| !TestNotNull(TEXT("mapped instance generated"), QAFindMappedInstance(WBP)))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Pay Now")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestTrue(TEXT("dedicated inert diagnostic present and names trigger + type"),
		QAHasMappingMessageWithAll(Result, TEXT("mapping_prototype_inert"),
			{ TEXT("qa_mib"), TEXT("ON_CLICK"), TEXT("mapped WBP interactions are project-owned") }));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 32 (integration, slice C C1): under a multi-page prototype document (synthesized switcher host root),
// the mapped instance gains an interaction in v2 (the same-named IR-side node becomes the trigger wrapper
// form named by node id). The reimport update plan must handle it via the Matched path: the instance is not
// created twice, no node-id-named copy appears, the sidecar holds no double record for the same designNodeId, and no false "content slot occupied" move conflict.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQASwitcherMappedNoDuplicateTest,
	"Design2UMG.SafeReimportQA.SwitcherHostMappedNoDuplicate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQASwitcherMappedNoDuplicateTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportSwitcherMapped");

	// The mapped instance: no interaction in v1; in v2 it gains an ON_CLICK NODE navigation (becoming the trigger wrapper form).
	const auto MakeMappedButton = [](const TCHAR* Label, const bool bWithInteraction)
	{
		const FString Interactions = bWithInteraction
			? TEXT(",\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_pagea\",\"navigation\":\"NAVIGATE\"}]}]")
			: FString();
		return FString::Printf(
			TEXT("{\"id\":\"qa_btn_1\",\"name\":\"PrimaryButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"%s\"}},"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}]%s}"),
			QAReimportComponentKey, Label, *Interactions);
	};
	// The text inside PageA carries a cross-page navigation (making the document a multi-page prototype; v1/v2 both synthesize the switcher host root).
	const auto MakePageAText = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"c41_pat\",\"name\":\"PageAText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":90,\"height\":20},\"characters\":\"to-main\",%s,"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"NODE\",\"destinationId\":\"c41_page\",\"navigation\":\"NAVIGATE\"}]}]}"),
			*QATextStyle());
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
			QAReimportScope,
			*MakeMappedButton(bButtonInteractive ? TEXT("proto-v2") : TEXT("proto-v1"), bButtonInteractive),
			*MakePageAText());
	};

	UWidgetBlueprint* WBP = QAImport(MakeSwitcherDoc(false), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGMappingQAWidget* MappedBefore = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("v1 mapped instance generated"), MappedBefore)
		|| !TestEqual(TEXT("v1 mapped instance keeps its design name"), MappedBefore->GetFName(), FName(TEXT("PrimaryButton")))
		|| !TestTrue(TEXT("v1 mapped instance sits under the page inner layer"),
			MappedBefore->GetParent() && MappedBefore->GetParent()->GetFName() == FName(TEXT("MainPage_inner"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = MappedBefore;

	const FDesignReimportResult Result = QAReimport(MakeSwitcherDoc(true), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	// The only instance + identity preserved + name unchanged (no node-id-named copy appears).
	int32 MappedInstanceCount = 0;
	UDesign2UMGMappingQAWidget* MappedAfter = nullptr;
	WBP->WidgetTree->ForEachWidget([&MappedInstanceCount, &MappedAfter](UWidget* Widget)
	{
		if (UDesign2UMGMappingQAWidget* Candidate = Cast<UDesign2UMGMappingQAWidget>(Widget))
		{
			++MappedInstanceCount;
			MappedAfter = Candidate;
		}
	});
	TestEqual(TEXT("exactly one mapped instance in the tree"), MappedInstanceCount, 1);
	TestNull(TEXT("no duplicate named after the design node id"),
		WBP->WidgetTree->FindWidget(TEXT("qa_btn_1")));
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
		QAHasMessageWithAll(Result, TEXT("reimport_updated"), { TEXT("PrimaryButton"), TEXT("qa_btn_1") }));
	TestEqual(TEXT("no added duplicate"), QACountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict from the synthetic inner layer"),
		QACountKind(Result, TEXT("reimport_move_conflict")), 0);

	// Sidecar: only one mapped record per designNodeId.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		int32 RecordsForButton = 0;
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* ButtonRecord = nullptr;
		for (const FDesign2UMGGenerationManifestSidecar::FInstanceRecord& Record : Sidecar.Instances)
		{
			if (Record.DesignNodeId == TEXT("qa_btn_1"))
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

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 35b (integration): the reimport report's componentMapping section - mapping config errors (ini
// validation diagnostics + the unmappable-class-load fallback) must be as visible on the reimport path as on
// the import path: both the result's ComponentMappingDiagnostics and the persisted report JSON's componentMapping.entries must contain the entries.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappingDiagnosticsSurfacedTest,
	"Design2UMG.SafeReimportQA.MappingDiagnosticsSurfaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappingDiagnosticsSurfacedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappingDiagnostics");

	const FString V1 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
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
		QAWipeOutput(OutputDir);
		return false;
	}
	FDesignReimportRequest Request;
	Request.TargetAssetPath = TargetPackage;
	Request.SettingsSnapshot = MakeQASnapshot(BrokenClassPath);
	{
		FDesign2UMGMappingDiagnostic ValidationDiagnostic;
		ValidationDiagnostic.Kind = TEXT("mapping_config_invalid");
		ValidationDiagnostic.Message = TEXT("Mapping 'figma|qa-reimport-doc-9f31|qa-reimport-comp-key' dropped (QA seeded)");
		Request.SettingsSnapshot->GenerationSettings.ComponentMapping.ValidationDiagnostics.Add(
			MoveTemp(ValidationDiagnostic));
	}
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
	TestTrue(TEXT("reimport itself succeeds (class fallback is a per-node path)"), Result.bSucceeded);

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
		HasMappingDiagnostic(TEXT("mapping_config_invalid"), TEXT("QA seeded")));
	TestTrue(TEXT("mapping scope diagnostic present"),
		HasMappingDiagnostic(TEXT("mapping_scope"), QAReimportScope));
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
		if (TestTrue(TEXT("report file readable"), QAReadFileString(NewReport, ReportJson)))
		{
			TSharedPtr<FJsonObject> ReportRoot;
			if (TestTrue(TEXT("report JSON parses"), QAParseJsonObject(ReportJson, ReportRoot)))
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

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 41 (integration, S2 mapped self-property divergence report): the mapped instance's own visual
// properties (node-level opacity) are written with the generic properties at import; reimport does not refresh
// them per the "mapped interior belongs to the project" boundary, but the divergence between the declared design value and the instance's current state must be named (project value vs design value).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMappedOwnAttrDivergenceTest,
	"Design2UMG.SafeReimportQA.MappedOwnAttrDivergenceReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMappedOwnAttrDivergenceTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMappedOwnAttr");

	// The mapped instance node carries a node-level opacity (Figma "opacity" -> Render.RenderOpacity, declared only when < 1).
	const auto BuildDoc = [](const TCHAR* NodeOpacity)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"QADoc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},\"children\":["
				 "{\"id\":\"qa_moa\",\"name\":\"HeroButton\",\"type\":\"INSTANCE\","
				 "\"componentId\":\"77:88\",\"componentSetId\":\"99:00\",\"mainComponentKey\":\"%s\","
				 "\"componentProperties\":{\"label\":{\"type\":\"TEXT\",\"value\":\"Checkout\"}},"
				 "\"opacity\":%s,"
				 "\"absoluteBoundingBox\":{\"x\":24,\"y\":24,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.5,\"b\":0.8,\"a\":1}}],"
				 "\"children\":[{\"id\":\"qa_moa_label\",\"name\":\"InnerLabel\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":32,\"y\":36,\"width\":80,\"height\":18},\"characters\":\"Checkout\",%s}]}"
				 "]}]}]}}"),
			QAReimportScope, QAReimportComponentKey, NodeOpacity, *QATextStyle());
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("0.5")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UDesign2UMGMappingQAWidget* MappedInstance = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedInstance))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TestEqual(TEXT("v1 import writes design opacity onto the instance"),
		MappedInstance->GetRenderOpacity(), 0.5f);
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: the design changes the node opacity to 0.25. Reimport does not refresh the self-property (the project side
	// keeps 0.5), but the divergence must be named in the report.
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("0.25")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestEqual(TEXT("own opacity kept as project-side"),
		QAFindMappedInstance(WBP)->GetRenderOpacity(), 0.5f);
	TestTrue(TEXT("divergence diagnostic names kept + design values"),
		QAHasMessageWithAll(Result, TEXT("reimport_mapped_attr_kept"),
			{ TEXT("renderOpacity project=0.500 design=0.250") }));

	QAWipeOutput(OutputDir);
	return true;
}

#endif

