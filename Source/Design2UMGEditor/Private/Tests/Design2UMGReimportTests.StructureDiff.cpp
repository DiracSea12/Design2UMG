// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

// Safe reimport: top-level structure diff cases (added nodes, safe remove, remove/kind-change conflicts, move and move conflict).

#include "Misc/AutomationTest.h"

#include "Design2UMGGenerationManifest.h"
#include "DesignImportPipeline.h"
#include "Tests/ComponentPropertyFixtures.h"

#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"
#include "DesignWidgetBlueprintUtils.h"
#include "Design2UMGReimportTestFixtures.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;

// ---------------------------------------------------------------------------
// added - a new top-level purely generated node and a new mapped instance join the tree and
// are recorded in the new manifest (the mapped record carries key/class/inputs declarations; the inputs values are already written into the instance per the design).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportAddedNodesTest,
	"Design2UMG.SafeReimport.AddedNodesAndMappedInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportAddedNodesTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportAdded");

	const FString V1 = MakeFixtureDoc(MakeFixtureText(TEXT("fixture_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")));
	UWidgetBlueprint* WBP = FixtureImport(V1, FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();
	TestNull(TEXT("v1 has no ExtraPanel yet"), WBP->WidgetTree->FindWidget(TEXT("ExtraPanel")));
	TestNull(TEXT("v1 has no Sidekick yet"), WBP->WidgetTree->FindWidget(TEXT("Sidekick")));

	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeFixtureText(TEXT("fixture_t1"), TEXT("AlphaText"), 24, 24, TEXT("One")),
		*MakeFixturePanel(TEXT("fixture_extra"), TEXT("ExtraPanel"), 24, 90,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixtureText(TEXT("fixture_beta"), TEXT("Beta"), 32, 98, TEXT("Beta")),
				*MakeFixtureText(TEXT("fixture_beta2"), TEXT("BetaTwo"), 32, 126, TEXT("Beta 2")))),
		*MakeFixtureButton(TEXT("fixture_btn_2"), TEXT("Sidekick"), 250, 90, TEXT("Jump In"))));

	const FDesignReimportResult Result = FixtureReimport(V2, FixtureMappedClassPath, TargetPackage);
	TestTrue(TEXT("v2 reimport succeeds"), Result.bSucceeded);
	TestEqual(TEXT("two top-level adds diagnosed"), FixtureCountKind(Result, TEXT("reimport_added")), 2);
	TestTrue(TEXT("added panel diagnosed"), FixtureHasMessage(Result, TEXT("reimport_added"), TEXT("ExtraPanel")));
	TestTrue(TEXT("added mapped instance diagnosed"), FixtureHasMessage(Result, TEXT("reimport_added"), TEXT("Sidekick")));

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
	UUserWidget* Sidekick = ComponentFixtures::CastInput(WBP->WidgetTree->FindWidget(TEXT("Sidekick")));
	TestNotNull(TEXT("new mapped instance added"), Sidekick);
	if (Sidekick)
	{
		TestEqual(TEXT("new mapped instance input written"), ComponentFixtures::Read<FTextProperty>(Sidekick, TEXT("TextValue")).ToString(), FString(TEXT("Jump In")));
	}

	// The manifest records the new relations.
	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Extra =
			Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_extra"));
		if (TestNotNull(TEXT("new generated node recorded"), Extra))
		{
			TestEqual(TEXT("generated kind recorded"), Extra->Kind, FString(TEXT("generated")));
			TestEqual(TEXT("generated widget name recorded"), Extra->WidgetName, FString(TEXT("ExtraPanel")));
		}
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Mapped =
			Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_btn_2"));
		if (TestNotNull(TEXT("new mapped instance recorded"), Mapped))
		{
			TestEqual(TEXT("mapped kind recorded"), Mapped->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("mapped key recorded"), Mapped->MappedKey, FString(FixtureMappingKey));
			TestEqual(TEXT("mapped class recorded"), Mapped->MappedClass, FString(FixtureMappedClassPath));
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

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// safe remove, positive - a purely D2U-owned node is deleted safely;
// a node the design deleted but the project already removed from the asset is only dropped from the records (no error, no wrongful delete).
// ---------------------------------------------------------------------------
// Named-slot content removal uses the same subtree deletion path as reimport.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeleteNamedSlotContentSubtreeTest,
	"Design2UMG.SafeReimport.DeleteNamedSlotContentSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDeleteNamedSlotContentSubtreeTest::RunTest(const FString&)
{
	// Blueprint compilation collects garbage; prepare the class before unrooted test objects.
	UClass* const HostClass = ComponentFixtures::NamedSlotHostClass();
	UWidgetBlueprint* Blueprint = NewObject<UWidgetBlueprint>(GetTransientPackage());
	Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint);
	UUserWidget* Host = Blueprint->WidgetTree->ConstructWidget<UUserWidget>(
		HostClass, TEXT("SlotHost"));
	Blueprint->WidgetTree->RootWidget = Host;
	Host->WidgetTree = NewObject<UWidgetTree>(Host);
	UNamedSlot* Slot = Host->WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(), TEXT("Content"));
	Host->WidgetTree->RootWidget = Slot;
	UTextBlock* Content = Blueprint->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NamedSlotContent"));
	Host->SetContentForSlot(TEXT("Content"), Content);
	if (!TestTrue(TEXT("Fixture content is attached to named slot"), Host->GetContentForSlot(TEXT("Content")) == Content))
	{
		return false;
	}

	if (!TestTrue(TEXT("Fixture host has native transactional flag"), Host->HasAnyFlags(RF_Transactional)))
	{
		return false;
	}
	DesignWidgetBlueprintUtils::DeleteSubtreeSilently(Blueprint, Content);

	TestNull(TEXT("Named slot content is detached"), Host->GetContentForSlot(TEXT("Content")));
	TestTrue(TEXT("Named slot host participates in transaction"), Host->HasAnyFlags(RF_Transactional));
	TestNull(TEXT("Removed named-slot widget is absent from tree"), Blueprint->WidgetTree->FindWidget(TEXT("NamedSlotContent")));
	TestEqual(TEXT("Removed named-slot widget is rehomed"), Content->GetOuter(), static_cast<UObject*>(GetTransientPackage()));
	return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportRemovePureNodeTest,
	"Design2UMG.SafeReimport.RemoveDeletesPureD2UNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportRemovePureNodeTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportRemovePure");

	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeFixtureText(TEXT("fixture_clean"), TEXT("CleanStat"), 24, 24, TEXT("Old")),
		*MakeFixtureText(TEXT("fixture_ghost"), TEXT("GhostStat"), 140, 24, TEXT("Gone")),
		*MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// The project side already deleted GhostStat's widget in the editor (the manifest still records it).
	// Uses the same subtree deletion as reimport (moves objects out and cleans variable GUIDs); a bare RemoveWidget merely
	// unhooks it from the tree while the object stays alive - any later compile would trip the compiler's ensure on the detached widget/GUID.
	if (UWidget* Ghost = WBP->WidgetTree->FindWidget(TEXT("GhostStat")))
	{
		DesignWidgetBlueprintUtils::DeleteSubtreeSilently(WBP, Ghost);
		TestEqual(TEXT("already-removed widget no longer belongs to the asset"), Ghost->GetOuter(), static_cast<UObject*>(GetTransientPackage()));
	}

	TStrongObjectPtr<UWidget> CleanStat(WBP->WidgetTree->FindWidget(TEXT("CleanStat")));
	const FString V2 = MakeFixtureDoc(MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport succeeds"), Result.bSucceeded);

	TestNull(TEXT("purely owned node deleted from tree"), WBP->WidgetTree->FindWidget(TEXT("CleanStat")));
	if (TestNotNull(TEXT("owned node existed before removal"), CleanStat.Get()))
	{
		TestEqual(TEXT("removed owned widget no longer belongs to the asset"), CleanStat->GetOuter(), static_cast<UObject*>(GetTransientPackage()));
	}
	TestNull(TEXT("already-absent node stays absent"), WBP->WidgetTree->FindWidget(TEXT("GhostStat")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("two removal diagnostics"), FixtureCountKind(Result, TEXT("reimport_removed")), 2);
	TestTrue(TEXT("safe-delete flow diagnosed for present widget"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("CleanStat"), TEXT("removed through the safe delete flow") }));
	TestTrue(TEXT("already-absent branch diagnosed"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_removed"), { TEXT("GhostStat"), TEXT("widget already absent from the tree") }));
	TestEqual(TEXT("no remove conflicts"), FixtureCountKind(Result, TEXT("reimport_remove_conflict")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("deleted node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_clean")));
		TestNull(TEXT("absent node dropped from instances"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_ghost")));
		TestNotNull(TEXT("kept node still recorded"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_keep")));
		TestFalse(TEXT("deleted node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("CleanStat")));
		TestFalse(TEXT("absent node dropped from d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("GhostStat")));
		TestTrue(TEXT("kept node still owned"), Sidecar.D2UOwned.Contains(TEXT("Keeper")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// remove conflict - when the deleted node's subtree contains project content (project-added
// children / developer bindings), no delete: kept + a conflict diagnostic stating the reason, and the new manifest keeps the instance.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportRemoveProjectContentTest,
	"Design2UMG.SafeReimport.RemovePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportRemoveProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportRemoveConflict");

	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s,%s"),
		*MakeFixturePanel(TEXT("fixture_doom"), TEXT("DoomedPanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixtureText(TEXT("fixture_dhead"), TEXT("DoomHead"), 32, 32, TEXT("Doom")),
				*MakeFixtureText(TEXT("fixture_dtail"), TEXT("DoomTail"), 32, 60, TEXT("Doom")))),
		*MakeFixturePanel(TEXT("fixture_bound"), TEXT("BoundPanel"), 24, 160,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixtureText(TEXT("fixture_bhead"), TEXT("BoundHead"), 32, 168, TEXT("Bound")),
				*MakeFixtureText(TEXT("fixture_btail"), TEXT("BoundTail"), 32, 196, TEXT("Bound")))),
		*MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
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

	const FString V2 = MakeFixtureDoc(MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep")));
	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflicts are per-node)"), Result.bSucceeded);

	TestNotNull(TEXT("panel with project child preserved"), WBP->WidgetTree->FindWidget(TEXT("DoomedPanel")));
	TestNotNull(TEXT("project child preserved"), WBP->WidgetTree->FindWidget(TEXT("ProjectChild")));
	TestNotNull(TEXT("panel with developer binding preserved"), WBP->WidgetTree->FindWidget(TEXT("BoundPanel")));
	TestNotNull(TEXT("kept node still present"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));

	TestEqual(TEXT("no silent delete"), FixtureCountKind(Result, TEXT("reimport_removed")), 0);
	TestEqual(TEXT("two remove conflicts"), FixtureCountKind(Result, TEXT("reimport_remove_conflict")), 2);
	TestTrue(TEXT("unowned-widget reason names the project child"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("DoomedPanel"), TEXT("widgets outside the generation record"), TEXT("ProjectChild") }));
	TestTrue(TEXT("developer-binding reason names the node"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_remove_conflict"),
			{ TEXT("BoundPanel"), TEXT("developer bindings in the blueprint") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("conflicted panel stays recorded (unowned child)"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_doom")));
		TestNotNull(TEXT("conflicted panel stays recorded (binding)"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_bound")));
		TestTrue(TEXT("subtree widgets stay owned"), Sidecar.D2UOwned.Contains(TEXT("DoomHead")) && Sidecar.D2UOwned.Contains(TEXT("BoundTail")));
		TestFalse(TEXT("project child stays outside d2uOwned"), Sidecar.D2UOwned.Contains(TEXT("ProjectChild")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// kind-changed conflict - a mapped->generated conversion whose old subtree contains
// project content (developer bindings) -> kept as-is + conflict diagnostic, not replaced; the manifest record is unchanged.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportKindChangeProjectContentTest,
	"Design2UMG.SafeReimport.KindChangePreservesProjectContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportKindChangeProjectContentTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportKindConflict");

	const FString V1 = MakeFixtureDoc(MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	UWidgetBlueprint* WBP = FixtureImport(V1, FixtureMappedClassPath, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UUserWidget* MappedBefore = FixtureFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("mapped instance generated"), MappedBefore))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UUserWidget> MappedWeak = MappedBefore;

	// The developer binding hangs on the mapped instance (the subtree root): the old subtree is no longer purely D2U-owned.
	FDelegateEditorBinding ProjectBinding;
	ProjectBinding.ObjectName = TEXT("HeroButton");
	ProjectBinding.PropertyName = TEXT("OnClicked");
	ProjectBinding.FunctionName = TEXT("HandleHero");
	WBP->Bindings.Add(ProjectBinding);

	// The updated design keeps the same node but removes its mapping, changing the kind from mapped to generated.
	const FString V2 = MakeFixtureDoc(MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Checkout")));
	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
	TestTrue(TEXT("reimport itself succeeds (conflict is per-node)"), Result.bSucceeded);

	UUserWidget* MappedAfter = MappedWeak.Get();
	TestNotNull(TEXT("mapped instance kept"), MappedAfter);
	if (MappedAfter)
	{
		TestEqual(TEXT("mapped instance class unchanged"), MappedAfter->GetClass()->GetPathName(), FString(FixtureMappedClassPath));
	}
	TestEqual(TEXT("kind change conflict diagnosed once"), FixtureCountKind(Result, TEXT("reimport_kind_conflict")), 1);
	TestTrue(TEXT("conflict reports mapped -> generated and the reason on one message"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_kind_conflict"),
			{ TEXT("HeroButton"), TEXT("mapped -> generated"), TEXT("developer bindings in the blueprint") }));
	TestEqual(TEXT("no replacement performed"), FixtureCountKind(Result, TEXT("reimport_kind_changed")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		const FDesign2UMGGenerationManifestSidecar::FInstanceRecord* Record =
			Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_btn_1"));
		if (TestNotNull(TEXT("transitioned node stays recorded"), Record))
		{
			TestEqual(TEXT("manifest kind stays mapped"), Record->Kind, FString(TEXT("mapped")));
			TestEqual(TEXT("manifest mapped class stays"), Record->MappedClass, FString(FixtureMappedClassPath));
			TestEqual(TEXT("manifest widget name stays"), Record->WidgetName, FString(TEXT("HeroButton")));
		}
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// move, positive - the design moves a node into a newly added container; both ends clearly
// D2U-owned -> the widget changes parents but the object identity survives; manifest top-level instances consolidated, ownership kept.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportMoveWithinOwnedBoundsTest,
	"Design2UMG.SafeReimport.MoveReparentWithinOwnedBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportMoveWithinOwnedBoundsTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportMoveOk");

	// MoverPanel is a fill-less two-child pure container in both versions (type consistent across versions); in v1 it hangs under the host root.
	const FString MoverPanelJson = MakeFixturePanel(TEXT("fixture_mover"), TEXT("MoverPanel"), 40, 32,
		FString::Printf(
			TEXT("%s,%s"),
			*MakeFixtureText(TEXT("fixture_ma"), TEXT("MoverA"), 48, 40, TEXT("A")),
			*MakeFixtureText(TEXT("fixture_mb"), TEXT("MoverB"), 48, 68, TEXT("B"))));

	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MoverPanelJson,
		*MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	UWidget* MoverBefore = WBP->WidgetTree->FindWidget(TEXT("MoverPanel"));
	if (!TestNotNull(TEXT("MoverPanel present in v1"), MoverBefore))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	TWeakObjectPtr<UWidget> MoverWeak = MoverBefore;
	TestEqual(TEXT("v1 parent is the page host root"),
		MoverBefore->GetParent()->GetFName(), WBP->WidgetTree->RootWidget->GetFName());

	// v2: the new container TargetBox adopts MoverPanel (the whole subtree moves along).
	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_target"), TEXT("TargetBox"), 24, 120,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixtureText(TEXT("fixture_pad"), TEXT("PadText"), 32, 128, TEXT("Pad")),
				*MakeFixturePanel(TEXT("fixture_mover"), TEXT("MoverPanel"), 32, 160,
					FString::Printf(
						TEXT("%s,%s"),
						*MakeFixtureText(TEXT("fixture_ma"), TEXT("MoverA"), 40, 168, TEXT("A")),
						*MakeFixtureText(TEXT("fixture_mb"), TEXT("MoverB"), 40, 196, TEXT("B")))))),
		*MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 250, 24, TEXT("Keep"))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
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

	TestEqual(TEXT("one move diagnosed"), FixtureCountKind(Result, TEXT("reimport_moved")), 1);
	TestTrue(TEXT("move diagnostic reports identity preserved on one message"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_moved"), { TEXT("MoverPanel"), TEXT("widget identity preserved") }));

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNull(TEXT("moved node no longer a top-level instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_mover")));
		TestNotNull(TEXT("new container recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_target")));
		TestTrue(TEXT("moved node still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverPanel")));
		TestTrue(TEXT("moved subtree still D2U-owned"), Sidecar.D2UOwned.Contains(TEXT("MoverA")) && Sidecar.D2UOwned.Contains(TEXT("MoverB")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

// ---------------------------------------------------------------------------
// move conflict - the new design's parent end is neither a manifest instance nor a D2U node
// created by the current update (a nested container with unclear ownership) -> structure kept + a move-conflict diagnostic; no move.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSafeReimportMoveBoundaryConflictTest,
	"Design2UMG.SafeReimport.MoveConflictOnUnclearBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSafeReimportMoveBoundaryConflictTest::RunTest(const FString&)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/FixtureReimportMoveConflict");

	// v1: SidePanel > InnerZone (a nested container, not a manifest instance) + the top-level Mover.
	const FString InnerZoneChildren = FString::Printf(
		TEXT("%s,%s"),
		*MakeFixtureText(TEXT("fixture_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
		*MakeFixtureText(TEXT("fixture_i2"), TEXT("InnerB"), 40, 68, TEXT("B")));
	const FString V1 = MakeFixtureDoc(FString::Printf(
		TEXT("%s,%s"),
		*MakeFixturePanel(TEXT("fixture_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixturePanel(TEXT("fixture_inner"), TEXT("InnerZone"), 32, 32, InnerZoneChildren),
				*MakeFixtureText(TEXT("fixture_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad")))),
		*MakeFixtureText(TEXT("fixture_mover"), TEXT("Mover"), 250, 24, TEXT("Move Me"))));
	UWidgetBlueprint* WBP = FixtureImport(V1, nullptr, OutputDir);
	if (!TestTrue(TEXT("v1 import generates"), WBP != nullptr))
	{
		FixtureWipeOutput(OutputDir);
		return false;
	}
	const FString TargetPackage = WBP->GetOutermost()->GetName();

	// v2: the design moves Mover into InnerZone (a nested container, boundary unclear).
	const FString V2 = MakeFixtureDoc(FString::Printf(
		TEXT("%s"),
		*MakeFixturePanel(TEXT("fixture_side"), TEXT("SidePanel"), 24, 24,
			FString::Printf(
				TEXT("%s,%s"),
				*MakeFixturePanel(TEXT("fixture_inner"), TEXT("InnerZone"), 32, 32,
					FString::Printf(
						TEXT("%s,%s,%s"),
						*MakeFixtureText(TEXT("fixture_i1"), TEXT("InnerA"), 40, 40, TEXT("A")),
						*MakeFixtureText(TEXT("fixture_i2"), TEXT("InnerB"), 40, 68, TEXT("B")),
						*MakeFixtureText(TEXT("fixture_mover"), TEXT("Mover"), 40, 96, TEXT("Move Me")))),
				*MakeFixtureText(TEXT("fixture_spad"), TEXT("SidePad"), 140, 32, TEXT("Pad"))))));

	const FDesignReimportResult Result = FixtureReimport(V2, nullptr, TargetPackage);
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
	TestEqual(TEXT("move conflict diagnosed once"), FixtureCountKind(Result, TEXT("reimport_move_conflict")), 1);
	TestTrue(TEXT("unclear-ownership reason reported on one message"),
		FixtureHasMessageWithAll(Result, TEXT("reimport_move_conflict"), { TEXT("Mover"), TEXT("not clearly D2U-owned") }));
	TestEqual(TEXT("no moves applied"), FixtureCountKind(Result, TEXT("reimport_moved")), 0);

	FDesign2UMGGenerationManifestSidecar Sidecar;
	if (TestTrue(TEXT("manifest reloads"), FixtureLoadSidecar(TargetPackage, Sidecar)))
	{
		TestNotNull(TEXT("mover stays recorded as instance"), Sidecar.FindInstanceByDesignNodeId(TEXT("fixture_mover")));
		TestTrue(TEXT("mover stays owned"), Sidecar.D2UOwned.Contains(TEXT("Mover")));
	}

	FixtureWipeOutput(OutputDir);
	return true;
}

#endif
