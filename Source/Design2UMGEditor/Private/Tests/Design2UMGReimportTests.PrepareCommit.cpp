// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Tests/Design2UMGReimportTestFixtures.h"
#include "Tests/Design2UMGReimportSlotTestWidget.h"
#include "DesignImportReimportInternal.h"
#include "Design2UMGComponentBlueprints.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTreeBuilder.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportFixtures;
using namespace DesignImportPipeline;

namespace
{
	FUMGWidgetNode MakeNode(const TCHAR* Name, const TCHAR* Type = TEXT("TextBlock"))
	{
		FUMGWidgetNode Node;
		Node.Name = Node.Id = Name;
		Node.Type = Type;
		Node.Bounds.W = 100;
		Node.Bounds.H = 40;
		return Node;
	}

	FString TestPage()
	{
		return MakeFixtureDoc(MakeFixtureButton(TEXT("fixture_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Original"))
			+ TEXT(",") + MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep")));
	}

	struct FApplyFixture
	{
		FDesign2UMGGenerationSettingsSnapshot Settings;
		FDesign2UMGComponentMappingBuildState Mapping{Settings.ComponentMapping};
		FDesign2UMGGenerationManifestSidecar Manifest;
		TSet<FString> Owned;
		TSet<FString> Alive;
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*> Instances;
		TArray<FDesign2UMGMappingDiagnostic> Diagnostics;
		TArray<FDesign2UMGMappedInstanceRecord> Mapped;
		TArray<FDesign2UMGGeneratedInstanceRecord> Generated;
		TMap<FString, UWidget*> Created;
		FReimportApplySharedState Shared;

		explicit FApplyFixture(UWidgetBlueprint* WBP)
		{
			FixtureLoadSidecar(WBP->GetOutermost()->GetName(), Manifest);
			Owned.Append(Manifest.D2UOwned);
			for (const auto& Record : Manifest.Instances)
			{
				Instances.Add(Record.DesignNodeId, &Record);
				Alive.Add(Record.WidgetName);
			}
			Mapping.SetRootWidget(WBP->WidgetTree->RootWidget);
			Shared.WBP = WBP;
			Shared.WidgetTree = WBP->WidgetTree;
			Shared.HostPanel = CastChecked<UPanelWidget>(WBP->WidgetTree->RootWidget);
			Shared.DesignSize = FIntPoint(360, 240);
			Shared.GenerationSettings = &Settings;
			Shared.OwnedNames = &Owned;
			Shared.InstancesById = &Instances;
			Shared.MappingBuildState = &Mapping;
			Shared.Diagnostics = &Diagnostics;
			Shared.UpdatedMappedRecords = &Mapped;
			Shared.UpdatedGeneratedRecords = &Generated;
			Shared.AliveInstanceWidgetNames = &Alive;
		}

		bool HasDiagnostic(const TCHAR* Kind) const
		{
			return Diagnostics.ContainsByPredicate([Kind](const auto& Entry) { return Entry.Kind == Kind; });
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportIdentityIndexRulesTest,
	"Design2UMG.SafeReimport.PrepareCommit.IdentityIndexRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportIdentityIndexRulesTest::RunTest(const FString&)
{
	auto MakeOwnedNode = [](const TCHAR* Id)
	{
		FUMGWidgetNode Node = MakeNode(Id, TEXT("CanvasPanel"));
		Node.MutableSource().SourceNodeId = Id;
		return Node;
	};
	auto AddRecord = [](FDesign2UMGGenerationManifestSidecar& Manifest, const TCHAR* Id)
	{
		auto& Record = Manifest.Instances.AddDefaulted_GetRef();
		Record.DesignNodeId = Id;
		Record.WidgetName = Id;
		Record.Kind = Design2UMGManifestKinds::Generated;
	};
	FUMGWidgetNode PreparedScope;
	auto BuildPlan = [&PreparedScope](const FUMGWidgetNode& Scope, const FDesign2UMGGenerationManifestSidecar& Manifest,
		FReimportTreeIndex& Index, FString& FailureReason)
	{
		PreparedScope = Scope;
		FDesign2UMGGenerationSettingsSnapshot Settings;
		FDesign2UMGComponentMappingBuildState Mapping(Settings.ComponentMapping);
		TMap<FString, const FDesign2UMGGenerationManifestSidecar::FInstanceRecord*> Instances;
		TArray<FReimportPlanEntry> Plan;
		TSet<FString> Alive;
		return BuildReimportUpdatePlan(
			Manifest, PreparedScope, Index,
			Mapping, Instances, Plan, Alive, FailureReason);
	};

	FUMGWidgetNode Scope = MakeNode(TEXT("scope"), TEXT("CanvasPanel"));
	FUMGWidgetNode InstanceA = MakeOwnedNode(TEXT("instance_a"));
	FUMGWidgetNode InstanceB = MakeOwnedNode(TEXT("instance_b"));
	InstanceA.Children.Add(MakeOwnedNode(TEXT("definition_child")));
	InstanceB.Children.Add(MakeOwnedNode(TEXT("definition_child")));

	// Converter pass-through wrapper: deterministic wrapper id equals its only source-owned child's stable id.
	FUMGWidgetNode Wrapper = MakeNode(TEXT("wrapped"), TEXT("SizeBox"));
	Wrapper.Children.Add(MakeOwnedNode(TEXT("wrapped")));
	Scope.Children = { InstanceA, InstanceB, Wrapper };

	FDesign2UMGGenerationManifestSidecar Manifest;
	AddRecord(Manifest, TEXT("instance_a"));
	AddRecord(Manifest, TEXT("instance_b"));
	AddRecord(Manifest, TEXT("wrapped"));
	FReimportTreeIndex Index;
	FString FailureReason;
	TestTrue(TEXT("component-local repeated definition ids do not block reconciliation"),
		BuildPlan(Scope, Manifest, Index, FailureReason));
	TestTrue(TEXT("component-local duplicate is recorded as ambiguous"),
		Index.AmbiguousDesignNodeIds.Contains(TEXT("definition_child")));
	TestFalse(TEXT("direct pass-through wrapper alias is not ambiguous"),
		Index.AmbiguousDesignNodeIds.Contains(TEXT("wrapped")));
	TestTrue(TEXT("the wrapper remains the manifest lookup target"),
		Index.DesignNodesById.FindRef(TEXT("wrapped")) == &PreparedScope.Children[2]);
	TestTrue(TEXT("parent index references the prepared tree"),
		Index.ParentsByNode.FindRef(&PreparedScope.Children[2].Children[0]) == &PreparedScope.Children[2]);
	TestNull(TEXT("a repeated component-local id cannot silently select one instance"),
		Index.DesignNodesById.FindRef(TEXT("definition_child")));

	Scope.Children[2].MutableSource().SourceNodeId = TEXT("wrapped");
	TestFalse(TEXT("two real source nodes are not a wrapper alias merely because one is the only child"),
		BuildPlan(Scope, Manifest, Index, FailureReason));
	Scope.Children[2].Type = TEXT("NamedSlot");
	Scope.Children[2].Id = TEXT("exposed_slot");
	TestTrue(TEXT("generated component content slot aliases its child's source identity"),
		BuildPlan(Scope, Manifest, Index, FailureReason));
	Scope.Children[2].MutableSource().SourceNodeType = TEXT("FRAME");
	TestFalse(TEXT("a source-owned NamedSlot is not treated as generated exposure"),
		BuildPlan(Scope, Manifest, Index, FailureReason));
	Scope.Children[2] = Wrapper;
	Scope.Children[2].Children.Add(MakeOwnedNode(TEXT("wrapped")));
	TestFalse(TEXT("a synthetic parent with multiple matching children is ambiguous"),
		BuildPlan(Scope, Manifest, Index, FailureReason));

	FUMGWidgetNode AmbiguousScope = MakeNode(TEXT("scope"), TEXT("CanvasPanel"));
	AmbiguousScope.Children = { MakeOwnedNode(TEXT("duplicate")), MakeOwnedNode(TEXT("duplicate")) };
	FDesign2UMGGenerationManifestSidecar AmbiguousManifest;
	AddRecord(AmbiguousManifest, TEXT("duplicate"));
	FReimportTreeIndex AmbiguousIndex;
	FailureReason.Reset();
	TestFalse(TEXT("ambiguous reconciliation identity is rejected before apply"),
		BuildPlan(AmbiguousScope, AmbiguousManifest, AmbiguousIndex, FailureReason));
	TestTrue(TEXT("rejection explains the ambiguous stable identity"),
		FailureReason.Contains(TEXT("Ambiguous stable design identity 'duplicate'")));
	AmbiguousManifest.Instances.Reset();
	TestFalse(TEXT("two new top-level roots are rejected before they enter a manifest"),
		BuildPlan(AmbiguousScope, AmbiguousManifest, AmbiguousIndex, FailureReason));

	// Check ownership across a whole alias chain, not just its adjacent pairs. The source may be at any depth;
	// generated NamedSlots forward identity without introducing another source node.
	for (const int32 ChainLength : {2, 3, 4})
	{
		for (int32 SourceMask = 0; SourceMask < (1 << ChainLength); ++SourceMask)
		{
			for (const bool bUseNamedSlots : {false, true})
			{
				FUMGWidgetNode Chain;
				int32 SourceCount = 0;
				for (int32 Depth = ChainLength - 1; Depth >= 0; --Depth)
				{
					FUMGWidgetNode Layer = MakeNode(TEXT("chain"), bUseNamedSlots ? TEXT("NamedSlot") : TEXT("SizeBox"));
					if ((SourceMask & (1 << Depth)) != 0)
					{
						Layer.MutableSource().SourceNodeId = TEXT("chain");
						Layer.MutableSource().SourceNodeType = TEXT("FRAME");
						++SourceCount;
					}
					else if (bUseNamedSlots)
					{
						Layer.MutableSource().SourceNodeId = TEXT("chain");
					}
					if (Depth < ChainLength - 1) Layer.Children.Add(MoveTemp(Chain));
					Chain = MoveTemp(Layer);
				}
				Scope.Children = {MoveTemp(Chain)};
				FDesign2UMGGenerationManifestSidecar ChainManifest;
				AddRecord(ChainManifest, TEXT("chain"));
				const bool bAccepted = BuildPlan(Scope, ChainManifest, Index, FailureReason);
				TestEqual(FString::Printf(TEXT("alias chain length=%d source-mask=%d named-slots=%d"),
					ChainLength, SourceMask, bUseNamedSlots), bAccepted, SourceCount <= 1);
				if (bAccepted)
				{
					TestTrue(TEXT("accepted chain retains its outermost reconciliation target"),
						Index.DesignNodesById.FindRef(TEXT("chain")) == &PreparedScope.Children[0]);
				}
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportMappedIdentityDomainTest,
	"Design2UMG.SafeReimport.PrepareCommit.MappedIdentityDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportMappedIdentityDomainTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixtureMappedIdentityDomain");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	const FString Button = MakeFixtureButton(TEXT("mapped"), TEXT("MappedButton"), 180, 24, TEXT("Label"));
	UWidgetBlueprint* WBP = FixtureImport(MakeFixtureDoc(
		MakeFixtureText(TEXT("first"), TEXT("Alpha"), 24, 24, TEXT("Original")) + TEXT(",") + Button), FixtureMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	const FString Package = WBP->GetOutermost()->GetName();
	UTextBlock* Alpha = CastChecked<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Alpha")));
	UWidget* Mapped = WBP->WidgetTree->FindWidget(TEXT("MappedButton"));
	if (!TestNotNull(TEXT("mapped instance"), Mapped)) return false;

	// These implementation children are replaced by the project class. Their ids must not compete with an
	// active sibling, the recorded source root, or the mapped instance itself.
	for (const TCHAR* HiddenId : {TEXT("first"), TEXT("fixture_root"), TEXT("mapped")})
	{
		const FString Hidden = Button.Replace(TEXT("\"id\":\"mapped_label\""),
			*FString::Printf(TEXT("\"id\":\"%s\""), HiddenId));
		const FString Updated = MakeFixtureDoc(
			MakeFixtureText(TEXT("first"), TEXT("Alpha"), 24, 24, HiddenId) + TEXT(",") + Hidden);
		const FDesignReimportResult Result = FixtureReimport(Updated, FixtureMappedClassPath, Package);
		TestTrue(FString::Printf(TEXT("unconsumed implementation id '%s' does not block reimport"), HiddenId), Result.bSucceeded);
		TestTrue(TEXT("existing active widget identity preserved"), WBP->WidgetTree->FindWidget(TEXT("Alpha")) == Alpha);
		TestTrue(TEXT("existing mapped instance preserved"), WBP->WidgetTree->FindWidget(TEXT("MappedButton")) == Mapped);
		TestEqual(TEXT("active design value updated"), Alpha->GetText().ToString(), FString(HiddenId));
		FDesign2UMGGenerationManifestSidecar Manifest;
		TestTrue(TEXT("resulting manifest remains valid"), FixtureLoadSidecar(Package, Manifest));
	}

	// Without a successful mapping, the same descendants are live again and must participate in ambiguity checks.
	const FString Unmapped = Button.Replace(TEXT("\"id\":\"mapped_label\""), TEXT("\"id\":\"first\""));
	const FDesignReimportResult UnmappedResult = FixtureReimport(MakeFixtureDoc(
		MakeFixtureText(TEXT("first"), TEXT("Alpha"), 24, 24, TEXT("Rejected")) + TEXT(",") + Unmapped), nullptr, Package);
	TestFalse(TEXT("unmapped implementation is not excluded from identity validation"), UnmappedResult.bSucceeded);
	TestEqual(TEXT("rejected source did not update active text"), Alpha->GetText().ToString(), FString(TEXT("mapped")));

	const FString SecondButton = MakeFixtureButton(TEXT("second_mapped"), TEXT("SiblingButton"), 180, 100, TEXT("Sibling"));
	UWidgetBlueprint* Siblings = FixtureImport(MakeFixtureDoc(Button + TEXT(",") + SecondButton), FixtureMappedClassPath, Output / TEXT("Siblings"));
	if (!TestNotNull(TEXT("two mapped siblings"), Siblings)) return false;
	UWidget* Sibling = Siblings->WidgetTree->FindWidget(TEXT("SiblingButton"));
	if (!TestNotNull(TEXT("second mapped sibling"), Sibling)) return false;
	const FString HiddenSibling = Button.Replace(TEXT("\"id\":\"mapped_label\""), TEXT("\"id\":\"second_mapped\""));
	for (const bool bReverseOrder : {false, true})
	{
		const FString Children = bReverseOrder ? SecondButton + TEXT(",") + HiddenSibling : HiddenSibling + TEXT(",") + SecondButton;
		const FDesignReimportResult Result = FixtureReimport(MakeFixtureDoc(Children), FixtureMappedClassPath, Siblings->GetOutermost()->GetName());
		TestTrue(TEXT("hidden descendant cannot suppress another mapped sibling regardless of order"), Result.bSucceeded);
		TestTrue(TEXT("sibling keeps its mapped widget rather than becoming generated content"),
			Siblings->WidgetTree->FindWidget(TEXT("SiblingButton")) == Sibling);
	}

	FDesignDocument Document;
	if (!TestTrue(TEXT("parse a selected scope and an unrelated mapped root"), FFigmaParser::ParseDocument(MakeFixtureDoc(
		MakeFixturePanel(TEXT("selected"), TEXT("SelectedScope"), 0, 0,
			MakeFixtureText(TEXT("leaf"), TEXT("Leaf"), 0, 0, TEXT("Local"))) + TEXT(",") + Button), Document))) return false;
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const auto Settings = MakeFixtureSnapshot(FixtureMappedClassPath);
	FDesignReimportRequest Request;
	const FReimportSession Session{nullptr, Request, Data, Settings.GenerationSettings};
	FDesign2UMGGenerationManifestSidecar ScopeManifest;
	ScopeManifest.Provider = TEXT("figma");
	ScopeManifest.DocumentScope = FixtureReimportScope;
	ScopeManifest.SourceRootNodeId = TEXT("selected");
	FUMGWidgetNode PreparedRoot;
	FDesign2UMGComponentMappingBuildState Mapping(Settings.GenerationSettings.ComponentMapping);
	FDesignReimportResult ScopeResult;
	const FUMGWidgetNode* ScopeRoot = nullptr;
	bool bHostContinued = false;
	TestFalse(TEXT("selected subtree prepares successfully"), PrepareReimportScope(
		Session, ScopeManifest, ScopeResult, PreparedRoot, Mapping, ScopeRoot, bHostContinued).IsSet());
	TestTrue(TEXT("prepared tree owns only the selected scope"), ScopeRoot == &PreparedRoot
		&& GetDesignNodeId(PreparedRoot) == TEXT("selected"));
	TestFalse(TEXT("unrelated mapped root is not published as a local mapping hit"),
		Mapping.GetDiagnostics().ContainsByPredicate([](const auto& Diagnostic) { return Diagnostic.Kind == TEXT("mapping_hit"); }));
	TestFalse(TEXT("unrelated mapped identity does not leak into this scope"), Mapping.IsMappedNodeId(TEXT("mapped")));
	TestFalse(TEXT("unrelated implementation identity does not leak into this scope"), Mapping.IsInsideMappedSubtree(TEXT("mapped_label")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportManifestLiveNamesTest,
	"Design2UMG.SafeReimport.PrepareCommit.ManifestUsesFinalLiveNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportManifestLiveNamesTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixtureManifestLiveNames");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	UWidgetBlueprint* WBP = FixtureImport(MakeFixtureDoc(
		MakeFixtureText(TEXT("first"), TEXT("Alpha"), 24, 24, TEXT("Remove")) + TEXT(",")
		+ MakeFixtureText(TEXT("second"), TEXT("Beta"), 160, 24, TEXT("Keep"))), nullptr, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	FApplyFixture F(WBP);
	FReimportTreeIndex StaleIndex;
	RebuildReimportWidgetNameIndex(WBP->WidgetTree, StaleIndex);
	F.Shared.TreeIndex = &StaleIndex;
	UWidget* Removed = WBP->WidgetTree->FindWidget(TEXT("Alpha"));
	if (!TestNotNull(TEXT("widget to detach"), Removed)) return false;
	Removed->RemoveFromParent();
	F.Owned.Add(TEXT("MissingWidget"));
	UCanvasPanel* Added = WBP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LatePanel"));
	F.Shared.HostPanel->AddChild(Added);
	UTextBlock* Leaf = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LateLeaf"));
	Added->AddChild(Leaf);
	UTextBlock* Detached = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Detached"));
	F.Mapping.RecordCreatedWidget(MakeNode(TEXT("late_panel")), Added, F.Shared.HostPanel);
	F.Mapping.RecordCreatedWidget(MakeNode(TEXT("late_leaf")), Leaf, Added);
	F.Mapping.RecordCreatedWidget(MakeNode(TEXT("detached")), Detached, nullptr);
	const FUMGWidgetData Data;
	const FDesign2UMGGenerationOutput Payload = BuildReimportManifestPayload(F.Manifest, false, Data, F.Shared, F.Owned, {});
	const auto& Names = Payload.Manifest.D2UOwnedNames;
	TestTrue(TEXT("surviving owned widget retained"), Names.Contains(TEXT("Beta")));
	TestTrue(TEXT("post-snapshot parent and descendant included"), Names.Contains(TEXT("LatePanel")) && Names.Contains(TEXT("LateLeaf")));
	TestFalse(TEXT("detached original is not live"), Names.Contains(TEXT("Alpha")));
	TestFalse(TEXT("absent name is dropped"), Names.Contains(TEXT("MissingWidget")));
	TestFalse(TEXT("new detached object is not live"), Names.Contains(TEXT("Detached")));
	TestFalse(TEXT("host root does not become an owned instance"), Names.Contains(WBP->WidgetTree->RootWidget->GetName()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportKindPreparationFailureTest,
	"Design2UMG.SafeReimport.PrepareCommit.KindFailurePreservesSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportKindPreparationFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixturePrepareKindFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	UWidgetBlueprint* WBP = FixtureImport(TestPage(), FixtureMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	FApplyFixture F(WBP);
	UWidget* Old = FixtureFindMappedInstance(WBP);
	if (!TestNotNull(TEXT("old mapped instance"), Old)) return false;
	UPanelWidget* Parent = Old->GetParent();
	UPanelSlot* Slot = Old->Slot;
	const int32 Index = Parent->GetChildIndex(Old);
	const int32 ObjectCount = WBP->GetAllSourceWidgets().Num();

	// Root construction and a later descendant failure must both preserve the old object.
	for (bool bNestedFailure : {false, true})
	{
		FUMGWidgetNode Replacement = MakeNode(TEXT("HeroButton"), TEXT("InvalidWidgetType"));
		if (bNestedFailure)
		{
			Replacement.Type = TEXT("CanvasPanel");
			Replacement.Children = {MakeNode(TEXT("PreparedChild")), MakeNode(TEXT("BrokenChild"), TEXT("InvalidWidgetType"))};
		}
		FReimportPlanEntry Entry;
		Entry.ManifestRecord = F.Manifest.FindInstanceByDesignNodeId(TEXT("fixture_btn_1"));
		Entry.NewNode = &Replacement;
		Entry.Action = EReimportPlanAction::KindChanged;
		TSet<FString> UpdatedOwned = F.Owned;
		TArray<FString> Kept;
		TestFalse(TEXT("kind transition returns failure"), ApplyKindChangedPlanEntries({Entry}, F.Shared, UpdatedOwned, Kept, F.Created));
		TestTrue(TEXT("old identity and parent retained"), FixtureFindMappedInstance(WBP) == Old && Old->GetParent() == Parent);
		TestTrue(TEXT("original slot and order retained"), Old->Slot == Slot && Parent->GetChildAt(Index) == Old);
		TestEqual(TEXT("no partial construction left in source tree"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
		TestNull(TEXT("prepared descendant discarded"), FindObject<UWidget>(WBP->WidgetTree, TEXT("PreparedChild")));
		TestTrue(TEXT("ownership unchanged"), UpdatedOwned.Includes(F.Owned) && F.Owned.Includes(UpdatedOwned));
		TestTrue(TEXT("new records not published"), F.Created.IsEmpty() && F.Mapping.GetCreatedWidgetNames().IsEmpty());
		TestFalse(TEXT("no false kind-change success"), F.HasDiagnostic(ReimportDiagnosticKinds::KindChanged));
		TestTrue(TEXT("preparation failure diagnosed"), F.HasDiagnostic(ReimportDiagnosticKinds::PrepareFailed));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportAttachmentFailureTest,
	"Design2UMG.SafeReimport.PrepareCommit.AttachAndMoveFailurePreservePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportAttachmentFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixturePrepareAttachFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	UWidgetBlueprint* WBP = FixtureImport(TestPage(), FixtureMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	FApplyFixture F(WBP);
	USizeBox* Full = WBP->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FullTarget"));
	F.Shared.HostPanel->AddChild(Full);
	UTextBlock* Occupant = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Occupant"));
	Full->AddChild(Occupant);
	const int32 ObjectCount = WBP->GetAllSourceWidgets().Num();
	const FUMGWidgetNode NewNode = MakeNode(TEXT("RejectedChild"));
	TestNull(TEXT("addition returns attachment failure"), CreateReimportWidget(NewNode, Full, F.Shared));
	TestTrue(TEXT("original target content retained"), Full->GetChildAt(0) == Occupant);
	TestEqual(TEXT("rejected object cleaned up"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
	TestTrue(TEXT("failed creation did not publish records"), F.Mapping.GetCreatedWidgetNames().IsEmpty());

	UWidget* Mover = WBP->WidgetTree->FindWidget(TEXT("Keeper"));
	UPanelWidget* OldParent = Mover->GetParent();
	UPanelSlot* OldSlot = Mover->Slot;
	const int32 OldIndex = OldParent->GetChildIndex(Mover);
	TestFalse(TEXT("move reports attach failure"), MoveReimportWidget(Mover, Full, MakeNode(TEXT("Keeper")), F.Shared));
	TestTrue(TEXT("move leaves original parent, slot and sibling index"),
		Mover->GetParent() == OldParent && Mover->Slot == OldSlot && OldParent->GetChildAt(OldIndex) == Mover);
	TestTrue(TEXT("attachment failure diagnosed"), F.HasDiagnostic(ReimportDiagnosticKinds::AttachFailed));
	TestFalse(TEXT("failed move has no success diagnostic"), F.HasDiagnostic(ReimportDiagnosticKinds::Moved));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportReplacementSlotTest,
	"Design2UMG.SafeReimport.PrepareCommit.ReplacementKeepsSingleContentSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportReplacementSlotTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixturePrepareReplaceSlot");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	UWidgetBlueprint* WBP = FixtureImport(TestPage(), FixtureMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	FApplyFixture F(WBP);
	USizeBox* Parent = WBP->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SingleParent"));
	F.Shared.HostPanel->AddChild(Parent);
	UWidget* Old = WBP->WidgetTree->FindWidget(TEXT("Keeper"));
	Parent->AddChild(Old);
	UPanelSlot* OriginalSlot = Old->Slot;
	UWidget* New = ReplaceReimportWidget(MakeNode(TEXT("NewContent")), Old, F.Shared, F.Owned);
	if (!TestNotNull(TEXT("replacement committed"), New)) return false;
	TestTrue(TEXT("original slot now carries replacement"), New->Slot == OriginalSlot && Parent->GetChildAt(0) == New);
	TestEqual(TEXT("single-content parent still contains exactly one child"), Parent->GetChildrenCount(), 1);
	TestNull(TEXT("old subtree removed only after commit"), WBP->WidgetTree->FindWidget(TEXT("Keeper")));
	TestTrue(TEXT("replacement ownership published"), F.Mapping.GetCreatedWidgetNames().Contains(New->GetName()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportAutomaticOverrideFailureTest,
	"Design2UMG.SafeReimport.PrepareCommit.AutomaticOverrideFailurePreservesAllSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportAutomaticOverrideFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixturePrepareAutomaticFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	UWidgetBlueprint* WBP = FixtureImport(TestPage(), FixtureMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	FApplyFixture F(WBP);
	// Match the production template path: ConstructWidget<UUserWidget> would initialize
	// an empty runtime tree and reject all named slots before the test even begins.
	UDesign2UMGReimportSlotTestWidget* Instance = CastChecked<UDesign2UMGReimportSlotTestWidget>(
		WBP->WidgetTree->ConstructWidget<UWidget>(UDesign2UMGReimportSlotTestWidget::StaticClass(), TEXT("Automatic")));
	F.Shared.HostPanel->AddChild(Instance);
	UTextBlock* First = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OldFirst"));
	UTextBlock* Second = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OldSecond"));
	Instance->SetContentForSlot(TEXT("D2U_Content_0_0"), First);
	Instance->SetContentForSlot(TEXT("D2U_Content_0_1"), Second);
	if (!TestTrue(TEXT("fixture holds both original template slot contents"),
		Instance->GetContentForSlot(TEXT("D2U_Content_0_0")) == First
		&& Instance->GetContentForSlot(TEXT("D2U_Content_0_1")) == Second)) return false;
	F.Owned.Add(First->GetName());
	F.Owned.Add(Second->GetName());
	F.Owned.Add(Instance->GetName());
	F.Settings.ComponentBlueprints = MakeShared<FDesign2UMGComponentBlueprintContext>();
	auto& Component = F.Settings.ComponentBlueprints->Components.Add(TEXT("auto-key"));
	Component.ClassPath = Instance->GetClass()->GetPathName();
	Component.Data.RootWidget = MakeNode(TEXT("DefaultRoot"), TEXT("CanvasPanel"));
	Component.Data.RootWidget.Children = {MakeNode(TEXT("First")), MakeNode(TEXT("Second"))};
	FUMGWidgetNode Node = Component.Data.RootWidget;
	Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
	Node.MutableSource().MainComponentKey = TEXT("auto-key");
	Node.Children[0].Type = TEXT("Border");
	Node.Children[1].Type = TEXT("InvalidWidgetType");
	const int32 ObjectCount = WBP->GetAllSourceWidgets().Num();
	UPanelSlot* OriginalSlot = Instance->Slot;
	TArray<UWidget*> MoveCandidates;

	TestFalse(TEXT("late override construction failure propagates through refresh"),
		RefreshNodeByKind(Node, Instance, F.Shared, MoveCandidates, F.Created));
	TestTrue(TEXT("both original overrides retained"), Instance->GetContentForSlot(TEXT("D2U_Content_0_0")) == First
		&& Instance->GetContentForSlot(TEXT("D2U_Content_0_1")) == Second);
	TestEqual(TEXT("partly prepared overrides discarded"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
	TestTrue(TEXT("outer slot retained before successful preparation"), Instance->Slot == OriginalSlot);
	TestTrue(TEXT("failed refresh emits no record"), F.Generated.IsEmpty() && F.Mapping.GetCreatedWidgetNames().IsEmpty());
	TestFalse(TEXT("no false refresh success"), F.HasDiagnostic(ReimportDiagnosticKinds::Updated));

	Node.Children[1].Type = TEXT("Border");
	UWidgetTree* PreviewTree = NewObject<UWidgetTree>(Instance);
	Instance->WidgetTree = PreviewTree;
	Instance->RejectedSlot = TEXT("D2U_Content_0_1");
	TestNull(TEXT("second slot rejects commit"), RefreshAutomaticInstance(Node, Instance, F.Shared));
	TestTrue(TEXT("earlier slot is restored after late rejection"), Instance->GetContentForSlot(TEXT("D2U_Content_0_0")) == First
		&& Instance->GetContentForSlot(TEXT("D2U_Content_0_1")) == Second);
	TestEqual(TEXT("rejected prepared overrides discarded"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
	TestTrue(TEXT("failed commit does not publish ownership"), F.Mapping.GetCreatedWidgetNames().IsEmpty());
	TestTrue(TEXT("slot rejection diagnosed as attachment failure"), F.HasDiagnostic(ReimportDiagnosticKinds::AttachFailed));

	Instance->RejectedSlot = NAME_None;
	Component.ClassPath = TEXT("/Script/Design2UMGEditor.MissingAutomaticClass");
	AddExpectedError(TEXT("has no generated class"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("class-swap construction failure retains instance"), RefreshAutomaticInstance(Node, Instance, F.Shared));
	TestTrue(TEXT("failed class swap retains both overrides and outer slot"), Instance->Slot == OriginalSlot
		&& Instance->GetContentForSlot(TEXT("D2U_Content_0_0")) == First
		&& Instance->GetContentForSlot(TEXT("D2U_Content_0_1")) == Second);
	Component.ClassPath = Instance->GetClass()->GetPathName();
	TestTrue(TEXT("successful override refresh preserves instance identity"), RefreshAutomaticInstance(Node, Instance, F.Shared) == Instance);
	TestTrue(TEXT("refresh retains the transient preview tree"), Instance->WidgetTree == PreviewTree);
	TestTrue(TEXT("successful override refresh replaces both contents"), Instance->GetContentForSlot(TEXT("D2U_Content_0_0")) != First
		&& Instance->GetContentForSlot(TEXT("D2U_Content_0_1")) != Second);
	TestEqual(TEXT("old overrides removed without leaving ghost source objects"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportApplyFailureResultTest,
	"Design2UMG.SafeReimport.PrepareCommit.ApplyFailureDoesNotSaveOrUndoUserEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportApplyFailureResultTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixturePrepareApplyFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	UWidgetBlueprint* WBP = FixtureImport(TestPage(), FixtureMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	const FString Package = WBP->GetOutermost()->GetName();
	FDesign2UMGGenerationManifestSidecar Manifest;
	if (!TestTrue(TEXT("manifest loaded"), FixtureLoadSidecar(Package, Manifest))) return false;
	FString ManifestBefore;
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);
	FixtureReadFileString(SidecarPath, ManifestBefore);
	TArray<uint8> AssetBytesBefore;
	FFileHelper::LoadFileToArray(AssetBytesBefore, *FixtureAssetFilePath(Package));
	UWidget* Old = FixtureFindMappedInstance(WBP);
	UTextBlock* Keeper = CastChecked<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Keeper")));
	Keeper->SetFlags(RF_Transactional);
	GEditor->BeginTransaction(TEXT("ReimportTest"), FText::FromString(TEXT("Previous user edit")), Keeper);
	Keeper->Modify();
	Keeper->SetText(FText::FromString(TEXT("User edit")));
	GEditor->EndTransaction();

	FDesignDocument Document;
	FFigmaParser::ParseDocument(TestPage(), Document);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TFunction<void(FUMGWidgetNode&)> BreakReplacement = [&](FUMGWidgetNode& Node)
	{
		if (GetDesignNodeId(Node) == TEXT("fixture_btn_1")) Node.Type = TEXT("InvalidWidgetType");
		for (FUMGWidgetNode& Child : Node.Children) BreakReplacement(Child);
	};
	BreakReplacement(Data.RootWidget);
	FDesignReimportRequest Request;
	Request.TargetAssetPath = Package;
	const auto Snapshot = MakeFixtureSnapshot(nullptr);
	const FReimportSession Session{&Document, Request, Data, Snapshot.GenerationSettings};
	FDesign2UMGGenerationOutput GenerationOutput;
	FUMGPrototypeNavigationData Navigation;
	int32 Transaction = INDEX_NONE;
	const FDesignReimportResult Result = ApplyPreparedReimport(Session, WBP, Manifest, GenerationOutput, Navigation, Transaction);
	TestFalse(TEXT("final result reports failure"), Result.bSucceeded);
	TestNull(TEXT("no updated asset reported"), Result.UpdatedBlueprint);
	TestEqual(TEXT("owned transaction closed"), Transaction, INDEX_NONE);
	TestFalse(TEXT("no active editor transaction leaked"), GEditor->IsTransactionActive());
	TestTrue(TEXT("failed replacement retained old object"), FixtureFindMappedInstance(WBP) == Old);
	TestEqual(TEXT("previous user transaction was not undone"), Keeper->GetText().ToString(), FString(TEXT("User edit")));
	TestEqual(TEXT("no kind-change success"), FixtureCountKind(Result, ReimportDiagnosticKinds::KindChanged), 0);
	TestEqual(TEXT("no completed success"), FixtureCountKind(Result, ReimportDiagnosticKinds::Completed), 0);
	TestTrue(TEXT("failure reaches result diagnostics"), FixtureCountKind(Result, ReimportDiagnosticKinds::ApplyFailed) > 0);
	TestTrue(TEXT("no manifest payload assembled"), GenerationOutput.Manifest.MappedInstances.IsEmpty()
		&& GenerationOutput.Manifest.TopLevelGenerated.IsEmpty() && GenerationOutput.Manifest.D2UOwnedNames.IsEmpty());
	FString ManifestAfter;
	FixtureReadFileString(SidecarPath, ManifestAfter);
	TArray<uint8> AssetBytesAfter;
	FFileHelper::LoadFileToArray(AssetBytesAfter, *FixtureAssetFilePath(Package));
	TestEqual(TEXT("manifest bytes unchanged"), ManifestAfter, ManifestBefore);
	TestTrue(TEXT("asset bytes unchanged"), AssetBytesBefore == AssetBytesAfter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportLateApplyFailureTest,
	"Design2UMG.SafeReimport.PrepareCommit.LateFailureRollsBackAddedAndRemovedNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportLateApplyFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixturePrepareLateFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	const FString Json = MakeFixtureDoc(MakeFixturePanel(TEXT("fixture_panel"), TEXT("ExistingPanel"), 20, 20,
		MakeFixtureText(TEXT("fixture_a"), TEXT("Alpha"), 24, 24, TEXT("A")) + TEXT(",")
		+ MakeFixtureText(TEXT("fixture_b"), TEXT("Beta"), 24, 60, TEXT("B"))) + TEXT(",")
		+ MakeFixtureText(TEXT("fixture_keep"), TEXT("Keeper"), 240, 24, TEXT("Keep")));
	UWidgetBlueprint* WBP = FixtureImport(Json, nullptr, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	const FString Package = WBP->GetOutermost()->GetName();
	FDesign2UMGGenerationManifestSidecar Manifest;
	if (!TestTrue(TEXT("manifest loaded"), FixtureLoadSidecar(Package, Manifest))) return false;
	UWidget* OriginalKeeper = WBP->WidgetTree->FindWidget(TEXT("Keeper"));
	UWidget* OriginalPanel = WBP->WidgetTree->FindWidget(TEXT("ExistingPanel"));
	const int32 ObjectCount = WBP->GetAllSourceWidgets().Num();
	FDesignDocument Document;
	FFigmaParser::ParseDocument(Json, Document);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TFunction<void(FUMGWidgetNode&)> Change = [&](FUMGWidgetNode& Node)
	{
		Node.Children.RemoveAll([](const FUMGWidgetNode& Child) { return GetDesignNodeId(Child) == TEXT("fixture_keep"); });
		if (GetDesignNodeId(Node) == TEXT("fixture_panel")) Node.Children.Add(MakeNode(TEXT("BrokenAddition"), TEXT("InvalidWidgetType")));
		for (FUMGWidgetNode& Child : Node.Children) Change(Child);
	};
	Change(Data.RootWidget);
	FUMGWidgetNode Added = MakeNode(TEXT("NewTop"), TEXT("CanvasPanel"));
	Added.Children.Add(MakeNode(TEXT("NewLeaf")));
	Data.RootWidget.Children.Add(Added);
	FDesignReimportRequest Request;
	Request.TargetAssetPath = Package;
	const auto Snapshot = MakeFixtureSnapshot(nullptr);
	const FReimportSession Session{&Document, Request, Data, Snapshot.GenerationSettings};
	FDesign2UMGGenerationOutput GenerationOutput;
	FUMGPrototypeNavigationData Navigation;
	int32 Transaction = INDEX_NONE;
	const FDesignReimportResult Result = ApplyPreparedReimport(Session, WBP, Manifest, GenerationOutput, Navigation, Transaction);
	TestFalse(TEXT("late structural failure reaches final result"), Result.bSucceeded);
	TestTrue(TEXT("owned transaction actually rolled back"), FixtureHasMessage(Result, ReimportDiagnosticKinds::ApplyFailed, TEXT("transaction rolled back")));
	TestTrue(TEXT("previously deleted widget restored with its identity"), WBP->WidgetTree->FindWidget(TEXT("Keeper")) == OriginalKeeper);
	TestTrue(TEXT("surviving panel identity preserved"), WBP->WidgetTree->FindWidget(TEXT("ExistingPanel")) == OriginalPanel);
	TestNull(TEXT("committed addition undone"), WBP->WidgetTree->FindWidget(TEXT("NewTop")));
	TestNull(TEXT("undone addition has no detached ghost"), FindObject<UWidget>(WBP->WidgetTree, TEXT("NewTop")));
	TestNull(TEXT("undone nested addition has no detached ghost"), FindObject<UWidget>(WBP->WidgetTree, TEXT("NewLeaf")));
	TestEqual(TEXT("source objects restored to original count"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
	TestEqual(TEXT("rolled-back addition not reported as success"), FixtureCountKind(Result, ReimportDiagnosticKinds::Added), 0);
	TestEqual(TEXT("rolled-back removal not reported as success"), FixtureCountKind(Result, ReimportDiagnosticKinds::Removed), 0);
	TestTrue(TEXT("no ghost manifest payload"), GenerationOutput.Manifest.D2UOwnedNames.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportIdentityFailurePreservesAssetTest,
	"Design2UMG.SafeReimport.PrepareCommit.IdentityFailurePreservesAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportIdentityFailurePreservesAssetTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/FixtureIdentityFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Output); };
	const FString Initial = MakeFixtureDoc(MakeFixtureText(TEXT("first"), TEXT("Alpha"), 24, 24, TEXT("Original"))
		+ TEXT(",") + MakeFixtureText(TEXT("second"), TEXT("Beta"), 160, 24, TEXT("Keep")));
	UWidgetBlueprint* WBP = FixtureImport(Initial, nullptr, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	const FString Package = WBP->GetOutermost()->GetName();
	const FString Sidecar = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);
	TArray<uint8> AssetBefore, ManifestBefore;
	if (!TestTrue(TEXT("asset bytes readable"), FFileHelper::LoadFileToArray(AssetBefore, *FixtureAssetFilePath(Package)))
		|| !TestTrue(TEXT("manifest bytes readable"), FFileHelper::LoadFileToArray(ManifestBefore, *Sidecar))) return false;
	UWidget* Root = WBP->WidgetTree->RootWidget;
	UTextBlock* Alpha = CastChecked<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Alpha")));
	const int32 WidgetCount = WBP->GetAllSourceWidgets().Num();

	const FString BlurredParent = MakeFixturePanel(TEXT("first"), TEXT("BlurredParent"), 0, 0,
		MakeFixtureText(TEXT("first"), TEXT("DuplicateChild"), 24, 24, TEXT("Duplicate")))
		.Replace(TEXT("\"children\":"), TEXT("\"effects\":[{\"type\":\"BACKGROUND_BLUR\",\"visible\":true,\"radius\":4}],\"children\":"));
	const TArray<FString> AmbiguousDocuments = {
		// Existing global reconciliation identity claimed by a second source branch.
		MakeFixtureDoc(MakeFixtureText(TEXT("first"), TEXT("Alpha"), 24, 24, TEXT("Changed"))
			+ TEXT(",") + MakeFixtureText(TEXT("first"), TEXT("Other"), 160, 24, TEXT("Duplicate"))),
		// New mapped children acquire manifest entries even when nested inside an existing generated panel.
		MakeFixtureDoc(MakeFixtureText(TEXT("first"), TEXT("Alpha"), 24, 24, TEXT("Changed"))
			+ TEXT(",") + MakeFixturePanel(TEXT("new_panel"), TEXT("NewPanel"), 0, 0,
				MakeFixtureButton(TEXT("new_mapped"), TEXT("OneButton"), 0, 0, TEXT("One")) + TEXT(",")
				+ MakeFixtureButton(TEXT("new_mapped"), TEXT("OtherButton"), 120, 0, TEXT("Two")))),
		// Real converter output: background blur separates the two same-id source nodes with a generated carrier.
		MakeFixtureDoc(BlurredParent + TEXT(",") + MakeFixtureText(TEXT("second"), TEXT("Beta"), 160, 24, TEXT("Keep")))
	};
	for (const FString& Json : AmbiguousDocuments)
	{
		const FDesignReimportResult Result = FixtureReimport(Json, FixtureMappedClassPath, Package);
		TestFalse(TEXT("ambiguous reimport does not succeed"), Result.bSucceeded);
		TestTrue(TEXT("failure explains identity ambiguity"),
			FixtureHasMessage(Result, ReimportDiagnosticKinds::PrepareFailed, TEXT("Ambiguous stable design identity")));
		TestEqual(TEXT("no transaction opened for ambiguous input"), Result.Commit.Memory, EDesign2UMGMemoryState::Unchanged);
		TestTrue(TEXT("original root and widget survive"), WBP->WidgetTree->RootWidget == Root
			&& WBP->WidgetTree->FindWidget(TEXT("Alpha")) == Alpha);
		TestEqual(TEXT("original value untouched"), Alpha->GetText().ToString(), FString(TEXT("Original")));
		TestEqual(TEXT("no detached or newly attached widgets"), WBP->GetAllSourceWidgets().Num(), WidgetCount);
		TArray<uint8> AssetAfter, ManifestAfter;
		TestTrue(TEXT("asset remains readable"), FFileHelper::LoadFileToArray(AssetAfter, *FixtureAssetFilePath(Package)));
		TestTrue(TEXT("manifest remains readable"), FFileHelper::LoadFileToArray(ManifestAfter, *Sidecar));
		TestTrue(TEXT("asset and manifest bytes unchanged"), AssetBefore == AssetAfter && ManifestBefore == ManifestAfter);
	}
	return true;
}

#endif
