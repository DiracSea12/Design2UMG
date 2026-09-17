// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe Reimport QA: top-level structure diff cases (added nodes, safe remove, remove/kind-change conflicts, move and move conflict).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "Tests/Design2UMGMappingQAWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Design2UMGReimportQATestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportQA;

// ---------------------------------------------------------------------------
// Case 8 (integration): added - a new top-level purely generated node and a new mapped instance join the tree and
// are recorded in the new manifest (the mapped record carries key/class/inputs declarations; the inputs values are already written into the instance per the design).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAAddedNodesTest,
	"Design2UMG.SafeReimportQA.AddedNodesAndMappedInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAAddedNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportAdded");

	const FString V1 = MakeQADoc(MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = QAImport(V1, QAMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNull(TEXT("v1 has no ExtraPanel yet"), WBP->WidgetTree->FindWidget(TEXT("ExtraPanel")));
	TestNull(TEXT("v1 has no Sidekick yet"), WBP->WidgetTree->FindWidget(TEXT("Sidekick")));

	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAText(TEXT("qa_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")),
		*MakeQAPanel(TEXT("qa_extra"), TEXT("ExtraPanel"), 24, 90,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_beta"), TEXT("Beta"), 32, 98, TEXT("Beta")),
				*MakeQAText(TEXT("qa_beta2"), TEXT("BetaTwo"), 32, 126, TEXT("Beta 2")))),
		*MakeQAButton(TEXT("qa_btn_2"), TEXT("Sidekick"), 250, 90, TEXT("Jump In"))));

	const FDesignReimportResult Result = QAReimport(V2, QAMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("two top-level adds diagnosed"), QACountKind(Result, TEXT("reimport_added")), 2);
	TestTrue(TEXT("added panel diagnosed"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("ExtraPanel")));
	TestTrue(TEXT("added mapped instance diagnosed"), QAHasMessage(Result, TEXT("reimport_added"), TEXT("Sidekick")));

	// The new purely generated node + nested children join.
	UWidget* ExtraPanel = WBP->WidgetTree->FindWidget(TEXT("ExtraPanel"));
	TestNotNull(TEXT("new generated node added"), ExtraPanel);
	if (UTextBlock* Beta = Cast<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Beta"))))
	{
		TestEqual(TEXT("nested new child text applied"), Beta->GetText().ToString(), FString(TEXT("Beta")));
		if (ExtraPanel)
		{
			TestEqual(TEXT("nested new child parented under the new panel"), Beta->GetParent()->GetFName(), ExtraPanel->GetFName());
		}
	}
	else
	{
		AddError(TEXT("nested new child missing"));
	}

	// The new mapped instance joins and its inputs are already written per the design.
	UDesign2UMGMappingQAWidget* Sidekick = Cast<UDesign2UMGMappingQAWidget>(WBP->WidgetTree->FindWidget(TEXT("Sidekick")));
	TestNotNull(TEXT("new mapped instance added"), Sidekick);
	if (Sidekick)
	{
		TestEqual(TEXT("new mapped instance input written"), Sidekick->TextValue.ToString(), FString(TEXT("Jump In")));
	}

	// The manifest records the new relations.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Extra =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_extra"));
		if (TestNotNull(TEXT("new generated node recorded"), Extra))
		{
			TestEqual(TEXT("generated kind recorded"), Extra->Kind, FString(TEXT("generated")));
			TestEqual(TEXT("generated widget name recorded"), Extra->WidgetName, FString(TEXT("ExtraPanel")));
		}
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Mapped =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_2"));
		if (TestNotNull(TEXT("new mapped instance recorded"), Mapped))
		{
			TestEqual(TEXT("mapped kind recorded"), Mapped->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("mapped key recorded"), Mapped->MappedKey, FString(QAMappingKey));
			TestEqual(TEXT("mapped class recorded"), Mapped->MappedClass, FString(QAMappedClassPath));
			if (TestEqual(TEXT("mapped inputs declaration recorded"), Mapped->Inputs.Num(), 1) && Mapped->Inputs.Num() == 1)
			{
				TestEqual(TEXT("input source recorded"), Mapped->Inputs[0].Source, FString(TEXT("label")));
				TestEqual(TEXT("input target recorded"), Mapped->Inputs[0].Target, FString(TEXT("TextValue")));
				TestEqual(TEXT("input valueKind recorded"), Mapped->Inputs[0].ValueKind, FString(TEXT("ComponentProperty")));
			}
		}
		TestTrue(TEXT("new panel ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("ExtraPanel")));
		TestTrue(TEXT("new nested child ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("Beta")));
		TestTrue(TEXT("new mapped instance ownership recorded"), Sidecar.D2UOwned.Contains(TEXT("Sidekick")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 9 (integration): safe remove, positive - a purely D2U-owned node is deleted safely;
// a node the design deleted but the project already removed from the asset is only dropped from the records (no error, no wrongful delete).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARemovePureNodeTest,
	"Design2UMG.SafeReimportQA.RemoveDeletesPureD2UNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARemovePureNodeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRemovePure");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAText(TEXT("qa_clean"), TEXT("CleanStat"), 24, 24, TEXT("Old")),
		*MakeQAText(TEXT("qa_ghost"), TEXT("GhostStat"), 140, 24, TEXT("Gone")),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// The project side already deleted GhostStat's widget in the editor (the manifest still records it).
	// Uses the editor's own DeleteWidgets (destroys the objects + cleans variable GUIDs); a bare RemoveWidget merely
	// unhooks it from the tree while the object stays alive - any later compile would trip the compiler's ensure on the detached widget/GUID.
	if (UWidget* Ghost = WBP->WidgetTree->FindWidget(TEXT("GhostStat")))
	{
		FWidgetBlueprintEditorUtils::DeleteWidgets(
			WBP, { Ghost }, FWidgetBlueprintEditorUtils::EDeleteWidgetWarningType::DeleteSilently);
	}

	const FString V2 = MakeQADoc(MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("purely owned node deleted from tree"), WBP->WidgetTree->FindWidget(TEXT("CleanStat")));
	TestNull(TEXT("already-absent node stays absent"), WBP->WidgetTree->FindWidget(TEXT("GhostStat")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("two removal diagnostics"), QACountKind(Result, TEXT("reimport_removed")), 2);
	TestTrue(TEXT("safe-delete flow diagnosed for present widget"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("CleanStat"), TEXT("removed through the safe delete flow") }));
	TestTrue(TEXT("already-absent branch diagnosed"),
		QAHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("GhostStat"), TEXT("widget already absent from the tree") }));
	TestEqual(TEXT("no remove conflicts"), QACountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("deleted node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_clean")));
		TestNull(TEXT("absent node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_ghost")));
		TestNotNull(TEXT("kept node still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_keep")));
		TestFalse(TEXT("deleted node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("CleanStat")));
		TestFalse(TEXT("absent node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("GhostStat")));
		TestTrue(TEXT("kept node still owned"), Sidecar.D2UOwned.Contains(TEXT("Keeper")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 10 (integration): remove conflict - when the deleted node's subtree contains project content (project-added
// children / developer bindings), no delete: kept + a conflict diagnostic stating the reason, and the new manifest keeps the instance.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQARemoveProjectContentTest,
	"Design2UMG.SafeReimportQA.RemovePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQARemoveProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportRemoveConflict");

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeQAPanel(TEXT("qa_doom"), TEXT("DoomedPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_dhead"), TEXT("DoomHead"), 32, 32, TEXT("Doom")),
				*MakeQAText(TEXT("qa_dtail"), TEXT("DoomTail"), 32, 60, TEXT("Doom")))),
		*MakeQAPanel(TEXT("qa_bound"), TEXT("BoundPanel"), 24, 160,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_bhead"), TEXT("BoundHead"), 32, 168, TEXT("Bound")),
				*MakeQAText(TEXT("qa_btail"), TEXT("BoundTail"), 32, 196, TEXT("Bound")))),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// Project-content signal A: a project-added child hangs inside the DoomedPanel subtree.
	if (UPanelWidget* Doomed = Cast<UPanelWidget>(WBP->WidgetTree->FindWidget(TEXT("DoomedPanel"))))
	{
		UTextBlock* ProjectChild = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProjectChild"));
		Doomed->AddChild(ProjectChild);
	}
	// Project-content signal B: WBP Bindings reference a widget inside the BoundPanel subtree.
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("BoundPanel");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleBound");
	WBP->Bindings.Add(ProjectBinding);

	const FString V2 = MakeQADoc(MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflicts are per-node)"), Result.bSucceeded);

	TestNotNull(TEXT("panel with project child preserved"), WBP->WidgetTree->FindWidget(TEXT("DoomedPanel")));
	TestNotNull(TEXT("project child preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	TestNotNull(TEXT("panel with developer binding preserved"), WBP->WidgetTree->FindWidget(TEXT("BoundPanel")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("no silent delete"), QACountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("two remove conflicts"), QACountKind(Result, TEXT("reimport_remove_conflict")), 2);
	TestTrue(TEXT("unowned-widget reason names the project child"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("DoomedPanel"), TEXT("widgets outside the generation record"), TEXT("ProjectChild") }));
	TestTrue(TEXT("developer-binding reason names the node"),
		QAHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("BoundPanel"), TEXT("developer bindings in the blueprint") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("conflicted panel stays recorded (unowned child)"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_doom")));
		TestNotNull(TEXT("conflicted panel stays recorded (binding)"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_bound")));
		TestTrue(TEXT("subtree widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("DoomHead")) && Sidecar.D2UOwned.Contains(TEXT("BoundTail")));
		TestFalse(TEXT("project child stays outside d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("ProjectChild")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 11 (integration): kind-changed conflict - a mapped->generated conversion whose old subtree contains
// project content (developer bindings) -> kept as-is + conflict diagnostic, not replaced; the manifest record is unchanged.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAKindChangeProjectContentTest,
	"Design2UMG.SafeReimportQA.KindChangePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAKindChangeProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportKindConflict");

	const FString V1 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
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

	// The developer binding hangs on the mapped instance (the subtree root): the old subtree is no longer purely D2U-owned.
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("HeroButton");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleHero");
	WBP->Bindings.Add(ProjectBinding);

	// v2: the same design node, but this round no longer provides the mapping -> kind flips mapped->generated.
	const FString V2 = MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UDesign2UMGMappingQAWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("mapped instance kept"), MappedAfter);
	if (MappedAfter)
	{
		TestEqual(TEXT("mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(QAMappedClassPath));
	}
	TestEqual(TEXT("kind change conflict diagnosed once"), QACountKind(Result, TEXT("reimport_kind_conflict")), 1);
	TestTrue(TEXT("conflict reports mapped -> generated and the reason on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_kind_conflict"),
			{ TEXT("HeroButton"), TEXT("mapped -> generated"), TEXT("developer bindings in the blueprint") }));
	TestEqual(TEXT("no replacement performed"), QACountKind(Result, TEXT("reimport_kind_changed")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind stays mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("manifest mapped class stays"), Record->MappedClass, FString(QAMappedClassPath));
			TestEqual(TEXT("manifest widget name stays"), Record->WidgetName, FString(TEXT("HeroButton")));
		}
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 12 (integration): move, positive - the design moves a node into a newly added container; both ends clearly
// D2U-owned -> the widget changes parents but the object identity survives; manifest top-level instances consolidated, ownership kept.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMoveWithinOwnedBoundsTest,
	"Design2UMG.SafeReimportQA.MoveReparentWithinOwnedBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMoveWithinOwnedBoundsTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMoveOk");

	// MoverPanel is a fill-less two-child pure container in both versions (type consistent across versions); in v1 it hangs under the host root.
	const FString MoverPanelJson = MakeQAPanel(TEXT("qa_mover"), TEXT("MoverPanel"), 40, 32,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeQAText(TEXT("qa_ma"), TEXT("MoverA"), 48, 40, TEXT("A")),
			*MakeQAText(TEXT("qa_mb"), TEXT("MoverB"), 48, 68, TEXT("B"))));

	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MoverPanelJson,
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UWidget* MoverBefore = WBP->WidgetTree->FindWidget(TEXT("MoverPanel"));
	if (!TestNotNull(TEXT("MoverPanel present in v1"), MoverBefore))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UWidget> MoverWeak = MoverBefore;
	TestEqual(TEXT("v1 parent is the page host root"),
		MoverBefore->GetParent()->GetFName(), WBP->WidgetTree->RootWidget->GetFName());

	// v2: the new container TargetBox adopts MoverPanel (the whole subtree moves along).
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_target"), TEXT("TargetBox"), 24, 120,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAText(TEXT("qa_pad"), TEXT("PadText"), 32, 128, TEXT("Pad")),
				*MakeQAPanel(TEXT("qa_mover"), TEXT("MoverPanel"), 32, 160,
					FString::Printf(
						TEXT("%s,%s"),
						*MakeQAText(TEXT("qa_ma"), TEXT("MoverA"), 40, 168, TEXT("A")),
						*MakeQAText(TEXT("qa_mb"), TEXT("MoverB"), 40, 196, TEXT("B")))))),
		*MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	UWidget* MoverAfter = MoverWeak.Get();
	TestNotNull(TEXT("moved widget still alive"), MoverAfter);
	UWidget* TargetBox = WBP->WidgetTree->FindWidget(TEXT("TargetBox"));
	TestNotNull(TEXT("target container added"), TargetBox);
	if (MoverAfter && TargetBox)
	{
		TestTrue(TEXT("widget object identity preserved across reparent"), MoverAfter == MoverBefore);
		TestEqual(TEXT("moved under the new container"), MoverAfter->GetParent()->GetFName(), TargetBox->GetFName());
		if (UWidget* MoverChild = WBP->WidgetTree->FindWidget(TEXT("MoverA")))
		{
			TestEqual(TEXT("mover subtree travelled with it"), MoverChild->GetParent()->GetFName(), MoverAfter->GetFName());
		}
		else
		{
			AddError(TEXT("mover subtree child missing after move"));
		}
	}

	TestEqual(TEXT("one move diagnosed"), QACountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("move diagnostic reports identity preserved on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("MoverPanel"), TEXT("widget identity preserved") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("moved node no longer a top-level instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_mover")));
		TestNotNull(TEXT("new container recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_target")));
		TestTrue(TEXT("moved node still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverPanel")));
		TestTrue(TEXT("moved subtree still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverA")) && Sidecar.D2UOwned.Contains(TEXT("MoverB")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// Case 13 (integration): move conflict - the new design's parent end is neither a manifest instance nor a D2U node
// created this round (a nested container, unclear ownership) -> structure kept + a move-conflict diagnostic; no move.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportQAMoveBoundaryConflictTest,
	"Design2UMG.SafeReimportQA.MoveConflictOnUnclearBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportQAMoveBoundaryConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/QAReimportMoveConflict");

	// v1: SidePanel > InnerZone (a nested container, not a manifest instance) + the top-level Mover.
	const FString InnerZoneChildren = FString::Printf(
		TEXT("%s,%s"),
		*MakeQAText(TEXT("qa_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
		*MakeQAText(TEXT("qa_i2"), TEXT("InnerB"), 40, 68, TEXT("B")));
	const FString V1 = MakeQADoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32, InnerZoneChildren),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad")))),
		*MakeQAText(TEXT("qa_mover"), TEXT("Mover"), 250, 24, TEXT("Move Me"))));
	UWidgetBlueprint* WBP = QAImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		QAWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: the design moves Mover into InnerZone (a nested container, boundary unclear).
	const FString V2 = MakeQADoc(FString::Printf(
		TEXT("%s"),
		*MakeQAPanel(TEXT("qa_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeQAPanel(TEXT("qa_inner"), TEXT("InnerZone"), 32, 32,
					FString::Printf(
						TEXT("%s,%s,%s"),
						*MakeQAText(TEXT("qa_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
						*MakeQAText(TEXT("qa_i2"), TEXT("InnerB"), 40, 68, TEXT("B")),
						*MakeQAText(TEXT("qa_mover"), TEXT("Mover"), 40, 96, TEXT("Move Me")))),
				*MakeQAText(TEXT("qa_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad"))))));

	const FDesignReimportResult Result = QAReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);
	TestTrue(TEXT("move conflict raises explicit degradation"), Result.bHasDegradation);
	TestEqual(TEXT("move conflict is not plain success"), Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);

	UWidget* Mover = WBP->WidgetTree->FindWidget(TEXT("Mover"));
	TestNotNull(TEXT("mover still exists"), Mover);
	if (Mover)
	{
		TestEqual(TEXT("structure preserved: mover stays under the page host root"),
			Mover->GetParent()->GetFName(), WBP->WidgetTree->RootWidget->GetFName());
	}
	TestEqual(TEXT("move conflict diagnosed once"), QACountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("unclear-ownership reason reported on one message"),
		QAHasMessageWithAll(Result, TEXT("reimport_move_conflict"), { TEXT("Mover"), TEXT("not clearly D2U-owned") }));
	TestEqual(TEXT("no moves applied"), QACountKind(Result, TEXT("reimport_moved")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), QALoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mover stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("qa_mover")));
		TestTrue(TEXT("mover stays owned"), Sidecar.D2UOwned.Contains(TEXT("Mover")));
	}

	QAWipeOutput(OutputDir);
	return true;
}

#endif

