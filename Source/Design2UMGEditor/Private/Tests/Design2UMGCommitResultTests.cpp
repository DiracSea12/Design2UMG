// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/Design2UMGFileFailureFixture.h"
#include "DesignImportPipeline.h"
#include "GeneratedAssetLifecycle.h"
#include "Tests/Design2UMGReimportTestFixtures.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/ScopeExit.h"

using namespace Design2UMGReimportFixtures;

namespace
{
	FString CommitDocument(const TCHAR* Text)
	{
		return MakeFixtureDoc(MakeFixturePanel(TEXT("panel"), TEXT("Panel"), 24, 24,
			MakeFixtureText(TEXT("head"), TEXT("Head"), 32, 32, Text)));
	}

	FUMGWidgetData CommitWidgetData()
	{
		FUMGWidgetData Data;
		Data.WidgetName = TEXT("WBP_CommitResult");
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Id = TEXT("root");
		Data.RootWidget.Name = TEXT("Root");
		Data.RootWidget.Bounds.W = 96;
		Data.RootWidget.Bounds.H = 64;
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommitImportMetadataFailureTest,
	"Design2UMG.CommitResult.ImportMetadataFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCommitImportMetadataFailureTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("GenerationManifest: failed to write temp manifest"), EAutomationExpectedErrorFlags::Contains, 3);
	AddExpectedError(TEXT("Widget Blueprint compile or save failed"), EAutomationExpectedErrorFlags::Contains, 3);
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/CommitMetadata");
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	FixtureWipeOutput(Folder);
	for (const auto Mode : {EDesignBlueprintGenerationMode::BlueprintOnly,
		EDesignBlueprintGenerationMode::BlueprintAndPng, EDesignBlueprintGenerationMode::BlueprintAndCompiledPng})
	{
		FUMGWidgetData Data = CommitWidgetData();
		FArtifactGenerationRequest Request;
		Request.OutputPath = Folder;
		Request.Generation.Mode = Mode;
		Request.Generation.CompiledPngFilePath = FPaths::ProjectSavedDir() / TEXT("Design2UMG/CommitMetadataTest.png");
		Design2UMGEditorTests::FScopedFileFailures FileFailures;
	FileFailures.RejectWrite = [](const FString& Path) { return Path.EndsWith(TEXT(".d2u.json.tmp")); };
		const FArtifactGenerationResult Result = DesignImportPipeline::GenerateArtifacts(Data, Request);
		TestFalse(TEXT("Partial commit is not artifact-generation success"), Result.bSucceeded);
		TestTrue(TEXT("Explicit metadata failure"), Result.Commit.Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure);
		TestTrue(TEXT("Asset committed"), Result.Commit.Asset == EDesign2UMGFileState::Saved);
		TestTrue(TEXT("Manifest not committed"), Result.Commit.Manifest == EDesign2UMGFileState::Unchanged);
		TestTrue(TEXT("Memory not undone"), Result.Commit.Memory == EDesign2UMGMemoryState::Modified);
		TestFalse(TEXT("Saved asset path retained"), Result.GeneratedAssetPath.IsEmpty());
		TestEqual(TEXT("Artifact and commit refer to the same saved asset"), Result.GeneratedAssetPath, Result.Commit.AssetPath);
		TestFalse(TEXT("Failure explains the partial commit"), Result.ErrorMessage.IsEmpty());
		const FString Package = FPackageName::ObjectPathToPackageName(Result.GeneratedAssetPath);
		TestTrue(TEXT("Asset actually exists on disk"), FPaths::FileExists(FixtureAssetFilePath(Package)));
		TestFalse(TEXT("No manifest was created"), FPaths::FileExists(Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package)));
		TestNotNull(TEXT("Committed asset remains loadable"), LoadObject<UWidgetBlueprint>(nullptr, *Result.GeneratedAssetPath));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommitReimportUndoFailureTest,
	"Design2UMG.CommitResult.ReimportUndoFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCommitReimportUndoFailureTest::RunTest(const FString&)
{
	Design2UMGEditorTests::FScopedTransactionBuffer Transactions;
	AddExpectedError(TEXT("SaveWidgetBlueprint: failed to save"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/CommitUndoFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	UWidgetBlueprint* Blueprint = FixtureImport(CommitDocument(TEXT("Before")), nullptr, Folder);
	if (!TestNotNull(TEXT("Initial asset saved"), Blueprint)) return false;
	const FString Package = Blueprint->GetOutermost()->GetName();
	const FString Hash = Design2UMGGenerationManifest::ComputeSavedAssetHash(Package);
	FString Before;
	if (!TestTrue(TEXT("Initial manifest readable"), FixtureReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package), Before))) return false;
	Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure(Design2UMGEditorTests::IsPackageSaveTargetFile);
	Transactions.Buffer->bRejectUndo = true;
	const FDesignReimportResult Result = FixtureReimport(CommitDocument(TEXT("After")), nullptr, Package);
	TestFalse(TEXT("Failed recovery is not success"), Result.bSucceeded);
	TestTrue(TEXT("Undo failure explicitly incomplete"), Result.Commit.Status == EDesign2UMGCommitStatus::RollbackIncomplete);
	TestTrue(TEXT("Memory recovery failed"), Result.Commit.Memory == EDesign2UMGMemoryState::RecoveryFailed);
	TestTrue(TEXT("Failed write reports uncertain disk state"), Result.Commit.Asset == EDesign2UMGFileState::Indeterminate);
	TestTrue(TEXT("Manifest unchanged on disk"), Result.Commit.Manifest == EDesign2UMGFileState::Unchanged);
	TestEqual(TEXT("Actual asset hash unchanged"), Design2UMGGenerationManifest::ComputeSavedAssetHash(Package), Hash);
	FString After;
	TestTrue(TEXT("Manifest still readable"), FixtureReadFileString(Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package), After));
	TestEqual(TEXT("Actual manifest unchanged"), After, Before);
	UTextBlock* Head = Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(TEXT("Head")));
	if (TestNotNull(TEXT("Modified widget retained"), Head))
		TestEqual(TEXT("Failed undo really leaves new memory"), Head->GetText().ToString(), FString(TEXT("After")));
	TestFalse(TEXT("No false rollback claim"), FixtureHasMessage(Result, TEXT("reimport_save_failed"), TEXT("transaction rolled back")));
	Transactions.Buffer->bRejectUndo = false;
	TestTrue(TEXT("Caller can still undo the failed operation"), GEditor->UndoTransaction(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommitOuterTransactionTest,
	"Design2UMG.CommitResult.OuterTransactionPreserved", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCommitOuterTransactionTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("SaveWidgetBlueprint: failed to save"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/CommitOuterTransaction");
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	UWidgetBlueprint* Blueprint = FixtureImport(CommitDocument(TEXT("Before")), nullptr, Folder);
	if (!TestNotNull(TEXT("Initial asset saved"), Blueprint)) return false;
	const FString Package = Blueprint->GetOutermost()->GetName();
	const FString Hash = Design2UMGGenerationManifest::ComputeSavedAssetHash(Package);
	GEditor->BeginTransaction(TEXT("Caller-owned test transaction"), FText::FromString(TEXT("Caller-owned test transaction")), Blueprint);
	const FGuid TransactionId = GUndo->GetContext().TransactionId;
	Design2UMGEditorTests::FScopedPackageSaveFailure SaveFailure(Design2UMGEditorTests::IsPackageSaveTargetFile);
	const FDesignReimportResult Result = FixtureReimport(CommitDocument(TEXT("After")), nullptr, Package);
	TestFalse(TEXT("Uncommitted update fails"), Result.bSucceeded);
	TestTrue(TEXT("No commit"), Result.Commit.Status == EDesign2UMGCommitStatus::RollbackIncomplete);
	TestTrue(TEXT("Caller retains memory changes"), Result.Commit.Memory == EDesign2UMGMemoryState::Modified);
	TestTrue(TEXT("Outer transaction stays active"), GEditor->IsTransactionActive());
	TestEqual(TEXT("Disk remains unchanged"), Design2UMGGenerationManifest::ComputeSavedAssetHash(Package), Hash);
	GEditor->EndTransaction();
	TestTrue(TEXT("Caller retains ownership of undo"), GeneratedAssetLifecycle::UndoOwnedTransaction(TransactionId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommitSuccessfulExecutionTest,
	"Design2UMG.CommitResult.SuccessAndDegradation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCommitSuccessfulExecutionTest::RunTest(const FString&)
{
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/CommitExecution");
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	FixtureWipeOutput(Folder);
	FUMGWidgetData Data = CommitWidgetData();
	FDesignImportRequest Request;
	Request.OutputPath = Folder;
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("Non-preview execution succeeds"), Result.bSucceeded);
	TestTrue(TEXT("Execution propagates commit success"), Result.Commit.IsSuccess());
	TestTrue(TEXT("Asset saved"), Result.Commit.Asset == EDesign2UMGFileState::Saved);
	TestTrue(TEXT("Manifest saved"), Result.Commit.Manifest == EDesign2UMGFileState::Saved);
	TestFalse(TEXT("Execution did not open a preview"), Result.bPreviewOpened);
	TestEqual(TEXT("Execution retains asset identity"), Result.GeneratedAssetPath, Result.Commit.AssetPath);
	Data.Warnings.Add(TEXT("Test input reports a degraded import"));
	FArtifactGenerationRequest Artifacts;
	Artifacts.OutputPath = Folder;
	const FArtifactGenerationResult Degraded = DesignImportPipeline::GenerateArtifacts(Data, Artifacts);
	TestTrue(TEXT("Degraded commit remains a success"), Degraded.bSucceeded);
	TestTrue(TEXT("Degradation is explicit"), Degraded.Commit.Status == EDesign2UMGCommitStatus::SucceededWithDegradation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommitUndoOwnershipTest,
	"Design2UMG.CommitResult.UndoOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCommitUndoOwnershipTest::RunTest(const FString&)
{
	UTextBlock* Probe = NewObject<UTextBlock>(GetTransientPackage(), NAME_None, RF_Transactional);
	Probe->AddToRoot();
	ON_SCOPE_EXIT { Probe->RemoveFromRoot(); };
	Probe->SetText(FText::FromString(TEXT("Before")));
	GEditor->BeginTransaction(TEXT("First test edit"), FText::FromString(TEXT("First test edit")), Probe);
	const FGuid FirstId = GUndo->GetContext().TransactionId;
	Probe->Modify();
	Probe->SetText(FText::FromString(TEXT("First")));
	GEditor->EndTransaction();
	GEditor->BeginTransaction(TEXT("Empty test transaction"), FText::FromString(TEXT("Empty test transaction")), Probe);
	const FGuid EmptyId = GUndo->GetContext().TransactionId;
	GEditor->EndTransaction();
	TestFalse(TEXT("Discarded empty transaction never undoes an earlier edit"), GeneratedAssetLifecycle::UndoOwnedTransaction(EmptyId));
	TestEqual(TEXT("Earlier edit retained"), Probe->GetText().ToString(), FString(TEXT("First")));
	GEditor->BeginTransaction(TEXT("Later user edit"), FText::FromString(TEXT("Later user edit")), Probe);
	const FGuid LaterId = GUndo->GetContext().TransactionId;
	Probe->Modify();
	Probe->SetText(FText::FromString(TEXT("Later")));
	GEditor->EndTransaction();
	TestFalse(TEXT("A later edit blocks stale automatic undo"), GeneratedAssetLifecycle::UndoOwnedTransaction(FirstId));
	TestEqual(TEXT("Later edit retained"), Probe->GetText().ToString(), FString(TEXT("Later")));
	TestTrue(TEXT("Later owner can undo"), GeneratedAssetLifecycle::UndoOwnedTransaction(LaterId));
	TestTrue(TEXT("Original owner can undo once current"), GeneratedAssetLifecycle::UndoOwnedTransaction(FirstId));
	TestEqual(TEXT("Both owned edits actually undone"), Probe->GetText().ToString(), FString(TEXT("Before")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommitResolutionCopyMetadataTest,
	"Design2UMG.CommitResult.ResolutionCopyMetadataFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCommitResolutionCopyMetadataTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("GenerationManifest: failed to write temp manifest"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/CommitResolutionCopy");
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	UWidgetBlueprint* Original = FixtureImport(CommitDocument(TEXT("Before")), nullptr, Folder);
	if (!TestNotNull(TEXT("Original imported"), Original)) return false;
	const FString Package = Original->GetOutermost()->GetName();
	const FString Hash = Design2UMGGenerationManifest::ComputeSavedAssetHash(Package);
	FDesignDocument Document;
	if (!TestTrue(TEXT("Conflicting source parsed"), FFigmaParser::ParseDocument(
		CommitDocument(TEXT("After")).Replace(FixtureReimportScope, FixtureReimportOtherScope), Document))) return false;
	FDesignReimportRequest Request;
	Request.TargetAssetPath = Package;
	Request.SettingsSnapshot = MakeFixtureSnapshot(nullptr);
	Request.bGenerateResolutionCopy = true;
	Design2UMGEditorTests::FScopedFileFailures FileFailures;
	FileFailures.RejectWrite = [](const FString& Path) { return Path.EndsWith(TEXT(".d2u.json.tmp")); };
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
	TestFalse(TEXT("Original not updated"), Result.bSucceeded);
	TestTrue(TEXT("Conflict explicitly reported"), Result.Commit.Status == EDesign2UMGCommitStatus::Conflict);
	TestTrue(TEXT("Original file unchanged"), Result.Commit.Asset == EDesign2UMGFileState::Unchanged);
	TestEqual(TEXT("Original actual bytes unchanged"), Design2UMGGenerationManifest::ComputeSavedAssetHash(Package), Hash);
	if (TestTrue(TEXT("Requested copy reports a distinct outcome"), Result.ResolutionCopyCommit.IsSet()))
	{
		TestTrue(TEXT("Copy metadata failure is a partial commit"), Result.ResolutionCopyCommit->Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure);
		TestTrue(TEXT("Copy asset saved"), Result.ResolutionCopyCommit->Asset == EDesign2UMGFileState::Saved);
		TestFalse(TEXT("A saved copy alone is not completed reimport success"), Result.ResolutionCopyCommit->IsSuccess());
		TestEqual(TEXT("Copy identity retained despite metadata failure"), Result.ResolutionCopyPath, Result.ResolutionCopyCommit->AssetPath);
		TestNotNull(TEXT("Saved copy is loadable"), LoadObject<UWidgetBlueprint>(nullptr, *Result.ResolutionCopyPath));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommitManifestFinalizeFailureTest,
	"Design2UMG.CommitResult.ManifestFinalizeFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCommitManifestFinalizeFailureTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("Error moving file"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("GenerationManifest: failed to finalize manifest"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/CommitFinalizeFailure");
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	UWidgetBlueprint* Blueprint = FixtureImport(CommitDocument(TEXT("Before")), nullptr, Folder);
	if (!TestNotNull(TEXT("Initial asset saved"), Blueprint)) return false;
	const FString Package = Blueprint->GetOutermost()->GetName();
	const FString Sidecar = Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package);
	const FString BeforeHash = Design2UMGGenerationManifest::ComputeSavedAssetHash(Package);
	if (!TestTrue(TEXT("Original manifest exists"), FPaths::FileExists(Sidecar))) return false;

	Design2UMGEditorTests::FScopedFileFailures FinalizeFailure;
	FinalizeFailure.RejectMove = [](const FString& Path, const FString&) { return Path.EndsWith(TEXT(".d2u.json")); };
	const FDesignReimportResult Result = FixtureReimport(CommitDocument(TEXT("After")), nullptr, Package);
	TestFalse(TEXT("Partial commit is not success"), Result.bSucceeded);
	TestTrue(TEXT("Saved asset with failed metadata"), Result.Commit.Status == EDesign2UMGCommitStatus::CommittedWithMetadataFailure);
	TestTrue(TEXT("Asset saved"), Result.Commit.Asset == EDesign2UMGFileState::Saved);
	TestTrue(TEXT("Failed replacement cannot promise an unchanged manifest"), Result.Commit.Manifest == EDesign2UMGFileState::Indeterminate);
	TestTrue(TEXT("Committed memory is not undone"), Result.Commit.Memory == EDesign2UMGMemoryState::Modified);
	TestTrue(TEXT("Saved blueprint retained in result"), Result.UpdatedBlueprint == Blueprint);
	TestTrue(TEXT("Asset bytes actually changed"), Design2UMGGenerationManifest::ComputeSavedAssetHash(Package) != BeforeHash);
	TestFalse(TEXT("Real failed move removed the old manifest"), FPaths::FileExists(Sidecar));
	TestFalse(TEXT("No staged file remains"), FPaths::FileExists(Sidecar + TEXT(".tmp")));
	UTextBlock* Head = Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(TEXT("Head")));
	if (TestNotNull(TEXT("Updated widget retained"), Head))
		TestEqual(TEXT("Memory agrees with the committed asset"), Head->GetText().ToString(), FString(TEXT("After")));
	TestFalse(TEXT("No false rollback claim"), FixtureHasMessage(Result, TEXT("reimport_save_failed"), TEXT("transaction rolled back")));
	return true;
}

#endif
