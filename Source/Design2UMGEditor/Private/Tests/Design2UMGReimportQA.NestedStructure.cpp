// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe Reimport QA: nested-structure boundaries (nested add/remove/move, synthetic layers, host-root shape change, top-level reparent).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "Tests/Design2UMGMappingQAWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/WidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "Design2UMGReimportQATestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportQA;

// ---------------------------------------------------------------------------
// Case 20 (integration): nested-structure boundary - design-added nested nodes inside an existing container go
// through the generation-chain rebuild; nested subtrees the design dropped keep their current state (the manifest records top-level instances only; nested deletion is outside the safe-delete scope).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedStructureBoundaryTest,
	"Design2UMG.SafeReimportQA.NestedAddAndRemovalBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedStructureBoundaryTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedBoundary");

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

	// v2: the container's Head text changes + FreshLine is added; Tail disappears from the design.
	const FString V2 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
			*MakeQAText(TEXT("qa_fresh"), TEXT("FreshLine"), 32, 60, TEXT("Fresh v2")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("nested kept node refreshed"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}
	else
	{
		AddError(TEXT("Head widget missing"));
	}

	UTextBlock* Fresh = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("FreshLine")));
	TestNotNull(TEXT("nested new node added"), Fresh);
	if (Fresh)
	{
		TestEqual(TEXT("nested new node text applied"), Fresh->GetText().ToString(), FString(TEXT("Fresh v2")));
		TestEqual(TEXT("nested new node parented under the container"),
			Fresh->GetParent()->GetFName(), FName(TEXT("InfoPanel")));
	}
	TestTrue(TEXT("nested add diagnosed with parent context on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_added"), { TEXT("FreshLine"), TEXT("added under 'InfoPanel'") }));

	// The design-dropped nested subtree: kept as-is (not a manifest instance, never enters the safe-delete flow).
	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestEqual(TEXT("no removal performed"), QACountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("no removal conflict"), QACountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("container stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_panel")));
		TestTrue(TEXT("nested widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("Head")) && Sidecar.D2UOwned.Contains(TEXT("Tail")));
		TestTrue(TEXT("nested new node ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("FreshLine")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 23 (integration, R2): when a nested mapped instance's IR parent is a non-instance nested container
// (SidePanel>InnerZone>Button) and the structure is unchanged, reimport must not emit a misleading
// reimport_move_conflict (a diagnostic must mean a structural change was really skipped); the instance refreshes as usual.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedParentNoNoiseTest,
	"Design2UMG.SafeReimportQA.NestedParentNoMoveConflictNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedParentNoNoiseTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedNoNoise");

	// SidePanel > InnerZone (a nested pure container, not a manifest instance) > the mapped button.
	const auto BuildDoc = [](const TCHAR* Label)
	{
		return MakeQADoc(FString::Printf(
			TEXT("%s,%s"),
			*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24, FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32,
					MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 40, 40, Label)),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 240, 32, TEXT("Pad")))),
			*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 200, TEXT("Keep"))));
	};

	UWidgetBlueprint* WBP = QAImport(BuildDoc(TEXT("Checkout")), QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UDesign2UMGMappingQAWidget* Mapped = QAFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("nested mapped instance generated"), Mapped)
		|| !TestTrue(TEXT("instance sits under InnerZone"),
			Mapped->GetParent() && Mapped->GetParent()->GetFName() == FName(TEXT("InnerZone"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UDesign2UMGMappingQAWidget> MappedWeak = Mapped;

	// A structure-unchanged reimport (only the input value changes): no move-conflict noise.
	const FDesignReimportResult Result = QAReimport(BuildDoc(TEXT("Pay Now")), QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("no move-conflict noise for the unchanged nested parent"),
		QACountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("instance still alive"), MappedAfter))
	{
		TestEqual(TEXT("instance stays under the nested container"),
			MappedAfter->GetParent()->GetFName(), FName(TEXT("InnerZone")));
		TestEqual(TEXT("instance still refreshed normally"), MappedAfter->TextValue.ToString(), FString(TEXT("Pay Now")));
	}
	TestTrue(TEXT("instance refresh diagnosed"), QAHasMessage(Result, TEXT("reimport_updated"), TEXT("HeroButton")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 24 (integration, R3 nested move): the design moves the nested node N from ContainerA to
// ContainerB - when the source end is purely D2U-owned, the widget moves (identity preserved, name
// unchanged, nothing left at the source, no copy built at the target) + a reimport_moved diagnostic; no false reimport_added.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedMoveIdentityTest,
	"Design2UMG.SafeReimportQA.NestedMovePreservesIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedMoveIdentityTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedMove");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20,
			MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* NBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (!TestNotNull(TEXT("nested N present in v1"), NBefore)
		|| !TestTrue(TEXT("N sits under ContainerA"),
			NBefore->GetParent() && NBefore->GetParent()->GetFName() == FName(TEXT("ContainerA"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2: N moves from ContainerA to ContainerB.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20,
			MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("N still present after move"), NAfter))
	{
		TestTrue(TEXT("widget object identity preserved"), NAfter == NWeak.Get());
		TestEqual(TEXT("N moved under ContainerB"), NAfter->GetParent()->GetFName(), FName(TEXT("ContainerB")));
		TestEqual(TEXT("moved N keeps its design text"), NAfter->GetText().ToString(), FString(TEXT("mv")));
	}
	TestNull(TEXT("no duplicate copy created under the target"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move diagnosed once"), QACountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("nested move diagnostic names node and identity preservation"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 25 (integration, R3 nested move conflict): the developer already hung the nested node N under the
// project container DevPanel, and the design moves N to ContainerB - N stays in DevPanel, ContainerB must
// not get a new copy, and a reimport_move_conflict names N and the reason.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedMoveConflictNoCopyTest,
	"Design2UMG.SafeReimportQA.NestedMoveConflictCreatesNoCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedMoveConflictNoCopyTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedMoveConflict");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20,
			MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: the project container DevPanel is hung into the host root and N is moved into it.
	UVerticalBox* DevPanel = WBP->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DevPanel"));
	if (UPanelWidget* Host = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		Host->AddChild(DevPanel);
	}
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("N present before customization"), N) || !TestTrue(TEXT("DevPanel added"), DevPanel != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2: the design moves N to ContainerB.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_ca"), TEXT("ContainerA"), 20, 20,
			MakeQAText(TEXT("qa_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeQAPanel(TEXT("qa_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeQAText(TEXT("qa_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* NAfter = NWeak.Get();
	if (TestNotNull(TEXT("N still alive"), NAfter))
	{
		TestEqual(TEXT("N kept under the project container DevPanel"),
			NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
	}
	TestNull(TEXT("no copy created under ContainerB"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container"), TEXT("no copy created") }));
	TestFalse(TEXT("N is not misreported as added"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 26 (integration, R3 nested delete conflict): the design-deleted nested node Tail contains a project
// child (DevChild) - Tail is kept + a reimport_remove_conflict naming the node and the reason;
// the silent keep of purely-D2U nested leftovers is covered by case 20 (NestedAddAndRemovalBoundary).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQANestedRemoveConflictTest,
	"Design2UMG.SafeReimportQA.NestedRemoveConflictNamesNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQANestedRemoveConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportNestedRemoveConflict");

	const FString V1 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24, FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
		*MakeQAPanel(TEXT("qa_tail"), TEXT("Tail"), 32, 60,
			MakeQAText(TEXT("qa_tail_in"), TEXT("TailInner"), 40, 66, TEXT("inner"))))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: the project child DevChild is hung into the nested node Tail.
	UWidget* Tail = WBP->WidgetTree->FindWidget(TEXT("Tail"));
	if (!TestNotNull(TEXT("nested Tail present in v1"), Tail))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(Tail)->AddChild(DevChild);

	// v2: Tail disappears from the design (Head stays and updates).
	const FString V2 = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("InfoPanel"), 24, 24,
		MakeQAText(TEXT("qa_head"), TEXT("Head"), 32, 32, TEXT("Head v2"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestNotNull(TEXT("project child stays inside the preserved node"),
		WBP->WidgetTree->FindWidget(TEXT("DevChild")));
	TestEqual(TEXT("nested remove conflict diagnosed once"), QACountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("conflict names the node and the project-content reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"), { TEXT("'Tail'"), TEXT("DevChild") }));
	if (UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head"))))
	{
		TestEqual(TEXT("kept sibling still refreshed"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 27 (integration, host-root form change): v1's interactive document generates the synthesized switcher
// host root (root_pc becomes a top-level instance); in v2, after the interactions vanish, root_pc directly
// becomes the IR root. Reimport must neither misreport root_pc's nested children as "host-root top-level
// added" (Keep_1) nor misreport root_pc itself as a move conflict; the deleted nested node N containing project content gets a remove-conflict diagnostic.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAHostRootShapeChangeTest,
	"Design2UMG.SafeReimportQA.HostRootShapeChangeNoMisdiagnosis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAHostRootShapeChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportHostRootShape");

	// v1: root_pc holds a button with an ON_CLICK interaction (a valid document navigation -> the switcher root is synthesized).
	const auto MakeInteractiveButton = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"pc_btn\",\"name\":\"Btn\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.2,\"b\":0.2,\"a\":1}}],"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://qa.example/rm\"}]}],"
				 "\"children\":[{\"id\":\"pc_btn_t\",\"name\":\"BtnText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":48,\"y\":48,\"width\":80,\"height\":20},\"characters\":\"press\",%s}]}"),
			*QATextStyle());
	};
	const auto MakeRoot = [&MakeInteractiveButton](bool bWithN)
	{
		const FString NBlock = FString::Printf(
			TEXT("{\"id\":\"pc_n\",\"name\":\"N\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":180,\"height\":120},"
				 "\"children\":[%s]},"),
			*MakeInteractiveButton());
		const FString KeepBlock = FString::Printf(
			TEXT("{\"id\":\"pc_other\",\"name\":\"Keep\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":260,\"y\":40,\"width\":120,\"height\":30},"
				 "\"characters\":\"keep\",%s}"),
			*QATextStyle());
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"root_pc\",\"name\":\"RemoveProjChild\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":440,\"height\":240},"
				 "\"children\":[%s%s]}]}]}}"),
			QAReimportScope,
			bWithN ? *NBlock : TEXT(""),
			*KeepBlock);
	};

	UWidgetBlueprint* WBP = QAImport(MakeRoot(true), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Precondition check: the synthesized switcher root + root_pc are top-level instances.
	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UWidget* RootPc = WBP->WidgetTree->FindWidget(TEXT("root_pc"));
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("root_pc is the page under the switcher"), RootPc)
		|| !TestNotNull(TEXT("nested N present in v1"), N))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(N)->AddChild(DevChild);

	// v2: the interaction and N vanish together -> no valid navigation -> root_pc directly becomes the root.
	const FDesignReimportResult Result = QAReimport(MakeRoot(false), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("Keep is not duplicated onto the host root"), WBP->WidgetTree->FindWidget(TEXT("Keep_1")));
	TestNotNull(TEXT("Keep stays under root_pc"), WBP->WidgetTree->FindWidget(TEXT("Keep")));
	TestEqual(TEXT("no false top-level add"), QACountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict for the page root"), QACountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("N design-deletion reported as remove conflict"), QACountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("remove conflict names N"),
		QAHasMessage(Result, TEXT("reimport_remove_conflict"), TEXT("'N'")));
	TestNotNull(TEXT("N preserved with its project child"), WBP->WidgetTree->FindWidget(TEXT("DevChild")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 33 (integration, slice C C2): after the canvas container gains a visual fill in v2, the converter
// inserts a synthetic inner layer for it (Border + inner canvas). Both the container's existing nested
// children and the newly moved-in node must pierce the synthetic layer to complete same-name matching:
// M refreshes in place, N moves from A into C (identity preserved), with no duplicate copies and no false additions.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQACompositeLayerNestedMoveTest,
	"Design2UMG.SafeReimportQA.CompositeLayerNestedMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQACompositeLayerNestedMoveTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportCompositeLayerMove");

	// C: a no-auto-layout container; bWithFill decides whether this version synthesizes the inner layer. A: a VERTICAL container.
	const auto MakeBox = [](const TCHAR* Id, const TCHAR* Name, const bool bWithFill, const bool bVertical, const FString& Children)
	{
		const FString Fills = bWithFill
			? TEXT("\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],")
			: FString();
		const FString Layout = bVertical ? TEXT("\"layoutMode\":\"VERTICAL\",") : FString();
		return FString::Printf(
			TEXT("{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":20,\"y\":20,\"width\":200,\"height\":120},%s%s\"children\":[%s]}"),
			Id, Name, *Fills, *Layout, *Children);
	};

	// v1: C has no fill (no inner layer) and directly holds M; A (VERTICAL) holds N.
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("qa_c"), TEXT("C"), false, false, MakeQAText(TEXT("qa_m"), TEXT("M"), 40, 40, TEXT("m-v1"))),
		*MakeBox(TEXT("qa_a"), TEXT("A"), true, true, MakeQAText(TEXT("qa_n"), TEXT("N"), 40, 40, TEXT("mv")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* MBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("M")));
	UTextBlock* NBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	UWidget* CBefore = WBP->WidgetTree->FindWidget(TEXT("C"));
	if (!TestNotNull(TEXT("M present in v1"), MBefore)
		|| !TestNotNull(TEXT("N present in v1"), NBefore)
		|| !TestNotNull(TEXT("C present in v1"), CBefore)
		|| !TestTrue(TEXT("v1 M sits directly under C (no synthetic layer)"),
			MBefore->GetParent() && MBefore->GetParent()->GetFName() == FName(TEXT("C"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> MWeak = MBefore;
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2: C gains a fill (the converter inserts the synthetic inner layer) and N moves from A into C.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("qa_c"), TEXT("C"), true, false, FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_m"), TEXT("M"), 40, 40, TEXT("m-v2")),
			*MakeQAText(TEXT("qa_n"), TEXT("N"), 40, 80, TEXT("mv")))),
		*MakeBox(TEXT("qa_a"), TEXT("A"), true, true, FString())));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	UTextBlock* MAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("M")));
	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("M still present"), MAfter))
	{
		TestTrue(TEXT("M identity preserved through the synthetic layer"), MAfter == MWeak.Get());
		TestEqual(TEXT("M keeps its parent C"), MAfter->GetParent()->GetFName(), FName(TEXT("C")));
		TestEqual(TEXT("M refreshed from the design"), MAfter->GetText().ToString(), FString(TEXT("m-v2")));
	}
	if (TestNotNull(TEXT("N still present"), NAfter))
	{
		TestTrue(TEXT("N identity preserved by the nested move"), NAfter == NWeak.Get());
		TestEqual(TEXT("N moved under C"), NAfter->GetParent()->GetFName(), FName(TEXT("C")));
		TestEqual(TEXT("moved N keeps its design text"), NAfter->GetText().ToString(), FString(TEXT("mv")));
	}
	TestNull(TEXT("no duplicate M created"), WBP->WidgetTree->FindWidget(TEXT("M_1")));
	TestNull(TEXT("no duplicate N created"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestTrue(TEXT("nested move diagnosed with identity preservation"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		QAHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 34 (integration, slice C C3): a single-page canvas root (with a fill -> the host root is a Border +
// synthetic inner layer, and the manifest's top-level instance records the inner layer) deletes the page's only
// subtree N: the safe-delete diagnostic must also name the design nodes inside the deleted subtree (widget
// names derive deterministically from design node names), not just reference the host synthetic layer.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAEmptyRootRemovalNamesDesignNodesTest,
	"Design2UMG.SafeReimportQA.EmptyRootRemovalNamesDesignNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAEmptyRootRemovalNamesDesignNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportEmptyRootRemoval");

	const auto MakeFilledRootDoc = [](const FString& Children)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"qa_root\",\"name\":\"QAPage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]}]}]}}"),
			QAReimportScope, *Children);
	};
	// The page's only subtree: N (VERTICAL with a fill -> Border) containing a Caption text.
	const FString NSubtree = FString::Printf(
		TEXT("{\"id\":\"qa_n\",\"name\":\"N\",\"type\":\"FRAME\",\"layoutMode\":\"VERTICAL\","
			 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":180,\"height\":120},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1}}],"
			 "\"children\":[%s]}"),
		*MakeQAText(TEXT("qa_cap"), TEXT("Caption"), 48, 48, TEXT("cap")));

	UWidgetBlueprint* WBP = QAImport(MakeFilledRootDoc(NSubtree), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestNotNull(TEXT("v1 N present"), WBP->WidgetTree->FindWidget(TEXT("N")))
		|| !TestNotNull(TEXT("v1 Caption present"), WBP->WidgetTree->FindWidget(TEXT("Caption")))
		|| !TestNotNull(TEXT("v1 inner layer present"), WBP->WidgetTree->FindWidget(TEXT("QAPage_inner"))))
	{
		QAWipeOutput(OutputDir);
		return false;
	}

	// v2: delete the only subtree; the page becomes an empty root.
	const FDesignReimportResult Result = QAReimport(MakeFilledRootDoc(FString()), nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("N removed"), WBP->WidgetTree->FindWidget(TEXT("N")));
	TestNull(TEXT("Caption removed"), WBP->WidgetTree->FindWidget(TEXT("Caption")));
	TestNull(TEXT("inner layer removed"), WBP->WidgetTree->FindWidget(TEXT("QAPage_inner")));

	// Diagnostic granularity: beyond the host synthetic layer, the design nodes (N, Caption) must be named.
	TestEqual(TEXT("one removal diagnostic"), QACountKind(Result, TEXT("reimport_removed")), 1);
	TestTrue(TEXT("removal diagnostic names the synthetic host layer and the design nodes"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"),
			{ TEXT("QAPage_inner"), TEXT("N"), TEXT("Caption"), TEXT("removed through the safe delete flow") }));

	// An empty page is a legal end state: the sidecar is written with 0 instances. Slice B semantics make the
	// load side reject an empty-instances sidecar (the next reimport goes full conflict for an unclear
	// relationship), so here the raw JSON is read directly to assert the records really emptied, instead of going through TryLoadSidecarForAsset.
	FString RawSidecar;
	if (TestTrue(TEXT("sidecar file readable"), QAReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), RawSidecar)))
	{
		TSharedPtr<FJsonObject> SidecarRoot;
		if (TestTrue(TEXT("sidecar JSON parses"), QAParseJsonObject(RawSidecar, SidecarRoot)))
		{
			const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
			if (TestTrue(TEXT("instances array present"), SidecarRoot->TryGetArrayField(TEXT("instances"), Instances) && Instances != nullptr))
			{
				TestEqual(TEXT("inner layer instance dropped from the manifest"), Instances->Num(), 0);
			}
			const TArray<TSharedPtr<FJsonValue>>* Owned = nullptr;
			if (TestTrue(TEXT("d2uOwned array present"), SidecarRoot->TryGetArrayField(TEXT("d2uOwned"), Owned) && Owned != nullptr))
			{
				// The host-root Border never enters d2uOwned anyway (the generation chain does not record the root widget);
				// an empty owned list after deleting the only subtree is a legal end state.
				TArray<FString> OwnedNames;
				for (const TSharedPtr<FJsonValue>& OwnedValue : *Owned)
				{
					OwnedNames.Add(OwnedValue->AsString());
				}
				TestFalse(TEXT("N dropped from d2uOwned"), OwnedNames.Contains(TEXT("N")));
				TestFalse(TEXT("Caption dropped from d2uOwned"), OwnedNames.Contains(TEXT("Caption")));
				TestFalse(TEXT("inner layer dropped from d2uOwned"), OwnedNames.Contains(TEXT("QAPage_inner")));
			}
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 42 (integration, R3 top-level reparent source-side ownership): after the developer moves a top-level
// generated node into a project-side container, reimport must not drag it back to the host root - it stays in
// the project container + a reimport_move_conflict names it; the design-side content update (text) still arrives (placement belongs to the project, content to the design).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQATopLevelReparentSourceOwnedTest,
	"Design2UMG.SafeReimportQA.TopLevelReparentSourceProjectContainerPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQATopLevelReparentSourceOwnedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportTopMoveSourceOwned");

	// v1: N is a top-level node directly under qa_root (a manifest instance); there is also a design container.
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv")),
		*MakeQAPanel(TEXT("qa_c"), TEXT("ContainerA"), 260, 20,
			MakeQAText(TEXT("qa_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: the project container DevPanel is hung into the host root and the top-level node N is moved into it.
	UVerticalBox* DevPanel = WBP->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DevPanel"));
	if (UPanelWidget* Host = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget))
	{
		Host->AddChild(DevPanel);
	}
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("N present before customization"), N) || !TestTrue(TEXT("DevPanel added"), DevPanel != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2: in the design N is still a top-level node under the root (the design did not move it); its text updates.
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_n"), TEXT("N"), 30, 30, TEXT("mv-v2")),
		*MakeQAPanel(TEXT("qa_c"), TEXT("ContainerA"), 260, 20,
			MakeQAText(TEXT("qa_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* NAfter = NWeak.Get();
	if (TestNotNull(TEXT("N still alive"), NAfter))
	{
		TestEqual(TEXT("N kept under the project container DevPanel"),
			NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
	}
	TestEqual(TEXT("top-level move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container") }));
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	QAWipeOutput(OutputDir);
	return true;
}

#endif

