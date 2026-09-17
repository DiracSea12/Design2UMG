// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Tests/Design2UMGReimportQATestFixtures.h"
#include "Tests/Design2UMGReimportSlotTestWidget.h"
#include "DesignImportReimportInternal.h"
#include "Design2UMGComponentBlueprints.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetUtils.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_AUTOMATION_WORKER

using namespace Design2UMGReimportQA;
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
		return MakeQADoc(MakeQAButton(TEXT("qa_btn_1"), TEXT("HeroButton"), 24, 24, TEXT("Original"))
			+ TEXT(",") + MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 200, 24, TEXT("Keep")));
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
			QALoadSidecar(WBP->GetOutermost()->GetName(), Manifest);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportKindPreparationFailureTest,
	"Design2UMG.SafeReimportQA.PrepareCommit.KindFailurePreservesSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportKindPreparationFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/QAPrepareKindFailure");
	ON_SCOPE_EXIT { QAWipeOutput(Output); };
	UWidgetBlueprint* WBP = QAImport(TestPage(), QAMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	FApplyFixture F(WBP);
	UWidget* Old = QAFindMappedInstance(WBP);
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
		Entry.ManifestRecord = F.Manifest.FindInstanceByDesignNodeId(TEXT("qa_btn_1"));
		Entry.NewNode = &Replacement;
		Entry.Action = EReimportPlanAction::KindChanged;
		TSet<FString> UpdatedOwned = F.Owned;
		TArray<FString> Kept;
		TestFalse(TEXT("kind transition returns failure"), ApplyKindChangedPlanEntries({Entry}, F.Shared, UpdatedOwned, Kept, F.Created));
		TestTrue(TEXT("old identity and parent retained"), QAFindMappedInstance(WBP) == Old && Old->GetParent() == Parent);
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
	"Design2UMG.SafeReimportQA.PrepareCommit.AttachAndMoveFailurePreservePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportAttachmentFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/QAPrepareAttachFailure");
	ON_SCOPE_EXIT { QAWipeOutput(Output); };
	UWidgetBlueprint* WBP = QAImport(TestPage(), QAMappedClassPath, Output);
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
	"Design2UMG.SafeReimportQA.PrepareCommit.ReplacementKeepsSingleContentSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportReplacementSlotTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/QAPrepareReplaceSlot");
	ON_SCOPE_EXIT { QAWipeOutput(Output); };
	UWidgetBlueprint* WBP = QAImport(TestPage(), QAMappedClassPath, Output);
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
	"Design2UMG.SafeReimportQA.PrepareCommit.AutomaticOverrideFailurePreservesAllSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportAutomaticOverrideFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/QAPrepareAutomaticFailure");
	ON_SCOPE_EXIT { QAWipeOutput(Output); };
	UWidgetBlueprint* WBP = QAImport(TestPage(), QAMappedClassPath, Output);
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
	TestTrue(TEXT("successful override refresh replaces both contents"), Instance->GetContentForSlot(TEXT("D2U_Content_0_0")) != First
		&& Instance->GetContentForSlot(TEXT("D2U_Content_0_1")) != Second);
	TestEqual(TEXT("old overrides removed without leaving ghost source objects"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportApplyFailureResultTest,
	"Design2UMG.SafeReimportQA.PrepareCommit.ApplyFailureDoesNotSaveOrUndoUserEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportApplyFailureResultTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/QAPrepareApplyFailure");
	ON_SCOPE_EXIT { QAWipeOutput(Output); };
	UWidgetBlueprint* WBP = QAImport(TestPage(), QAMappedClassPath, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	const FString Package = WBP->GetOutermost()->GetName();
	FDesign2UMGGenerationManifestSidecar Manifest;
	if (!TestTrue(TEXT("manifest loaded"), QALoadSidecar(Package, Manifest))) return false;
	FString ManifestBefore;
	const FString SidecarPath = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);
	QAReadFileString(SidecarPath, ManifestBefore);
	TArray<uint8> AssetBytesBefore;
	FFileHelper::LoadFileToArray(AssetBytesBefore, *QAAssetFilePath(Package));
	UWidget* Old = QAFindMappedInstance(WBP);
	UTextBlock* Keeper = CastChecked<UTextBlock>(WBP->WidgetTree->FindWidget(TEXT("Keeper")));
	Keeper->SetFlags(RF_Transactional);
	GEditor->BeginTransaction(TEXT("QA"), FText::FromString(TEXT("Previous user edit")), Keeper);
	Keeper->Modify();
	Keeper->SetText(FText::FromString(TEXT("User edit")));
	GEditor->EndTransaction();

	FDesignDocument Document;
	FFigmaParser::ParseDocument(TestPage(), Document);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TFunction<void(FUMGWidgetNode&)> BreakReplacement = [&](FUMGWidgetNode& Node)
	{
		if (GetDesignNodeId(Node) == TEXT("qa_btn_1")) Node.Type = TEXT("InvalidWidgetType");
		for (FUMGWidgetNode& Child : Node.Children) BreakReplacement(Child);
	};
	BreakReplacement(Data.RootWidget);
	FDesignReimportRequest Request;
	Request.TargetAssetPath = Package;
	const auto Snapshot = MakeQASnapshot(nullptr);
	const FReimportSession Session{&Document, Request, Data, Snapshot.GenerationSettings};
	FDesign2UMGGenerationOutput GenerationOutput;
	FUMGPrototypeNavigationData Navigation;
	int32 Transaction = INDEX_NONE;
	const FDesignReimportResult Result = ApplyPreparedReimport(Session, WBP, Manifest, GenerationOutput, Navigation, Transaction);
	TestFalse(TEXT("final result reports failure"), Result.bSucceeded);
	TestNull(TEXT("no updated asset reported"), Result.UpdatedBlueprint);
	TestEqual(TEXT("owned transaction closed"), Transaction, INDEX_NONE);
	TestFalse(TEXT("no active editor transaction leaked"), GEditor->IsTransactionActive());
	TestTrue(TEXT("failed replacement retained old object"), QAFindMappedInstance(WBP) == Old);
	TestEqual(TEXT("previous user transaction was not undone"), Keeper->GetText().ToString(), FString(TEXT("User edit")));
	TestEqual(TEXT("no kind-change success"), QACountKind(Result, ReimportDiagnosticKinds::KindChanged), 0);
	TestEqual(TEXT("no completed success"), QACountKind(Result, ReimportDiagnosticKinds::Completed), 0);
	TestTrue(TEXT("failure reaches result diagnostics"), QACountKind(Result, ReimportDiagnosticKinds::ApplyFailed) > 0);
	TestTrue(TEXT("no manifest payload assembled"), GenerationOutput.Manifest.MappedInstances.IsEmpty()
		&& GenerationOutput.Manifest.TopLevelGenerated.IsEmpty() && GenerationOutput.Manifest.D2UOwnedNames.IsEmpty());
	FString ManifestAfter;
	QAReadFileString(SidecarPath, ManifestAfter);
	TArray<uint8> AssetBytesAfter;
	FFileHelper::LoadFileToArray(AssetBytesAfter, *QAAssetFilePath(Package));
	TestEqual(TEXT("manifest bytes unchanged"), ManifestAfter, ManifestBefore);
	TestTrue(TEXT("asset bytes unchanged"), AssetBytesBefore == AssetBytesAfter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimportLateApplyFailureTest,
	"Design2UMG.SafeReimportQA.PrepareCommit.LateFailureRollsBackAddedAndRemovedNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FReimportLateApplyFailureTest::RunTest(const FString&)
{
	const FString Output = TEXT("/Game/Design2UMG/_TestOutput/QAPrepareLateFailure");
	ON_SCOPE_EXIT { QAWipeOutput(Output); };
	const FString Json = MakeQADoc(MakeQAPanel(TEXT("qa_panel"), TEXT("ExistingPanel"), 20, 20,
		MakeQAText(TEXT("qa_a"), TEXT("Alpha"), 24, 24, TEXT("A")) + TEXT(",")
		+ MakeQAText(TEXT("qa_b"), TEXT("Beta"), 24, 60, TEXT("B"))) + TEXT(",")
		+ MakeQAText(TEXT("qa_keep"), TEXT("Keeper"), 240, 24, TEXT("Keep")));
	UWidgetBlueprint* WBP = QAImport(Json, nullptr, Output);
	if (!TestNotNull(TEXT("initial asset"), WBP)) return false;
	const FString Package = WBP->GetOutermost()->GetName();
	FDesign2UMGGenerationManifestSidecar Manifest;
	if (!TestTrue(TEXT("manifest loaded"), QALoadSidecar(Package, Manifest))) return false;
	UWidget* OriginalKeeper = WBP->WidgetTree->FindWidget(TEXT("Keeper"));
	UWidget* OriginalPanel = WBP->WidgetTree->FindWidget(TEXT("ExistingPanel"));
	const int32 ObjectCount = WBP->GetAllSourceWidgets().Num();
	FDesignDocument Document;
	FFigmaParser::ParseDocument(Json, Document);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TFunction<void(FUMGWidgetNode&)> Change = [&](FUMGWidgetNode& Node)
	{
		Node.Children.RemoveAll([](const FUMGWidgetNode& Child) { return GetDesignNodeId(Child) == TEXT("qa_keep"); });
		if (GetDesignNodeId(Node) == TEXT("qa_panel")) Node.Children.Add(MakeNode(TEXT("BrokenAddition"), TEXT("InvalidWidgetType")));
		for (FUMGWidgetNode& Child : Node.Children) Change(Child);
	};
	Change(Data.RootWidget);
	FUMGWidgetNode Added = MakeNode(TEXT("NewTop"), TEXT("CanvasPanel"));
	Added.Children.Add(MakeNode(TEXT("NewLeaf")));
	Data.RootWidget.Children.Add(Added);
	FDesignReimportRequest Request;
	Request.TargetAssetPath = Package;
	const auto Snapshot = MakeQASnapshot(nullptr);
	const FReimportSession Session{&Document, Request, Data, Snapshot.GenerationSettings};
	FDesign2UMGGenerationOutput GenerationOutput;
	FUMGPrototypeNavigationData Navigation;
	int32 Transaction = INDEX_NONE;
	const FDesignReimportResult Result = ApplyPreparedReimport(Session, WBP, Manifest, GenerationOutput, Navigation, Transaction);
	TestFalse(TEXT("late structural failure reaches final result"), Result.bSucceeded);
	TestTrue(TEXT("owned transaction actually rolled back"), QAHasMessage(Result, ReimportDiagnosticKinds::ApplyFailed, TEXT("transaction rolled back")));
	TestTrue(TEXT("previously deleted widget restored with its identity"), WBP->WidgetTree->FindWidget(TEXT("Keeper")) == OriginalKeeper);
	TestTrue(TEXT("surviving panel identity preserved"), WBP->WidgetTree->FindWidget(TEXT("ExistingPanel")) == OriginalPanel);
	TestNull(TEXT("committed addition undone"), WBP->WidgetTree->FindWidget(TEXT("NewTop")));
	TestNull(TEXT("undone addition has no detached ghost"), FindObject<UWidget>(WBP->WidgetTree, TEXT("NewTop")));
	TestNull(TEXT("undone nested addition has no detached ghost"), FindObject<UWidget>(WBP->WidgetTree, TEXT("NewLeaf")));
	TestEqual(TEXT("source objects restored to original count"), WBP->GetAllSourceWidgets().Num(), ObjectCount);
	TestEqual(TEXT("rolled-back addition not reported as success"), QACountKind(Result, ReimportDiagnosticKinds::Added), 0);
	TestEqual(TEXT("rolled-back removal not reported as success"), QACountKind(Result, ReimportDiagnosticKinds::Removed), 0);
	TestTrue(TEXT("no ghost manifest payload"), GenerationOutput.Manifest.D2UOwnedNames.IsEmpty());
	return true;
}

#endif
