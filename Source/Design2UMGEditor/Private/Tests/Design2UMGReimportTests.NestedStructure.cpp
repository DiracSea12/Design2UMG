// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe reimport: nested-structure boundaries (nested add/remove/move, synthetic layers, host-root shape change, top-level reparent).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "Tests/ComponentPropertyFixtures.h"

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
#include "Design2UMGReimportTestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;

// ---------------------------------------------------------------------------
// nested-structure boundary - design-added nested nodes inside an existing container go
// through the generation-chain rebuild; nested subtrees the design dropped keep their current state (the manifest records top-level instances only; nested deletion is outside the safe-delete scope).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNestedStructureBoundaryTest,
	"Design2UMG.SafeReimport.NestedAddAndRemovalBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNestedStructureBoundaryTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportNestedBoundary");

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

	// v2: the container's Head text changes + FreshLine is added; Tail disappears from the design.
	const FString V2 = MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, TEXT("Head v2")),
			*MakeFixtureText(TEXT("fixture_fresh"), TEXT("FreshLine"), 32, 60, TEXT("Fresh v2")))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
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
		FixtureHasMessageWithAll(Result, TEXT("reimport_added"), { TEXT("FreshLine"), TEXT("added under 'InfoPanel'") }));

	// The design-dropped nested subtree: kept as-is (not a manifest instance, never enters the safe-delete flow).
	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestEqual(TEXT("no removal performed"), FixtureCountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("no removal conflict"), FixtureCountKind(Result, TEXT("reimport_remove_conflict")), 0);
	TestEqual(TEXT("nested preserve reports partial sync"), FixtureCountKind(Result, TEXT("reimport_partial_sync")), 1);
	TestTrue(TEXT("nested preserve raises explicit degradation"), Result.bHasDegradation);
	TestEqual(TEXT("nested preserve is not plain success"), Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("container stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_panel")));
		TestTrue(TEXT("nested widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("Head")) && Sidecar.D2UOwned.Contains(TEXT("Tail")));
		TestTrue(TEXT("nested new node ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("FreshLine")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Property-collision name serialization (R2): a nested widget whose sanitized name collides with a
// UWidget base property is stored as Base_w<N> by the generator. Reimport must recognize that
// generated tail under its base name and refresh the existing widget in place; rebuilding it as
// a second widget would leave the old _w1 behind and create _w2 (duplicate content).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportPropertyCollisionNestedIdentityTest,
	"Design2UMG.SafeReimport.NestedPropertyCollisionIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportPropertyCollisionNestedIdentityTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportNestedPropertyCollision");

	const auto BuildDoc = [](const TCHAR* Text)
	{
		return MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
			MakeFixtureText(TEXT("fixture_tip"), TEXT("Tooltip Text"), 32, 32, Text)));
	};

	UWidgetBlueprint* WBP = FixtureImport(BuildDoc(TEXT("first")), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* TipBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("TooltipText_w1")));
	if (!TestNotNull(TEXT("property-collision widget is stored with the _w1 tail"), TipBefore))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	TestEqual(TEXT("v1 text applied"), TipBefore->GetText().ToString(), FString(TEXT("first")));

	const FDesignReimportResult Result = FixtureReimport(BuildDoc(TEXT("second")), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UTextBlock* TipAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("TooltipText_w1")));
	TestTrue(TEXT("existing _w1 widget is kept by identity"), TipAfter == TipBefore);
	TestEqual(TEXT("existing _w1 widget is refreshed in place"),
		TipAfter ? TipAfter->GetText().ToString() : FString(), FString(TEXT("second")));
	TestNull(TEXT("no duplicate _w2 widget is created"), WBP->WidgetTree->FindWidget(TEXT("TooltipText_w2")));
	TestEqual(TEXT("no removal conflict was reported"), FixtureCountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// when a nested mapped instance's IR parent is a non-instance nested container
// (SidePanel>InnerZone>Button) and the structure is unchanged, reimport must not emit a misleading
// reimport_move_conflict (a diagnostic must mean a structural change was really skipped); the instance refreshes as usual.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNestedParentNoNoiseTest,
	"Design2UMG.SafeReimport.NestedParentNoMoveConflictNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNestedParentNoNoiseTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportNestedNoNoise");

	// SidePanel > InnerZone (a nested pure container, not a manifest instance) > the mapped button.
	const auto BuildDoc = [](const TCHAR* Label)
	{
		return MakeFixtureDoc(FString::Printf(
			TEXT("%s,%s"),
			*MakeFixturePanel(TEXT("fixture_side"), TEXT("SidePanel"), 24, 24, FString::Printf(
				TEXT("%s,%s"),
				*MakeFixturePanel(TEXT("fixture_inner"), TEXT("InnerZone"), 32, 32,
					MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 40, 40, Label)),
				*MakeFixtureText(TEXT("fixture_spad"), TEXT("SidePad"), 240, 32, TEXT("Pad")))),
			*MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 250, 200, TEXT("Keep"))));
	};

	UWidgetBlueprint* WBP = FixtureImport(BuildDoc(TEXT("Checkout")), FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UUserWidget* Mapped = FixtureFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("nested mapped instance generated"), Mapped)
		|| !TestTrue(TEXT("instance sits under InnerZone"),
			Mapped->GetParent() && Mapped->GetParent()->GetFName() == FName(TEXT("InnerZone"))))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UUserWidget> MappedWeak = Mapped;

	// A structure-unchanged reimport (only the input value changes): no move-conflict noise.
	const FDesignReimportResult Result = FixtureReimport(BuildDoc(TEXT("Pay Now")), FixtureMappedClassPath, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("no move-conflict noise for the unchanged nested parent"),
		FixtureCountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("no moves applied"), FixtureCountKind(Result, TEXT("reimport_moved")), 0);

	UUserWidget* MappedAfter = MappedWeak.Get();
	if (TestNotNull(TEXT("instance still alive"), MappedAfter))
	{
		TestEqual(TEXT("instance stays under the nested container"),
			MappedAfter->GetParent()->GetFName(), FName(TEXT("InnerZone")));
		TestEqual(TEXT("instance still refreshed normally"), ComponentFixtures::Read<FTextProperty>(MappedAfter, TEXT("TextValue")).ToString(), FString(TEXT("Pay Now")));
	}
	TestTrue(TEXT("instance refresh diagnosed"), FixtureHasMessage(Result, TEXT("reimport_updated"), TEXT("HeroButton")));

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// the design moves the nested node N from ContainerA to
// ContainerB - when the source end is purely D2U-owned, the widget moves (identity preserved, name
// unchanged, nothing left at the source, no copy built at the target) + a reimport_moved diagnostic; no false reimport_added.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNestedMoveIdentityTest,
	"Design2UMG.SafeReimport.NestedMovePreservesIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNestedMoveIdentityTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportNestedMove");

	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeFixtureText(TEXT("fixture_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeFixturePanel(TEXT("fixture_cb"), TEXT("ContainerB"), 260, 20,
			MakeFixtureText(TEXT("fixture_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UTextBlock* NBefore = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (!TestNotNull(TEXT("nested N present in v1"), NBefore)
		|| !TestTrue(TEXT("N sits under ContainerA"),
			NBefore->GetParent() && NBefore->GetParent()->GetFName() == FName(TEXT("ContainerA"))))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2: N moves from ContainerA to ContainerB.
	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_ca"), TEXT("ContainerA"), 20, 20,
			MakeFixtureText(TEXT("fixture_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeFixturePanel(TEXT("fixture_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeFixtureText(TEXT("fixture_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("N still present after move"), NAfter))
	{
		TestTrue(TEXT("widget object identity preserved"), NAfter == NWeak.Get());
		TestEqual(TEXT("N moved under ContainerB"), NAfter->GetParent()->GetFName(), FName(TEXT("ContainerB")));
		TestEqual(TEXT("moved N keeps its design text"), NAfter->GetText().ToString(), FString(TEXT("mv")));
	}
	TestNull(TEXT("no duplicate copy created under the target"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move diagnosed once"), FixtureCountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("nested move diagnostic names node and identity preservation"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		FixtureHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// the developer already hung the nested node N under the
// project container DevPanel, and the design moves N to ContainerB - N stays in DevPanel, ContainerB must
// not get a new copy, and a reimport_move_conflict names N and the reason.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNestedMoveConflictNoCopyTest,
	"Design2UMG.SafeReimport.NestedMoveConflictCreatesNoCopy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNestedMoveConflictNoCopyTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportNestedMoveConflict");

	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_ca"), TEXT("ContainerA"), 20, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeFixtureText(TEXT("fixture_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint")))),
		*MakeFixturePanel(TEXT("fixture_cb"), TEXT("ContainerB"), 260, 20,
			MakeFixtureText(TEXT("fixture_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint")))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
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
		FixtureWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2: the design moves N to ContainerB.
	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_ca"), TEXT("ContainerA"), 20, 20,
			MakeFixtureText(TEXT("fixture_ah"), TEXT("AHint"), 30, 80, TEXT("a-hint"))),
		*MakeFixturePanel(TEXT("fixture_cb"), TEXT("ContainerB"), 260, 20, FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 30, 30, TEXT("mv")),
			*MakeFixtureText(TEXT("fixture_bh"), TEXT("BHint"), 30, 80, TEXT("b-hint"))))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UWidget* NAfter = NWeak.Get();
	if (TestNotNull(TEXT("N still alive"), NAfter))
	{
		TestEqual(TEXT("N kept under the project container DevPanel"),
			NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
	}
	TestNull(TEXT("no copy created under ContainerB"), WBP->WidgetTree->FindWidget(TEXT("N_1")));
	TestEqual(TEXT("nested move conflict diagnosed once"), FixtureCountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container"), TEXT("no copy created") }));
	TestFalse(TEXT("N is not misreported as added"), FixtureHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	FixtureWipeOutput(OutputDir);
	return true;
}

// Keep a design-deleted nested node that contains project content, and report its
// removal conflict by node name. Owned-only retention is covered by NestedAddAndRemovalBoundary.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportNestedRemoveConflictTest,
	"Design2UMG.SafeReimport.NestedRemoveConflictNamesNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportNestedRemoveConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportNestedRemoveConflict");

	const FString V1 = MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24, FString::Printf(
		TEXT("%s,%s"),
		*MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, TEXT("Head v1")),
		*MakeFixturePanel(TEXT("fixture_tail"), TEXT("Tail"), 32, 60,
			MakeFixtureText(TEXT("fixture_tail_in"), TEXT("TailInner"), 40, 66, TEXT("inner"))))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Developer customization: the project child DevChild is hung into the nested node Tail.
	UWidget* Tail = WBP->WidgetTree->FindWidget(TEXT("Tail"));
	if (!TestNotNull(TEXT("nested Tail present in v1"), Tail))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(Tail)->AddChild(DevChild);

	// v2: Tail disappears from the design (Head stays and updates).
	const FString V2 = MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("InfoPanel"), 24, 24,
		MakeFixtureText(TEXT("fixture_head"), TEXT("Head"), 32, 32, TEXT("Head v2"))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNotNull(TEXT("design-removed nested node preserved"), WBP->WidgetTree->FindWidget(TEXT("Tail")));
	TestNotNull(TEXT("project child stays inside the preserved node"),
		WBP->WidgetTree->FindWidget(TEXT("DevChild")));
	TestEqual(TEXT("nested remove conflict diagnosed once"), FixtureCountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("conflict names the node and the project-content reason"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_remove_conflict"), { TEXT("'Tail'"), TEXT("DevChild") }));
	UTextBlock* Head = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Head")));
	if (TestNotNull(TEXT("kept sibling Head still present"), Head))
	{
		TestEqual(TEXT("kept sibling still refreshed"), Head->GetText().ToString(), FString(TEXT("Head v2")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// v1's interactive document generates the synthesized switcher
// host root (root_pc becomes a top-level instance); in v2, after the interactions vanish, root_pc directly
// becomes the IR root. Reimport must neither misreport root_pc's nested children as "host-root top-level
// added" (Keep_1) nor misreport root_pc itself as a move conflict; the deleted nested node N containing project content gets a remove-conflict diagnostic.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportHostRootShapeChangeTest,
	"Design2UMG.SafeReimport.HostRootShapeChangeNoMisdiagnosis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportHostRootShapeChangeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportHostRootShape");

	// v1: root_pc holds a button with an ON_CLICK interaction (a valid document navigation -> the switcher root is synthesized).
	const auto MakeInteractiveButton = []()
	{
		return FString::Printf(
			TEXT("{\"id\":\"pc_btn\",\"name\":\"Btn\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":120,\"height\":44},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.2,\"g\":0.2,\"b\":0.2,\"a\":1}}],"
				 "\"interactions\":[{\"trigger\":{\"type\":\"ON_CLICK\"},\"actions\":[{\"type\":\"URL\",\"url\":\"https://fixture.example/rm\"}]}],"
				 "\"children\":[{\"id\":\"pc_btn_t\",\"name\":\"BtnText\",\"type\":\"TEXT\","
				 "\"absoluteBoundingBox\":{\"x\":48,\"y\":48,\"width\":80,\"height\":20},\"characters\":\"press\",%s}]}"),
			*FixtureTextStyle());
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
			*FixtureTextStyle());
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"root_pc\",\"name\":\"RemoveProjChild\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":440,\"height\":240},"
				 "\"children\":[%s%s]}]}]}}"),
			FixtureReimportScope,
			bWithN ? *NBlock : TEXT(""),
			*KeepBlock);
	};

	UWidgetBlueprint* WBP = FixtureImport(MakeRoot(true), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Precondition check: the synthesized switcher root + root_pc are top-level instances.
	if (!TestTrue(TEXT("v1 host root is the synthetic switcher"),
			WBP->WidgetTree->RootWidget && WBP->WidgetTree->RootWidget->IsA<UWidgetSwitcher>()))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	UWidget* RootPc = WBP->WidgetTree->FindWidget(TEXT("root_pc"));
	UWidget* N = WBP->WidgetTree->FindWidget(TEXT("N"));
	if (!TestNotNull(TEXT("root_pc is the page under the switcher"), RootPc)
		|| !TestNotNull(TEXT("nested N present in v1"), N))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	UTextBlock* DevChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DevChild"));
	DevChild->SetText(FText::FromString(TEXT("dev")));
	Cast<UPanelWidget>(N)->AddChild(DevChild);

	// v2: the interaction and N vanish together -> no valid navigation -> root_pc directly becomes the root.
	const FDesignReimportResult Result = FixtureReimport(MakeRoot(false), nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("Keep is not duplicated onto the host root"), WBP->WidgetTree->FindWidget(TEXT("Keep_1")));
	TestNotNull(TEXT("Keep stays under root_pc"), WBP->WidgetTree->FindWidget(TEXT("Keep")));
	TestEqual(TEXT("no false top-level add"), FixtureCountKind(Result, TEXT("reimport_added")), 0);
	TestEqual(TEXT("no false move conflict for the page root"), FixtureCountKind(Result, TEXT("reimport_move_conflict")), 0);
	TestEqual(TEXT("N design-deletion reported as remove conflict"), FixtureCountKind(Result, TEXT("reimport_remove_conflict")), 1);
	TestTrue(TEXT("remove conflict names N"),
		FixtureHasMessage(Result, TEXT("reimport_remove_conflict"), TEXT("'N'")));
	TestNotNull(TEXT("N preserved with its project child"), WBP->WidgetTree->FindWidget(TEXT("DevChild")));

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// after the canvas container gains a visual fill in v2, the converter
// inserts a synthetic inner layer for it (Border + inner canvas). Both the container's existing nested
// children and the newly moved-in node must pierce the synthetic layer to complete same-name matching:
// M refreshes in place, N moves from A into C (identity preserved), with no duplicate copies and no false additions.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportCompositeLayerNestedMoveTest,
	"Design2UMG.SafeReimport.CompositeLayerNestedMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportCompositeLayerNestedMoveTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportCompositeLayerMove");

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
	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("fixture_c"), TEXT("C"), false, false, MakeFixtureText(TEXT("fixture_m"), TEXT("M"), 40, 40, TEXT("m-v1"))),
		*MakeBox(TEXT("fixture_a"), TEXT("A"), true, true, MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 40, 40, TEXT("mv")))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
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
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const TWeakObjectPtr<UTextBlock> MWeak = MBefore;
	const TWeakObjectPtr<UTextBlock> NWeak = NBefore;

	// v2: C gains a fill (the converter inserts the synthetic inner layer) and N moves from A into C.
	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeBox(TEXT("fixture_c"), TEXT("C"), true, false, FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_m"), TEXT("M"), 40, 40, TEXT("m-v2")),
			*MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 40, 80, TEXT("mv")))),
		*MakeBox(TEXT("fixture_a"), TEXT("A"), true, true, FString())));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
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
		FixtureHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("'N'"), TEXT("widget identity preserved") }));
	TestFalse(TEXT("N is not misreported as added"),
		FixtureHasMessage(Result, TEXT("reimport_added"), TEXT("'N'")));

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// a single-page canvas root (with a fill -> the host root is a Border +
// synthetic inner layer, and the manifest's top-level instance records the inner layer) deletes the page's only
// subtree N: the safe-delete diagnostic must also name the design nodes inside the deleted subtree (widget
// names derive deterministically from design node names), not just reference the host synthetic layer.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportEmptyRootRemovalNamesDesignNodesTest,
	"Design2UMG.SafeReimport.EmptyRootRemovalNamesDesignNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportEmptyRootRemovalNamesDesignNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportEmptyRootRemoval");

	const auto MakeFilledRootDoc = [](const FString& Children)
	{
		return FString::Printf(
			TEXT("{\"fileKey\":\"%s\",\"document\":{\"id\":\"0:0\",\"name\":\"Doc\",\"type\":\"DOCUMENT\",\"children\":["
				 "{\"id\":\"0:1\",\"name\":\"Page\",\"type\":\"CANVAS\",\"children\":["
				 "{\"id\":\"fixture_root\",\"name\":\"FixturePage\",\"type\":\"FRAME\","
				 "\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":360,\"height\":240},"
				 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.9,\"g\":0.9,\"b\":0.9,\"a\":1}}],"
				 "\"children\":[%s]}]}]}}"),
			FixtureReimportScope, *Children);
	};
	// The page's only subtree: N (VERTICAL with a fill -> Border) containing a Caption text.
	const FString NSubtree = FString::Printf(
		TEXT("{\"id\":\"fixture_n\",\"name\":\"N\",\"type\":\"FRAME\",\"layoutMode\":\"VERTICAL\","
			 "\"absoluteBoundingBox\":{\"x\":40,\"y\":40,\"width\":180,\"height\":120},"
			 "\"fills\":[{\"type\":\"SOLID\",\"visible\":true,\"color\":{\"r\":0.8,\"g\":0.8,\"b\":0.8,\"a\":1}}],"
			 "\"children\":[%s]}"),
		*MakeFixtureText(TEXT("fixture_cap"), TEXT("Caption"), 48, 48, TEXT("cap")));

	UWidgetBlueprint* WBP = FixtureImport(MakeFilledRootDoc(NSubtree), nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	if (!TestNotNull(TEXT("v1 N present"), WBP->WidgetTree->FindWidget(TEXT("N")))
		|| !TestNotNull(TEXT("v1 Caption present"), WBP->WidgetTree->FindWidget(TEXT("Caption")))
		|| !TestNotNull(TEXT("v1 inner layer present"), WBP->WidgetTree->FindWidget(TEXT("FixturePage_inner"))))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}

	// v2: delete the only subtree; the page becomes an empty root.
	const FDesignReimportResult Result = FixtureReimport(MakeFilledRootDoc(FString()), nullptr, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("N removed"), WBP->WidgetTree->FindWidget(TEXT("N")));
	TestNull(TEXT("Caption removed"), WBP->WidgetTree->FindWidget(TEXT("Caption")));
	TestNull(TEXT("inner layer removed"), WBP->WidgetTree->FindWidget(TEXT("FixturePage_inner")));

	// Diagnostic granularity: beyond the host synthetic layer, the design nodes (N, Caption) must be named.
	TestEqual(TEXT("one removal diagnostic"), FixtureCountKind(Result, TEXT("reimport_removed")), 1);
	TestTrue(TEXT("removal diagnostic names the synthetic host layer and the design nodes"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_removed"),
			{ TEXT("FixturePage_inner"), TEXT("N"), TEXT("Caption"), TEXT("removed through the safe delete flow") }));

	// An empty page is a legal end state: the sidecar is written with 0 instances. Reimport semantics make the
	// load side reject an empty-instances sidecar (the next reimport goes full conflict for an unclear
	// relationship), so here the raw JSON is read directly to assert the records really emptied, instead of going through TryLoadSidecarForAsset.
	FString RawSidecar;
	if (TestTrue(TEXT("sidecar file readable"), FixtureReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(TargetPackage), RawSidecar)))
	{
		TSharedPtr<FJsonObject> SidecarRoot;
		if (TestTrue(TEXT("sidecar JSON parses"), FixtureParseJsonObject(RawSidecar, SidecarRoot)))
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
				TestFalse(TEXT("inner layer dropped from d2uOwned"), OwnedNames.Contains(TEXT("FixturePage_inner")));
			}
		}
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// after the developer moves a top-level
// generated node into a project-side container, reimport must not drag it back to the host root - it stays in
// the project container + a reimport_move_conflict names it; the design-side content update (text) still arrives (placement belongs to the project, content to the design).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportTopLevelReparentSourceOwnedTest,
	"Design2UMG.SafeReimport.TopLevelReparentSourceProjectContainerPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportTopLevelReparentSourceOwnedTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportTopMoveSourceOwned");

	// v1: N is a top-level node directly under fixture_root (a manifest instance); there is also a design container.
	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 30, 30, TEXT("mv")),
		*MakeFixturePanel(TEXT("fixture_c"), TEXT("ContainerA"), 260, 20,
			MakeFixtureText(TEXT("fixture_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
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
		FixtureWipeOutput(OutputDir);
		return false;
	}
	WBP->WidgetTree->RemoveWidget(N);
	DevPanel->AddChild(N);
	const TWeakObjectPtr<UWidget> NWeak = N;

	// v2: in the design N is still a top-level node under the root (the design did not move it); its text updates.
	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixtureText(TEXT("fixture_n"), TEXT("N"), 30, 30, TEXT("mv-v2")),
		*MakeFixturePanel(TEXT("fixture_c"), TEXT("ContainerA"), 260, 20,
			MakeFixtureText(TEXT("fixture_h"), TEXT("AHint"), 30, 80, TEXT("a-hint")))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UTextBlock* NAfter = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("N")));
	if (TestNotNull(TEXT("N still present in the widget tree"), NAfter))
	{
		TestTrue(TEXT("N identity preserved"), NAfter == NWeak.Get());
		if (TestNotNull(TEXT("N still has a parent"), NAfter->GetParent()))
		{
			TestEqual(TEXT("N kept under the project container DevPanel"),
				NAfter->GetParent()->GetFName(), FName(TEXT("DevPanel")));
		}
		TestEqual(TEXT("Design text refreshed despite the top-level move conflict"),
			NAfter->GetText().ToString(), FString(TEXT("mv-v2")));
	}
	TestEqual(TEXT("top-level move conflict diagnosed once"), FixtureCountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("conflict names N and the project-container reason"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_move_conflict"),
			{ TEXT("'N'"), TEXT("project-side container") }));
	TestEqual(TEXT("no moves applied"), FixtureCountKind(Result, TEXT("reimport_moved")), 0);

	FixtureWipeOutput(OutputDir);
	return true;
}

#endif
